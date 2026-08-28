#include "dependency_plan.hpp"

#include "localization.hpp"
#include "package_relation_presentation.hpp"

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

std::optional<PlanSelectedProviderIdentityConflictReason>
selected_provider_identity_conflict(const BuildPlan& plan) {
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
            return PlanSelectedProviderIdentityConflictReason{
                conflict->provider, selected.provider};
        }
    }
    return std::nullopt;
}

bool resolved_candidate_matches_edge(
    const BuildPlanDependencyEdge& edge) {
    return std::visit(
        [&edge](const auto& candidate) {
            using Candidate = std::decay_t<decltype(candidate)>;
            if constexpr(std::is_same_v<Candidate, InstalledExactPackage>) {
                return edge.kind == DependencyKind::Installed &&
                       edge.resolved_package_name == candidate.package_name;
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    RepositoryExactPackage>) {
                return edge.kind == DependencyKind::Repo &&
                       !candidate.repository.repository_name.empty() &&
                       edge.resolved_package_name == candidate.package_name;
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    AurResolvedDependencyCandidate>) {
                return edge.kind == DependencyKind::Aur &&
                       edge.resolved_package_name == candidate.package_name &&
                       edge.resolved_package_base == candidate.package_base;
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    LocalResolvedDependencyCandidate>) {
                return edge.kind == DependencyKind::Local &&
                       edge.resolved_package_name == candidate.package_name &&
                       edge.resolved_package_base == candidate.package_base;
            } else {
                return edge.kind == DependencyKind::Provided &&
                       edge.resolved_provider.has_value() &&
                       same_provider_identity(
                           edge.resolved_provider.value(),
                           candidate.provider) &&
                       edge.resolved_provider->provided_dependency_name ==
                           candidate.provider.provided_dependency_name;
            }
        },
        edge.resolved_candidate.value());
}

