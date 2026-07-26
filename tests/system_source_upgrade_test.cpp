#include "artifact_install_executor.hpp"
#include "system_source_upgrade.hpp"
#include "stubs/system-source-upgrade/phase_stub.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using SystemSourceUpgradeExecutor = SystemSourceUpgradeResult (*)(
        PreparedSystemSourceUpgrade,
        const AppConfig&,
        const SystemSourceUpgradeEventObserver&);

static_assert(!std::is_copy_constructible_v<PreparedSystemSourceUpgrade>);
static_assert(std::is_move_constructible_v<PreparedSystemSourceUpgrade>);
static_assert(
        std::is_same_v<
                decltype(&execute_prepared_system_source_upgrade),
                SystemSourceUpgradeExecutor>);
static_assert(
        std::is_invocable_v<
                SystemSourceUpgradeExecutor,
                PreparedSystemSourceUpgrade&&,
                const AppConfig&,
                const SystemSourceUpgradeEventObserver&>);
static_assert(
        !std::is_invocable_v<
                SystemSourceUpgradeExecutor,
                PreparedSystemSourceUpgrade&,
                const AppConfig&,
                const SystemSourceUpgradeEventObserver&>);

namespace {

namespace fs = std::filesystem;
namespace stub = system_source_upgrade_test_stub;

constexpr const char* SYSTEM_COMMAND =
        "sudo pacman '-Syu' '--noconfirm'";

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

AppConfig full_option_config() {
    AppConfig config;
    config.no_edit = true;
    config.no_diff = true;
    config.no_confirm = true;
    config.rebuild = true;
    config.clean_build = true;
    config.editor = "test-editor";
    return config;
}

stub::ConfigSnapshot snapshot_config(const AppConfig& config) {
    return stub::ConfigSnapshot{
            config.no_edit,
            config.no_diff,
            config.no_confirm,
            config.rebuild,
            config.clean_build,
            config.rm_deps,
            config.editor};
}

SourcePreferenceDirectorySnapshot preference_directory(
        const std::vector<std::string>& package_names,
        bool root_exists = true) {
    SourcePreferenceDirectorySnapshot snapshot;
    snapshot.root_exists = root_exists;
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        snapshot.entries.push_back(SourcePreferenceEntrySnapshot{
                index,
                fs::path("/preferences") / package_names[index],
                package_names[index],
                true});
    }
    return snapshot;
}

SourcePreferenceLoaded loaded_preference(
        const std::string& package_name,
        std::vector<std::string> warnings = {}) {
    SourceBuildEnvironment environment;
    environment.ordered_assignments.push_back(
            SourceEnvironmentAssignment{
                    "PACKAGE_NAME", package_name});
    return SourcePreferenceLoaded{
            fs::path("/preferences") / package_name,
            std::move(environment),
            std::move(warnings)};
}

InstalledPackageMetadata installed_package(
        const std::string& package_name,
        const std::string& version = "1.0-1") {
    return InstalledPackageMetadata{
            package_name,
            version,
            InstalledPackageReason::Explicit};
}

stub::MetadataSessionScript metadata_session(
        const std::vector<std::string>& package_names,
        LocalPackageVersionSnapshot local_snapshot,
        const std::string& installed_version = "1.0-1") {
    stub::MetadataSessionScript script;
    script.local_package_snapshot = std::move(local_snapshot);
    for(const auto& package_name : package_names) {
        script.installed_package_results.emplace(
                package_name,
                installed_package(package_name, installed_version));
    }
    return script;
}

SourceBuildExecutionResult source_execution(
        SourceBuildExecutionStatus status,
        std::string diagnostic = {}) {
    SourceBuildExecutionResult result;
    result.status = status;
    result.diagnostic = std::move(diagnostic);
    if(status == SourceBuildExecutionStatus::UpdateStatusUnknownSkipped) {
        result.update_status_unknown_skip_reason =
                SourceBuildUpdateStatusUnknownSkipReason::NoConfirm;
    }
    return result;
}

std::string progress_subject(const SystemSourceUpgradeEvent& event) {
    switch(event.kind) {
        case SystemSourceUpgradeEventKind::LoadingSourcePreference:
            return "load:" + event.preference_package_name.value_or("<missing>");
        case SystemSourceUpgradeEventKind::SourcePreferenceWarning:
            return "warning:" + event.diagnostic;
        case SystemSourceUpgradeEventKind::SystemUpgradeStarting:
            return "system-start";
        case SystemSourceUpgradeEventKind::CheckingSourcePackages:
            return "source-check";
        case SystemSourceUpgradeEventKind::InvalidPreferenceWarning:
            return "invalid:" +
                    event.preference_package_name.value_or("<missing>");
    }
    return "unknown";
}

const SystemSourceUpgradeEventObserver OBSERVER =
        [](const SystemSourceUpgradeEvent& event) {
            stub::record_progress(progress_subject(event));
        };

