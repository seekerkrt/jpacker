#include "app_config.hpp"
#include "artifact_install_executor.hpp"
#include "source_build.hpp"
#include "stubs/upgrade-all-operation/operation_stub.hpp"
#include "upgrade_all_operation.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using UpgradeAllOperationExecutor = UpgradeAllOperationResult (*)(
        PreparedUpgradeAllOperation,
        const AppConfig&);

static_assert(!std::is_default_constructible_v<PreparedUpgradeAllOperation>);
static_assert(!std::is_copy_constructible_v<PreparedUpgradeAllOperation>);
static_assert(std::is_nothrow_move_constructible_v<
              PreparedUpgradeAllOperation>);
static_assert(!std::is_move_assignable_v<PreparedUpgradeAllOperation>);
static_assert(std::is_same_v<
              decltype(&execute_prepared_upgrade_all_operation),
              UpgradeAllOperationExecutor>);
static_assert(std::is_invocable_v<
              UpgradeAllOperationExecutor,
              PreparedUpgradeAllOperation&&,
              const AppConfig&>);
static_assert(!std::is_invocable_v<
              UpgradeAllOperationExecutor,
              PreparedUpgradeAllOperation&,
              const AppConfig&>);

namespace {

namespace fs = std::filesystem;
namespace stub = upgrade_all_operation_test_stub;

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

AppConfig full_option_config() {
    AppConfig config;
    config.no_edit = true;
    config.no_diff = true;
    config.no_confirm = true;
    config.rebuild = true;
    config.clean_build = true;
    config.rm_deps = true;
    config.editor = "upgrade-all-test-editor";
    config.log_file = "/upgrade-all-test/log";
    return config;
}

SourcePreferenceDirectorySnapshot preference_directory(
        const std::vector<std::string>& package_names,
        bool root_exists = true) {
    SourcePreferenceDirectorySnapshot snapshot;
    snapshot.root_exists = root_exists;
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        snapshot.entries.push_back(SourcePreferenceEntrySnapshot{
                index,
                fs::path("/preferences") / package_names[index],
                package_names[index],
                true});
    }
    return snapshot;
}

SourcePreferenceLoaded loaded_preference(
        const std::string& package_name,
        std::vector<std::string> warnings = {}) {
    SourceBuildEnvironment environment;
    environment.ordered_assignments.push_back(
            SourceEnvironmentAssignment{"PACKAGE_NAME", package_name});
    return SourcePreferenceLoaded{
            fs::path("/preferences") / package_name,
            std::move(environment),
            std::move(warnings)};
}

SourcePreferenceFailure preference_failure(
        const std::string& package_name,
        const std::string& diagnostic) {
    return SourcePreferenceFailure{
            SourcePreferenceFailureKind::ReadFailed,
            fs::path("/preferences") / package_name,
            std::make_error_code(std::errc::io_error),
            std::nullopt,
            diagnostic};
}

InstalledPackageMetadata installed_package(
        const std::string& package_name,
        const std::string& version = "1.0-1",
        InstalledPackageReason reason = InstalledPackageReason::Explicit) {
    return InstalledPackageMetadata{package_name, version, reason};
}

stub::MetadataSessionScript metadata_session(
        const std::vector<std::string>& package_names,
        LocalPackageVersionSnapshotResult local_snapshot,
        const std::string& installed_version = "1.0-1") {
    stub::MetadataSessionScript script;
    script.local_package_snapshot = std::move(local_snapshot);
    for(const std::string& package_name : package_names) {
        script.installed_package_results.emplace(
                package_name,
                installed_package(package_name, installed_version));
    }
    return script;
}

SourceBuildExecutionResult source_execution(
        SourceBuildExecutionStatus status,
        std::string diagnostic = {}) {
    SourceBuildExecutionResult result;
    result.status = status;
    result.diagnostic = std::move(diagnostic);
    if(status == SourceBuildExecutionStatus::UpdateStatusUnknownSkipped) {
        result.update_status_unknown_skip_reason =
                SourceBuildUpdateStatusUnknownSkipReason::NoConfirm;
    }
    return result;
}

ResolvedSourceBuildIdentity source_identity(
        const std::string& requested_name,
        const std::string& package_base,
        const std::string& canonical_key) {
    return ResolvedSourceBuildIdentity{
            requested_name,
            package_base,
            canonical_key,
            "https://packages.example/" + package_base + ".git",
            SourceBuildSourceKind::Repository,
            false};
}

void configure_sources(
        const std::vector<std::string>& package_names,
        LocalPackageVersionSnapshot before_snapshot =
                LocalPackageVersionSnapshot{{"core", "1.0-1"}}) {
    stub::set_preference_directory(preference_directory(package_names));
    for(const std::string& package_name : package_names) {
        stub::enqueue_preference_result(
                package_name, loaded_preference(package_name));
    }
    if(!package_names.empty()) {
        stub::enqueue_metadata_session(metadata_session(
                package_names, std::move(before_snapshot)));
    }
}

void enqueue_post_source_metadata(
        const std::vector<std::string>& package_names,
        LocalPackageVersionSnapshot after_snapshot =
                LocalPackageVersionSnapshot{{"core", "1.0-1"}}) {
    stub::enqueue_metadata_session(metadata_session(
            package_names, std::move(after_snapshot)));
}

PreparedUpgradeAllOperation take_prepared(
        UpgradeAllOperationPreparation preparation,
        const std::string& context) {
    if(!std::holds_alternative<PreparedUpgradeAllOperation>(preparation)) {
        const UpgradeAllOperationResult& blocked =
                std::get<UpgradeAllOperationResult>(preparation);
        std::string detail;
        if(!blocked.system_source.issues.empty()) {
            detail = ": " +
                    blocked.system_source.issues.front().diagnostic;
        } else if(!blocked.issues.empty()) {
            detail = ": " + blocked.issues.front().diagnostic;
        }
        throw std::runtime_error(
                context + ": preparation unexpectedly blocked" + detail);
    }
    return std::move(std::get<PreparedUpgradeAllOperation>(preparation));
}

UpgradeAllOperationResult take_blocked(
        UpgradeAllOperationPreparation preparation,
        const std::string& context) {
    expect(
            std::holds_alternative<UpgradeAllOperationResult>(preparation),
            context + ": preparation unexpectedly returned a capability");
    return std::move(std::get<UpgradeAllOperationResult>(preparation));
}

PreparedUpgradeAllOperation prepare_sources(
        const std::vector<std::string>& package_names,
        const AppConfig& config,
        LocalPackageVersionSnapshot before_snapshot =
                LocalPackageVersionSnapshot{{"core", "1.0-1"}}) {
    configure_sources(package_names, std::move(before_snapshot));
    return take_prepared(
            prepare_upgrade_all_operation(config), "registered sources");
}

AurPackageInfo package_info(
        const std::string& package_name,
        const std::string& package_base = {},
        const std::string& version = "2.0-1") {
    AurPackageInfo info;
    info.Name = package_name;
    info.PackageBase = package_base.empty() ? package_name : package_base;
    info.Version = version;
    info.Description = "upgrade-all operation fixture";
    info.Maintainer = "moguet-test";
    return info;
}

