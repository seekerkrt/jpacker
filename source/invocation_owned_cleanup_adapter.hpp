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

// The evidence borrows existing production values. It adds only the lifecycle
// boundary that current result types do not express; it does not copy command
// output or invent package-level transaction ownership.
class CleanupInvocationLifecycleEvidence final {
public:
    [[nodiscard]] static CleanupInvocationLifecycleEvidence unknown() noexcept;
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    before_build_completion(
        const PreparedProductionSourceBuildInvocation& invocation) noexcept;
    [[nodiscard]] static CleanupInvocationLifecycleEvidence after_work_item(
        const PreparedProductionSourceBuildInvocation& invocation,
        const ProductionSourceBuildInvocationResult& result,
        std::size_t completed_work_item_index) noexcept;
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    after_successful_invocation(
        const PreparedProductionSourceBuildInvocation& invocation,
        const ProductionSourceBuildInvocationResult& result) noexcept;
    [[nodiscard]] static CleanupInvocationLifecycleEvidence
    after_invocation_completion(
        const PreparedProductionSourceBuildInvocation& invocation,
        const ProductionSourceBuildInvocationResult& result) noexcept;

    [[nodiscard]] CleanupLifecycleBoundary boundary() const noexcept;
    [[nodiscard]] const PreparedProductionSourceBuildInvocation* invocation()
        const noexcept;
    [[nodiscard]] const ProductionSourceBuildInvocationResult* result()
        const noexcept;
    [[nodiscard]] const std::optional<std::size_t>&
    completed_work_item_index() const noexcept;

private:
    CleanupInvocationLifecycleEvidence(
        CleanupLifecycleBoundary boundary,
        std::optional<std::reference_wrapper<
            const PreparedProductionSourceBuildInvocation>>
            invocation,
        std::optional<std::reference_wrapper<
            const ProductionSourceBuildInvocationResult>>
            result,
        std::optional<std::size_t> completed_work_item_index) noexcept;

    CleanupLifecycleBoundary boundary_;
    std::optional<std::reference_wrapper<
        const PreparedProductionSourceBuildInvocation>>
        invocation_;
    std::optional<std::reference_wrapper<
        const ProductionSourceBuildInvocationResult>>
        result_;
    std::optional<std::size_t> completed_work_item_index_;
};

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
    [[nodiscard]] const CleanupInvocationIdentity& invocation() const noexcept;
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
        CleanupInvocationIdentity invocation,
        SourceArtifactInstallCausalEvidence causal_evidence,
        std::size_t work_item_index,
        std::string package_base,
        std::vector<CleanupSourceArtifactSelectedCorrelation>
            selected_artifacts,
        std::vector<std::string> actual_install_set,
        std::vector<CleanupSourceArtifactCorrelationIssueKind> issues) noexcept;

    CleanupEvidenceCompleteness completeness_;
    CleanupInvocationIdentity invocation_;
    SourceArtifactInstallCausalEvidence causal_evidence_;
    std::size_t work_item_index_;
    std::string package_base_;
    std::vector<CleanupSourceArtifactSelectedCorrelation>
        selected_artifacts_;
    std::vector<std::string> actual_install_set_;
    std::vector<CleanupSourceArtifactCorrelationIssueKind> issues_;

    friend CleanupSourceArtifactCorrelationEvidence
    correlate_source_artifact_install_to_build_plan(
        const CleanupInvocationIdentity& invocation_identity,
        const BuildPlan& plan,
        const PreparedProductionSourceBuildInvocation& invocation,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        const SourceArtifactInstallCausalEvidence& causal_evidence);
};

[[nodiscard]] CleanupSourceArtifactCorrelationEvidence
correlate_source_artifact_install_to_build_plan(
    const CleanupInvocationIdentity& invocation_identity,
    const BuildPlan& plan,
    const PreparedProductionSourceBuildInvocation& invocation,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const SourceArtifactInstallCausalEvidence& causal_evidence);

