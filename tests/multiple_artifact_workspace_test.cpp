#include "artifact_workspace.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<ExpectedPackageArtifactSet>);
static_assert(!std::is_constructible_v<
              ExpectedPackageArtifactSet, std::filesystem::path>);
static_assert(!std::is_constructible_v<
              ExpectedPackageArtifactSet,
              std::vector<std::filesystem::path>>);

static_assert(!std::is_default_constructible_v<ValidatedPackageArtifactSet>);
static_assert(!std::is_copy_constructible_v<ValidatedPackageArtifactSet>);
static_assert(!std::is_copy_assignable_v<ValidatedPackageArtifactSet>);
static_assert(std::is_move_constructible_v<ValidatedPackageArtifactSet>);
static_assert(
        std::is_nothrow_move_constructible_v<ValidatedPackageArtifactSet>);
static_assert(!std::is_move_assignable_v<ValidatedPackageArtifactSet>);
static_assert(std::is_nothrow_destructible_v<ValidatedPackageArtifactSet>);
static_assert(!std::is_constructible_v<
              ValidatedPackageArtifactSet, std::filesystem::path>);
static_assert(!std::is_constructible_v<ValidatedPackageArtifactSet, int>);

namespace {

namespace fs = std::filesystem;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_equal(
        const std::string& context, const std::string& actual,
        const std::string& expected) {
    if(actual == expected) return;
    throw std::runtime_error(
            context + ": expected [" + expected + "], actual [" + actual + "]");
}

template <typename Callable>
std::string expect_runtime_error(
        Callable&& callable, const std::string& context,
        const std::string& expected_fragment = "") {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::runtime_error& error) {
        if(!expected_fragment.empty() &&
           std::string(error.what()).find(expected_fragment) ==
                   std::string::npos) {
            throw std::runtime_error(
                    context + ": unexpected error [" + error.what() + "]");
        }
        return error.what();
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected runtime_error");
}

void write_file(const fs::path& path, const std::string& contents = "fixture") {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file) {
        throw std::runtime_error(
                "Failed to create test fixture file: " + path.string());
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if(!file) {
        throw std::runtime_error(
                "Failed to finish test fixture file: " + path.string());
    }
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        throw std::runtime_error(
                "Failed to read test fixture file: " + path.string());
    }
    return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
}

void create_fifo(const fs::path& path) {
    if(mkfifo(path.c_str(), 0600) != 0) {
        throw std::runtime_error(
                "Failed to create test fixture FIFO: " + path.string());
    }
}

class ScopedEnvironmentVariable final {
    std::string                key_;
    std::optional<std::string> previous_value_;

public:
    ScopedEnvironmentVariable(
            std::string key, const std::optional<std::string>& value)
        : key_(std::move(key)) {
        const char* previous = std::getenv(key_.c_str());
        if(previous != nullptr) previous_value_ = previous;

        const int result = value.has_value()
                                   ? setenv(key_.c_str(), value->c_str(), 1)
                                   : unsetenv(key_.c_str());
        if(result != 0) {
            throw std::runtime_error(
                    "Failed to set test environment variable: " + key_);
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(
            const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_value_.has_value()) {
            static_cast<void>(setenv(
                    key_.c_str(), previous_value_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv(key_.c_str()));
        }
    }
};

class TestEnvironment final {
    fs::path path_;
    fs::path packagelist_output_file_;
    fs::path command_log_;
    fs::path argv_log_;
    fs::path cwd_log_;
    std::vector<std::unique_ptr<ScopedEnvironmentVariable>> variables_;

    void set_variable(
            const std::string& key,
            const std::optional<std::string>& value) {
        variables_.push_back(
                std::make_unique<ScopedEnvironmentVariable>(key, value));
    }

public:
    explicit TestEnvironment(const fs::path& makepkg_stub_directory) {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "jpacker-multiple-artifact-workspace-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create multiple artifact workspace test "
                    "directory.");
        }
        path_ = created_path;

        try {
            const fs::path absolute_stub_directory =
                    fs::absolute(makepkg_stub_directory).lexically_normal();
            expect(
                    fs::is_regular_file(absolute_stub_directory / "makepkg"),
                    "makepkg test stub is missing");

            packagelist_output_file_ = path_ / "packagelist-output";
            command_log_ = path_ / "command-log";
            argv_log_ = path_ / "argv-log";
            cwd_log_ = path_ / "cwd-log";
            write_file(packagelist_output_file_, "");
            write_file(command_log_, "");
            write_file(argv_log_, "");
            write_file(cwd_log_, "");

            std::string command_path = absolute_stub_directory.string();
            const char* previous_path = std::getenv("PATH");
            if(previous_path != nullptr &&
               std::string(previous_path).length() > 0) {
                command_path += ":" + std::string(previous_path);
            }

            set_variable("XDG_CACHE_HOME", (path_ / "cache-home").string());
            set_variable("PATH", command_path);
            set_variable("PKGDEST", std::nullopt);
            set_variable("JPACKER_TEST_COMMAND_LOG", command_log_.string());
            set_variable("JPACKER_TEST_MAKEPKG_ARGV_LOG", argv_log_.string());
            set_variable("JPACKER_TEST_MAKEPKG_CWD_LOG", cwd_log_.string());
            set_variable(
                    "JPACKER_TEST_MAKEPKG_PACKAGELIST_OUTPUT_FILE",
                    packagelist_output_file_.string());
            set_variable(
                    "JPACKER_TEST_MAKEPKG_PACKAGELIST_EXIT_CODE",
                    std::string("0"));
            set_variable("JPACKER_TEST_MAKEPKG_EXIT_CODE", std::string("0"));
        } catch(...) {
            variables_.clear();
            std::error_code error;
            fs::remove_all(path_, error);
            throw;
        }
    }

