#include "system_source_upgrade.hpp"

#include "app_config.hpp"
#include "cache_authority.hpp"
#include "dependency_plan.hpp"
#include "localization.hpp"
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

std::string unknown_system_failure_diagnostic() {
    return localization::translate_message(
            "The system upgrade failed with an unknown exception.");
}

std::string unknown_source_failure_diagnostic() {
    return localization::translate_message(
            "Registered source package processing failed with an unknown exception.");
}

std::string join_package_names(
        const std::vector<std::string>& package_names) {
    std::string joined;
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        if(index > 0) joined += ", ";
        joined += package_names[index];
    }
    return joined;
}

std::string post_upgrade_snapshot_failure_diagnostic(
        const std::string& detail) {
    return localization::format_translated_message(
            "The system upgrade completed, but the post-upgrade package metadata snapshot failed: {}",
            detail);
}

ProviderSelectionCallback registered_source_provider_selection_callback(
        ProviderSelectionCallback select_provider) {
    if(!select_provider) return {};
    return [select_provider = std::move(select_provider)](
                   const std::string& dependency,
                   const std::vector<ProvidedDependency>& candidates)
                   -> std::optional<ProvidedDependency> {
        const bool has_aur_candidate = std::any_of(
                candidates.begin(), candidates.end(),
                [](const ProvidedDependency& candidate) {
                    return std::holds_alternative<AurProviderOrigin>(
                            candidate.origin);
                });
        if(has_aur_candidate) return std::nullopt;
        return select_provider(dependency, candidates);
    };
}

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
    std::optional<BuildPlan> aur_invocation_plan;
    std::optional<PreparedProductionSourceBuildInvocation> source_invocation;
    std::vector<RegisteredSourceCorrelation> correlations;
    SourceUpdateBaselines update_baselines;
    std::optional<PacmanDatabasePaths> system_database_paths;
    std::optional<LocalPackageVersionSnapshot> before_system_snapshot;
    SystemUpgradePhaseResult system;
    std::vector<SystemSourceUpgradeWarning> warnings;
    std::vector<SystemSourceUpgradeIssue> issues;
    std::vector<SystemSourceUpgradeDiagnostic> diagnostics;
#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
    std::optional<SystemSourceUpgradeUnexpectedExceptionPoint>
            unexpected_exception_point;
    bool unexpected_exception_is_unknown = false;
#endif
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
            config.user_config.review.pkgbuild == ReviewPolicy::Skip,
            config.user_config.review.diff == ReviewPolicy::Skip,
            config.no_confirm,
            config.user_config.build.mode == BuildMode::Rebuild,
            config.user_config.build.mode == BuildMode::Clean,
            config.rm_deps,
            config.editor, false};
}

bool options_match(
        const SystemSourceUpgradeOptionSnapshot& snapshot,
        const AppConfig& config) noexcept {
    return snapshot.no_edit ==
                   (config.user_config.review.pkgbuild == ReviewPolicy::Skip) &&
           snapshot.no_diff ==
                   (config.user_config.review.diff == ReviewPolicy::Skip) &&
           snapshot.no_confirm == config.no_confirm &&
           snapshot.rebuild ==
                   (config.user_config.build.mode == BuildMode::Rebuild) &&
           snapshot.clean_build ==
                   (config.user_config.build.mode == BuildMode::Clean) &&
           snapshot.rm_deps == config.rm_deps &&
           snapshot.editor == config.editor && !snapshot.needed;
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
                            localization::format_translated_message(
                                    "Failed to query installed package metadata for {}: {}",
                                    package_name, failure->diagnostic)}};
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
    result.aur_invocation_plan = std::move(state.aur_invocation_plan);
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

#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
struct UnknownSystemSourceTestException {};

void throw_unexpected_exception_for_test(
        const SystemSourceUpgradePreparationState& state,
        SystemSourceUpgradeUnexpectedExceptionPoint point) {
    if(!state.unexpected_exception_point.has_value() ||
       *state.unexpected_exception_point != point) {
        return;
    }
    if(state.unexpected_exception_is_unknown) {
        throw UnknownSystemSourceTestException{};
    }
    // NO_TRANSLATE(Issue #308): this is a test-only injected exception value.
    throw std::runtime_error(
            "fixture unexpected system/source execution exception");
}
#endif

