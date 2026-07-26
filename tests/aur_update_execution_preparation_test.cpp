#include "app_config.hpp"
#include "aur_update_execution_preparation.hpp"
#include "stubs/aur-update-execution-preparation/preparation_stub.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(
        !std::is_default_constructible_v<
                PreparedAurUpdateSourceBuildInvocation>);
static_assert(
        !std::is_copy_constructible_v<
                PreparedAurUpdateSourceBuildInvocation>);
static_assert(
        std::is_nothrow_move_constructible_v<
                PreparedAurUpdateSourceBuildInvocation>);
static_assert(
        !std::is_move_assignable_v<
                PreparedAurUpdateSourceBuildInvocation>);
static_assert(!std::is_aggregate_v<PreparedAurUpdateSourceBuildInvocation>);
static_assert(
        !std::is_constructible_v<
                PreparedAurUpdateSourceBuildInvocation,
                PreparedProductionSourceBuildInvocation,
                std::vector<AurUpdatePreparedWorkItemAttribution>>);

namespace {

namespace fs = std::filesystem;
namespace stub = aur_update_execution_preparation_test_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

SourceBuildEnvironment environment(
        std::initializer_list<SourceEnvironmentAssignment> assignments) {
    return SourceBuildEnvironment{
            std::vector<SourceEnvironmentAssignment>{assignments}};
}

bool same_environment(
        const SourceBuildEnvironment& lhs,
        const SourceBuildEnvironment& rhs) {
    if(lhs.ordered_assignments.size() != rhs.ordered_assignments.size()) {
        return false;
    }
    for(std::size_t index = 0; index < lhs.ordered_assignments.size(); ++index) {
        if(lhs.ordered_assignments[index].key !=
                   rhs.ordered_assignments[index].key ||
           lhs.ordered_assignments[index].value !=
                   rhs.ordered_assignments[index].value) {
            return false;
        }
    }
    return true;
}

SourcePreferenceLoaded loaded_preference(
        const std::string& preference_name,
        SourceBuildEnvironment source_environment = {},
        std::vector<std::string> warnings = {}) {
    return SourcePreferenceLoaded{
            fs::path("/stub/preferences") / preference_name,
            std::move(source_environment),
            std::move(warnings)};
}

SourcePreferenceFailure preference_failure(
        const std::string& preference_name,
        SourcePreferenceFailureKind kind,
        std::optional<fs::file_type> observed_file_type = std::nullopt) {
    SourcePreferenceFailure failure{
            kind,
            fs::path("/stub/preferences") / preference_name,
            std::nullopt,
            observed_file_type,
            "strict preference failure for " + preference_name};
    if(kind != SourcePreferenceFailureKind::UnsupportedFileType) {
        failure.system_error =
                std::make_error_code(std::errc::permission_denied);
    }
    return failure;
}

AurUpdatePlanEntry update_entry(
        const std::string& package_name,
        DesiredInstallReason desired_reason,
        const std::string& package_base) {
    return AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            desired_reason == DesiredInstallReason::Explicit
                    ? InstalledPackageReason::Explicit
                    : InstalledPackageReason::Dependency,
            AurUpdateRemotePackage{
                    package_name,
                    package_base,
                    "2.0-1",
                    AurVersionRelation::NewerThanInstalled},
            AurUpdateClassification::UpdateAvailable};
}

AurUpdateExecutionTarget executable_target(
        std::size_t update_plan_index,
        std::size_t root_index,
        const std::string& package_name,
        DesiredInstallReason desired_reason,
        const std::string& package_base) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.build_plan_root_index = root_index;
    target.update = update_entry(
            package_name, desired_reason, package_base);
    target.status = AurUpdateExecutionTargetStatus::Executable;
    target.desired_install_reason = desired_reason;
    return target;
}

AurUpdateExecutionTarget skipped_target(
        std::size_t update_plan_index,
        const std::string& package_name) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.update = AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            InstalledPackageReason::Explicit,
            AurUpdateRemotePackage{
                    package_name,
                    package_name,
                    "1.0-1",
                    AurVersionRelation::SameAsInstalled},
            AurUpdateClassification::UpToDate};
    target.status = AurUpdateExecutionTargetStatus::Skipped;
    target.issues.push_back(AurUpdateExecutionIssue{
            AurUpdateExecutionReason::UpToDate,
            package_name,
            package_name,
            std::nullopt,
            "Already up to date."});
    return target;
}

AurUpdateExecutionIssue build_plan_inconsistent_issue(
        const std::string& package_name) {
    return AurUpdateExecutionIssue{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            package_name,
            package_name + "-base",
            "dependency>=2",
            "Skipped target retained a BuildPlan inconsistency."};
}

AurUpdateExecutionTarget blocking_target(
        std::size_t update_plan_index,
        const std::string& package_name,
        AurUpdateExecutionTargetStatus status,
        AurUpdateExecutionReason reason) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.update = update_entry(
            package_name, DesiredInstallReason::Explicit, package_name);
    target.status = status;
    target.issues.push_back(AurUpdateExecutionIssue{
            reason,
            package_name,
            package_name,
            std::nullopt,
            "Typed blocking preflight issue."});
    return target;
}

AurUpdateExecutionPreflight single_root_preflight(
        const std::string& package_name = "single-root",
        DesiredInstallReason desired_reason = DesiredInstallReason::Explicit,
        const std::string& package_base = {}) {
    const std::string resolved_package_base =
            package_base.empty() ? package_name : package_base;
    const RootTargetIdentity root{0, package_name};

    BuildPlan plan;
    plan.root_targets.push_back(root);
    plan.package_targets.push_back(PlannedPackageTarget{
            package_name,
            resolved_package_base,
            {PackageRole::Root},
            {root}});
    plan.order.push_back(
            BuildPlanEntry{resolved_package_base, {package_name}});

    AurUpdateExecutionPreflight preflight;
    preflight.targets.push_back(executable_target(
            0, 0, package_name, desired_reason, resolved_package_base));
    preflight.build_plan = std::move(plan);
    return preflight;
}

