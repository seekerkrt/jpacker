#include "app_config.hpp"
#include "artifact_install_executor.hpp"
#include "aur_update_execution_runner.hpp"
#include "stubs/aur-update-execution-preparation/preparation_stub.hpp"
#include "stubs/aur-update-execution-runner/execution_stub.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using AurUpdateRunnerFunction = AurUpdateSourceBuildExecutionResult (*)(
        PreparedAurUpdateSourceBuildInvocation,
        const AppConfig&);

static_assert(
        std::is_same_v<
                decltype(&execute_prepared_aur_update_source_build_invocation),
                AurUpdateRunnerFunction>);
static_assert(
        std::is_invocable_v<
                AurUpdateRunnerFunction,
                PreparedAurUpdateSourceBuildInvocation&&,
                const AppConfig&>);
static_assert(
        !std::is_invocable_v<
                AurUpdateRunnerFunction,
                PreparedAurUpdateSourceBuildInvocation&,
                const AppConfig&>);
static_assert(
        !std::is_invocable_v<
                AurUpdateRunnerFunction,
                const PreparedAurUpdateSourceBuildInvocation&,
                const AppConfig&>);

namespace {

namespace fs = std::filesystem;
namespace execution_stub = aur_update_execution_runner_test_stub;
namespace preparation_stub = aur_update_execution_preparation_test_stub;

constexpr const char* UNKNOWN_EXCEPTION_DIAGNOSTIC =
        "Prepared AUR update source-build work item failed with an unknown exception.";

struct ExpectedWorkItem {
    std::size_t                     index = 0;
    std::string                     package_name;
    std::string                     package_base;
    std::vector<std::string>        plan_package_names;
    std::vector<std::size_t>        affected_update_plan_indices;
    std::vector<RootTargetIdentity> affected_roots;
};

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
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
        DesiredInstallReason desired_reason) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = update_plan_index;
    target.build_plan_root_index = root_index;
    target.update = update_entry(
            package_name, desired_reason, package_name);
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

AurUpdateExecutionPreflight three_item_single_root_preflight() {
    const RootTargetIdentity root{0, "root-package"};

    BuildPlan plan;
    plan.root_targets.push_back(root);
    plan.package_targets = {
            PlannedPackageTarget{
                    "first-dependency",
                    "first-dependency",
                    {PackageRole::BuildDependency},
                    {root}},
            PlannedPackageTarget{
                    "second-dependency",
                    "second-dependency",
                    {PackageRole::RuntimeDependency},
                    {root}},
            PlannedPackageTarget{
                    "root-package",
                    "root-package",
                    {PackageRole::Root},
                    {root}},
    };
    plan.order = {
            BuildPlanEntry{
                    "first-dependency", {"first-dependency"}},
            BuildPlanEntry{
                    "second-dependency", {"second-dependency"}},
            BuildPlanEntry{"root-package", {"root-package"}},
    };

    AurUpdateExecutionPreflight preflight;
    preflight.targets.push_back(executable_target(
            0, 0, "root-package", DesiredInstallReason::Explicit));
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
                    0, 0, "root-a", DesiredInstallReason::Explicit),
            skipped_target(1, "skipped-root"),
            executable_target(
                    2, 1, "root-b", DesiredInstallReason::Dependency),
    };
    preflight.build_plan = std::move(plan);
    return preflight;
}

std::vector<ExpectedWorkItem> single_root_expectations() {
    const RootTargetIdentity root{0, "root-package"};
    return {
            ExpectedWorkItem{
                    0,
                    "first-dependency",
                    "first-dependency",
                    {"first-dependency"},
                    {0},
                    {root}},
            ExpectedWorkItem{
                    1,
                    "second-dependency",
                    "second-dependency",
                    {"second-dependency"},
                    {0},
                    {root}},
            ExpectedWorkItem{
                    2,
                    "root-package",
                    "root-package",
                    {"root-package"},
                    {0},
                    {root}},
    };
}