ForeignPackageInventory foreign_inventory(
        const std::vector<std::string>& package_names,
        const std::string& version = "1.0-1") {
    ForeignPackageInventory inventory;
    for(const std::string& package_name : package_names) {
        inventory.push_back(installed_package(package_name, version));
    }
    return inventory;
}

void enqueue_aur_query(
        const std::vector<std::pair<std::string, std::string>>& packages,
        const std::string& comparison = "1") {
    std::map<std::string, AurPackageInfo> response;
    for(const auto& [package_name, package_base] : packages) {
        response.emplace(
                package_name,
                package_info(package_name, package_base));
    }
    stub::enqueue_info_many_result(std::move(response));
    for(std::size_t index = 0; index < packages.size(); ++index) {
        stub::enqueue_vercmp_result(comparison);
    }
}

struct RootSpec {
    std::string package_name;
    std::string package_base;
};

template<typename Value>
void append_unique(std::vector<Value>& values, const Value& value) {
    if(std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

PlannedPackageTarget* find_package_target(
        BuildPlan& plan,
        const std::string& package_name) {
    auto found = std::find_if(
            plan.package_targets.begin(), plan.package_targets.end(),
            [&package_name](const PlannedPackageTarget& target) {
                return target.package_name == package_name;
            });
    return found == plan.package_targets.end() ? nullptr : &*found;
}

const PlannedPackageTarget& require_package_target(
        const BuildPlan& plan,
        const std::string& package_name) {
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
                "BuildPlan dependency PackageBase differs");
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
        BuildPlan plan,
        std::vector<std::string> expected_targets) {
    stub::set_resolver_handler(
            [plan = std::move(plan),
             expected_targets = std::move(expected_targets)](
                    const std::vector<std::string>& targets) {
                if(targets != expected_targets) {
                    throw std::logic_error(
                            "Aggregate resolver target order differs");
                }
                return plan;
            });
}

bool has_issue(
        const UpgradeAllOperationResult& result,
        UpgradeAllOperationIssueKind kind) {
    return std::any_of(
            result.issues.begin(), result.issues.end(),
            [kind](const UpgradeAllOperationIssue& issue) {
                return issue.kind == kind;
            });
}

std::size_t event_position(
        stub::EventKind kind,
        const std::string& subject,
        std::size_t occurrence = 0) {
    std::size_t seen = 0;
    const auto& events = stub::event_history();
    for(std::size_t index = 0; index < events.size(); ++index) {
        if(events[index].kind != kind || events[index].subject != subject) {
            continue;
        }
        if(seen == occurrence) return index;
        ++seen;
    }
    throw std::runtime_error(
            "Missing event " + subject + " occurrence " +
            std::to_string(occurrence));
}

void expect_aur_not_attempted(
        const UpgradeAllOperationResult& result,
        UpgradeAllNotAttemptedReason reason,
        const std::string& context) {
    expect(
            result.aur.status == UpgradeAllAurPhaseStatus::NotAttempted &&
                    result.aur.not_attempted_reason == reason,
            context + ": AUR NotAttempted reason differs");
    expect(
            !result.aur.operation_result.has_value(),
            context + ": unattempted AUR phase gained a normal result");
}

void expect_no_inventory_or_aur(const std::string& context) {
    expect(
            stub::repository_configuration_calls() == 0 &&
                    stub::inventory_calls() == 0,
            context + ": foreign inventory boundary was crossed");
    expect(
            stub::info_many_call_history().empty() &&
                    stub::resolver_call_count() == 0 &&
                    stub::aur_execution_calls().empty(),
            context + ": AUR boundary was crossed");
}

void test_empty_source_preparation_snapshot() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const AppConfig config = full_option_config();

    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config), "empty source");
    const UpgradeAllOperationPreparedSnapshot* snapshot = prepared.snapshot();
    expect(snapshot != nullptr, "Prepared aggregate snapshot is unavailable");
    expect(
            snapshot->system_source.preference_root_exists &&
                    snapshot->system_source.registered_sources.empty() &&
                    snapshot->explicit_source_adapter.entries.empty() &&
                    snapshot->explicit_source_adapter.is_valid(),
            "Empty registered-source intent changed during preparation");
    expect(
            snapshot->system_source.options.no_edit == config.no_edit &&
                    snapshot->system_source.options.rm_deps ==
                            config.rm_deps &&
                    snapshot->system_source.options.editor == config.editor,
            "Aggregate option fingerprint was not frozen");
    stub::require_script_consumed();
}

void test_strict_preference_absence_blocks_without_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({"missing-source"}));
    stub::enqueue_preference_result(
            "missing-source", SourcePreferenceAbsent{});

    UpgradeAllOperationResult result = take_blocked(
            prepare_upgrade_all_operation(AppConfig{}),
            "strict preference absence");
    expect(
            result.status ==
                            UpgradeAllOperationStatus::BlockedBeforeMutation &&
                    result.system_source.status ==
                            SystemSourceUpgradeStatus::BlockedBeforeMutation &&
                    result.system_source.registered_source_results.size() == 1,
            "Strict preference absence did not retain blocked source result");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::PreparationBlocked,
            "strict preference absence");
    expect(stub::system_commands().empty(),
           "Strict preference absence executed system mutation");
    expect_no_inventory_or_aur("strict preference absence");
    stub::require_script_consumed();
}

void test_strict_preference_read_failure_blocks_without_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({"broken-source"}));
    stub::enqueue_preference_result(
            "broken-source",
            preference_failure(
                    "broken-source", "fixture strict read failure"));

    UpgradeAllOperationResult result = take_blocked(
            prepare_upgrade_all_operation(AppConfig{}),
            "strict preference read failure");
    expect(
            result.status ==
                            UpgradeAllOperationStatus::BlockedBeforeMutation &&
                    !result.system_source.issues.empty() &&
                    result.system_source.issues.front().
                                    source_preference_failure.has_value(),
            "Strict read failure lost typed preparation detail");
    expect_no_inventory_or_aur("strict preference read failure");
    stub::require_script_consumed();
}

void test_system_source_preparation_blocker_is_aggregate_blocker() {
    stub::reset();
    stub::set_preference_directory(preference_directory({"source-root"}));
    stub::enqueue_preference_result(
            "source-root", loaded_preference("source-root"));
    stub::fail_source_identity(
            "source-root", "fixture identity resolution failure");

    UpgradeAllOperationResult result = take_blocked(
            prepare_upgrade_all_operation(AppConfig{}),
            "system/source preparation blocker");
    expect(
            result.status ==
                            UpgradeAllOperationStatus::BlockedBeforeMutation &&
                    result.stopped_phase ==
                            UpgradeAllOperationPhase::Preparation &&
                    result.system_source.has_blocking_issue(),
            "Nested preparation blocker was not preserved");
    expect_no_inventory_or_aur("system/source preparation blocker");
    stub::require_script_consumed();
}