AurUpdateExecutionPreflight ordered_multi_root_preflight() {
    const RootTargetIdentity root_a{0, "root-a"};
    const RootTargetIdentity root_b{1, "root-b"};

    BuildPlan plan;
    plan.root_targets = {root_a, root_b};
    plan.package_targets = {
            PlannedPackageTarget{
                    "private-dependency",
                    "private-dependency",
                    {PackageRole::BuildDependency},
                    {root_a}},
            PlannedPackageTarget{
                    "shared-dependency",
                    "shared-dependency",
                    {PackageRole::RuntimeDependency},
                    {root_a, root_b}},
            // root-bはroot-aのdependencyでもある。Root roleからreasonを
            // 再計算するとDependency-installed rootがExplicitへ誤昇格するfixture。
            PlannedPackageTarget{
                    "root-b",
                    "root-b",
                    {PackageRole::Root, PackageRole::RuntimeDependency},
                    {root_a, root_b}},
            PlannedPackageTarget{
                    "root-a",
                    "root-a",
                    {PackageRole::Root},
                    {root_a}},
    };
    plan.order = {
            BuildPlanEntry{
                    "private-dependency", {"private-dependency"}},
            BuildPlanEntry{
                    "shared-dependency", {"shared-dependency"}},
            BuildPlanEntry{"root-b", {"root-b"}},
            BuildPlanEntry{"root-a", {"root-a"}},
    };

    AurUpdateExecutionPreflight preflight;
    preflight.targets = {
            executable_target(
                    0, 0, "root-a", DesiredInstallReason::Explicit,
                    "root-a"),
            skipped_target(1, "skipped-root"),
            executable_target(
                    2, 1, "root-b", DesiredInstallReason::Dependency,
                    "root-b"),
    };
    preflight.build_plan = std::move(plan);
    return preflight;
}

const AurUpdatePreparationIssue& require_issue(
        const AurUpdateSourceBuildPreparation& preparation,
        AurUpdatePreparationReason reason,
        std::string_view context) {
    auto found = std::find_if(
            preparation.issues.begin(), preparation.issues.end(),
            [reason](const AurUpdatePreparationIssue& issue) {
                return issue.reason == reason;
            });
    if(found == preparation.issues.end()) {
        throw std::runtime_error(
                std::string(context) + ": expected issue is missing");
    }
    return *found;
}

void expect_no_external_preparation_boundary(const std::string& context) {
    expect(
            stub::strict_preference_read_history().empty(),
            context + ": strict preference reader was called");
    expect(
            stub::supported_options_guard_history().empty(),
            context + ": generic option guard was called");
    expect(
            stub::pkgdest_guard_history().empty(),
            context + ": generic PKGDEST guard was called");
    expect(
            stub::database_call_count() == 0,
            context + ": Pacman DB resolver was called");
}

void expect_result_invariant(
        const AurUpdateSourceBuildPreparation& preparation,
        const std::string& context) {
    const int state_count =
            static_cast<int>(preparation.is_prepared()) +
            static_cast<int>(preparation.is_noop()) +
            static_cast<int>(preparation.is_blocked());
    expect(state_count == 1, context + ": result invariant is broken");
    expect(
            !preparation.invocation.has_value() ||
                    preparation.issues.empty(),
            context + ": partial invocation escaped with issues");
    if(!preparation.invocation.has_value()) return;

    const PreparedProductionSourceBuildInvocation& production_invocation =
            preparation.invocation->production_invocation_for_test();
    const auto& attributions =
            preparation.invocation->work_item_attributions();
    expect(
            !production_invocation.work_items.empty() &&
                    attributions.size() ==
                            production_invocation.work_items.size(),
            context + ": prepared correlation count differs");
    for(std::size_t index = 0; index < attributions.size(); ++index) {
        const auto& attribution = attributions[index];
        const auto& work_item = production_invocation.work_items[index];
        expect(
                attribution.invocation_work_item_index == index &&
                        attribution.package_name ==
                                work_item.request.package_name &&
                        attribution.package_base ==
                                work_item.request.checkout_name &&
                        !attribution.affected_update_plan_indices.empty() &&
                        !attribution.affected_roots.empty(),
                context + ": prepared correlation identity differs at " +
                        std::to_string(index));
    }
}

void expect_blocked_reason(
        const AurUpdateSourceBuildPreparation& preparation,
        AurUpdatePreparationReason reason,
        const std::string& context) {
    expect_result_invariant(preparation, context);
    expect(preparation.is_blocked(), context + ": result was not blocked");
    expect(!preparation.invocation.has_value(), context + ": partial invocation escaped");
    static_cast<void>(require_issue(preparation, reason, context));
}

void expect_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const std::string& package_name,
        const SourceBuildEnvironment& expected_environment,
        DesiredInstallReason desired_reason,
        bool needed,
        const std::string& context) {
    expect(
            work_item.request.package_name == package_name,
            context + ": package_name differs");
    expect(
            work_item.request.checkout_name == package_name,
            context + ": checkout_name differs");
    expect(
            work_item.request.git_url ==
                    "https://aur.archlinux.org/" + package_name + ".git",
            context + ": git_url differs");
    expect(
            same_environment(
                    work_item.request.custom_environment,
                    expected_environment),
            context + ": custom_environment differs");
    expect(
            work_item.request.empty_value_policy ==
                    SourceEnvironmentEmptyValuePolicy::Omit,
            context + ": empty value policy differs");
    expect(work_item.request.needed == needed, context + ": needed differs");
    expect(
            !work_item.request.only_if_updated,
            context + ": only_if_updated differs");
    expect(
            !work_item.request.installed_snapshot.has_value(),
            context + ": installed snapshot was synthesized");
    expect(
            !work_item.request.update_baseline.has_value(),
            context + ": update baseline was synthesized");
    expect(
            work_item.desired_reason == desired_reason,
            context + ": desired install reason differs");
    expect(
            work_item.is_build_plan_entry,
            context + ": BuildPlan marker differs");
    expect(
            !work_item.uses_system_update_baseline,
            context + ": system update baseline marker differs");
    expect(
            work_item.plan_package_names ==
                    std::vector<std::string>{package_name},
            context + ": plan_package_names differs");
}

void expect_attribution(
        const AurUpdatePreparedWorkItemAttribution& attribution,
        std::size_t work_item_index,
        const std::string& package_name,
        const std::string& package_base,
        const std::vector<std::size_t>& affected_update_plan_indices,
        const std::vector<RootTargetIdentity>& affected_roots,
        const std::string& context) {
    expect(
            attribution.invocation_work_item_index == work_item_index,
            context + ": work item index differs");
    expect(
            attribution.package_name == package_name,
            context + ": package name differs");
    expect(
            attribution.package_base == package_base,
            context + ": PackageBase differs");
    expect(
            attribution.affected_update_plan_indices ==
                    affected_update_plan_indices,
            context + ": affected update-plan indices differ");
    expect(
            attribution.affected_roots == affected_roots,
            context + ": affected roots differ");
}

void expect_event_kinds(
        const std::vector<stub::EventKind>& expected,
        const std::string& context) {
    const std::vector<stub::Event>& events = stub::event_history();
    expect(events.size() == expected.size(), context + ": event count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect(
                events[index].kind == expected[index],
                context + ": event order differs at " +
                        std::to_string(index));
    }
}

