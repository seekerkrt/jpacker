#pragma once

#include "aur_update_plan.hpp"
#include "build_plan_artifact_target_projection.hpp"
#include "dependency_plan.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// RequiresCheckのexecution解釈はsaved source preferenceとは独立した
// route policyである。Current production routeはBlockOperationを使い、
// SkipIndependentTargetはdependency相関を備えた後続capability用に保持する。
enum class DevelRequiresCheckPolicy {
    BlockOperation,
    SkipIndependentTarget,
};

constexpr bool is_known_devel_requires_check_policy(
    DevelRequiresCheckPolicy policy) noexcept {
    switch(policy) {
        case DevelRequiresCheckPolicy::BlockOperation:
        case DevelRequiresCheckPolicy::SkipIndependentTarget:
            return true;
    }
    return false;
}

enum class AurUpdateExecutionSkipKind {
    UpToDate,
    NonAurForeign,
    IndependentDevelRequiresCheck,
};

constexpr bool is_known_aur_update_execution_skip_kind(
    AurUpdateExecutionSkipKind kind) noexcept {
    switch(kind) {
        case AurUpdateExecutionSkipKind::UpToDate:
        case AurUpdateExecutionSkipKind::NonAurForeign:
        case AurUpdateExecutionSkipKind::IndependentDevelRequiresCheck:
            return true;
    }
    return false;
}

enum class AurUpdateRequiredDevelTargetRelation {
    AurExactDependency,
    AurProvider,
    RepositoryExactDependency,
    RepositoryProvider,
    RequiredArtifactChild,
    IdentityDrift,
};

constexpr bool is_known_aur_update_required_devel_target_relation(
    AurUpdateRequiredDevelTargetRelation relation) noexcept {
    switch(relation) {
        case AurUpdateRequiredDevelTargetRelation::AurExactDependency:
        case AurUpdateRequiredDevelTargetRelation::AurProvider:
        case AurUpdateRequiredDevelTargetRelation::
            RepositoryExactDependency:
        case AurUpdateRequiredDevelTargetRelation::RepositoryProvider:
        case AurUpdateRequiredDevelTargetRelation::RequiredArtifactChild:
        case AurUpdateRequiredDevelTargetRelation::IdentityDrift:
            return true;
    }
    return false;
}

// BuildPlan上の再登場をaffected rootへ帰属させる後続capability用の
// lossless record。Current production producerはこのrecordを生成しない。
struct AurUpdateRequiredDevelTargetBlocker {
    AurUpdateRequiredDevelTargetBlocker() = delete;

    AurUpdateRequiredDevelTargetBlocker(
        AurUpdateRequiredDevelTargetRelation relation_value,
        std::size_t requires_check_update_plan_index_value,
        std::string package_name_value,
        DevelRequiresCheckReason devel_requires_check_reason_value)
        : relation(relation_value),
          requires_check_update_plan_index(
              requires_check_update_plan_index_value),
          package_name(std::move(package_name_value)),
          devel_requires_check_reason(
              devel_requires_check_reason_value) {
    }

    AurUpdateRequiredDevelTargetRelation relation;
    std::size_t requires_check_update_plan_index;
    std::optional<std::size_t> dependency_edge_index;
    std::optional<std::size_t> build_plan_order_index;
    std::string package_name;
    std::optional<std::string> package_base;
    std::vector<PackageRole> roles;
    DevelRequiresCheckReason devel_requires_check_reason;
    std::vector<RootTargetIdentity> affected_roots;

    bool operator==(
        const AurUpdateRequiredDevelTargetBlocker&) const = default;
};

enum class AurUpdateExecutionTargetStatus {
    Executable,
    Skipped,
    Unsupported,
    Incomplete
};

enum class AurUpdateExecutionReason {
    None,

    UpToDate,
    DevelRequiresCheck,
    RequiredDevelTargetRequiresCheck,
    NonAurForeign,

    AurMetadataUnavailable,
    VersionComparisonUnavailable,
    InstalledReasonUnknown,
    UpdatePlanInconsistent,
    DuplicateUpdateTarget,

    RepositoryMetadataUnavailable,
    AurDependencyMetadataUnavailable,
    ProviderMetadataUnavailable,
    UnresolvedDependency,
    VersionConstraintUnverified,
    DependencyCycle,
    BuildPlanInconsistent,
    PackageBaseMismatch,

    SplitPackageSelectionRequired,
    MultiplePackageTargetsForPackageBase,
    AmbiguousProvider,
    ConflictsOrReplacesUnresolved,

    InstalledPackageMetadataUnavailable
};

struct AurUpdateExecutionIssue {
    AurUpdateExecutionReason reason = AurUpdateExecutionReason::None;
    std::optional<std::string> package_name;
    std::optional<std::string> package_base;
    std::optional<std::string> dependency_specification;
    std::string diagnostic;
    std::optional<DevelRequiresCheckReason> devel_requires_check_reason;
    std::optional<BuildPlanArtifactTargetProjectionIssue>
        build_plan_projection_issue;
    std::optional<PlanDeclaredRelationReason> relation_reason;
    std::optional<AurUpdateRequiredDevelTargetBlocker>
        required_devel_target_blocker;

    AurUpdateExecutionIssue() = default;

