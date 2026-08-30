#include "invocation_owned_cleanup_adapter.hpp"

#include "build_plan_artifact_target_projection.hpp"
#include "package_identifier.hpp"
#include "source_package_identity_projection.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

void add_issue(
    std::vector<CleanupLifecycleProjectionIssue>& issues,
    CleanupLifecycleProjectionIssueKind kind,
    std::optional<std::size_t> edge_index = std::nullopt,
    std::optional<std::string> package_name = std::nullopt,
    std::optional<PackageMetadataFailure> metadata_failure =
        std::nullopt) {
    issues.push_back(CleanupLifecycleProjectionIssue{
        kind, edge_index, std::move(package_name),
        std::move(metadata_failure)});
}

bool snapshot_succeeded(
    const InstalledPackageStateSnapshotResult& snapshot) noexcept {
    return std::holds_alternative<InstalledPackageStateSnapshot>(snapshot);
}

const InstalledPackageStateSnapshot* successful_snapshot(
    const InstalledPackageStateSnapshotResult& snapshot) noexcept {
    return std::get_if<InstalledPackageStateSnapshot>(&snapshot);
}

void retain_snapshot_failure(
    const InstalledPackageStateSnapshotResult& snapshot,
    CleanupLifecycleProjectionIssueKind kind,
    std::vector<CleanupLifecycleProjectionIssue>& issues) {
    const auto* failure = std::get_if<PackageMetadataFailure>(&snapshot);
    if(failure != nullptr) {
        add_issue(issues, kind, std::nullopt, std::nullopt, *failure);
    }
}

bool is_known_role(PackageRole role) noexcept {
    switch(role) {
        case PackageRole::Root:
        case PackageRole::RuntimeDependency:
        case PackageRole::BuildDependency:
        case PackageRole::CheckDependency:
            return true;
    }
    return false;
}

bool is_dependency_role(PackageRole role) noexcept {
    return role == PackageRole::RuntimeDependency ||
           role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency;
}

#ifdef MOGUET_ENABLE_INVOCATION_TRANSACTION_LEDGER_TEST_HOOKS
bool transaction_requested_packages_are_valid(
    const std::vector<std::string>& requested_package_names) {
    if(requested_package_names.empty()) return false;
    std::set<std::string> unique_names;
    return std::all_of(
        requested_package_names.begin(), requested_package_names.end(),
        [&unique_names](const std::string& package_name) {
            return is_valid_package_name(package_name) &&
                   unique_names.insert(package_name).second;
        });
}
#endif

bool has_role(
    const std::vector<PackageRole>& roles, PackageRole expected) {
    return std::find(roles.begin(), roles.end(), expected) != roles.end();
}

std::string resolved_candidate_package_name(
    const ResolvedDependencyCandidate& candidate) {
    return std::visit(
        [](const auto& resolved) {
            using Candidate = std::decay_t<decltype(resolved)>;
            if constexpr(std::is_same_v<
                             Candidate,
                             ProviderResolvedDependencyCandidate>) {
                return resolved.provider.package_name;
            } else {
                return resolved.package_name;
            }
        },
        candidate);
}

std::optional<std::string> potential_edge_package_name(
    const BuildPlanDependencyEdge& edge) {
    // POLICY(#404): name is used only to conservatively widen the set of
    // edges that must be checked. Exact source/PackageBase/child identity is
    // verified below; a name-only match can only downgrade evidence.
    if(edge.resolved_candidate.has_value()) {
        return resolved_candidate_package_name(
            edge.resolved_candidate.value());
    }
    if(edge.resolved_provider.has_value()) {
        return edge.resolved_provider->package_name;
    }
    return edge.resolved_package_name;
}

bool same_source_provenance(
    const PackageSourceIdentity& lhs,
    const PackageSourceIdentity& rhs) noexcept {
    if(lhs.kind() != rhs.kind()) return false;
    if(lhs.kind() != PackageSourceKind::Repository) return true;
    const std::string* lhs_repository = lhs.repository_name();
    const std::string* rhs_repository = rhs.repository_name();
    return lhs_repository != nullptr && rhs_repository != nullptr &&
           *lhs_repository == *rhs_repository;
}

std::optional<PackageBaseIdentity> work_item_source_identity(
    const ProductionSourceBuildWorkItem& work_item) {
    if(work_item.required_target_provenance ==
       RequiredTargetProvenance::AurBuildPlanProjection) {
        if(!work_item.request.aur_review_identity.has_value() ||
           work_item.request.aur_review_identity->package_base() !=
               work_item.request.checkout_name) {
            return std::nullopt;
        }
        return work_item.request.aur_review_identity;
    }

    if(work_item.required_target_provenance !=
           RequiredTargetProvenance::RepositoryExactPackageProjection ||
       !work_item.repository_identity.has_value() ||
       work_item.request.git_url.empty()) {
        return std::nullopt;
    }
    const ResolvedRepositorySourceBuildIdentity& repository =
        work_item.repository_identity.value();
    if(repository.package_base() != work_item.request.checkout_name) {
        return std::nullopt;
    }
    try {
        return PackageBaseIdentity::make(
            PackageSourceIdentity::repository(
                repository.exact_package().repository_name,
                SourceLocationIdentity::known_git_remote(
                    work_item.request.git_url)),
            repository.package_base());
    } catch(const std::invalid_argument&) {
        return std::nullopt;
    }
}

bool same_required_target(
    const RequiredPackageArtifactTarget& lhs,
    const RequiredPackageArtifactTarget& rhs) noexcept {
    return lhs.package_base == rhs.package_base &&
           lhs.package_name == rhs.package_name &&
           lhs.desired_reason == rhs.desired_reason;
}

struct PreparedInvocationProjection {
    bool complete = false;
    std::vector<PackageBaseIdentity> package_bases;
};

PreparedInvocationProjection project_prepared_invocation(
    const BuildPlanArtifactTargetProjectionSuccess& artifact_projection,
    const CleanupInvocationLifecycleEvidence& lifecycle) {
    PreparedInvocationProjection projection;
    const PreparedProductionSourceBuildInvocation* invocation =
        lifecycle.invocation();
    if(invocation == nullptr ||
       invocation->work_items.size() !=
           artifact_projection.build_units.size()) {
        return projection;
    }

    projection.package_bases.reserve(invocation->work_items.size());
    for(std::size_t index = 0; index < invocation->work_items.size();
        ++index) {
        const ProductionSourceBuildWorkItem& work_item =
            invocation->work_items[index];
        const ProjectedBuildPlanArtifactTargets& build_unit =
            artifact_projection.build_units[index];
        if(build_unit.build_plan_order_index != index ||
           build_unit.package_base != work_item.request.checkout_name ||
           build_unit.required_targets.size() !=
               work_item.required_targets.size()) {
            return projection;
        }
        for(std::size_t target_index = 0;
            target_index < build_unit.required_targets.size();
            ++target_index) {
            if(!same_required_target(
                   build_unit.required_targets[target_index],
                   work_item.required_targets[target_index])) {
                return projection;
            }
        }

        std::optional<PackageBaseIdentity> source =
            work_item_source_identity(work_item);
        if(!source.has_value()) return projection;
        if(std::any_of(
               projection.package_bases.begin(),
               projection.package_bases.end(),
               [&source](const PackageBaseIdentity& existing) {
                   return existing.package_base() ==
                          source->package_base();
               })) {
            return projection;
        }
        projection.package_bases.push_back(std::move(source.value()));
    }
    projection.complete = true;
    return projection;
}

const PackageBaseIdentity* find_package_base_source(
    const std::vector<PackageBaseIdentity>& package_bases,
    const std::string& package_base) noexcept {
    const PackageBaseIdentity* match = nullptr;
    for(const PackageBaseIdentity& candidate : package_bases) {
        if(candidate.package_base() != package_base) continue;
        if(match != nullptr) return nullptr;
        match = &candidate;
    }
    return match;
}

std::optional<SourceAwarePackageIdentity> bind_prepared_source_identity(
    const SourceAwarePackageIdentity& observed,
    const std::vector<PackageBaseIdentity>& package_bases) {
    const PackageBaseIdentity& observed_base =
        observed.package().package_base();
    const PackageBaseIdentity* prepared_base = find_package_base_source(
        package_bases, observed_base.package_base());
    if(prepared_base == nullptr ||
       !same_source_provenance(
           prepared_base->source(), observed_base.source())) {
        return std::nullopt;
    }

    try {
        return SourceAwarePackageIdentity::make(
            PackageChildIdentity::make(
                *prepared_base,
                observed.package().package_name()),
            observed.source_revision(), observed.package_version(),
            observed.architecture());
    } catch(const std::invalid_argument&) {
        return std::nullopt;
    }
}

std::optional<SourceAwarePackageIdentity> project_dependency_identity(
    const ResolvedDependencyCandidate& candidate) {
    SourcePackageIdentityProjectionResult projection =
        project_dependency_source_package_identity(candidate);
    const SourcePackageIdentityProjectionSuccess* success =
        projection.success();
    if(success == nullptr || success->identities.size() != 1) {
        return std::nullopt;
    }
    return success->identities.front();
}

const PlannedPackageTarget* find_unique_package_target(
    const BuildPlan& plan,
    const std::string& package_name,
    const std::string& package_base) noexcept {
    const PlannedPackageTarget* match = nullptr;
    for(const PlannedPackageTarget& target : plan.package_targets) {
        if(target.package_name != package_name ||
           target.package_base != package_base) {
            continue;
        }
        if(match != nullptr) return nullptr;
        match = &target;
    }
    return match;
}

std::optional<PackageChildIdentity> project_requiring_package(
    const BuildPlanDependencyEdge& edge,
    const std::vector<PackageBaseIdentity>& package_bases) {
    const PackageBaseIdentity* source = find_package_base_source(
        package_bases, edge.parent_package_base);
    if(source == nullptr) return std::nullopt;
    try {
        return PackageChildIdentity::make(
            *source, edge.parent_package_name);
    } catch(const std::invalid_argument&) {
        return std::nullopt;
    }
}

bool requirement_matches_raw_specification(
    const DependencyRequirement& requirement,
    const std::string& specification) {
    return std::visit(
        [&specification](const auto& typed_requirement) {
            return typed_requirement.raw_specification() ==
                   specification;
        },
        requirement);
}

std::string requirement_identity(
    const DependencyRequirement& requirement) {
    return std::visit(
        [](const auto& typed_requirement) {
            using Requirement =
                std::decay_t<decltype(typed_requirement)>;
            if constexpr(std::is_same_v<
                             Requirement,
                             ConsumerDependencyRequirement>) {
                return typed_requirement.package_name();
            } else {
                return typed_requirement.soname();
            }
        },
        requirement);
}

bool successful_constraint_evaluation(
    const BuildPlanDependencyEdge& edge) noexcept {
    if(!edge.constraint_evaluation.has_value()) return false;
    switch(edge.constraint_evaluation->satisfaction()) {
        case ConstraintSatisfaction::Unconstrained:
        case ConstraintSatisfaction::Satisfied:
            return true;
        case ConstraintSatisfaction::Unsatisfied:
        case ConstraintSatisfaction::Unknown:
        case ConstraintSatisfaction::Invalid:
        case ConstraintSatisfaction::Conflicting:
            return false;
    }
    return false;
}

bool repository_provider_provenance_is_complete(
    const ProvidedDependency& provider,
    const BuildPlan& plan) noexcept {
    const auto* repository =
        std::get_if<RepositoryProviderOrigin>(&provider.origin);
    if(repository == nullptr) return true;
    if(!repository->configured_order.has_value() ||
       !plan.configured_repository_order.has_value()) {
        return false;
    }
    const std::size_t order = repository->configured_order.value();
    return order < plan.configured_repository_order->size() &&
           (*plan.configured_repository_order)[order] ==
               repository->repository_name;
}

bool provider_association_is_complete(
    const BuildPlanDependencyEdge& edge,
    const DependencyRequirement& requirement,
    const SourceAwarePackageIdentity& candidate,
    const BuildPlan& plan,
    std::size_t edge_index,
    std::vector<CleanupLifecycleProjectionIssue>& issues) {
    if(edge.kind != DependencyKind::Provided ||
       !edge.resolved_provider.has_value() ||
       !edge.resolved_candidate.has_value()) {
        return false;
    }
    const auto* resolved = std::get_if<ProviderResolvedDependencyCandidate>(
        &edge.resolved_candidate.value());
    if(resolved == nullptr ||
       !same_provider_identity(
           edge.resolved_provider.value(), resolved->provider)) {
        return false;
    }

    const ProvidedDependency& provider = edge.resolved_provider.value();
    if(provider.package_name != candidate.package().package_name() ||
       provider.package_base.empty()) {
        if(provider.package_base.empty()) {
            add_issue(
                issues,
                CleanupLifecycleProjectionIssueKind::
                    RepositoryProviderPackageBaseUnavailable,
                edge_index, provider.package_name);
        }
        return false;
    }
    if(!repository_provider_provenance_is_complete(provider, plan)) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                RepositoryProviderProvenanceIncomplete,
            edge_index, provider.package_name);
        return false;
    }

    const std::string required_identity = requirement_identity(requirement);
    if(provider.provided_dependency_name != required_identity) return false;
    if(provider.constraint_metadata.has_value()) {
        const ProviderCapability& capability =
            provider.constraint_metadata->provided_capability;
        if(capability.package_name() != required_identity ||
           capability.raw_specification() !=
               provider.provided_dependency_specification) {
            return false;
        }
    } else if(provider.provided_dependency_specification.empty()) {
        return false;
    }
    return true;
}

