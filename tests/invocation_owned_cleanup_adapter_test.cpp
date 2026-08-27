#include "build_plan_artifact_target_projection.hpp"
#include "invocation_owned_cleanup_adapter.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

bool has_reason(
        const CleanupClassificationResult& result,
        CleanupClassificationReason reason) {
    return std::find(
                   result.reasons().begin(), result.reasons().end(), reason) !=
            result.reasons().end();
}

bool has_issue(
        const InvocationOwnedCleanupCandidateProjectionSuccess& projection,
        CleanupLifecycleProjectionIssueKind kind) {
    return std::any_of(
            projection.issues.begin(), projection.issues.end(),
            [kind](const CleanupLifecycleProjectionIssue& issue) {
                return issue.kind == kind;
            });
}

RootTargetIdentity root(
        std::size_t invocation_index, std::string requested_name) {
    return RootTargetIdentity{
            invocation_index, std::move(requested_name)};
}

DependencyRequirement requirement(const std::string& package_name) {
    return ConsumerDependencyRequirement(
            package_name, package_name, std::nullopt);
}

PackageBaseIdentity aur_package_base(const std::string& package_base) {
    return PackageBaseIdentity::make(
            PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(
                            "https://aur.archlinux.org/" + package_base +
                            ".git")),
            package_base);
}

BuildPlanDependencyEdge aur_edge(
        std::string parent_name,
        std::string parent_base,
        PackageRole role,
        std::string package_name = "build-tool",
        std::string package_base = "build-tools") {
    BuildPlanDependencyEdge edge;
    edge.parent_package_name = std::move(parent_name);
    edge.parent_package_base = std::move(parent_base);
    edge.dependency_spec = package_name;
    edge.role = role;
    edge.kind = DependencyKind::Aur;
    edge.resolved_package_name = package_name;
    edge.resolved_package_base = package_base;
    edge.requirement = requirement(package_name);
    edge.resolved_candidate = AurResolvedDependencyCandidate{
            std::move(package_name), std::move(package_base),
            ObservedVersion::available(
                    ObservedVersionSource::AurExactPackage, "1.0-1")};
    edge.constraint_evaluation = ConstraintEvaluation::satisfied();
    return edge;
}

BuildPlan basic_plan() {
    BuildPlan plan;
    const RootTargetIdentity app_root = root(0, "application");
    plan.root_targets.push_back(app_root);
    plan.package_targets.push_back(PlannedPackageTarget{
            "build-tool", "build-tools",
            {PackageRole::BuildDependency}, {app_root}});
    plan.package_targets.push_back(PlannedPackageTarget{
            "application", "application",
            {PackageRole::Root}, {app_root}});
    plan.order.push_back(BuildPlanEntry{
            "build-tools", {"build-tool"}});
    plan.order.push_back(BuildPlanEntry{
            "application", {"application"}});
    plan.dependency_edges.push_back(aur_edge(
            "application", "application",
            PackageRole::BuildDependency));
    return plan;
}

BuildPlan multiple_root_plan() {
    BuildPlan plan;
    const RootTargetIdentity first_root = root(0, "first-app");
    const RootTargetIdentity second_root = root(1, "second-app");
    plan.root_targets = {first_root, second_root};
    plan.package_targets.push_back(PlannedPackageTarget{
            "build-tool", "build-tools",
            {PackageRole::BuildDependency, PackageRole::CheckDependency},
            {first_root, second_root}});
    plan.package_targets.push_back(PlannedPackageTarget{
            "first-app", "first-base", {PackageRole::Root},
            {first_root}});
    plan.package_targets.push_back(PlannedPackageTarget{
            "second-app", "second-base", {PackageRole::Root},
            {second_root}});
    plan.order.push_back(BuildPlanEntry{
            "build-tools", {"build-tool"}});
    plan.order.push_back(BuildPlanEntry{"first-base", {"first-app"}});
    plan.order.push_back(BuildPlanEntry{
            "second-base", {"second-app"}});
    plan.dependency_edges.push_back(aur_edge(
            "first-app", "first-base",
            PackageRole::BuildDependency));
    plan.dependency_edges.push_back(aur_edge(
            "second-app", "second-base",
            PackageRole::CheckDependency));
    return plan;
}

