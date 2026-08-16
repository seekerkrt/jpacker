#include "separated_source_build.hpp"

#include "stubs/artifact-install-executor/process_stub.hpp"
#include "stubs/package-metadata/alpm_stub.hpp"
#include "trusted_cache.hpp"
#include "trusted_cache_test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using SeparatedSourceBuildExecutor = ArtifactInstallExecutionOutcome (*)(
        SeparatedSourceBuildUnitRequest,
        const SeparatedSourceBuildUnitOptions&);

static_assert(
        std::is_same_v<
                decltype(&execute_separated_source_build_unit),
                SeparatedSourceBuildExecutor>);
static_assert(std::is_base_of_v<
              std::runtime_error, SeparatedSourceBuildCleanupError>);
static_assert(!std::is_invocable_v<
              SeparatedSourceBuildExecutor,
              ValidatedPackageArtifactPath,
              const SeparatedSourceBuildUnitOptions&>);
static_assert(!std::is_constructible_v<
              SeparatedSourceBuildUnitRequest, std::filesystem::path>);

namespace {

namespace fs = std::filesystem;
namespace metadata_stub = package_metadata_test_stub;
namespace process_stub = artifact_install_executor_test_stub;

constexpr const char* PACKAGE_NAME = "sample-package";
constexpr const char* ARTIFACT_VERSION = "2:1.4.0-3";
constexpr const char* ARTIFACT_LEAF =
        "sample-package-1.4.0-3-x86_64.pkg.tar.zst";
constexpr const char* ARTIFACT_WORKSPACE_PREFIX = ".artifact-workspace~-";
constexpr const char* BUILD_RUNNER_THROW_DIAGNOSTIC =
        "Fixture build runner threw after producing an artifact.";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Callable>
std::string expect_runtime_error(
        Callable&& callable, const std::string& context,
        const std::string& expected_fragment) {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::logic_error& error) {
        throw std::runtime_error(
                context + ": unexpected logic_error: " + error.what());
    } catch(const std::runtime_error& error) {
        if(std::string(error.what()).find(expected_fragment) ==
           std::string::npos) {
            throw std::runtime_error(
                    context + ": unexpected diagnostic [" + error.what() +
                    "]");
        }
        return error.what();
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " +
                error.what());
    }
    throw std::runtime_error(context + ": expected runtime_error");
}

struct CleanupErrorObservation {
    ArtifactInstallExecutionOutcome install_outcome;
    std::string                     diagnostic;
};

template <typename Callable>
CleanupErrorObservation expect_cleanup_error(
        Callable&& callable, const std::string& context) {
    try {
        std::forward<Callable>(callable)();
    } catch(const SeparatedSourceBuildCleanupError& error) {
        return CleanupErrorObservation{
                error.install_outcome(), error.what()};
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": cleanup failure was reported as another error: " +
                error.what());
    }
    throw std::runtime_error(
            context + ": expected SeparatedSourceBuildCleanupError");
}

class BuildRunnerTestError final : public std::runtime_error {
public:
    explicit BuildRunnerTestError(const std::string& diagnostic)
        : std::runtime_error(diagnostic) {
    }
};

template <typename Callable>
void expect_build_runner_error(Callable&& callable, const std::string& context) {
    try {
        std::forward<Callable>(callable)();
    } catch(const BuildRunnerTestError& error) {
        expect(
                std::string(error.what()) == BUILD_RUNNER_THROW_DIAGNOSTIC,
                context + ": build runner diagnostic changed");
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": build runner exception type changed: " +
                error.what());
    }
    throw std::runtime_error(context + ": expected BuildRunnerTestError");
}