bool direct_association_is_complete(
    const BuildPlanDependencyEdge& edge,
    const DependencyRequirement& requirement,
    const SourceAwarePackageIdentity& resolved) {
    if(edge.kind == DependencyKind::Provided ||
       edge.resolved_provider.has_value() ||
       !edge.resolved_candidate.has_value() ||
       edge.resolved_package_name != resolved.package().package_name()) {
        return false;
    }
    const auto* consumer =
        std::get_if<ConsumerDependencyRequirement>(&requirement);
    if(consumer == nullptr ||
       consumer->package_name() != resolved.package().package_name()) {
        return false;
    }
    if(edge.kind == DependencyKind::Aur || edge.kind == DependencyKind::Local) {
        return edge.resolved_package_base ==
               resolved.package().package_base().package_base();
    }
    return true;
}

bool work_item_outcome_succeeded(
    const ProductionSourceBuildWorkItemOutcome& outcome) noexcept {
    return outcome.status ==
               ProductionSourceBuildWorkItemStatus::Succeeded &&
           outcome.production_outcome.has_value() &&
           outcome.production_outcome->build_outcome ==
               ProductionSourceBuildCommandOutcome::Succeeded &&
           outcome.production_outcome->install_outcome ==
               ProductionSourceInstallOutcome::Succeeded;
}

bool lifecycle_result_matches_invocation(
    const CleanupInvocationLifecycleEvidence& lifecycle,
    bool require_all_succeeded) noexcept {
    const PreparedProductionSourceBuildInvocation* invocation =
        lifecycle.invocation();
    const ProductionSourceBuildInvocationResult* result =
        lifecycle.result();
    if(invocation == nullptr || result == nullptr ||
       invocation->work_items.size() != result->work_items.size()) {
        return false;
    }
    for(std::size_t index = 0; index < invocation->work_items.size();
        ++index) {
        if(invocation->work_items[index].request.checkout_name !=
           result->work_items[index].package_base) {
            return false;
        }
        if(require_all_succeeded &&
           !work_item_outcome_succeeded(result->work_items[index])) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> build_plan_order_index(
    const BuildPlan& plan,
    const std::string& package_base) noexcept {
    std::optional<std::size_t> match;
    for(std::size_t index = 0; index < plan.order.size(); ++index) {
        if(plan.order[index].package_base != package_base) continue;
        if(match.has_value()) return std::nullopt;
        match = index;
    }
    return match;
}

CleanupSharedRequirementState project_shared_requirement(
    const BuildPlan& plan,
    const std::vector<std::size_t>& relevant_edge_indices,
    const std::vector<PackageRole>& observed_roles,
    CleanupCorrelationCoverage coverage,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    bool prepared_invocation_complete) noexcept {
    if(has_role(observed_roles, PackageRole::Root) ||
       has_role(observed_roles, PackageRole::RuntimeDependency)) {
        return CleanupSharedRequirementState::StillRequired;
    }

    if(lifecycle.boundary() == CleanupLifecycleBoundary::AfterWorkItem &&
       prepared_invocation_complete &&
       lifecycle_result_matches_invocation(lifecycle, false) &&
       lifecycle.completed_work_item_index().has_value()) {
        const std::size_t completed =
            lifecycle.completed_work_item_index().value();
        const ProductionSourceBuildInvocationResult* result =
            lifecycle.result();
        if(result != nullptr && completed < result->work_items.size() &&
           work_item_outcome_succeeded(result->work_items[completed])) {
            for(const std::size_t edge_index : relevant_edge_indices) {
                if(edge_index >= plan.dependency_edges.size()) continue;
                const std::optional<std::size_t> requiring_index =
                    build_plan_order_index(
                        plan,
                        plan.dependency_edges[edge_index]
                            .parent_package_base);
                if(requiring_index.has_value() &&
                   requiring_index.value() > completed) {
                    return CleanupSharedRequirementState::StillRequired;
                }
            }
        }
        return CleanupSharedRequirementState::Unknown;
    }

    if(lifecycle.boundary() ==
           CleanupLifecycleBoundary::AfterSuccessfulInvocation &&
       prepared_invocation_complete &&
       coverage == CleanupCorrelationCoverage::Complete &&
       lifecycle_result_matches_invocation(lifecycle, true) &&
       !observed_roles.empty() &&
       std::all_of(
           observed_roles.begin(), observed_roles.end(),
           [](PackageRole role) {
               return role == PackageRole::BuildDependency ||
                      role == PackageRole::CheckDependency;
           })) {
        return CleanupSharedRequirementState::NoLongerRequired;
    }

    return CleanupSharedRequirementState::Unknown;
}

CleanupCorrelationCoverage initial_correlation_coverage(
    const BuildPlan& plan,
    std::vector<CleanupLifecycleProjectionIssue>& issues) {
    const PlanStateProjection state = project_build_plan_state(plan);
    if(state.completeness == PlanCompleteness::Unknown ||
       state.provider_decision == ProviderDecision::Unavailable) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                BuildPlanCoverageUnknown);
        return CleanupCorrelationCoverage::Unknown;
    }
    if(state.construction != PlanConstruction::Constructed ||
       state.completeness != PlanCompleteness::Complete ||
       state.provider_decision == ProviderDecision::Ambiguous ||
       state.provider_decision == ProviderDecision::Cancelled) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::BuildPlanIncomplete);
        return CleanupCorrelationCoverage::Incomplete;
    }
    return CleanupCorrelationCoverage::Complete;
}

void downgrade_complete_coverage(
    CleanupCorrelationCoverage& coverage) noexcept {
    if(coverage == CleanupCorrelationCoverage::Complete) {
        coverage = CleanupCorrelationCoverage::Incomplete;
    }
}

bool is_valid_cleanup_policy_completeness(
    CleanupPolicyMetadataCompleteness completeness) noexcept {
    switch(completeness) {
        case CleanupPolicyMetadataCompleteness::Complete:
        case CleanupPolicyMetadataCompleteness::Incomplete:
        case CleanupPolicyMetadataCompleteness::Failed:
            return true;
    }
    return false;
}

bool is_valid_cleanup_policy_observation(
    CleanupPolicyAuthorityObservation observation) noexcept {
    switch(observation) {
        case CleanupPolicyAuthorityObservation::Present:
        case CleanupPolicyAuthorityObservation::Absent:
        case CleanupPolicyAuthorityObservation::NotObserved:
        case CleanupPolicyAuthorityObservation::Unavailable:
            return true;
    }
    return false;
}

bool is_valid_cleanup_policy_evaluation(
    CleanupPolicyCandidateEvaluation evaluation) noexcept {
    switch(evaluation) {
        case CleanupPolicyCandidateEvaluation::Protected:
        case CleanupPolicyCandidateEvaluation::NotProtected:
        case CleanupPolicyCandidateEvaluation::NotEvaluated:
            return true;
    }
    return false;
}

bool cleanup_policy_strings_are_unique_and_nonempty(
    const std::vector<std::string>& values) {
    std::set<std::string> observed;
    return std::all_of(
        values.begin(), values.end(),
        [&observed](const std::string& value) {
            return !value.empty() && observed.insert(value).second;
        });
}

bool cleanup_policy_meta_package_shape_is_valid(
    const CleanupPolicyMetaPackageMetadata& metadata,
    CleanupPolicyAuthorityKind expected_kind) {
    if(metadata.authority_kind != expected_kind ||
       metadata.package_name != "base-devel" ||
       metadata.version.empty() || metadata.dependencies.empty() ||
       !cleanup_policy_strings_are_unique_and_nonempty(
           metadata.dependencies)) {
        return false;
    }

    if(expected_kind ==
       CleanupPolicyAuthorityKind::InstalledBaseDevelMetaPackage) {
        return !metadata.configured_repository_order.has_value() &&
               !metadata.repository_name.has_value();
    }
    return metadata.configured_repository_order.has_value() &&
           metadata.repository_name.has_value() &&
           !metadata.repository_name->empty();
}

bool cleanup_policy_group_shape_is_valid(
    const CleanupPolicyGroupMetadata& group) {
    if(group.group_name != "base-devel" ||
       group.repository_order.empty() ||
       group.repositories_with_group.empty() || group.members.empty() ||
       !cleanup_policy_strings_are_unique_and_nonempty(
           group.repository_order) ||
       !cleanup_policy_strings_are_unique_and_nonempty(
           group.repositories_with_group)) {
        return false;
    }

    for(const std::string& repository : group.repositories_with_group) {
        if(std::find(
               group.repository_order.begin(),
               group.repository_order.end(), repository) ==
           group.repository_order.end()) {
            return false;
        }
    }

    std::set<std::string> observed_members;
    for(const CleanupPolicyGroupMemberMetadata& member : group.members) {
        if(member.configured_repository_order >=
               group.repository_order.size() ||
           member.repository_name !=
               group.repository_order[member.configured_repository_order] ||
           !is_valid_package_name(member.package_name) ||
           !observed_members.insert(member.package_name).second) {
            return false;
        }
    }
    return true;
}

bool cleanup_policy_authority_shape_is_valid(
    const CleanupPolicyAuthorityEvidence& authority,
    CleanupPolicyAuthorityKind expected_kind) noexcept {
    if(authority.authority_kind != expected_kind ||
       !is_valid_cleanup_policy_observation(authority.observation) ||
       !is_valid_cleanup_policy_completeness(
           authority.inventory_completeness) ||
       !is_valid_cleanup_policy_evaluation(
           authority.candidate_evaluation) ||
       !is_valid_cleanup_policy_completeness(
           authority.evaluation_completeness)) {
        return false;
    }
    if((authority.candidate_evaluation ==
        CleanupPolicyCandidateEvaluation::NotEvaluated) ==
       (authority.evaluation_completeness ==
        CleanupPolicyMetadataCompleteness::Complete)) {
        return false;
    }
    if(authority.observation != CleanupPolicyAuthorityObservation::Present &&
       authority.candidate_evaluation !=
           CleanupPolicyCandidateEvaluation::NotEvaluated) {
        return false;
    }

    const bool is_group_authority =
        expected_kind ==
        CleanupPolicyAuthorityKind::BaseDevelGroupCompatibility;
    if(authority.observation == CleanupPolicyAuthorityObservation::Present &&
       authority.inventory_completeness ==
           CleanupPolicyMetadataCompleteness::Complete) {
        if(is_group_authority) {
            return authority.group.has_value() &&
                   authority.meta_packages.empty() &&
                   cleanup_policy_group_shape_is_valid(
                       authority.group.value());
        }
        return !authority.meta_packages.empty() &&
               !authority.group.has_value() &&
               std::all_of(
                   authority.meta_packages.begin(),
                   authority.meta_packages.end(),
                   [expected_kind](
                       const CleanupPolicyMetaPackageMetadata& metadata) {
                       return cleanup_policy_meta_package_shape_is_valid(
                           metadata, expected_kind);
                   });
    }

    if(authority.observation != CleanupPolicyAuthorityObservation::Present) {
        return authority.meta_packages.empty() &&
               !authority.group.has_value();
    }
    if(is_group_authority) {
        return authority.meta_packages.empty() &&
               (!authority.group.has_value() ||
                cleanup_policy_group_shape_is_valid(
                    authority.group.value()));
    }
    return !authority.group.has_value() &&
           std::all_of(
               authority.meta_packages.begin(),
               authority.meta_packages.end(),
               [expected_kind](
                   const CleanupPolicyMetaPackageMetadata& metadata) {
                   return cleanup_policy_meta_package_shape_is_valid(
                       metadata, expected_kind);
               });
}

bool cleanup_policy_authority_is_complete_absence(
    const CleanupPolicyAuthorityEvidence& authority) noexcept {
    return authority.observation ==
               CleanupPolicyAuthorityObservation::Absent &&
           authority.inventory_completeness ==
               CleanupPolicyMetadataCompleteness::Complete &&
           authority.candidate_evaluation ==
               CleanupPolicyCandidateEvaluation::NotEvaluated;
}