void configure_preferences(
        const std::vector<std::string>& package_names,
        bool add_first_warning = false) {
    stub::set_preference_directory(preference_directory(package_names));
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        std::vector<std::string> warnings;
        if(add_first_warning && index == 0) {
            warnings.push_back("scripted preference warning");
        }
        stub::enqueue_preference_result(
                package_names[index],
                loaded_preference(package_names[index], std::move(warnings)));
    }
}

PreparedSystemSourceUpgrade prepare_sources(
        const std::vector<std::string>& package_names,
        const AppConfig& config,
        LocalPackageVersionSnapshot before_snapshot =
                LocalPackageVersionSnapshot{{"core", "1.0-1"}},
        bool add_first_warning = false) {
    configure_preferences(package_names, add_first_warning);
    stub::enqueue_metadata_session(metadata_session(
            package_names, std::move(before_snapshot)));
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    expect(
            std::holds_alternative<PreparedSystemSourceUpgrade>(preparation),
            "Source preparation unexpectedly blocked");
    return std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
}

void enqueue_post_metadata(
        const std::vector<std::string>& package_names,
        LocalPackageVersionSnapshot after_snapshot =
                LocalPackageVersionSnapshot{{"core", "1.0-1"}}) {
    stub::enqueue_metadata_session(metadata_session(
            package_names, std::move(after_snapshot)));
}

SystemSourceUpgradeResult take_blocked(
        SystemSourceUpgradePreparation preparation) {
    expect(
            std::holds_alternative<SystemSourceUpgradeResult>(preparation),
            "Preparation unexpectedly returned an executable capability");
    return std::move(std::get<SystemSourceUpgradeResult>(preparation));
}

std::size_t event_position(
        stub::EventKind kind,
        const std::string& subject,
        std::size_t occurrence = 0) {
    std::size_t seen = 0;
    const auto& history = stub::events();
    for(std::size_t index = 0; index < history.size(); ++index) {
        if(history[index].kind != kind || history[index].subject != subject) {
            continue;
        }
        if(seen == occurrence) return index;
        ++seen;
    }
    throw std::runtime_error(
            "Missing event: " + subject + " occurrence " +
            std::to_string(occurrence));
}

void expect_source_order(
        const SystemSourceUpgradeResult& result,
        const std::vector<std::string>& expected_names) {
    expect(
            result.registered_source_results.size() == expected_names.size(),
            "Registered source result count differs from preference count");
    for(std::size_t index = 0; index < expected_names.size(); ++index) {
        const RegisteredSourceUpgradeResult& source =
                result.registered_source_results[index];
        expect(
                source.preference_package_name == expected_names[index],
                "Registered source result order changed");
        expect(
                source.original_preference_index == index,
                "Original preference index changed");
    }
}

void test_empty_registered_snapshot() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    AppConfig config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    expect(
            std::holds_alternative<PreparedSystemSourceUpgrade>(preparation),
            "Empty preference snapshot was not executable");
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    const SystemSourceUpgradePreparedSnapshot* snapshot = prepared.snapshot();
    expect(snapshot != nullptr, "Prepared snapshot is unavailable");
    expect(snapshot->preference_root_exists, "Existing empty root was lost");
    expect(snapshot->registered_sources.empty(), "Empty root gained sources");

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(result.is_success(), "System-only result is not successful");
    expect(
            result.system.status == SystemUpgradePhaseStatus::Completed,
            "System-only phase did not complete");
    expect(
            result.system.package_state_change == PackageStateChange::Unknown,
            "System-only phase guessed package state");
    expect(result.registered_source_results.empty(), "System-only result gained sources");
    expect(stub::system_commands() == std::vector<std::string>{SYSTEM_COMMAND},
           "System command changed");
    expect(stub::source_execution_calls().empty(), "System-only phase executed source");
    stub::require_script_consumed();
}

