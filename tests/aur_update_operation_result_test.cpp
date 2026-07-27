#include "app_config.hpp"
#include "aur_update_operation_result.hpp"
#include "stubs/aur-update-execution-preparation/preparation_stub.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace preparation_stub = aur_update_execution_preparation_test_stub;

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

AurUpdatePlanEntry update_entry(
        const std::string& package_name,
        const std::string& package_base = {}) {
    const std::string resolved_package_base =
            package_base.empty() ? package_name : package_base;
    return AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            InstalledPackageReason::Explicit,
            AurUpdateRemotePackage{
                    package_name,
                    resolved_package_base,
                    "2.0-1",
                    AurVersionRelation::NewerThanInstalled},
            AurUpdateClassification::UpdateAvailable};
}

AurUpdateExecutionTarget executable_target(
        std::size_t update_plan_index,
        const std::string& package_name) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.build_plan_root_index = update_plan_index;
    target.update = update_entry(package_name);
    target.status = AurUpdateExecutionTargetStatus::Executable;
    target.desired_install_reason = DesiredInstallReason::Explicit;
    return target;
}

AurUpdateExecutionTarget up_to_date_target(
        std::size_t update_plan_index,
        const std::string& package_name) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.update = AurUpdatePlanEntry{
            package_name,
            "2.0-1",
            InstalledPackageReason::Explicit,
            AurUpdateRemotePackage{
                    package_name,
                    package_name,
                    "2.0-1",
                    AurVersionRelation::SameAsInstalled},
            AurUpdateClassification::UpToDate};
    target.status = AurUpdateExecutionTargetStatus::Skipped;
    target.issues.push_back(AurUpdateExecutionIssue{
            AurUpdateExecutionReason::UpToDate,
            package_name,
            package_name,
            std::nullopt,
            "Installed package is already up to date."});
    return target;
}

AurUpdateExecutionTarget non_aur_target(
        std::size_t update_plan_index,
        const std::string& package_name) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.update = AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            InstalledPackageReason::Explicit,
            std::nullopt,
            AurUpdateClassification::NonAurForeign};
    target.status = AurUpdateExecutionTargetStatus::Skipped;
    target.issues.push_back(AurUpdateExecutionIssue{
            AurUpdateExecutionReason::NonAurForeign,
            package_name,
            std::nullopt,
            std::nullopt,
            "Installed foreign package is not present in AUR."});
    return target;
}

AurUpdateExecutionTarget blocking_target(
        std::size_t update_plan_index,
        const std::string& package_name,
        AurUpdateExecutionTargetStatus status,
        AurUpdateExecutionReason reason) {
    AurUpdateExecutionTarget target =
            executable_target(update_plan_index, package_name);
    target.status = status;
    target.issues.push_back(AurUpdateExecutionIssue{
            reason,
            package_name,
            package_name,
            std::nullopt,
            "Typed preflight blocker."});
    return target;
}

AurUpdateExecutionPreflight preflight_with(
        std::vector<AurUpdateExecutionTarget> targets) {
    AurUpdateExecutionPreflight preflight;
    preflight.targets = std::move(targets);
    return preflight;
}

AurUpdateSourceBuildPreparation preparation_for_execution(
        const AurUpdateExecutionPreflight& preflight) {
    AurUpdateSourceBuildPreparation preparation;
    for(const auto& target : preflight.targets) {
        if(target.status == AurUpdateExecutionTargetStatus::Executable) {
            preparation.affected_update_targets.push_back(target);
        }
    }
    return preparation;
}

AurUpdatePreparationIssue preparation_issue(
        AurUpdatePreparationReason reason,
        std::vector<std::size_t> affected_update_plan_indices,
        const std::string& diagnostic) {
    AurUpdatePreparationIssue issue;
    issue.reason = reason;
    issue.affected_update_plan_indices =
            std::move(affected_update_plan_indices);
    issue.diagnostic = diagnostic;
    return issue;
}

AurUpdateWorkItemExecutionResult work_item_result(
        std::size_t work_item_index,
        AurUpdateWorkItemExecutionStatus status,
        std::vector<std::size_t> affected_update_plan_indices,
        const std::string& package_base = {}) {
    const std::string resolved_package_base = package_base.empty()
            ? "work-" + std::to_string(work_item_index)
            : package_base;
    AurUpdateWorkItemExecutionResult result;
    result.work_item_index = work_item_index;
    result.package_name = resolved_package_base;
    result.package_base = resolved_package_base;
    result.plan_package_names = {resolved_package_base};
    result.affected_update_plan_indices =
            std::move(affected_update_plan_indices);
    result.status = status;

    switch(status) {
    case AurUpdateWorkItemExecutionStatus::Updated:
    case AurUpdateWorkItemExecutionStatus::NoChange:
        result.failure_kind = AurUpdateWorkItemFailureKind::None;
        break;
    case AurUpdateWorkItemExecutionStatus::Failed:
        result.failure_kind =
                AurUpdateWorkItemFailureKind::BuildOrInstallFailed;
        result.diagnostic = "scripted work item failure";
        break;
    case AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed:
        result.failure_kind = AurUpdateWorkItemFailureKind::
                CleanupFailedAfterPackageTransaction;
        result.diagnostic = "scripted cleanup failure";
        break;
    case AurUpdateWorkItemExecutionStatus::NotAttempted:
        result.failure_kind =
                AurUpdateWorkItemFailureKind::PriorWorkItemStopped;
        break;
    }
    return result;
}