    TestEnvironment(const TestEnvironment&) = delete;
    TestEnvironment& operator=(const TestEnvironment&) = delete;

    ~TestEnvironment() noexcept {
        variables_.clear();
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

    const fs::path& packagelist_output_file() const noexcept {
        return packagelist_output_file_;
    }

    const fs::path& command_log() const noexcept {
        return command_log_;
    }

    const fs::path& argv_log() const noexcept {
        return argv_log_;
    }

    const fs::path& cwd_log() const noexcept {
        return cwd_log_;
    }

    void clear_makepkg_logs() const {
        write_file(command_log_, "");
        write_file(argv_log_, "");
        write_file(cwd_log_, "");
    }
};

ValidatedCachePath prepare_checkout(const ValidatedCacheRoot& root) {
    const fs::path checkout_path = root.path() / "multiple-source-checkout";
    fs::create_directory(checkout_path);
    return require_trusted_cache_path(
            root, checkout_path, CachePathRequirement::ExistingDirectory);
}

ArtifactWorkspace create_test_artifact_workspace(
        const ValidatedCacheRoot& expected_root) {
    ValidatedPrivateCacheRoot root = prepare_private_trusted_cache_root();
    expect(
            root.canonical_path() == expected_root.canonical_path(),
            "Private and legacy cache root paths differ");
    return create_artifact_workspace(std::move(root));
}

std::uintmax_t different_user_id() {
    const std::uintmax_t current_user =
            static_cast<std::uintmax_t>(geteuid());
    return current_user == 0 ? 1 : 0;
}

std::string make_packagelist_output(
        const ArtifactWorkspace& workspace,
        std::initializer_list<std::string> leaf_names) {
    std::string output;
    for(const std::string& leaf_name : leaf_names) {
        output += (workspace.path() / leaf_name).string() + "\n";
    }
    return output;
}

ExpectedPackageArtifactSet declare_expected_artifacts(
        const ArtifactWorkspace& workspace,
        std::initializer_list<std::string> leaf_names) {
    return validate_makepkg_packagelist_output_set(
            workspace, make_packagelist_output(workspace, leaf_names));
}

fs::path signature_path(const fs::path& artifact_path) {
    return fs::path(artifact_path.string() + ".sig");
}

void write_all_expected_artifacts(
        const ExpectedPackageArtifactSet& expected) {
    for(std::size_t index = 0; index < expected.size(); ++index) {
        write_file(expected.path_at(index), "artifact-" + std::to_string(index));
    }
}

void expect_workspace_remains_caller_owned(
        const ArtifactWorkspace& workspace, const std::string& context) {
    workspace.require_unchanged_identity();
    expect(
            fs::is_directory(workspace.path()),
            context + ": caller workspace disappeared");
}

fs::path g_replacement_target;
bool     g_replacement_observer_called = false;

fs::path g_replaced_workspace_original;
bool     g_workspace_replacement_observer_called = false;

void replace_artifact_during_validation(const fs::path& workspace_path) {
    if(workspace_path != g_replacement_target.parent_path() ||
       g_replacement_observer_called) {
        return;
    }

    g_replacement_observer_called = true;
    expect(
            fs::remove(g_replacement_target),
            "Validation replacement target was not removed");
    write_file(g_replacement_target, "validation replacement");
}

void replace_workspace_during_validation(const fs::path& workspace_path) {
    if(g_workspace_replacement_observer_called) return;

    g_workspace_replacement_observer_called = true;
    g_replaced_workspace_original = workspace_path;
    g_replaced_workspace_original += ".validation-original";
    fs::rename(workspace_path, g_replaced_workspace_original);
    fs::create_directory(workspace_path);
    fs::permissions(
            workspace_path, fs::perms::owner_all,
            fs::perm_options::replace);
}

class ScopedMultipleArtifactValidationObserver final {
public:
    explicit ScopedMultipleArtifactValidationObserver(
            MultipleArtifactValidationObserverForTest observer) {
        set_multiple_artifact_validation_observer_for_test(observer);
    }

