#include "source_environment.hpp"
#include "source_preference.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include <sys/stat.h>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_warnings;
std::vector<fs::path> g_loaded_paths;
std::vector<std::string> g_callback_events;

void record_warning(const std::string& warning) {
    g_warnings.push_back(warning);
}

void record_load_event(const fs::path& entry_path) {
    g_loaded_paths.push_back(entry_path);
    g_callback_events.push_back("load");
}

void record_warning_event(const std::string& warning) {
    g_warnings.push_back(warning);
    g_callback_events.push_back("warning");
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

template<typename Alternative>
Alternative expect_strict_alternative(
        const StrictSourcePreferenceResult& result,
        const std::string& test_case) {
    const Alternative* alternative = std::get_if<Alternative>(&result);
    expect(alternative != nullptr, test_case + ": unexpected strict result alternative");
    return *alternative;
}

SourcePreferenceFailure expect_strict_failure(
        const StrictSourcePreferenceResult& result,
        SourcePreferenceFailureKind expected_kind,
        const std::string& test_case) {
    SourcePreferenceFailure failure =
            expect_strict_alternative<SourcePreferenceFailure>(result, test_case);
    expect(failure.kind == expected_kind, test_case + ": unexpected failure kind");
    expect(!failure.diagnostic.empty(), test_case + ": missing failure diagnostic");
    return failure;
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

void remove_preference_entry(const std::string& package_name) {
    const fs::path entry_path = source_preference_entry_path(package_name);
    std::error_code remove_error;
    fs::remove_all(entry_path, remove_error);
    if(remove_error) {
        throw std::runtime_error(
                "Failed to remove source preference fixture for " + package_name +
                ": " + remove_error.message());
    }
}

void write_preference(
        const std::string& package_name,
        const std::string& contents) {
    remove_preference_entry(package_name);
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

std::string read_preference_contents(const std::string& package_name) {
    std::ifstream file(
            source_preference_entry_path(package_name),
            std::ios::binary);
    if(!file) {
        throw std::runtime_error(
                "Failed to read source preference fixture for " + package_name);
    }
    return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
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

void test_legacy_callback_timing_and_absent_behavior() {
    const std::string absent_package = "legacy-callback-absent";
    remove_preference_entry(absent_package);
    g_loaded_paths.clear();
    g_warnings.clear();
    g_callback_events.clear();

    SourceBuildEnvironment absent_environment = get_package_env(
            absent_package, record_load_event, record_warning_event);
    expect(
            absent_environment.ordered_assignments.empty(),
            "Legacy absent callback environment is not empty");
    expect(g_loaded_paths.empty(), "Legacy absent preference called on_load");
    expect(g_warnings.empty(), "Legacy absent preference emitted a warning");
    expect(g_callback_events.empty(), "Legacy absent preference emitted a callback event");

    const std::string package_name = "legacy-callback-target";
    write_preference(
            package_name,
            "GOOD=before\n"
            "9INVALID=value\n"
            "AFTER=still-parsed\n");
    g_loaded_paths.clear();
    g_warnings.clear();
    g_callback_events.clear();

    SourceBuildEnvironment environment = get_package_env(
            package_name, record_load_event, record_warning_event);
    expect(
            environment.ordered_assignments.size() == 2,
            "Legacy invalid assignment stopped parsing");
    expect_assignment(environment, 0, "GOOD", "before");
    expect_assignment(environment, 1, "AFTER", "still-parsed");
    expect(g_loaded_paths.size() == 1, "Legacy preference on_load count changed");
    expect(
            g_loaded_paths.front() == source_preference_entry_path(package_name),
            "Legacy preference on_load path changed");
    expect(g_warnings.size() == 1, "Legacy warning callback count changed");
    expect(
            g_callback_events == std::vector<std::string>{"load", "warning"},
            "Legacy on_load no longer precedes parser warnings");
}

void test_strict_absent_and_empty_file_are_distinct() {
    const std::string absent_package = "strict-absent-target";
    remove_preference_entry(absent_package);
    StrictSourcePreferenceResult absent =
            read_source_preference_strict(absent_package);
    static_cast<void>(expect_strict_alternative<SourcePreferenceAbsent>(
            absent, "strict absent preference"));

    const std::string empty_package = "strict-empty-target";
    write_preference(empty_package, "");
    StrictSourcePreferenceResult empty =
            read_source_preference_strict(empty_package);
    const SourcePreferenceLoaded loaded =
            expect_strict_alternative<SourcePreferenceLoaded>(
                    empty, "strict empty preference");
    expect(
            loaded.entry_path == source_preference_entry_path(empty_package),
            "Strict empty preference returned an unexpected path");
    expect(
            loaded.environment.ordered_assignments.empty(),
            "Strict empty preference returned assignments");
    expect(loaded.warnings.empty(), "Strict empty preference returned warnings");
    expect_equal(
            "strict empty preference contents",
            read_preference_contents(empty_package), "");
}

void test_strict_parser_compatibility_and_file_preservation() {
    const std::string package_name = "strict-structured-target";
    const std::string contents =
            "9FIRST_INVALID=value\n"
            "FIRST = \"alpha value\" # stripped comment\n"
            "QUOTED = 'quoted # value' # stripped after quoted hash\n"
            "EMPTY=\n"
            "BRACED=${FIRST}/brace\n"
            "UNDEFINED=$MISSING\n"
            "DUP=first\n"
            "DUP=second\n"
            "SIMPLE=$DUP/simple\n"
            "=second-invalid\n"
            "ignored without equals\n";
    write_preference(package_name, contents);

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceLoaded loaded =
            expect_strict_alternative<SourcePreferenceLoaded>(
                    result, "strict structured preference");
    const SourceBuildEnvironment& environment = loaded.environment;
    expect(
            environment.ordered_assignments.size() == 8,
            "Unexpected strict structured assignment count");
    expect_assignment(environment, 0, "FIRST", "alpha value");
    expect_assignment(environment, 1, "QUOTED", "quoted # value");
    expect_assignment(environment, 2, "EMPTY", "");
    expect_assignment(environment, 3, "BRACED", "alpha value/brace");
    expect_assignment(environment, 4, "UNDEFINED", "");
    expect_assignment(environment, 5, "DUP", "first");
    expect_assignment(environment, 6, "DUP", "second");
    expect_assignment(environment, 7, "SIMPLE", "second/simple");
    expect(
            environment.defines("EMPTY"),
            "Strict parser lost an explicit empty assignment");
    expect(
            loaded.warnings == std::vector<std::string>{
                    "Ignoring invalid environment assignment: 9FIRST_INVALID=value",
                    "Ignoring invalid environment assignment: =second-invalid"},
            "Strict parser warning order changed");
    expect_equal(
            "strict source preference remained unchanged",
            read_preference_contents(package_name), contents);
}

void test_strict_malformed_only_file_is_loaded_with_warnings() {
    const std::string package_name = "strict-malformed-only-target";
    write_preference(
            package_name,
            "9INVALID=value\n"
            "ignored without equals\n");

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceLoaded loaded =
            expect_strict_alternative<SourcePreferenceLoaded>(
                    result, "strict malformed-only preference");
    expect(
            loaded.environment.ordered_assignments.empty(),
            "Strict malformed-only preference returned an assignment");
    expect(
            loaded.warnings == std::vector<std::string>{
                    "Ignoring invalid environment assignment: 9INVALID=value"},
            "Strict malformed-only warning changed");
}

void test_strict_status_failure_is_not_absent() {
    const std::string package_name = "strict-status-failure-target";
    write_preference(package_name, "VALUE=available\n");
    reset_source_preference_test_hooks();
    fail_next_source_preference_operation_for_test(
            package_name, SourcePreferenceTestFailurePoint::Status);

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::StatusUnavailable,
            "strict status failure");
    expect(
            failure.entry_path == source_preference_entry_path(package_name),
            "Strict status failure returned an unexpected path");
    expect(
            failure.system_error == std::make_error_code(std::errc::permission_denied),
            "Strict status failure lost its system error");
    expect(
            !failure.observed_file_type.has_value(),
            "Strict status failure invented an observed file type");

    StrictSourcePreferenceResult retry =
            read_source_preference_strict(package_name);
    static_cast<void>(expect_strict_alternative<SourcePreferenceLoaded>(
            retry, "strict status failure one-shot retry"));
}

void test_strict_symlink_is_rejected_and_legacy_follows_it() {
    const std::string target_package = "strict-symlink-source";
    const std::string link_package = "strict-symlink-target";
    const std::string target_contents = "FROM_TARGET=followed\n";
    write_preference(target_package, target_contents);
    remove_preference_entry(link_package);
    fs::create_symlink(
            fs::path(target_package),
            source_preference_entry_path(link_package));

    StrictSourcePreferenceResult result =
            read_source_preference_strict(link_package);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::UnsupportedFileType,
            "strict symlink preference");
    expect(
            failure.observed_file_type == fs::file_type::symlink,
            "Strict symlink preference lost its observed file type");

    SourceBuildEnvironment legacy = load_preference(link_package);
    expect(
            legacy.ordered_assignments.size() == 1,
            "Legacy source preference stopped following symlinks");
    expect_assignment(legacy, 0, "FROM_TARGET", "followed");
    expect_equal(
            "strict symlink target remained unread and unchanged",
            read_preference_contents(target_package), target_contents);
}

void test_strict_dangling_symlink_is_not_absent() {
    const std::string missing_package = "strict-dangling-missing";
    const std::string link_package = "strict-dangling-target";
    remove_preference_entry(missing_package);
    remove_preference_entry(link_package);
    fs::create_symlink(
            fs::path(missing_package),
            source_preference_entry_path(link_package));

    StrictSourcePreferenceResult result =
            read_source_preference_strict(link_package);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::UnsupportedFileType,
            "strict dangling symlink preference");
    expect(
            failure.observed_file_type == fs::file_type::symlink,
            "Strict dangling symlink lost its observed file type");
}

void test_strict_directory_entry_is_rejected() {
    const std::string package_name = "strict-directory-target";
    const fs::path entry_path = source_preference_entry_path(package_name);
    remove_preference_entry(package_name);
    fs::create_directory(entry_path);

    g_loaded_paths.clear();
    SourceBuildEnvironment legacy = get_package_env(
            package_name, record_load_event, record_warning);
    expect(
            legacy.ordered_assignments.empty(),
            "Legacy directory preference stopped being failure-tolerant");
    expect(
            g_loaded_paths == std::vector<fs::path>{entry_path},
            "Legacy directory preference changed on_load timing");

    StrictSourcePreferenceResult result = read_source_preference_strict(package_name);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::UnsupportedFileType,
            "strict directory preference");
    expect(
            failure.observed_file_type == fs::file_type::directory,
            "Strict directory preference lost its observed file type");
}