AurUpdateSourceBuildExecutionResult execution_result(
        AurUpdateInvocationExecutionStatus status,
        std::vector<AurUpdateWorkItemExecutionResult> work_items) {
    return AurUpdateSourceBuildExecutionResult{
            status,
            std::move(work_items)};
}

bool has_reduction_issue(
        const AurUpdateOperationResult& result,
        AurUpdateOperationReductionReason reason) {
    return std::any_of(
            result.reduction_issues.begin(),
            result.reduction_issues.end(),
            [reason](const AurUpdateOperationReductionIssue& issue) {
                return issue.reason == reason;
            });
}

void expect_target_statuses(
        const AurUpdateOperationResult& result,
        const std::vector<AurUpdateOperationTargetStatus>& expected,
        const std::string& context) {
    expect(
            result.targets.size() == expected.size(),
            context + ": target count differs");
    for(std::size_t position = 0; position < expected.size(); ++position) {
        expect(
                result.targets[position].status == expected[position],
                context + ": target status differs at " +
                        std::to_string(position));
    }
}

AurUpdateExecutionPreflight prepared_single_root_preflight(
        const std::string& package_name) {
    const RootTargetIdentity root{0, package_name};
    BuildPlan plan;
    plan.root_targets.push_back(root);
    plan.package_targets.push_back(PlannedPackageTarget{
            package_name,
            package_name,
            {PackageRole::Root},
            {root}});
    plan.order.push_back(BuildPlanEntry{package_name, {package_name}});

    AurUpdateExecutionPreflight preflight;
    preflight.targets.push_back(executable_target(0, package_name));
    preflight.build_plan = std::move(plan);
    return preflight;
}

AurUpdateSourceBuildPreparation prepare_single_root(
        const AurUpdateExecutionPreflight& preflight) {
    preparation_stub::reset();
    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, false, config);
    expect(preparation.is_prepared(), "Real preparation fixture was not prepared");
    return preparation;
}

void test_all_skipped_is_no_updates() {
    AurUpdateExecutionTarget current =
            up_to_date_target(0, "current-package");
    AurUpdateExecutionTarget foreign =
            non_aur_target(1, "foreign-package");
    // POLICY(#267): normal skipではproducerがinstall reasonを未解決のまま
    // 保持するため、Unknownは不整合ではない。
    current.update.install_reason = InstalledPackageReason::Unknown;
    foreign.update.install_reason = InstalledPackageReason::Unknown;
    const AurUpdateExecutionPreflight preflight = preflight_with({
            std::move(current),
            std::move(foreign),
    });
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::NoUpdates,
            "Skip-only operation status differs");
    expect(result.is_success(), "Skip-only operation was not successful");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Skipped,
                    AurUpdateOperationTargetStatus::Skipped,
            },
            "skip-only operation");
    expect(
            !result.changed_package_state(),
            "Skip-only operation reported package mutation");
}

void test_skip_order_and_typed_reasons_are_preserved() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            up_to_date_target(0, "up-to-date"),
            non_aur_target(1, "non-aur"),
    });
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.targets[0].update.installed_name == "up-to-date" &&
                    result.targets[1].update.installed_name == "non-aur",
            "Skipped target order differs");
    expect(
            result.targets[0].update_plan_index == 0 &&
                    result.targets[1].update_plan_index == 1,
            "Skipped update plan indices differ");
    expect(
            result.targets[0].update.installed_version == "2.0-1" &&
                    result.targets[0].update.install_reason ==
                            InstalledPackageReason::Explicit &&
                    result.targets[0].update.classification ==
                            AurUpdateClassification::UpToDate &&
                    result.targets[0].update.aur_package.has_value() &&
                    result.targets[0].update.aur_package->aur_name ==
                            "up-to-date" &&
                    result.targets[0].update.aur_package->package_base ==
                            "up-to-date" &&
                    result.targets[0].update.aur_package->version ==
                            "2.0-1" &&
                    result.targets[0].update.aur_package->version_relation ==
                            AurVersionRelation::SameAsInstalled,
            "Original update plan entry was not retained as an owned value");
    expect(
            result.targets[0].preflight_issues.size() == 1 &&
                    result.targets[0].preflight_issues.front().reason ==
                            AurUpdateExecutionReason::UpToDate &&
                    result.targets[1].preflight_issues.size() == 1 &&
                    result.targets[1].preflight_issues.front().reason ==
                            AurUpdateExecutionReason::NonAurForeign,
            "Skipped typed reasons were flattened or reordered");
    expect(
            result.targets[0].package_base ==
                            std::optional<std::string>{"up-to-date"} &&
                    !result.targets[1].package_base.has_value(),
            "Skipped PackageBase ownership differs");
}

void test_skipped_without_normal_reason_is_inconsistent() {
    AurUpdateExecutionTarget malformed = up_to_date_target(0, "malformed-skip");
    malformed.issues.clear();
    const AurUpdateExecutionPreflight preflight =
            preflight_with({std::move(malformed)});
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    !result.is_success() &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    OtherCorrelationInconsistent),
            "Skipped target without a normal reason became NoUpdates");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Incomplete},
            "malformed skipped target");
}

void test_unknown_preflight_reason_is_typed() {
    AurUpdateExecutionTarget malformed = up_to_date_target(0, "unknown-reason");
    malformed.issues.front().reason =
            static_cast<AurUpdateExecutionReason>(999);
    const AurUpdateExecutionPreflight preflight =
            preflight_with({std::move(malformed)});
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownEnumValue),
            "Unknown preflight reason was not typed");
}

