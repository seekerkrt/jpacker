#include "system_source_upgrade.hpp"

#include "app_config.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "separated_source_build.hpp"
#include "shell_words.hpp"
#include "source_build.hpp"
#include "source_install.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr const char* POST_UPGRADE_SNAPSHOT_FAILURE_PREFIX =
        "System upgrade completed, but post-upgrade package metadata snapshot failed: ";
constexpr const char* UNKNOWN_SYSTEM_FAILURE_DIAGNOSTIC =
        "System upgrade failed with an unknown exception.";
constexpr const char* UNKNOWN_SOURCE_FAILURE_DIAGNOSTIC =
        "Registered source package processing failed with an unknown exception.";

using SourceUpdateBaselines = std::map<std::string, SourceUpdateBaseline>;
using SourceInstalledSnapshotResult = std::variant<
        SourceInstalledSnapshot,
        PackageMetadataFailure>;
using SourceInstalledSnapshotResults =
        std::map<std::string, SourceInstalledSnapshotResult>;

struct RegisteredSourceCorrelation {
    std::size_t snapshot_index = 0;
    bool        has_valid_package_name = false;
    std::optional<std::size_t> work_item_index;
};

struct SystemSourceUpgradePreparationState {
    SystemSourceUpgradePreparedSnapshot snapshot;
    std::optional<PreparedProductionSourceBuildInvocation> source_invocation;
    std::vector<RegisteredSourceCorrelation> correlations;
    SourceUpdateBaselines update_baselines;
    std::optional<PacmanDatabasePaths> system_database_paths;
    std::optional<LocalPackageVersionSnapshot> before_system_snapshot;
    SystemUpgradePhaseResult system;
    std::vector<SystemSourceUpgradeWarning> warnings;
    std::vector<SystemSourceUpgradeIssue> issues;
    std::vector<SystemSourceUpgradeDiagnostic> diagnostics;
};

struct SourceBaselineFailure {
    std::size_t package_index = 0;
    PackageMetadataFailure failure;
};

void notify(
        const SystemSourceUpgradeEventObserver& observer,
        SystemSourceUpgradeEvent event) {
    if(observer) observer(event);
}

SystemSourceUpgradeOptionSnapshot snapshot_options(const AppConfig& config) {
    return SystemSourceUpgradeOptionSnapshot{
            config.no_edit,
            config.no_diff,
            config.no_confirm,
            config.rebuild,
            config.clean_build,
            config.rm_deps,
            config.editor};
}

bool options_match(
        const SystemSourceUpgradeOptionSnapshot& snapshot,
        const AppConfig& config) noexcept {
    return snapshot.no_edit == config.no_edit &&
           snapshot.no_diff == config.no_diff &&
           snapshot.no_confirm == config.no_confirm &&
           snapshot.rebuild == config.rebuild &&
           snapshot.clean_build == config.clean_build &&
           snapshot.rm_deps == config.rm_deps &&
           snapshot.editor == config.editor;
}

std::string system_upgrade_command(const AppConfig& config) {
    std::vector<std::string> arguments = {"-Syu"};
    if(config.no_confirm) arguments.push_back("--noconfirm");
    return "sudo pacman " + shell_words::join(arguments);
}

SourceInstalledSnapshotResult map_source_installed_snapshot(
        const InstalledPackageQueryResult& result) {
    if(const auto* metadata = std::get_if<InstalledPackageMetadata>(&result)) {
        return SourceInstalledSnapshot{metadata->version};
    }
    if(std::holds_alternative<PackageNotFound>(result)) {
        return SourceInstalledSnapshot{std::nullopt};
    }
    return std::get<PackageMetadataFailure>(result);
}

std::optional<SourceBaselineFailure> snapshot_source_update_baselines(
        const PackageMetadataSession& session,
        const std::vector<std::string>& package_names,
        SourceUpdateBaselines& baselines) {
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        const std::string& package_name = package_names[index];
        SourceInstalledSnapshotResult snapshot_result =
                map_source_installed_snapshot(
                        session.query_installed_package(package_name));
        if(const auto* failure =
                   std::get_if<PackageMetadataFailure>(&snapshot_result)) {
            return SourceBaselineFailure{
                    index,
                    PackageMetadataFailure{
                            failure->code,
                            "Failed to query installed package metadata for " +
                                    package_name + ": " + failure->diagnostic}};
        }

        const auto& snapshot =
                std::get<SourceInstalledSnapshot>(snapshot_result);
        baselines.emplace(
                package_name,
                SourceUpdateBaseline{snapshot.installed_version});
    }
    return std::nullopt;
}

SourceInstalledSnapshotResults snapshot_post_upgrade_installed_packages(
        const PackageMetadataSession& session,
        const std::vector<std::string>& package_names) {
    SourceInstalledSnapshotResults snapshots;
    for(const auto& package_name : package_names) {
        snapshots.emplace(
                package_name,
                map_source_installed_snapshot(
                        session.query_installed_package(package_name)));
    }
    return snapshots;
}

RegisteredSourceUpgradeResult make_not_attempted_source_result(
        const RegisteredSourcePreferenceSnapshot& source) {
    return RegisteredSourceUpgradeResult{
            source.original_preference_index,
            source.preference_package_name,
            source.canonical_source_identity_key,
            source.resolved_package_base,
            RegisteredSourceUpgradeStatus::NotAttempted,
            RegisteredSourceUpgradeFailureKind::PriorPhaseStopped,
            PackageStateChange::NoChange,
            std::nullopt,
            std::nullopt};
}