void test_option_mismatch_rejected_before_system_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const AppConfig prepared_config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(prepared_config),
            "option mismatch fixture");
    AppConfig execution_config = prepared_config;
    execution_config.no_confirm = true;

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), execution_config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    has_issue(
                            result,
                            UpgradeAllOperationIssueKind::
                                    OptionSnapshotMismatch) &&
                    result.has_inconsistency(),
            "Aggregate option mismatch was not typed");
    expect(stub::system_commands().empty(),
           "Option mismatch crossed system mutation boundary");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::PriorAggregateInconsistency,
            "option mismatch");
    stub::require_script_consumed();
}

void test_capability_replay_rejected_before_second_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory({});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config), "replay fixture");

    UpgradeAllOperationResult first =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(first.is_success(), "First aggregate execution failed");
    const std::size_t command_count = stub::system_commands().size();

    UpgradeAllOperationResult replay =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            replay.status == UpgradeAllOperationStatus::InconsistentResult &&
                    has_issue(
                            replay,
                            UpgradeAllOperationIssueKind::
                                    PreparedCapabilityConsumed),
            "Aggregate capability replay was not typed");
    expect(
            stub::system_commands().size() == command_count,
            "Aggregate replay performed a second system mutation");
    stub::require_script_consumed();
}

void test_outer_source_snapshot_mismatch_is_rejected() {
    stub::reset();
    const AppConfig config;
    PreparedUpgradeAllOperation prepared =
            prepare_sources({"source-root"}, config);
    prepared.make_source_snapshot_inconsistent_for_test();

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            has_issue(
                    result,
                    UpgradeAllOperationIssueKind::SourceSnapshotMismatch) &&
                    result.has_inconsistency(),
            "Outer/nested source snapshot mismatch was not rejected");
    expect(stub::system_commands().empty(),
           "Source snapshot mismatch executed system mutation");
    stub::require_script_consumed();
}

void test_explicit_source_adapter_correlation_mismatch_is_rejected() {
    stub::reset();
    const AppConfig config;
    PreparedUpgradeAllOperation prepared =
            prepare_sources({"source-root"}, config);
    prepared.make_explicit_source_correlation_inconsistent_for_test();

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            has_issue(
                    result,
                    UpgradeAllOperationIssueKind::
                            ExplicitSourceCorrelationInconsistent) &&
                    result.has_inconsistency(),
            "Explicit source adapter mismatch was not rejected");
    expect(stub::system_commands().empty(),
           "Explicit adapter mismatch executed system mutation");
    stub::require_script_consumed();
}

void test_nested_system_source_correlation_mismatch_is_rejected() {
    stub::reset();
    const AppConfig config;
    PreparedUpgradeAllOperation prepared =
            prepare_sources({"source-root"}, config);
    prepared.make_nested_system_source_correlation_inconsistent_for_test();

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.system_source.status ==
                            SystemSourceUpgradeStatus::InconsistentResult &&
                    result.has_inconsistency(),
            "Nested PR2 correlation mismatch was not retained");
    expect(stub::system_commands().empty(),
           "Nested PR2 mismatch executed system mutation");
    expect_no_inventory_or_aur("nested PR2 mismatch");
    stub::require_script_consumed();
}

void test_unexpected_exception_after_system_start_is_not_unattempted() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "unexpected system-start exception fixture");
    prepared.set_nested_system_source_unexpected_exception_for_test(
            SystemSourceUpgradeUnexpectedExceptionPoint::SystemPhaseStarted);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.stopped_phase ==
                            UpgradeAllOperationPhase::System &&
                    result.system_source.system.status ==
                            SystemUpgradePhaseStatus::Failed &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::Unknown &&
                    result.system_source.system.diagnostic.has_value() &&
                    result.system_source.system.diagnostic->find(
                            "System result unavailable after phase started") !=
                            std::string::npos,
            "System-start exception was falsely reported as NotAttempted");
    expect(
            stub::system_commands().empty(),
            "System-start test hook crossed the system mutation boundary");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::PriorAggregateInconsistency,
            "unexpected system-start exception");
    expect_no_inventory_or_aur("unexpected system-start exception");
    stub::require_script_consumed();
}

void test_unexpected_exception_preserves_system_completion() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "unexpected post-system exception fixture");
    prepared.set_nested_system_source_unexpected_exception_for_test(
            SystemSourceUpgradeUnexpectedExceptionPoint::SystemPhaseCompleted);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.system_source.system.status ==
                            SystemUpgradePhaseStatus::Completed &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::Unknown &&
                    result.has_partial_completion(),
            "Post-system exception discarded known system completion");
    expect(
            stub::system_commands().size() == 1,
            "Post-system exception fixture did not complete system command");
    expect_no_inventory_or_aur("unexpected post-system exception");
    stub::require_script_consumed();
}

void test_unexpected_exception_after_source_start_is_incomplete() {
    stub::reset();
    const std::vector<std::string> sources = {"source-started"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    prepared.set_nested_system_source_unexpected_exception_for_test(
            SystemSourceUpgradeUnexpectedExceptionPoint::SourceWorkItemStarted,
            true);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    const RegisteredSourceUpgradeResult& source =
            result.system_source.registered_source_results.front();
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.stopped_phase ==
                            UpgradeAllOperationPhase::RegisteredSource &&
                    result.system_source.system.status ==
                            SystemUpgradePhaseStatus::Completed &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::NoChange &&
                    source.status ==
                            RegisteredSourceUpgradeStatus::Incomplete &&
                    source.failure_kind ==
                            RegisteredSourceUpgradeFailureKind::
                                    UnknownException &&
                    source.diagnostic.has_value() &&
                    source.diagnostic->find(
                            "Registered source result unavailable after phase started") !=
                            std::string::npos &&
                    result.has_partial_completion(),
            "Source-start exception was falsely reported as NotAttempted");
    expect(
            stub::source_execution_calls().empty(),
            "Source-start test hook crossed the source mutation boundary");
    expect_no_inventory_or_aur("unexpected source-start exception");
    stub::require_script_consumed();
}

void test_unexpected_exception_preserves_recorded_source_result() {
    stub::reset();
    const std::vector<std::string> sources = {
            "source-recorded", "source-pending"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::Installed));
    prepared.set_nested_system_source_unexpected_exception_for_test(
            SystemSourceUpgradeUnexpectedExceptionPoint::SourceResultRecorded);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::Updated &&
                    result.system_source.registered_source_results[0].
                                    package_state_change ==
                            PackageStateChange::Changed &&
                    result.system_source.registered_source_results[1].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.package_state_change() ==
                            PackageStateChange::Changed &&
                    result.has_partial_completion(),
            "Unexpected exception discarded a recorded source result");
    expect(
            stub::source_execution_calls().size() == 1,
            "Recorded-source exception executed a later source");
    expect_no_inventory_or_aur("unexpected recorded-source exception");
    stub::require_script_consumed();
}