void test_strict_fifo_entry_is_rejected_without_opening_it() {
    const std::string package_name = "strict-fifo-target";
    const fs::path entry_path = source_preference_entry_path(package_name);
    remove_preference_entry(package_name);
    if(::mkfifo(entry_path.c_str(), 0600) != 0) {
        const int fixture_errno = errno;
        throw std::runtime_error(
                "Failed to create strict FIFO fixture: " +
                std::string(std::strerror(fixture_errno)));
    }

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::UnsupportedFileType,
            "strict FIFO preference");
    expect(
            failure.observed_file_type == fs::file_type::fifo,
            "Strict FIFO preference lost its observed file type");
}

void test_strict_open_failure_is_typed_and_one_shot() {
    const std::string package_name = "strict-open-failure-target";
    const std::string contents = "VALUE=available\n";
    write_preference(package_name, contents);
    reset_source_preference_test_hooks();
    fail_next_source_preference_operation_for_test(
            package_name, SourcePreferenceTestFailurePoint::Open);

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::OpenFailed,
            "strict open failure");
    expect(
            failure.system_error == std::make_error_code(std::errc::permission_denied),
            "Strict open failure lost its system error");
    expect_equal(
            "strict open failure left the file unchanged",
            read_preference_contents(package_name), contents);

    StrictSourcePreferenceResult retry =
            read_source_preference_strict(package_name);
    const SourcePreferenceLoaded loaded =
            expect_strict_alternative<SourcePreferenceLoaded>(
                    retry, "strict open failure one-shot retry");
    expect_assignment(loaded.environment, 0, "VALUE", "available");
}

