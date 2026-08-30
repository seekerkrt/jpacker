#pragma once

#include "invocation_owned_cleanup_model.hpp"
#include "package_metadata.hpp"
#include "source_artifact_install_receipt_evidence.hpp"
#include "source_install.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class CleanupLifecycleBoundary {
    BeforeBuildCompletion,
    AfterWorkItem,
    AfterSuccessfulInvocation,
    AfterInvocationCompletion,
    Unknown,
};

enum class CleanupObservationPhase {
    BeforeFirstDependencyMutation,
    AfterFullSupportedInvocationSuccess,
};

enum class CleanupWorkItemOutcomeShape {
    ValidSucceeded,
    ValidFailed,
    ValidNotAttempted,
    Invalid,
};

[[nodiscard]] CleanupWorkItemOutcomeShape
validate_production_source_build_work_item_outcome(
    const ProductionSourceBuildWorkItemOutcome& outcome) noexcept;

// The evidence borrows existing production values. It adds only the lifecycle
// boundary that current result types do not express; it does not copy command
// output or invent package-level transaction ownership.
class CleanupInvocationLifecycleEvidence final {
public:
    [[nodiscard]] static CleanupInvocationLifecycleEvidence unknown() noexcept;
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    before_build_completion(
        const CleanupInvocationSession& session);
    [[nodiscard]] static CleanupInvocationLifecycleEvidence after_work_item(
        const CleanupInvocationSession& session,
        const ProductionSourceBuildInvocationResult& result,
        std::size_t completed_work_item_index);
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    after_successful_invocation(
        const CleanupInvocationSession& session,
        const ProductionSourceBuildInvocationResult& result);
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    after_invocation_completion(
        const CleanupInvocationSession& session,
        const ProductionSourceBuildInvocationResult& result);

    [[nodiscard]] CleanupLifecycleBoundary boundary() const noexcept;
    [[nodiscard]] const std::optional<CleanupInvocationAuthority>& authority()
        const noexcept;
    [[nodiscard]] const PreparedProductionSourceBuildInvocation* invocation()
        const noexcept;
    [[nodiscard]] const ProductionSourceBuildInvocationResult* result()
        const noexcept;
    [[nodiscard]] const std::optional<std::size_t>&
    completed_work_item_index() const noexcept;

private:
    CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary boundary,
        std::optional<CleanupInvocationAuthority> authority,
        std::optional<std::reference_wrapper<
            const PreparedProductionSourceBuildInvocation>>
            invocation,
        std::optional<std::reference_wrapper<
            const ProductionSourceBuildInvocationResult>>
            result,
        std::optional<std::size_t> completed_work_item_index) noexcept;

    CleanupLifecycleBoundary boundary_;
    std::optional<CleanupInvocationAuthority> authority_;
    std::optional<std::reference_wrapper<
        const PreparedProductionSourceBuildInvocation>>
        invocation_;
    std::optional<std::reference_wrapper<
        const ProductionSourceBuildInvocationResult>>
        result_;
    std::optional<std::size_t> completed_work_item_index_;
};

class CleanupBaselineSnapshotObservation final {
public:
    CleanupBaselineSnapshotObservation() = delete;
    CleanupBaselineSnapshotObservation(
        const CleanupBaselineSnapshotObservation&) = default;
    CleanupBaselineSnapshotObservation(
        CleanupBaselineSnapshotObservation&&) noexcept = default;
    CleanupBaselineSnapshotObservation& operator=(
        const CleanupBaselineSnapshotObservation&) = default;
    CleanupBaselineSnapshotObservation& operator=(
        CleanupBaselineSnapshotObservation&&) noexcept = default;
    ~CleanupBaselineSnapshotObservation() = default;

    [[nodiscard]] const CleanupInvocationAuthority& authority()
        const noexcept;
    [[nodiscard]] CleanupObservationPhase phase() const noexcept;
    [[nodiscard]] const InstalledPackageStateSnapshotResult& snapshot()
        const noexcept;

private:
    CleanupBaselineSnapshotObservation(
        CleanupInvocationAuthority authority,
        InstalledPackageStateSnapshotResult snapshot) noexcept;

