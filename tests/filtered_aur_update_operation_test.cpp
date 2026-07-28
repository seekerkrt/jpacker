#include "app_config.hpp"
#include "artifact_install_executor.hpp"
#include "filtered_aur_update_operation.hpp"
#include "stubs/aur-update-execution-preflight/preflight_stub.hpp"
#include "stubs/aur-update-execution-preparation/preparation_stub.hpp"
#include "stubs/aur-update-execution-runner/execution_stub.hpp"
#include "stubs/filtered-aur-update-operation/query_stub.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<
              PreparedFilteredAurUpdateOperation>);
static_assert(!std::is_copy_constructible_v<
              PreparedFilteredAurUpdateOperation>);
static_assert(std::is_nothrow_move_constructible_v<
              PreparedFilteredAurUpdateOperation>);
static_assert(!std::is_move_assignable_v<
              PreparedFilteredAurUpdateOperation>);

namespace {

namespace preflight_stub = aur_update_execution_preflight_test_stub;
namespace preparation_stub = aur_update_execution_preparation_test_stub;
namespace execution_stub = aur_update_execution_runner_test_stub;
namespace query_stub = filtered_aur_update_operation_query_test_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename ExpectedException, typename Callable>
void expect_exception(Callable callable, const std::string& context) {
    try {
        callable();
    } catch(const ExpectedException&) {
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " +
                error.what());
    }
    throw std::runtime_error(context + ": expected exception was not raised");
}

void reset_stubs() {
    query_stub::reset();
    preflight_stub::reset_preflight_stub();
    preparation_stub::reset();
    execution_stub::reset();
}

AurUpdatePlanEntry update_entry(
        const std::string& package_name,
        const std::string& package_base = {},
        InstalledPackageReason reason = InstalledPackageReason::Explicit) {
    const std::string resolved_base =
            package_base.empty() ? package_name : package_base;
    return AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            reason,
            AurUpdateRemotePackage{
                    package_name,
                    resolved_base,
                    "2.0-1",
                    AurVersionRelation::NewerThanInstalled},
            AurUpdateClassification::UpdateAvailable};
}

AurUpdatePlanEntry current_entry(const std::string& package_name) {
    return AurUpdatePlanEntry{
            package_name,
            "2.0-1",
            InstalledPackageReason::Explicit,
            AurUpdateRemotePackage{
                    package_name,
                    package_name,
                    "2.0-1",
                    AurVersionRelation::SameAsInstalled},
            AurUpdateClassification::UpToDate};
}

AurUpdatePlanEntry non_aur_entry(const std::string& package_name) {
    return AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            InstalledPackageReason::Dependency,
            std::nullopt,
            AurUpdateClassification::NonAurForeign};
}

AurUpdatePlanEntry unavailable_entry(const std::string& package_name) {
    return AurUpdatePlanEntry{
            package_name,
            "1.0-1",
            InstalledPackageReason::Explicit,
            std::nullopt,
            AurUpdateClassification::MetadataUnavailable};
}

AurUpdateQueryResult query_result(
        std::vector<AurUpdatePlanEntry> entries,
        std::vector<AurUpdateQueryFailure> failures = {}) {
    return AurUpdateQueryResult{
            AurUpdatePlan{std::move(entries)}, std::move(failures)};
}

UpgradeAllExplicitSourceIdentity explicit_source(
        const std::string& preference_name,
        const std::string& package_base,
        std::vector<std::string> produced_names = {},
        const std::string& source_key = {}) {
    return UpgradeAllExplicitSourceIdentity{
            preference_name,
            UpgradeAllResolvedPackageBase{package_base},
            std::move(produced_names),
            UpgradeAllResolvedSourceIdentity{
                    source_key.empty()
                            ? "source://" + preference_name
                            : source_key}};
}

AurPackageInfo package_info(
        const std::string& package_name,
        const std::string& package_base = {},
        const std::string& version = "2.0-1") {
    AurPackageInfo info;
    info.Name = package_name;
    info.PackageBase = package_base.empty() ? package_name : package_base;
    info.Version = version;
    info.Description = "filtered AUR operation fixture";
    info.Maintainer = "jpacker-test";
    return info;
}

struct RootSpec {
    std::string package_name;
    std::string package_base;
};

PlannedPackageTarget* find_package_target(
        BuildPlan& plan, const std::string& package_name) {
    auto found = std::find_if(
            plan.package_targets.begin(), plan.package_targets.end(),
            [&package_name](const PlannedPackageTarget& target) {
                return target.package_name == package_name;
            });
    return found == plan.package_targets.end() ? nullptr : &*found;
}

const PlannedPackageTarget& require_package_target(
        const BuildPlan& plan, const std::string& package_name) {
    auto found = std::find_if(
            plan.package_targets.begin(), plan.package_targets.end(),
            [&package_name](const PlannedPackageTarget& target) {
                return target.package_name == package_name;
            });
    if(found == plan.package_targets.end()) {
        throw std::logic_error(
                "BuildPlan fixture parent is missing: " + package_name);
    }
    return *found;
}

template<typename Value>
void append_unique(std::vector<Value>& values, const Value& value) {
    if(std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

BuildPlan root_plan(std::vector<RootSpec> roots) {
    BuildPlan plan;
    for(std::size_t index = 0; index < roots.size(); ++index) {
        const RootSpec& fixture = roots[index];
        const RootTargetIdentity root{index, fixture.package_name};
        plan.root_targets.push_back(root);
        plan.package_targets.push_back(PlannedPackageTarget{
                fixture.package_name,
                fixture.package_base,
                {PackageRole::Root},
                {root}});

        auto same_base = [&fixture](const BuildPlanEntry& entry) {
            return entry.package_base == fixture.package_base;
        };
        auto order = std::find_if(
                plan.order.begin(), plan.order.end(), same_base);
        if(order == plan.order.end()) {
            plan.order.push_back(BuildPlanEntry{
                    fixture.package_base, {fixture.package_name}});
        } else {
            append_unique(order->package_names, fixture.package_name);
        }
    }
    return plan;
}

// dependency edgeとdeclared role/rootを同時に足し、actual preflightのgraph
// consistency checkを通るfixtureだけを作る。
void add_aur_dependency(
        BuildPlan& plan,
        const std::string& parent_package_name,
        const std::string& dependency_package_name,
        const std::string& dependency_package_base,
        PackageRole role = PackageRole::RuntimeDependency) {
    const PlannedPackageTarget& parent =
            require_package_target(plan, parent_package_name);
    const std::string parent_package_base = parent.package_base;
    const std::vector<RootTargetIdentity> parent_roots = parent.roots;

    PlannedPackageTarget* dependency =
            find_package_target(plan, dependency_package_name);
    if(dependency == nullptr) {
        plan.package_targets.push_back(PlannedPackageTarget{
                dependency_package_name,
                dependency_package_base,
                {role},
                parent_roots});
    } else {
        expect(
                dependency->package_base == dependency_package_base,
                "BuildPlan fixture dependency PackageBase differs");
        append_unique(dependency->roles, role);
        for(const RootTargetIdentity& root : parent_roots) {
            append_unique(dependency->roots, root);
        }
    }

    auto same_base = [&dependency_package_base](const BuildPlanEntry& entry) {
        return entry.package_base == dependency_package_base;
    };
    auto order = std::find_if(plan.order.begin(), plan.order.end(), same_base);
    if(order == plan.order.end()) {
        plan.order.insert(
                plan.order.begin(),
                BuildPlanEntry{
                        dependency_package_base,
                        {dependency_package_name}});
    } else {
        append_unique(order->package_names, dependency_package_name);
    }

    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            parent_package_name,
            parent_package_base,
            dependency_package_name,
            role,
            DependencyKind::Aur,
            dependency_package_name,
            dependency_package_base,
            std::nullopt});
}

