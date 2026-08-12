#pragma once

#include "artifact_install_plan.hpp"
#include "build_plan_artifact_target_projection.hpp"
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
#include <string>
#include <variant>
#include <vector>

class LocalBuildPlan;
class LocalSourceBuildProjectionAuthority;

struct AurUpdateExecutionPreflight;
struct AurUpdateExecutionIssue;
struct AurUpdatePreparationIssue;
struct AurUpdateSourceBuildPreparation;
struct AurUpdatePlanEntry;
struct AurUpdateQueryFailure;
struct FetchPreparation;
struct LocalDependencyPlanFailure;
struct PreparedRemoteSourceBuild;
struct ProductionSourceBuildWorkItem;
struct PreparedSyncInstall;
struct RegisteredSourcePreferenceSnapshot;
struct RepositoryPackageNotFound;
struct RepositoryPackagePresent;
struct RemoteSourceBuildPlanFailure;
struct ResolvedSourceBuildIdentity;
class RootPackageRoutingProjection;
struct RootPackageSearchCandidate;
struct RootPackageInstallPreparationFailure;
struct SystemSourceUpgradeResult;
struct SystemSourceUpgradeIssue;
struct SyncInstallPreparationFailure;
class SystemSourceUpgradeProjectionAuthority;
struct UpgradeAllOperationResult;
struct UpgradeAllOperationIssue;
class UpgradeAllOperationProjectionAuthority;
class PreparedFilteredAurUpdateOperation;
class PreparedUpgradeAllAurPreflight;

// Public observation leaves may borrow production-owned authority, but the
// borrow itself must not be detached from the observation by value. Keeping
// std::reference_wrapper private also prevents nested variant/member copies
// from silently recreating an independently-lived reference-bearing leaf.
template<typename T>
class UnifiedPlanBorrowedAuthorityReference final {
public:
    explicit UnifiedPlanBorrowedAuthorityReference(const T& authority) noexcept
        : authority_(authority) {}
    UnifiedPlanBorrowedAuthorityReference(T&&) = delete;
    UnifiedPlanBorrowedAuthorityReference(const T&&) = delete;

    UnifiedPlanBorrowedAuthorityReference(
            const UnifiedPlanBorrowedAuthorityReference&) = delete;
    UnifiedPlanBorrowedAuthorityReference& operator=(
            const UnifiedPlanBorrowedAuthorityReference&) = delete;
    UnifiedPlanBorrowedAuthorityReference(
            UnifiedPlanBorrowedAuthorityReference&&) noexcept = default;
    UnifiedPlanBorrowedAuthorityReference& operator=(
            UnifiedPlanBorrowedAuthorityReference&&) noexcept = default;
    ~UnifiedPlanBorrowedAuthorityReference() = default;

    [[nodiscard]] const T& get() const noexcept {
        return authority_.get();
    }

private:
    std::reference_wrapper<const T> authority_;
};

enum class SystemSourceUpgradePhase;
enum class UpgradeAllOperationPhase;
enum class RequiredTargetProvenance;
enum class ArtifactLifecycleIntent;

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

// Registered repository source builds do not carry a configured repository
// name. Keep their production source key and PackageBase separate from an
// exact repository transaction identity.
struct RepositorySourceBuildRootIdentity {
    std::string package_name;
    std::string package_base;
    std::string canonical_source_identity_key;

    bool operator==(const RepositorySourceBuildRootIdentity&) const = default;
};