    ScopedMultipleArtifactValidationObserver(
            const ScopedMultipleArtifactValidationObserver&) = delete;
    ScopedMultipleArtifactValidationObserver& operator=(
            const ScopedMultipleArtifactValidationObserver&) = delete;

    ~ScopedMultipleArtifactValidationObserver() noexcept {
        set_multiple_artifact_validation_observer_for_test(nullptr);
    }
};

void test_packagelist_single_multiple_and_order(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace, {"ordinary.pkg.tar.zst"});
        expect(expected.size() == 1, "Single expected set size differs");
        expect(
                expected.path_at(0) ==
                        workspace.path() / "ordinary.pkg.tar.zst",
                "Single expected set path differs");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"third.pkg.tar.zst", "first.pkg.tar.zst",
                 "second.pkg.tar.zst"});
        expect(expected.size() == 3, "Multiple expected set size differs");
        expect(
                expected.path_at(0).filename() == "third.pkg.tar.zst" &&
                        expected.path_at(1).filename() ==
                                "first.pkg.tar.zst" &&
                        expected.path_at(2).filename() ==
                                "second.pkg.tar.zst",
                "Expected set did not preserve packagelist order");
    }
}

void test_packagelist_cardinality_and_path_rejections(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, "\n  \n"));
                },
                "zero expected artifacts");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string duplicate =
                (workspace.path() / "duplicate.pkg.tar.zst").string();
        expect_runtime_error(
                [&workspace, &duplicate]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace,
                            duplicate + "\n" + duplicate + "\n"));
                },
                "duplicate expected path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, "relative.pkg.tar.zst\n"));
                },
                "relative expected path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path outside =
                root.canonical_path().parent_path() / "outside.pkg.tar.zst";
        expect_runtime_error(
                [&workspace, &outside]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, outside.string() + "\n"));
                },
                "outside expected path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path nested =
                workspace.path() / "nested" / "nested.pkg.tar.zst";
        expect_runtime_error(
                [&workspace, &nested]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, nested.string() + "\n"));
                },
                "nested expected path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string dot_component = workspace.path().string() +
                                          "/./dot.pkg.tar.zst\n";
        expect_runtime_error(
                [&workspace, &dot_component]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, dot_component));
                },
                "dot-component expected path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string dot_dot_component =
                workspace.path().string() + "/../" +
                workspace.path().filename().string() +
                "/dot-dot.pkg.tar.zst\n";
        expect_runtime_error(
                [&workspace, &dot_dot_component]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, dot_dot_component));
                },
                "dot-dot-component expected path");
    }
}

void test_packagelist_namespace_and_preexistence_rejections(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace,
                            make_packagelist_output(
                                    workspace, {"foo", "foo.sig"})));
                },
                "artifact/signature namespace collision");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path artifact_path =
                workspace.path() / "preexisting.pkg.tar.zst";
        write_file(artifact_path);
        expect_runtime_error(
                [&workspace, &artifact_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, artifact_path.string() + "\n"));
                },
                "preexisting expected artifact");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path artifact_path =
                workspace.path() / "preexisting-signature.pkg.tar.zst";
        write_file(signature_path(artifact_path), "signature");
        expect_runtime_error(
                [&workspace, &artifact_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, artifact_path.string() + "\n"));
                },
                "preexisting expected signature");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path first_path =
                workspace.path() / "fresh-first.pkg.tar.zst";
        const fs::path second_path =
                workspace.path() / "preexisting-second.pkg.tar.zst";
        const std::string output =
                first_path.string() + "\n" + second_path.string() + "\n";
        write_file(second_path);
        expect_runtime_error(
                [&workspace, &output]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, output));
                },
                "preexisting later expected artifact");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const fs::path first_path =
                workspace.path() / "fresh-signed-first.pkg.tar.zst";
        const fs::path second_path =
                workspace.path() / "preexisting-signed-second.pkg.tar.zst";
        const std::string output =
                first_path.string() + "\n" + second_path.string() + "\n";
        write_file(signature_path(second_path), "signature");
        expect_runtime_error(
                [&workspace, &output]() {
                    static_cast<void>(validate_makepkg_packagelist_output_set(
                            workspace, output));
                },
                "preexisting later expected signature");
    }
}

