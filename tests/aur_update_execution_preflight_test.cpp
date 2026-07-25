#include "aur_update_execution_preflight.hpp"
#include "dependency_provider.hpp"
#include "stubs/aur-update-execution-preflight/preflight_stub.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using aur_update_execution_preflight_test_stub::reset_preflight_stub;
using aur_update_execution_preflight_test_stub::resolver_call_count;
using aur_update_execution_preflight_test_stub::resolver_calls;
using aur_update_execution_preflight_test_stub::set_resolver_handler;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

AurUpdatePlanEntry remote_entry(
        const std::string& installed_name,
        InstalledPackageReason installed_reason,
        AurUpdateClassification classification =
                AurUpdateClassification::UpdateAvailable,
        const std::string& aur_name = {},
        const std::string& package_base = {}) {
    const std::string resolved_aur_name =
            aur_name.empty() ? installed_name : aur_name;
    const std::string resolved_package_base =
            package_base.empty() ? resolved_aur_name : package_base;
    return AurUpdatePlanEntry{
            installed_name,
            "1.0-1",
            installed_reason,
            AurUpdateRemotePackage{
                    resolved_aur_name,
                    resolved_package_base,
                    "2.0-1",
                    classification == AurUpdateClassification::UpToDate
                            ? AurVersionRelation::SameAsInstalled
                            : classification ==
                                              AurUpdateClassification::
                                                      VersionComparisonUnavailable
                                      ? AurVersionRelation::Unavailable
                                      : AurVersionRelation::NewerThanInstalled},
            classification};
}

AurUpdatePlanEntry entry_without_remote(
        const std::string& installed_name,
        AurUpdateClassification classification,
        InstalledPackageReason installed_reason =
                InstalledPackageReason::Unknown) {
    return AurUpdatePlanEntry{
            installed_name,
            "1.0-1",
            installed_reason,
            std::nullopt,
            classification};
}

struct RootFixture {
    std::string requested_name;
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
    return found == plan.package_targets.end() ? nullptr : &(*found);
}

void add_root_fixture(
        BuildPlan& plan, std::size_t invocation_index,
        const RootFixture& fixture) {
    RootTargetIdentity root{invocation_index, fixture.requested_name};
    plan.root_targets.push_back(root);

    PlannedPackageTarget* target =
            find_package_target(plan, fixture.package_name);
    if(target == nullptr) {
        plan.package_targets.push_back(PlannedPackageTarget{
                fixture.package_name,
                fixture.package_base,
                {PackageRole::Root},
                {root}});
    } else {
        if(std::find(target->roles.begin(), target->roles.end(), PackageRole::Root) ==
           target->roles.end()) {
            target->roles.push_back(PackageRole::Root);
        }
        target->roots.push_back(root);
    }

    auto same_base = [&fixture](const BuildPlanEntry& entry) {
        return entry.package_base == fixture.package_base;
    };
    auto order_entry =
            std::find_if(plan.order.begin(), plan.order.end(), same_base);
    if(order_entry == plan.order.end()) {
        plan.order.push_back(
                BuildPlanEntry{fixture.package_base, {fixture.package_name}});
    } else if(std::find(
                      order_entry->package_names.begin(),
                      order_entry->package_names.end(),
                      fixture.package_name) == order_entry->package_names.end()) {
        order_entry->package_names.push_back(fixture.package_name);
    }
}

BuildPlan build_plan_for(const std::vector<RootFixture>& roots) {
    BuildPlan plan;
    for(std::size_t i = 0; i < roots.size(); ++i) {
        add_root_fixture(plan, i, roots[i]);
    }
    return plan;
}

void add_dependency_target(
        BuildPlan& plan, const std::string& package_name,
        const std::string& package_base,
        const std::vector<RootTargetIdentity>& roots,
        PackageRole role = PackageRole::RuntimeDependency) {
    plan.package_targets.push_back(
            PlannedPackageTarget{package_name, package_base, {role}, roots});
}

BuildPlanDependencyEdge provided_dependency_edge(
        const std::string& parent_package_name,
        const std::string& dependency_spec,
        std::optional<ProvidedDependency> provider) {
    return BuildPlanDependencyEdge{
            parent_package_name,
            parent_package_name,
            dependency_spec,
            PackageRole::RuntimeDependency,
            DependencyKind::Provided,
            std::nullopt,
            std::nullopt,
            std::move(provider)};
}

void return_build_plan(BuildPlan plan) {
    set_resolver_handler(
            [plan = std::move(plan)](const std::vector<std::string>&) {
                return plan;
            });
}

bool has_issue(
        const AurUpdateExecutionTarget& target,
        AurUpdateExecutionReason reason) {
    return std::any_of(
            target.issues.begin(), target.issues.end(),
            [reason](const AurUpdateExecutionIssue& issue) {
                return issue.reason == reason;
            });
}

std::size_t issue_count(
        const AurUpdateExecutionTarget& target,
        AurUpdateExecutionReason reason) {
    return static_cast<std::size_t>(std::count_if(
            target.issues.begin(), target.issues.end(),
            [reason](const AurUpdateExecutionIssue& issue) {
                return issue.reason == reason;
            }));
}

void expect_status(
        const AurUpdateExecutionTarget& target,
        AurUpdateExecutionTargetStatus expected,
        const std::string& context) {
    expect(target.status == expected, context + ": status differs");
}

void expect_single_resolver_call(
        const std::vector<std::string>& expected_targets,
        const std::string& context) {
    expect(resolver_call_count() == 1, context + ": resolver call count differs");
    expect(
            resolver_calls().front() == expected_targets,
            context + ": resolver target vector differs");
}

