#include "aur_update_execution_preflight.hpp"
#include "system_source_upgrade.hpp"
#include "unified_plan_observation.hpp"
#include "upgrade_all_operation.hpp"

#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <typename T>
concept HasExecuteMember = requires(T value) { value.execute(); };

template <typename T>
concept HasResolveMember = requires(T value) { value.resolve(); };

template <typename T>
concept HasCommandMember = requires(T value) { value.command(); };

template <typename T>
concept HasArgvMember = requires(T value) { value.argv(); };

template <typename T>
concept HasProviderSelectionMember = requires(T value) {
    value.select_provider();
};

template <typename T>
concept HasOrderingOperator = requires(const T& lhs, const T& rhs) {
    { lhs < rhs } -> std::convertible_to<bool>;
};

static_assert(!std::is_default_constructible_v<UnifiedPlanObservation>);
static_assert(!std::is_aggregate_v<UnifiedPlanObservation>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanObservation>);
static_assert(!std::is_copy_assignable_v<UnifiedPlanObservation>);
static_assert(!std::is_constructible_v<
              UnifiedPlanObservation,
              const UnifiedPlanObservation&&>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanObservationResult>);
static_assert(!std::is_copy_assignable_v<UnifiedPlanObservationResult>);
static_assert(!std::is_constructible_v<
              UnifiedPlanObservationResult,
              const UnifiedPlanObservationResult&&>);
static_assert(!std::is_constructible_v<
              UnifiedPlanObservation,
              ProviderSelectionCallback>);
static_assert(!HasExecuteMember<UnifiedPlanObservation>);
static_assert(!HasResolveMember<UnifiedPlanObservation>);
static_assert(!HasCommandMember<UnifiedPlanObservation>);
static_assert(!HasArgvMember<UnifiedPlanObservation>);
static_assert(!HasProviderSelectionMember<UnifiedPlanObservation>);
static_assert(!HasOrderingOperator<UnifiedPlanObservation>);
static_assert(!std::is_constructible_v<
              UnifiedPlanDependencyAuthorityReference,
              ProviderSelectionCallback>);
static_assert(!HasExecuteMember<UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasResolveMember<UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasCommandMember<UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasArgvMember<UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasProviderSelectionMember<
              UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasOrderingOperator<
              UnifiedPlanDependencyAuthorityReference>);
static_assert(!HasExecuteMember<UnifiedPlanBuildUnitReference>);
static_assert(!HasResolveMember<UnifiedPlanBuildUnitReference>);
static_assert(!HasCommandMember<UnifiedPlanBuildUnitReference>);
static_assert(!HasArgvMember<UnifiedPlanBuildUnitReference>);
static_assert(!HasProviderSelectionMember<UnifiedPlanBuildUnitReference>);
static_assert(!HasOrderingOperator<UnifiedPlanBuildUnitReference>);
static_assert(!HasExecuteMember<RepositoryPackageTransactionIntent>);
static_assert(!HasCommandMember<RepositoryPackageTransactionIntent>);
static_assert(!HasArgvMember<RepositoryPackageTransactionIntent>);
static_assert(!HasExecuteMember<SourceBuiltArtifactInstallBoundaryIntent>);
static_assert(!HasCommandMember<SourceBuiltArtifactInstallBoundaryIntent>);
static_assert(!HasArgvMember<SourceBuiltArtifactInstallBoundaryIntent>);
static_assert(!std::is_constructible_v<
              SourceBuiltArtifactInstallBoundaryIntent,
              ProducedPackageArtifact>);
static_assert(!std::is_constructible_v<UnifiedPlanBlocker, std::string>);
static_assert(!std::same_as<
              AurPackageBaseBuildUnitReference,
              RequiredPackageArtifactTarget>);
static_assert(!std::same_as<
              RequiredArtifactTargetReference,
              ProducedPackageArtifact>);
static_assert(!std::is_copy_constructible_v<RequiredArtifactTargetReference>);
static_assert(!std::is_constructible_v<
              RequiredArtifactTargetReference,
              const RequiredArtifactTargetReference&&>);
static_assert(!std::is_constructible_v<
              RequiredArtifactTargetReference,
              UnifiedPlanBuildUnitReference,
              RequiredPackageArtifactTarget&&>);
static_assert(!std::is_constructible_v<
              AurPackageBaseBuildUnitReference,
              BuildPlan&&,
              std::size_t>);
static_assert(!std::is_constructible_v<
              LocalSourceBuildUnitReference,
              LocalSourceRootObservationIdentity,
              LocalPackageMetadata&&>);
static_assert(!std::is_constructible_v<
              UnifiedPlanConfiguredRepositoryOrderReference,
              std::vector<std::string>&&>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanBuildUnitReference>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanBlocker>);
