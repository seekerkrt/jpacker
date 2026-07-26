#include "app_config.hpp"
#include "dependency_plan.hpp"
#include "process.hpp"
#include "separated_source_build.hpp"
#include "source_install.hpp"
#include "trusted_cache.hpp"

#include "stubs/artifact-install-executor/process_stub.hpp"
#include "stubs/package-metadata/alpm_stub.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

// process.cppをlinkしないfake-symbol binaryでも、production checkout ownerが使う
// trimmed capture APIだけは同じ意味でstrict raw-capture FIFOへ接続する。
CapturedCommandResult capture_command_output(const char* command) {
    CapturedCommandResult result = capture_command_output_raw(command);
    const std::size_t first = result.output.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) {
        result.output.clear();
        return result;
    }
    const std::size_t last = result.output.find_last_not_of(" \t\n\r");
    result.output = result.output.substr(first, last - first + 1);
    return result;
}

std::string exec_command(const char* command) {
    return capture_command_output(command).output;
}

int command_status(const std::string& command) {
    return run_command(command);
}

namespace {

namespace fs = std::filesystem;
namespace metadata_stub = package_metadata_test_stub;
namespace process_stub = artifact_install_executor_test_stub;

constexpr const char* PACMAN_DATABASE_PATH_COMMAND =
        "pacman-conf --verbose RootDir DBPath 2>/dev/null";
constexpr const char* GIT_REMOTE_COMMAND =
        "git config --get remote.origin.url";
constexpr const char* GIT_BRANCH_COMMAND =
        "git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null";
constexpr const char* ARTIFACT_VERSION = "1.0-1";

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

template <typename Callable>
std::string expect_runtime_error(
        Callable&& callable, const std::string& context,
        const std::string& expected_fragment) {
    try {
        std::forward<Callable>(callable)();
    } catch(const SeparatedSourceBuildCleanupError& error) {
        throw std::runtime_error(
                context + ": cleanup partial-success was not expected: " +
                error.what());
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
                context + ": cleanup partial-success was flattened: " +
                error.what());
    }
    throw std::runtime_error(
            context + ": expected SeparatedSourceBuildCleanupError");
}

template <typename Callable>
std::string expect_logic_error(
        Callable&& callable, const std::string& context,
        const std::string& expected_fragment) {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::logic_error& error) {
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
    throw std::runtime_error(context + ": expected logic_error");
}

void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error(
                "Failed to create production source-build fixture: " +
                path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error(
                "Failed to finish production source-build fixture: " +
                path.string());
    }
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) {
        throw std::runtime_error(
                "Failed to open production source-build fixture: " +
                path.string());
    }
    std::string contents(
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{});
    if(input.bad()) {
        throw std::runtime_error(
                "Failed to read production source-build fixture: " +
                path.string());
    }
    return contents;
}

struct CacheTreeEntry {
    std::string   relative_path;
    fs::file_type type = fs::file_type::unknown;
    std::string   payload;

    bool operator==(const CacheTreeEntry&) const = default;
};

using CacheTreeSnapshot = std::vector<CacheTreeEntry>;

CacheTreeSnapshot snapshot_cache_tree(const fs::path& cache_root_path) {
    CacheTreeSnapshot snapshot;
    for(const fs::directory_entry& entry :
        fs::recursive_directory_iterator(cache_root_path)) {
        const fs::file_status status = entry.symlink_status();
        std::string           payload;
        if(fs::is_regular_file(status)) {
            payload = read_file(entry.path());
        } else if(fs::is_symlink(status)) {
            payload = fs::read_symlink(entry.path()).generic_string();
        }
        snapshot.push_back(CacheTreeEntry{
                entry.path()
                        .lexically_relative(cache_root_path)
                        .generic_string(),
                status.type(),
                std::move(payload)});
    }
    std::sort(
            snapshot.begin(), snapshot.end(),
            [](const CacheTreeEntry& left, const CacheTreeEntry& right) {
                return left.relative_path < right.relative_path;
            });
    return snapshot;
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
                                   ? setenv(
                                             key_.c_str(),
                                             value->c_str(), 1)
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

class TemporaryProductionEnvironment final {
    fs::path                   original_working_directory_;
    fs::path                   path_;
    fs::path                   cache_root_path_;
    std::optional<std::string> previous_xdg_cache_home_;
    std::optional<std::string> previous_pkgdest_;

public:
    TemporaryProductionEnvironment()
        : original_working_directory_(fs::current_path()) {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "jpacker-production-source-build-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create production source-build test directory.");
        }
        path_ = created_path;

        const char* previous_xdg = std::getenv("XDG_CACHE_HOME");
        if(previous_xdg != nullptr) previous_xdg_cache_home_ = previous_xdg;
        const char* previous_pkgdest = std::getenv("PKGDEST");
        if(previous_pkgdest != nullptr) previous_pkgdest_ = previous_pkgdest;

        if(setenv("XDG_CACHE_HOME", path_.c_str(), 1) != 0 ||
           unsetenv("PKGDEST") != 0) {
            restore_environment();
            throw std::runtime_error(
                    "Failed to prepare production source-build test environment.");
        }

        try {
            ValidatedCacheRoot root = prepare_trusted_cache_root();
            cache_root_path_ = root.canonical_path();

            // POLICY: preflight snapshotはdirect entryだけでなく、file内容と
            // symlink targetの変化も検出できるfixtureを常に含める。
            const fs::path snapshot_fixture =
                    cache_root_path_ / "preflight-snapshot-fixture";
            fs::create_directory(snapshot_fixture);
            write_file(
                    snapshot_fixture / "state.txt",
                    "stable preflight fixture\n");
            fs::create_symlink(
                    "state.txt", snapshot_fixture / "state-link");
        } catch(...) {
            restore_environment();
            std::error_code error;
            fs::remove_all(path_, error);
            throw;
        }
    }

    TemporaryProductionEnvironment(
            const TemporaryProductionEnvironment&) = delete;
    TemporaryProductionEnvironment& operator=(
            const TemporaryProductionEnvironment&) = delete;

    ~TemporaryProductionEnvironment() noexcept {
        set_separated_source_build_workspace_observer_for_test(nullptr);
        process_stub::set_capture_hook(nullptr);
        process_stub::set_run_hook(nullptr);
        std::error_code working_directory_error;
        fs::current_path(
                original_working_directory_, working_directory_error);
        restore_environment();
        std::error_code error;
        fs::remove_all(path_, error);
    }

    fs::path ensure_checkout(
            const std::string& package_name,
            const std::string& git_url) const {
        fs::path checkout_path = cache_root_path_ / package_name;
        if(!fs::exists(checkout_path)) {
            fs::create_directory(checkout_path);
            fs::create_directory(checkout_path / ".git");
            write_file(
                    checkout_path / "PKGBUILD",
                    "# production source-build fixture for " + package_name +
                            "\n# remote: " + git_url + "\n");
        }
        return fs::canonical(checkout_path);
    }

    std::vector<fs::path> artifact_workspaces() const {
        std::vector<fs::path> workspaces;
        if(!fs::exists(cache_root_path_)) return workspaces;
        for(const fs::directory_entry& entry :
            fs::directory_iterator(cache_root_path_)) {
            if(entry.path().filename().string().starts_with(
                       ".artifact-workspace~-")) {
                workspaces.push_back(entry.path());
            }
        }
        std::sort(workspaces.begin(), workspaces.end());
        return workspaces;
    }

    CacheTreeSnapshot cache_tree_snapshot() const {
        return snapshot_cache_tree(cache_root_path_);
    }

    fs::path checkout_target_path(const std::string& package_name) const {
        return cache_root_path_ / package_name;
    }

    const fs::path& original_working_directory() const noexcept {
        return original_working_directory_;
    }