    CleanupInvocationAuthority authority_;
    InstalledPackageStateSnapshotResult snapshot_;

    friend class CleanupPhaseObservationProducer;
};

class CleanupCurrentInstalledObservation final {
public:
    CleanupCurrentInstalledObservation() = delete;
    CleanupCurrentInstalledObservation(
        const CleanupCurrentInstalledObservation&) = default;
    CleanupCurrentInstalledObservation(
        CleanupCurrentInstalledObservation&&) noexcept = default;
    CleanupCurrentInstalledObservation& operator=(
        const CleanupCurrentInstalledObservation&) = default;
    CleanupCurrentInstalledObservation& operator=(
        CleanupCurrentInstalledObservation&&) noexcept = default;
    ~CleanupCurrentInstalledObservation() = default;

    [[nodiscard]] const CleanupInvocationAuthority& authority()
        const noexcept;
    [[nodiscard]] CleanupObservationPhase phase() const noexcept;
    [[nodiscard]] const InstalledPackageStateSnapshotResult& snapshot()
        const noexcept;
    [[nodiscard]] std::size_t completed_transaction_count() const noexcept;

private:
    CleanupCurrentInstalledObservation(
        CleanupInvocationAuthority authority,
        CleanupObservationPhase phase,
        std::size_t completed_transaction_count,
        InstalledPackageStateSnapshotResult snapshot) noexcept;

    CleanupInvocationAuthority authority_;
    CleanupObservationPhase phase_;
    std::size_t completed_transaction_count_;
    InstalledPackageStateSnapshotResult snapshot_;

    friend class CleanupPhaseObservationProducer;
};

class CleanupPolicyObservation final {
public:
    CleanupPolicyObservation() = delete;
    CleanupPolicyObservation(const CleanupPolicyObservation&) = default;
    CleanupPolicyObservation(CleanupPolicyObservation&&) noexcept = default;
    CleanupPolicyObservation& operator=(const CleanupPolicyObservation&) =
        default;
    CleanupPolicyObservation& operator=(CleanupPolicyObservation&&) noexcept =
        default;
    ~CleanupPolicyObservation() = default;

    [[nodiscard]] const CleanupInvocationAuthority& authority()
        const noexcept;
    [[nodiscard]] CleanupObservationPhase phase() const noexcept;
    [[nodiscard]] const CleanupPolicyProtectionEvidence& evidence()
        const noexcept;
    [[nodiscard]] std::size_t completed_transaction_count() const noexcept;

private:
    CleanupPolicyObservation(
        CleanupInvocationAuthority authority,
        CleanupObservationPhase phase,
        std::size_t completed_transaction_count,
        CleanupPolicyProtectionEvidence evidence) noexcept;

    CleanupInvocationAuthority authority_;
    CleanupObservationPhase phase_;
    std::size_t completed_transaction_count_;
    CleanupPolicyProtectionEvidence evidence_;

    friend class CleanupPhaseObservationProducer;
};

[[nodiscard]] CleanupBaselineSnapshotObservation
observe_cleanup_baseline_before_first_dependency_mutation(
    CleanupInvocationSession& session);

[[nodiscard]] CleanupCurrentInstalledObservation
observe_cleanup_current_after_full_supported_invocation(
    CleanupInvocationSession& session,
    const ProductionSourceBuildInvocationResult& result);

[[nodiscard]] CleanupPolicyObservation
observe_cleanup_policy_after_full_supported_invocation(
    CleanupInvocationSession& session,
    const ProductionSourceBuildInvocationResult& result,
    const std::string& candidate_package_name);

#ifdef MOGUET_ENABLE_CLEANUP_INVOCATION_SESSION_TEST_HOOKS
[[nodiscard]] CleanupBaselineSnapshotObservation
make_cleanup_baseline_observation_for_test(
    CleanupInvocationSession& session,
    InstalledPackageStateSnapshotResult snapshot);

[[nodiscard]] CleanupCurrentInstalledObservation
make_cleanup_current_observation_for_test(
    CleanupInvocationSession& session,
    InstalledPackageStateSnapshotResult snapshot,
    CleanupObservationPhase phase = CleanupObservationPhase::
        AfterFullSupportedInvocationSuccess);