void test_unknown_owned_plan_enum_is_typed() {
    AurUpdateExecutionTarget malformed = up_to_date_target(0, "unknown-plan");
    malformed.update.classification =
            static_cast<AurUpdateClassification>(999);
    const AurUpdateExecutionPreflight preflight =
            preflight_with({std::move(malformed)});
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownEnumValue),
            "Unknown owned plan enum was not typed");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Incomplete},
            "unknown owned plan enum");
}

void test_executable_preflight_issue_keeps_known_update() {
    AurUpdateExecutionTarget malformed =
            executable_target(0, "malformed-executable");
    malformed.issues.push_back(AurUpdateExecutionIssue{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            "malformed-executable",
            "malformed-executable",
            std::nullopt,
            "Executable target retained a blocking issue."});
    const AurUpdateExecutionPreflight preflight =
            preflight_with({std::move(malformed)});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    OtherCorrelationInconsistent),
            "Executable target accepted a blocking preflight issue");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after executable preflight inconsistency");
}

void test_preflight_blockers_keep_status_and_stop_executable_targets() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "executable"),
            blocking_target(
                    1,
                    "unsupported",
                    AurUpdateExecutionTargetStatus::Unsupported,
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
            up_to_date_target(2, "skipped"),
            blocking_target(
                    3,
                    "incomplete",
                    AurUpdateExecutionTargetStatus::Incomplete,
                    AurUpdateExecutionReason::BuildPlanInconsistent),
    });
    preparation_stub::reset();
    const AppConfig config;
    const AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, false, config);

    expect(
            preparation.is_blocked() &&
                    preparation.affected_update_targets.size() == 2 &&
                    preparation.affected_update_targets[0]
                                    .update_plan_index == 1 &&
                    preparation.affected_update_targets[1]
                                    .update_plan_index == 3 &&
                    preparation.issues.size() == 2 &&
                    preparation.issues[0].reason ==
                            AurUpdatePreparationReason::BlockingPreflight &&
                    preparation.issues[1].reason ==
                            AurUpdatePreparationReason::BlockingPreflight,
            "Preflight blocker preparation fixture differs from the producer contract");

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status ==
                    AurUpdateOperationStatus::BlockedBeforeExecution,
            "Preflight blocker operation status differs");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::Unsupported,
                    AurUpdateOperationTargetStatus::Skipped,
                    AurUpdateOperationTargetStatus::Incomplete,
            },
            "preflight blocker mapping");
    expect(result.has_blocking_targets(), "Blocking target helper returned false");
    expect(
            result.has_not_attempted_targets(),
            "Blocked executable target was not reported as unattempted");
    expect(
            result.targets[1].preflight_issues.front().reason ==
                            AurUpdateExecutionReason::
                                    SplitPackageSelectionRequired &&
                    result.targets[3].preflight_issues.front().reason ==
                            AurUpdateExecutionReason::BuildPlanInconsistent,
            "Blocking preflight reasons were not retained");
    expect(
            result.reduction_issues.empty() &&
                    !has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Normal preflight blocker preparation was treated as an executable snapshot mismatch");
}

void test_preflight_blocker_missing_snapshot_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "executable"),
            blocking_target(
                    1,
                    "unsupported",
                    AurUpdateExecutionTargetStatus::Unsupported,
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
    });
    preparation_stub::reset();
    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, false, config);
    preparation.affected_update_targets.clear();

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Preflight blocker accepted a missing blocker target snapshot");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::Unsupported,
            },
            "preflight blocker with missing target snapshot");
}

void test_preflight_blocker_with_invocation_is_inconsistent() {
    AurUpdateSourceBuildPreparation invocation_source = prepare_single_root(
            prepared_single_root_preflight("prepared-invocation"));
    const AurUpdateExecutionPreflight preflight = preflight_with({
            blocking_target(
                    0,
                    "unsupported",
                    AurUpdateExecutionTargetStatus::Unsupported,
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
    });
    preparation_stub::reset();
    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, false, config);
    preparation.issues.clear();
    preparation.invocation.emplace(
            std::move(*invocation_source.invocation));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    OtherCorrelationInconsistent),
            "Preflight blocker accepted an execution invocation");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Unsupported},
            "preflight blocker with invocation");
}

void test_target_attributed_preparation_failure() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "untouched"),
            executable_target(1, "failed"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            {1},
            "typed target preparation failure"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status ==
                    AurUpdateOperationStatus::BlockedBeforeExecution,
            "Attributed preparation failure operation status differs");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::Failed,
            },
            "attributed preparation failure");
    expect(
            result.targets[0].preparation_issues.empty() &&
                    result.targets[1].preparation_issues.size() == 1 &&
                    result.targets[1].preparation_issues.front().reason ==
                            AurUpdatePreparationReason::
                                    SourcePreferenceUnavailable,
            "Preparation issue target attribution differs");
    expect(
            result.reduction_issues.empty(),
            "Normal target-attributed preparation failure was inconsistent");
}

