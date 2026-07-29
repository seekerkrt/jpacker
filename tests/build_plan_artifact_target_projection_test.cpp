#include "build_plan_artifact_target_projection.hpp"

#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(
        !std::is_default_constructible_v<
                BuildPlanArtifactTargetProjectionResult>);
static_assert(
        !std::is_aggregate_v<BuildPlanArtifactTargetProjectionResult>);
static_assert(
        !std::is_constructible_v<
                BuildPlanArtifactTargetProjectionResult,
                BuildPlanArtifactTargetProjectionSuccess>);
static_assert(
        !std::is_constructible_v<
                BuildPlanArtifactTargetProjectionResult,
                BuildPlanArtifactTargetProjectionFailure>);
static_assert(std::same_as<
              decltype(std::declval<
                               const BuildPlanArtifactTargetProjectionResult&>()
                               .success()),
              const BuildPlanArtifactTargetProjectionSuccess*>);
static_assert(std::same_as<
              decltype(std::declval<
                               const BuildPlanArtifactTargetProjectionResult&>()
                               .failure()),
              const BuildPlanArtifactTargetProjectionFailure*>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

RootTargetIdentity root(std::size_t invocation_index,
                        const std::string& requested_name) {
    return RootTargetIdentity{invocation_index, requested_name};
}

PlannedPackageTarget package_target(
        const std::string& package_name, const std::string& package_base,
        std::vector<PackageRole> roles,
        std::vector<RootTargetIdentity> roots) {
    return PlannedPackageTarget{
            package_name, package_base, std::move(roles), std::move(roots)};
}

const BuildPlanArtifactTargetProjectionSuccess& expect_success(
        const BuildPlanArtifactTargetProjectionResult& result,
        const char* context) {
    const std::string context_text(context);
    expect(result.is_success(), context_text + " did not return success");
    expect(result.success() != nullptr, context_text + " has no success payload");
    expect(result.failure() == nullptr, context_text + " also has a failure payload");
    return *result.success();
}

const BuildPlanArtifactTargetProjectionFailure& expect_failure(
        const BuildPlanArtifactTargetProjectionResult& result,
        const char* context) {
    const std::string context_text(context);
    expect(!result.is_success(), context_text + " unexpectedly returned success");
    expect(result.success() == nullptr, context_text + " exposes partial targets");
    expect(result.failure() != nullptr, context_text + " has no failure payload");
    return *result.failure();
}

bool has_issue(
        const BuildPlanArtifactTargetProjectionFailure& failure,
        BuildPlanArtifactTargetProjectionIssueKind expected_kind) {
    for(const auto& issue : failure.issues) {
        if(issue.kind == expected_kind) return true;
    }
    return false;
}

void expect_failure_issue(
        const BuildPlanArtifactTargetProjectionResult& result,
        const char* context,
        BuildPlanArtifactTargetProjectionIssueKind expected_kind,
        const std::string& missing_issue_message) {
    const auto& failure = expect_failure(result, context);
    expect(has_issue(failure, expected_kind), missing_issue_message);
}

void expect_required_target(
        const RequiredPackageArtifactTarget& target,
        const std::string& expected_package_base,
        const std::string& expected_package_name,
        DesiredInstallReason expected_reason,
        const std::string& context) {
    expect(
            target.package_base == expected_package_base,
            context + " PackageBase differs");
    expect(
            target.package_name == expected_package_name,
            context + " package name differs");
    expect(
            target.desired_reason == expected_reason,
            context + " desired reason differs");
}

BuildPlan ordinary_plan() {
    BuildPlan plan;
    const RootTargetIdentity package_root = root(0, "ordinary-package");
    plan.root_targets.push_back(package_root);
    plan.package_targets.push_back(package_target(
            "ordinary-package", "ordinary-package", {PackageRole::Root},
            {package_root}));
    plan.order.push_back(
            BuildPlanEntry{"ordinary-package", {"ordinary-package"}});
    return plan;
}

void test_ordinary_size_one() {
    BuildPlanArtifactTargetProjectionResult result =
            project_build_plan_required_artifact_targets(ordinary_plan());
    const auto& success = expect_success(result, "Ordinary projection");

    expect(success.build_units.size() == 1, "Ordinary build-unit count differs");
    const auto& unit = success.build_units.front();
    expect(unit.build_plan_order_index == 0, "Ordinary order index differs");
    expect(
            unit.package_base == "ordinary-package",
            "Ordinary build-unit PackageBase differs");
    expect(unit.required_targets.size() == 1, "Ordinary target count differs");
    expect_required_target(
            unit.required_targets.front(), "ordinary-package",
            "ordinary-package", DesiredInstallReason::Explicit,
            "Ordinary target");
}

void test_split_stable_order_and_desired_reasons() {
    BuildPlan plan;
    const RootTargetIdentity app_root = root(0, "shared-suite-app");
    plan.root_targets.push_back(app_root);

    // package_targetsのstorage順ではなく、entryのfirst-seen順をauthorityにする。
    plan.package_targets.push_back(package_target(
            "shared-suite-app", "shared-suite",
            {PackageRole::Root, PackageRole::BuildDependency}, {app_root}));
    plan.package_targets.push_back(package_target(
            "shared-suite-runtime", "shared-suite",
            {PackageRole::RuntimeDependency}, {app_root}));
    plan.order.push_back(BuildPlanEntry{
            "shared-suite", {"shared-suite-runtime", "shared-suite-app"}});

    BuildPlanArtifactTargetProjectionResult result =
            project_build_plan_required_artifact_targets(plan);
    const auto& success = expect_success(result, "Split projection");
    expect(success.build_units.size() == 1, "Split build-unit count differs");
    const auto& unit = success.build_units.front();
    expect(unit.required_targets.size() == 2, "Split target count differs");
    expect_required_target(
            unit.required_targets[0], "shared-suite", "shared-suite-runtime",
            DesiredInstallReason::Dependency, "First split target");
    expect_required_target(
            unit.required_targets[1], "shared-suite", "shared-suite-app",
            DesiredInstallReason::Explicit, "Second split target");
}

void test_unrelated_sibling_is_not_inferred() {
    BuildPlan plan = ordinary_plan();
    // Adapter authority外のdiagnostic collectionからartifactを推測しない。
    plan.split_package_targets.push_back(BuildPlanSplitPackageTarget{
            "ordinary-package", "ordinary-package-debug"});

    BuildPlanArtifactTargetProjectionResult result =
            project_build_plan_required_artifact_targets(plan);
    const auto& success = expect_success(result, "Unrelated sibling projection");
    expect(success.build_units.size() == 1, "Sibling build-unit count differs");
    expect(
            success.build_units.front().required_targets.size() == 1,
            "Unrelated sibling was added as a required target");
    expect_required_target(
            success.build_units.front().required_targets.front(),
            "ordinary-package", "ordinary-package",
            DesiredInstallReason::Explicit, "Required non-sibling target");
}

void test_missing_planned_target_is_typed_failure() {
    BuildPlan plan;
    plan.root_targets.push_back(root(0, "missing-child"));
    plan.order.push_back(BuildPlanEntry{"shared-suite", {"missing-child"}});

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Missing target projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    MissingPlannedPackageTarget,
            "Missing target issue is absent");
}