std::vector<ExpectedWorkItem> multi_root_expectations() {
    const RootTargetIdentity root_a{0, "root-a"};
    const RootTargetIdentity root_b{1, "root-b"};
    return {
            ExpectedWorkItem{
                    0,
                    "private-dependency",
                    "private-dependency",
                    {"private-dependency"},
                    {0},
                    {root_a}},
            ExpectedWorkItem{
                    1,
                    "shared-dependency",
                    "shared-dependency",
                    {"shared-dependency"},
                    {0, 2},
                    {root_a, root_b}},
            ExpectedWorkItem{
                    2,
                    "root-b",
                    "root-b",
                    {"root-b"},
                    {0, 2},
                    {root_a, root_b}},
            ExpectedWorkItem{
                    3,
                    "root-a",
                    "root-a",
                    {"root-a"},
                    {0},
                    {root_a}},
    };
}

AurUpdateSourceBuildPreparation prepare_fixture(
        AurUpdateExecutionPreflight preflight,
        const AppConfig& config,
        PacmanDatabasePaths database_paths,
        bool needed = false) {
    preparation_stub::reset();
    preparation_stub::set_database_paths(std::move(database_paths));

    AurUpdateSourceBuildPreparation preparation =
            prepare_aur_update_source_build_invocation(
                    preflight, needed, config);
    expect(preparation.is_prepared(), "Runner fixture was not prepared");
    expect(preparation.issues.empty(), "Runner fixture retained preparation issues");
    expect(
            preparation.invocation.has_value(),
            "Runner fixture has no correlated invocation");

    const PreparedAurUpdateSourceBuildInvocation& invocation =
            *preparation.invocation;
    expect(
            !invocation.production_invocation_for_test().work_items.empty(),
            "Runner fixture has no production work items");
    expect(
            invocation.production_invocation_for_test().work_items.size() ==
                    invocation.work_item_attributions().size(),
            "Runner fixture correlation count differs");
    expect(
            preparation_stub::database_call_count() == 1,
            "Runner fixture did not resolve its database snapshot exactly once");
    return preparation;
}

AurUpdateSourceBuildExecutionResult execute_without_escape(
        PreparedAurUpdateSourceBuildInvocation invocation,
        const AppConfig& config,
        const std::string& context) {
    try {
        return execute_prepared_aur_update_source_build_invocation(
                std::move(invocation), config);
    } catch(...) {
        throw std::runtime_error(
                context + ": runner leaked an executor exception");
    }
}

void expect_result_identity(
        const AurUpdateWorkItemExecutionResult& actual,
        const ExpectedWorkItem& expected,
        const std::string& context) {
    expect(
            actual.work_item_index == expected.index,
            context + ": work item index differs");
    expect(
            actual.package_name == expected.package_name,
            context + ": package name differs");
    expect(
            actual.package_base == expected.package_base,
            context + ": PackageBase differs");
    expect(
            actual.plan_package_names == expected.plan_package_names,
            context + ": plan package names differ");
    expect(
            actual.affected_update_plan_indices ==
                    expected.affected_update_plan_indices,
            context + ": affected update-plan indices differ");
    expect(
            actual.affected_roots == expected.affected_roots,
            context + ": affected roots differ");
}

void expect_all_result_identities(
        const AurUpdateSourceBuildExecutionResult& result,
        const std::vector<ExpectedWorkItem>& expected,
        const std::string& context) {
    expect(
            result.work_item_results.size() == expected.size(),
            context + ": result count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect_result_identity(
                result.work_item_results[index],
                expected[index],
                context + " item " + std::to_string(index));
    }
}

void expect_entry_state(
        const AurUpdateWorkItemExecutionResult& result,
        AurUpdateWorkItemExecutionStatus status,
        AurUpdateWorkItemFailureKind failure_kind,
        const std::optional<std::string>& diagnostic,
        const std::string& context) {
    expect(result.status == status, context + ": status differs");
    expect(
            result.failure_kind == failure_kind,
            context + ": failure kind differs");
    expect(
            result.diagnostic == diagnostic,
            context + ": diagnostic differs");
}

void expect_stopped_result_entries(
        const AurUpdateSourceBuildExecutionResult& result,
        std::size_t stopped_index,
        AurUpdateWorkItemExecutionStatus stopped_status,
        AurUpdateWorkItemFailureKind stopped_failure_kind,
        const std::string& stopped_diagnostic,
        const std::string& context) {
    for(std::size_t index = 0;
        index < result.work_item_results.size(); ++index) {
        if(index < stopped_index) {
            expect_entry_state(
                    result.work_item_results[index],
                    AurUpdateWorkItemExecutionStatus::Updated,
                    AurUpdateWorkItemFailureKind::None,
                    std::nullopt,
                    context + " updated item " + std::to_string(index));
        } else if(index == stopped_index) {
            expect_entry_state(
                    result.work_item_results[index],
                    stopped_status,
                    stopped_failure_kind,
                    stopped_diagnostic,
                    context + " stopped item " + std::to_string(index));
        } else {
            expect_entry_state(
                    result.work_item_results[index],
                    AurUpdateWorkItemExecutionStatus::NotAttempted,
                    AurUpdateWorkItemFailureKind::PriorWorkItemStopped,
                    std::nullopt,
                    context + " not-attempted item " +
                            std::to_string(index));
        }
    }
}