BuildPlan mixed_runtime_plan() {
    BuildPlan plan = basic_plan();
    plan.package_targets.front().roles = {
            PackageRole::RuntimeDependency,
            PackageRole::BuildDependency};
    plan.dependency_edges.insert(
            plan.dependency_edges.begin(),
            aur_edge(
                    "application", "application",
                    PackageRole::RuntimeDependency));
    return plan;
}

ProvidedDependency repository_provider() {
    ProviderCapability capability(
            "virtual-build-tool", "virtual-build-tool", std::nullopt);
    return ProvidedDependency::from_repository_constraint_metadata(
            "core", 0, "repository-tool",
            ProviderConstraintMetadata{
                    capability,
                    ObservedVersion::available(
                            ObservedVersionSource::RepositoryExactPackage,
                            "2.0-1"),
                    ObservedVersion::unknown(
                            ObservedVersionSource::
                                    RepositoryProviderCapability,
                            ObservedVersionUnknownReason::
                                    UnversionedProviderCapability)});
}

BuildPlan repository_provider_plan() {
    BuildPlan plan;
    const RootTargetIdentity app_root = root(0, "application");
    plan.root_targets.push_back(app_root);
    plan.package_targets.push_back(PlannedPackageTarget{
            "application", "application", {PackageRole::Root},
            {app_root}});
    plan.order.push_back(BuildPlanEntry{
            "application", {"application"}});
    plan.configured_repository_order =
            std::vector<std::string>{"core"};

    const ProvidedDependency provider = repository_provider();
    BuildPlanDependencyEdge edge;
    edge.parent_package_name = "application";
    edge.parent_package_base = "application";
    edge.dependency_spec = "virtual-build-tool";
    edge.role = PackageRole::BuildDependency;
    edge.kind = DependencyKind::Provided;
    edge.resolved_package_name = provider.package_name;
    edge.resolved_provider = provider;
    edge.provider_resolution = ProviderResolutionKind::UserSelected;
    edge.requirement = requirement("virtual-build-tool");
    edge.resolved_candidate = ProviderResolvedDependencyCandidate{
            provider,
            provider.constraint_metadata->provided_version};
    edge.constraint_evaluation = ConstraintEvaluation::satisfied();
    plan.dependency_edges.push_back(std::move(edge));
    plan.provided.push_back(BuildPlanProvidedDependency{
            "virtual-build-tool", provider,
            ProviderResolutionKind::UserSelected});
    return plan;
}

ResolvedDependencyCandidate repository_exact_candidate() {
    return RepositoryExactPackage{
            ConfiguredRepositoryIdentity{"core", 0},
            "repository-tool",
            "repository-tools",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage,
                    "2.0-1"),
            {}};
}

PreparedProductionSourceBuildInvocation prepared_invocation(
        const BuildPlan& plan) {
    BuildPlanArtifactTargetProjectionResult projected =
            project_build_plan_required_artifact_targets(plan);
    const BuildPlanArtifactTargetProjectionSuccess* success =
            projected.success();
    if(success == nullptr) {
        throw std::runtime_error(
                "test plan did not project artifact targets");
    }

    std::vector<ProductionSourceBuildWorkItem> work_items;
    for(const ProjectedBuildPlanArtifactTargets& unit :
        success->build_units) {
        ProductionSourceBuildWorkItem work_item;
        work_item.request.checkout_name = unit.package_base;
        work_item.request.git_url =
                "https://aur.archlinux.org/" + unit.package_base + ".git";
        work_item.request.aur_review_identity =
                aur_package_base(unit.package_base);
        if(unit.required_targets.size() == 1) {
            work_item.request.package_name =
                    unit.required_targets.front().package_name;
        }
        work_item.required_targets = unit.required_targets;
        work_item.required_target_provenance =
                RequiredTargetProvenance::AurBuildPlanProjection;
        work_item.artifact_lifecycle_intent =
                ArtifactLifecycleIntent::PackageBaseSet;
        work_items.push_back(std::move(work_item));
    }
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items), {},
            PacmanDatabasePaths{"/", "/var/lib/pacman"},
            std::nullopt, std::nullopt};
}