void test_strict_read_failure_does_not_publish_partial_environment() {
    const std::string package_name = "strict-read-failure-target";
    const std::string contents =
            "FIRST=partial-byte-source\n"
            "SECOND=must-not-publish\n";
    write_preference(package_name, contents);
    reset_source_preference_test_hooks();
    fail_next_source_preference_operation_for_test(
            package_name, SourcePreferenceTestFailurePoint::Read);

    StrictSourcePreferenceResult result =
            read_source_preference_strict(package_name);
    const SourcePreferenceFailure failure = expect_strict_failure(
            result, SourcePreferenceFailureKind::ReadFailed,
            "strict partial read failure");
    expect(
            failure.system_error == std::make_error_code(std::errc::io_error),
            "Strict read failure lost its system error");
    expect(
            std::get_if<SourcePreferenceLoaded>(&result) == nullptr,
            "Strict read failure published a partial environment");
    expect_equal(
            "strict read failure left the file unchanged",
            read_preference_contents(package_name), contents);

    StrictSourcePreferenceResult retry =
            read_source_preference_strict(package_name);
    const SourcePreferenceLoaded loaded =
            expect_strict_alternative<SourcePreferenceLoaded>(
                    retry, "strict read failure one-shot retry");
    expect(
            loaded.environment.ordered_assignments.size() == 2,
            "Strict read failure retry did not load the full file");
    expect_assignment(loaded.environment, 0, "FIRST", "partial-byte-source");
    expect_assignment(loaded.environment, 1, "SECOND", "must-not-publish");
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
        test_legacy_callback_timing_and_absent_behavior();
        test_strict_absent_and_empty_file_are_distinct();
        test_strict_parser_compatibility_and_file_preservation();
        test_strict_malformed_only_file_is_loaded_with_warnings();
        test_strict_status_failure_is_not_absent();
        test_strict_symlink_is_rejected_and_legacy_follows_it();
        test_strict_dangling_symlink_is_not_absent();
        test_strict_directory_entry_is_rejected();
        test_strict_fifo_entry_is_rejected_without_opening_it();
        test_strict_open_failure_is_typed_and_one_shot();
        test_strict_read_failure_does_not_publish_partial_environment();
        reset_source_preference_test_hooks();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "source environment tests: all checks passed\n";
    return 0;
}