std::optional<PlanConstraintAuthorityReason>
constraint_authority_issue(const BuildPlan& plan) {
    for(std::size_t index = 0; index < plan.dependency_edges.size(); ++index) {
        const BuildPlanDependencyEdge& edge = plan.dependency_edges[index];
        const bool has_typed_state = edge.requirement.has_value() ||
                                     edge.resolved_candidate.has_value() ||
                                     edge.constraint_evaluation.has_value();
        // Older pure fixtures may describe graph shape only. Production
        // resolver edges always enter the typed branch.
        if(!has_typed_state) continue;

        if(!edge.requirement.has_value()) {
            return PlanConstraintAuthorityReason{
                PlanConstraintAuthorityIssueKind::MissingRequirement,
                index,
                edge.dependency_spec,
                std::nullopt};
        }

        const std::string& raw_specification = std::visit(
            [](const auto& requirement) -> const std::string& {
                return requirement.raw_specification();
            },
            edge.requirement.value());
        if(raw_specification != edge.dependency_spec) {
            return PlanConstraintAuthorityReason{
                PlanConstraintAuthorityIssueKind::
                    RequirementIdentityChanged,
                index,
                edge.dependency_spec,
                std::nullopt};
        }

        if(edge.resolved_candidate.has_value()) {
            if(!resolved_candidate_matches_edge(edge)) {
                return PlanConstraintAuthorityReason{
                    PlanConstraintAuthorityIssueKind::
                        SourceIdentityInconsistent,
                    index,
                    edge.dependency_spec,
                    std::nullopt};
            }
            if(!edge.constraint_evaluation.has_value()) {
                return PlanConstraintAuthorityReason{
                    PlanConstraintAuthorityIssueKind::MissingEvaluation,
                    index,
                    edge.dependency_spec,
                    std::nullopt};
            }
        }

        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
            edge.constraint_evaluation->satisfaction();
        if(satisfaction == ConstraintSatisfaction::Invalid) {
            return PlanConstraintAuthorityReason{
                PlanConstraintAuthorityIssueKind::InvalidEvaluation,
                index,
                edge.dependency_spec,
                satisfaction};
        }
        if(satisfaction == ConstraintSatisfaction::Conflicting) {
            return PlanConstraintAuthorityReason{
                PlanConstraintAuthorityIssueKind::ConflictingEvaluation,
                index,
                edge.dependency_spec,
                satisfaction};
        }
    }
    return std::nullopt;
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

namespace {

void append_required_action(
    std::vector<PlanRequiredAction>& actions,
    PlanRequiredAction action) {
    if(std::find(actions.begin(), actions.end(), action) == actions.end()) {
        actions.push_back(action);
    }
}

enum class PlanCompletenessEffect {
    None,
    Unknown,
    Incomplete,
};

constexpr unsigned int capability_bit(
    ExecutionCapability capability) noexcept {
    return 1U << static_cast<unsigned int>(capability);
}

constexpr unsigned int ALL_EXECUTION_CAPABILITIES =
    capability_bit(ExecutionCapability::Fetch) |
    capability_bit(ExecutionCapability::Build) |
    capability_bit(ExecutionCapability::Install);

struct AssessedPlanReason {
    ExecutionReadinessReason readiness;
    unsigned int capability_mask = 0;
    PlanCompletenessEffect completeness_effect =
        PlanCompletenessEffect::None;
};

void append_assessed_reason(
    std::vector<AssessedPlanReason>& reasons, PlanReason reason,
    ExecutionReadinessState state, bool blocks_production_guard,
    PlanRequiredAction required_action, unsigned int capability_mask,
    PlanCompletenessEffect completeness_effect) {
    reasons.push_back(AssessedPlanReason{
        ExecutionReadinessReason{
            std::move(reason), state, blocks_production_guard,
            required_action},
        capability_mask,
        completeness_effect});
}

std::vector<AssessedPlanReason> assess_plan_reasons(const BuildPlan& plan) {
    std::vector<AssessedPlanReason> reasons;

    if(const auto conflict = selected_provider_identity_conflict(plan);
       conflict.has_value()) {
        append_assessed_reason(
            reasons, conflict.value(), ExecutionReadinessState::Blocked,
            true, PlanRequiredAction::ResolveDependency,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Incomplete);
    }

    if(const auto issue = constraint_authority_issue(plan);
       issue.has_value()) {
        append_assessed_reason(
            reasons, issue.value(), ExecutionReadinessState::Blocked,
            true, PlanRequiredAction::CorrectPlanAuthority,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Incomplete);
    }

    for(std::size_t index = 0; index < plan.dependency_edges.size(); ++index) {
        const BuildPlanDependencyEdge& edge = plan.dependency_edges[index];
        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
            edge.constraint_evaluation->satisfaction();
        if(satisfaction == ConstraintSatisfaction::Unknown) {
            append_assessed_reason(
                reasons,
                PlanConstraintReadinessReason{
                    index, edge.dependency_spec, satisfaction},
                ExecutionReadinessState::Unknown, true,
                PlanRequiredAction::ObtainMetadata,
                ALL_EXECUTION_CAPABILITIES,
                PlanCompletenessEffect::Unknown);
        } else if(satisfaction == ConstraintSatisfaction::Unsatisfied) {
            append_assessed_reason(
                reasons,
                PlanConstraintReadinessReason{
                    index, edge.dependency_spec, satisfaction},
                ExecutionReadinessState::Blocked, true,
                PlanRequiredAction::ResolveDependency,
                ALL_EXECUTION_CAPABILITIES,
                PlanCompletenessEffect::Incomplete);
        }
    }

    for(const BuildPlanResolutionFailure& failure :
        plan.resolution_failures) {
        append_assessed_reason(
            reasons, PlanResolutionReason{failure},
            ExecutionReadinessState::Unknown, false,
            PlanRequiredAction::ObtainMetadata,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Unknown);
    }
    for(const IncompleteProviderCandidateSet& candidate_set :
        plan.incomplete_provider_candidate_sets) {
        append_assessed_reason(
            reasons,
            PlanIncompleteProviderCandidateReason{candidate_set},
            ExecutionReadinessState::Unknown, false,
            PlanRequiredAction::ObtainMetadata,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Unknown);
    }
    for(const std::string& dependency : plan.unresolved) {
        append_assessed_reason(
            reasons, PlanUnresolvedDependencyReason{dependency},
            ExecutionReadinessState::Blocked, true,
            PlanRequiredAction::ResolveDependency,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Incomplete);
    }
    for(const AmbiguousProvidedDependency& dependency :
        plan.ambiguous_providers) {
        append_assessed_reason(
            reasons, PlanAmbiguousProviderReason{dependency},
            ExecutionReadinessState::Blocked, true,
            PlanRequiredAction::SelectProvider,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Incomplete);
    }
    for(const std::string& dependency : plan.cycles) {
        append_assessed_reason(
            reasons, PlanDependencyCycleReason{dependency},
            ExecutionReadinessState::Blocked, true,
            PlanRequiredAction::ResolveDependency,
            ALL_EXECUTION_CAPABILITIES,
            PlanCompletenessEffect::Incomplete);
    }

    const unsigned int build_and_install =
        capability_bit(ExecutionCapability::Build) |
        capability_bit(ExecutionCapability::Install);
    for(const PackageRelationAssessment& assessment :
        plan.relation_assessments) {
        switch(assessment.kind) {
            case PackageRelationAssessmentKind::
                ConfirmedNoMatchingCurrentOrPlannedTarget:
                append_assessed_reason(
                    reasons, PlanDeclaredRelationReason{assessment},
                    ExecutionReadinessState::Ready, false,
                    PlanRequiredAction::None, build_and_install,
                    PlanCompletenessEffect::None);
                break;
            case PackageRelationAssessmentKind::ConfirmedInstalledConflict:
            case PackageRelationAssessmentKind::ConfirmedPlannedTargetConflict:
            case PackageRelationAssessmentKind::PotentialReplacement:
                append_assessed_reason(
                    reasons, PlanDeclaredRelationReason{assessment},
                    ExecutionReadinessState::RequiresCheck, true,
                    PlanRequiredAction::ReviewDeclaredRelations,
                    build_and_install, PlanCompletenessEffect::None);
                break;
            case PackageRelationAssessmentKind::DeclaredRelation:
            case PackageRelationAssessmentKind::Unknown:
                append_assessed_reason(
                    reasons, PlanDeclaredRelationReason{assessment},
                    ExecutionReadinessState::RequiresCheck, true,
                    PlanRequiredAction::ReviewDeclaredRelations,
                    build_and_install, PlanCompletenessEffect::Unknown);
                break;
            case PackageRelationAssessmentKind::Invalid:
                append_assessed_reason(
                    reasons, PlanDeclaredRelationReason{assessment},
                    ExecutionReadinessState::Blocked, true,
                    PlanRequiredAction::ReviewDeclaredRelations,
                    build_and_install, PlanCompletenessEffect::Incomplete);
                break;
        }
    }

    for(const BuildPlanSplitPackageTarget& target :
        plan.split_package_targets) {
        append_assessed_reason(
            reasons, PlanSplitPackageReason{target},
            ExecutionReadinessState::Blocked, true,
            PlanRequiredAction::UsePackageBaseSetLifecycle,
            capability_bit(ExecutionCapability::Install),
            PlanCompletenessEffect::None);
    }
    return reasons;
}

ExecutionReadiness project_execution_readiness(
    const std::vector<AssessedPlanReason>& assessed,
    ExecutionCapability capability) {
    ExecutionReadiness readiness;
    readiness.capability = capability;

    bool has_blocked_reason = false;
    bool has_unknown_reason = false;
    bool has_requires_check_reason = false;
    for(const AssessedPlanReason& reason : assessed) {
        if((reason.capability_mask & capability_bit(capability)) == 0) {
            continue;
        }
        readiness.reasons.push_back(reason.readiness);
        readiness.is_blocked_by_production_guard |=
            reason.readiness.blocks_production_guard;
        append_required_action(
            readiness.required_actions,
            reason.readiness.required_action);
        has_blocked_reason |= reason.readiness.state ==
                              ExecutionReadinessState::Blocked;
        has_unknown_reason |= reason.readiness.state ==
                              ExecutionReadinessState::Unknown;
        has_requires_check_reason |= reason.readiness.state ==
                                     ExecutionReadinessState::RequiresCheck;
    }

    if(has_blocked_reason) {
        readiness.state = ExecutionReadinessState::Blocked;
    } else if(has_unknown_reason) {
        readiness.state = ExecutionReadinessState::Unknown;
    } else if(has_requires_check_reason) {
        readiness.state = ExecutionReadinessState::RequiresCheck;
    } else {
        readiness.state = ExecutionReadinessState::Ready;
        append_required_action(
            readiness.required_actions, PlanRequiredAction::None);
    }
    return readiness;
}

void throw_production_guard_reason(
    const std::string& target, const BuildPlan& plan,
    const PlanReason& reason) {
    if(const auto* conflict = std::get_if<
           PlanSelectedProviderIdentityConflictReason>(&reason)) {
        throw std::runtime_error(
            selected_provider_package_identity_conflict_diagnostic(
                conflict->existing, conflict->selected));
    }
    if(std::holds_alternative<PlanConstraintAuthorityReason>(reason)) {
        require_constructible_build_plan_constraints(plan);
    }
    if(const auto* constraint =
           std::get_if<PlanConstraintReadinessReason>(&reason)) {
        const BuildPlanDependencyEdge& edge =
            plan.dependency_edges[constraint->edge_index];
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute build plan for {}; dependency {} is {}: {}.",
            target, edge.dependency_spec,
            constraint_satisfaction_display(constraint->satisfaction),
            constraint_evaluation_reason_display(
                edge.constraint_evaluation.value())));
    }
    if(std::holds_alternative<PlanUnresolvedDependencyReason>(reason)) {
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute build plan for {}; unresolved dependencies: {}",
            target, join_guard_summary_values(plan.unresolved)));
    }
    if(std::holds_alternative<PlanAmbiguousProviderReason>(reason)) {
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute build plan for {}; ambiguous providers: {}",
            target,
            join_ambiguous_provider_summaries(
                plan.ambiguous_providers)));
    }
    if(std::holds_alternative<PlanDependencyCycleReason>(reason)) {
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute build plan for {}; cyclic dependencies: {}",
            target, join_guard_summary_values(plan.cycles)));
    }
    if(const auto* relation =
           std::get_if<PlanDeclaredRelationReason>(&reason)) {
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute build plan for {}; {}", target,
            package_relation_assessment_diagnostic_display(
                relation->assessment)));
    }
    if(std::holds_alternative<PlanSplitPackageReason>(reason)) {
        throw std::runtime_error(localization::format_translated_message(
            "Cannot execute singular install plan for {}; split package targets require the {} set lifecycle: {}",
            target, "PackageBase",
            join_split_package_target_summaries(
                plan.split_package_targets)));
    }
    // NO_TRANSLATE(Issue #350): internal typed-readiness invariant token.
    throw std::logic_error(
        "Build plan readiness marked an unsupported reason as blocking.");
}