[[nodiscard]] CleanupPolicyObservation
make_cleanup_policy_observation_for_test(
    CleanupInvocationSession& session,
    CleanupPolicyProtectionEvidence evidence,
    CleanupObservationPhase phase = CleanupObservationPhase::
        AfterFullSupportedInvocationSuccess);
#endif

enum class CleanupLifecycleProjectionIssueKind {
    BaselineSnapshotUnavailable,
    CurrentSnapshotUnavailable,
    CandidateSourceIdentityUnavailable,
    BuildPlanIncomplete,
    BuildPlanCoverageUnknown,
    BuildPlanArtifactProjectionFailed,
    PackageBaseSourceIdentityIncomplete,
    DependencyCorrelationIncomplete,
    RepositoryProviderPackageBaseUnavailable,
    RepositoryProviderProvenanceIncomplete,
    LifecycleEvidenceIncomplete,
    CausalOwnershipUnavailable,
    PolicyProtectionUnavailable,
};

struct CleanupLifecycleProjectionIssue {
    CleanupLifecycleProjectionIssueKind kind;
    std::optional<std::size_t> build_plan_edge_index;
    std::optional<std::string> package_name;
    std::optional<PackageMetadataFailure> metadata_failure;
};

struct InvocationOwnedCleanupCandidateProjectionSuccess {
    InvocationOwnedCleanupCandidate candidate;
    std::vector<CleanupLifecycleProjectionIssue> issues;
};

struct InvocationOwnedCleanupCandidateProjectionFailure {
    std::vector<CleanupLifecycleProjectionIssue> issues;
};

using InvocationOwnedCleanupCandidateProjectionResult = std::variant<
    InvocationOwnedCleanupCandidateProjectionSuccess,
    InvocationOwnedCleanupCandidateProjectionFailure>;

enum class CleanupSourceArtifactCorrelationIssueKind {
    InvocationAuthorityMissing,
    InvocationMismatch,
    LifecycleInvocationMismatch,
    WorkItemIndexOutOfRange,
    WorkItemPackageBaseMismatch,
    WorkItemOutcomeNotSuccessful,
    SelectedArtifactMissing,
    SelectedArtifactTargetMismatch,
    DependencyEdgeAttributionMissing,
    DependencyEdgeAttributionMismatch,
    DependencyEdgeOutOfRange,
    DependencyEdgeRequirementIncomplete,
    DependencyEdgeConstraintIncomplete,
    DependencyEdgeIdentityMismatch,
    DependencyRoleMismatch,
    RequestedRootAttributionMismatch,
    UncorrelatedActualInstall,
};

struct CleanupSourceArtifactSelectedCorrelation {
    SourceArtifactInstallCorrelatedSelectedArtifact artifact;
    std::vector<CleanupPackageCorrelation> dependency_correlations;
};

class CleanupSourceArtifactCorrelationEvidence final {
public:
    CleanupSourceArtifactCorrelationEvidence() = delete;
    CleanupSourceArtifactCorrelationEvidence(
        const CleanupSourceArtifactCorrelationEvidence&) = default;
    CleanupSourceArtifactCorrelationEvidence(
        CleanupSourceArtifactCorrelationEvidence&&) noexcept = default;
    CleanupSourceArtifactCorrelationEvidence& operator=(
        const CleanupSourceArtifactCorrelationEvidence&) = default;
    CleanupSourceArtifactCorrelationEvidence& operator=(
        CleanupSourceArtifactCorrelationEvidence&&) noexcept = default;
    ~CleanupSourceArtifactCorrelationEvidence() = default;

    [[nodiscard]] CleanupEvidenceCompleteness completeness() const noexcept;
    [[nodiscard]] const CleanupInvocationAuthority& authority()
        const noexcept;
    [[nodiscard]] const SourceArtifactInstallCausalEvidence& causal_evidence()
        const noexcept;
    [[nodiscard]] std::size_t work_item_index() const noexcept;
    [[nodiscard]] const std::string& package_base() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSourceArtifactSelectedCorrelation>&
    selected_artifacts() const noexcept;
    [[nodiscard]] const std::vector<std::string>& actual_install_set()
        const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSourceArtifactCorrelationIssueKind>&
    issues() const noexcept;

private:
    CleanupSourceArtifactCorrelationEvidence(
        CleanupEvidenceCompleteness completeness,
        CleanupInvocationAuthority authority,
        SourceArtifactInstallCausalEvidence causal_evidence,
        std::size_t work_item_index,
        std::string package_base,
        std::vector<CleanupSourceArtifactSelectedCorrelation>
            selected_artifacts,
        std::vector<std::string> actual_install_set,
        std::vector<CleanupSourceArtifactCorrelationIssueKind> issues) noexcept;

