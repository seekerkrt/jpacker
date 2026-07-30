#include "commands_aur_update.hpp"

#include "app_config.hpp"
#include "aur_update_cli_presentation.hpp"
#include "cache_authority.hpp"
#include "filtered_aur_update_operation.hpp"
#include "logging.hpp"
#include "source_install.hpp"

#include <iostream>
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
    case AurUpdatePreparationReason::BuildUnitSelectionInconsistent:
        return "build unit selection inconsistent";
    case AurUpdatePreparationReason::ExternalSatisfactionInconsistent:
        return "external satisfaction inconsistent";
    }
    throw std::logic_error("Unknown AUR update preparation reason.");
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
    case AurUpdateOperationReductionReason::
            DuplicateExecutionChildAttribution:
        return "duplicate execution child attribution";
    case AurUpdateOperationReductionReason::
            MissingExecutionChildAttribution:
        return "missing execution child attribution";
    case AurUpdateOperationReductionReason::
            UnexpectedExecutionChildAttribution:
        return "unexpected execution child attribution";
    case AurUpdateOperationReductionReason::
            UnknownExecutionChildUpdatePlanIndex:
        return "unknown execution child update plan index";
    case AurUpdateOperationReductionReason::
            ExecutionChildSnapshotInconsistent:
        return "execution child snapshot inconsistent";
    case AurUpdateOperationReductionReason::UnexpectedSelectedArtifact:
        return "unexpected selected artifact";
    case AurUpdateOperationReductionReason::
            UnexpectedUnselectedArtifactIdentity:
        return "unexpected unselected artifact identity";
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
        return "failed: " + aur_update_cli_target_failure_summary(target);
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
    // Child/work-item enumやselected identityのincoherenceは、success summaryを
    // 一行でも出す前にfail-closedにする。
    const AurUpdateCliPresentation execution_presentation =
            format_aur_update_cli_presentation(result);

    std::cout << "AUR update: " << operation_status_label(result.status)
              << std::endl;
    for(const auto& target : result.targets) {
        std::cout << target.update.installed_name << ": "
                  << target_status_label(target, result.status) << std::endl;
    }
    for(const std::string& line : execution_presentation.summary_lines) {
        std::cout << line << std::endl;
    }

    print_preflight_issues(result);
    print_preparation_details(result);
    for(const std::string& line : execution_presentation.error_lines) {
        Logger::error(line);
    }
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

} // namespace

int cmd_upgrade_aur(const AppConfig& config) {
    // POLICY(#267): NoUpdatesでもunsupported optionを成功へ落とさない。
    require_supported_production_source_build_options(config);

    // Validなupgrade-aurはcache-capable mutation routeである。query/curlより
    // 前にauthorityを確定し、NoUpdatesでも同じfail-closed順序を保つ。
    ValidatedCacheRoot cache_root = prepare_process_cache_root();
    AurUpdateQueryResult query_result = query_installed_aur_updates();
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    std::move(query_result), {}, config, cache_root);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);

    // POLICY(#281): upgrade-aur presentationはlegacy reducer resultを正本にし、
    // filtered boundary固有のplanner/mapping detailをcommand outputへ追加しない。
    print_operation_result(
            result.query_result, result.reduced_operation_result);
    return result.is_success() ? 0 : 1;
}