void test_all_sources_success_order_options_and_change() {
    stub::reset();
    const std::vector<std::string> packages = {"beta", "alpha", "gamma"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(
            packages, config,
            LocalPackageVersionSnapshot{{"core", "1.0-1"}},
            true);
    const SystemSourceUpgradePreparedSnapshot* prepared_snapshot =
            prepared.snapshot();
    expect(prepared_snapshot != nullptr,
           "Prepared source snapshot is unavailable before execution");
    expect(prepared_snapshot->options.no_edit == config.no_edit &&
                   prepared_snapshot->options.no_diff == config.no_diff &&
                   prepared_snapshot->options.no_confirm == config.no_confirm &&
                   prepared_snapshot->options.rebuild == config.rebuild &&
                   prepared_snapshot->options.clean_build ==
                           config.clean_build &&
                   prepared_snapshot->options.rm_deps == config.rm_deps &&
                   prepared_snapshot->options.editor == config.editor,
           "Options were not frozen during preparation");
    expect(prepared_snapshot->registered_sources.size() == packages.size(),
           "Prepared source intent count changed before execution");
    for(std::size_t index = 0; index < packages.size(); ++index) {
        const RegisteredSourcePreferenceSnapshot& source =
                prepared_snapshot->registered_sources[index];
        expect(source.preference_package_name == packages[index] &&
                       source.original_preference_index == index,
               "Preference order was not frozen during preparation");
        expect(source.environment.has_value() &&
                       source.environment->ordered_assignments.front().value ==
                               packages[index],
               "Strict preference environment was not owned during preparation");
        expect(source.canonical_source_identity_key ==
                       std::optional<std::string>(
                               "repository:" + packages[index]) &&
                       source.resolved_package_base ==
                               std::optional<std::string>(packages[index]),
               "Canonical source identity was not frozen during preparation");
    }
    expect(prepared_snapshot->registered_sources.front().
                   preference_load_warnings ==
                   std::vector<std::string>{"scripted preference warning"},
           "Preference warning was not retained in the prepared snapshot");
    enqueue_post_metadata(
            packages,
            LocalPackageVersionSnapshot{{"core", "2.0-1"}});
    for(std::size_t index = 0; index < packages.size(); ++index) {
        stub::enqueue_source_success(source_execution(
                SourceBuildExecutionStatus::Installed));
    }

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect_source_order(result, packages);
    expect(result.is_success(), "All-source success helper returned false");
    expect(
            result.system.package_state_change == PackageStateChange::Changed,
            "System name/version delta was not Changed");
    expect(
            result.package_state_change() == PackageStateChange::Changed,
            "Aggregate package-state change was not Changed");
    expect(result.definitely_changed_package_state(),
           "Strict changed helper returned false");
    expect(!result.has_partial_completion(),
           "Complete result reported partial completion");
    expect(result.warnings.size() == 1,
           "Preference warning was not retained exactly once");

    const auto& calls = stub::source_execution_calls();
    expect(calls.size() == packages.size(), "Source executor call count changed");
    for(std::size_t index = 0; index < packages.size(); ++index) {
        expect(calls[index].package_name == packages[index],
               "Source executor order changed");
        expect(calls[index].package_base == packages[index],
               "Resolved PackageBase was not forwarded");
        expect(calls[index].config == snapshot_config(config),
               "Source option propagation changed");
        expect(calls[index].request.only_if_updated,
               "Upgrade source work item lost only-if-updated");
        expect(!calls[index].request.needed,
               "Upgrade source work item invented --needed");
        expect(
                calls[index].request.custom_environment.
                        ordered_assignments.front().value == packages[index],
                "Strict preference environment was not forwarded");
        expect(
                result.registered_source_results[index].
                        canonical_source_identity_key ==
                        std::optional<std::string>(
                                "repository:" + packages[index]),
                "Canonical source identity was not retained");
        expect(
                result.registered_source_results[index].status ==
                        RegisteredSourceUpgradeStatus::Updated,
                "Installed source did not map to Updated");
    }
    expect(stub::supported_option_configs() ==
                   std::vector<stub::ConfigSnapshot>{snapshot_config(config)},
           "Supported-option guard did not receive the exact config");
    expect(stub::invocation_configs() ==
                   std::vector<stub::ConfigSnapshot>{snapshot_config(config)},
           "Invocation preparation did not receive the exact config");
    expect(stub::system_commands() == std::vector<std::string>{SYSTEM_COMMAND},
           "System phase received source-only options");

    expect(
            event_position(stub::EventKind::LocalPackageSnapshot,
                           "local-packages", 0) <
                    event_position(stub::EventKind::Progress, "system-start"),
            "Pre-system package snapshot occurred after system mutation");
    expect(
            event_position(stub::EventKind::SystemCommand, SYSTEM_COMMAND) <
                    event_position(stub::EventKind::Progress, "source-check"),
            "Source check started before system command");
    expect(
            event_position(stub::EventKind::InstalledPackageQuery, "gamma", 1) <
                    event_position(stub::EventKind::SourceExecution, "beta"),
            "Source execution started before all post-system queries completed");
    expect(
            event_position(stub::EventKind::SourceExecution, "beta") <
                    event_position(stub::EventKind::SourceExecution, "alpha") &&
            event_position(stub::EventKind::SourceExecution, "alpha") <
                    event_position(stub::EventKind::SourceExecution, "gamma"),
            "External source mutation order changed");
    stub::require_script_consumed();
}

void test_source_options_propagate_independently() {
    struct OptionCase {
        const char* name;
        void (*enable)(AppConfig& config);
    };
    const std::vector<OptionCase> cases = {
            {"noedit", [](AppConfig& config) { config.no_edit = true; }},
            {"nodiff", [](AppConfig& config) { config.no_diff = true; }},
            {"noconfirm", [](AppConfig& config) {
                 config.no_confirm = true;
             }},
            {"rebuild", [](AppConfig& config) { config.rebuild = true; }},
            {"cleanbuild", [](AppConfig& config) {
                 config.clean_build = true;
             }},
    };

    for(const auto& option_case : cases) {
        stub::reset();
        AppConfig config;
        config.editor = "option-test-editor";
        option_case.enable(config);
        PreparedSystemSourceUpgrade prepared =
                prepare_sources({"source"}, config);
        enqueue_post_metadata({"source"});
        stub::enqueue_source_success(source_execution(
                SourceBuildExecutionStatus::UpToDate));

        SystemSourceUpgradeResult result =
                execute_prepared_system_source_upgrade(
                        std::move(prepared), config, OBSERVER);
        expect(result.status == SystemSourceUpgradeStatus::Completed,
               std::string("Independent option case failed: ") +
                       option_case.name);
        expect(stub::source_execution_calls().size() == 1 &&
                       stub::source_execution_calls().front().config ==
                               snapshot_config(config),
               std::string("Source option propagation was coupled: ") +
                       option_case.name);
        const std::string expected_system_command = config.no_confirm
                ? "sudo pacman '-Syu' '--noconfirm'"
                : "sudo pacman '-Syu'";
        expect(stub::system_commands() ==
                       std::vector<std::string>{expected_system_command},
               std::string("Source-only option leaked to system command: ") +
                       option_case.name);
        stub::require_script_consumed();
    }
}

void test_nonregular_entries_preserve_original_preference_index() {
    stub::reset();
    SourcePreferenceDirectorySnapshot directory;
    directory.root_exists = true;
    directory.entries = {
            SourcePreferenceEntrySnapshot{
                    3, "/preferences/not-a-file", "not-a-file", false},
            SourcePreferenceEntrySnapshot{
                    8, "/preferences/source", "source", true},
    };
    stub::set_preference_directory(std::move(directory));
    stub::enqueue_preference_result(
            "source", loaded_preference("source"));
    stub::enqueue_metadata_session(metadata_session(
            {"source"}, LocalPackageVersionSnapshot{{"core", "1.0-1"}}));
    AppConfig config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    expect(std::holds_alternative<PreparedSystemSourceUpgrade>(preparation),
           "Non-regular preference entry blocked preparation");
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    const SystemSourceUpgradePreparedSnapshot* snapshot = prepared.snapshot();
    expect(snapshot != nullptr && snapshot->registered_sources.size() == 1 &&
                   snapshot->registered_sources.front().
                           original_preference_index == 8,
           "Preparation renumbered original preference index");
    enqueue_post_metadata({"source"});
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::UpToDate));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(result.registered_source_results.size() == 1 &&
                   result.registered_source_results.front().
                           original_preference_index == 8,
           "Result renumbered original preference index");
    stub::require_script_consumed();
}

