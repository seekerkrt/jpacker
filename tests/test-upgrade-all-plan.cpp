#include "upgrade_all_plan.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename ExpectedException, typename Callable>
void expect_exception(Callable callable, const std::string& expected_message) {
    try {
        callable();
    } catch(const ExpectedException& error) {
        expect(
                std::string(error.what()) == expected_message,
                "Unexpected exception message: " + std::string(error.what()));
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Unexpected exception category: " + std::string(error.what()));
    }
    throw std::runtime_error("Expected exception: " + expected_message);
}

UpgradeAllPackageBaseIdentity resolved_base(const std::string& package_base) {
    return UpgradeAllResolvedPackageBase{package_base};
}

UpgradeAllSourceIdentity resolved_source(const std::string& key) {
    return UpgradeAllResolvedSourceIdentity{key};
}

UpgradeAllExplicitSourceIdentity explicit_source(
        const std::string& preference_name, const std::string& package_base,
        const std::string& source_key,
        std::vector<std::string> produced_names = {}) {
    return UpgradeAllExplicitSourceIdentity{
            preference_name,
            resolved_base(package_base),
            std::move(produced_names),
            resolved_source(source_key)};
}

UpgradeAllAurTarget candidate(
        const std::string& package_name, const std::string& package_base) {
    return UpgradeAllAurTarget{
            package_name,
            resolved_base(package_base),
            UpgradeAllAurTargetStatus::Candidate,
            {}};
}

UpgradeAllBuildUnitRootAttribution root(
        std::size_t target_index,
        UpgradeAllBuildUnitRole role = UpgradeAllBuildUnitRole::Root) {
    return UpgradeAllBuildUnitRootAttribution{target_index, role};
}

UpgradeAllAurBuildUnit build_unit(
        const std::string& package_base, std::vector<std::string> package_names,
        std::vector<UpgradeAllBuildUnitRootAttribution> roots) {
    return UpgradeAllAurBuildUnit{
            resolved_base(package_base),
            std::move(package_names),
            std::move(roots)};
}

bool has_issue(
        const UpgradeAllPlan& plan, UpgradeAllPlanningIssueKind expected_kind) {
    for(const UpgradeAllPlanningIssue& issue : plan.issues) {
        if(issue.kind == expected_kind) return true;
    }
    return false;
}

const UpgradeAllPlanningIssue& require_issue(
        const UpgradeAllPlan& plan, UpgradeAllPlanningIssueKind expected_kind) {
    const UpgradeAllPlanningIssue* found = nullptr;
    for(const UpgradeAllPlanningIssue& issue : plan.issues) {
        if(issue.kind != expected_kind) continue;
        if(found != nullptr) {
            throw std::runtime_error("Expected exactly one planning issue of this kind");
        }
        found = &issue;
    }
    if(found == nullptr) throw std::runtime_error("Expected planning issue was not found");
    return *found;
}

const UpgradeAllTargetPlanEntry& target_at(
        const UpgradeAllPlan& plan, std::size_t original_index) {
    expect(
            original_index < plan.target_dispositions.size(),
            "Target disposition index is out of range");
    const UpgradeAllTargetPlanEntry& target = plan.target_dispositions[original_index];
    expect(
            target.original_target_index == original_index,
            "Target disposition lost original order");
    return target;
}

const UpgradeAllBuildUnitPlanEntry& build_unit_at(
        const UpgradeAllPlan& plan, std::size_t original_index) {
    expect(
            original_index < plan.build_unit_dispositions.size(),
            "Build-unit disposition index is out of range");
    const UpgradeAllBuildUnitPlanEntry& unit =
            plan.build_unit_dispositions[original_index];
    expect(
            unit.original_build_plan_index == original_index,
            "Build-unit disposition lost BuildPlan order");
    return unit;
}

void expect_no_issues(const UpgradeAllPlan& plan) {
    expect(plan.issues.empty(), "Plan unexpectedly contains planning issues");
    expect(!has_upgrade_all_planning_issues(plan), "Issue helper returned true");
}

void test_empty_explicit_set_selects_complete_aur_inputs() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {},
            {candidate("alpha", "alpha")},
            {build_unit("alpha", {"alpha"}, {root(0)})}});

    expect_no_issues(plan);
    expect(plan.selected_targets.size() == 1, "AUR target was not selected");
    expect(plan.selected_build_units.size() == 1, "AUR build unit was not selected");
    expect(
            target_at(plan, 0).disposition == UpgradeAllTargetDisposition::Selected,
            "Target disposition differs");
}