private:
    void restore_environment() noexcept {
        if(previous_xdg_cache_home_.has_value()) {
            static_cast<void>(setenv(
                    "XDG_CACHE_HOME",
                    previous_xdg_cache_home_->c_str(), 1));
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

std::string shell_quote(const std::string& value) {
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

std::string shell_join(const std::vector<std::string>& arguments) {
    std::string command;
    for(std::size_t index = 0; index < arguments.size(); ++index) {
        if(index != 0) command += " ";
        command += shell_quote(arguments[index]);
    }
    return command;
}

ProductionSourceBuildWorkItem make_work_item(
        const std::string& package_name,
        DesiredInstallReason desired_reason =
                DesiredInstallReason::Explicit) {
    ProductionSourceBuildWorkItem work_item;
    work_item.request.package_name = package_name;
    work_item.request.checkout_name = package_name;
    work_item.request.git_url =
            "https://aur.archlinux.org/" + package_name + ".git";
    work_item.desired_reason = desired_reason;
    return work_item;
}

BuildPlan two_entry_plan() {
    BuildPlan plan;
    const RootTargetIdentity root_identity{0, "root-package"};
    plan.root_targets.push_back(root_identity);
    plan.package_targets.push_back(PlannedPackageTarget{
            "dependency-package", "dependency-package",
            {PackageRole::RuntimeDependency}, {root_identity}});
    // Explicit優先はproduction側で再実装せず、BuildPlan helperの確定値を使う。
    plan.package_targets.push_back(PlannedPackageTarget{
            "root-package", "root-package",
            {PackageRole::RuntimeDependency, PackageRole::Root},
            {root_identity}});
    plan.order.push_back(
            BuildPlanEntry{"dependency-package", {"dependency-package"}});
    plan.order.push_back(
            BuildPlanEntry{"root-package", {"root-package"}});
    return plan;
}

AppConfig noninteractive_config() {
    AppConfig config;
    config.no_edit = true;
    config.no_diff = true;
    return config;
}

enum class MetadataMode {
    Absent,
    ExistingDependency,
    ExistingExplicitDifferentVersion,
    ExistingExplicitSameVersion,
    QueryFailure,
};

struct UnitPlan {
    std::string                       package_name;
    std::string                       git_url;
    SourceBuildEnvironment            source_environment;
    SourceEnvironmentEmptyValuePolicy empty_value_policy =
            SourceEnvironmentEmptyValuePolicy::Omit;
    bool     needed = false;
    fs::path checkout_path;

    int          build_exit_code = 0;
    bool         expect_identity = true;
    MetadataMode metadata_mode = MetadataMode::Absent;
    bool         expect_install = true;
    int          install_exit_code = 0;
    const char*  install_reason_option = nullptr;
    bool         replace_workspace_after_install = false;
};

struct ProductionScenario {
    AppConfig             config;
    fs::path              caller_working_directory;
    std::vector<UnitPlan> units;

    std::vector<fs::path> workspace_paths;
    std::vector<fs::path> artifact_paths;
    std::vector<fs::path> displaced_workspace_paths;
    std::vector<std::string> install_attempt_order;

    std::size_t active_unit = 0;
    std::size_t resolver_calls = 0;
    std::size_t git_remote_calls = 0;
    std::size_t git_fetch_calls = 0;
    std::size_t git_branch_calls = 0;
    std::size_t git_reset_calls = 0;
    std::size_t packagelist_calls = 0;
    std::size_t build_calls = 0;
    std::size_t identity_calls = 0;
    std::size_t install_calls = 0;
};

ProductionScenario* g_scenario = nullptr;

std::string source_environment_prefix(
        const UnitPlan& unit, const fs::path& workspace_path) {
    std::string prefix;
    for(const SourceEnvironmentAssignment& assignment :
        unit.source_environment.ordered_assignments) {
        if(assignment.value.empty() &&
           unit.empty_value_policy == SourceEnvironmentEmptyValuePolicy::Omit) {
            continue;
        }
        prefix += assignment.key + "=" + shell_quote(assignment.value) + " ";
    }
    prefix += "PKGDEST=" + shell_quote(workspace_path.string()) + " ";
    return prefix;
}

std::string expected_packagelist_command(
        const UnitPlan& unit, const fs::path& workspace_path) {
    return source_environment_prefix(unit, workspace_path) +
           shell_join({"makepkg", "--packagelist"});
}

std::string expected_build_command(
        const ProductionScenario& scenario, const UnitPlan& unit,
        const fs::path& workspace_path) {
    std::vector<std::string> arguments{"makepkg", "-sc"};
    if(scenario.config.no_confirm) arguments.emplace_back("--noconfirm");
    if(scenario.config.rebuild) arguments.emplace_back("-f");
    if(scenario.config.clean_build) arguments.emplace_back("-C");
    return source_environment_prefix(unit, workspace_path) +
           shell_join(arguments);
}

std::string expected_identity_command(const fs::path& artifact_path) {
    return "LC_ALL=C " + shell_join(
            {"pacman", "-U", "--print", "--print-format", "%n\t%v",
             "--", artifact_path.string()});
}

std::string expected_install_command(
        const ProductionScenario& scenario, const UnitPlan& unit,
        const fs::path& artifact_path) {
    std::vector<std::string> arguments{"sudo", "pacman", "-U"};
    if(scenario.config.no_confirm) arguments.emplace_back("--noconfirm");
    if(unit.needed) arguments.emplace_back("--needed");
    if(unit.install_reason_option != nullptr) {
        arguments.emplace_back(unit.install_reason_option);
    }
    arguments.emplace_back("--");
    arguments.push_back(artifact_path.string());
    return shell_join(arguments);
}

void expect_database_paths(int exit_code = 0) {
    process_stub::expect_capture_command(
            PACMAN_DATABASE_PATH_COMMAND,
            CapturedCommandResult{
                    "RootDir = /\nDBPath = /var/lib/pacman\n", exit_code});
}

void schedule_source_unit(std::size_t unit_index) {
    if(g_scenario == nullptr) {
        throw std::logic_error(
                "Source unit was scheduled outside an active scenario.");
    }
    ProductionScenario& scenario = *g_scenario;
    if(unit_index >= scenario.units.size()) {
        throw std::logic_error("Scheduled source unit is out of range.");
    }
    scenario.active_unit = unit_index;
    const UnitPlan& unit = scenario.units[unit_index];
    process_stub::expect_capture_command(
            GIT_REMOTE_COMMAND,
            CapturedCommandResult{unit.git_url + "\n", 0});
    process_stub::expect_run_command("git fetch origin", 0);
    process_stub::expect_capture_command(
            GIT_BRANCH_COMMAND,
            CapturedCommandResult{"origin/main\n", 0});
    process_stub::expect_run_command(
            "git reset --hard 'origin/main'", 0);
}

void configure_metadata(const UnitPlan& unit) {
    switch(unit.metadata_mode) {
    case MetadataMode::Absent:
        metadata_stub::set_package_absent();
        return;
    case MetadataMode::ExistingDependency:
        metadata_stub::set_package_metadata(
                unit.package_name, "0.9-1", ALPM_PKG_REASON_DEPEND);
        return;
    case MetadataMode::ExistingExplicitDifferentVersion:
        metadata_stub::set_package_metadata(
                unit.package_name, "0.9-1", ALPM_PKG_REASON_EXPLICIT);
        return;
    case MetadataMode::ExistingExplicitSameVersion:
        metadata_stub::set_package_metadata(
                unit.package_name, ARTIFACT_VERSION,
                ALPM_PKG_REASON_EXPLICIT);
        return;
    case MetadataMode::QueryFailure:
        metadata_stub::set_package_query_failure(ALPM_ERR_DB_OPEN);
        return;
    }
    throw std::logic_error("Unknown production metadata fixture mode.");
}

void observe_workspace(const fs::path& workspace_path) {
    if(g_scenario == nullptr) {
        throw std::logic_error(
                "Production lifecycle created a workspace outside an active scenario.");
    }
    ProductionScenario& scenario = *g_scenario;
    const std::size_t unit_index = scenario.workspace_paths.size();
    if(unit_index >= scenario.units.size() ||
       unit_index != scenario.active_unit) {
        throw std::logic_error(
                "Production lifecycle violated the scheduled BuildPlan order.");
    }
    expect(
            scenario.git_reset_calls == unit_index + 1,
            "Workspace was created before checkout preparation completed");

    const UnitPlan& unit = scenario.units[unit_index];
    const fs::path artifact_path =
            workspace_path /
            (unit.package_name + "-1.0-1-x86_64.pkg.tar.zst");
    scenario.workspace_paths.push_back(workspace_path);
    scenario.artifact_paths.push_back(artifact_path);
    scenario.displaced_workspace_paths.emplace_back();
    configure_metadata(unit);

    process_stub::expect_capture_command(
            expected_packagelist_command(unit, workspace_path),
            CapturedCommandResult{artifact_path.string() + "\n", 0});
    process_stub::expect_run_command(
            expected_build_command(scenario, unit, workspace_path),
            unit.build_exit_code);
    if(unit.expect_identity) {
        process_stub::expect_capture_command(
                expected_identity_command(artifact_path),
                CapturedCommandResult{
                        unit.package_name + "\t" + ARTIFACT_VERSION + "\n",
                        0});
    }
    if(unit.expect_install) {
        process_stub::expect_run_command(
                expected_install_command(scenario, unit, artifact_path),
                unit.install_exit_code);
    }
}

void observe_capture_command() {
    if(g_scenario == nullptr) {
        throw std::logic_error(
                "Process capture occurred outside an active production scenario.");
    }
    ProductionScenario& scenario = *g_scenario;
    const std::string command = process_stub::last_captured_command();
    if(command == PACMAN_DATABASE_PATH_COMMAND) {
        ++scenario.resolver_calls;
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Database path resolver changed the caller working directory");
        expect(
                scenario.workspace_paths.empty(),
                "Database paths were resolved after workspace creation");
        return;
    }

    const UnitPlan& unit = scenario.units.at(scenario.active_unit);
    if(command == GIT_REMOTE_COMMAND) {
        ++scenario.git_remote_calls;
        expect(
                fs::current_path() == unit.checkout_path,
                "Remote URL query did not run from the scheduled checkout");
        return;
    }
    if(command == GIT_BRANCH_COMMAND) {
        ++scenario.git_branch_calls;
        expect(
                scenario.git_fetch_calls == scenario.git_branch_calls,
                "Branch detection ran before git fetch");
        expect(
                fs::current_path() == unit.checkout_path,
                "Branch query did not run from the scheduled checkout");
        return;
    }
    if(command.starts_with("LC_ALL=C ")) {
        ++scenario.identity_calls;
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Artifact identity query leaked the makepkg working directory");
        expect(
                fs::is_regular_file(
                        scenario.artifact_paths.at(scenario.active_unit)),
                "Artifact identity query started before the build output existed");
        return;
    }
    if(command.find("'makepkg' '--packagelist'") != std::string::npos) {
        ++scenario.packagelist_calls;
        expect(
                fs::current_path() == unit.checkout_path,
                "makepkg --packagelist did not run from the scheduled checkout");
        expect(
                !fs::exists(
                        scenario.artifact_paths.at(scenario.active_unit)),
                "Fresh workspace contained an artifact before the build");
        return;
    }
    throw std::logic_error("Unknown production capture command category.");
}

void require_metadata_released_before_install() {
    expect(
            metadata_stub::created_handle_count() > 0,
            "sudo pacman started without a metadata session");
    expect(
            metadata_stub::release_call_count() ==
                    metadata_stub::created_handle_count(),
            "sudo pacman started before metadata session release");
    for(std::size_t index = 0;
        index < metadata_stub::created_handle_count(); ++index) {
        expect(
                metadata_stub::release_count_for_handle(index) == 1,
                "Metadata session was not released exactly once");
    }
}

void observe_run_command() {
    if(g_scenario == nullptr) {
        throw std::logic_error(
                "Process run occurred outside an active production scenario.");
    }
    ProductionScenario& scenario = *g_scenario;
    const UnitPlan& unit = scenario.units.at(scenario.active_unit);
    const std::string command = process_stub::last_run_command();
    if(command == "git fetch origin") {
        ++scenario.git_fetch_calls;
        expect(
                scenario.git_remote_calls == scenario.git_fetch_calls,
                "git fetch ran before remote identity validation");
        expect(
                fs::current_path() == unit.checkout_path,
                "git fetch did not run from the scheduled checkout");
        return;
    }
    if(command == "git reset --hard 'origin/main'") {
        ++scenario.git_reset_calls;
        expect(
                scenario.git_branch_calls == scenario.git_reset_calls,
                "git reset ran before branch detection");
        expect(
                fs::current_path() == unit.checkout_path,
                "git reset did not run from the scheduled checkout");
        return;
    }
    if(command.find("'makepkg' '-sc'") != std::string::npos) {
        ++scenario.build_calls;
        expect(
                scenario.packagelist_calls == scenario.build_calls,
                "Build-only makepkg ran before makepkg --packagelist");
        expect(
                metadata_stub::initialize_call_count() + 1 ==
                        scenario.build_calls,
                "Metadata session opened before build-only makepkg");
        expect(
                fs::current_path() == unit.checkout_path,
                "Build-only makepkg did not run from the scheduled checkout");
        if(unit.build_exit_code == 0) {
            write_file(
                    scenario.artifact_paths.at(scenario.active_unit),
                    "built package artifact\n");
        }
        return;
    }
    if(command.starts_with("'sudo' 'pacman' '-U'")) {
        ++scenario.install_calls;
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "sudo pacman ran before restoring the caller directory");
        require_metadata_released_before_install();
        scenario.install_attempt_order.push_back(unit.package_name);

        if(unit.replace_workspace_after_install) {
            fs::path displaced =
                    scenario.workspace_paths.at(scenario.active_unit);
            displaced += ".installed-before-cleanup";
            fs::rename(
                    scenario.workspace_paths.at(scenario.active_unit),
                    displaced);
            fs::create_directory(
                    scenario.workspace_paths.at(scenario.active_unit));
            scenario.displaced_workspace_paths.at(scenario.active_unit) =
                    std::move(displaced);
        }

        // Failure/cleanup partial-success must stop before a later PackageBase.
        if(unit.install_exit_code == 0 &&
           !unit.replace_workspace_after_install &&
           scenario.active_unit + 1 < scenario.units.size()) {
            schedule_source_unit(scenario.active_unit + 1);
        }
        return;
    }
    throw std::logic_error("Unknown production run command category.");
}

void activate_scenario(ProductionScenario& scenario) {
    process_stub::reset_process_stub();
    metadata_stub::reset_alpm_stub();
    scenario.workspace_paths.clear();
    scenario.artifact_paths.clear();
    scenario.displaced_workspace_paths.clear();
    scenario.install_attempt_order.clear();
    scenario.active_unit = 0;
    scenario.resolver_calls = 0;
    scenario.git_remote_calls = 0;
    scenario.git_fetch_calls = 0;
    scenario.git_branch_calls = 0;
    scenario.git_reset_calls = 0;
    scenario.packagelist_calls = 0;
    scenario.build_calls = 0;
    scenario.identity_calls = 0;
    scenario.install_calls = 0;

    g_scenario = &scenario;
    set_separated_source_build_workspace_observer_for_test(observe_workspace);
    process_stub::set_capture_hook(observe_capture_command);
    process_stub::set_run_hook(observe_run_command);
}

void deactivate_scenario() {
    set_separated_source_build_workspace_observer_for_test(nullptr);
    process_stub::set_capture_hook(nullptr);
    process_stub::set_run_hook(nullptr);
    g_scenario = nullptr;
}

void require_scenario_complete(
        const ProductionScenario& scenario,
        std::size_t expected_workspace_count,
        const std::string& context) {
    process_stub::require_process_expectations_consumed();
    expect(
            scenario.workspace_paths.size() == expected_workspace_count,
            context + ": workspace count differs");
    expect(
            scenario.packagelist_calls == expected_workspace_count,
            context + ": packagelist count differs");
    expect(
            scenario.build_calls == expected_workspace_count,
            context + ": build count differs");
    deactivate_scenario();
}

ProductionScenario make_execution_scenario(
        const TemporaryProductionEnvironment& environment,
        const std::vector<ProductionSourceBuildWorkItem>& work_items,
        const AppConfig& config) {
    ProductionScenario scenario;
    scenario.config = config;
    scenario.caller_working_directory =
            environment.original_working_directory();
    for(const ProductionSourceBuildWorkItem& work_item : work_items) {
        UnitPlan unit;
        unit.package_name = work_item.request.package_name;
        unit.git_url = work_item.request.git_url;
        unit.source_environment = work_item.request.custom_environment;
        unit.empty_value_policy = work_item.request.empty_value_policy;
        unit.needed = work_item.request.needed;
        unit.checkout_path = environment.ensure_checkout(
                work_item.request.checkout_name,
                work_item.request.git_url);
        scenario.units.push_back(std::move(unit));
    }
    return scenario;
}

PreparedProductionSourceBuildInvocation prepare_execution(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        ProductionScenario& scenario) {
    activate_scenario(scenario);
    expect_database_paths();
    PreparedProductionSourceBuildInvocation invocation =
            prepare_production_source_build_invocation(
                    std::move(work_items), scenario.config);
    expect(
            scenario.resolver_calls == 1,
            "Production resolver did not run exactly once during preflight");
    expect(
            scenario.workspace_paths.empty(),
            "Production preflight created an artifact workspace");
    schedule_source_unit(0);
    return invocation;
}

void execute_invocation(
        const PreparedProductionSourceBuildInvocation& invocation,
        ProductionScenario& scenario) {
    expect(
            fs::current_path() == scenario.caller_working_directory,
            "Production execution started from a drifted working directory");
    try {
        execute_prepared_source_build_invocation(invocation, scenario.config);
    } catch(...) {
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Production failure leaked a changed working directory");
        throw;
    }
    expect(
            fs::current_path() == scenario.caller_working_directory,
            "Production success leaked a changed working directory");
}

std::optional<ArtifactInstallExecutionOutcome> execute_work_item(
        const PreparedProductionSourceBuildInvocation& invocation,
        std::size_t work_item_index,
        ProductionScenario& scenario) {
    expect(
            fs::current_path() == scenario.caller_working_directory,
            "Production work-item execution started from a drifted working directory");
    try {
        std::optional<ArtifactInstallExecutionOutcome> outcome =
                execute_prepared_source_build_work_item(
                        invocation.work_items.at(work_item_index),
                        invocation.database_paths, scenario.config);
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Production work-item success leaked a changed working directory");
        return outcome;
    } catch(...) {
        expect(
                fs::current_path() == scenario.caller_working_directory,
                "Production work-item failure leaked a changed working directory");
        throw;
    }
}

struct PreflightFilesystemSnapshot {
    CacheTreeSnapshot     cache_tree;
    std::vector<fs::path> expected_missing_checkout_paths;
};

bool cache_entry_exists_without_following(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if(error == std::errc::no_such_file_or_directory) return false;
    if(error) {
        throw std::runtime_error(
                "Failed to inspect production preflight fixture path: " +
                path.string() + ": " + error.message());
    }
    return fs::exists(status);
}

PreflightFilesystemSnapshot snapshot_preflight_filesystem(
        const TemporaryProductionEnvironment& environment,
        const std::vector<std::string>& expected_missing_checkout_names,
        const std::string& context) {
    PreflightFilesystemSnapshot snapshot;
    snapshot.cache_tree = environment.cache_tree_snapshot();

    // POLICY: relative path setがcache root直下entry一覧を表し、payloadが
    // regular file内容とsymlink targetのmutationを固定する。
    expect(
            std::find(
                    snapshot.cache_tree.begin(), snapshot.cache_tree.end(),
                    CacheTreeEntry{
                            "preflight-snapshot-fixture",
                            fs::file_type::directory,
                            ""}) != snapshot.cache_tree.end(),
            context + ": snapshot omitted the direct cache fixture entry");
    expect(
            std::find(
                    snapshot.cache_tree.begin(), snapshot.cache_tree.end(),
                    CacheTreeEntry{
                            "preflight-snapshot-fixture/state.txt",
                            fs::file_type::regular,
                            "stable preflight fixture\n"}) !=
                    snapshot.cache_tree.end(),
            context + ": snapshot omitted fixture file contents");
    expect(
            std::find(
                    snapshot.cache_tree.begin(), snapshot.cache_tree.end(),
                    CacheTreeEntry{
                            "preflight-snapshot-fixture/state-link",
                            fs::file_type::symlink,
                            "state.txt"}) != snapshot.cache_tree.end(),
            context + ": snapshot omitted fixture symlink target");
    expect(
            environment.artifact_workspaces().empty(),
            context + ": artifact workspace existed before preflight");

    for(const std::string& checkout_name :
        expected_missing_checkout_names) {
        fs::path checkout_path =
                environment.checkout_target_path(checkout_name);
        expect(
                !cache_entry_exists_without_following(checkout_path),
                context + ": checkout target existed before preflight: " +
                        checkout_name);
        snapshot.expected_missing_checkout_paths.push_back(
                std::move(checkout_path));
    }
    return snapshot;
}

void expect_zero_mutation_state(
        const TemporaryProductionEnvironment& environment,
        const PreflightFilesystemSnapshot& before,
        const ProductionScenario& scenario,
        const std::string& context) {
    expect(
            scenario.workspace_paths.empty(),
            context + ": workspace observer unexpectedly ran");
    expect(
            environment.cache_tree_snapshot() == before.cache_tree,
            context + ": cache/source filesystem changed during preflight");
    expect(
            environment.artifact_workspaces().empty(),
            context + ": artifact workspace was created");
    for(const fs::path& checkout_path :
        before.expected_missing_checkout_paths) {
        expect(
                !cache_entry_exists_without_following(checkout_path),
                context + ": checkout target was created: " +
                        checkout_path.filename().string());
    }
    expect(
            process_stub::run_command_call_count() == 0,
            context + ": mutation-capable process unexpectedly ran");
    expect(
            metadata_stub::initialize_call_count() == 0,
            context + ": metadata session unexpectedly opened");
}

void test_process_stub_rejects_cross_kind_reordering() {
    process_stub::reset_process_stub();
    process_stub::expect_capture_command(
            "capture-before-run", CapturedCommandResult{"unused", 0});
    process_stub::expect_run_command("run-after-capture", 0);

    const std::string diagnostic = expect_logic_error(
            []() {
                static_cast<void>(run_command("run-after-capture"));
            },
            "process global FIFO kind mismatch",
            "Unexpected artifact install run command");
    expect(
            process_stub::capture_command_call_count() == 0 &&
                    process_stub::run_command_call_count() == 1,
            "Process global FIFO kind mismatch reached the later run expectation");
    expect(
            expect_logic_error(
                    process_stub::require_process_expectations_consumed,
                    "sticky process global FIFO kind mismatch",
                    "Unexpected artifact install run command") == diagnostic,
            "Process global FIFO kind mismatch was not retained");

    process_stub::reset_process_stub();
    process_stub::require_process_expectations_consumed();
}

void test_build_plan_projection() {
    const BuildPlan plan = two_entry_plan();
    const std::vector<ProductionSourceBuildWorkItem> work_items =
            prepare_aur_source_build_work_items(plan, false, true);
    expect(work_items.size() == 2, "BuildPlan projection changed unit count");
    expect(
            work_items[0].request.package_name == "dependency-package" &&
                    work_items[1].request.package_name == "root-package",
            "BuildPlan::order was not preserved by production work-item preparation");
    expect(
            work_items[0].desired_reason == DesiredInstallReason::Dependency,
            "Dependency role was not projected to DesiredInstallReason::Dependency");
    expect(
            work_items[1].desired_reason == DesiredInstallReason::Explicit,
            "Root role did not retain BuildPlan's explicit-priority result");
    expect(
            work_items[0].request.needed && work_items[1].request.needed,
            "--needed was not projected to every BuildPlan unit");
}

void test_rmdeps_global_rejection(
        const TemporaryProductionEnvironment& environment) {
    ProductionScenario scenario;
    scenario.caller_working_directory =
            environment.original_working_directory();
    scenario.config = noninteractive_config();
    scenario.config.rm_deps = true;
    activate_scenario(scenario);
    const PreflightFilesystemSnapshot before =
            snapshot_preflight_filesystem(
                    environment, {"rmdeps-first", "rmdeps-second"},
                    "--rmdeps global preflight");

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("rmdeps-first"));
    work_items.push_back(make_work_item("rmdeps-second"));
    static_cast<void>(expect_runtime_error(
            [&]() {
                static_cast<void>(prepare_production_source_build_invocation(
                        std::move(work_items), scenario.config));
            },
            "--rmdeps global preflight", "does not support --rmdeps"));

    expect(
            process_stub::capture_command_call_count() == 0,
            "--rmdeps rejection called the database resolver");
    expect_zero_mutation_state(
            environment, before, scenario, "--rmdeps global preflight");
    process_stub::require_process_expectations_consumed();
    deactivate_scenario();
}

void test_inherited_pkgdest_global_rejection(
        const TemporaryProductionEnvironment& environment) {
    ScopedEnvironmentVariable inherited_pkgdest(
            "PKGDEST", std::optional<std::string>(""));
    ProductionScenario scenario;
    scenario.caller_working_directory =
            environment.original_working_directory();
    scenario.config = noninteractive_config();
    activate_scenario(scenario);
    const PreflightFilesystemSnapshot before =
            snapshot_preflight_filesystem(
                    environment, {"inherited-pkgdest"},
                    "inherited PKGDEST preflight");

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("inherited-pkgdest"));
    static_cast<void>(expect_runtime_error(
            [&]() {
                static_cast<void>(prepare_production_source_build_invocation(
                        std::move(work_items), scenario.config));
            },
            "inherited PKGDEST preflight", "PKGDEST"));

    expect(
            process_stub::capture_command_call_count() == 0,
            "Inherited PKGDEST rejection called the database resolver");
    expect_zero_mutation_state(
            environment, before, scenario, "inherited PKGDEST preflight");
    process_stub::require_process_expectations_consumed();
    deactivate_scenario();
}