void return_build_plan(
        BuildPlan plan, std::vector<std::string> expected_targets) {
    preflight_stub::set_resolver_handler(
            [plan = std::move(plan),
             expected_targets = std::move(expected_targets)](
                    const std::vector<std::string>& targets) {
                if(targets != expected_targets) {
                    throw std::logic_error(
                            "Filtered operation resolver target order differs");
                }
                return plan;
            });
}

const PreparedProductionSourceBuildInvocation& require_production_invocation(
        const PreparedFilteredAurUpdateOperation& prepared) {
    expect(
            prepared.source_build_preparation().has_value() &&
                    prepared.source_build_preparation()
                            ->invocation.has_value(),
            "Filtered operation fixture has no production invocation");
    return prepared.source_build_preparation()
            ->invocation->production_invocation_for_test();
}

execution_stub::ExpectedExecution expected_execution_at(
        const PreparedFilteredAurUpdateOperation& prepared,
        std::size_t work_item_index,
        const AppConfig& config) {
    const PreparedProductionSourceBuildInvocation& invocation =
            require_production_invocation(prepared);
    expect(
            work_item_index < invocation.work_items.size(),
            "Filtered operation execution expectation index is out of range");
    const ProductionSourceBuildWorkItem& work_item =
            invocation.work_items[work_item_index];
    return execution_stub::ExpectedExecution{
            work_item_index,
            work_item.request.checkout_name,
            work_item.required_targets,
            work_item.request.needed,
            invocation.database_paths,
            config};
}

std::vector<PackageBaseSourceBuildSelectedResult> selected_children_at(
        const PreparedFilteredAurUpdateOperation& prepared,
        std::size_t work_item_index,
        ArtifactInstallExecutionOutcome outcome) {
    const PreparedProductionSourceBuildInvocation& invocation =
            require_production_invocation(prepared);
    expect(
            work_item_index < invocation.work_items.size(),
            "Filtered operation selected result index is out of range");
    std::vector<PackageBaseSourceBuildSelectedResult> children;
    for(const auto& target :
        invocation.work_items[work_item_index].required_targets) {
        children.push_back(PackageBaseSourceBuildSelectedResult{
                ArtifactPackageIdentity{target.package_name, "2.0-1"},
                target.desired_reason,
                outcome});
    }
    return children;
}

void enqueue_outcome(
        const PreparedFilteredAurUpdateOperation& prepared,
        const AppConfig& config,
        std::size_t count,
        ArtifactInstallExecutionOutcome outcome) {
    const PreparedProductionSourceBuildInvocation& invocation =
            require_production_invocation(prepared);
    expect(
            count <= invocation.work_items.size(),
            "Filtered operation execution expectation count is out of range");
    for(std::size_t index = 0; index < count; ++index) {
        const std::string package_base =
                invocation.work_items[index].request.checkout_name;
        execution_stub::enqueue_success(
                expected_execution_at(prepared, index, config),
                package_base,
                selected_children_at(prepared, index, outcome));
    }
}

void enqueue_installed(
        const PreparedFilteredAurUpdateOperation& prepared,
        const AppConfig& config,
        std::size_t count) {
    enqueue_outcome(
            prepared,
            config,
            count,
            ArtifactInstallExecutionOutcome::Installed);
}

void enqueue_no_change(
        const PreparedFilteredAurUpdateOperation& prepared,
        const AppConfig& config,
        std::size_t count) {
    enqueue_outcome(
            prepared,
            config,
            count,
            ArtifactInstallExecutionOutcome::SkippedAsNeeded);
}

bool has_operation_issue(
        const std::vector<FilteredAurUpdateOperationIssue>& issues,
        FilteredAurUpdateOperationIssueKind expected) {
    return std::any_of(
            issues.begin(), issues.end(),
            [expected](const FilteredAurUpdateOperationIssue& issue) {
                return issue.kind == expected;
            });
}

bool has_planner_issue(
        const UpgradeAllPlan& plan,
        UpgradeAllPlanningIssueKind expected) {
    return std::any_of(
            plan.issues.begin(), plan.issues.end(),
            [expected](const UpgradeAllPlanningIssue& issue) {
                return issue.kind == expected;
            });
}

void expect_no_mutation(const std::string& context) {
    expect(
            execution_stub::call_history().empty(),
            context + ": source-build executor was called");
    expect(
            execution_stub::event_history().empty(),
            context + ": source-build lifecycle event was emitted");
}

void expect_statuses(
        const FilteredAurUpdateExecutionResult& result,
        const std::vector<AurUpdateOperationTargetStatus>& expected,
        const std::string& context) {
    expect(
            result.selected_target_results.size() == expected.size(),
            context + ": selected result count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect(
                result.selected_target_results[index]
                                .operation_result.status == expected[index],
                context + ": target status differs at " +
                        std::to_string(index));
    }
}

bool same_config(const AppConfig& lhs, const AppConfig& rhs) {
    return lhs.no_edit == rhs.no_edit &&
           lhs.no_diff == rhs.no_diff &&
           lhs.no_confirm == rhs.no_confirm &&
           lhs.rebuild == rhs.rebuild &&
           lhs.clean_build == rhs.clean_build &&
           lhs.rm_deps == rhs.rm_deps &&
           lhs.editor == rhs.editor && lhs.log_file == rhs.log_file;
}

std::string prepared_diagnostic(
        const PreparedFilteredAurUpdateOperation& prepared) {
    std::string diagnostic =
            " operation-issues=" +
            std::to_string(prepared.operation_issues().size()) +
            " planner-issues=" +
            std::to_string(
                    prepared.target_and_build_unit_plan().issues.size());
    if(!prepared.operation_issues().empty()) {
        diagnostic += " operation-first=" +
                prepared.operation_issues().front().diagnostic;
    }
    if(prepared.source_build_preparation().has_value()) {
        diagnostic += " preparation-issues=" +
                std::to_string(
                        prepared.source_build_preparation()->issues.size());
        if(!prepared.source_build_preparation()->issues.empty()) {
            diagnostic += " preparation-first=" +
                    prepared.source_build_preparation()
                            ->issues.front().diagnostic;
        }
    }
    return diagnostic;
}