void test_empty_aur_target_set_is_a_valid_empty_plan() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("explicit", "explicit", "source:explicit")}, {}, {}});

    expect_no_issues(plan);
    expect(plan.target_dispositions.empty(), "Empty target plan is not empty");
    expect(plan.original_to_selected_index.empty(), "Empty mapping is not empty");
    expect(plan.build_unit_dispositions.empty(), "Empty BuildPlan is not empty");
}

void test_no_duplicates_preserves_all_selected_identities() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("other", "other", "source:other")},
            {candidate("alpha", "alpha"), candidate("beta", "beta")},
            {build_unit("alpha", {"alpha"}, {root(0)}),
             build_unit("beta", {"beta"}, {root(1)})}});

    expect_no_issues(plan);
    expect(plan.selected_targets.size() == 2, "Selected target count differs");
    expect(plan.selected_targets[0].package_name == "alpha", "First target reordered");
    expect(plan.selected_targets[1].package_name == "beta", "Second target reordered");
    expect(plan.selected_build_units.size() == 2, "Selected build-unit count differs");
}

void test_exact_preference_package_name_excludes_target() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("sample", "source-base", "source:sample")},
            {candidate("sample", "aur-base")},
            {}});

    expect_no_issues(plan);
    const UpgradeAllTargetPlanEntry& target = target_at(plan, 0);
    expect(
            target.disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Exact package-name duplicate was not excluded");
    expect(target.explicit_source.has_value(), "Exclusion lost source attribution");
    expect(
            target.explicit_source->explicit_source_indexes ==
                    std::vector<std::size_t>{0},
            "Exclusion source index differs");
    expect(
            target.explicit_source->matched_package_name ==
                    std::optional<std::string>{"sample"},
            "Matched package name was not preserved");
}

void test_build_unit_reachable_only_from_excluded_target_is_not_required() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("sample", "source-base", "source:sample")},
            {candidate("sample", "aur-base")},
            {build_unit("aur-base", {"sample"}, {root(0)})}});

    expect_no_issues(plan);
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Root target was not excluded by its explicit package name");
    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::NotRequiredBySelectedTarget,
            "Build unit reachable only from an excluded target remained executable");
    expect(
            plan.selected_build_units.empty(),
            "Excluded target left an AUR execution build unit");
}

void test_produced_package_name_is_an_exact_match() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source(
                    "suite", "suite", "source:suite", {"suite-cli", "suite-lib"})},
            {candidate("suite-lib", "unrelated-base")},
            {}});

    expect_no_issues(plan);
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Produced package name was not matched exactly");
}

void test_root_package_base_match_excludes_target() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("suite", "suite", "source:suite")},
            {candidate("suite-cli", "suite")},
            {}});

    expect_no_issues(plan);
    const UpgradeAllTargetPlanEntry& target = target_at(plan, 0);
    expect(
            target.disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase,
            "PackageBase duplicate was not excluded");
    expect(
            target.explicit_source->matched_package_base ==
                    std::optional<std::string>{"suite"},
            "Matched PackageBase was not preserved");
}

void test_split_package_targets_are_all_attributed_to_one_explicit_base() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("suite", "suite", "source:suite")},
            {candidate("suite-cli", "suite"), candidate("suite-lib", "suite")},
            {}});

    expect_no_issues(plan);
    expect(
            plan.excluded_duplicate_target_indexes ==
                    std::vector<std::size_t>({0, 1}),
            "Split target exclusions differ");
    for(std::size_t index = 0; index < 2; ++index) {
        const UpgradeAllTargetPlanEntry& target = target_at(plan, index);
        expect(
                target.disposition ==
                        UpgradeAllTargetDisposition::
                                ExcludedByExplicitPackageBase,
                "Split target was not excluded by PackageBase");
        expect(
                target.explicit_source->explicit_source_indexes ==
                        std::vector<std::size_t>{0},
                "Split target attribution differs");
    }
}

void test_semantically_identical_explicit_entries_are_normalized() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("suite", "suite", "source:suite"),
             explicit_source("suite-devel", "suite", "source:suite")},
            {candidate("suite-cli", "suite")},
            {}});

    expect_no_issues(plan);
    const UpgradeAllTargetPlanEntry& target = target_at(plan, 0);
    expect(
            target.disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase,
            "Equivalent explicit entries did not exclude the target");
    expect(
            target.explicit_source->explicit_source_indexes ==
                    std::vector<std::size_t>({0, 1}),
            "Equivalent explicit attribution was not normalized");
    expect(
            target.explicit_source->source_identity_keys ==
                    std::vector<std::string>{"source:suite"},
            "Equivalent source identity key was duplicated");
}

