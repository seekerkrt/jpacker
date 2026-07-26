#include "commands_aur_update.hpp"

#include "app_config.hpp"
#include "aur_update_execution_preflight.hpp"
#include "aur_update_execution_preparation.hpp"
#include "aur_update_execution_runner.hpp"
#include "aur_update_operation_result.hpp"
#include "aur_update_query.hpp"
#include "logging.hpp"
#include "source_install.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string_view operation_status_label(AurUpdateOperationStatus status) {
    switch(status) {
    case AurUpdateOperationStatus::NoUpdates:
        return "no updates";
    case AurUpdateOperationStatus::Completed:
        return "completed";
    case AurUpdateOperationStatus::BlockedBeforeExecution:
        return "blocked before execution";
    case AurUpdateOperationStatus::StoppedOnWorkItemFailure:
        return "stopped after work-item failure";
    case AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure:
        return "stopped after cleanup failure";
    case AurUpdateOperationStatus::InconsistentResult:
        return "inconsistent result";
    }
    throw std::logic_error("Unknown AUR update operation status.");
}

std::string_view preflight_reason_label(AurUpdateExecutionReason reason) {
    switch(reason) {
    case AurUpdateExecutionReason::None:
        return "none";
    case AurUpdateExecutionReason::UpToDate:
        return "up to date";
    case AurUpdateExecutionReason::NonAurForeign:
        return "non-AUR foreign";
    case AurUpdateExecutionReason::AurMetadataUnavailable:
        return "AUR metadata unavailable";
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
        return "version comparison unavailable";
    case AurUpdateExecutionReason::InstalledReasonUnknown:
        return "installed reason unknown";
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
        return "update plan inconsistent";
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
        return "duplicate update target";
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
        return "repository metadata unavailable";
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
        return "AUR dependency metadata unavailable";
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
        return "provider metadata unavailable";
    case AurUpdateExecutionReason::UnresolvedDependency:
        return "unresolved dependency";
    case AurUpdateExecutionReason::VersionConstraintUnverified:
        return "version constraint unverified";
    case AurUpdateExecutionReason::DependencyCycle:
        return "dependency cycle";
    case AurUpdateExecutionReason::BuildPlanInconsistent:
        return "build plan inconsistent";
    case AurUpdateExecutionReason::PackageBaseMismatch:
        return "package base mismatch";
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
        return "split package selection required";
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
        return "multiple package targets for package base";
    case AurUpdateExecutionReason::AmbiguousProvider:
        return "ambiguous provider";
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return "conflicts/replaces unresolved";
    }
    throw std::logic_error("Unknown AUR update preflight reason.");
}

std::string_view preparation_reason_label(AurUpdatePreparationReason reason) {
    switch(reason) {
    case AurUpdatePreparationReason::None:
        return "none";
    case AurUpdatePreparationReason::BlockingPreflight:
        return "blocking preflight";
    case AurUpdatePreparationReason::PreflightInconsistent:
        return "preflight inconsistent";
    case AurUpdatePreparationReason::BuildPlanMissing:
        return "build plan missing";
    case AurUpdatePreparationReason::BuildPlanOrderEmpty:
        return "build plan order empty";
    case AurUpdatePreparationReason::RootAttributionInconsistent:
        return "root attribution inconsistent";
    case AurUpdatePreparationReason::PackageTargetAttributionInconsistent:
        return "package target attribution inconsistent";
    case AurUpdatePreparationReason::DesiredInstallReasonMissing:
        return "desired install reason missing";
    case AurUpdatePreparationReason::SourcePreferenceUnavailable:
        return "source preference unavailable";
    case AurUpdatePreparationReason::SourcePreferencePkgdestConflict:
        return "source preference PKGDEST conflict";
    case AurUpdatePreparationReason::StaticWorkItemInvalid:
        return "static work item invalid";
    case AurUpdatePreparationReason::PacmanDatabaseUnavailable:
        return "pacman database unavailable";
    case AurUpdatePreparationReason::GenericPreparationInconsistent:
        return "generic preparation inconsistent";
    }
    throw std::logic_error("Unknown AUR update preparation reason.");
}