void expect_success_lifecycle(
        const std::vector<execution_stub::ExecutionCall>& calls,
        const std::vector<std::string>& expected_package_names,
        const AppConfig& expected_config,
        const std::string& context) {
    const std::vector<execution_stub::EventKind> expected_events{
            execution_stub::EventKind::Checkout,
            execution_stub::EventKind::Build,
            execution_stub::EventKind::Install,
            execution_stub::EventKind::Cleanup};
    expect(calls.size() == expected_package_names.size(),
           context + ": executor call count differs");
    for(std::size_t index = 0; index < calls.size(); ++index) {
        expect(
                calls[index].call_index == index &&
                        calls[index].package_name ==
                                expected_package_names[index] &&
                        calls[index].events == expected_events &&
                        same_config(calls[index].config, expected_config),
                context + ": call identity/options/lifecycle differs at " +
                        std::to_string(index));
    }

    const std::vector<execution_stub::Event>& events =
            execution_stub::event_history();
    expect(
            events.size() == expected_package_names.size() *
                    expected_events.size(),
            context + ": global lifecycle count differs");
    for(std::size_t call_index = 0;
        call_index < expected_package_names.size(); ++call_index) {
        for(std::size_t event_index = 0;
            event_index < expected_events.size(); ++event_index) {
            const execution_stub::Event& event = events[
                    call_index * expected_events.size() + event_index];
            expect(
                    event.call_index == call_index &&
                            event.kind == expected_events[event_index] &&
                            event.package_name ==
                                    expected_package_names[call_index],
                    context + ": global lifecycle order differs");
        }
    }
}

void test_empty_query_plan_is_normal_noop() {
    reset_stubs();
    const AppConfig config;

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation({}, {}, config);
    expect(prepared.is_noop(), "Empty query was not prepared as a no-op");
    expect(prepared.filtered_plan().entries.empty(),
           "Empty query produced filtered targets");
    expect(preflight_stub::resolver_call_count() == 0,
           "Empty query called BuildPlan resolver");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success(), "Empty query operation was not successful");
    expect(result.reduced_operation_result.status ==
                   AurUpdateOperationStatus::NoUpdates,
           "Empty query did not reduce to NoUpdates");
    expect(!result.execution.has_value(),
           "Empty query produced an execution result");
    expect_no_mutation("empty query");
}

void test_real_query_wrapper_and_empty_explicit_set_match_legacy_path() {
    reset_stubs();
    const ForeignPackageInventory inventory{{
            "legacy-root", "1.0-1", InstalledPackageReason::Explicit}};
    query_stub::set_foreign_inventory(inventory);
    query_stub::enqueue_info_many_result({
            {"legacy-root", package_info("legacy-root")}});
    query_stub::enqueue_vercmp_result("1");

    AurUpdateQueryResult query = query_installed_aur_updates();
    query_stub::require_script_consumed();
    expect(query_stub::repository_configuration_calls() == 1 &&
                   query_stub::inventory_calls() == 1,
           "Legacy query wrapper did not obtain one foreign inventory");
    expect(query.plan.entries.size() == 1 &&
                   query.plan.entries[0].classification ==
                           AurUpdateClassification::UpdateAvailable,
           "Real query wrapper did not create an update candidate");

    return_build_plan(
            root_plan({{"legacy-root", "legacy-root"}}),
            {"legacy-root"});
    AppConfig config;
    config.no_edit = true;
    config.no_diff = true;
    config.no_confirm = true;
    config.rebuild = true;
    config.clean_build = true;
    config.rm_deps = true;
    config.editor = "fixture-editor";
    config.log_file = "/fixture/log";

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    std::move(query), {}, config);
    expect(prepared.is_prepared(),
           "Empty explicit set did not prepare the legacy AUR update");
    expect(prepared.filtered_plan().entries.size() == 1 &&
                   prepared.target_and_build_unit_plan()
                                   .selected_targets.size() == 1 &&
                   prepared.target_and_build_unit_plan()
                                   .selected_build_units.size() == 1 &&
                   prepared.source_build_preparation()
                                   ->externally_satisfied_build_units.empty(),
           "Empty explicit set changed legacy selection semantics");
    expect(preparation_stub::supported_options_guard_history() ==
                   std::vector<bool>{true},
           "rm_deps option did not reach preparation");

    enqueue_installed(prepared, config, 1);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && result.changed_package_state(),
           "Legacy-equivalent operation did not update successfully");
    expect_statuses(
            result, {AurUpdateOperationTargetStatus::Updated},
            "legacy-equivalent operation");
    expect_success_lifecycle(
            execution_stub::call_history(), {"legacy-root"}, config,
            "legacy-equivalent operation");
    execution_stub::require_script_consumed();
}

void test_explicit_inventory_query_core_bypasses_inventory_wrapper() {
    reset_stubs();
    query_stub::enqueue_info_many_result({
            {"latest-root", package_info("latest-root")}});
    query_stub::enqueue_vercmp_result("1");
    ForeignPackageInventory latest_inventory{{
            "latest-root", "1.0-1", InstalledPackageReason::Explicit}};

    AurUpdateQueryResult query = query_aur_updates_for_foreign_inventory(
            std::move(latest_inventory));
    query_stub::require_script_consumed();
    expect(query_stub::repository_configuration_calls() == 0 &&
                   query_stub::inventory_calls() == 0,
           "Explicit inventory query unexpectedly reopened system inventory");
    expect(query_stub::info_many_call_history() ==
                   std::vector<std::vector<std::string>>{{"latest-root"}},
           "Explicit inventory query lost inventory order");

    return_build_plan(
            root_plan({{"latest-root", "latest-root"}}),
            {"latest-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    std::move(query), {}, config);
    enqueue_no_change(prepared, config, 1);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && !result.changed_package_state(),
           "Explicit inventory query result did not complete");
    execution_stub::require_script_consumed();
}

void test_package_name_target_exclusion() {
    reset_stubs();
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("name-match", "aur-base")}),
                    {explicit_source("name-match", "source-base")},
                    config);

    expect(prepared.is_noop(), "Package-name exclusion was not a no-op");
    expect(prepared.target_and_build_unit_plan()
                                   .target_dispositions.size() == 1 &&
                   prepared.target_and_build_unit_plan()
                                   .target_dispositions[0].disposition ==
                           UpgradeAllTargetDisposition::
                                   ExcludedByExplicitPackageName,
           "Package-name duplicate was not excluded");
    expect(prepared.target_and_build_unit_plan()
                                   .excluded_duplicate_target_indexes ==
                   std::vector<std::size_t>{0},
           "Package-name exclusion lost duplicate attribution");
    expect(prepared.original_query_result()
                           .plan.entries[0].installed_name == "name-match",
           "Package-name exclusion lost original payload");
    expect(preflight_stub::resolver_call_count() == 0,
           "Excluded package-name target reached preflight resolver");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && result.has_duplicate_exclusions(),
           "Package-name exclusion result helpers differ");
    expect_no_mutation("package-name exclusion");
}

