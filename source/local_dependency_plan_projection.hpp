#pragma once

#include "dependency_constraint.hpp"
#include "dependency_plan.hpp"
#include "local_package_metadata.hpp"

#include <optional>
#include <string>
#include <vector>

enum class LocalDependencyResolutionKind {
    Package,
    Provided
};

struct LocalDependencyPlanCandidate {
    std::string package_name;
    std::optional<std::string> provided_specification;
    std::optional<std::string> version;
    std::optional<ProvidedDependency> remote_provider = std::nullopt;
    std::optional<ConstraintEvaluation> constraint_evaluation = std::nullopt;

    bool operator==(const LocalDependencyPlanCandidate&) const = default;
};

enum class LocalDependencyPlanFailureKind {
    UnsupportedArchitecture,
    ConstraintMismatch,
    AmbiguousLocalProvider,
    RemoteProviderIdentityConflict
};

struct LocalDependencyPlanFailure {
    LocalDependencyPlanFailureKind kind;
    std::string parent_package_name;
    std::optional<std::string> dependency_specification;
    std::optional<std::string> effective_architecture;
    std::vector<LocalDependencyPlanCandidate> candidates;

    bool operator==(const LocalDependencyPlanFailure&) const = default;
};

struct LocalDependencyPlanInternalEdge {
    std::string parent_package_name;
    std::string dependency_specification;
    PackageRole role;
    std::string resolved_package_name;
    LocalDependencyResolutionKind resolution_kind;
    std::optional<std::string> provided_specification;
    bool is_cycle;
    std::optional<DependencyRequirement> requirement = std::nullopt;
    std::optional<LocalResolvedDependencyCandidate> resolved_candidate =
        std::nullopt;
    std::optional<ConstraintEvaluation> constraint_evaluation =
        std::nullopt;

    bool operator==(const LocalDependencyPlanInternalEdge&) const = default;
};

// Local rootを既存AUR execution consumerへ暗黙に渡さないためのtyped wrapper。
// underlying BuildPlanはread-only observationにだけ公開する。
class LocalBuildPlan final {
public:
    LocalBuildPlan() = delete;
    LocalBuildPlan(const LocalBuildPlan&) = default;
    LocalBuildPlan(LocalBuildPlan&&) noexcept = default;
    LocalBuildPlan& operator=(const LocalBuildPlan&) = delete;
    LocalBuildPlan& operator=(LocalBuildPlan&&) noexcept = delete;
    ~LocalBuildPlan() = default;

    [[nodiscard]] const BuildPlan& build_plan() const noexcept;
    [[nodiscard]] const LocalPackageMetadata& local_metadata() const noexcept;
    [[nodiscard]] const std::string& effective_architecture() const noexcept;
    [[nodiscard]] const std::vector<LocalDependencyPlanInternalEdge>&
    internal_edges() const noexcept;
    [[nodiscard]] const std::vector<LocalDependencyPlanFailure>& failures()
        const noexcept;

private:
    LocalBuildPlan(
        BuildPlan build_plan, LocalPackageMetadata local_metadata,
        std::string effective_architecture,
        std::vector<LocalDependencyPlanInternalEdge> internal_edges,
        std::vector<LocalDependencyPlanFailure> failures) noexcept;

    BuildPlan build_plan_;
    LocalPackageMetadata local_metadata_;
    std::string effective_architecture_;
    std::vector<LocalDependencyPlanInternalEdge> internal_edges_;
    std::vector<LocalDependencyPlanFailure> failures_;

    friend LocalBuildPlan resolve_local_build_plan(
        const LocalPackageMetadata&, const std::string&,
        const ProviderSelectionCallback&);
    friend LocalBuildPlan resolve_local_build_plan(
        const LocalPackageMetadata&, const std::string&,
        PackageRelationLocalSourceIdentity,
        const ProviderSelectionCallback&);
};

LocalBuildPlan resolve_local_build_plan(
    const LocalPackageMetadata& metadata,
    const std::string& effective_architecture);
LocalBuildPlan resolve_local_build_plan(
    const LocalPackageMetadata& metadata,
    const std::string& effective_architecture,
    const ProviderSelectionCallback& select_provider);
LocalBuildPlan resolve_local_build_plan(
    const LocalPackageMetadata& metadata,
    const std::string& effective_architecture,
    PackageRelationLocalSourceIdentity source);
LocalBuildPlan resolve_local_build_plan(
    const LocalPackageMetadata& metadata,
    const std::string& effective_architecture,
    PackageRelationLocalSourceIdentity source,
    const ProviderSelectionCallback& select_provider);