void test_noop_and_blocking_preflight_short_circuit() {
    const AppConfig config;

    stub::reset();
    AurUpdateSourceBuildPreparation empty =
            prepare_aur_update_source_build_invocation(
                    AurUpdateExecutionPreflight{}, false, config);
    expect_result_invariant(empty, "empty preflight");
    expect(empty.is_noop(), "Empty preflight was not a normal no-op");
    expect(empty.issues.empty(), "Empty preflight produced issues");
    expect_no_external_preparation_boundary("empty preflight");

    stub::reset();
    AurUpdateExecutionPreflight skip_only;
    skip_only.targets.push_back(skipped_target(0, "skip-only"));
    AurUpdateSourceBuildPreparation skipped =
            prepare_aur_update_source_build_invocation(
                    skip_only, false, config);
    expect_result_invariant(skipped, "skip-only preflight");
    expect(skipped.is_noop(), "Skip-only preflight was not a normal no-op");
    expect(skipped.issues.empty(), "Normal skip leaked into preparation issues");
    expect_no_external_preparation_boundary("skip-only preflight");

    stub::reset();
    AurUpdateExecutionPreflight non_aur_skip_only;
    AurUpdateExecutionTarget non_aur_skip =
            skipped_target(0, "non-aur-skip");
    non_aur_skip.update.classification =
            AurUpdateClassification::NonAurForeign;
    non_aur_skip.update.aur_package.reset();
    non_aur_skip.issues = {
            AurUpdateExecutionIssue{
                    AurUpdateExecutionReason::NonAurForeign,
                    "non-aur-skip",
                    std::nullopt,
                    std::nullopt,
                    "Package is not present in AUR."}};
    non_aur_skip_only.targets.push_back(std::move(non_aur_skip));
    AurUpdateSourceBuildPreparation non_aur_skipped =
            prepare_aur_update_source_build_invocation(
                    non_aur_skip_only, false, config);
    expect_result_invariant(non_aur_skipped, "Non-AUR skip-only preflight");
    expect(
            non_aur_skipped.is_noop(),
            "NonAurForeign skip-only preflight was not a normal no-op");
    expect(
            non_aur_skipped.issues.empty(),
            "NonAurForeign normal skip leaked into preparation issues");
    expect_no_external_preparation_boundary(
            "Non-AUR skip-only preflight");

    stub::reset();
    AurUpdateExecutionPreflight blocked = single_root_preflight();
    blocked.targets.push_back(blocking_target(
            1,
            "blocked-root",
            AurUpdateExecutionTargetStatus::Unsupported,
            AurUpdateExecutionReason::SplitPackageSelectionRequired));
    AurUpdateSourceBuildPreparation blocking =
            prepare_aur_update_source_build_invocation(
                    blocked, false, config);
    expect_blocked_reason(
            blocking,
            AurUpdatePreparationReason::BlockingPreflight,
            "blocking preflight");
    const AurUpdatePreparationIssue& typed = require_issue(
            blocking,
            AurUpdatePreparationReason::BlockingPreflight,
            "blocking preflight typed issue");
    expect(
            typed.preflight_issue.has_value() &&
                    typed.preflight_issue->reason ==
                            AurUpdateExecutionReason::
                                    SplitPackageSelectionRequired,
            "Blocking preflight issue lost its typed source");
    expect(
            typed.affected_update_plan_indices ==
                    std::vector<std::size_t>{1},
            "Blocking preflight issue attribution differs");
    expect_no_external_preparation_boundary("blocking preflight");
}

void test_skipped_preflight_inconsistencies_fail_closed() {
    const AppConfig config;

    stub::reset();
    AurUpdateExecutionPreflight skip_only;
    AurUpdateExecutionTarget invalid_skip =
            skipped_target(0, "invalid-skip-only");
    const AurUpdateExecutionIssue skip_only_issue =
            build_plan_inconsistent_issue("invalid-skip-only");
    invalid_skip.issues = {skip_only_issue};
    skip_only.targets.push_back(std::move(invalid_skip));
    AurUpdateSourceBuildPreparation skip_only_result =
            prepare_aur_update_source_build_invocation(
                    skip_only, false, config);
    expect_blocked_reason(
            skip_only_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "skip-only target with blocking issue");
    const AurUpdatePreparationIssue& retained_skip_only_issue =
            require_issue(
                    skip_only_result,
                    AurUpdatePreparationReason::PreflightInconsistent,
                    "skip-only typed preflight issue");
    expect(
            retained_skip_only_issue.preflight_issue.has_value() &&
                    retained_skip_only_issue.preflight_issue->reason ==
                            skip_only_issue.reason &&
                    retained_skip_only_issue.preflight_issue->package_name ==
                            skip_only_issue.package_name &&
                    retained_skip_only_issue.preflight_issue->package_base ==
                            skip_only_issue.package_base &&
                    retained_skip_only_issue.preflight_issue
                                    ->dependency_specification ==
                            skip_only_issue.dependency_specification &&
                    retained_skip_only_issue.preflight_issue->diagnostic ==
                            skip_only_issue.diagnostic,
            "Skip-only blocker lost its typed preflight issue");
    expect(
            retained_skip_only_issue.affected_update_plan_indices ==
                    std::vector<std::size_t>{0},
            "Skip-only blocker attribution differs");
    expect_no_external_preparation_boundary(
            "skip-only target with blocking issue");

    stub::reset();
    AurUpdateExecutionPreflight executable_and_invalid_skip =
            ordered_multi_root_preflight();
    const AurUpdateExecutionIssue mixed_issue =
            build_plan_inconsistent_issue("skipped-root");
    executable_and_invalid_skip.targets[1].issues.push_back(mixed_issue);
    AurUpdateSourceBuildPreparation mixed_result =
            prepare_aur_update_source_build_invocation(
                    executable_and_invalid_skip, false, config);
    expect_blocked_reason(
            mixed_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "executable and invalid skipped target");
    const AurUpdatePreparationIssue& retained_mixed_issue = require_issue(
            mixed_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "mixed typed skipped issue");
    expect(
            retained_mixed_issue.preflight_issue.has_value() &&
                    retained_mixed_issue.preflight_issue->reason ==
                            mixed_issue.reason &&
                    retained_mixed_issue.preflight_issue->package_name ==
                            mixed_issue.package_name &&
                    retained_mixed_issue.preflight_issue->package_base ==
                            mixed_issue.package_base &&
                    retained_mixed_issue.preflight_issue
                                    ->dependency_specification ==
                            mixed_issue.dependency_specification &&
                    retained_mixed_issue.preflight_issue->diagnostic ==
                            mixed_issue.diagnostic &&
                    retained_mixed_issue.affected_update_plan_indices ==
                            std::vector<std::size_t>{1},
            "Mixed skipped blocker was not retained exactly");
    expect_no_external_preparation_boundary(
            "executable and invalid skipped target");

    stub::reset();
    AurUpdateExecutionPreflight reasonless_skip;
    AurUpdateExecutionTarget reasonless_target =
            skipped_target(0, "reasonless-skip");
    reasonless_target.issues.clear();
    reasonless_skip.targets.push_back(std::move(reasonless_target));
    AurUpdateSourceBuildPreparation reasonless_result =
            prepare_aur_update_source_build_invocation(
                    reasonless_skip, false, config);
    expect_blocked_reason(
            reasonless_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "Skipped target without normal skip issue");
    const AurUpdatePreparationIssue& reasonless_issue = require_issue(
            reasonless_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "reasonless skipped issue");
    expect(
            !reasonless_issue.preflight_issue.has_value() &&
                    reasonless_issue.affected_update_plan_indices ==
                            std::vector<std::size_t>{0},
            "Reasonless skipped target inconsistency differs");
    expect_no_external_preparation_boundary(
            "Skipped target without normal skip issue");
}