void test_global_preparation_failure_stays_operation_level() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first"),
            executable_target(1, "second"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            {},
            "global preparation failure"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::NotAttempted,
            },
            "global preparation failure");
    expect(
            result.preparation_issues.size() == 1 &&
                    result.preparation_issues.front().reason ==
                            AurUpdatePreparationReason::
                                    GenericPreparationInconsistent &&
                    result.preparation_issues.front().diagnostic ==
                            "global preparation failure" &&
                    result.targets[0].preparation_issues.empty() &&
                    result.targets[1].preparation_issues.empty(),
            "Global preparation issue was not retained at operation level");
    expect(
            result.status ==
                    AurUpdateOperationStatus::BlockedBeforeExecution,
            "Global preparation failure did not block execution");
    expect(
            result.reduction_issues.empty(),
            "Normal global preparation failure was inconsistent");
}

void test_blocked_preparation_missing_target_snapshot_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "failed"),
            executable_target(1, "missing-snapshot"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.affected_update_targets.pop_back();
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            {0},
            "typed target preparation failure"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Blocked preparation accepted a missing executable target snapshot");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Failed,
                    AurUpdateOperationTargetStatus::NotAttempted,
            },
            "blocked preparation with missing target snapshot");
}

void test_blocked_preparation_target_snapshot_order_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first"),
            executable_target(1, "second"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    std::swap(
            preparation.affected_update_targets[0],
            preparation.affected_update_targets[1]);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            {},
            "global preparation failure"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Blocked preparation accepted an out-of-order target snapshot");
}

void test_preparation_issue_attributed_to_skipped_target_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "executable"),
            up_to_date_target(1, "skipped"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            {1},
            "invalid skipped-target attribution"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationAttributionInconsistent),
            "Preparation issue accepted attribution to a skipped target");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::Skipped,
            },
            "preparation issue attributed to skipped target");
    expect(
            result.targets[1].preparation_issues.size() == 1,
            "Malformed preparation issue attribution was discarded");
}

void test_preparation_warning_attributed_to_skipped_target_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "executable"),
            up_to_date_target(1, "skipped"),
    });
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            {},
            "global preparation failure"));
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "skipped-warning",
            fs::path("/stub/preferences/skipped-warning"),
            {1},
            {},
            "invalid skipped-target warning attribution"});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    result.preparation_warnings.size() == 1 &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationAttributionInconsistent),
            "Preparation warning accepted attribution to a skipped target");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NotAttempted,
                    AurUpdateOperationTargetStatus::Skipped,
            },
            "preparation warning attributed to skipped target");
    expect(
            result.preparation_warnings.front()
                            .affected_update_plan_indices ==
                    std::vector<std::size_t>{1},
            "Malformed preparation warning attribution was discarded");
}

void test_skip_only_global_warning_remains_no_updates() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({up_to_date_target(0, "skipped")});
    AurUpdateSourceBuildPreparation preparation;
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "global-warning",
            fs::path("/stub/preferences/global-warning"),
            {},
            {},
            "global warning"});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::NoUpdates &&
                    result.is_success() &&
                    result.preparation_warnings.size() == 1 &&
                    result.reduction_issues.empty(),
            "A global warning changed a normal skip-only operation");
}

void test_skip_only_global_preparation_issue_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({up_to_date_target(0, "skipped")});
    AurUpdateSourceBuildPreparation preparation;
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            {},
            "impossible no-target preparation issue"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    result.preparation_issues.size() == 1 &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationAttributionInconsistent),
            "Skip-only operation accepted a preparation issue");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Skipped},
            "skip-only operation with preparation issue");
}

void test_skip_only_target_warning_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({up_to_date_target(0, "skipped")});
    AurUpdateSourceBuildPreparation preparation;
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "skipped-warning",
            fs::path("/stub/preferences/skipped-warning"),
            {0},
            {},
            "impossible no-target warning attribution"});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    result.preparation_warnings.size() == 1 &&
                    result.preparation_warnings.front()
                                    .affected_update_plan_indices ==
                            std::vector<std::size_t>{0} &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationAttributionInconsistent),
            "Skip-only operation accepted target-attributed warning");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Skipped},
            "skip-only operation with target warning");
}

void test_skip_only_preparation_snapshot_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({up_to_date_target(0, "skipped")});
    AurUpdateSourceBuildPreparation preparation;
    preparation.affected_update_targets.push_back(preflight.targets.front());

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Skip-only operation accepted a preparation target snapshot");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Skipped},
            "skip-only operation with preparation snapshot");
}

void test_unknown_preparation_reason_is_typed() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "unknown-preparation")});
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            static_cast<AurUpdatePreparationReason>(999),
            {0},
            "unknown preparation reason"));

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownEnumValue),
            "Unknown preparation reason was not typed");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Failed},
            "unknown preparation reason");
}

void test_build_unit_preparation_reasons_are_known_blockers() {
    for(const AurUpdatePreparationReason reason : {
                AurUpdatePreparationReason::BuildUnitSelectionInconsistent,
                AurUpdatePreparationReason::ExternalSatisfactionInconsistent}) {
        const AurUpdateExecutionPreflight preflight =
                preflight_with({executable_target(0, "selection-blocked")});
        AurUpdateSourceBuildPreparation preparation =
                preparation_for_execution(preflight);
        preparation.issues.push_back(preparation_issue(
                reason, {0}, "typed build-unit preparation blocker"));

        const AurUpdateOperationResult result =
                reduce_aur_update_operation_result(
                        preflight, preparation, std::nullopt);
        expect(
                result.status ==
                                AurUpdateOperationStatus::BlockedBeforeExecution &&
                        !has_reduction_issue(
                                result,
                                AurUpdateOperationReductionReason::
                                        UnknownEnumValue),
                "Build-unit preparation reason was not accepted as a known blocker");
        expect_target_statuses(
                result, {AurUpdateOperationTargetStatus::Failed},
                "typed build-unit preparation blocker");
    }
}

