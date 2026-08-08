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

std::string root_source_display(UnifiedPlanRootSourceKind source) {
    switch(source) {
    case UnifiedPlanRootSourceKind::Repository:
        return localization::translate_message("repository");
    case UnifiedPlanRootSourceKind::Aur:
        return "AUR";
    case UnifiedPlanRootSourceKind::Local:
        return localization::translate_message("local");
    }
    return localization::translate_message("unsupported");
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

std::string root_identity_display(const UnifiedPlanRootReference& root) {
    return std::visit(
            [](const auto& identity) -> std::string {
                using Identity = std::decay_t<decltype(identity)>;
                if constexpr(std::is_same_v<
                                     Identity,
                                     RepositoryRootPackageIdentity>) {
                    return identity.repository_name + "/" +
                           identity.package_name;
                } else if constexpr(std::is_same_v<
                                            Identity,
                                            RepositorySourceBuildRootIdentity>) {
                    return localization::format_translated_message(
                            "{} ({}: {}; source key: {})",
                            identity.package_name, "PackageBase",
                            identity.package_base,
                            identity.canonical_source_identity_key);
                } else if constexpr(std::is_same_v<
                                            Identity,
                                            AurRootPackageIdentity>) {
                    return localization::format_translated_message(
                            "{}/{} ({}: {})", "AUR",
                            identity.package_name, "PackageBase",
                            identity.package_base);
                } else {
                    return localization::format_translated_message(
                            "{} (device: {}; inode: {})",
                            identity.canonical_path.generic_string(),
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

std::string resolved_candidate_display(
        const ResolvedDependencyCandidate& candidate) {
    return std::visit(
            [](const auto& resolved) -> std::string {
                using Candidate = std::decay_t<decltype(resolved)>;
                if constexpr(std::is_same_v<Candidate, InstalledExactPackage>) {
                    return localization::format_translated_message(
                            "installed/{}", resolved.package_name);
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            RepositoryExactPackage>) {
                    return resolved.repository.repository_name + "/" +
                           resolved.package_name;
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            AurResolvedDependencyCandidate>) {
                    return localization::format_translated_message(
                            "{}/{} ({}: {})", "AUR",
                            resolved.package_name, "PackageBase",
                            resolved.package_base);
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            LocalResolvedDependencyCandidate>) {
                    return localization::format_translated_message(
                            "local/{} ({}: {})", resolved.package_name,
                            "PackageBase", resolved.package_base);
                } else {
                    return provider_package_identity_display(
                            resolved.provider);
                }
            },
            candidate);
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
        std::size_t build_unit_index) {
    switch(kind) {
    case SourceBuildSourceKind::Repository:
        return localization::translate_message("repository");
    case SourceBuildSourceKind::Aur:
        return "AUR";
    }
    return unsupported_display(
            state, UnifiedPlanRenderingSection::BuildUnits,
            build_unit_index, std::nullopt,
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
                                root_identity_display(root))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Source: {}",
                                root_source_display(root.source_kind()))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Route: {}",
                                root_route_display(
                                        root.route_kind(), state, index))
                     << '\n';
        state.output << localization::format_translated_message(
                                "     Request: {} (invocation index: {})",
                                correlation.requested_name,
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
                                "     {}. {} ({}: {}) -> {} [{}]",
                                edge_index + 1, edge.parent_package_name,
                                "PackageBase", edge.parent_package_base,
                                edge.dependency_spec,
                                dependency_kind_display(
                                        edge.kind, state, authority_index,
                                        edge_index))
                     << '\n';
        if(edge.resolved_candidate.has_value()) {
            state.output << localization::format_translated_message(
                                    "        Selected identity: {}",
                                    resolved_candidate_display(
                                            edge.resolved_candidate.value()))
                         << '\n';
        } else if(edge.resolved_provider.has_value()) {
            state.output << localization::format_translated_message(
                                    "        Selected provider: {}",
                                    provider_package_identity_display(
                                            edge.resolved_provider.value()))
                         << '\n';
        }
        if(!edge.constraint_evaluation.has_value()) continue;
        try {
            const ConstraintEvaluation& evaluation =
                    edge.constraint_evaluation.value();
            state.output << localization::format_translated_message(
                                    "        Constraint: {} ({})",
                                    constraint_satisfaction_display(
                                            evaluation.satisfaction()),
                                    constraint_evaluation_reason_display(
                                            evaluation))
                         << '\n';
        } catch(const std::exception& error) {
            state.add_issue(
                    UnifiedPlanRenderingIssueKind::UnsupportedValue,
                    UnifiedPlanRenderingSection::Dependencies,
                    authority_index, edge_index,
                    localization::format_translated_message(
                            "A dependency constraint could not be rendered: {}",
                            error.what()));
            state.output << localization::translate_message(
                                    "        Constraint: unsupported")
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
        if(authority.local_build_plan() != nullptr) {
            state.output << localization::format_translated_message(
                                    "  {}. LocalBuildPlan", index + 1)
                         << '\n';
            state.output << localization::translate_message(
                                    "     Local dependency authority is borrowed without detaching it.")
                         << '\n';
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

void render_build_units(
        const UnifiedPlanObservation& observation, RenderState& state) {
    state.output << localization::translate_message("Build units:") << '\n';
    if(observation.build_units().empty()) {
        state.output << localization::translate_message("  None") << '\n';
        return;
    }
    for(std::size_t index = 0; index < observation.build_units().size();
        ++index) {
        state.output << "  " << index + 1 << ". ";
        std::visit(
                [&](const auto& unit) {
                    using Unit = std::decay_t<decltype(unit)>;
                    if constexpr(std::is_same_v<
                                         Unit,
                                         AurPackageBaseBuildUnitReference>) {
                        const BuildPlanEntry* entry = unit.entry();
                        if(entry == nullptr) {
                            state.output << unavailable_display(
                                    state,
                                    UnifiedPlanRenderingSection::BuildUnits,
                                    index, std::nullopt,
                                    localization::format_translated_message(
                                            "An {} build unit no longer references a {} entry.",
                                            "AUR", "BuildPlan"));
                            return;
                        }
                        state.output << localization::format_translated_message(
                                "{} {}: {}", "AUR", "PackageBase",
                                entry->package_base);
                        state.output << '\n'
                                     << localization::format_translated_message(
                                                "     Package children: {}",
                                                join_display_values(
                                                        entry->package_names));
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
                                "Local {}: {} ({})", "PackageBase",
                                metadata.package_base,
                                unit.source_root()
                                        .canonical_path.generic_string());
                        state.output << '\n'
                                     << localization::format_translated_message(
                                                "     Package children: {}",
                                                join_display_values(children));
                    } else {
                        const RegisteredSourcePreferenceSnapshot& source =
                                unit.source();
                        const std::string source_kind = source.source_kind
                                        .has_value()
                                ? source_build_kind_display(
                                          source.source_kind.value(), state,
                                          index)
                                : localization::translate_message(
                                          "not observed");
                        const std::string source_key =
                                source.canonical_source_identity_key
                                                .has_value()
                                ? source.canonical_source_identity_key.value()
                                : localization::translate_message(
                                          "not observed");
                        state.output << localization::format_translated_message(
                                "Prepared {} source {}: {}", source_kind,
                                "PackageBase",
                                unit.checkout_package_base());
                        state.output << '\n'
                                     << localization::format_translated_message(
                                                "     Requested package: {}",
                                                unit.requested_package_name())
                                     << '\n'
                                     << localization::format_translated_message(
                                                "     Source key: {}",
                                                source_key);
                    }
                },
                observation.build_units()[index]);
        state.output << '\n';
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
        const RequiredPackageArtifactTarget target =
                observation.required_artifacts()[index].target();
        state.output << localization::format_translated_message(
                                "  {}. {}: {}; package child: {}; install reason: {}",
                                index + 1, "PackageBase",
                                target.package_base,
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
                            root_identity_display(typed_target.root));
                } else if constexpr(std::is_same_v<
                                            Target,
                                            RepositoryDependencyInstallIntent>) {
                    const RepositoryExactPackage& package =
                            typed_target.package.get();
                    state.output << localization::format_translated_message(
                            "dependency: {}/{}",
                            package.repository.repository_name,
                            package.package_name);
                } else if constexpr(std::is_same_v<
                                            Target,
                                            RepositoryProviderInstallIntent>) {
                    state.output << localization::format_translated_message(
                            "selected provider: {}",
                            provider_package_identity_display(
                                    typed_target.provider.get()));
                } else {
                    state.output << localization::translate_message(
                            "system upgrade");
                }
            },
            target);
    state.output << '\n';
    static_cast<void>(intent_index);
    static_cast<void>(target_index);
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
    const RequiredPackageArtifactTarget artifact =
            observation.required_artifacts()[artifact_index].target();
    state.output << localization::format_translated_message(
            "{} artifact #{}: {}/{}",
            is_root ? localization::translate_message("root")
                    : localization::translate_message("dependency"),
            artifact_index + 1, artifact.package_base,
            artifact.package_name);
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
                                                "  {}. Repository package transaction (needed: {})",
                                                intent_index + 1,
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
                                                "  {}. Source-built artifact install boundary (needed: {})",
                                                intent_index + 1,
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

std::string source_failure_detail_display(
        const SourceFailureUnifiedPlanBlocker& blocker) {
    return std::visit(
            [](const auto& reference) -> std::string {
                using Reference = std::decay_t<decltype(reference)>;
                const auto& detail = reference.get();
                if constexpr(std::is_same_v<
                                     Reference,
                                     UnifiedPlanBorrowedAuthorityReference<
                                             BuildPlanResolutionFailure>>) {
                    return localization::format_translated_message(
                            "{} resolution failure for {}: {}", "BuildPlan",
                            detail.subject, detail.diagnostic);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    IncompleteProviderCandidateSet>>) {
                    return localization::format_translated_message(
                            "incomplete provider candidates for {}",
                            detail.dependency);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    RepositoryExactPackageSourceFailure>>) {
                    return localization::format_translated_message(
                            "repository package source failure: {}/{}",
                            detail.repository.repository_name,
                            detail.package_name);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    RepositoryProviderSourceFailure>>) {
                    return localization::format_translated_message(
                            "repository provider source failure: {}",
                            detail.repository.repository_name);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    LocalSourceRootFailure>>) {
                    return localization::format_translated_message(
                            "local source failure: {}",
                            detail.path.generic_string());
                } else {
                    return localization::format_translated_message(
                            "{} query failure for {}: {}", "AUR",
                            join_display_values(detail.package_names),
                            detail.diagnostic);
                }
            },
            blocker.detail);
}

std::string build_plan_state_blocker_display(
        const BuildPlanStateUnifiedPlanBlocker& blocker,
        std::size_t blocker_index, RenderState& state) {
    const BuildPlan& plan = blocker.authority.get();
    switch(blocker.kind) {
    case BuildPlanStateUnifiedPlanBlockerKind::UnresolvedDependency:
        if(blocker.authority_index < plan.unresolved.size()) {
            return localization::format_translated_message(
                    "unresolved dependency: {}",
                    plan.unresolved[blocker.authority_index]);
        }
        break;
    case BuildPlanStateUnifiedPlanBlockerKind::DependencyCycle:
        if(blocker.authority_index < plan.cycles.size()) {
            return localization::format_translated_message(
                    "dependency cycle: {}",
                    plan.cycles[blocker.authority_index]);
        }
        break;
    case BuildPlanStateUnifiedPlanBlockerKind::SplitPackageSelectionRequired:
        if(blocker.authority_index < plan.split_package_targets.size()) {
            const BuildPlanSplitPackageTarget& target =
                    plan.split_package_targets[blocker.authority_index];
            return localization::format_translated_message(
                    "split package selection required: {} ({}: {})",
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

std::string route_preflight_blocker_display(
        const RoutePreflightUnifiedPlanBlocker& blocker) {
    return std::visit(
            [](const auto& reference) -> std::string {
                using Reference = std::decay_t<decltype(reference)>;
                const auto& detail = reference.get();
                if constexpr(std::is_same_v<
                                     Reference,
                                     UnifiedPlanBorrowedAuthorityReference<
                                             AurUpdateExecutionIssue>>) {
                    return localization::format_translated_message(
                            "{} execution preflight failure: {}", "AUR",
                            detail.diagnostic);
                } else if constexpr(std::is_same_v<
                                            Reference,
                                            UnifiedPlanBorrowedAuthorityReference<
                                                    SystemSourceUpgradeIssue>>) {
                    return localization::format_translated_message(
                            "system/source preflight failure: {}",
                            detail.diagnostic);
                } else {
                    return localization::format_translated_message(
                            "{} preflight failure: {}", "upgrade-all",
                            detail.diagnostic);
                }
            },
            blocker.detail);
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
                            "unknown dependency: {} required by {} ({}: {})",
                            edge.dependency_spec, edge.parent_package_name,
                            "PackageBase", edge.parent_package_base);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            AmbiguousUnifiedPlanBlocker>) {
                    const AmbiguousProvidedDependency& ambiguous =
                            typed_blocker.detail.get();
                    std::vector<std::string> candidates;
                    candidates.reserve(ambiguous.candidates.size());
                    for(const ProvidedDependency& candidate :
                        ambiguous.candidates) {
                        candidates.push_back(
                                provider_package_identity_display(candidate));
                    }
                    return localization::format_translated_message(
                            "ambiguous provider for {}: {}",
                            ambiguous.dependency,
                            join_display_values(candidates));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            UnsupportedUnifiedPlanBlocker>) {
                    const MixedPackageBaseInstallReasonUnsupported& detail =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "unsupported mixed install reason for {} {}",
                            "PackageBase", detail.package_base);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            SourceFailureUnifiedPlanBlocker>) {
                    return source_failure_detail_display(typed_blocker);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            ConstraintFailureUnifiedPlanBlocker>) {
                    const ConstraintEvaluation& evaluation =
                            typed_blocker.detail.get();
                    try {
                        return localization::format_translated_message(
                                "constraint failure: {} ({})",
                                constraint_satisfaction_display(
                                        evaluation.satisfaction()),
                                constraint_evaluation_reason_display(
                                        evaluation));
                    } catch(const std::exception& error) {
                        return unsupported_display(
                                state,
                                UnifiedPlanRenderingSection::Blockers,
                                blocker_index, std::nullopt,
                                localization::format_translated_message(
                                        "A constraint blocker could not be rendered: {}",
                                        error.what()));
                    }
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            MetadataRiskUnifiedPlanBlocker>) {
                    const BuildPlanMetadataRisk& risk =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "metadata risk for {} ({}: {}); conflicts: {}; replaces: {}",
                            risk.package_name, "PackageBase",
                            risk.package_base,
                            join_display_values(risk.conflicts),
                            join_display_values(risk.replaces));
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            LocalDependencyPlanUnifiedPlanBlocker>) {
                    const LocalDependencyPlanFailure& failure =
                            typed_blocker.detail.get();
                    return localization::format_translated_message(
                            "local dependency plan failure for {}",
                            failure.parent_package_name);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            RootPackagePreparationUnifiedPlanBlocker>) {
                    const RootPackageInstallPreparationFailure& failure =
                            typed_blocker.detail.get();
                    return localization::format_translated_plural_message(
                            "root package preparation failure ({} typed detail)",
                            "root package preparation failure ({} typed details)",
                            failure.details.size(), failure.details.size());
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            BuildPlanArtifactProjectionUnifiedPlanBlocker>) {
                    return localization::format_translated_message(
                            "required artifact projection failure: {}",
                            typed_blocker.detail.diagnostic);
                } else if constexpr(std::is_same_v<
                                            Blocker,
                                            BuildPlanStateUnifiedPlanBlocker>) {
                    return build_plan_state_blocker_display(
                            typed_blocker, blocker_index, state);
                } else {
                    return route_preflight_blocker_display(typed_blocker);
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
