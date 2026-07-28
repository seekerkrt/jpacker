#pragma once

#include "artifact_install_plan.hpp"
#include "dependency_plan.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class BuildPlanArtifactTargetProjectionIssueKind {
    InvalidPackageBase,
    EmptyEntryPackageNames,
    InvalidPackageName,
    DuplicateEntryPackageName,
    MissingPlannedPackageTarget,
    DuplicatePlannedPackageTarget,
    PackageBaseMismatch,
    UncoveredPlannedPackageTarget,
    DesiredInstallReasonUnavailable,
    RootAttributionInconsistent,
    DuplicatePackageBaseEntry
};

struct BuildPlanArtifactTargetProjectionIssue {
    BuildPlanArtifactTargetProjectionIssueKind kind;
    std::optional<std::size_t>                  build_plan_order_index;
    std::optional<std::size_t>                  entry_package_name_index;
    std::vector<std::size_t>                    package_target_indices;
    std::optional<std::string>                  package_base;
    std::optional<std::string>                  package_name;
    std::vector<RootTargetIdentity>             roots;
    std::string                                 diagnostic;

    bool operator==(
            const BuildPlanArtifactTargetProjectionIssue&) const = default;
};

struct ProjectedBuildPlanArtifactTargets {
    std::size_t                                build_plan_order_index;
    std::string                                package_base;
    std::vector<RequiredPackageArtifactTarget> required_targets;
};

struct BuildPlanArtifactTargetProjectionSuccess {
    // POLICY(#268): BuildPlan::orderと1対1で、各required targetは
    // BuildPlanEntry::package_namesのfirst-seen順を保つ。
    std::vector<ProjectedBuildPlanArtifactTargets> build_units;
};

struct BuildPlanArtifactTargetProjectionFailure {
    // Failureは診断だけを保持し、部分的にprojectできたtargetを公開しない。
    std::vector<BuildPlanArtifactTargetProjectionIssue> issues;
};

class BuildPlanArtifactTargetProjectionResult final {
  public:
    BuildPlanArtifactTargetProjectionResult() = delete;
    BuildPlanArtifactTargetProjectionResult(
            const BuildPlanArtifactTargetProjectionResult&) = default;
    BuildPlanArtifactTargetProjectionResult(
            BuildPlanArtifactTargetProjectionResult&&) noexcept = default;
    BuildPlanArtifactTargetProjectionResult& operator=(
            const BuildPlanArtifactTargetProjectionResult&) = delete;
    BuildPlanArtifactTargetProjectionResult& operator=(
            BuildPlanArtifactTargetProjectionResult&&) noexcept = delete;
    ~BuildPlanArtifactTargetProjectionResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const BuildPlanArtifactTargetProjectionSuccess* success()
            const noexcept;
    [[nodiscard]] const BuildPlanArtifactTargetProjectionFailure* failure()
            const noexcept;

  private:
    explicit BuildPlanArtifactTargetProjectionResult(
            BuildPlanArtifactTargetProjectionSuccess success);
    explicit BuildPlanArtifactTargetProjectionResult(
            BuildPlanArtifactTargetProjectionFailure failure);

    std::variant<BuildPlanArtifactTargetProjectionSuccess,
                 BuildPlanArtifactTargetProjectionFailure>
            outcome_;

    friend BuildPlanArtifactTargetProjectionResult
    project_build_plan_required_artifact_targets(const BuildPlan& plan);
};

// BuildPlanだけをauthorityとし、caller指定のPackageBase/name/reasonを受けない。
BuildPlanArtifactTargetProjectionResult
project_build_plan_required_artifact_targets(const BuildPlan& plan);