void test_preparation_warning_only_does_not_fail_execution() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "warning-root")});
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "warning-root",
            fs::path("/stub/preferences/warning-root"),
            {0},
            {{0, "warning-root"}},
            "first warning"});
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "global-warning",
            fs::path("/stub/preferences/global-warning"),
            {},
            {},
            "second warning"});
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::NoChange,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.is_success(),
            "Warning-only operation was treated as failure");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::NoChange},
            "warning-only operation");
    expect(
            result.preparation_warnings.size() == 2 &&
                    result.preparation_warnings[0].diagnostic ==
                            "first warning" &&
                    result.preparation_warnings[1].diagnostic ==
                            "second warning" &&
                    result.preparation_warnings[0]
                                    .affected_update_plan_indices ==
                            std::vector<std::size_t>{0} &&
                    result.preparation_warnings[0].affected_roots ==
                            std::vector<RootTargetIdentity>{{0, "warning-root"}} &&
                    result.preparation_warnings[1]
                            .affected_update_plan_indices.empty() &&
                    result.preparation_warnings[1].affected_roots.empty() &&
                    result.preparation_warnings[1].entry_path ==
                            fs::path("/stub/preferences/global-warning"),
            "Preparation warning order or attribution differs");
}

void test_unknown_warning_attribution_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({up_to_date_target(0, "warning-target")});
    AurUpdateSourceBuildPreparation preparation;
    preparation.warnings.push_back(AurUpdatePreparationWarning{
            "unknown-warning",
            fs::path("/stub/preferences/unknown-warning"),
            {99},
            {},
            "unknown warning attribution"});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    result.preparation_warnings.size() == 1 &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownPreparationUpdatePlanIndex),
            "Unknown warning attribution was accepted or discarded");
}

void test_all_updated() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first"),
            executable_target(1, "second"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {1}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::Updated,
            },
            "all-updated operation");
    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.changed_package_state() &&
                    !result.has_partial_completion(),
            "All-updated helpers differ");
}

void test_all_no_change() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first"),
            executable_target(1, "second"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::NoChange,
                            {0}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::NoChange,
                            {1}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NoChange,
                    AurUpdateOperationTargetStatus::NoChange,
            },
            "all-no-change operation");
    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    !result.changed_package_state(),
            "All-no-change helpers differ");
}

void test_updated_and_no_change_targets() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "updated-target"),
            executable_target(1, "unchanged-target"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::NoChange,
                            {1}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::NoChange,
            },
            "updated/no-change targets");
    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.changed_package_state() &&
                    !result.has_partial_completion() &&
                    result.reduction_issues.empty(),
            "Updated/no-change target aggregate differs");
}

void test_updated_and_no_change_contributions_fold_to_updated() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "mixed-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0},
                            "dependency"),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::NoChange,
                            {0},
                            "mixed-root"),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Updated},
            "updated/no-change fold");
    expect(
            result.targets.front().execution_contributions.size() == 2 &&
                    result.targets.front().execution_work_item_index ==
                            std::optional<std::size_t>{0} &&
                    !has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    DuplicateExecutionAttribution),
            "Normal many-to-one attribution was treated as duplicate");
}

void test_one_work_item_projects_to_multiple_targets() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first-root"),
            executable_target(1, "second-root"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::NoChange,
                    {0, 1},
                    "shared-dependency")});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::NoChange,
                    AurUpdateOperationTargetStatus::NoChange,
            },
            "shared work-item projection");
    for(const auto& target : result.targets) {
        expect(
                target.execution_contributions.size() == 1 &&
                        target.execution_work_item_index ==
                                std::optional<std::size_t>{0} &&
                        target.execution_contributions.front().package_base ==
                                "shared-dependency",
                "Shared work-item contribution differs");
    }
    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.reduction_issues.empty(),
            "Normal one-to-many execution attribution was rejected");
}

void test_ordinary_failure_and_not_attempted_suffix() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "updated"),
            executable_target(1, "failed"),
            executable_target(2, "not-attempted"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::StoppedOnWorkItemFailure,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::Failed,
                            {1}),
                    work_item_result(
                            2,
                            AurUpdateWorkItemExecutionStatus::NotAttempted,
                            {2}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status ==
                    AurUpdateOperationStatus::StoppedOnWorkItemFailure,
            "Ordinary failure operation status differs");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::Failed,
                    AurUpdateOperationTargetStatus::NotAttempted,
            },
            "ordinary failure mapping");
    expect(
            result.targets[1].execution_failure_kind ==
                            std::optional<AurUpdateWorkItemFailureKind>{
                                    AurUpdateWorkItemFailureKind::
                                            BuildOrInstallFailed} &&
                    result.targets[1].execution_diagnostic ==
                            std::optional<std::string>{
                                    "scripted work item failure"},
            "Ordinary failure typed detail differs");
    expect(
            result.has_partial_completion() &&
                    result.has_not_attempted_targets(),
            "Ordinary failure helpers differ");
}