using UnifiedPlanRootIdentity = std::variant<
        RepositoryRootPackageIdentity,
        RepositorySourceBuildRootIdentity,
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
    UnifiedPlanDependencyAuthorityReference(
            const UnifiedPlanDependencyAuthorityReference&) = delete;
    UnifiedPlanDependencyAuthorityReference& operator=(
            const UnifiedPlanDependencyAuthorityReference&) = delete;
    UnifiedPlanDependencyAuthorityReference(
            UnifiedPlanDependencyAuthorityReference&&) noexcept = default;
    UnifiedPlanDependencyAuthorityReference& operator=(
            UnifiedPlanDependencyAuthorityReference&&) noexcept = default;
    ~UnifiedPlanDependencyAuthorityReference() = default;

    [[nodiscard]] static UnifiedPlanDependencyAuthorityReference
    from_build_plan(const BuildPlan& plan) noexcept;
    static UnifiedPlanDependencyAuthorityReference from_build_plan(
            BuildPlan&&) = delete;
    static UnifiedPlanDependencyAuthorityReference from_build_plan(
            const BuildPlan&&) = delete;
    [[nodiscard]] static UnifiedPlanDependencyAuthorityReference
    from_local_build_plan(const LocalBuildPlan& plan) noexcept;
    static UnifiedPlanDependencyAuthorityReference from_local_build_plan(
            LocalBuildPlan&&) = delete;
    static UnifiedPlanDependencyAuthorityReference from_local_build_plan(
            const LocalBuildPlan&&) = delete;

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
            std::reference_wrapper<const BuildPlan> authority,
            std::size_t build_plan_order_index) noexcept;

    AurPackageBaseBuildUnitReference(
            const AurPackageBaseBuildUnitReference&) = delete;
    AurPackageBaseBuildUnitReference& operator=(
            const AurPackageBaseBuildUnitReference&) = delete;
    AurPackageBaseBuildUnitReference(
            AurPackageBaseBuildUnitReference&&) noexcept = default;
    AurPackageBaseBuildUnitReference& operator=(
            AurPackageBaseBuildUnitReference&&) noexcept = default;
    ~AurPackageBaseBuildUnitReference() = default;

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
            std::reference_wrapper<const LocalPackageMetadata> metadata);

    LocalSourceBuildUnitReference(const LocalSourceBuildUnitReference&) =
            delete;
    LocalSourceBuildUnitReference& operator=(
            const LocalSourceBuildUnitReference&) = delete;
    LocalSourceBuildUnitReference(
            LocalSourceBuildUnitReference&&) noexcept = default;
    LocalSourceBuildUnitReference& operator=(
            LocalSourceBuildUnitReference&&) noexcept = default;
    ~LocalSourceBuildUnitReference() = default;

    [[nodiscard]] const LocalSourceRootObservationIdentity& source_root()
            const noexcept;
    [[nodiscard]] const LocalPackageMetadata& metadata() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

private:
    LocalSourceRootObservationIdentity                source_root_;
    std::reference_wrapper<const LocalPackageMetadata> metadata_;
};

// standalone / sync repository source-buildのcache-free production workをborrowする。
// exact lifecycleは各route projection ownerが固定し、AUR unitは別referenceを使う。
class PreparedRemoteSourceBuildUnitReference final {
public:
    PreparedRemoteSourceBuildUnitReference(
            std::reference_wrapper<const ResolvedSourceBuildIdentity>
                    source,
            std::reference_wrapper<const ProductionSourceBuildWorkItem>
                    work_item) noexcept;

    PreparedRemoteSourceBuildUnitReference(
            const PreparedRemoteSourceBuildUnitReference&) = delete;
    PreparedRemoteSourceBuildUnitReference& operator=(
            const PreparedRemoteSourceBuildUnitReference&) = delete;
    PreparedRemoteSourceBuildUnitReference(
            PreparedRemoteSourceBuildUnitReference&&) noexcept = default;
    PreparedRemoteSourceBuildUnitReference& operator=(
            PreparedRemoteSourceBuildUnitReference&&) noexcept = default;
    ~PreparedRemoteSourceBuildUnitReference() = default;

    [[nodiscard]] const ResolvedSourceBuildIdentity& source()
            const noexcept;
    [[nodiscard]] const ProductionSourceBuildWorkItem& work_item()
            const noexcept;
    [[nodiscard]] const std::vector<RequiredPackageArtifactTarget>&
    required_targets() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

private:
    std::reference_wrapper<const ResolvedSourceBuildIdentity> source_;
    std::reference_wrapper<const ProductionSourceBuildWorkItem> work_item_;
};

// system/source preparationが確定したactual work itemのidentityとtarget setを
// borrowする。BuildPlanからwork itemを再構築しない。
class PreparedSystemSourceBuildUnitReference final {
public:
    PreparedSystemSourceBuildUnitReference(
            std::reference_wrapper<
                    const RegisteredSourcePreferenceSnapshot> source,
            std::reference_wrapper<const std::string>
                    requested_package_name,
            std::reference_wrapper<const std::string>
                    checkout_package_base,
            RequiredTargetProvenance required_target_provenance,
            ArtifactLifecycleIntent artifact_lifecycle_intent,
            bool uses_system_update_baseline,
            std::reference_wrapper<const std::vector<
                    RequiredPackageArtifactTarget>> required_targets) noexcept;

    PreparedSystemSourceBuildUnitReference(
            const PreparedSystemSourceBuildUnitReference&) = delete;
    PreparedSystemSourceBuildUnitReference& operator=(
            const PreparedSystemSourceBuildUnitReference&) = delete;
    PreparedSystemSourceBuildUnitReference(
            PreparedSystemSourceBuildUnitReference&&) noexcept = default;
    PreparedSystemSourceBuildUnitReference& operator=(
            PreparedSystemSourceBuildUnitReference&&) noexcept = default;
    ~PreparedSystemSourceBuildUnitReference() = default;