void test_system_failure_is_fail_fast() {
    stub::reset();
    const AppConfig config;
    PreparedUpgradeAllOperation prepared =
            prepare_sources({"source-root"}, config);
    stub::set_system_command_exit_status(7);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedOnSystemFailure &&
                    result.system_source.system.status ==
                            SystemUpgradePhaseStatus::Failed &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.has_not_attempted_phase() &&
                    !result.has_partial_completion(),
            "System failure aggregate state differs");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::SystemFailure,
            "system failure");
    expect(stub::source_execution_calls().empty(),
           "System failure started source mutation");
    expect_no_inventory_or_aur("system failure");
    stub::require_script_consumed();
}

void test_first_source_failure_stops_remaining_and_aur() {
    stub::reset();
    const std::vector<std::string> sources = {"first", "second", "third"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_failure("fixture first source failure");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedOnSourceFailure &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::Failed &&
                    result.system_source.registered_source_results[1].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.system_source.registered_source_results[2].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.has_partial_completion() &&
                    result.has_not_attempted_phase(),
            "First source failure did not preserve fail-fast results");
    expect(
            stub::source_execution_calls().size() == 1,
            "First source failure executed a later source");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::SourceFailure,
            "first source failure");
    expect_no_inventory_or_aur("first source failure");
    stub::require_script_consumed();
}

void test_later_source_failure_preserves_prior_update() {
    stub::reset();
    const std::vector<std::string> sources = {"updated-first", "failed-second",
                                               "pending-third"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::Installed));
    stub::enqueue_source_failure("fixture later source failure");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::Updated &&
                    result.system_source.registered_source_results[1].status ==
                            RegisteredSourceUpgradeStatus::Failed &&
                    result.system_source.registered_source_results[2].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.package_state_change() ==
                            PackageStateChange::Changed &&
                    result.has_partial_completion(),
            "Later source failure lost prior source completion");
    expect_no_inventory_or_aur("later source failure");
    stub::require_script_consumed();
}

void test_source_cleanup_failure_stops_before_inventory() {
    stub::reset();
    const std::vector<std::string> sources = {"cleanup-source", "pending"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_cleanup_failure(
            ArtifactInstallExecutionOutcome::Installed,
            "fixture source cleanup failure");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::
                                     StoppedAfterSourceCleanupFailure &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::
                                    UpdatedCleanupFailed &&
                    result.system_source.registered_source_results[1].status ==
                            RegisteredSourceUpgradeStatus::NotAttempted &&
                    result.has_cleanup_failure() &&
                    result.has_partial_completion() &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "Source cleanup failure lost post-transaction state");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::SourceCleanupFailure,
            "source cleanup failure");
    expect_no_inventory_or_aur("source cleanup failure");
    stub::require_script_consumed();
}

void test_foreign_inventory_configuration_failure() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_repository_configuration_failure(PackageMetadataFailure{
            PackageMetadataErrorCode::ConfigurationMalformed,
            "fixture repository configuration failure"});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "inventory configuration failure fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedBeforeAurExecution &&
                    result.foreign_inventory.status ==
                            UpgradeAllForeignInventoryPhaseStatus::Failed &&
                    has_issue(
                            result,
                            UpgradeAllOperationIssueKind::
                                    ForeignInventoryConfigurationFailed) &&
                    result.has_query_failure(),
            "Repository configuration failure was not typed");
    expect(stub::inventory_calls() == 0,
           "Configuration failure called foreign inventory reader");
    expect(stub::info_many_call_history().empty(),
           "Configuration failure called AUR query");
    stub::require_script_consumed();
}

void test_foreign_inventory_read_failure() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory_failure(PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "fixture foreign inventory failure"});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "inventory read failure fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.foreign_inventory.status ==
                            UpgradeAllForeignInventoryPhaseStatus::Failed &&
                    result.foreign_inventory.failure.has_value() &&
                    has_issue(
                            result,
                            UpgradeAllOperationIssueKind::
                                    ForeignInventoryReadFailed) &&
                    result.has_query_failure() &&
                    result.has_partial_completion(),
            "Foreign inventory read failure lost typed detail");
    expect_aur_not_attempted(
            result,
            UpgradeAllNotAttemptedReason::ForeignInventoryFailure,
            "inventory read failure");
    expect(stub::info_many_call_history().empty(),
           "Inventory read failure called AUR query");
    stub::require_script_consumed();
}

void test_recoverable_aur_query_failure_blocks_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory(foreign_inventory({"recoverable-root"}));
    stub::enqueue_info_many_failure("fixture recoverable AUR failure");
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "recoverable query fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedBeforeAurExecution &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::BlockedBeforeExecution &&
                    result.aur.operation_result.has_value() &&
                    result.has_query_failure() &&
                    result.has_planning_issue() &&
                    result.has_partial_completion(),
            "Recoverable AUR query failure was not retained");
    expect(stub::aur_execution_calls().empty(),
           "Recoverable AUR failure reached mutation");
    stub::require_script_consumed();
}

void test_total_aur_query_failure_blocks_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory(foreign_inventory({"fatal-root"}));
    stub::enqueue_info_many_response_failure(
            "fixture fatal AUR response failure");
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "total query failure fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.stopped_phase == UpgradeAllOperationPhase::AurQuery &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::BlockedBeforeExecution &&
                    !result.aur.operation_result.has_value() &&
                    has_issue(
                            result,
                            UpgradeAllOperationIssueKind::AurQueryFailed) &&
                    result.has_query_failure(),
            "Fatal AUR query failure was not typed by aggregate");
    expect(stub::resolver_call_count() == 0 &&
                   stub::aur_execution_calls().empty(),
           "Fatal AUR query failure crossed planning/mutation boundary");
    stub::require_script_consumed();
}

void test_planner_conflict_blocks_before_preflight() {
    stub::reset();
    const std::vector<std::string> sources = {"source-a", "source-b"};
    configure_sources(sources);
    stub::set_source_identity(
            "source-a",
            source_identity("source-a", "source-a", "source://shared"));
    stub::set_source_identity(
            "source-b",
            source_identity("source-b", "source-b", "source://shared"));
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "planner conflict fixture");
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory(foreign_inventory({"aur-root"}));
    enqueue_aur_query({{"aur-root", "aur-root"}});
    return_build_plan(root_plan({{"aur-root", "aur-root"}}), {"aur-root"});

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.aur.operation_result.has_value(),
            "Explicit source identity conflict lost filtered result: status=" +
                    std::to_string(static_cast<int>(result.status)) +
                    " issues=" + std::to_string(result.issues.size()));
    expect(
            result.has_planning_issue(),
            "Explicit source identity conflict lost planning issue: planner=" +
                    std::to_string(
                            result.aur.operation_result->upgrade_all_plan.
                                    issues.size()));
    expect(!result.is_success(),
           "Explicit source identity conflict was rounded to success");
    expect(stub::resolver_call_count() == 1 &&
                   stub::aur_execution_calls().empty(),
           "Planner issue did not stop before AUR mutation");
    stub::require_script_consumed();
}