std::string_view execution_failure_label(AurUpdateWorkItemFailureKind kind) {
    switch(kind) {
    case AurUpdateWorkItemFailureKind::None:
        return "none";
    case AurUpdateWorkItemFailureKind::BuildOrInstallFailed:
        return "build or install failed";
    case AurUpdateWorkItemFailureKind::CleanupFailedAfterPackageTransaction:
        return "cleanup failed after package transaction";
    case AurUpdateWorkItemFailureKind::UnknownException:
        return "unknown exception";
    case AurUpdateWorkItemFailureKind::PriorWorkItemStopped:
        return "prior work item stopped";
    }
    throw std::logic_error("Unknown AUR update execution failure kind.");
}

bool should_print_execution_failure_detail(
        AurUpdateWorkItemFailureKind kind) {
    switch(kind) {
    case AurUpdateWorkItemFailureKind::None:
        return false;
    case AurUpdateWorkItemFailureKind::BuildOrInstallFailed:
    case AurUpdateWorkItemFailureKind::CleanupFailedAfterPackageTransaction:
    case AurUpdateWorkItemFailureKind::UnknownException:
        return true;
    case AurUpdateWorkItemFailureKind::PriorWorkItemStopped:
        // POLICY(#267): fail-fastから派生した未実行状態はtarget-level表示へ集約する。
        return false;
    }
    throw std::logic_error("Unknown AUR update execution failure kind.");
}

std::string_view reduction_stage_label(AurUpdateOperationReductionStage stage) {
    switch(stage) {
    case AurUpdateOperationReductionStage::Preflight:
        return "preflight";
    case AurUpdateOperationReductionStage::Preparation:
        return "preparation";
    case AurUpdateOperationReductionStage::Execution:
        return "execution";
    }
    throw std::logic_error("Unknown AUR update reduction stage.");
}

std::string_view reduction_reason_label(AurUpdateOperationReductionReason reason) {
    switch(reason) {
    case AurUpdateOperationReductionReason::DuplicatePreflightUpdatePlanIndex:
        return "duplicate preflight update plan index";
    case AurUpdateOperationReductionReason::OutOfRangePreflightUpdatePlanIndex:
        return "out-of-range preflight update plan index";
    case AurUpdateOperationReductionReason::PreflightTargetOrderInconsistent:
        return "preflight target order inconsistent";
    case AurUpdateOperationReductionReason::DuplicatePreparationAttribution:
        return "duplicate preparation attribution";
    case AurUpdateOperationReductionReason::UnknownPreparationUpdatePlanIndex:
        return "unknown preparation update plan index";
    case AurUpdateOperationReductionReason::PreparationAttributionInconsistent:
        return "preparation attribution inconsistent";
    case AurUpdateOperationReductionReason::PreparationTargetSnapshotInconsistent:
        return "preparation target snapshot inconsistent";
    case AurUpdateOperationReductionReason::DuplicateExecutionWorkItemIndex:
        return "duplicate execution work item index";
    case AurUpdateOperationReductionReason::ExecutionWorkItemOrderInconsistent:
        return "execution work item order inconsistent";
    case AurUpdateOperationReductionReason::DuplicateExecutionAttribution:
        return "duplicate execution attribution";
    case AurUpdateOperationReductionReason::UnknownExecutionUpdatePlanIndex:
        return "unknown execution update plan index";
    case AurUpdateOperationReductionReason::MissingExecutionAttribution:
        return "missing execution attribution";
    case AurUpdateOperationReductionReason::ExecutionResultWithPreparationIssues:
        return "execution result with preparation issues";
    case AurUpdateOperationReductionReason::MissingExecutionResult:
        return "missing execution result";
    case AurUpdateOperationReductionReason::UnknownEnumValue:
        return "unknown enum value";
    case AurUpdateOperationReductionReason::WorkItemResultInconsistent:
        return "work item result inconsistent";
    case AurUpdateOperationReductionReason::InvocationResultInconsistent:
        return "invocation result inconsistent";
    case AurUpdateOperationReductionReason::OtherCorrelationInconsistent:
        return "other correlation inconsistency";
    }
    throw std::logic_error("Unknown AUR update reduction reason.");
}