void test_one_explicit_base_excludes_multiple_unrelated_target_names() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("suite", "suite", "source:suite")},
            {candidate("first-output", "suite"),
             candidate("second-output", "suite"),
             candidate("third-output", "suite")},
            {}});

    expect_no_issues(plan);
    expect(plan.selected_targets.empty(), "Duplicate targets remained selected");
    expect(
            plan.excluded_duplicate_target_indexes ==
                    std::vector<std::size_t>({0, 1, 2}),
            "Affected target attribution differs");
}

void test_transitive_build_unit_is_externally_satisfied() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("shared-lib", "shared-lib", "source:shared-lib")},
            {candidate("application", "application")},
            {build_unit(
                     "shared-lib", {"shared-lib"},
                     {root(0, UpgradeAllBuildUnitRole::RuntimeDependency)}),
             build_unit("application", {"application"}, {root(0)})}});

    expect_no_issues(plan);
    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::
                            ExternallySatisfiedByExplicitSourcePackageBase,
            "Transitive unit was not externally satisfied");
    expect(
            plan.externally_satisfied_build_unit_indexes ==
                    std::vector<std::size_t>{0},
            "External build-unit index differs");
    expect(
            plan.externally_satisfied_package_bases ==
                    std::vector<std::string>{"shared-lib"},
            "Externally satisfied PackageBase differs");
    expect(plan.selected_build_units.size() == 1, "Execution build-unit count differs");
    expect(
            plan.selected_build_units[0].original_build_plan_index == 1,
            "Execution order did not compact around external unit");
}

void test_transitive_package_name_with_different_base_fails_closed() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source(
                    "shared-lib", "source-base", "source:shared-lib")},
            {candidate("application", "application")},
            {build_unit(
                    "aur-base", {"shared-lib", "application-helper"},
                    {root(0, UpgradeAllBuildUnitRole::RuntimeDependency)})}});

    expect(
            target_at(plan, 0).disposition == UpgradeAllTargetDisposition::Selected,
            "Unrelated AUR root was not selected");
    const UpgradeAllBuildUnitPlanEntry& unit = build_unit_at(plan, 0);
    expect(
            unit.disposition == UpgradeAllBuildUnitDisposition::
                    ConflictingExplicitSourceIdentity,
            "Transitive package-name overlap remained executable");
    expect(
            unit.explicit_source.has_value(),
            "Transitive package-name conflict lost explicit-source attribution");
    expect(
            unit.explicit_source->explicit_source_indexes ==
                    std::vector<std::size_t>{0},
            "Transitive package-name conflict source attribution differs");
    expect(
            unit.explicit_source->matched_package_name ==
                    std::optional<std::string>{"shared-lib"},
            "Transitive exact package-name match was not retained");
    expect(
            plan.selected_build_units.empty(),
            "Conflicting transitive build unit remained selected");
    expect(
            plan.externally_satisfied_build_unit_indexes.empty(),
            "Mismatched PackageBase was incorrectly treated as external");

    const UpgradeAllPlanningIssue& issue = require_issue(
            plan, UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase);
    expect(
            issue.explicit_source_indexes == std::vector<std::size_t>{0} &&
                    issue.original_target_indexes ==
                            std::vector<std::size_t>{0} &&
                    issue.build_unit_indexes == std::vector<std::size_t>{0},
            "Transitive conflict index attribution differs");
    expect(
            issue.package_name == std::optional<std::string>{"shared-lib"} &&
                    issue.package_base == std::optional<std::string>{"aur-base"},
            "Transitive conflict identity attribution differs");
}

void test_one_transitive_unit_preserves_multiple_root_attributions() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("shared-lib", "shared-lib", "source:shared-lib")},
            {candidate("first-app", "first-app"),
             candidate("second-app", "second-app")},
            {build_unit(
                    "shared-lib", {"shared-lib"},
                    {root(0, UpgradeAllBuildUnitRole::RuntimeDependency),
                     root(1, UpgradeAllBuildUnitRole::BuildDependency)})}});

    expect_no_issues(plan);
    const UpgradeAllBuildUnitPlanEntry& unit = build_unit_at(plan, 0);
    expect(
            unit.disposition ==
                    UpgradeAllBuildUnitDisposition::
                            ExternallySatisfiedByExplicitSourcePackageBase,
            "Shared transitive unit was not external");
    expect(
            unit.build_unit.root_attributions.size() == 2,
            "Affected roots were not preserved");
    expect(
            unit.build_unit.root_attributions[0].role ==
                    UpgradeAllBuildUnitRole::RuntimeDependency,
            "First dependency role differs");
    expect(
            unit.build_unit.root_attributions[1].role ==
                    UpgradeAllBuildUnitRole::BuildDependency,
            "Second dependency role differs");
}