void expect_execution_calls(
        const std::vector<ExpectedWorkItem>& expected,
        std::size_t expected_call_count,
        const PacmanDatabasePaths& expected_database_paths,
        const std::string& context) {
    const std::vector<execution_stub::ExecutionCall>& calls =
            execution_stub::call_history();
    expect(
            calls.size() == expected_call_count,
            context + ": executor call count differs");
    for(std::size_t index = 0; index < calls.size(); ++index) {
        const execution_stub::ExecutionCall& call = calls[index];
        expect(call.call_index == index, context + ": call index differs");
        expect(
                call.package_name == expected[index].package_name &&
                        call.package_base == expected[index].package_base,
                context + ": executor identity differs at " +
                        std::to_string(index));
        expect(
                call.plan_package_names == expected[index].plan_package_names,
                context + ": executor plan package names differ at " +
                        std::to_string(index));
        expect(
                call.database_paths.root_dir ==
                                expected_database_paths.root_dir &&
                        call.database_paths.db_path ==
                                expected_database_paths.db_path,
                context + ": executor database snapshot differs at " +
                        std::to_string(index));
    }
    execution_stub::require_script_consumed();
}

void enqueue_successes(
        std::size_t count,
        ArtifactInstallExecutionOutcome outcome =
                ArtifactInstallExecutionOutcome::Installed) {
    for(std::size_t index = 0; index < count; ++index) {
        execution_stub::enqueue_success(outcome);
    }
}

void test_all_success() {
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/runner/root", "/runner/database"};
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            expected_database_paths);

    execution_stub::reset();
    enqueue_successes(expected.size());
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation),
            config,
            "all-success execution");

    expect_all_result_identities(result, expected, "all-success result");
    for(std::size_t index = 0;
        index < result.work_item_results.size(); ++index) {
        expect_entry_state(
                result.work_item_results[index],
                AurUpdateWorkItemExecutionStatus::Updated,
                AurUpdateWorkItemFailureKind::None,
                std::nullopt,
                "all-success item " + std::to_string(index));
    }
    expect(
            result.status == AurUpdateInvocationExecutionStatus::Completed,
            "All-success invocation did not complete");
    expect(result.is_success(), "All-success helper reported failure");
    expect(
            result.changed_package_state(),
            "All-success changed-package-state helper lost the update");
    expect(
            !result.has_not_attempted_items(),
            "All-success helper reported unattempted work");
    expect(
            !result.has_cleanup_failure(),
            "All-success helper reported cleanup failure");
    expect(
            !result.stopped_work_item_index().has_value(),
            "All-success helper reported a stop index");
    expect_execution_calls(
            expected,
            expected.size(),
            expected_database_paths,
            "all-success calls");
    expect(
            preparation_stub::database_call_count() == 1,
            "All-success runner re-resolved Pacman database paths");
    expect(
            preparation_stub::strict_preference_read_history() ==
                    std::vector<std::string>{
                            "first-dependency",
                            "second-dependency",
                            "root-package"},
            "All-success runner re-read source preferences");
}