void test_classification_order_and_combined_resolution() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry(
                    "update-explicit", InstalledPackageReason::Explicit),
            remote_entry(
                    "current-package", InstalledPackageReason::Unknown,
                    AurUpdateClassification::UpToDate),
            entry_without_remote(
                    "foreign-package",
                    AurUpdateClassification::NonAurForeign),
            remote_entry(
                    "update-dependency", InstalledPackageReason::Dependency),
            entry_without_remote(
                    "metadata-failed",
                    AurUpdateClassification::MetadataUnavailable),
            remote_entry(
                    "version-failed", InstalledPackageReason::Explicit,
                    AurUpdateClassification::VersionComparisonUnavailable),
    }};
    return_build_plan(build_plan_for({
            {"update-explicit", "update-explicit", "update-explicit"},
            {"update-dependency", "update-dependency", "update-dependency"},
    }));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);

    expect(preflight.targets.size() == update_plan.entries.size(), "Target count differs");
    for(std::size_t i = 0; i < preflight.targets.size(); ++i) {
        expect(
                preflight.targets[i].update_plan_index == i,
                "Original update-plan index differs at " + std::to_string(i));
        expect(
                preflight.targets[i].update.installed_name ==
                        update_plan.entries[i].installed_name,
                "Target order differs at " + std::to_string(i));
    }
    expect_single_resolver_call(
            {"update-explicit", "update-dependency"},
            "Combined classification plan");
    expect(preflight.build_plan.has_value(), "Combined BuildPlan is missing");

    expect_status(
            preflight.targets[0], AurUpdateExecutionTargetStatus::Executable,
            "Explicit update");
    expect(
            preflight.targets[0].build_plan_root_index ==
                    std::optional<std::size_t>{0},
            "First candidate root index differs");
    expect(
            preflight.targets[0].desired_install_reason ==
                    std::optional<DesiredInstallReason>{
                            DesiredInstallReason::Explicit},
            "Explicit update reason differs");

    expect_status(
            preflight.targets[1], AurUpdateExecutionTargetStatus::Skipped,
            "Up-to-date target");
    expect(
            has_issue(preflight.targets[1], AurUpdateExecutionReason::UpToDate),
            "Up-to-date reason is missing");
    expect(!preflight.targets[1].build_plan_root_index.has_value(), "Skipped target has a root index");

    expect_status(
            preflight.targets[2], AurUpdateExecutionTargetStatus::Skipped,
            "Non-AUR target");
    expect(
            has_issue(
                    preflight.targets[2],
                    AurUpdateExecutionReason::NonAurForeign),
            "Non-AUR reason is missing");

    expect_status(
            preflight.targets[3], AurUpdateExecutionTargetStatus::Executable,
            "Dependency update");
    expect(
            preflight.targets[3].build_plan_root_index ==
                    std::optional<std::size_t>{1},
            "Second candidate root index differs");
    expect(
            preflight.targets[3].desired_install_reason ==
                    std::optional<DesiredInstallReason>{
                            DesiredInstallReason::Dependency},
            "Dependency update reason differs");

    expect_status(
            preflight.targets[4], AurUpdateExecutionTargetStatus::Incomplete,
            "Metadata failure");
    expect(
            has_issue(
                    preflight.targets[4],
                    AurUpdateExecutionReason::AurMetadataUnavailable),
            "Metadata-unavailable reason is missing");
    expect_status(
            preflight.targets[5], AurUpdateExecutionTargetStatus::Incomplete,
            "Version comparison failure");
    expect(
            has_issue(
                    preflight.targets[5],
                    AurUpdateExecutionReason::VersionComparisonUnavailable),
            "Version-comparison reason is missing");

    expect(has_executable_targets(preflight), "Executable targets were not detected");
    expect(has_blocking_targets(preflight), "Blocking targets were not detected");
    expect(!can_execute(preflight), "Incomplete invocation was executable");
}

void test_empty_and_skip_only_plans_suppress_resolution() {
    reset_preflight_stub();
    AurUpdateExecutionPreflight empty =
            resolve_aur_update_execution_preflight(AurUpdatePlan{});
    expect(empty.targets.empty(), "Empty update plan produced targets");
    expect(!empty.build_plan.has_value(), "Empty update plan produced a BuildPlan");
    expect(resolver_call_count() == 0, "Empty update plan called the resolver");
    expect(!has_executable_targets(empty), "Empty preflight has executable targets");
    expect(!has_blocking_targets(empty), "Empty preflight has blocking targets");
    expect(!can_execute(empty), "Empty preflight was executable");

    reset_preflight_stub();
    AurUpdatePlan skip_only{{
            remote_entry(
                    "current-package", InstalledPackageReason::Unknown,
                    AurUpdateClassification::UpToDate),
            entry_without_remote(
                    "foreign-package",
                    AurUpdateClassification::NonAurForeign),
    }};
    AurUpdateExecutionPreflight skipped =
            resolve_aur_update_execution_preflight(skip_only);
    expect(resolver_call_count() == 0, "Skip-only update plan called the resolver");
    expect(!skipped.build_plan.has_value(), "Skip-only update plan produced a BuildPlan");
    expect(!has_executable_targets(skipped), "Skip-only preflight has executable targets");
    expect(!has_blocking_targets(skipped), "Normal skips were treated as blockers");
    expect(!can_execute(skipped), "Skip-only preflight was executable");
}

void test_installed_reason_mapping_and_root_dependency_overlap() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry("dependency-root", InstalledPackageReason::Dependency),
            remote_entry("explicit-root", InstalledPackageReason::Explicit),
            remote_entry("unknown-root", InstalledPackageReason::Unknown),
    }};
    BuildPlan plan = build_plan_for({
            {"dependency-root", "dependency-root", "dependency-root"},
            {"explicit-root", "explicit-root", "explicit-root"},
            {"unknown-root", "unknown-root", "unknown-root"},
    });
    PlannedPackageTarget* dependency_root =
            find_package_target(plan, "dependency-root");
    expect(dependency_root != nullptr, "Dependency root fixture is missing");
    dependency_root->roles.push_back(PackageRole::RuntimeDependency);
    dependency_root->roots.push_back(RootTargetIdentity{1, "explicit-root"});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "explicit-root",
            "explicit-root",
            "dependency-root",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"dependency-root"},
            std::optional<std::string>{"dependency-root"},
            std::nullopt});
    return_build_plan(std::move(plan));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);

    expect_single_resolver_call(
            {"dependency-root", "explicit-root", "unknown-root"},
            "Installed reason plan");
    expect(
            preflight.targets[0].desired_install_reason ==
                    std::optional<DesiredInstallReason>{
                            DesiredInstallReason::Dependency},
            "Dependency root was promoted to explicit");
    expect_status(
            preflight.targets[0], AurUpdateExecutionTargetStatus::Executable,
            "Dependency root overlap");
    expect(
            preflight.targets[1].desired_install_reason ==
                    std::optional<DesiredInstallReason>{
                            DesiredInstallReason::Explicit},
            "Explicit root reason differs");
    expect(
            !preflight.targets[2].desired_install_reason.has_value(),
            "Unknown root acquired an install reason");
    expect_status(
            preflight.targets[2], AurUpdateExecutionTargetStatus::Incomplete,
            "Unknown installed reason");
    expect(
            has_issue(
                    preflight.targets[2],
                    AurUpdateExecutionReason::InstalledReasonUnknown),
            "Unknown installed reason issue is missing");
}

void test_duplicate_update_targets_suppress_resolution() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry("duplicate-root", InstalledPackageReason::Explicit),
            remote_entry("duplicate-root", InstalledPackageReason::Dependency),
    }};

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);

    expect(resolver_call_count() == 0, "Duplicate roots reached the resolver");
    expect(!preflight.build_plan.has_value(), "Duplicate roots produced a BuildPlan");
    for(const auto& target : preflight.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Incomplete,
                "Duplicate update target");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::DuplicateUpdateTarget),
                "Duplicate update issue is missing");
    }
    expect(!can_execute(preflight), "Duplicate update preflight was executable");
}