SystemSourceUpgradeResult make_result_from_state(
        SystemSourceUpgradePreparationState&& state,
        SystemSourceUpgradeStatus status,
        SystemSourceUpgradePhase stopped_phase) {
    SystemSourceUpgradeResult result;
    result.status = status;
    result.stopped_phase = stopped_phase;
    result.prepared_snapshot = std::move(state.snapshot);
    result.system = std::move(state.system);
    result.warnings = std::move(state.warnings);
    result.issues = std::move(state.issues);
    result.diagnostics = std::move(state.diagnostics);
    result.registered_source_results.reserve(
            result.prepared_snapshot.registered_sources.size());
    for(const auto& source : result.prepared_snapshot.registered_sources) {
        result.registered_source_results.push_back(
                make_not_attempted_source_result(source));
    }
    return result;
}

SystemSourceUpgradeIssue make_issue(
        SystemSourceUpgradeIssueKind kind,
        SystemSourceUpgradeIssueImpact impact,
        SystemSourceUpgradePhase phase,
        std::string diagnostic) {
    SystemSourceUpgradeIssue issue;
    issue.kind = kind;
    issue.impact = impact;
    issue.phase = phase;
    issue.diagnostic = std::move(diagnostic);
    return issue;
}

void attribute_issue_to_source(
        SystemSourceUpgradeIssue& issue,
        const RegisteredSourcePreferenceSnapshot& source) {
    issue.original_preference_index = source.original_preference_index;
    issue.preference_package_name = source.preference_package_name;
    issue.resolved_package_base = source.resolved_package_base;
}

SystemSourceUpgradeDiagnostic make_diagnostic(
        SystemSourceUpgradePhase phase,
        std::string diagnostic,
        bool stops_execution) {
    SystemSourceUpgradeDiagnostic detail;
    detail.phase = phase;
    detail.stops_execution = stops_execution;
    detail.diagnostic = std::move(diagnostic);
    return detail;
}

SystemSourceUpgradeResult block_preparation(
        SystemSourceUpgradePreparationState&& state,
        SystemSourceUpgradeIssue issue,
        std::optional<std::size_t> source_position,
        RegisteredSourceUpgradeFailureKind source_failure_kind) {
    SystemSourceUpgradeDiagnostic diagnostic = make_diagnostic(
            SystemSourceUpgradePhase::Preparation,
            issue.diagnostic,
            true);
    diagnostic.original_preference_index = issue.original_preference_index;
    diagnostic.preference_package_name = issue.preference_package_name;
    diagnostic.resolved_package_base = issue.resolved_package_base;
    state.diagnostics.push_back(std::move(diagnostic));
    state.issues.push_back(std::move(issue));

    SystemSourceUpgradeResult result = make_result_from_state(
            std::move(state),
            SystemSourceUpgradeStatus::BlockedBeforeMutation,
            SystemSourceUpgradePhase::Preparation);
    if(source_position.has_value() &&
       source_position.value() < result.registered_source_results.size()) {
        RegisteredSourceUpgradeResult& source_result =
                result.registered_source_results[source_position.value()];
        source_result.status = RegisteredSourceUpgradeStatus::Incomplete;
        source_result.failure_kind = source_failure_kind;
        source_result.diagnostic = result.diagnostics.back().diagnostic;
    }
    return result;
}

void record_system_snapshot_failure(
        SystemSourceUpgradePreparationState& state,
        PackageMetadataFailure failure) {
    const std::string diagnostic =
            "Failed to snapshot local package versions before system upgrade: " +
            failure.diagnostic;
    state.system.before_snapshot_failure = failure;

    SystemSourceUpgradeIssue issue = make_issue(
            SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable,
            SystemSourceUpgradeIssueImpact::ObservabilityOnly,
            SystemSourceUpgradePhase::System,
            diagnostic);
    issue.package_metadata_failure = std::move(failure);
    state.issues.push_back(std::move(issue));
    state.diagnostics.push_back(make_diagnostic(
            SystemSourceUpgradePhase::System,
            diagnostic,
            false));
}

void record_post_system_snapshot_failure(
        SystemSourceUpgradeResult& result,
        PackageMetadataFailure failure) {
    const std::string diagnostic =
            "Failed to snapshot local package versions after system upgrade: " +
            failure.diagnostic;
    result.system.after_snapshot_failure = failure;
    result.system.package_state_change = PackageStateChange::Unknown;

    SystemSourceUpgradeIssue issue = make_issue(
            SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable,
            SystemSourceUpgradeIssueImpact::ObservabilityOnly,
            SystemSourceUpgradePhase::System,
            diagnostic);
    issue.package_metadata_failure = std::move(failure);
    result.issues.push_back(std::move(issue));
    result.diagnostics.push_back(make_diagnostic(
            SystemSourceUpgradePhase::System,
            diagnostic,
            false));
}

PackageMetadataFailure generic_metadata_failure(
        const std::string& diagnostic) {
    return PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            diagnostic};
}

bool has_non_success_source_status(
        RegisteredSourceUpgradeStatus status) noexcept {
    return status != RegisteredSourceUpgradeStatus::Updated &&
           status != RegisteredSourceUpgradeStatus::NoChange;
}