void test_executable_structure_failures() {
    const AppConfig config;

    stub::reset();
    AurUpdateExecutionPreflight missing_plan = single_root_preflight();
    missing_plan.build_plan.reset();
    AurUpdateSourceBuildPreparation missing_plan_result =
            prepare_aur_update_source_build_invocation(
                    missing_plan, false, config);
    expect_blocked_reason(
            missing_plan_result,
            AurUpdatePreparationReason::BuildPlanMissing,
            "missing BuildPlan");
    expect_no_external_preparation_boundary("missing BuildPlan");

    stub::reset();
    AurUpdateExecutionPreflight empty_order = single_root_preflight();
    empty_order.build_plan->order.clear();
    AurUpdateSourceBuildPreparation empty_order_result =
            prepare_aur_update_source_build_invocation(
                    empty_order, false, config);
    expect_blocked_reason(
            empty_order_result,
            AurUpdatePreparationReason::BuildPlanOrderEmpty,
            "empty BuildPlan order");
    expect_no_external_preparation_boundary("empty BuildPlan order");

    stub::reset();
    AurUpdateExecutionPreflight missing_reason = single_root_preflight();
    missing_reason.targets.front().desired_install_reason.reset();
    AurUpdateSourceBuildPreparation missing_reason_result =
            prepare_aur_update_source_build_invocation(
                    missing_reason, false, config);
    expect_blocked_reason(
            missing_reason_result,
            AurUpdatePreparationReason::DesiredInstallReasonMissing,
            "missing desired install reason");
    const RootTargetIdentity expected_missing_reason_root{0, "single-root"};
    const AurUpdatePreparationIssue& missing_reason_issue = require_issue(
            missing_reason_result,
            AurUpdatePreparationReason::DesiredInstallReasonMissing,
            "missing desired install reason attribution");
    expect(
            missing_reason_issue.affected_update_plan_indices ==
                            std::vector<std::size_t>{0} &&
                    missing_reason_issue.affected_roots ==
                            std::vector<RootTargetIdentity>{
                                    expected_missing_reason_root} &&
                    missing_reason_result.affected_roots ==
                            std::vector<RootTargetIdentity>{
                                    expected_missing_reason_root},
            "Missing desired install reason lost its valid root attribution");
    expect_no_external_preparation_boundary("missing desired install reason");

    stub::reset();
    AurUpdateExecutionPreflight executable_with_issue =
            single_root_preflight("executable-with-issue");
    const AurUpdateExecutionIssue retained_preflight_issue{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            std::optional<std::string>{"executable-with-issue"},
            std::optional<std::string>{"executable-with-issue"},
            std::optional<std::string>{"dependency>=1"},
            "Executable target retained a typed blocker."};
    executable_with_issue.targets.front().issues.push_back(
            retained_preflight_issue);
    AurUpdateSourceBuildPreparation executable_issue_result =
            prepare_aur_update_source_build_invocation(
                    executable_with_issue, false, config);
    expect_blocked_reason(
            executable_issue_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "executable target with preflight issue");
    const AurUpdatePreparationIssue& executable_issue = require_issue(
            executable_issue_result,
            AurUpdatePreparationReason::PreflightInconsistent,
            "executable target typed preflight issue");
    expect(
            executable_issue.preflight_issue.has_value() &&
                    executable_issue.preflight_issue->reason ==
                            retained_preflight_issue.reason &&
                    executable_issue.preflight_issue->package_name ==
                            retained_preflight_issue.package_name &&
                    executable_issue.preflight_issue->package_base ==
                            retained_preflight_issue.package_base &&
                    executable_issue.preflight_issue
                                    ->dependency_specification ==
                            retained_preflight_issue
                                    .dependency_specification &&
                    executable_issue.preflight_issue->diagnostic ==
                            retained_preflight_issue.diagnostic,
            "Executable target preflight issue was not retained exactly");
    expect(
            executable_issue.affected_update_plan_indices ==
                    std::vector<std::size_t>{0},
            "Executable target preflight issue attribution differs");
    expect_no_external_preparation_boundary(
            "executable target with preflight issue");

    stub::reset();
    AurUpdateExecutionPreflight wrong_root = single_root_preflight();
    wrong_root.build_plan->root_targets.front().requested_name =
            "different-root";
    AurUpdateSourceBuildPreparation wrong_root_result =
            prepare_aur_update_source_build_invocation(
                    wrong_root, false, config);
    expect_blocked_reason(
            wrong_root_result,
            AurUpdatePreparationReason::RootAttributionInconsistent,
            "root identity mismatch");
    expect_no_external_preparation_boundary("root identity mismatch");

    stub::reset();
    AurUpdateExecutionPreflight missing_target = single_root_preflight();
    missing_target.build_plan->package_targets.clear();
    AurUpdateSourceBuildPreparation missing_target_result =
            prepare_aur_update_source_build_invocation(
                    missing_target, false, config);
    expect_blocked_reason(
            missing_target_result,
            AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
            "missing root package target");
    expect_no_external_preparation_boundary("missing root package target");

    stub::reset();
    AurUpdateExecutionPreflight wrong_update_index = single_root_preflight();
    wrong_update_index.targets.front().update_plan_index = 7;
    AurUpdateSourceBuildPreparation wrong_update_index_result =
            prepare_aur_update_source_build_invocation(
                    wrong_update_index, false, config);
    expect_blocked_reason(
            wrong_update_index_result,
            AurUpdatePreparationReason::RootAttributionInconsistent,
            "update-plan index/position mismatch");
    expect_no_external_preparation_boundary(
            "update-plan index/position mismatch");
}