void write_file(const fs::path& path, const std::string& contents = "fixture") {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error(
                "Failed to create separated lifecycle fixture: " +
                path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error(
                "Failed to finish separated lifecycle fixture: " +
                path.string());
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

        int result = value.has_value()
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

class ScopedWorkingDirectoryRestore final {
    fs::path working_directory_;

public:
    explicit ScopedWorkingDirectoryRestore(fs::path working_directory)
        : working_directory_(std::move(working_directory)) {
    }

    ScopedWorkingDirectoryRestore(const ScopedWorkingDirectoryRestore&) = delete;
    ScopedWorkingDirectoryRestore& operator=(
            const ScopedWorkingDirectoryRestore&) = delete;

    ~ScopedWorkingDirectoryRestore() noexcept {
        std::error_code error;
        fs::current_path(working_directory_, error);
    }
};

class TemporaryTestEnvironment final {
    fs::path                   original_working_directory_;
    fs::path                   path_;
    fs::path                   checkout_path_;
    std::optional<std::string> previous_xdg_cache_home_;
    std::optional<std::string> previous_pkgdest_;

public:
    TemporaryTestEnvironment()
        : original_working_directory_(fs::current_path()) {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "moguet-separated-source-build-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create separated lifecycle test directory.");
        }
        path_ = created_path;

        const char* previous_xdg = std::getenv("XDG_CACHE_HOME");
        if(previous_xdg != nullptr) previous_xdg_cache_home_ = previous_xdg;
        const char* previous_pkgdest = std::getenv("PKGDEST");
        if(previous_pkgdest != nullptr) previous_pkgdest_ = previous_pkgdest;

        if(setenv("XDG_CACHE_HOME", path_.c_str(), 1) != 0 ||
           unsetenv("PKGDEST") != 0) {
            throw std::runtime_error(
                    "Failed to prepare separated lifecycle test environment.");
        }

        try {
            ValidatedCacheRoot root = prepare_test_trusted_cache_root();
            checkout_path_ = root.path() / "source-checkout";
            fs::create_directory(checkout_path_);
            fs::permissions(
                    checkout_path_, fs::perms::owner_all,
                    fs::perm_options::replace);
            static_cast<void>(require_trusted_cache_path(
                    root, checkout_path_,
                    CachePathRequirement::ExistingDirectory));
            if(fs::equivalent(
                       original_working_directory_, checkout_path_)) {
                throw std::runtime_error(
                        "Separated lifecycle caller and checkout directories must differ in this test.");
            }
        } catch(...) {
            restore_environment();
            std::error_code error;
            fs::remove_all(path_, error);
            throw;
        }
    }

    TemporaryTestEnvironment(const TemporaryTestEnvironment&) = delete;
    TemporaryTestEnvironment& operator=(
            const TemporaryTestEnvironment&) = delete;

    ~TemporaryTestEnvironment() noexcept {
        set_separated_source_build_workspace_observer_for_test(nullptr);
        restore_environment();
        std::error_code working_directory_error;
        fs::current_path(
                original_working_directory_, working_directory_error);
        std::error_code error;
        fs::remove_all(path_, error);
    }

    SeparatedSourceBuildUnitRequest request(
            const SourceBuildEnvironment& source_environment,
            SourceEnvironmentEmptyValuePolicy empty_value_policy,
            DesiredInstallReason desired_reason) const {
        ValidatedCacheRoot root = prepare_test_trusted_cache_root();
        ValidatedCachePath checkout = require_trusted_cache_path(
                root, checkout_path_,
                CachePathRequirement::ExistingDirectory);
        return SeparatedSourceBuildUnitRequest{
                std::move(checkout),
                prepare_private_trusted_cache_root(root),
                PACKAGE_NAME,
                PACKAGE_NAME,
                desired_reason,
                source_environment,
                empty_value_policy,
                PacmanDatabasePaths{"/", "/var/lib/pacman"}};
    }

    std::vector<fs::path> artifact_workspaces() const {
        std::vector<fs::path> workspaces;
        ValidatedCacheRoot root = prepare_test_trusted_cache_root();
        for(const fs::directory_entry& entry :
            fs::directory_iterator(root.canonical_path())) {
            if(entry.path().filename().string().starts_with(
                       ARTIFACT_WORKSPACE_PREFIX)) {
                workspaces.push_back(entry.path());
            }
        }
        std::sort(workspaces.begin(), workspaces.end());
        return workspaces;
    }

    fs::path checkout_path() const {
        return fs::canonical(checkout_path_);
    }

    const fs::path& original_working_directory() const noexcept {
        return original_working_directory_;
    }

private:
    void restore_environment() noexcept {
        if(previous_xdg_cache_home_.has_value()) {
            static_cast<void>(setenv(
                    "XDG_CACHE_HOME", previous_xdg_cache_home_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("XDG_CACHE_HOME"));
        }
        if(previous_pkgdest_.has_value()) {
            static_cast<void>(setenv(
                    "PKGDEST", previous_pkgdest_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("PKGDEST"));
        }
    }
};

std::string expected_shell_quote(const std::string& value) {
    std::string quoted = "'";
    for(char character : value) {
        if(character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    quoted += "'";
    return quoted;
}

std::string expected_shell_join(const std::vector<std::string>& arguments) {
    std::string command;
    for(std::size_t index = 0; index < arguments.size(); ++index) {
        if(index != 0) command += " ";
        command += expected_shell_quote(arguments[index]);
    }
    return command;
}

enum class PackagelistOutputKind {
    Single,
    Zero,
    Multiple,
    InvalidRelative,
};

enum class BuildOutputKind {
    RegularArtifact,
    RegularArtifactThenThrow,
    MissingArtifact,
    ReplacedWithSymlink,
    UnexpectedEntry,
};

struct InvocationPlan {
    PackagelistOutputKind packagelist_output =
            PackagelistOutputKind::Single;
    bool            expect_build = true;
    int             build_exit_code = 0;
    BuildOutputKind build_output = BuildOutputKind::RegularArtifact;
    bool            expect_identity = true;
    std::string     identity_package_name = PACKAGE_NAME;
    std::string     identity_version = ARTIFACT_VERSION;
    int             identity_exit_code = 0;
    bool            expect_install = false;
    int             install_exit_code = 0;
    const char*     install_reason_option = nullptr;
    bool            replace_workspace_after_install = false;
};

struct LifecycleScenario {
    SourceBuildEnvironment             source_environment;
    SourceEnvironmentEmptyValuePolicy empty_value_policy =
            SourceEnvironmentEmptyValuePolicy::Omit;
    SeparatedSourceBuildUnitOptions options;
    std::vector<InvocationPlan>     plans{InvocationPlan{}};

    fs::path expected_checkout_path;
    fs::path caller_working_directory;

    std::vector<fs::path> workspace_paths;
    std::vector<fs::path> artifact_paths;
    std::vector<fs::path> displaced_workspace_paths;
    std::vector<std::size_t> build_invocation_indices;
    std::vector<std::size_t> identity_invocation_indices;
    std::vector<std::size_t> install_invocation_indices;
    std::size_t observed_packagelist_calls = 0;
    std::size_t observed_build_calls = 0;
    std::size_t observed_identity_calls = 0;
    std::size_t observed_install_calls = 0;
};

LifecycleScenario* g_scenario = nullptr;

std::string expected_environment_prefix(
        const LifecycleScenario& scenario,
        const fs::path& workspace_path) {
    std::string prefix;
    for(const SourceEnvironmentAssignment& assignment :
        scenario.source_environment.ordered_assignments) {
        if(assignment.value.empty() &&
           scenario.empty_value_policy ==
                   SourceEnvironmentEmptyValuePolicy::Omit) {
            continue;
        }
        prefix += assignment.key + "=" +
                  expected_shell_quote(assignment.value) + " ";
    }
    prefix += "PKGDEST=" + expected_shell_quote(workspace_path.string()) + " ";
    return prefix;
}

std::vector<std::string> expected_assignment_words(
        const LifecycleScenario& scenario,
        const fs::path& workspace_path) {
    std::vector<std::string> words;
    for(const SourceEnvironmentAssignment& assignment :
        scenario.source_environment.ordered_assignments) {
        if(assignment.value.empty() &&
           scenario.empty_value_policy ==
                   SourceEnvironmentEmptyValuePolicy::Omit) {
            continue;
        }
        words.push_back(assignment.key + "=" + assignment.value);
    }
    words.push_back("PKGDEST=" + workspace_path.string());
    return words;
}

std::string expected_packagelist_command(
        const LifecycleScenario& scenario,
        const fs::path& workspace_path) {
    std::vector<std::string> arguments{"makepkg", "--packagelist"};
    const std::vector<std::string> assignment_words =
            expected_assignment_words(scenario, workspace_path);
    arguments.insert(
            arguments.end(), assignment_words.begin(),
            assignment_words.end());
    return expected_environment_prefix(scenario, workspace_path) +
           expected_shell_join(arguments);
}

std::string expected_build_command(
        const LifecycleScenario& scenario,
        const fs::path& workspace_path) {
    std::vector<std::string> arguments{"makepkg", "-sc"};
    if(scenario.options.no_confirm) arguments.emplace_back("--noconfirm");
    if(scenario.options.rebuild) arguments.emplace_back("-f");
    if(scenario.options.clean_build) arguments.emplace_back("-C");
    const std::vector<std::string> assignment_words =
            expected_assignment_words(scenario, workspace_path);
    arguments.insert(
            arguments.end(), assignment_words.begin(),
            assignment_words.end());
    return expected_environment_prefix(scenario, workspace_path) +
           expected_shell_join(arguments);
}

std::string expected_identity_command(const fs::path& artifact_path) {
    return "LC_ALL=C " + expected_shell_join(
            {"pacman", "-Qp", "--color", "never", "--",
             artifact_path.string()});
}

std::string expected_install_command(
        const LifecycleScenario& scenario,
        const InvocationPlan& plan,
        const fs::path& artifact_path) {
    std::vector<std::string> arguments{"sudo", "pacman", "-U"};
    if(scenario.options.no_confirm) arguments.emplace_back("--noconfirm");
    if(scenario.options.needed) arguments.emplace_back("--needed");
    if(plan.install_reason_option != nullptr) {
        arguments.emplace_back(plan.install_reason_option);
    }
    arguments.emplace_back("--");
    arguments.push_back(artifact_path.string());
    return expected_shell_join(arguments);
}

std::string packagelist_output(
        PackagelistOutputKind kind,
        const fs::path& workspace_path,
        const fs::path& artifact_path) {
    switch(kind) {
    case PackagelistOutputKind::Single:
        return artifact_path.string() + "\n";
    case PackagelistOutputKind::Zero:
        return "";
    case PackagelistOutputKind::Multiple:
        return artifact_path.string() + "\n" +
               (workspace_path / "sibling.pkg.tar.zst").string() + "\n";
    case PackagelistOutputKind::InvalidRelative:
        return "relative-output.pkg.tar.zst\n";
    }
    throw std::logic_error("Unknown packagelist fixture kind.");
}

void observe_workspace(const fs::path& workspace_path) {
    if(g_scenario == nullptr) {
        throw std::logic_error(
                "Separated lifecycle created a workspace outside an active scenario.");
    }
    LifecycleScenario& scenario = *g_scenario;
    const std::size_t invocation_index = scenario.workspace_paths.size();
    if(invocation_index >= scenario.plans.size()) {
        throw std::logic_error(
                "Separated lifecycle created more workspaces than expected.");
    }

    const InvocationPlan& plan = scenario.plans[invocation_index];
    const fs::path artifact_path = workspace_path / ARTIFACT_LEAF;
    scenario.workspace_paths.push_back(workspace_path);
    scenario.artifact_paths.push_back(artifact_path);
    scenario.displaced_workspace_paths.emplace_back();

    process_stub::expect_capture_command(
            expected_packagelist_command(scenario, workspace_path),
            CapturedCommandResult{
                    packagelist_output(
                            plan.packagelist_output, workspace_path,
                            artifact_path),
                    0});

    if(plan.expect_build) {
        scenario.build_invocation_indices.push_back(invocation_index);
        process_stub::expect_run_command(
                expected_build_command(scenario, workspace_path),
                plan.build_exit_code);
    }
    if(plan.expect_identity) {
        scenario.identity_invocation_indices.push_back(invocation_index);
        process_stub::expect_capture_command(
                expected_identity_command(artifact_path),
                CapturedCommandResult{
                        plan.identity_package_name + " " +
                                plan.identity_version + "\n",
                        plan.identity_exit_code});
    }
    if(plan.expect_install) {
        scenario.install_invocation_indices.push_back(invocation_index);
        process_stub::expect_run_command(
                expected_install_command(scenario, plan, artifact_path),
                plan.install_exit_code);
    }
}

void observe_capture_command() {
    if(g_scenario == nullptr) {
        throw std::logic_error("Process capture occurred outside an active scenario.");
    }
    LifecycleScenario& scenario = *g_scenario;
    const std::string command = process_stub::last_captured_command();
    if(command.starts_with("LC_ALL=C ")) {
        const std::size_t ordinal = scenario.observed_identity_calls++;
        if(ordinal >= scenario.identity_invocation_indices.size()) {
            throw std::logic_error("Unexpected identity capture ordinal.");
        }
        const std::size_t invocation_index =
                scenario.identity_invocation_indices[ordinal];
        expect(
                fs::is_regular_file(scenario.artifact_paths[invocation_index]),
                "Identity query started without a post-build artifact");
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Identity query ran before makepkg restored the caller directory");
        return;
    }

    const std::size_t ordinal = scenario.observed_packagelist_calls++;
    expect(
            ordinal < scenario.workspace_paths.size(),
            "Unexpected packagelist capture ordinal");
    expect(
            fs::is_directory(scenario.workspace_paths[ordinal]),
            "Packagelist query started without an owned workspace");
    expect(
            !fs::exists(scenario.artifact_paths[ordinal]),
            "Packagelist query observed a pre-existing artifact");
    expect(
            fs::current_path() == scenario.expected_checkout_path,
            "Packagelist query did not run from the source checkout");
}

void create_build_output(
        BuildOutputKind kind,
        const fs::path& workspace_path,
        const fs::path& artifact_path) {
    switch(kind) {
    case BuildOutputKind::RegularArtifact:
        write_file(artifact_path, "built artifact\n");
        return;
    case BuildOutputKind::RegularArtifactThenThrow:
        write_file(artifact_path, "built artifact before runner throw\n");
        throw BuildRunnerTestError(BUILD_RUNNER_THROW_DIAGNOSTIC);
    case BuildOutputKind::MissingArtifact:
        return;
    case BuildOutputKind::ReplacedWithSymlink: {
        write_file(artifact_path, "original artifact\n");
        fs::path displaced_artifact = artifact_path;
        displaced_artifact += ".before-replacement";
        fs::rename(artifact_path, displaced_artifact);
        fs::create_symlink(displaced_artifact.filename(), artifact_path);
        return;
    }
    case BuildOutputKind::UnexpectedEntry:
        write_file(artifact_path, "built artifact\n");
        write_file(workspace_path / "unexpected-build-output", "unexpected\n");
        return;
    }
    throw std::logic_error("Unknown build output fixture kind.");
}

void require_metadata_released_before_install() {
    expect(
            metadata_stub::created_handle_count() > 0,
            "sudo pacman started without a metadata session");
    expect(
            metadata_stub::release_call_count() ==
                    metadata_stub::created_handle_count(),
            "sudo pacman started before all metadata sessions were released");
    for(std::size_t index = 0;
        index < metadata_stub::created_handle_count(); ++index) {
        expect(
                metadata_stub::release_count_for_handle(index) == 1,
                "Metadata session was not released exactly once before sudo pacman");
    }
}

void observe_run_command() {
    if(g_scenario == nullptr) {
        throw std::logic_error("Process run occurred outside an active scenario.");
    }
    LifecycleScenario& scenario = *g_scenario;
    const std::string command = process_stub::last_run_command();
    if(command.find("'makepkg'") != std::string::npos) {
        const std::size_t ordinal = scenario.observed_build_calls++;
        if(ordinal >= scenario.build_invocation_indices.size()) {
            throw std::logic_error("Unexpected makepkg build ordinal.");
        }
        const std::size_t invocation_index =
                scenario.build_invocation_indices[ordinal];
        expect(
                scenario.observed_packagelist_calls == ordinal + 1,
                "Build started before its packagelist query");
        expect(
                metadata_stub::initialize_call_count() == ordinal,
                "Build started after metadata was opened for its invocation");
        expect(
                fs::current_path() == scenario.expected_checkout_path,
                "Build-only makepkg did not reuse the packagelist working directory");
        create_build_output(
                scenario.plans[invocation_index].build_output,
                scenario.workspace_paths[invocation_index],
                scenario.artifact_paths[invocation_index]);
        return;
    }

    const std::size_t ordinal = scenario.observed_install_calls++;
    if(ordinal >= scenario.install_invocation_indices.size()) {
        throw std::logic_error("Unexpected sudo pacman ordinal.");
    }
    const std::size_t invocation_index =
            scenario.install_invocation_indices[ordinal];
    expect(
            fs::current_path() == scenario.caller_working_directory,
            "sudo pacman ran before makepkg restored the caller directory");
    require_metadata_released_before_install();

    const InvocationPlan& plan = scenario.plans[invocation_index];
    if(!plan.replace_workspace_after_install) return;

    fs::path displaced_workspace = scenario.workspace_paths[invocation_index];
    displaced_workspace += ".installed-before-cleanup";
    fs::rename(
            scenario.workspace_paths[invocation_index], displaced_workspace);
    fs::create_directory(scenario.workspace_paths[invocation_index]);
    fs::permissions(
            scenario.workspace_paths[invocation_index],
            fs::perms::owner_all, fs::perm_options::replace);
    scenario.displaced_workspace_paths[invocation_index] =
            std::move(displaced_workspace);
}

void activate_scenario(LifecycleScenario& scenario) {
    process_stub::reset_process_stub();
    metadata_stub::reset_alpm_stub();

    scenario.workspace_paths.clear();
    scenario.artifact_paths.clear();
    scenario.displaced_workspace_paths.clear();
    scenario.build_invocation_indices.clear();
    scenario.identity_invocation_indices.clear();
    scenario.install_invocation_indices.clear();
    scenario.observed_packagelist_calls = 0;
    scenario.observed_build_calls = 0;
    scenario.observed_identity_calls = 0;
    scenario.observed_install_calls = 0;

    g_scenario = &scenario;
    set_separated_source_build_workspace_observer_for_test(observe_workspace);
    process_stub::set_capture_hook(observe_capture_command);
    process_stub::set_run_hook(observe_run_command);
}

void require_scenario_complete(LifecycleScenario& scenario) {
    process_stub::require_process_expectations_consumed();
    expect(
            scenario.workspace_paths.size() == scenario.plans.size(),
            "Workspace observer call count differs");
    expect(
            scenario.observed_packagelist_calls == scenario.plans.size(),
            "Packagelist call count differs from workspace count");
    expect(
            scenario.observed_build_calls ==
                    scenario.build_invocation_indices.size(),
            "Build-only makepkg call count differs");
    expect(
            scenario.observed_identity_calls ==
                    scenario.identity_invocation_indices.size(),
            "Artifact identity query count differs");
    expect(
            scenario.observed_install_calls ==
                    scenario.install_invocation_indices.size(),
            "sudo pacman call count differs");
    set_separated_source_build_workspace_observer_for_test(nullptr);
    g_scenario = nullptr;
}

void expect_process_counts(
        std::size_t capture_calls, std::size_t run_calls,
        const std::string& context) {
    expect(
            process_stub::capture_command_call_count() == capture_calls,
            context + ": capture command count differs");
    expect(
            process_stub::run_command_call_count() == run_calls,
            context + ": run command count differs");
}

void expect_metadata_counts(
        std::size_t initialize_calls, std::size_t query_calls,
        std::size_t release_calls, const std::string& context) {
    expect(
            metadata_stub::initialize_call_count() == initialize_calls,
            context + ": metadata initialize count differs");
    expect(
            metadata_stub::package_query_call_count() == query_calls,
            context + ": metadata query count differs");
    expect(
            metadata_stub::release_call_count() == release_calls,
            context + ": metadata release count differs");
}

void expect_retained_workspace(
        const LifecycleScenario& scenario, std::size_t invocation_index,
        const std::string& context) {
    expect(
            fs::is_directory(scenario.workspace_paths.at(invocation_index)),
            context + ": workspace was not retained");
}

void expect_retained_regular_artifact(
        const LifecycleScenario& scenario, std::size_t invocation_index,
        const std::string& context) {
    expect_retained_workspace(scenario, invocation_index, context);
    expect(
            fs::is_regular_file(scenario.artifact_paths.at(invocation_index)),
            context + ": validated artifact was not retained");
}

void expect_no_workspace_created(
        const TemporaryTestEnvironment& environment,
        const std::vector<fs::path>& before,
        const LifecycleScenario& scenario,
        const std::string& context) {
    expect(
            scenario.workspace_paths.empty(),
            context + ": workspace observer unexpectedly ran");
    expect(
            environment.artifact_workspaces() == before,
            context + ": workspace directory was created");
}

ArtifactInstallExecutionOutcome execute_scenario(
        const TemporaryTestEnvironment& environment,
        LifecycleScenario& scenario,
        DesiredInstallReason desired_reason = DesiredInstallReason::Explicit) {
    // LANDMINE(#242): fixed baselineとのassertより先にrestore guardを作る。
    // assert自体が失敗しても後続caseを汚さず、assert前にdriftを隠さない。
    ScopedWorkingDirectoryRestore working_directory_restore(
            environment.original_working_directory());
    scenario.expected_checkout_path = environment.checkout_path();
    scenario.caller_working_directory =
            environment.original_working_directory();
    expect(
            scenario.caller_working_directory !=
                    scenario.expected_checkout_path,
            "Separated lifecycle caller and checkout directories unexpectedly match");
    expect(
            fs::current_path() == scenario.caller_working_directory,
            "Separated lifecycle scenario started from a drifted working directory");

    const ArtifactInstallExecutionOutcome install_outcome = [&]() {
        try {
            return execute_separated_source_build_unit(
                    environment.request(
                            scenario.source_environment,
                            scenario.empty_value_policy,
                            desired_reason),
                    scenario.options);
        } catch(...) {
            expect(
                    fs::current_path() == scenario.caller_working_directory,
                    "Separated lifecycle exception did not restore the caller working directory");
            throw;
        }
    }();

    expect(
            fs::current_path() == scenario.caller_working_directory,
            "Separated lifecycle success did not restore the caller working directory");
    return install_outcome;
}

void test_rmdeps_rejected_before_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.clear();
    scenario.options.rm_deps = true;
    activate_scenario(scenario);
    const std::vector<fs::path> before = environment.artifact_workspaces();

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "--rmdeps preflight",
            "Separated build/install does not support --rmdeps."));

    expect_process_counts(0, 0, "--rmdeps preflight");
    expect_metadata_counts(0, 0, 0, "--rmdeps preflight");
    expect_no_workspace_created(
            environment, before, scenario, "--rmdeps preflight");
    require_scenario_complete(scenario);
}

void test_source_pkgdest_rejected_before_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.clear();
    // POLICY: defined-emptyもexecutor-owned PKGDESTとのownership conflictである。
    scenario.source_environment.ordered_assignments.push_back(
            SourceEnvironmentAssignment{"PKGDEST", ""});
    activate_scenario(scenario);
    const std::vector<fs::path> before = environment.artifact_workspaces();

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "source PKGDEST preflight", "Source environment PKGDEST conflicts"));

    expect_process_counts(0, 0, "source PKGDEST preflight");
    expect_metadata_counts(0, 0, 0, "source PKGDEST preflight");
    expect_no_workspace_created(
            environment, before, scenario, "source PKGDEST preflight");
    require_scenario_complete(scenario);
}