static_assert(!std::is_constructible_v<
              UnifiedPlanBlocker, const UnifiedPlanBlocker&&>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanTransactionIntent>);
static_assert(!std::is_constructible_v<
              UnifiedPlanTransactionIntent,
              const UnifiedPlanTransactionIntent&&>);
static_assert(!std::is_copy_constructible_v<
              UnifiedPlanBorrowedAuthorityReference<
                      BuildPlanResolutionFailure>>);
static_assert(!std::is_constructible_v<
              UnifiedPlanBorrowedAuthorityReference<
                      BuildPlanResolutionFailure>,
              BuildPlanResolutionFailure&&>);
static_assert(!std::is_copy_constructible_v<UnknownUnifiedPlanBlocker>);
static_assert(!std::is_copy_constructible_v<
              SourceFailureUnifiedPlanBlocker>);
static_assert(!std::is_copy_constructible_v<
              BuildPlanStateUnifiedPlanBlocker>);
static_assert(!std::is_copy_constructible_v<
              RepositoryDependencyInstallIntent>);
static_assert(!std::is_copy_constructible_v<
              RepositoryProviderInstallIntent>);
static_assert(!std::is_copy_constructible_v<
              RepositoryInstallIntentTarget>);
using NestedSourceFailureBorrow = std::variant_alternative_t<
        0, SourceFailureUnifiedPlanBlockerDetail>;
static_assert(!std::is_copy_constructible_v<NestedSourceFailureBorrow>);
static_assert(!std::is_constructible_v<
              NestedSourceFailureBorrow,
              const NestedSourceFailureBorrow&&>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

const UnifiedPlanObservation& expect_valid(
        const UnifiedPlanObservationResult& result,
        std::string_view context) {
    expect(
            result.is_valid(),
            std::string(context) + " unexpectedly failed validation");
    expect(
            result.observation() != nullptr,
            std::string(context) + " has no observation");
    expect(
            result.failure() == nullptr,
            std::string(context) + " also has a failure");
    return *result.observation();
}

InvalidUnifiedPlanObservation expect_invalid(
        const UnifiedPlanObservationResult& result,
        const std::string& context) {
    expect(!result.is_valid(), context + " unexpectedly passed validation");
    expect(result.observation() == nullptr, context + " exposes observation");
    expect(result.failure() != nullptr, context + " has no failure");
    return *result.failure();
}

bool has_invariant_issue(
        const InvalidUnifiedPlanObservation& failure,
        UnifiedPlanObservationInvariantIssueKind expected) {
    for(const auto& issue : failure.issues) {
        if(issue.kind == expected) return true;
    }
    return false;
}

LocalSourceRootObservationIdentity local_source_identity(
        const std::string& path) {
    return LocalSourceRootObservationIdentity{
            path,
            LocalSourceDirectoryIdentity{
                    LocalSourceNodeType::Directory, 17, 23, 1000, 0755}};
}

UnifiedPlanRootReference repository_root(
        std::size_t invocation_index, const std::string& package_name,
        UnifiedPlanRootRouteKind route =
                UnifiedPlanRootRouteKind::RepositoryTransaction) {
    return UnifiedPlanRootReference(
            RootTargetIdentity{invocation_index, package_name},
            RepositoryRootPackageIdentity{"core", package_name}, route);
}

UnifiedPlanRootReference aur_root(
        std::size_t invocation_index, const std::string& package_name,
        const std::string& package_base) {
    return UnifiedPlanRootReference(
            RootTargetIdentity{invocation_index, package_name},
            AurRootPackageIdentity{package_name, package_base},
            UnifiedPlanRootRouteKind::AurSourceBuild);
}

UnifiedPlanRootReference local_root(
        std::size_t invocation_index, const std::string& correlation_name,
        const std::string& path) {
    return UnifiedPlanRootReference(
            RootTargetIdentity{invocation_index, correlation_name},
            local_source_identity(path),
            UnifiedPlanRootRouteKind::LocalSourceBuild);
}

LocalPackageMetadata local_metadata() {
    return LocalPackageMetadata{
            "shared-suite",
            std::nullopt,
            "1.0",
            "1",
            {"x86_64"},
            {LocalPackageMetadataChild{
                     "same-name", false, false, {}},
             LocalPackageMetadataChild{
                     "shared-runtime", false, false, {}}},
            {}};
}

BuildPlan build_plan_fixture() {
    BuildPlan plan;
    const RootTargetIdentity root{0, "same-name"};
    plan.root_targets.push_back(root);
    plan.order.push_back(BuildPlanEntry{
            "shared-suite", {"shared-runtime", "same-name"}});
    plan.package_targets.push_back(PlannedPackageTarget{
            "shared-runtime", "shared-suite",
            {PackageRole::RuntimeDependency}, {root}});
    plan.package_targets.push_back(PlannedPackageTarget{
            "same-name", "shared-suite", {PackageRole::Root}, {root}});

    const ConsumerDependencyRequirement requirement(
            "virtual-runtime>=2", "virtual-runtime",
            DependencyVersionConstraint{
                    DependencyVersionRelation::GreaterThanOrEqual, "2"});
    const ProvidedDependency provider = ProvidedDependency::from_repository(
            "extra", "runtime-provider", "virtual-runtime",
            "virtual-runtime=2", "2");
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "same-name",
            "shared-suite",
            "virtual-runtime>=2",
            PackageRole::RuntimeDependency,
            DependencyKind::Provided,
            "runtime-provider",
            std::nullopt,
            provider,
            ProviderResolutionKind::UserSelected,
            DependencyRequirement{requirement},
            ResolvedDependencyCandidate{ProviderResolvedDependencyCandidate{
                    provider,
                    ObservedVersion::available(
                            ObservedVersionSource::
                                    RepositoryProviderCapability,
                            "2")}},
            ConstraintEvaluation::satisfied()});
    plan.resolution_failures.push_back(BuildPlanResolutionFailure{
            BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
            "same-name",
            "shared-suite",
            "other-virtual",
            "other-virtual>=1",
            {root},
            "provider observation unavailable"});
    return plan;
}

