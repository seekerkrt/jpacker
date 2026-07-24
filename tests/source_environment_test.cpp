#include "source_environment.hpp"
#include "source_preference.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_warnings;

void record_warning(const std::string& warning) {
    g_warnings.push_back(warning);
}

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_equal(
        const std::string& test_case,
        const std::string& actual,
        const std::string& expected) {
    if(actual == expected) return;
    throw std::runtime_error(
            test_case + ": expected [" + expected + "], actual [" + actual + "]");
}

void expect_assignment(
        const SourceBuildEnvironment& environment,
        size_t index,
        const std::string& expected_key,
        const std::string& expected_value) {
    expect(
            index < environment.ordered_assignments.size(),
            "Missing source environment assignment at index " + std::to_string(index));
    const SourceEnvironmentAssignment& assignment =
            environment.ordered_assignments[index];
    expect_equal("assignment key", assignment.key, expected_key);
    expect_equal("assignment value for " + expected_key, assignment.value, expected_value);
}

void write_preference(
        const std::string& package_name,
        const std::string& contents) {
    std::ofstream file(source_preference_entry_path(package_name));
    if(!file) {
        throw std::runtime_error(
                "Failed to create source preference fixture for " + package_name);
    }
    file << contents;
    if(!file) {
        throw std::runtime_error(
                "Failed to write source preference fixture for " + package_name);
    }
}

SourceBuildEnvironment load_preference(const std::string& package_name) {
    g_warnings.clear();
    return get_package_env(package_name, nullptr, record_warning);
}

void test_absent_environment() {
    fs::remove(source_preference_entry_path("absent-target"));
    SourceBuildEnvironment environment = load_preference("absent-target");

    expect(environment.ordered_assignments.empty(), "Absent environment is not empty");
    expect(!environment.defines("PKGDEST"), "Absent environment defines PKGDEST");
    expect(
            !environment.has_forwarded_nonempty_assignment(),
            "Absent environment has a forwardable assignment");
    expect_equal(
            "absent preference serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Omit),
            "");
    expect_equal(
            "absent CLI serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Forward),
            "");
}

void test_preference_parsing_and_serialization() {
    write_preference(
            "structured-target",
            "FIRST = \"alpha value\" # stripped comment\n"
            "QUOTED = 'quoted # value' # stripped after quoted hash\n"
            "EMPTY=\n"
            "BRACED=${FIRST}/brace\n"
            "UNDEFINED=$MISSING\n"
            "DUP=first\n"
            "DUP=second\n"
            "SIMPLE=$DUP/simple\n"
            "9INVALID=value\n"
            "ignored without equals\n");

    SourceBuildEnvironment environment = load_preference("structured-target");
    expect(
            environment.ordered_assignments.size() == 8,
            "Unexpected structured assignment count");
    expect_assignment(environment, 0, "FIRST", "alpha value");
    expect_assignment(environment, 1, "QUOTED", "quoted # value");
    expect_assignment(environment, 2, "EMPTY", "");
    expect_assignment(environment, 3, "BRACED", "alpha value/brace");
    expect_assignment(environment, 4, "UNDEFINED", "");
    expect_assignment(environment, 5, "DUP", "first");
    expect_assignment(environment, 6, "DUP", "second");
    expect_assignment(environment, 7, "SIMPLE", "second/simple");
    expect(environment.defines("EMPTY"), "Empty assignment definition was lost");
    expect(environment.defines("UNDEFINED"), "Expanded-empty definition was lost");
    expect(
            environment.has_forwarded_nonempty_assignment(),
            "Nonempty preference assignment was not detected");
    expect(
            g_warnings.size() == 1,
            "Unexpected source preference warning count");
    expect_equal(
            "invalid preference warning",
            g_warnings.front(),
            "Ignoring invalid environment assignment: 9INVALID=value");

    expect_equal(
            "preference compatibility serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Omit),
            "FIRST='alpha value' QUOTED='quoted # value' BRACED='alpha value/brace' "
            "DUP='first' DUP='second' SIMPLE='second/simple' ");
    expect_equal(
            "CLI compatibility serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Forward),
            "FIRST='alpha value' QUOTED='quoted # value' EMPTY='' "
            "BRACED='alpha value/brace' UNDEFINED='' DUP='first' DUP='second' "
            "SIMPLE='second/simple' ");
}