void test_source_no_change_and_package_state_no_change() {
    stub::reset();
    const std::vector<std::string> packages = {"stable"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::UpToDate,
            "stable is up to date"));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(result.is_success(), "No-change phase is not successful");
    expect(
            result.registered_source_results.front().status ==
                    RegisteredSourceUpgradeStatus::NoChange,
            "UpToDate did not map to NoChange");
    expect(
            result.package_state_change() == PackageStateChange::NoChange,
            "Equal system/source snapshots did not reduce to NoChange");
    expect(!result.definitely_changed_package_state(),
           "NoChange was reported as definitely changed");
    stub::require_script_consumed();
}

void test_system_failure_keeps_all_sources_not_attempted() {
    stub::reset();
    const std::vector<std::string> packages = {"first", "second", "third"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    stub::set_system_command_exit_status(42);

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::StoppedOnSystemFailure,
            "System failure did not stop the phase");
    expect(
            result.system.status == SystemUpgradePhaseStatus::Failed,
            "System failure was not typed Failed");
    expect(result.system.command_exit_status == std::optional<int>(42),
           "System command exit status was lost");
    expect(result.stopped_phase == SystemSourceUpgradePhase::System,
           "System failure stopped phase was not retained");
    expect(result.failure_diagnostic() ==
                   std::optional<std::string>("Update failed."),
           "System failure diagnostic was lost");
    expect(
            result.system.package_state_change == PackageStateChange::Unknown,
            "Failed pacman transaction guessed package state");
    expect_source_order(result, packages);
    expect(result.has_not_attempted_sources(),
           "System failure lost NotAttempted sources");
    expect(std::all_of(
                   result.registered_source_results.begin(),
                   result.registered_source_results.end(),
                   [](const RegisteredSourceUpgradeResult& source) {
                       return source.status ==
                               RegisteredSourceUpgradeStatus::NotAttempted;
                   }),
           "System failure changed a source status");
    expect(stub::source_execution_calls().empty(),
           "System failure reached source mutation");
    expect(
            std::none_of(
                    stub::events().begin(), stub::events().end(),
                    [](const stub::Event& event) {
                        return event.kind == stub::EventKind::Progress &&
                               event.subject == "source-check";
                    }),
            "System failure reached post-system source check");
    stub::require_script_consumed();
}