void test_no_change_and_mixed_success() {
    const AppConfig config;
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();

    const PacmanDatabasePaths no_change_database_paths{
            "/no-change/root", "/no-change/database"};
    AurUpdateSourceBuildPreparation no_change_preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            no_change_database_paths,
            true);
    execution_stub::reset();
    enqueue_successes(
            expected.size(),
            ArtifactInstallExecutionOutcome::SkippedAsNeeded);
    AurUpdateSourceBuildExecutionResult no_change_result =
            execute_without_escape(
                    std::move(*no_change_preparation.invocation),
                    config,
                    "all-no-change execution");

    expect_all_result_identities(
            no_change_result, expected, "all-no-change result");
    for(std::size_t index = 0;
        index < no_change_result.work_item_results.size(); ++index) {
        expect_entry_state(
                no_change_result.work_item_results[index],
                AurUpdateWorkItemExecutionStatus::NoChange,
                AurUpdateWorkItemFailureKind::None,
                std::nullopt,
                "all-no-change item " + std::to_string(index));
    }
    expect(
            no_change_result.status ==
                            AurUpdateInvocationExecutionStatus::Completed &&
                    no_change_result.is_success(),
            "All-no-change invocation did not complete successfully");
    expect(
            !no_change_result.changed_package_state(),
            "All-no-change changed-package-state helper reported "
            "a package state change");
    expect(
            !no_change_result.has_not_attempted_items() &&
                    !no_change_result.has_cleanup_failure() &&
                    !no_change_result.stopped_work_item_index().has_value(),
            "All-no-change helpers reported a stopped execution");
    expect_execution_calls(
            expected,
            expected.size(),
            no_change_database_paths,
            "all-no-change calls");

    const PacmanDatabasePaths mixed_database_paths{
            "/mixed/root", "/mixed/database"};
    AurUpdateSourceBuildPreparation mixed_preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            mixed_database_paths,
            true);
    execution_stub::reset();
    execution_stub::enqueue_success(
            ArtifactInstallExecutionOutcome::Installed);
    execution_stub::enqueue_success(
            ArtifactInstallExecutionOutcome::SkippedAsNeeded);
    execution_stub::enqueue_success(
            ArtifactInstallExecutionOutcome::SkippedAsNeeded);
    AurUpdateSourceBuildExecutionResult mixed_result = execute_without_escape(
            std::move(*mixed_preparation.invocation),
            config,
            "updated-then-no-change execution");

    expect_all_result_identities(
            mixed_result, expected, "updated-then-no-change result");
    expect_entry_state(
            mixed_result.work_item_results[0],
            AurUpdateWorkItemExecutionStatus::Updated,
            AurUpdateWorkItemFailureKind::None,
            std::nullopt,
            "updated-then-no-change first item");
    for(std::size_t index = 1;
        index < mixed_result.work_item_results.size(); ++index) {
        expect_entry_state(
                mixed_result.work_item_results[index],
                AurUpdateWorkItemExecutionStatus::NoChange,
                AurUpdateWorkItemFailureKind::None,
                std::nullopt,
                "updated-then-no-change item " + std::to_string(index));
    }
    expect(
            mixed_result.status ==
                            AurUpdateInvocationExecutionStatus::Completed &&
                    mixed_result.is_success(),
            "Updated/no-change invocation did not complete successfully");
    expect(
            mixed_result.changed_package_state(),
            "Updated/no-change changed-package-state helper lost the earlier update");
    expect_execution_calls(
            expected,
            expected.size(),
            mixed_database_paths,
            "updated-then-no-change calls");
}

void run_ordinary_failure_case(std::size_t failure_index) {
    const std::string context =
            "ordinary failure at " + std::to_string(failure_index);
    const std::string diagnostic =
            "scripted ordinary failure " + std::to_string(failure_index);
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/ordinary/root", "/ordinary/database"};
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            expected_database_paths);

    execution_stub::reset();
    enqueue_successes(failure_index);
    execution_stub::enqueue_ordinary_failure(diagnostic);
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation), config, context);

    expect_all_result_identities(result, expected, context);
    expect_stopped_result_entries(
            result,
            failure_index,
            AurUpdateWorkItemExecutionStatus::Failed,
            AurUpdateWorkItemFailureKind::BuildOrInstallFailed,
            diagnostic,
            context);
    expect(
            result.status == AurUpdateInvocationExecutionStatus::
                    StoppedOnWorkItemFailure,
            context + ": overall status differs");
    expect(!result.is_success(), context + ": helper reported success");
    expect(
            result.changed_package_state() == (failure_index > 0),
            context + ": changed-package-state helper differs");
    expect(
            result.has_not_attempted_items(),
            context + ": helper lost unattempted work");
    expect(
            !result.has_cleanup_failure(),
            context + ": helper reported cleanup failure");
    expect(
            result.stopped_work_item_index() == failure_index,
            context + ": stop index differs");
    expect_execution_calls(
            expected,
            failure_index + 1,
            expected_database_paths,
            context + " calls");
    expect(
            preparation_stub::database_call_count() == 1,
            context + ": runner re-resolved Pacman database paths");
}