bool validate_prepared_correlation(
        const SystemSourceUpgradePreparedSnapshot& snapshot,
        const std::vector<RegisteredSourceCorrelation>& correlations,
        const std::optional<PreparedProductionSourceBuildInvocation>& invocation) {
    if(correlations.size() != snapshot.registered_sources.size()) return false;

    std::set<std::size_t> work_item_indices;
    std::size_t valid_source_count = 0;
    for(std::size_t index = 0; index < correlations.size(); ++index) {
        const RegisteredSourceCorrelation& correlation = correlations[index];
        const RegisteredSourcePreferenceSnapshot& source =
                snapshot.registered_sources[index];
        if(correlation.snapshot_index != index) return false;
        if(correlation.has_valid_package_name !=
           is_valid_package_name(source.preference_package_name)) {
            return false;
        }
        if(!correlation.has_valid_package_name) {
            if(correlation.work_item_index.has_value()) return false;
            continue;
        }

        ++valid_source_count;
        if(!correlation.work_item_index.has_value() ||
           !source.environment.has_value() ||
           !source.canonical_source_identity_key.has_value() ||
           !source.resolved_package_base.has_value() ||
           !invocation.has_value()) {
            return false;
        }

        const std::size_t work_item_index =
                correlation.work_item_index.value();
        if(work_item_index >= invocation->work_items.size() ||
           !work_item_indices.insert(work_item_index).second) {
            return false;
        }
        const ProductionSourceBuildWorkItem& work_item =
                invocation->work_items[work_item_index];
        if(work_item.request.package_name != source.preference_package_name ||
           work_item.request.checkout_name != source.resolved_package_base.value()) {
            return false;
        }
    }

    if(valid_source_count == 0) return !invocation.has_value();
    return invocation.has_value() &&
           invocation->work_items.size() == valid_source_count &&
           work_item_indices.size() == valid_source_count;
}

void add_inconsistent_issue(
        SystemSourceUpgradeResult& result,
        SystemSourceUpgradeIssueKind kind,
        SystemSourceUpgradePhase phase,
        std::string diagnostic) {
    result.status = SystemSourceUpgradeStatus::InconsistentResult;
    result.stopped_phase = phase;
    result.issues.push_back(make_issue(
            kind,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            phase,
            diagnostic));
    result.diagnostics.push_back(make_diagnostic(
            phase,
            std::move(diagnostic),
            true));
}

void stop_for_source_metadata_failure(
        SystemSourceUpgradeResult& result,
        std::size_t source_position,
        PackageMetadataFailure failure,
        std::string diagnostic) {
    RegisteredSourceUpgradeResult& source_result =
            result.registered_source_results[source_position];
    source_result.status = RegisteredSourceUpgradeStatus::Incomplete;
    source_result.failure_kind =
            RegisteredSourceUpgradeFailureKind::PackageMetadataUnavailable;
    source_result.package_state_change = PackageStateChange::NoChange;
    source_result.diagnostic = diagnostic;

    SystemSourceUpgradeIssue issue = make_issue(
            SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            SystemSourceUpgradePhase::RegisteredSource,
            diagnostic);
    issue.original_preference_index = source_result.original_preference_index;
    issue.preference_package_name = source_result.preference_package_name;
    issue.resolved_package_base = source_result.resolved_package_base;
    issue.package_metadata_failure = std::move(failure);
    result.issues.push_back(std::move(issue));

    SystemSourceUpgradeDiagnostic detail = make_diagnostic(
            SystemSourceUpgradePhase::RegisteredSource,
            std::move(diagnostic),
            true);
    detail.original_preference_index = source_result.original_preference_index;
    detail.preference_package_name = source_result.preference_package_name;
    detail.resolved_package_base = source_result.resolved_package_base;
    result.diagnostics.push_back(std::move(detail));
    result.status = SystemSourceUpgradeStatus::StoppedOnSourceFailure;
    result.stopped_phase = SystemSourceUpgradePhase::RegisteredSource;
}

void stop_for_global_source_metadata_failure(
        SystemSourceUpgradeResult& result,
        std::optional<PackageMetadataFailure> failure,
        std::string diagnostic) {
    SystemSourceUpgradeIssue issue = make_issue(
            SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            SystemSourceUpgradePhase::RegisteredSource,
            diagnostic);
    issue.package_metadata_failure = std::move(failure);
    result.issues.push_back(std::move(issue));
    result.diagnostics.push_back(make_diagnostic(
            SystemSourceUpgradePhase::RegisteredSource,
            std::move(diagnostic),
            true));
    result.status = SystemSourceUpgradeStatus::StoppedOnSourceFailure;
    result.stopped_phase = SystemSourceUpgradePhase::RegisteredSource;
}

void map_source_execution_result(
        RegisteredSourceUpgradeResult& result,
        const SourceBuildExecutionResult& execution) {
    result.diagnostic = execution.diagnostic.empty()
            ? std::nullopt
            : std::optional<std::string>(execution.diagnostic);
    result.cleanup_diagnostic = std::nullopt;
    switch(execution.status) {
        case SourceBuildExecutionStatus::Installed:
            result.status = RegisteredSourceUpgradeStatus::Updated;
            result.failure_kind = RegisteredSourceUpgradeFailureKind::None;
            result.package_state_change = PackageStateChange::Changed;
            return;
        case SourceBuildExecutionStatus::SkippedAsNeeded:
        case SourceBuildExecutionStatus::UpToDate:
            result.status = RegisteredSourceUpgradeStatus::NoChange;
            result.failure_kind = RegisteredSourceUpgradeFailureKind::None;
            result.package_state_change = PackageStateChange::NoChange;
            return;
        case SourceBuildExecutionStatus::UpdateStatusUnknownSkipped:
            result.status = RegisteredSourceUpgradeStatus::Incomplete;
            result.failure_kind = RegisteredSourceUpgradeFailureKind::
                    UpdateStatusUnknownSkipped;
            result.package_state_change = PackageStateChange::NoChange;
            return;
    }
    throw std::logic_error("Unknown source-build execution status.");
}