void test_package_base_target_exclusion() {
    reset_stubs();
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("suite-cli", "suite")}),
                    {explicit_source("source-suite", "suite")},
                    config);

    expect(prepared.is_noop(), "PackageBase exclusion was not a no-op");
    const UpgradeAllTargetPlanEntry& target =
            prepared.target_and_build_unit_plan()
                    .target_dispositions.front();
    expect(target.disposition ==
                   UpgradeAllTargetDisposition::
                           ExcludedByExplicitPackageBase &&
                   target.explicit_source.has_value() &&
                   target.explicit_source->matched_package_base ==
                           std::optional<std::string>{"suite"},
           "PackageBase exclusion lost typed source attribution");
    expect(preflight_stub::resolver_call_count() == 0,
           "Excluded PackageBase reached preflight resolver");
    static_cast<void>(execute_prepared_filtered_aur_update_operation(
            std::move(prepared), config));
    expect_no_mutation("PackageBase exclusion");
}

void test_split_package_targets_share_exclusion_attribution() {
    reset_stubs();
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("suite-cli", "suite"),
                            update_entry("suite-lib", "suite")}),
                    {explicit_source("suite-source", "suite")},
                    config);

    expect(prepared.is_noop(), "Split-package exclusion was not a no-op");
    expect(prepared.target_and_build_unit_plan()
                           .excluded_duplicate_target_indexes ==
                   std::vector<std::size_t>({0, 1}),
           "Split-package exclusion indexes differ");
    for(const UpgradeAllTargetPlanEntry& target :
        prepared.target_and_build_unit_plan().target_dispositions) {
        expect(target.disposition ==
                       UpgradeAllTargetDisposition::
                               ExcludedByExplicitPackageBase &&
                       target.explicit_source.has_value() &&
                       target.explicit_source->explicit_source_indexes ==
                               std::vector<std::size_t>{0},
               "Split-package exclusion attribution differs");
    }
    static_cast<void>(execute_prepared_filtered_aur_update_operation(
            std::move(prepared), config));
    expect_no_mutation("split-package exclusion");
}

void test_original_filtered_and_preflight_index_mapping() {
    reset_stubs();
    const AppConfig config;
    return_build_plan(
            root_plan({{"beta", "beta"}}), {"beta"});

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            current_entry("current"),
                            update_entry("alpha", "alpha-base"),
                            non_aur_entry("foreign"),
                            update_entry("beta")}),
                    {explicit_source("alpha", "source-alpha")},
                    config);

    expect(
            prepared.is_prepared(),
            "Mapped fixture did not prepare:" +
                    prepared_diagnostic(prepared));
    expect(prepared.filtered_to_original_indexes() ==
                   std::vector<std::size_t>({0, 2, 3}),
           "Filtered-to-original mapping differs");
    expect(prepared.original_to_filtered_indexes() ==
                   std::vector<std::optional<std::size_t>>({
                           0, std::nullopt, 1, 2}),
           "Original-to-filtered mapping differs");
    expect(prepared.filtered_plan().entries.size() == 3 &&
                   prepared.filtered_plan().entries[0].installed_name ==
                           "current" &&
                   prepared.filtered_plan().entries[1].installed_name ==
                           "foreign" &&
                   prepared.filtered_plan().entries[2].installed_name ==
                           "beta",
           "Filtered target order differs");
    expect(prepared.selected_target_correlations().size() == 1,
           "Selected target correlation count differs");
    const FilteredAurUpdateTargetCorrelation& mapping =
            prepared.selected_target_correlations().front();
    expect(mapping.planner_target_index == 1 &&
                   mapping.original_query_plan_index == 3 &&
                   mapping.selected_target_index == 0 &&
                   mapping.filtered_update_plan_index == 2 &&
                   mapping.preflight_invocation_index ==
                           std::optional<std::size_t>{0} &&
                   mapping.build_plan_root_index ==
                           std::optional<std::size_t>{0},
           "Original/selected/filtered/preflight mapping was conflated");
    expect(preflight_stub::resolver_calls() ==
                   std::vector<std::vector<std::string>>{{"beta"}},
           "Excluded root leaked into BuildPlan resolution");

    enqueue_installed(prepared, config, 1);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.selected_target_results.size() == 1 &&
                   result.selected_target_results[0]
                                   .original_query_plan_index == 3 &&
                   result.selected_target_results[0]
                                   .filtered_update_plan_index == 2 &&
                   result.selected_target_results[0].operation_result.status ==
                           AurUpdateOperationTargetStatus::Updated,
           "Reduced selected result lost original index attribution");
    expect(result.reduced_operation_result.targets.size() == 3 &&
                   result.reduced_operation_result.targets[0].status ==
                           AurUpdateOperationTargetStatus::Skipped &&
                   result.reduced_operation_result.targets[1].status ==
                           AurUpdateOperationTargetStatus::Skipped &&
                   result.reduced_operation_result.targets[2].status ==
                           AurUpdateOperationTargetStatus::Updated,
           "Filtered result order/status differs");
    expect_success_lifecycle(
            execution_stub::call_history(), {"beta"}, config,
            "mapped selected target");
    execution_stub::require_script_consumed();
}

void test_transitive_external_satisfaction_keeps_selected_root_executable() {
    reset_stubs();
    const AppConfig config;
    BuildPlan plan = root_plan({{"application", "application"}});
    add_aur_dependency(
            plan, "application", "middle-library", "middle-library");
    add_aur_dependency(
            plan, "middle-library", "external-library", "external-library",
            PackageRole::BuildDependency);
    return_build_plan(std::move(plan), {"application"});

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("application")}),
                    {explicit_source("external-library", "external-library")},
                    config);
    expect(prepared.is_prepared(),
           "Selected root with external dependency was blocked");
    expect(prepared.target_and_build_unit_plan()
                           .externally_satisfied_build_unit_indexes ==
                   std::vector<std::size_t>{0} &&
                   prepared.target_and_build_unit_plan()
                                   .selected_build_units.size() == 2,
           "Transitive external selection did not compact execution order");
    expect(prepared.source_build_preparation()
                           ->externally_satisfied_build_units.size() == 1,
           "External satisfaction was not carried into preparation");
    const AurUpdateExternallySatisfiedBuildUnit& external =
            prepared.source_build_preparation()
                    ->externally_satisfied_build_units.front();
    expect(external.package_name == "external-library" &&
                   external.package_base == "external-library" &&
                   external.required_target_attributions.size() == 1 &&
                   external.required_target_attributions.front()
                                   .required_target.package_name ==
                           "external-library" &&
                   external.affected_update_plan_indices ==
                           std::vector<std::size_t>{0} &&
                   external.affected_roots ==
                           std::vector<RootTargetIdentity>{{0, "application"}} &&
                   external.roles ==
                           std::vector<PackageRole>{
                                   PackageRole::BuildDependency} &&
                   external.external_satisfaction.matched_package_base ==
                           std::optional<std::string>{"external-library"},
           "External satisfaction lost identity/root/role attribution");

    enqueue_installed(prepared, config, 2);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && result.changed_package_state(),
           "Selected root with external dependency did not execute");
    expect_success_lifecycle(
            execution_stub::call_history(),
            {"middle-library", "application"}, config,
            "transitive external satisfaction");
    expect(std::none_of(
                   execution_stub::call_history().begin(),
                   execution_stub::call_history().end(),
                   [](const execution_stub::ExecutionCall& call) {
                       return call.package_name == "external-library";
                   }),
           "External unit reached checkout/build/install/cleanup boundary");
    execution_stub::require_script_consumed();
}