void test_query_and_exact_build_only_command(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    const fs::path first = workspace.path() / "first.pkg.tar.zst";
    const fs::path second = workspace.path() / "second.pkg.tar.zst";
    write_file(
            test_environment.packagelist_output_file(),
            first.string() + "\n" + second.string() + "\n");

    ExpectedPackageArtifactSet expected =
            query_makepkg_packagelist_set(workspace, context);
    expect(expected.size() == 2, "Queried expected set size differs");
    expect(
            expected.path_at(0) == first && expected.path_at(1) == second,
            "Queried expected set order differs");

    test_environment.clear_makepkg_logs();
    expect(
            context.run_makepkg_build_only(
                    workspace, expected,
                    ArtifactMakepkgBuildOptions{
                            .no_confirm = true,
                            .rebuild = true,
                            .clean_build = true}) == 0,
            "Multiple expected build-only makepkg fake failed");
    expect_equal(
            "multiple build-only command count",
            read_file(test_environment.command_log()),
            "makepkg -sc --noconfirm -f -C\n");
    expect_equal(
            "multiple build-only exact argv",
            read_file(test_environment.argv_log()),
            "argv-begin\n"
            "arg[0]=<-sc>\n"
            "arg[1]=<--noconfirm>\n"
            "arg[2]=<-f>\n"
            "arg[3]=<-C>\n"
            "argv-end\n");
    expect_equal(
            "multiple build-only cwd",
            read_file(test_environment.cwd_log()),
            checkout.canonical_path().string() + "\n");
}

void test_makepkg_context_provenance_rejections(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        SourceBuildEnvironment environment;
        ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
                checkout, workspace, environment,
                SourceEnvironmentEmptyValuePolicy::Forward);
        ExpectedPackageArtifactSet unbound = declare_expected_artifacts(
                workspace, {"unbound-one.pkg.tar.zst",
                            "unbound-two.pkg.tar.zst"});
        test_environment.clear_makepkg_logs();
        expect_runtime_error(
                [&workspace, &context, &unbound]() {
                    static_cast<void>(context.run_makepkg_build_only(
                            workspace, unbound,
                            ArtifactMakepkgBuildOptions{}));
                },
                "unbound expected set build");
        expect_equal(
                "unbound expected set command count",
                read_file(test_environment.command_log()), "");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        SourceBuildEnvironment environment{{{"FIRST", "same value"}}};
        ArtifactMakepkgContext query_context =
                prepare_artifact_makepkg_context(
                        checkout, workspace, environment,
                        SourceEnvironmentEmptyValuePolicy::Forward);
        ArtifactMakepkgContext other_context =
                prepare_artifact_makepkg_context(
                        checkout, workspace, environment,
                        SourceEnvironmentEmptyValuePolicy::Forward);
        write_file(
                test_environment.packagelist_output_file(),
                make_packagelist_output(
                        workspace, {"context-one.pkg.tar.zst",
                                    "context-two.pkg.tar.zst"}));
        ExpectedPackageArtifactSet expected =
                query_makepkg_packagelist_set(workspace, query_context);

        test_environment.clear_makepkg_logs();
        expect_runtime_error(
                [&workspace, &other_context, &expected]() {
                    static_cast<void>(other_context.run_makepkg_build_only(
                            workspace, expected,
                            ArtifactMakepkgBuildOptions{}));
                },
                "different makepkg context");
        expect_equal(
                "different makepkg context command count",
                read_file(test_environment.command_log()), "");
    }
}