void test_first_source_failure_stops_suffix() {
    stub::reset();
    const std::vector<std::string> packages = {"first", "second", "third"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_failure("scripted first source failure");

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::StoppedOnSourceFailure,
            "First source failure did not stop source phase");
    expect(
            result.registered_source_results[0].status ==
                    RegisteredSourceUpgradeStatus::Failed,
            "Failing source was not Failed");
    expect(
            result.registered_source_results[0].package_state_change ==
                    PackageStateChange::Unknown,
            "Ordinary source failure guessed package state");
    expect(
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::NotAttempted &&
            result.registered_source_results[2].status ==
                    RegisteredSourceUpgradeStatus::NotAttempted,
            "Suffix sources were not retained as NotAttempted");
    expect(stub::source_execution_calls().size() == 1,
           "Source executor continued after first failure");
    expect(result.has_partial_completion(),
           "Completed system + source failure lost partial completion");
    expect(result.failure_diagnostic() ==
                   std::optional<std::string>(
                           "scripted first source failure"),
           "Source failure diagnostic was lost");
    stub::require_script_consumed();
}

void test_partial_source_completion() {
    stub::reset();
    const std::vector<std::string> packages = {"first", "second", "third"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::Installed));
    stub::enqueue_source_failure("scripted second source failure");

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.registered_source_results[0].status ==
                    RegisteredSourceUpgradeStatus::Updated &&
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::Failed &&
            result.registered_source_results[2].status ==
                    RegisteredSourceUpgradeStatus::NotAttempted,
            "Partial source completion statuses are inconsistent");
    expect(result.package_state_change() == PackageStateChange::Changed,
           "Known prior update was hidden by later Unknown failure");
    expect(result.has_partial_completion(),
           "Partial source completion helper returned false");
    stub::require_script_consumed();
}

void test_cleanup_failure_preserves_transaction_outcome() {
    stub::reset();
    const std::vector<std::string> packages = {"first", "second"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_cleanup_failure(
            ArtifactInstallExecutionOutcome::Installed,
            "scripted cleanup failure");

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    const RegisteredSourceUpgradeResult& failed =
            result.registered_source_results.front();
    expect(
            result.status == SystemSourceUpgradeStatus::
                    StoppedAfterSourceCleanupFailure,
            "Cleanup failure did not retain aggregate status");
    expect(
            failed.status ==
                    RegisteredSourceUpgradeStatus::UpdatedCleanupFailed,
            "Installed cleanup failure lost Updated outcome");
    expect(failed.package_state_change == PackageStateChange::Changed,
           "Installed cleanup failure lost package-state change");
    expect(failed.cleanup_diagnostic ==
                   std::optional<std::string>("scripted cleanup failure"),
           "Cleanup diagnostic was lost");
    expect(
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::NotAttempted,
            "Cleanup failure executed a later source");
    expect(result.has_cleanup_failure(),
           "Cleanup helper returned false");
    expect(result.has_partial_completion(),
           "Cleanup partial completion helper returned false");
    stub::require_script_consumed();
}

void test_no_change_cleanup_failure() {
    stub::reset();
    const std::vector<std::string> packages = {"needed-skip"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_cleanup_failure(
            ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            "scripted no-change cleanup failure");

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.registered_source_results.front().status ==
                    RegisteredSourceUpgradeStatus::NoChangeCleanupFailed,
            "SkippedAsNeeded cleanup failure lost NoChange outcome");
    expect(
            result.registered_source_results.front().package_state_change ==
                    PackageStateChange::NoChange,
            "No-change cleanup failure guessed a package change");
    expect(result.has_cleanup_failure(),
           "No-change cleanup helper returned false");
    stub::require_script_consumed();
}

void test_preference_read_failure_blocks_before_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({"broken"}));
    SourcePreferenceFailure failure{
            SourcePreferenceFailureKind::ReadFailed,
            fs::path("/preferences/broken"),
            std::make_error_code(std::errc::io_error),
            std::nullopt,
            "scripted strict preference read failure"};
    stub::enqueue_preference_result("broken", failure);

    SystemSourceUpgradeResult result = take_blocked(
            prepare_system_source_upgrade(full_option_config(), OBSERVER));
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::BlockedBeforeMutation,
            "Preference failure did not block preparation");
    expect(
            result.system.status == SystemUpgradePhaseStatus::NotAttempted,
            "Preference failure attempted the system phase");
    expect(
            result.registered_source_results.front().status ==
                    RegisteredSourceUpgradeStatus::Incomplete,
            "Preference failure was not retained as Incomplete");
    expect(
            result.issues.front().source_preference_failure.has_value(),
            "Typed preference failure was lost");
    expect(result.has_blocking_issue(),
           "Preference failure helper did not report a blocker");
    expect(stub::system_commands().empty(),
           "Preference failure reached system mutation");
    expect(stub::source_execution_calls().empty(),
           "Preference failure reached source mutation");
    stub::require_script_consumed();
}