void test_updated_cleanup_failure() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "cleanup-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::UpdatedCleanupFailed},
            "updated cleanup failure");
    expect(
            result.status == AurUpdateOperationStatus::
                                     StoppedAfterPackageCleanupFailure &&
                    result.changed_package_state() &&
                    result.has_partial_completion() &&
                    result.has_cleanup_failure(),
            "Updated cleanup failure helpers differ");
}

void test_no_change_cleanup_failure() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "cleanup-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::
                            NoChangeCleanupFailed,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::NoChangeCleanupFailed},
            "no-change cleanup failure");
    expect(
            result.status == AurUpdateOperationStatus::
                                     StoppedAfterPackageCleanupFailure &&
                    !result.changed_package_state() &&
                    !result.has_partial_completion() &&
                    result.has_cleanup_failure(),
            "No-change cleanup failure helpers differ");
}

void test_cleanup_failure_after_prior_update_is_partial_completion() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "updated"),
            executable_target(1, "cleanup-failed"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::
                                    NoChangeCleanupFailed,
                            {1}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::NoChangeCleanupFailed,
            },
            "cleanup failure after update");
    expect(
            result.changed_package_state() &&
                    result.has_partial_completion() &&
                    result.has_cleanup_failure(),
            "Cleanup partial-completion helpers differ");
}

void test_preflight_target_order_is_not_replaced_by_work_item_order() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first-target"),
            up_to_date_target(1, "skipped-target"),
            executable_target(2, "third-target"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::NoChange,
                            {2}),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0}),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.targets[0].update.installed_name == "first-target" &&
                    result.targets[1].update.installed_name ==
                            "skipped-target" &&
                    result.targets[2].update.installed_name == "third-target",
            "Result order followed work-item order");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::Skipped,
                    AurUpdateOperationTargetStatus::NoChange,
            },
            "preflight-order result");
    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.reduction_issues.empty(),
            "Work-item order differing from target order was rejected");
}

void test_moved_from_preparation_uses_valid_execution_result() {
    const AurUpdateExecutionPreflight preflight =
            prepared_single_root_preflight("moved-root");
    AurUpdateSourceBuildPreparation preparation =
            prepare_single_root(preflight);

    PreparedAurUpdateSourceBuildInvocation consumed =
            std::move(*preparation.invocation);
    expect(consumed.is_valid(), "Moved invocation destination is invalid");
    expect(
            preparation.invocation.has_value() &&
                    !preparation.invocation->is_valid(),
            "Preparation fixture is not in the moved-from state");

    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0},
                    "moved-root")});
    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::Completed &&
                    result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.reduction_issues.empty(),
            "Moved-from preparation was treated as execution failure");
}

void test_prepared_target_without_execution_result_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            prepared_single_root_preflight("missing-result-root");
    const AurUpdateSourceBuildPreparation preparation =
            prepare_single_root(preflight);

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    MissingExecutionResult),
            "Missing execution result was not typed as inconsistent");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::NotAttempted},
            "missing execution result");
}

void test_execution_without_preparation_snapshot_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "missing-snapshot")});
    const AurUpdateSourceBuildPreparation preparation;
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Execution without a preparation target snapshot was accepted");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after preparation snapshot mismatch");
}

void test_execution_with_mismatched_preparation_snapshot_keeps_update() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "mismatched-snapshot")});
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.affected_update_targets.front().update.installed_version =
            "unexpected-version";
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    PreparationTargetSnapshotInconsistent),
            "Execution accepted a preparation snapshot identity mismatch");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after preparation snapshot identity mismatch");
}

void test_duplicate_execution_attribution_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "duplicate-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0, 0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    DuplicateExecutionAttribution),
            "Duplicate execution attribution was not typed");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.targets.front().execution_contributions.size() == 1,
            "Duplicate attribution discarded or duplicated known update");
}

void test_out_of_range_execution_attribution_preserves_known_update() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "known-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0, 99})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownExecutionUpdatePlanIndex),
            "Out-of-range execution attribution was not typed");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after out-of-range attribution");
}

void test_only_unknown_execution_attribution_preserves_work_item() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "unmapped-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {99},
                    "known-updated-base")});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownExecutionUpdatePlanIndex) &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    MissingExecutionAttribution),
            "Fully unknown execution attribution was not typed");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::NotAttempted},
            "fully unknown execution attribution");
    expect(
            result.execution_status ==
                            std::optional<AurUpdateInvocationExecutionStatus>{
                                    AurUpdateInvocationExecutionStatus::
                                            Completed} &&
                    result.execution_work_items.size() == 1 &&
                    result.execution_work_items.front().package_base ==
                            "known-updated-base" &&
                    result.execution_work_items.front().status ==
                            AurUpdateWorkItemExecutionStatus::Updated &&
                    result.execution_work_items.front().failure_kind ==
                            AurUpdateWorkItemFailureKind::None &&
                    result.execution_work_items.front()
                                    .affected_update_plan_indices ==
                            std::vector<std::size_t>{99} &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Unattributed known update work item was discarded");
}

void test_missing_execution_attribution_is_inconsistent() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "known-root"),
            executable_target(1, "missing-root"),
    });
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    MissingExecutionAttribution),
            "Missing execution attribution was not typed");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::NotAttempted,
            },
            "missing execution attribution");
}

void test_execution_with_preparation_issue_keeps_known_update() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "known-root")});
    AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    preparation.issues.push_back(preparation_issue(
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            {0},
            "inconsistent preparation issue"));
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    ExecutionResultWithPreparationIssues),
            "Execution/preparation contradiction was not typed");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.targets.front().preparation_issues.size() == 1 &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after preparation contradiction");
}