void test_makepkg_workspace_mismatch_rejections(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace first_workspace = create_test_artifact_workspace(root);
    ArtifactWorkspace second_workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, first_workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    write_file(
            test_environment.packagelist_output_file(),
            make_packagelist_output(
                    first_workspace, {"workspace-one.pkg.tar.zst",
                                      "workspace-two.pkg.tar.zst"}));
    ExpectedPackageArtifactSet expected =
            query_makepkg_packagelist_set(first_workspace, context);

    test_environment.clear_makepkg_logs();
    expect_runtime_error(
            [&second_workspace, &context]() {
                static_cast<void>(query_makepkg_packagelist_set(
                        second_workspace, context));
            },
            "query workspace mismatch");
    expect_equal(
            "query workspace mismatch command count",
            read_file(test_environment.command_log()), "");

    expect_runtime_error(
            [&second_workspace, &context, &expected]() {
                static_cast<void>(context.run_makepkg_build_only(
                        second_workspace, expected,
                        ArtifactMakepkgBuildOptions{}));
            },
            "build workspace mismatch");
    expect_equal(
            "build workspace mismatch command count",
            read_file(test_environment.command_log()), "");
}

void test_multiple_artifact_success_move_and_cleanup(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path workspace_path = workspace.path();
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"gamma.pkg.tar.zst", "alpha.pkg.tar.zst",
             "beta.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    const fs::path first_signature = signature_path(expected.path_at(0));
    const fs::path third_signature = signature_path(expected.path_at(2));
    write_file(first_signature, "first signature");
    write_file(third_signature, "third signature");

    ValidatedPackageArtifactSet validated =
            validate_post_build_package_artifacts(
                    std::move(workspace), expected);
    expect_runtime_error(
            [&workspace]() { workspace.require_unchanged_identity(); },
            "successfully consumed workspace");
    expect(validated.size() == 3, "Validated set size differs");
    for(std::size_t index = 0; index < validated.size(); ++index) {
        expect(
                validated.path_at(index) == expected.path_at(index),
                "Validated set order differs at index " +
                        std::to_string(index));
    }
    expect(
            validated.workspace_path() == workspace_path,
            "Validated set lost aggregate workspace ownership");
    validated.require_validity();

    ValidatedPackageArtifactSet moved = std::move(validated);
    expect_runtime_error(
            [&validated]() { validated.require_validity(); },
            "moved-from validated set reuse");
    expect_runtime_error(
            [&validated]() { static_cast<void>(validated.size()); },
            "moved-from validated set size query");
    expect_runtime_error(
            [&validated]() { static_cast<void>(validated.path_at(0)); },
            "moved-from validated set path query");
    expect_runtime_error(
            [&validated]() {
                static_cast<void>(validated.workspace_path());
            },
            "moved-from validated set workspace query");
    expect_runtime_error(
            [&validated]() {
                validated.retain_workspace_for_diagnostics();
            },
            "moved-from validated set retention");
    expect_runtime_error(
            [&validated]() { validated.cleanup_workspace(); },
            "moved-from validated set cleanup");
    moved.require_validity();
    moved.cleanup_workspace();
    expect(
            !fs::exists(workspace_path),
            "Aggregate cleanup left workspace behind");
    expect(
            !fs::exists(first_signature) && !fs::exists(third_signature),
            "Aggregate cleanup left a signature behind");
    expect_runtime_error(
            [&moved]() { moved.require_validity(); },
            "cleaned validated set reuse");
    expect_runtime_error(
            [&moved]() { static_cast<void>(moved.workspace_path()); },
            "cleaned validated set workspace query");
    expect_runtime_error(
            [&moved]() { static_cast<void>(moved.size()); },
            "cleaned validated set size query");
    expect_runtime_error(
            [&moved]() { static_cast<void>(moved.path_at(0)); },
            "cleaned validated set path query");
    expect_runtime_error(
            [&moved]() { moved.retain_workspace_for_diagnostics(); },
            "cleaned validated set retention");
    expect_runtime_error(
            [&moved]() { moved.cleanup_workspace(); },
            "cleaned validated set cleanup");
}

void test_moved_from_destructor_does_not_own_workspace(
        const ValidatedCacheRoot& root) {
    fs::path workspace_path;
    ValidatedPackageArtifactSet moved = [&root, &workspace_path]() {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        workspace_path = workspace.path();
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"move-out-one.pkg.tar.zst", "move-out-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        ValidatedPackageArtifactSet source =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);
        return ValidatedPackageArtifactSet(std::move(source));
    }();

    expect(
            fs::is_directory(workspace_path),
            "Moved-from destructor removed aggregate workspace");
    moved.require_validity();
    moved.cleanup_workspace();
    expect(
            !fs::exists(workspace_path),
            "Moved aggregate cleanup left workspace behind");
}

