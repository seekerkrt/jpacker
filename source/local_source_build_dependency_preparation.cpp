#include "source_install.hpp"

#include "build_plan_artifact_target_projection.hpp"
#include "local_dependency_plan_projection.hpp"
#include "localization.hpp"
#include "source_install_internal.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

void add_selected_repository_provider(
    std::vector<ProvidedDependency>& providers,
    const ProvidedDependency& provider) {
    const auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider_identity(existing, provider);
    };
    if(std::find_if(providers.begin(), providers.end(), same) ==
       providers.end()) {
        providers.push_back(provider);
    }
}

std::vector<ProvidedDependency> collect_selected_repository_providers(
    const BuildPlan& plan) {
    std::vector<ProvidedDependency> providers;
    for(const BuildPlanDependencyEdge& edge : plan.dependency_edges) {
        if(edge.kind != DependencyKind::Provided ||
           edge.provider_resolution != ProviderResolutionKind::UserSelected ||
           !edge.resolved_provider.has_value() ||
           !std::holds_alternative<RepositoryProviderOrigin>(
               edge.resolved_provider->origin)) {
            continue;
        }
        add_selected_repository_provider(
            providers, edge.resolved_provider.value());
    }
    return providers;
}

} // namespace

LocalSourceBuildDependencyPreparation
prepare_local_source_build_dependencies(
    const LocalBuildPlan& local_plan,
    bool use_source_build_preferences,
    bool needed) {
    const BuildPlan& plan = local_plan.build_plan();
    require_compatible_selected_provider_package_identities(plan);
    BuildPlanArtifactTargetProjectionResult projection =
        project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success()) {
        throw std::logic_error(localization::format_translated_message(
            "{} required artifact target projection failed before local source-build dependency preparation.",
            "BuildPlan"));
    }

    const std::string& local_package_base =
        local_plan.local_metadata().package_base;
    bool saw_local_unit = false;
    std::vector<ProductionSourceBuildWorkItem> remote_work_items;
    remote_work_items.reserve(
        projection.success()->build_units.size());
    for(const auto& unit : projection.success()->build_units) {
        if(unit.package_base == local_package_base) {
            if(saw_local_unit) {
                // TRANSLATORS: The placeholder is the literal PackageBase identity.
                throw std::logic_error(
                    localization::format_translated_message(
                        "Local source-build dependency preparation found duplicate local {} units.",
                        "PackageBase"));
            }
            saw_local_unit = true;
            continue;
        }
        remote_work_items.push_back(
            prepare_aur_source_build_work_item_internal(
                unit, plan, use_source_build_preferences, needed));
    }
    if(!saw_local_unit) {
        // TRANSLATORS: The placeholder is the literal PackageBase identity.
        throw std::logic_error(localization::format_translated_message(
            "Local source-build dependency preparation did not find its local {} unit.",
            "PackageBase"));
    }
    return LocalSourceBuildDependencyPreparation(
        std::move(remote_work_items),
        collect_selected_repository_providers(plan));
}