void test_registered_preference_disappearance_blocks_before_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({"vanished"}));
    stub::enqueue_preference_result(
            "vanished", SourcePreferenceAbsent{});

    SystemSourceUpgradeResult result = take_blocked(
            prepare_system_source_upgrade(full_option_config(), OBSERVER));
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::BlockedBeforeMutation,
            "Disappeared registered preference did not block preparation");
    expect(
            result.registered_source_results.front().status ==
                    RegisteredSourceUpgradeStatus::Incomplete,
            "Disappeared preference was not retained as Incomplete");
    expect(result.failure_diagnostic() ==
                   std::optional<std::string>(
                           "Registered source preference disappeared before preparation: /preferences/vanished"),
           "Disappeared preference diagnostic was lost");
    expect(stub::system_commands().empty(),
           "Disappeared preference reached system mutation");
    stub::require_script_consumed();
}

void test_known_package_base_survives_later_preparation_failure() {
    stub::reset();
    configure_preferences({"split"});
    stub::set_source_identity(
            "split",
            ResolvedSourceBuildIdentity{
                    "split",
                    "known-base",
                    "aur:known-base",
                    "https://aur.example/known-base.git",
                    SourceBuildSourceKind::Aur,
                    true});
    stub::fail_source_work_item(
            "split", "scripted post-identity preparation failure");

    SystemSourceUpgradeResult result = take_blocked(
            prepare_system_source_upgrade(full_option_config(), OBSERVER));
    const RegisteredSourceUpgradeResult& source =
            result.registered_source_results.front();
    expect(source.resolved_package_base ==
                   std::optional<std::string>("known-base"),
           "Known PackageBase was lost after later failure");
    expect(source.canonical_source_identity_key ==
                   std::optional<std::string>("aur:known-base"),
           "Known canonical source key was lost after later failure");
    expect(source.diagnostic ==
                   std::optional<std::string>(
                           "scripted post-identity preparation failure"),
           "Post-identity failure diagnostic was lost");
    expect(stub::system_commands().empty(),
           "Post-identity preparation failure reached mutation");
    stub::require_script_consumed();
}

void test_system_snapshot_failure_is_unknown_and_nonblocking() {
    stub::reset();
    const std::vector<std::string> packages = {"source"};
    configure_preferences(packages);
    stub::MetadataSessionScript before = metadata_session(packages, {});
    before.local_package_snapshot = PackageMetadataFailure{
            PackageMetadataErrorCode::MalformedMetadata,
            "scripted pre-system snapshot failure"};
    stub::enqueue_metadata_session(std::move(before));
    AppConfig config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    expect(
            std::holds_alternative<PreparedSystemSourceUpgrade>(preparation),
            "System observability failure blocked source preparation");
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    enqueue_post_metadata(packages);
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::UpToDate));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.system.status == SystemUpgradePhaseStatus::Completed,
            "Snapshot failure changed successful system command status");
    expect(
            result.system.package_state_change == PackageStateChange::Unknown,
            "Snapshot failure was rounded to a boolean state");
    expect(result.package_state_change() == PackageStateChange::Unknown,
           "Aggregate pre-system snapshot failure was not Unknown");
    expect(!result.definitely_changed_package_state(),
           "Unknown pre-system snapshot was reported as definitely changed");
    expect(result.system.before_snapshot_failure.has_value(),
           "Pre-system snapshot failure was not retained");
    expect(!result.is_success(),
           "Pre-system snapshot failure was rounded to aggregate success");
    expect(!result.has_blocking_issue(),
           "Observability-only snapshot failure became a blocker");
    expect(stub::source_execution_calls().size() == 1,
           "System snapshot failure blocked valid source execution");
    stub::require_script_consumed();
}

void test_post_system_snapshot_failure_is_unknown_and_nonblocking() {
    stub::reset();
    const std::vector<std::string> packages = {"source"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    stub::MetadataSessionScript after = metadata_session(packages, {});
    after.local_package_snapshot = PackageMetadataFailure{
            PackageMetadataErrorCode::MalformedMetadata,
            "scripted post-system snapshot failure"};
    stub::enqueue_metadata_session(std::move(after));
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::UpToDate));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.system.package_state_change == PackageStateChange::Unknown,
            "Post-system snapshot failure was not Unknown");
    expect(result.package_state_change() == PackageStateChange::Unknown,
           "Aggregate post-system snapshot failure was not Unknown");
    expect(!result.definitely_changed_package_state(),
           "Unknown post-system snapshot was reported as definitely changed");
    expect(result.system.after_snapshot_failure.has_value(),
           "Post-system snapshot failure was lost");
    expect(!result.is_success(),
           "Post-system snapshot failure was rounded to aggregate success");
    expect(result.status == SystemSourceUpgradeStatus::Completed,
           "Observability failure stopped source execution");
    expect(result.registered_source_results.front().status ==
                   RegisteredSourceUpgradeStatus::NoChange,
           "Source result was lost after system snapshot failure");
    stub::require_script_consumed();
}