BuildPlanDependencyEdge unknown_dependency_edge_fixture() {
    const ConsumerDependencyRequirement requirement(
            "unknown-runtime>=1", "unknown-runtime",
            DependencyVersionConstraint{
                    DependencyVersionRelation::GreaterThanOrEqual, "1"});
    return BuildPlanDependencyEdge{
            "consumer",
            "consumer-base",
            "unknown-runtime>=1",
            PackageRole::RuntimeDependency,
            DependencyKind::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            ProviderResolutionKind::Unique,
            DependencyRequirement{requirement},
            std::nullopt,
            ConstraintEvaluation::unknown(
                    ObservedVersionUnknownReason::MetadataQueryFailure)};
}

void test_status_invariants() {
    UnifiedPlanObservationInput ready_input;
    ready_input.status = UnifiedPlanObservationStatus::Ready;
    ready_input.roots = {repository_root(0, "ready-root")};
    const UnifiedPlanObservationResult ready_result =
            make_unified_plan_observation(std::move(ready_input));
    const UnifiedPlanObservation& ready =
            expect_valid(ready_result, "Ready observation");
    expect(
            ready.status() == UnifiedPlanObservationStatus::Ready,
            "Ready status was not retained");

    UnifiedPlanObservationInput no_op_input;
    no_op_input.status = UnifiedPlanObservationStatus::NoOp;
    no_op_input.roots = {repository_root(0, "no-op-root")};
    const UnifiedPlanObservationResult no_op_result =
            make_unified_plan_observation(std::move(no_op_input));
    const UnifiedPlanObservation& no_op =
            expect_valid(no_op_result, "NoOp observation");
    expect(
            no_op.status() == UnifiedPlanObservationStatus::NoOp,
            "NoOp status was not retained");
    expect(
            no_op.transaction_intents().empty(),
            "NoOp retained a mutation intent");

    const BuildPlanDependencyEdge unknown =
            unknown_dependency_edge_fixture();
    UnifiedPlanObservationInput blocked_input;
    blocked_input.status = UnifiedPlanObservationStatus::Blocked;
    blocked_input.blockers.push_back(UnknownUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge>(
                    unknown)});
    const UnifiedPlanObservationResult blocked_result =
            make_unified_plan_observation(std::move(blocked_input));
    const UnifiedPlanObservation& blocked =
            expect_valid(blocked_result, "Blocked observation");
    expect(
            blocked.status() == UnifiedPlanObservationStatus::Blocked,
            "Blocked status was not retained");
    expect(blocked.blockers().size() == 1, "Blocked detail was lost");

    UnifiedPlanObservationInput ready_with_blocker_input;
    ready_with_blocker_input.status = UnifiedPlanObservationStatus::Ready;
    ready_with_blocker_input.roots = {
            repository_root(0, "ready-root")};
    ready_with_blocker_input.blockers.push_back(
            UnknownUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            BuildPlanDependencyEdge>(unknown)});
    const auto ready_with_blocker = make_unified_plan_observation(
            std::move(ready_with_blocker_input));
    expect(
            has_invariant_issue(
                    expect_invalid(ready_with_blocker, "Ready with blocker"),
                    UnifiedPlanObservationInvariantIssueKind::
                            ReadyHasBlockers),
            "Ready blocker invariant was not reported");

    UnifiedPlanObservationInput empty_blocked_input;
    empty_blocked_input.status = UnifiedPlanObservationStatus::Blocked;
    const auto blocked_without_blocker = make_unified_plan_observation(
            std::move(empty_blocked_input));
    expect(
            has_invariant_issue(
                    expect_invalid(
                            blocked_without_blocker,
                            "Blocked without blocker"),
                    UnifiedPlanObservationInvariantIssueKind::
                            BlockedWithoutBlocker),
            "Blocked nonempty-detail invariant was not reported");

    RepositoryPackageTransactionIntent transaction;
    transaction.targets.push_back(RepositoryRootInstallIntent{
            repository_root(0, "no-op-root")});
    transaction.policy = RepositoryTransactionPolicyView{false};
    UnifiedPlanObservationInput no_op_intent_input;
    no_op_intent_input.status = UnifiedPlanObservationStatus::NoOp;
    no_op_intent_input.roots = {repository_root(0, "no-op-root")};
    no_op_intent_input.transaction_intents.push_back(
            std::move(transaction));
    const auto no_op_with_intent = make_unified_plan_observation(
            std::move(no_op_intent_input));
    expect(
            has_invariant_issue(
                    expect_invalid(no_op_with_intent, "NoOp with intent"),
                    UnifiedPlanObservationInvariantIssueKind::
                            NoOpHasMutationIntent),
            "NoOp mutation-intent invariant was not reported");

    UnifiedPlanObservationInput blocked_intent_input;
    blocked_intent_input.status = UnifiedPlanObservationStatus::Blocked;
    blocked_intent_input.roots = {repository_root(0, "blocked-root")};
    blocked_intent_input.blockers.push_back(UnknownUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge>(
                    unknown)});
    RepositoryPackageTransactionIntent blocked_transaction;
    blocked_transaction.targets.push_back(RepositoryRootInstallIntent{
            repository_root(0, "blocked-root")});
    blocked_intent_input.transaction_intents.push_back(
            std::move(blocked_transaction));
    const UnifiedPlanObservationResult blocked_with_intent =
            make_unified_plan_observation(
                    std::move(blocked_intent_input));
    expect(
            has_invariant_issue(
                    expect_invalid(
                            blocked_with_intent, "Blocked with intent"),
                    UnifiedPlanObservationInvariantIssueKind::
                            BlockedHasMutationIntent),
            "Blocked mutation-intent invariant was not reported");
}

