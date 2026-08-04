#pragma once

#include "root_package_selection.hpp"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

class RootPackageRoutingProjectionResult;

struct UnrepresentableRepositoryRootPackageRouteTarget {
    std::size_t                   selection_index;
    RepositoryRootPackageIdentity identity;

    bool operator==(
            const UnrepresentableRepositoryRootPackageRouteTarget&) const =
            default;
};

struct InvalidRootPackageRoutingProjection {
    std::vector<UnrepresentableRepositoryRootPackageRouteTarget>
            unrepresentable_repository_targets;

    bool operator==(
            const InvalidRootPackageRoutingProjection&) const = default;
};

// exact repository/package routeへ渡すidentityとselectionの0-origin indexを
// selected rootのまま保持する。
class RepositoryRootPackageRouteTarget final {
public:
    RepositoryRootPackageRouteTarget(
            const RepositoryRootPackageRouteTarget&) = default;
    RepositoryRootPackageRouteTarget(
            RepositoryRootPackageRouteTarget&&) noexcept = default;
    RepositoryRootPackageRouteTarget& operator=(
            const RepositoryRootPackageRouteTarget&) = default;
    RepositoryRootPackageRouteTarget& operator=(
            RepositoryRootPackageRouteTarget&&) noexcept = default;
    ~RepositoryRootPackageRouteTarget() = default;

    [[nodiscard]] std::size_t selection_index() const noexcept;
    [[nodiscard]] const SelectedRootPackageTarget& selected_target()
            const noexcept;
    [[nodiscard]] const RepositoryRootPackageIdentity& identity()
            const noexcept;
    [[nodiscard]] std::string exact_package_target() const;

    bool operator==(const RepositoryRootPackageRouteTarget&) const = default;

private:
    RepositoryRootPackageRouteTarget(
            std::size_t selection_index,
            SelectedRootPackageTarget selected_target) noexcept;

    std::size_t               selection_index_;
    SelectedRootPackageTarget selected_target_;

    friend RootPackageRoutingProjectionResult project_root_package_routing(
            const RootPackageSelection& selection);
};

// AurOnly相当のrouteへPackageBaseとselectionの0-origin indexを失わず渡す。
class AurRootPackageRouteTarget final {
public:
    AurRootPackageRouteTarget(const AurRootPackageRouteTarget&) = default;
    AurRootPackageRouteTarget(AurRootPackageRouteTarget&&) noexcept = default;
    AurRootPackageRouteTarget& operator=(
            const AurRootPackageRouteTarget&) = default;
    AurRootPackageRouteTarget& operator=(
            AurRootPackageRouteTarget&&) noexcept = default;
    ~AurRootPackageRouteTarget() = default;

    [[nodiscard]] std::size_t selection_index() const noexcept;
    [[nodiscard]] const SelectedRootPackageTarget& selected_target()
            const noexcept;
    [[nodiscard]] const AurRootPackageIdentity& identity() const noexcept;

    bool operator==(const AurRootPackageRouteTarget&) const = default;

private:
    AurRootPackageRouteTarget(
            std::size_t selection_index,
            SelectedRootPackageTarget selected_target) noexcept;

    std::size_t               selection_index_;
    SelectedRootPackageTarget selected_target_;

    friend RootPackageRoutingProjectionResult project_root_package_routing(
            const RootPackageSelection& selection);
};

// selection順との相関を保持し、source内のrelative orderを変えずpartitionする。
class RootPackageRoutingProjection final {
public:
    RootPackageRoutingProjection(const RootPackageRoutingProjection&) =
            default;
    RootPackageRoutingProjection(RootPackageRoutingProjection&&) noexcept =
            default;
    RootPackageRoutingProjection& operator=(
            const RootPackageRoutingProjection&) = default;
    RootPackageRoutingProjection& operator=(
            RootPackageRoutingProjection&&) noexcept = default;
    ~RootPackageRoutingProjection() = default;

    [[nodiscard]] const std::vector<RepositoryRootPackageRouteTarget>&
    repository_targets() const noexcept;
    [[nodiscard]] const std::vector<AurRootPackageRouteTarget>& aur_targets()
            const noexcept;

    bool operator==(const RootPackageRoutingProjection&) const = default;

private:
    RootPackageRoutingProjection(
            std::vector<RepositoryRootPackageRouteTarget> repository_targets,
            std::vector<AurRootPackageRouteTarget> aur_targets) noexcept;

    std::vector<RepositoryRootPackageRouteTarget> repository_targets_;
    std::vector<AurRootPackageRouteTarget>        aur_targets_;

    friend RootPackageRoutingProjectionResult project_root_package_routing(
            const RootPackageSelection& selection);
};

// failure armではpartial routeを公開しない。
class RootPackageRoutingProjectionResult final {
public:
    RootPackageRoutingProjectionResult() = delete;
    RootPackageRoutingProjectionResult(
            const RootPackageRoutingProjectionResult&) = default;
    RootPackageRoutingProjectionResult(
            RootPackageRoutingProjectionResult&&) noexcept = default;
    RootPackageRoutingProjectionResult& operator=(
            const RootPackageRoutingProjectionResult&) = delete;
    RootPackageRoutingProjectionResult& operator=(
            RootPackageRoutingProjectionResult&&) noexcept = delete;
    ~RootPackageRoutingProjectionResult() = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] const RootPackageRoutingProjection* projection()
            const noexcept;
    [[nodiscard]] const InvalidRootPackageRoutingProjection* failure()
            const noexcept;

private:
    explicit RootPackageRoutingProjectionResult(
            RootPackageRoutingProjection projection) noexcept;
    explicit RootPackageRoutingProjectionResult(
            InvalidRootPackageRoutingProjection failure) noexcept;

    std::variant<
            RootPackageRoutingProjection,
            InvalidRootPackageRoutingProjection>
            outcome_;

    friend RootPackageRoutingProjectionResult project_root_package_routing(
            const RootPackageSelection& selection);
};

RootPackageRoutingProjectionResult project_root_package_routing(
        const RootPackageSelection& selection);