bool cleanup_policy_authority_has_complete_protection(
    const CleanupPolicyAuthorityEvidence& authority) noexcept {
    return authority.observation ==
               CleanupPolicyAuthorityObservation::Present &&
           authority.candidate_evaluation ==
               CleanupPolicyCandidateEvaluation::Protected &&
           authority.evaluation_completeness ==
               CleanupPolicyMetadataCompleteness::Complete;
}

bool cleanup_policy_authority_has_complete_negative(
    const CleanupPolicyAuthorityEvidence& authority) noexcept {
    return authority.observation ==
               CleanupPolicyAuthorityObservation::Present &&
           authority.inventory_completeness ==
               CleanupPolicyMetadataCompleteness::Complete &&
           authority.candidate_evaluation ==
               CleanupPolicyCandidateEvaluation::NotProtected &&
           authority.evaluation_completeness ==
               CleanupPolicyMetadataCompleteness::Complete;
}

template <typename Issue>
void add_unique_typed_issue(
    std::vector<Issue>& issues, Issue issue) {
    if(std::find(issues.begin(), issues.end(), issue) == issues.end()) {
        issues.push_back(issue);
    }
}

template <typename Issue>
void canonicalize_typed_issues(std::vector<Issue>& issues) {
    std::sort(issues.begin(), issues.end(), [](Issue lhs, Issue rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
}

bool root_sets_match(
    const std::vector<RootTargetIdentity>& lhs,
    const std::vector<RootTargetIdentity>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(
        lhs.begin(), lhs.end(), [&rhs](const RootTargetIdentity& root) {
            return std::find(rhs.begin(), rhs.end(), root) != rhs.end();
        });
}

bool role_sets_match(
    const std::vector<PackageRole>& lhs,
    const std::vector<PackageRole>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(
        lhs.begin(), lhs.end(), [&rhs](PackageRole role) {
            return std::find(rhs.begin(), rhs.end(), role) != rhs.end();
        });
}

bool edge_index_sets_match(
    const std::vector<std::size_t>& lhs,
    const std::vector<std::size_t>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(
        lhs.begin(), lhs.end(), [&rhs](std::size_t edge_index) {
            return std::find(rhs.begin(), rhs.end(), edge_index) !=
                   rhs.end();
        });
}

bool edge_indices_are_unique(
    const std::vector<std::size_t>& edge_indices) {
    std::set<std::size_t> observed;
    return std::all_of(
        edge_indices.begin(), edge_indices.end(),
        [&observed](std::size_t edge_index) {
            return observed.insert(edge_index).second;
        });
}

bool same_cleanup_package_identity(
    const SourceAwarePackageIdentity& lhs,
    const SourceAwarePackageIdentity& rhs) noexcept {
    return lhs.package() == rhs.package() &&
           lhs.package_version() == rhs.package_version();
}

bool is_build_or_check_role(PackageRole role) noexcept {
    return role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency;
}

bool edge_resolves_to_aur_source(
    const BuildPlanDependencyEdge& edge) noexcept {
    if(!edge.resolved_candidate.has_value()) return false;
    if(std::holds_alternative<AurResolvedDependencyCandidate>(
           edge.resolved_candidate.value())) {
        return true;
    }
    const auto* provider =
        std::get_if<ProviderResolvedDependencyCandidate>(
            &edge.resolved_candidate.value());
    return provider != nullptr &&
           std::holds_alternative<AurProviderOrigin>(
               provider->provider.origin);
}

bool current_package_matches_provider(
    const CleanupCurrentPackageEvidence& current,
    const ProvidedDependency& provider) noexcept {
    return current.state == CleanupInstalledState::Present &&
           current.verification == CleanupEvidenceVerification::Verified &&
           current.metadata.has_value() &&
           current.metadata->name == provider.package_name &&
           current.metadata->reason == InstalledPackageReason::Dependency &&
           provider.package_version.has_value() &&
           current.metadata->version == provider.package_version.value();
}

std::optional<std::size_t> find_work_item_for_edge(
    const PreparedProductionSourceBuildInvocation& invocation,
    std::size_t edge_index,
    bool selected_repository_provider) noexcept {
    std::optional<std::size_t> match;
    for(std::size_t work_item_index = 0;
        work_item_index < invocation.work_items.size();
        ++work_item_index) {
        const ProductionSourceBuildWorkItem& work_item =
            invocation.work_items[work_item_index];
        const auto& edge_indices = selected_repository_provider
                                       ? work_item.selected_repository_provider_edge_indices
                                       : work_item.build_plan_dependency_edge_indices;
        if(std::find(edge_indices.begin(), edge_indices.end(), edge_index) ==
           edge_indices.end()) {
            continue;
        }
        if(match.has_value()) return std::nullopt;
        match = work_item_index;
    }
    return match;
}

bool selected_provider_decision_matches_plan(
    const BuildPlan& plan,
    const BuildPlanDependencyEdge& edge,
    const ProvidedDependency& provider) {
    std::size_t matches = 0;
    for(const BuildPlanProvidedDependency& selected : plan.provided) {
        if(selected.dependency != edge.dependency_spec ||
           selected.resolution != edge.provider_resolution ||
           !same_provider_identity(selected.provider, provider)) {
            continue;
        }
        ++matches;
    }
    return matches == 1;
}

std::vector<std::string> selected_provider_package_names(
    const std::vector<ProvidedDependency>& providers) {
    std::vector<std::string> names;
    names.reserve(providers.size());
    for(const ProvidedDependency& provider : providers) {
        names.push_back(provider.package_name);
    }
    return names;
}

bool provider_sets_match(
    const std::vector<ProvidedDependency>& lhs,
    const std::vector<ProvidedDependency>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    for(std::size_t index = 0; index < lhs.size(); ++index) {
        if(lhs[index] != rhs[index]) return false;
    }
    return true;
}

std::optional<std::size_t> exact_work_item_index_for_package_base(
    const std::vector<CleanupInvocationWorkItemEvidence>& work_items,
    const std::string& package_base) noexcept {
    std::optional<std::size_t> match;
    for(const CleanupInvocationWorkItemEvidence& work_item : work_items) {
        if(work_item.package_base.package_base() != package_base) continue;
        if(match.has_value()) return std::nullopt;
        match = work_item.work_item_index;
    }
    return match;
}

bool route_kind_is_valid(CleanupRouteKind route_kind) noexcept {
    switch(route_kind) {
        case CleanupRouteKind::RemoteAurSourceBuild:
        case CleanupRouteKind::LocalSourceBuild:
        case CleanupRouteKind::Upgrade:
        case CleanupRouteKind::UpgradeAur:
        case CleanupRouteKind::UpgradeAll:
        case CleanupRouteKind::StandaloneRepositorySourceBuild:
        case CleanupRouteKind::MakepkgSyncDependencies:
        case CleanupRouteKind::Unknown:
            return true;
    }
    return false;
}

} // namespace

CleanupInvocationLifecycleEvidence::CleanupInvocationLifecycleEvidence(
    CleanupLifecycleBoundary boundary,
    std::optional<std::reference_wrapper<
        const PreparedProductionSourceBuildInvocation>>
        invocation,
    std::optional<std::reference_wrapper<
        const ProductionSourceBuildInvocationResult>>
        result,
    std::optional<std::size_t> completed_work_item_index) noexcept
    : boundary_(boundary), invocation_(invocation), result_(result),
      completed_work_item_index_(completed_work_item_index) {
}

CleanupInvocationLifecycleEvidence
CleanupInvocationLifecycleEvidence::unknown() noexcept {
    return CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary::Unknown, std::nullopt, std::nullopt,
        std::nullopt);
}

CleanupInvocationLifecycleEvidence
CleanupInvocationLifecycleEvidence::before_build_completion(
    const PreparedProductionSourceBuildInvocation& invocation) noexcept {
    return CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary::BeforeBuildCompletion,
        std::cref(invocation), std::nullopt, std::nullopt);
}

CleanupInvocationLifecycleEvidence
CleanupInvocationLifecycleEvidence::after_work_item(
    const PreparedProductionSourceBuildInvocation& invocation,
    const ProductionSourceBuildInvocationResult& result,
    std::size_t completed_work_item_index) noexcept {
    return CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary::AfterWorkItem,
        std::cref(invocation), std::cref(result),
        completed_work_item_index);
}

CleanupInvocationLifecycleEvidence
CleanupInvocationLifecycleEvidence::after_successful_invocation(
    const PreparedProductionSourceBuildInvocation& invocation,
    const ProductionSourceBuildInvocationResult& result) noexcept {
    return CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary::AfterSuccessfulInvocation,
        std::cref(invocation), std::cref(result), std::nullopt);
}

CleanupInvocationLifecycleEvidence
CleanupInvocationLifecycleEvidence::after_invocation_completion(
    const PreparedProductionSourceBuildInvocation& invocation,
    const ProductionSourceBuildInvocationResult& result) noexcept {
    return CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary::AfterInvocationCompletion,
        std::cref(invocation), std::cref(result), std::nullopt);
}

CleanupLifecycleBoundary
CleanupInvocationLifecycleEvidence::boundary() const noexcept {
    return boundary_;
}

const PreparedProductionSourceBuildInvocation*
CleanupInvocationLifecycleEvidence::invocation() const noexcept {
    return invocation_.has_value() ? &invocation_->get() : nullptr;
}

const ProductionSourceBuildInvocationResult*
CleanupInvocationLifecycleEvidence::result() const noexcept {
    return result_.has_value() ? &result_->get() : nullptr;
}

const std::optional<std::size_t>&
CleanupInvocationLifecycleEvidence::completed_work_item_index()
    const noexcept {
    return completed_work_item_index_;
}

CleanupSourceArtifactCorrelationEvidence::
    CleanupSourceArtifactCorrelationEvidence(
        CleanupEvidenceCompleteness completeness,
        CleanupInvocationIdentity invocation,
        SourceArtifactInstallCausalEvidence causal_evidence,
        std::size_t work_item_index,
        std::string package_base,
        std::vector<CleanupSourceArtifactSelectedCorrelation>
            selected_artifacts,
        std::vector<std::string> actual_install_set,
        std::vector<CleanupSourceArtifactCorrelationIssueKind> issues) noexcept
    : completeness_(completeness), invocation_(std::move(invocation)),
      causal_evidence_(std::move(causal_evidence)),
      work_item_index_(work_item_index),
      package_base_(std::move(package_base)),
      selected_artifacts_(std::move(selected_artifacts)),
      actual_install_set_(std::move(actual_install_set)),
      issues_(std::move(issues)) {
}

CleanupEvidenceCompleteness
CleanupSourceArtifactCorrelationEvidence::completeness() const noexcept {
    return completeness_;
}

const CleanupInvocationIdentity&
CleanupSourceArtifactCorrelationEvidence::invocation() const noexcept {
    return invocation_;
}

const SourceArtifactInstallCausalEvidence&
CleanupSourceArtifactCorrelationEvidence::causal_evidence() const noexcept {
    return causal_evidence_;
}

std::size_t CleanupSourceArtifactCorrelationEvidence::work_item_index()
    const noexcept {
    return work_item_index_;
}

const std::string&
CleanupSourceArtifactCorrelationEvidence::package_base() const noexcept {
    return package_base_;
}

const std::vector<CleanupSourceArtifactSelectedCorrelation>&
CleanupSourceArtifactCorrelationEvidence::selected_artifacts()
    const noexcept {
    return selected_artifacts_;
}

const std::vector<std::string>&
CleanupSourceArtifactCorrelationEvidence::actual_install_set()
    const noexcept {
    return actual_install_set_;
}

const std::vector<CleanupSourceArtifactCorrelationIssueKind>&
CleanupSourceArtifactCorrelationEvidence::issues() const noexcept {
    return issues_;
}

