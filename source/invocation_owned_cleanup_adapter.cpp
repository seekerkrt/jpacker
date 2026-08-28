#include "invocation_owned_cleanup_adapter.hpp"

#include "build_plan_artifact_target_projection.hpp"
#include "makepkg_syncdeps_receipt_model.hpp"
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

bool current_package_identity_supports_causal_projection(
    const SourceAwarePackageIdentity& candidate,
    const CleanupCurrentPackageEvidence& current_package) noexcept {
    if(current_package.state != CleanupInstalledState::Present ||
       current_package.verification !=
           CleanupEvidenceVerification::Verified ||
       !current_package.metadata.has_value() ||
       current_package.metadata->name !=
           candidate.package().package_name()) {
        return false;
    }

    const PackageVersionIdentity& version = candidate.package_version();
    if(version.state() != PackageVersionState::Known) return true;
    const std::string* full_version = version.full_version();
    return full_version != nullptr &&
           *full_version == current_package.metadata->version;
}

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

CleanupCausalOwnership project_cleanup_causal_ownership(
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

CleanupCausalOwnership
project_makepkg_sync_dependency_causal_ownership(
    const std::string& package_name,
    CleanupBaselineObservation baseline,
    const CleanupCurrentPackageEvidence& current_package,
    const MakepkgSyncDependencySessionReceipt& session_receipt) noexcept {
    if(!is_valid_package_name(package_name) ||
       baseline != CleanupBaselineObservation::NewlyObserved ||
       current_package.state != CleanupInstalledState::Present ||
       current_package.verification !=
           CleanupEvidenceVerification::Verified ||
       !current_package.metadata.has_value() ||
       current_package.metadata->name != package_name ||
       !session_receipt.is_trusted_terminal() ||
       session_receipt.adapter_coverage() !=
           MakepkgSyncDependencyAdapterCoverage::Complete ||
       session_receipt.process_binding() !=
           MakepkgSyncDependencyProcessBinding::
               InstalledLauncherAndExactLauncherMakepkgLifetime ||
       !session_receipt.session_identity().has_value() ||
       !is_valid_makepkg_sync_dependency_session_token(
           session_receipt.session_identity()->session_token) ||
       !session_receipt.owner().has_value() ||
       session_receipt.owner().value() !=
           InvocationDependencyTransactionOwner::MakepkgSyncDependencies ||
       !session_receipt.transaction_count().has_value() ||
       session_receipt.transaction_count().value() !=
           session_receipt.transaction_observations().size() ||
       session_receipt.transaction_count().value() > 2 ||
       !session_receipt.contains_authoritative_install(package_name)) {
        return CleanupCausalOwnership::Unknown;
    }
    return CleanupCausalOwnership::InvocationOwned;
}

CleanupPolicyProtection project_cleanup_policy_protection() noexcept {
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
        provider_transactions,
    const InvocationDependencyTransactionLedger& transaction_ledger) {
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
    CleanupCausalOwnership causal = CleanupCausalOwnership::Unknown;
    if(current_package_identity_supports_causal_projection(
           projected_candidate.value(), current)) {
        causal = project_cleanup_causal_ownership(
            package_name, baseline, current, transaction_ledger);
    }
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
    const CleanupPolicyProtection policy =
        project_cleanup_policy_protection();
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