void map_cleanup_failure(
        RegisteredSourceUpgradeResult& result,
        const SeparatedSourceBuildCleanupError& error) {
    switch(error.install_outcome()) {
        case ArtifactInstallExecutionOutcome::Installed:
            result.status =
                    RegisteredSourceUpgradeStatus::UpdatedCleanupFailed;
            result.package_state_change = PackageStateChange::Changed;
            break;
        case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
            result.status =
                    RegisteredSourceUpgradeStatus::NoChangeCleanupFailed;
            result.package_state_change = PackageStateChange::NoChange;
            break;
    }
    result.failure_kind = RegisteredSourceUpgradeFailureKind::
            CleanupFailedAfterPackageTransaction;
    result.diagnostic = error.what();
    result.cleanup_diagnostic = error.what();
}

} // namespace

struct PreparedSystemSourceUpgrade::Impl {
    explicit Impl(SystemSourceUpgradePreparationState&& prepared_state)
        : state(std::move(prepared_state)) {
    }

    SystemSourceUpgradePreparationState state;
};

struct SystemSourceUpgradePreparationAccess {
    static PreparedSystemSourceUpgrade make(
            SystemSourceUpgradePreparationState&& state) {
        return PreparedSystemSourceUpgrade(
                std::make_unique<PreparedSystemSourceUpgrade::Impl>(
                        std::move(state)));
    }
};

bool SystemSourceUpgradeResult::is_success() const noexcept {
    if(status != SystemSourceUpgradeStatus::Completed) return false;
    // snapshot failureはexecutionを止めない場合もあるが、観測不能を完全成功へ
    // 丸めない。legacy CLIのexit互換はcommand adapter側で別に判断する。
    if(!issues.empty()) return false;
    return std::none_of(
            registered_source_results.begin(),
            registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return has_non_success_source_status(source.status);
            });
}

PackageStateChange SystemSourceUpgradeResult::package_state_change()
        const noexcept {
    bool has_unknown = system.package_state_change ==
            PackageStateChange::Unknown;
    if(system.package_state_change == PackageStateChange::Changed) {
        return PackageStateChange::Changed;
    }
    for(const auto& source : registered_source_results) {
        if(source.package_state_change == PackageStateChange::Changed) {
            return PackageStateChange::Changed;
        }
        has_unknown = has_unknown ||
                source.package_state_change == PackageStateChange::Unknown;
    }
    return has_unknown ? PackageStateChange::Unknown
                       : PackageStateChange::NoChange;
}

bool SystemSourceUpgradeResult::definitely_changed_package_state()
        const noexcept {
    return package_state_change() == PackageStateChange::Changed;
}

bool SystemSourceUpgradeResult::has_partial_completion() const noexcept {
    if(system.status != SystemUpgradePhaseStatus::Completed) return false;
    // system phase完了後の停止・内部相関failureは、source mutationの有無に
    // かかわらずaggregate全体としてpartial completionである。
    if(status != SystemSourceUpgradeStatus::Completed) return true;
    return std::any_of(
            registered_source_results.begin(),
            registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return has_non_success_source_status(source.status);
            });
}

bool SystemSourceUpgradeResult::has_not_attempted_sources() const noexcept {
    return std::any_of(
            registered_source_results.begin(),
            registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return source.status ==
                        RegisteredSourceUpgradeStatus::NotAttempted;
            });
}

bool SystemSourceUpgradeResult::has_cleanup_failure() const noexcept {
    return std::any_of(
            registered_source_results.begin(),
            registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return source.status ==
                               RegisteredSourceUpgradeStatus::
                                       UpdatedCleanupFailed ||
                       source.status ==
                               RegisteredSourceUpgradeStatus::
                                       NoChangeCleanupFailed;
            });
}

bool SystemSourceUpgradeResult::has_blocking_issue() const noexcept {
    return std::any_of(
            issues.begin(), issues.end(),
            [](const SystemSourceUpgradeIssue& issue) {
                return issue.impact ==
                        SystemSourceUpgradeIssueImpact::BlocksExecution;
            });
}

std::optional<std::string>
SystemSourceUpgradeResult::failure_diagnostic() const {
    for(const auto& diagnostic : diagnostics) {
        if(diagnostic.stops_execution) return diagnostic.diagnostic;
    }
    if(system.diagnostic.has_value() &&
       system.status == SystemUpgradePhaseStatus::Failed) {
        return system.diagnostic;
    }
    for(const auto& source : registered_source_results) {
        if(source.cleanup_diagnostic.has_value()) {
            return source.cleanup_diagnostic;
        }
        if(source.diagnostic.has_value() &&
           (source.status == RegisteredSourceUpgradeStatus::Failed ||
            source.status == RegisteredSourceUpgradeStatus::Incomplete)) {
            return source.diagnostic;
        }
    }
    return std::nullopt;
}