void test_source_aware_root_identity() {
    const UnifiedPlanRootReference repository =
            repository_root(0, "same-name");
    const UnifiedPlanRootReference aur =
            aur_root(0, "same-name", "same-name-base");
    const UnifiedPlanRootReference local =
            local_root(0, "same-name", "/work/same-name");

    expect(
            repository.invocation_correlation() ==
                    aur.invocation_correlation(),
            "Invocation correlation should not become source identity");
    expect(
            repository.source_identity() != aur.source_identity(),
            "Repository and AUR roots were flattened");
    expect(
            aur.source_identity() != local.source_identity(),
            "AUR and local roots were flattened");
    expect(
            repository.source_kind() ==
                    UnifiedPlanRootSourceKind::Repository &&
                    aur.source_kind() == UnifiedPlanRootSourceKind::Aur &&
                    local.source_kind() == UnifiedPlanRootSourceKind::Local,
            "Root source kinds were not retained");
    expect(
            repository.route_kind() ==
                            UnifiedPlanRootRouteKind::
                                    RepositoryTransaction &&
                    aur.route_kind() ==
                            UnifiedPlanRootRouteKind::AurSourceBuild &&
                    local.route_kind() ==
                            UnifiedPlanRootRouteKind::LocalSourceBuild,
            "Root route kinds were not retained");

    const UnifiedPlanRootReference incomplete(
            RootTargetIdentity{0, "same-name"},
            AurRootPackageIdentity{"same-name", ""},
            UnifiedPlanRootRouteKind::AurSourceBuild);
    UnifiedPlanObservationInput incomplete_ready_input;
    incomplete_ready_input.status = UnifiedPlanObservationStatus::Ready;
    incomplete_ready_input.roots = {incomplete};
    const auto invalid_ready = make_unified_plan_observation(
            std::move(incomplete_ready_input));
    expect(
            has_invariant_issue(
                    expect_invalid(
                            invalid_ready, "Ready incomplete root identity"),
                    UnifiedPlanObservationInvariantIssueKind::
                            NonBlockedRootIdentityIncomplete),
            "Incomplete source-aware root was accepted as Ready");
}