void test_duplicate_preflight_index_keeps_exact_target_count() {
    const AurUpdateExecutionPreflight preflight = preflight_with({
            executable_target(0, "first-duplicate"),
            executable_target(0, "second-duplicate"),
    });
    const AurUpdateSourceBuildPreparation preparation;

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, std::nullopt);

    expect(
            result.targets.size() == preflight.targets.size() &&
                    result.targets[0].update.installed_name ==
                            "first-duplicate" &&
                    result.targets[1].update.installed_name ==
                            "second-duplicate",
            "Duplicate preflight index changed target count or order");
    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    DuplicatePreflightUpdatePlanIndex),
            "Duplicate preflight index was not typed");
    expect_target_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Incomplete,
                    AurUpdateOperationTargetStatus::Incomplete,
            },
            "duplicate preflight index");
}

void test_unknown_execution_enum_is_typed_without_throwing() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "unknown-status-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    AurUpdateWorkItemExecutionResult unknown = work_item_result(
            0,
            AurUpdateWorkItemExecutionStatus::NoChange,
            {0});
    unknown.status = static_cast<AurUpdateWorkItemExecutionStatus>(999);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {std::move(unknown)});

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownEnumValue),
            "Unknown execution enum was not typed");
    expect_target_statuses(
            result,
            {AurUpdateOperationTargetStatus::Incomplete},
            "unknown execution enum");
}

void test_unknown_invocation_status_preserves_known_update() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "unknown-invocation")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::Completed,
            {work_item_result(
                    0,
                    AurUpdateWorkItemExecutionStatus::Updated,
                    {0})});
    execution.status =
            static_cast<AurUpdateInvocationExecutionStatus>(999);

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.status == AurUpdateOperationStatus::InconsistentResult &&
                    has_reduction_issue(
                            result,
                            AurUpdateOperationReductionReason::
                                    UnknownEnumValue),
            "Unknown invocation status was not typed");
    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.changed_package_state() &&
                    result.has_partial_completion(),
            "Known update was lost after unknown invocation status");
}

void test_same_target_partial_update_and_failure_keeps_contributions() {
    const AurUpdateExecutionPreflight preflight =
            preflight_with({executable_target(0, "multi-work-root")});
    const AurUpdateSourceBuildPreparation preparation =
            preparation_for_execution(preflight);
    const AurUpdateSourceBuildExecutionResult execution = execution_result(
            AurUpdateInvocationExecutionStatus::StoppedOnWorkItemFailure,
            {
                    work_item_result(
                            0,
                            AurUpdateWorkItemExecutionStatus::Updated,
                            {0},
                            "dependency"),
                    work_item_result(
                            1,
                            AurUpdateWorkItemExecutionStatus::Failed,
                            {0},
                            "multi-work-root"),
                    work_item_result(
                            2,
                            AurUpdateWorkItemExecutionStatus::NotAttempted,
                            {0},
                            "suffix"),
            });

    const AurUpdateOperationResult result =
            reduce_aur_update_operation_result(
                    preflight, preparation, execution);

    expect(
            result.targets.front().status ==
                            AurUpdateOperationTargetStatus::Failed &&
                    result.targets.front().execution_work_item_index ==
                            std::optional<std::size_t>{1} &&
                    result.targets.front().execution_contributions.size() == 3,
            "Same-target terminal fold differs");
    expect(
            result.changed_package_state() &&
                    result.has_partial_completion() &&
                    !result.has_not_attempted_targets(),
            "Same-target partial mutation helpers differ");
}

void test_all_query_helpers() {
    AurUpdateOperationResult no_updates;
    no_updates.status = AurUpdateOperationStatus::NoUpdates;
    expect(no_updates.is_success(), "NoUpdates helper returned failure");
    expect(
            !no_updates.changed_package_state() &&
                    !no_updates.has_partial_completion() &&
                    !no_updates.has_not_attempted_targets() &&
                    !no_updates.has_cleanup_failure() &&
                    !no_updates.has_blocking_targets(),
            "NoUpdates helper baseline differs");

    AurUpdateOperationResult completed;
    completed.status = AurUpdateOperationStatus::Completed;
    AurUpdateOperationTargetResult updated;
    updated.status = AurUpdateOperationTargetStatus::Updated;
    completed.targets.push_back(std::move(updated));
    expect(
            completed.is_success() &&
                    completed.changed_package_state() &&
                    !completed.has_partial_completion(),
            "Completed helper semantics differ");

    AurUpdateOperationResult partial;
    partial.status =
            AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure;
    AurUpdateOperationTargetResult cleanup;
    cleanup.status =
            AurUpdateOperationTargetStatus::UpdatedCleanupFailed;
    AurUpdateOperationTargetResult not_attempted;
    not_attempted.status = AurUpdateOperationTargetStatus::NotAttempted;
    AurUpdateOperationTargetResult blocker;
    blocker.status = AurUpdateOperationTargetStatus::Unsupported;
    partial.targets = {
            std::move(cleanup),
            std::move(not_attempted),
            std::move(blocker)};
    expect(
            !partial.is_success() &&
                    partial.changed_package_state() &&
                    partial.has_partial_completion() &&
                    partial.has_not_attempted_targets() &&
                    partial.has_cleanup_failure() &&
                    partial.has_blocking_targets(),
            "Failure-state helper semantics differ");
}

