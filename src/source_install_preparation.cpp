#include "source_install.hpp"

#include "app_config.hpp"
#include "artifact_install_plan.hpp"
#include "artifact_workspace.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <cctype>
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

bool is_safe_repository_target_name(const std::string& repository_name) {
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

void require_selected_repository_provider(
        const ProvidedDependency& provider) {
    const auto* repository =
            std::get_if<RepositoryProviderOrigin>(&provider.origin);
    if(repository == nullptr) {
        throw std::logic_error(
                "Production source-build selected provider is not repository-owned.");
    }
    if(!is_safe_repository_target_name(repository->repository_name)) {
        throw std::logic_error(
                "Production source-build selected provider has an invalid repository name.");
    }
    require_valid_package_name(provider.package_name);
    require_valid_package_name(provider.provided_dependency_name);
    if(!provider.package_base.empty()) {
        throw std::logic_error(
                "Production source-build repository provider has an AUR PackageBase.");
    }
}

std::vector<ProvidedDependency> collect_selected_repository_providers(
        const std::vector<ProductionSourceBuildWorkItem>& work_items) {
    std::vector<ProvidedDependency> providers;
    for(const auto& work_item : work_items) {
        for(const auto& provider : work_item.selected_repository_providers) {
            require_selected_repository_provider(provider);
            auto same = [&provider](const ProvidedDependency& existing) {
                return same_provider_identity(existing, provider);
            };
            if(std::find_if(providers.begin(), providers.end(), same) ==
               providers.end()) {
                providers.push_back(provider);
            }
        }
    }
    return providers;
}

void add_selected_repository_provider(
        std::vector<ProvidedDependency>& providers,
        const ProvidedDependency& provider) {
    require_selected_repository_provider(provider);
    const auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider_identity(existing, provider);
    };
    if(std::find_if(providers.begin(), providers.end(), same) ==
       providers.end()) {
        providers.push_back(provider);
    }
}

} // namespace