    [[nodiscard]] const RegisteredSourcePreferenceSnapshot& source()
            const noexcept;
    [[nodiscard]] const std::vector<RequiredPackageArtifactTarget>&
    required_targets() const noexcept;
    [[nodiscard]] const std::string& requested_package_name() const noexcept;
    [[nodiscard]] const std::string& checkout_package_base() const noexcept;
    [[nodiscard]] RequiredTargetProvenance required_target_provenance()
            const noexcept;
    [[nodiscard]] ArtifactLifecycleIntent artifact_lifecycle_intent()
            const noexcept;
    [[nodiscard]] bool uses_system_update_baseline() const noexcept;
    [[nodiscard]] bool has_complete_identity() const noexcept;

private:
    std::reference_wrapper<const RegisteredSourcePreferenceSnapshot> source_;
    std::reference_wrapper<
            const std::vector<RequiredPackageArtifactTarget>>
            required_targets_;
    std::reference_wrapper<const std::string> requested_package_name_;
    std::reference_wrapper<const std::string> checkout_package_base_;
    RequiredTargetProvenance required_target_provenance_;
    ArtifactLifecycleIntent artifact_lifecycle_intent_;
    bool uses_system_update_baseline_ = false;
};

using UnifiedPlanBuildUnitReference = std::variant<
        AurPackageBaseBuildUnitReference,
        LocalSourceBuildUnitReference,
        PreparedRemoteSourceBuildUnitReference,
        PreparedSystemSourceBuildUnitReference>;

using UnifiedPlanRootMetadataAuthorityReference = std::variant<
        UnifiedPlanBorrowedAuthorityReference<RootPackageSearchCandidate>,
        UnifiedPlanBorrowedAuthorityReference<AurUpdatePlanEntry>,
        UnifiedPlanBorrowedAuthorityReference<LocalPackageMetadata>,
        UnifiedPlanBorrowedAuthorityReference<ResolvedSourceBuildIdentity>,
        UnifiedPlanBorrowedAuthorityReference<RepositoryPackagePresent>,
        UnifiedPlanBorrowedAuthorityReference<RepositoryPackageNotFound>,
        UnifiedPlanBorrowedAuthorityReference<
                RegisteredSourcePreferenceSnapshot>>;

class UnifiedPlanConfiguredRepositoryOrderReference final {
public:
    explicit UnifiedPlanConfiguredRepositoryOrderReference(
            std::reference_wrapper<const std::vector<std::string>>
                    configured_order) noexcept;

    UnifiedPlanConfiguredRepositoryOrderReference(
            const UnifiedPlanConfiguredRepositoryOrderReference&) = delete;
    UnifiedPlanConfiguredRepositoryOrderReference& operator=(
            const UnifiedPlanConfiguredRepositoryOrderReference&) = delete;
    UnifiedPlanConfiguredRepositoryOrderReference(
            UnifiedPlanConfiguredRepositoryOrderReference&&) noexcept =
            default;
    UnifiedPlanConfiguredRepositoryOrderReference& operator=(
            UnifiedPlanConfiguredRepositoryOrderReference&&) noexcept =
            default;
    ~UnifiedPlanConfiguredRepositoryOrderReference() = default;

    [[nodiscard]] const std::vector<std::string>& configured_order()
            const noexcept;

private:
    std::reference_wrapper<const std::vector<std::string>> configured_order_;
};

using UnifiedPlanRoutePreflightAuthorityReference = std::variant<
        UnifiedPlanBorrowedAuthorityReference<RootPackageRoutingProjection>,
        UnifiedPlanBorrowedAuthorityReference<LocalSourceRoot>,
        UnifiedPlanBorrowedAuthorityReference<
                LocalSourceBuildProjectionAuthority>,
        UnifiedPlanBorrowedAuthorityReference<LocalBuildPlan>,
        UnifiedPlanBorrowedAuthorityReference<FetchPreparation>,
        UnifiedPlanBorrowedAuthorityReference<PreparedSyncInstall>,
        UnifiedPlanBorrowedAuthorityReference<
                SyncInstallPreparationFailure>,
        UnifiedPlanBorrowedAuthorityReference<PreparedRemoteSourceBuild>,
        UnifiedPlanBorrowedAuthorityReference<RemoteSourceBuildPlanFailure>,
        UnifiedPlanBorrowedAuthorityReference<AurUpdateExecutionPreflight>,
        UnifiedPlanBorrowedAuthorityReference<
                AurUpdateSourceBuildPreparation>,
        UnifiedPlanBorrowedAuthorityReference<
                PreparedFilteredAurUpdateOperation>,
        UnifiedPlanBorrowedAuthorityReference<
                PreparedUpgradeAllAurPreflight>,
        UnifiedPlanBorrowedAuthorityReference<
                SystemSourceUpgradeProjectionAuthority>,
        UnifiedPlanBorrowedAuthorityReference<SystemSourceUpgradeResult>,
        UnifiedPlanBorrowedAuthorityReference<
                UpgradeAllOperationProjectionAuthority>,
        UnifiedPlanBorrowedAuthorityReference<UpgradeAllOperationResult>>;

