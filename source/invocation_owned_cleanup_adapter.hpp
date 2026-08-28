#pragma once

#include "invocation_owned_cleanup_model.hpp"
#include "package_metadata.hpp"
#include "source_install.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class MakepkgSyncDependencySessionReceipt;

enum class CleanupLifecycleBoundary {
    BeforeBuildCompletion,
    AfterWorkItem,
    AfterSuccessfulInvocation,
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

[[nodiscard]] CleanupBaselineObservation
project_cleanup_baseline_observation(
    const InstalledPackageStateSnapshotResult& baseline_snapshot,
    const std::string& package_name) noexcept;

[[nodiscard]] CleanupCurrentPackageEvidence
project_cleanup_current_package_evidence(
    const InstalledPackageStateSnapshotResult& current_snapshot,
    const std::string& package_name);

// Legacy lifecycle and selected-repository-provider result status fields do
// not contain package-level causal change authority. No combination of their
// success/status fields is promoted to InvocationOwned.
[[nodiscard]] CleanupCausalOwnership project_cleanup_causal_ownership(
    const CleanupInvocationLifecycleEvidence& lifecycle,
    const std::vector<SelectedRepositoryProviderTransactionResult>&
        provider_transactions) noexcept;

// InvocationOwned requires a complete receipt for the exact transaction and
// an actual Install record for this package. Omission, Upgrade, command
// success alone, or malformed ledger data remains Unknown.
[[nodiscard]] CleanupCausalOwnership project_cleanup_causal_ownership(
    const std::string& package_name,
    CleanupBaselineObservation baseline,
    const CleanupCurrentPackageEvidence& current_package,
    const InvocationDependencyTransactionLedger& transaction_ledger);

// Makepkg syncdeps authority is route-specific: the complete validated
// session receipt remains attached to coverage, process binding, identity,
// and fixed owner checks. Raw ordered observations are not converted to the
// generic cleanup ledger at this boundary.
[[nodiscard]] CleanupCausalOwnership
project_makepkg_sync_dependency_causal_ownership(
    const std::string& package_name,
    CleanupBaselineObservation baseline,
    const CleanupCurrentPackageEvidence& current_package,
    const MakepkgSyncDependencySessionReceipt& session_receipt) noexcept;

// Slice 3.6 adds causal transport only; it still has no complete group/policy
// inventory authority.
[[nodiscard]] CleanupPolicyProtection
project_cleanup_policy_protection() noexcept;

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
        provider_transactions = {},
    const InvocationDependencyTransactionLedger& transaction_ledger = {});