void test_root_and_transitive_matches_are_both_retained() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source(
                    "suite", "suite", "source:suite", {"suite-root"})},
            {candidate("suite-root", "suite"), candidate("application", "application")},
            {build_unit("suite", {"suite-root"}, {root(0)}),
             build_unit(
                     "suite", {"suite-lib"},
                     {root(1, UpgradeAllBuildUnitRole::RuntimeDependency)}),
             build_unit("application", {"application"}, {root(1)})}});

    expect_no_issues(plan);
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Root target match was not retained");
    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::
                            ExternallySatisfiedByExplicitSourcePackageBase,
            "Root build unit match was not retained");
    expect(
            build_unit_at(plan, 1).disposition ==
                    UpgradeAllBuildUnitDisposition::
                            ExternallySatisfiedByExplicitSourcePackageBase,
            "Transitive build unit match was not retained");
    expect(
            plan.externally_satisfied_build_unit_indexes ==
                    std::vector<std::size_t>({0, 1}),
            "Root/transitive external indexes differ");
}

void test_explicit_package_base_absence_preserves_safe_name_match() {
    UpgradeAllExplicitSourceIdentity source{
            "sample",
            UpgradeAllPackageBaseAbsent{},
            {},
            resolved_source("source:sample")};
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {source},
            {candidate("sample", "remote-base"), candidate("other", "other")},
            {}});

    expect(has_upgrade_all_planning_issues(plan), "Missing explicit base was not reported");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::ExplicitPackageBaseAbsent),
            "Explicit PackageBase absence issue is missing");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Known exact-name match was lost");
    expect(
            target_at(plan, 1).disposition == UpgradeAllTargetDisposition::Selected,
            "Unknown base was guessed into a duplicate exclusion");
}

void test_explicit_package_base_failure_is_distinct_from_absence() {
    UpgradeAllExplicitSourceIdentity source{
            "sample",
            UpgradeAllPackageBaseResolutionFailed{"source metadata failed"},
            {},
            resolved_source("source:sample")};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {source}, {candidate("sample", "remote-base")});

    expect(
            has_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::
                            ExplicitPackageBaseResolutionFailed),
            "Explicit PackageBase resolution failure was not preserved");
    expect(
            !has_issue(plan, UpgradeAllPlanningIssueKind::ExplicitPackageBaseAbsent),
            "Explicit PackageBase failure was flattened to absence");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Known name disposition was lost alongside failure");
}

void test_unresolved_explicit_source_identity_fails_closed_on_known_overlap() {
    UpgradeAllExplicitSourceIdentity source{
            "sample",
            resolved_base("sample"),
            {},
            UpgradeAllSourceIdentityAbsent{}};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {source}, {candidate("sample", "sample")});

    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::ExplicitSourceIdentityAbsent),
            "Missing source identity was not reported");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity,
            "Unresolved source identity allowed a known overlap to execute");
    expect(
            target_at(plan, 0).explicit_source.has_value(),
            "Known overlap lost its incomplete explicit-source attribution");
    expect(
            target_at(plan, 0).explicit_source->explicit_source_indexes ==
                    std::vector<std::size_t>{0},
            "Incomplete explicit-source index attribution differs");
    expect(
            target_at(plan, 0).explicit_source->source_identity_keys.empty(),
            "Unresolved source identity acquired a guessed key");
}

void test_aur_target_package_base_absence_is_identity_incomplete() {
    UpgradeAllAurTarget target{
            "unknown-base",
            UpgradeAllPackageBaseAbsent{},
            UpgradeAllAurTargetStatus::Candidate,
            "remote identity unavailable"};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan({}, {target});

    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::IdentityIncomplete,
            "Target with missing PackageBase was selected");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent),
            "Missing target PackageBase issue is absent");
}

void test_aur_target_package_base_failure_remains_distinct() {
    UpgradeAllAurTarget target{
            "failed-base",
            UpgradeAllPackageBaseResolutionFailed{"AUR metadata failed"},
            UpgradeAllAurTargetStatus::Candidate,
            "query failed"};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan({}, {target});

    expect(
            has_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::
                            AurTargetPackageBaseResolutionFailed),
            "Target PackageBase resolution failure is absent");
    expect(
            !has_issue(plan, UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent),
            "Target PackageBase failure was flattened to absence");
}