std::string target_reason_label(const AurUpdateOperationTargetResult& target) {
    if(!target.preflight_issues.empty()) {
        return std::string(
                preflight_reason_label(target.preflight_issues.front().reason));
    }
    if(!target.preparation_issues.empty()) {
        return std::string(preparation_reason_label(
                target.preparation_issues.front().reason));
    }
    return "reason unavailable";
}

std::string target_failure_diagnostic(const AurUpdateOperationTargetResult& target) {
    if(target.execution_diagnostic.has_value() &&
       !target.execution_diagnostic->empty()) {
        return *target.execution_diagnostic;
    }
    for(const auto& issue : target.preparation_issues) {
        if(!issue.diagnostic.empty()) return issue.diagnostic;
    }
    for(const auto& issue : target.preflight_issues) {
        if(!issue.diagnostic.empty()) return issue.diagnostic;
    }
    for(const auto& contribution : target.execution_contributions) {
        if(contribution.diagnostic.has_value() &&
           !contribution.diagnostic->empty()) {
            return *contribution.diagnostic;
        }
    }
    return "diagnostic unavailable";
}

std::string target_status_label(
        const AurUpdateOperationTargetResult& target,
        AurUpdateOperationStatus operation_status) {
    switch(target.status) {
    case AurUpdateOperationTargetStatus::Updated:
        return "updated";
    case AurUpdateOperationTargetStatus::NoChange:
        return "no change";
    case AurUpdateOperationTargetStatus::Skipped:
        return "skipped: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Unsupported:
        return "unsupported: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Incomplete:
        return "incomplete: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Failed:
        return "failed: " + target_failure_diagnostic(target);
    case AurUpdateOperationTargetStatus::UpdatedCleanupFailed:
        return "updated, but cleanup failed";
    case AurUpdateOperationTargetStatus::NoChangeCleanupFailed:
        return "no package change, but cleanup failed";
    case AurUpdateOperationTargetStatus::NotAttempted:
        if(target.execution_failure_kind ==
           AurUpdateWorkItemFailureKind::PriorWorkItemStopped) {
            return "not attempted: prior work item stopped";
        }
        if(operation_status ==
           AurUpdateOperationStatus::BlockedBeforeExecution) {
            return "not attempted: operation blocked before execution";
        }
        if(operation_status == AurUpdateOperationStatus::InconsistentResult) {
            return "not attempted: result inconsistent";
        }
        return "not attempted: prior work item stopped";
    }
    throw std::logic_error("Unknown AUR update target status.");
}

bool is_normal_skip_reason(AurUpdateExecutionReason reason) {
    switch(reason) {
    case AurUpdateExecutionReason::UpToDate:
    case AurUpdateExecutionReason::NonAurForeign:
        return true;
    case AurUpdateExecutionReason::None:
    case AurUpdateExecutionReason::AurMetadataUnavailable:
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
    case AurUpdateExecutionReason::InstalledReasonUnknown:
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
    case AurUpdateExecutionReason::UnresolvedDependency:
    case AurUpdateExecutionReason::VersionConstraintUnverified:
    case AurUpdateExecutionReason::DependencyCycle:
    case AurUpdateExecutionReason::BuildPlanInconsistent:
    case AurUpdateExecutionReason::PackageBaseMismatch:
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
    case AurUpdateExecutionReason::AmbiguousProvider:
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return false;
    }
    throw std::logic_error("Unknown AUR update preflight reason.");
}

std::string join_package_names(const std::vector<std::string>& package_names) {
    if(package_names.empty()) return "unknown packages";

    std::string joined;
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        if(index != 0) joined += ", ";
        joined += package_names[index];
    }
    return joined;
}