void test_validation_failure_keeps_workspace_ownership(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"complete.pkg.tar.zst", "initially-missing.pkg.tar.zst"});
    write_file(expected.path_at(0));

    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifacts(
                        std::move(workspace), expected));
            },
            "one missing artifact");
    expect_workspace_remains_caller_owned(
            workspace, "missing artifact validation");

    write_file(expected.path_at(1));
    ValidatedPackageArtifactSet validated =
            validate_post_build_package_artifacts(
                    std::move(workspace), expected);
    validated.require_validity();
}

template <typename FixtureMutation>
void expect_post_build_validation_failure(
        const ValidatedCacheRoot& root, const std::string& context,
        FixtureMutation&& mutation) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"failure-one.pkg.tar.zst", "failure-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    std::forward<FixtureMutation>(mutation)(workspace, expected);

    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifacts(
                        std::move(workspace), expected));
            },
            context);
    expect_workspace_remains_caller_owned(workspace, context);
}

void test_unsafe_and_unexpected_entries(const ValidatedCacheRoot& root) {
    expect_post_build_validation_failure(
            root, "expected artifact symlink",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                fs::remove(expected.path_at(1));
                fs::create_symlink("/dev/null", expected.path_at(1));
            });
    expect_post_build_validation_failure(
            root, "expected artifact directory",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                fs::remove(expected.path_at(1));
                fs::create_directory(expected.path_at(1));
            });
    expect_post_build_validation_failure(
            root, "expected artifact special file",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                fs::remove(expected.path_at(1));
                create_fifo(expected.path_at(1));
            });
    expect_post_build_validation_failure(
            root, "unexpected artifact",
            [](ArtifactWorkspace& workspace,
               const ExpectedPackageArtifactSet&) {
                write_file(workspace.path() / "unexpected.pkg.tar.zst");
            });
    expect_post_build_validation_failure(
            root, "unexpected directory",
            [](ArtifactWorkspace& workspace,
               const ExpectedPackageArtifactSet&) {
                fs::create_directory(workspace.path() / "unexpected-directory");
            });
    expect_post_build_validation_failure(
            root, "unmatched signature",
            [](ArtifactWorkspace& workspace,
               const ExpectedPackageArtifactSet&) {
                write_file(
                        workspace.path() / "not-expected.pkg.tar.zst.sig",
                        "signature");
            });
    expect_post_build_validation_failure(
            root, "matching signature symlink",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                fs::create_symlink(
                        "/dev/null", signature_path(expected.path_at(0)));
            });
    expect_post_build_validation_failure(
            root, "matching signature directory",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                fs::create_directory(signature_path(expected.path_at(0)));
            });
    expect_post_build_validation_failure(
            root, "matching signature special file",
            [](ArtifactWorkspace&, const ExpectedPackageArtifactSet& expected) {
                create_fifo(signature_path(expected.path_at(0)));
            });
}

void test_owner_mismatch(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"owner-one.pkg.tar.zst", "owner-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);

    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(
                        validate_post_build_package_artifacts_for_test(
                                std::move(workspace), expected,
                                different_user_id()));
            },
            "multiple artifact owner mismatch");
    expect_workspace_remains_caller_owned(workspace, "owner mismatch");
}

void test_signature_owner_mismatch(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"signed-owner-one.pkg.tar.zst",
             "signed-owner-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    write_file(signature_path(expected.path_at(1)), "signature");

    const std::string error = expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(
                        validate_post_build_package_artifacts_for_test(
                                std::move(workspace), expected,
                                static_cast<std::uintmax_t>(geteuid()),
                                static_cast<std::uintmax_t>(geteuid()),
                                different_user_id()));
            },
            "signature owner mismatch",
            "Package signature owner does not match");
    expect(
            error.find("Package artifact owner does not match") ==
                    std::string::npos,
            "Signature owner mismatch stopped at artifact owner validation");
    expect_workspace_remains_caller_owned(
            workspace, "signature owner mismatch");
}

void test_expected_set_workspace_mismatch(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace expected_workspace =
            create_test_artifact_workspace(root);
    ArtifactWorkspace other_workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            expected_workspace,
            {"bound-one.pkg.tar.zst", "bound-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);

    expect_runtime_error(
            [&other_workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifacts(
                        std::move(other_workspace), expected));
            },
            "expected set workspace mismatch");
    expect_workspace_remains_caller_owned(
            other_workspace, "expected set workspace mismatch");
}