void test_missing_aur_target_package_name_is_identity_incomplete() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {}, {candidate("", "resolved-base")});

    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::IdentityIncomplete,
            "Target with a missing package name was selected");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::AurTargetPackageNameMissing),
            "Missing target package-name issue is absent");
}

void test_build_unit_package_base_absence_is_identity_incomplete() {
    UpgradeAllAurBuildUnit unit{
            UpgradeAllPackageBaseAbsent{}, {"unknown"}, {root(0)}};
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {}, {candidate("application", "application")}, {unit}});

    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::IdentityIncomplete,
            "Build unit with missing PackageBase was selected");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::BuildUnitPackageBaseAbsent),
            "Missing build-unit PackageBase issue is absent");
}

void test_unsupported_and_incomplete_targets_keep_status_details() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {},
            {UpgradeAllAurTarget{
                     "unsupported",
                     resolved_base("unsupported"),
                     UpgradeAllAurTargetStatus::Unsupported,
                     "split selection required"},
             UpgradeAllAurTarget{
                     "incomplete",
                     resolved_base("incomplete"),
                     UpgradeAllAurTargetStatus::Incomplete,
                     "provider identity unavailable"}});

    expect(
            target_at(plan, 0).disposition == UpgradeAllTargetDisposition::Unsupported,
            "Unsupported target disposition differs");
    expect(
            target_at(plan, 1).disposition ==
                    UpgradeAllTargetDisposition::IdentityIncomplete,
            "Incomplete target disposition differs");
    expect(
            target_at(plan, 0).target.status_detail == "split selection required",
            "Unsupported target detail was lost");
    expect(
            target_at(plan, 1).target.status_detail ==
                    "provider identity unavailable",
            "Incomplete target detail was lost");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::UnsupportedAurTarget),
            "Unsupported target issue is absent");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::IncompleteAurTarget),
            "Incomplete target issue is absent");
}

void test_incomplete_target_keeps_exact_package_name_exclusion() {
    UpgradeAllAurTarget target{
            "sample",
            UpgradeAllPackageBaseAbsent{},
            UpgradeAllAurTargetStatus::Incomplete,
            "PackageBase query incomplete"};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("sample", "source-base", "source:sample")},
            {target});

    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Incomplete target lost a reliable package-name exclusion");
    expect(
            plan.excluded_duplicate_target_indexes ==
                    std::vector<std::size_t>{0},
            "Incomplete duplicate target was not retained in exclusions");
    expect(
            target_at(plan, 0).explicit_source->matched_package_name == "sample",
            "Incomplete target lost package-name attribution");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::IncompleteAurTarget),
            "Incomplete target issue was discarded by exclusion");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent),
            "Incomplete target identity issue was discarded by exclusion");
    expect(
            target_at(plan, 0).target.status_detail ==
                    "PackageBase query incomplete",
            "Incomplete target detail was discarded by exclusion");
}

void test_unsupported_target_keeps_exact_package_base_exclusion() {
    UpgradeAllAurTarget target{
            "aur-name",
            resolved_base("shared-base"),
            UpgradeAllAurTargetStatus::Unsupported,
            "unsupported candidate shape"};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("source-name", "shared-base", "source:shared")},
            {target});

    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase,
            "Unsupported target lost a reliable PackageBase exclusion");
    expect(
            plan.excluded_duplicate_target_indexes ==
                    std::vector<std::size_t>{0},
            "Unsupported duplicate target was not retained in exclusions");
    expect(
            target_at(plan, 0).explicit_source->matched_package_base ==
                    std::optional<std::string>{"shared-base"},
            "Unsupported target lost PackageBase attribution");
    expect(
            has_issue(plan, UpgradeAllPlanningIssueKind::UnsupportedAurTarget),
            "Unsupported target issue was discarded by exclusion");
    expect(
            target_at(plan, 0).target.status_detail ==
                    "unsupported candidate shape",
            "Unsupported target detail was discarded by exclusion");
}

void test_original_target_order_is_preserved() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {},
            {candidate("zeta", "zeta"), candidate("alpha", "alpha"),
             candidate("middle", "middle")});

    expect_no_issues(plan);
    expect(target_at(plan, 0).target.package_name == "zeta", "First target moved");
    expect(target_at(plan, 1).target.package_name == "alpha", "Second target moved");
    expect(target_at(plan, 2).target.package_name == "middle", "Third target moved");
}

