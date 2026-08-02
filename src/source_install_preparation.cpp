#include "source_install.hpp"

#include "app_config.hpp"
#include "artifact_install_plan.hpp"
#include "artifact_workspace.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// production source-buildのmutation前検証とPacman DB snapshotだけを所有する。
// POLICY(#267): execution symbolをこのTUへ持ち込まず、preparation-only binaryから
// checkout/build/install側をlinkしない境界を維持する。
// NO_TRANSLATE(Issue #308): diagnostics in this translation unit describe
// internal capability/work-item correlation contract violations.

namespace {

std::optional<ValidatedCacheRoot> shared_prepared_cache_root(
        const std::vector<ProductionSourceBuildWorkItem>& work_items) {
    std::optional<ValidatedCacheRoot> shared_root;
    bool                              saw_missing_root = false;
    for(const auto& work_item : work_items) {
        if(!work_item.cache_root.has_value()) {
            saw_missing_root = true;
            continue;
        }
        if(!shared_root.has_value()) {
            shared_root = work_item.cache_root.value();
            continue;
        }
    }
    if(shared_root.has_value() && saw_missing_root) {
        throw std::logic_error(
                "Production source-build work items have a partial cache authority.");
    }
    return shared_root;
}

} // namespace

void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item) {
    require_valid_package_name(work_item.request.checkout_name);
    if(work_item.request.git_url.empty()) {
        throw std::logic_error(
                "Production source-build work item has an empty Git URL for " +
                work_item.request.checkout_name + ".");
    }

    if(work_item.required_targets.empty()) {
        throw std::logic_error(
                "Production source-build work item has no required package target for PackageBase " +
                work_item.request.checkout_name + ".");
    }
    for(std::size_t index = 0; index < work_item.required_targets.size();
        ++index) {
        const RequiredPackageArtifactTarget& target =
                work_item.required_targets[index];
        require_valid_package_name(target.package_base);
        require_valid_package_name(target.package_name);
        if(target.package_base != work_item.request.checkout_name) {
            throw std::logic_error(
                    "Production source-build required target has a mismatched PackageBase: " +
                    target.package_name + " / " + target.package_base + ".");
        }
        if(std::any_of(
                   work_item.required_targets.begin(),
                   work_item.required_targets.begin() + index,
                   [&target](const RequiredPackageArtifactTarget& existing) {
                       return existing.package_name == target.package_name;
                   })) {
            throw std::logic_error(
                    "Production source-build work item contains duplicate required package target: " +
                    target.package_name + ".");
        }
        switch(target.desired_reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            break;
        default:
            throw std::logic_error(
                    "Production source-build work item has an unknown install reason.");
        }
    }

    if(work_item.required_targets.size() == 1) {
        require_valid_package_name(work_item.request.package_name);
        if(work_item.request.package_name !=
           work_item.required_targets.front().package_name) {
            throw std::logic_error(
                    "Production source-build singular request does not match its required package target: " +
                    work_item.request.package_name + ".");
        }
    } else if(!work_item.request.package_name.empty()) {
        throw std::logic_error(
                "Production source-build multiple-target work item must not expose a singular requested package.");
    }
}

const RequiredPackageArtifactTarget& require_singular_required_package_target(
        const ProductionSourceBuildWorkItem& work_item) {
    if(work_item.required_targets.size() != 1) {
        throw std::logic_error(
                "Production separated source-build requires exactly one required package target for PackageBase " +
                work_item.request.checkout_name + ".");
    }
    const RequiredPackageArtifactTarget& target =
            work_item.required_targets.front();
    if(work_item.request.package_name != target.package_name ||
       work_item.request.checkout_name != target.package_base) {
        throw std::logic_error(
                "Production separated source-build singular identity is inconsistent for PackageBase " +
                work_item.request.checkout_name + ".");
    }
    return target;
}

void require_supported_production_source_build_options(
        const AppConfig& config) {
    require_supported_separated_install_options(config.rm_deps);
}

PreparedProductionSourceBuildInvocation prepare_production_source_build_invocation(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        const AppConfig& config) {
    if(work_items.empty()) {
        throw std::invalid_argument(
                "Production source-build invocation must contain at least one work item.");
    }

    // POLICY(#242): exact orderはrmdeps → inherited PKGDEST → all source
    // environments → static identity/role → database paths。ここまではworkspace、
    // checkout、makepkg、metadata session、sudoを開始しない。
    require_supported_separated_install_options(config.rm_deps);
    require_unclaimed_artifact_pkgdest(SourceBuildEnvironment{});
    for(const auto& work_item : work_items) {
        require_unclaimed_artifact_pkgdest(
                work_item.request.custom_environment);
    }
    for(const auto& work_item : work_items) {
        require_static_production_source_build_work_item(work_item);
    }

    // Explicit build/sync routeがnetwork前に準備済みならcapabilityを保持する。
    // Update preparationはfilesystem mutationを行わず、execution ownerがactivateする。
    std::optional<ValidatedCacheRoot> supplied_cache_root =
            shared_prepared_cache_root(work_items);
    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items), std::move(database_paths),
            std::move(supplied_cache_root)};
}
