#pragma once

#include "aur_update_plan.hpp"
#include "dependency_plan.hpp"

#include <cstddef>
#include <optional>
#include <string>
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
bool has_executable_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool has_blocking_targets(const AurUpdateExecutionPreflight& preflight) noexcept;
bool can_execute(const AurUpdateExecutionPreflight& preflight) noexcept;