void test_build_plan_order_is_preserved_after_filtering() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {explicit_source("external", "external", "source:external")},
            {candidate("application", "application")},
            {build_unit(
                     "first-dependency", {"first-dependency"},
                     {root(0, UpgradeAllBuildUnitRole::RuntimeDependency)}),
             build_unit(
                     "external", {"external"},
                     {root(0, UpgradeAllBuildUnitRole::BuildDependency)}),
             build_unit("application", {"application"}, {root(0)})}});

    expect_no_issues(plan);
    expect(
            plan.build_unit_dispositions[0].build_unit.package_names[0] ==
                    "first-dependency",
            "First BuildPlan unit moved");
    expect(
            plan.build_unit_dispositions[1].build_unit.package_names[0] ==
                    "external",
            "Second BuildPlan unit moved");
    expect(
            plan.build_unit_dispositions[2].build_unit.package_names[0] ==
                    "application",
            "Third BuildPlan unit moved");
    expect(
            plan.selected_build_units[0].original_build_plan_index == 0 &&
                    plan.selected_build_units[1].original_build_plan_index == 2,
            "Selected BuildPlan order mapping differs");
}

void test_original_to_selected_index_mapping_is_dense_and_unique() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("first", "first", "source:first"),
             explicit_source("third", "third", "source:third")},
            {candidate("first", "first"), candidate("second", "second"),
             candidate("third-child", "third"), candidate("fourth", "fourth")});

    expect_no_issues(plan);
    expect(
            plan.original_to_selected_index ==
                    std::vector<std::optional<std::size_t>>(
                            {std::nullopt, 0, std::nullopt, 1}),
            "Original-to-selected mapping differs");
    expect(
            plan.selected_targets[0].original_target_index == 1 &&
                    plan.selected_targets[1].original_target_index == 3,
            "Selected-to-original mapping differs");
}

void test_duplicate_selected_target_package_base_is_rejected() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {},
            {candidate("split-cli", "split-suite"),
             candidate("split-lib", "split-suite")});

    expect(
            plan.selected_targets.empty(),
            "Duplicate selected PackageBase remained executable");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ConflictingSelectedPackageBase &&
                    target_at(plan, 1).disposition ==
                            UpgradeAllTargetDisposition::
                                    ConflictingSelectedPackageBase,
            "Duplicate selected targets did not fail closed");
    const UpgradeAllPlanningIssue& issue = require_issue(
            plan,
            UpgradeAllPlanningIssueKind::DuplicateSelectedTargetPackageBase);
    expect(
            issue.original_target_indexes == std::vector<std::size_t>({0, 1}),
            "Duplicate target issue attribution differs");
}

void test_duplicate_selected_build_unit_package_base_is_rejected() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {},
            {candidate("application", "application")},
            {build_unit("shared", {"shared-a"}, {root(0)}),
             build_unit("shared", {"shared-b"}, {root(0)})}});

    expect(
            plan.selected_build_units.empty(),
            "Duplicate build-unit PackageBase remained executable");
    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::
                            ConflictingSelectedPackageBase &&
                    build_unit_at(plan, 1).disposition ==
                            UpgradeAllBuildUnitDisposition::
                                    ConflictingSelectedPackageBase,
            "Duplicate build units did not fail closed");
    const UpgradeAllPlanningIssue& issue = require_issue(
            plan,
            UpgradeAllPlanningIssueKind::DuplicateSelectedBuildUnitPackageBase);
    expect(
            issue.build_unit_indexes == std::vector<std::size_t>({0, 1}),
            "Duplicate build-unit issue attribution differs");
}

void test_conflicting_explicit_identities_fail_closed() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("sample", "shared", "source:first"),
             explicit_source("sample", "shared", "source:second")},
            {candidate("sample", "shared")});

    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity,
            "Conflicting explicit identities selected an arbitrary owner");
    expect(
            target_at(plan, 0).explicit_source->explicit_source_indexes ==
                    std::vector<std::size_t>({0, 1}),
            "Conflicting explicit attribution differs");
    expect(
            has_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::ConflictingExplicitPackageName),
            "Conflicting package-name issue is absent");
    expect(
            has_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase),
            "Conflicting PackageBase issue is absent");
}

void test_one_source_identity_with_conflicting_bases_fails_closed() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("sample", "first-base", "source:sample"),
             explicit_source("sample-alt", "second-base", "source:sample")},
            {candidate("sample", "first-base")});

    expect(
            has_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::
                            ConflictingExplicitSourceIdentityDefinition),
            "Conflicting source definition issue is absent");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity,
            "Conflicting source definition was used for exclusion");
}

