#include "app_config.hpp"
#include "aur_update_execution_preflight.hpp"
#include "aur_update_execution_preparation.hpp"
#include "aur_update_execution_runner.hpp"
#include "aur_update_operation_result.hpp"
#include "aur_update_query.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// CLI command boundaryだけをdeterministicに検証するscenario-driven stub。
// POLICY(#267): queryからreducerまでのproduction実装は個別testで検証済みのため、
// このbinaryでは公開typed APIを保ったまま外部状態とmutationを完全に遮断する。
namespace {

const std::string& scenario() {
    static const std::string s_scenario = [] {
        const char* value = std::getenv("JPACKER_TEST_AUR_UPDATE_SCENARIO");
        if(value == nullptr || value[0] == '\0') {
            throw std::logic_error(
                    "JPACKER_TEST_AUR_UPDATE_SCENARIO is required.");
        }
        return std::string(value);
    }();
    return s_scenario;
}

void append_event(const std::string& event) {
    const char* event_log_path = std::getenv("JPACKER_TEST_COMMAND_LOG");
    if(event_log_path == nullptr || event_log_path[0] == '\0') {
        throw std::logic_error("JPACKER_TEST_COMMAND_LOG is required.");
    }

    std::ofstream event_log(event_log_path, std::ios::app);
    if(!event_log) {
        throw std::runtime_error("Cannot open the AUR update command event log.");
    }
    event_log << event << '\n';
}

const char* bool_text(bool value) {
    return value ? "true" : "false";
}

std::string config_snapshot(const AppConfig& config) {
    return "noedit=" + std::string(bool_text(config.no_edit)) +
            " nodiff=" + bool_text(config.no_diff) +
            " noconfirm=" + bool_text(config.no_confirm) +
            " rebuild=" + bool_text(config.rebuild) +
            " cleanbuild=" + bool_text(config.clean_build) +
            " rmdeps=" + bool_text(config.rm_deps);
}

AurUpdatePlanEntry make_plan_entry(
        std::string name,
        AurUpdateClassification classification) {
    AurUpdatePlanEntry entry;
    entry.installed_name = std::move(name);
    entry.installed_version = "1.0-1";
    entry.install_reason = InstalledPackageReason::Explicit;
    entry.classification = classification;

    if(classification == AurUpdateClassification::UpdateAvailable ||
       classification == AurUpdateClassification::UpToDate ||
       classification == AurUpdateClassification::VersionComparisonUnavailable) {
        AurUpdateRemotePackage remote;
        remote.aur_name = entry.installed_name;
        remote.package_base = entry.installed_name;
        remote.version = classification == AurUpdateClassification::UpToDate
                ? "1.0-1"
                : "2.0-1";
        remote.version_relation =
                classification == AurUpdateClassification::UpToDate
                ? AurVersionRelation::SameAsInstalled
                : classification ==
                                  AurUpdateClassification::VersionComparisonUnavailable
                        ? AurVersionRelation::Unavailable
                        : AurVersionRelation::NewerThanInstalled;
        entry.aur_package = std::move(remote);
    }
    return entry;
}

AurUpdateExecutionIssue make_preflight_issue(
        AurUpdateExecutionReason reason,
        const std::string& package_name,
        std::string diagnostic) {
    AurUpdateExecutionIssue issue;
    issue.reason = reason;
    issue.package_name = package_name;
    issue.diagnostic = std::move(diagnostic);
    return issue;
}

AurUpdatePreparationIssue make_preparation_issue(
        AurUpdatePreparationReason reason,
        std::size_t update_plan_index,
        std::string diagnostic) {
    AurUpdatePreparationIssue issue;
    issue.reason = reason;
    issue.affected_update_plan_indices = {update_plan_index};
    issue.diagnostic = std::move(diagnostic);
    return issue;
}

AurUpdateOperationTargetResult make_target_result(
        const AurUpdateExecutionTarget& target,
        AurUpdateOperationTargetStatus status) {
    AurUpdateOperationTargetResult result;
    result.update_plan_index = target.update_plan_index;
    result.update = target.update;
    if(target.update.aur_package.has_value()) {
        result.package_base = target.update.aur_package->package_base;
    }
    result.status = status;
    result.preflight_issues = target.issues;
    return result;
}

AurUpdateOperationExecutionContribution make_contribution(
        std::size_t work_item_index,
        const std::string& package_name,
        AurUpdateWorkItemExecutionStatus status,
        AurUpdateWorkItemFailureKind failure_kind,
        std::optional<std::string> diagnostic = std::nullopt) {
    AurUpdateOperationExecutionContribution contribution;
    contribution.work_item_index = work_item_index;
    contribution.package_name = package_name;
    contribution.package_base = package_name;
    contribution.status = status;
    contribution.failure_kind = failure_kind;
    contribution.diagnostic = std::move(diagnostic);
    return contribution;
}

AurUpdateWorkItemExecutionResult make_work_item_result(
        std::size_t work_item_index,
        std::size_t update_plan_index,
        const std::string& package_name,
        AurUpdateWorkItemExecutionStatus status,
        AurUpdateWorkItemFailureKind failure_kind,
        std::optional<std::string> diagnostic = std::nullopt) {
    AurUpdateWorkItemExecutionResult result;
    result.work_item_index = work_item_index;
    result.package_name = package_name;
    result.package_base = package_name;
    result.plan_package_names = {package_name};
    result.affected_update_plan_indices = {update_plan_index};
    result.status = status;
    result.failure_kind = failure_kind;
    result.diagnostic = std::move(diagnostic);
    return result;
}

void set_execution_failure(
        AurUpdateOperationTargetResult& target,
        std::size_t work_item_index,
        AurUpdateWorkItemExecutionStatus work_item_status,
        AurUpdateWorkItemFailureKind failure_kind,
        std::optional<std::string> diagnostic) {
    target.execution_work_item_index = work_item_index;
    target.execution_failure_kind = failure_kind;
    target.execution_diagnostic = diagnostic;
    target.execution_contributions.push_back(make_contribution(
            work_item_index,
            target.update.installed_name,
            work_item_status,
            failure_kind,
            std::move(diagnostic)));
}

bool is_blocking_status(AurUpdateExecutionTargetStatus status) {
    return status == AurUpdateExecutionTargetStatus::Unsupported ||
            status == AurUpdateExecutionTargetStatus::Incomplete;
}

bool is_completed_failure_matrix_scenario(const std::string& test_scenario) {
    return test_scenario == "completed-preparation-issue" ||
            test_scenario == "completed-reduction-issue" ||
            test_scenario == "completed-unsupported-target" ||
            test_scenario == "completed-incomplete-target" ||
            test_scenario == "completed-execution-failure" ||
            test_scenario == "completed-invocation-execution-failure" ||
            test_scenario == "completed-invocation-cleanup-failure" ||
            test_scenario == "completed-missing-invocation-status" ||
            test_scenario == "completed-cleanup-failure" ||
            test_scenario == "completed-not-attempted";
}

} // namespace