void test_single_root_exact_work_item_and_snapshot() {
    stub::reset();
    const SourceBuildEnvironment expected_environment =
            environment({{"MAKEFLAGS", "-j4"}, {"EMPTY", ""}});
    stub::enqueue_source_preference_result(
            "single-root",
            loaded_preference(
                    "single-root",
                    expected_environment,
                    {"first warning", "second warning"}));
    stub::set_database_paths(
            PacmanDatabasePaths{"/snapshot/root", "/snapshot/database"});

    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight(), true, config);

    expect_result_invariant(preparation, "single-root prepared result");
    expect(preparation.is_prepared(), "Single root was not prepared");
    const PreparedProductionSourceBuildInvocation& production_invocation =
            preparation.invocation->production_invocation_for_test();
    expect(production_invocation.work_items.size() == 1, "Single root work count differs");
    expect_work_item(
            production_invocation.work_items.front(),
            "single-root",
            expected_environment,
            DesiredInstallReason::Explicit,
            true,
            "single-root exact work item");
    const auto& attributions =
            preparation.invocation->work_item_attributions();
    expect(attributions.size() == 1, "Single root attribution count differs");
    expect_attribution(
            attributions.front(), 0, "single-root", "single-root", {0},
            {{0, "single-root"}}, "single-root exact attribution");
    expect(
            production_invocation.database_paths.root_dir ==
                            fs::path("/snapshot/root") &&
                    production_invocation.database_paths.db_path ==
                            fs::path("/snapshot/database"),
            "Pacman DB snapshot differs");
    expect(stub::database_call_count() == 1, "Prepared invocation resolved DB more than once");
    expect(
            preparation.warnings.size() == 2 &&
                    preparation.warnings[0].diagnostic == "first warning" &&
                    preparation.warnings[1].diagnostic == "second warning",
            "Strict preference warning order differs");
    for(const auto& warning : preparation.warnings) {
        expect(
                warning.preference_name == "single-root" &&
                        warning.entry_path ==
                                fs::path("/stub/preferences/single-root") &&
                        warning.affected_update_plan_indices ==
                                std::vector<std::size_t>{0} &&
                        warning.affected_roots ==
                                std::vector<RootTargetIdentity>{{0, "single-root"}},
                "Strict preference warning attribution differs");
    }

    expect_event_kinds(
            {
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::SeparatedInstallOptionsGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::PacmanDatabaseResolution,
            },
            "single-root preparation order");
    expect(
            stub::pkgdest_guard_history().size() == 2 &&
                    stub::pkgdest_guard_history()[0].ordered_assignments.empty() &&
                    same_environment(
                            stub::pkgdest_guard_history()[1],
                            expected_environment),
            "Generic PKGDEST guard inputs differ");
}

void test_build_plan_order_skip_exclusion_and_install_reasons() {
    stub::reset();
    const SourceBuildEnvironment shared_environment =
            environment({{"CFLAGS", "-O2"}});
    stub::enqueue_source_preference_result(
            "private-dependency", SourcePreferenceAbsent{});
    stub::enqueue_source_preference_result(
            "shared-dependency",
            loaded_preference(
                    "shared-dependency",
                    shared_environment,
                    {"shared warning"}));
    stub::enqueue_source_preference_result(
            "root-b", SourcePreferenceAbsent{});
    stub::enqueue_source_preference_result(
            "root-a",
            loaded_preference(
                    "root-a", environment({{"MAKEFLAGS", "-j2"}})));
    stub::set_database_paths(
            PacmanDatabasePaths{"/multi/root", "/multi/database"});

    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    ordered_multi_root_preflight(), false, config);

    expect_result_invariant(preparation, "ordered multi-root result");
    expect(preparation.is_prepared(), "Ordered multi-root invocation was blocked");
    const PreparedProductionSourceBuildInvocation& production_invocation =
            preparation.invocation->production_invocation_for_test();
    const auto& work_items = production_invocation.work_items;
    expect(work_items.size() == 4, "Skipped target changed work item count");
    expect_work_item(
            work_items[0],
            "private-dependency",
            SourceBuildEnvironment{},
            DesiredInstallReason::Dependency,
            false,
            "private non-root dependency");
    expect_work_item(
            work_items[1],
            "shared-dependency",
            shared_environment,
            DesiredInstallReason::Dependency,
            false,
            "shared dependency");
    expect_work_item(
            work_items[2],
            "root-b",
            SourceBuildEnvironment{},
            DesiredInstallReason::Dependency,
            false,
            "dependency-installed root overlap");
    expect_work_item(
            work_items[3],
            "root-a",
            environment({{"MAKEFLAGS", "-j2"}}),
            DesiredInstallReason::Explicit,
            false,
            "explicit root");

    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{
                            "private-dependency", "shared-dependency",
                            "root-b", "root-a"},
            "Strict reads did not preserve BuildPlan::order");
    expect(
            std::none_of(
                    work_items.begin(), work_items.end(),
                    [](const ProductionSourceBuildWorkItem& work_item) {
                        return work_item.request.package_name ==
                                "skipped-root";
                    }),
            "Skipped target was projected into a work item");
    expect(
            preparation.affected_update_targets.size() == 2 &&
                    preparation.affected_update_targets[0].update_plan_index == 0 &&
                    preparation.affected_update_targets[1].update_plan_index == 2,
            "Executable target attribution lost original update-plan indices");
    expect(
            preparation.affected_roots ==
                    std::vector<RootTargetIdentity>{
                            {0, "root-a"}, {1, "root-b"}},
            "Prepared root snapshot differs");
    expect(
            preparation.warnings.size() == 1 &&
                    preparation.warnings.front().affected_update_plan_indices ==
                            std::vector<std::size_t>{0, 2} &&
                    preparation.warnings.front().affected_roots ==
                            std::vector<RootTargetIdentity>{
                                    {0, "root-a"}, {1, "root-b"}},
            "Shared dependency warning attribution differs");
    const auto& attributions =
            preparation.invocation->work_item_attributions();
    expect(attributions.size() == 4, "Multi-root attribution count differs");
    expect_attribution(
            attributions[0], 0, "private-dependency", "private-dependency",
            {0}, {{0, "root-a"}}, "private dependency attribution");
    expect_attribution(
            attributions[1], 1, "shared-dependency", "shared-dependency",
            {0, 2}, {{0, "root-a"}, {1, "root-b"}},
            "shared dependency attribution");
    expect_attribution(
            attributions[2], 2, "root-b", "root-b", {0, 2},
            {{0, "root-a"}, {1, "root-b"}}, "root-b attribution");
    expect_attribution(
            attributions[3], 3, "root-a", "root-a", {0},
            {{0, "root-a"}}, "root-a attribution");
    expect(stub::database_call_count() == 1, "Multi-root invocation resolved DB per work item");
    expect(
            production_invocation.database_paths.root_dir ==
                            fs::path("/multi/root") &&
                    production_invocation.database_paths.db_path ==
                            fs::path("/multi/database"),
            "Multi-root invocation did not retain one owned DB snapshot");
    expect_event_kinds(
            {
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::SeparatedInstallOptionsGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::PacmanDatabaseResolution,
            },
            "multi-root read/validation/DB order");
}