void print_preflight_issues(const AurUpdateOperationResult& result) {
    for(const auto& target : result.targets) {
        for(const auto& issue : target.preflight_issues) {
            if(is_normal_skip_reason(issue.reason)) continue;
            Logger::error(
                    "  preflight issue: " +
                    std::string(preflight_reason_label(issue.reason)) +
                    ": " + issue.diagnostic);
        }
    }
}

void print_preparation_details(const AurUpdateOperationResult& result) {
    for(const auto& warning : result.preparation_warnings) {
        const std::string preference = warning.preference_name.empty()
                ? "unknown preference"
                : warning.preference_name;
        Logger::warn(
                "  preparation warning: " + preference + ": " +
                warning.diagnostic);
    }
    for(const auto& issue : result.preparation_issues) {
        Logger::error(
                "  preparation issue: " +
                std::string(preparation_reason_label(issue.reason)) + ": " +
                issue.diagnostic);
    }
}

void print_execution_failures(const AurUpdateOperationResult& result) {
    std::set<std::size_t> printed_work_items;
    for(const auto& target : result.targets) {
        if(!target.execution_failure_kind.has_value() ||
           !should_print_execution_failure_detail(
                   *target.execution_failure_kind)) {
            continue;
        }
        if(target.execution_work_item_index.has_value() &&
           !printed_work_items.insert(*target.execution_work_item_index).second) {
            continue;
        }
        Logger::error(
                "  execution failure: " +
                std::string(execution_failure_label(
                        *target.execution_failure_kind)) +
                ": " + target_failure_diagnostic(target));
    }

    for(const auto& work_item : result.execution_work_items) {
        if(!should_print_execution_failure_detail(work_item.failure_kind) ||
           printed_work_items.contains(work_item.work_item_index)) {
            continue;
        }
        const std::string diagnostic = work_item.diagnostic.has_value() &&
                        !work_item.diagnostic->empty()
                ? *work_item.diagnostic
                : "diagnostic unavailable";
        Logger::error(
                "  execution failure: " +
                std::string(execution_failure_label(work_item.failure_kind)) +
                ": " + diagnostic);
    }
}

void print_reduction_issues(const AurUpdateOperationResult& result) {
    for(const auto& issue : result.reduction_issues) {
        Logger::error(
                "  reduction issue: " +
                std::string(reduction_stage_label(issue.stage)) + ": " +
                std::string(reduction_reason_label(issue.reason)) + ": " +
                issue.diagnostic);
    }
}

void print_query_failures(const AurUpdateQueryResult& query_result) {
    for(const auto& failure : query_result.recoverable_failures) {
        Logger::error(
                "AUR update query failure for " +
                join_package_names(failure.package_names) + ": " +
                failure.diagnostic);
    }
}

void print_operation_result(
        const AurUpdateQueryResult& query_result,
        const AurUpdateOperationResult& result) {
    std::cout << "AUR update: " << operation_status_label(result.status)
              << std::endl;
    for(const auto& target : result.targets) {
        std::cout << target.update.installed_name << ": "
                  << target_status_label(target, result.status) << std::endl;
    }

    print_preflight_issues(result);
    print_preparation_details(result);
    print_execution_failures(result);
    print_reduction_issues(result);

    if(result.has_partial_completion()) {
        std::cout << "AUR update partially completed before failure."
                  << std::endl;
    }
    if(result.has_cleanup_failure()) {
        std::cout << "AUR update cleanup failed after a package transaction."
                  << std::endl;
    }
    if(result.has_not_attempted_targets()) {
        std::cout << "AUR update has targets that were not attempted."
                  << std::endl;
    }

    print_query_failures(query_result);
    if(!query_result.recoverable_failures.empty() && result.is_success()) {
        Logger::error(
                "AUR update completed, but query failures were reported.");
    }
}