ProductionSourceBuildStagedOutcome successful_staged_outcome() {
    ProductionSourceBuildStagedOutcome outcome;
    outcome.build_outcome =
            ProductionSourceBuildCommandOutcome::Succeeded;
    outcome.install_outcome = ProductionSourceInstallOutcome::Succeeded;
    return outcome;
}

ProductionSourceBuildInvocationResult successful_result(
        const PreparedProductionSourceBuildInvocation& invocation) {
    ProductionSourceBuildInvocationResult result;
    for(const ProductionSourceBuildWorkItem& work_item :
        invocation.work_items) {
        result.work_items.push_back(ProductionSourceBuildWorkItemOutcome{
                work_item.request.checkout_name,
                ProductionSourceBuildWorkItemStatus::Succeeded,
                successful_staged_outcome(), std::nullopt, std::nullopt,
                nullptr});
    }
    return result;
}

ProductionSourceBuildInvocationResult result_after_work_item(
        const PreparedProductionSourceBuildInvocation& invocation,
        std::size_t completed_index) {
    ProductionSourceBuildInvocationResult result;
    for(std::size_t index = 0; index < invocation.work_items.size();
        ++index) {
        ProductionSourceBuildWorkItemOutcome outcome;
        outcome.package_base =
                invocation.work_items[index].request.checkout_name;
        if(index <= completed_index) {
            outcome.status =
                    ProductionSourceBuildWorkItemStatus::Succeeded;
            outcome.production_outcome = successful_staged_outcome();
        }
        result.work_items.push_back(std::move(outcome));
    }
    return result;
}

InstalledPackageStateSnapshotResult absent_snapshot() {
    return InstalledPackageStateSnapshot{};
}

InstalledPackageStateSnapshotResult present_snapshot(
        const std::string& package_name,
        const std::string& version,
        InstalledPackageReason reason) {
    InstalledPackageStateSnapshot snapshot;
    snapshot.emplace(
            package_name,
            InstalledPackageMetadata{package_name, version, reason});
    return snapshot;
}

InstalledPackageStateSnapshotResult failed_snapshot() {
    return PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "typed metadata failure"};
}

InvocationOwnedCleanupCandidateProjectionSuccess require_projection(
        InvocationOwnedCleanupCandidateProjectionResult result,
        const std::string& context) {
    auto* success = std::get_if<
            InvocationOwnedCleanupCandidateProjectionSuccess>(&result);
    if(success == nullptr) {
        throw std::runtime_error(context + ": projection failed");
    }
    return std::move(*success);
}

InvocationOwnedCleanupCandidateProjectionSuccess project_basic_candidate(
        const BuildPlan& plan,
        const ResolvedDependencyCandidate& candidate_authority,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        InstalledPackageStateSnapshotResult baseline = absent_snapshot(),
        InstalledPackageStateSnapshotResult current = present_snapshot(
                "build-tool", "1.0-1",
                InstalledPackageReason::Dependency),
        const std::vector<SelectedRepositoryProviderTransactionResult>&
                provider_transactions = {}) {
    return require_projection(
            project_invocation_owned_cleanup_candidate(
                    baseline, current, plan, candidate_authority, lifecycle,
                    provider_transactions),
            "basic cleanup candidate");
}