void expect_global_package_root_attribution_blocker(
        const AurUpdateSourceBuildPreparation& preparation,
        const std::string& package_name,
        const std::string& context) {
    expect_blocked_reason(
            preparation,
            AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
            context);
    expect(
            preparation.issues.size() == 1,
            context + ": unexpected additional issues were produced");
    const AurUpdatePreparationIssue& issue = preparation.issues.front();
    expect(
            issue.package_name == package_name &&
                    issue.package_base == package_name,
            context + ": package identity differs");
    expect(
            issue.affected_update_plan_indices ==
                    std::vector<std::size_t>{0, 2},
            context + ": partial update target attribution escaped");
    expect(
            issue.affected_roots ==
                    std::vector<RootTargetIdentity>{
                            {0, "root-a"}, {1, "root-b"}},
            context + ": affected roots are not the exact global snapshot");
    expect(
            preparation.affected_roots ==
                    std::vector<RootTargetIdentity>{
                            {0, "root-a"}, {1, "root-b"}},
            context + ": preparation root snapshot is not exact");
    expect(
            issue.diagnostic.find("cannot be attributed exactly") !=
                    std::string::npos,
            context + ": diagnostic does not identify attribution failure");
    expect(
            preparation.affected_update_targets.size() == 2 &&
                    preparation.affected_update_targets[0]
                                    .update_plan_index == 0 &&
                    preparation.affected_update_targets[1]
                                    .update_plan_index == 2,
            context + ": global executable target snapshot differs");
    expect_no_external_preparation_boundary(context);
}

void test_unknown_package_root_attribution_is_global() {
    const AppConfig config;
    const RootTargetIdentity unknown_root{9, "unknown-root"};

    stub::reset();
    AurUpdateExecutionPreflight shared_dependency =
            ordered_multi_root_preflight();
    shared_dependency.build_plan->package_targets[1].roots.push_back(
            unknown_root);
    AurUpdateSourceBuildPreparation shared_dependency_result =
            prepare_aur_update_source_build_invocation(
                    shared_dependency, false, config);
    expect_global_package_root_attribution_blocker(
            shared_dependency_result,
            "shared-dependency",
            "shared dependency with unknown root");
    expect(
            std::find(
                    shared_dependency_result.issues.front()
                            .affected_roots.begin(),
                    shared_dependency_result.issues.front()
                            .affected_roots.end(),
                    unknown_root) ==
                    shared_dependency_result.issues.front()
                            .affected_roots.end(),
            "Unknown raw root escaped as an exact affected root");

    stub::reset();
    AurUpdateExecutionPreflight missing_order_entry =
            ordered_multi_root_preflight();
    missing_order_entry.build_plan->package_targets.push_back(
            PlannedPackageTarget{
                    "unordered-dependency",
                    "unordered-dependency",
                    {PackageRole::BuildDependency},
                    {{0, "root-a"}, unknown_root}});
    AurUpdateSourceBuildPreparation missing_order_result =
            prepare_aur_update_source_build_invocation(
                    missing_order_entry, false, config);
    expect_global_package_root_attribution_blocker(
            missing_order_result,
            "unordered-dependency",
            "unordered dependency with unknown root");
    expect(
            missing_order_result.issues.front().diagnostic.find(
                    "must occur exactly once in BuildPlan execution order") !=
                    std::string::npos,
            "Order-count attribution failure lost its primary diagnostic");
}

void test_strict_absent_empty_valid_and_warning_results() {
    const AppConfig config;

    stub::reset();
    AurUpdateSourceBuildPreparation absent =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("absent-root"), false, config);
    expect(absent.is_prepared(), "Absent preference did not select an empty environment");
    expect(
            absent.invocation->production_invocation_for_test()
                    .work_items.front()
                    .request.custom_environment.ordered_assignments.empty(),
            "Absent preference synthesized assignments");

    stub::reset();
    stub::enqueue_source_preference_result(
            "empty-root", loaded_preference("empty-root"));
    AurUpdateSourceBuildPreparation empty =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("empty-root"), false, config);
    expect(empty.is_prepared(), "Loaded empty preference was treated as absent/failure");
    expect(empty.warnings.empty(), "Loaded empty preference synthesized warnings");

    stub::reset();
    const SourceBuildEnvironment valid_environment =
            environment({{"CC", "clang"}});
    stub::enqueue_source_preference_result(
            "valid-root",
            loaded_preference("valid-root", valid_environment));
    AurUpdateSourceBuildPreparation valid =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("valid-root"), false, config);
    expect(valid.is_prepared(), "Valid strict preference was rejected");
    expect(
            same_environment(
                    valid.invocation->production_invocation_for_test()
                            .work_items.front()
                            .request.custom_environment,
                    valid_environment),
            "Valid strict preference environment differs");

    stub::reset();
    stub::enqueue_source_preference_result(
            "malformed-only",
            loaded_preference(
                    "malformed-only", {},
                    {"line 1 is malformed", "line 2 is malformed"}));
    AurUpdateSourceBuildPreparation malformed_only =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("malformed-only"), false, config);
    expect(malformed_only.is_prepared(), "Malformed-only loaded preference was rejected");
    expect(
            malformed_only.warnings.size() == 2 &&
                    malformed_only.warnings[0].diagnostic ==
                            "line 1 is malformed" &&
                    malformed_only.warnings[1].diagnostic ==
                            "line 2 is malformed",
            "Malformed-only warning order differs");
}

