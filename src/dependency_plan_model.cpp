#include "dependency_plan.hpp"

#include "localization.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace {

std::string selected_provider_package_identity_conflict_diagnostic(
        const ProvidedDependency& existing,
        const ProvidedDependency& selected) {
    return localization::format_translated_message(
            "Selected providers use incompatible identities for package {}: {} and {}.",
            selected.package_name,
            provider_package_identity_display(existing),
            provider_package_identity_display(selected));
}

std::string join_guard_summary_values(const std::vector<std::string>& values) {
    std::stringstream summary;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) summary << ", ";
        summary << values[i];
    }
    return summary.str();
}

std::string provider_summary(const ProvidedDependency& provider) {
    return provided_dependency_display(provider);
}

std::string ambiguous_provider_dependency_summary(
        const AmbiguousProvidedDependency& dependency) {
    std::vector<std::string> candidates;
    for(const auto& candidate : dependency.candidates) {
        candidates.push_back(provider_summary(candidate));
    }
    return dependency.dependency + " (" +
           join_guard_summary_values(candidates) + ")";
}

std::string join_ambiguous_provider_summaries(
        const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::vector<std::string> values;
    for(const auto& dependency : dependencies) {
        values.push_back(ambiguous_provider_dependency_summary(dependency));
    }
    return join_guard_summary_values(values);
}

std::string split_package_target_summary(
        const BuildPlanSplitPackageTarget& target) {
    // NO_TRANSLATE(Issue #308): This guard summary is a stable structured
    // package identity; "base" names its BuildPlan relationship.
    return target.package_name + " (base: " + target.package_base + ")";
}

std::string join_split_package_target_summaries(
        const std::vector<BuildPlanSplitPackageTarget>& targets) {
    std::vector<std::string> values;
    for(const auto& target : targets) {
        values.push_back(split_package_target_summary(target));
    }
    return join_guard_summary_values(values);
}

std::string metadata_risk_summary(const BuildPlanMetadataRisk& risk) {
    // NO_TRANSLATE(Issue #308): These are stable BuildPlan metadata field
    // tokens surrounding package identities, not human-readable prose.
    std::vector<std::string> metadata;
    if(!risk.conflicts.empty()) {
        metadata.push_back(
                "conflicts: " + join_guard_summary_values(risk.conflicts));
    }
    if(!risk.replaces.empty()) {
        metadata.push_back(
                "replaces: " + join_guard_summary_values(risk.replaces));
    }

    std::string package_display = risk.package_name;
    if(risk.package_base != risk.package_name) {
        package_display += " (base: " + risk.package_base + ")";
    }

    std::stringstream metadata_summary;
    for(size_t i = 0; i < metadata.size(); ++i) {
        if(i > 0) metadata_summary << "; ";
        metadata_summary << metadata[i];
    }
    return package_display + " [" + metadata_summary.str() + "]";
}

std::string join_metadata_risk_summaries(
        const std::vector<BuildPlanMetadataRisk>& risks) {
    std::vector<std::string> values;
    for(const auto& risk : risks) {
        values.push_back(metadata_risk_summary(risk));
    }
    return join_guard_summary_values(values);
}

void require_mutation_constraint_preflight(
        const std::string& target, const BuildPlan& plan) {
    require_constructible_build_plan_constraints(plan);
    for(const auto& edge : plan.dependency_edges) {
        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
                edge.constraint_evaluation->satisfaction();
        if(satisfaction != ConstraintSatisfaction::Unsatisfied &&
           satisfaction != ConstraintSatisfaction::Unknown) {
            continue;
        }
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; dependency {} is {}: {}.",
                target, edge.dependency_spec,
                constraint_satisfaction_display(satisfaction),
                constraint_evaluation_reason_display(
                        edge.constraint_evaluation.value())));
    }
}

} // namespace

DesiredInstallReason desired_install_reason(
        const PlannedPackageTarget& target) {
    if(std::find(
               target.roles.begin(), target.roles.end(), PackageRole::Root) !=
       target.roles.end()) {
        return DesiredInstallReason::Explicit;
    }

    for(const auto role : target.roles) {
        if(role == PackageRole::RuntimeDependency ||
           role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency) {
            return DesiredInstallReason::Dependency;
        }
    }

    // NO_TRANSLATE(Issue #308): production projection validates roles before
    // this reducer and converts an unavailable reason to a typed issue.
    throw std::logic_error(
            "Planned package target has no package role: " +
            target.package_name);
}

bool has_incomplete_constraint_evaluations(const BuildPlan& plan) noexcept {
    return std::any_of(
            plan.dependency_edges.begin(), plan.dependency_edges.end(),
            [](const BuildPlanDependencyEdge& edge) {
                if(!edge.constraint_evaluation.has_value()) return false;
                const ConstraintSatisfaction satisfaction =
                        edge.constraint_evaluation->satisfaction();
                return satisfaction == ConstraintSatisfaction::Unsatisfied ||
                       satisfaction == ConstraintSatisfaction::Unknown;
            });
}