void test_known_dispositions_survive_planning_issues() {
    UpgradeAllExplicitSourceIdentity source{
            "known",
            UpgradeAllPackageBaseResolutionFailed{"metadata failed"},
            {},
            resolved_source("source:known")};
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {source}, {candidate("known", "remote"), candidate("safe", "safe")});

    expect(has_upgrade_all_planning_issues(plan), "Expected issue is absent");
    expect(plan.target_dispositions.size() == 2, "Partial target result was lost");
    expect(
            target_at(plan, 0).disposition ==
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName,
            "Known exclusion was lost");
    expect(
            target_at(plan, 1).disposition == UpgradeAllTargetDisposition::Selected,
            "Known selection was lost");
}

void test_out_of_range_build_attribution_is_typed_not_thrown() {
    const UpgradeAllPlan plan = make_upgrade_all_plan(UpgradeAllPlanInput{
            {},
            {candidate("application", "application")},
            {build_unit("dependency", {"dependency"}, {root(99)})}});

    expect(
            target_at(plan, 0).disposition == UpgradeAllTargetDisposition::Selected,
            "Known target disposition was lost");
    expect(
            build_unit_at(plan, 0).disposition ==
                    UpgradeAllBuildUnitDisposition::IdentityIncomplete,
            "Bad build-unit correlation was not retained");
    const UpgradeAllPlanningIssue& issue = require_issue(
            plan, UpgradeAllPlanningIssueKind::BuildUnitTargetIndexOutOfRange);
    expect(
            issue.original_target_indexes == std::vector<std::size_t>{99},
            "Out-of-range target attribution differs");
}

void test_package_matching_is_exact_not_partial() {
    const UpgradeAllPlan plan = make_upgrade_all_target_plan(
            {explicit_source("sample", "sample", "source:sample")},
            {candidate("sample-extra", "sample-extra")});

    expect_no_issues(plan);
    expect(
            target_at(plan, 0).disposition == UpgradeAllTargetDisposition::Selected,
            "Partial package-name match caused an exclusion");
}

void test_planner_owns_values_and_does_not_mutate_input() {
    std::vector<UpgradeAllExplicitSourceIdentity> sources = {
            explicit_source("explicit", "explicit", "source:explicit")};
    std::vector<UpgradeAllAurTarget> targets = {candidate("selected", "selected")};
    std::vector<UpgradeAllAurBuildUnit> units = {
            build_unit("selected", {"selected"}, {root(0)})};

    const UpgradeAllPlan target_plan = make_upgrade_all_target_plan(sources, targets);
    sources[0].preference_package_name = "mutated-source";
    targets[0].package_name = "mutated-target";
    const UpgradeAllPlan complete_plan =
            complete_upgrade_all_build_unit_plan(target_plan, units);
    units[0].package_names[0] = "mutated-unit";

    expect(
            complete_plan.explicit_sources[0].preference_package_name == "explicit",
            "Planner retained a reference to explicit input");
    expect(
            target_at(complete_plan, 0).target.package_name == "selected",
            "Planner retained a reference to target input");
    expect(
            build_unit_at(complete_plan, 0).build_unit.package_names[0] ==
                    "selected",
            "Planner retained a reference to BuildPlan input");
}

void test_staged_and_combined_planning_are_equivalent() {
    const UpgradeAllPlanInput input{
            {explicit_source("external", "external", "source:external")},
            {candidate("application", "application")},
            {build_unit(
                     "external", {"external"},
                     {root(0, UpgradeAllBuildUnitRole::RuntimeDependency)}),
             build_unit("application", {"application"}, {root(0)})}};

    const UpgradeAllPlan combined = make_upgrade_all_plan(input);
    const UpgradeAllPlan staged = complete_upgrade_all_build_unit_plan(
            make_upgrade_all_target_plan(input.explicit_sources, input.aur_targets),
            input.build_units);

    expect(
            combined.selected_targets.size() == staged.selected_targets.size(),
            "Staged target result differs");
    expect(
            combined.original_to_selected_index == staged.original_to_selected_index,
            "Staged target mapping differs");
    expect(
            combined.externally_satisfied_build_unit_indexes ==
                    staged.externally_satisfied_build_unit_indexes,
            "Staged external-unit result differs");
    expect(
            combined.selected_build_units.size() ==
                    staged.selected_build_units.size(),
            "Staged execution-unit result differs");
}