void test_dependency_authority_reference() {
    const BuildPlan plan = build_plan_fixture();
    const UnifiedPlanDependencyAuthorityReference authority =
            UnifiedPlanDependencyAuthorityReference::from_build_plan(plan);

    expect(
            authority.build_plan() == &plan,
            "BuildPlan authority was copied instead of referenced");
    expect(
            authority.local_build_plan() == nullptr,
            "BuildPlan reference became a local authority");

    const BuildPlan& observed = *authority.build_plan();
    expect(
            &observed.dependency_edges.front() ==
                    &plan.dependency_edges.front(),
            "Typed dependency edge was copied");
    const BuildPlanDependencyEdge& edge = observed.dependency_edges.front();
    expect(edge.requirement.has_value(), "Requirement identity was lost");
    expect(
            edge.resolved_candidate.has_value(),
            "Resolved candidate identity was lost");
    expect(
            edge.resolved_provider.has_value() &&
                    std::holds_alternative<RepositoryProviderOrigin>(
                            edge.resolved_provider->origin),
            "Provider source identity was lost");
    expect(
            edge.constraint_evaluation.has_value() &&
                    edge.constraint_evaluation->satisfaction() ==
                            ConstraintSatisfaction::Satisfied,
            "Constraint state was lost");
    expect(
            observed.package_targets.back().roots == observed.root_targets,
            "Root attribution was lost");
    expect(
            observed.resolution_failures.front().roots ==
                    observed.root_targets,
            "Failure/root association was lost");
}

void test_build_unit_and_required_artifact_identity() {
    const BuildPlan plan = build_plan_fixture();
    const LocalPackageMetadata metadata = local_metadata();
    const AurPackageBaseBuildUnitReference inspected_aur_unit(
            std::cref(plan), 0);
    const LocalSourceBuildUnitReference inspected_local_unit(
            local_source_identity("/work/local-suite"),
            std::cref(metadata));
    expect(
            inspected_aur_unit.has_complete_identity(),
            "AUR build unit is incomplete");
    expect(
            inspected_local_unit.has_complete_identity(),
            "Local source build unit is incomplete");
    expect(
            inspected_aur_unit.entry()->package_base == "shared-suite",
            "AUR PackageBase identity differs");
    expect(
            inspected_local_unit.source_root().canonical_path ==
                    "/work/local-suite",
            "Local build unit lost source-root identity");

    const RequiredPackageArtifactTarget root_target{
            "shared-suite", "same-name", DesiredInstallReason::Explicit};
    const RequiredPackageArtifactTarget dependency_target{
            "shared-suite", "shared-runtime",
            DesiredInstallReason::Dependency};
    RequiredArtifactTargetReference root_artifact(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(root_target));
    RequiredArtifactTargetReference dependency_artifact(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(dependency_target));
    expect(
            root_artifact.matches_build_unit(),
            "Root artifact target does not match PackageBase unit");
    expect(
            dependency_artifact.matches_build_unit(),
            "Dependency artifact target does not match PackageBase unit");
    expect(
            root_artifact.target().package_name !=
                    inspected_aur_unit.entry()->package_base,
            "PackageBase and child package identity were conflated");
    const RequiredPackageArtifactTarget retained_target =
            root_artifact.target();
    expect(
            retained_target.package_base == root_target.package_base &&
                    retained_target.package_name == root_target.package_name &&
                    retained_target.desired_reason ==
                            root_target.desired_reason,
            "Required artifact target identity changed");

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.roots = {aur_root(0, "same-name", "shared-suite")};
    input.build_units.push_back(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0));
    input.build_units.push_back(LocalSourceBuildUnitReference(
            local_source_identity("/work/local-suite"),
            std::cref(metadata)));
    input.required_artifacts.push_back(std::move(root_artifact));
    input.required_artifacts.push_back(std::move(dependency_artifact));
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanObservation& observation = expect_valid(
            observation_result, "Build/artifact identity observation");
    expect(
            observation.build_units().size() == 2 &&
                    observation.required_artifacts().size() == 2,
            "Build unit and artifact target cardinality was flattened");

    const RequiredPackageArtifactTarget mismatch{
            "different-base", "same-name", DesiredInstallReason::Explicit};
    RequiredArtifactTargetReference mismatched_artifact(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(mismatch));
    UnifiedPlanObservationInput invalid_input;
    invalid_input.status = UnifiedPlanObservationStatus::Ready;
    invalid_input.roots = {aur_root(0, "same-name", "shared-suite")};
    invalid_input.build_units.push_back(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0));
    invalid_input.required_artifacts.push_back(
            std::move(mismatched_artifact));
    expect(
            has_invariant_issue(
                    expect_invalid(
                            make_unified_plan_observation(
                                    std::move(invalid_input)),
                            "Mismatched required artifact"),
                    UnifiedPlanObservationInvariantIssueKind::
                            RequiredArtifactBuildUnitMismatch),
            "Artifact/build-unit mismatch was not rejected");
}