void test_later_target_pkgdest_global_rejection(
        const TemporaryProductionEnvironment& environment) {
    ProductionScenario scenario;
    scenario.caller_working_directory =
            environment.original_working_directory();
    scenario.config = noninteractive_config();
    activate_scenario(scenario);
    const PreflightFilesystemSnapshot before =
            snapshot_preflight_filesystem(
                    environment, {"valid-first", "invalid-later"},
                    "later target PKGDEST preflight");

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("valid-first"));
    ProductionSourceBuildWorkItem invalid_later =
            make_work_item("invalid-later");
    invalid_later.request.custom_environment.ordered_assignments.push_back(
            SourceEnvironmentAssignment{"PKGDEST", ""});
    work_items.push_back(std::move(invalid_later));

    static_cast<void>(expect_runtime_error(
            [&]() {
                static_cast<void>(prepare_production_source_build_invocation(
                        std::move(work_items), scenario.config));
            },
            "later target PKGDEST preflight", "PKGDEST"));

    expect(
            process_stub::capture_command_call_count() == 0,
            "Later-target PKGDEST rejection called the database resolver");
    expect_zero_mutation_state(
            environment, before, scenario,
            "later target PKGDEST preflight");
    process_stub::require_process_expectations_consumed();
    deactivate_scenario();
}