void test_snapshot_observation_projection() {
    InstalledPackageStateSnapshotResult preexisting = present_snapshot(
            "build-tool", "1.0-1",
            InstalledPackageReason::Explicit);
    expect(
            project_cleanup_baseline_observation(
                    preexisting, "build-tool") ==
                    CleanupBaselineObservation::PreExisting,
            "pre-existing snapshot was not projected as PreExisting");
    expect(
            project_cleanup_baseline_observation(
                    absent_snapshot(), "build-tool") ==
                    CleanupBaselineObservation::NewlyObserved,
            "successful absence was not projected as NewlyObserved");
    expect(
            project_cleanup_baseline_observation(
                    failed_snapshot(), "build-tool") ==
                    CleanupBaselineObservation::Unknown,
            "failed baseline snapshot asserted absence");

    const CleanupCurrentPackageEvidence present =
            project_cleanup_current_package_evidence(
                    present_snapshot(
                            "build-tool", "1.0-1",
                            InstalledPackageReason::Dependency),
                    "build-tool");
    expect(
            present.state == CleanupInstalledState::Present &&
                    present.metadata.has_value() &&
                    present.metadata->reason ==
                            InstalledPackageReason::Dependency &&
                    present.verification ==
                            CleanupEvidenceVerification::Verified,
            "current present metadata projection differs");
    const CleanupCurrentPackageEvidence absent =
            project_cleanup_current_package_evidence(
                    absent_snapshot(), "build-tool");
    expect(
            absent.state == CleanupInstalledState::Absent &&
                    !absent.metadata.has_value(),
            "successful current absence was not confirmed");
    const CleanupCurrentPackageEvidence unknown =
            project_cleanup_current_package_evidence(
                    failed_snapshot(), "build-tool");
    expect(
            unknown.state == CleanupInstalledState::Unknown &&
                    !unknown.metadata.has_value() &&
                    unknown.verification ==
                            CleanupEvidenceVerification::Unverified,
            "failed current snapshot asserted absence or metadata");
}

void test_current_lifecycle_never_proves_causal_ownership() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult result =
            successful_result(invocation);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(invocation, result);

    expect(
            project_cleanup_causal_ownership(lifecycle, {}) ==
                    CleanupCausalOwnership::Unknown,
            "successful makepkg/install lifecycle proved package ownership");

    const ProvidedDependency provider = repository_provider();
    SelectedRepositoryProviderTransactionResult succeeded;
    succeeded.status =
            SelectedRepositoryProviderTransactionStatus::Succeeded;
    succeeded.selected_providers = {provider};
    succeeded.package_state_change = PackageStateChange::Unknown;
    succeeded.command_exit_status = 0;
    expect(
            project_cleanup_causal_ownership(lifecycle, {succeeded}) ==
                    CleanupCausalOwnership::Unknown,
            "successful --needed provider transaction with Unknown change proved ownership");

    SelectedRepositoryProviderTransactionResult aggregate_changed =
            succeeded;
    aggregate_changed.selected_providers.push_back(
            ProvidedDependency::from_repository_constraint_metadata(
                    "extra", 1, "second-provider",
                    ProviderConstraintMetadata{
                            ProviderCapability(
                                    "other-virtual", "other-virtual",
                                    std::nullopt),
                            ObservedVersion::available(
                                    ObservedVersionSource::
                                            RepositoryExactPackage,
                                    "1.0-1"),
                            ObservedVersion::unknown(
                                    ObservedVersionSource::
                                            RepositoryProviderCapability,
                                    ObservedVersionUnknownReason::
                                            UnversionedProviderCapability)}));
    aggregate_changed.package_state_change = PackageStateChange::Changed;
    expect(
            project_cleanup_causal_ownership(
                    lifecycle, {aggregate_changed}) ==
                    CleanupCausalOwnership::Unknown,
            "multi-provider aggregate Changed proved package ownership");
}