void test_multiple_child_external_satisfaction_keeps_set_snapshot() {
    reset_stubs();
    const AppConfig config;
    BuildPlan plan = root_plan({{"application", "application"}});
    add_aur_dependency(
            plan, "application", "split-runtime", "split-suite");
    add_aur_dependency(
            plan, "application", "split-tools", "split-suite",
            PackageRole::BuildDependency);
    return_build_plan(std::move(plan), {"application"});

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("application")}),
                    {explicit_source("split-source", "split-suite")},
                    config);
    expect(
            prepared.is_prepared(),
            "Selected root with external multiple-child unit was blocked" +
                    prepared_diagnostic(prepared));
    expect(
            prepared.target_and_build_unit_plan()
                            .externally_satisfied_build_unit_indexes ==
                            std::vector<std::size_t>{0} &&
                    prepared.target_and_build_unit_plan()
                                    .selected_build_units.size() == 1 &&
                    prepared.source_build_preparation()
                                    ->externally_satisfied_build_units.size() ==
                            1,
            "External multiple-child selection or dense execution order differs");

    const AurUpdateExternallySatisfiedBuildUnit& external =
            prepared.source_build_preparation()
                    ->externally_satisfied_build_units.front();
    expect(
            external.package_name.empty() &&
                    external.package_base == "split-suite" &&
                    external.plan_package_names ==
                            std::vector<std::string>{
                                    "split-runtime", "split-tools"} &&
                    external.required_target_attributions.size() == 2 &&
                    external.required_target_attributions[0]
                                    .required_target.package_name ==
                            "split-runtime" &&
                    external.required_target_attributions[0].roles ==
                            std::vector<PackageRole>{
                                    PackageRole::RuntimeDependency} &&
                    external.required_target_attributions[1]
                                    .required_target.package_name ==
                            "split-tools" &&
                    external.required_target_attributions[1].roles ==
                            std::vector<PackageRole>{
                                    PackageRole::BuildDependency} &&
                    !external.desired_install_reason.has_value() &&
                    external.affected_update_plan_indices ==
                            std::vector<std::size_t>{0} &&
                    external.affected_roots ==
                            std::vector<RootTargetIdentity>{{0, "application"}},
            "External multiple-child snapshot lost ordered child attribution");
    expect(
            preparation_stub::strict_preference_read_history() ==
                    std::vector<std::string>{"application"},
            "External multiple-child unit reached source preference IO");

    enqueue_installed(prepared, config, 1);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::Updated},
            "external multiple-child selected root");
    expect_success_lifecycle(
            execution_stub::call_history(), {"application"}, config,
            "external multiple-child selected root");
    execution_stub::require_script_consumed();
}

void test_one_external_unit_shared_by_multiple_roots() {
    reset_stubs();
    const AppConfig config;
    BuildPlan plan = root_plan({
            {"first-root", "first-root"},
            {"second-root", "second-root"}});
    add_aur_dependency(
            plan, "first-root", "shared-library", "shared-library");
    add_aur_dependency(
            plan, "second-root", "shared-library", "shared-library");
    return_build_plan(std::move(plan), {"first-root", "second-root"});

    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("first-root"),
                            update_entry("second-root")}),
                    {explicit_source("shared-library", "shared-library")},
                    config);
    expect(prepared.is_prepared(), "Shared external fixture did not prepare");
    expect(prepared.source_build_preparation()
                           ->externally_satisfied_build_units.size() == 1,
           "Shared external unit was duplicated");
    const AurUpdateExternallySatisfiedBuildUnit& external =
            prepared.source_build_preparation()
                    ->externally_satisfied_build_units.front();
    expect(external.required_target_attributions.size() == 1 &&
                   external.required_target_attributions.front()
                                   .required_target.package_name ==
                           "shared-library" &&
                   external.affected_update_plan_indices ==
                   std::vector<std::size_t>({0, 1}) &&
                   external.affected_roots ==
                           std::vector<RootTargetIdentity>({
                                   {0, "first-root"},
                                   {1, "second-root"}}),
           "Shared external unit lost multi-root attribution");

    enqueue_installed(prepared, config, 2);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::Updated,
             AurUpdateOperationTargetStatus::Updated},
            "shared external roots");
    expect_success_lifecycle(
            execution_stub::call_history(),
            {"first-root", "second-root"}, config,
            "shared external roots");
    execution_stub::require_script_consumed();
}

void test_query_recoverable_failure_is_retained_and_blocks_mutation() {
    reset_stubs();
    query_stub::enqueue_info_many_failure("fixture AUR transport failure");
    AurUpdateQueryResult query = query_aur_updates_for_foreign_inventory({
            {"query-failed", "1.0-1", InstalledPackageReason::Explicit}});
    query_stub::require_script_consumed();
    expect(query.recoverable_failures.size() == 1 &&
                   query.recoverable_failures[0].package_names ==
                           std::vector<std::string>{"query-failed"} &&
                   query.plan.entries[0].classification ==
                           AurUpdateClassification::MetadataUnavailable,
           "Query recoverable failure was not typed into the query result");

    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    std::move(query), {}, config);
    expect(prepared.is_blocked(),
           "Recoverable query failure did not block incomplete planning");
    expect(has_planner_issue(
                   prepared.target_and_build_unit_plan(),
                   UpgradeAllPlanningIssueKind::IncompleteAurTarget),
           "Query failure did not retain its planning issue");
    expect(preflight_stub::resolver_call_count() == 0,
           "Incomplete query target reached BuildPlan resolver");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.has_query_failure() && result.has_planning_issue() &&
                   !result.is_success(),
           "Query/planning failure helpers differ");
    expect(result.query_result.recoverable_failures[0].diagnostic ==
                   "fixture AUR transport failure",
           "Query failure diagnostic was lost");
    expect_no_mutation("query recoverable failure");
}

void test_planner_issue_blocks_mutation_but_keeps_disposition() {
    reset_stubs();
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({unavailable_entry("incomplete-target")}),
                    {}, config);
    expect(prepared.is_blocked(), "Incomplete planner target was not blocked");
    expect(prepared.target_and_build_unit_plan()
                                   .target_dispositions.size() == 1 &&
                   prepared.target_and_build_unit_plan()
                                   .target_dispositions[0]
                                   .disposition ==
                           UpgradeAllTargetDisposition::IdentityIncomplete &&
                   has_planner_issue(
                           prepared.target_and_build_unit_plan(),
                           UpgradeAllPlanningIssueKind::IncompleteAurTarget),
           "Planner issue lost known target disposition");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.has_planning_issue() && !result.is_success(),
           "Planner issue was rounded to success");
    expect_no_mutation("planner issue");
}

