#include "unified_plan_renderer.hpp"

#include "aur_update_execution_preflight.hpp"
#include "aur_update_query.hpp"
#include "commands_sync.hpp"
#include "local_dependency_plan_projection.hpp"
#include "localization.hpp"
#include "system_source_upgrade.hpp"
#include "upgrade_all_operation.hpp"

#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

struct RenderState {
    std::ostringstream                    output;
    std::vector<UnifiedPlanRenderingIssue> issues;

    void add_issue(
            UnifiedPlanRenderingIssueKind kind,
            UnifiedPlanRenderingSection section, std::size_t item_index,
            std::optional<std::size_t> detail_index,
            std::string diagnostic) {
        issues.push_back(UnifiedPlanRenderingIssue{
                kind, section, item_index, detail_index,
                std::move(diagnostic)});
    }
};

std::string unsupported_display(
        RenderState& state, UnifiedPlanRenderingSection section,
        std::size_t item_index, std::optional<std::size_t> detail_index,
        std::string diagnostic) {
    state.add_issue(
            UnifiedPlanRenderingIssueKind::UnsupportedValue, section,
            item_index, detail_index, std::move(diagnostic));
    return localization::translate_message("unsupported");
}

std::string unavailable_display(
        RenderState& state, UnifiedPlanRenderingSection section,
        std::size_t item_index, std::optional<std::size_t> detail_index,
        std::string diagnostic) {
    state.add_issue(
            UnifiedPlanRenderingIssueKind::MissingReferencedValue, section,
            item_index, detail_index, std::move(diagnostic));
    return localization::translate_message("unavailable");
}

std::string required_string_display(
        const std::string& value, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index,
        std::string diagnostic) {
    if(!value.empty()) return value;
    return unavailable_display(
            state, section, item_index, detail_index, std::move(diagnostic));
}

std::string optional_string_display(
        const std::optional<std::string>& value) {
    return value.has_value() && !value->empty()
            ? value.value()
            : localization::translate_message("not observed");
}

std::string optional_index_display(
        const std::optional<std::size_t>& value) {
    return value.has_value()
            ? std::to_string(value.value())
            : localization::translate_message("not observed");
}

std::string yes_no_display(bool value) {
    return value ? localization::translate_message("yes")
                 : localization::translate_message("no");
}

std::string join_display_values(const std::vector<std::string>& values) {
    std::string display;
    for(std::size_t index = 0; index < values.size(); ++index) {
        if(index != 0) display += ", ";
        display += values[index];
    }
    return display;
}

std::string observation_status_display(
        UnifiedPlanObservationStatus status, RenderState& state) {
    switch(status) {
    case UnifiedPlanObservationStatus::Ready:
        return localization::translate_message("Ready");
    case UnifiedPlanObservationStatus::NoOp:
        return localization::translate_message("NoOp");
    case UnifiedPlanObservationStatus::Blocked:
        return localization::translate_message("Blocked");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Status, 0, std::nullopt,
            localization::translate_message(
                    "Unified plan status is not supported by the renderer."));
}

std::string root_source_display(
        UnifiedPlanRootSourceKind source, RenderState& state,
        std::size_t root_index) {
    switch(source) {
    case UnifiedPlanRootSourceKind::Repository:
        return localization::translate_message("repository");
    case UnifiedPlanRootSourceKind::Aur:
        return "AUR";
    case UnifiedPlanRootSourceKind::Local:
        return localization::translate_message("local");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Roots, root_index,
            std::nullopt,
            localization::translate_message(
                    "A root source is not supported by the renderer."));
}

std::string root_route_display(
        UnifiedPlanRootRouteKind route, RenderState& state,
        std::size_t root_index) {
    switch(route) {
    case UnifiedPlanRootRouteKind::RepositoryTransaction:
        return localization::translate_message("repository transaction");
    case UnifiedPlanRootRouteKind::RepositorySourceBuild:
        return localization::translate_message("repository source build");
    case UnifiedPlanRootRouteKind::AurSourceBuild:
        return localization::format_translated_message(
                "{} source build", "AUR");
    case UnifiedPlanRootRouteKind::LocalSourceBuild:
        return localization::translate_message("local source build");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Roots, root_index,
            std::nullopt,
            localization::translate_message(
                    "A root route is not supported by the renderer."));
}

std::string root_identity_display(
        const UnifiedPlanRootReference& root, RenderState& state,
        std::size_t root_index,
        UnifiedPlanRenderingSection section =
                UnifiedPlanRenderingSection::Roots,
        std::optional<std::size_t> detail_index = std::nullopt) {
    return std::visit(
            [&](const auto& identity) -> std::string {
                using Identity = std::decay_t<decltype(identity)>;
                if constexpr(std::is_same_v<
                                     Identity,
                                     RepositoryRootPackageIdentity>) {
                    return required_string_display(
                                   identity.repository_name, state, section,
                                   root_index, detail_index,
                                   localization::translate_message(
                                           "A repository root is missing its repository identity.")) +
                           "/" +
                           required_string_display(
                                   identity.package_name, state, section,
                                   root_index, detail_index,
                                   localization::translate_message(
                                           "A repository root is missing its package identity."));
                } else if constexpr(std::is_same_v<
                                            Identity,
                                            RepositorySourceBuildRootIdentity>) {
                    return localization::format_translated_message(
                            "{} ({}: {}; source key: {})",
                            required_string_display(
                                    identity.package_name, state, section,
                                    root_index, detail_index,
                                    localization::translate_message(
                                            "A repository source root is missing its package identity.")),
                            "PackageBase",
                            required_string_display(
                                    identity.package_base, state, section,
                                    root_index, detail_index,
                                    localization::format_translated_message(
                                            "A repository source root is missing its {} identity.",
                                            "PackageBase")),
                            required_string_display(
                                    identity.canonical_source_identity_key,
                                    state, section, root_index, detail_index,
                                    localization::translate_message(
                                            "A repository source root is missing its source identity key.")));
                } else if constexpr(std::is_same_v<
                                            Identity,
                                            AurRootPackageIdentity>) {
                    return localization::format_translated_message(
                            "{}/{} ({}: {})", "AUR",
                            required_string_display(
                                    identity.package_name, state, section,
                                    root_index, detail_index,
                                    localization::format_translated_message(
                                            "An {} root is missing its package identity.",
                                            "AUR")),
                            "PackageBase",
                            required_string_display(
                                    identity.package_base, state, section,
                                    root_index, detail_index,
                                    localization::format_translated_message(
                                            "An {} root is missing its {} identity.",
                                            "AUR", "PackageBase")));
                } else {
                    return localization::format_translated_message(
                            "{} (device: {}; inode: {})",
                            required_string_display(
                                    identity.canonical_path.generic_string(),
                                    state, section, root_index, detail_index,
                                    localization::translate_message(
                                            "A local root is missing its canonical path identity.")),
                            identity.directory_identity.device,
                            identity.directory_identity.inode);
                }
            },
            root.source_identity());
}

std::string observation_phase_display(
        UnifiedPlanObservationPhase phase, RenderState& state,
        std::size_t phase_index) {
    switch(phase) {
    case UnifiedPlanObservationPhase::RequestDiscovery:
        return localization::translate_message("request / root discovery");
    case UnifiedPlanObservationPhase::MetadataDiscovery:
        return localization::translate_message(
                "metadata / dependency candidate discovery");
    case UnifiedPlanObservationPhase::ProviderDecision:
        return localization::translate_message(
                "provider decision / refreshed resolution");
    case UnifiedPlanObservationPhase::ExecutionProjection:
        return localization::translate_message(
                "execution preflight / projection");
    case UnifiedPlanObservationPhase::RepositoryTransaction:
        return localization::translate_message("repository transaction");
    case UnifiedPlanObservationPhase::SourceRetrieval:
        return localization::translate_message("source retrieval");
    case UnifiedPlanObservationPhase::SourceBuild:
        return localization::translate_message("source build");
    case UnifiedPlanObservationPhase::ArtifactValidation:
        return localization::translate_message(
                "artifact validation / selection");
    case UnifiedPlanObservationPhase::SourceArtifactInstall:
        return localization::translate_message(
                "source-built artifact install");
    case UnifiedPlanObservationPhase::CleanupReduction:
        return localization::translate_message("cleanup / reduction");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Phases, phase_index,
            std::nullopt,
            localization::translate_message(
                    "An observation phase is not supported by the renderer."));
}

std::string authority_owner_display(
        UnifiedPlanAuthorityOwner owner, RenderState& state,
        std::size_t phase_index) {
    switch(owner) {
    case UnifiedPlanAuthorityOwner::Moguet:
        return "Moguet";
    case UnifiedPlanAuthorityOwner::Libalpm:
        return "libalpm";
    case UnifiedPlanAuthorityOwner::Pacman:
        return "pacman";
    case UnifiedPlanAuthorityOwner::AurRpc:
        return "AUR RPC";
    case UnifiedPlanAuthorityOwner::Git:
        return "Git";
    case UnifiedPlanAuthorityOwner::Makepkg:
        return "makepkg";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Phases, phase_index,
            std::nullopt,
            localization::translate_message(
                    "A phase owner is not supported by the renderer."));
}

std::string system_source_phase_display(
        SystemSourceUpgradePhase phase, RenderState& state,
        std::size_t phase_index) {
    switch(phase) {
    case SystemSourceUpgradePhase::None:
        return localization::translate_message("none");
    case SystemSourceUpgradePhase::Preparation:
        return localization::translate_message("preparation");
    case SystemSourceUpgradePhase::System:
        return localization::translate_message("system");
    case SystemSourceUpgradePhase::RegisteredSource:
        return localization::translate_message("registered source");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Phases, phase_index,
            std::nullopt,
            localization::translate_message(
                    "A system/source route phase is not supported by the renderer."));
}

std::string upgrade_all_phase_display(
        UpgradeAllOperationPhase phase, RenderState& state,
        std::size_t phase_index) {
    switch(phase) {
    case UpgradeAllOperationPhase::None:
        return localization::translate_message("none");
    case UpgradeAllOperationPhase::Preparation:
        return localization::translate_message("preparation");
    case UpgradeAllOperationPhase::System:
        return localization::translate_message("system");
    case UpgradeAllOperationPhase::RegisteredSource:
        return localization::translate_message("registered source");
    case UpgradeAllOperationPhase::ForeignInventory:
        return localization::translate_message("foreign package inventory");
    case UpgradeAllOperationPhase::AurQuery:
        return localization::format_translated_message("{} query", "AUR");
    case UpgradeAllOperationPhase::AurPreparation:
        return localization::format_translated_message(
                "{} preparation", "AUR");
    case UpgradeAllOperationPhase::AurExecution:
        return localization::format_translated_message(
                "{} execution", "AUR");
    case UpgradeAllOperationPhase::Reduction:
        return localization::translate_message("reduction");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Phases, phase_index,
            std::nullopt,
            localization::format_translated_message(
                    "An {} route phase is not supported by the renderer.",
                    "upgrade-all"));
}

std::string existing_route_phase_display(
        const ExistingRoutePhaseReference& phase, RenderState& state,
        std::size_t phase_index) {
    return std::visit(
            [&](const auto& existing_phase) {
                using Phase = std::decay_t<decltype(existing_phase)>;
                if constexpr(std::is_same_v<Phase, SystemSourceUpgradePhase>) {
                    return localization::format_translated_message(
                            "system/source: {}",
                            system_source_phase_display(
                                    existing_phase, state, phase_index));
                } else {
                    return localization::format_translated_message(
                            "{}: {}", "upgrade-all",
                            upgrade_all_phase_display(
                                    existing_phase, state, phase_index));
                }
            },
            phase);
}

