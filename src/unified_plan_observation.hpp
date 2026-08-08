#pragma once

#include "artifact_install_plan.hpp"
#include "dependency_plan.hpp"
#include "local_package_metadata.hpp"
#include "local_source_root.hpp"
#include "package_base_artifact_install_plan.hpp"
#include "package_constraint_metadata.hpp"
#include "root_package_candidate.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

class LocalBuildPlan;

struct AurUpdateExecutionIssue;
struct SystemSourceUpgradeIssue;
struct UpgradeAllOperationIssue;

enum class SystemSourceUpgradePhase;
enum class UpgradeAllOperationPhase;

enum class UnifiedPlanObservationStatus {
    Ready,
    NoOp,
    Blocked
};

enum class UnifiedPlanRootSourceKind {
    Repository,
    Aur,
    Local
};

enum class UnifiedPlanRootRouteKind {
    RepositoryTransaction,
    RepositorySourceBuild,
    AurSourceBuild,
    LocalSourceBuild
};

// LocalSourceRootのdescriptor capabilityをcopyせず、既に観測済みのstable
// filesystem identityだけをsource-aware identityとして保持する。
struct LocalSourceRootObservationIdentity {
    std::filesystem::path         canonical_path;
    LocalSourceDirectoryIdentity directory_identity;

    bool operator==(const LocalSourceRootObservationIdentity&) const = default;
};

using UnifiedPlanRootIdentity = std::variant<
        RepositoryRootPackageIdentity,
        AurRootPackageIdentity,
        LocalSourceRootObservationIdentity>;

class UnifiedPlanRootReference final {
public:
    UnifiedPlanRootReference(
            RootTargetIdentity invocation_correlation,
            UnifiedPlanRootIdentity source_identity,
            UnifiedPlanRootRouteKind route_kind);

    [[nodiscard]] const RootTargetIdentity& invocation_correlation()
            const noexcept;
    [[nodiscard]] const UnifiedPlanRootIdentity& source_identity()
            const noexcept;
    [[nodiscard]] UnifiedPlanRootSourceKind source_kind() const noexcept;
    [[nodiscard]] UnifiedPlanRootRouteKind route_kind() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

    bool operator==(const UnifiedPlanRootReference&) const = default;

private:
    RootTargetIdentity         invocation_correlation_;
    UnifiedPlanRootIdentity    source_identity_;
    UnifiedPlanRootRouteKind   route_kind_;
};

// The referenced authority must outlive the observation. No BuildPlan content
// is copied, normalized, or made independently resolvable by this view.
class UnifiedPlanDependencyAuthorityReference final {
public:
    [[nodiscard]] static UnifiedPlanDependencyAuthorityReference
    from_build_plan(const BuildPlan& plan) noexcept;
    [[nodiscard]] static UnifiedPlanDependencyAuthorityReference
    from_local_build_plan(const LocalBuildPlan& plan) noexcept;

    [[nodiscard]] const BuildPlan* build_plan() const noexcept;
    [[nodiscard]] const LocalBuildPlan* local_build_plan() const noexcept;

private:
    using Authority = std::variant<
            std::reference_wrapper<const BuildPlan>,
            std::reference_wrapper<const LocalBuildPlan>>;

    explicit UnifiedPlanDependencyAuthorityReference(Authority authority)
        noexcept;

    Authority authority_;
};

class AurPackageBaseBuildUnitReference final {
public:
    AurPackageBaseBuildUnitReference(
            const BuildPlan& authority,
            std::size_t build_plan_order_index) noexcept;

    [[nodiscard]] const BuildPlan& authority() const noexcept;
    [[nodiscard]] std::size_t build_plan_order_index() const noexcept;
    [[nodiscard]] const BuildPlanEntry* entry() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

private:
    std::reference_wrapper<const BuildPlan> authority_;
    std::size_t                            build_plan_order_index_;
};

class LocalSourceBuildUnitReference final {
public:
    LocalSourceBuildUnitReference(
            LocalSourceRootObservationIdentity source_root,
            const LocalPackageMetadata& metadata);

    [[nodiscard]] const LocalSourceRootObservationIdentity& source_root()
            const noexcept;
    [[nodiscard]] const LocalPackageMetadata& metadata() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

private:
    LocalSourceRootObservationIdentity                source_root_;
    std::reference_wrapper<const LocalPackageMetadata> metadata_;
};

using UnifiedPlanBuildUnitReference = std::variant<
        AurPackageBaseBuildUnitReference,
        LocalSourceBuildUnitReference>;

// Pre-build required targetだけを表す。ProducedPackageArtifactやartifact path、
// produced versionへ変換するAPIは持たない。
class RequiredArtifactTargetReference final {
public:
    RequiredArtifactTargetReference(
            UnifiedPlanBuildUnitReference build_unit,
            const RequiredPackageArtifactTarget& target);