bool target_status_is_success(AurUpdateOperationTargetStatus status) {
    switch(status) {
    case AurUpdateOperationTargetStatus::Updated:
    case AurUpdateOperationTargetStatus::NoChange:
    case AurUpdateOperationTargetStatus::Skipped:
        return true;
    case AurUpdateOperationTargetStatus::Unsupported:
    case AurUpdateOperationTargetStatus::Incomplete:
    case AurUpdateOperationTargetStatus::Failed:
    case AurUpdateOperationTargetStatus::UpdatedCleanupFailed:
    case AurUpdateOperationTargetStatus::NoChangeCleanupFailed:
    case AurUpdateOperationTargetStatus::NotAttempted:
        return false;
    }
    throw std::logic_error("Unknown AUR update target status.");
}

bool work_item_status_is_success(AurUpdateWorkItemExecutionStatus status) {
    switch(status) {
    case AurUpdateWorkItemExecutionStatus::Updated:
    case AurUpdateWorkItemExecutionStatus::NoChange:
        return true;
    case AurUpdateWorkItemExecutionStatus::Failed:
    case AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NotAttempted:
        return false;
    }
    throw std::logic_error("Unknown AUR update work item status.");
}

bool invocation_status_matches_operation(
        AurUpdateOperationStatus operation_status,
        const std::optional<AurUpdateInvocationExecutionStatus>& status) {
    switch(operation_status) {
    case AurUpdateOperationStatus::NoUpdates:
        return !status.has_value();
    case AurUpdateOperationStatus::Completed:
        if(!status.has_value()) return false;
        switch(*status) {
        case AurUpdateInvocationExecutionStatus::Completed:
            return true;
        case AurUpdateInvocationExecutionStatus::StoppedOnWorkItemFailure:
        case AurUpdateInvocationExecutionStatus::
                StoppedAfterPackageCleanupFailure:
            return false;
        }
        throw std::logic_error("Unknown AUR update invocation status.");
    case AurUpdateOperationStatus::BlockedBeforeExecution:
    case AurUpdateOperationStatus::StoppedOnWorkItemFailure:
    case AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure:
    case AurUpdateOperationStatus::InconsistentResult:
        return false;
    }
    throw std::logic_error("Unknown AUR update operation status.");
}

bool command_succeeded(
    const AurUpdateQueryResult& query_result,
        const AurUpdateOperationResult& result) {
    if(!result.is_success() ||
       !invocation_status_matches_operation(
               result.status, result.execution_status) ||
       !query_result.recoverable_failures.empty() ||
       !result.preparation_issues.empty() || !result.reduction_issues.empty() ||
       result.has_blocking_targets() || result.has_cleanup_failure() ||
       result.has_not_attempted_targets()) {
        return false;
    }
    if(!std::all_of(
               result.targets.begin(), result.targets.end(),
               [](const AurUpdateOperationTargetResult& target) {
                   return target_status_is_success(target.status);
               })) {
        return false;
    }
    return std::all_of(
            result.execution_work_items.begin(),
            result.execution_work_items.end(),
            [](const AurUpdateWorkItemExecutionResult& work_item) {
                return work_item_status_is_success(work_item.status) &&
                       work_item.failure_kind ==
                               AurUpdateWorkItemFailureKind::None;
            });
}

} // namespace

int cmd_upgrade_aur(const AppConfig& config) {
    // POLICY(#267): NoUpdatesでもunsupported optionを成功へ落とさない。
    require_supported_production_source_build_options(config);

    AurUpdateQueryResult query_result = query_installed_aur_updates();
    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(query_result.plan);
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, false, config);

    std::optional<AurUpdateSourceBuildExecutionResult> execution;
    if(preparation.is_prepared()) {
        // LANDMINE(#267): invocation capabilityだけをconsumeし、issue/warning/
        // attributionを含むmoved-from preparation snapshotはreducerへ残す。
        execution.emplace(
                execute_prepared_aur_update_source_build_invocation(
                        std::move(*preparation.invocation), config));
    }

    AurUpdateOperationResult result = reduce_aur_update_operation_result(
            preflight, preparation, execution);
    print_operation_result(query_result, result);
    return command_succeeded(query_result, result) ? 0 : 1;
}