void test_post_source_metadata_failure_stops_all_source_mutation() {
    stub::reset();
    const std::vector<std::string> packages = {"first", "second"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    stub::MetadataSessionScript after = metadata_session(
            packages, LocalPackageVersionSnapshot{{"core", "1.0-1"}});
    after.installed_package_results["second"] = PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "scripted post-system query failure"};
    stub::enqueue_metadata_session(std::move(after));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::StoppedOnSourceFailure,
            "Post-system source metadata failure did not stop phase");
    expect(
            result.registered_source_results[0].status ==
                    RegisteredSourceUpgradeStatus::NotAttempted &&
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::Incomplete,
            "Post-system metadata result attribution is inconsistent");
    expect(stub::source_execution_calls().empty(),
           "Post-system metadata failure allowed source mutation");
    expect(result.has_partial_completion(),
           "System-completed metadata stop lost partial completion");
    stub::require_script_consumed();
}

void test_update_status_unknown_is_incomplete_but_continues() {
    stub::reset();
    const std::vector<std::string> packages = {"unknown", "later"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    enqueue_post_metadata(packages);
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::UpdateStatusUnknownSkipped,
            "scripted unknown update status"));
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::Installed));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(result.status == SystemSourceUpgradeStatus::Completed,
           "Normal unknown-status skip stopped the phase");
    expect(
            result.registered_source_results[0].status ==
                    RegisteredSourceUpgradeStatus::Incomplete &&
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::Updated,
            "Unknown-status skip result/continue policy changed");
    expect(!result.is_success(),
           "Incomplete source was rounded to aggregate success");
    expect(result.has_partial_completion(),
           "Incomplete+updated result lost partial completion");
    expect(result.diagnostics.back().diagnostic ==
                   "scripted unknown update status",
           "Unknown-status diagnostic was lost");
    stub::require_script_consumed();
}

void test_invalid_preference_is_unsupported_and_continues() {
    stub::reset();
    const std::vector<std::string> entries = {"bad name", "valid"};
    stub::set_preference_directory(preference_directory(entries));
    stub::enqueue_preference_result("valid", loaded_preference("valid"));
    stub::enqueue_metadata_session(metadata_session(
            {"valid"}, LocalPackageVersionSnapshot{{"core", "1.0-1"}}));
    AppConfig config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    expect(
            std::holds_alternative<PreparedSystemSourceUpgrade>(preparation),
            "Invalid filename blocked system preparation");
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    enqueue_post_metadata({"valid"});
    stub::enqueue_source_success(source_execution(
            SourceBuildExecutionStatus::Installed));

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect_source_order(result, entries);
    expect(
            result.registered_source_results[0].status ==
                    RegisteredSourceUpgradeStatus::Unsupported &&
            result.registered_source_results[1].status ==
                    RegisteredSourceUpgradeStatus::Updated,
            "Invalid preference continue policy changed");
    expect(!result.is_success(),
           "Unsupported preference was rounded to success");
    expect(stub::source_execution_calls().size() == 1,
           "Invalid preference changed valid source execution count");
    expect(event_position(stub::EventKind::Progress, "invalid:bad name") <
                   event_position(stub::EventKind::SourceExecution, "valid"),
           "Invalid warning moved after later source mutation");
    stub::require_script_consumed();
}

void test_prepared_capability_replay_is_rejected() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    AppConfig config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(config, OBSERVER);
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    SystemSourceUpgradeResult first =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(first.status == SystemSourceUpgradeStatus::Completed,
           "First capability execution failed");
    expect(!prepared.is_valid(),
           "Caller capability remained valid after consume");

    SystemSourceUpgradeResult replay =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            replay.status ==
                    SystemSourceUpgradeStatus::InconsistentResult,
            "Capability replay was not rejected");
    expect(replay.has_blocking_issue(),
           "Capability replay lacks a blocking issue");
    expect(stub::system_commands().size() == 1,
           "Capability replay reached a second system mutation");
    stub::require_script_consumed();
}

void test_option_mismatch_is_rejected_before_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    AppConfig prepared_config = full_option_config();
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(prepared_config, OBSERVER);
    PreparedSystemSourceUpgrade prepared = std::move(
            std::get<PreparedSystemSourceUpgrade>(preparation));
    AppConfig execution_config = prepared_config;
    execution_config.no_confirm = false;

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), execution_config, OBSERVER);
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::InconsistentResult,
            "Option mismatch was not rejected");
    expect(result.system.status == SystemUpgradePhaseStatus::NotAttempted,
           "Option mismatch reached system phase");
    expect(stub::system_commands().empty(),
           "Option mismatch reached system mutation");
    stub::require_script_consumed();
}