PreparedSystemSourceUpgrade::PreparedSystemSourceUpgrade(
        std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {
}

PreparedSystemSourceUpgrade::PreparedSystemSourceUpgrade(
        PreparedSystemSourceUpgrade&&) noexcept = default;

PreparedSystemSourceUpgrade::~PreparedSystemSourceUpgrade() noexcept = default;

bool PreparedSystemSourceUpgrade::is_valid() const noexcept {
    return impl_ != nullptr;
}

const SystemSourceUpgradePreparedSnapshot*
PreparedSystemSourceUpgrade::snapshot() const noexcept {
    return impl_ == nullptr ? nullptr : &impl_->state.snapshot;
}

#ifdef JPACKER_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
void PreparedSystemSourceUpgrade::
make_first_source_correlation_inconsistent_for_test() {
    if(impl_ == nullptr || impl_->state.correlations.empty()) return;
    ++impl_->state.correlations.front().snapshot_index;
}
#endif

SystemSourceUpgradePreparation prepare_system_source_upgrade(
        const AppConfig& config,
        const SystemSourceUpgradeEventObserver& observer) {
    SystemSourceUpgradePreparationState state;
    state.snapshot.options = snapshot_options(config);

    SourcePreferenceDirectorySnapshot directory_snapshot;
    try {
        directory_snapshot = snapshot_source_preference_directory();
    } catch(const std::exception& error) {
        SystemSourceUpgradeIssue issue = make_issue(
                SystemSourceUpgradeIssueKind::
                        PreferenceEnumerationUnavailable,
                SystemSourceUpgradeIssueImpact::BlocksExecution,
                SystemSourceUpgradePhase::Preparation,
                error.what());
        return block_preparation(
                std::move(state), std::move(issue), std::nullopt,
                RegisteredSourceUpgradeFailureKind::PreferenceUnavailable);
    } catch(...) {
        SystemSourceUpgradeIssue issue = make_issue(
                SystemSourceUpgradeIssueKind::
                        PreferenceEnumerationUnavailable,
                SystemSourceUpgradeIssueImpact::BlocksExecution,
                SystemSourceUpgradePhase::Preparation,
                "Source preference enumeration failed with an unknown exception.");
        return block_preparation(
                std::move(state), std::move(issue), std::nullopt,
                RegisteredSourceUpgradeFailureKind::UnknownException);
    }

    state.snapshot.preference_root_exists = directory_snapshot.root_exists;
    for(const auto& entry : directory_snapshot.entries) {
        if(!entry.is_regular_file) continue;
        state.snapshot.registered_sources.push_back(
                RegisteredSourcePreferenceSnapshot{
                        entry.original_index,
                        entry.package_name,
                        entry.entry_path,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {}});
        state.correlations.push_back(RegisteredSourceCorrelation{
                state.snapshot.registered_sources.size() - 1,
                is_valid_package_name(entry.package_name),
                std::nullopt});
    }

    const bool has_valid_source = std::any_of(
            state.correlations.begin(), state.correlations.end(),
            [](const RegisteredSourceCorrelation& correlation) {
                return correlation.has_valid_package_name;
            });
    if(has_valid_source) {
        try {
            require_supported_production_source_build_options(config);
        } catch(const std::exception& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceInvocationPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed);
        }
    }

    std::vector<ProductionSourceBuildWorkItem> source_work_items;
    std::vector<std::string> package_names;
    for(std::size_t source_position = 0;
        source_position < state.snapshot.registered_sources.size();
        ++source_position) {
        RegisteredSourceCorrelation& correlation =
                state.correlations[source_position];
        RegisteredSourcePreferenceSnapshot& source =
                state.snapshot.registered_sources[source_position];
        if(!correlation.has_valid_package_name) continue;

        StrictSourcePreferenceResult preference =
                read_source_preference_strict(
                        source.preference_package_name);
        if(const auto* loaded =
                   std::get_if<SourcePreferenceLoaded>(&preference)) {
            source.entry_path = loaded->entry_path;
            source.environment = loaded->environment;
            source.preference_load_warnings = loaded->warnings;
            notify(observer, SystemSourceUpgradeEvent{
                    SystemSourceUpgradeEventKind::LoadingSourcePreference,
                    source.original_preference_index,
                    source.preference_package_name,
                    source.entry_path,
                    {}});
            for(const auto& warning_text : loaded->warnings) {
                state.warnings.push_back(SystemSourceUpgradeWarning{
                        SystemSourceUpgradeWarningKind::SourcePreference,
                        source.original_preference_index,
                        source.preference_package_name,
                        source.entry_path,
                        warning_text});
                notify(observer, SystemSourceUpgradeEvent{
                        SystemSourceUpgradeEventKind::SourcePreferenceWarning,
                        source.original_preference_index,
                        source.preference_package_name,
                        source.entry_path,
                        warning_text});
            }
        } else {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::PreferenceUnavailable,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    {});
            attribute_issue_to_source(issue, source);
            if(const auto* failure =
                       std::get_if<SourcePreferenceFailure>(&preference)) {
                issue.source_preference_failure = *failure;
                issue.diagnostic = failure->diagnostic;
            } else {
                issue.diagnostic =
                        "Registered source preference disappeared before preparation: " +
                        source.entry_path.string();
            }
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::PreferenceUnavailable);
        }

        ResolvedSourceBuildIdentity identity;
        try {
            identity = resolve_source_build_identity(
                    source.preference_package_name);
            source.canonical_source_identity_key =
                    identity.canonical_source_key;
            source.resolved_package_base = identity.package_base;
        } catch(const std::exception& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceIdentityResolutionFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed);
        } catch(...) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceIdentityResolutionFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    "Source identity resolution failed with an unknown exception.");
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
        }

        try {
            correlation.work_item_index = source_work_items.size();
            source_work_items.push_back(
                    prepare_resolved_source_build_work_item(
                            identity,
                            source.environment.value(),
                            true,
                            false));
            package_names.push_back(source.preference_package_name);
        } catch(const std::exception& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceWorkItemPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed);
        } catch(...) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceWorkItemPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    "Source work-item preparation failed with an unknown exception.");
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
        }
    }

    if(!source_work_items.empty()) {
        try {
            state.source_invocation =
                    prepare_production_source_build_invocation(
                            std::move(source_work_items), config);
        } catch(const std::exception& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceInvocationPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed);
        } catch(...) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceInvocationPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    "Source invocation preparation failed with an unknown exception.");
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
        }

        const PacmanDatabasePaths database_paths =
                state.source_invocation->database_paths;
        state.system_database_paths = database_paths;
        try {
            PackageMetadataSession session =
                    PackageMetadataSession::open(database_paths);
            std::optional<SourceBaselineFailure> baseline_failure =
                    snapshot_source_update_baselines(
                            session,
                            package_names,
                            state.update_baselines);
            if(baseline_failure.has_value()) {
                const std::string& package_name =
                        package_names[baseline_failure->package_index];
                auto source = std::find_if(
                        state.snapshot.registered_sources.begin(),
                        state.snapshot.registered_sources.end(),
                        [&package_name](
                                const RegisteredSourcePreferenceSnapshot& candidate) {
                            return candidate.preference_package_name == package_name;
                        });
                const std::size_t source_position = static_cast<std::size_t>(
                        std::distance(
                                state.snapshot.registered_sources.begin(),
                                source));
                SystemSourceUpgradeIssue issue = make_issue(
                        SystemSourceUpgradeIssueKind::
                                SourceBaselineSnapshotUnavailable,
                        SystemSourceUpgradeIssueImpact::BlocksExecution,
                        SystemSourceUpgradePhase::Preparation,
                        baseline_failure->failure.diagnostic);
                issue.package_metadata_failure = baseline_failure->failure;
                if(source != state.snapshot.registered_sources.end()) {
                    attribute_issue_to_source(issue, *source);
                }
                return block_preparation(
                        std::move(state), std::move(issue), source_position,
                        RegisteredSourceUpgradeFailureKind::
                                PackageMetadataUnavailable);
            }

            LocalPackageVersionSnapshotResult system_snapshot =
                    session.snapshot_local_package_versions();
            if(const auto* failure =
                       std::get_if<PackageMetadataFailure>(&system_snapshot)) {
                record_system_snapshot_failure(state, *failure);
            } else {
                state.before_system_snapshot =
                        std::get<LocalPackageVersionSnapshot>(
                                std::move(system_snapshot));
            }
        } catch(const PackageMetadataError& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceBaselineSnapshotUnavailable,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            issue.package_metadata_failure = error.failure();
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::
                            PackageMetadataUnavailable);
        } catch(const std::exception& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceBaselineSnapshotUnavailable,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::
                            PackageMetadataUnavailable);
        }
    }

    return SystemSourceUpgradePreparationAccess::make(std::move(state));
}