void test_inherited_pkgdest_rejected_before_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.clear();
    activate_scenario(scenario);
    const std::vector<fs::path> before = environment.artifact_workspaces();
    ScopedEnvironmentVariable inherited_pkgdest("PKGDEST", std::string(""));

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "inherited PKGDEST preflight", "Inherited PKGDEST conflicts"));

    expect_process_counts(0, 0, "inherited PKGDEST preflight");
    expect_metadata_counts(0, 0, 0, "inherited PKGDEST preflight");
    expect_no_workspace_created(
            environment, before, scenario, "inherited PKGDEST preflight");
    require_scenario_complete(scenario);
}

void test_packagelist_zero_output_cleanup(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().packagelist_output = PackagelistOutputKind::Zero;
    scenario.plans.front().expect_build = false;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "zero packagelist output", "got 0"));

    expect_process_counts(1, 0, "zero packagelist output");
    expect_metadata_counts(0, 0, 0, "zero packagelist output");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Zero-output packagelist failure retained a pre-build workspace");
    require_scenario_complete(scenario);
}

void test_packagelist_multiple_output_cleanup(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().packagelist_output =
            PackagelistOutputKind::Multiple;
    scenario.plans.front().expect_build = false;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "multiple packagelist output", "got 2"));

    expect_process_counts(1, 0, "multiple packagelist output");
    expect_metadata_counts(0, 0, 0, "multiple packagelist output");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Multiple-output packagelist failure retained a pre-build workspace");
    require_scenario_complete(scenario);
}