void test_replacement_during_validation(const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"race-one.pkg.tar.zst", "race-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);

        g_replacement_target = expected.path_at(1);
        g_replacement_observer_called = false;
        {
            ScopedMultipleArtifactValidationObserver observer(
                    replace_artifact_during_validation);
            expect_runtime_error(
                    [&workspace, &expected]() {
                        static_cast<void>(
                                validate_post_build_package_artifacts(
                                        std::move(workspace), expected));
                    },
                    "artifact replacement during validation",
                    "Refusing changed package artifact");
        }
        expect(
                g_replacement_observer_called,
                "Artifact validation replacement observer was not called");
        expect_workspace_remains_caller_owned(
                workspace, "artifact replacement during validation");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"signed-race-one.pkg.tar.zst",
                 "signed-race-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        g_replacement_target = signature_path(expected.path_at(0));
        write_file(g_replacement_target, "original signature");
        g_replacement_observer_called = false;
        {
            ScopedMultipleArtifactValidationObserver observer(
                    replace_artifact_during_validation);
            expect_runtime_error(
                    [&workspace, &expected]() {
                        static_cast<void>(
                                validate_post_build_package_artifacts(
                                        std::move(workspace), expected));
                    },
                    "signature replacement during validation",
                    "Refusing changed package signature");
        }
        expect(
                g_replacement_observer_called,
                "Signature validation replacement observer was not called");
        expect_workspace_remains_caller_owned(
                workspace, "signature replacement during validation");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"workspace-race-one.pkg.tar.zst",
                 "workspace-race-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        const fs::path workspace_path = workspace.path();
        g_replaced_workspace_original.clear();
        g_workspace_replacement_observer_called = false;
        {
            ScopedMultipleArtifactValidationObserver observer(
                    replace_workspace_during_validation);
            expect_runtime_error(
                    [&workspace, &expected]() {
                        static_cast<void>(
                                validate_post_build_package_artifacts(
                                        std::move(workspace), expected));
                    },
                    "workspace replacement during validation");
        }
        expect(
                g_workspace_replacement_observer_called,
                "Workspace validation replacement observer was not called");
        fs::remove(workspace_path);
        fs::rename(g_replaced_workspace_original, workspace_path);
        expect_workspace_remains_caller_owned(
                workspace, "workspace replacement during validation");
    }
}

void test_replacement_after_validation(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"stable-one.pkg.tar.zst", "stable-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    ValidatedPackageArtifactSet validated =
            validate_post_build_package_artifacts(
                    std::move(workspace), expected);

    expect(
            fs::remove(validated.path_at(1)),
            "Validated artifact replacement target was not removed");
    write_file(validated.path_at(1), "replacement");
    expect_runtime_error(
            [&validated]() { validated.require_validity(); },
            "artifact replacement after validation",
            "Validated package artifact path changed identity or owner");
    validated.retain_workspace_for_diagnostics();
}

void test_signature_changes_after_validation(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"signed-stable.pkg.tar.zst",
                 "signed-sibling.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        const fs::path signed_path = signature_path(expected.path_at(0));
        write_file(signed_path, "original signature");
        ValidatedPackageArtifactSet validated =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);

        expect(
                fs::remove(signed_path),
                "Validated signature replacement target was not removed");
        write_file(signed_path, "replacement signature");
        expect_runtime_error(
                [&validated]() { validated.require_validity(); },
                "signature replacement after validation",
                "Validated package signature path changed identity or owner");
        validated.retain_workspace_for_diagnostics();
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"unsigned-stable.pkg.tar.zst",
                 "unsigned-sibling.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        ValidatedPackageArtifactSet validated =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);

        write_file(
                signature_path(expected.path_at(1)),
                "late signature");
        expect_runtime_error(
                [&validated]() { validated.require_validity(); },
                "signature appearance after validation");
        validated.retain_workspace_for_diagnostics();
    }
}