std::string dependency_kind_display(
        DependencyKind kind, RenderState& state,
        std::size_t authority_index, std::size_t edge_index) {
    switch(kind) {
    case DependencyKind::Installed:
        return localization::translate_message("installed");
    case DependencyKind::Repo:
        return localization::translate_message("repository");
    case DependencyKind::Aur:
        return "AUR";
    case DependencyKind::Local:
        return localization::translate_message("local");
    case DependencyKind::Provided:
        return localization::translate_message("provider");
    case DependencyKind::AmbiguousProvider:
        return localization::translate_message("ambiguous provider");
    case DependencyKind::Unknown:
        return localization::translate_message("unknown");
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Dependencies,
            authority_index, edge_index,
            localization::translate_message(
                    "A dependency kind is not supported by the renderer."));
}

std::string package_role_display(
        PackageRole role, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index) {
    switch(role) {
    case PackageRole::Root:
        return localization::translate_message("root");
    case PackageRole::RuntimeDependency:
        return localization::translate_message("runtime dependency");
    case PackageRole::BuildDependency:
        return localization::translate_message("build dependency");
    case PackageRole::CheckDependency:
        return localization::translate_message("check dependency");
    }
    return unsupported_display(
            state, section, item_index, detail_index,
            localization::translate_message(
                    "A package role is not supported by the renderer."));
}

std::string provider_identity_display(
        const ProvidedDependency& provider, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index) {
    if(const auto* repository =
               std::get_if<RepositoryProviderOrigin>(&provider.origin);
       repository != nullptr) {
        return required_string_display(
                       repository->repository_name, state, section,
                       item_index, detail_index,
                       localization::translate_message(
                               "A repository provider is missing its repository identity.")) +
               "/" +
               required_string_display(
                       provider.package_name, state, section, item_index,
                       detail_index,
                       localization::translate_message(
                               "A repository provider is missing its package identity."));
    }
    return localization::format_translated_message(
            "{}/{} ({}: {})", "AUR",
            required_string_display(
                    provider.package_name, state, section, item_index,
                    detail_index,
                    localization::format_translated_message(
                            "An {} provider is missing its package identity.",
                            "AUR")),
            "PackageBase",
            required_string_display(
                    provider.package_base, state, section, item_index,
                    detail_index,
                    localization::format_translated_message(
                            "An {} provider is missing its {} identity.",
                            "AUR", "PackageBase")));
}

std::string resolved_candidate_display(
        const ResolvedDependencyCandidate& candidate, RenderState& state,
        std::size_t authority_index, std::size_t edge_index) {
    return std::visit(
            [&](const auto& resolved) -> std::string {
                using Candidate = std::decay_t<decltype(resolved)>;
                if constexpr(std::is_same_v<Candidate, InstalledExactPackage>) {
                    return localization::format_translated_message(
                            "installed/{}",
                            required_string_display(
                                    resolved.package_name, state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, edge_index,
                                    localization::translate_message(
                                            "An installed dependency candidate is missing its package identity.")));
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            RepositoryExactPackage>) {
                    return required_string_display(
                                   resolved.repository.repository_name, state,
                                   UnifiedPlanRenderingSection::Dependencies,
                                   authority_index, edge_index,
                                   localization::translate_message(
                                           "A repository dependency candidate is missing its repository identity.")) +
                           "/" +
                           required_string_display(
                                   resolved.package_name, state,
                                   UnifiedPlanRenderingSection::Dependencies,
                                   authority_index, edge_index,
                                   localization::translate_message(
                                           "A repository dependency candidate is missing its package identity."));
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            AurResolvedDependencyCandidate>) {
                    return localization::format_translated_message(
                            "{}/{} ({}: {})", "AUR",
                            required_string_display(
                                    resolved.package_name, state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, edge_index,
                                    localization::format_translated_message(
                                            "An {} dependency candidate is missing its package identity.",
                                            "AUR")),
                            "PackageBase",
                            required_string_display(
                                    resolved.package_base, state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, edge_index,
                                    localization::format_translated_message(
                                            "An {} dependency candidate is missing its {} identity.",
                                            "AUR", "PackageBase")));
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            LocalResolvedDependencyCandidate>) {
                    return localization::format_translated_message(
                            "local/{} ({}: {})",
                            required_string_display(
                                    resolved.package_name, state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, edge_index,
                                    localization::translate_message(
                                            "A local dependency candidate is missing its package identity.")),
                            "PackageBase",
                            required_string_display(
                                    resolved.package_base, state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, edge_index,
                                    localization::format_translated_message(
                                            "A local dependency candidate is missing its {} identity.",
                                            "PackageBase")));
                } else {
                    return provider_identity_display(
                            resolved.provider, state,
                            UnifiedPlanRenderingSection::Dependencies,
                            authority_index, edge_index);
                }
            },
            candidate);
}

std::string constraint_evaluation_display(
        const ConstraintEvaluation& evaluation, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index) {
    try {
        return localization::format_translated_message(
                "{} ({})",
                constraint_satisfaction_display(evaluation.satisfaction()),
                constraint_evaluation_reason_display(evaluation));
    } catch(const std::exception& error) {
        return unsupported_display(
                state, section, item_index, detail_index,
                localization::format_translated_message(
                        "A dependency constraint could not be rendered: {}",
                        error.what()));
    }
}

std::string install_reason_display(
        DesiredInstallReason reason, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index = std::nullopt) {
    switch(reason) {
    case DesiredInstallReason::Explicit:
        return localization::translate_message("explicit");
    case DesiredInstallReason::Dependency:
        return localization::translate_message("dependency");
    }
    return unsupported_display(
            state, section, item_index, detail_index,
            localization::translate_message(
                    "An install reason is not supported by the renderer."));
}

std::string source_build_kind_display(
        SourceBuildSourceKind kind, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index = std::nullopt) {
    switch(kind) {
    case SourceBuildSourceKind::Repository:
        return localization::translate_message("repository");
    case SourceBuildSourceKind::Aur:
        return "AUR";
    }
    return unsupported_display(
            state, section, item_index, detail_index,
            localization::translate_message(
                    "A source-build kind is not supported by the renderer."));
}