void test_packagelist_invalid_path_cleanup(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().packagelist_output =
            PackagelistOutputKind::InvalidRelative;
    scenario.plans.front().expect_build = false;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "invalid packagelist path", "relative artifact path"));

    expect_process_counts(1, 0, "invalid packagelist path");
    expect_metadata_counts(0, 0, 0, "invalid packagelist path");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Invalid-path packagelist failure retained a pre-build workspace");
    require_scenario_complete(scenario);
}

void test_build_failure_retains_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().build_exit_code = 47;
    scenario.plans.front().build_output = BuildOutputKind::MissingArtifact;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "build-only failure", "The build-only makepkg command failed with exit code 47."));

    expect_process_counts(1, 1, "build-only failure");
    expect_metadata_counts(0, 0, 0, "build-only failure");
    expect_retained_workspace(scenario, 0, "build-only failure");
    expect(
            !fs::exists(scenario.artifact_paths.at(0)),
            "Build failure unexpectedly produced an artifact fixture");
    require_scenario_complete(scenario);

    LifecycleScenario runner_throw_scenario;
    runner_throw_scenario.plans.front().build_output =
            BuildOutputKind::RegularArtifactThenThrow;
    runner_throw_scenario.plans.front().expect_identity = false;
    activate_scenario(runner_throw_scenario);

    expect_build_runner_error(
            [&]() {
                execute_scenario(environment, runner_throw_scenario);
            },
            "build runner throw");

    expect_process_counts(1, 1, "build runner throw");
    expect_metadata_counts(0, 0, 0, "build runner throw");
    expect_retained_workspace(
            runner_throw_scenario, 0, "build runner throw");
    expect(
            fs::is_regular_file(
                    runner_throw_scenario.artifact_paths.at(0)),
            "Build runner throw did not retain its produced artifact");
    require_scenario_complete(runner_throw_scenario);
}