    CleanupEvidenceCompleteness completeness_;
    CleanupInvocationAuthority authority_;
    SourceArtifactInstallCausalEvidence causal_evidence_;
    std::size_t work_item_index_;
    std::string package_base_;
    std::vector<CleanupSourceArtifactSelectedCorrelation>
        selected_artifacts_;
    std::vector<std::string> actual_install_set_;
    std::vector<CleanupSourceArtifactCorrelationIssueKind> issues_;

    friend CleanupSourceArtifactCorrelationEvidence
    correlate_source_artifact_install_to_build_plan(
        const CleanupInvocationSession& session,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        const SourceArtifactInstallCausalEvidence& causal_evidence);
};

[[nodiscard]] CleanupSourceArtifactCorrelationEvidence
correlate_source_artifact_install_to_build_plan(
    const CleanupInvocationSession& session,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const SourceArtifactInstallCausalEvidence& causal_evidence);

enum class CleanupSelectedProviderCorrelationIssueKind {
    InvocationMismatch,
    LifecycleInvocationMismatch,
    CurrentObservationMismatch,
    BuildPlanIncomplete,
    BindingSetMissing,
    BindingSetMismatch,
    DependencyEdgeOutOfRange,
    DependencyEdgeNotSelectedRepositoryProvider,
    DependencyEdgeAttributionMismatch,
    SelectedDecisionMismatch,
    RepositoryProvenanceMismatch,
    PackageBaseMissing,
    ProviderIdentityMismatch,
    RequirementMismatch,
    ProvidedCapabilityMismatch,
    CurrentPackageIdentityIncomplete,
    CurrentPackageIdentityMismatch,
    ProviderNotInstalled,
    UncorrelatedActualInstall,
};

struct CleanupSelectedProviderEdgeCorrelation {
    std::size_t work_item_index;
    std::size_t build_plan_edge_index;
    DependencyRequirement requirement;
    BuildPlanProvidedDependency selected_decision;
    ProvidedDependency provider;
    SourceAwarePackageIdentity package_identity;
    CleanupCurrentPackageEvidence current_package;
};

class CleanupSelectedProviderCorrelationEvidence final {
public:
    CleanupSelectedProviderCorrelationEvidence() = delete;
    CleanupSelectedProviderCorrelationEvidence(
        const CleanupSelectedProviderCorrelationEvidence&) = default;
    CleanupSelectedProviderCorrelationEvidence(
        CleanupSelectedProviderCorrelationEvidence&&) noexcept = default;
    CleanupSelectedProviderCorrelationEvidence& operator=(
        const CleanupSelectedProviderCorrelationEvidence&) = default;
    CleanupSelectedProviderCorrelationEvidence& operator=(
        CleanupSelectedProviderCorrelationEvidence&&) noexcept = default;
    ~CleanupSelectedProviderCorrelationEvidence() = default;

    [[nodiscard]] CleanupEvidenceCompleteness completeness() const noexcept;
    [[nodiscard]] const CleanupInvocationAuthority& authority()
        const noexcept;
    [[nodiscard]] const std::string& transaction_token() const noexcept;
    [[nodiscard]] const std::vector<CleanupSelectedProviderEdgeCorrelation>&
    edge_correlations() const noexcept;
    [[nodiscard]] const std::vector<std::string>& actual_install_set()
        const noexcept;
    [[nodiscard]] const SelectedRepositoryProviderTrustedExecutionEvidence&
    execution() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSelectedProviderCorrelationIssueKind>&
    issues() const noexcept;

private:
    CleanupSelectedProviderCorrelationEvidence(
        CleanupEvidenceCompleteness completeness,
        CleanupInvocationAuthority authority,
        std::string transaction_token,
        std::vector<CleanupSelectedProviderEdgeCorrelation>
            edge_correlations,
        std::vector<std::string> actual_install_set,
        SelectedRepositoryProviderTrustedExecutionEvidence execution,
        std::vector<CleanupSelectedProviderCorrelationIssueKind>
            issues) noexcept;