void test_update_plan_and_build_plan_consistency() {
    reset_preflight_stub();
    AurUpdatePlan invalid_update{{entry_without_remote(
            "missing-update-metadata",
            AurUpdateClassification::UpdateAvailable,
            InstalledPackageReason::Explicit)}};
    return_build_plan(build_plan_for({
            {"missing-update-metadata", "missing-update-metadata",
             "missing-update-metadata"},
    }));
    AurUpdateExecutionPreflight invalid =
            resolve_aur_update_execution_preflight(invalid_update);
    expect_single_resolver_call(
            {"missing-update-metadata"},
            "Inconsistent UpdateAvailable target");
    expect_status(
            invalid.targets.front(), AurUpdateExecutionTargetStatus::Incomplete,
            "Inconsistent update plan");
    expect(
            has_issue(
                    invalid.targets.front(),
                    AurUpdateExecutionReason::UpdatePlanInconsistent),
            "Update-plan inconsistency issue is missing");

    reset_preflight_stub();
    AurUpdatePlan mismatched{{remote_entry(
            "split-cli", InstalledPackageReason::Explicit,
            AurUpdateClassification::UpdateAvailable,
            "split-cli", "expected-base")}};
    return_build_plan(build_plan_for(
            {{"split-cli", "split-cli", "different-base"}}));
    AurUpdateExecutionPreflight mismatch =
            resolve_aur_update_execution_preflight(mismatched);
    expect_status(
            mismatch.targets.front(), AurUpdateExecutionTargetStatus::Incomplete,
            "PackageBase mismatch");
    expect(
            has_issue(
                    mismatch.targets.front(),
                    AurUpdateExecutionReason::PackageBaseMismatch),
            "PackageBase mismatch issue is missing");

    reset_preflight_stub();
    AurUpdatePlan missing_root{{remote_entry(
            "missing-root", InstalledPackageReason::Explicit)}};
    return_build_plan(BuildPlan{});
    AurUpdateExecutionPreflight missing =
            resolve_aur_update_execution_preflight(missing_root);
    expect_status(
            missing.targets.front(), AurUpdateExecutionTargetStatus::Incomplete,
            "Missing BuildPlan root");
    expect(
            has_issue(
                    missing.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Missing BuildPlan root inconsistency is absent");

    reset_preflight_stub();
    AurUpdatePlan duplicate_package_target{{remote_entry(
            "duplicate-plan-root", InstalledPackageReason::Explicit)}};
    BuildPlan duplicate_plan = build_plan_for({
            {"duplicate-plan-root", "duplicate-plan-root", "duplicate-plan-root"},
    });
    duplicate_plan.package_targets.push_back(
            duplicate_plan.package_targets.front());
    return_build_plan(std::move(duplicate_plan));
    AurUpdateExecutionPreflight duplicate =
            resolve_aur_update_execution_preflight(duplicate_package_target);
    expect_status(
            duplicate.targets.front(), AurUpdateExecutionTargetStatus::Incomplete,
            "Duplicate BuildPlan package target");
    expect(
            has_issue(
                    duplicate.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Duplicate BuildPlan target inconsistency is absent");
}

void test_incomplete_build_plan_issues_are_typed_and_deduplicated() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{remote_entry(
            "incomplete-root", InstalledPackageReason::Explicit)}};
    BuildPlan plan = build_plan_for({
            {"incomplete-root", "incomplete-root", "incomplete-root"},
    });
    const RootTargetIdentity root{0, "incomplete-root"};
    add_dependency_target(plan, "cycle-package", "cycle-base", {root});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "incomplete-root",
            "incomplete-root",
            "missing-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "incomplete-root",
            "incomplete-root",
            "constrained-dependency>=2",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"constrained-dependency"},
            std::optional<std::string>{"constrained-dependency"},
            std::nullopt});
    plan.unresolved = {
            "missing-dependency",
            "constrained-dependency>=2 (version constraint is not verified)",
    };
    plan.cycles = {"cycle-base"};

    BuildPlanResolutionFailure metadata_failure{
            BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            std::optional<std::string>{"incomplete-root"},
            std::optional<std::string>{"incomplete-root"},
            "metadata-dependency",
            std::optional<std::string>{"metadata-dependency"},
            {root},
            "metadata unavailable"};
    plan.resolution_failures.push_back(metadata_failure);
    plan.resolution_failures.push_back(metadata_failure);
    plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            std::optional<std::string>{"incomplete-root"},
            std::optional<std::string>{"incomplete-root"},
            "repository-dependency",
            std::optional<std::string>{"repository-dependency"},
            {root},
            "repository metadata unavailable"});
    plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            std::optional<std::string>{"incomplete-root"},
            std::optional<std::string>{"incomplete-root"},
            "virtual-dependency",
            std::optional<std::string>{"virtual-dependency"},
            {root},
            "provider search unavailable"});
    plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
            std::optional<std::string>{"incomplete-root"},
            std::optional<std::string>{"incomplete-root"},
            "provider-candidate",
            std::optional<std::string>{"virtual-dependency"},
            {root},
            "provider candidate unavailable"});
    return_build_plan(std::move(plan));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);
    const AurUpdateExecutionTarget& target = preflight.targets.front();

    expect_status(
            target, AurUpdateExecutionTargetStatus::Incomplete,
            "Typed incomplete BuildPlan");
    expect(
            has_issue(target, AurUpdateExecutionReason::UnresolvedDependency),
            "Unresolved dependency issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::RepositoryMetadataUnavailable),
            "Repository metadata issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::VersionConstraintUnverified),
            "Version constraint issue is missing");
    expect(
            has_issue(target, AurUpdateExecutionReason::DependencyCycle),
            "Dependency cycle issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::AurDependencyMetadataUnavailable),
            "AUR dependency metadata issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::ProviderMetadataUnavailable),
            "Provider metadata issue is missing");
    expect(
            issue_count(
                    target,
                    AurUpdateExecutionReason::AurDependencyMetadataUnavailable) == 1,
            "Duplicate metadata failure produced duplicate target issues");
}