void test_missing_artifact_retains_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().build_output = BuildOutputKind::MissingArtifact;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "missing post-build artifact", "Expected package artifact is missing"));

    expect_process_counts(1, 1, "missing post-build artifact");
    expect_metadata_counts(0, 0, 0, "missing post-build artifact");
    expect_retained_workspace(scenario, 0, "missing post-build artifact");
    expect(
            !fs::exists(scenario.artifact_paths.at(0)),
            "Missing-artifact fixture unexpectedly exists");
    require_scenario_complete(scenario);
}

void test_replaced_artifact_retains_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().build_output =
            BuildOutputKind::ReplacedWithSymlink;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "replaced post-build artifact", "must not be a symlink"));

    expect_process_counts(1, 1, "replaced post-build artifact");
    expect_metadata_counts(0, 0, 0, "replaced post-build artifact");
    expect_retained_workspace(scenario, 0, "replaced post-build artifact");
    expect(
            fs::is_symlink(fs::symlink_status(scenario.artifact_paths.at(0))),
            "Replacement artifact fixture is not a symlink");
    require_scenario_complete(scenario);
}

void test_unexpected_workspace_entry_retains_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().build_output = BuildOutputKind::UnexpectedEntry;
    scenario.plans.front().expect_identity = false;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "unexpected workspace entry", "Unexpected entry in artifact workspace"));

    expect_process_counts(1, 1, "unexpected workspace entry");
    expect_metadata_counts(0, 0, 0, "unexpected workspace entry");
    expect_retained_regular_artifact(
            scenario, 0, "unexpected workspace entry");
    expect(
            fs::is_regular_file(
                    scenario.workspace_paths.at(0) /
                    "unexpected-build-output"),
            "Unexpected workspace entry fixture disappeared");
    require_scenario_complete(scenario);
}