CleanupSourceArtifactCorrelationEvidence
correlate_source_artifact_install_to_build_plan(
    const CleanupInvocationIdentity& invocation_identity,
    const BuildPlan& plan,
    const PreparedProductionSourceBuildInvocation& invocation,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const SourceArtifactInstallCausalEvidence& causal_evidence) {
    using Issue = CleanupSourceArtifactCorrelationIssueKind;
    std::vector<Issue> issues;
    std::vector<CleanupSourceArtifactSelectedCorrelation>
        selected_correlations;

    if(causal_evidence.work_item().invocation != invocation_identity) {
        add_unique_typed_issue(issues, Issue::InvocationMismatch);
    }
    if(lifecycle.invocation() != &invocation || lifecycle.result() == nullptr) {
        add_unique_typed_issue(issues, Issue::LifecycleInvocationMismatch);
    }

    const std::size_t work_item_index =
        causal_evidence.work_item().work_item_index;
    const ProductionSourceBuildWorkItem* work_item = nullptr;
    const ProductionSourceBuildWorkItemOutcome* work_item_outcome = nullptr;
    if(work_item_index >= invocation.work_items.size() ||
       lifecycle.result() == nullptr ||
       work_item_index >= lifecycle.result()->work_items.size()) {
        add_unique_typed_issue(issues, Issue::WorkItemIndexOutOfRange);
    } else {
        work_item = &invocation.work_items[work_item_index];
        work_item_outcome = &lifecycle.result()->work_items[work_item_index];
        if(work_item->request.checkout_name !=
               causal_evidence.work_item().package_base ||
           work_item_outcome->package_base !=
               causal_evidence.work_item().package_base) {
            add_unique_typed_issue(
                issues, Issue::WorkItemPackageBaseMismatch);
        }
        if(!work_item_outcome_succeeded(*work_item_outcome)) {
            add_unique_typed_issue(
                issues, Issue::WorkItemOutcomeNotSuccessful);
        }
    }

    BuildPlanArtifactTargetProjectionResult artifact_projection =
        project_build_plan_required_artifact_targets(plan);
    PreparedInvocationProjection prepared_projection;
    if(artifact_projection.success() != nullptr) {
        prepared_projection = project_prepared_invocation(
            *artifact_projection.success(), lifecycle);
    }

    std::set<std::string> correlated_install_names;
    for(const SourceArtifactInstallCorrelatedSelectedArtifact& selected :
        causal_evidence.selected_artifacts()) {
        CleanupSourceArtifactSelectedCorrelation correlation{
            selected, {}};
        if(work_item == nullptr) {
            add_unique_typed_issue(issues, Issue::SelectedArtifactMissing);
            selected_correlations.push_back(std::move(correlation));
            continue;
        }

        const auto target = std::find_if(
            work_item->required_targets.begin(),
            work_item->required_targets.end(),
            [&selected](const RequiredPackageArtifactTarget& candidate) {
                return candidate.package_name ==
                           selected.expected_identity.package().package_name() &&
                       candidate.package_base ==
                           selected.expected_identity.package()
                               .package_base()
                               .package_base() &&
                       candidate.desired_reason == selected.desired_reason;
            });
        if(target == work_item->required_targets.end()) {
            add_unique_typed_issue(
                issues, Issue::SelectedArtifactTargetMismatch);
        }

        if(selected.build_plan_dependency_edge_indices.empty()) {
            add_unique_typed_issue(
                issues, Issue::DependencyEdgeAttributionMissing);
        }
        if(!edge_indices_are_unique(
               selected.build_plan_dependency_edge_indices)) {
            add_unique_typed_issue(
                issues, Issue::DependencyEdgeAttributionMismatch);
        }

        std::vector<std::size_t> expected_edge_indices;
        for(const std::size_t edge_index :
            work_item->build_plan_dependency_edge_indices) {
            if(edge_index >= plan.dependency_edges.size()) continue;
            const BuildPlanDependencyEdge& edge =
                plan.dependency_edges[edge_index];
            if(!is_build_or_check_role(edge.role) ||
               !edge.resolved_candidate.has_value()) {
                continue;
            }
            std::optional<SourceAwarePackageIdentity> edge_identity =
                project_dependency_identity(
                    edge.resolved_candidate.value());
            if(edge_identity.has_value()) {
                edge_identity = bind_prepared_source_identity(
                    edge_identity.value(),
                    prepared_projection.package_bases);
            }
            if(edge_identity.has_value() &&
               same_cleanup_package_identity(
                   edge_identity.value(), selected.expected_identity)) {
                expected_edge_indices.push_back(edge_index);
            }
        }
        if(!edge_index_sets_match(
               expected_edge_indices,
               selected.build_plan_dependency_edge_indices)) {
            add_unique_typed_issue(
                issues, Issue::DependencyEdgeAttributionMismatch);
        }

        std::vector<RootTargetIdentity> correlated_roots;
        std::vector<PackageRole> correlated_roles;
        for(const std::size_t edge_index :
            selected.build_plan_dependency_edge_indices) {
            if(std::find(
                   work_item->build_plan_dependency_edge_indices.begin(),
                   work_item->build_plan_dependency_edge_indices.end(),
                   edge_index) ==
               work_item->build_plan_dependency_edge_indices.end()) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeAttributionMismatch);
                continue;
            }
            if(edge_index >= plan.dependency_edges.size()) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeOutOfRange);
                continue;
            }
            const BuildPlanDependencyEdge& edge =
                plan.dependency_edges[edge_index];
            if(!edge.requirement.has_value() ||
               !requirement_matches_raw_specification(
                   edge.requirement.value(), edge.dependency_spec)) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeRequirementIncomplete);
                continue;
            }
            if(!successful_constraint_evaluation(edge)) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeConstraintIncomplete);
                continue;
            }

            std::optional<SourceAwarePackageIdentity> edge_identity;
            if(edge.resolved_candidate.has_value()) {
                edge_identity = project_dependency_identity(
                    edge.resolved_candidate.value());
                if(edge_identity.has_value()) {
                    edge_identity = bind_prepared_source_identity(
                        edge_identity.value(),
                        prepared_projection.package_bases);
                }
            }
            if(!edge_identity.has_value() ||
               !same_cleanup_package_identity(
                   edge_identity.value(), selected.expected_identity)) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeIdentityMismatch);
                continue;
            }

            bool association_complete = false;
            std::optional<CleanupProviderCorrelation> provider_correlation;
            if(edge.kind == DependencyKind::Provided &&
               edge.resolved_provider.has_value() &&
               edge.resolved_candidate.has_value()) {
                const auto* resolved =
                    std::get_if<ProviderResolvedDependencyCandidate>(
                        &edge.resolved_candidate.value());
                const ProvidedDependency& provider =
                    edge.resolved_provider.value();
                association_complete =
                    resolved != nullptr &&
                    std::holds_alternative<AurProviderOrigin>(
                        provider.origin) &&
                    same_provider_identity(provider, resolved->provider) &&
                    provider.package_name ==
                        selected.expected_identity.package().package_name() &&
                    provider.package_base ==
                        selected.expected_identity.package()
                            .package_base()
                            .package_base() &&
                    provider.provided_dependency_name ==
                        requirement_identity(edge.requirement.value()) &&
                    selected_provider_decision_matches_plan(
                        plan, edge, provider);
                if(association_complete &&
                   provider.constraint_metadata.has_value()) {
                    const ProviderCapability& capability =
                        provider.constraint_metadata->provided_capability;
                    association_complete =
                        capability.package_name() ==
                            provider.provided_dependency_name &&
                        capability.raw_specification() ==
                            provider.provided_dependency_specification;
                }
                provider_correlation = CleanupProviderCorrelation{
                    provider, edge.provider_resolution};
            } else {
                association_complete = direct_association_is_complete(
                    edge, edge.requirement.value(),
                    edge_identity.value());
            }
            if(!association_complete) {
                add_unique_typed_issue(
                    issues, Issue::DependencyEdgeIdentityMismatch);
                continue;
            }
            if(!is_build_or_check_role(edge.role) ||
               std::find(
                   selected.dependency_roles.begin(),
                   selected.dependency_roles.end(), edge.role) ==
                   selected.dependency_roles.end()) {
                add_unique_typed_issue(
                    issues, Issue::DependencyRoleMismatch);
                continue;
            }

            const PlannedPackageTarget* parent =
                find_unique_package_target(
                    plan, edge.parent_package_name,
                    edge.parent_package_base);
            const auto requiring_package = project_requiring_package(
                edge, prepared_projection.package_bases);
            if(parent == nullptr || parent->roots.empty() ||
               !requiring_package.has_value()) {
                add_unique_typed_issue(
                    issues, Issue::RequestedRootAttributionMismatch);
                continue;
            }
            if(std::find(
                   correlated_roles.begin(), correlated_roles.end(),
                   edge.role) == correlated_roles.end()) {
                correlated_roles.push_back(edge.role);
            }
            for(const RootTargetIdentity& root : parent->roots) {
                if(std::find(
                       correlated_roots.begin(), correlated_roots.end(),
                       root) == correlated_roots.end()) {
                    correlated_roots.push_back(root);
                }
                correlation.dependency_correlations.push_back(
                    CleanupPackageCorrelation{
                        root,
                        selected.expected_identity.package(),
                        edge.role,
                        CleanupDependencyEdgeCorrelation{
                            edge_index,
                            requiring_package.value(),
                            edge.requirement.value(),
                            provider_correlation},
                        CleanupEvidenceVerification::Verified});
            }
        }

        const PlannedPackageTarget* selected_target =
            find_unique_package_target(
                plan,
                selected.expected_identity.package().package_name(),
                selected.expected_identity.package()
                    .package_base()
                    .package_base());
        if(selected_target == nullptr ||
           !role_sets_match(
               correlated_roles, selected.dependency_roles)) {
            add_unique_typed_issue(issues, Issue::DependencyRoleMismatch);
        }
        if(selected_target == nullptr ||
           !root_sets_match(
               correlated_roots, selected.requested_roots) ||
           !root_sets_match(
               selected_target->roots, selected.requested_roots)) {
            add_unique_typed_issue(
                issues, Issue::RequestedRootAttributionMismatch);
        }
        correlated_install_names.insert(
            selected.expected_identity.package().package_name());
        selected_correlations.push_back(std::move(correlation));
    }

    for(const std::string& installed : causal_evidence.actual_install_set()) {
        if(correlated_install_names.find(installed) ==
           correlated_install_names.end()) {
            add_unique_typed_issue(
                issues, Issue::UncorrelatedActualInstall);
        }
    }

    canonicalize_typed_issues(issues);
    const CleanupEvidenceCompleteness completeness =
        issues.empty() ? CleanupEvidenceCompleteness::Complete
                       : CleanupEvidenceCompleteness::Incomplete;
    return CleanupSourceArtifactCorrelationEvidence(
        completeness,
        invocation_identity, causal_evidence, work_item_index,
        causal_evidence.work_item().package_base,
        std::move(selected_correlations),
        causal_evidence.actual_install_set(), std::move(issues));
}

CleanupSelectedProviderCorrelationEvidence::
    CleanupSelectedProviderCorrelationEvidence(
        CleanupEvidenceCompleteness completeness,
        CleanupInvocationIdentity invocation,
        std::size_t work_item_index,
        std::size_t build_plan_edge_index,
        std::optional<ProvidedDependency> provider,
        std::optional<SourceAwarePackageIdentity> package_identity,
        CleanupCurrentPackageEvidence current_package,
        std::optional<std::string> transaction_token,
        std::vector<std::string> actual_install_set,
        SelectedRepositoryProviderTrustedReceiptExecutionResult execution,
        std::vector<CleanupSelectedProviderCorrelationIssueKind>
            issues) noexcept
    : completeness_(completeness), invocation_(std::move(invocation)),
      work_item_index_(work_item_index),
      build_plan_edge_index_(build_plan_edge_index),
      provider_(std::move(provider)),
      package_identity_(std::move(package_identity)),
      current_package_(std::move(current_package)),
      transaction_token_(std::move(transaction_token)),
      actual_install_set_(std::move(actual_install_set)),
      execution_(std::move(execution)),
      issues_(std::move(issues)) {
}

CleanupEvidenceCompleteness
CleanupSelectedProviderCorrelationEvidence::completeness() const noexcept {
    return completeness_;
}

const CleanupInvocationIdentity&
CleanupSelectedProviderCorrelationEvidence::invocation() const noexcept {
    return invocation_;
}

std::size_t
CleanupSelectedProviderCorrelationEvidence::work_item_index() const noexcept {
    return work_item_index_;
}

std::size_t
CleanupSelectedProviderCorrelationEvidence::build_plan_edge_index()
    const noexcept {
    return build_plan_edge_index_;
}

const std::optional<ProvidedDependency>&
CleanupSelectedProviderCorrelationEvidence::provider() const noexcept {
    return provider_;
}

const std::optional<SourceAwarePackageIdentity>&
CleanupSelectedProviderCorrelationEvidence::package_identity()
    const noexcept {
    return package_identity_;
}

const CleanupCurrentPackageEvidence&
CleanupSelectedProviderCorrelationEvidence::current_package()
    const noexcept {
    return current_package_;
}

const std::optional<std::string>&
CleanupSelectedProviderCorrelationEvidence::transaction_token()
    const noexcept {
    return transaction_token_;
}

const std::vector<std::string>&
CleanupSelectedProviderCorrelationEvidence::actual_install_set()
    const noexcept {
    return actual_install_set_;
}

const SelectedRepositoryProviderTrustedReceiptExecutionResult&
CleanupSelectedProviderCorrelationEvidence::execution() const noexcept {
    return execution_;
}

const std::vector<CleanupSelectedProviderCorrelationIssueKind>&
CleanupSelectedProviderCorrelationEvidence::issues() const noexcept {
    return issues_;
}