void test_typed_blockers() {
    const BuildPlanDependencyEdge unknown =
            unknown_dependency_edge_fixture();
    const BuildPlanResolutionFailure source_failure{
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            "consumer",
            "consumer-base",
            "missing-dependency",
            "missing-dependency>=1",
            {RootTargetIdentity{0, "consumer"}},
            "repository metadata unavailable"};
    const AmbiguousProvidedDependency ambiguous{
            "virtual",
            {ProvidedDependency::from_repository(
                     "core", "provider-one"),
             ProvidedDependency::from_aur(
                     "provider-two", "provider-two-base", "virtual",
                     "virtual", std::nullopt)}};
    const MixedPackageBaseInstallReasonUnsupported unsupported{
            "mixed-suite", {}};
    const IncompleteProviderCandidateSet partial_source{
            "partial-virtual",
            {ProvidedDependency::from_repository(
                    "core", "observed-provider")},
            ObservedVersionUnknownReason::PartialSourceFailure};
    const ConstraintEvaluation constraint =
            ConstraintEvaluation::unsatisfied();
    const BuildPlanMetadataRisk metadata_risk{
            "risky-child", "risky-base", {"old-package"},
            {"replacement"}};
    const AurUpdateExecutionIssue route_preflight{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            "route-package",
            "route-base",
            std::nullopt,
            "route preflight blocked"};

    std::vector<UnifiedPlanBlocker> blockers;
    blockers.push_back(UnknownUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge>(
                    unknown)});
    blockers.push_back(AmbiguousUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<
                    AmbiguousProvidedDependency>(ambiguous)});
    blockers.push_back(UnsupportedUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<
                    MixedPackageBaseInstallReasonUnsupported>(unsupported)});
    blockers.push_back(SourceFailureUnifiedPlanBlocker{
            SourceFailureUnifiedPlanBlockerDetail{
                    UnifiedPlanBorrowedAuthorityReference<
                            BuildPlanResolutionFailure>(source_failure)}});
    blockers.push_back(SourceFailureUnifiedPlanBlocker{
            SourceFailureUnifiedPlanBlockerDetail{
                    UnifiedPlanBorrowedAuthorityReference<
                            IncompleteProviderCandidateSet>(partial_source)}});
    blockers.push_back(ConstraintFailureUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<ConstraintEvaluation>(
                    constraint)});
    blockers.push_back(MetadataRiskUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlanMetadataRisk>(
                    metadata_risk)});
    blockers.push_back(RoutePreflightUnifiedPlanBlocker{
            RoutePreflightUnifiedPlanBlockerDetail{
                    UnifiedPlanBorrowedAuthorityReference<
                            AurUpdateExecutionIssue>(route_preflight)}});

    UnifiedPlanObservationInput partial_ready_input;
    partial_ready_input.status = UnifiedPlanObservationStatus::Ready;
    partial_ready_input.blockers.push_back(
            SourceFailureUnifiedPlanBlocker{
                    SourceFailureUnifiedPlanBlockerDetail{
                            UnifiedPlanBorrowedAuthorityReference<
                                    IncompleteProviderCandidateSet>(
                                    partial_source)}});
    const auto partial_ready = make_unified_plan_observation(
            std::move(partial_ready_input));
    expect(
            has_invariant_issue(
                    expect_invalid(
                            partial_ready,
                            "Ready partial-source observation"),
                    UnifiedPlanObservationInvariantIssueKind::
                            ReadyHasBlockers),
            "Partial source failure was accepted as Ready");

    expect(
            std::holds_alternative<UnknownUnifiedPlanBlocker>(blockers[0]) &&
                    std::holds_alternative<AmbiguousUnifiedPlanBlocker>(
                            blockers[1]) &&
                    std::holds_alternative<UnsupportedUnifiedPlanBlocker>(
                            blockers[2]) &&
                    std::holds_alternative<SourceFailureUnifiedPlanBlocker>(
                            blockers[3]) &&
                    std::holds_alternative<SourceFailureUnifiedPlanBlocker>(
                            blockers[4]) &&
                    std::holds_alternative<ConstraintFailureUnifiedPlanBlocker>(
                            blockers[5]) &&
                    std::holds_alternative<MetadataRiskUnifiedPlanBlocker>(
                            blockers[6]) &&
                    std::holds_alternative<RoutePreflightUnifiedPlanBlocker>(
                            blockers[7]),
            "Typed blockers were flattened into one failure kind");

    const auto& resolution_source_blocker =
            std::get<SourceFailureUnifiedPlanBlocker>(blockers[3]);
    const auto* resolution_failure_reference = std::get_if<
            UnifiedPlanBorrowedAuthorityReference<
                    BuildPlanResolutionFailure>>(
            &resolution_source_blocker.detail);
    expect(
            resolution_failure_reference != nullptr &&
                    &resolution_failure_reference->get() == &source_failure &&
                    resolution_failure_reference->get().kind ==
                            BuildPlanResolutionFailureKind::
                                    RepositoryMetadataUnavailable,
            "Typed BuildPlan source failure was copied or reclassified");

    const auto& source_blocker =
            std::get<SourceFailureUnifiedPlanBlocker>(blockers[4]);
    const auto* partial_reference = std::get_if<
            UnifiedPlanBorrowedAuthorityReference<
                    IncompleteProviderCandidateSet>>(
            &source_blocker.detail);
    expect(
            partial_reference != nullptr &&
                    &partial_reference->get() == &partial_source &&
                    partial_reference->get().observed_candidates.size() == 1 &&
                    partial_reference->get().reason ==
                            ObservedVersionUnknownReason::
                                    PartialSourceFailure,
            "Partial source candidates/reason were not retained diagnostically");

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Blocked;
    input.blockers = std::move(blockers);
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanObservation& observation =
            expect_valid(observation_result, "Typed blocker observation");
    expect(
            observation.blockers().size() == 8,
            "Blocked taxonomy lost typed details");
}