void test_unsupported_build_plan_issues_and_same_base_blind_spot() {
    reset_preflight_stub();
    AurUpdatePlan split_root_plan{{remote_entry(
            "split-cli", InstalledPackageReason::Explicit,
            AurUpdateClassification::UpdateAvailable,
            "split-cli", "split-suite")}};
    BuildPlan root_plan = build_plan_for({
            {"split-cli", "split-cli", "split-suite"},
    });
    root_plan.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{"split-suite", "split-cli"});
    return_build_plan(std::move(root_plan));
    AurUpdateExecutionPreflight split_root =
            resolve_aur_update_execution_preflight(split_root_plan);
    expect_status(
            split_root.targets.front(),
            AurUpdateExecutionTargetStatus::Unsupported,
            "Split update root");
    expect(
            has_issue(
                    split_root.targets.front(),
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
            "Split root issue is missing");

    reset_preflight_stub();
    AurUpdatePlan update_plan{{remote_entry(
            "unsupported-root", InstalledPackageReason::Explicit)}};
    BuildPlan plan = build_plan_for({
            {"unsupported-root", "unsupported-root", "unsupported-root"},
    });
    const RootTargetIdentity root{0, "unsupported-root"};
    add_dependency_target(plan, "split-child", "shared-suite", {root});
    add_dependency_target(plan, "second-child", "shared-suite", {root});
    // same-base blind spotはpackage_namesからsecond-childだけが落ちる形で作る。
    plan.order.push_back(BuildPlanEntry{"shared-suite", {"split-child"}});
    plan.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{"shared-suite", "split-child"});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "unsupported-root",
            "unsupported-root",
            "split-child",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"split-child"},
            std::optional<std::string>{"shared-suite"},
            std::nullopt});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "unsupported-root",
            "unsupported-root",
            "second-child",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"second-child"},
            std::optional<std::string>{"shared-suite"},
            std::nullopt});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "unsupported-root",
            "unsupported-root",
            "virtual-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::AmbiguousProvider,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    plan.ambiguous_providers.push_back(AmbiguousProvidedDependency{
            "virtual-dependency",
            {
                    ProvidedDependency::from_repository(
                            "extra", "provider-a"),
                    ProvidedDependency::from_aur("provider-b"),
            }});
    plan.metadata_risks.push_back(BuildPlanMetadataRisk{
            "second-child", "shared-suite", {"old-package"}, {"renamed-package"}});
    return_build_plan(std::move(plan));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);
    const AurUpdateExecutionTarget& target = preflight.targets.front();

    expect_status(
            target, AurUpdateExecutionTargetStatus::Unsupported,
            "Unsupported dependency plan");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
            "Split dependency issue is missing");
    expect(
            has_issue(target, AurUpdateExecutionReason::AmbiguousProvider),
            "Ambiguous provider issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::ConflictsOrReplacesUnresolved),
            "Conflicts/replaces issue is missing");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase),
            "Same-PackageBase package_targets blind spot was not detected");
    expect(!can_execute(preflight), "Unsupported invocation was executable");
}

void test_incomplete_status_precedes_unsupported_and_preserves_both() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{remote_entry(
            "mixed-root", InstalledPackageReason::Explicit)}};
    BuildPlan plan = build_plan_for({
            {"mixed-root", "mixed-root", "mixed-root"},
    });
    const RootTargetIdentity root{0, "mixed-root"};
    plan.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{"mixed-root", "mixed-root"});
    plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
            std::optional<std::string>{"mixed-root"},
            std::optional<std::string>{"mixed-root"},
            "provider-candidate",
            std::optional<std::string>{"virtual-dependency"},
            {root},
            "provider candidate unavailable"});
    return_build_plan(std::move(plan));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);
    const AurUpdateExecutionTarget& target = preflight.targets.front();
    expect_status(
            target, AurUpdateExecutionTargetStatus::Incomplete,
            "Incomplete/unsupported reducer");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::ProviderMetadataUnavailable),
            "Incomplete issue was lost");
    expect(
            has_issue(
                    target,
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
            "Unsupported issue was lost");
}

void test_issue_attribution_and_global_fallback() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry("affected-root", InstalledPackageReason::Explicit),
            remote_entry("clean-root", InstalledPackageReason::Explicit),
    }};
    BuildPlan attributed_plan = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    attributed_plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "affected-root",
            "affected-root",
            "missing-child",
            PackageRole::RuntimeDependency,
            DependencyKind::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    attributed_plan.unresolved.push_back("missing-child");
    return_build_plan(std::move(attributed_plan));

    AurUpdateExecutionPreflight attributed =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            attributed.targets[0], AurUpdateExecutionTargetStatus::Incomplete,
            "Affected root attribution");
    expect(
            has_issue(
                    attributed.targets[0],
                    AurUpdateExecutionReason::UnresolvedDependency),
            "Affected root unresolved issue is missing");
    expect_status(
            attributed.targets[1], AurUpdateExecutionTargetStatus::Executable,
            "Unaffected root attribution");
    expect(
            !has_issue(
                    attributed.targets[1],
                    AurUpdateExecutionReason::UnresolvedDependency),
            "Unresolved issue leaked to an unaffected root");

    reset_preflight_stub();
    BuildPlan global_plan = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    global_plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            std::optional<std::string>{"orphan-parent"},
            std::optional<std::string>{"orphan-base"},
            "orphan-virtual",
            std::optional<std::string>{"orphan-virtual"},
            {},
            "unattributed provider search failure"});
    return_build_plan(std::move(global_plan));

    AurUpdateExecutionPreflight global =
            resolve_aur_update_execution_preflight(update_plan);
    for(const auto& target : global.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Incomplete,
                "Global blocker attribution");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::ProviderMetadataUnavailable),
                "Global typed blocker is missing");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::BuildPlanInconsistent),
                "Global attribution inconsistency marker is missing");
    }

    reset_preflight_stub();
    BuildPlan mismatched_plan = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    mismatched_plan.resolution_failures.push_back(
            BuildPlanResolutionFailure{
                    BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                    std::optional<std::string>{"affected-root"},
                    std::optional<std::string>{"affected-root"},
                    "virtual-dependency",
                    std::optional<std::string>{"virtual-dependency"},
                    {{1, "clean-root"}},
                    "failure attributed to an unrelated known root"});
    return_build_plan(std::move(mismatched_plan));

    AurUpdateExecutionPreflight mismatched =
            resolve_aur_update_execution_preflight(update_plan);
    for(const auto& target : mismatched.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Incomplete,
                "Mismatched failure attribution");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::ProviderMetadataUnavailable),
                "Mismatched typed blocker did not fall back globally");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::BuildPlanInconsistent),
                "Mismatched failure attribution was not rejected");
    }
}