void test_strict_typed_failures_are_not_flattened() {
    struct FailureCase {
        std::string                       name;
        SourcePreferenceFailureKind       kind;
        std::optional<fs::file_type>      observed_file_type;
    };
    const std::vector<FailureCase> cases = {
            {"status-failure", SourcePreferenceFailureKind::StatusUnavailable, std::nullopt},
            {"open-failure", SourcePreferenceFailureKind::OpenFailed, std::nullopt},
            {"read-failure", SourcePreferenceFailureKind::ReadFailed, std::nullopt},
            {"symlink-failure", SourcePreferenceFailureKind::UnsupportedFileType, fs::file_type::symlink},
            {"directory-failure", SourcePreferenceFailureKind::UnsupportedFileType, fs::file_type::directory},
            {"fifo-failure", SourcePreferenceFailureKind::UnsupportedFileType, fs::file_type::fifo},
    };
    const AppConfig config;

    for(const auto& test_case : cases) {
        stub::reset();
        const SourcePreferenceFailure scripted = preference_failure(
                test_case.name,
                test_case.kind,
                test_case.observed_file_type);
        stub::enqueue_source_preference_result(
                test_case.name, scripted);

        AurUpdateSourceBuildPreparation preparation =
                prepare_aur_update_source_build_invocation(
                        single_root_preflight(test_case.name), false, config);
        expect_blocked_reason(
                preparation,
                AurUpdatePreparationReason::SourcePreferenceUnavailable,
                test_case.name);
        const AurUpdatePreparationIssue& issue = require_issue(
                preparation,
                AurUpdatePreparationReason::SourcePreferenceUnavailable,
                test_case.name);
        expect(
                issue.source_preference_failure.has_value(),
                test_case.name + ": typed failure is missing");
        expect(
                issue.source_preference_failure->kind == test_case.kind &&
                        issue.source_preference_failure->entry_path ==
                                scripted.entry_path &&
                        issue.source_preference_failure->system_error ==
                                scripted.system_error &&
                        issue.source_preference_failure->observed_file_type ==
                                scripted.observed_file_type,
                test_case.name + ": typed failure fields differ");
        expect(
                stub::strict_preference_read_history() ==
                        std::vector<std::string>{test_case.name},
                test_case.name + ": strict read count differs");
        expect(stub::database_call_count() == 0, test_case.name + ": failure reached DB resolution");
        expect(!preparation.invocation.has_value(), test_case.name + ": partial invocation escaped");
    }
}

void test_package_base_fallback_and_failure_order() {
    const AppConfig config;

    stub::reset();
    stub::enqueue_source_preference_result(
            "split-cli",
            loaded_preference(
                    "split-cli", {}, {"requested warning"}));
    stub::enqueue_source_preference_result(
            "split-suite",
            loaded_preference(
                    "split-suite",
                    environment({{"MAKEFLAGS", "-j8"}}),
                    {"base warning"}));
    AurUpdateSourceBuildPreparation fallback =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight(
                            "split-cli",
                            DesiredInstallReason::Explicit,
                            "split-suite"),
                    false,
                    config);
    // Production契約ではrequested package != PackageBaseをstatic guardが拒否する。
    // その直前まで、strict fallbackのread順とwarning ownershipは保持される。
    expect_blocked_reason(
            fallback,
            AurUpdatePreparationReason::StaticWorkItemInvalid,
            "package-to-Base fallback");
    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{"split-cli", "split-suite"},
            "PackageBase fallback read order differs");
    expect(
            fallback.warnings.size() == 2 &&
                    fallback.warnings[0].preference_name == "split-cli" &&
                    fallback.warnings[1].preference_name == "split-suite",
            "PackageBase fallback warning order differs");
    expect(stub::database_call_count() == 0, "Static split guard reached DB resolution");
    expect(
            stub::supported_options_guard_history().empty(),
            "Update static validation was deferred into generic preparation");

    stub::reset();
    const SourcePreferenceFailure package_failure = preference_failure(
            "split-cli", SourcePreferenceFailureKind::OpenFailed);
    stub::enqueue_source_preference_result(
            "split-cli", package_failure);
    stub::enqueue_source_preference_result(
            "split-suite",
            loaded_preference(
                    "split-suite", environment({{"CC", "clang"}})));
    AurUpdateSourceBuildPreparation package_failed =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight(
                            "split-cli",
                            DesiredInstallReason::Explicit,
                            "split-suite"),
                    false,
                    config);
    expect_blocked_reason(
            package_failed,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            "requested package preference failure");
    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{"split-cli"},
            "Package failure incorrectly read PackageBase fallback");
    expect(stub::database_call_count() == 0, "Package preference failure reached DB resolution");

    stub::reset();
    stub::enqueue_source_preference_result(
            "split-cli", loaded_preference("split-cli"));
    const SourcePreferenceFailure base_failure = preference_failure(
            "split-suite", SourcePreferenceFailureKind::ReadFailed);
    stub::enqueue_source_preference_result(
            "split-suite", base_failure);
    AurUpdateSourceBuildPreparation base_failed =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight(
                            "split-cli",
                            DesiredInstallReason::Explicit,
                            "split-suite"),
                    false,
                    config);
    expect_blocked_reason(
            base_failed,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            "PackageBase preference failure");
    const AurUpdatePreparationIssue& base_issue = require_issue(
            base_failed,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            "PackageBase typed failure");
    expect(
            base_issue.package_name ==
                    std::optional<std::string>{"split-suite"} &&
                    base_issue.source_preference_failure.has_value() &&
                    base_issue.source_preference_failure->kind ==
                            SourcePreferenceFailureKind::ReadFailed,
            "PackageBase failure attribution differs");
    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{"split-cli", "split-suite"},
            "PackageBase failure read order differs");
    expect(stub::database_call_count() == 0, "PackageBase preference failure reached DB resolution");
}

void test_pkgdest_conflicts_stop_before_database() {
    const AppConfig config;
    for(const std::string& value : {std::string{}, std::string{"/claimed"}}) {
        stub::reset();
        stub::enqueue_source_preference_result(
                "pkgdest-root",
                loaded_preference(
                        "pkgdest-root",
                        environment({{"PKGDEST", value}})));
        AurUpdateSourceBuildPreparation preparation =
                prepare_aur_update_source_build_invocation(
                        single_root_preflight("pkgdest-root"), false, config);
        expect_blocked_reason(
                preparation,
                AurUpdatePreparationReason::SourcePreferencePkgdestConflict,
                value.empty() ? "empty PKGDEST" : "nonempty PKGDEST");
        expect(stub::database_call_count() == 0, "PKGDEST conflict reached DB resolution");
        expect(
                stub::supported_options_guard_history().empty() &&
                        stub::pkgdest_guard_history().empty(),
                "PKGDEST conflict escaped update-owned validation");
    }
}