void test_ordinary_failure_first_and_middle() {
    run_ordinary_failure_case(0);
    run_ordinary_failure_case(1);
}

void run_cleanup_failure_case(std::size_t failure_index) {
    const std::string context =
            "cleanup failure at " + std::to_string(failure_index);
    const std::string diagnostic =
            "scripted cleanup failure " + std::to_string(failure_index);
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/cleanup/root", "/cleanup/database"};
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            expected_database_paths,
            true);

    execution_stub::reset();
    enqueue_successes(failure_index);
    execution_stub::enqueue_cleanup_failure(
            ArtifactInstallExecutionOutcome::Installed,
            diagnostic);
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation), config, context);

    expect_all_result_identities(result, expected, context);
    expect_stopped_result_entries(
            result,
            failure_index,
            AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed,
            AurUpdateWorkItemFailureKind::
                    CleanupFailedAfterPackageTransaction,
            diagnostic,
            context);
    expect(
            result.status == AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure,
            context + ": overall status differs");
    expect(!result.is_success(), context + ": helper reported success");
    expect(
            result.changed_package_state(),
            context +
                    ": changed-package-state helper lost the completed installation");
    expect(
            result.has_not_attempted_items() ==
                    (failure_index + 1 < expected.size()),
            context + ": unattempted helper differs");
    expect(
            result.has_cleanup_failure(),
            context + ": helper lost cleanup failure");
    expect(
            result.stopped_work_item_index() == failure_index,
            context + ": stop index differs");
    expect_execution_calls(
            expected,
            failure_index + 1,
            expected_database_paths,
            context + " calls");
    expect(
            preparation_stub::database_call_count() == 1,
            context + ": runner re-resolved Pacman database paths");
}

void test_cleanup_partial_success_first_middle_and_last() {
    run_cleanup_failure_case(0);
    run_cleanup_failure_case(1);
    run_cleanup_failure_case(2);
}

void run_no_change_cleanup_failure_case(bool has_prior_update) {
    const std::size_t failure_index = has_prior_update ? 1 : 0;
    const std::string context = has_prior_update
            ? "updated then no-change cleanup failure"
            : "no-change cleanup failure";
    const std::string diagnostic = "scripted " + context;
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/no-change-cleanup/root",
            "/no-change-cleanup/database"};
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            expected_database_paths,
            true);

    execution_stub::reset();
    if(has_prior_update) {
        execution_stub::enqueue_success(
                ArtifactInstallExecutionOutcome::Installed);
    }
    execution_stub::enqueue_cleanup_failure(
            ArtifactInstallExecutionOutcome::SkippedAsNeeded,
            diagnostic);
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation), config, context);

    expect_all_result_identities(result, expected, context);
    expect_stopped_result_entries(
            result,
            failure_index,
            AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed,
            AurUpdateWorkItemFailureKind::
                    CleanupFailedAfterPackageTransaction,
            diagnostic,
            context);
    expect(
            result.status == AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure,
            context + ": overall status differs");
    expect(!result.is_success(), context + ": helper reported success");
    expect(
            result.changed_package_state() == has_prior_update,
            context + ": changed-package-state helper differs");
    expect(
            result.has_not_attempted_items(),
            context + ": helper lost the unattempted suffix");
    expect(
            result.has_cleanup_failure(),
            context + ": helper lost cleanup failure");
    expect(
            result.stopped_work_item_index() == failure_index,
            context + ": stop index differs");
    expect_execution_calls(
            expected,
            failure_index + 1,
            expected_database_paths,
            context + " calls");
}

void test_no_change_cleanup_failure_with_and_without_prior_update() {
    run_no_change_cleanup_failure_case(false);
    run_no_change_cleanup_failure_case(true);
}