void test_duplicate_entry_name_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    plan.order.front().package_names.push_back("ordinary-package");

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Duplicate entry-name projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    DuplicateEntryPackageName,
            "Duplicate entry-name issue is absent");
}

void test_duplicate_planned_target_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    plan.package_targets.push_back(plan.package_targets.front());

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Duplicate planned-target projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    DuplicatePlannedPackageTarget,
            "Duplicate planned-target issue is absent");
}

void test_package_base_mismatch_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    plan.package_targets.front().package_base = "different-base";

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "PackageBase mismatch projection",
            BuildPlanArtifactTargetProjectionIssueKind::PackageBaseMismatch,
            "PackageBase mismatch issue is absent");
}

void test_uncovered_same_base_target_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    const RootTargetIdentity package_root = plan.root_targets.front();
    plan.package_targets.push_back(package_target(
            "ordinary-package-tools", "ordinary-package",
            {PackageRole::RuntimeDependency}, {package_root}));

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Uncovered target projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    UncoveredPlannedPackageTarget,
            "Uncovered target issue is absent");
}

void test_unavailable_desired_reason_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    plan.package_targets.front().roles.clear();

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Unavailable desired-reason projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    DesiredInstallReasonUnavailable,
            "Unavailable desired-reason issue is absent");

    plan = ordinary_plan();
    plan.package_targets.front().roles = {
            static_cast<PackageRole>(999)};
    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Unknown desired-reason role projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    DesiredInstallReasonUnavailable,
            "Unknown desired-reason role issue is absent");
}

void test_inconsistent_root_attribution_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    plan.package_targets.front().roots = {root(9, "ordinary-package")};

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Unknown root-attribution projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    RootAttributionInconsistent,
            "Root-attribution issue is absent");

    plan = ordinary_plan();
    plan.package_targets.front().roots.push_back(
            plan.package_targets.front().roots.front());
    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Duplicate root-attribution projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    RootAttributionInconsistent,
            "Duplicate root-attribution issue is absent");

    plan = ordinary_plan();
    plan.root_targets.front().invocation_index = 9;
    plan.package_targets.front().roots = plan.root_targets;
    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Out-of-order root identity projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    RootAttributionInconsistent,
            "Out-of-order root identity issue is absent");
}

void test_duplicate_package_base_entry_is_typed_failure() {
    BuildPlan plan = ordinary_plan();
    const RootTargetIdentity package_root = plan.root_targets.front();
    plan.package_targets.push_back(package_target(
            "ordinary-package-tools", "ordinary-package",
            {PackageRole::RuntimeDependency}, {package_root}));
    plan.order.push_back(BuildPlanEntry{
            "ordinary-package", {"ordinary-package-tools"}});

    expect_failure_issue(
            project_build_plan_required_artifact_targets(plan),
            "Duplicate PackageBase entry projection",
            BuildPlanArtifactTargetProjectionIssueKind::
                    DuplicatePackageBaseEntry,
            "Duplicate PackageBase entry issue is absent");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "PASS: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("ordinary size one", test_ordinary_size_one);
        run_case(
                "split stable order and desired reasons",
                test_split_stable_order_and_desired_reasons);
        run_case(
                "unrelated sibling is not inferred",
                test_unrelated_sibling_is_not_inferred);
        run_case(
                "missing planned target",
                test_missing_planned_target_is_typed_failure);
        run_case(
                "duplicate entry name",
                test_duplicate_entry_name_is_typed_failure);
        run_case(
                "duplicate planned target",
                test_duplicate_planned_target_is_typed_failure);
        run_case(
                "PackageBase mismatch",
                test_package_base_mismatch_is_typed_failure);
        run_case(
                "uncovered same-base target",
                test_uncovered_same_base_target_is_typed_failure);
        run_case(
                "unavailable desired reason",
                test_unavailable_desired_reason_is_typed_failure);
        run_case(
                "inconsistent root attribution",
                test_inconsistent_root_attribution_is_typed_failure);
        run_case(
                "duplicate PackageBase entry",
                test_duplicate_package_base_entry_is_typed_failure);
    } catch(const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