void test_mapping_issue_blocks_aur_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory(foreign_inventory({"mapping-root"}));
    enqueue_aur_query({{"mapping-root", "mapping-root"}});
    BuildPlan plan = root_plan({{"mapping-root", "mapping-root"}});
    plan.root_targets[0].requested_name = "wrong-root";
    plan.package_targets[0].roots[0] = plan.root_targets[0];
    return_build_plan(std::move(plan), {"mapping-root"});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "mapping issue fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::InconsistentResult &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::InconsistentResult &&
                    result.has_inconsistency(),
            "Filtered mapping issue was not retained as inconsistency");
    expect(stub::aur_execution_calls().empty(),
           "Mapping issue reached AUR mutation");
    stub::require_script_consumed();
}

void test_preflight_blocker_stops_before_preparation_io() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory(foreign_inventory({"blocked-root"}));
    enqueue_aur_query({{"blocked-root", "blocked-root"}});
    BuildPlan plan = root_plan({{"blocked-root", "blocked-root"}});
    plan.unresolved.push_back("missing-dependency>=2");
    return_build_plan(std::move(plan), {"blocked-root"});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "preflight blocker fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedBeforeAurExecution &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::BlockedBeforeExecution &&
                    result.aur.operation_result.has_value() &&
                    !result.is_success(),
            "Preflight blocker was rounded to success");
    expect(stub::database_call_count() == 0 &&
                   stub::aur_execution_calls().empty(),
           "Preflight blocker crossed AUR preparation/mutation boundary");
    stub::require_script_consumed();
}

void test_preparation_blocker_stops_before_aur_mutation() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory(foreign_inventory({"preparation-root"}));
    enqueue_aur_query({{"preparation-root", "preparation-root"}});
    return_build_plan(
            root_plan({{"preparation-root", "preparation-root"}}),
            {"preparation-root"});
    stub::fail_supported_options_guard("fixture AUR option guard failure");
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "AUR preparation blocker fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::
                                    StoppedBeforeAurExecution &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::BlockedBeforeExecution &&
                    result.aur.operation_result.has_value() &&
                    !result.aur.operation_result->reduced_operation_result.
                            preparation_issues.empty(),
            "AUR preparation blocker lost typed detail");
    expect(stub::aur_execution_calls().empty(),
           "AUR preparation blocker reached mutation");
    stub::require_script_consumed();
}

void test_no_updates_success_contract() {
    stub::reset();
    const std::vector<std::string> sources = {"same-source"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory({});

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::NoUpdates &&
                    result.is_success() &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::NoChange &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::NoChange &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::NoUpdates &&
                    result.package_state_change() ==
                            PackageStateChange::NoChange &&
                    !result.has_partial_completion() &&
                    !result.has_not_attempted_phase(),
            "NoUpdates conjunction was not enforced");
    stub::require_script_consumed();
}

void test_system_unknown_does_not_reduce_to_no_updates() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    stub::set_foreign_inventory({});
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "system Unknown fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::Completed &&
                    result.is_success() &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::Unknown &&
                    result.package_state_change() ==
                            PackageStateChange::Unknown,
            "Unknown system package state was rounded to NoUpdates");
    stub::require_script_consumed();
}

void test_system_changed_only() {
    stub::reset();
    const std::vector<std::string> sources = {"same-source"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(
            sources,
            config,
            LocalPackageVersionSnapshot{{"core", "1.0-1"}});
    enqueue_post_source_metadata(
            sources,
            LocalPackageVersionSnapshot{{"core", "2.0-1"}});
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory({});

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::Completed &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::Changed &&
                    result.system_source.registered_source_results[0].
                                    package_state_change ==
                            PackageStateChange::NoChange &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "System-only package change was not aggregated");
    stub::require_script_consumed();
}

void test_source_updated_only() {
    stub::reset();
    const std::vector<std::string> sources = {"updated-source"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::Installed));
    stub::set_foreign_inventory({});

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::NoChange &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::Updated &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "Source-only package change was not aggregated");
    stub::require_script_consumed();
}

void test_aur_updated_only_and_fresh_inventory() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "fresh inventory fixture");

    // POLICY(#281): preparation後に差し替えたinventoryだけがAUR queryへ届く。
    stub::set_foreign_inventory(foreign_inventory({"latest-root"}));
    enqueue_aur_query({{"latest-root", "latest-root"}});
    return_build_plan(
            root_plan({{"latest-root", "latest-root"}}),
            {"latest-root"});
    stub::enqueue_aur_success(ArtifactInstallExecutionOutcome::Installed);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() &&
                    result.foreign_inventory.inventory.size() == 1 &&
                    result.foreign_inventory.inventory[0].name ==
                            "latest-root" &&
                    result.foreign_inventory.inventory[0].version ==
                            "1.0-1" &&
                    result.foreign_inventory.inventory[0].reason ==
                            InstalledPackageReason::Explicit &&
                    stub::info_many_call_history() ==
                            std::vector<std::vector<std::string>>{
                                    {"latest-root"}} &&
                    result.aur.status ==
                            UpgradeAllAurPhaseStatus::Completed &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "Fresh foreign inventory did not drive AUR update");
    expect(stub::aur_execution_calls().size() == 1,
           "AUR-only update did not execute exactly once");
    stub::require_script_consumed();
}

void test_split_package_base_executes_once_with_child_results() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const std::vector<std::string> roots = {
            "split-runtime", "split-tools"};
    stub::set_foreign_inventory(foreign_inventory(roots));
    enqueue_aur_query({
            {roots[0], "split-suite"},
            {roots[1], "split-suite"}});
    return_build_plan(
            root_plan({
                    {roots[0], "split-suite"},
                    {roots[1], "split-suite"}}),
            roots);
    stub::enqueue_aur_successes(
            {ArtifactInstallExecutionOutcome::Installed,
             ArtifactInstallExecutionOutcome::SkippedAsNeeded},
            {ArtifactPackageIdentity{"split-debug", "2.0-1"}});

    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "split PackageBase fixture");
    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);

    expect(
            result.is_success() && result.aur.operation_result.has_value() &&
                    result.aur.operation_result->execution.has_value() &&
                    result.aur.operation_result->execution->
                                    work_item_results.size() == 1 &&
                    result.aur.operation_result->reduced_operation_result.
                                    targets.size() == 2 &&
                    result.aur.operation_result->reduced_operation_result.
                                    targets[0].status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.aur.operation_result->reduced_operation_result.
                                    targets[1].status ==
                            AurUpdateOperationTargetStatus::NoChange,
            "Split PackageBase child outcomes were not reduced independently");

    const AurUpdateWorkItemExecutionResult& work_item =
            result.aur.operation_result->execution->work_item_results.front();
    expect(
            work_item.package_name.empty() &&
                    work_item.package_base == "split-suite" &&
                    work_item.child_results.size() == 2 &&
                    work_item.child_results[0].required_package_name ==
                            roots[0] &&
                    work_item.child_results[0].status ==
                            AurUpdateChildExecutionStatus::Installed &&
                    work_item.child_results[1].required_package_name ==
                            roots[1] &&
                    work_item.child_results[1].status ==
                            AurUpdateChildExecutionStatus::SkippedAsNeeded &&
                    work_item.unselected_artifacts.size() == 1 &&
                    work_item.unselected_artifacts[0].package_name ==
                            "split-debug" &&
                    work_item.unselected_artifacts[0].full_version ==
                            "2.0-1",
            "Split PackageBase execution snapshot lost child or unselected identities");
    expect(
            stub::aur_execution_calls().size() == 1 &&
                    stub::aur_execution_calls()[0].package_name.empty() &&
                    stub::aur_execution_calls()[0].package_base ==
                            "split-suite" &&
                    stub::aur_execution_calls()[0].plan_package_names ==
                            roots &&
                    stub::aur_execution_calls()[0].required_targets.size() ==
                            2,
            "Split PackageBase did not use one ordered set-owner call");
    stub::require_script_consumed();
}