void test_default_cleanup_retention_and_explicit_cleanup(
        const ValidatedCacheRoot& root) {
    fs::path default_cleanup_path;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        default_cleanup_path = workspace.path();
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"default-one.pkg.tar.zst", "default-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        ValidatedPackageArtifactSet validated =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);
        validated.require_validity();
    }
    expect(
            !fs::exists(default_cleanup_path),
            "Validated aggregate destructor left workspace behind");

    fs::path retained_path;
    fs::path retained_first_artifact;
    fs::path retained_second_artifact;
    fs::path retained_signature;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        retained_path = workspace.path();
        ExpectedPackageArtifactSet expected = declare_expected_artifacts(
                workspace,
                {"retained-one.pkg.tar.zst", "retained-two.pkg.tar.zst"});
        write_all_expected_artifacts(expected);
        retained_first_artifact = expected.path_at(0);
        retained_second_artifact = expected.path_at(1);
        retained_signature = signature_path(retained_second_artifact);
        write_file(retained_signature, "retained signature");
        ValidatedPackageArtifactSet validated =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);
        validated.retain_workspace_for_diagnostics();
    }
    expect(
            fs::is_directory(retained_path),
            "Diagnostic retention removed aggregate workspace");
    expect_equal(
            "retained first artifact contents",
            read_file(retained_first_artifact), "artifact-0");
    expect_equal(
            "retained second artifact contents",
            read_file(retained_second_artifact), "artifact-1");
    expect_equal(
            "retained signature contents", read_file(retained_signature),
            "retained signature");

    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path cleanup_path = workspace.path();
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"cleanup-one.pkg.tar.zst", "cleanup-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    write_file(signature_path(expected.path_at(1)), "signature");
    ValidatedPackageArtifactSet validated =
            validate_post_build_package_artifacts(
                    std::move(workspace), expected);
    validated.retain_workspace_for_diagnostics();
    validated.cleanup_workspace();
    expect(
            !fs::exists(cleanup_path),
            "Explicit aggregate cleanup did not override retention");
}

void test_cleanup_failure_refuses_replacement(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactSet expected = declare_expected_artifacts(
            workspace,
            {"cleanup-failure-one.pkg.tar.zst",
             "cleanup-failure-two.pkg.tar.zst"});
    write_all_expected_artifacts(expected);
    ValidatedPackageArtifactSet validated =
            validate_post_build_package_artifacts(
                    std::move(workspace), expected);

    const fs::path workspace_path = validated.workspace_path();
    fs::path original_workspace = workspace_path;
    original_workspace += ".cleanup-original";
    fs::rename(workspace_path, original_workspace);
    fs::create_directory(workspace_path);
    fs::permissions(
            workspace_path, fs::perms::owner_all,
            fs::perm_options::replace);
    const fs::path replacement_sentinel = workspace_path / "do-not-delete";
    write_file(replacement_sentinel);

    expect_runtime_error(
            [&validated]() { validated.cleanup_workspace(); },
            "aggregate cleanup workspace replacement");
    expect(
            fs::is_regular_file(replacement_sentinel),
            "Aggregate cleanup deleted replacement workspace contents");
    validated.retain_workspace_for_diagnostics();
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const char* configured_stub =
                std::getenv("JPACKER_TEST_MAKEPKG_STUB");
        const fs::path makepkg_stub_directory =
                argc >= 2
                        ? fs::path(argv[1])
                : configured_stub != nullptr
                        ? fs::path(configured_stub).parent_path()
                        : fs::path("tests/stubs");
        if(argc > 2) {
            throw std::runtime_error(
                    "Usage: multiple-artifact-workspace-test "
                    "[makepkg-stub-directory]");
        }

        TestEnvironment test_environment(makepkg_stub_directory);
        {
            ValidatedPrivateCacheRoot private_root =
                    prepare_private_trusted_cache_root();
            private_root.require_unchanged_identity();
        }
        ValidatedCacheRoot root = prepare_trusted_cache_root();
        ValidatedCachePath checkout = prepare_checkout(root);

        test_packagelist_single_multiple_and_order(root);
        test_packagelist_cardinality_and_path_rejections(root);
        test_packagelist_namespace_and_preexistence_rejections(root);
        test_query_and_exact_build_only_command(
                root, checkout, test_environment);
        test_makepkg_context_provenance_rejections(
                root, checkout, test_environment);
        test_makepkg_workspace_mismatch_rejections(
                root, checkout, test_environment);

        test_multiple_artifact_success_move_and_cleanup(root);
        test_moved_from_destructor_does_not_own_workspace(root);
        test_validation_failure_keeps_workspace_ownership(root);
        test_unsafe_and_unexpected_entries(root);
        test_owner_mismatch(root);
        test_signature_owner_mismatch(root);
        test_expected_set_workspace_mismatch(root);
        test_replacement_during_validation(root);
        test_replacement_after_validation(root);
        test_signature_changes_after_validation(root);
        test_default_cleanup_retention_and_explicit_cleanup(root);
        test_cleanup_failure_refuses_replacement(root);
    } catch(const std::exception& error) {
        std::cerr << "multiple artifact workspace test failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "multiple artifact workspace tests passed\n";
    return 0;
}