void test_complete_plan_projects_complete_correlations_and_lifetime() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult result =
            successful_result(invocation);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(invocation, result);
    const ResolvedDependencyCandidate& candidate =
            plan.dependency_edges.front().resolved_candidate.value();

    InvocationOwnedCleanupCandidateProjectionSuccess projection =
            project_basic_candidate(plan, candidate, lifecycle);
    expect(
            projection.candidate.correlation_coverage ==
                    CleanupCorrelationCoverage::Complete,
            "complete BuildPlan did not project Complete coverage");
    expect(
            projection.candidate.correlations.size() == 1 &&
                    projection.candidate.correlations.front().role ==
                            PackageRole::BuildDependency &&
                    projection.candidate.correlations.front().verification ==
                            CleanupEvidenceVerification::Verified,
            "complete BuildPlan correlation was flattened or unverified");
    expect(
            projection.candidate.shared_requirement ==
                    CleanupSharedRequirementState::NoLongerRequired,
            "full successful invocation did not close build-only lifetime");
    expect(
            projection.candidate.package.package().package_base().source()
                            .location()
                            .state() == SourceLocationState::Known,
            "prepared AUR source identity was not retained");
}

void test_incomplete_plan_states_never_project_complete_coverage() {
    auto expect_not_complete = [](BuildPlan plan, const std::string& context) {
        PreparedProductionSourceBuildInvocation invocation =
                prepared_invocation(plan);
        CleanupInvocationLifecycleEvidence lifecycle =
                CleanupInvocationLifecycleEvidence::
                        before_build_completion(invocation);
        InvocationOwnedCleanupCandidateProjectionSuccess projection =
                project_basic_candidate(
                        plan,
                        plan.dependency_edges.front()
                                .resolved_candidate.value(),
                        lifecycle);
        expect(
                projection.candidate.correlation_coverage !=
                        CleanupCorrelationCoverage::Complete,
                context + " projected Complete coverage");
    };

    BuildPlan resolution_failure = basic_plan();
    resolution_failure.resolution_failures.push_back(
            BuildPlanResolutionFailure{
                    BuildPlanResolutionFailureKind::
                            AurPackageMetadataUnavailable,
                    "application", "application", "missing-dependency",
                    "missing-dependency", resolution_failure.root_targets,
                    "typed resolution failure"});
    expect_not_complete(
            std::move(resolution_failure), "resolution failure");

    BuildPlan unresolved = basic_plan();
    unresolved.unresolved.push_back("missing-dependency");
    expect_not_complete(std::move(unresolved), "unresolved dependency");

    BuildPlan ambiguous = basic_plan();
    ambiguous.ambiguous_providers.push_back(
            AmbiguousProvidedDependency{
                    "virtual-tool",
                    {ProvidedDependency::from_aur("provider-one")}});
    expect_not_complete(std::move(ambiguous), "ambiguous provider");

    BuildPlan cancelled = basic_plan();
    cancelled.cancelled_provider_dependencies.push_back("virtual-tool");
    expect_not_complete(std::move(cancelled), "cancelled provider");

    BuildPlan incomplete_candidates = basic_plan();
    incomplete_candidates.incomplete_provider_candidate_sets.push_back(
            IncompleteProviderCandidateSet{
                    "virtual-tool", {},
                    ObservedVersionUnknownReason::PartialSourceFailure});
    expect_not_complete(
            std::move(incomplete_candidates),
            "incomplete provider candidate set");
}