void test_all_phases_changed_in_exact_order() {
    stub::reset();
    const std::vector<std::string> sources = {"source-root"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(
            sources,
            config,
            LocalPackageVersionSnapshot{{"core", "1.0-1"}});
    enqueue_post_source_metadata(
            sources,
            LocalPackageVersionSnapshot{{"core", "2.0-1"}});
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::Installed));
    stub::set_foreign_inventory(foreign_inventory({"aur-root"}));
    enqueue_aur_query({{"aur-root", "aur-root"}});
    return_build_plan(root_plan({{"aur-root", "aur-root"}}), {"aur-root"});
    stub::enqueue_aur_success(ArtifactInstallExecutionOutcome::Installed);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() &&
                    result.package_state_change() ==
                            PackageStateChange::Changed &&
                    result.system_source.system.package_state_change ==
                            PackageStateChange::Changed &&
                    result.system_source.registered_source_results[0].status ==
                            RegisteredSourceUpgradeStatus::Updated &&
                    result.aur.status == UpgradeAllAurPhaseStatus::Completed,
            "All-changed aggregate result differs");

    const std::string system_command = stub::system_commands().front();
    expect(
            event_position(stub::EventKind::SystemCommand, system_command) <
                    event_position(
                            stub::EventKind::SourceExecution,
                            "source-root") &&
                    event_position(
                            stub::EventKind::SourceExecution,
                            "source-root") <
                    event_position(
                            stub::EventKind::ForeignInventoryQuery,
                            "foreign-inventory") &&
                    event_position(
                            stub::EventKind::ForeignInventoryQuery,
                            "foreign-inventory") <
                    event_position(
                            stub::EventKind::AurInfoMany,
                            "aur-info-many") &&
                    event_position(
                            stub::EventKind::AurInfoMany,
                            "aur-info-many") <
                    event_position(
                            stub::EventKind::BuildPlanResolution,
                            "build-plan") &&
                    event_position(
                            stub::EventKind::BuildPlanResolution,
                            "build-plan") <
                    event_position(
                            stub::EventKind::AurCheckout,
                            "aur-root"),
            "Cross-phase mutation/query order changed");
    stub::require_script_consumed();
}

void test_source_no_change_and_aur_no_change() {
    stub::reset();
    const std::vector<std::string> sources = {"same-source"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory(foreign_inventory({"same-aur"}));
    enqueue_aur_query({{"same-aur", "same-aur"}});
    return_build_plan(root_plan({{"same-aur", "same-aur"}}), {"same-aur"});
    stub::enqueue_aur_success(
            ArtifactInstallExecutionOutcome::SkippedAsNeeded);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::NoUpdates &&
                    result.aur.status == UpgradeAllAurPhaseStatus::Completed &&
                    result.aur.operation_result->reduced_operation_result.
                                    targets[0].status ==
                            AurUpdateOperationTargetStatus::NoChange &&
                    result.package_state_change() ==
                            PackageStateChange::NoChange,
            "All-NoChange execution did not satisfy NoUpdates contract");
    stub::require_script_consumed();
}

void test_duplicate_exclusion_uses_prepared_source_intent() {
    stub::reset();
    const std::vector<std::string> sources = {"duplicate-root"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);

    // LANDMINE(#281): source intentのauthorityはprepared snapshotであり、
    // execution直前のdirectory/identity再観測ではない。
    stub::set_preference_directory(preference_directory({}));
    stub::set_source_identity(
            "duplicate-root",
            source_identity(
                    "duplicate-root", "changed-base", "source://changed"));
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory(foreign_inventory({"duplicate-root"}));
    enqueue_aur_query({{"duplicate-root", "duplicate-root"}});

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() && result.has_duplicate_exclusions() &&
                    result.duplicate_excluded_aur_targets.size() == 1 &&
                    result.duplicate_excluded_aur_targets[0].
                                    original_query_plan_index == 0 &&
                    result.duplicate_excluded_aur_targets[0].query_entry.
                                    installed_name == "duplicate-root" &&
                    result.prepared_snapshot.explicit_source_adapter.
                                    entries[0].resolved_package_base ==
                            std::optional<std::string>{"duplicate-root"},
            "Prepared source intent did not author duplicate exclusion");
    expect(stub::resolver_call_count() == 0 &&
                   stub::aur_execution_calls().empty(),
           "Duplicate-excluded root reached AUR build/install");
    stub::require_script_consumed();
}

void test_transitive_external_satisfaction_attribution() {
    stub::reset();
    const std::vector<std::string> sources = {"external-library"};
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = prepare_sources(sources, config);
    enqueue_post_source_metadata(sources);
    stub::enqueue_source_success(
            source_execution(SourceBuildExecutionStatus::UpToDate));
    stub::set_foreign_inventory(foreign_inventory({"application"}));
    enqueue_aur_query({{"application", "application"}});

    BuildPlan plan = root_plan({{"application", "application"}});
    add_aur_dependency(
            plan, "application", "middle-library", "middle-library");
    add_aur_dependency(
            plan,
            "middle-library",
            "external-library",
            "external-library",
            PackageRole::BuildDependency);
    return_build_plan(std::move(plan), {"application"});
    stub::enqueue_aur_success(ArtifactInstallExecutionOutcome::Installed);
    stub::enqueue_aur_success(ArtifactInstallExecutionOutcome::Installed);

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.is_success() && result.has_external_satisfaction() &&
                    result.externally_satisfied_aur_build_units.size() == 1,
            "Transitive external satisfaction was not aggregated");
    const UpgradeAllExternallySatisfiedAurBuildUnit& external =
            result.externally_satisfied_aur_build_units.front();
    expect(
            external.operation_unit.package_base == "external-library" &&
                    external.operation_unit.package_name ==
                            "external-library" &&
                    external.operation_unit.plan_package_names ==
                            std::vector<std::string>{"external-library"} &&
                    external.operation_unit.required_target_attributions
                                    .size() == 1 &&
                    external.operation_unit.required_target_attributions
                                    .front().required_target.package_name ==
                            "external-library" &&
                    external.operation_unit.required_target_attributions
                                    .front().required_target.package_base ==
                            "external-library" &&
                    external.root_correlations.size() == 1 &&
                    external.root_correlations[0].original_query_plan_index ==
                            0 &&
                    external.root_correlations[0].role ==
                            UpgradeAllBuildUnitRole::BuildDependency,
            "External root-role correlation was reconstructed incorrectly");
    expect(
            stub::aur_execution_calls().size() == 2 &&
                    stub::aur_execution_calls()[0].package_name ==
                            "middle-library" &&
                    stub::aur_execution_calls()[1].package_name ==
                            "application" &&
                    std::none_of(
                            stub::aur_execution_calls().begin(),
                            stub::aur_execution_calls().end(),
                            [](const stub::AurExecutionCall& call) {
                                return call.package_name ==
                                        "external-library";
                            }),
            "Externally satisfied build unit reached AUR execution");
    stub::require_script_consumed();
}