void require_capability_readiness(
    const std::string& target, const BuildPlan& plan,
    ExecutionCapability capability) {
    const std::vector<AssessedPlanReason> assessed =
        assess_plan_reasons(plan);
    const ExecutionReadiness readiness =
        project_execution_readiness(assessed, capability);
    const auto blocking_reason = std::find_if(
        readiness.reasons.begin(), readiness.reasons.end(),
        [](const ExecutionReadinessReason& reason) {
            return reason.blocks_production_guard;
        });
    if(blocking_reason != readiness.reasons.end()) {
        throw_production_guard_reason(target, plan, blocking_reason->reason);
    }
}

} // namespace

PlanStateProjection project_build_plan_state(const BuildPlan& plan) {
    PlanStateProjection projection;
    const std::vector<AssessedPlanReason> assessed =
        assess_plan_reasons(plan);
    projection.readiness = {
        project_execution_readiness(
            assessed, ExecutionCapability::Fetch),
        project_execution_readiness(
            assessed, ExecutionCapability::Build),
        project_execution_readiness(
            assessed, ExecutionCapability::Install)};

    bool has_definite_incomplete_reason = false;
    bool has_unknown_reason = false;
    for(const AssessedPlanReason& reason : assessed) {
        const auto* relation = std::get_if<PlanDeclaredRelationReason>(
            &reason.readiness.reason);
        if(std::holds_alternative<PlanConstraintAuthorityReason>(
               reason.readiness.reason) ||
           (relation != nullptr &&
            relation->assessment.kind ==
                PackageRelationAssessmentKind::Invalid)) {
            projection.construction = PlanConstruction::Failed;
        }
        switch(reason.completeness_effect) {
            case PlanCompletenessEffect::None:
                continue;
            case PlanCompletenessEffect::Unknown:
                has_unknown_reason = true;
                break;
            case PlanCompletenessEffect::Incomplete:
                has_definite_incomplete_reason = true;
                break;
        }
        projection.completeness_reasons.push_back(
            reason.readiness.reason);
    }
    if(projection.construction == PlanConstruction::Failed ||
       has_definite_incomplete_reason) {
        projection.completeness = PlanCompleteness::Incomplete;
    } else if(has_unknown_reason) {
        projection.completeness = PlanCompleteness::Unknown;
    } else {
        projection.completeness = PlanCompleteness::Complete;
    }

    if(!plan.cancelled_provider_dependencies.empty()) {
        projection.provider_decision = ProviderDecision::Cancelled;
    } else if(!plan.ambiguous_providers.empty()) {
        projection.provider_decision = ProviderDecision::Ambiguous;
    } else if(!plan.incomplete_provider_candidate_sets.empty()) {
        projection.provider_decision = ProviderDecision::Unavailable;
    } else if(std::any_of(
                  plan.provided.begin(), plan.provided.end(),
                  [](const BuildPlanProvidedDependency& dependency) {
                      return dependency.resolution ==
                             ProviderResolutionKind::UserSelected;
                  })) {
        projection.provider_decision = ProviderDecision::Selected;
    } else {
        projection.provider_decision = ProviderDecision::Unique;
    }

    return projection;
}