void test_repository_provider_without_package_base_is_unverified() {
    BuildPlan plan = repository_provider_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    before_build_completion(invocation);
    ResolvedDependencyCandidate candidate = repository_exact_candidate();

    InvocationOwnedCleanupCandidateProjectionSuccess projection =
            project_basic_candidate(
                    plan, candidate, lifecycle, absent_snapshot(),
                    present_snapshot(
                            "repository-tool", "2.0-1",
                            InstalledPackageReason::Dependency));
    expect(
            projection.candidate.correlation_coverage ==
                    CleanupCorrelationCoverage::Incomplete,
            "repository provider without PackageBase kept Complete coverage");
    expect(
            !projection.candidate.correlations.empty() &&
                    projection.candidate.correlations.front().verification ==
                            CleanupEvidenceVerification::Unverified,
            "repository provider without PackageBase became Verified");
    expect(
            has_issue(
                    projection,
                    CleanupLifecycleProjectionIssueKind::
                            RepositoryProviderPackageBaseUnavailable),
            "missing repository provider PackageBase issue was not retained");
}

void test_mixed_roles_and_multiple_roots_are_preserved() {
    BuildPlan mixed = mixed_runtime_plan();
    PreparedProductionSourceBuildInvocation mixed_invocation =
            prepared_invocation(mixed);
    ProductionSourceBuildInvocationResult mixed_result =
            successful_result(mixed_invocation);
    CleanupInvocationLifecycleEvidence mixed_lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(
                            mixed_invocation, mixed_result);
    InvocationOwnedCleanupCandidateProjectionSuccess mixed_projection =
            project_basic_candidate(
                    mixed,
                    mixed.dependency_edges.front()
                            .resolved_candidate.value(),
                    mixed_lifecycle);
    bool saw_runtime = false;
    bool saw_build = false;
    for(const CleanupPackageCorrelation& correlation :
        mixed_projection.candidate.correlations) {
        saw_runtime |= correlation.role == PackageRole::RuntimeDependency;
        saw_build |= correlation.role == PackageRole::BuildDependency;
    }
    const CleanupClassificationResult mixed_classification =
            classify_invocation_owned_cleanup(mixed_projection.candidate);
    expect(
            saw_runtime && saw_build &&
                    mixed_projection.candidate.shared_requirement ==
                            CleanupSharedRequirementState::StillRequired &&
                    mixed_classification.classification() ==
                            CleanupClassification::Protected,
            "Build + Runtime roles were flattened or not protected");

    BuildPlan multiple = multiple_root_plan();
    PreparedProductionSourceBuildInvocation multiple_invocation =
            prepared_invocation(multiple);
    ProductionSourceBuildInvocationResult multiple_result =
            successful_result(multiple_invocation);
    CleanupInvocationLifecycleEvidence multiple_lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(
                            multiple_invocation, multiple_result);
    InvocationOwnedCleanupCandidateProjectionSuccess multiple_projection =
            project_basic_candidate(
                    multiple,
                    multiple.dependency_edges.front()
                            .resolved_candidate.value(),
                    multiple_lifecycle);
    expect(
            multiple_projection.candidate.correlation_coverage ==
                            CleanupCorrelationCoverage::Complete &&
                    multiple_projection.candidate.correlations.size() == 2 &&
                    multiple_projection.candidate.correlations[0]
                                    .requested_root !=
                            multiple_projection.candidate.correlations[1]
                                    .requested_root &&
                    multiple_projection.candidate.correlations[0]
                                    .dependency_edge->requiring_package
                                    .package_base()
                                    .package_base() !=
                            multiple_projection.candidate.correlations[1]
                                    .dependency_edge->requiring_package
                                    .package_base()
                                    .package_base(),
            "multiple roots/PackageBases were not retained");
}