CleanupSelectedProviderCorrelationEvidence
correlate_selected_repository_provider_to_build_plan(
    const CleanupInvocationIdentity& invocation_identity,
    const BuildPlan& plan,
    const PreparedProductionSourceBuildInvocation& invocation,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    std::size_t build_plan_edge_index,
    const CleanupCurrentPackageEvidence& current_package,
    const SelectedRepositoryProviderTrustedReceiptExecutionResult& execution) {
    using Issue = CleanupSelectedProviderCorrelationIssueKind;
    std::vector<Issue> issues;
    std::optional<ProvidedDependency> provider;
    std::optional<SourceAwarePackageIdentity> package_identity;
    std::optional<std::string> transaction_token;
    std::vector<std::string> actual_install_set;
    std::size_t work_item_index = 0;

    if(!execution.invocation_identity.has_value()) {
        add_unique_typed_issue(issues, Issue::InvocationIdentityMissing);
    } else if(execution.invocation_identity.value() != invocation_identity) {
        add_unique_typed_issue(issues, Issue::InvocationMismatch);
    }
    if(lifecycle.invocation() != &invocation) {
        add_unique_typed_issue(issues, Issue::LifecycleInvocationMismatch);
    }
    const PlanStateProjection plan_state = project_build_plan_state(plan);
    if(plan_state.construction != PlanConstruction::Constructed ||
       plan_state.completeness != PlanCompleteness::Complete ||
       (plan_state.provider_decision != ProviderDecision::Selected &&
        plan_state.provider_decision != ProviderDecision::Unique)) {
        add_unique_typed_issue(issues, Issue::BuildPlanIncomplete);
    }

    const BuildPlanDependencyEdge* edge = nullptr;
    if(build_plan_edge_index >= plan.dependency_edges.size()) {
        add_unique_typed_issue(issues, Issue::DependencyEdgeOutOfRange);
    } else {
        edge = &plan.dependency_edges[build_plan_edge_index];
        if(edge->kind != DependencyKind::Provided ||
           edge->provider_resolution !=
               ProviderResolutionKind::UserSelected ||
           !edge->resolved_provider.has_value() ||
           !edge->resolved_candidate.has_value() ||
           !std::holds_alternative<RepositoryProviderOrigin>(
               edge->resolved_provider->origin)) {
            add_unique_typed_issue(
                issues, Issue::DependencyEdgeNotSelectedRepositoryProvider);
        } else {
            provider = edge->resolved_provider;
            package_identity = project_dependency_identity(
                edge->resolved_candidate.value());
        }
    }

    const std::optional<std::size_t> attributed_work_item =
        find_work_item_for_edge(invocation, build_plan_edge_index, true);
    if(!attributed_work_item.has_value()) {
        add_unique_typed_issue(
            issues, Issue::DependencyEdgeAttributionMismatch);
    } else {
        work_item_index = attributed_work_item.value();
        if(edge != nullptr &&
           invocation.work_items[work_item_index].request.checkout_name !=
               edge->parent_package_base) {
            add_unique_typed_issue(
                issues, Issue::DependencyEdgeAttributionMismatch);
        }
    }

    if(edge != nullptr && provider.has_value()) {
        const auto* repository =
            std::get_if<RepositoryProviderOrigin>(&provider->origin);
        if(provider->package_base.empty()) {
            add_unique_typed_issue(issues, Issue::PackageBaseMissing);
        }
        if(repository == nullptr ||
           !repository_provider_provenance_is_complete(*provider, plan)) {
            add_unique_typed_issue(
                issues, Issue::RepositoryProvenanceMismatch);
        }
        const auto* resolved =
            std::get_if<ProviderResolvedDependencyCandidate>(
                &edge->resolved_candidate.value());
        if(resolved == nullptr ||
           !same_provider_identity(*provider, resolved->provider)) {
            add_unique_typed_issue(issues, Issue::ProviderIdentityMismatch);
        }
        if(!edge->requirement.has_value() ||
           !requirement_matches_raw_specification(
               edge->requirement.value(), edge->dependency_spec) ||
           provider->provided_dependency_name !=
               (edge->requirement.has_value()
                    ? requirement_identity(edge->requirement.value())
                    : std::string{})) {
            add_unique_typed_issue(issues, Issue::RequirementMismatch);
        }
        if(!provider->constraint_metadata.has_value() ||
           provider->constraint_metadata->provided_capability.package_name() !=
               provider->provided_dependency_name ||
           provider->constraint_metadata->provided_capability
                   .raw_specification() !=
               provider->provided_dependency_specification ||
           !successful_constraint_evaluation(*edge)) {
            add_unique_typed_issue(
                issues, Issue::ProvidedCapabilityMismatch);
        }
        if(!selected_provider_decision_matches_plan(
               plan, *edge, *provider)) {
            add_unique_typed_issue(issues, Issue::SelectedDecisionMismatch);
        }
        if(!current_package_matches_provider(current_package, *provider)) {
            if(current_package.state != CleanupInstalledState::Present ||
               current_package.verification !=
                   CleanupEvidenceVerification::Verified ||
               !current_package.metadata.has_value()) {
                add_unique_typed_issue(
                    issues, Issue::CurrentPackageIdentityIncomplete);
            } else {
                add_unique_typed_issue(
                    issues, Issue::CurrentPackageIdentityMismatch);
            }
        }
    }

    if(execution.transaction.status !=
           SelectedRepositoryProviderTransactionStatus::Succeeded ||
       execution.transaction.command_exit_status != std::optional<int>{0} ||
       !provider_sets_match(
           execution.transaction.selected_providers,
           invocation.selected_repository_providers)) {
        add_unique_typed_issue(issues, Issue::TransactionResultMismatch);
    }
    if(provider.has_value() &&
       std::count_if(
           execution.transaction.selected_providers.begin(),
           execution.transaction.selected_providers.end(),
           [&provider](const ProvidedDependency& selected) {
               return same_provider_identity(selected, provider.value());
           }) != 1) {
        add_unique_typed_issue(issues, Issue::ProviderIdentityMismatch);
    }

    if(!execution.receipt_capture.has_value()) {
        add_unique_typed_issue(issues, Issue::ReceiptCaptureMissing);
    } else {
        const TrustedAlpmReceiptCaptureResult& capture =
            execution.receipt_capture.value();
        if(capture.status != TrustedAlpmReceiptCaptureStatus::Complete ||
           capture.pacman_exit_status != std::optional<int>{0}) {
            add_unique_typed_issue(
                issues, Issue::ReceiptCaptureIncomplete);
        }
        if(capture.transaction_ledger.transactions.size() != 1) {
            add_unique_typed_issue(issues, Issue::TransactionLedgerMismatch);
        } else {
            const InvocationDependencyTransaction& transaction =
                capture.transaction_ledger.transactions.front();
            transaction_token = transaction.transaction_token;
            for(const PacmanInstalledPackageReceipt& installed :
                transaction.receipt.newly_installed_packages()) {
                actual_install_set.push_back(installed.package_name);
            }
            if(transaction.owner !=
                   InvocationDependencyTransactionOwner::
                       SelectedRepositoryProvider ||
               !is_valid_pacman_transaction_token(
                   transaction.transaction_token) ||
               transaction.command_outcome !=
                   InvocationDependencyTransactionCommandOutcome::Succeeded ||
               transaction.requested_package_names !=
                   selected_provider_package_names(
                       invocation.selected_repository_providers)) {
                add_unique_typed_issue(
                    issues, Issue::TransactionLedgerMismatch);
            }
            if(!transaction.receipt.is_complete_for(
                   transaction.transaction_token,
                   InvocationDependencyTransactionOwner::
                       SelectedRepositoryProvider)) {
                add_unique_typed_issue(issues, Issue::ReceiptMismatch);
            }
            if(provider.has_value() &&
               !transaction.receipt.contains_newly_installed_package(
                   provider->package_name)) {
                add_unique_typed_issue(issues, Issue::ProviderNotInstalled);
            }
            const std::vector<std::string> selected_names =
                selected_provider_package_names(
                    invocation.selected_repository_providers);
            for(const std::string& installed : actual_install_set) {
                if(std::find(
                       selected_names.begin(), selected_names.end(),
                       installed) == selected_names.end()) {
                    add_unique_typed_issue(
                        issues, Issue::UncorrelatedActualInstall);
                }
            }
        }
    }

    canonicalize_typed_issues(issues);
    const CleanupEvidenceCompleteness completeness =
        issues.empty() ? CleanupEvidenceCompleteness::Complete
                       : CleanupEvidenceCompleteness::Incomplete;
    return CleanupSelectedProviderCorrelationEvidence(
        completeness,
        invocation_identity, work_item_index, build_plan_edge_index,
        std::move(provider), std::move(package_identity), current_package,
        std::move(transaction_token), std::move(actual_install_set), execution,
        std::move(issues));
}

CleanupInvocationEvidence::CleanupInvocationEvidence(
    CleanupRouteKind route_kind,
    CleanupRouteAuthority route_authority,
    CleanupEvidenceCompleteness completeness,
    std::optional<CleanupInvocationIdentity> invocation,
    std::optional<BuildPlan> build_plan,
    std::vector<RootTargetIdentity> roots,
    std::vector<CleanupInvocationWorkItemEvidence> work_items,
    std::vector<PackageBaseIdentity> package_bases,
    std::vector<CleanupSourceArtifactCorrelationEvidence>
        source_artifact_evidence,
    std::vector<CleanupSelectedProviderCorrelationEvidence>
        selected_provider_evidence,
    CleanupLifecycleBoundary lifecycle_boundary,
    std::optional<std::size_t> completed_work_item_index,
    std::vector<CleanupInvocationEvidenceIssueKind> issues) noexcept
    : route_kind_(route_kind), route_authority_(route_authority),
      completeness_(completeness), invocation_(std::move(invocation)),
      build_plan_(std::move(build_plan)), roots_(std::move(roots)),
      work_items_(std::move(work_items)),
      package_bases_(std::move(package_bases)),
      source_artifact_evidence_(std::move(source_artifact_evidence)),
      selected_provider_evidence_(std::move(selected_provider_evidence)),
      lifecycle_boundary_(lifecycle_boundary),
      completed_work_item_index_(completed_work_item_index),
      issues_(std::move(issues)) {
}

CleanupRouteKind CleanupInvocationEvidence::route_kind() const noexcept {
    return route_kind_;
}

CleanupRouteAuthority
CleanupInvocationEvidence::route_authority() const noexcept {
    return route_authority_;
}

CleanupEvidenceCompleteness
CleanupInvocationEvidence::completeness() const noexcept {
    return completeness_;
}

const std::optional<CleanupInvocationIdentity>&
CleanupInvocationEvidence::invocation() const noexcept {
    return invocation_;
}

const BuildPlan* CleanupInvocationEvidence::build_plan() const noexcept {
    return build_plan_.has_value() ? &build_plan_.value() : nullptr;
}

const std::vector<RootTargetIdentity>&
CleanupInvocationEvidence::roots() const noexcept {
    return roots_;
}

const std::vector<CleanupInvocationWorkItemEvidence>&
CleanupInvocationEvidence::work_items() const noexcept {
    return work_items_;
}

const std::vector<PackageBaseIdentity>&
CleanupInvocationEvidence::package_bases() const noexcept {
    return package_bases_;
}

const std::vector<CleanupSourceArtifactCorrelationEvidence>&
CleanupInvocationEvidence::source_artifact_evidence() const noexcept {
    return source_artifact_evidence_;
}

const std::vector<CleanupSelectedProviderCorrelationEvidence>&
CleanupInvocationEvidence::selected_provider_evidence() const noexcept {
    return selected_provider_evidence_;
}

CleanupLifecycleBoundary
CleanupInvocationEvidence::lifecycle_boundary() const noexcept {
    return lifecycle_boundary_;
}

const std::optional<std::size_t>&
CleanupInvocationEvidence::completed_work_item_index() const noexcept {
    return completed_work_item_index_;
}

const std::vector<CleanupInvocationEvidenceIssueKind>&
CleanupInvocationEvidence::issues() const noexcept {
    return issues_;
}

CleanupRouteAuthority project_cleanup_route_authority(
    CleanupRouteKind route_kind,
    CleanupEvidenceCompleteness completeness) noexcept {
    switch(route_kind) {
        case CleanupRouteKind::RemoteAurSourceBuild:
            return completeness == CleanupEvidenceCompleteness::Complete
                       ? CleanupRouteAuthority::Complete
                       : CleanupRouteAuthority::Unknown;
        case CleanupRouteKind::LocalSourceBuild:
        case CleanupRouteKind::Upgrade:
        case CleanupRouteKind::UpgradeAur:
        case CleanupRouteKind::UpgradeAll:
            return CleanupRouteAuthority::Unsupported;
        case CleanupRouteKind::StandaloneRepositorySourceBuild:
        case CleanupRouteKind::MakepkgSyncDependencies:
        case CleanupRouteKind::Unknown:
            return CleanupRouteAuthority::Unknown;
    }
    return CleanupRouteAuthority::Unknown;
}