enum class CleanupSelectedProviderCorrelationIssueKind {
    InvocationIdentityMissing,
    InvocationMismatch,
    LifecycleInvocationMismatch,
    BuildPlanIncomplete,
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
    TransactionResultMismatch,
    ReceiptCaptureMissing,
    ReceiptCaptureIncomplete,
    TransactionLedgerMismatch,
    ReceiptMismatch,
    ProviderNotInstalled,
    UncorrelatedActualInstall,
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
    [[nodiscard]] const CleanupInvocationIdentity& invocation() const noexcept;
    [[nodiscard]] std::size_t work_item_index() const noexcept;
    [[nodiscard]] std::size_t build_plan_edge_index() const noexcept;
    [[nodiscard]] const std::optional<ProvidedDependency>& provider()
        const noexcept;
    [[nodiscard]] const std::optional<SourceAwarePackageIdentity>&
    package_identity() const noexcept;
    [[nodiscard]] const CleanupCurrentPackageEvidence& current_package()
        const noexcept;
    [[nodiscard]] const std::optional<std::string>& transaction_token()
        const noexcept;
    [[nodiscard]] const std::vector<std::string>& actual_install_set()
        const noexcept;
    [[nodiscard]] const SelectedRepositoryProviderTrustedReceiptExecutionResult&
    execution() const noexcept;
    [[nodiscard]] const std::vector<
        CleanupSelectedProviderCorrelationIssueKind>&
    issues() const noexcept;

private:
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
            issues) noexcept;

    CleanupEvidenceCompleteness completeness_;
    CleanupInvocationIdentity invocation_;
    std::size_t work_item_index_;
    std::size_t build_plan_edge_index_;
    std::optional<ProvidedDependency> provider_;
    std::optional<SourceAwarePackageIdentity> package_identity_;
    CleanupCurrentPackageEvidence current_package_;
    std::optional<std::string> transaction_token_;
    std::vector<std::string> actual_install_set_;
    SelectedRepositoryProviderTrustedReceiptExecutionResult execution_;
    std::vector<CleanupSelectedProviderCorrelationIssueKind> issues_;

    friend CleanupSelectedProviderCorrelationEvidence
    correlate_selected_repository_provider_to_build_plan(
        const CleanupInvocationIdentity& invocation_identity,
        const BuildPlan& plan,
        const PreparedProductionSourceBuildInvocation& invocation,
        const CleanupInvocationLifecycleEvidence& lifecycle,
        std::size_t build_plan_edge_index,
        const CleanupCurrentPackageEvidence& current_package,
        const SelectedRepositoryProviderTrustedReceiptExecutionResult&
            execution);
};

[[nodiscard]] CleanupSelectedProviderCorrelationEvidence
correlate_selected_repository_provider_to_build_plan(
    const CleanupInvocationIdentity& invocation_identity,
    const BuildPlan& plan,
    const PreparedProductionSourceBuildInvocation& invocation,
    const CleanupInvocationLifecycleEvidence& lifecycle,
    std::size_t build_plan_edge_index,
    const CleanupCurrentPackageEvidence& current_package,
    const SelectedRepositoryProviderTrustedReceiptExecutionResult& execution);

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
    SourceArtifactCorrelationMissing,
    SourceArtifactCorrelationUnexpected,
    SelectedProviderCorrelationMissing,
    SelectedProviderCorrelationUnexpected,
    CorrelationInvocationMismatch,
    CorrelationIncomplete,
    MakepkgSyncDependenciesUnowned,
    UncorrelatedActualInstall,
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
    [[nodiscard]] const std::optional<CleanupInvocationIdentity>& invocation()
        const noexcept;
    [[nodiscard]] const BuildPlan* build_plan() const noexcept;
    [[nodiscard]] const std::vector<RootTargetIdentity>& roots() const noexcept;
    [[nodiscard]] const std::vector<CleanupInvocationWorkItemEvidence>&
    work_items() const noexcept;
    [[nodiscard]] const std::vector<PackageBaseIdentity>& package_bases()
        const noexcept;
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
        std::vector<CleanupInvocationEvidenceIssueKind> issues) noexcept;

    CleanupRouteKind route_kind_;
    CleanupRouteAuthority route_authority_;
    CleanupEvidenceCompleteness completeness_;
    std::optional<CleanupInvocationIdentity> invocation_;
    std::optional<BuildPlan> build_plan_;
    std::vector<RootTargetIdentity> roots_;
    std::vector<CleanupInvocationWorkItemEvidence> work_items_;
    std::vector<PackageBaseIdentity> package_bases_;
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
        const CleanupInvocationIdentity& invocation_identity,
        const PreparedRemoteSourceBuild& prepared,
        const CleanupInvocationLifecycleEvidence& lifecycle,
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
    const CleanupInvocationIdentity& invocation_identity,
    const PreparedRemoteSourceBuild& prepared,
    const CleanupInvocationLifecycleEvidence& lifecycle,
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