void test_later_work_item_and_unknown_lifecycle_fail_safe() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult partial =
            result_after_work_item(invocation, 0);
    CleanupInvocationLifecycleEvidence after_dependency =
            CleanupInvocationLifecycleEvidence::after_work_item(
                    invocation, partial, 0);
    InvocationOwnedCleanupCandidateProjectionSuccess still_required =
            project_basic_candidate(
                    plan,
                    plan.dependency_edges.front()
                            .resolved_candidate.value(),
                    after_dependency);
    expect(
            still_required.candidate.shared_requirement ==
                    CleanupSharedRequirementState::StillRequired,
            "later work item requirement was not StillRequired");

    CleanupInvocationLifecycleEvidence unknown_lifecycle =
            CleanupInvocationLifecycleEvidence::unknown();
    InvocationOwnedCleanupCandidateProjectionSuccess unknown =
            project_basic_candidate(
                    plan,
                    plan.dependency_edges.front()
                            .resolved_candidate.value(),
                    unknown_lifecycle);
    expect(
            unknown.candidate.shared_requirement ==
                            CleanupSharedRequirementState::Unknown &&
                    unknown.candidate.correlation_coverage !=
                            CleanupCorrelationCoverage::Complete,
            "unknown lifecycle asserted completion or lifetime");
}

void test_local_remote_dependency_subset_is_not_complete_authority() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation remote_dependencies =
            prepared_invocation(plan);
    // Local production preparation owns only remote dependency units; the
    // local root remains with the local lifecycle owner. A subset must not be
    // treated as the complete BuildPlan/work-item set.
    remote_dependencies.work_items.pop_back();
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::before_build_completion(
                    remote_dependencies);
    InvocationOwnedCleanupCandidateProjectionSuccess projection =
            project_basic_candidate(
                    plan,
                    plan.dependency_edges.front()
                            .resolved_candidate.value(),
                    lifecycle);
    expect(
            projection.candidate.correlation_coverage ==
                            CleanupCorrelationCoverage::Incomplete &&
                    projection.candidate.shared_requirement ==
                            CleanupSharedRequirementState::Unknown,
            "local remote-dependency subset became complete lifecycle authority");
}

void test_metadata_and_source_failures_remain_typed_unknown_evidence() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult result =
            successful_result(invocation);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(invocation, result);
    const ResolvedDependencyCandidate& candidate =
            plan.dependency_edges.front().resolved_candidate.value();

    InvocationOwnedCleanupCandidateProjectionSuccess metadata_failure =
            project_basic_candidate(
                    plan, candidate, lifecycle, failed_snapshot(),
                    failed_snapshot());
    const CleanupClassificationResult metadata_classification =
            classify_invocation_owned_cleanup(metadata_failure.candidate);
    expect(
            metadata_failure.candidate.baseline ==
                            CleanupBaselineObservation::Unknown &&
                    metadata_failure.candidate.current_package.state ==
                            CleanupInstalledState::Unknown &&
                    metadata_failure.candidate.correlations.front()
                                    .verification ==
                            CleanupEvidenceVerification::Unverified &&
                    metadata_classification.classification() ==
                            CleanupClassification::Unknown &&
                    has_issue(
                            metadata_failure,
                            CleanupLifecycleProjectionIssueKind::
                                    BaselineSnapshotUnavailable) &&
                    has_issue(
                            metadata_failure,
                            CleanupLifecycleProjectionIssueKind::
                                    CurrentSnapshotUnavailable),
            "metadata failure became absence, verified evidence, or Eligible");

    PreparedProductionSourceBuildInvocation source_incomplete =
            prepared_invocation(plan);
    source_incomplete.work_items.front()
            .request.aur_review_identity.reset();
    CleanupInvocationLifecycleEvidence source_lifecycle =
            CleanupInvocationLifecycleEvidence::before_build_completion(
                    source_incomplete);
    InvocationOwnedCleanupCandidateProjectionSuccess source_failure =
            project_basic_candidate(
                    plan, candidate, source_lifecycle);
    expect(
            source_failure.candidate.correlation_coverage !=
                            CleanupCorrelationCoverage::Complete &&
                    source_failure.candidate.correlations.front()
                                    .verification ==
                            CleanupEvidenceVerification::Unverified,
            "source correlation failure became complete/verified");
}