void test_aur_ordinary_failure_partial_and_not_attempted() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const std::vector<std::string> roots = {
            "updated-first", "failed-second", "pending-third"};
    stub::set_foreign_inventory(foreign_inventory(roots));
    enqueue_aur_query({
            {roots[0], roots[0]},
            {roots[1], roots[1]},
            {roots[2], roots[2]}});
    return_build_plan(
            root_plan({
                    {roots[0], roots[0]},
                    {roots[1], roots[1]},
                    {roots[2], roots[2]}}),
            roots);
    stub::enqueue_aur_success(ArtifactInstallExecutionOutcome::Installed);
    stub::enqueue_aur_ordinary_failure("fixture AUR build failure");
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "ordinary AUR failure fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status ==
                            UpgradeAllOperationStatus::StoppedOnAurFailure &&
                    result.aur.status == UpgradeAllAurPhaseStatus::
                                                 StoppedOnWorkItemFailure &&
                    result.aur.operation_result.has_value() &&
                    result.aur.operation_result->selected_target_results.
                                    size() == 3 &&
                    result.aur.operation_result->selected_target_results[0].
                                    operation_result.status ==
                            AurUpdateOperationTargetStatus::Updated &&
                    result.aur.operation_result->selected_target_results[1].
                                    operation_result.status ==
                            AurUpdateOperationTargetStatus::Failed &&
                    result.aur.operation_result->selected_target_results[2].
                                    operation_result.status ==
                            AurUpdateOperationTargetStatus::NotAttempted &&
                    result.has_partial_completion() &&
                    result.has_not_attempted_phase() &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "Ordinary AUR failure lost partial/NotAttempted result");
    expect(stub::aur_execution_calls().size() == 2,
           "Ordinary AUR failure did not stop later work item");
    stub::require_script_consumed();
}

void test_aur_cleanup_failure_partial_and_not_attempted() {
    stub::reset();
    stub::set_preference_directory(preference_directory({}));
    const std::vector<std::string> roots = {"cleanup-root", "pending-root"};
    stub::set_foreign_inventory(foreign_inventory(roots));
    enqueue_aur_query({
            {roots[0], roots[0]},
            {roots[1], roots[1]}});
    return_build_plan(
            root_plan({
                    {roots[0], roots[0]},
                    {roots[1], roots[1]}}),
            roots);
    stub::enqueue_aur_cleanup_failure(
            ArtifactInstallExecutionOutcome::Installed,
            "fixture AUR cleanup failure");
    const AppConfig config;
    PreparedUpgradeAllOperation prepared = take_prepared(
            prepare_upgrade_all_operation(config),
            "AUR cleanup failure fixture");

    UpgradeAllOperationResult result =
            execute_prepared_upgrade_all_operation(
                    std::move(prepared), config);
    expect(
            result.status == UpgradeAllOperationStatus::
                                     StoppedAfterAurCleanupFailure &&
                    result.aur.status == UpgradeAllAurPhaseStatus::
                                                 StoppedAfterCleanupFailure &&
                    result.has_cleanup_failure() &&
                    result.has_partial_completion() &&
                    result.has_not_attempted_phase() &&
                    result.package_state_change() ==
                            PackageStateChange::Changed,
            "AUR cleanup failure lost known package transaction");
    expect(stub::aur_execution_calls().size() == 1,
           "AUR cleanup failure did not stop later work item");
    stub::require_script_consumed();
}

void test_constructed_no_source_no_updates_helper_fixture() {
    // PR2のsourceなしproduction resultはsystem stateをUnknownにするため、
    // pure aggregate helperのNoChange conjunctionだけをconstructed resultで固定する。
    UpgradeAllOperationResult result;
    result.status = UpgradeAllOperationStatus::NoUpdates;
    result.stopped_phase = UpgradeAllOperationPhase::None;
    result.system_source.status = SystemSourceUpgradeStatus::Completed;
    result.system_source.stopped_phase = SystemSourceUpgradePhase::None;
    result.system_source.system.status = SystemUpgradePhaseStatus::Completed;
    result.system_source.system.package_state_change =
            PackageStateChange::NoChange;
    result.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Completed;
    result.aur.status = UpgradeAllAurPhaseStatus::NoUpdates;
    result.aur.operation_result.emplace();
    result.aur.operation_result->reduced_operation_result.status =
            AurUpdateOperationStatus::NoUpdates;

    expect(
            result.is_success() &&
                    result.package_state_change() ==
                            PackageStateChange::NoChange &&
                    !result.has_partial_completion() &&
                    !result.has_not_attempted_phase() &&
                    !result.has_cleanup_failure() &&
                    !result.has_query_failure() &&
                    !result.has_planning_issue() &&
                    !result.has_duplicate_exclusions() &&
                    !result.has_external_satisfaction() &&
                    !result.has_inconsistency(),
            "Constructed no-source NoUpdates helper semantics differ");
}

UpgradeAllOperationResult make_constructed_completed_helper_fixture(
        PackageStateChange system_package_state) {
    UpgradeAllOperationResult result;
    result.status = UpgradeAllOperationStatus::Completed;
    result.stopped_phase = UpgradeAllOperationPhase::None;
    result.system_source.status = SystemSourceUpgradeStatus::Completed;
    result.system_source.stopped_phase = SystemSourceUpgradePhase::None;
    result.system_source.system.status = SystemUpgradePhaseStatus::Completed;
    result.system_source.system.package_state_change = system_package_state;
    result.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Completed;
    result.aur.status = UpgradeAllAurPhaseStatus::Completed;
    result.aur.operation_result.emplace();
    result.aur.operation_result->reduced_operation_result.status =
            AurUpdateOperationStatus::Completed;
    result.aur.operation_result->reduced_operation_result.execution_status =
            AurUpdateInvocationExecutionStatus::Completed;
    return result;
}

void test_constructed_completed_unknown_success_fixture() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::Unknown);

    expect(
            result.is_success() &&
                    result.package_state_change() ==
                            PackageStateChange::Unknown,
            "Completed + Unknown package state must remain successful");
}

void test_constructed_success_metadata_fixture() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    result.duplicate_excluded_aur_targets.emplace_back();
    result.externally_satisfied_aur_build_units.emplace_back();

    expect(
            result.is_success() && result.has_duplicate_exclusions() &&
                    result.has_external_satisfaction(),
            "Duplicate exclusion or external satisfaction changed success");
}

void test_constructed_completed_query_failure_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    result.aur.operation_result->query_result.recoverable_failures.push_back(
            AurUpdateQueryFailure{{"fixture-query"}, "fixture query failure"});

    expect(
            !result.is_success() && result.has_query_failure(),
            "Completed result hid a recoverable query failure");
}