void test_unknown_exception_is_typed_and_contained() {
    constexpr std::size_t FAILURE_INDEX = 1;
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/unknown/root", "/unknown/database"};
    const std::vector<ExpectedWorkItem> expected =
            single_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            three_item_single_root_preflight(),
            config,
            expected_database_paths);

    execution_stub::reset();
    enqueue_successes(FAILURE_INDEX);
    execution_stub::enqueue_unknown_failure();
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation),
            config,
            "unknown exception");

    expect_all_result_identities(result, expected, "unknown exception");
    expect_stopped_result_entries(
            result,
            FAILURE_INDEX,
            AurUpdateWorkItemExecutionStatus::Failed,
            AurUpdateWorkItemFailureKind::UnknownException,
            UNKNOWN_EXCEPTION_DIAGNOSTIC,
            "unknown exception");
    expect(
            result.status == AurUpdateInvocationExecutionStatus::
                    StoppedOnWorkItemFailure,
            "Unknown exception overall status differs");
    expect(!result.is_success(), "Unknown exception helper reported success");
    expect(
            result.changed_package_state(),
            "Unknown exception changed-package-state helper lost the earlier update");
    expect(
            result.has_not_attempted_items(),
            "Unknown exception helper lost unattempted work");
    expect(
            !result.has_cleanup_failure(),
            "Unknown exception helper reported cleanup failure");
    expect(
            result.stopped_work_item_index() == FAILURE_INDEX,
            "Unknown exception stop index differs");
    expect_execution_calls(
            expected,
            FAILURE_INDEX + 1,
            expected_database_paths,
            "unknown exception calls");
}

void test_multi_root_attribution_execution_order_and_one_shot() {
    const AppConfig config;
    const PacmanDatabasePaths expected_database_paths{
            "/multi/root", "/multi/database"};
    const std::vector<ExpectedWorkItem> expected =
            multi_root_expectations();
    AurUpdateSourceBuildPreparation preparation = prepare_fixture(
            ordered_multi_root_preflight(),
            config,
            expected_database_paths);

    execution_stub::reset();
    enqueue_successes(expected.size());
    AurUpdateSourceBuildExecutionResult result = execute_without_escape(
            std::move(*preparation.invocation),
            config,
            "multi-root attribution");

    expect_all_result_identities(
            result, expected, "multi-root attribution");
    for(std::size_t index = 0;
        index < result.work_item_results.size(); ++index) {
        expect_entry_state(
                result.work_item_results[index],
                AurUpdateWorkItemExecutionStatus::Updated,
                AurUpdateWorkItemFailureKind::None,
                std::nullopt,
                "multi-root item " + std::to_string(index));
    }
    expect(
            result.status == AurUpdateInvocationExecutionStatus::Completed &&
                    result.is_success(),
            "Multi-root execution did not complete");
    expect_execution_calls(
            expected,
            expected.size(),
            expected_database_paths,
            "multi-root calls");
    expect(
            preparation_stub::database_call_count() == 1,
            "Multi-root runner re-resolved Pacman database paths");

    // POLICY(#267): correlated capabilityのconsume後はpreparation側のmove元を
    // invalid化し、replayは最初のexecutor callより前に拒否する。
    expect(
            !preparation.is_prepared(),
            "Consumed preparation still reports a prepared invocation");
    expect(
            preparation.invocation.has_value() &&
                    !preparation.invocation->is_valid(),
            "Consumed preparation did not retain a typed moved-from state");

    execution_stub::reset();
    bool replay_rejected = false;
    try {
        static_cast<void>(
                execute_prepared_aur_update_source_build_invocation(
                        std::move(*preparation.invocation), config));
    } catch(const std::logic_error&) {
        replay_rejected = true;
    } catch(...) {
        throw std::runtime_error(
                "Moved-from invocation replay raised an unexpected exception");
    }

    expect(
            replay_rejected,
            "Moved-from invocation replay was not rejected");
    expect(
            execution_stub::call_history().empty(),
            "Moved-from invocation replay reached the executor");
    execution_stub::require_script_consumed();
}

template<typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("all success", test_all_success);
        run_case(
                "no-change and mixed success",
                test_no_change_and_mixed_success);
        run_case(
                "ordinary failure first and middle",
                test_ordinary_failure_first_and_middle);
        run_case(
                "cleanup partial-success first, middle, and last",
                test_cleanup_partial_success_first_middle_and_last);
        run_case(
                "no-change cleanup failure with and without prior update",
                test_no_change_cleanup_failure_with_and_without_prior_update);
        run_case(
                "unknown exception is typed and contained",
                test_unknown_exception_is_typed_and_contained);
        run_case(
                "multi-root attribution, execution order, and one-shot replay",
                test_multi_root_attribution_execution_order_and_one_shot);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update execution runner tests: all checks passed\n";
    return 0;
}