// Pre-build required targetだけを表す。ProducedPackageArtifactやartifact path、
// produced versionへ変換するAPIは持たない。
class RequiredArtifactTargetReference final {
public:
    RequiredArtifactTargetReference(
            UnifiedPlanBuildUnitReference build_unit,
            std::reference_wrapper<const RequiredPackageArtifactTarget>
                    target);

    RequiredArtifactTargetReference(const RequiredArtifactTargetReference&) =
            delete;
    RequiredArtifactTargetReference& operator=(
            const RequiredArtifactTargetReference&) = delete;
    RequiredArtifactTargetReference(
            RequiredArtifactTargetReference&&) noexcept = default;
    RequiredArtifactTargetReference& operator=(
            RequiredArtifactTargetReference&&) noexcept = default;
    ~RequiredArtifactTargetReference() = default;

    [[nodiscard]] const UnifiedPlanBuildUnitReference& build_unit()
            const noexcept;
    // Return the small identity value, never a reference into projection-owned
    // artifact storage.
    [[nodiscard]] RequiredPackageArtifactTarget target() const;
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
    UnifiedPlanBorrowedAuthorityReference<RepositoryExactPackage> package;
};

struct RepositoryProviderInstallIntent {
    UnifiedPlanBorrowedAuthorityReference<ProvidedDependency> provider;
};

struct RepositorySystemUpgradeIntent {};

using RepositoryInstallIntentTarget = std::variant<
        RepositoryRootInstallIntent,
        RepositoryDependencyInstallIntent,
        RepositoryProviderInstallIntent,
        RepositorySystemUpgradeIntent>;

// This is an ordered target/options observation, not pacman argv or a prepared
// transaction capability.
struct RepositoryPackageTransactionIntent {
    RepositoryPackageTransactionIntent() = default;
    RepositoryPackageTransactionIntent(
            const RepositoryPackageTransactionIntent&) = delete;
    RepositoryPackageTransactionIntent& operator=(
            const RepositoryPackageTransactionIntent&) = delete;
    RepositoryPackageTransactionIntent(
            RepositoryPackageTransactionIntent&&) noexcept = default;
    RepositoryPackageTransactionIntent& operator=(
            RepositoryPackageTransactionIntent&&) noexcept = default;
    ~RepositoryPackageTransactionIntent() = default;

    std::vector<RepositoryInstallIntentTarget> targets;
    RepositoryTransactionPolicyView            policy;
};

struct SourceRootArtifactInstallIntent {
    std::size_t required_artifact_index;
};

struct SourceDependencyArtifactInstallIntent {
    std::size_t required_artifact_index;
};

using SourceArtifactInstallIntentTarget = std::variant<
        SourceRootArtifactInstallIntent,
        SourceDependencyArtifactInstallIntent>;

// Required targets identify the future install boundary. No package file path
// or post-build install-reason plan exists at this stage.
struct SourceBuiltArtifactInstallBoundaryIntent {
    SourceBuiltArtifactInstallBoundaryIntent() = default;
    SourceBuiltArtifactInstallBoundaryIntent(
            const SourceBuiltArtifactInstallBoundaryIntent&) = delete;
    SourceBuiltArtifactInstallBoundaryIntent& operator=(
            const SourceBuiltArtifactInstallBoundaryIntent&) = delete;
    SourceBuiltArtifactInstallBoundaryIntent(
            SourceBuiltArtifactInstallBoundaryIntent&&) noexcept = default;
    SourceBuiltArtifactInstallBoundaryIntent& operator=(
            SourceBuiltArtifactInstallBoundaryIntent&&) noexcept = default;
    ~SourceBuiltArtifactInstallBoundaryIntent() = default;

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
    UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge> detail;
};

struct AmbiguousUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<AmbiguousProvidedDependency> detail;
};

struct UnsupportedUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<
            MixedPackageBaseInstallReasonUnsupported>
            detail;
};

