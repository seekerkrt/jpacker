#include "build_plan_artifact_target_projection.hpp"

#include "localization.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

BuildPlanArtifactTargetProjectionIssue make_issue(
        BuildPlanArtifactTargetProjectionIssueKind kind,
        const std::string& diagnostic) {
    BuildPlanArtifactTargetProjectionIssue issue;
    issue.kind = kind;
    issue.diagnostic = diagnostic;
    return issue;
}

bool has_role(const PlannedPackageTarget& target, PackageRole role) {
    return std::find(target.roles.begin(), target.roles.end(), role) !=
            target.roles.end();
}

bool is_known_package_role(PackageRole role) noexcept {
    switch(role) {
    case PackageRole::Root:
    case PackageRole::RuntimeDependency:
    case PackageRole::BuildDependency:
    case PackageRole::CheckDependency:
        return true;
    }
    return false;
}

bool has_known_reason_roles(const PlannedPackageTarget& target) noexcept {
    return !target.roles.empty() &&
            std::all_of(
                    target.roles.begin(), target.roles.end(),
                    is_known_package_role);
}

bool is_known_desired_install_reason(DesiredInstallReason reason) noexcept {
    switch(reason) {
    case DesiredInstallReason::Explicit:
    case DesiredInstallReason::Dependency:
        return true;
    }
    return false;
}

bool root_occurs_exactly_once(
        const std::vector<RootTargetIdentity>& roots,
        const RootTargetIdentity& expected) {
    return std::count(roots.begin(), roots.end(), expected) == 1;
}

bool target_contains_root_exactly_once(
        const PlannedPackageTarget& target,
        const RootTargetIdentity& expected) {
    return root_occurs_exactly_once(target.roots, expected);
}

bool validate_target_root_attribution(
        const BuildPlan& plan, const PlannedPackageTarget& target,
        std::size_t target_index,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    bool is_consistent = !target.roots.empty();
    for(std::size_t root_index = 0; root_index < target.roots.size();
        ++root_index) {
        const RootTargetIdentity& attributed_root = target.roots[root_index];
        if(!is_valid_package_name(attributed_root.requested_name) ||
           !root_occurs_exactly_once(plan.root_targets, attributed_root)) {
            is_consistent = false;
        }
        for(std::size_t prior = 0; prior < root_index; ++prior) {
            if(target.roots[prior] == attributed_root) {
                is_consistent = false;
                break;
            }
        }
    }

    const bool has_root_role = has_role(target, PackageRole::Root);
    const bool has_self_root = std::any_of(
            target.roots.begin(), target.roots.end(),
            [&target](const RootTargetIdentity& attributed_root) {
                return attributed_root.requested_name == target.package_name;
            });
    if(has_root_role != has_self_root) is_consistent = false;

    if(is_consistent) return true;

    BuildPlanArtifactTargetProjectionIssue issue = make_issue(
            BuildPlanArtifactTargetProjectionIssueKind::
                    RootAttributionInconsistent,
            localization::translate_message(
                    "Planned package target root attribution is inconsistent."));
    issue.package_target_indices.push_back(target_index);
    issue.package_base = target.package_base;
    issue.package_name = target.package_name;
    issue.roots = target.roots;
    failure.issues.push_back(std::move(issue));
    return false;
}

void validate_plan_root_coverage(
        const BuildPlan& plan,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    for(std::size_t root_index = 0; root_index < plan.root_targets.size();
        ++root_index) {
        const RootTargetIdentity& root = plan.root_targets[root_index];
        std::vector<std::size_t> matching_target_indices;
        for(std::size_t target_index = 0;
            target_index < plan.package_targets.size(); ++target_index) {
            const PlannedPackageTarget& target =
                    plan.package_targets[target_index];
            if(target.package_name == root.requested_name &&
               has_role(target, PackageRole::Root) &&
               target_contains_root_exactly_once(target, root)) {
                matching_target_indices.push_back(target_index);
            }
        }

        if(root.invocation_index == root_index &&
           matching_target_indices.size() == 1 &&
           root_occurs_exactly_once(plan.root_targets, root)) {
            continue;
        }

        BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                BuildPlanArtifactTargetProjectionIssueKind::
                        RootAttributionInconsistent,
                localization::format_translated_message(
                        "{} root does not have exactly one self-attributed planned package target.",
                        "BuildPlan"));
        issue.package_target_indices = std::move(matching_target_indices);
        issue.package_name = root.requested_name;
        issue.roots.push_back(root);
        failure.issues.push_back(std::move(issue));
    }
}