void test_resolution_failure_root_validation() {
    AurUpdatePlan update_plan{{
            remote_entry("affected-root", InstalledPackageReason::Explicit),
            remote_entry("clean-root", InstalledPackageReason::Explicit),
    }};
    const RootTargetIdentity affected_root{0, "affected-root"};
    const RootTargetIdentity clean_root{1, "clean-root"};

    auto expect_global_fallback = [&](BuildPlan plan, const std::string& context) {
        reset_preflight_stub();
        return_build_plan(std::move(plan));
        AurUpdateExecutionPreflight preflight =
                resolve_aur_update_execution_preflight(update_plan);
        for(const auto& target : preflight.targets) {
            expect_status(
                    target, AurUpdateExecutionTargetStatus::Incomplete,
                    context);
            expect(
                    has_issue(
                            target,
                            AurUpdateExecutionReason::ProviderMetadataUnavailable),
                    context + ": typed issue did not fall back globally");
            expect(
                    has_issue(
                            target,
                            AurUpdateExecutionReason::BuildPlanInconsistent),
                    context + ": attribution inconsistency is missing");
        }
    };

    BuildPlan extra_root = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    extra_root.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            std::optional<std::string>{"affected-root"},
            std::optional<std::string>{"affected-root"},
            "virtual-dependency",
            std::optional<std::string>{"virtual-dependency"},
            {affected_root, clean_root},
            "failure contains an extra root"});
    expect_global_fallback(std::move(extra_root), "Failure with extra root");

    BuildPlan unknown_root = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    unknown_root.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            std::optional<std::string>{"affected-root"},
            std::optional<std::string>{"affected-root"},
            "virtual-dependency",
            std::optional<std::string>{"virtual-dependency"},
            {{99, "unknown-root"}},
            "failure contains an unknown root"});
    expect_global_fallback(std::move(unknown_root), "Failure with unknown root");

    BuildPlan missing_parent = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    missing_parent.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            std::optional<std::string>{"missing-parent"},
            std::optional<std::string>{"missing-parent"},
            "virtual-dependency",
            std::optional<std::string>{"virtual-dependency"},
            {affected_root},
            "failure parent target is missing"});
    expect_global_fallback(std::move(missing_parent), "Failure with missing parent");

    BuildPlan incomplete_parent_identity = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    incomplete_parent_identity.resolution_failures.push_back(
            BuildPlanResolutionFailure{
                    BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                    std::optional<std::string>{"affected-root"},
                    std::nullopt,
                    "virtual-dependency",
                    std::optional<std::string>{"virtual-dependency"},
                    {affected_root},
                    "failure parent identity is incomplete"});
    expect_global_fallback(
            std::move(incomplete_parent_identity),
            "Failure with incomplete parent identity");

    reset_preflight_stub();
    BuildPlan shared_parent = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    add_dependency_target(
            shared_parent, "shared-parent", "shared-parent",
            {affected_root, clean_root});
    shared_parent.order.insert(
            shared_parent.order.begin(),
            BuildPlanEntry{"shared-parent", {"shared-parent"}});
    for(const auto& root : {affected_root, clean_root}) {
        shared_parent.dependency_edges.push_back(BuildPlanDependencyEdge{
                root.requested_name,
                root.requested_name,
                "shared-parent",
                PackageRole::RuntimeDependency,
                DependencyKind::Aur,
                std::optional<std::string>{"shared-parent"},
                std::optional<std::string>{"shared-parent"},
                std::nullopt});
    }
    shared_parent.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            std::optional<std::string>{"shared-parent"},
            std::optional<std::string>{"shared-parent"},
            "shared-child",
            std::optional<std::string>{"shared-child"},
            {affected_root, clean_root},
            "valid shared dependency failure"});
    return_build_plan(std::move(shared_parent));
    AurUpdateExecutionPreflight shared =
            resolve_aur_update_execution_preflight(update_plan);
    for(const auto& target : shared.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Incomplete,
                "Valid shared failure attribution");
        expect(
                has_issue(
                        target,
                        AurUpdateExecutionReason::AurDependencyMetadataUnavailable),
                "Shared failure did not reach every owning root");
        expect(
                !has_issue(
                        target,
                        AurUpdateExecutionReason::BuildPlanInconsistent),
                "Valid shared failure attribution was rejected");
    }

    reset_preflight_stub();
    BuildPlan root_failure = build_plan_for({
            {"affected-root", "affected-root", "affected-root"},
            {"clean-root", "clean-root", "clean-root"},
    });
    root_failure.package_targets.erase(root_failure.package_targets.begin());
    root_failure.order.erase(root_failure.order.begin());
    root_failure.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            std::nullopt,
            std::nullopt,
            "affected-root",
            std::nullopt,
            {affected_root},
            "valid root metadata failure"});
    return_build_plan(std::move(root_failure));
    AurUpdateExecutionPreflight root =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            root.targets[0], AurUpdateExecutionTargetStatus::Incomplete,
            "Valid root metadata failure");
    expect(
            has_issue(
                    root.targets[0],
                    AurUpdateExecutionReason::AurDependencyMetadataUnavailable),
            "Root metadata failure lost its owning root");
    expect_status(
            root.targets[1], AurUpdateExecutionTargetStatus::Executable,
            "Root metadata failure leaked to unrelated root");
}