using SourceFailureUnifiedPlanBlockerDetail = std::variant<
        UnifiedPlanBorrowedAuthorityReference<BuildPlanResolutionFailure>,
        UnifiedPlanBorrowedAuthorityReference<IncompleteProviderCandidateSet>,
        UnifiedPlanBorrowedAuthorityReference<
                RepositoryExactPackageSourceFailure>,
        UnifiedPlanBorrowedAuthorityReference<RepositoryProviderSourceFailure>,
        UnifiedPlanBorrowedAuthorityReference<LocalSourceRootFailure>,
        UnifiedPlanBorrowedAuthorityReference<AurUpdateQueryFailure>>;

struct SourceFailureUnifiedPlanBlocker {
    SourceFailureUnifiedPlanBlockerDetail detail;
};

struct ConstraintFailureUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge> detail;
};

struct MetadataRiskUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<BuildPlanMetadataRisk> detail;
};

struct LocalDependencyPlanUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<LocalDependencyPlanFailure> detail;
};

// makepkg evaluationへ進めないread-only local routeが、既存descriptorの
// metadata stateを推測でReadyへ変換しないためのtyped blocker。
struct LocalSourceMetadataEvaluationUnifiedPlanBlocker {
    LocalSourceRootObservationIdentity source_root;
    UnifiedPlanBorrowedAuthorityReference<LocalSourceMetadataSnapshot> detail;
};

struct RootPackagePreparationUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<
            RootPackageInstallPreparationFailure>
            detail;
};

struct SyncInstallPreparationUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<SyncInstallPreparationFailure>
            detail;
};

struct BuildPlanArtifactProjectionUnifiedPlanBlocker {
    BuildPlanArtifactTargetProjectionIssue detail;
};

enum class BuildPlanStateUnifiedPlanBlockerKind {
    UnresolvedDependency,
    DependencyCycle,
    SplitPackageSelectionRequired
};

struct BuildPlanStateUnifiedPlanBlocker {
    UnifiedPlanBorrowedAuthorityReference<BuildPlan> authority;
    BuildPlanStateUnifiedPlanBlockerKind     kind;
    std::size_t                              authority_index;
};

using RoutePreflightUnifiedPlanBlockerDetail = std::variant<
        UnifiedPlanBorrowedAuthorityReference<AurUpdateExecutionIssue>,
        UnifiedPlanBorrowedAuthorityReference<AurUpdatePreparationIssue>,
        UnifiedPlanBorrowedAuthorityReference<SystemSourceUpgradeIssue>,
        UnifiedPlanBorrowedAuthorityReference<UpgradeAllOperationIssue>>;

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
        LocalDependencyPlanUnifiedPlanBlocker,
        LocalSourceMetadataEvaluationUnifiedPlanBlocker,
        RootPackagePreparationUnifiedPlanBlocker,
        SyncInstallPreparationUnifiedPlanBlocker,
        BuildPlanArtifactProjectionUnifiedPlanBlocker,
        BuildPlanStateUnifiedPlanBlocker,
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
    std::vector<UnifiedPlanRootMetadataAuthorityReference> root_metadata;
    std::optional<UnifiedPlanConfiguredRepositoryOrderReference>
            configured_repository_order;
    std::vector<UnifiedPlanRoutePreflightAuthorityReference>
            route_preflight_authorities;
};

enum class UnifiedPlanObservationInvariantIssueKind {
    UnknownStatus,
    ReadyHasBlockers,
    NoOpHasBlockers,
    NoOpHasMutationIntent,
    BlockedWithoutBlocker,
    BlockedHasMutationIntent,
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
    UnifiedPlanObservation(const UnifiedPlanObservation&) = delete;
    UnifiedPlanObservation(UnifiedPlanObservation&&) noexcept = default;
    UnifiedPlanObservation& operator=(const UnifiedPlanObservation&) = delete;
    UnifiedPlanObservation& operator=(UnifiedPlanObservation&&) noexcept =
            default;
    ~UnifiedPlanObservation() = default;

    [[nodiscard]] UnifiedPlanObservationStatus status() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanRootReference>& roots()
            const noexcept;
    [[nodiscard]] const std::vector<
            UnifiedPlanRootMetadataAuthorityReference>&
    root_metadata() const noexcept;
    [[nodiscard]] const UnifiedPlanConfiguredRepositoryOrderReference*
    configured_repository_order() const noexcept;
    [[nodiscard]] const std::vector<UnifiedPlanDependencyAuthorityReference>&
    dependency_authorities() const noexcept;
    [[nodiscard]] const std::vector<
            UnifiedPlanRoutePreflightAuthorityReference>&
    route_preflight_authorities() const noexcept;
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
            delete;
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
