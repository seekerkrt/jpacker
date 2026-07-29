#include "upgrade_all_operation.hpp"

#include "app_config.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>
#include <utility>

namespace {

constexpr const char* CONSUMED_CAPABILITY_DIAGNOSTIC =
        "Prepared upgrade-all operation is invalid or has already been consumed.";
constexpr const char* OPTION_MISMATCH_DIAGNOSTIC =
        "Prepared upgrade-all options differ from execution options.";
constexpr const char* SOURCE_SNAPSHOT_MISMATCH_DIAGNOSTIC =
        "Prepared upgrade-all source snapshot differs from its nested system/source capability.";
constexpr const char* SOURCE_CORRELATION_MISMATCH_DIAGNOSTIC =
        "Prepared upgrade-all explicit source correlation is inconsistent.";

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

bool environments_match(
        const std::optional<SourceBuildEnvironment>& lhs,
        const std::optional<SourceBuildEnvironment>& rhs) noexcept {
    if(lhs.has_value() != rhs.has_value()) return false;
    if(!lhs.has_value()) return true;
    if(lhs->ordered_assignments.size() != rhs->ordered_assignments.size()) {
        return false;
    }
    for(std::size_t index = 0;
        index < lhs->ordered_assignments.size(); ++index) {
        const SourceEnvironmentAssignment& lhs_assignment =
                lhs->ordered_assignments[index];
        const SourceEnvironmentAssignment& rhs_assignment =
                rhs->ordered_assignments[index];
        if(lhs_assignment.key != rhs_assignment.key ||
           lhs_assignment.value != rhs_assignment.value) {
            return false;
        }
    }
    return true;
}

bool option_snapshots_match(
        const SystemSourceUpgradeOptionSnapshot& lhs,
        const SystemSourceUpgradeOptionSnapshot& rhs) noexcept {
    return lhs.no_edit == rhs.no_edit &&
           lhs.no_diff == rhs.no_diff &&
           lhs.no_confirm == rhs.no_confirm &&
           lhs.rebuild == rhs.rebuild &&
           lhs.clean_build == rhs.clean_build &&
           lhs.rm_deps == rhs.rm_deps &&
           lhs.editor == rhs.editor;
}

bool source_entries_match(
        const RegisteredSourcePreferenceSnapshot& lhs,
        const RegisteredSourcePreferenceSnapshot& rhs) noexcept {
    return lhs.original_preference_index == rhs.original_preference_index &&
           lhs.preference_package_name == rhs.preference_package_name &&
           lhs.entry_path == rhs.entry_path &&
           environments_match(lhs.environment, rhs.environment) &&
           lhs.canonical_source_identity_key ==
                   rhs.canonical_source_identity_key &&
           lhs.resolved_package_base == rhs.resolved_package_base &&
           lhs.preference_load_warnings == rhs.preference_load_warnings;
}

bool source_snapshots_match(
        const SystemSourceUpgradePreparedSnapshot& lhs,
        const SystemSourceUpgradePreparedSnapshot& rhs) noexcept {
    if(lhs.preference_root_exists != rhs.preference_root_exists ||
       !option_snapshots_match(lhs.options, rhs.options) ||
       lhs.registered_sources.size() != rhs.registered_sources.size()) {
        return false;
    }
    for(std::size_t index = 0;
        index < lhs.registered_sources.size(); ++index) {
        if(!source_entries_match(
                   lhs.registered_sources[index],
                   rhs.registered_sources[index])) {
            return false;
        }
    }
    return true;
}

UpgradeAllExplicitSourceAdapterIssue make_adapter_issue(
        UpgradeAllExplicitSourceAdapterIssueKind kind,
        std::size_t adapter_index,
        const RegisteredSourcePreferenceSnapshot& source,
        std::string diagnostic) {
    return UpgradeAllExplicitSourceAdapterIssue{
            kind,
            adapter_index,
            source.original_preference_index,
            source.preference_package_name,
            std::move(diagnostic)};
}

std::vector<UpgradeAllOperationWarning> collect_preparation_warnings(
        const SystemSourceUpgradePreparedSnapshot& snapshot) {
    std::vector<UpgradeAllOperationWarning> warnings;
    for(const RegisteredSourcePreferenceSnapshot& source :
        snapshot.registered_sources) {
        for(const std::string& diagnostic :
            source.preference_load_warnings) {
            warnings.push_back(UpgradeAllOperationWarning{
                    UpgradeAllOperationWarningKind::
                            RegisteredSourcePreference,
                    UpgradeAllOperationPhase::Preparation,
                    source.original_preference_index,
                    source.preference_package_name,
                    diagnostic});
        }
    }
    return warnings;
}

bool adapter_matches_snapshot(
        const UpgradeAllExplicitSourceIdentityAdapter& adapter,
        const SystemSourceUpgradePreparedSnapshot& snapshot) noexcept {
    if(!adapter.issues.empty() ||
       adapter.entries.size() != snapshot.registered_sources.size()) {
        return false;
    }
    for(std::size_t index = 0; index < adapter.entries.size(); ++index) {
        const UpgradeAllExplicitSourceAdapterEntry& entry =
                adapter.entries[index];
        const RegisteredSourcePreferenceSnapshot& source =
                snapshot.registered_sources[index];
        if(entry.adapter_index != index ||
           entry.original_preference_index !=
                   source.original_preference_index ||
           entry.preference_package_name != source.preference_package_name ||
           entry.canonical_source_identity_key !=
                   source.canonical_source_identity_key ||
           entry.resolved_package_base != source.resolved_package_base ||
           entry.affected_package_names !=
                   std::vector<std::string>{
                           source.preference_package_name} ||
           entry.planner_identity.preference_package_name !=
                   source.preference_package_name ||
           entry.planner_identity.produced_package_names !=
                   entry.affected_package_names) {
            return false;
        }

        const auto* package_base =
                std::get_if<UpgradeAllResolvedPackageBase>(
                        &entry.planner_identity.package_base);
        const auto* source_identity =
                std::get_if<UpgradeAllResolvedSourceIdentity>(
                        &entry.planner_identity.source_identity);
        if(package_base == nullptr || source_identity == nullptr ||
           !source.resolved_package_base.has_value() ||
           !source.canonical_source_identity_key.has_value() ||
           package_base->package_base != *source.resolved_package_base ||
           source_identity->key != *source.canonical_source_identity_key) {
            return false;
        }
    }
    return true;
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

SystemSourceUpgradeResult make_unattempted_system_source_result(
        const SystemSourceUpgradePreparedSnapshot& snapshot,
        SystemSourceUpgradeStatus status) {
    SystemSourceUpgradeResult result;
    result.status = status;
    result.stopped_phase = SystemSourceUpgradePhase::Preparation;
    result.prepared_snapshot = snapshot;
    result.registered_source_results.reserve(
            snapshot.registered_sources.size());
    for(const RegisteredSourcePreferenceSnapshot& source :
        snapshot.registered_sources) {
        result.registered_source_results.push_back(
                make_not_attempted_source_result(source));
    }
    return result;
}

SystemSourceUpgradeResult make_unavailable_system_source_result(
        const SystemSourceUpgradePreparedSnapshot& snapshot,
        SystemSourceUpgradePhase observed_phase,
        const std::string& diagnostic) {
    SystemSourceUpgradeResult result = make_unattempted_system_source_result(
            snapshot, SystemSourceUpgradeStatus::InconsistentResult);
    result.stopped_phase = observed_phase;

    if(observed_phase == SystemSourceUpgradePhase::System) {
        result.system.status = SystemUpgradePhaseStatus::Failed;
        result.system.package_state_change = PackageStateChange::Unknown;
        result.system.diagnostic =
                "System result unavailable after phase started due to an unexpected exception: " +
                diagnostic;
    } else if(observed_phase ==
              SystemSourceUpgradePhase::RegisteredSource) {
        result.system.status = SystemUpgradePhaseStatus::Completed;
        result.system.package_state_change = PackageStateChange::Unknown;
        for(RegisteredSourceUpgradeResult& source :
            result.registered_source_results) {
            source.status = RegisteredSourceUpgradeStatus::Incomplete;
            source.failure_kind =
                    RegisteredSourceUpgradeFailureKind::UnknownException;
            source.package_state_change = PackageStateChange::Unknown;
            source.diagnostic =
                    "Registered source result unavailable after phase started due to an unexpected exception: " +
                    diagnostic;
        }
    }
    return result;
}

UpgradeAllOperationPhase aggregate_phase_for_system_source(
        SystemSourceUpgradePhase phase) noexcept {
    switch(phase) {
    case SystemSourceUpgradePhase::None:
    case SystemSourceUpgradePhase::Preparation:
        return UpgradeAllOperationPhase::Preparation;
    case SystemSourceUpgradePhase::System:
        return UpgradeAllOperationPhase::System;
    case SystemSourceUpgradePhase::RegisteredSource:
        return UpgradeAllOperationPhase::RegisteredSource;
    }
    return UpgradeAllOperationPhase::Preparation;
}

UpgradeAllOperationIssue make_issue(
        UpgradeAllOperationIssueKind kind,
        UpgradeAllOperationPhase phase,
        std::string diagnostic) {
    UpgradeAllOperationIssue issue;
    issue.kind = kind;
    issue.phase = phase;
    issue.diagnostic = std::move(diagnostic);
    return issue;
}

void add_stopping_diagnostic(
        UpgradeAllOperationResult& result,
        UpgradeAllOperationPhase phase,
        const std::string& diagnostic) {
    result.diagnostics.push_back(UpgradeAllOperationDiagnostic{
            phase, true, diagnostic});
}

void set_not_attempted_after(
        UpgradeAllOperationResult& result,
        UpgradeAllNotAttemptedReason reason) {
    result.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::NotAttempted;
    result.foreign_inventory.not_attempted_reason = reason;
    result.aur.status = UpgradeAllAurPhaseStatus::NotAttempted;
    result.aur.not_attempted_reason = reason;
}

UpgradeAllOperationPreparedSnapshot make_prepared_snapshot(
        const SystemSourceUpgradePreparedSnapshot& source_snapshot) {
    UpgradeAllOperationPreparedSnapshot snapshot;
    snapshot.system_source = source_snapshot;
    snapshot.explicit_source_adapter =
            adapt_prepared_source_identities_for_upgrade_all(
                    source_snapshot);
    snapshot.warnings = collect_preparation_warnings(source_snapshot);
    return snapshot;
}

void append_adapter_issues(
        UpgradeAllOperationResult& result,
        const UpgradeAllExplicitSourceIdentityAdapter& adapter) {
    for(const UpgradeAllExplicitSourceAdapterIssue& adapter_issue :
        adapter.issues) {
        UpgradeAllOperationIssue issue = make_issue(
                UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid,
                UpgradeAllOperationPhase::Preparation,
                adapter_issue.diagnostic);
        issue.adapter_index = adapter_issue.adapter_index;
        issue.original_preference_index =
                adapter_issue.original_preference_index;
        issue.package_name = adapter_issue.preference_package_name;
        result.issues.push_back(std::move(issue));
    }
}

UpgradeAllOperationResult make_blocked_preparation_result(
        SystemSourceUpgradeResult source_result) {
    UpgradeAllOperationResult result;
    result.status = UpgradeAllOperationStatus::BlockedBeforeMutation;
    result.stopped_phase = UpgradeAllOperationPhase::Preparation;
    result.prepared_snapshot = make_prepared_snapshot(
            source_result.prepared_snapshot);
    result.warnings = result.prepared_snapshot.warnings;
    result.system_source = std::move(source_result);
    append_adapter_issues(
            result, result.prepared_snapshot.explicit_source_adapter);
    set_not_attempted_after(
            result, UpgradeAllNotAttemptedReason::PreparationBlocked);
    return result;
}

UpgradeAllOperationResult make_preexecution_rejection(
        UpgradeAllOperationPreparedSnapshot snapshot,
        UpgradeAllOperationIssueKind issue_kind,
        std::string diagnostic,
        UpgradeAllOperationStatus status =
                UpgradeAllOperationStatus::InconsistentResult) {
    UpgradeAllOperationResult result;
    result.status = status;
    result.stopped_phase = UpgradeAllOperationPhase::Preparation;
    result.prepared_snapshot = std::move(snapshot);
    result.warnings = result.prepared_snapshot.warnings;
    result.system_source = make_unattempted_system_source_result(
            result.prepared_snapshot.system_source,
            status == UpgradeAllOperationStatus::BlockedBeforeMutation
                    ? SystemSourceUpgradeStatus::BlockedBeforeMutation
                    : SystemSourceUpgradeStatus::InconsistentResult);
    result.issues.push_back(make_issue(
            issue_kind,
            UpgradeAllOperationPhase::Preparation,
            diagnostic));
    add_stopping_diagnostic(
            result, UpgradeAllOperationPhase::Preparation, diagnostic);
    set_not_attempted_after(
            result,
            status == UpgradeAllOperationStatus::BlockedBeforeMutation
                    ? UpgradeAllNotAttemptedReason::PreparationBlocked
                    : UpgradeAllNotAttemptedReason::
                              PriorAggregateInconsistency);
    return result;
}

bool is_successful_source_status(
        RegisteredSourceUpgradeStatus status) noexcept {
    return status == RegisteredSourceUpgradeStatus::Updated ||
           status == RegisteredSourceUpgradeStatus::NoChange;
}

bool has_non_successful_source(
        const SystemSourceUpgradeResult& result) noexcept {
    return std::any_of(
            result.registered_source_results.begin(),
            result.registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return !is_successful_source_status(source.status);
            });
}

bool stop_after_system_source_failure(UpgradeAllOperationResult& result) {
    switch(result.system_source.status) {
        case SystemSourceUpgradeStatus::Completed:
            break;
        case SystemSourceUpgradeStatus::StoppedOnSystemFailure:
            result.status =
                    UpgradeAllOperationStatus::StoppedOnSystemFailure;
            result.stopped_phase = UpgradeAllOperationPhase::System;
            set_not_attempted_after(
                    result, UpgradeAllNotAttemptedReason::SystemFailure);
            return true;
        case SystemSourceUpgradeStatus::StoppedOnSourceFailure:
            result.status =
                    UpgradeAllOperationStatus::StoppedOnSourceFailure;
            result.stopped_phase =
                    UpgradeAllOperationPhase::RegisteredSource;
            set_not_attempted_after(
                    result, UpgradeAllNotAttemptedReason::SourceFailure);
            return true;
        case SystemSourceUpgradeStatus::
                StoppedAfterSourceCleanupFailure:
            result.status = UpgradeAllOperationStatus::
                    StoppedAfterSourceCleanupFailure;
            result.stopped_phase =
                    UpgradeAllOperationPhase::RegisteredSource;
            set_not_attempted_after(
                    result,
                    UpgradeAllNotAttemptedReason::SourceCleanupFailure);
            return true;
        case SystemSourceUpgradeStatus::BlockedBeforeMutation:
            result.status = UpgradeAllOperationStatus::InconsistentResult;
            result.stopped_phase = UpgradeAllOperationPhase::Preparation;
            set_not_attempted_after(
                    result,
                    UpgradeAllNotAttemptedReason::
                            PriorAggregateInconsistency);
            return true;
        case SystemSourceUpgradeStatus::InconsistentResult:
            result.status = UpgradeAllOperationStatus::InconsistentResult;
            result.stopped_phase = aggregate_phase_for_system_source(
                    result.system_source.stopped_phase);
            set_not_attempted_after(
                    result,
                    UpgradeAllNotAttemptedReason::
                            PriorAggregateInconsistency);
            return true;
    }

    if(result.system_source.is_success()) return false;

    if(has_non_successful_source(result.system_source)) {
        result.status = UpgradeAllOperationStatus::StoppedOnSourceFailure;
        result.stopped_phase =
                UpgradeAllOperationPhase::RegisteredSource;
        set_not_attempted_after(
                result, UpgradeAllNotAttemptedReason::SourceFailure);
        return true;
    }

    const std::string diagnostic =
            "System/source phase completed without a fully successful typed result; AUR processing did not start.";
    result.status =
            UpgradeAllOperationStatus::StoppedBeforeAurExecution;
    result.stopped_phase = UpgradeAllOperationPhase::System;
    result.issues.push_back(make_issue(
            UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete,
            UpgradeAllOperationPhase::System,
            diagnostic));
    add_stopping_diagnostic(
            result, UpgradeAllOperationPhase::System, diagnostic);
    set_not_attempted_after(
            result,
            UpgradeAllNotAttemptedReason::SystemSourceIncomplete);
    return true;
}

PackageMetadataFailure generic_metadata_failure(
        PackageMetadataErrorCode code,
        const std::string& diagnostic) {
    return PackageMetadataFailure{code, diagnostic};
}

void stop_for_inventory_failure(
        UpgradeAllOperationResult& result,
        UpgradeAllOperationIssueKind issue_kind,
        PackageMetadataFailure failure) {
    result.status = UpgradeAllOperationStatus::StoppedBeforeAurExecution;
    result.stopped_phase = UpgradeAllOperationPhase::ForeignInventory;
    result.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Failed;
    result.foreign_inventory.not_attempted_reason.reset();
    result.foreign_inventory.failure = failure;
    result.foreign_inventory.diagnostic = failure.diagnostic;
    UpgradeAllOperationIssue issue = make_issue(
            issue_kind,
            UpgradeAllOperationPhase::ForeignInventory,
            failure.diagnostic);
    issue.package_metadata_failure = failure;
    result.issues.push_back(std::move(issue));
    add_stopping_diagnostic(
            result,
            UpgradeAllOperationPhase::ForeignInventory,
            failure.diagnostic);
    result.aur.status = UpgradeAllAurPhaseStatus::NotAttempted;
    result.aur.not_attempted_reason =
            UpgradeAllNotAttemptedReason::ForeignInventoryFailure;
}

bool capture_duplicate_exclusions(
        UpgradeAllOperationResult& aggregate,
        const FilteredAurUpdateExecutionResult& filtered) {
    bool is_consistent = true;
    const UpgradeAllPlan& plan = filtered.upgrade_all_plan;
    for(const std::size_t planner_index :
        plan.excluded_duplicate_target_indexes) {
        const bool in_range =
                planner_index < plan.target_dispositions.size() &&
                planner_index < filtered.target_adapter.
                        planner_target_to_original_query_plan_index.size();
        if(!in_range) {
            UpgradeAllOperationIssue issue = make_issue(
                    UpgradeAllOperationIssueKind::
                            DuplicateExclusionCorrelationInconsistent,
                    UpgradeAllOperationPhase::Reduction,
                    "Duplicate-excluded AUR target has no planner/query correlation.");
            issue.adapter_index = planner_index;
            aggregate.issues.push_back(std::move(issue));
            is_consistent = false;
            continue;
        }

        const std::size_t original_query_index =
                filtered.target_adapter.
                        planner_target_to_original_query_plan_index[
                                planner_index];
        if(original_query_index >= filtered.query_result.plan.entries.size()) {
            UpgradeAllOperationIssue issue = make_issue(
                    UpgradeAllOperationIssueKind::
                            DuplicateExclusionCorrelationInconsistent,
                    UpgradeAllOperationPhase::Reduction,
                    "Duplicate-excluded AUR target maps outside the original query plan.");
            issue.adapter_index = planner_index;
            issue.original_query_plan_index = original_query_index;
            aggregate.issues.push_back(std::move(issue));
            is_consistent = false;
            continue;
        }

        const UpgradeAllTargetPlanEntry& planner_entry =
                plan.target_dispositions[planner_index];
        const AurUpdatePlanEntry& query_entry =
                filtered.query_result.plan.entries[original_query_index];
        if(planner_entry.original_target_index != planner_index ||
           planner_entry.target.package_name != query_entry.installed_name) {
            UpgradeAllOperationIssue issue = make_issue(
                    UpgradeAllOperationIssueKind::
                            DuplicateExclusionCorrelationInconsistent,
                    UpgradeAllOperationPhase::Reduction,
                    "Duplicate-excluded AUR target identity differs from its original query entry.");
            issue.adapter_index = planner_index;
            issue.original_query_plan_index = original_query_index;
            issue.package_name = query_entry.installed_name;
            aggregate.issues.push_back(std::move(issue));
            is_consistent = false;
            continue;
        }

        aggregate.duplicate_excluded_aur_targets.push_back(
                UpgradeAllDuplicateExcludedAurTarget{
                        planner_index,
                        original_query_index,
                        planner_entry,
                        query_entry});
    }
    return is_consistent;
}

bool has_exact_external_child_identity(
        const AurUpdateExternallySatisfiedBuildUnit& unit,
        const FilteredAurUpdateBuildUnitCorrelation& correlation) noexcept {
    if(correlation.package_names.empty() ||
       unit.plan_package_names != correlation.package_names ||
       unit.required_target_attributions.size() !=
               correlation.package_names.size()) {
        return false;
    }

    const bool is_singular = correlation.package_names.size() == 1;
    if((is_singular &&
        unit.package_name != correlation.package_names.front()) ||
       (!is_singular && !unit.package_name.empty())) {
        return false;
    }

    for(std::size_t child_index = 0;
        child_index < unit.required_target_attributions.size();
        ++child_index) {
        const RequiredPackageArtifactTarget& required_target =
                unit.required_target_attributions[child_index]
                        .required_target;
        if(required_target.package_base != unit.package_base ||
           required_target.package_name !=
                   correlation.package_names[child_index] ||
           std::find(
                   correlation.package_names.begin(),
                   correlation.package_names.begin() + child_index,
                   required_target.package_name) !=
                   correlation.package_names.begin() + child_index) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> external_singular_package_name(
        const AurUpdateExternallySatisfiedBuildUnit& unit) {
    if(unit.required_target_attributions.size() != 1) return std::nullopt;
    return unit.required_target_attributions.front()
            .required_target.package_name;
}

bool capture_external_satisfaction(
        UpgradeAllOperationResult& aggregate,
        const FilteredAurUpdateExecutionResult& filtered) {
    bool is_consistent =
            filtered.preparation.externally_satisfied_build_units.size() ==
            filtered.upgrade_all_plan.
                    externally_satisfied_build_unit_indexes.size();
    for(const AurUpdateExternallySatisfiedBuildUnit& unit :
        filtered.preparation.externally_satisfied_build_units) {
        if(unit.build_plan_order_index >=
           filtered.build_unit_correlations.size()) {
            UpgradeAllOperationIssue issue = make_issue(
                    UpgradeAllOperationIssueKind::
                            ExternalSatisfactionCorrelationInconsistent,
                    UpgradeAllOperationPhase::Reduction,
                    "Externally satisfied AUR build unit has no PR3 root-role correlation.");
            issue.build_plan_order_index = unit.build_plan_order_index;
            issue.package_name = external_singular_package_name(unit);
            aggregate.issues.push_back(std::move(issue));
            is_consistent = false;
            continue;
        }
        const FilteredAurUpdateBuildUnitCorrelation& correlation =
                filtered.build_unit_correlations[
                        unit.build_plan_order_index];
        if(correlation.original_build_plan_index !=
                   unit.build_plan_order_index ||
           correlation.package_base != unit.package_base ||
           !has_exact_external_child_identity(unit, correlation)) {
            UpgradeAllOperationIssue issue = make_issue(
                    UpgradeAllOperationIssueKind::
                            ExternalSatisfactionCorrelationInconsistent,
                    UpgradeAllOperationPhase::Reduction,
                    "Externally satisfied AUR build-unit child identity differs from its PR3 correlation.");
            issue.build_plan_order_index = unit.build_plan_order_index;
            issue.package_name = external_singular_package_name(unit);
            aggregate.issues.push_back(std::move(issue));
            is_consistent = false;
            continue;
        }
        aggregate.externally_satisfied_aur_build_units.push_back(
                UpgradeAllExternallySatisfiedAurBuildUnit{
                        unit,
                        correlation.root_correlations});
    }

    if(!is_consistent &&
       aggregate.externally_satisfied_aur_build_units.empty() &&
       !filtered.preparation.externally_satisfied_build_units.empty()) {
        return false;
    }
    return is_consistent;
}

void append_aur_warnings(
        UpgradeAllOperationResult& aggregate,
        const FilteredAurUpdateExecutionResult& filtered) {
    for(const AurUpdatePreparationWarning& warning :
        filtered.reduced_operation_result.preparation_warnings) {
        aggregate.warnings.push_back(UpgradeAllOperationWarning{
                UpgradeAllOperationWarningKind::AurPreparation,
                UpgradeAllOperationPhase::AurPreparation,
                std::nullopt,
                warning.preference_name,
                warning.diagnostic});
    }
}

void map_filtered_result_status(UpgradeAllOperationResult& aggregate) {
    FilteredAurUpdateExecutionResult& filtered =
            *aggregate.aur.operation_result;
    append_aur_warnings(aggregate, filtered);
    const bool duplicate_correlation_valid =
            capture_duplicate_exclusions(aggregate, filtered);
    const bool external_correlation_valid =
            capture_external_satisfaction(aggregate, filtered);

    if(!duplicate_correlation_valid || !external_correlation_valid ||
       !filtered.issues.empty() ||
       !filtered.reduced_operation_result.reduction_issues.empty() ||
       filtered.reduced_operation_result.status ==
               AurUpdateOperationStatus::InconsistentResult) {
        aggregate.status = UpgradeAllOperationStatus::InconsistentResult;
        aggregate.stopped_phase = UpgradeAllOperationPhase::Reduction;
        aggregate.aur.status =
                UpgradeAllAurPhaseStatus::InconsistentResult;
        return;
    }

    if(filtered.has_query_failure()) {
        aggregate.status =
                UpgradeAllOperationStatus::StoppedBeforeAurExecution;
        aggregate.stopped_phase = UpgradeAllOperationPhase::AurQuery;
        aggregate.aur.status =
                UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
        aggregate.aur.diagnostic =
                "AUR update query completed with recoverable failures; filtered execution did not start.";
        return;
    }

    switch(filtered.reduced_operation_result.status) {
        case AurUpdateOperationStatus::NoUpdates:
            if(!filtered.is_success()) {
                aggregate.status =
                        UpgradeAllOperationStatus::InconsistentResult;
                aggregate.stopped_phase =
                        UpgradeAllOperationPhase::Reduction;
                aggregate.aur.status =
                        UpgradeAllAurPhaseStatus::InconsistentResult;
                return;
            }
            aggregate.aur.status = UpgradeAllAurPhaseStatus::NoUpdates;
            aggregate.status = UpgradeAllOperationStatus::Completed;
            aggregate.stopped_phase = UpgradeAllOperationPhase::None;
            return;
        case AurUpdateOperationStatus::Completed:
            if(!filtered.is_success()) {
                aggregate.status =
                        UpgradeAllOperationStatus::InconsistentResult;
                aggregate.stopped_phase =
                        UpgradeAllOperationPhase::Reduction;
                aggregate.aur.status =
                        UpgradeAllAurPhaseStatus::InconsistentResult;
                return;
            }
            aggregate.aur.status = UpgradeAllAurPhaseStatus::Completed;
            aggregate.status = UpgradeAllOperationStatus::Completed;
            aggregate.stopped_phase = UpgradeAllOperationPhase::None;
            return;
        case AurUpdateOperationStatus::BlockedBeforeExecution:
            aggregate.status =
                    UpgradeAllOperationStatus::StoppedBeforeAurExecution;
            aggregate.stopped_phase =
                    UpgradeAllOperationPhase::AurPreparation;
            aggregate.aur.status =
                    UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
            return;
        case AurUpdateOperationStatus::StoppedOnWorkItemFailure:
            aggregate.status = UpgradeAllOperationStatus::StoppedOnAurFailure;
            aggregate.stopped_phase = UpgradeAllOperationPhase::AurExecution;
            aggregate.aur.status =
                    UpgradeAllAurPhaseStatus::StoppedOnWorkItemFailure;
            return;
        case AurUpdateOperationStatus::
                StoppedAfterPackageCleanupFailure:
            aggregate.status = UpgradeAllOperationStatus::
                    StoppedAfterAurCleanupFailure;
            aggregate.stopped_phase = UpgradeAllOperationPhase::AurExecution;
            aggregate.aur.status =
                    UpgradeAllAurPhaseStatus::StoppedAfterCleanupFailure;
            return;
        case AurUpdateOperationStatus::InconsistentResult:
            aggregate.status =
                    UpgradeAllOperationStatus::InconsistentResult;
            aggregate.stopped_phase = UpgradeAllOperationPhase::Reduction;
            aggregate.aur.status =
                    UpgradeAllAurPhaseStatus::InconsistentResult;
            return;
    }
}

bool all_registered_sources_no_change(
        const SystemSourceUpgradeResult& result) noexcept {
    return std::all_of(
            result.registered_source_results.begin(),
            result.registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return source.status ==
                               RegisteredSourceUpgradeStatus::NoChange &&
                       source.package_state_change ==
                               PackageStateChange::NoChange;
            });
}

bool qualifies_as_no_updates(
        const UpgradeAllOperationResult& result) noexcept {
    if(result.system_source.system.status !=
               SystemUpgradePhaseStatus::Completed ||
       result.system_source.system.package_state_change !=
               PackageStateChange::NoChange ||
       !all_registered_sources_no_change(result.system_source) ||
       result.foreign_inventory.status !=
               UpgradeAllForeignInventoryPhaseStatus::Completed ||
       !result.aur.operation_result.has_value() ||
       !result.aur.operation_result->is_success() ||
       (result.aur.status != UpgradeAllAurPhaseStatus::NoUpdates &&
        result.aur.status != UpgradeAllAurPhaseStatus::Completed) ||
       result.package_state_change() != PackageStateChange::NoChange ||
       result.has_cleanup_failure() || result.has_query_failure() ||
       result.has_planning_issue() || result.has_not_attempted_phase() ||
       result.has_inconsistency()) {
        return false;
    }
    return true;
}

} // namespace

struct PreparedUpgradeAllOperation::Impl {
    Impl(
            UpgradeAllOperationPreparedSnapshot prepared_snapshot,
            PreparedSystemSourceUpgrade prepared_system_source)
        : snapshot(std::move(prepared_snapshot)),
          system_source(std::move(prepared_system_source)) {
    }

