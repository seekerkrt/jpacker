#pragma once

#include "aur_update_plan.hpp"
#include "build_plan_artifact_target_projection.hpp"
#include "dependency_plan.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class AurUpdateExecutionTargetStatus {
    Executable,
    Skipped,
    Unsupported,
    Incomplete
};

enum class AurUpdateExecutionReason {
    None,

    UpToDate,
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
    ConflictsOrReplacesUnresolved
};

struct AurUpdateExecutionIssue {
    AurUpdateExecutionReason   reason = AurUpdateExecutionReason::None;
    std::optional<std::string> package_name;
    std::optional<std::string> package_base;
    std::optional<std::string> dependency_specification;
    std::string                diagnostic;
    std::optional<BuildPlanArtifactTargetProjectionIssue>
            build_plan_projection_issue;

    AurUpdateExecutionIssue() = default;

    AurUpdateExecutionIssue(
            AurUpdateExecutionReason reason_value,
            std::optional<std::string> package_name_value,
            std::optional<std::string> package_base_value,
            std::optional<std::string> dependency_specification_value,
            std::string diagnostic_value,
            std::optional<BuildPlanArtifactTargetProjectionIssue>
                    projection_issue = std::nullopt)
        : reason(reason_value),
          package_name(std::move(package_name_value)),
          package_base(std::move(package_base_value)),
          dependency_specification(
                  std::move(dependency_specification_value)),
          diagnostic(std::move(diagnostic_value)),
          build_plan_projection_issue(std::move(projection_issue)) {
    }
};

struct AurUpdateExecutionTarget {
    std::size_t                         update_plan_index = 0;
    std::optional<std::size_t>          build_plan_root_index;
    AurUpdatePlanEntry                  update;
    AurUpdateExecutionTargetStatus      status =
            AurUpdateExecutionTargetStatus::Incomplete;
    std::optional<DesiredInstallReason> desired_install_reason;
    std::vector<AurUpdateExecutionIssue> issues;
};

struct AurUpdateExecutionPreflight {
    std::vector<AurUpdateExecutionTarget> targets;
    std::optional<BuildPlan>               build_plan;
};

AurUpdateExecutionPreflight resolve_aur_update_execution_preflight(
        const AurUpdatePlan& update_plan);
AurUpdateExecutionPreflight resolve_aur_update_execution_preflight(
        const AurUpdatePlan& update_plan,
        const ProviderSelectionCallback& select_provider);
bool has_executable_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool has_blocking_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool can_execute(const AurUpdateExecutionPreflight& preflight) noexcept;