void record_unexpected_execution_failure(
        SystemSourceUpgradeResult& result,
        SystemSourceUpgradePhase active_phase,
        std::optional<std::size_t> active_source_position,
        bool system_package_state_finalized,
        const std::string& exception_diagnostic) {
    result.status = SystemSourceUpgradeStatus::InconsistentResult;
    result.stopped_phase = active_phase;

    std::string diagnostic = exception_diagnostic.empty()
            ? localization::translate_message(
                      "The system/source result is inconsistent after an unexpected exception.")
            : localization::format_translated_message(
                      "The system/source result is inconsistent after an unexpected exception: {}",
                      exception_diagnostic);

    if(active_phase == SystemSourceUpgradePhase::System &&
       result.system.status == SystemUpgradePhaseStatus::NotAttempted) {
        const std::string unavailable = exception_diagnostic.empty()
                ? localization::translate_message(
                          "The system result is unavailable because an unexpected exception occurred after the phase started.")
                : localization::format_translated_message(
                          "The system result is unavailable because an unexpected exception occurred after the phase started: {}",
                          exception_diagnostic);
        result.system.status = SystemUpgradePhaseStatus::Failed;
        result.system.package_state_change = PackageStateChange::Unknown;
        result.system.diagnostic = unavailable;
        diagnostic = unavailable;
    } else if(result.system.status == SystemUpgradePhaseStatus::Completed &&
              !system_package_state_finalized) {
        // system command completionは既知でも、post-state snapshot前なら
        // package mutationの有無は断言しない。
        result.system.package_state_change = PackageStateChange::Unknown;
    }

    if(active_phase == SystemSourceUpgradePhase::RegisteredSource &&
       active_source_position.has_value() &&
       *active_source_position < result.registered_source_results.size()) {
        RegisteredSourceUpgradeResult& source =
                result.registered_source_results[*active_source_position];
        if(source.status == RegisteredSourceUpgradeStatus::NotAttempted) {
            const std::string unavailable = exception_diagnostic.empty()
                    ? localization::translate_message(
                              "The registered source result is unavailable because an unexpected exception occurred after the phase started.")
                    : localization::format_translated_message(
                              "The registered source result is unavailable because an unexpected exception occurred after the phase started: {}",
                              exception_diagnostic);
            source.status = RegisteredSourceUpgradeStatus::Incomplete;
            source.failure_kind =
                    RegisteredSourceUpgradeFailureKind::UnknownException;
            source.package_state_change = PackageStateChange::Unknown;
            source.diagnostic = unavailable;
        }
    }

    SystemSourceUpgradeDiagnostic detail = make_diagnostic(
            active_phase, std::move(diagnostic), true);
    if(active_source_position.has_value() &&
       *active_source_position < result.registered_source_results.size()) {
        const RegisteredSourceUpgradeResult& source =
                result.registered_source_results[*active_source_position];
        detail.original_preference_index = source.original_preference_index;
        detail.preference_package_name = source.preference_package_name;
        detail.resolved_package_base = source.resolved_package_base;
    }
    result.diagnostics.push_back(std::move(detail));
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

SystemSourceUpgradeIssue make_cache_authority_issue(
        std::string diagnostic) {
    return make_issue(
            SystemSourceUpgradeIssueKind::CacheAuthorityInvalid,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            SystemSourceUpgradePhase::Preparation,
            std::move(diagnostic));
}

SystemSourceUpgradeResult block_cache_authority_preparation(
        SystemSourceUpgradePreparationState&& state,
        SystemSourceUpgradeIssue issue) {
    return block_preparation(
            std::move(state), std::move(issue), std::nullopt,
            RegisteredSourceUpgradeFailureKind::CacheAuthorityFailure);
}

void record_system_snapshot_failure(
        SystemSourceUpgradePreparationState& state,
        PackageMetadataFailure failure) {
    const std::string diagnostic =
            localization::format_translated_message(
                    "Failed to snapshot local package versions before the system upgrade: {}",
                    failure.diagnostic);
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
            localization::format_translated_message(
                    "Failed to snapshot local package versions after the system upgrade: {}",
                    failure.diagnostic);
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

void stop_for_repository_provider_transaction_failure(
        SystemSourceUpgradeResult& result,
        std::string diagnostic) {
    result.diagnostics.push_back(make_diagnostic(
            SystemSourceUpgradePhase::RegisteredSource,
            std::move(diagnostic), true));
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
    throw std::logic_error(localization::translate_message(
            "Unknown source-build execution status."));
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
    if(!selected_repository_provider_transaction.is_success()) return false;
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
                               PackageStateChange::Unknown ||
            selected_repository_provider_transaction.package_state_change ==
                    PackageStateChange::Unknown;
    if(system.package_state_change == PackageStateChange::Changed ||
       selected_repository_provider_transaction.package_state_change ==
               PackageStateChange::Changed) {
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
    if(!selected_repository_provider_transaction.is_success() &&
       selected_repository_provider_transaction.diagnostic.has_value()) {
        return selected_repository_provider_transaction.diagnostic;
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
        std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
    if(impl_ == nullptr) return;

    SystemSourceUpgradePreparationState& state = impl_->state;
    if(state.correlations.size() !=
       state.snapshot.registered_sources.size()) {
        throw std::logic_error(
                "Prepared system/source projection correlation count is inconsistent.");
    }

    const std::size_t work_item_count = state.source_invocation.has_value()
            ? state.source_invocation->work_items.size()
            : 0;
    std::vector<bool> observed_work_items(work_item_count, false);
    std::vector<PreparedSystemSourceWorkReference> source_work_items;
    source_work_items.reserve(work_item_count);
    for(const RegisteredSourceCorrelation& correlation : state.correlations) {
        if(!correlation.work_item_index.has_value()) continue;
        if(correlation.snapshot_index >=
                   state.snapshot.registered_sources.size() ||
           !state.source_invocation.has_value() ||
           correlation.work_item_index.value() >= work_item_count ||
           observed_work_items[correlation.work_item_index.value()]) {
            throw std::logic_error(
                    "Prepared system/source work-item correlation is inconsistent.");
        }

        observed_work_items[correlation.work_item_index.value()] = true;
        source_work_items.push_back(PreparedSystemSourceWorkReference(
                state.snapshot.registered_sources[correlation.snapshot_index],
                state.source_invocation
                        ->work_items[correlation.work_item_index.value()]));
    }
    if(std::any_of(
               observed_work_items.begin(), observed_work_items.end(),
               [](bool observed) { return !observed; })) {
        throw std::logic_error(
                "Prepared system/source work item has no source correlation.");
    }

    SystemSourceUpgradeProjectionAuthority authority(
            state.snapshot,
            state.aur_invocation_plan.has_value()
                    ? &state.aur_invocation_plan.value()
                    : nullptr,
            state.issues, std::move(source_work_items));
    projection_authority_.emplace(std::move(authority));
}

PreparedSystemSourceUpgrade::PreparedSystemSourceUpgrade(
        PreparedSystemSourceUpgrade&& other) noexcept
    : impl_(std::move(other.impl_)),
      projection_authority_(std::move(other.projection_authority_)) {
    other.projection_authority_.reset();
}

PreparedSystemSourceUpgrade::~PreparedSystemSourceUpgrade() noexcept = default;

bool PreparedSystemSourceUpgrade::is_valid() const noexcept {
    return impl_ != nullptr;
}

const SystemSourceUpgradePreparedSnapshot*
PreparedSystemSourceUpgrade::snapshot() const noexcept {
    return impl_ == nullptr ? nullptr : &impl_->state.snapshot;
}

const BuildPlan* PreparedSystemSourceUpgrade::aur_invocation_plan()
        const noexcept {
    if(impl_ == nullptr || !impl_->state.aur_invocation_plan.has_value()) {
        return nullptr;
    }
    return &impl_->state.aur_invocation_plan.value();
}

const std::vector<SystemSourceUpgradeIssue>&
PreparedSystemSourceUpgrade::issues() const noexcept {
    static const std::vector<SystemSourceUpgradeIssue> s_empty;
    return impl_ == nullptr ? s_empty : impl_->state.issues;
}

const SystemSourceUpgradeProjectionAuthority*
PreparedSystemSourceUpgrade::projection_authority() const noexcept {
    return impl_ != nullptr && projection_authority_.has_value()
            ? &projection_authority_.value()
            : nullptr;
}

#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
void PreparedSystemSourceUpgrade::
make_first_source_correlation_inconsistent_for_test() {
    if(impl_ == nullptr || impl_->state.correlations.empty()) return;
    ++impl_->state.correlations.front().snapshot_index;
}

void PreparedSystemSourceUpgrade::set_unexpected_exception_for_test(
        SystemSourceUpgradeUnexpectedExceptionPoint point,
        bool unknown_exception) {
    if(impl_ == nullptr) return;
    impl_->state.unexpected_exception_point = point;
    impl_->state.unexpected_exception_is_unknown = unknown_exception;
}
#endif

SystemSourceUpgradePreparation prepare_system_source_upgrade(
        const AppConfig& config,
        const SystemSourceUpgradeEventObserver& observer,
        std::optional<ValidatedCacheRoot> cache_root) {
    SystemSourceUpgradePreparationState state;
    state.snapshot.options = snapshot_options(config);

    SourcePreferenceDirectorySnapshot directory_snapshot;
    try {
        directory_snapshot = snapshot_source_preference_directory();
    } catch(const SourcePreferenceError& error) {
        SystemSourceUpgradeIssue issue = make_issue(
                SystemSourceUpgradeIssueKind::
                        PreferenceEnumerationUnavailable,
                SystemSourceUpgradeIssueImpact::BlocksExecution,
                SystemSourceUpgradePhase::Preparation,
                error.what());
        issue.source_preference_failure = error.failure();
        return block_preparation(
                std::move(state), std::move(issue), std::nullopt,
                RegisteredSourceUpgradeFailureKind::PreferenceUnavailable);
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
                localization::translate_message(
                        "Source preference enumeration failed with an unknown exception."));
        return block_preparation(
                std::move(state), std::move(issue), std::nullopt,
                RegisteredSourceUpgradeFailureKind::UnknownException);
    }

    for(const auto& entry : directory_snapshot.entries) {
        std::string diagnostic;
        if(!entry.is_regular_file) {
            diagnostic = localization::format_translated_message(
                    "Source preference enumeration found a non-regular entry: {}",
                    entry.entry_path.string());
        } else if(!is_valid_package_name(entry.package_name)) {
            diagnostic = localization::format_translated_message(
                    "Source preference enumeration found an invalid entry name: {}",
                    entry.entry_path.string());
        }
        if(diagnostic.empty()) continue;

        SystemSourceUpgradeIssue issue = make_issue(
                SystemSourceUpgradeIssueKind::
                        PreferenceEnumerationUnavailable,
                SystemSourceUpgradeIssueImpact::BlocksExecution,
                SystemSourceUpgradePhase::Preparation,
                std::move(diagnostic));
        return block_preparation(
                std::move(state), std::move(issue), std::nullopt,
                RegisteredSourceUpgradeFailureKind::PreferenceUnavailable);
    }

    state.snapshot.preference_root_exists = directory_snapshot.root_exists;
    for(const auto& entry : directory_snapshot.entries) {
        state.snapshot.registered_sources.push_back(
                RegisteredSourcePreferenceSnapshot{
                        entry.original_index,
                        entry.package_name,
                        entry.entry_path,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {},
                        std::nullopt});
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

    // Local preference failureはcache mutationより前に全件確定する。identity
    // resolutionはpacman/curlへ進み得るため、次のphaseでcacheを先にadoptする。
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
            continue;
        }

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
            issue.diagnostic = localization::format_translated_message(
                    "The registered source preference disappeared before preparation: {}",
                    source.entry_path.string());
        }
        return block_preparation(
                std::move(state), std::move(issue), source_position,
                RegisteredSourceUpgradeFailureKind::PreferenceUnavailable);
    }

    std::vector<std::optional<ResolvedSourceBuildIdentity>>
            resolved_identities(
                    state.snapshot.registered_sources.size());
    ProviderSelectionCallback select_provider =
            registered_source_provider_selection_callback(
                    provider_selection_callback(config));
    for(std::size_t source_position = 0;
        source_position < state.snapshot.registered_sources.size();
        ++source_position) {
        RegisteredSourceCorrelation& correlation =
                state.correlations[source_position];
        RegisteredSourcePreferenceSnapshot& source =
                state.snapshot.registered_sources[source_position];
        if(!correlation.has_valid_package_name) continue;

        try {
            ResolvedSourceBuildIdentity identity =
                    resolve_source_build_identity(
                            source.preference_package_name);
            source.canonical_source_identity_key =
                    identity.canonical_source_key;
            source.resolved_package_base = identity.package_base;
            source.source_kind = identity.source_kind;
            resolved_identities[source_position] = std::move(identity);
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
                    localization::translate_message(
                            "Source identity resolution failed with an unknown exception."));
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
        }

    }

    std::vector<std::string> aur_package_names;
    std::optional<std::size_t> first_aur_source_position;
    for(std::size_t source_position = 0;
        source_position < resolved_identities.size();
        ++source_position) {
        if(!resolved_identities[source_position].has_value() ||
           resolved_identities[source_position]->source_kind !=
                   SourceBuildSourceKind::Aur) {
            continue;
        }
        if(!first_aur_source_position.has_value()) {
            first_aur_source_position = source_position;
        }
        aur_package_names.push_back(
                resolved_identities[source_position]->requested_name);
    }
    if(!aur_package_names.empty()) {
        try {
            state.aur_invocation_plan.emplace(resolve_build_plan(
                    aur_package_names, select_provider));
            require_executable_install_plan(
                    join_package_names(aur_package_names),
                    state.aur_invocation_plan.value());
        } catch(const std::exception& error) {
            const std::size_t source_position =
                    first_aur_source_position.value();
            RegisteredSourcePreferenceSnapshot& source =
                    state.snapshot.registered_sources[source_position];
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
            const std::size_t source_position =
                    first_aur_source_position.value();
            RegisteredSourcePreferenceSnapshot& source =
                    state.snapshot.registered_sources[source_position];
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::
                            SourceWorkItemPreparationFailed,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    localization::translate_message(
                            "Source invocation constraint preflight failed with an unknown exception."));
            attribute_issue_to_source(issue, source);
            return block_preparation(
                    std::move(state), std::move(issue), source_position,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
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
        if(!resolved_identities[source_position].has_value()) continue;

        try {
            correlation.work_item_index = source_work_items.size();
            source_work_items.push_back(
                    prepare_resolved_source_build_work_item(
                            resolved_identities[source_position].value(),
                            source.environment.value(),
                            true,
                            state.snapshot.options.needed,
                            select_provider));
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
                    localization::translate_message(
                            "Source work-item preparation failed with an unknown exception."));
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
                    localization::translate_message(
                            "Source invocation preparation failed with an unknown exception."));
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::UnknownException);
        }

        // POLICY(#272): registered-source provider choiceとstatic invocation
        // preflightを確定してから、最初のcache filesystem mutationへ進む。
        try {
            if(cache_root.has_value()) {
                cache_root->require_unchanged_identity();
            } else {
                cache_root = prepare_process_cache_root();
            }
            seed_production_source_build_cache(
                    state.source_invocation.value(), cache_root.value());
        } catch(const xdg_paths::ResolutionError& error) {
            SystemSourceUpgradeIssue issue =
                    make_cache_authority_issue(error.what());
            issue.cache_resolution_failure = error.failure();
            return block_cache_authority_preparation(
                    std::move(state), std::move(issue));
        } catch(const xdg_directory_safety::PreparationError& error) {
            SystemSourceUpgradeIssue issue =
                    make_cache_authority_issue(error.what());
            issue.cache_preparation_failure = error.failure();
            return block_cache_authority_preparation(
                    std::move(state), std::move(issue));
        } catch(const TrustedCacheError& error) {
            SystemSourceUpgradeIssue issue =
                    make_cache_authority_issue(error.what());
            issue.trusted_cache_failure = error.failure();
            return block_cache_authority_preparation(
                    std::move(state), std::move(issue));
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
                localization::translate_message(
                        "The prepared system/source upgrade is invalid or has already been consumed."));
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
                localization::translate_message(
                        "The prepared system/source upgrade options differ from the execution options."));
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
                localization::translate_message(
                        "The prepared system/source upgrade source correlation is inconsistent."));
        return result;
    }

    // Registered source workがある場合はsystem pacmanより前にcache authorityを
    // 1回だけ確定し、typed failureをresultへ保持する。
    if(state.source_invocation.has_value()) {
        try {
            activate_production_source_build_cache(
                    state.source_invocation.value());
        } catch(const TrustedCacheError& error) {
            SystemSourceUpgradeIssue issue = make_issue(
                    SystemSourceUpgradeIssueKind::CacheAuthorityInvalid,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation,
                    error.what());
            issue.trusted_cache_failure = error.failure();
            return block_preparation(
                    std::move(state), std::move(issue), std::nullopt,
                    RegisteredSourceUpgradeFailureKind::
                            CacheAuthorityFailure);
        }
    }

    // public snapshot/result detailだけを移し、executionに必要なprepared
    // invocation/correlation/baselineはone-shot capability内へ残す。
    SystemSourceUpgradeResult result = make_result_from_state(
            std::move(state),
            SystemSourceUpgradeStatus::Completed,
            SystemSourceUpgradePhase::None);

    SystemSourceUpgradePhase active_phase = SystemSourceUpgradePhase::System;
    std::optional<std::size_t> active_source_position;
    bool system_package_state_finalized = false;
    try {
        notify(observer, SystemSourceUpgradeEvent{
            SystemSourceUpgradeEventKind::SystemUpgradeStarting,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            {}});
#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
        throw_unexpected_exception_for_test(
                state,
                SystemSourceUpgradeUnexpectedExceptionPoint::
                        SystemPhaseStarted);
#endif
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
            result.system.diagnostic = unknown_system_failure_diagnostic();
            result.status = SystemSourceUpgradeStatus::StoppedOnSystemFailure;
            result.stopped_phase = SystemSourceUpgradePhase::System;
            result.diagnostics.push_back(make_diagnostic(
                    SystemSourceUpgradePhase::System,
                    unknown_system_failure_diagnostic(),
                    true));
            return result;
        }
        result.system.command_exit_status = system_exit_status;
        if(system_exit_status != 0) {
            result.system.status = SystemUpgradePhaseStatus::Failed;
            result.system.package_state_change = PackageStateChange::Unknown;
            result.system.diagnostic = localization::translate_message(
                    "The update failed.");
            result.status = SystemSourceUpgradeStatus::StoppedOnSystemFailure;
            result.stopped_phase = SystemSourceUpgradePhase::System;
            result.diagnostics.push_back(make_diagnostic(
                    SystemSourceUpgradePhase::System,
                    localization::translate_message("The update failed."),
                    true));
            return result;
        }
        result.system.status = SystemUpgradePhaseStatus::Completed;