void test_empty_duplicate_pkgdest_definitions() {
    write_preference(
            "requested-target",
            "PKGDEST=\n"
            "PKGDEST=\"\"\n"
            "PKGDEST=$UNDEFINED_PKGDEST\n");

    SourceBuildEnvironment environment = load_preference("requested-target");
    expect(
            environment.ordered_assignments.size() == 3,
            "Unexpected PKGDEST assignment count");
    expect_assignment(environment, 0, "PKGDEST", "");
    expect_assignment(environment, 1, "PKGDEST", "");
    expect_assignment(environment, 2, "PKGDEST", "");
    expect(environment.defines("PKGDEST"), "Empty PKGDEST definition was lost");
    expect(
            !environment.has_forwarded_nonempty_assignment(),
            "Empty-only PKGDEST changed fallback eligibility");
    expect_equal(
            "preference empty PKGDEST serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Omit),
            "");
    expect_equal(
            "CLI empty PKGDEST serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Forward),
            "PKGDEST='' PKGDEST='' PKGDEST='' ");
}

void test_mixed_duplicate_pkgdest_uses_last_value_for_expansion() {
    write_preference(
            "mixed-pkgdest-target",
            "PKGDEST=first-path\n"
            "PKGDEST=\n"
            "AFTER=$PKGDEST\n");

    SourceBuildEnvironment environment = load_preference("mixed-pkgdest-target");
    expect(
            environment.ordered_assignments.size() == 3,
            "Unexpected mixed PKGDEST assignment count");
    expect_assignment(environment, 0, "PKGDEST", "first-path");
    expect_assignment(environment, 1, "PKGDEST", "");
    expect_assignment(environment, 2, "AFTER", "");
    expect(environment.defines("PKGDEST"), "Mixed PKGDEST definition was lost");
    expect(
            environment.has_forwarded_nonempty_assignment(),
            "Mixed PKGDEST lost its forwardable nonempty assignment");
    expect_equal(
            "preference mixed PKGDEST serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Omit),
            "PKGDEST='first-path' ");
    expect_equal(
            "CLI mixed PKGDEST serialization",
            serialize_source_build_environment(
                    environment, SourceEnvironmentEmptyValuePolicy::Forward),
            "PKGDEST='first-path' PKGDEST='' AFTER='' ");
}

void test_invalid_only_preference() {
    write_preference(
            "invalid-only-target",
            "9INVALID=value\n"
            "ignored without equals\n");

    SourceBuildEnvironment environment = load_preference("invalid-only-target");
    expect(
            environment.ordered_assignments.empty(),
            "Invalid preference assignment entered structured environment");
    expect(
            !environment.has_forwarded_nonempty_assignment(),
            "Invalid-only preference changed fallback eligibility");
    expect(
            g_warnings.size() == 1,
            "Unexpected invalid-only warning count");
    expect_equal(
            "invalid-only warning",
            g_warnings.front(),
            "Ignoring invalid environment assignment: 9INVALID=value");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if(argc != 2) {
            throw std::runtime_error(
                    "Usage: source-environment-test <fixture-root>");
        }

        const fs::path expected_root = fs::path(argv[1]).lexically_normal();
        expect(
                source_preference_root().lexically_normal() == expected_root,
                "Source preference test override was not captured before main");
        fs::create_directories(expected_root);

        test_absent_environment();
        test_preference_parsing_and_serialization();
        test_empty_duplicate_pkgdest_definitions();
        test_mixed_duplicate_pkgdest_uses_last_value_for_expansion();
        test_invalid_only_preference();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "source environment tests: all checks passed\n";
    return 0;
}