void test_invalid_enum_is_a_programming_error() {
    UpgradeAllAurTarget target = candidate("sample", "sample");
    target.status = static_cast<UpgradeAllAurTargetStatus>(99);

    expect_exception<std::logic_error>(
            [&target]() {
                static_cast<void>(make_upgrade_all_target_plan({}, {target}));
            },
            "Unknown upgrade-all AUR target status.");
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
                "empty explicit set selects complete AUR inputs",
                test_empty_explicit_set_selects_complete_aur_inputs);
        run_case(
                "empty AUR target set is a valid empty plan",
                test_empty_aur_target_set_is_a_valid_empty_plan);
        run_case(
                "no duplicates preserves all selected identities",
                test_no_duplicates_preserves_all_selected_identities);
        run_case(
                "exact preference package name excludes target",
                test_exact_preference_package_name_excludes_target);
        run_case(
                "excluded target leaves no required build unit",
                test_build_unit_reachable_only_from_excluded_target_is_not_required);
        run_case(
                "produced package name is an exact match",
                test_produced_package_name_is_an_exact_match);
        run_case(
                "root PackageBase match excludes target",
                test_root_package_base_match_excludes_target);
        run_case(
                "split package targets share explicit attribution",
                test_split_package_targets_are_all_attributed_to_one_explicit_base);
        run_case(
                "semantically identical explicit entries are normalized",
                test_semantically_identical_explicit_entries_are_normalized);
        run_case(
                "one explicit base excludes multiple target names",
                test_one_explicit_base_excludes_multiple_unrelated_target_names);
        run_case(
                "transitive build unit is externally satisfied",
                test_transitive_build_unit_is_externally_satisfied);
        run_case(
                "transitive package-name/base mismatch fails closed",
                test_transitive_package_name_with_different_base_fails_closed);
        run_case(
                "one transitive unit preserves multiple roots",
                test_one_transitive_unit_preserves_multiple_root_attributions);
        run_case(
                "root and transitive matches are retained",
                test_root_and_transitive_matches_are_both_retained);
        run_case(
                "explicit PackageBase absence preserves safe name match",
                test_explicit_package_base_absence_preserves_safe_name_match);
        run_case(
                "explicit PackageBase failure remains distinct",
                test_explicit_package_base_failure_is_distinct_from_absence);
        run_case(
                "unresolved explicit source identity fails closed",
                test_unresolved_explicit_source_identity_fails_closed_on_known_overlap);
        run_case(
                "AUR target PackageBase absence is incomplete",
                test_aur_target_package_base_absence_is_identity_incomplete);
        run_case(
                "AUR target PackageBase failure remains distinct",
                test_aur_target_package_base_failure_remains_distinct);
        run_case(
                "missing AUR target package name is incomplete",
                test_missing_aur_target_package_name_is_identity_incomplete);
        run_case(
                "build unit PackageBase absence is incomplete",
                test_build_unit_package_base_absence_is_identity_incomplete);
        run_case(
                "unsupported and incomplete targets retain details",
                test_unsupported_and_incomplete_targets_keep_status_details);
        run_case(
                "incomplete target keeps exact package-name exclusion",
                test_incomplete_target_keeps_exact_package_name_exclusion);
        run_case(
                "unsupported target keeps exact PackageBase exclusion",
                test_unsupported_target_keeps_exact_package_base_exclusion);
        run_case("original target order is preserved", test_original_target_order_is_preserved);
        run_case(
                "BuildPlan order is preserved after filtering",
                test_build_plan_order_is_preserved_after_filtering);
        run_case(
                "original-to-selected mapping is dense and unique",
                test_original_to_selected_index_mapping_is_dense_and_unique);
        run_case(
                "duplicate selected target PackageBase is rejected",
                test_duplicate_selected_target_package_base_is_rejected);
        run_case(
                "duplicate selected build-unit PackageBase is rejected",
                test_duplicate_selected_build_unit_package_base_is_rejected);
        run_case(
                "conflicting explicit identities fail closed",
                test_conflicting_explicit_identities_fail_closed);
        run_case(
                "one source identity with conflicting bases fails closed",
                test_one_source_identity_with_conflicting_bases_fails_closed);
        run_case(
                "known dispositions survive planning issues",
                test_known_dispositions_survive_planning_issues);
        run_case(
                "out-of-range build attribution is typed",
                test_out_of_range_build_attribution_is_typed_not_thrown);
        run_case(
                "package matching is exact, not partial",
                test_package_matching_is_exact_not_partial);
        run_case(
                "planner owns values and does not mutate input",
                test_planner_owns_values_and_does_not_mutate_input);
        run_case(
                "staged and combined planning are equivalent",
                test_staged_and_combined_planning_are_equivalent);
        run_case(
                "invalid enum is a programming error",
                test_invalid_enum_is_a_programming_error);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "upgrade-all plan tests: all checks passed\n";
    return 0;
}