CleanupInvocationEvidence project_cleanup_route_evidence(
    CleanupRouteKind route_kind) {
    using Issue = CleanupInvocationEvidenceIssueKind;
    std::vector<Issue> issues;
    if(!route_kind_is_valid(route_kind) ||
       route_kind == CleanupRouteKind::Unknown ||
       route_kind == CleanupRouteKind::StandaloneRepositorySourceBuild ||
       route_kind == CleanupRouteKind::MakepkgSyncDependencies ||
       route_kind == CleanupRouteKind::RemoteAurSourceBuild) {
        add_unique_typed_issue(issues, Issue::RouteAuthorityUnknown);
    } else {
        add_unique_typed_issue(issues, Issue::UnsupportedRoute);
    }
    return CleanupInvocationEvidence(
        route_kind,
        project_cleanup_route_authority(
            route_kind, CleanupEvidenceCompleteness::Unknown),
        CleanupEvidenceCompleteness::Unknown, std::nullopt,
        std::nullopt, {}, {}, {}, {}, {},
        CleanupLifecycleBoundary::Unknown, std::nullopt,
        std::move(issues));
}

CleanupInvocationEvidence aggregate_remote_aur_cleanup_invocation_evidence(
    const CleanupInvocationIdentity& invocation_identity,
    const PreparedRemoteSourceBuild& prepared,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    std::vector<CleanupSourceArtifactCorrelationEvidence>
        source_artifact_evidence,
    std::vector<CleanupSelectedProviderCorrelationEvidence>
        selected_provider_evidence) {
    using Issue = CleanupInvocationEvidenceIssueKind;
    std::vector<Issue> issues;
    std::optional<BuildPlan> plan;
    std::vector<RootTargetIdentity> roots;
    std::vector<CleanupInvocationWorkItemEvidence> work_items;
    std::vector<PackageBaseIdentity> package_bases;

    if(prepared.source.source_kind() != SourceBuildSourceKind::Aur ||
       !prepared.aur_build_plan.has_value()) {
        add_unique_typed_issue(issues, Issue::RemoteAurBuildPlanMissing);
    } else {
        plan = prepared.aur_build_plan;
        roots = plan->root_targets;
        const PlanStateProjection plan_state = project_build_plan_state(*plan);
        if(plan_state.construction != PlanConstruction::Constructed ||
           plan_state.completeness != PlanCompleteness::Complete ||
           (plan_state.provider_decision != ProviderDecision::Unique &&
            plan_state.provider_decision != ProviderDecision::Selected)) {
            add_unique_typed_issue(issues, Issue::BuildPlanIncomplete);
        }
    }

    if(roots.empty()) {
        add_unique_typed_issue(issues, Issue::RootInventoryIncomplete);
    } else {
        std::set<std::pair<std::size_t, std::string>> root_identities;
        std::set<std::size_t> root_indices;
        std::set<std::string> root_names;
        for(const RootTargetIdentity& root : roots) {
            if(!is_valid_package_name(root.requested_name) ||
               !root_identities.emplace(
                                   root.invocation_index, root.requested_name)
                    .second ||
               !root_indices.insert(root.invocation_index).second ||
               !root_names.insert(root.requested_name).second) {
                add_unique_typed_issue(
                    issues, Issue::RootInventoryIncomplete);
            }
            if(plan.has_value()) {
                const std::size_t root_target_matches =
                    static_cast<std::size_t>(std::count_if(
                        plan->package_targets.begin(),
                        plan->package_targets.end(),
                        [&root](const PlannedPackageTarget& target) {
                            return target.package_name ==
                                       root.requested_name &&
                                   has_role(
                                       target.roles,
                                       PackageRole::Root) &&
                                   std::find(
                                       target.roots.begin(),
                                       target.roots.end(), root) !=
                                       target.roots.end();
                        }));
                if(root_target_matches != 1) {
                    add_unique_typed_issue(
                        issues, Issue::RootInventoryIncomplete);
                }
            }
        }
    }

    const ProductionSourceBuildInvocationResult* result = lifecycle.result();
    if(lifecycle.invocation() != &prepared.invocation || result == nullptr ||
       result->work_items.size() != prepared.invocation.work_items.size()) {
        add_unique_typed_issue(issues, Issue::WorkItemInventoryIncomplete);
    }

    if(plan.has_value()) {
        BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan.value());
        if(projection.success() == nullptr) {
            add_unique_typed_issue(
                issues, Issue::WorkItemInventoryIncomplete);
        } else {
            PreparedInvocationProjection prepared_projection =
                project_prepared_invocation(*projection.success(), lifecycle);
            if(!prepared_projection.complete) {
                add_unique_typed_issue(
                    issues, Issue::WorkItemInventoryIncomplete);
            } else {
                package_bases = prepared_projection.package_bases;
            }
        }
    }
    if(package_bases.size() != prepared.invocation.work_items.size()) {
        add_unique_typed_issue(
            issues, Issue::PackageBaseInventoryIncomplete);
    }

    if(result != nullptr &&
       result->work_items.size() == prepared.invocation.work_items.size()) {
        work_items.reserve(prepared.invocation.work_items.size());
        for(std::size_t index = 0;
            index < prepared.invocation.work_items.size(); ++index) {
            const ProductionSourceBuildWorkItem& prepared_work_item =
                prepared.invocation.work_items[index];
            const ProductionSourceBuildWorkItemOutcome& outcome =
                result->work_items[index];
            std::optional<PackageBaseIdentity> package_base =
                work_item_source_identity(prepared_work_item);
            if(!package_base.has_value() ||
               outcome.package_base !=
                   prepared_work_item.request.checkout_name) {
                add_unique_typed_issue(
                    issues, Issue::PackageBaseInventoryIncomplete);
                continue;
            }
            work_items.push_back(CleanupInvocationWorkItemEvidence{
                index,
                std::move(package_base.value()),
                prepared_work_item.required_targets,
                prepared_work_item.build_plan_dependency_edge_indices,
                prepared_work_item
                    .selected_repository_provider_edge_indices,
                outcome});
            switch(outcome.status) {
                case ProductionSourceBuildWorkItemStatus::Succeeded:
                    if(!work_item_outcome_succeeded(outcome)) {
                        add_unique_typed_issue(
                            issues, Issue::WorkItemOutcomeUnknown);
                    }
                    break;
                case ProductionSourceBuildWorkItemStatus::Failed:
                    add_unique_typed_issue(
                        issues, Issue::WorkItemOutcomeFailed);
                    break;
                case ProductionSourceBuildWorkItemStatus::NotAttempted:
                    add_unique_typed_issue(
                        issues, Issue::WorkItemOutcomeNotAttempted);
                    break;
            }
        }
    }

    if(lifecycle.boundary() !=
       CleanupLifecycleBoundary::AfterSuccessfulInvocation) {
        add_unique_typed_issue(issues, Issue::WorkItemInventoryIncomplete);
    }

    std::set<std::size_t> expected_source_edges;
    std::set<std::size_t> expected_provider_edges;
    if(plan.has_value()) {
        std::set<std::size_t> attributed_source_edges;
        std::set<std::size_t> attributed_provider_edges;
        for(const ProductionSourceBuildWorkItem& work_item :
            prepared.invocation.work_items) {
            for(const std::size_t edge_index :
                work_item.build_plan_dependency_edge_indices) {
                if(edge_index >= plan->dependency_edges.size() ||
                   !attributed_source_edges.insert(edge_index).second ||
                   !edge_resolves_to_aur_source(
                       plan->dependency_edges[edge_index])) {
                    add_unique_typed_issue(
                        issues, Issue::WorkItemInventoryIncomplete);
                }
            }
            for(const std::size_t edge_index :
                work_item.selected_repository_provider_edge_indices) {
                if(edge_index >= plan->dependency_edges.size() ||
                   !attributed_provider_edges.insert(edge_index).second) {
                    add_unique_typed_issue(
                        issues, Issue::WorkItemInventoryIncomplete);
                }
            }
        }
        for(std::size_t edge_index = 0;
            edge_index < plan->dependency_edges.size(); ++edge_index) {
            const BuildPlanDependencyEdge& edge =
                plan->dependency_edges[edge_index];
            if(edge.kind == DependencyKind::Repo) {
                add_unique_typed_issue(
                    issues, Issue::MakepkgSyncDependenciesUnowned);
            }
            if(edge.kind == DependencyKind::Provided &&
               edge.resolved_provider.has_value() &&
               std::holds_alternative<RepositoryProviderOrigin>(
                   edge.resolved_provider->origin) &&
               edge.provider_resolution !=
                   ProviderResolutionKind::UserSelected) {
                add_unique_typed_issue(
                    issues, Issue::MakepkgSyncDependenciesUnowned);
            }
            if(edge.kind == DependencyKind::Provided &&
               edge.resolved_provider.has_value() &&
               std::holds_alternative<RepositoryProviderOrigin>(
                   edge.resolved_provider->origin) &&
               edge.provider_resolution ==
                   ProviderResolutionKind::UserSelected) {
                expected_provider_edges.insert(edge_index);
                if(attributed_provider_edges.find(edge_index) ==
                       attributed_provider_edges.end() ||
                   !find_work_item_for_edge(
                        prepared.invocation, edge_index, true)
                        .has_value()) {
                    add_unique_typed_issue(
                        issues, Issue::WorkItemInventoryIncomplete);
                }
            }
            if(is_build_or_check_role(edge.role) &&
               edge_resolves_to_aur_source(edge)) {
                expected_source_edges.insert(edge_index);
                if(attributed_source_edges.find(edge_index) ==
                       attributed_source_edges.end() ||
                   !find_work_item_for_edge(
                        prepared.invocation, edge_index, false)
                        .has_value()) {
                    add_unique_typed_issue(
                        issues, Issue::WorkItemInventoryIncomplete);
                }
            }
        }
    }

    std::set<std::size_t> observed_source_edges;
    for(const CleanupSourceArtifactCorrelationEvidence& evidence :
        source_artifact_evidence) {
        if(evidence.invocation() != invocation_identity) {
            add_unique_typed_issue(
                issues, Issue::CorrelationInvocationMismatch);
        }
        if(evidence.completeness() !=
           CleanupEvidenceCompleteness::Complete) {
            add_unique_typed_issue(issues, Issue::CorrelationIncomplete);
        }
        if(evidence.work_item_index() >=
               prepared.invocation.work_items.size() ||
           prepared.invocation
                   .work_items[evidence.work_item_index()]
                   .request.checkout_name != evidence.package_base()) {
            add_unique_typed_issue(
                issues, Issue::SourceArtifactCorrelationUnexpected);
        }
        std::set<std::size_t> evidence_source_edges;
        for(const CleanupSourceArtifactSelectedCorrelation& selected :
            evidence.selected_artifacts()) {
            for(const CleanupPackageCorrelation& correlation :
                selected.dependency_correlations) {
                if(correlation.dependency_edge.has_value()) {
                    const std::size_t edge_index =
                        correlation.dependency_edge
                            ->build_plan_edge_index;
                    evidence_source_edges.insert(edge_index);
                    if(plan.has_value() &&
                       edge_index < plan->dependency_edges.size() &&
                       plan->dependency_edges[edge_index]
                           .resolved_candidate.has_value()) {
                        std::optional<SourceAwarePackageIdentity>
                            current_identity = project_dependency_identity(
                                plan->dependency_edges[edge_index]
                                    .resolved_candidate.value());
                        if(current_identity.has_value()) {
                            current_identity = bind_prepared_source_identity(
                                current_identity.value(), package_bases);
                        }
                        if(!current_identity.has_value() ||
                           !same_cleanup_package_identity(
                               current_identity.value(),
                               selected.artifact.expected_identity)) {
                            add_unique_typed_issue(
                                issues, Issue::CorrelationIncomplete);
                        }
                    } else {
                        add_unique_typed_issue(
                            issues, Issue::CorrelationIncomplete);
                    }
                }
            }
        }
        for(const std::size_t edge_index : evidence_source_edges) {
            if(!observed_source_edges.insert(edge_index).second) {
                add_unique_typed_issue(
                    issues, Issue::SourceArtifactCorrelationUnexpected);
            }
        }
        if(std::find(
               evidence.issues().begin(), evidence.issues().end(),
               CleanupSourceArtifactCorrelationIssueKind::
                   UncorrelatedActualInstall) != evidence.issues().end()) {
            add_unique_typed_issue(
                issues, Issue::UncorrelatedActualInstall);
        }
    }
    for(const std::size_t edge_index : expected_source_edges) {
        if(observed_source_edges.find(edge_index) ==
           observed_source_edges.end()) {
            add_unique_typed_issue(
                issues, Issue::SourceArtifactCorrelationMissing);
        }
    }
    for(const std::size_t edge_index : observed_source_edges) {
        if(expected_source_edges.find(edge_index) ==
           expected_source_edges.end()) {
            add_unique_typed_issue(
                issues, Issue::SourceArtifactCorrelationUnexpected);
        }
    }

    std::set<std::size_t> observed_provider_edges;
    for(const CleanupSelectedProviderCorrelationEvidence& evidence :
        selected_provider_evidence) {
        if(evidence.invocation() != invocation_identity) {
            add_unique_typed_issue(
                issues, Issue::CorrelationInvocationMismatch);
        }
        if(evidence.completeness() !=
           CleanupEvidenceCompleteness::Complete) {
            add_unique_typed_issue(issues, Issue::CorrelationIncomplete);
        }
        if(!observed_provider_edges
                .insert(evidence.build_plan_edge_index())
                .second) {
            add_unique_typed_issue(
                issues, Issue::SelectedProviderCorrelationUnexpected);
        }
        if(plan.has_value() && evidence.provider().has_value() &&
           evidence.build_plan_edge_index() <
               plan->dependency_edges.size()) {
            const BuildPlanDependencyEdge& current_edge =
                plan->dependency_edges[evidence.build_plan_edge_index()];
            if(!current_edge.resolved_provider.has_value() ||
               current_edge.resolved_provider.value() !=
                   evidence.provider().value()) {
                add_unique_typed_issue(
                    issues, Issue::CorrelationIncomplete);
            }
        } else {
            add_unique_typed_issue(issues, Issue::CorrelationIncomplete);
        }
        if(std::find(
               evidence.issues().begin(), evidence.issues().end(),
               CleanupSelectedProviderCorrelationIssueKind::
                   UncorrelatedActualInstall) != evidence.issues().end()) {
            add_unique_typed_issue(
                issues, Issue::UncorrelatedActualInstall);
        }
    }
    for(const std::size_t edge_index : expected_provider_edges) {
        if(observed_provider_edges.find(edge_index) ==
           observed_provider_edges.end()) {
            add_unique_typed_issue(
                issues, Issue::SelectedProviderCorrelationMissing);
        }
    }
    for(const std::size_t edge_index : observed_provider_edges) {
        if(expected_provider_edges.find(edge_index) ==
           expected_provider_edges.end()) {
            add_unique_typed_issue(
                issues, Issue::SelectedProviderCorrelationUnexpected);
        }
    }

    canonicalize_typed_issues(issues);
    const CleanupEvidenceCompleteness completeness =
        issues.empty() ? CleanupEvidenceCompleteness::Complete
                       : CleanupEvidenceCompleteness::Incomplete;
    return CleanupInvocationEvidence(
        CleanupRouteKind::RemoteAurSourceBuild,
        project_cleanup_route_authority(
            CleanupRouteKind::RemoteAurSourceBuild, completeness),
        completeness, invocation_identity, std::move(plan),
        std::move(roots), std::move(work_items),
        std::move(package_bases), std::move(source_artifact_evidence),
        std::move(selected_provider_evidence), lifecycle.boundary(),
        lifecycle.completed_work_item_index(), std::move(issues));
}