void test_constructed_completed_direct_inventory_failure_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    result.foreign_inventory.failure = PackageMetadataFailure{
            PackageMetadataErrorCode::LocalDatabaseUnavailable,
            "fixture direct foreign inventory failure"};
    result.foreign_inventory.diagnostic =
            "fixture direct foreign inventory failure";

    expect(
            !result.is_success() && result.issues.empty(),
            "Completed result depended on an aggregate issue copy to detect a direct inventory failure");
}

void test_constructed_completed_planning_issue_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    UpgradeAllPlanningIssue issue;
    issue.kind =
            UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase;
    issue.package_base = "fixture-conflict";
    result.aur.operation_result->upgrade_all_plan.issues.push_back(
            std::move(issue));

    expect(
            !result.is_success() && result.has_planning_issue(),
            "Completed result hid a planning issue");
}

void test_constructed_completed_inconsistency_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    UpgradeAllOperationIssue issue;
    issue.kind = UpgradeAllOperationIssueKind::
            ExternalSatisfactionCorrelationInconsistent;
    issue.phase = UpgradeAllOperationPhase::Reduction;
    issue.diagnostic = "fixture aggregate inconsistency";
    result.issues.push_back(std::move(issue));

    expect(
            !result.is_success() && result.has_inconsistency(),
            "Completed result hid an aggregate inconsistency");
}

void test_constructed_completed_cleanup_failure_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    AurUpdateOperationTargetResult target;
    target.status =
            AurUpdateOperationTargetStatus::NoChangeCleanupFailed;
    result.aur.operation_result->reduced_operation_result.targets.push_back(
            std::move(target));

    expect(
            !result.is_success() && result.has_cleanup_failure(),
            "Completed result hid a cleanup failure");
}

void test_constructed_completed_not_attempted_is_not_success() {
    UpgradeAllOperationResult result =
            make_constructed_completed_helper_fixture(
                    PackageStateChange::NoChange);
    AurUpdateOperationTargetResult target;
    target.status = AurUpdateOperationTargetStatus::NotAttempted;
    result.aur.operation_result->reduced_operation_result.targets.push_back(
            std::move(target));

    expect(
            !result.is_success() && result.has_not_attempted_phase(),
            "Completed result hid a NotAttempted target");
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
                "empty registered-source preparation snapshot",
                test_empty_source_preparation_snapshot);
        run_case(
                "strict preference absence",
                test_strict_preference_absence_blocks_without_mutation);
        run_case(
                "strict preference read failure",
                test_strict_preference_read_failure_blocks_without_mutation);
        run_case(
                "system/source preparation blocker",
                test_system_source_preparation_blocker_is_aggregate_blocker);
        run_case(
                "aggregate option mismatch",
                test_option_mismatch_rejected_before_system_mutation);
        run_case(
                "aggregate capability replay",
                test_capability_replay_rejected_before_second_mutation);
        run_case(
                "outer source snapshot mismatch",
                test_outer_source_snapshot_mismatch_is_rejected);
        run_case(
                "explicit source adapter correlation mismatch",
                test_explicit_source_adapter_correlation_mismatch_is_rejected);
        run_case(
                "nested system/source correlation mismatch",
                test_nested_system_source_correlation_mismatch_is_rejected);
        run_case(
                "unexpected exception after system start",
                test_unexpected_exception_after_system_start_is_not_unattempted);
        run_case(
                "unexpected exception preserves system completion",
                test_unexpected_exception_preserves_system_completion);
        run_case(
                "unexpected exception after source start",
                test_unexpected_exception_after_source_start_is_incomplete);
        run_case(
                "unexpected exception preserves source result",
                test_unexpected_exception_preserves_recorded_source_result);
        run_case("system failure fail-fast", test_system_failure_is_fail_fast);
        run_case(
                "first source failure fail-fast",
                test_first_source_failure_stops_remaining_and_aur);
        run_case(
                "later source failure partial completion",
                test_later_source_failure_preserves_prior_update);
        run_case(
                "source cleanup failure",
                test_source_cleanup_failure_stops_before_inventory);
        run_case(
                "inventory configuration failure",
                test_foreign_inventory_configuration_failure);
        run_case(
                "foreign inventory read failure",
                test_foreign_inventory_read_failure);
        run_case(
                "recoverable AUR query failure",
                test_recoverable_aur_query_failure_blocks_mutation);
        run_case(
                "total AUR query failure",
                test_total_aur_query_failure_blocks_mutation);
        run_case(
                "planner identity conflict",
                test_planner_conflict_blocks_before_preflight);
        run_case(
                "filtered mapping issue",
                test_mapping_issue_blocks_aur_mutation);
        run_case(
                "AUR preflight blocker",
                test_preflight_blocker_stops_before_preparation_io);
        run_case(
                "AUR preparation blocker",
                test_preparation_blocker_stops_before_aur_mutation);
        run_case("NoUpdates success", test_no_updates_success_contract);
        run_case(
                "system Unknown is not NoUpdates",
                test_system_unknown_does_not_reduce_to_no_updates);
        run_case("system Changed only", test_system_changed_only);
        run_case("source Updated only", test_source_updated_only);
        run_case(
                "AUR Updated only and fresh inventory",
                test_aur_updated_only_and_fresh_inventory);
        run_case(
                "split PackageBase child execution",
                test_split_package_base_executes_once_with_child_results);
        run_case(
                "all phases Changed and exact order",
                test_all_phases_changed_in_exact_order);
        run_case(
                "source and AUR NoChange",
                test_source_no_change_and_aur_no_change);
        run_case(
                "prepared source intent duplicate exclusion",
                test_duplicate_exclusion_uses_prepared_source_intent);
        run_case(
                "transitive external satisfaction",
                test_transitive_external_satisfaction_attribution);
        run_case(
                "ordinary AUR failure partial completion",
                test_aur_ordinary_failure_partial_and_not_attempted);
        run_case(
                "AUR cleanup failure partial completion",
                test_aur_cleanup_failure_partial_and_not_attempted);
        run_case(
                "constructed no-source NoUpdates helpers",
                test_constructed_no_source_no_updates_helper_fixture);
        run_case(
                "constructed Completed Unknown success",
                test_constructed_completed_unknown_success_fixture);
        run_case(
                "constructed success metadata",
                test_constructed_success_metadata_fixture);
        run_case(
                "constructed Completed query failure",
                test_constructed_completed_query_failure_is_not_success);
        run_case(
                "constructed Completed direct inventory failure",
                test_constructed_completed_direct_inventory_failure_is_not_success);
        run_case(
                "constructed Completed planning issue",
                test_constructed_completed_planning_issue_is_not_success);
        run_case(
                "constructed Completed inconsistency",
                test_constructed_completed_inconsistency_is_not_success);
        run_case(
                "constructed Completed cleanup failure",
                test_constructed_completed_cleanup_failure_is_not_success);
        run_case(
                "constructed Completed NotAttempted",
                test_constructed_completed_not_attempted_is_not_success);
    } catch(const std::exception& error) {
        std::cerr << "upgrade_all_operation_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