void test_inconsistent_correlation_is_rejected_before_mutation() {
    stub::reset();
    const std::vector<std::string> packages = {"source"};
    AppConfig config = full_option_config();
    PreparedSystemSourceUpgrade prepared = prepare_sources(packages, config);
    prepared.make_first_source_correlation_inconsistent_for_test();

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(prepared), config, OBSERVER);
    expect(
            result.status ==
                    SystemSourceUpgradeStatus::InconsistentResult,
            "Inconsistent correlation was not rejected");
    expect(result.system.status == SystemUpgradePhaseStatus::NotAttempted,
           "Inconsistent correlation reached system phase");
    expect(stub::system_commands().empty(),
           "Inconsistent correlation reached system mutation");
    expect(result.has_not_attempted_sources(),
           "Inconsistent correlation lost NotAttempted source");
    stub::require_script_consumed();
}

void test_preference_enumeration_failure_blocks() {
    stub::reset();
    stub::fail_preference_directory("scripted preference enumeration failure");
    SystemSourceUpgradeResult result = take_blocked(
            prepare_system_source_upgrade(full_option_config(), OBSERVER));
    expect(result.status ==
                   SystemSourceUpgradeStatus::BlockedBeforeMutation,
           "Preference enumeration failure did not block");
    expect(result.failure_diagnostic() ==
                   std::optional<std::string>(
                           "scripted preference enumeration failure"),
           "Preference enumeration diagnostic was lost");
    expect(stub::system_commands().empty(),
           "Preference enumeration failure reached mutation");
    stub::require_script_consumed();
}

template<typename Function>
void run_case(
        const std::string& name,
        Function&& function,
        std::size_t& completed_cases) {
    function();
    ++completed_cases;
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        std::size_t completed_cases = 0;
        run_case(
                "empty registered source snapshot",
                test_empty_registered_snapshot,
                completed_cases);
        run_case(
                "all sources success, order, options, and package change",
                test_all_sources_success_order_options_and_change,
                completed_cases);
        run_case(
                "source options propagate independently",
                test_source_options_propagate_independently,
                completed_cases);
        run_case(
                "non-regular entries preserve original preference index",
                test_nonregular_entries_preserve_original_preference_index,
                completed_cases);
        run_case(
                "source no-change and aggregate no-change",
                test_source_no_change_and_package_state_no_change,
                completed_cases);
        run_case(
                "system failure leaves every source NotAttempted",
                test_system_failure_keeps_all_sources_not_attempted,
                completed_cases);
        run_case(
                "first source failure stops suffix",
                test_first_source_failure_stops_suffix,
                completed_cases);
        run_case(
                "partial source completion",
                test_partial_source_completion,
                completed_cases);
        run_case(
                "updated cleanup failure",
                test_cleanup_failure_preserves_transaction_outcome,
                completed_cases);
        run_case(
                "no-change cleanup failure",
                test_no_change_cleanup_failure,
                completed_cases);
        run_case(
                "preference read failure blocks mutation",
                test_preference_read_failure_blocks_before_mutation,
                completed_cases);
        run_case(
                "registered preference disappearance blocks mutation",
                test_registered_preference_disappearance_blocks_before_mutation,
                completed_cases);
        run_case(
                "known PackageBase survives preparation failure",
                test_known_package_base_survives_later_preparation_failure,
                completed_cases);
        run_case(
                "pre-system package snapshot failure is Unknown",
                test_system_snapshot_failure_is_unknown_and_nonblocking,
                completed_cases);
        run_case(
                "post-system package snapshot failure is Unknown",
                test_post_system_snapshot_failure_is_unknown_and_nonblocking,
                completed_cases);
        run_case(
                "post-system source metadata failure blocks all source mutation",
                test_post_source_metadata_failure_stops_all_source_mutation,
                completed_cases);
        run_case(
                "unknown update status is Incomplete and continues",
                test_update_status_unknown_is_incomplete_but_continues,
                completed_cases);
        run_case(
                "invalid preference is Unsupported and continues",
                test_invalid_preference_is_unsupported_and_continues,
                completed_cases);
        run_case(
                "prepared capability replay is rejected",
                test_prepared_capability_replay_is_rejected,
                completed_cases);
        run_case(
                "option mismatch is rejected before mutation",
                test_option_mismatch_is_rejected_before_mutation,
                completed_cases);
        run_case(
                "inconsistent correlation is rejected before mutation",
                test_inconsistent_correlation_is_rejected_before_mutation,
                completed_cases);
        run_case(
                "preference enumeration failure blocks",
                test_preference_enumeration_failure_blocks,
                completed_cases);
        std::cout << "system/source upgrade tests: " << completed_cases
                  << " scenarios passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "system/source upgrade test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
