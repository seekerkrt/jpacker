#pragma once

#include "dependency_plan.hpp"
#include "installed_package.hpp"
#include "source_package_identity.hpp"

#include <cstddef>
#include <optional>
#include <vector>

enum class CleanupBaselineObservation {
    PreExisting,
    NewlyObserved,
    Unknown,
};

enum class CleanupInstalledState {
    Present,
    Absent,
    Unknown,
};

// Causal ownership is deliberately independent from baseline observation.
// In particular, NewlyObserved cannot be passed as InvocationOwned.
enum class CleanupCausalOwnership {
    InvocationOwned,
    NotInvocationOwned,
    Unknown,
};

enum class CleanupSharedRequirementState {
    StillRequired,
    NoLongerRequired,
    Unknown,
};

enum class CleanupEvidenceVerification {
    Verified,
    Unverified,
};

// This is set-level authority and is independent from verification of any
// individual correlation. A verified edge never proves that no other root,
// PackageBase, role, or dependency edge exists.
enum class CleanupCorrelationCoverage {
    Complete,
    Incomplete,
    Unknown,
};

// Adapters may project group or other policy evidence without teaching this
// pure model package-name heuristics.
enum class CleanupPolicyProtection {
    NotProtected,
    Protected,
    Unknown,
};

struct CleanupCurrentPackageEvidence {
    CleanupInstalledState                    state;
    std::optional<InstalledPackageMetadata> metadata;
    CleanupEvidenceVerification             verification;
};

struct CleanupProviderCorrelation {
    ProvidedDependency     provider;
    ProviderResolutionKind resolution;
};

// This is the minimal pure projection of a BuildPlan dependency edge. It
// retains the edge index, requiring source package, typed requirement, and
// provider identity without owning BuildPlan or an execution object.
struct CleanupDependencyEdgeCorrelation {
    std::size_t                               build_plan_edge_index;
    PackageChildIdentity                      requiring_package;
    DependencyRequirement                     requirement;
    std::optional<CleanupProviderCorrelation> provider;
};

// One package may retain several root, PackageBase, role, and edge
// correlations. Vector order is evidence order and never classification
// priority. Verified means the adapter checked this correlation's role, edge,
// typed requirement, and any provider association against one authoritative
// observation; it does not establish set-level completeness.
struct CleanupPackageCorrelation {
    RootTargetIdentity                              requested_root;
    PackageChildIdentity                            package;
    std::optional<PackageRole>                      role;
    std::optional<CleanupDependencyEdgeCorrelation> dependency_edge;
    CleanupEvidenceVerification                     verification;
};

struct InvocationOwnedCleanupCandidate {
    SourceAwarePackageIdentity             package;
    CleanupBaselineObservation             baseline;
    CleanupCurrentPackageEvidence          current_package;
    CleanupCausalOwnership                 causal_ownership;
    CleanupSharedRequirementState          shared_requirement;
    CleanupPolicyProtection                policy_protection;
    CleanupCorrelationCoverage             correlation_coverage;
    std::vector<CleanupPackageCorrelation> correlations;
};

enum class CleanupClassification {
    Eligible,
    Protected,
    Unknown,
    Invalid,
};

// Declaration order is the canonical reason order. The classifier returns
// only reasons for the selected precedence arm.
enum class CleanupClassificationReason {
    InvalidTypedState,
    CurrentPackageIdentityMismatch,
    CurrentPackageVersionMismatch,
    MalformedRequestedRootIdentity,
    CorrelationPackageIdentityMismatch,
    CorrelationShapeMismatch,
    DependencyRequirementIdentityMismatch,
    ProviderIdentityMismatch,
    ProviderRequirementIdentityMismatch,

    PreExisting,
    CurrentPackageAbsent,
    ExplicitInstallReason,
    KnownNotInvocationOwned,
    RootTarget,
    RuntimeDependency,
    StillRequired,
    PolicyProtected,

    BaselineObservationUnknown,
    CurrentInstalledStateUnknown,
    InstallReasonUnknown,
    CurrentPackageVersionUnknown,
    CurrentPackageVersionUnavailable,
    CausalOwnershipUnknown,
    CurrentPackageEvidenceUnverified,
    CorrelationCoverageIncomplete,
    CorrelationCoverageUnknown,
    DependencyCorrelationMissing,
    DependencyRoleUnknown,
    DependencyEdgeCorrelationMissing,
    DependencyCorrelationUnverified,
    BuildOrCheckDependencyCorrelationMissing,
    SharedRequirementUnknown,
    PolicyProtectionUnknown,

    EligibleEvidenceComplete,
};

class CleanupClassificationResult final {
public:
    CleanupClassificationResult() = delete;
    CleanupClassificationResult(const CleanupClassificationResult&) = default;
    CleanupClassificationResult(CleanupClassificationResult&&) noexcept =
            default;
    CleanupClassificationResult& operator=(
            const CleanupClassificationResult&) = default;
    CleanupClassificationResult& operator=(
            CleanupClassificationResult&&) noexcept = default;
    ~CleanupClassificationResult() = default;

    [[nodiscard]] CleanupClassification classification() const noexcept;
    [[nodiscard]] const std::vector<CleanupClassificationReason>& reasons()
            const noexcept;

    bool operator==(const CleanupClassificationResult&) const = default;

private:
    CleanupClassificationResult(
            CleanupClassification classification,
            std::vector<CleanupClassificationReason> reasons) noexcept;

    CleanupClassification                    classification_;
    std::vector<CleanupClassificationReason> reasons_;

    friend CleanupClassificationResult classify_invocation_owned_cleanup(
            const InvocationOwnedCleanupCandidate& candidate);
};

// Precedence is Invalid, then Protected, then Unknown, and finally Eligible.
// The function reads no external state and grants no cleanup capability.
[[nodiscard]] CleanupClassificationResult classify_invocation_owned_cleanup(
        const InvocationOwnedCleanupCandidate& candidate);