void test_version_mismatch_and_unknown_policy_fail_closed() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult result =
            successful_result(invocation);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(invocation, result);
    const ResolvedDependencyCandidate& candidate =
            plan.dependency_edges.front().resolved_candidate.value();

    InvocationOwnedCleanupCandidateProjectionSuccess mismatch =
            project_basic_candidate(
                    plan, candidate, lifecycle, absent_snapshot(),
                    present_snapshot(
                            "build-tool", "2.0-1",
                            InstalledPackageReason::Dependency));
    expect(
            classify_invocation_owned_cleanup(mismatch.candidate)
                            .classification() ==
                    CleanupClassification::Invalid,
            "current installed version mismatch was not Invalid");

    InvocationOwnedCleanupCandidateProjectionSuccess policy_unknown =
            project_basic_candidate(plan, candidate, lifecycle);
    const CleanupClassificationResult policy_result =
            classify_invocation_owned_cleanup(policy_unknown.candidate);
    expect(
            policy_unknown.candidate.policy_protection ==
                            CleanupPolicyProtection::Unknown &&
                    policy_result.classification() ==
                            CleanupClassification::Unknown &&
                    has_reason(
                            policy_result,
                            CleanupClassificationReason::
                                    PolicyProtectionUnknown),
            "unknown policy authority permitted Eligible");
}

// LANDMINE(#404): every observational/planning success below is still not a
// package-level causal transaction proof.
void test_newly_observed_dependency_with_success_is_never_eligible() {
    BuildPlan plan = basic_plan();
    PreparedProductionSourceBuildInvocation invocation =
            prepared_invocation(plan);
    ProductionSourceBuildInvocationResult result =
            successful_result(invocation);
    CleanupInvocationLifecycleEvidence lifecycle =
            CleanupInvocationLifecycleEvidence::
                    after_successful_invocation(invocation, result);
    InvocationOwnedCleanupCandidateProjectionSuccess projection =
            project_basic_candidate(
                    plan,
                    plan.dependency_edges.front()
                            .resolved_candidate.value(),
                    lifecycle);

    expect(
            projection.candidate.baseline ==
                            CleanupBaselineObservation::NewlyObserved &&
                    projection.candidate.current_package.state ==
                            CleanupInstalledState::Present &&
                    projection.candidate.current_package.metadata->reason ==
                            InstalledPackageReason::Dependency &&
                    projection.candidate.correlation_coverage ==
                            CleanupCorrelationCoverage::Complete &&
                    projection.candidate.correlations.front().verification ==
                            CleanupEvidenceVerification::Verified &&
                    projection.candidate.shared_requirement ==
                            CleanupSharedRequirementState::NoLongerRequired,
            "landmine did not assemble the intended observational evidence");
    const CleanupClassificationResult classified =
            classify_invocation_owned_cleanup(projection.candidate);
    expect(
            projection.candidate.causal_ownership ==
                            CleanupCausalOwnership::Unknown &&
                    classified.classification() !=
                            CleanupClassification::Eligible &&
                    has_reason(
                            classified,
                            CleanupClassificationReason::
                                    CausalOwnershipUnknown),
            "pre absent + post Dependency + verified plan + success became Eligible");
}

} // namespace

void run_invocation_owned_cleanup_adapter_tests() {
    test_snapshot_observation_projection();
    test_current_lifecycle_never_proves_causal_ownership();
    test_complete_plan_projects_complete_correlations_and_lifetime();
    test_incomplete_plan_states_never_project_complete_coverage();
    test_repository_provider_without_package_base_is_unverified();
    test_mixed_roles_and_multiple_roots_are_preserved();
    test_later_work_item_and_unknown_lifecycle_fail_safe();
    test_local_remote_dependency_subset_is_not_complete_authority();
    test_metadata_and_source_failures_remain_typed_unknown_evidence();
    test_version_mismatch_and_unknown_policy_fail_closed();
    test_newly_observed_dependency_with_success_is_never_eligible();
}
