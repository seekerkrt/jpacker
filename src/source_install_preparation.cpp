#include "source_install.hpp"

#include "app_config.hpp"
#include "artifact_install_plan.hpp"
#include "artifact_workspace.hpp"
#include "package_identifier.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

// production source-buildのmutation前検証とPacman DB snapshotだけを所有する。
// POLICY(#267): execution symbolをこのTUへ持ち込まず、preparation-only binaryから
// checkout/build/install側をlinkしない境界を維持する。

void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item) {
    require_valid_package_name(work_item.request.package_name);
    require_valid_package_name(work_item.request.checkout_name);
    if(work_item.request.package_name != work_item.request.checkout_name) {
        throw std::runtime_error(
                "Production separated source-build requires requested package and "
                "PackageBase to match: " + work_item.request.package_name + " / " +
                work_item.request.checkout_name + ".");
    }
    if(work_item.request.git_url.empty()) {
        throw std::logic_error(
                "Production source-build work item has an empty Git URL for " +
                work_item.request.package_name + ".");
    }
    switch(work_item.desired_reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            break;
        default:
            throw std::logic_error(
                    "Production source-build work item has an unknown install reason.");
    }
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

    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items), std::move(database_paths)};
}