void test_database_resolver_failure_stops_all_targets(
        const TemporaryProductionEnvironment& environment) {
    ProductionScenario scenario;
    scenario.caller_working_directory =
            environment.original_working_directory();
    scenario.config = noninteractive_config();
    activate_scenario(scenario);
    expect_database_paths(41);
    const PreflightFilesystemSnapshot before =
            snapshot_preflight_filesystem(
                    environment, {"resolver-first", "resolver-second"},
                    "database resolver failure");

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("resolver-first"));
    work_items.push_back(make_work_item("resolver-second"));
    static_cast<void>(expect_runtime_error(
            [&]() {
                static_cast<void>(prepare_production_source_build_invocation(
                        std::move(work_items), scenario.config));
            },
            "database resolver failure", "pacman-conf failed with exit code 41"));

    expect(
            scenario.resolver_calls == 1 &&
                    process_stub::capture_command_call_count() == 1,
            "Database resolver failure did not stop after exactly one call");
    expect_zero_mutation_state(
            environment, before, scenario, "database resolver failure");
    process_stub::require_process_expectations_consumed();
    deactivate_scenario();
}

void expect_single_work_item_outcome(
        const TemporaryProductionEnvironment& environment,
        const std::string& package_name,
        bool needed,
        MetadataMode metadata_mode,
        ArtifactInstallExecutionOutcome expected_outcome,
        const std::string& context) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item(package_name));
    work_items[0].request.needed = needed;
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].metadata_mode = metadata_mode;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    const std::optional<ArtifactInstallExecutionOutcome> outcome =
            execute_work_item(invocation, 0, scenario);

    expect(outcome.has_value(), context + ": typed outcome was omitted");
    expect(
            *outcome == expected_outcome,
            context + ": typed outcome differs");
    expect(
            scenario.install_attempt_order ==
                    std::vector<std::string>{package_name},
            context + ": successful pacman -U was not observed");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            context + ": successful execution retained its workspace");
    require_scenario_complete(scenario, 1, context);
}