void test_fail_closed_cross_field_consistency() {
    AurUpdatePlan update_plan{{remote_entry(
            "consistency-root", InstalledPackageReason::Explicit)}};

    reset_preflight_stub();
    BuildPlan missing_order = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    missing_order.order.clear();
    return_build_plan(std::move(missing_order));
    AurUpdateExecutionPreflight missing_order_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            missing_order_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Missing BuildPlan execution order");
    expect(
            has_issue(
                    missing_order_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Missing execution order was not rejected");

    reset_preflight_stub();
    BuildPlan wrong_order_name = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    wrong_order_name.order.front().package_names = {"unrelated-package"};
    return_build_plan(std::move(wrong_order_name));
    AurUpdateExecutionPreflight wrong_order_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            wrong_order_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Mismatched BuildPlan order package name");

    reset_preflight_stub();
    BuildPlan ambiguous_edge_only = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    ambiguous_edge_only.dependency_edges.push_back(BuildPlanDependencyEdge{
            "consistency-root",
            "consistency-root",
            "missing-provider-summary",
            PackageRole::RuntimeDependency,
            DependencyKind::AmbiguousProvider,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    return_build_plan(std::move(ambiguous_edge_only));
    AurUpdateExecutionPreflight ambiguous_edge_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            ambiguous_edge_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Ambiguous edge without summary");
    expect(
            has_issue(
                    ambiguous_edge_preflight.targets.front(),
                    AurUpdateExecutionReason::AmbiguousProvider),
            "Ambiguous edge blocker was not derived from the typed edge");
    expect(
            has_issue(
                    ambiguous_edge_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Missing ambiguous summary was not rejected");

    reset_preflight_stub();
    AurUpdatePlan split_plan{{remote_entry(
            "split-only-cli", InstalledPackageReason::Explicit,
            AurUpdateClassification::UpdateAvailable,
            "split-only-cli", "split-only-suite")}};
    return_build_plan(build_plan_for({
            {"split-only-cli", "split-only-cli", "split-only-suite"},
    }));
    AurUpdateExecutionPreflight split_without_summary =
            resolve_aur_update_execution_preflight(split_plan);
    expect_status(
            split_without_summary.targets.front(),
            AurUpdateExecutionTargetStatus::Unsupported,
            "Split identity without summary");
    expect(
            has_issue(
                    split_without_summary.targets.front(),
                    AurUpdateExecutionReason::SplitPackageSelectionRequired),
            "Split identity did not produce an unsupported issue");

    reset_preflight_stub();
    BuildPlan rootless_dependency = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    add_dependency_target(
            rootless_dependency, "orphan-dependency", "orphan-dependency", {});
    rootless_dependency.order.insert(
            rootless_dependency.order.begin(),
            BuildPlanEntry{"orphan-dependency", {"orphan-dependency"}});
    return_build_plan(std::move(rootless_dependency));
    AurUpdateExecutionPreflight rootless_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            rootless_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Rootless dependency target");
    expect(
            has_issue(
                    rootless_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Rootless dependency target was not a global blocker");

    reset_preflight_stub();
    BuildPlan orphan_dependency = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    add_dependency_target(
            orphan_dependency, "orphan-dependency", "orphan-dependency",
            {{0, "consistency-root"}});
    orphan_dependency.order.insert(
            orphan_dependency.order.begin(),
            BuildPlanEntry{"orphan-dependency", {"orphan-dependency"}});
    return_build_plan(std::move(orphan_dependency));
    AurUpdateExecutionPreflight orphan_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            orphan_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Dependency target without an incoming edge");
    expect(
            has_issue(
                    orphan_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Known-root orphan dependency target was not rejected");

    reset_preflight_stub();
    BuildPlan orphan_cycle = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    add_dependency_target(
            orphan_cycle, "orphan-cycle", "orphan-cycle",
            {{0, "consistency-root"}});
    orphan_cycle.order.insert(
            orphan_cycle.order.begin(),
            BuildPlanEntry{"orphan-cycle", {"orphan-cycle"}});
    orphan_cycle.dependency_edges.push_back(BuildPlanDependencyEdge{
            "orphan-cycle",
            "orphan-cycle",
            "orphan-cycle",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"orphan-cycle"},
            std::optional<std::string>{"orphan-cycle"},
            std::nullopt});
    return_build_plan(std::move(orphan_cycle));
    AurUpdateExecutionPreflight orphan_cycle_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            orphan_cycle_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Unreachable dependency self-cycle");
    expect(
            has_issue(
                    orphan_cycle_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Unreachable self-cycle justified its own root ownership");

    reset_preflight_stub();
    BuildPlan reachable_cycle = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    PlannedPackageTarget* cycle_root =
            find_package_target(reachable_cycle, "consistency-root");
    expect(cycle_root != nullptr, "Reachable cycle root fixture is missing");
    cycle_root->roles.push_back(PackageRole::RuntimeDependency);
    reachable_cycle.dependency_edges.push_back(BuildPlanDependencyEdge{
            "consistency-root",
            "consistency-root",
            "consistency-root",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"consistency-root"},
            std::optional<std::string>{"consistency-root"},
            std::nullopt});
    return_build_plan(std::move(reachable_cycle));
    AurUpdateExecutionPreflight reachable_cycle_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            reachable_cycle_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Reachable self-cycle without summary");
    expect(
            has_issue(
                    reachable_cycle_preflight.targets.front(),
                    AurUpdateExecutionReason::DependencyCycle),
            "Typed graph cycle was lost when the cycle summary was missing");
    expect(
            has_issue(
                    reachable_cycle_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Missing cycle summary was not marked inconsistent");

    reset_preflight_stub();
    BuildPlan repository_edge_target = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    add_dependency_target(
            repository_edge_target, "repository-only-dependency",
            "repository-only-dependency", {{0, "consistency-root"}});
    repository_edge_target.order.insert(
            repository_edge_target.order.begin(),
            BuildPlanEntry{
                    "repository-only-dependency",
                    {"repository-only-dependency"}});
    repository_edge_target.dependency_edges.push_back(
            BuildPlanDependencyEdge{
                    "consistency-root",
                    "consistency-root",
                    "repository-only-dependency",
                    PackageRole::RuntimeDependency,
                    DependencyKind::Repo,
                    std::optional<std::string>{
                            "repository-only-dependency"},
                    std::nullopt,
                    std::nullopt});
    return_build_plan(std::move(repository_edge_target));
    AurUpdateExecutionPreflight repository_edge_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            repository_edge_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Repository edge cannot justify an AUR dependency target");
    expect(
            has_issue(
                    repository_edge_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Repository edge incorrectly justified an AUR target");

    reset_preflight_stub();
    BuildPlan wrong_repo_identity = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    wrong_repo_identity.dependency_edges.push_back(BuildPlanDependencyEdge{
            "consistency-root",
            "consistency-root",
            "expected-repository-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::Repo,
            std::optional<std::string>{"different-repository-package"},
            std::nullopt,
            std::nullopt});
    return_build_plan(std::move(wrong_repo_identity));
    AurUpdateExecutionPreflight wrong_repo_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            wrong_repo_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Mismatched direct repository edge identity");

    reset_preflight_stub();
    AurUpdatePlan two_roots{{
            remote_entry("owner-root", InstalledPackageReason::Explicit),
            remote_entry("other-root", InstalledPackageReason::Explicit),
    }};
    BuildPlan wrong_aur_ownership = build_plan_for({
            {"owner-root", "owner-root", "owner-root"},
            {"other-root", "other-root", "other-root"},
    });
    add_dependency_target(
            wrong_aur_ownership, "owned-dependency", "owned-dependency",
            {{1, "other-root"}});
    wrong_aur_ownership.order.insert(
            wrong_aur_ownership.order.begin(),
            BuildPlanEntry{"owned-dependency", {"owned-dependency"}});
    wrong_aur_ownership.dependency_edges.push_back(BuildPlanDependencyEdge{
            "owner-root",
            "owner-root",
            "owned-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"owned-dependency"},
            std::optional<std::string>{"owned-dependency"},
            std::nullopt});
    return_build_plan(std::move(wrong_aur_ownership));
    AurUpdateExecutionPreflight wrong_ownership_preflight =
            resolve_aur_update_execution_preflight(two_roots);
    expect_status(
            wrong_ownership_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "AUR edge resolved target with wrong root ownership");

    reset_preflight_stub();
    BuildPlan missing_roles = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    missing_roles.package_targets.push_back(PlannedPackageTarget{
            "roleless-dependency",
            "roleless-dependency",
            {},
            {{0, "consistency-root"}}});
    missing_roles.order.insert(
            missing_roles.order.begin(),
            BuildPlanEntry{"roleless-dependency", {"roleless-dependency"}});
    return_build_plan(std::move(missing_roles));
    AurUpdateExecutionPreflight missing_roles_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            missing_roles_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Roleless planned dependency target");

    reset_preflight_stub();
    BuildPlan root_role_edge = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    root_role_edge.dependency_edges.push_back(BuildPlanDependencyEdge{
            "consistency-root",
            "consistency-root",
            "repository-dependency",
            PackageRole::Root,
            DependencyKind::Repo,
            std::optional<std::string>{"repository-dependency"},
            std::nullopt,
            std::nullopt});
    return_build_plan(std::move(root_role_edge));
    AurUpdateExecutionPreflight root_role_edge_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            root_role_edge_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Dependency edge with Root role");

    reset_preflight_stub();
    BuildPlan missing_provider = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    missing_provider.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency", std::nullopt));
    return_build_plan(std::move(missing_provider));
    AurUpdateExecutionPreflight missing_provider_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            missing_provider_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Provided dependency without resolved provider");
    expect(
            has_issue(
                    missing_provider_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Missing resolved provider was accepted");

    reset_preflight_stub();
    BuildPlan empty_repository_origin = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    empty_repository_origin.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_repository(
                            "", "provider-package")));
    return_build_plan(std::move(empty_repository_origin));
    AurUpdateExecutionPreflight empty_repository_origin_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            empty_repository_origin_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Provided dependency with empty repository origin");
    expect(
            has_issue(
                    empty_repository_origin_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Empty repository provider origin was accepted");

    reset_preflight_stub();
    BuildPlan provider_with_control_origin = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    provider_with_control_origin.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_repository(
                            "co\nre", "provider-package")));
    return_build_plan(std::move(provider_with_control_origin));
    AurUpdateExecutionPreflight provider_with_control_origin_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            provider_with_control_origin_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Provided dependency with control-character repository origin");
    expect(
            has_issue(
                    provider_with_control_origin_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Control-character provider origin was accepted");

    reset_preflight_stub();
    BuildPlan repository_provider = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    repository_provider.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_repository(
                            "aur", "provider-package")));
    return_build_plan(std::move(repository_provider));
    AurUpdateExecutionPreflight repository_provider_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            repository_provider_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Executable,
            "Repository provider with valid origin");

    reset_preflight_stub();
    BuildPlan invalid_provider_package = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    invalid_provider_package.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_repository(
                            "aur", "invalid/provider")));
    return_build_plan(std::move(invalid_provider_package));
    AurUpdateExecutionPreflight invalid_provider_package_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            invalid_provider_package_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Provided dependency with invalid provider package");
    expect(
            has_issue(
                    invalid_provider_package_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Invalid provider package was accepted");

    reset_preflight_stub();
    BuildPlan missing_aur_provider_target = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    missing_aur_provider_target.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_aur("missing-provider")));
    return_build_plan(std::move(missing_aur_provider_target));
    AurUpdateExecutionPreflight missing_aur_provider_target_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            missing_aur_provider_target_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "AUR provider without planned target");
    expect(
            has_issue(
                    missing_aur_provider_target_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "AUR provider without matching target was accepted");

    reset_preflight_stub();
    BuildPlan orphan_repository_provider = build_plan_for({
            {"consistency-root", "consistency-root", "consistency-root"},
    });
    const RootTargetIdentity consistency_root{0, "consistency-root"};
    add_dependency_target(
            orphan_repository_provider, "repository-provider",
            "repository-provider", {consistency_root});
    orphan_repository_provider.order.insert(
            orphan_repository_provider.order.begin(),
            BuildPlanEntry{
                    "repository-provider", {"repository-provider"}});
    orphan_repository_provider.dependency_edges.push_back(
            provided_dependency_edge(
                    "consistency-root", "virtual-dependency",
                    ProvidedDependency::from_repository(
                            "aur", "repository-provider")));
    return_build_plan(std::move(orphan_repository_provider));
    AurUpdateExecutionPreflight orphan_repository_provider_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect_status(
            orphan_repository_provider_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Incomplete,
            "Repository provider with orphan source target");
    expect(
            has_issue(
                    orphan_repository_provider_preflight.targets.front(),
                    AurUpdateExecutionReason::BuildPlanInconsistent),
            "Repository provider incorrectly grounded a source target");
}

void test_rooted_aur_dependency_graph() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry("first-root", InstalledPackageReason::Explicit),
            remote_entry("second-root", InstalledPackageReason::Explicit),
    }};
    BuildPlan shared = build_plan_for({
            {"first-root", "first-root", "first-root"},
            {"second-root", "second-root", "second-root"},
    });
    const std::vector<RootTargetIdentity> roots{
            {0, "first-root"},
            {1, "second-root"},
    };
    add_dependency_target(
            shared, "shared-dependency", "shared-dependency", roots);
    shared.order.insert(
            shared.order.begin(),
            BuildPlanEntry{"shared-dependency", {"shared-dependency"}});
    for(const auto& root : roots) {
        shared.dependency_edges.push_back(BuildPlanDependencyEdge{
                root.requested_name,
                root.requested_name,
                "shared-dependency",
                PackageRole::RuntimeDependency,
                DependencyKind::Aur,
                std::optional<std::string>{"shared-dependency"},
                std::optional<std::string>{"shared-dependency"},
                std::nullopt});
    }
    return_build_plan(std::move(shared));

    AurUpdateExecutionPreflight shared_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    for(const auto& target : shared_preflight.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Executable,
                "Shared dependency rooted graph");
    }

    reset_preflight_stub();
    BuildPlan mutual_cycle = build_plan_for({
            {"first-root", "first-root", "first-root"},
            {"second-root", "second-root", "second-root"},
    });
    PlannedPackageTarget* first_root =
            find_package_target(mutual_cycle, "first-root");
    PlannedPackageTarget* second_root =
            find_package_target(mutual_cycle, "second-root");
    expect(
            first_root != nullptr && second_root != nullptr,
            "Mutual cycle root fixtures are missing");
    first_root->roles.push_back(PackageRole::RuntimeDependency);
    second_root->roles.push_back(PackageRole::RuntimeDependency);
    first_root->roots.push_back(roots[1]);
    second_root->roots.push_back(roots[0]);
    mutual_cycle.dependency_edges.push_back(BuildPlanDependencyEdge{
            "first-root",
            "first-root",
            "second-root",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"second-root"},
            std::optional<std::string>{"second-root"},
            std::nullopt});
    mutual_cycle.dependency_edges.push_back(BuildPlanDependencyEdge{
            "second-root",
            "second-root",
            "first-root",
            PackageRole::RuntimeDependency,
            DependencyKind::Aur,
            std::optional<std::string>{"first-root"},
            std::optional<std::string>{"first-root"},
            std::nullopt});
    // resolverのcycle summaryはSCC全memberではなく、再訪したPackageBaseを持つ。
    mutual_cycle.cycles.push_back("first-root");
    return_build_plan(std::move(mutual_cycle));

    AurUpdateExecutionPreflight mutual_cycle_preflight =
            resolve_aur_update_execution_preflight(update_plan);
    for(const auto& target : mutual_cycle_preflight.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Incomplete,
                "Mutual root cycle");
        expect(
                has_issue(target, AurUpdateExecutionReason::DependencyCycle),
                "Mutual root cycle was not attributed to every root");
        expect(
                !has_issue(
                        target,
                        AurUpdateExecutionReason::BuildPlanInconsistent),
                "Valid cycle summary was compared as an exact back-edge set");
    }

    reset_preflight_stub();
    AurUpdatePlan provider_plan{{remote_entry(
            "provider-root", InstalledPackageReason::Explicit)}};
    BuildPlan provider = build_plan_for({
            {"provider-root", "provider-root", "provider-root"},
    });
    add_dependency_target(
            provider, "aur-provider", "aur-provider",
            {{0, "provider-root"}});
    provider.order.insert(
            provider.order.begin(),
            BuildPlanEntry{"aur-provider", {"aur-provider"}});
    provider.dependency_edges.push_back(BuildPlanDependencyEdge{
            "provider-root",
            "provider-root",
            "virtual-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::Provided,
            std::nullopt,
            std::nullopt,
            ProvidedDependency::from_aur("aur-provider")});
    return_build_plan(std::move(provider));

    AurUpdateExecutionPreflight provider_preflight =
            resolve_aur_update_execution_preflight(provider_plan);
    expect_status(
            provider_preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Executable,
            "AUR provider rooted graph");
}

void test_ambiguous_provider_specification_normalization() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{
            remote_entry("first-root", InstalledPackageReason::Explicit),
            remote_entry("second-root", InstalledPackageReason::Explicit),
    }};
    BuildPlan plan = build_plan_for({
            {"first-root", "first-root", "first-root"},
            {"second-root", "second-root", "second-root"},
    });
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "first-root", "first-root", "shared-virtual",
            PackageRole::RuntimeDependency,
            DependencyKind::AmbiguousProvider,
            std::nullopt, std::nullopt, std::nullopt});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "second-root", "second-root", " shared-virtual ",
            PackageRole::RuntimeDependency,
            DependencyKind::AmbiguousProvider,
            std::nullopt, std::nullopt, std::nullopt});
    plan.ambiguous_providers.push_back(AmbiguousProvidedDependency{
            "shared-virtual",
            {
                    ProvidedDependency::from_repository(
                            "aur", "shared-provider"),
                    ProvidedDependency::from_aur("shared-provider"),
            }});
    return_build_plan(std::move(plan));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect(
            preflight.build_plan.has_value() &&
                    preflight.build_plan->ambiguous_providers.size() == 1,
            "Typed ambiguous provider summary is missing");
    expect(
            preflight.build_plan->ambiguous_providers.front().candidates ==
                    std::vector<ProvidedDependency>{
                            ProvidedDependency::from_repository(
                                    "aur", "shared-provider"),
                            ProvidedDependency::from_aur("shared-provider"),
                    },
            "Typed ambiguous provider candidates were reordered or deduplicated");
    for(const auto& target : preflight.targets) {
        expect_status(
                target, AurUpdateExecutionTargetStatus::Unsupported,
                "Normalized ambiguous provider attribution");
        expect(
                has_issue(target, AurUpdateExecutionReason::AmbiguousProvider),
                "Ambiguous provider did not reach every affected root");
    }
}