const ExecutionReadiness& execution_readiness(
    const PlanStateProjection& projection,
    ExecutionCapability capability) noexcept {
    switch(capability) {
        case ExecutionCapability::Fetch:
            return projection.readiness[0];
        case ExecutionCapability::Build:
            return projection.readiness[1];
        case ExecutionCapability::Install:
            return projection.readiness[2];
    }
    return projection.readiness[0];
}

void require_compatible_selected_provider_package_identities(
    const BuildPlan& plan) {
    const std::optional<PlanSelectedProviderIdentityConflictReason> conflict =
        selected_provider_identity_conflict(plan);
    if(!conflict.has_value()) return;
    throw std::runtime_error(
        selected_provider_package_identity_conflict_diagnostic(
            conflict->existing, conflict->selected));
}

void require_constructible_build_plan_constraints(const BuildPlan& plan) {
    const std::optional<PlanConstraintAuthorityReason> issue =
        constraint_authority_issue(plan);
    if(!issue.has_value()) return;

    const BuildPlanDependencyEdge& edge =
        plan.dependency_edges[issue->edge_index];
    switch(issue->kind) {
        case PlanConstraintAuthorityIssueKind::MissingRequirement:
            throw std::runtime_error(localization::format_translated_message(
                "Build plan constraint requirement is invalid for {}.",
                edge.dependency_spec));
        case PlanConstraintAuthorityIssueKind::RequirementIdentityChanged:
            throw std::runtime_error(localization::format_translated_message(
                "Build plan constraint requirement identity changed for {}.",
                edge.dependency_spec));
        case PlanConstraintAuthorityIssueKind::SourceIdentityInconsistent:
            throw std::runtime_error(localization::format_translated_message(
                "Build plan constraint source identity is inconsistent for {}.",
                edge.dependency_spec));
        case PlanConstraintAuthorityIssueKind::MissingEvaluation:
            throw std::runtime_error(localization::format_translated_message(
                "Build plan constraint evaluation is missing for {}.",
                edge.dependency_spec));
        case PlanConstraintAuthorityIssueKind::InvalidEvaluation:
        case PlanConstraintAuthorityIssueKind::ConflictingEvaluation:
            throw std::runtime_error(localization::format_translated_message(
                "Cannot construct build plan; dependency {} is {}: {}.",
                edge.dependency_spec,
                constraint_satisfaction_display(issue->satisfaction.value()),
                constraint_evaluation_reason_display(
                    edge.constraint_evaluation.value())));
    }
}

void require_fetchable_build_plan(
    const std::string& target, const BuildPlan& plan) {
    require_capability_readiness(
        target, plan, ExecutionCapability::Fetch);
}

void require_executable_build_plan(
    const std::string& target, const BuildPlan& plan) {
    require_capability_readiness(
        target, plan, ExecutionCapability::Build);
}

void require_executable_install_plan(
    const std::string& target, const BuildPlan& plan) {
    // POLICY: projectionのtyped reason順は、複数問題時に最初に報告する
    // production guard categoryの既存契約でもある。
    require_capability_readiness(
        target, plan, ExecutionCapability::Install);
}