void test_database_failure_is_typed_and_has_no_partial_invocation() {
    stub::reset();
    const PackageMetadataFailure failure{
            PackageMetadataErrorCode::ConfigurationMalformed,
            "typed database configuration failure"};
    stub::set_database_failure(failure);

    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("database-root"), false, config);

    expect_blocked_reason(
            preparation,
            AurUpdatePreparationReason::PacmanDatabaseUnavailable,
            "typed Pacman DB failure");
    const AurUpdatePreparationIssue& issue = require_issue(
            preparation,
            AurUpdatePreparationReason::PacmanDatabaseUnavailable,
            "typed Pacman DB failure");
    expect(
            issue.package_metadata_failure.has_value() &&
                    issue.package_metadata_failure->code == failure.code &&
                    issue.package_metadata_failure->diagnostic ==
                            failure.diagnostic,
            "PackageMetadataFailure fields were flattened");
    expect(stub::database_call_count() == 1, "Failed DB resolution was retried");
    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{"database-root"},
            "DB resolution ran before all strict reads");
    expect_event_kinds(
            {
                    stub::EventKind::StrictPreferenceRead,
                    stub::EventKind::SeparatedInstallOptionsGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::ArtifactPkgdestGuard,
                    stub::EventKind::PacmanDatabaseResolution,
            },
            "typed DB failure order");
}

void test_unexpected_generic_failure_is_global_and_stops_before_database() {
    stub::reset();
    stub::fail_supported_options_guard("scripted generic guard failure");

    const AppConfig config;
    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    ordered_multi_root_preflight(), false, config);

    expect_blocked_reason(
            preparation,
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            "unexpected generic preparation failure");
    const AurUpdatePreparationIssue& issue = require_issue(
            preparation,
            AurUpdatePreparationReason::GenericPreparationInconsistent,
            "unexpected generic preparation failure");
    expect(
            issue.affected_update_plan_indices ==
                    std::vector<std::size_t>{0, 2} &&
                    issue.affected_roots ==
                            std::vector<RootTargetIdentity>{
                                    {0, "root-a"}, {1, "root-b"}},
            "Unexpected generic failure was not invocation-global");
    expect(
            stub::strict_preference_read_history() ==
                    std::vector<std::string>{
                            "private-dependency", "shared-dependency",
                            "root-b", "root-a"},
            "Generic guard ran before strict preference completion");
    expect(stub::supported_options_guard_history().size() == 1, "Generic option guard call count differs");
    expect(stub::pkgdest_guard_history().empty(), "Generic failure continued into PKGDEST guards");
    expect(stub::database_call_count() == 0, "Unexpected generic failure reached DB resolution");
}

void test_result_state_helpers_reject_forbidden_combination() {
    AurUpdateSourceBuildPreparation noop;
    expect_result_invariant(noop, "direct no-op model");
    expect(noop.is_noop(), "Default result is not no-op");

    AurUpdateSourceBuildPreparation blocked;
    AurUpdatePreparationIssue blocked_issue;
    blocked_issue.reason =
            AurUpdatePreparationReason::PreflightInconsistent;
    blocked_issue.diagnostic = "blocked";
    blocked.issues.push_back(std::move(blocked_issue));
    expect_result_invariant(blocked, "direct blocked model");
    expect(blocked.is_blocked(), "Issue-only result is not blocked");

    stub::reset();
    const AppConfig config;
    AurUpdateSourceBuildPreparation prepared =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("prepared-root"), false, config);
    expect_result_invariant(prepared, "factory prepared model");
    expect(prepared.is_prepared(), "Factory result is not prepared");

    AurUpdatePreparationIssue forbidden_issue;
    forbidden_issue.reason =
            AurUpdatePreparationReason::GenericPreparationInconsistent;
    forbidden_issue.diagnostic = "forbidden";
    prepared.issues.push_back(std::move(forbidden_issue));
    expect(
            !prepared.is_prepared() && !prepared.is_noop() &&
                    !prepared.is_blocked(),
            "Forbidden invocation-plus-issues state was classified as valid");
}

void test_move_preserves_correlation_and_invalidates_source() {
    stub::reset();
    const AppConfig config;
    AurUpdateSourceBuildPreparation source =
            prepare_aur_update_source_build_invocation(
                    single_root_preflight("move-root"), false, config);
    expect(source.is_prepared(), "Move source was not prepared");

    AurUpdateSourceBuildPreparation destination = std::move(source);
    expect(
            destination.is_prepared(),
            "Move destination lost prepared correlation");
    expect(
            destination.invocation->production_invocation_for_test()
                            .work_items.front()
                            .request.package_name == "move-root" &&
                    destination.invocation->work_item_attributions()
                                    .front()
                                    .package_name == "move-root",
            "Move destination changed work-item attribution correlation");
    expect(
            !source.is_prepared(),
            "Moved-from preparation still reports prepared");
    expect(
            source.invocation.has_value() &&
                    !source.invocation->is_valid(),
            "Moved-from preparation did not retain an invalid capability");
}

template<typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "no-op and blocking preflight short circuit",
                test_noop_and_blocking_preflight_short_circuit);
        run_case(
                "skipped preflight inconsistencies fail closed",
                test_skipped_preflight_inconsistencies_fail_closed);
        run_case(
                "executable structure failures",
                test_executable_structure_failures);
        run_case(
                "single-root exact work item and DB snapshot",
                test_single_root_exact_work_item_and_snapshot);
        run_case(
                "BuildPlan order, skipped exclusion, and install reasons",
                test_build_plan_order_skip_exclusion_and_install_reasons);
        run_case(
                "unknown package root attribution is global",
                test_unknown_package_root_attribution_is_global);
        run_case(
                "strict absent, empty, valid, and warning results",
                test_strict_absent_empty_valid_and_warning_results);
        run_case(
                "strict typed failures are not flattened",
                test_strict_typed_failures_are_not_flattened);
        run_case(
                "package to PackageBase fallback and failure order",
                test_package_base_fallback_and_failure_order);
        run_case(
                "PKGDEST conflicts stop before DB",
                test_pkgdest_conflicts_stop_before_database);
        run_case(
                "typed DB failure has no partial invocation",
                test_database_failure_is_typed_and_has_no_partial_invocation);
        run_case(
                "unexpected generic failure is global",
                test_unexpected_generic_failure_is_global_and_stops_before_database);
        run_case(
                "result state helpers reject forbidden combination",
                test_result_state_helpers_reject_forbidden_combination);
        run_case(
                "move preserves correlation and invalidates source",
                test_move_preserves_correlation_and_invalidates_source);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update execution preparation tests: all checks passed\n";
    return 0;
}