template<typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("all skipped is NoUpdates", test_all_skipped_is_no_updates);
        run_case(
                "skip order and typed reasons are preserved",
                test_skip_order_and_typed_reasons_are_preserved);
        run_case(
                "skipped target without normal reason is inconsistent",
                test_skipped_without_normal_reason_is_inconsistent);
        run_case(
                "unknown preflight reason is typed",
                test_unknown_preflight_reason_is_typed);
        run_case(
                "unknown owned plan enum is typed",
                test_unknown_owned_plan_enum_is_typed);
        run_case(
                "executable preflight issue keeps known update",
                test_executable_preflight_issue_keeps_known_update);
        run_case(
                "preflight blockers preserve status and stop executables",
                test_preflight_blockers_keep_status_and_stop_executable_targets);
        run_case(
                "preflight blocker missing snapshot is inconsistent",
                test_preflight_blocker_missing_snapshot_is_inconsistent);
        run_case(
                "preflight blocker with invocation is inconsistent",
                test_preflight_blocker_with_invocation_is_inconsistent);
        run_case(
                "target-attributed preparation failure",
                test_target_attributed_preparation_failure);
        run_case(
                "global preparation failure stays operation-level",
                test_global_preparation_failure_stays_operation_level);
        run_case(
                "blocked preparation missing target snapshot is inconsistent",
                test_blocked_preparation_missing_target_snapshot_is_inconsistent);
        run_case(
                "blocked preparation target order is inconsistent",
                test_blocked_preparation_target_snapshot_order_is_inconsistent);
        run_case(
                "preparation issue cannot target a skipped target",
                test_preparation_issue_attributed_to_skipped_target_is_inconsistent);
        run_case(
                "preparation warning cannot target a skipped target",
                test_preparation_warning_attributed_to_skipped_target_is_inconsistent);
        run_case(
                "skip-only global warning remains NoUpdates",
                test_skip_only_global_warning_remains_no_updates);
        run_case(
                "skip-only preparation issue is inconsistent",
                test_skip_only_global_preparation_issue_is_inconsistent);
        run_case(
                "skip-only target warning is inconsistent",
                test_skip_only_target_warning_is_inconsistent);
        run_case(
                "skip-only preparation snapshot is inconsistent",
                test_skip_only_preparation_snapshot_is_inconsistent);
        run_case(
                "unknown preparation reason is typed",
                test_unknown_preparation_reason_is_typed);
        run_case(
                "build-unit preparation reasons are known blockers",
                test_build_unit_preparation_reasons_are_known_blockers);
        run_case(
                "preparation warning alone does not fail execution",
                test_preparation_warning_only_does_not_fail_execution);
        run_case(
                "unknown warning attribution is inconsistent",
                test_unknown_warning_attribution_is_inconsistent);
        run_case("all updated", test_all_updated);
        run_case("all no-change", test_all_no_change);
        run_case(
                "updated and no-change targets",
                test_updated_and_no_change_targets);
        run_case(
                "updated/no-change contributions fold to updated",
                test_updated_and_no_change_contributions_fold_to_updated);
        run_case(
                "one work item projects to multiple targets",
                test_one_work_item_projects_to_multiple_targets);
        run_case(
                "ordinary failure and not-attempted suffix",
                test_ordinary_failure_and_not_attempted_suffix);
        run_case("updated cleanup failure", test_updated_cleanup_failure);
        run_case(
                "no-change cleanup failure",
                test_no_change_cleanup_failure);
        run_case(
                "cleanup failure after prior update is partial completion",
                test_cleanup_failure_after_prior_update_is_partial_completion);
        run_case(
                "preflight target order is preserved",
                test_preflight_target_order_is_not_replaced_by_work_item_order);
        run_case(
                "moved-from preparation uses valid execution result",
                test_moved_from_preparation_uses_valid_execution_result);
        run_case(
                "prepared target without execution result is inconsistent",
                test_prepared_target_without_execution_result_is_inconsistent);
        run_case(
                "execution without preparation snapshot is inconsistent",
                test_execution_without_preparation_snapshot_is_inconsistent);
        run_case(
                "execution snapshot mismatch keeps known update",
                test_execution_with_mismatched_preparation_snapshot_keeps_update);
        run_case(
                "duplicate execution attribution is inconsistent",
                test_duplicate_execution_attribution_is_inconsistent);
        run_case(
                "out-of-range attribution preserves known update",
                test_out_of_range_execution_attribution_preserves_known_update);
        run_case(
                "fully unknown attribution preserves work item",
                test_only_unknown_execution_attribution_preserves_work_item);
        run_case(
                "missing execution attribution is inconsistent",
                test_missing_execution_attribution_is_inconsistent);
        run_case(
                "execution with preparation issue keeps known update",
                test_execution_with_preparation_issue_keeps_known_update);
        run_case(
                "duplicate preflight index keeps exact target count",
                test_duplicate_preflight_index_keeps_exact_target_count);
        run_case(
                "unknown execution enum is typed",
                test_unknown_execution_enum_is_typed_without_throwing);
        run_case(
                "unknown invocation status preserves known update",
                test_unknown_invocation_status_preserves_known_update);
        run_case(
                "same-target partial update and failure keeps contributions",
                test_same_target_partial_update_and_failure_keeps_contributions);
        run_case("all query helpers", test_all_query_helpers);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update operation result tests: all checks passed\n";
    return 0;
}