    CleanupEvidenceCompleteness completeness_;
    CleanupInvocationAuthority authority_;
    std::string transaction_token_;
    std::vector<CleanupSelectedProviderEdgeCorrelation>
        edge_correlations_;
    std::vector<std::string> actual_install_set_;
    SelectedRepositoryProviderTrustedExecutionEvidence execution_;
    std::vector<CleanupSelectedProviderCorrelationIssueKind> issues_;

    friend CleanupSelectedProviderCorrelationEvidence
    correlate_selected_repository_provider_to_build_plan(
        const CleanupInvocationSession& session,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        const CleanupCurrentInstalledObservation& current_observation,
        const SelectedRepositoryProviderTrustedExecutionEvidence& execution);
};

[[nodiscard]] CleanupSelectedProviderCorrelationEvidence
correlate_selected_repository_provider_to_build_plan(
    const CleanupInvocationSession& session,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const CleanupCurrentInstalledObservation& current_observation,
    const SelectedRepositoryProviderTrustedExecutionEvidence& execution);

enum class CleanupInvocationEvidenceIssueKind {
    UnsupportedRoute,
    RouteAuthorityUnknown,
    RemoteAurBuildPlanMissing,
    BuildPlanIncomplete,
    RootInventoryIncomplete,
    WorkItemInventoryIncomplete,
    PackageBaseInventoryIncomplete,
    WorkItemOutcomeFailed,
    WorkItemOutcomeNotAttempted,
    WorkItemOutcomeUnknown,
    WorkItemOutcomeInvalid,
    DependencyEdgeInventoryEmpty,
    CleanupRelevantEdgeInventoryEmpty,
    DependencyEdgeUnsupportedOrUnowned,
    DependencyEdgeInvalidOrUnknown,
    DependencyEdgeAttributionMismatch,
    SourceArtifactCorrelationMissing,
    SourceArtifactCorrelationUnexpected,
    SelectedProviderCorrelationMissing,
    SelectedProviderCorrelationUnexpected,
    CorrelationInvocationMismatch,
    CorrelationIncomplete,
    TransactionTokenInventoryMissing,
    TransactionTokenInventoryUnexpected,
    TransactionTokenOwnerMismatch,
    TransactionTokenDuplicate,
    PhaseObservationMissing,
    PhaseObservationMismatch,
    MakepkgSyncDependenciesUnowned,
    UncorrelatedActualInstall,
};

enum class CleanupDependencyEdgeClassificationKind {
    SupportedOwnerSpecificReceipt,
    AuthoritativelyPreExistingOrIrrelevant,
    UnsupportedOrUnowned,
    InvalidOrUnknown,
};

struct CleanupDependencyEdgeClassification {
    std::size_t build_plan_edge_index;
    CleanupDependencyEdgeClassificationKind classification;
    std::optional<InvocationDependencyTransactionOwner> owner;
    std::optional<std::size_t> work_item_index;

    bool operator==(const CleanupDependencyEdgeClassification&) const =
        default;
};

struct CleanupInvocationWorkItemEvidence {
    std::size_t work_item_index;
    PackageBaseIdentity package_base;
    std::vector<RequiredPackageArtifactTarget> required_targets;
    std::vector<std::size_t> build_plan_dependency_edge_indices;
    std::vector<std::size_t> selected_repository_provider_edge_indices;
    ProductionSourceBuildWorkItemOutcome outcome;
};

class CleanupInvocationEvidence final {
public:
    CleanupInvocationEvidence() = delete;
    CleanupInvocationEvidence(const CleanupInvocationEvidence&) = default;
    CleanupInvocationEvidence(CleanupInvocationEvidence&&) noexcept = default;
    CleanupInvocationEvidence& operator=(
        const CleanupInvocationEvidence&) = default;
    CleanupInvocationEvidence& operator=(
        CleanupInvocationEvidence&&) noexcept = default;
    ~CleanupInvocationEvidence() = default;