AurUpdateQueryResult query_installed_aur_updates() {
    append_event("query");

    AurUpdateQueryResult query;
    const std::string& test_scenario = scenario();
    if(test_scenario == "no-installed-foreign") return query;
    if(test_scenario == "all-up-to-date") {
        query.plan.entries.push_back(make_plan_entry(
                "up-to-date-pkg", AurUpdateClassification::UpToDate));
        return query;
    }
    if(test_scenario == "non-aur-foreign") {
        query.plan.entries.push_back(make_plan_entry(
                "non-aur-pkg", AurUpdateClassification::NonAurForeign));
        return query;
    }
    if(test_scenario == "metadata-unavailable") {
        query.plan.entries.push_back(make_plan_entry(
                "metadata-pkg", AurUpdateClassification::MetadataUnavailable));
        return query;
    }
    if(test_scenario == "version-comparison-unavailable") {
        query.plan.entries.push_back(make_plan_entry(
                "version-pkg",
                AurUpdateClassification::VersionComparisonUnavailable));
        return query;
    }
    if(test_scenario == "unsupported-blocker") {
        query.plan.entries.push_back(make_plan_entry(
                "unsupported-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "incomplete-blocker") {
        query.plan.entries.push_back(make_plan_entry(
                "incomplete-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "preparation-failure") {
        query.plan.entries.push_back(make_plan_entry(
                "preparation-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "preparation-warning") {
        query.plan.entries.push_back(make_plan_entry(
                "warning-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "all-updated" ||
       test_scenario == "options-propagation") {
        query.plan.entries.push_back(make_plan_entry(
                "updated-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "all-no-change") {
        query.plan.entries.push_back(make_plan_entry(
                "no-change-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "updated-no-change-mixed") {
        query.plan.entries.push_back(make_plan_entry(
                "zeta-pkg", AurUpdateClassification::UpdateAvailable));
        query.plan.entries.push_back(make_plan_entry(
                "alpha-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "ordinary-execution-failure") {
        query.plan.entries.push_back(make_plan_entry(
                "failed-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "updated-cleanup-failure") {
        query.plan.entries.push_back(make_plan_entry(
                "updated-cleanup-pkg",
                AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "no-change-cleanup-failure") {
        query.plan.entries.push_back(make_plan_entry(
                "no-change-cleanup-pkg",
                AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "partial-completion") {
        query.plan.entries.push_back(make_plan_entry(
                "first-pkg", AurUpdateClassification::UpdateAvailable));
        query.plan.entries.push_back(make_plan_entry(
                "failed-pkg", AurUpdateClassification::UpdateAvailable));
        query.plan.entries.push_back(make_plan_entry(
                "later-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "reducer-inconsistency") {
        query.plan.entries.push_back(make_plan_entry(
                "inconsistent-pkg",
                AurUpdateClassification::UpdateAvailable));
        return query;
    }
    if(test_scenario == "query-recoverable-failure") {
        query.plan.entries.push_back(make_plan_entry(
                "query-survivor", AurUpdateClassification::UpdateAvailable));
        query.recoverable_failures.push_back(AurUpdateQueryFailure{
                {"query-broken", "query-also-broken"},
                "fixture RPC timeout"});
        return query;
    }
    if(is_completed_failure_matrix_scenario(test_scenario)) {
        query.plan.entries.push_back(make_plan_entry(
                "defensive-pkg", AurUpdateClassification::UpdateAvailable));
        return query;
    }

    throw std::logic_error("Unknown AUR update command test scenario: " +
                           test_scenario);
}

AurUpdateExecutionPreflight resolve_aur_update_execution_preflight(
        const AurUpdatePlan& update_plan) {
    append_event("preflight");

    AurUpdateExecutionPreflight preflight;
    for(std::size_t index = 0; index < update_plan.entries.size(); ++index) {
        AurUpdateExecutionTarget target;
        target.update_plan_index = index;
        target.update = update_plan.entries[index];

        switch(target.update.classification) {
            case AurUpdateClassification::UpdateAvailable:
                target.status = AurUpdateExecutionTargetStatus::Executable;
                target.desired_install_reason = DesiredInstallReason::Explicit;
                break;
            case AurUpdateClassification::UpToDate:
                target.status = AurUpdateExecutionTargetStatus::Skipped;
                target.issues.push_back(make_preflight_issue(
                        AurUpdateExecutionReason::UpToDate,
                        target.update.installed_name,
                        "installed package is already up to date"));
                break;
            case AurUpdateClassification::NonAurForeign:
                target.status = AurUpdateExecutionTargetStatus::Skipped;
                target.issues.push_back(make_preflight_issue(
                        AurUpdateExecutionReason::NonAurForeign,
                        target.update.installed_name,
                        "foreign package does not exist in AUR"));
                break;
            case AurUpdateClassification::MetadataUnavailable:
                target.status = AurUpdateExecutionTargetStatus::Incomplete;
                target.issues.push_back(make_preflight_issue(
                        AurUpdateExecutionReason::AurMetadataUnavailable,
                        target.update.installed_name,
                        "fixture AUR metadata request failed"));
                break;
            case AurUpdateClassification::VersionComparisonUnavailable:
                target.status = AurUpdateExecutionTargetStatus::Incomplete;
                target.issues.push_back(make_preflight_issue(
                        AurUpdateExecutionReason::VersionComparisonUnavailable,
                        target.update.installed_name,
                        "fixture version comparator failed"));
                break;
        }

        if(scenario() == "unsupported-blocker") {
            target.status = AurUpdateExecutionTargetStatus::Unsupported;
            target.issues.push_back(make_preflight_issue(
                    AurUpdateExecutionReason::SplitPackageSelectionRequired,
                    target.update.installed_name,
                    "fixture split package needs selection"));
        } else if(scenario() == "incomplete-blocker") {
            target.status = AurUpdateExecutionTargetStatus::Incomplete;
            target.issues.push_back(make_preflight_issue(
                    AurUpdateExecutionReason::UnresolvedDependency,
                    target.update.installed_name,
                    "fixture dependency could not be resolved"));
        }

        preflight.targets.push_back(std::move(target));
    }
    return preflight;
}

bool has_executable_targets(
        const AurUpdateExecutionPreflight& preflight) noexcept {
    return std::any_of(
            preflight.targets.begin(), preflight.targets.end(),
            [](const AurUpdateExecutionTarget& target) {
                return target.status == AurUpdateExecutionTargetStatus::Executable;
            });
}

bool has_blocking_targets(
        const AurUpdateExecutionPreflight& preflight) noexcept {
    return std::any_of(
            preflight.targets.begin(), preflight.targets.end(),
            [](const AurUpdateExecutionTarget& target) {
                return is_blocking_status(target.status);
            });
}

bool can_execute(const AurUpdateExecutionPreflight& preflight) noexcept {
    return has_executable_targets(preflight) && !has_blocking_targets(preflight);
}

PreparedAurUpdateSourceBuildInvocation::PreparedAurUpdateSourceBuildInvocation(
        PreparedProductionSourceBuildInvocation&& production_invocation,
        std::vector<AurUpdatePreparedWorkItemAttribution>&&
                work_item_attributions) noexcept
    : production_invocation_(std::move(production_invocation)),
      work_item_attributions_(std::move(work_item_attributions)) {
}

PreparedAurUpdateSourceBuildInvocation::PreparedAurUpdateSourceBuildInvocation(
        PreparedAurUpdateSourceBuildInvocation&& other) noexcept
    : production_invocation_(std::move(other.production_invocation_)),
      work_item_attributions_(std::move(other.work_item_attributions_)),
      valid_(other.valid_) {
    other.valid_ = false;
}

bool AurUpdateSourceBuildPreparation::is_prepared() const noexcept {
    return issues.empty() && invocation.has_value() && invocation->is_valid();
}

bool AurUpdateSourceBuildPreparation::is_noop() const noexcept {
    return issues.empty() && !invocation.has_value() &&
            std::none_of(
                    affected_update_targets.begin(),
                    affected_update_targets.end(),
                    [](const AurUpdateExecutionTarget& target) {
                        return target.status ==
                                AurUpdateExecutionTargetStatus::Executable;
                    });
}

bool AurUpdateSourceBuildPreparation::is_blocked() const noexcept {
    return !is_prepared() && !is_noop();
}

AurUpdateSourceBuildPreparation prepare_aur_update_source_build_invocation(
        const AurUpdateExecutionPreflight& preflight,
        bool needed,
        const AppConfig& config) {
    append_event(
            "prepare needed=" + std::string(bool_text(needed)) + " " +
            config_snapshot(config));

    AurUpdateSourceBuildPreparation preparation;
    preparation.affected_update_targets = preflight.targets;

    const bool should_create_invocation =
            has_executable_targets(preflight) && !has_blocking_targets(preflight);
    if(should_create_invocation) {
        PreparedProductionSourceBuildInvocation production_invocation;
        std::vector<AurUpdatePreparedWorkItemAttribution> attributions;
        PreparedAurUpdateSourceBuildInvocation invocation(
                std::move(production_invocation), std::move(attributions));
        preparation.invocation.emplace(std::move(invocation));
    }

    if(scenario() == "preparation-failure") {
        // LANDMINE(#267): invocationの存在だけではrunnerへ進めないことを検証する。
        preparation.issues.push_back(make_preparation_issue(
                AurUpdatePreparationReason::SourcePreferenceUnavailable,
                0,
                "fixture source preference read failed"));
    }
    if(scenario() == "preparation-warning") {
        AurUpdatePreparationWarning warning;
        warning.preference_name = "warning-pkg";
        warning.entry_path = "/fixture/package.build/warning-pkg";
        warning.affected_update_plan_indices = {0};
        warning.diagnostic = "fixture source preference warning";
        preparation.warnings.push_back(std::move(warning));
    }
    return preparation;
}

bool AurUpdateSourceBuildExecutionResult::is_success() const noexcept {
    return status == AurUpdateInvocationExecutionStatus::Completed;
}

bool AurUpdateSourceBuildExecutionResult::changed_package_state() const noexcept {
    return std::any_of(
            work_item_results.begin(), work_item_results.end(),
            [](const AurUpdateWorkItemExecutionResult& work_item) {
                return work_item.status == AurUpdateWorkItemExecutionStatus::Updated ||
                        work_item.status ==
                                AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed;
            });
}

bool AurUpdateSourceBuildExecutionResult::has_not_attempted_items() const noexcept {
    return std::any_of(
            work_item_results.begin(), work_item_results.end(),
            [](const AurUpdateWorkItemExecutionResult& work_item) {
                return work_item.status ==
                        AurUpdateWorkItemExecutionStatus::NotAttempted;
            });
}

bool AurUpdateSourceBuildExecutionResult::has_cleanup_failure() const noexcept {
    return status ==
                    AurUpdateInvocationExecutionStatus::
                            StoppedAfterPackageCleanupFailure ||
            std::any_of(
                    work_item_results.begin(), work_item_results.end(),
                    [](const AurUpdateWorkItemExecutionResult& work_item) {
                        return work_item.status ==
                                       AurUpdateWorkItemExecutionStatus::
                                               UpdatedCleanupFailed ||
                                work_item.status ==
                                       AurUpdateWorkItemExecutionStatus::
                                               NoChangeCleanupFailed;
                    });
}

std::optional<std::size_t>
AurUpdateSourceBuildExecutionResult::stopped_work_item_index() const noexcept {
    for(const auto& work_item : work_item_results) {
        if(work_item.status == AurUpdateWorkItemExecutionStatus::Failed ||
           work_item.status ==
                   AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed ||
           work_item.status ==
                   AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed) {
            return work_item.work_item_index;
        }
    }
    return std::nullopt;
}

AurUpdateSourceBuildExecutionResult
execute_prepared_aur_update_source_build_invocation(
        PreparedAurUpdateSourceBuildInvocation invocation,
        const AppConfig& config) {
    if(!invocation.is_valid()) {
        throw std::logic_error(
                "AUR update command passed an invalid prepared invocation.");
    }
    append_event("execute " + config_snapshot(config));
    append_event("external git clone fixture");
    append_event("external makepkg -sc fixture");
    append_event("external sudo pacman -U fixture");

    AurUpdateSourceBuildExecutionResult execution;
    if(scenario() == "ordinary-execution-failure" ||
       scenario() == "partial-completion") {
        execution.status =
                AurUpdateInvocationExecutionStatus::StoppedOnWorkItemFailure;
    } else if(scenario() == "updated-cleanup-failure" ||
              scenario() == "no-change-cleanup-failure") {
        execution.status = AurUpdateInvocationExecutionStatus::
                StoppedAfterPackageCleanupFailure;
    }
    return execution;
}

AurUpdateOperationResult reduce_aur_update_operation_result(
        const AurUpdateExecutionPreflight& preflight,
        const AurUpdateSourceBuildPreparation& preparation,
        const std::optional<AurUpdateSourceBuildExecutionResult>& execution) {
    append_event(
            "reduce execution=" +
            std::string(execution.has_value() ? "yes" : "no"));

    AurUpdateOperationResult result;
    result.preparation_issues = preparation.issues;
    result.preparation_warnings = preparation.warnings;
    if(execution.has_value()) result.execution_status = execution->status;

    const std::string& test_scenario = scenario();
    if(test_scenario == "no-installed-foreign") {
        result.status = AurUpdateOperationStatus::NoUpdates;
        return result;
    }

    for(const auto& target : preflight.targets) {
        AurUpdateOperationTargetStatus status =
                AurUpdateOperationTargetStatus::Updated;
        if(target.status == AurUpdateExecutionTargetStatus::Skipped) {
            status = AurUpdateOperationTargetStatus::Skipped;
        } else if(target.status == AurUpdateExecutionTargetStatus::Unsupported) {
            status = AurUpdateOperationTargetStatus::Unsupported;
        } else if(target.status == AurUpdateExecutionTargetStatus::Incomplete) {
            status = AurUpdateOperationTargetStatus::Incomplete;
        }
        result.targets.push_back(make_target_result(target, status));
    }

    if(test_scenario == "all-up-to-date" ||
       test_scenario == "non-aur-foreign") {
        result.status = AurUpdateOperationStatus::NoUpdates;
        return result;
    }
    if(test_scenario == "metadata-unavailable" ||
       test_scenario == "version-comparison-unavailable" ||
       test_scenario == "unsupported-blocker" ||
       test_scenario == "incomplete-blocker") {
        result.status = AurUpdateOperationStatus::BlockedBeforeExecution;
        return result;
    }
    if(test_scenario == "preparation-failure") {
        result.status = AurUpdateOperationStatus::BlockedBeforeExecution;
        result.targets.front().status = AurUpdateOperationTargetStatus::Incomplete;
        result.targets.front().preparation_issues = preparation.issues;
        return result;
    }
    if(test_scenario == "all-no-change") {
        result.status = AurUpdateOperationStatus::Completed;
        result.targets.front().status = AurUpdateOperationTargetStatus::NoChange;
        return result;
    }
    if(test_scenario == "updated-no-change-mixed") {
        result.status = AurUpdateOperationStatus::Completed;
        result.targets[0].status = AurUpdateOperationTargetStatus::Updated;
        result.targets[1].status = AurUpdateOperationTargetStatus::NoChange;
        return result;
    }
    if(is_completed_failure_matrix_scenario(test_scenario)) {
        // POLICY(#267): production reducerの正常出力では成立しない組合せを
        // stub境界だけで作り、Completedだけを信頼せずfail-closedに扱う契約を固定する。
        result.status = AurUpdateOperationStatus::Completed;
        if(test_scenario == "completed-preparation-issue") {
            result.preparation_issues.push_back(make_preparation_issue(
                    AurUpdatePreparationReason::SourcePreferenceUnavailable,
                    0,
                    "fixture completed preparation issue"));
        } else if(test_scenario == "completed-reduction-issue") {
            AurUpdateOperationReductionIssue issue;
            issue.reason = AurUpdateOperationReductionReason::
                    UnknownExecutionUpdatePlanIndex;
            issue.stage = AurUpdateOperationReductionStage::Execution;
            issue.affected_update_plan_indices = {99};
            issue.diagnostic = "fixture completed reduction issue";
            result.reduction_issues.push_back(std::move(issue));
        } else if(test_scenario == "completed-unsupported-target") {
            result.targets.front().status =
                    AurUpdateOperationTargetStatus::Unsupported;
            result.targets.front().preflight_issues.push_back(
                    make_preflight_issue(
                            AurUpdateExecutionReason::
                                    SplitPackageSelectionRequired,
                            "defensive-pkg",
                            "fixture completed unsupported target"));
        } else if(test_scenario == "completed-incomplete-target") {
            result.targets.front().status =
                    AurUpdateOperationTargetStatus::Incomplete;
            result.targets.front().preflight_issues.push_back(
                    make_preflight_issue(
                            AurUpdateExecutionReason::UnresolvedDependency,
                            "defensive-pkg",
                            "fixture completed incomplete target"));
        } else if(test_scenario == "completed-execution-failure") {
            result.execution_work_items.push_back(make_work_item_result(
                    0,
                    0,
                    "defensive-pkg",
                    AurUpdateWorkItemExecutionStatus::Failed,
                    AurUpdateWorkItemFailureKind::BuildOrInstallFailed,
                    "fixture completed execution failure"));
        } else if(test_scenario ==
                  "completed-invocation-execution-failure") {
            result.execution_status = AurUpdateInvocationExecutionStatus::
                    StoppedOnWorkItemFailure;
        } else if(test_scenario ==
                  "completed-invocation-cleanup-failure") {
            result.execution_status = AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure;
        } else if(test_scenario ==
                  "completed-missing-invocation-status") {
            result.execution_status.reset();
        } else if(test_scenario == "completed-cleanup-failure") {
            result.targets.front().status =
                    AurUpdateOperationTargetStatus::UpdatedCleanupFailed;
            set_execution_failure(
                    result.targets.front(),
                    0,
                    AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed,
                    AurUpdateWorkItemFailureKind::
                            CleanupFailedAfterPackageTransaction,
                    "fixture completed cleanup failure");
        } else if(test_scenario == "completed-not-attempted") {
            result.targets.front().status =
                    AurUpdateOperationTargetStatus::NotAttempted;
            set_execution_failure(
                    result.targets.front(),
                    0,
                    AurUpdateWorkItemExecutionStatus::NotAttempted,
                    AurUpdateWorkItemFailureKind::PriorWorkItemStopped,
                    std::nullopt);
            result.execution_work_items.push_back(make_work_item_result(
                    0,
                    0,
                    "defensive-pkg",
                    AurUpdateWorkItemExecutionStatus::NotAttempted,
                    AurUpdateWorkItemFailureKind::PriorWorkItemStopped));
        }
        return result;
    }
    if(test_scenario == "ordinary-execution-failure") {
        result.status = AurUpdateOperationStatus::StoppedOnWorkItemFailure;
        result.targets.front().status = AurUpdateOperationTargetStatus::Failed;
        set_execution_failure(
                result.targets.front(),
                0,
                AurUpdateWorkItemExecutionStatus::Failed,
                AurUpdateWorkItemFailureKind::BuildOrInstallFailed,
                "fixture build or install failed");
        return result;
    }
    if(test_scenario == "updated-cleanup-failure") {
        result.status =
                AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure;
        result.targets.front().status =
                AurUpdateOperationTargetStatus::UpdatedCleanupFailed;
        set_execution_failure(
                result.targets.front(),
                0,
                AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed,
                AurUpdateWorkItemFailureKind::
                        CleanupFailedAfterPackageTransaction,
                "fixture cleanup failed after update");
        return result;
    }
    if(test_scenario == "no-change-cleanup-failure") {
        result.status =
                AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure;
        result.targets.front().status =
                AurUpdateOperationTargetStatus::NoChangeCleanupFailed;
        set_execution_failure(
                result.targets.front(),
                0,
                AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed,
                AurUpdateWorkItemFailureKind::
                        CleanupFailedAfterPackageTransaction,
                "fixture cleanup failed without package change");
        return result;
    }
    if(test_scenario == "partial-completion") {
        result.status = AurUpdateOperationStatus::StoppedOnWorkItemFailure;
        result.targets[0].status = AurUpdateOperationTargetStatus::Updated;
        result.targets[0].execution_contributions.push_back(make_contribution(
                0,
                "first-pkg",
                AurUpdateWorkItemExecutionStatus::Updated,
                AurUpdateWorkItemFailureKind::None));
        result.targets[1].status = AurUpdateOperationTargetStatus::Failed;
        set_execution_failure(
                result.targets[1],
                1,
                AurUpdateWorkItemExecutionStatus::Failed,
                AurUpdateWorkItemFailureKind::BuildOrInstallFailed,
                "fixture second work item failed");
        result.targets[2].status = AurUpdateOperationTargetStatus::NotAttempted;
        set_execution_failure(
                result.targets[2],
                2,
                AurUpdateWorkItemExecutionStatus::NotAttempted,
                AurUpdateWorkItemFailureKind::PriorWorkItemStopped,
                std::nullopt);
        result.execution_work_items.push_back(make_work_item_result(
                2,
                2,
                "later-pkg",
                AurUpdateWorkItemExecutionStatus::NotAttempted,
                AurUpdateWorkItemFailureKind::PriorWorkItemStopped));
        return result;
    }
    if(test_scenario == "reducer-inconsistency") {
        result.status = AurUpdateOperationStatus::InconsistentResult;
        result.targets.front().status = AurUpdateOperationTargetStatus::Incomplete;
        AurUpdateOperationReductionIssue issue;
        issue.reason =
                AurUpdateOperationReductionReason::UnknownExecutionUpdatePlanIndex;
        issue.stage = AurUpdateOperationReductionStage::Execution;
        issue.affected_update_plan_indices = {99};
        issue.diagnostic = "fixture reducer mismatch";
        result.reduction_issues.push_back(std::move(issue));
        return result;
    }

    result.status = AurUpdateOperationStatus::Completed;
    return result;
}

bool AurUpdateOperationResult::is_success() const noexcept {
    return status == AurUpdateOperationStatus::NoUpdates ||
            status == AurUpdateOperationStatus::Completed;
}

bool AurUpdateOperationResult::changed_package_state() const noexcept {
    return std::any_of(
            targets.begin(), targets.end(),
            [](const AurUpdateOperationTargetResult& target) {
                return target.status == AurUpdateOperationTargetStatus::Updated ||
                        target.status ==
                                AurUpdateOperationTargetStatus::UpdatedCleanupFailed;
            });
}

bool AurUpdateOperationResult::has_partial_completion() const noexcept {
    return !is_success() && changed_package_state();
}

bool AurUpdateOperationResult::has_not_attempted_targets() const noexcept {
    return std::any_of(
            targets.begin(), targets.end(),
            [](const AurUpdateOperationTargetResult& target) {
                return target.status ==
                        AurUpdateOperationTargetStatus::NotAttempted;
            });
}

bool AurUpdateOperationResult::has_cleanup_failure() const noexcept {
    return status ==
                    AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure ||
            std::any_of(
                    targets.begin(), targets.end(),
                    [](const AurUpdateOperationTargetResult& target) {
                        return target.status ==
                                       AurUpdateOperationTargetStatus::
                                               UpdatedCleanupFailed ||
                                target.status ==
                                       AurUpdateOperationTargetStatus::
                                               NoChangeCleanupFailed;
                    });
}

bool AurUpdateOperationResult::has_blocking_targets() const noexcept {
    return std::any_of(
            targets.begin(), targets.end(),
            [](const AurUpdateOperationTargetResult& target) {
                return target.status == AurUpdateOperationTargetStatus::Unsupported ||
                        target.status ==
                                AurUpdateOperationTargetStatus::Incomplete;
            });
}