void test_transaction_intent_boundaries() {
    const UnifiedPlanRootReference root = repository_root(0, "root-package");
    const RepositoryExactPackage dependency{
            ConfiguredRepositoryIdentity{"extra", 1},
            "exact-dependency",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage, "3.0-1"),
            {}};
    const ProvidedDependency provider =
            ProvidedDependency::from_repository("core", "provider-package");
    RepositoryPackageTransactionIntent repository_intent;
    repository_intent.targets.push_back(RepositoryRootInstallIntent{root});
    repository_intent.targets.push_back(
            RepositoryDependencyInstallIntent{
                    UnifiedPlanBorrowedAuthorityReference<
                            RepositoryExactPackage>(dependency)});
    repository_intent.targets.push_back(
            RepositoryProviderInstallIntent{
                    UnifiedPlanBorrowedAuthorityReference<ProvidedDependency>(
                            provider)});
    repository_intent.policy = RepositoryTransactionPolicyView{true};

    UnifiedPlanObservationInput repository_input;
    repository_input.status = UnifiedPlanObservationStatus::Ready;
    repository_input.roots = {root};
    repository_input.transaction_intents.push_back(
            std::move(repository_intent));
    const UnifiedPlanObservationResult repository_result =
            make_unified_plan_observation(std::move(repository_input));
    const UnifiedPlanObservation& repository_observation = expect_valid(
            repository_result, "Repository transaction intent");
    const auto& retained_repository_intent =
            std::get<RepositoryPackageTransactionIntent>(
                    repository_observation.transaction_intents().front());
    expect(
            retained_repository_intent.targets.size() == 3 &&
                    retained_repository_intent.policy.needed,
            "Repository root/dependency/provider intent was flattened");
    expect(
            &std::get<RepositoryDependencyInstallIntent>(
                     retained_repository_intent.targets[1])
                     .package.get() == &dependency &&
                    &std::get<RepositoryProviderInstallIntent>(
                             retained_repository_intent.targets[2])
                             .provider.get() == &provider,
            "Exact-known repository targets were copied or reconstructed");

    const BuildPlan plan = build_plan_fixture();
    const RequiredPackageArtifactTarget root_target{
            "shared-suite", "same-name", DesiredInstallReason::Explicit};
    const RequiredPackageArtifactTarget dependency_target{
            "shared-suite", "shared-runtime",
            DesiredInstallReason::Dependency};
    RequiredArtifactTargetReference root_artifact(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(root_target));
    RequiredArtifactTargetReference dependency_artifact(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(dependency_target));
    SourceBuiltArtifactInstallBoundaryIntent source_intent;
    source_intent.targets.push_back(SourceRootArtifactInstallIntent{0});
    source_intent.targets.push_back(
            SourceDependencyArtifactInstallIntent{1});
    source_intent.needed = true;

    UnifiedPlanObservationInput source_input;
    source_input.status = UnifiedPlanObservationStatus::Ready;
    source_input.roots = {aur_root(0, "same-name", "shared-suite")};
    source_input.build_units.push_back(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0));
    source_input.required_artifacts.push_back(std::move(root_artifact));
    source_input.required_artifacts.push_back(
            std::move(dependency_artifact));
    source_input.transaction_intents.push_back(std::move(source_intent));
    const UnifiedPlanObservationResult source_result =
            make_unified_plan_observation(std::move(source_input));
    const UnifiedPlanObservation& source_observation = expect_valid(
            source_result, "Source artifact install boundary");
    const auto& retained_source_intent =
            std::get<SourceBuiltArtifactInstallBoundaryIntent>(
                    source_observation.transaction_intents().front());
    expect(
            retained_source_intent.targets.size() == 2 &&
                    std::holds_alternative<SourceRootArtifactInstallIntent>(
                            retained_source_intent.targets[0]) &&
                    std::holds_alternative<
                            SourceDependencyArtifactInstallIntent>(
                            retained_source_intent.targets[1]),
            "Root/dependency artifact install intent was flattened");
}