void test_initial_preflight_identity_blocker_stops_before_mutation() {
    reset_stubs();
    BuildPlan plan = root_plan({{"identity-root", "identity-root"}});
    plan.root_targets[0].requested_name = "wrong-root";
    plan.package_targets[0].roots[0] = plan.root_targets[0];
    return_build_plan(std::move(plan), {"identity-root"});

    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("identity-root")}), {}, config);
    expect(prepared.is_blocked(),
           "Initial preflight identity mismatch was not blocked");
    expect(has_operation_issue(
                   prepared.operation_issues(),
                   FilteredAurUpdateOperationIssueKind::
                           BuildPlanRootIndexMissing),
           "Initial preflight mismatch did not retain missing root mapping");
    expect(has_operation_issue(
                   prepared.operation_issues(),
                   FilteredAurUpdateOperationIssueKind::
                           PreflightInvocationIdentityMismatch),
           "Initial preflight mismatch lost typed invocation identity issue");
    expect(prepared.execution_preflight().targets[0].status ==
                   AurUpdateExecutionTargetStatus::Incomplete,
           "Initial preflight mismatch did not remain typed incomplete");
    expect(preparation_stub::strict_preference_read_history().empty() &&
                   preparation_stub::database_call_count() == 0,
           "Preflight blocker crossed preparation external boundaries");

    static_cast<void>(execute_prepared_filtered_aur_update_operation(
            std::move(prepared), config));
    expect_no_mutation("initial preflight identity blocker");
}

void test_preflight_blocker_stops_before_mutation() {
    reset_stubs();
    BuildPlan plan = root_plan({{"blocked-root", "blocked-root"}});
    plan.unresolved.push_back("missing-dependency>=2");
    return_build_plan(std::move(plan), {"blocked-root"});

    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("blocked-root")}), {}, config);
    expect(prepared.is_blocked() &&
                   prepared.source_build_preparation()->is_blocked(),
           "Preflight blocker did not block preparation");
    expect(preparation_stub::strict_preference_read_history().empty() &&
                   preparation_stub::database_call_count() == 0,
           "Preflight blocker reached strict reader or Pacman DB");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(!result.is_success() &&
                   result.reduced_operation_result.has_blocking_targets(),
           "Preflight blocker was rounded to success");
    expect_no_mutation("preflight blocker");
}

void test_preparation_blocker_stops_before_mutation() {
    reset_stubs();
    return_build_plan(
            root_plan({{"preparation-root", "preparation-root"}}),
            {"preparation-root"});
    preparation_stub::fail_supported_options_guard(
            "fixture option guard failure");

    AppConfig config;
    config.rm_deps = true;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("preparation-root")}),
                    {}, config);
    expect(prepared.is_blocked() &&
                   prepared.source_build_preparation()->is_blocked() &&
                   !prepared.source_build_preparation()->issues.empty(),
           "Preparation option blocker did not fail closed");
    expect(preparation_stub::supported_options_guard_history() ==
                   std::vector<bool>{true},
           "Preparation blocker lost option propagation");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(!result.is_success() &&
                   !result.reduced_operation_result.preparation_issues.empty(),
           "Preparation blocker was rounded to success");
    expect_no_mutation("preparation blocker");
}

void test_all_updated() {
    reset_stubs();
    return_build_plan(
            root_plan({{"updated-a", "updated-a"},
                       {"updated-b", "updated-b"}}),
            {"updated-a", "updated-b"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("updated-a"),
                            update_entry("updated-b")}),
                    {}, config);
    enqueue_installed(prepared, config, 2);

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && result.changed_package_state() &&
                   !result.has_partial_completion(),
           "All-updated helper semantics differ");
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::Updated,
             AurUpdateOperationTargetStatus::Updated},
            "all updated");
    expect_success_lifecycle(
            execution_stub::call_history(),
            {"updated-a", "updated-b"}, config, "all updated");
    execution_stub::require_script_consumed();
}

void test_all_no_change() {
    reset_stubs();
    return_build_plan(
            root_plan({{"same-a", "same-a"}, {"same-b", "same-b"}}),
            {"same-a", "same-b"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("same-a"),
                            update_entry("same-b")}),
                    {}, config);
    enqueue_no_change(prepared, config, 2);

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(result.is_success() && !result.changed_package_state(),
           "All-NoChange helper semantics differ");
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::NoChange,
             AurUpdateOperationTargetStatus::NoChange},
            "all no-change");
    execution_stub::require_script_consumed();
}

void test_same_package_base_children_execute_once_with_mixed_outcomes() {
    reset_stubs();
    return_build_plan(
            root_plan({
                    {"split-runtime", "split-suite"},
                    {"split-cli", "split-suite"}}),
            {"split-runtime", "split-cli"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry(
                                    "split-runtime", "split-suite",
                                    InstalledPackageReason::Dependency),
                            update_entry("split-cli", "split-suite")}),
                    {}, config);
    expect(
            prepared.is_prepared(),
            "Same-PackageBase targets did not prepare" +
                    prepared_diagnostic(prepared));
    expect(
            prepared.target_and_build_unit_plan().selected_targets.size() ==
                            2 &&
                    prepared.target_and_build_unit_plan()
                                    .selected_build_units.size() == 1 &&
                    require_production_invocation(prepared)
                                    .work_items.size() == 1,
            "Same-PackageBase targets did not compact to one work item");

    const ProductionSourceBuildWorkItem& work_item =
            require_production_invocation(prepared).work_items.front();
    expect(
            work_item.request.package_name.empty() &&
                    work_item.request.checkout_name == "split-suite" &&
                    work_item.required_targets.size() == 2 &&
                    work_item.required_targets[0].package_name ==
                            "split-runtime" &&
                    work_item.required_targets[0].desired_reason ==
                            DesiredInstallReason::Dependency &&
                    work_item.required_targets[1].package_name ==
                            "split-cli" &&
                    work_item.required_targets[1].desired_reason ==
                            DesiredInstallReason::Explicit &&
                    preparation_stub::strict_preference_read_history() ==
                            std::vector<std::string>{"split-suite"},
            "Same-PackageBase work-item identity, reason, or preference differs");

    execution_stub::enqueue_success(
            expected_execution_at(prepared, 0, config),
            "split-suite",
            {
                    PackageBaseSourceBuildSelectedResult{
                            ArtifactPackageIdentity{
                                    "split-runtime", "2.0-1"},
                            DesiredInstallReason::Dependency,
                            ArtifactInstallExecutionOutcome::Installed},
                    PackageBaseSourceBuildSelectedResult{
                            ArtifactPackageIdentity{
                                    "split-cli", "2.0-1"},
                            DesiredInstallReason::Explicit,
                            ArtifactInstallExecutionOutcome::SkippedAsNeeded},
            },
            {ArtifactPackageIdentity{"split-debug", "2.0-1"}});

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() && result.changed_package_state() &&
                    !result.has_partial_completion(),
            "Same-PackageBase mixed child outcome aggregate differs");
    expect_statuses(
            result,
            {
                    AurUpdateOperationTargetStatus::Updated,
                    AurUpdateOperationTargetStatus::NoChange,
            },
            "same-PackageBase mixed child outcomes");
    expect(
            result.execution.has_value() &&
                    result.execution->work_item_results.size() == 1 &&
                    result.execution->work_item_results.front()
                                    .child_results.size() == 2 &&
                    result.execution->work_item_results.front()
                                    .unselected_artifacts.size() == 1 &&
                    result.execution->work_item_results.front()
                                    .unselected_artifacts.front()
                                    .package_name == "split-debug" &&
                    result.execution->work_item_results.front()
                                    .unselected_artifacts.front()
                                    .full_version == "2.0-1" &&
                    result.selected_target_results[0]
                                    .operation_result
                                    .execution_contributions.size() == 1 &&
                    result.selected_target_results[1]
                                    .operation_result
                                    .execution_contributions.size() == 1,
            "Same-PackageBase child or unselected snapshot was not retained exactly");

    const std::vector<execution_stub::ExecutionCall>& calls =
            execution_stub::call_history();
    expect(
            calls.size() == 1 && calls.front().package_name.empty() &&
                    calls.front().package_base == "split-suite" &&
                    calls.front().plan_package_names ==
                            std::vector<std::string>{
                                    "split-runtime", "split-cli"} &&
                    calls.front().events ==
                            std::vector<execution_stub::EventKind>{
                                    execution_stub::EventKind::Checkout,
                                    execution_stub::EventKind::Build,
                                    execution_stub::EventKind::Install,
                                    execution_stub::EventKind::Cleanup},
            "Same-PackageBase set owner call count or lifecycle differs");
    execution_stub::require_script_consumed();
}