    AurUpdateExecutionIssue(
        AurUpdateExecutionReason reason_value,
        std::optional<std::string> package_name_value,
        std::optional<std::string> package_base_value,
        std::optional<std::string> dependency_specification_value,
        std::string diagnostic_value,
        std::optional<BuildPlanArtifactTargetProjectionIssue>
            projection_issue = std::nullopt,
        std::optional<PlanDeclaredRelationReason>
            relation_reason_value = std::nullopt,
        std::optional<DevelRequiresCheckReason>
            devel_requires_check_reason_value = std::nullopt,
        std::optional<AurUpdateRequiredDevelTargetBlocker>
            required_devel_target_blocker_value = std::nullopt)
        : reason(reason_value),
          package_name(std::move(package_name_value)),
          package_base(std::move(package_base_value)),
          dependency_specification(
              std::move(dependency_specification_value)),
          diagnostic(std::move(diagnostic_value)),
          devel_requires_check_reason(
              devel_requires_check_reason_value),
          build_plan_projection_issue(std::move(projection_issue)),
          relation_reason(std::move(relation_reason_value)),
          required_devel_target_blocker(
              std::move(required_devel_target_blocker_value)) {
    }
};

inline bool is_valid_aur_update_normal_skip_snapshot(
    const AurUpdatePlanEntry& update,
    const std::vector<AurUpdateExecutionIssue>& issues,
    const std::optional<AurUpdateExecutionSkipKind>& skip_kind) noexcept {
    if(!skip_kind.has_value() ||
       !is_known_aur_update_execution_skip_kind(*skip_kind) ||
       *skip_kind ==
           AurUpdateExecutionSkipKind::IndependentDevelRequiresCheck) {
        return false;
    }

    std::size_t normal_skip_issue_count = 0;
    bool has_matching_reason = false;
    for(const auto& issue : issues) {
        if(issue.devel_requires_check_reason.has_value() ||
           issue.required_devel_target_blocker.has_value() ||
           issue.dependency_specification.has_value() ||
           issue.build_plan_projection_issue.has_value() ||
           issue.relation_reason.has_value()) {
            return false;
        }
        if(issue.reason == AurUpdateExecutionReason::None) continue;
        if(issue.reason == AurUpdateExecutionReason::UpToDate) {
            ++normal_skip_issue_count;
            has_matching_reason = has_matching_reason ||
                                  *skip_kind ==
                                      AurUpdateExecutionSkipKind::UpToDate;
            continue;
        }
        if(issue.reason == AurUpdateExecutionReason::NonAurForeign) {
            ++normal_skip_issue_count;
            has_matching_reason =
                has_matching_reason ||
                *skip_kind ==
                    AurUpdateExecutionSkipKind::NonAurForeign;
            continue;
        }
        return false;
    }
    if(normal_skip_issue_count != 1 || !has_matching_reason) return false;

    switch(*skip_kind) {
        case AurUpdateExecutionSkipKind::UpToDate:
            return update.classification ==
                       AurUpdateClassification::UpToDate &&
                   update.aur_package.has_value() &&
                   update.devel_assessment ==
                       DevelUpdateAssessment::not_applicable() &&
                   (update.aur_package->version_relation ==
                        AurVersionRelation::OlderThanInstalled ||
                    update.aur_package->version_relation ==
                        AurVersionRelation::SameAsInstalled);
        case AurUpdateExecutionSkipKind::NonAurForeign:
            return update.classification ==
                       AurUpdateClassification::NonAurForeign &&
                   !update.aur_package.has_value() &&
                   update.devel_assessment ==
                       DevelUpdateAssessment::not_applicable();
        case AurUpdateExecutionSkipKind::IndependentDevelRequiresCheck:
            return false;
    }
    return false;
}

struct AurUpdateExecutionTarget {
    std::size_t update_plan_index = 0;
    std::optional<std::size_t> build_plan_root_index;
    AurUpdatePlanEntry update;
    AurUpdateExecutionTargetStatus status =
        AurUpdateExecutionTargetStatus::Incomplete;
    std::optional<DesiredInstallReason> desired_install_reason;
    std::vector<AurUpdateExecutionIssue> issues;
    std::optional<AurUpdateExecutionSkipKind> skip_kind;
};

inline bool is_valid_aur_update_execution_target_skip_snapshot(
    const AurUpdateExecutionTarget& target) noexcept {
    switch(target.status) {
        case AurUpdateExecutionTargetStatus::Executable:
            // CONTRACT(#508): Executable is authority-free. Even a None
            // issue is a malformed retained snapshot, not a placeholder.
            return !target.skip_kind.has_value() && target.issues.empty();
        case AurUpdateExecutionTargetStatus::Skipped:
            return is_valid_aur_update_normal_skip_snapshot(
                target.update, target.issues, target.skip_kind);
        case AurUpdateExecutionTargetStatus::Unsupported:
        case AurUpdateExecutionTargetStatus::Incomplete:
            return !target.skip_kind.has_value();
    }
    return false;
}

struct AurUpdateExecutionPreflight {
    std::vector<AurUpdateExecutionTarget> targets;
    std::optional<BuildPlan> build_plan;
    std::optional<DevelRequiresCheckPolicy>
        devel_requires_check_policy;
};

inline bool has_valid_aur_update_execution_policy_snapshot(
    const AurUpdateExecutionPreflight& preflight) noexcept {
    if(!preflight.devel_requires_check_policy.has_value() ||
       !is_known_devel_requires_check_policy(
           *preflight.devel_requires_check_policy)) {
        return false;
    }
    for(const auto& target : preflight.targets) {
        if(!is_valid_aur_update_execution_target_skip_snapshot(target)) {
            return false;
        }
    }
    return true;
}

AurUpdateExecutionPreflight resolve_aur_update_execution_preflight(
    const AurUpdatePlan& update_plan,
    DevelRequiresCheckPolicy devel_requires_check_policy);
AurUpdateExecutionPreflight resolve_aur_update_execution_preflight(
    const AurUpdatePlan& update_plan,
    DevelRequiresCheckPolicy devel_requires_check_policy,
    const ProviderSelectionCallback& select_provider);
bool has_executable_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool has_blocking_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool can_execute(const AurUpdateExecutionPreflight& preflight) noexcept;