void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item) {
    switch(work_item.required_target_provenance) {
    case RequiredTargetProvenance::RepositoryExactPackageProjection:
    case RequiredTargetProvenance::AurBuildPlanProjection:
        break;
    case RequiredTargetProvenance::Unspecified:
    default:
        throw std::logic_error(
                "Production source-build work item has no supported required-target provenance.");
    }
    switch(work_item.artifact_lifecycle_intent) {
    case ArtifactLifecycleIntent::SingularCompatibility:
    case ArtifactLifecycleIntent::PackageBaseSet:
        break;
    case ArtifactLifecycleIntent::Unspecified:
    default:
        throw std::logic_error(
                "Production source-build work item has no supported artifact lifecycle intent.");
    }
    if(work_item.artifact_lifecycle_intent ==
               ArtifactLifecycleIntent::PackageBaseSet &&
       work_item.request.only_if_updated) {
        throw std::logic_error(
                "Production PackageBase set source-build work item does not support only-if-updated requests.");
    }

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
    if(work_item.artifact_lifecycle_intent ==
               ArtifactLifecycleIntent::SingularCompatibility &&
       work_item.required_targets.size() != 1) {
        throw std::logic_error(
                "Production source-build singular compatibility work item has multiple required targets.");
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

    for(std::size_t index = 0;
        index < work_item.selected_repository_providers.size(); ++index) {
        const ProvidedDependency& provider =
                work_item.selected_repository_providers[index];
        require_selected_repository_provider(provider);
        if(std::any_of(
                   work_item.selected_repository_providers.begin(),
                   work_item.selected_repository_providers.begin() + index,
                   [&provider](const ProvidedDependency& existing) {
                       return same_provider_identity(existing, provider);
                   })) {
            throw std::logic_error(
                    "Production source-build work item contains a duplicate selected repository provider.");
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

    if(work_item.required_target_provenance ==
       RequiredTargetProvenance::RepositoryExactPackageProjection) {
        if(!work_item.repository_identity.has_value()) {
            throw std::logic_error(
                    "Repository source-build work item has no exact repository identity.");
        }
        const ResolvedRepositorySourceBuildIdentity& identity =
                work_item.repository_identity.value();
        const RepositoryPackagePresent& exact = identity.exact_package();
        if(work_item.required_targets.size() != 1 ||
           work_item.request.package_name != identity.requested_child() ||
           work_item.request.checkout_name != identity.package_base() ||
           work_item.request.git_url != identity.checkout().git_url() ||
           work_item.required_targets.front().package_base !=
                   identity.package_base() ||
           work_item.required_targets.front().package_name !=
                   identity.requested_child() ||
           work_item.required_targets.front().desired_reason !=
                   DesiredInstallReason::Explicit ||
           work_item.configured_repository_order !=
                   exact.configured_repository_order ||
           !work_item.uses_system_update_baseline) {
            throw std::logic_error(
                    "Repository source-build work item does not match its exact repository identity.");
        }
    } else if(work_item.repository_identity.has_value()) {
        throw std::logic_error(
                "AUR BuildPlan source-build work item contains a repository identity.");
    }
}

const RequiredPackageArtifactTarget& require_singular_required_package_target(
        const ProductionSourceBuildWorkItem& work_item) {
    if(work_item.artifact_lifecycle_intent !=
       ArtifactLifecycleIntent::SingularCompatibility) {
        throw std::logic_error(
                "Production separated source-build requires singular compatibility lifecycle intent.");
    }
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
    std::vector<ProvidedDependency> selected_repository_providers =
            collect_selected_repository_providers(work_items);
    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items),
            std::move(selected_repository_providers),
            std::move(database_paths),
            std::move(supplied_cache_root)};
}

PreparedProductionSourceBuildInvocation
prepare_local_source_build_dependency_invocation(
        LocalSourceBuildDependencyPreparation preparation,
        const ValidatedCacheRoot& cache_root,
        const AppConfig& config) {
    // POLICY(#271): local root ownerが別に存在するためremote work item 0件を
    // 許可する。generic production invocationのnonempty契約は変更しない。
    require_supported_separated_install_options(config.rm_deps);
    require_unclaimed_artifact_pkgdest(SourceBuildEnvironment{});
    for(const auto& work_item : preparation.remote_work_items_) {
        require_unclaimed_artifact_pkgdest(
                work_item.request.custom_environment);
    }
    for(const auto& work_item : preparation.remote_work_items_) {
        require_static_production_source_build_work_item(work_item);
    }

    std::vector<ProvidedDependency> selected_repository_providers;
    selected_repository_providers.reserve(
            preparation.selected_repository_providers_.size());
    for(const auto& provider :
        preparation.selected_repository_providers_) {
        add_selected_repository_provider(
                selected_repository_providers, provider);
    }
    for(const auto& work_item : preparation.remote_work_items_) {
        for(const auto& provider :
            work_item.selected_repository_providers) {
            add_selected_repository_provider(
                    selected_repository_providers, provider);
        }
    }

    cache_root.require_unchanged_identity();
    std::optional<ValidatedCacheRoot> existing_cache_root =
            shared_prepared_cache_root(preparation.remote_work_items_);
    if(existing_cache_root.has_value()) {
        existing_cache_root->require_unchanged_identity();
        if(existing_cache_root->device() != cache_root.device() ||
           existing_cache_root->inode() != cache_root.inode() ||
           existing_cache_root->owner() != cache_root.owner()) {
            throw std::logic_error(
                    "Local source-build dependencies use a different cache authority.");
        }
    }
    for(const auto& work_item : preparation.remote_work_items_) {
        if(!work_item.cache_root.has_value()) {
            continue;
        }
        work_item.cache_root->require_unchanged_identity();
        if(work_item.cache_root->device() != cache_root.device() ||
           work_item.cache_root->inode() != cache_root.inode() ||
           work_item.cache_root->owner() != cache_root.owner()) {
            throw std::logic_error(
                    "Local source-build dependencies use a different cache authority.");
        }
    }
    for(auto& work_item : preparation.remote_work_items_) {
        work_item.cache_root = cache_root;
    }

    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();
    return PreparedProductionSourceBuildInvocation{
            std::move(preparation.remote_work_items_),
            std::move(selected_repository_providers),
            std::move(database_paths), cache_root,
            LocalSourceBuildInvocationAuthority{}};
}