SystemSourceUpgradeResult execute_prepared_system_source_upgrade(
        PreparedSystemSourceUpgrade prepared,
        const AppConfig& config,
        const SystemSourceUpgradeEventObserver& observer) {
    if(prepared.impl_ == nullptr) {
        SystemSourceUpgradeResult result;
        result.status = SystemSourceUpgradeStatus::InconsistentResult;
        result.stopped_phase = SystemSourceUpgradePhase::Preparation;
        add_inconsistent_issue(
                result,
                SystemSourceUpgradeIssueKind::PreparedCapabilityConsumed,
                SystemSourceUpgradePhase::Preparation,
                "Prepared system/source upgrade is invalid or has already been consumed.");
        return result;
    }

    SystemSourceUpgradePreparationState& state = prepared.impl_->state;
    if(!options_match(state.snapshot.options, config)) {
        SystemSourceUpgradeResult result = make_result_from_state(
                std::move(state),
                SystemSourceUpgradeStatus::InconsistentResult,
                SystemSourceUpgradePhase::Preparation);
        add_inconsistent_issue(
                result,
                SystemSourceUpgradeIssueKind::OptionSnapshotMismatch,
                SystemSourceUpgradePhase::Preparation,
                "Prepared system/source upgrade options differ from execution options.");
        return result;
    }
    if(!validate_prepared_correlation(
               state.snapshot,
               state.correlations,
               state.source_invocation)) {
        SystemSourceUpgradeResult result = make_result_from_state(
                std::move(state),
                SystemSourceUpgradeStatus::InconsistentResult,
                SystemSourceUpgradePhase::Preparation);
        add_inconsistent_issue(
                result,
                SystemSourceUpgradeIssueKind::PreparedCorrelationInconsistent,
                SystemSourceUpgradePhase::Preparation,
                "Prepared system/source upgrade source correlation is inconsistent.");
        return result;
    }

    // public snapshot/result detailだけを移し、executionに必要なprepared
    // invocation/correlation/baselineはone-shot capability内へ残す。
    SystemSourceUpgradeResult result = make_result_from_state(
            std::move(state),
            SystemSourceUpgradeStatus::Completed,
            SystemSourceUpgradePhase::None);

    notify(observer, SystemSourceUpgradeEvent{
            SystemSourceUpgradeEventKind::SystemUpgradeStarting,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {}});
    int system_exit_status = 1;
    try {
        system_exit_status = run_command(system_upgrade_command(config));
    } catch(const std::exception& error) {
        result.system.status = SystemUpgradePhaseStatus::Failed;
        result.system.package_state_change = PackageStateChange::Unknown;
        result.system.diagnostic = error.what();
        result.status = SystemSourceUpgradeStatus::StoppedOnSystemFailure;
        result.stopped_phase = SystemSourceUpgradePhase::System;
        result.diagnostics.push_back(make_diagnostic(
                SystemSourceUpgradePhase::System,
                error.what(),
                true));
        return result;
    } catch(...) {
        result.system.status = SystemUpgradePhaseStatus::Failed;
        result.system.package_state_change = PackageStateChange::Unknown;
        result.system.diagnostic = UNKNOWN_SYSTEM_FAILURE_DIAGNOSTIC;
        result.status = SystemSourceUpgradeStatus::StoppedOnSystemFailure;
        result.stopped_phase = SystemSourceUpgradePhase::System;
        result.diagnostics.push_back(make_diagnostic(
                SystemSourceUpgradePhase::System,
                UNKNOWN_SYSTEM_FAILURE_DIAGNOSTIC,
                true));
        return result;
    }
    result.system.command_exit_status = system_exit_status;
    if(system_exit_status != 0) {
        result.system.status = SystemUpgradePhaseStatus::Failed;
        result.system.package_state_change = PackageStateChange::Unknown;
        result.system.diagnostic = "Update failed.";
        result.status = SystemSourceUpgradeStatus::StoppedOnSystemFailure;
        result.stopped_phase = SystemSourceUpgradePhase::System;
        result.diagnostics.push_back(make_diagnostic(
                SystemSourceUpgradePhase::System,
                "Update failed.",
                true));
        return result;
    }
    result.system.status = SystemUpgradePhaseStatus::Completed;

    const bool has_source_work_items = state.source_invocation.has_value();
    if(has_source_work_items) {
        notify(observer, SystemSourceUpgradeEvent{
                SystemSourceUpgradeEventKind::CheckingSourcePackages,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {}});
    }

    SourceInstalledSnapshotResults installed_snapshots;
    if(state.system_database_paths.has_value() &&
       (state.before_system_snapshot.has_value() || has_source_work_items)) {
        try {
            PackageMetadataSession session = PackageMetadataSession::open(
                    state.system_database_paths.value());
            if(state.before_system_snapshot.has_value()) {
                LocalPackageVersionSnapshotResult after_snapshot =
                        session.snapshot_local_package_versions();
                if(const auto* failure =
                           std::get_if<PackageMetadataFailure>(
                                   &after_snapshot)) {
                    record_post_system_snapshot_failure(result, *failure);
                } else {
                    const LocalPackageVersionSnapshot& after =
                            std::get<LocalPackageVersionSnapshot>(
                                    after_snapshot);
                    result.system.package_state_change =
                            state.before_system_snapshot.value() == after
                            ? PackageStateChange::NoChange
                            : PackageStateChange::Changed;
                }
            } else {
                result.system.package_state_change = PackageStateChange::Unknown;
            }

            if(has_source_work_items) {
                std::vector<std::string> package_names;
                package_names.reserve(
                        state.source_invocation->work_items.size());
                for(const auto& work_item :
                    state.source_invocation->work_items) {
                    package_names.push_back(work_item.request.package_name);
                }
                installed_snapshots =
                        snapshot_post_upgrade_installed_packages(
                                session, package_names);
            }
        } catch(const PackageMetadataError& error) {
            if(state.before_system_snapshot.has_value()) {
                record_post_system_snapshot_failure(
                        result, error.failure());
            } else {
                result.system.package_state_change = PackageStateChange::Unknown;
            }
            if(has_source_work_items) {
                const std::string diagnostic =
                        std::string(POST_UPGRADE_SNAPSHOT_FAILURE_PREFIX) +
                        error.failure().diagnostic;
                stop_for_global_source_metadata_failure(
                        result, error.failure(), diagnostic);
                return result;
            }
        } catch(const std::exception& error) {
            if(state.before_system_snapshot.has_value()) {
                record_post_system_snapshot_failure(
                        result,
                        generic_metadata_failure(error.what()));
            } else {
                result.system.package_state_change = PackageStateChange::Unknown;
            }
            if(has_source_work_items) {
                stop_for_global_source_metadata_failure(
                        result,
                        std::nullopt,
                        std::string(POST_UPGRADE_SNAPSHOT_FAILURE_PREFIX) +
                                error.what());
                return result;
            }
        } catch(...) {
            const std::string diagnostic =
                    "Post-upgrade package metadata snapshot failed with an unknown exception.";
            if(state.before_system_snapshot.has_value()) {
                record_post_system_snapshot_failure(
                        result,
                        generic_metadata_failure(diagnostic));
            } else {
                result.system.package_state_change = PackageStateChange::Unknown;
            }
            if(has_source_work_items) {
                stop_for_global_source_metadata_failure(
                        result,
                        std::nullopt,
                        std::string(POST_UPGRADE_SNAPSHOT_FAILURE_PREFIX) +
                                diagnostic);
                return result;
            }
        }
    } else if(result.system.before_snapshot_failure.has_value()) {
        result.system.package_state_change = PackageStateChange::Unknown;
    } else {
        result.system.package_state_change = PackageStateChange::Unknown;
    }

    // POLICY: post-Syu metadata failureは、1件でもsource mutationを始める前に
    // preference順で全correlationを検査する。
    for(std::size_t source_position = 0;
        source_position < state.correlations.size();
        ++source_position) {
        const RegisteredSourceCorrelation& correlation =
                state.correlations[source_position];
        if(!correlation.has_valid_package_name) continue;
        if(!correlation.work_item_index.has_value() ||
           !state.source_invocation.has_value()) {
            add_inconsistent_issue(
                    result,
                    SystemSourceUpgradeIssueKind::
                            PreparedCorrelationInconsistent,
                    SystemSourceUpgradePhase::RegisteredSource,
                    "Prepared source work item is missing after system upgrade.");
            return result;
        }

        const ProductionSourceBuildWorkItem& work_item =
                state.source_invocation->work_items[
                        correlation.work_item_index.value()];
        auto installed_snapshot = installed_snapshots.find(
                work_item.request.package_name);
        if(installed_snapshot == installed_snapshots.end()) {
            add_inconsistent_issue(
                    result,
                    SystemSourceUpgradeIssueKind::
                            PreparedCorrelationInconsistent,
                    SystemSourceUpgradePhase::RegisteredSource,
                    "System upgrade completed, but authoritative post-upgrade installed package snapshot is missing for " +
                            work_item.request.package_name +
                            "; source processing did not start.");
            return result;
        }
        if(const auto* metadata_failure =
                   std::get_if<PackageMetadataFailure>(
                           &installed_snapshot->second)) {
            const std::string diagnostic =
                    "System upgrade completed, but post-upgrade package metadata query failed for " +
                    work_item.request.package_name + ": " +
                    metadata_failure->diagnostic +
                    " Source processing did not start.";
            stop_for_source_metadata_failure(
                    result,
                    source_position,
                    *metadata_failure,
                    diagnostic);
            return result;
        }
    }

    for(std::size_t source_position = 0;
        source_position < state.correlations.size();
        ++source_position) {
        const RegisteredSourceCorrelation& correlation =
                state.correlations[source_position];
        RegisteredSourceUpgradeResult& source_result =
                result.registered_source_results[source_position];
        if(!correlation.has_valid_package_name) {
            const std::string diagnostic =
                    "Ignoring invalid source-build preference filename: " +
                    source_result.preference_package_name;
            source_result.status = RegisteredSourceUpgradeStatus::Unsupported;
            source_result.failure_kind =
                    RegisteredSourceUpgradeFailureKind::InvalidPreferenceName;
            source_result.package_state_change = PackageStateChange::NoChange;
            source_result.diagnostic = diagnostic;

            result.warnings.push_back(SystemSourceUpgradeWarning{
                    SystemSourceUpgradeWarningKind::InvalidPreferenceName,
                    source_result.original_preference_index,
                    source_result.preference_package_name,
                    result.prepared_snapshot.registered_sources[
                            source_position].entry_path,
                    diagnostic});
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::InvalidPreferenceName,
                    SystemSourceUpgradeIssueImpact::AffectsSuccess,
                    SystemSourceUpgradePhase::RegisteredSource,
                    diagnostic);
            issue.original_preference_index =
                    source_result.original_preference_index;
            issue.preference_package_name =
                    source_result.preference_package_name;
            result.issues.push_back(std::move(issue));
            notify(observer, SystemSourceUpgradeEvent{
                    SystemSourceUpgradeEventKind::InvalidPreferenceWarning,
                    source_result.original_preference_index,
                    source_result.preference_package_name,
                    result.prepared_snapshot.registered_sources[
                            source_position].entry_path,
                    diagnostic});
            continue;
        }

        ProductionSourceBuildWorkItem& work_item =
                state.source_invocation->work_items[
                        correlation.work_item_index.value()];
        if(work_item.uses_system_update_baseline) {
            auto baseline = state.update_baselines.find(
                    work_item.request.package_name);
            if(baseline != state.update_baselines.end()) {
                work_item.request.update_baseline = baseline->second;
            }
        }
        work_item.request.installed_snapshot =
                std::get<SourceInstalledSnapshot>(
                        installed_snapshots.at(
                                work_item.request.package_name));

        try {
            SourceBuildExecutionResult execution =
                    execute_prepared_source_build_work_item_typed(
                            work_item,
                            state.source_invocation->database_paths,
                            config);
            map_source_execution_result(source_result, execution);
            if(execution.status ==
                       SourceBuildExecutionStatus::
                               UpdateStatusUnknownSkipped &&
               !execution.diagnostic.empty()) {
                SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                        SystemSourceUpgradePhase::RegisteredSource,
                        execution.diagnostic,
                        false);
                detail.original_preference_index =
                        source_result.original_preference_index;
                detail.preference_package_name =
                        source_result.preference_package_name;
                detail.resolved_package_base =
                        source_result.resolved_package_base;
                result.diagnostics.push_back(std::move(detail));
            }
        } catch(const SeparatedSourceBuildCleanupError& error) {
            map_cleanup_failure(source_result, error);
            SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                    SystemSourceUpgradePhase::RegisteredSource,
                    error.what(),
                    true);
            detail.original_preference_index =
                    source_result.original_preference_index;
            detail.preference_package_name =
                    source_result.preference_package_name;
            detail.resolved_package_base =
                    source_result.resolved_package_base;
            result.diagnostics.push_back(std::move(detail));
            result.status = SystemSourceUpgradeStatus::
                    StoppedAfterSourceCleanupFailure;
            result.stopped_phase =
                    SystemSourceUpgradePhase::RegisteredSource;
            return result;
        } catch(const std::exception& error) {
            source_result.status = RegisteredSourceUpgradeStatus::Failed;
            source_result.failure_kind =
                    RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed;
            source_result.package_state_change = PackageStateChange::Unknown;
            source_result.diagnostic = error.what();
            SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                    SystemSourceUpgradePhase::RegisteredSource,
                    error.what(),
                    true);
            detail.original_preference_index =
                    source_result.original_preference_index;
            detail.preference_package_name =
                    source_result.preference_package_name;
            detail.resolved_package_base =
                    source_result.resolved_package_base;
            result.diagnostics.push_back(std::move(detail));
            result.status =
                    SystemSourceUpgradeStatus::StoppedOnSourceFailure;
            result.stopped_phase =
                    SystemSourceUpgradePhase::RegisteredSource;
            return result;
        } catch(...) {
            source_result.status = RegisteredSourceUpgradeStatus::Failed;
            source_result.failure_kind =
                    RegisteredSourceUpgradeFailureKind::UnknownException;
            source_result.package_state_change = PackageStateChange::Unknown;
            source_result.diagnostic = UNKNOWN_SOURCE_FAILURE_DIAGNOSTIC;
            SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                    SystemSourceUpgradePhase::RegisteredSource,
                    UNKNOWN_SOURCE_FAILURE_DIAGNOSTIC,
                    true);
            detail.original_preference_index =
                    source_result.original_preference_index;
            detail.preference_package_name =
                    source_result.preference_package_name;
            detail.resolved_package_base =
                    source_result.resolved_package_base;
            result.diagnostics.push_back(std::move(detail));
            result.status =
                    SystemSourceUpgradeStatus::StoppedOnSourceFailure;
            result.stopped_phase =
                    SystemSourceUpgradePhase::RegisteredSource;
            return result;
        }
    }

    return result;
}