void test_ordinary_failure_partial_completion_and_not_attempted() {
    reset_stubs();
    return_build_plan(
            root_plan({
                    {"first-success", "first-success"},
                    {"middle-failure", "middle-failure"},
                    {"last-pending", "last-pending"}}),
            {"first-success", "middle-failure", "last-pending"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("first-success"),
                            update_entry("middle-failure"),
                            update_entry("last-pending")}),
                    {}, config);
    enqueue_installed(prepared, config, 1);
    execution_stub::enqueue_phase_failure(
            expected_execution_at(prepared, 1, config),
            SeparatedPackageBaseSourceBuildFailurePhase::Build,
            "fixture build failure");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(!result.is_success() && result.changed_package_state() &&
                   result.has_partial_completion() &&
                   result.has_not_attempted_targets(),
           "Ordinary failure helper semantics differ");
    expect(result.reduced_operation_result.status ==
                   AurUpdateOperationStatus::StoppedOnWorkItemFailure,
           "Ordinary failure operation status differs");
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::Updated,
             AurUpdateOperationTargetStatus::Failed,
             AurUpdateOperationTargetStatus::NotAttempted},
            "ordinary failure");
    expect(execution_stub::call_history().size() == 2,
           "Ordinary failure did not fail fast");
    execution_stub::require_script_consumed();
}

void test_cleanup_failure_partial_completion_and_not_attempted() {
    reset_stubs();
    return_build_plan(
            root_plan({{"cleanup-failure", "cleanup-failure"},
                       {"cleanup-pending", "cleanup-pending"}}),
            {"cleanup-failure", "cleanup-pending"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("cleanup-failure"),
                            update_entry("cleanup-pending")}),
                    {}, config);
    const std::string package_base = require_production_invocation(prepared)
            .work_items.front()
            .request.checkout_name;
    execution_stub::enqueue_cleanup_failure(
            expected_execution_at(prepared, 0, config),
            package_base,
            selected_children_at(
                    prepared,
                    0,
                    ArtifactInstallExecutionOutcome::Installed),
            {},
            "fixture cleanup failure");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(!result.is_success() && result.changed_package_state() &&
                   result.has_partial_completion() &&
                   result.has_cleanup_failure() &&
                   result.has_not_attempted_targets(),
           "Cleanup failure helper semantics differ");
    expect(result.reduced_operation_result.status ==
                   AurUpdateOperationStatus::
                           StoppedAfterPackageCleanupFailure,
           "Cleanup failure operation status differs");
    expect_statuses(
            result,
            {AurUpdateOperationTargetStatus::UpdatedCleanupFailed,
             AurUpdateOperationTargetStatus::NotAttempted},
            "cleanup failure");
    expect(execution_stub::call_history().size() == 1,
           "Cleanup failure did not stop subsequent work items");
    execution_stub::require_script_consumed();
}