    UpgradeAllOperationPreparedSnapshot snapshot;
    PreparedSystemSourceUpgrade system_source;
};

struct UpgradeAllOperationPreparationAccess {
    static PreparedUpgradeAllOperation make(
            UpgradeAllOperationPreparedSnapshot snapshot,
            PreparedSystemSourceUpgrade system_source) {
        return PreparedUpgradeAllOperation(
                std::make_unique<PreparedUpgradeAllOperation::Impl>(
                        std::move(snapshot),
                        std::move(system_source)));
    }
};

bool UpgradeAllExplicitSourceIdentityAdapter::is_valid() const noexcept {
    return issues.empty();
}

std::vector<UpgradeAllExplicitSourceIdentity>
UpgradeAllExplicitSourceIdentityAdapter::planner_identities() const {
    std::vector<UpgradeAllExplicitSourceIdentity> identities;
    identities.reserve(entries.size());
    for(const UpgradeAllExplicitSourceAdapterEntry& entry : entries) {
        identities.push_back(entry.planner_identity);
    }
    return identities;
}

UpgradeAllExplicitSourceIdentityAdapter
adapt_prepared_source_identities_for_upgrade_all(
        const SystemSourceUpgradePreparedSnapshot& source_snapshot) {
    UpgradeAllExplicitSourceIdentityAdapter adapter;
    adapter.entries.reserve(source_snapshot.registered_sources.size());
    std::set<std::size_t> original_preference_indexes;

    for(std::size_t adapter_index = 0;
        adapter_index < source_snapshot.registered_sources.size();
        ++adapter_index) {
        const RegisteredSourcePreferenceSnapshot& source =
                source_snapshot.registered_sources[adapter_index];
        std::vector<std::string> affected_package_names;
        if(!source.preference_package_name.empty()) {
            affected_package_names.push_back(
                    source.preference_package_name);
        } else {
            adapter.issues.push_back(make_adapter_issue(
                    UpgradeAllExplicitSourceAdapterIssueKind::
                            PreferencePackageNameMissing,
                    adapter_index,
                    source,
                    "Registered source preference has no package name."));
        }

        UpgradeAllPackageBaseIdentity package_base =
                UpgradeAllPackageBaseAbsent{};
        if(source.resolved_package_base.has_value() &&
           !source.resolved_package_base->empty()) {
            package_base = UpgradeAllResolvedPackageBase{
                    *source.resolved_package_base};
        } else {
            adapter.issues.push_back(make_adapter_issue(
                    UpgradeAllExplicitSourceAdapterIssueKind::
                            PackageBaseUnavailable,
                    adapter_index,
                    source,
                    "Registered source preference has no prepared PackageBase identity."));
        }

        UpgradeAllSourceIdentity source_identity =
                UpgradeAllSourceIdentityAbsent{};
        if(source.canonical_source_identity_key.has_value() &&
           !source.canonical_source_identity_key->empty()) {
            source_identity = UpgradeAllResolvedSourceIdentity{
                    *source.canonical_source_identity_key};
        } else {
            adapter.issues.push_back(make_adapter_issue(
                    UpgradeAllExplicitSourceAdapterIssueKind::
                            CanonicalSourceIdentityUnavailable,
                    adapter_index,
                    source,
                    "Registered source preference has no prepared canonical source identity."));
        }

        if(!original_preference_indexes.insert(
                   source.original_preference_index).second) {
            adapter.issues.push_back(make_adapter_issue(
                    UpgradeAllExplicitSourceAdapterIssueKind::
                            DuplicateOriginalPreferenceIndex,
                    adapter_index,
                    source,
                    "Registered source preferences contain a duplicate original index."));
        }

        UpgradeAllExplicitSourceIdentity planner_identity{
                source.preference_package_name,
                std::move(package_base),
                affected_package_names,
                std::move(source_identity)};
        adapter.entries.push_back(UpgradeAllExplicitSourceAdapterEntry{
                adapter_index,
                source.original_preference_index,
                source.preference_package_name,
                source.canonical_source_identity_key,
                source.resolved_package_base,
                std::move(affected_package_names),
                std::move(planner_identity)});
    }
    return adapter;
}

PreparedUpgradeAllOperation::PreparedUpgradeAllOperation(
        std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {
}

PreparedUpgradeAllOperation::PreparedUpgradeAllOperation(
        PreparedUpgradeAllOperation&&) noexcept = default;

PreparedUpgradeAllOperation::~PreparedUpgradeAllOperation() noexcept = default;

bool PreparedUpgradeAllOperation::is_valid() const noexcept {
    return impl_ != nullptr;
}

const UpgradeAllOperationPreparedSnapshot*
PreparedUpgradeAllOperation::snapshot() const noexcept {
    return impl_ == nullptr ? nullptr : &impl_->snapshot;
}

#ifdef JPACKER_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS
void PreparedUpgradeAllOperation::
make_source_snapshot_inconsistent_for_test() {
    if(impl_ == nullptr ||
       impl_->snapshot.system_source.registered_sources.empty()) {
        return;
    }
    impl_->snapshot.system_source.registered_sources.front().
            preference_package_name += "-outer-corruption";
}

void PreparedUpgradeAllOperation::
make_explicit_source_correlation_inconsistent_for_test() {
    if(impl_ == nullptr ||
       impl_->snapshot.explicit_source_adapter.entries.empty()) {
        return;
    }
    ++impl_->snapshot.explicit_source_adapter.entries.front().adapter_index;
}

void PreparedUpgradeAllOperation::
make_nested_system_source_correlation_inconsistent_for_test() {
#ifdef JPACKER_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
    if(impl_ != nullptr) {
        impl_->system_source.
                make_first_source_correlation_inconsistent_for_test();
    }
#endif
}

#ifdef JPACKER_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
void PreparedUpgradeAllOperation::
set_nested_system_source_unexpected_exception_for_test(
        SystemSourceUpgradeUnexpectedExceptionPoint point,
        bool unknown_exception) {
    if(impl_ == nullptr) return;
    impl_->system_source.set_unexpected_exception_for_test(
            point, unknown_exception);
}
#endif
#endif

UpgradeAllOperationPreparation prepare_upgrade_all_operation(
        const AppConfig& config) {
    try {
        SystemSourceUpgradePreparation source_preparation =
                prepare_system_source_upgrade(config);
        if(auto* blocked =
                   std::get_if<SystemSourceUpgradeResult>(
                           &source_preparation)) {
            return make_blocked_preparation_result(std::move(*blocked));
        }

        PreparedSystemSourceUpgrade prepared_source = std::move(
                std::get<PreparedSystemSourceUpgrade>(source_preparation));
        const SystemSourceUpgradePreparedSnapshot* source_snapshot =
                prepared_source.snapshot();
        if(source_snapshot == nullptr) {
            return make_preexecution_rejection(
                    {},
                    UpgradeAllOperationIssueKind::
                            PreparedCapabilityConsumed,
                    CONSUMED_CAPABILITY_DIAGNOSTIC);
        }

        UpgradeAllOperationPreparedSnapshot snapshot =
                make_prepared_snapshot(*source_snapshot);
        if(!snapshot.explicit_source_adapter.is_valid()) {
            UpgradeAllOperationResult result = make_preexecution_rejection(
                    std::move(snapshot),
                    UpgradeAllOperationIssueKind::
                            ExplicitSourceAdapterInvalid,
                    "Prepared registered source identity cannot be adapted safely for upgrade-all planning.",
                    UpgradeAllOperationStatus::BlockedBeforeMutation);
            append_adapter_issues(
                    result,
                    result.prepared_snapshot.explicit_source_adapter);
            return result;
        }

        return UpgradeAllOperationPreparationAccess::make(
                std::move(snapshot), std::move(prepared_source));
    } catch(const std::exception& error) {
        UpgradeAllOperationResult result = make_preexecution_rejection(
                {},
                UpgradeAllOperationIssueKind::UnknownFailure,
                error.what(),
                UpgradeAllOperationStatus::BlockedBeforeMutation);
        return result;
    } catch(...) {
        UpgradeAllOperationResult result = make_preexecution_rejection(
                {},
                UpgradeAllOperationIssueKind::UnknownFailure,
                "Upgrade-all preparation failed with an unknown exception.",
                UpgradeAllOperationStatus::BlockedBeforeMutation);
        return result;
    }
}

UpgradeAllOperationResult execute_prepared_upgrade_all_operation(
        PreparedUpgradeAllOperation prepared,
        const AppConfig& config) {
    if(prepared.impl_ == nullptr) {
        return make_preexecution_rejection(
                {},
                UpgradeAllOperationIssueKind::PreparedCapabilityConsumed,
                CONSUMED_CAPABILITY_DIAGNOSTIC);
    }

    UpgradeAllOperationPreparedSnapshot snapshot =
            prepared.impl_->snapshot;
    const SystemSourceUpgradePreparedSnapshot* nested_snapshot =
            prepared.impl_->system_source.snapshot();
    if(!options_match(snapshot.system_source.options, config)) {
        return make_preexecution_rejection(
                std::move(snapshot),
                UpgradeAllOperationIssueKind::OptionSnapshotMismatch,
                OPTION_MISMATCH_DIAGNOSTIC);
    }
    if(nested_snapshot == nullptr ||
       !source_snapshots_match(snapshot.system_source, *nested_snapshot)) {
        return make_preexecution_rejection(
                std::move(snapshot),
                UpgradeAllOperationIssueKind::SourceSnapshotMismatch,
                SOURCE_SNAPSHOT_MISMATCH_DIAGNOSTIC);
    }
    if(!adapter_matches_snapshot(
               snapshot.explicit_source_adapter,
               snapshot.system_source)) {
        return make_preexecution_rejection(
                std::move(snapshot),
                UpgradeAllOperationIssueKind::
                        ExplicitSourceCorrelationInconsistent,
                SOURCE_CORRELATION_MISMATCH_DIAGNOSTIC);
    }

    std::vector<UpgradeAllExplicitSourceIdentity> explicit_sources =
            snapshot.explicit_source_adapter.planner_identities();
    UpgradeAllOperationResult result;
    result.prepared_snapshot = snapshot;
    result.warnings = snapshot.warnings;

    SystemSourceUpgradePhase observed_system_source_phase =
            SystemSourceUpgradePhase::Preparation;
    const SystemSourceUpgradeEventObserver progress_observer =
            [&observed_system_source_phase](
                    const SystemSourceUpgradeEvent& event) noexcept {
                switch(event.kind) {
                case SystemSourceUpgradeEventKind::LoadingSourcePreference:
                case SystemSourceUpgradeEventKind::SourcePreferenceWarning:
                    return;
                case SystemSourceUpgradeEventKind::SystemUpgradeStarting:
                    observed_system_source_phase =
                            SystemSourceUpgradePhase::System;
                    return;
                case SystemSourceUpgradeEventKind::CheckingSourcePackages:
                case SystemSourceUpgradeEventKind::InvalidPreferenceWarning:
                    observed_system_source_phase =
                            SystemSourceUpgradePhase::RegisteredSource;
                    return;
                }
            };

    try {
        // POLICY(#281): noexcept observerはnested result自体も返せない例外時の
        // 最終防御専用。通常のpartial typed result保持はnested executorが担う。
        result.system_source = execute_prepared_system_source_upgrade(
                std::move(prepared.impl_->system_source),
                config,
                progress_observer);
    } catch(const std::exception& error) {
        const UpgradeAllOperationPhase stopped_phase =
                aggregate_phase_for_system_source(
                        observed_system_source_phase);
        result.status = UpgradeAllOperationStatus::InconsistentResult;
        result.stopped_phase = stopped_phase;
        result.system_source = make_unavailable_system_source_result(
                snapshot.system_source,
                observed_system_source_phase,
                error.what());
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::
                        SystemSourceExecutionFailedUnexpectedly,
                stopped_phase,
                error.what()));
        add_stopping_diagnostic(
                result, stopped_phase, error.what());
        set_not_attempted_after(
                result,
                UpgradeAllNotAttemptedReason::PriorAggregateInconsistency);
        return result;
    } catch(...) {
        const std::string diagnostic =
                "System/source execution failed with an unknown exception.";
        const UpgradeAllOperationPhase stopped_phase =
                aggregate_phase_for_system_source(
                        observed_system_source_phase);
        result.status = UpgradeAllOperationStatus::InconsistentResult;
        result.stopped_phase = stopped_phase;
        result.system_source = make_unavailable_system_source_result(
                snapshot.system_source,
                observed_system_source_phase,
                diagnostic);
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::
                        SystemSourceExecutionFailedUnexpectedly,
                stopped_phase,
                diagnostic));
        add_stopping_diagnostic(
                result, stopped_phase, diagnostic);
        set_not_attempted_after(
                result,
                UpgradeAllNotAttemptedReason::PriorAggregateInconsistency);
        return result;
    }

    if(!source_snapshots_match(
               result.system_source.prepared_snapshot,
               snapshot.system_source)) {
        const std::string diagnostic =
                "System/source result no longer matches the prepared upgrade-all source intent snapshot.";
        result.status = UpgradeAllOperationStatus::InconsistentResult;
        result.stopped_phase = UpgradeAllOperationPhase::RegisteredSource;
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::SourceSnapshotMismatch,
                UpgradeAllOperationPhase::RegisteredSource,
                diagnostic));
        add_stopping_diagnostic(
                result,
                UpgradeAllOperationPhase::RegisteredSource,
                diagnostic);
        set_not_attempted_after(
                result,
                UpgradeAllNotAttemptedReason::PriorAggregateInconsistency);
        return result;
    }

    if(stop_after_system_source_failure(result)) return result;

    try {
        result.foreign_inventory.repository_configuration =
                resolve_pacman_repository_configuration();
    } catch(const PackageMetadataError& error) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::
                        ForeignInventoryConfigurationFailed,
                error.failure());
        return result;
    } catch(const std::exception& error) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::
                        ForeignInventoryConfigurationFailed,
                generic_metadata_failure(
                        PackageMetadataErrorCode::ConfigurationUnavailable,
                        error.what()));
        return result;
    } catch(...) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::
                        ForeignInventoryConfigurationFailed,
                generic_metadata_failure(
                        PackageMetadataErrorCode::ConfigurationUnavailable,
                        "Foreign inventory configuration resolution failed with an unknown exception."));
        return result;
    }

    ForeignPackageInventoryResult inventory_result;
    try {
        inventory_result = query_foreign_package_inventory(
                *result.foreign_inventory.repository_configuration);
    } catch(const PackageMetadataError& error) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::ForeignInventoryReadFailed,
                error.failure());
        return result;
    } catch(const std::exception& error) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::ForeignInventoryReadFailed,
                generic_metadata_failure(
                        PackageMetadataErrorCode::QueryFailed,
                        error.what()));
        return result;
    } catch(...) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::ForeignInventoryReadFailed,
                generic_metadata_failure(
                        PackageMetadataErrorCode::QueryFailed,
                        "Foreign inventory read failed with an unknown exception."));
        return result;
    }
    if(const auto* failure =
               std::get_if<PackageMetadataFailure>(&inventory_result)) {
        stop_for_inventory_failure(
                result,
                UpgradeAllOperationIssueKind::ForeignInventoryReadFailed,
                *failure);
        return result;
    }
    result.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Completed;
    result.foreign_inventory.not_attempted_reason.reset();
    result.foreign_inventory.inventory =
            std::get<ForeignPackageInventory>(std::move(inventory_result));

    AurUpdateQueryResult query_result;
    try {
        // LANDMINE(#281): result snapshotを残すためinventoryをcopyで渡す。
        // query_installed_aur_updates()による再取得は禁止する。
        query_result = query_aur_updates_for_foreign_inventory(
                result.foreign_inventory.inventory);
    } catch(const std::exception& error) {
        result.status =
                UpgradeAllOperationStatus::StoppedBeforeAurExecution;
        result.stopped_phase = UpgradeAllOperationPhase::AurQuery;
        result.aur.status =
                UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
        result.aur.diagnostic = error.what();
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::AurQueryFailed,
                UpgradeAllOperationPhase::AurQuery,
                error.what()));
        add_stopping_diagnostic(
                result, UpgradeAllOperationPhase::AurQuery, error.what());
        return result;
    } catch(...) {
        const std::string diagnostic =
                "AUR update query failed with an unknown exception.";
        result.status =
                UpgradeAllOperationStatus::StoppedBeforeAurExecution;
        result.stopped_phase = UpgradeAllOperationPhase::AurQuery;
        result.aur.status =
                UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
        result.aur.diagnostic = diagnostic;
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::AurQueryFailed,
                UpgradeAllOperationPhase::AurQuery,
                diagnostic));
        add_stopping_diagnostic(
                result, UpgradeAllOperationPhase::AurQuery, diagnostic);
        return result;
    }

    try {
        PreparedFilteredAurUpdateOperation filtered_preparation =
                prepare_filtered_aur_update_operation(
                        std::move(query_result),
                        std::move(explicit_sources),
                        config);
        // PR3 consume boundaryはblocked/no-op時にrunnerを呼ばず、正確な
        // correlation/reducer resultだけをmaterializeする。
        result.aur.operation_result.emplace(
                execute_prepared_filtered_aur_update_operation(
                        std::move(filtered_preparation), config));
    } catch(const std::logic_error& error) {
        result.status = UpgradeAllOperationStatus::InconsistentResult;
        result.stopped_phase = UpgradeAllOperationPhase::AurPreparation;
        result.aur.status = UpgradeAllAurPhaseStatus::InconsistentResult;
        result.aur.diagnostic = error.what();
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::FilteredAurExecutionFailed,
                UpgradeAllOperationPhase::AurPreparation,
                error.what()));
        add_stopping_diagnostic(
                result,
                UpgradeAllOperationPhase::AurPreparation,
                error.what());
        return result;
    } catch(const std::exception& error) {
        result.status =
                UpgradeAllOperationStatus::StoppedBeforeAurExecution;
        result.stopped_phase = UpgradeAllOperationPhase::AurPreparation;
        result.aur.status =
                UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
        result.aur.diagnostic = error.what();
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::FilteredAurPreparationFailed,
                UpgradeAllOperationPhase::AurPreparation,
                error.what()));
        add_stopping_diagnostic(
                result,
                UpgradeAllOperationPhase::AurPreparation,
                error.what());
        return result;
    } catch(...) {
        const std::string diagnostic =
                "Filtered AUR operation failed with an unknown exception.";
        result.status = UpgradeAllOperationStatus::InconsistentResult;
        result.stopped_phase = UpgradeAllOperationPhase::AurPreparation;
        result.aur.status = UpgradeAllAurPhaseStatus::InconsistentResult;
        result.aur.diagnostic = diagnostic;
        result.issues.push_back(make_issue(
                UpgradeAllOperationIssueKind::FilteredAurExecutionFailed,
                UpgradeAllOperationPhase::AurPreparation,
                diagnostic));
        add_stopping_diagnostic(
                result,
                UpgradeAllOperationPhase::AurPreparation,
                diagnostic);
        return result;
    }

    map_filtered_result_status(result);
    if(result.status == UpgradeAllOperationStatus::Completed &&
       qualifies_as_no_updates(result)) {
        result.status = UpgradeAllOperationStatus::NoUpdates;
    }
    return result;
}