void test_work_item_typed_install_outcomes(
        const TemporaryProductionEnvironment& environment) {
    expect_single_work_item_outcome(
            environment, "outcome-needed-false", false,
            MetadataMode::Absent,
            ArtifactInstallExecutionOutcome::Installed,
            "needed=false install outcome");
    expect_single_work_item_outcome(
            environment, "outcome-needed-different", true,
            MetadataMode::ExistingExplicitDifferentVersion,
            ArtifactInstallExecutionOutcome::Installed,
            "different-version --needed install outcome");
    expect_single_work_item_outcome(
            environment, "outcome-needed-same", true,
            MetadataMode::ExistingExplicitSameVersion,
            ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            "same-version --needed install outcome");
}

void test_single_aur_root_uses_shared_lifecycle(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("single-root"));
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);

    execute_invocation(invocation, scenario);

    expect(
            scenario.install_attempt_order ==
                    std::vector<std::string>{"single-root"},
            "Single AUR root did not reach typed pacman -U");
    expect(
            metadata_stub::created_handle_count() == 1 &&
                    metadata_stub::release_call_count() == 1,
            "Single AUR root did not use one fresh metadata session");
    expect(
            !fs::exists(scenario.workspace_paths.at(0)),
            "Single AUR root success did not clean its workspace");
    require_scenario_complete(scenario, 1, "single AUR root success");
}