void validate_order_entries(
        const BuildPlan& plan,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    for(std::size_t order_index = 0; order_index < plan.order.size();
        ++order_index) {
        const BuildPlanEntry& entry = plan.order[order_index];
        if(!is_valid_package_name(entry.package_base)) {
            BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                    BuildPlanArtifactTargetProjectionIssueKind::
                            InvalidPackageBase,
                    localization::format_translated_message(
                            "{} entry has an invalid {}.", "BuildPlan",
                            "PackageBase"));
            issue.build_plan_order_index = order_index;
            issue.package_base = entry.package_base;
            failure.issues.push_back(std::move(issue));
        }
        if(entry.package_names.empty()) {
            BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                    BuildPlanArtifactTargetProjectionIssueKind::
                            EmptyEntryPackageNames,
                    localization::format_translated_message(
                            "{} entry has no required package names.",
                            "BuildPlan"));
            issue.build_plan_order_index = order_index;
            issue.package_base = entry.package_base;
            failure.issues.push_back(std::move(issue));
        }

        bool is_first_base_occurrence = true;
        for(std::size_t prior = 0; prior < order_index; ++prior) {
            if(plan.order[prior].package_base == entry.package_base) {
                is_first_base_occurrence = false;
                break;
            }
        }
        if(is_first_base_occurrence) {
            std::size_t base_count = 0;
            for(const BuildPlanEntry& candidate : plan.order) {
                if(candidate.package_base == entry.package_base) ++base_count;
            }
            if(base_count > 1) {
                BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                        BuildPlanArtifactTargetProjectionIssueKind::
                                DuplicatePackageBaseEntry,
                        localization::format_translated_message(
                                "{} appears more than once in {} execution order.",
                                "PackageBase", "BuildPlan"));
                issue.build_plan_order_index = order_index;
                issue.package_base = entry.package_base;
                failure.issues.push_back(std::move(issue));
            }
        }

        for(std::size_t name_index = 0;
            name_index < entry.package_names.size(); ++name_index) {
            const std::string& package_name = entry.package_names[name_index];
            if(!is_valid_package_name(package_name)) {
                BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                        BuildPlanArtifactTargetProjectionIssueKind::
                                InvalidPackageName,
                        localization::format_translated_message(
                                "{} entry has an invalid required package name.",
                                "BuildPlan"));
                issue.build_plan_order_index = order_index;
                issue.entry_package_name_index = name_index;
                issue.package_base = entry.package_base;
                issue.package_name = package_name;
                failure.issues.push_back(std::move(issue));
            }

            bool is_duplicate = false;
            for(std::size_t prior = 0; prior < name_index; ++prior) {
                if(entry.package_names[prior] == package_name) {
                    is_duplicate = true;
                    break;
                }
            }
            if(!is_duplicate) continue;

            BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                    BuildPlanArtifactTargetProjectionIssueKind::
                            DuplicateEntryPackageName,
                    localization::format_translated_message(
                            "{} entry contains a duplicate required package name.",
                            "BuildPlan"));
            issue.build_plan_order_index = order_index;
            issue.entry_package_name_index = name_index;
            issue.package_base = entry.package_base;
            issue.package_name = package_name;
            failure.issues.push_back(std::move(issue));
        }
    }
}

void validate_package_targets(
        const BuildPlan& plan,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    for(std::size_t target_index = 0;
        target_index < plan.package_targets.size(); ++target_index) {
        const PlannedPackageTarget& target = plan.package_targets[target_index];
        if(!is_valid_package_name(target.package_base)) {
            BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                    BuildPlanArtifactTargetProjectionIssueKind::
                            InvalidPackageBase,
                    localization::format_translated_message(
                            "Planned package target has an invalid {}.",
                            "PackageBase"));
            issue.package_target_indices.push_back(target_index);
            issue.package_base = target.package_base;
            issue.package_name = target.package_name;
            failure.issues.push_back(std::move(issue));
        }
        if(!is_valid_package_name(target.package_name)) {
            BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                    BuildPlanArtifactTargetProjectionIssueKind::
                            InvalidPackageName,
                    localization::translate_message(
                            "Planned package target has an invalid package name."));
            issue.package_target_indices.push_back(target_index);
            issue.package_base = target.package_base;
            issue.package_name = target.package_name;
            failure.issues.push_back(std::move(issue));
        }

        static_cast<void>(validate_target_root_attribution(
                plan, target, target_index, failure));

        bool is_first_name_occurrence = true;
        for(std::size_t prior = 0; prior < target_index; ++prior) {
            if(plan.package_targets[prior].package_name == target.package_name) {
                is_first_name_occurrence = false;
                break;
            }
        }
        if(!is_first_name_occurrence) continue;

        std::vector<std::size_t> same_name_indices;
        bool all_bases_match = true;
        for(std::size_t candidate_index = target_index;
            candidate_index < plan.package_targets.size(); ++candidate_index) {
            const PlannedPackageTarget& candidate =
                    plan.package_targets[candidate_index];
            if(candidate.package_name != target.package_name) continue;
            same_name_indices.push_back(candidate_index);
            if(candidate.package_base != target.package_base) {
                all_bases_match = false;
            }
        }
        if(same_name_indices.size() <= 1) continue;

        BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                all_bases_match
                        ? BuildPlanArtifactTargetProjectionIssueKind::
                                  DuplicatePlannedPackageTarget
                        : BuildPlanArtifactTargetProjectionIssueKind::
                                  PackageBaseMismatch,
                all_bases_match
                        ? localization::format_translated_message(
                                  "{} contains duplicate planned package targets.",
                                  "BuildPlan")
                        : localization::format_translated_message(
                                  "One package name is attributed to multiple {}.",
                                  "PackageBases"));
        issue.package_target_indices = std::move(same_name_indices);
        issue.package_base = target.package_base;
        issue.package_name = target.package_name;
        failure.issues.push_back(std::move(issue));
    }
}

