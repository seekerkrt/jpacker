#include "root_package_route_projection.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

bool is_safe_exact_repository_target_name(
        const std::string& repository_name) {
    if(repository_name.empty() || repository_name == "." ||
       repository_name == ".." ||
       repository_name.find('/') != std::string::npos) {
        return false;
    }
    return std::none_of(
            repository_name.begin(), repository_name.end(),
            [](unsigned char character) {
                return std::iscntrl(character) != 0;
            });
}

} // namespace

RepositoryRootPackageRouteTarget::RepositoryRootPackageRouteTarget(
        std::size_t selection_index,
        SelectedRootPackageTarget selected_target) noexcept
    : selection_index_(selection_index),
      selected_target_(std::move(selected_target)) {}

std::size_t RepositoryRootPackageRouteTarget::selection_index()
        const noexcept {
    return selection_index_;
}

const SelectedRootPackageTarget&
RepositoryRootPackageRouteTarget::selected_target() const noexcept {
    return selected_target_;
}

const RepositoryRootPackageIdentity&
RepositoryRootPackageRouteTarget::identity() const noexcept {
    return std::get<RepositoryRootPackageIdentity>(
            selected_target_.identity());
}

std::string RepositoryRootPackageRouteTarget::exact_package_target() const {
    return identity().repository_name + "/" + identity().package_name;
}

AurRootPackageRouteTarget::AurRootPackageRouteTarget(
        std::size_t selection_index,
        SelectedRootPackageTarget selected_target) noexcept
    : selection_index_(selection_index),
      selected_target_(std::move(selected_target)) {}

std::size_t AurRootPackageRouteTarget::selection_index() const noexcept {
    return selection_index_;
}

const SelectedRootPackageTarget&
AurRootPackageRouteTarget::selected_target() const noexcept {
    return selected_target_;
}

const AurRootPackageIdentity& AurRootPackageRouteTarget::identity()
        const noexcept {
    return std::get<AurRootPackageIdentity>(selected_target_.identity());
}

RootPackageRoutingProjection::RootPackageRoutingProjection(
        std::vector<RepositoryRootPackageRouteTarget> repository_targets,
        std::vector<AurRootPackageRouteTarget> aur_targets) noexcept
    : repository_targets_(std::move(repository_targets)),
      aur_targets_(std::move(aur_targets)) {}

const std::vector<RepositoryRootPackageRouteTarget>&
RootPackageRoutingProjection::repository_targets() const noexcept {
    return repository_targets_;
}

const std::vector<AurRootPackageRouteTarget>&
RootPackageRoutingProjection::aur_targets() const noexcept {
    return aur_targets_;
}

RootPackageRoutingProjectionResult::RootPackageRoutingProjectionResult(
        RootPackageRoutingProjection projection) noexcept
    : outcome_(std::move(projection)) {}

RootPackageRoutingProjectionResult::RootPackageRoutingProjectionResult(
        InvalidRootPackageRoutingProjection failure) noexcept
    : outcome_(std::move(failure)) {}

bool RootPackageRoutingProjectionResult::is_valid() const noexcept {
    return std::holds_alternative<RootPackageRoutingProjection>(outcome_);
}

const RootPackageRoutingProjection*
RootPackageRoutingProjectionResult::projection() const noexcept {
    return std::get_if<RootPackageRoutingProjection>(&outcome_);
}

const InvalidRootPackageRoutingProjection*
RootPackageRoutingProjectionResult::failure() const noexcept {
    return std::get_if<InvalidRootPackageRoutingProjection>(&outcome_);
}

RootPackageRoutingProjectionResult project_root_package_routing(
        const RootPackageSelection& selection) {
    std::vector<UnrepresentableRepositoryRootPackageRouteTarget>
            unrepresentable;
    for(std::size_t index = 0; index < selection.targets().size(); ++index) {
        const auto* repository =
                std::get_if<RepositoryRootPackageIdentity>(
                        &selection.targets()[index].identity());
        if(repository != nullptr &&
           !is_safe_exact_repository_target_name(
                   repository->repository_name)) {
            unrepresentable.push_back(
                    UnrepresentableRepositoryRootPackageRouteTarget{
                            index, *repository});
        }
    }
    if(!unrepresentable.empty()) {
        return RootPackageRoutingProjectionResult(
                InvalidRootPackageRoutingProjection{
                        std::move(unrepresentable)});
    }

    std::vector<RepositoryRootPackageRouteTarget> repository_targets;
    std::vector<AurRootPackageRouteTarget>        aur_targets;
    for(std::size_t index = 0; index < selection.targets().size(); ++index) {
        const SelectedRootPackageTarget& target = selection.targets()[index];
        if(target.source_kind() == RootPackageSourceKind::Repository) {
            repository_targets.push_back(
                    RepositoryRootPackageRouteTarget(index, target));
        } else {
            aur_targets.push_back(AurRootPackageRouteTarget(index, target));
        }
    }

    return RootPackageRoutingProjectionResult(
            RootPackageRoutingProjection(
                    std::move(repository_targets),
                    std::move(aur_targets)));
}