    [[nodiscard]] CleanupRouteKind route_kind() const noexcept;
    [[nodiscard]] CleanupRouteAuthority route_authority() const noexcept;
    [[nodiscard]] CleanupEvidenceCompleteness completeness() const noexcept;
    [[nodiscard]] const std::optional<CleanupInvocationAuthority>& authority()
        const noexcept;
    [[nodiscard]] const BuildPlan* build_plan() const noexcept;
    [[nodiscard]] const std::vector<RootTargetIdentity>& roots() const noexcept;
    [[nodiscard]] const std::vector<CleanupInvocationWorkItemEvidence>&
    work_items() const noexcept;
    [[nodiscard]] const std::vector<PackageBaseIdentity>& package_bases()
        const noexcept;
    [[nodiscard]] const std::vector<CleanupDependencyEdgeClassification>&
    edge_classifications() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupTrustedTransactionTokenInventoryEntry>&
    transaction_token_inventory() const noexcept;
    [[nodiscard]] const std::optional<CleanupBaselineSnapshotObservation>&
    baseline_observation() const noexcept;
    [[nodiscard]] const std::optional<CleanupCurrentInstalledObservation>&
    current_observation() const noexcept;
    [[nodiscard]] const std::optional<CleanupPolicyObservation>&
    policy_observation() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSourceArtifactCorrelationEvidence>&
    source_artifact_evidence() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSelectedProviderCorrelationEvidence>&
    selected_provider_evidence() const noexcept;
    [[nodiscard]] CleanupLifecycleBoundary lifecycle_boundary()
        const noexcept;
    [[nodiscard]] const std::optional<std::size_t>&
    completed_work_item_index() const noexcept;
    [[nodiscard]] const std::vector<CleanupInvocationEvidenceIssueKind>&
    issues() const noexcept;

private:
    CleanupInvocationEvidence(
        CleanupRouteKind route_kind,
        CleanupRouteAuthority route_authority,
        CleanupEvidenceCompleteness completeness,
        std::optional<CleanupInvocationAuthority> authority,
        std::optional<BuildPlan> build_plan,
        std::vector<RootTargetIdentity> roots,
        std::vector<CleanupInvocationWorkItemEvidence> work_items,
        std::vector<PackageBaseIdentity> package_bases,
        std::vector<CleanupDependencyEdgeClassification>
            edge_classifications,
        std::vector<CleanupTrustedTransactionTokenInventoryEntry>
            transaction_token_inventory,
        std::optional<CleanupBaselineSnapshotObservation>
            baseline_observation,
        std::optional<CleanupCurrentInstalledObservation>
            current_observation,
        std::optional<CleanupPolicyObservation> policy_observation,
        std::vector<CleanupSourceArtifactCorrelationEvidence>
            source_artifact_evidence,
        std::vector<CleanupSelectedProviderCorrelationEvidence>
            selected_provider_evidence,
        CleanupLifecycleBoundary lifecycle_boundary,
        std::optional<std::size_t> completed_work_item_index,
        std::vector<CleanupInvocationEvidenceIssueKind> issues) noexcept;

    CleanupRouteKind route_kind_;
    CleanupRouteAuthority route_authority_;
    CleanupEvidenceCompleteness completeness_;
    std::optional<CleanupInvocationAuthority> authority_;
    std::optional<BuildPlan> build_plan_;
    std::vector<RootTargetIdentity> roots_;
    std::vector<CleanupInvocationWorkItemEvidence> work_items_;
    std::vector<PackageBaseIdentity> package_bases_;
    std::vector<CleanupDependencyEdgeClassification>
        edge_classifications_;
    std::vector<CleanupTrustedTransactionTokenInventoryEntry>
        transaction_token_inventory_;
    std::optional<CleanupBaselineSnapshotObservation>
        baseline_observation_;
    std::optional<CleanupCurrentInstalledObservation>
        current_observation_;
    std::optional<CleanupPolicyObservation> policy_observation_;
    std::vector<CleanupSourceArtifactCorrelationEvidence>
        source_artifact_evidence_;
    std::vector<CleanupSelectedProviderCorrelationEvidence>
        selected_provider_evidence_;
    CleanupLifecycleBoundary lifecycle_boundary_;
    std::optional<std::size_t> completed_work_item_index_;
    std::vector<CleanupInvocationEvidenceIssueKind> issues_;