void test_phase_and_owner_vocabulary() {
    const std::array<UnifiedPlanObservationPhase, 10> expected_order{
            UnifiedPlanObservationPhase::RequestDiscovery,
            UnifiedPlanObservationPhase::MetadataDiscovery,
            UnifiedPlanObservationPhase::ProviderDecision,
            UnifiedPlanObservationPhase::ExecutionProjection,
            UnifiedPlanObservationPhase::RepositoryTransaction,
            UnifiedPlanObservationPhase::SourceRetrieval,
            UnifiedPlanObservationPhase::SourceBuild,
            UnifiedPlanObservationPhase::ArtifactValidation,
            UnifiedPlanObservationPhase::SourceArtifactInstall,
            UnifiedPlanObservationPhase::CleanupReduction};
    expect(
            UNIFIED_PLAN_OBSERVATION_PHASE_ORDER == expected_order,
            "Observation phase order differs from the authority contract");

    std::vector<UnifiedPlanPhaseReference> phases{
            {UnifiedPlanObservationPhase::RequestDiscovery,
             UnifiedPlanAuthorityOwner::Moguet,
             ExistingRoutePhaseReference{
                     SystemSourceUpgradePhase::Preparation}},
            {UnifiedPlanObservationPhase::MetadataDiscovery,
             UnifiedPlanAuthorityOwner::AurRpc,
             std::nullopt},
            {UnifiedPlanObservationPhase::ExecutionProjection,
             UnifiedPlanAuthorityOwner::Libalpm,
             ExistingRoutePhaseReference{
                     UpgradeAllOperationPhase::Preparation}},
            {UnifiedPlanObservationPhase::RepositoryTransaction,
             UnifiedPlanAuthorityOwner::Pacman,
             std::nullopt},
            {UnifiedPlanObservationPhase::SourceRetrieval,
             UnifiedPlanAuthorityOwner::Git,
             std::nullopt},
            {UnifiedPlanObservationPhase::SourceBuild,
             UnifiedPlanAuthorityOwner::Makepkg,
             std::nullopt}};

    UnifiedPlanObservationInput ordered_input;
    ordered_input.status = UnifiedPlanObservationStatus::Ready;
    ordered_input.phases = phases;
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(ordered_input));
    const UnifiedPlanObservation& observation =
            expect_valid(observation_result, "Ordered phase observation");
    expect(
            observation.phases()[0].existing_route_phase.has_value() &&
                    std::holds_alternative<SystemSourceUpgradePhase>(
                            observation.phases()[0]
                                    .existing_route_phase.value()) &&
                    std::holds_alternative<UpgradeAllOperationPhase>(
                            observation.phases()[2]
                                    .existing_route_phase.value()),
            "Existing route-specific phase reference was replaced");
    expect(
            observation.phases()[1].owner ==
                            UnifiedPlanAuthorityOwner::AurRpc &&
                    observation.phases()[3].owner ==
                            UnifiedPlanAuthorityOwner::Pacman &&
                    observation.phases()[4].owner ==
                            UnifiedPlanAuthorityOwner::Git &&
                    observation.phases()[5].owner ==
                            UnifiedPlanAuthorityOwner::Makepkg,
            "External owner attribution was lost");

    UnifiedPlanObservationInput reversed_input;
    reversed_input.status = UnifiedPlanObservationStatus::Ready;
    reversed_input.phases = {
            {UnifiedPlanObservationPhase::SourceBuild,
             UnifiedPlanAuthorityOwner::Makepkg,
             std::nullopt},
            {UnifiedPlanObservationPhase::MetadataDiscovery,
             UnifiedPlanAuthorityOwner::AurRpc,
             std::nullopt}};
    expect(
            has_invariant_issue(
                    expect_invalid(
                            make_unified_plan_observation(
                                    std::move(reversed_input)),
                            "Reversed phase observation"),
                    UnifiedPlanObservationInvariantIssueKind::
                            ObservationPhaseOrderInvalid),
            "Out-of-order observation phase was accepted");
}

} // namespace

int main() {
    try {
        test_status_invariants();
        test_source_aware_root_identity();
        test_dependency_authority_reference();
        test_build_unit_and_required_artifact_identity();
        test_typed_blockers();
        test_transaction_intent_boundaries();
        test_phase_and_owner_vocabulary();
    } catch(const std::exception& error) {
        std::cerr << "unified plan observation test failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "unified plan observation tests passed\n";
    return 0;
}