void test_preflight_invocation_index_out_of_range() {
    reset_stubs();
    BuildPlan plan = root_plan({{"range-root", "range-root"}});
    plan.root_targets[0].invocation_index = 9;
    plan.package_targets[0].roots[0] = plan.root_targets[0];
    return_build_plan(std::move(plan), {"range-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("range-root")}), {}, config);
    expect(prepared.is_blocked(),
           "Out-of-range preflight invocation unexpectedly prepared");
    expect(has_operation_issue(
                   prepared.operation_issues(),
                   FilteredAurUpdateOperationIssueKind::
                           PreflightInvocationIndexOutOfRange),
           "Out-of-range invocation index was not typed during preparation");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(has_operation_issue(
                   result.issues,
                   FilteredAurUpdateOperationIssueKind::
                           PreflightInvocationIndexOutOfRange),
           "Out-of-range invocation index was not typed");
    expect(result.has_planning_issue() && !result.execution.has_value(),
           "Out-of-range invocation index did not block execution");
    expect_no_mutation("out-of-range invocation index");
}

void test_preflight_invocation_identity_mismatch() {
    reset_stubs();
    BuildPlan plan = root_plan({
            {"identity-a", "identity-a"},
            {"identity-b", "identity-b"}});
    plan.root_targets[0].invocation_index = 1;
    plan.package_targets[0].roots[0] = plan.root_targets[0];
    return_build_plan(
            std::move(plan), {"identity-a", "identity-b"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({
                            update_entry("identity-a"),
                            update_entry("identity-b")}),
                    {}, config);
    expect(prepared.is_blocked(),
           "Mismatched preflight invocation unexpectedly prepared");
    expect(has_operation_issue(
                   prepared.operation_issues(),
                   FilteredAurUpdateOperationIssueKind::
                           PreflightInvocationIdentityMismatch),
           "Invocation identity mismatch was not typed during preparation");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(has_operation_issue(
                   result.issues,
                   FilteredAurUpdateOperationIssueKind::
                           PreflightInvocationIdentityMismatch),
           "Invocation identity mismatch was not typed");
    expect(result.has_planning_issue() && !result.execution.has_value(),
           "Invocation identity mismatch did not block execution");
    expect_no_mutation("invocation identity mismatch");
}

void test_build_unit_order_identity_mismatch_blocks_mutation() {
    reset_stubs();
    BuildPlan plan = root_plan({{"correlation-root", "correlation-root"}});
    plan.order[0].package_names = {"wrong-package"};
    return_build_plan(std::move(plan), {"correlation-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("correlation-root")}),
                    {}, config);
    expect(prepared.is_blocked(),
           "Build-unit order identity mismatch unexpectedly prepared");
    expect(has_operation_issue(
                   prepared.operation_issues(),
                   FilteredAurUpdateOperationIssueKind::
                           BuildUnitOrderIdentityMismatch),
           "Build-unit order identity mismatch was not typed");

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(has_operation_issue(
                   result.issues,
                   FilteredAurUpdateOperationIssueKind::
                           BuildUnitOrderIdentityMismatch),
           "Build-unit identity mismatch was not typed");
    expect(!result.execution.has_value() && !result.is_success(),
           "Build-unit identity mismatch did not block execution");
    expect_no_mutation("build-unit correlation mismatch");
}

void test_projection_payload_private_snapshot_drift_blocks_mutation() {
    reset_stubs();
    return_build_plan(
            root_plan({{"payload-root", "payload-root"}}),
            {"payload-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("payload-root")}), {}, config);
    expect(prepared.is_prepared(), "Projection drift fixture did not prepare");

    BuildPlanArtifactTargetProjectionIssue projection_issue{
            BuildPlanArtifactTargetProjectionIssueKind::
                    MissingPlannedPackageTarget,
            std::size_t{0},
            std::size_t{0},
            {0},
            std::string{"payload-root"},
            std::string{"payload-root"},
            {RootTargetIdentity{0, "payload-root"}},
            "Typed projection payload."};
    AurUpdateExecutionIssue issue{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            std::string{"payload-root"},
            std::string{"payload-root"},
            std::nullopt,
            "Projection snapshot issue.",
            projection_issue};

    // Public observerはconstのまま保ち、testだけがowned private snapshotを
    // 意図的に破損してpre-execution firewallを確認する。
    AurUpdateExecutionPreflight& owned_preflight =
            const_cast<AurUpdateExecutionPreflight&>(
                    prepared.execution_preflight());
    AurUpdateSourceBuildPreparation& owned_preparation =
            const_cast<AurUpdateSourceBuildPreparation&>(
                    *prepared.source_build_preparation());
    owned_preflight.targets.front().issues.push_back(issue);
    owned_preparation.affected_update_targets.front().issues.push_back(
            std::move(issue));
    owned_preparation.affected_update_targets.front()
            .issues.back()
            .build_plan_projection_issue->package_target_indices = {1};

    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);

    expect(
            has_operation_issue(
                    result.issues,
                    FilteredAurUpdateOperationIssueKind::
                            PreflightTargetMappingInconsistent),
            "Projection-only private snapshot drift was not typed");
    expect(
            !result.execution.has_value() && result.has_planning_issue(),
            "Projection-only private snapshot drift reached execution");
    expect_no_mutation("projection payload private snapshot drift");
}

void test_prepared_operation_replay_is_rejected() {
    reset_stubs();
    return_build_plan(
            root_plan({{"one-shot-root", "one-shot-root"}}),
            {"one-shot-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("one-shot-root")}), {}, config);
    enqueue_installed(prepared, config, 1);
    FilteredAurUpdateExecutionResult first =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);
    expect(first.is_success(), "First prepared operation execution failed");
    execution_stub::require_script_consumed();

    execution_stub::reset();
    expect_exception<std::logic_error>(
            [&prepared, &config] {
                static_cast<void>(
                        execute_prepared_filtered_aur_update_operation(
                                std::move(prepared), config));
            },
            "prepared filtered operation replay");
    expect_no_mutation("prepared operation replay");
}

void test_reducer_inconsistency_is_retained() {
    reset_stubs();
    return_build_plan(
            root_plan({{"reducer-root", "reducer-root"}}),
            {"reducer-root"});
    const AppConfig config;
    PreparedFilteredAurUpdateOperation prepared =
            prepare_filtered_aur_update_operation(
                    query_result({update_entry("reducer-root")}), {}, config);
    expect(prepared.is_prepared(), "Reducer fixture did not prepare");

    enqueue_installed(prepared, config, 1);
    FilteredAurUpdateExecutionResult result =
            execute_prepared_filtered_aur_update_operation(
                    std::move(prepared), config);

    // reducer入力用のpublic owned result snapshotだけを壊し、actual runnerの
    // known outcomeとreduction issueがどちらも残ることを固定する。
    result.preflight.targets[0].update_plan_index = 8;
    AurUpdateOperationResult inconsistent =
            reduce_aur_update_operation_result(
                    result.preflight, result.preparation, result.execution);
    expect(result.execution.has_value() &&
                   !inconsistent.reduction_issues.empty() &&
                   inconsistent.status ==
                           AurUpdateOperationStatus::InconsistentResult &&
                   !inconsistent.is_success(),
           "Reducer inconsistency was not retained with known execution");
    expect(inconsistent.execution_work_items.size() == 1 &&
                   inconsistent.execution_work_items[0]
                                   .status ==
                           AurUpdateWorkItemExecutionStatus::Updated,
           "Reducer inconsistency lost known execution outcome");
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
        run_case("empty query plan", test_empty_query_plan_is_normal_noop);
        run_case(
                "real query wrapper and empty explicit legacy equivalence",
                test_real_query_wrapper_and_empty_explicit_set_match_legacy_path);
        run_case(
                "explicit inventory query core",
                test_explicit_inventory_query_core_bypasses_inventory_wrapper);
        run_case(
                "package-name target exclusion",
                test_package_name_target_exclusion);
        run_case(
                "PackageBase target exclusion",
                test_package_base_target_exclusion);
        run_case(
                "split-package target exclusion",
                test_split_package_targets_share_exclusion_attribution);
        run_case(
                "original, filtered, and preflight index mapping",
                test_original_filtered_and_preflight_index_mapping);
        run_case(
                "transitive external satisfaction",
                test_transitive_external_satisfaction_keeps_selected_root_executable);
        run_case(
                "multiple-child external satisfaction",
                test_multiple_child_external_satisfaction_keeps_set_snapshot);
        run_case(
                "shared external unit",
                test_one_external_unit_shared_by_multiple_roots);
        run_case(
                "query recoverable failure",
                test_query_recoverable_failure_is_retained_and_blocks_mutation);
        run_case(
                "planner issue no mutation",
                test_planner_issue_blocks_mutation_but_keeps_disposition);
        run_case(
                "initial preflight identity blocker",
                test_initial_preflight_identity_blocker_stops_before_mutation);
        run_case(
                "preflight blocker no mutation",
                test_preflight_blocker_stops_before_mutation);
        run_case(
                "preparation blocker no mutation",
                test_preparation_blocker_stops_before_mutation);
        run_case("all Updated", test_all_updated);
        run_case("all NoChange", test_all_no_change);
        run_case(
                "same PackageBase mixed child outcomes",
                test_same_package_base_children_execute_once_with_mixed_outcomes);
        run_case(
                "ordinary failure, partial completion, and NotAttempted",
                test_ordinary_failure_partial_completion_and_not_attempted);
        run_case(
                "cleanup failure, partial completion, and NotAttempted",
                test_cleanup_failure_partial_completion_and_not_attempted);
        run_case(
                "preflight invocation index out of range",
                test_preflight_invocation_index_out_of_range);
        run_case(
                "preflight invocation identity mismatch",
                test_preflight_invocation_identity_mismatch);
        run_case(
                "build-unit order identity mismatch",
                test_build_unit_order_identity_mismatch_blocks_mutation);
        run_case(
                "projection payload private snapshot drift",
                test_projection_payload_private_snapshot_drift_blocks_mutation);
        run_case(
                "prepared operation replay",
                test_prepared_operation_replay_is_rejected);
        run_case(
                "reducer inconsistency",
                test_reducer_inconsistency_is_retained);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "Filtered AUR update operation tests: all checks passed\n";
    return 0;
}
