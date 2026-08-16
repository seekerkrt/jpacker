#include "upgrade_all_operation.hpp"

#include "operation_state_model.hpp"

#include <algorithm>

namespace {

PackageStateChange aur_package_state_change(
        const UpgradeAllAurPhaseResult& aur) noexcept {
    if(!aur.operation_result.has_value()) return PackageStateChange::Unknown;

    const FilteredAurUpdateExecutionResult& filtered =
            *aur.operation_result;
    if(filtered.package_state_change() == PackageStateChange::Changed) {
        return PackageStateChange::Changed;
    }
    if(filtered.package_state_change() == PackageStateChange::Unknown) {
        return PackageStateChange::Unknown;
    }
    if((aur.status == UpgradeAllAurPhaseStatus::NoUpdates ||
        aur.status == UpgradeAllAurPhaseStatus::Completed) &&
       filtered.is_success()) {
        return PackageStateChange::NoChange;
    }
    return PackageStateChange::Unknown;
}

std::optional<NoOpBasis> upgrade_all_no_op_basis(
        const UpgradeAllOperationResult& result,
        PackageStateChange package_state_change) noexcept {
    if(result.status != UpgradeAllOperationStatus::NoUpdates ||
       !result.is_success() ||
       package_state_change != PackageStateChange::NoChange) {
        return std::nullopt;
    }

    const bool has_registered_source_work =
            !result.system_source.registered_source_results.empty();
    bool has_aur_target_work = false;
    if(result.aur.operation_result.has_value()) {
        const FilteredAurUpdateExecutionResult& filtered =
                result.aur.operation_result.value();
        has_aur_target_work =
                !filtered.query_result.plan.entries.empty() ||
                !filtered.target_adapter.entries.empty() ||
                !filtered.reduced_operation_result.targets.empty();
    }

    return has_registered_source_work || has_aur_target_work
            ? NoOpBasis::VerifiedUnchanged
            : NoOpBasis::NoRelevantWork;
}

} // namespace

OperationStateProjection project_upgrade_all_operation_state(
        const UpgradeAllOperationResult& result) noexcept {
    const PackageStateChange package_state_change =
            result.package_state_change();

    ObservationReason unverified_reason =
            ObservationReason::ObservationNotPrepared;
    if(result.system_source.system.before_snapshot_failure.has_value()) {
        unverified_reason = ObservationReason::BeforeSnapshotUnavailable;
    } else if(result.system_source.system.after_snapshot_failure.has_value()) {
        unverified_reason = ObservationReason::AfterSnapshotUnavailable;
    } else if(result.has_inconsistency()) {
        unverified_reason = ObservationReason::InconsistentEvidence;
    } else if(result.has_not_attempted_phase()) {
        unverified_reason = ObservationReason::PhaseNotAttempted;
    } else if(!result.is_success()) {
        unverified_reason = ObservationReason::OperationFailed;
    }

    return project_operation_state(OperationStateProjectionInput{
            result.is_success(),
            upgrade_all_no_op_basis(result, package_state_change),
            result.status == UpgradeAllOperationStatus::BlockedBeforeMutation,
            result.has_partial_completion(),
            result.status != UpgradeAllOperationStatus::BlockedBeforeMutation,
            result.has_inconsistency(),
            package_state_change,
            unverified_reason});
}

bool UpgradeAllOperationResult::is_success() const noexcept {
    if(status != UpgradeAllOperationStatus::Completed &&
       status != UpgradeAllOperationStatus::NoUpdates) {
        return false;
    }

    // POLICY(#281): aggregate statusだけを成功の根拠にしない。syntheticな
    // inconsistent resultもCLIで成功へ丸めないよう、nested typed resultと
    // phase/issue情報をすべてfail-closedで合成する。
    const bool aur_phase_completed =
            aur.status == UpgradeAllAurPhaseStatus::NoUpdates ||
            aur.status == UpgradeAllAurPhaseStatus::Completed;
    const bool has_stopping_diagnostic = std::any_of(
            diagnostics.begin(), diagnostics.end(),
            [](const UpgradeAllOperationDiagnostic& diagnostic) {
                return diagnostic.stops_execution;
            });
    if(stopped_phase != UpgradeAllOperationPhase::None ||
       !system_source.is_success() ||
       system_source.system.status != SystemUpgradePhaseStatus::Completed ||
       foreign_inventory.status !=
               UpgradeAllForeignInventoryPhaseStatus::Completed ||
       foreign_inventory.not_attempted_reason.has_value() ||
       foreign_inventory.failure.has_value() ||
       foreign_inventory.diagnostic.has_value() ||
       !aur_phase_completed || !aur.operation_result.has_value() ||
       aur.not_attempted_reason.has_value() || aur.diagnostic.has_value() ||
       !aur.operation_result->is_success() || !issues.empty() ||
       has_stopping_diagnostic || has_cleanup_failure() ||
       has_query_failure() || has_planning_issue() ||
       has_not_attempted_phase() || has_inconsistency()) {
        return false;
    }
    return true;
}