void test_identity_query_failure_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().identity_exit_code = 29;
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "artifact identity failure", "identity with exit code 29"));

    expect_process_counts(2, 1, "artifact identity failure");
    expect_metadata_counts(0, 0, 0, "artifact identity failure");
    expect_retained_regular_artifact(
            scenario, 0, "artifact identity failure");
    require_scenario_complete(scenario);
}

void test_identity_name_mismatch_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().identity_package_name = "different-package";
    activate_scenario(scenario);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "artifact identity name mismatch",
            "Produced artifact package name does not match"));

    expect_process_counts(2, 1, "artifact identity name mismatch");
    expect_metadata_counts(0, 0, 0, "artifact identity name mismatch");
    expect_retained_regular_artifact(
            scenario, 0, "artifact identity name mismatch");
    require_scenario_complete(scenario);
}

void test_metadata_open_failure_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    activate_scenario(scenario);
    metadata_stub::set_initialize_failure(ALPM_ERR_SYSTEM);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "metadata session open failure", "Failed to initialize"));

    expect_process_counts(2, 1, "metadata session open failure");
    expect_metadata_counts(1, 0, 0, "metadata session open failure");
    expect_retained_regular_artifact(
            scenario, 0, "metadata session open failure");
    require_scenario_complete(scenario);
}

void test_metadata_query_failure_releases_session_and_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    activate_scenario(scenario);
    metadata_stub::set_package_query_failure(ALPM_ERR_DB_OPEN);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "metadata query failure", "Installed package query failed"));

    expect_process_counts(2, 1, "metadata query failure");
    expect_metadata_counts(1, 1, 1, "metadata query failure");
    expect(
            metadata_stub::release_count_for_handle(0) == 1,
            "Metadata query failure did not release its fresh session once");
    expect_retained_regular_artifact(
            scenario, 0, "metadata query failure");
    require_scenario_complete(scenario);
}

void test_package_absence_reaches_typed_asdeps_install(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().expect_install = true;
    scenario.plans.front().install_reason_option = "--asdeps";
    activate_scenario(scenario);
    metadata_stub::set_package_absent();

    const ArtifactInstallExecutionOutcome install_outcome = execute_scenario(
            environment, scenario, DesiredInstallReason::Dependency);

    expect_process_counts(2, 2, "package absence");
    expect_metadata_counts(1, 1, 1, "package absence");
    expect(
            install_outcome == ArtifactInstallExecutionOutcome::Installed,
            "Package absence did not return Installed");
    expect(
            metadata_stub::last_queried_package_name() == PACKAGE_NAME,
            "Package absence queried a different package name");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Successful absent-package install did not clean its workspace");
    require_scenario_complete(scenario);
}

void test_unknown_installed_reason_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    activate_scenario(scenario);
    metadata_stub::set_package_metadata(
            PACKAGE_NAME, "2:1.4.0-2", ALPM_PKG_REASON_UNKNOWN);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "unknown installed reason", "unknown install reason"));

    expect_process_counts(2, 1, "unknown installed reason");
    expect_metadata_counts(1, 1, 1, "unknown installed reason");
    expect_retained_regular_artifact(
            scenario, 0, "unknown installed reason");
    require_scenario_complete(scenario);
}

void test_same_version_needed_promotion_rejected_before_transaction(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.options.needed = true;
    activate_scenario(scenario);
    metadata_stub::set_package_metadata(
            PACKAGE_NAME, ARTIFACT_VERSION, ALPM_PKG_REASON_DEPEND);

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "same-version needed promotion", "Cannot change the install reason"));

    // --needed belongs only to PreparedArtifactInstall; exact build expectation
    // above rejects accidental propagation into makepkg.
    expect_process_counts(2, 1, "same-version needed promotion");
    expect_metadata_counts(1, 1, 1, "same-version needed promotion");
    expect_retained_regular_artifact(
            scenario, 0, "same-version needed promotion");
    require_scenario_complete(scenario);
}

void test_same_version_needed_default_returns_skipped_as_needed(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.options.needed = true;
    scenario.plans.front().expect_install = true;
    activate_scenario(scenario);
    metadata_stub::set_package_metadata(
            PACKAGE_NAME, ARTIFACT_VERSION, ALPM_PKG_REASON_EXPLICIT);

    const ArtifactInstallExecutionOutcome install_outcome =
            execute_scenario(environment, scenario);

    expect(
            install_outcome ==
                    ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            "Same-version --needed install did not return SkippedAsNeeded");
    expect_process_counts(2, 2, "same-version needed default");
    expect_metadata_counts(1, 1, 1, "same-version needed default");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Same-version --needed success did not clean its workspace");
    require_scenario_complete(scenario);
}

void test_typed_sudo_failure_retains_artifact(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().expect_install = true;
    scenario.plans.front().install_exit_code = 73;
    activate_scenario(scenario);
    metadata_stub::set_package_absent();

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "typed sudo pacman failure", "pacman -U failed with exit code 73."));

    expect_process_counts(2, 2, "typed sudo pacman failure");
    expect_metadata_counts(1, 1, 1, "typed sudo pacman failure");
    expect_retained_regular_artifact(
            scenario, 0, "typed sudo pacman failure");
    require_scenario_complete(scenario);
}