void render_roots(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message("Roots:") << '\n';
    if(observation.roots().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0; index < observation.roots().size(); ++index) {
        const UnifiedPlanRootReference& root = observation.roots()[index];
        const RootTargetIdentity& correlation = root.invocation_correlation();
        state.output << localization::format_translated_message(
                                "  {}. Identity: {}", index + 1,
                                root_identity_display(root, state, index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Source: {}",
                                root_source_display(
                                        root.source_kind(), state, index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Route: {}",
                                root_route_display(
                                        root.route_kind(), state, index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Request: {} (invocation index: {})",
                                required_string_display(
                                        correlation.requested_name, state,
                                        UnifiedPlanRenderingSection::Roots,
                                        index, std::nullopt,
                                        localization::translate_message(
                                                "A root is missing its invocation request identity.")),
                                correlation.invocation_index)
                     << '\n';
    }
}

void render_phases(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message("Phases:") << '\n';
    if(observation.phases().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0; index < observation.phases().size(); ++index) {
        const UnifiedPlanPhaseReference& phase = observation.phases()[index];
        state.output << localization::format_translated_message(
                                "  {}. {}", index + 1,
                                observation_phase_display(
                                        phase.observation_phase, state,
                                        index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     External owner: {}",
                                authority_owner_display(
                                        phase.owner, state, index))
                     << '\n';
        if(phase.existing_route_phase.has_value()) {
            state.output << localization::format_translated_message(
                                    "     Route phase: {}",
                                    existing_route_phase_display(
                                            phase.existing_route_phase.value(),
                                            state, index))
                         << '\n';
        }
    }
}

void render_configured_repositories(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message(
                            "Configured repository order:")
                 << '\n';
    const UnifiedPlanConfiguredRepositoryOrderReference* repositories =
            observation.configured_repository_order();
    if(repositories == nullptr) {
        state.output << localization::translate_message("  Not observed")
                     << '\n';
        return;
    }
    if(repositories->configured_order().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0;
        index < repositories->configured_order().size(); ++index) {
        state.output << "  " << index + 1 << ". "
                     << repositories->configured_order()[index] << '\n';
    }
}

void render_build_plan_dependencies(
        const BuildPlan& plan, std::size_t authority_index,
        RenderState& state) {
    if(plan.dependency_edges.empty()) {
        state.output << localization::translate_message(
                                "     Dependencies: None")
                     << '\n';
        return;
    }
    for(std::size_t edge_index = 0;
        edge_index < plan.dependency_edges.size(); ++edge_index) {
        const BuildPlanDependencyEdge& edge =
                plan.dependency_edges[edge_index];
        state.output << localization::format_translated_message(
                                "     {}. {} ({}: {}) -> {} [{}; role: {}]",
                                edge_index + 1,
                                required_string_display(
                                        edge.parent_package_name, state,
                                        UnifiedPlanRenderingSection::
                                                Dependencies,
                                        authority_index, edge_index,
                                        localization::translate_message(
                                                "A dependency edge is missing its parent package identity.")),
                                "PackageBase",
                                required_string_display(
                                        edge.parent_package_base, state,
                                        UnifiedPlanRenderingSection::
                                                Dependencies,
                                        authority_index, edge_index,
                                        localization::format_translated_message(
                                                "A dependency edge is missing its parent {} identity.",
                                                "PackageBase")),
                                required_string_display(
                                        edge.dependency_spec, state,
                                        UnifiedPlanRenderingSection::
                                                Dependencies,
                                        authority_index, edge_index,
                                        localization::translate_message(
                                                "A dependency edge is missing its dependency specification.")),
                                dependency_kind_display(
                                        edge.kind, state, authority_index,
                                        edge_index),
                                package_role_display(
                                        edge.role, state,
                                        UnifiedPlanRenderingSection::
                                                Dependencies,
                                        authority_index, edge_index))
                     << '\n';
        if(edge.resolved_candidate.has_value()) {
            state.output << localization::format_translated_message(
                                    "        Selected identity: {}",
                                    resolved_candidate_display(
                                            edge.resolved_candidate.value(),
                                            state, authority_index,
                                            edge_index))
                         << '\n';
        } else if(edge.resolved_provider.has_value()) {
            state.output << localization::format_translated_message(
                                    "        Selected provider: {}",
                                    provider_identity_display(
                                            edge.resolved_provider.value(),
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index))
                         << '\n';
        } else if(edge.kind != DependencyKind::Unknown &&
                  edge.kind != DependencyKind::AmbiguousProvider) {
            state.output << localization::format_translated_message(
                                    "        Selected identity: {}",
                                    unavailable_display(
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index,
                                            localization::translate_message(
                                                    "A resolved dependency edge is missing its selected candidate or provider identity.")))
                         << '\n';
        }
        if(edge.constraint_evaluation.has_value()) {
            state.output << localization::format_translated_message(
                                    "        Stored constraint result: {}",
                                    constraint_evaluation_display(
                                            edge.constraint_evaluation.value(),
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index))
                         << '\n';
        } else {
            state.output << localization::format_translated_message(
                                    "        Stored constraint result: {}",
                                    unavailable_display(
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index,
                                            localization::translate_message(
                                                    "A dependency edge is missing its stored constraint result.")))
                         << '\n';
        }
    }
}

std::string local_dependency_plan_failure_display(
        const LocalDependencyPlanFailure& failure, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index);

void render_local_dependency_details(
        const LocalBuildPlan& plan, std::size_t authority_index,
        RenderState& state) {
    state.output << localization::format_translated_message(
                            "     Effective architecture: {}",
                            required_string_display(
                                    plan.effective_architecture(), state,
                                    UnifiedPlanRenderingSection::Dependencies,
                                    authority_index, std::nullopt,
                                    localization::translate_message(
                                            "A LocalBuildPlan is missing its effective architecture.")))
                 << '\n';
    if(plan.internal_edges().empty()) {
        state.output << localization::translate_message(
                                "     Local internal dependencies: None")
                     << '\n';
    } else {
        state.output << localization::translate_message(
                                "     Local internal dependencies:")
                     << '\n';
        for(std::size_t edge_index = 0;
            edge_index < plan.internal_edges().size(); ++edge_index) {
            const LocalDependencyPlanInternalEdge& edge =
                    plan.internal_edges()[edge_index];
            state.output << localization::format_translated_message(
                                    "       {}. {} -> {} (resolved package: {}; role: {}; cycle: {})",
                                    edge_index + 1,
                                    required_string_display(
                                            edge.parent_package_name, state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index,
                                            localization::translate_message(
                                                    "A local dependency edge is missing its parent package identity.")),
                                    required_string_display(
                                            edge.dependency_specification,
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index,
                                            localization::translate_message(
                                                    "A local dependency edge is missing its dependency specification.")),
                                    required_string_display(
                                            edge.resolved_package_name, state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index,
                                            localization::translate_message(
                                                    "A local dependency edge is missing its resolved package identity.")),
                                    package_role_display(
                                            edge.role, state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, edge_index),
                                    yes_no_display(edge.is_cycle))
                         << '\n';
            if(edge.provided_specification.has_value()) {
                state.output << localization::format_translated_message(
                                        "          Provided capability: {}",
                                        edge.provided_specification.value())
                             << '\n';
            }
            if(edge.resolved_candidate.has_value()) {
                state.output << localization::format_translated_message(
                                        "          Selected identity: {}",
                                        resolved_candidate_display(
                                                ResolvedDependencyCandidate{
                                                        edge.resolved_candidate
                                                                .value()},
                                                state, authority_index,
                                                edge_index))
                             << '\n';
            } else {
                state.output << localization::format_translated_message(
                                        "          Selected identity: {}",
                                        unavailable_display(
                                                state,
                                                UnifiedPlanRenderingSection::
                                                        Dependencies,
                                                authority_index, edge_index,
                                                localization::translate_message(
                                                        "A local dependency edge is missing its selected candidate identity.")))
                             << '\n';
            }
            if(edge.constraint_evaluation.has_value()) {
                state.output << localization::format_translated_message(
                                        "          Stored constraint result: {}",
                                        constraint_evaluation_display(
                                                edge.constraint_evaluation
                                                        .value(),
                                                state,
                                                UnifiedPlanRenderingSection::
                                                        Dependencies,
                                                authority_index, edge_index))
                             << '\n';
            } else {
                state.output << localization::format_translated_message(
                                        "          Stored constraint result: {}",
                                        unavailable_display(
                                                state,
                                                UnifiedPlanRenderingSection::
                                                        Dependencies,
                                                authority_index, edge_index,
                                                localization::translate_message(
                                                        "A local dependency edge is missing its stored constraint result.")))
                             << '\n';
            }
        }
    }
    if(plan.failures().empty()) {
        state.output << localization::translate_message(
                                "     Local dependency failures: None")
                     << '\n';
    } else {
        state.output << localization::translate_message(
                                "     Local dependency failures:")
                     << '\n';
        for(std::size_t failure_index = 0;
            failure_index < plan.failures().size(); ++failure_index) {
            state.output << localization::format_translated_message(
                                    "       {}. {}", failure_index + 1,
                                    local_dependency_plan_failure_display(
                                            plan.failures()[failure_index],
                                            state,
                                            UnifiedPlanRenderingSection::
                                                    Dependencies,
                                            authority_index, failure_index))
                         << '\n';
        }
    }
}

void render_dependencies(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message(
                            "Dependency authorities:")
                 << '\n';
    if(observation.dependency_authorities().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0;
        index < observation.dependency_authorities().size(); ++index) {
        const UnifiedPlanDependencyAuthorityReference& authority =
                observation.dependency_authorities()[index];
        if(const BuildPlan* plan = authority.build_plan(); plan != nullptr) {
            state.output << localization::format_translated_message(
                                    "  {}. {}", index + 1, "BuildPlan")
                         << '\n';
            render_build_plan_dependencies(*plan, index, state);
            continue;
        }
        if(const LocalBuildPlan* local_plan = authority.local_build_plan();
           local_plan != nullptr) {
            state.output << localization::format_translated_message(
                                    "  {}. {}", index + 1,
                                    "LocalBuildPlan")
                         << '\n';
            render_build_plan_dependencies(
                    local_plan->build_plan(), index, state);
            render_local_dependency_details(*local_plan, index, state);
            continue;
        }
        state.output << localization::format_translated_message(
                                "  {}. {}", index + 1,
                                unavailable_display(
                                        state,
                                        UnifiedPlanRenderingSection::
                                                Dependencies,
                                        index, std::nullopt,
                                        localization::translate_message(
                                                "A dependency authority reference is unavailable.")))
                     << '\n';
    }
}

std::string build_unit_identity_display(
        const UnifiedPlanBuildUnitReference& build_unit, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index = std::nullopt) {
    return std::visit(
            [&](const auto& unit) -> std::string {
                using Unit = std::decay_t<decltype(unit)>;
                if(!unit.has_complete_identity()) {
                    state.add_issue(
                            UnifiedPlanRenderingIssueKind::
                                    MissingReferencedValue,
                            section, item_index, detail_index,
                            localization::translate_message(
                                    "A build unit has an incomplete typed source identity."));
                }
                if constexpr(std::is_same_v<
                                     Unit,
                                     AurPackageBaseBuildUnitReference>) {
                    const BuildPlanEntry* entry = unit.entry();
                    const std::string package_base =
                            entry == nullptr
                            ? localization::translate_message("unavailable")
                            : required_string_display(
                                      entry->package_base, state, section,
                                      item_index, detail_index,
                                      localization::format_translated_message(
                                              "An {} build unit is missing its {} identity.",
                                              "AUR", "PackageBase"));
                    if(entry == nullptr) {
                        state.add_issue(
                                UnifiedPlanRenderingIssueKind::
                                        MissingReferencedValue,
                                section, item_index, detail_index,
                                localization::format_translated_message(
                                        "An {} build unit no longer references a {} entry.",
                                        "AUR", "BuildPlan"));
                    }
                    return localization::format_translated_message(
                            "{} {} unit #{} ({}: {})", "AUR", "BuildPlan",
                            unit.build_plan_order_index() + 1, "PackageBase",
                            package_base);
                } else if constexpr(std::is_same_v<
                                            Unit,
                                            LocalSourceBuildUnitReference>) {
                    const LocalSourceRootObservationIdentity& source =
                            unit.source_root();
                    return localization::format_translated_message(
                            "local source {} (device: {}; inode: {}; {}: {})",
                            required_string_display(
                                    source.canonical_path.generic_string(),
                                    state, section, item_index, detail_index,
                                    localization::translate_message(
                                            "A local build unit is missing its canonical path identity.")),
                            source.directory_identity.device,
                            source.directory_identity.inode, "PackageBase",
                            required_string_display(
                                    unit.metadata().package_base, state,
                                    section, item_index, detail_index,
                                    localization::format_translated_message(
                                            "A local build unit is missing its {} identity.",
                                            "PackageBase")));
                } else {
                    const RegisteredSourcePreferenceSnapshot& source =
                            unit.source();
                    const std::string source_kind =
                            source.source_kind.has_value()
                            ? source_build_kind_display(
                                      source.source_kind.value(), state,
                                      section, item_index, detail_index)
                            : unavailable_display(
                                      state, section, item_index,
                                      detail_index,
                                      localization::translate_message(
                                              "A prepared source build unit is missing its source kind."));
                    const std::string source_key =
                            source.canonical_source_identity_key.has_value() &&
                                    !source.canonical_source_identity_key
                                             ->empty()
                            ? source.canonical_source_identity_key.value()
                            : unavailable_display(
                                      state, section, item_index,
                                      detail_index,
                                      localization::translate_message(
                                              "A prepared source build unit is missing its source identity key."));
                    return localization::format_translated_message(
                            "{} source key {} (requested package: {}; checkout {}: {})",
                            source_kind, source_key,
                            required_string_display(
                                    unit.requested_package_name(), state,
                                    section, item_index, detail_index,
                                    localization::translate_message(
                                            "A prepared source build unit is missing its requested package identity.")),
                            "PackageBase",
                            required_string_display(
                                    unit.checkout_package_base(), state,
                                    section, item_index, detail_index,
                                    localization::format_translated_message(
                                            "A prepared source build unit is missing its checkout {} identity.",
                                            "PackageBase")));
                }
            },
            build_unit);
}

void render_build_units(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message("Build units:") << '\n';
    if(observation.build_units().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0; index < observation.build_units().size();
        ++index) {
        const UnifiedPlanBuildUnitReference& build_unit =
                observation.build_units()[index];
        state.output << localization::format_translated_message(
                                "  {}. {}", index + 1,
                                build_unit_identity_display(
                                        build_unit, state,
                                        UnifiedPlanRenderingSection::
                                                BuildUnits,
                                        index))
                     << '\n';
        std::visit(
                [&](const auto& unit) {
                    using Unit = std::decay_t<decltype(unit)>;
                    if constexpr(std::is_same_v<
                                         Unit,
                                         AurPackageBaseBuildUnitReference>) {
                        const BuildPlanEntry* entry = unit.entry();
                        if(entry == nullptr) {
                            return;
                        }
                        const std::string children =
                                entry->package_names.empty()
                                ? localization::translate_message(
                                          "unavailable")
                                : join_display_values(entry->package_names);
                        state.output << localization::format_translated_message(
                                                "     Child packages: {}",
                                                children)
                                     << '\n';
                    } else if constexpr(std::is_same_v<
                                                Unit,
                                                LocalSourceBuildUnitReference>) {
                        const LocalPackageMetadata& metadata = unit.metadata();
                        std::vector<std::string> children;
                        children.reserve(metadata.children.size());
                        for(const LocalPackageMetadataChild& child :
                            metadata.children) {
                            children.push_back(child.name);
                        }
                        state.output << localization::format_translated_message(
                                                "     Child packages: {}",
                                                children.empty()
                                                        ? localization::
                                                                  translate_message(
                                                                          "unavailable")
                                                        : join_display_values(
                                                                  children))
                                     << '\n';
                    } else {
                        state.output << localization::format_translated_message(
                                                "     Source preference: {}",
                                                required_string_display(
                                                        unit.source()
                                                                .preference_package_name,
                                                        state,
                                                        UnifiedPlanRenderingSection::
                                                                BuildUnits,
                                                        index, std::nullopt,
                                                        localization::
                                                                translate_message(
                                                                        "A prepared source build unit is missing its source preference identity.")))
                                     << '\n';
                        for(std::size_t target_index = 0;
                            target_index < unit.required_targets().size();
                            ++target_index) {
                            const RequiredPackageArtifactTarget& target =
                                    unit.required_targets()[target_index];
                            state.output << localization::format_translated_message(
                                                    "     Required target {}: {}/{} ({})",
                                                    target_index + 1,
                                                    target.package_base,
                                                    target.package_name,
                                                    install_reason_display(
                                                            target.desired_reason,
                                                            state,
                                                            UnifiedPlanRenderingSection::
                                                                    BuildUnits,
                                                            index,
                                                            target_index))
                                         << '\n';
                        }
                    }
                },
                build_unit);
    }
}

void render_required_artifacts(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message(
                            "Required artifact targets:")
                 << '\n';
    if(observation.required_artifacts().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0;
        index < observation.required_artifacts().size(); ++index) {
        const RequiredArtifactTargetReference& artifact =
                observation.required_artifacts()[index];
        const RequiredPackageArtifactTarget target =
                artifact.target();
        state.output << localization::format_translated_message(
                                "  {}. Source/build unit: {}",
                                index + 1,
                                build_unit_identity_display(
                                        artifact.build_unit(), state,
                                        UnifiedPlanRenderingSection::
                                                RequiredArtifacts,
                                        index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Required target: {}: {}; child package: {}; install reason: {}",
                                "PackageBase", target.package_base,
                                target.package_name,
                                install_reason_display(
                                        target.desired_reason, state,
                                        UnifiedPlanRenderingSection::
                                                RequiredArtifacts,
                                        index))
                     << '\n';
    }
}

void render_repository_transaction_target(
        const RepositoryInstallIntentTarget& target,
        std::size_t intent_index, std::size_t target_index,
        RenderState& state) {
    state.output << "     - ";
    std::visit(
            [&](const auto& typed_target) {
                using Target = std::decay_t<decltype(typed_target)>;
                if constexpr(std::is_same_v<
                                     Target,
                                     RepositoryRootInstallIntent>) {
                    state.output << localization::format_translated_message(
                            "root: {}",
                            root_identity_display(
                                    typed_target.root, state, intent_index,
                                    UnifiedPlanRenderingSection::
                                            TransactionIntents,
                                    target_index));
                } else if constexpr(std::is_same_v<
                                            Target,
                                            RepositoryDependencyInstallIntent>) {
                    const RepositoryExactPackage& package =
                            typed_target.package.get();
                    state.output << localization::format_translated_message(
                            "dependency: {}/{}",
                            required_string_display(
                                    package.repository.repository_name, state,
                                    UnifiedPlanRenderingSection::
                                            TransactionIntents,
                                    intent_index, target_index,
                                    localization::translate_message(
                                            "A repository transaction dependency is missing its repository identity.")),
                            required_string_display(
                                    package.package_name, state,
                                    UnifiedPlanRenderingSection::
                                            TransactionIntents,
                                    intent_index, target_index,
                                    localization::translate_message(
                                            "A repository transaction dependency is missing its package identity.")));
                } else if constexpr(std::is_same_v<
                                            Target,
                                            RepositoryProviderInstallIntent>) {
                    state.output << localization::format_translated_message(
                            "selected provider: {}",
                            provider_identity_display(
                                    typed_target.provider.get(), state,
                                    UnifiedPlanRenderingSection::
                                            TransactionIntents,
                                    intent_index, target_index));
                } else {
                    state.output << localization::translate_message(
                            "system upgrade");
                }
            },
            target);
    state.output << '\n';
}

void render_source_transaction_target(
        const SourceArtifactInstallIntentTarget& target,
        const UnifiedPlanObservation& observation,
        std::size_t intent_index, std::size_t target_index,
        RenderState& state) {
    const std::size_t artifact_index = std::visit(
            [](const auto& typed_target) {
                return typed_target.required_artifact_index;
            },
            target);
    state.output << "     - ";
    const bool is_root =
            std::holds_alternative<SourceRootArtifactInstallIntent>(target);
    if(artifact_index >= observation.required_artifacts().size()) {
        state.output << unavailable_display(
                state, UnifiedPlanRenderingSection::TransactionIntents,
                intent_index, target_index,
                localization::translate_message(
                        "A source install intent references an unavailable required artifact."));
        state.output << '\n';
        return;
    }
    const RequiredArtifactTargetReference& artifact_reference =
            observation.required_artifacts()[artifact_index];
    const RequiredPackageArtifactTarget artifact =
            artifact_reference.target();
    state.output << localization::format_translated_message(
            "{} required artifact #{}: {}; target: {}/{}; install reason: {}",
            is_root ? localization::translate_message("root")
                    : localization::translate_message("dependency"),
            artifact_index + 1,
            build_unit_identity_display(
                    artifact_reference.build_unit(), state,
                    UnifiedPlanRenderingSection::TransactionIntents,
                    intent_index, target_index),
            artifact.package_base, artifact.package_name,
            install_reason_display(
                    artifact.desired_reason, state,
                    UnifiedPlanRenderingSection::TransactionIntents,
                    intent_index, target_index));
    state.output << '\n';
}

void render_transaction_intents(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message(
                            "Transaction intents:")
                 << '\n';
    if(observation.transaction_intents().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t intent_index = 0;
        intent_index < observation.transaction_intents().size();
        ++intent_index) {
        std::visit(
                [&](const auto& intent) {
                    using Intent = std::decay_t<decltype(intent)>;
                    if constexpr(std::is_same_v<
                                         Intent,
                                         RepositoryPackageTransactionIntent>) {
                        state.output << localization::format_translated_message(
                                                "  {}. {} ({} policy: {})",
                                                intent_index + 1,
                                                localization::translate_message(
                                                        "Repository package transaction"),
                                                "pacman --needed",
                                                yes_no_display(
                                                        intent.policy.needed))
                                     << '\n';
                        for(std::size_t target_index = 0;
                            target_index < intent.targets.size();
                            ++target_index) {
                            render_repository_transaction_target(
                                    intent.targets[target_index], intent_index,
                                    target_index, state);
                        }
                    } else {
                        state.output << localization::format_translated_message(
                                                "  {}. {} ({} policy: {})",
                                                intent_index + 1,
                                                localization::translate_message(
                                                        "Source-built artifact install boundary"),
                                                "pacman --needed",
                                                yes_no_display(intent.needed))
                                     << '\n';
                        for(std::size_t target_index = 0;
                            target_index < intent.targets.size();
                            ++target_index) {
                            render_source_transaction_target(
                                    intent.targets[target_index], observation,
                                    intent_index, target_index, state);
                        }
                    }
                },
                observation.transaction_intents()[intent_index]);
    }
}

std::string build_plan_resolution_failure_kind_display(
        BuildPlanResolutionFailureKind kind, std::size_t blocker_index,
        RenderState& state) {
    switch(kind) {
    case BuildPlanResolutionFailureKind::InstalledPackageMetadataUnavailable:
        return "BuildPlanResolutionFailureKind::InstalledPackageMetadataUnavailable";
    case BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable:
        return "BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable";
    case BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable:
        return "BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable";
    case BuildPlanResolutionFailureKind::ProviderSearchUnavailable:
        return "BuildPlanResolutionFailureKind::ProviderSearchUnavailable";
    case BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable:
        return "BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::format_translated_message(
                    "A {} resolution failure kind is not supported by the renderer.",
                    "BuildPlan"));
}

std::string observed_version_unknown_reason_display(
        ObservedVersionUnknownReason reason, std::size_t blocker_index,
        RenderState& state) {
    switch(reason) {
    case ObservedVersionUnknownReason::MissingVersionMetadata:
        return "ObservedVersionUnknownReason::MissingVersionMetadata";
    case ObservedVersionUnknownReason::UnversionedProviderCapability:
        return "ObservedVersionUnknownReason::UnversionedProviderCapability";
    case ObservedVersionUnknownReason::MetadataQueryFailure:
        return "ObservedVersionUnknownReason::MetadataQueryFailure";
    case ObservedVersionUnknownReason::PartialSourceFailure:
        return "ObservedVersionUnknownReason::PartialSourceFailure";
    case ObservedVersionUnknownReason::ComparisonAuthorityUnavailable:
        return "ObservedVersionUnknownReason::ComparisonAuthorityUnavailable";
    case ObservedVersionUnknownReason::CandidateVersionUnavailable:
        return "ObservedVersionUnknownReason::CandidateVersionUnavailable";
    case ObservedVersionUnknownReason::RelationKindNotComparable:
        return "ObservedVersionUnknownReason::RelationKindNotComparable";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "An observed-version unknown reason is not supported by the renderer."));
}

std::string package_metadata_error_code_display(
        PackageMetadataErrorCode code, std::size_t blocker_index,
        RenderState& state) {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationUnavailable:
        return "PackageMetadataErrorCode::ConfigurationUnavailable";
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return "PackageMetadataErrorCode::ConfigurationMalformed";
    case PackageMetadataErrorCode::InitializationFailed:
        return "PackageMetadataErrorCode::InitializationFailed";
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        return "PackageMetadataErrorCode::LocalDatabaseUnavailable";
    case PackageMetadataErrorCode::InvalidPackageName:
        return "PackageMetadataErrorCode::InvalidPackageName";
    case PackageMetadataErrorCode::QueryFailed:
        return "PackageMetadataErrorCode::QueryFailed";
    case PackageMetadataErrorCode::MalformedMetadata:
        return "PackageMetadataErrorCode::MalformedMetadata";
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return "PackageMetadataErrorCode::SyncDatabaseUnavailable";
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return "PackageMetadataErrorCode::RepositoryNotConfigured";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A package metadata error code is not supported by the renderer."));
}

std::string dependency_constraint_parse_failure_kind_display(
        DependencyConstraintParseFailureKind kind,
        std::size_t blocker_index, RenderState& state) {
    switch(kind) {
    case DependencyConstraintParseFailureKind::EmptySpecification:
        return "DependencyConstraintParseFailureKind::EmptySpecification";
    case DependencyConstraintParseFailureKind::InvalidPackageIdentity:
        return "DependencyConstraintParseFailureKind::InvalidPackageIdentity";
    case DependencyConstraintParseFailureKind::InvalidSonameIdentity:
        return "DependencyConstraintParseFailureKind::InvalidSonameIdentity";
    case DependencyConstraintParseFailureKind::UnsupportedConsumerOperator:
        return "DependencyConstraintParseFailureKind::UnsupportedConsumerOperator";
    case DependencyConstraintParseFailureKind::UnsupportedProviderOperator:
        return "DependencyConstraintParseFailureKind::UnsupportedProviderOperator";
    case DependencyConstraintParseFailureKind::MissingVersion:
        return "DependencyConstraintParseFailureKind::MissingVersion";
    case DependencyConstraintParseFailureKind::InvalidVersion:
        return "DependencyConstraintParseFailureKind::InvalidVersion";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A dependency constraint parse failure kind is not supported by the renderer."));
}

std::string repository_source_failure_reason_display(
        const RepositoryExactPackageSourceFailureReason& reason,
        std::size_t blocker_index, RenderState& state) {
    return std::visit(
            [&](const auto& detail) {
                using Detail = std::decay_t<decltype(detail)>;
                if constexpr(std::is_same_v<Detail, PackageMetadataFailure>) {
                    return localization::format_translated_message(
                            "{}; diagnostic: {}",
                            package_metadata_error_code_display(
                                    detail.code, blocker_index, state),
                            required_string_display(
                                    detail.diagnostic, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A repository source failure is missing its metadata diagnostic.")));
                } else {
                    return localization::format_translated_message(
                            "{}; specification: {}",
                            dependency_constraint_parse_failure_kind_display(
                                    detail.kind, blocker_index, state),
                            required_string_display(
                                    detail.raw_specification, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A repository source failure is missing its constraint specification.")));
                }
            },
            reason);
}

std::string local_source_root_stage_display(
        LocalSourceRootStage stage, std::size_t blocker_index,
        RenderState& state) {
    switch(stage) {
    case LocalSourceRootStage::InvocationAnchorOpen:
        return "LocalSourceRootStage::InvocationAnchorOpen";
    case LocalSourceRootStage::RootInspection:
        return "LocalSourceRootStage::RootInspection";
    case LocalSourceRootStage::RootOpen:
        return "LocalSourceRootStage::RootOpen";
    case LocalSourceRootStage::CanonicalPathResolution:
        return "LocalSourceRootStage::CanonicalPathResolution";
    case LocalSourceRootStage::RootRevalidation:
        return "LocalSourceRootStage::RootRevalidation";
    case LocalSourceRootStage::PkgbuildInspection:
        return "LocalSourceRootStage::PkgbuildInspection";
    case LocalSourceRootStage::PkgbuildOpen:
        return "LocalSourceRootStage::PkgbuildOpen";
    case LocalSourceRootStage::PkgbuildRead:
        return "LocalSourceRootStage::PkgbuildRead";
    case LocalSourceRootStage::PkgbuildRevalidation:
        return "LocalSourceRootStage::PkgbuildRevalidation";
    case LocalSourceRootStage::SrcinfoInspection:
        return "LocalSourceRootStage::SrcinfoInspection";
    case LocalSourceRootStage::SrcinfoOpen:
        return "LocalSourceRootStage::SrcinfoOpen";
    case LocalSourceRootStage::SrcinfoRead:
        return "LocalSourceRootStage::SrcinfoRead";
    case LocalSourceRootStage::SrcinfoRevalidation:
        return "LocalSourceRootStage::SrcinfoRevalidation";
    case LocalSourceRootStage::MetadataRevalidation:
        return "LocalSourceRootStage::MetadataRevalidation";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A local source failure stage is not supported by the renderer."));
}

std::string local_source_root_error_code_display(
        LocalSourceRootErrorCode code, std::size_t blocker_index,
        RenderState& state) {
    switch(code) {
    case LocalSourceRootErrorCode::InvalidInputPath:
        return "LocalSourceRootErrorCode::InvalidInputPath";
    case LocalSourceRootErrorCode::Missing:
        return "LocalSourceRootErrorCode::Missing";
    case LocalSourceRootErrorCode::Symlink:
        return "LocalSourceRootErrorCode::Symlink";
    case LocalSourceRootErrorCode::NotDirectory:
        return "LocalSourceRootErrorCode::NotDirectory";
    case LocalSourceRootErrorCode::NotRegularFile:
        return "LocalSourceRootErrorCode::NotRegularFile";
    case LocalSourceRootErrorCode::OwnershipMismatch:
        return "LocalSourceRootErrorCode::OwnershipMismatch";
    case LocalSourceRootErrorCode::UnsafePermissions:
        return "LocalSourceRootErrorCode::UnsafePermissions";
    case LocalSourceRootErrorCode::PermissionDenied:
        return "LocalSourceRootErrorCode::PermissionDenied";
    case LocalSourceRootErrorCode::MetadataFailure:
        return "LocalSourceRootErrorCode::MetadataFailure";
    case LocalSourceRootErrorCode::ReadFailure:
        return "LocalSourceRootErrorCode::ReadFailure";
    case LocalSourceRootErrorCode::ConcurrentReplacement:
        return "LocalSourceRootErrorCode::ConcurrentReplacement";
    case LocalSourceRootErrorCode::ContentChanged:
        return "LocalSourceRootErrorCode::ContentChanged";
    case LocalSourceRootErrorCode::UnsafeMetadata:
        return "LocalSourceRootErrorCode::UnsafeMetadata";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A local source failure code is not supported by the renderer."));
}

std::string root_target_identity_display(const RootTargetIdentity& root) {
    return localization::format_translated_message(
            "{} (invocation index: {})", root.requested_name,
            root.invocation_index);
}

std::string source_failure_detail_display(
        const SourceFailureUnifiedPlanBlocker& blocker,
        std::size_t blocker_index, RenderState& state) {
    return std::visit(
            [&](const auto& reference) -> std::string {
                using Reference = std::decay_t<decltype(reference)>;
                const auto& detail = reference.get();
                if constexpr(std::is_same_v<
                                     Reference,
                                     UnifiedPlanBorrowedAuthorityReference<
                                             BuildPlanResolutionFailure>>) {
                    return localization::format_translated_message(
                            "source authority failure ({}); subject: {}; parent: {}; {}: {}; dependency: {}; roots: {}; diagnostic: {}",
                            build_plan_resolution_failure_kind_display(
                                    detail.kind, blocker_index, state),
                            required_string_display(
                                    detail.subject, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "A {} resolution failure is missing its subject.",
                                            "BuildPlan")),
                            optional_string_display(
                                    detail.parent_package_name),
                            "PackageBase",
                            optional_string_display(
                                    detail.parent_package_base),
                            optional_string_display(
                                    detail.dependency_specification),
                            detail.roots.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values([&detail] {
                                          std::vector<std::string> roots;
                                          roots.reserve(detail.roots.size());
                                          for(const RootTargetIdentity& root :
                                              detail.roots) {
                                              roots.push_back(
                                                      root_target_identity_display(
                                                              root));
                                          }
                                          return roots;
                                      }()),
                            required_string_display(
                                    detail.diagnostic, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "A {} resolution failure is missing its diagnostic.",
                                            "BuildPlan")));
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    IncompleteProviderCandidateSet>>) {
                    std::vector<std::string> candidates;
                    candidates.reserve(detail.observed_candidates.size());
                    for(const ProvidedDependency& candidate :
                        detail.observed_candidates) {
                        candidates.push_back(provider_identity_display(
                                candidate, state,
                                UnifiedPlanRenderingSection::Blockers,
                                blocker_index, std::nullopt));
                    }
                    return localization::format_translated_message(
                            "source authority failure ({}); dependency: {}; observed candidates: {}; reason: {}",
                            "IncompleteProviderCandidateSet",
                            required_string_display(
                                    detail.dependency, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "An incomplete provider set is missing its dependency identity.")),
                            candidates.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values(candidates),
                            observed_version_unknown_reason_display(
                                    detail.reason, blocker_index, state));
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    RepositoryExactPackageSourceFailure>>) {
                    return localization::format_translated_message(
                            "source authority failure ({}); package: {}/{}; reason: {}",
                            "RepositoryExactPackageSourceFailure",
                            required_string_display(
                                    detail.repository.repository_name, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A repository package source failure is missing its repository identity.")),
                            required_string_display(
                                    detail.package_name, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A repository package source failure is missing its package identity.")),
                            repository_source_failure_reason_display(
                                    detail.reason, blocker_index, state));
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    RepositoryProviderSourceFailure>>) {
                    return localization::format_translated_message(
                            "source authority failure ({}); repository: {}; reason: {}",
                            "RepositoryProviderSourceFailure",
                            required_string_display(
                                    detail.repository.repository_name, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A repository provider source failure is missing its repository identity.")),
                            repository_source_failure_reason_display(
                                    detail.reason, blocker_index, state));
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    LocalSourceRootFailure>>) {
                    return localization::format_translated_message(
                            "source authority failure ({}); path: {}; stage: {}; code: {}; system error: {}",
                            "LocalSourceRootFailure",
                            required_string_display(
                                    detail.path.generic_string(), state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A local source failure is missing its path identity.")),
                            local_source_root_stage_display(
                                    detail.stage, blocker_index, state),
                            local_source_root_error_code_display(
                                    detail.code, blocker_index, state),
                            detail.system_error.has_value()
                                    ? localization::format_translated_message(
                                              "{} ({})",
                                              detail.system_error->value(),
                                              detail.system_error->message())
                                    : localization::translate_message(
                                              "not observed"));
                } else {
                    return localization::format_translated_message(
                            "source authority failure ({}); packages: {}; diagnostic: {}",
                            "AurUpdateQueryFailure",
                            detail.package_names.empty()
                                    ? unavailable_display(
                                              state,
                                              UnifiedPlanRenderingSection::
                                                      Blockers,
                                              blocker_index, std::nullopt,
                                              localization::format_translated_message(
                                                      "An {} query failure is missing its package identities.",
                                                      "AUR"))
                                    : join_display_values(
                                              detail.package_names),
                            required_string_display(
                                    detail.diagnostic, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "An {} query failure is missing its diagnostic.",
                                            "AUR")));
                }
            },
            blocker.detail);
}

std::string local_dependency_plan_failure_kind_display(
        LocalDependencyPlanFailureKind kind, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index) {
    switch(kind) {
    case LocalDependencyPlanFailureKind::UnsupportedArchitecture:
        return "LocalDependencyPlanFailureKind::UnsupportedArchitecture";
    case LocalDependencyPlanFailureKind::ConstraintMismatch:
        return "LocalDependencyPlanFailureKind::ConstraintMismatch";
    case LocalDependencyPlanFailureKind::AmbiguousLocalProvider:
        return "LocalDependencyPlanFailureKind::AmbiguousLocalProvider";
    case LocalDependencyPlanFailureKind::RemoteProviderIdentityConflict:
        return "LocalDependencyPlanFailureKind::RemoteProviderIdentityConflict";
    }
    return unsupported_display(
            state, section, item_index, detail_index,
            localization::translate_message(
                    "A local dependency plan failure kind is not supported by the renderer."));
}

std::string local_dependency_plan_failure_display(
        const LocalDependencyPlanFailure& failure, RenderState& state,
        UnifiedPlanRenderingSection section, std::size_t item_index,
        std::optional<std::size_t> detail_index) {
    std::vector<std::string> candidates;
    candidates.reserve(failure.candidates.size());
    for(const LocalDependencyPlanCandidate& candidate : failure.candidates) {
        const std::string remote_provider =
                candidate.remote_provider.has_value()
                ? provider_identity_display(
                          candidate.remote_provider.value(), state, section,
                          item_index, detail_index)
                : localization::translate_message("not observed");
        const std::string constraint =
                candidate.constraint_evaluation.has_value()
                ? constraint_evaluation_display(
                          candidate.constraint_evaluation.value(), state,
                          section, item_index, detail_index)
                : localization::translate_message("not observed");
        candidates.push_back(localization::format_translated_message(
                "{} (provided capability: {}; version: {}; remote provider: {}; stored constraint result: {})",
                required_string_display(
                        candidate.package_name, state, section, item_index,
                        detail_index,
                        localization::translate_message(
                                "A local dependency plan failure candidate is missing its package identity.")),
                optional_string_display(candidate.provided_specification),
                optional_string_display(candidate.version), remote_provider,
                constraint));
    }

    const bool requires_dependency =
            failure.kind !=
            LocalDependencyPlanFailureKind::UnsupportedArchitecture;
    const bool requires_architecture =
            failure.kind ==
            LocalDependencyPlanFailureKind::UnsupportedArchitecture;
    const bool requires_candidates =
            failure.kind !=
            LocalDependencyPlanFailureKind::UnsupportedArchitecture;
    std::string dependency = optional_string_display(
            failure.dependency_specification);
    if(requires_dependency &&
       (!failure.dependency_specification.has_value() ||
        failure.dependency_specification->empty())) {
        dependency = unavailable_display(
                state, section, item_index, detail_index,
                localization::translate_message(
                        "A local dependency plan failure is missing its dependency specification."));
    }
    std::string architecture = optional_string_display(
            failure.effective_architecture);
    if(requires_architecture &&
       (!failure.effective_architecture.has_value() ||
        failure.effective_architecture->empty())) {
        architecture = unavailable_display(
                state, section, item_index, detail_index,
                localization::translate_message(
                        "An unsupported-architecture local dependency failure is missing its architecture."));
    }
    std::string candidate_display =
            candidates.empty() ? localization::translate_message("None")
                               : join_display_values(candidates);
    if(requires_candidates && candidates.empty()) {
        candidate_display = unavailable_display(
                state, section, item_index, detail_index,
                localization::translate_message(
                        "A local dependency plan failure is missing its candidate identities."));
    }
    return localization::format_translated_message(
            "local dependency plan failure ({}); parent: {}; dependency: {}; architecture: {}; candidates: {}",
            local_dependency_plan_failure_kind_display(
                    failure.kind, state, section, item_index, detail_index),
            required_string_display(
                    failure.parent_package_name, state, section, item_index,
                    detail_index,
                    localization::translate_message(
                            "A local dependency plan failure is missing its parent package identity.")),
            dependency, architecture, candidate_display);
}

std::string build_plan_state_blocker_display(
        const BuildPlanStateUnifiedPlanBlocker& blocker,
        std::size_t blocker_index, RenderState& state) {
    const BuildPlan& plan = blocker.authority.get();
    switch(blocker.kind) {
    case BuildPlanStateUnifiedPlanBlockerKind::UnresolvedDependency:
        if(blocker.authority_index < plan.unresolved.size()) {
            return localization::format_translated_message(
                    "{} state blocker ({}): unresolved dependency: {}",
                    "BuildPlan",
                    "BuildPlanStateUnifiedPlanBlockerKind::UnresolvedDependency",
                    plan.unresolved[blocker.authority_index]);
        }
        break;
    case BuildPlanStateUnifiedPlanBlockerKind::DependencyCycle:
        if(blocker.authority_index < plan.cycles.size()) {
            return localization::format_translated_message(
                    "{} state blocker ({}): dependency cycle: {}",
                    "BuildPlan",
                    "BuildPlanStateUnifiedPlanBlockerKind::DependencyCycle",
                    plan.cycles[blocker.authority_index]);
        }
        break;
    case BuildPlanStateUnifiedPlanBlockerKind::SplitPackageSelectionRequired:
        if(blocker.authority_index < plan.split_package_targets.size()) {
            const BuildPlanSplitPackageTarget& target =
                    plan.split_package_targets[blocker.authority_index];
            return localization::format_translated_message(
                    "{} state blocker ({}): split package selection required: {} ({}: {})",
                    "BuildPlan",
                    "BuildPlanStateUnifiedPlanBlockerKind::SplitPackageSelectionRequired",
                    target.package_name, "PackageBase",
                    target.package_base);
        }
        break;
    default:
        return unsupported_display(
                state, UnifiedPlanRenderingSection::Blockers, blocker_index,
                std::nullopt,
                localization::format_translated_message(
                        "A {} blocker kind is not supported by the renderer.",
                        "BuildPlan"));
    }
    return unavailable_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            blocker.authority_index,
            localization::format_translated_message(
                    "A {} blocker references an unavailable authority item.",
                    "BuildPlan"));
}

std::string root_package_preparation_issue_kind_display(
        RootPackageInstallPreparationIssueKind kind,
        std::size_t blocker_index, std::optional<std::size_t> detail_index,
        RenderState& state) {
    switch(kind) {
    case RootPackageInstallPreparationIssueKind::EmptyQuery:
        return "RootPackageInstallPreparationIssueKind::EmptyQuery";
    case RootPackageInstallPreparationIssueKind::RemoveDependenciesUnsupported:
        return "RootPackageInstallPreparationIssueKind::RemoveDependenciesUnsupported";
    case RootPackageInstallPreparationIssueKind::InputGateUnavailable:
        return "RootPackageInstallPreparationIssueKind::InputGateUnavailable";
    case RootPackageInstallPreparationIssueKind::SelectionUnavailable:
        return "RootPackageInstallPreparationIssueKind::SelectionUnavailable";
    case RootPackageInstallPreparationIssueKind::SelectionCancelled:
        return "RootPackageInstallPreparationIssueKind::SelectionCancelled";
    case RootPackageInstallPreparationIssueKind::SourceOptionsWithoutAurTarget:
        return "RootPackageInstallPreparationIssueKind::SourceOptionsWithoutAurTarget";
    case RootPackageInstallPreparationIssueKind::BuildPlanPreparationFailed:
        return "RootPackageInstallPreparationIssueKind::BuildPlanPreparationFailed";
    case RootPackageInstallPreparationIssueKind::SourceWorkPreparationFailed:
        return "RootPackageInstallPreparationIssueKind::SourceWorkPreparationFailed";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            detail_index,
            localization::translate_message(
                    "A root package preparation issue kind is not supported by the renderer."));
}

std::string root_selection_input_gate_display(
        RootPackageSelectionInputGate gate, std::size_t blocker_index,
        std::optional<std::size_t> detail_index, RenderState& state) {
    switch(gate) {
    case RootPackageSelectionInputGate::Interactive:
        return "RootPackageSelectionInputGate::Interactive";
    case RootPackageSelectionInputGate::NonTty:
        return "RootPackageSelectionInputGate::NonTty";
    case RootPackageSelectionInputGate::NoConfirm:
        return "RootPackageSelectionInputGate::NoConfirm";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            detail_index,
            localization::translate_message(
                    "A root package selection input gate is not supported by the renderer."));
}

std::string root_selection_unavailable_reason_display(
        RootPackageSelectionUnavailableReason reason,
        std::size_t blocker_index, std::optional<std::size_t> detail_index,
        RenderState& state) {
    switch(reason) {
    case RootPackageSelectionUnavailableReason::NonInteractiveInput:
        return "RootPackageSelectionUnavailableReason::NonInteractiveInput";
    case RootPackageSelectionUnavailableReason::NoConfirm:
        return "RootPackageSelectionUnavailableReason::NoConfirm";
    case RootPackageSelectionUnavailableReason::NoCandidates:
        return "RootPackageSelectionUnavailableReason::NoCandidates";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            detail_index,
            localization::translate_message(
                    "A root package selection unavailable reason is not supported by the renderer."));
}

std::string root_selection_cancellation_reason_display(
        RootPackageSelectionCancellationReason reason,
        std::size_t blocker_index, std::optional<std::size_t> detail_index,
        RenderState& state) {
    switch(reason) {
    case RootPackageSelectionCancellationReason::EmptyInput:
        return "RootPackageSelectionCancellationReason::EmptyInput";
    case RootPackageSelectionCancellationReason::CancelToken:
        return "RootPackageSelectionCancellationReason::CancelToken";
    case RootPackageSelectionCancellationReason::EndOfInput:
        return "RootPackageSelectionCancellationReason::EndOfInput";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            detail_index,
            localization::translate_message(
                    "A root package selection cancellation reason is not supported by the renderer."));
}

std::string root_package_preparation_detail_display(
        const RootPackageInstallPreparationFailureDetail& detail,
        std::size_t blocker_index, std::size_t detail_index,
        RenderState& state) {
    return std::visit(
            [&](const auto& typed_detail) -> std::string {
                using Detail = std::decay_t<decltype(typed_detail)>;
                if constexpr(std::is_same_v<
                                     Detail,
                                     RootPackageInstallPreparationIssue>) {
                    return localization::format_translated_message(
                            "{}; input gate: {}; unavailable reason: {}; cancellation reason: {}; diagnostic: {}",
                            root_package_preparation_issue_kind_display(
                                    typed_detail.kind, blocker_index,
                                    detail_index, state),
                            typed_detail.input_gate.has_value()
                                    ? root_selection_input_gate_display(
                                              typed_detail.input_gate.value(),
                                              blocker_index, detail_index,
                                              state)
                                    : localization::translate_message(
                                              "not observed"),
                            typed_detail.selection_unavailable_reason
                                            .has_value()
                                    ? root_selection_unavailable_reason_display(
                                              typed_detail
                                                      .selection_unavailable_reason
                                                      .value(),
                                              blocker_index, detail_index,
                                              state)
                                    : localization::translate_message(
                                              "not observed"),
                            typed_detail.selection_cancellation_reason
                                            .has_value()
                                    ? root_selection_cancellation_reason_display(
                                              typed_detail
                                                      .selection_cancellation_reason
                                                      .value(),
                                              blocker_index, detail_index,
                                              state)
                                    : localization::translate_message(
                                              "not observed"),
                            typed_detail.diagnostic.empty()
                                    ? localization::translate_message(
                                              "not observed")
                                    : typed_detail.diagnostic);
                } else if constexpr(std::is_same_v<
                                            Detail,
                                            RepositoryRootPackageSearchFailure>) {
                    return localization::format_translated_message(
                            "{}; code: {}; diagnostic: {}",
                            "RepositoryRootPackageSearchFailure",
                            package_metadata_error_code_display(
                                    typed_detail.failure.code,
                                    blocker_index, state),
                            required_string_display(
                                    typed_detail.failure.diagnostic, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, detail_index,
                                    localization::translate_message(
                                            "A repository root search failure is missing its diagnostic.")));
                } else if constexpr(std::is_same_v<
                                            Detail,
                                            AurRootPackageSearchFailure>) {
                    return localization::format_translated_message(
                            "{}; diagnostic: {}",
                            "AurRootPackageSearchFailure",
                            required_string_display(
                                    typed_detail.diagnostic, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, detail_index,
                                    localization::format_translated_message(
                                            "An {} root search failure is missing its diagnostic.",
                                            "AUR")));
                } else if constexpr(std::is_same_v<
                                            Detail,
                                            InvalidRootPackageSearchSnapshot>) {
                    return localization::format_translated_message(
                            "{}; candidate validation failures: {}; candidate pair issues: {}; invalid group matches: {}; duplicate repository-order entries: {}; unranked repository candidates: {}",
                            "InvalidRootPackageSearchSnapshot",
                            typed_detail.validation_failures.size(),
                            typed_detail.candidate_pair_issues.size(),
                            typed_detail.invalid_group_matches.size(),
                            typed_detail
                                    .duplicate_repository_order_entries.size(),
                            typed_detail
                                    .unranked_repository_candidates.size());
                } else {
                    std::vector<std::string> targets;
                    targets.reserve(
                            typed_detail
                                    .unrepresentable_repository_targets
                                    .size());
                    for(const auto& target :
                        typed_detail.unrepresentable_repository_targets) {
                        targets.push_back(
                                localization::format_translated_message(
                                        "selection index {}: {}/{}",
                                        target.selection_index,
                                        target.identity.repository_name,
                                        target.identity.package_name));
                    }
                    return localization::format_translated_message(
                            "{}; targets: {}",
                            "InvalidRootPackageRoutingProjection",
                            targets.empty()
                                    ? unavailable_display(
                                              state,
                                              UnifiedPlanRenderingSection::
                                                      Blockers,
                                              blocker_index, detail_index,
                                              localization::translate_message(
                                                      "An invalid root routing projection is missing its affected target identities."))
                                    : join_display_values(targets));
                }
            },
            detail);
}

std::string root_package_preparation_failure_display(
        const RootPackageInstallPreparationFailure& failure,
        std::size_t blocker_index, RenderState& state) {
    if(failure.details.empty()) {
        return localization::format_translated_message(
                "root package preparation failure: {}",
                unavailable_display(
                        state, UnifiedPlanRenderingSection::Blockers,
                        blocker_index, std::nullopt,
                        localization::translate_message(
                                "A root package preparation failure has no typed details.")));
    }
    std::vector<std::string> details;
    details.reserve(failure.details.size());
    for(std::size_t detail_index = 0;
        detail_index < failure.details.size(); ++detail_index) {
        details.push_back(root_package_preparation_detail_display(
                failure.details[detail_index], blocker_index, detail_index,
                state));
    }
    return localization::format_translated_message(
            "root package preparation failure: {}",
            join_display_values(details));
}

std::string build_plan_artifact_projection_issue_kind_display(
        BuildPlanArtifactTargetProjectionIssueKind kind,
        std::size_t blocker_index, RenderState& state) {
    switch(kind) {
    case BuildPlanArtifactTargetProjectionIssueKind::InvalidPackageBase:
        return "BuildPlanArtifactTargetProjectionIssueKind::InvalidPackageBase";
    case BuildPlanArtifactTargetProjectionIssueKind::EmptyEntryPackageNames:
        return "BuildPlanArtifactTargetProjectionIssueKind::EmptyEntryPackageNames";
    case BuildPlanArtifactTargetProjectionIssueKind::InvalidPackageName:
        return "BuildPlanArtifactTargetProjectionIssueKind::InvalidPackageName";
    case BuildPlanArtifactTargetProjectionIssueKind::DuplicateEntryPackageName:
        return "BuildPlanArtifactTargetProjectionIssueKind::DuplicateEntryPackageName";
    case BuildPlanArtifactTargetProjectionIssueKind::MissingPlannedPackageTarget:
        return "BuildPlanArtifactTargetProjectionIssueKind::MissingPlannedPackageTarget";
    case BuildPlanArtifactTargetProjectionIssueKind::DuplicatePlannedPackageTarget:
        return "BuildPlanArtifactTargetProjectionIssueKind::DuplicatePlannedPackageTarget";
    case BuildPlanArtifactTargetProjectionIssueKind::PackageBaseMismatch:
        return "BuildPlanArtifactTargetProjectionIssueKind::PackageBaseMismatch";
    case BuildPlanArtifactTargetProjectionIssueKind::UncoveredPlannedPackageTarget:
        return "BuildPlanArtifactTargetProjectionIssueKind::UncoveredPlannedPackageTarget";
    case BuildPlanArtifactTargetProjectionIssueKind::DesiredInstallReasonUnavailable:
        return "BuildPlanArtifactTargetProjectionIssueKind::DesiredInstallReasonUnavailable";
    case BuildPlanArtifactTargetProjectionIssueKind::RootAttributionInconsistent:
        return "BuildPlanArtifactTargetProjectionIssueKind::RootAttributionInconsistent";
    case BuildPlanArtifactTargetProjectionIssueKind::DuplicatePackageBaseEntry:
        return "BuildPlanArtifactTargetProjectionIssueKind::DuplicatePackageBaseEntry";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A required artifact projection issue kind is not supported by the renderer."));
}

std::string build_plan_artifact_projection_issue_display(
        const BuildPlanArtifactTargetProjectionIssue& issue,
        std::size_t blocker_index, RenderState& state) {
    std::vector<std::string> roots;
    roots.reserve(issue.roots.size());
    for(const RootTargetIdentity& root : issue.roots) {
        roots.push_back(root_target_identity_display(root));
    }
    std::vector<std::string> package_target_indices;
    package_target_indices.reserve(issue.package_target_indices.size());
    for(const std::size_t index : issue.package_target_indices) {
        package_target_indices.push_back(std::to_string(index));
    }
    return localization::format_translated_message(
            "required artifact projection failure ({}); {} unit index: {}; entry package index: {}; package target indices: {}; {}: {}; package: {}; roots: {}; diagnostic: {}",
            build_plan_artifact_projection_issue_kind_display(
                    issue.kind, blocker_index, state),
            "BuildPlan",
            optional_index_display(issue.build_plan_order_index),
            optional_index_display(issue.entry_package_name_index),
            package_target_indices.empty()
                    ? localization::translate_message("None")
                    : join_display_values(package_target_indices),
            "PackageBase", optional_string_display(issue.package_base),
            optional_string_display(issue.package_name),
            roots.empty() ? localization::translate_message("None")
                          : join_display_values(roots),
            issue.diagnostic.empty()
                    ? localization::translate_message("not observed")
                    : issue.diagnostic);
}

std::string aur_update_execution_reason_display(
        AurUpdateExecutionReason reason, std::size_t blocker_index,
        RenderState& state) {
    switch(reason) {
    case AurUpdateExecutionReason::None:
        state.add_issue(
                UnifiedPlanRenderingIssueKind::MissingReferencedValue,
                UnifiedPlanRenderingSection::Blockers, blocker_index,
                std::nullopt,
                localization::format_translated_message(
                        "An {} route preflight blocker has no failure reason.",
                        "AUR"));
        return "AurUpdateExecutionReason::None";
    case AurUpdateExecutionReason::UpToDate:
        return "AurUpdateExecutionReason::UpToDate";
    case AurUpdateExecutionReason::NonAurForeign:
        return "AurUpdateExecutionReason::NonAurForeign";
    case AurUpdateExecutionReason::AurMetadataUnavailable:
        return "AurUpdateExecutionReason::AurMetadataUnavailable";
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
        return "AurUpdateExecutionReason::VersionComparisonUnavailable";
    case AurUpdateExecutionReason::InstalledReasonUnknown:
        return "AurUpdateExecutionReason::InstalledReasonUnknown";
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
        return "AurUpdateExecutionReason::UpdatePlanInconsistent";
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
        return "AurUpdateExecutionReason::DuplicateUpdateTarget";
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
        return "AurUpdateExecutionReason::RepositoryMetadataUnavailable";
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
        return "AurUpdateExecutionReason::AurDependencyMetadataUnavailable";
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
        return "AurUpdateExecutionReason::ProviderMetadataUnavailable";
    case AurUpdateExecutionReason::UnresolvedDependency:
        return "AurUpdateExecutionReason::UnresolvedDependency";
    case AurUpdateExecutionReason::VersionConstraintUnverified:
        return "AurUpdateExecutionReason::VersionConstraintUnverified";
    case AurUpdateExecutionReason::DependencyCycle:
        return "AurUpdateExecutionReason::DependencyCycle";
    case AurUpdateExecutionReason::BuildPlanInconsistent:
        return "AurUpdateExecutionReason::BuildPlanInconsistent";
    case AurUpdateExecutionReason::PackageBaseMismatch:
        return "AurUpdateExecutionReason::PackageBaseMismatch";
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
        return "AurUpdateExecutionReason::SplitPackageSelectionRequired";
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
        return "AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase";
    case AurUpdateExecutionReason::AmbiguousProvider:
        return "AurUpdateExecutionReason::AmbiguousProvider";
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return "AurUpdateExecutionReason::ConflictsOrReplacesUnresolved";
    case AurUpdateExecutionReason::InstalledPackageMetadataUnavailable:
        return "AurUpdateExecutionReason::InstalledPackageMetadataUnavailable";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::format_translated_message(
                    "An {} execution preflight reason is not supported by the renderer.",
                    "AUR"));
}

std::string system_source_issue_kind_display(
        SystemSourceUpgradeIssueKind kind, std::size_t blocker_index,
        RenderState& state) {
    switch(kind) {
    case SystemSourceUpgradeIssueKind::PreferenceEnumerationUnavailable:
        return "SystemSourceUpgradeIssueKind::PreferenceEnumerationUnavailable";
    case SystemSourceUpgradeIssueKind::PreferenceUnavailable:
        return "SystemSourceUpgradeIssueKind::PreferenceUnavailable";
    case SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed:
        return "SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed";
    case SystemSourceUpgradeIssueKind::SourceWorkItemPreparationFailed:
        return "SystemSourceUpgradeIssueKind::SourceWorkItemPreparationFailed";
    case SystemSourceUpgradeIssueKind::SourceInvocationPreparationFailed:
        return "SystemSourceUpgradeIssueKind::SourceInvocationPreparationFailed";
    case SystemSourceUpgradeIssueKind::SourceBaselineSnapshotUnavailable:
        return "SystemSourceUpgradeIssueKind::SourceBaselineSnapshotUnavailable";
    case SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable:
        return "SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable";
    case SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable:
        return "SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable";
    case SystemSourceUpgradeIssueKind::CacheAuthorityInvalid:
        return "SystemSourceUpgradeIssueKind::CacheAuthorityInvalid";
    case SystemSourceUpgradeIssueKind::InvalidPreferenceName:
        return "SystemSourceUpgradeIssueKind::InvalidPreferenceName";
    case SystemSourceUpgradeIssueKind::OptionSnapshotMismatch:
        return "SystemSourceUpgradeIssueKind::OptionSnapshotMismatch";
    case SystemSourceUpgradeIssueKind::PreparedCorrelationInconsistent:
        return "SystemSourceUpgradeIssueKind::PreparedCorrelationInconsistent";
    case SystemSourceUpgradeIssueKind::PreparedCapabilityConsumed:
        return "SystemSourceUpgradeIssueKind::PreparedCapabilityConsumed";
    case SystemSourceUpgradeIssueKind::UnknownPreparationFailure:
        return "SystemSourceUpgradeIssueKind::UnknownPreparationFailure";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A system/source upgrade issue kind is not supported by the renderer."));
}

std::string system_source_issue_impact_display(
        SystemSourceUpgradeIssueImpact impact, std::size_t blocker_index,
        RenderState& state) {
    switch(impact) {
    case SystemSourceUpgradeIssueImpact::ObservabilityOnly:
        return "SystemSourceUpgradeIssueImpact::ObservabilityOnly";
    case SystemSourceUpgradeIssueImpact::AffectsSuccess:
        return "SystemSourceUpgradeIssueImpact::AffectsSuccess";
    case SystemSourceUpgradeIssueImpact::BlocksExecution:
        return "SystemSourceUpgradeIssueImpact::BlocksExecution";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "A system/source upgrade issue impact is not supported by the renderer."));
}

std::string upgrade_all_issue_kind_display(
        UpgradeAllOperationIssueKind kind, std::size_t blocker_index,
        RenderState& state) {
    switch(kind) {
    case UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid:
        return "UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid";
    case UpgradeAllOperationIssueKind::OptionSnapshotMismatch:
        return "UpgradeAllOperationIssueKind::OptionSnapshotMismatch";
    case UpgradeAllOperationIssueKind::SourceSnapshotMismatch:
        return "UpgradeAllOperationIssueKind::SourceSnapshotMismatch";
    case UpgradeAllOperationIssueKind::ExplicitSourceCorrelationInconsistent:
        return "UpgradeAllOperationIssueKind::ExplicitSourceCorrelationInconsistent";
    case UpgradeAllOperationIssueKind::PreparedCapabilityConsumed:
        return "UpgradeAllOperationIssueKind::PreparedCapabilityConsumed";
    case UpgradeAllOperationIssueKind::SystemSourceExecutionFailedUnexpectedly:
        return "UpgradeAllOperationIssueKind::SystemSourceExecutionFailedUnexpectedly";
    case UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete:
        return "UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete";
    case UpgradeAllOperationIssueKind::ForeignInventoryConfigurationFailed:
        return "UpgradeAllOperationIssueKind::ForeignInventoryConfigurationFailed";
    case UpgradeAllOperationIssueKind::ForeignInventoryReadFailed:
        return "UpgradeAllOperationIssueKind::ForeignInventoryReadFailed";
    case UpgradeAllOperationIssueKind::CacheAuthorityInvalid:
        return "UpgradeAllOperationIssueKind::CacheAuthorityInvalid";
    case UpgradeAllOperationIssueKind::AurQueryFailed:
        return "UpgradeAllOperationIssueKind::AurQueryFailed";
    case UpgradeAllOperationIssueKind::FilteredAurPreparationFailed:
        return "UpgradeAllOperationIssueKind::FilteredAurPreparationFailed";
    case UpgradeAllOperationIssueKind::FilteredAurExecutionFailed:
        return "UpgradeAllOperationIssueKind::FilteredAurExecutionFailed";
    case UpgradeAllOperationIssueKind::DuplicateExclusionCorrelationInconsistent:
        return "UpgradeAllOperationIssueKind::DuplicateExclusionCorrelationInconsistent";
    case UpgradeAllOperationIssueKind::ExternalSatisfactionCorrelationInconsistent:
        return "UpgradeAllOperationIssueKind::ExternalSatisfactionCorrelationInconsistent";
    case UpgradeAllOperationIssueKind::UnknownFailure:
        return "UpgradeAllOperationIssueKind::UnknownFailure";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::format_translated_message(
                    "An {} issue kind is not supported by the renderer.",
                    "upgrade-all"));
}

std::string route_preflight_blocker_display(
        const RoutePreflightUnifiedPlanBlocker& blocker,
        std::size_t blocker_index, RenderState& state) {
    return std::visit(
            [&](const auto& reference) -> std::string {
                using Reference = std::decay_t<decltype(reference)>;
                const auto& detail = reference.get();
                if constexpr(std::is_same_v<
                                     Reference,
                                     UnifiedPlanBorrowedAuthorityReference<
                                             AurUpdateExecutionIssue>>) {
                    if(!detail.package_name.has_value() &&
                       !detail.package_base.has_value() &&
                       !detail.dependency_specification.has_value() &&
                       detail.diagnostic.empty() &&
                       !detail.build_plan_projection_issue.has_value()) {
                        state.add_issue(
                                UnifiedPlanRenderingIssueKind::
                                        MissingReferencedValue,
                                UnifiedPlanRenderingSection::Blockers,
                                blocker_index, std::nullopt,
                                localization::format_translated_message(
                                        "An {} route preflight blocker has no affected identity or diagnostic detail.",
                                        "AUR"));
                    }
                    return localization::format_translated_message(
                            "{} route preflight failure ({}); package: {}; {}: {}; dependency: {}; artifact projection: {}; diagnostic: {}",
                            "AUR",
                            aur_update_execution_reason_display(
                                    detail.reason, blocker_index, state),
                            optional_string_display(detail.package_name),
                            "PackageBase",
                            optional_string_display(detail.package_base),
                            optional_string_display(
                                    detail.dependency_specification),
                            detail.build_plan_projection_issue.has_value()
                                    ? build_plan_artifact_projection_issue_display(
                                              detail
                                                      .build_plan_projection_issue
                                                      .value(),
                                              blocker_index, state)
                                    : localization::translate_message(
                                              "not observed"),
                            detail.diagnostic.empty()
                                    ? localization::translate_message(
                                              "not observed")
                                    : detail.diagnostic);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    SystemSourceUpgradeIssue>>) {
                    std::vector<std::string> nested_details;
                    if(detail.source_preference_failure.has_value()) {
                        nested_details.push_back("SourcePreferenceFailure");
                    }
                    if(detail.package_metadata_failure.has_value()) {
                        nested_details.push_back("PackageMetadataFailure");
                    }
                    if(detail.cache_resolution_failure.has_value()) {
                        nested_details.push_back(
                                "xdg_paths::ResolutionFailure");
                    }
                    if(detail.cache_preparation_failure.has_value()) {
                        nested_details.push_back(
                                "xdg_directory_safety::PreparationFailure");
                    }
                    if(detail.trusted_cache_failure.has_value()) {
                        nested_details.push_back("TrustedCacheFailure");
                    }
                    return localization::format_translated_message(
                            "system/source route preflight failure ({}); impact: {}; phase: {}; preference index: {}; package: {}; {}: {}; typed nested details: {}; diagnostic: {}",
                            system_source_issue_kind_display(
                                    detail.kind, blocker_index, state),
                            system_source_issue_impact_display(
                                    detail.impact, blocker_index, state),
                            system_source_phase_display(
                                    detail.phase, state, blocker_index),
                            optional_index_display(
                                    detail.original_preference_index),
                            optional_string_display(
                                    detail.preference_package_name),
                            "PackageBase",
                            optional_string_display(
                                    detail.resolved_package_base),
                            nested_details.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values(nested_details),
                            detail.diagnostic.empty()
                                    ? localization::translate_message(
                                              "not observed")
                                    : detail.diagnostic);
                } else {
                    std::vector<std::string> nested_details;
                    if(detail.package_metadata_failure.has_value()) {
                        nested_details.push_back("PackageMetadataFailure");
                    }
                    if(detail.cache_resolution_failure.has_value()) {
                        nested_details.push_back(
                                "xdg_paths::ResolutionFailure");
                    }
                    if(detail.cache_preparation_failure.has_value()) {
                        nested_details.push_back(
                                "xdg_directory_safety::PreparationFailure");
                    }
                    if(detail.trusted_cache_failure.has_value()) {
                        nested_details.push_back("TrustedCacheFailure");
                    }
                    return localization::format_translated_message(
                            "{} route preflight failure ({}); phase: {}; adapter index: {}; preference index: {}; query plan index: {}; {} unit index: {}; package: {}; typed nested details: {}; diagnostic: {}",
                            "upgrade-all",
                            upgrade_all_issue_kind_display(
                                    detail.kind, blocker_index, state),
                            upgrade_all_phase_display(
                                    detail.phase, state, blocker_index),
                            optional_index_display(detail.adapter_index),
                            optional_index_display(
                                    detail.original_preference_index),
                            optional_index_display(
                                    detail.original_query_plan_index),
                            "BuildPlan",
                            optional_index_display(
                                    detail.build_plan_order_index),
                            optional_string_display(detail.package_name),
                            nested_details.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values(nested_details),
                            detail.diagnostic.empty()
                                    ? localization::translate_message(
                                              "not observed")
                                    : detail.diagnostic);
                }
            },
            blocker.detail);
}

std::string installed_version_state_display(
        InstalledVersionState status, std::size_t blocker_index,
        RenderState& state) {
    switch(status) {
    case InstalledVersionState::NotInstalled:
        return "InstalledVersionState::NotInstalled";
    case InstalledVersionState::SameVersion:
        return "InstalledVersionState::SameVersion";
    case InstalledVersionState::DifferentVersion:
        return "InstalledVersionState::DifferentVersion";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "An installed version state is not supported by the renderer."));
}

std::string existing_install_reason_display(
        ExistingInstallReason reason, std::size_t blocker_index,
        RenderState& state) {
    switch(reason) {
    case ExistingInstallReason::Explicit:
        return "ExistingInstallReason::Explicit";
    case ExistingInstallReason::Dependency:
        return "ExistingInstallReason::Dependency";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "An existing install reason is not supported by the renderer."));
}

std::string install_reason_directive_display(
        InstallReasonDirective directive, std::size_t blocker_index,
        RenderState& state) {
    switch(directive) {
    case InstallReasonDirective::Default:
        return "InstallReasonDirective::Default";
    case InstallReasonDirective::AsExplicit:
        return "InstallReasonDirective::AsExplicit";
    case InstallReasonDirective::AsDependency:
        return "InstallReasonDirective::AsDependency";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::Blockers, blocker_index,
            std::nullopt,
            localization::translate_message(
                    "An install reason directive is not supported by the renderer."));
}

std::string blocker_display(
        const UnifiedPlanBlocker& blocker, std::size_t blocker_index,
        RenderState& state) {
    return std::visit(
            [&](const auto& typed_blocker) -> std::string {
                using Blocker = std::decay_t<decltype(typed_blocker)>;
                if constexpr(std::is_same_v<Blocker, UnknownUnifiedPlanBlocker>) {
                    const BuildPlanDependencyEdge& edge =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "unknown dependency blocker ({}); dependency: {}; parent: {} ({}: {}); role: {}",
                            "UnknownUnifiedPlanBlocker",
                            required_string_display(
                                    edge.dependency_spec, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "An unknown dependency blocker is missing its dependency specification.")),
                            required_string_display(
                                    edge.parent_package_name, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "An unknown dependency blocker is missing its parent package identity.")),
                            "PackageBase",
                            required_string_display(
                                    edge.parent_package_base, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "An unknown dependency blocker is missing its parent {} identity.",
                                            "PackageBase")),
                            package_role_display(
                                    edge.role, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            AmbiguousUnifiedPlanBlocker>) {
                    const AmbiguousProvidedDependency& ambiguous =
                            typed_blocker.detail.get();
                    std::vector<std::string> candidates;
                    candidates.reserve(ambiguous.candidates.size());
                    for(const ProvidedDependency& candidate :
                        ambiguous.candidates) {
                        candidates.push_back(provider_identity_display(
                                candidate, state,
                                UnifiedPlanRenderingSection::Blockers,
                                blocker_index, std::nullopt));
                    }
                    return localization::format_translated_message(
                            "ambiguous provider blocker ({}); dependency: {}; candidates: {}",
                            "AmbiguousUnifiedPlanBlocker",
                            required_string_display(
                                    ambiguous.dependency, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "An ambiguous provider blocker is missing its dependency identity.")),
                            candidates.empty()
                                    ? unavailable_display(
                                              state,
                                              UnifiedPlanRenderingSection::
                                                      Blockers,
                                              blocker_index, std::nullopt,
                                              localization::translate_message(
                                                      "An ambiguous provider blocker is missing its candidate identities."))
                                    : join_display_values(candidates));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            UnsupportedUnifiedPlanBlocker>) {
                    const MixedPackageBaseInstallReasonUnsupported& detail =
                            typed_blocker.detail.get();
                    std::vector<std::string> artifacts;
                    artifacts.reserve(detail.selected_artifacts.size());
                    for(const MixedPackageBaseInstallReasonArtifact& artifact :
                        detail.selected_artifacts) {
                        artifacts.push_back(
                                localization::format_translated_message(
                                        "{} {} (desired reason: {}; installed version state: {}; existing reason: {}; directive: {})",
                                        artifact.identity.package_name,
                                        artifact.identity.full_version,
                                        install_reason_display(
                                                artifact.desired_reason,
                                                state,
                                                UnifiedPlanRenderingSection::
                                                        Blockers,
                                                blocker_index),
                                        installed_version_state_display(
                                                artifact.installed_version_state,
                                                blocker_index, state),
                                        artifact.existing_reason.has_value()
                                                ? existing_install_reason_display(
                                                          artifact
                                                                  .existing_reason
                                                                  .value(),
                                                          blocker_index,
                                                          state)
                                                : localization::
                                                          translate_message(
                                                                  "not observed"),
                                        install_reason_directive_display(
                                                artifact.directive,
                                                blocker_index, state)));
                    }
                    return localization::format_translated_message(
                            "unsupported blocker ({}); mixed install reason for {} {}; artifacts: {}",
                            "MixedPackageBaseInstallReasonUnsupported",
                            "PackageBase",
                            required_string_display(
                                    detail.package_base, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "A mixed install-reason blocker is missing its {} identity.",
                                            "PackageBase")),
                            artifacts.empty()
                                    ? unavailable_display(
                                              state,
                                              UnifiedPlanRenderingSection::
                                                      Blockers,
                                              blocker_index, std::nullopt,
                                              localization::translate_message(
                                                      "A mixed install-reason blocker is missing its artifact details."))
                                    : join_display_values(artifacts));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            SourceFailureUnifiedPlanBlocker>) {
                    return source_failure_detail_display(
                            typed_blocker, blocker_index, state);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            ConstraintFailureUnifiedPlanBlocker>) {
                    const ConstraintEvaluation& evaluation =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "constraint failure blocker ({}); stored result: {}",
                            "ConstraintFailureUnifiedPlanBlocker",
                            constraint_evaluation_display(
                                    evaluation, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            MetadataRiskUnifiedPlanBlocker>) {
                    const BuildPlanMetadataRisk& risk =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "metadata risk blocker ({}); package: {} ({}: {}); conflicts: {}; replaces: {}",
                            "MetadataRiskUnifiedPlanBlocker",
                            required_string_display(
                                    risk.package_name, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::translate_message(
                                            "A metadata risk blocker is missing its package identity.")),
                            "PackageBase",
                            required_string_display(
                                    risk.package_base, state,
                                    UnifiedPlanRenderingSection::Blockers,
                                    blocker_index, std::nullopt,
                                    localization::format_translated_message(
                                            "A metadata risk blocker is missing its {} identity.",
                                            "PackageBase")),
                            risk.conflicts.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values(risk.conflicts),
                            risk.replaces.empty()
                                    ? localization::translate_message("None")
                                    : join_display_values(risk.replaces));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            LocalDependencyPlanUnifiedPlanBlocker>) {
                    const LocalDependencyPlanFailure& failure =
                            typed_blocker.detail.get();
                    return local_dependency_plan_failure_display(
                            failure, state,
                            UnifiedPlanRenderingSection::Blockers,
                            blocker_index, std::nullopt);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            RootPackagePreparationUnifiedPlanBlocker>) {
                    const RootPackageInstallPreparationFailure& failure =
                            typed_blocker.detail.get();
                    return root_package_preparation_failure_display(
                            failure, blocker_index, state);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            BuildPlanArtifactProjectionUnifiedPlanBlocker>) {
                    return build_plan_artifact_projection_issue_display(
                            typed_blocker.detail, blocker_index, state);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            BuildPlanStateUnifiedPlanBlocker>) {
                    return build_plan_state_blocker_display(
                            typed_blocker, blocker_index, state);
                } else {
                    return route_preflight_blocker_display(
                            typed_blocker, blocker_index, state);
                }
            },
            blocker);
}

void render_blockers(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message("Blockers:") << '\n';
    if(observation.blockers().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0; index < observation.blockers().size();
        ++index) {
        state.output << localization::format_translated_message(
                                "  {}. {}", index + 1,
                                blocker_display(
                                        observation.blockers()[index], index,
                                        state))
                     << '\n';
    }
}

} // namespace

UnifiedPlanRenderingResult render_unified_plan_observation(
        const UnifiedPlanObservation& observation) {
    RenderState state;
    state.output << localization::translate_message("Unified plan:") << '\n';
    state.output << localization::format_translated_message(
                            "  Status: {}",
                            observation_status_display(
                                    observation.status(), state))
                 << '\n';
    render_roots(observation, state);
    render_phases(observation, state);
    render_configured_repositories(observation, state);
    render_dependencies(observation, state);
    render_build_units(observation, state);
    render_required_artifacts(observation, state);
    render_transaction_intents(observation, state);
    render_blockers(observation, state);
    return UnifiedPlanRenderingResult{
            state.output.str(), std::move(state.issues)};
}