CleanupSharedRequirementState project_shared_requirement(
    const CleanupInvocationEvidence& evidence,
    const SourceAwarePackageIdentity& package) noexcept {
    const BuildPlan* plan = evidence.build_plan();
    if(plan == nullptr) return CleanupSharedRequirementState::Unknown;

    std::vector<PackageRole> roles;
    std::vector<std::size_t> relevant_edges;
    const PlannedPackageTarget* target = find_unique_package_target(
        *plan, package.package().package_name(),
        package.package().package_base().package_base());
    if(target != nullptr) {
        roles = target->roles;
    }

    for(std::size_t edge_index = 0;
        edge_index < plan->dependency_edges.size(); ++edge_index) {
        const BuildPlanDependencyEdge& edge =
            plan->dependency_edges[edge_index];
        if(!edge.resolved_candidate.has_value()) continue;
        std::optional<SourceAwarePackageIdentity> resolved =
            project_dependency_identity(edge.resolved_candidate.value());
        if(resolved.has_value()) {
            std::optional<SourceAwarePackageIdentity> prepared_identity =
                bind_prepared_source_identity(
                    resolved.value(), evidence.package_bases());
            if(prepared_identity.has_value()) {
                resolved = std::move(prepared_identity);
            } else if(resolved->package().package_base().source().kind() !=
                      PackageSourceKind::Repository) {
                resolved.reset();
            }
        }
        if(!resolved.has_value() ||
           !same_cleanup_package_identity(resolved.value(), package)) {
            continue;
        }
        relevant_edges.push_back(edge_index);
        if(std::find(roles.begin(), roles.end(), edge.role) == roles.end()) {
            roles.push_back(edge.role);
        }
    }

    if(has_role(roles, PackageRole::Root) ||
       has_role(roles, PackageRole::RuntimeDependency)) {
        return CleanupSharedRequirementState::StillRequired;
    }

    if(evidence.work_items().size() != plan->order.size()) {
        return CleanupSharedRequirementState::Unknown;
    }
    for(std::size_t index = 0; index < evidence.work_items().size(); ++index) {
        if(evidence.work_items()[index].work_item_index != index) {
            return CleanupSharedRequirementState::Unknown;
        }
    }

    if(evidence.lifecycle_boundary() ==
           CleanupLifecycleBoundary::AfterWorkItem &&
       evidence.completed_work_item_index().has_value()) {
        const std::size_t completed =
            evidence.completed_work_item_index().value();
        if(completed >= evidence.work_items().size() ||
           evidence.work_items()[completed].outcome.status !=
               ProductionSourceBuildWorkItemStatus::Succeeded) {
            return CleanupSharedRequirementState::Unknown;
        }
        for(const std::size_t edge_index : relevant_edges) {
            const auto requiring_work_item =
                exact_work_item_index_for_package_base(
                    evidence.work_items(),
                    plan->dependency_edges[edge_index]
                        .parent_package_base);
            if(requiring_work_item.has_value() &&
               requiring_work_item.value() > completed) {
                return CleanupSharedRequirementState::StillRequired;
            }
        }
        return CleanupSharedRequirementState::Unknown;
    }

    const bool all_work_items_succeeded = std::all_of(
        evidence.work_items().begin(), evidence.work_items().end(),
        [](const CleanupInvocationWorkItemEvidence& work_item) {
            return work_item.outcome.status ==
                   ProductionSourceBuildWorkItemStatus::Succeeded;
        });
    if(evidence.lifecycle_boundary() ==
           CleanupLifecycleBoundary::AfterSuccessfulInvocation &&
       evidence.route_authority() == CleanupRouteAuthority::Complete &&
       evidence.completeness() == CleanupEvidenceCompleteness::Complete &&
       all_work_items_succeeded && !roles.empty() &&
       std::all_of(roles.begin(), roles.end(), is_build_or_check_role)) {
        return CleanupSharedRequirementState::NoLongerRequired;
    }
    return CleanupSharedRequirementState::Unknown;
}

CleanupBaselineObservation project_cleanup_baseline_observation(
    const InstalledPackageStateSnapshotResult& baseline_snapshot,
    const std::string& package_name) noexcept {
    const InstalledPackageStateSnapshot* snapshot =
        successful_snapshot(baseline_snapshot);
    if(snapshot == nullptr) return CleanupBaselineObservation::Unknown;
    return snapshot->find(package_name) == snapshot->end()
               ? CleanupBaselineObservation::NewlyObserved
               : CleanupBaselineObservation::PreExisting;
}

CleanupCurrentPackageEvidence project_cleanup_current_package_evidence(
    const InstalledPackageStateSnapshotResult& current_snapshot,
    const std::string& package_name) {
    const InstalledPackageStateSnapshot* snapshot =
        successful_snapshot(current_snapshot);
    if(snapshot == nullptr) {
        return CleanupCurrentPackageEvidence{
            CleanupInstalledState::Unknown, std::nullopt,
            CleanupEvidenceVerification::Unverified};
    }
    const auto found = snapshot->find(package_name);
    if(found == snapshot->end()) {
        return CleanupCurrentPackageEvidence{
            CleanupInstalledState::Absent, std::nullopt,
            CleanupEvidenceVerification::Verified};
    }
    return CleanupCurrentPackageEvidence{
        CleanupInstalledState::Present, found->second,
        CleanupEvidenceVerification::Verified};
}

CleanupCausalOwnership project_cleanup_causal_ownership(
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const std::vector<SelectedRepositoryProviderTransactionResult>&
        provider_transactions) noexcept {
    // LANDMINE(#404): makepkg success is not a package changed-set, and the
    // selected-provider result is transaction-level with current
    // PackageStateChange::Unknown. Even a future aggregate Changed value does
    // not identify which package changed or exclude an external transaction.
    static_cast<void>(lifecycle);
    static_cast<void>(provider_transactions);
    return CleanupCausalOwnership::Unknown;
}

#ifdef MOGUET_ENABLE_INVOCATION_TRANSACTION_LEDGER_TEST_HOOKS
CleanupCausalOwnership
project_cleanup_causal_ownership_from_raw_ledger_for_test(
    const std::string& package_name,
    CleanupBaselineObservation baseline,
    const CleanupCurrentPackageEvidence& current_package,
    const InvocationDependencyTransactionLedger& transaction_ledger) {
    // POLICY(#404): the receipt proves causality only when the independent
    // observation dimensions do not contradict a newly installed package.
    if(!is_valid_package_name(package_name) ||
       baseline != CleanupBaselineObservation::NewlyObserved ||
       current_package.state != CleanupInstalledState::Present ||
       current_package.verification !=
           CleanupEvidenceVerification::Verified ||
       !current_package.metadata.has_value() ||
       current_package.metadata->name != package_name) {
        return CleanupCausalOwnership::Unknown;
    }

    for(const InvocationDependencyTransaction& transaction :
        transaction_ledger.transactions) {
        if(transaction.command_outcome !=
               InvocationDependencyTransactionCommandOutcome::Succeeded ||
           !is_valid_pacman_transaction_token(
               transaction.transaction_token) ||
           !transaction_requested_packages_are_valid(
               transaction.requested_package_names) ||
           !transaction.receipt.is_complete_for(
               transaction.transaction_token, transaction.owner)) {
            continue;
        }
        if(transaction.receipt.contains_newly_installed_package(
               package_name)) {
            return CleanupCausalOwnership::InvocationOwned;
        }
    }

    // Receipt omission is deliberately not NotInvocationOwned. Another
    // transaction may be unavailable/incomplete, and a complete receipt may
    // contain only an Upgrade for this package.
    return CleanupCausalOwnership::Unknown;
}
#endif

CleanupPolicyProtection project_cleanup_policy_protection(
    const CleanupPolicyProtectionEvidence& evidence) noexcept {
    if(!is_valid_cleanup_policy_completeness(
           evidence.local_database_completeness) ||
       !is_valid_cleanup_policy_completeness(
           evidence.candidate_metadata_completeness) ||
       !cleanup_policy_authority_shape_is_valid(
           evidence.installed_base_devel,
           CleanupPolicyAuthorityKind::InstalledBaseDevelMetaPackage) ||
       !cleanup_policy_authority_shape_is_valid(
           evidence.configured_sync_base_devel,
           CleanupPolicyAuthorityKind::
               ConfiguredSyncBaseDevelMetaPackage) ||
       !cleanup_policy_authority_shape_is_valid(
           evidence.base_devel_group,
           CleanupPolicyAuthorityKind::BaseDevelGroupCompatibility) ||
       evidence.consistency !=
           CleanupPolicyEvidenceConsistency::Consistent ||
       evidence.local_database_completeness !=
           CleanupPolicyMetadataCompleteness::Complete ||
       evidence.candidate_metadata_completeness !=
           CleanupPolicyMetadataCompleteness::Complete ||
       !evidence.candidate.has_value() ||
       !is_valid_package_name(evidence.candidate->package_name) ||
       evidence.candidate->version.empty() ||
       !cleanup_policy_strings_are_unique_and_nonempty(
           evidence.candidate->provides) ||
       !cleanup_policy_strings_are_unique_and_nonempty(
           evidence.candidate->groups)) {
        return CleanupPolicyProtection::Unknown;
    }

    const CleanupPolicyAuthorityEvidence* selected_authority = nullptr;
    if(evidence.installed_base_devel.observation ==
       CleanupPolicyAuthorityObservation::Present) {
        selected_authority = &evidence.installed_base_devel;
    } else if(cleanup_policy_authority_is_complete_absence(
                  evidence.installed_base_devel)) {
        if(evidence.configured_sync_base_devel.observation ==
           CleanupPolicyAuthorityObservation::Present) {
            selected_authority = &evidence.configured_sync_base_devel;
        } else if(cleanup_policy_authority_is_complete_absence(
                      evidence.configured_sync_base_devel) &&
                  evidence.base_devel_group.observation ==
                      CleanupPolicyAuthorityObservation::Present) {
            selected_authority = &evidence.base_devel_group;
        }
    }

    if(selected_authority == nullptr) {
        return CleanupPolicyProtection::Unknown;
    }
    if(selected_authority == &evidence.base_devel_group) {
        const bool candidate_declares_exact_group =
            std::find(
                evidence.candidate->groups.begin(),
                evidence.candidate->groups.end(), "base-devel") !=
            evidence.candidate->groups.end();
        if((selected_authority->candidate_evaluation ==
                CleanupPolicyCandidateEvaluation::Protected &&
            !candidate_declares_exact_group) ||
           (selected_authority->candidate_evaluation ==
                CleanupPolicyCandidateEvaluation::NotProtected &&
            candidate_declares_exact_group)) {
            return CleanupPolicyProtection::Unknown;
        }
    }
    if(cleanup_policy_authority_has_complete_protection(
           *selected_authority)) {
        return CleanupPolicyProtection::Protected;
    }
    if(evidence.failures.empty() &&
       cleanup_policy_authority_has_complete_negative(
           *selected_authority)) {
        return CleanupPolicyProtection::NotProtected;
    }
    return CleanupPolicyProtection::Unknown;
}