void require_compatible_selected_provider_package_identities(
        const BuildPlan& plan) {
    for(std::size_t index = 0; index < plan.provided.size(); ++index) {
        const BuildPlanProvidedDependency& selected = plan.provided[index];
        if(selected.resolution != ProviderResolutionKind::UserSelected) {
            continue;
        }
        auto conflict = std::find_if(
                plan.provided.begin(), plan.provided.begin() + index,
                [&selected](const BuildPlanProvidedDependency& existing) {
                    return existing.resolution ==
                                   ProviderResolutionKind::UserSelected &&
                           has_incompatible_provider_package_identity(
                                   existing.provider, selected.provider);
                });
        if(conflict != plan.provided.begin() + index) {
            throw std::runtime_error(
                    selected_provider_package_identity_conflict_diagnostic(
                            conflict->provider, selected.provider));
        }
    }
}

void require_constructible_build_plan_constraints(const BuildPlan& plan) {
    for(const auto& edge : plan.dependency_edges) {
        const bool has_typed_state = edge.requirement.has_value() ||
                edge.resolved_candidate.has_value() ||
                edge.constraint_evaluation.has_value();
        // Older pure fixtures may describe graph shape only. Production
        // resolver edges always enter the typed branch.
        if(!has_typed_state) continue;

        if(!edge.requirement.has_value()) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Build plan constraint requirement is invalid for {}.",
                            edge.dependency_spec));
        }

        const std::string& raw_specification = std::visit(
                [](const auto& requirement) -> const std::string& {
                    return requirement.raw_specification();
                },
                edge.requirement.value());
        if(raw_specification != edge.dependency_spec) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Build plan constraint requirement identity changed for {}.",
                            edge.dependency_spec));
        }

        if(edge.resolved_candidate.has_value()) {
            const bool identity_matches = std::visit(
                    [&edge](const auto& candidate) {
                        using Candidate =
                                std::decay_t<decltype(candidate)>;
                        if constexpr(std::is_same_v<
                                             Candidate,
                                             InstalledExactPackage>) {
                            return edge.kind == DependencyKind::Installed &&
                                   edge.resolved_package_name ==
                                           candidate.package_name;
                        } else if constexpr(std::is_same_v<
                                                    Candidate,
                                                    RepositoryExactPackage>) {
                            return edge.kind == DependencyKind::Repo &&
                                   !candidate.repository.repository_name.empty() &&
                                   edge.resolved_package_name ==
                                           candidate.package_name;
                        } else if constexpr(std::is_same_v<
                                                    Candidate,
                                                    AurResolvedDependencyCandidate>) {
                            return edge.kind == DependencyKind::Aur &&
                                   edge.resolved_package_name ==
                                           candidate.package_name &&
                                   edge.resolved_package_base ==
                                           candidate.package_base;
                        } else if constexpr(std::is_same_v<
                                                    Candidate,
                                                    LocalResolvedDependencyCandidate>) {
                            return edge.kind == DependencyKind::Local &&
                                   edge.resolved_package_name ==
                                           candidate.package_name &&
                                   edge.resolved_package_base ==
                                           candidate.package_base;
                        } else {
                            return edge.kind == DependencyKind::Provided &&
                                   edge.resolved_provider.has_value() &&
                                   same_provider_identity(
                                           edge.resolved_provider.value(),
                                           candidate.provider) &&
                                   edge.resolved_provider
                                                   ->provided_dependency_name ==
                                           candidate.provider
                                                   .provided_dependency_name;
                        }
                    },
                    edge.resolved_candidate.value());
            if(!identity_matches) {
                throw std::runtime_error(
                        localization::format_translated_message(
                                "Build plan constraint source identity is inconsistent for {}.",
                                edge.dependency_spec));
            }
            if(!edge.constraint_evaluation.has_value()) {
                throw std::runtime_error(
                        localization::format_translated_message(
                                "Build plan constraint evaluation is missing for {}.",
                                edge.dependency_spec));
            }
        }

        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
                edge.constraint_evaluation->satisfaction();
        if(satisfaction == ConstraintSatisfaction::Invalid ||
           satisfaction == ConstraintSatisfaction::Conflicting) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Cannot construct build plan; dependency {} is {}: {}.",
                            edge.dependency_spec,
                            constraint_satisfaction_display(satisfaction),
                            constraint_evaluation_reason_display(
                                    edge.constraint_evaluation.value())));
        }
    }
}

void require_fetchable_build_plan(
        const std::string& target, const BuildPlan& plan) {
    require_compatible_selected_provider_package_identities(plan);
    require_mutation_constraint_preflight(target, plan);
    if(!plan.unresolved.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; unresolved dependencies: {}",
                target, join_guard_summary_values(plan.unresolved)));
    }
    if(!plan.ambiguous_providers.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; ambiguous providers: {}",
                target,
                join_ambiguous_provider_summaries(
                        plan.ambiguous_providers)));
    }
    if(!plan.cycles.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; cyclic dependencies: {}",
                target, join_guard_summary_values(plan.cycles)));
    }
}

void require_executable_build_plan(
        const std::string& target, const BuildPlan& plan) {
    require_fetchable_build_plan(target, plan);
    if(!plan.metadata_risks.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; conflicts/replaces metadata requires manual review: {}",
                target,
                join_metadata_risk_summaries(plan.metadata_risks)));
    }
}

void require_executable_install_plan(
        const std::string& target, const BuildPlan& plan) {
    // POLICY: 段階的なguard呼び出し順は、複数の問題があるplanで最初に報告するcategoryの契約。
    require_executable_build_plan(target, plan);
    if(!plan.split_package_targets.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute singular install plan for {}; split package targets require the {} set lifecycle: {}",
                target, "PackageBase",
                join_split_package_target_summaries(
                        plan.split_package_targets)));
    }
}