#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
        throw_unexpected_exception_for_test(
                state,
                SystemSourceUpgradeUnexpectedExceptionPoint::
                        SystemPhaseCompleted);
#endif

        const bool has_source_work_items = state.source_invocation.has_value();
        if(has_source_work_items) {
            active_phase = SystemSourceUpgradePhase::RegisteredSource;
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
                system_package_state_finalized = true;

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
                if(!system_package_state_finalized) {
                    if(state.before_system_snapshot.has_value()) {
                        record_post_system_snapshot_failure(
                                result, error.failure());
                    } else {
                        result.system.package_state_change =
                                PackageStateChange::Unknown;
                    }
                    system_package_state_finalized = true;
                }
                if(has_source_work_items) {
                    const std::string diagnostic =
                            post_upgrade_snapshot_failure_diagnostic(
                                    error.failure().diagnostic);
                    stop_for_global_source_metadata_failure(
                            result, error.failure(), diagnostic);
                    return result;
                }
            } catch(const std::exception& error) {
                if(!system_package_state_finalized) {
                    if(state.before_system_snapshot.has_value()) {
                        record_post_system_snapshot_failure(
                                result,
                                generic_metadata_failure(error.what()));
                    } else {
                        result.system.package_state_change =
                                PackageStateChange::Unknown;
                    }
                    system_package_state_finalized = true;
                }
                if(has_source_work_items) {
                    stop_for_global_source_metadata_failure(
                            result,
                            std::nullopt,
                            post_upgrade_snapshot_failure_diagnostic(
                                    error.what()));
                    return result;
                }
            } catch(...) {
                const std::string diagnostic = localization::translate_message(
                        "The post-upgrade package metadata snapshot failed with an unknown exception.");
                if(!system_package_state_finalized) {
                    if(state.before_system_snapshot.has_value()) {
                        record_post_system_snapshot_failure(
                                result,
                                generic_metadata_failure(diagnostic));
                    } else {
                        result.system.package_state_change =
                                PackageStateChange::Unknown;
                    }
                    system_package_state_finalized = true;
                }
                if(has_source_work_items) {
                    stop_for_global_source_metadata_failure(
                            result,
                            std::nullopt,
                            post_upgrade_snapshot_failure_diagnostic(
                                    diagnostic));
                    return result;
                }
            }
        } else if(result.system.before_snapshot_failure.has_value()) {
            result.system.package_state_change = PackageStateChange::Unknown;
        } else {
            result.system.package_state_change = PackageStateChange::Unknown;
        }
        system_package_state_finalized = true;

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
                        localization::translate_message(
                                "The prepared source work item is missing after the system upgrade."));
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
                        localization::format_translated_message(
                                "The system upgrade completed, but the authoritative post-upgrade installed package snapshot is missing for {}; source processing did not start.",
                                work_item.request.package_name));
                return result;
            }
            if(const auto* metadata_failure =
                       std::get_if<PackageMetadataFailure>(
                               &installed_snapshot->second)) {
                const std::string diagnostic =
                        localization::format_translated_message(
                                "The system upgrade completed, but the post-upgrade package metadata query failed for {}: {} Source processing did not start.",
                                work_item.request.package_name,
                                metadata_failure->diagnostic);
                stop_for_source_metadata_failure(
                        result,
                        source_position,
                        *metadata_failure,
                        diagnostic);
                return result;
            }
        }

        if(state.source_invocation.has_value()) {
            try {
                // POLICY(#272): system phaseと全post-system metadata guardの後、
                // registered-source checkout/buildより前にphase全体で1回行う。
                result.selected_repository_provider_transaction =
                        execute_selected_repository_provider_transaction(
                                state.source_invocation.value(), config);
                if(!result.selected_repository_provider_transaction.
                           is_success()) {
                    stop_for_repository_provider_transaction_failure(
                            result,
                            result.selected_repository_provider_transaction.
                                    diagnostic.value_or(
                                            localization::translate_message(
                                                    "Failed to install selected repository providers.")));
                    return result;
                }
            } catch(const TrustedCacheError& error) {
                result.selected_repository_provider_transaction.status =
                        SelectedRepositoryProviderTransactionStatus::
                                BlockedBeforeExecution;
                result.selected_repository_provider_transaction.
                                selected_providers =
                        state.source_invocation->
                                selected_repository_providers;
                result.selected_repository_provider_transaction.
                                package_state_change =
                        PackageStateChange::NoChange;
                result.selected_repository_provider_transaction.diagnostic =
                        error.what();
                SystemSourceUpgradeIssue issue = make_issue(
                        SystemSourceUpgradeIssueKind::CacheAuthorityInvalid,
                        SystemSourceUpgradeIssueImpact::BlocksExecution,
                        SystemSourceUpgradePhase::RegisteredSource,
                        error.what());
                issue.trusted_cache_failure = error.failure();
                result.issues.push_back(std::move(issue));
                result.diagnostics.push_back(make_diagnostic(
                        SystemSourceUpgradePhase::RegisteredSource,
                        error.what(), true));
                result.status =
                        SystemSourceUpgradeStatus::StoppedOnSourceFailure;
                result.stopped_phase =
                        SystemSourceUpgradePhase::RegisteredSource;
                return result;
            } catch(const std::exception& error) {
                result.selected_repository_provider_transaction.status =
                        SelectedRepositoryProviderTransactionStatus::
                                BlockedBeforeExecution;
                result.selected_repository_provider_transaction.
                                selected_providers =
                        state.source_invocation->
                                selected_repository_providers;
                result.selected_repository_provider_transaction.diagnostic =
                        error.what();
                stop_for_repository_provider_transaction_failure(
                        result, error.what());
                return result;
            } catch(...) {
                const std::string diagnostic =
                        localization::translate_message(
                                "Failed to install selected repository providers with an unknown exception.");
                result.selected_repository_provider_transaction.status =
                        SelectedRepositoryProviderTransactionStatus::
                                BlockedBeforeExecution;
                result.selected_repository_provider_transaction.
                                selected_providers =
                        state.source_invocation->
                                selected_repository_providers;
                result.selected_repository_provider_transaction.diagnostic =
                        diagnostic;
                stop_for_repository_provider_transaction_failure(
                        result, diagnostic);
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
                        localization::format_translated_message(
                                "Ignoring invalid source-build preference filename: {}",
                                source_result.preference_package_name);
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

            active_source_position = source_position;
#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
            throw_unexpected_exception_for_test(
                    state,
                    SystemSourceUpgradeUnexpectedExceptionPoint::
                            SourceWorkItemStarted);
#endif
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
            } catch(const TrustedCacheError& error) {
                source_result.status = RegisteredSourceUpgradeStatus::Failed;
                source_result.failure_kind =
                        RegisteredSourceUpgradeFailureKind::
                                CacheAuthorityFailure;
                source_result.package_state_change =
                        PackageStateChange::Unknown;
                source_result.diagnostic = error.what();

                SystemSourceUpgradeIssue issue = make_issue(
                        SystemSourceUpgradeIssueKind::CacheAuthorityInvalid,
                        SystemSourceUpgradeIssueImpact::BlocksExecution,
                        SystemSourceUpgradePhase::RegisteredSource,
                        error.what());
                issue.original_preference_index =
                        source_result.original_preference_index;
                issue.preference_package_name =
                        source_result.preference_package_name;
                issue.resolved_package_base =
                        source_result.resolved_package_base;
                issue.trusted_cache_failure = error.failure();
                result.issues.push_back(std::move(issue));

                SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                        SystemSourceUpgradePhase::RegisteredSource,
                        error.what(), true);
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
                source_result.diagnostic =
                        unknown_source_failure_diagnostic();
                SystemSourceUpgradeDiagnostic detail = make_diagnostic(
                        SystemSourceUpgradePhase::RegisteredSource,
                        unknown_source_failure_diagnostic(),
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
#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
            throw_unexpected_exception_for_test(
                    state,
                    SystemSourceUpgradeUnexpectedExceptionPoint::
                            SourceResultRecorded);
#endif
            active_source_position.reset();
        }

        return result;
    } catch(const std::exception& error) {
        record_unexpected_execution_failure(
                result,
                active_phase,
                active_source_position,
                system_package_state_finalized,
                error.what());
        return result;
    } catch(...) {
        record_unexpected_execution_failure(
                result,
                active_phase,
                active_source_position,
                system_package_state_finalized,
                localization::translate_message(
                        "An unknown exception occurred."));
        return result;
    }
}