PackageStateChange UpgradeAllOperationResult::package_state_change()
        const noexcept {
    const PackageStateChange system_source_change =
            system_source.package_state_change();
    const PackageStateChange aur_change = aur_package_state_change(aur);
    if(system_source_change == PackageStateChange::Changed ||
       aur_change == PackageStateChange::Changed) {
        return PackageStateChange::Changed;
    }
    if(system_source_change == PackageStateChange::Unknown ||
       aur_change == PackageStateChange::Unknown) {
        return PackageStateChange::Unknown;
    }
    return PackageStateChange::NoChange;
}

bool UpgradeAllOperationResult::has_partial_completion() const noexcept {
    if(is_success()) return false;
    return system_source.system.status ==
            SystemUpgradePhaseStatus::Completed;
}

bool UpgradeAllOperationResult::has_not_attempted_phase() const noexcept {
    if(system_source.system.status ==
               SystemUpgradePhaseStatus::NotAttempted ||
       system_source.has_not_attempted_sources() ||
       foreign_inventory.status ==
               UpgradeAllForeignInventoryPhaseStatus::NotAttempted ||
       aur.status == UpgradeAllAurPhaseStatus::NotAttempted) {
        return true;
    }
    return aur.operation_result.has_value() &&
           aur.operation_result->has_not_attempted_targets();
}

bool UpgradeAllOperationResult::has_cleanup_failure() const noexcept {
    return system_source.has_cleanup_failure() ||
           (aur.operation_result.has_value() &&
            aur.operation_result->has_cleanup_failure());
}

bool UpgradeAllOperationResult::has_query_failure() const noexcept {
    if(foreign_inventory.status ==
       UpgradeAllForeignInventoryPhaseStatus::Failed) {
        return true;
    }
    if(std::any_of(
               issues.begin(), issues.end(),
               [](const UpgradeAllOperationIssue& issue) {
                   return issue.kind ==
                                  UpgradeAllOperationIssueKind::
                                          AurQueryFailed ||
                          issue.kind ==
                                  UpgradeAllOperationIssueKind::
                                          ForeignInventoryConfigurationFailed ||
                          issue.kind ==
                                  UpgradeAllOperationIssueKind::
                                          ForeignInventoryReadFailed;
               })) {
        return true;
    }
    return aur.operation_result.has_value() &&
           aur.operation_result->has_query_failure();
}

bool UpgradeAllOperationResult::has_planning_issue() const noexcept {
    if(!prepared_snapshot.explicit_source_adapter.issues.empty()) return true;
    return aur.operation_result.has_value() &&
           aur.operation_result->has_planning_issue();
}

bool UpgradeAllOperationResult::has_duplicate_exclusions() const noexcept {
    return !duplicate_excluded_aur_targets.empty();
}

bool UpgradeAllOperationResult::has_external_satisfaction() const noexcept {
    return !externally_satisfied_aur_build_units.empty();
}

bool UpgradeAllOperationResult::has_inconsistency() const noexcept {
    if(status == UpgradeAllOperationStatus::InconsistentResult ||
       system_source.status == SystemSourceUpgradeStatus::InconsistentResult ||
       aur.status == UpgradeAllAurPhaseStatus::InconsistentResult) {
        return true;
    }
    if(aur.operation_result.has_value() &&
       (!aur.operation_result->issues.empty() ||
        !aur.operation_result->reduced_operation_result.
                reduction_issues.empty() ||
        aur.operation_result->reduced_operation_result.status ==
                AurUpdateOperationStatus::InconsistentResult)) {
        return true;
    }
    return std::any_of(
            issues.begin(), issues.end(),
            [](const UpgradeAllOperationIssue& issue) {
                switch(issue.kind) {
                    case UpgradeAllOperationIssueKind::OptionSnapshotMismatch:
                    case UpgradeAllOperationIssueKind::SourceSnapshotMismatch:
                    case UpgradeAllOperationIssueKind::
                            ExplicitSourceCorrelationInconsistent:
                    case UpgradeAllOperationIssueKind::
                            PreparedCapabilityConsumed:
                    case UpgradeAllOperationIssueKind::
                            SystemSourceExecutionFailedUnexpectedly:
                    case UpgradeAllOperationIssueKind::
                            FilteredAurExecutionFailed:
                    case UpgradeAllOperationIssueKind::
                            DuplicateExclusionCorrelationInconsistent:
                    case UpgradeAllOperationIssueKind::
                            ExternalSatisfactionCorrelationInconsistent:
                    case UpgradeAllOperationIssueKind::UnknownFailure:
                        return true;
                    case UpgradeAllOperationIssueKind::
                            ExplicitSourceAdapterInvalid:
                    case UpgradeAllOperationIssueKind::
                            SystemSourcePhaseIncomplete:
                    case UpgradeAllOperationIssueKind::
                            ForeignInventoryConfigurationFailed:
                    case UpgradeAllOperationIssueKind::
                            ForeignInventoryReadFailed:
                    case UpgradeAllOperationIssueKind::
                            CacheAuthorityInvalid:
                    case UpgradeAllOperationIssueKind::AurQueryFailed:
                    case UpgradeAllOperationIssueKind::
                            FilteredAurPreparationFailed:
                        return false;
                }
                return true;
            });
}