void test_skipped_identity_mismatch_is_incomplete() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{remote_entry(
            "installed-name", InstalledPackageReason::Unknown,
            AurUpdateClassification::UpToDate,
            "different-aur-name", "different-aur-name")}};

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);
    expect(resolver_call_count() == 0, "Up-to-date identity mismatch called resolver");
    expect_status(
            preflight.targets.front(), AurUpdateExecutionTargetStatus::Incomplete,
            "Up-to-date AUR identity mismatch");
    expect(
            has_issue(
                    preflight.targets.front(),
                    AurUpdateExecutionReason::UpdatePlanInconsistent),
            "Up-to-date identity mismatch was treated as a normal skip");
}

void test_invocation_helpers() {
    AurUpdateExecutionTarget executable;
    executable.status = AurUpdateExecutionTargetStatus::Executable;
    AurUpdateExecutionTarget skipped;
    skipped.status = AurUpdateExecutionTargetStatus::Skipped;
    AurUpdateExecutionTarget unsupported;
    unsupported.status = AurUpdateExecutionTargetStatus::Unsupported;
    AurUpdateExecutionTarget incomplete;
    incomplete.status = AurUpdateExecutionTargetStatus::Incomplete;

    AurUpdateExecutionPreflight executable_only{{executable, skipped}, std::nullopt};
    expect(has_executable_targets(executable_only), "Executable helper returned false");
    expect(!has_blocking_targets(executable_only), "Executable plan has a blocker");
    expect(can_execute(executable_only), "Executable plan could not execute");

    AurUpdateExecutionPreflight skip_only{{skipped}, std::nullopt};
    expect(!has_executable_targets(skip_only), "Skip-only helper found executable work");
    expect(!has_blocking_targets(skip_only), "Skip-only helper found a blocker");
    expect(!can_execute(skip_only), "Skip-only helper allowed execution");

    AurUpdateExecutionPreflight with_unsupported{
            {executable, unsupported}, std::nullopt};
    expect(has_blocking_targets(with_unsupported), "Unsupported blocker was not found");
    expect(!can_execute(with_unsupported), "Unsupported plan allowed execution");

    AurUpdateExecutionPreflight with_incomplete{
            {executable, incomplete}, std::nullopt};
    expect(has_blocking_targets(with_incomplete), "Incomplete blocker was not found");
    expect(!can_execute(with_incomplete), "Incomplete plan allowed execution");
}