std::vector<std::size_t> matching_package_target_indices(
        const BuildPlan& plan, const std::string& package_name,
        const std::optional<std::string>& package_base) {
    std::vector<std::size_t> indices;
    for(std::size_t target_index = 0;
        target_index < plan.package_targets.size(); ++target_index) {
        const PlannedPackageTarget& target = plan.package_targets[target_index];
        if(target.package_name != package_name) continue;
        if(package_base.has_value() && target.package_base != *package_base) {
            continue;
        }
        indices.push_back(target_index);
    }
    return indices;
}

void add_missing_or_mismatched_target_issue(
        const BuildPlan& plan, const BuildPlanEntry& entry,
        std::size_t order_index, std::size_t name_index,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    const std::string& package_name = entry.package_names[name_index];
    std::vector<std::size_t> same_name_indices =
            matching_package_target_indices(plan, package_name, std::nullopt);
    const bool has_mismatched_target = !same_name_indices.empty();
    BuildPlanArtifactTargetProjectionIssue issue = make_issue(
            has_mismatched_target
                    ? BuildPlanArtifactTargetProjectionIssueKind::
                              PackageBaseMismatch
                    : BuildPlanArtifactTargetProjectionIssueKind::
                              MissingPlannedPackageTarget,
            has_mismatched_target
                    ? localization::format_translated_message(
                              "Required package name is attributed to a different {}.",
                              "PackageBase")
                    : localization::format_translated_message(
                              "{} entry has no matching planned package target.",
                              "BuildPlan"));
    issue.build_plan_order_index = order_index;
    issue.entry_package_name_index = name_index;
    issue.package_target_indices = std::move(same_name_indices);
    issue.package_base = entry.package_base;
    issue.package_name = package_name;
    failure.issues.push_back(std::move(issue));
}

std::optional<DesiredInstallReason> project_desired_install_reason(
        const PlannedPackageTarget& target, std::size_t target_index,
        std::size_t order_index, std::size_t name_index,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    if(!has_known_reason_roles(target)) {
        BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                BuildPlanArtifactTargetProjectionIssueKind::
                        DesiredInstallReasonUnavailable,
                localization::translate_message(
                        "Planned package target has no complete known package-role set."));
        issue.build_plan_order_index = order_index;
        issue.entry_package_name_index = name_index;
        issue.package_target_indices.push_back(target_index);
        issue.package_base = target.package_base;
        issue.package_name = target.package_name;
        issue.roots = target.roots;
        failure.issues.push_back(std::move(issue));
        return std::nullopt;
    }

    try {
        DesiredInstallReason reason = desired_install_reason(target);
        if(is_known_desired_install_reason(reason)) return reason;
    } catch(const std::exception&) {
        // typed issueを正本にし、reducerの例外文字列だけへfailureを潰さない。
    }

    BuildPlanArtifactTargetProjectionIssue issue = make_issue(
            BuildPlanArtifactTargetProjectionIssueKind::
                    DesiredInstallReasonUnavailable,
            localization::translate_message(
                    "Planned package target desired install reason is unavailable."));
    issue.build_plan_order_index = order_index;
    issue.entry_package_name_index = name_index;
    issue.package_target_indices.push_back(target_index);
    issue.package_base = target.package_base;
    issue.package_name = target.package_name;
    issue.roots = target.roots;
    failure.issues.push_back(std::move(issue));
    return std::nullopt;
}