    friend CleanupInvocationEvidence project_cleanup_route_evidence(
        CleanupRouteKind route_kind);
    friend CleanupInvocationEvidence
    aggregate_remote_aur_cleanup_invocation_evidence(
        const CleanupInvocationSession& session,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        const CleanupBaselineSnapshotObservation& baseline_observation,
        const CleanupCurrentInstalledObservation& current_observation,
        const CleanupPolicyObservation& policy_observation,
        std::vector<CleanupSourceArtifactCorrelationEvidence>
            source_artifact_evidence,
        std::vector<CleanupSelectedProviderCorrelationEvidence>
            selected_provider_evidence);
};

[[nodiscard]] CleanupRouteAuthority project_cleanup_route_authority(
    CleanupRouteKind route_kind,
    CleanupEvidenceCompleteness completeness) noexcept;

[[nodiscard]] CleanupInvocationEvidence project_cleanup_route_evidence(
    CleanupRouteKind route_kind);

[[nodiscard]] CleanupInvocationEvidence
aggregate_remote_aur_cleanup_invocation_evidence(
    const CleanupInvocationSession& session,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const CleanupBaselineSnapshotObservation& baseline_observation,
    const CleanupCurrentInstalledObservation& current_observation,
    const CleanupPolicyObservation& policy_observation,
    std::vector<CleanupSourceArtifactCorrelationEvidence>
        source_artifact_evidence,
    std::vector<CleanupSelectedProviderCorrelationEvidence>
        selected_provider_evidence);

[[nodiscard]] CleanupSharedRequirementState project_shared_requirement(
    const CleanupInvocationEvidence& evidence,
    const SourceAwarePackageIdentity& package) noexcept;

[[nodiscard]] CleanupBaselineObservation
project_cleanup_baseline_observation(
    const InstalledPackageStateSnapshotResult& baseline_snapshot,
    const std::string& package_name) noexcept;

[[nodiscard]] CleanupCurrentPackageEvidence
project_cleanup_current_package_evidence(
    const InstalledPackageStateSnapshotResult& current_snapshot,
    const std::string& package_name);

// Current makepkg syncdeps and selected-repository-provider result types do
// not contain package-level causal change authority. No combination of their
// success/status fields is promoted to InvocationOwned.
[[nodiscard]] CleanupCausalOwnership project_cleanup_causal_ownership(
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const std::vector<SelectedRepositoryProviderTransactionResult>&
        provider_transactions) noexcept;

// Raw generic ledger projection is deliberately test-only. Production
// SourceArtifactInstall positive authority is the closed owner-specific model
// in source_artifact_install_receipt_evidence.hpp.
#ifdef MOGUET_ENABLE_INVOCATION_TRANSACTION_LEDGER_TEST_HOOKS
[[nodiscard]] CleanupCausalOwnership
project_cleanup_causal_ownership_from_raw_ledger_for_test(
    const std::string& package_name,
    CleanupBaselineObservation baseline,
    const CleanupCurrentPackageEvidence& current_package,
    const InvocationDependencyTransactionLedger& transaction_ledger);
#endif

[[nodiscard]] CleanupPolicyProtection
project_cleanup_policy_protection(
    const CleanupPolicyProtectionEvidence& evidence) noexcept;

// candidate_authority must be a typed BuildPlan/exact-metadata observation.
// Installed-only or otherwise source-incomplete authority returns a typed
// projection failure; package name/version snapshots are never used to infer
// Repository/AUR/Local identity.
[[nodiscard]] InvocationOwnedCleanupCandidateProjectionResult
project_invocation_owned_cleanup_candidate(
    const InstalledPackageStateSnapshotResult& baseline_snapshot,
    const InstalledPackageStateSnapshotResult& current_snapshot,
    const BuildPlan& plan,
    const ResolvedDependencyCandidate& candidate_authority,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const std::vector<SelectedRepositoryProviderTransactionResult>&
        provider_transactions = {});