void test_multi_unit_options_roles_and_order(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    config.no_confirm = true;
    config.rebuild = true;
    config.clean_build = true;
    const BuildPlan plan = two_entry_plan();
    std::vector<ProductionSourceBuildWorkItem> work_items =
            prepare_aur_source_build_work_items(plan, false, true);
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].install_reason_option = "--asdeps";
    scenario.units[1].metadata_mode = MetadataMode::ExistingDependency;
    scenario.units[1].install_reason_option = "--asexplicit";

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    execute_invocation(invocation, scenario);

    expect(
            scenario.resolver_calls == 1,
            "Multiple units re-resolved PacmanDatabasePaths");
    expect(
            metadata_stub::created_handle_count() == 2 &&
                    metadata_stub::release_call_count() == 2,
            "Multiple units did not use fresh metadata sessions");
    expect(
            metadata_stub::release_count_for_handle(0) == 1 &&
                    metadata_stub::release_count_for_handle(1) == 1,
            "Metadata sessions were shared or released incorrectly");
    expect(
            scenario.install_attempt_order == std::vector<std::string>{
                    "dependency-package", "root-package"},
            "Production execution changed BuildPlan::order");
    expect(
            process_stub::run_command_call_count() == 8,
            "Combined option scenario changed its strict run boundary count");
    for(const fs::path& workspace_path : scenario.workspace_paths) {
        expect(
                !fs::exists(workspace_path),
                "Successful multi-unit execution retained a workspace");
    }
    require_scenario_complete(
            scenario, 2, "multi-unit role/option/order projection");
}