void test_preflight_uses_combined_resolver_seam() {
    reset_preflight_stub();
    AurUpdatePlan update_plan{{remote_entry(
            "read-only-root", InstalledPackageReason::Explicit)}};
    return_build_plan(build_plan_for({
            {"read-only-root", "read-only-root", "read-only-root"},
    }));

    AurUpdateExecutionPreflight preflight =
            resolve_aur_update_execution_preflight(update_plan);

    expect_status(
            preflight.targets.front(),
            AurUpdateExecutionTargetStatus::Executable,
            "Resolver seam preflight");
    expect(
            resolver_call_count() == 1 &&
                    resolver_calls() ==
                            std::vector<std::vector<std::string>>{{"read-only-root"}},
            "Preflight did not use the combined resolver seam exactly once");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "classification order and combined resolution",
                test_classification_order_and_combined_resolution);
        run_case(
                "empty and skip-only plans suppress resolution",
                test_empty_and_skip_only_plans_suppress_resolution);
        run_case(
                "installed reason mapping and root/dependency overlap",
                test_installed_reason_mapping_and_root_dependency_overlap);
        run_case(
                "duplicate update targets suppress resolution",
                test_duplicate_update_targets_suppress_resolution);
        run_case(
                "update-plan and BuildPlan consistency",
                test_update_plan_and_build_plan_consistency);
        run_case(
                "incomplete BuildPlan issues are typed and deduplicated",
                test_incomplete_build_plan_issues_are_typed_and_deduplicated);
        run_case(
                "unsupported BuildPlan issues and same-base blind spot",
                test_unsupported_build_plan_issues_and_same_base_blind_spot);
        run_case(
                "incomplete status precedes unsupported and preserves both",
                test_incomplete_status_precedes_unsupported_and_preserves_both);
        run_case(
                "issue attribution and global fallback",
                test_issue_attribution_and_global_fallback);
        run_case(
                "resolution failure root validation",
                test_resolution_failure_root_validation);
        run_case(
                "fail-closed BuildPlan cross-field consistency",
                test_fail_closed_cross_field_consistency);
        run_case(
                "rooted AUR dependency graph",
                test_rooted_aur_dependency_graph);
        run_case(
                "ambiguous provider specification normalization",
                test_ambiguous_provider_specification_normalization);
        run_case(
                "skipped identity mismatch is incomplete",
                test_skipped_identity_mismatch_is_incomplete);
        run_case("invocation helpers", test_invocation_helpers);
        run_case(
                "preflight uses combined resolver seam",
                test_preflight_uses_combined_resolver_seam);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update execution preflight tests: all checks passed\n";
    return 0;
}