InvocationOwnedCleanupCandidateProjectionResult
project_invocation_owned_cleanup_candidate(
    const InstalledPackageStateSnapshotResult& baseline_snapshot,
    const InstalledPackageStateSnapshotResult& current_snapshot,
    const BuildPlan& plan,
    const ResolvedDependencyCandidate& candidate_authority,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const std::vector<SelectedRepositoryProviderTransactionResult>&
        provider_transactions) {
    std::vector<CleanupLifecycleProjectionIssue> issues;
    retain_snapshot_failure(
        baseline_snapshot,
        CleanupLifecycleProjectionIssueKind::
            BaselineSnapshotUnavailable,
        issues);
    retain_snapshot_failure(
        current_snapshot,
        CleanupLifecycleProjectionIssueKind::
            CurrentSnapshotUnavailable,
        issues);

    std::optional<SourceAwarePackageIdentity> projected_candidate =
        project_dependency_identity(candidate_authority);
    if(!projected_candidate.has_value()) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                CandidateSourceIdentityUnavailable,
            std::nullopt,
            resolved_candidate_package_name(candidate_authority));
        return InvocationOwnedCleanupCandidateProjectionFailure{
            std::move(issues)};
    }

    CleanupCorrelationCoverage coverage =
        initial_correlation_coverage(plan, issues);
    BuildPlanArtifactTargetProjectionResult artifact_projection =
        project_build_plan_required_artifact_targets(plan);
    const BuildPlanArtifactTargetProjectionSuccess* artifact_success =
        artifact_projection.success();
    if(artifact_success == nullptr) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                BuildPlanArtifactProjectionFailed);
        downgrade_complete_coverage(coverage);
    }

    PreparedInvocationProjection prepared_projection;
    if(artifact_success != nullptr) {
        prepared_projection = project_prepared_invocation(
            *artifact_success, lifecycle);
    }
    if(!prepared_projection.complete) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                LifecycleEvidenceIncomplete);
        if(lifecycle.invocation() == nullptr) {
            if(coverage == CleanupCorrelationCoverage::Complete) {
                coverage = CleanupCorrelationCoverage::Unknown;
            }
        } else {
            downgrade_complete_coverage(coverage);
        }
    }

    if(std::optional<SourceAwarePackageIdentity> prepared_candidate =
           bind_prepared_source_identity(
               projected_candidate.value(),
               prepared_projection.package_bases);
       prepared_candidate.has_value()) {
        projected_candidate = std::move(prepared_candidate);
    } else if(projected_candidate->package().package_base().source().kind() !=
              PackageSourceKind::Repository) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                PackageBaseSourceIdentityIncomplete,
            std::nullopt,
            projected_candidate->package().package_name());
        downgrade_complete_coverage(coverage);
    }

    const PackageChildIdentity candidate_package =
        projected_candidate->package();
    const std::string& package_name = candidate_package.package_name();
    const std::string& package_base =
        candidate_package.package_base().package_base();
    const PlannedPackageTarget* candidate_target =
        find_unique_package_target(
            plan, package_name, package_base);

    std::vector<CleanupPackageCorrelation> correlations;
    std::vector<std::size_t> relevant_edge_indices;
    std::vector<PackageRole> observed_roles;
    std::set<std::size_t> structurally_verified_edges;
    const bool metadata_complete = snapshot_succeeded(baseline_snapshot) &&
                                   snapshot_succeeded(current_snapshot);

    for(std::size_t edge_index = 0;
        edge_index < plan.dependency_edges.size(); ++edge_index) {
        const BuildPlanDependencyEdge& edge =
            plan.dependency_edges[edge_index];
        const std::optional<std::string> edge_package_name =
            potential_edge_package_name(edge);
        if(!edge_package_name.has_value() ||
           edge_package_name.value() != package_name) {
            continue;
        }
        relevant_edge_indices.push_back(edge_index);
        if(is_known_role(edge.role) &&
           !has_role(observed_roles, edge.role)) {
            observed_roles.push_back(edge.role);
        }
        if(edge.kind == DependencyKind::Provided &&
           edge.resolved_provider.has_value() &&
           edge.resolved_provider->package_base.empty()) {
            add_issue(
                issues,
                CleanupLifecycleProjectionIssueKind::
                    RepositoryProviderPackageBaseUnavailable,
                edge_index, edge.resolved_provider->package_name);
        }

        std::optional<SourceAwarePackageIdentity> resolved_identity;
        if(edge.resolved_candidate.has_value()) {
            resolved_identity = project_dependency_identity(
                edge.resolved_candidate.value());
            if(resolved_identity.has_value()) {
                if(std::optional<SourceAwarePackageIdentity> prepared =
                       bind_prepared_source_identity(
                           resolved_identity.value(),
                           prepared_projection.package_bases);
                   prepared.has_value()) {
                    resolved_identity = std::move(prepared);
                }
            }
        }

        const PlannedPackageTarget* parent_target =
            find_unique_package_target(
                plan, edge.parent_package_name,
                edge.parent_package_base);
        std::optional<PackageChildIdentity> requiring_package =
            project_requiring_package(
                edge, prepared_projection.package_bases);

        std::optional<CleanupDependencyEdgeCorrelation> edge_correlation;
        if(edge.requirement.has_value() &&
           requiring_package.has_value()) {
            std::optional<CleanupProviderCorrelation> provider;
            if(edge.resolved_provider.has_value()) {
                provider = CleanupProviderCorrelation{
                    edge.resolved_provider.value(),
                    edge.provider_resolution};
            }
            edge_correlation = CleanupDependencyEdgeCorrelation{
                edge_index, requiring_package.value(),
                edge.requirement.value(), std::move(provider)};
        }

        bool structurally_verified =
            resolved_identity.has_value() &&
            edge.requirement.has_value() &&
            edge_correlation.has_value() &&
            parent_target != nullptr &&
            !parent_target->roots.empty() &&
            is_dependency_role(edge.role) &&
            requirement_matches_raw_specification(
                edge.requirement.value(), edge.dependency_spec) &&
            successful_constraint_evaluation(edge);
        if(structurally_verified) {
            if(edge.kind == DependencyKind::Provided) {
                structurally_verified = provider_association_is_complete(
                    edge, edge.requirement.value(),
                    projected_candidate.value(), plan, edge_index,
                    issues);
            } else {
                structurally_verified = direct_association_is_complete(
                    edge, edge.requirement.value(),
                    resolved_identity.value());
            }
        }
        if(structurally_verified &&
           resolved_identity->package() != candidate_package) {
            // Preserve the contradictory typed identity in the correlation so
            // Slice 2 classifies it as Invalid instead of hiding it as a gap.
            downgrade_complete_coverage(coverage);
        } else if(structurally_verified) {
            structurally_verified_edges.insert(edge_index);
        }

        if(parent_target == nullptr || parent_target->roots.empty()) {
            add_issue(
                issues,
                CleanupLifecycleProjectionIssueKind::
                    DependencyCorrelationIncomplete,
                edge_index, package_name);
            downgrade_complete_coverage(coverage);
            continue;
        }

        const PackageChildIdentity correlation_package =
            resolved_identity.has_value()
                ? resolved_identity->package()
                : candidate_package;
        const CleanupEvidenceVerification verification =
            structurally_verified && metadata_complete
                ? CleanupEvidenceVerification::Verified
                : CleanupEvidenceVerification::Unverified;
        for(const RootTargetIdentity& root : parent_target->roots) {
            correlations.push_back(CleanupPackageCorrelation{
                root,
                correlation_package,
                is_known_role(edge.role)
                    ? std::optional<PackageRole>{edge.role}
                    : std::nullopt,
                edge_correlation,
                verification});
        }
        if(!structurally_verified) {
            add_issue(
                issues,
                CleanupLifecycleProjectionIssueKind::
                    DependencyCorrelationIncomplete,
                edge_index, package_name);
            downgrade_complete_coverage(coverage);
        }
    }

    if(candidate_target != nullptr) {
        for(const PackageRole role : candidate_target->roles) {
            if(!is_known_role(role)) {
                downgrade_complete_coverage(coverage);
                continue;
            }
            if(!has_role(observed_roles, role)) {
                observed_roles.push_back(role);
            }
            if(role == PackageRole::Root) {
                for(const RootTargetIdentity& root : candidate_target->roots) {
                    correlations.push_back(CleanupPackageCorrelation{
                        root, candidate_package, role, std::nullopt,
                        metadata_complete
                            ? CleanupEvidenceVerification::Verified
                            : CleanupEvidenceVerification::
                                  Unverified});
                }
                continue;
            }

            const bool role_has_edge = std::any_of(
                relevant_edge_indices.begin(),
                relevant_edge_indices.end(),
                [&plan, role](std::size_t edge_index) {
                    return plan.dependency_edges[edge_index].role == role;
                });
            if(role_has_edge) continue;
            for(const RootTargetIdentity& root : candidate_target->roots) {
                correlations.push_back(CleanupPackageCorrelation{
                    root, candidate_package, role, std::nullopt,
                    CleanupEvidenceVerification::Unverified});
            }
            add_issue(
                issues,
                CleanupLifecycleProjectionIssueKind::
                    DependencyCorrelationIncomplete,
                std::nullopt, package_name);
            downgrade_complete_coverage(coverage);
        }
    } else if(candidate_package.package_base().source().kind() !=
              PackageSourceKind::Repository) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                DependencyCorrelationIncomplete,
            std::nullopt, package_name);
        downgrade_complete_coverage(coverage);
    }

    if(relevant_edge_indices.empty() ||
       structurally_verified_edges.size() !=
           relevant_edge_indices.size() ||
       correlations.empty()) {
        downgrade_complete_coverage(coverage);
    }

    const CleanupBaselineObservation baseline =
        project_cleanup_baseline_observation(
            baseline_snapshot, package_name);
    CleanupCurrentPackageEvidence current =
        project_cleanup_current_package_evidence(
            current_snapshot, package_name);
    // Legacy lifecycle/provider success evidence remains intentionally inert.
    static_cast<void>(lifecycle);
    static_cast<void>(provider_transactions);
    // POLICY(#485): raw generic ledger values are factual records only. A
    // production-positive owner-specific capability is intentionally not yet
    // connected to this broader route/candidate adapter.
    const CleanupCausalOwnership causal = CleanupCausalOwnership::Unknown;
    if(causal == CleanupCausalOwnership::Unknown) {
        add_issue(
            issues,
            CleanupLifecycleProjectionIssueKind::
                CausalOwnershipUnavailable,
            std::nullopt, package_name);
    }

    const CleanupSharedRequirementState shared =
        project_shared_requirement(
            plan, relevant_edge_indices, observed_roles, coverage,
            lifecycle, prepared_projection.complete);
    // Slice 3 completes the standalone factual query and pure policy reducer,
    // but this broader production candidate lifecycle remains intentionally
    // unconnected until route/correlation authority is closed in Slice 4.
    const CleanupPolicyProtection policy =
        CleanupPolicyProtection::Unknown;
    add_issue(
        issues,
        CleanupLifecycleProjectionIssueKind::
            PolicyProtectionUnavailable,
        std::nullopt, package_name);

    return InvocationOwnedCleanupCandidateProjectionSuccess{
        InvocationOwnedCleanupCandidate{
            std::move(projected_candidate.value()),
            baseline,
            std::move(current),
            causal,
            shared,
            policy,
            coverage,
            std::move(correlations)},
        std::move(issues)};
}