void test_build_failure_does_not_reach_sudo(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("build-failure"));
    work_items.push_back(make_work_item("build-not-started"));
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].build_exit_code = 37;
    scenario.units[0].expect_identity = false;
    scenario.units[0].expect_install = false;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    static_cast<void>(expect_runtime_error(
            [&]() { execute_invocation(invocation, scenario); },
            "production build failure", "Build-only makepkg failed with exit code 37"));

    expect(
            scenario.install_calls == 0 &&
                    metadata_stub::initialize_call_count() == 0,
            "Build failure reached metadata or sudo pacman");
    expect(
            scenario.workspace_paths.size() == 1,
            "Build failure started a later PackageBase");
    require_scenario_complete(scenario, 1, "production build failure");
}

void test_metadata_failure_does_not_reach_sudo(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("metadata-failure"));
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].metadata_mode = MetadataMode::QueryFailure;
    scenario.units[0].expect_install = false;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    static_cast<void>(expect_runtime_error(
            [&]() { execute_invocation(invocation, scenario); },
            "production metadata failure", "Installed package query failed"));

    expect(
            scenario.install_calls == 0,
            "Metadata failure reached sudo pacman");
    expect(
            metadata_stub::initialize_call_count() == 1 &&
                    metadata_stub::package_query_call_count() == 1 &&
                    metadata_stub::release_call_count() == 1,
            "Metadata failure did not close its fresh session");
    require_scenario_complete(scenario, 1, "production metadata failure");
}