void test_makepkg_build_options_are_projected_independently(
        const TemporaryTestEnvironment& environment) {
    struct BuildOptionProjectionCase {
        const char*                     context;
        SeparatedSourceBuildUnitOptions options;
        std::vector<std::string>        expected_arguments;
    };

    // POLICY(#242): productionと同じbool mapping helperへ期待値を委譲せず、
    // one-hot inputごとのexact argvをliteralで固定する。
    const BuildOptionProjectionCase test_cases[] = {
            {
                    "makepkg no_confirm projection",
                    SeparatedSourceBuildUnitOptions{.no_confirm = true},
                    {"makepkg", "-sc", "--noconfirm"},
            },
            {
                    "makepkg rebuild projection",
                    SeparatedSourceBuildUnitOptions{.rebuild = true},
                    {"makepkg", "-sc", "-f"},
            },
            {
                    "makepkg clean_build projection",
                    SeparatedSourceBuildUnitOptions{.clean_build = true},
                    {"makepkg", "-sc", "-C"},
            },
            {
                    "makepkg combined projection",
                    SeparatedSourceBuildUnitOptions{
                            .no_confirm = true,
                            .rebuild = true,
                            .clean_build = true,
                    },
                    {"makepkg", "-sc", "--noconfirm", "-f", "-C"},
            },
    };

    for(const BuildOptionProjectionCase& test_case : test_cases) {
        LifecycleScenario scenario;
        scenario.options = test_case.options;
        scenario.plans.front().build_exit_code = 47;
        scenario.plans.front().build_output = BuildOutputKind::MissingArtifact;
        scenario.plans.front().expect_identity = false;
        activate_scenario(scenario);

        static_cast<void>(expect_runtime_error(
                [&]() { execute_scenario(environment, scenario); },
                test_case.context,
                "The build-only makepkg command failed with exit code 47."));

        std::vector<std::string> expected_arguments =
                test_case.expected_arguments;
        expected_arguments.push_back(
                "PKGDEST=" + scenario.workspace_paths.at(0).string());
        const std::string expected_command =
                expected_environment_prefix(
                        scenario, scenario.workspace_paths.at(0)) +
                expected_shell_join(expected_arguments);
        expect(
                process_stub::last_run_command() == expected_command,
                std::string(test_case.context) +
                        ": exact makepkg argv differs");
        expect_process_counts(1, 1, test_case.context);
        expect_metadata_counts(0, 0, 0, test_case.context);
        expect_retained_workspace(scenario, 0, test_case.context);
        expect(
                !fs::exists(scenario.artifact_paths.at(0)),
                std::string(test_case.context) +
                        ": option projection unexpectedly produced an artifact fixture");
        require_scenario_complete(scenario);
    }
}

void test_success_uses_typed_options_and_explicit_cleanup(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.source_environment.ordered_assignments = {
            {"FIRST", "alpha value"},
            {"EMPTY", ""},
    };
    scenario.empty_value_policy = SourceEnvironmentEmptyValuePolicy::Forward;
    scenario.options.no_confirm = true;
    scenario.options.needed = true;
    scenario.options.rebuild = true;
    scenario.options.clean_build = true;
    scenario.plans.front().expect_install = true;
    scenario.plans.front().install_reason_option = "--asexplicit";
    activate_scenario(scenario);
    metadata_stub::set_package_metadata(
            PACKAGE_NAME, "2:1.4.0-2", ALPM_PKG_REASON_DEPEND);

    const ArtifactInstallExecutionOutcome install_outcome =
            execute_scenario(environment, scenario);

    expect_process_counts(2, 2, "successful separated lifecycle");
    expect_metadata_counts(1, 1, 1, "successful separated lifecycle");
    expect(
            install_outcome == ArtifactInstallExecutionOutcome::Installed,
            "Different-version --needed install did not return Installed");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Successful separated lifecycle did not explicitly clean workspace");
    expect(
            process_stub::last_run_command().find("'--asexplicit'") !=
                    std::string::npos,
            "Successful root promotion omitted --asexplicit");
    expect(
            process_stub::last_run_command().find("'-D'") ==
                    std::string::npos,
            "Successful lifecycle used forbidden pacman -D fallback");
    require_scenario_complete(scenario);
}

void test_cleanup_failure_is_distinct_from_install_failure(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.plans.front().expect_install = true;
    scenario.plans.front().replace_workspace_after_install = true;
    activate_scenario(scenario);
    metadata_stub::set_package_absent();

    const CleanupErrorObservation cleanup_failure = expect_cleanup_error(
            [&]() { execute_scenario(environment, scenario); },
            "post-install cleanup failure");

    expect(
            cleanup_failure.install_outcome ==
                    ArtifactInstallExecutionOutcome::Installed,
            "Post-install cleanup failure did not retain Installed outcome");
    expect(
            cleanup_failure.diagnostic.find("Package installation succeeded") !=
                    std::string::npos,
            "Cleanup failure did not report the successful transaction");
    expect(
            cleanup_failure.diagnostic.find("pacman -U failed") ==
                    std::string::npos,
            "Cleanup failure was mislabeled as transaction failure");
    expect_process_counts(2, 2, "post-install cleanup failure");
    expect_metadata_counts(1, 1, 1, "post-install cleanup failure");
    expect(
            fs::is_directory(scenario.workspace_paths.at(0)),
            "Cleanup-failure replacement workspace disappeared");
    expect(
            fs::is_regular_file(
                    scenario.displaced_workspace_paths.at(0) / ARTIFACT_LEAF),
            "Installed artifact workspace was not preserved after cleanup failure");

    std::error_code cleanup_error;
    fs::remove_all(scenario.workspace_paths.at(0), cleanup_error);
    expect(!cleanup_error, "Failed to remove cleanup-failure replacement fixture");
    fs::remove_all(
            scenario.displaced_workspace_paths.at(0), cleanup_error);
    expect(!cleanup_error, "Failed to remove displaced installed workspace fixture");
    require_scenario_complete(scenario);
}

void test_skipped_as_needed_cleanup_failure_retains_typed_outcome(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    scenario.options.needed = true;
    scenario.plans.front().expect_install = true;
    scenario.plans.front().replace_workspace_after_install = true;
    activate_scenario(scenario);
    metadata_stub::set_package_metadata(
            PACKAGE_NAME, ARTIFACT_VERSION, ALPM_PKG_REASON_EXPLICIT);

    const CleanupErrorObservation cleanup_failure = expect_cleanup_error(
            [&]() { execute_scenario(environment, scenario); },
            "post-skip cleanup failure");

    expect(
            cleanup_failure.install_outcome ==
                    ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            "Post-skip cleanup failure did not retain SkippedAsNeeded outcome");
    expect(
            cleanup_failure.diagnostic.find("Package installation succeeded") !=
                    std::string::npos,
            "Post-skip cleanup diagnostic contract changed");
    expect(
            cleanup_failure.diagnostic.find("pacman -U failed") ==
                    std::string::npos,
            "Post-skip cleanup failure was mislabeled as transaction failure");
    expect_process_counts(2, 2, "post-skip cleanup failure");
    expect_metadata_counts(1, 1, 1, "post-skip cleanup failure");
    expect(
            fs::is_directory(scenario.workspace_paths.at(0)),
            "Post-skip cleanup-failure replacement workspace disappeared");
    expect(
            fs::is_regular_file(
                    scenario.displaced_workspace_paths.at(0) / ARTIFACT_LEAF),
            "Skipped artifact workspace was not preserved after cleanup failure");

    std::error_code cleanup_error;
    fs::remove_all(scenario.workspace_paths.at(0), cleanup_error);
    expect(
            !cleanup_error,
            "Failed to remove post-skip cleanup-failure replacement fixture");
    fs::remove_all(
            scenario.displaced_workspace_paths.at(0), cleanup_error);
    expect(
            !cleanup_error,
            "Failed to remove displaced skipped workspace fixture");
    require_scenario_complete(scenario);
}