    [[nodiscard]] const UnifiedPlanBuildUnitReference& build_unit()
            const noexcept;
    [[nodiscard]] const RequiredPackageArtifactTarget& target() const noexcept;
    [[nodiscard]] bool matches_build_unit() const noexcept;

private:
    UnifiedPlanBuildUnitReference
            build_unit_;
    std::reference_wrapper<const RequiredPackageArtifactTarget> target_;
};

struct RepositoryTransactionPolicyView {
    bool needed = false;
};

struct RepositoryRootInstallIntent {
    UnifiedPlanRootReference root;
};

struct RepositoryDependencyInstallIntent {
    std::reference_wrapper<const RepositoryExactPackage> package;
};

struct RepositoryProviderInstallIntent {
    std::reference_wrapper<const ProvidedDependency> provider;
};

using RepositoryInstallIntentTarget = std::variant<
        RepositoryRootInstallIntent,
        RepositoryDependencyInstallIntent,
        RepositoryProviderInstallIntent>;

// This is an ordered target/options observation, not pacman argv or a prepared
// transaction capability.
struct RepositoryPackageTransactionIntent {
    std::vector<RepositoryInstallIntentTarget> targets;
    RepositoryTransactionPolicyView            policy;
};

struct SourceRootArtifactInstallIntent {
    RequiredArtifactTargetReference target;
};

struct SourceDependencyArtifactInstallIntent {
    RequiredArtifactTargetReference target;
};

using SourceArtifactInstallIntentTarget = std::variant<
        SourceRootArtifactInstallIntent,
        SourceDependencyArtifactInstallIntent>;

// Required targets identify the future install boundary. No package file path
// or post-build install-reason plan exists at this stage.
struct SourceBuiltArtifactInstallBoundaryIntent {
    std::vector<SourceArtifactInstallIntentTarget> targets;
    bool                                           needed = false;
};

using UnifiedPlanTransactionIntent = std::variant<
        RepositoryPackageTransactionIntent,
        SourceBuiltArtifactInstallBoundaryIntent>;

// This ordering is observation vocabulary only. It does not replace any
// route-specific execution phase enum.
enum class UnifiedPlanObservationPhase {
    RequestDiscovery,
    MetadataDiscovery,
    ProviderDecision,
    ExecutionProjection,
    RepositoryTransaction,
    SourceRetrieval,
    SourceBuild,
    ArtifactValidation,
    SourceArtifactInstall,
    CleanupReduction
};