void test_pacman_failure_stops_later_unit(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("pacman-failure"));
    work_items.push_back(make_work_item("pacman-later"));
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].install_exit_code = 73;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    static_cast<void>(expect_runtime_error(
            [&]() { execute_invocation(invocation, scenario); },
            "production pacman failure", "pacman -U failed with exit code 73"));

    expect(
            scenario.workspace_paths.size() == 1 &&
                    scenario.install_attempt_order ==
                            std::vector<std::string>{"pacman-failure"},
            "pacman failure started a later PackageBase");
    expect(
            fs::is_regular_file(scenario.artifact_paths.at(0)),
            "pacman failure did not retain its diagnostic artifact");
    require_scenario_complete(scenario, 1, "production pacman failure");
}

void test_cleanup_partial_success_stays_distinct_and_stops(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("cleanup-partial"));
    work_items.push_back(make_work_item("cleanup-later"));
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].replace_workspace_after_install = true;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    const CleanupErrorObservation cleanup_error = expect_cleanup_error(
            [&]() { execute_invocation(invocation, scenario); },
            "production cleanup partial-success");

    expect(
            cleanup_error.install_outcome ==
                    ArtifactInstallExecutionOutcome::Installed,
            "Cleanup error lost the completed install outcome");
    expect(
            cleanup_error.diagnostic.find("Package installation succeeded") !=
                    std::string::npos,
            "Cleanup error omitted successful package installation");
    expect(
            cleanup_error.diagnostic.find("Failed while building/installing") ==
                    std::string::npos &&
                    cleanup_error.diagnostic.find("pacman -U failed") ==
                            std::string::npos,
            "Cleanup partial-success was flattened to transaction failure");
    expect(
            scenario.workspace_paths.size() == 1 &&
                    scenario.install_attempt_order ==
                            std::vector<std::string>{"cleanup-partial"},
            "Cleanup partial-success started a later PackageBase");
    expect(
            fs::is_regular_file(
                    scenario.displaced_workspace_paths.at(0) /
                    "cleanup-partial-1.0-1-x86_64.pkg.tar.zst"),
            "Cleanup partial-success lost the installed artifact workspace");
    require_scenario_complete(
            scenario, 1, "production cleanup partial-success");
}

void test_needed_same_version_cleanup_failure_preserves_no_change(
        const TemporaryProductionEnvironment& environment) {
    AppConfig config = noninteractive_config();
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(make_work_item("cleanup-no-change"));
    work_items[0].request.needed = true;
    ProductionScenario scenario =
            make_execution_scenario(environment, work_items, config);
    scenario.units[0].metadata_mode =
            MetadataMode::ExistingExplicitSameVersion;
    scenario.units[0].replace_workspace_after_install = true;

    PreparedProductionSourceBuildInvocation invocation = prepare_execution(
            std::move(work_items), scenario);
    const CleanupErrorObservation cleanup_error = expect_cleanup_error(
            [&]() {
                static_cast<void>(execute_work_item(invocation, 0, scenario));
            },
            "same-version --needed cleanup failure");

    expect(
            cleanup_error.install_outcome ==
                    ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            "Cleanup error flattened same-version --needed skip to install");
    expect(
            cleanup_error.diagnostic.find("Package installation succeeded") !=
                    std::string::npos,
            "No-change cleanup error changed the existing diagnostic");
    expect(
            scenario.install_attempt_order ==
                    std::vector<std::string>{"cleanup-no-change"},
            "No-change cleanup failure did not reach successful pacman -U");
    expect(
            fs::is_regular_file(
                    scenario.displaced_workspace_paths.at(0) /
                    "cleanup-no-change-1.0-1-x86_64.pkg.tar.zst"),
            "No-change cleanup failure lost its diagnostic artifact workspace");
    require_scenario_complete(
            scenario, 1, "same-version --needed cleanup failure");
}

} // namespace

int main() {
    try {
        test_process_stub_rejects_cross_kind_reordering();
        test_build_plan_projection();
        TemporaryProductionEnvironment environment;
        test_rmdeps_global_rejection(environment);
        test_inherited_pkgdest_global_rejection(environment);
        test_later_target_pkgdest_global_rejection(environment);
        test_database_resolver_failure_stops_all_targets(environment);
        test_work_item_typed_install_outcomes(environment);
        test_single_aur_root_uses_shared_lifecycle(environment);
        test_multi_unit_options_roles_and_order(environment);
        test_build_failure_does_not_reach_sudo(environment);
        test_metadata_failure_does_not_reach_sudo(environment);
        test_pacman_failure_stops_later_unit(environment);
        test_cleanup_partial_success_stays_distinct_and_stops(environment);
        test_needed_same_version_cleanup_failure_preserves_no_change(
                environment);
        std::cout << "production source-build tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "production source-build test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