void test_repeated_invocation_creates_fresh_workspace(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    InvocationPlan success;
    success.expect_install = true;
    scenario.plans = {success, success};
    activate_scenario(scenario);
    metadata_stub::set_package_absent();

    execute_scenario(environment, scenario);
    execute_scenario(environment, scenario);

    expect(
            scenario.workspace_paths.at(0) != scenario.workspace_paths.at(1),
            "Repeated invocation reused the previous workspace path");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)) &&
                    !fs::exists(scenario.workspace_paths.at(1)),
            "Repeated successful invocations did not clean both workspaces");
    expect_process_counts(4, 4, "repeated successful invocation");
    expect_metadata_counts(2, 2, 2, "repeated successful invocation");
    expect(
            metadata_stub::created_handle_count() == 2 &&
                    metadata_stub::release_count_for_handle(0) == 1 &&
                    metadata_stub::release_count_for_handle(1) == 1,
            "Repeated invocation did not use two fresh metadata sessions");
    require_scenario_complete(scenario);
}

void test_retained_artifact_is_not_reused(
        const TemporaryTestEnvironment& environment) {
    LifecycleScenario scenario;
    InvocationPlan failed_install;
    failed_install.expect_install = true;
    failed_install.install_exit_code = 63;
    InvocationPlan successful_install;
    successful_install.expect_install = true;
    scenario.plans = {failed_install, successful_install};
    activate_scenario(scenario);
    metadata_stub::set_package_absent();

    static_cast<void>(expect_runtime_error(
            [&]() { execute_scenario(environment, scenario); },
            "first retained invocation", "pacman -U failed with exit code 63."));
    expect_retained_regular_artifact(
            scenario, 0, "first retained invocation");

    execute_scenario(environment, scenario);

    expect(
            scenario.workspace_paths.at(0) != scenario.workspace_paths.at(1),
            "Second invocation reused the retained workspace path");
    expect(
            fs::is_regular_file(scenario.artifact_paths.at(0)),
            "Second invocation consumed or cleaned the retained artifact");
    expect(
            !fs::exists(scenario.workspace_paths.at(1)),
            "Second successful invocation did not clean its fresh workspace");
    expect_process_counts(4, 4, "retained artifact non-reuse");
    expect_metadata_counts(2, 2, 2, "retained artifact non-reuse");
    require_scenario_complete(scenario);
}

template <typename Callable>
void run_case(const std::string& name, Callable&& callable) {
    std::forward<Callable>(callable)();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        TemporaryTestEnvironment environment;

        run_case("--rmdeps rejection", [&]() {
            test_rmdeps_rejected_before_workspace(environment);
        });
        run_case("source environment PKGDEST conflict", [&]() {
            test_source_pkgdest_rejected_before_workspace(environment);
        });
        run_case("inherited environment PKGDEST conflict", [&]() {
            test_inherited_pkgdest_rejected_before_workspace(environment);
        });
        run_case("packagelist zero output", [&]() {
            test_packagelist_zero_output_cleanup(environment);
        });
        run_case("packagelist multiple output", [&]() {
            test_packagelist_multiple_output_cleanup(environment);
        });
        run_case("packagelist invalid path", [&]() {
            test_packagelist_invalid_path_cleanup(environment);
        });
        run_case("build failure retention", [&]() {
            test_build_failure_retains_workspace(environment);
        });
        run_case("expected artifact missing", [&]() {
            test_missing_artifact_retains_workspace(environment);
        });
        run_case("artifact replacement after build", [&]() {
            test_replaced_artifact_retains_workspace(environment);
        });
        run_case("unexpected workspace entry", [&]() {
            test_unexpected_workspace_entry_retains_workspace(environment);
        });
        run_case("artifact identity query failure", [&]() {
            test_identity_query_failure_retains_artifact(environment);
        });
        run_case("artifact package name mismatch", [&]() {
            test_identity_name_mismatch_retains_artifact(environment);
        });
        run_case("metadata session open failure", [&]() {
            test_metadata_open_failure_retains_artifact(environment);
        });
        run_case("metadata query failure", [&]() {
            test_metadata_query_failure_releases_session_and_retains_artifact(
                    environment);
        });
        run_case("package absence", [&]() {
            test_package_absence_reaches_typed_asdeps_install(environment);
        });
        run_case("unknown installed reason", [&]() {
            test_unknown_installed_reason_retains_artifact(environment);
        });
        run_case("same-version needed reason promotion", [&]() {
            test_same_version_needed_promotion_rejected_before_transaction(
                    environment);
        });
        run_case("same-version needed default outcome", [&]() {
            test_same_version_needed_default_returns_skipped_as_needed(
                    environment);
        });
        run_case("typed sudo pacman nonzero", [&]() {
            test_typed_sudo_failure_retains_artifact(environment);
        });
        run_case("independent makepkg build options", [&]() {
            test_makepkg_build_options_are_projected_independently(environment);
        });
        run_case("successful install", [&]() {
            test_success_uses_typed_options_and_explicit_cleanup(environment);
        });
        run_case("successful install followed by cleanup failure", [&]() {
            test_cleanup_failure_is_distinct_from_install_failure(environment);
        });
        run_case("needed skip followed by cleanup failure", [&]() {
            test_skipped_as_needed_cleanup_failure_retains_typed_outcome(
                    environment);
        });
        run_case("repeated invocation creates fresh workspace", [&]() {
            test_repeated_invocation_creates_fresh_workspace(environment);
        });
        run_case("retained artifact is not reused", [&]() {
            test_retained_artifact_is_not_reused(environment);
        });
    } catch(const std::exception& error) {
        std::cerr << "separated source build test failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "separated source build tests: all checks passed\n";
    return 0;
}