inline constexpr std::array<UnifiedPlanObservationPhase, 10>
        UNIFIED_PLAN_OBSERVATION_PHASE_ORDER = {
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

enum class UnifiedPlanAuthorityOwner {
    Moguet,
    Libalpm,
    Pacman,
    AurRpc,
    Git,
    Makepkg
};

using ExistingRoutePhaseReference = std::variant<
        SystemSourceUpgradePhase,
        UpgradeAllOperationPhase>;

struct UnifiedPlanPhaseReference {
    UnifiedPlanObservationPhase              observation_phase;
    UnifiedPlanAuthorityOwner                owner;
    std::optional<ExistingRoutePhaseReference> existing_route_phase;
};

// Blocker wrappers classify the observation while retaining the existing
// typed detail by reference. They intentionally have no generic message arm.
struct UnknownUnifiedPlanBlocker {
    std::reference_wrapper<const BuildPlanDependencyEdge> detail;
};

struct AmbiguousUnifiedPlanBlocker {
    std::reference_wrapper<const AmbiguousProvidedDependency> detail;
};

struct UnsupportedUnifiedPlanBlocker {
    std::reference_wrapper<const MixedPackageBaseInstallReasonUnsupported>
            detail;
};

using SourceFailureUnifiedPlanBlockerDetail = std::variant<
        std::reference_wrapper<const BuildPlanResolutionFailure>,
        std::reference_wrapper<const IncompleteProviderCandidateSet>,
        std::reference_wrapper<const RepositoryExactPackageSourceFailure>,
        std::reference_wrapper<const RepositoryProviderSourceFailure>,
        std::reference_wrapper<const LocalSourceRootFailure>>;

struct SourceFailureUnifiedPlanBlocker {
    SourceFailureUnifiedPlanBlockerDetail detail;
};

struct ConstraintFailureUnifiedPlanBlocker {
    std::reference_wrapper<const ConstraintEvaluation> detail;
};

struct MetadataRiskUnifiedPlanBlocker {
    std::reference_wrapper<const BuildPlanMetadataRisk> detail;
};

using RoutePreflightUnifiedPlanBlockerDetail = std::variant<
        std::reference_wrapper<const AurUpdateExecutionIssue>,
        std::reference_wrapper<const SystemSourceUpgradeIssue>,
        std::reference_wrapper<const UpgradeAllOperationIssue>>;

struct RoutePreflightUnifiedPlanBlocker {
    RoutePreflightUnifiedPlanBlockerDetail detail;
};

using UnifiedPlanBlocker = std::variant<
        UnknownUnifiedPlanBlocker,
        AmbiguousUnifiedPlanBlocker,
        UnsupportedUnifiedPlanBlocker,
        SourceFailureUnifiedPlanBlocker,
        ConstraintFailureUnifiedPlanBlocker,
        MetadataRiskUnifiedPlanBlocker,
        RoutePreflightUnifiedPlanBlocker>;

struct UnifiedPlanObservationInput {
    UnifiedPlanObservationStatus status =
            UnifiedPlanObservationStatus::Blocked;
    std::vector<UnifiedPlanRootReference> roots;
    std::vector<UnifiedPlanDependencyAuthorityReference>
            dependency_authorities;
    std::vector<UnifiedPlanBuildUnitReference> build_units;
    std::vector<RequiredArtifactTargetReference> required_artifacts;
    std::vector<UnifiedPlanTransactionIntent> transaction_intents;
    std::vector<UnifiedPlanPhaseReference> phases;
    std::vector<UnifiedPlanBlocker> blockers;
};

enum class UnifiedPlanObservationInvariantIssueKind {
    UnknownStatus,
    ReadyHasBlockers,
    NoOpHasBlockers,
    NoOpHasMutationIntent,
    BlockedWithoutBlocker,
    NonBlockedRootIdentityIncomplete,
    NonBlockedBuildUnitIdentityIncomplete,
    RequiredArtifactTargetIncomplete,
    RequiredArtifactBuildUnitMismatch,
    RequiredArtifactBuildUnitNotObserved,
    RepositoryTransactionHasNoTarget,
    RepositoryTransactionTargetInvalid,
    SourceArtifactInstallHasNoTarget,
    SourceArtifactInstallTargetInvalid,
    SourceArtifactInstallTargetNotObserved,
    ObservationPhaseOrderInvalid
};

struct UnifiedPlanObservationInvariantIssue {
    UnifiedPlanObservationInvariantIssueKind kind;
    std::optional<std::size_t>                primary_index;
    std::optional<std::size_t>                secondary_index;
};

struct InvalidUnifiedPlanObservation {
    std::vector<UnifiedPlanObservationInvariantIssue> issues;
};

class UnifiedPlanObservationResult;

class UnifiedPlanObservation final {
public:
    UnifiedPlanObservation(const UnifiedPlanObservation&) = default;
    UnifiedPlanObservation(UnifiedPlanObservation&&) noexcept = default;
    UnifiedPlanObservation& operator=(const UnifiedPlanObservation&) = default;
    UnifiedPlanObservation& operator=(UnifiedPlanObservation&&) noexcept =
            default;
    ~UnifiedPlanObservation() = default;

    [[nodiscard]] UnifiedPlanObservationStatus status() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanRootReference>& roots()
            const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanDependencyAuthorityReference>&
    dependency_authorities() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanBuildUnitReference>&
    build_units() const noexcept;
    [[nodiscard]] const std::vector<RequiredArtifactTargetReference>&
    required_artifacts() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanTransactionIntent>&
    transaction_intents() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanPhaseReference>& phases()
            const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanBlocker>& blockers()
            const noexcept;

private:
    explicit UnifiedPlanObservation(UnifiedPlanObservationInput input);

    UnifiedPlanObservationInput input_;

    friend UnifiedPlanObservationResult make_unified_plan_observation(
            UnifiedPlanObservationInput input);
};

class UnifiedPlanObservationResult final {
public:
    UnifiedPlanObservationResult() = delete;
    UnifiedPlanObservationResult(const UnifiedPlanObservationResult&) =
            default;
    UnifiedPlanObservationResult(UnifiedPlanObservationResult&&) noexcept =
            default;
    UnifiedPlanObservationResult& operator=(
            const UnifiedPlanObservationResult&) = delete;
    UnifiedPlanObservationResult& operator=(
            UnifiedPlanObservationResult&&) noexcept = delete;
    ~UnifiedPlanObservationResult() = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] const UnifiedPlanObservation* observation() const noexcept;
    [[nodiscard]] const InvalidUnifiedPlanObservation* failure()
            const noexcept;

private:
    explicit UnifiedPlanObservationResult(
            UnifiedPlanObservation observation);
    explicit UnifiedPlanObservationResult(
            InvalidUnifiedPlanObservation failure);

    std::variant<UnifiedPlanObservation, InvalidUnifiedPlanObservation>
            outcome_;

    friend UnifiedPlanObservationResult make_unified_plan_observation(
            UnifiedPlanObservationInput input);
};

UnifiedPlanObservationResult make_unified_plan_observation(
        UnifiedPlanObservationInput input);