std::vector<ProjectedBuildPlanArtifactTargets> project_build_units(
        const BuildPlan& plan,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    std::vector<ProjectedBuildPlanArtifactTargets> projected_units;
    projected_units.reserve(plan.order.size());
    for(std::size_t order_index = 0; order_index < plan.order.size();
        ++order_index) {
        const BuildPlanEntry& entry = plan.order[order_index];
        ProjectedBuildPlanArtifactTargets unit{
                order_index, entry.package_base, {}};
        unit.required_targets.reserve(entry.package_names.size());

        for(std::size_t name_index = 0;
            name_index < entry.package_names.size(); ++name_index) {
            const std::string& package_name = entry.package_names[name_index];
            std::vector<std::size_t> exact_matches =
                    matching_package_target_indices(
                            plan, package_name, entry.package_base);
            if(exact_matches.empty()) {
                add_missing_or_mismatched_target_issue(
                        plan, entry, order_index, name_index, failure);
                continue;
            }
            if(exact_matches.size() != 1) continue;

            const std::size_t target_index = exact_matches.front();
            const PlannedPackageTarget& target =
                    plan.package_targets[target_index];
            std::optional<DesiredInstallReason> desired_reason =
                    project_desired_install_reason(
                            target, target_index, order_index, name_index,
                            failure);
            if(!desired_reason.has_value()) continue;

            unit.required_targets.push_back(
                    RequiredPackageArtifactTarget{
                            entry.package_base, package_name, *desired_reason});
        }
        projected_units.push_back(std::move(unit));
    }
    return projected_units;
}

void validate_reverse_coverage(
        const BuildPlan& plan,
        BuildPlanArtifactTargetProjectionFailure& failure) {
    for(std::size_t target_index = 0;
        target_index < plan.package_targets.size(); ++target_index) {
        const PlannedPackageTarget& target = plan.package_targets[target_index];
        std::size_t exact_entry_occurrences = 0;
        for(const BuildPlanEntry& entry : plan.order) {
            if(entry.package_base != target.package_base) continue;
            exact_entry_occurrences += static_cast<std::size_t>(std::count(
                    entry.package_names.begin(), entry.package_names.end(),
                    target.package_name));
        }
        if(exact_entry_occurrences != 0) continue;

        BuildPlanArtifactTargetProjectionIssue issue = make_issue(
                BuildPlanArtifactTargetProjectionIssueKind::
                        UncoveredPlannedPackageTarget,
                localization::format_translated_message(
                        "Planned package target is absent from {} execution order.",
                        "BuildPlan"));
        issue.package_target_indices.push_back(target_index);
        issue.package_base = target.package_base;
        issue.package_name = target.package_name;
        issue.roots = target.roots;
        failure.issues.push_back(std::move(issue));
    }
}

} // namespace

BuildPlanArtifactTargetProjectionResult::
        BuildPlanArtifactTargetProjectionResult(
                BuildPlanArtifactTargetProjectionSuccess success)
    : outcome_(
              std::in_place_type<BuildPlanArtifactTargetProjectionSuccess>,
              std::move(success)) {
}

BuildPlanArtifactTargetProjectionResult::
        BuildPlanArtifactTargetProjectionResult(
                BuildPlanArtifactTargetProjectionFailure failure)
    : outcome_(
              std::in_place_type<BuildPlanArtifactTargetProjectionFailure>,
              std::move(failure)) {
}

bool BuildPlanArtifactTargetProjectionResult::is_success() const noexcept {
    return std::holds_alternative<BuildPlanArtifactTargetProjectionSuccess>(
            outcome_);
}

const BuildPlanArtifactTargetProjectionSuccess*
BuildPlanArtifactTargetProjectionResult::success() const noexcept {
    return std::get_if<BuildPlanArtifactTargetProjectionSuccess>(&outcome_);
}

const BuildPlanArtifactTargetProjectionFailure*
BuildPlanArtifactTargetProjectionResult::failure() const noexcept {
    return std::get_if<BuildPlanArtifactTargetProjectionFailure>(&outcome_);
}

BuildPlanArtifactTargetProjectionResult
project_build_plan_required_artifact_targets(const BuildPlan& plan) {
    BuildPlanArtifactTargetProjectionFailure failure;
    validate_order_entries(plan, failure);
    validate_package_targets(plan, failure);
    validate_plan_root_coverage(plan, failure);
    std::vector<ProjectedBuildPlanArtifactTargets> projected_units =
            project_build_units(plan, failure);
    validate_reverse_coverage(plan, failure);

    if(!failure.issues.empty()) {
        return BuildPlanArtifactTargetProjectionResult(std::move(failure));
    }
    return BuildPlanArtifactTargetProjectionResult(
            BuildPlanArtifactTargetProjectionSuccess{
                    std::move(projected_units)});
}
