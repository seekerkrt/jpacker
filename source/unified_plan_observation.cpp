#include "unified_plan_observation.hpp"

#include "source_install.hpp"
#include "system_source_upgrade.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace {

bool has_complete_local_source_identity(
    const LocalSourceRootObservationIdentity& identity) noexcept {
    return !identity.canonical_path.empty() &&
           identity.directory_identity.type == LocalSourceNodeType::Directory;
}

bool has_complete_repository_source_build_identity(
    const RepositorySourceBuildRootIdentity& identity) noexcept {
    return !identity.package_name.empty() && !identity.package_base.empty() &&
           !identity.canonical_source_identity_key.empty();
}

bool route_matches_source(
    UnifiedPlanRootSourceKind source,
    UnifiedPlanRootRouteKind route) noexcept {
    switch(source) {
        case UnifiedPlanRootSourceKind::Repository:
            return route == UnifiedPlanRootRouteKind::RepositoryTransaction ||
                   route == UnifiedPlanRootRouteKind::RepositorySourceBuild;
        case UnifiedPlanRootSourceKind::Aur:
            return route == UnifiedPlanRootRouteKind::AurSourceBuild;
        case UnifiedPlanRootSourceKind::Local:
            return route == UnifiedPlanRootRouteKind::LocalSourceBuild;
    }
    return false;
}

bool has_complete_build_unit_identity(
    const UnifiedPlanBuildUnitReference& build_unit) noexcept {
    return std::visit(
        [](const auto& reference) {
            return reference.has_complete_identity();
        },
        build_unit);
}

bool same_build_unit_reference(
    const UnifiedPlanBuildUnitReference& lhs,
    const UnifiedPlanBuildUnitReference& rhs) noexcept {
    if(lhs.index() != rhs.index()) return false;

    return std::visit(
        [](const auto& lhs_reference, const auto& rhs_reference) {
            using Lhs = std::decay_t<decltype(lhs_reference)>;
            using Rhs = std::decay_t<decltype(rhs_reference)>;
            if constexpr(!std::is_same_v<Lhs, Rhs>) {
                return false;
            } else if constexpr(
                std::is_same_v<Lhs,
                               AurPackageBaseBuildUnitReference>) {
                return &lhs_reference.authority() ==
                           &rhs_reference.authority() &&
                       lhs_reference.build_plan_order_index() ==
                           rhs_reference.build_plan_order_index();
            } else if constexpr(
                std::is_same_v<Lhs,
                               LocalSourceBuildUnitReference>) {
                return lhs_reference.source_root() ==
                           rhs_reference.source_root() &&
                       &lhs_reference.metadata() ==
                           &rhs_reference.metadata();
            } else if constexpr(
                std::is_same_v<
                    Lhs,
                    PreparedRemoteSourceBuildUnitReference>) {
                return &lhs_reference.source() ==
                           &rhs_reference.source() &&
                       &lhs_reference.work_item() ==
                           &rhs_reference.work_item();
            } else {
                return &lhs_reference.source() ==
                           &rhs_reference.source() &&
                       &lhs_reference.required_targets() ==
                           &rhs_reference.required_targets() &&
                       &lhs_reference.requested_package_name() ==
                           &rhs_reference.requested_package_name() &&
                       &lhs_reference.checkout_package_base() ==
                           &rhs_reference.checkout_package_base();
            }
        },
        lhs, rhs);
}

bool is_observed_build_unit(
    const std::vector<UnifiedPlanBuildUnitReference>& observed,
    const UnifiedPlanBuildUnitReference& candidate) noexcept {
    return std::any_of(
        observed.begin(), observed.end(),
        [&candidate](const UnifiedPlanBuildUnitReference& build_unit) {
            return same_build_unit_reference(build_unit, candidate);
        });
}

bool is_known_install_reason(DesiredInstallReason reason) noexcept {
    return reason == DesiredInstallReason::Explicit ||
           reason == DesiredInstallReason::Dependency;
}

bool is_valid_repository_transaction_target(
    const RepositoryInstallIntentTarget& target) noexcept {
    return std::visit(
        [](const auto& intent) {
            using Intent = std::decay_t<decltype(intent)>;
            if constexpr(std::is_same_v<
                             Intent, RepositoryRootInstallIntent>) {
                return intent.root.has_complete_identity() &&
                       intent.root.source_kind() ==
                           UnifiedPlanRootSourceKind::Repository &&
                       intent.root.route_kind() ==
                           UnifiedPlanRootRouteKind::
                               RepositoryTransaction;
            } else if constexpr(std::is_same_v<
                                    Intent,
                                    RepositoryDependencyInstallIntent>) {
                const RepositoryExactPackage& package =
                    intent.package.get();
                return !package.repository.repository_name.empty() &&
                       !package.package_name.empty();
            } else if constexpr(std::is_same_v<
                                    Intent,
                                    RepositoryProviderInstallIntent>) {
                const ProvidedDependency& provider = intent.provider.get();
                const auto* origin =
                    std::get_if<RepositoryProviderOrigin>(
                        &provider.origin);
                return origin != nullptr &&
                       !origin->repository_name.empty() &&
                       !provider.package_name.empty();
            } else {
                return true;
            }
        },
        target);
}

std::size_t source_intent_artifact_index(
    const SourceArtifactInstallIntentTarget& target) noexcept {
    return std::visit(
        [](const auto& intent) {
            return intent.required_artifact_index;
        },
        target);
}

bool is_valid_source_artifact_install_target(
    const SourceArtifactInstallIntentTarget& target,
    const std::vector<RequiredArtifactTargetReference>& artifacts) noexcept {
    const std::size_t artifact_index =
        source_intent_artifact_index(target);
    if(artifact_index >= artifacts.size()) return false;
    const RequiredArtifactTargetReference& artifact =
        artifacts[artifact_index];
    return std::visit(
        [&artifact](const auto& intent) {
            using Intent = std::decay_t<decltype(intent)>;
            const RequiredPackageArtifactTarget target_value =
                artifact.target();
            if(!artifact.matches_build_unit()) return false;
            if constexpr(std::is_same_v<
                             Intent,
                             SourceRootArtifactInstallIntent>) {
                return target_value.desired_reason ==
                       DesiredInstallReason::Explicit;
            } else {
                return target_value.desired_reason ==
                       DesiredInstallReason::Dependency;
            }
        },
        target);
}

bool transaction_stage_matches_intent(
    const UnifiedPlanTransactionIntent& intent) noexcept {
    return std::visit(
        [](const auto& typed_intent) {
            using Intent = std::decay_t<decltype(typed_intent)>;
            switch(typed_intent.stage) {
                case UnifiedPlanTransactionIntentStage::RouteOwned:
                    return true;
                case UnifiedPlanTransactionIntentStage::
                    RepositorySystemUpgrade:
                    if constexpr(std::is_same_v<
                                     Intent,
                                     RepositoryPackageTransactionIntent>) {
                        return typed_intent.targets.size() == 1 &&
                               std::holds_alternative<
                                   RepositorySystemUpgradeIntent>(
                                   typed_intent.targets.front());
                    }
                    return false;
                case UnifiedPlanTransactionIntentStage::LaterNormalAur:
                    if constexpr(std::is_same_v<
                                     Intent,
                                     RepositoryPackageTransactionIntent>) {
                        return !typed_intent.targets.empty() &&
                               std::none_of(
                                   typed_intent.targets.begin(),
                                   typed_intent.targets.end(),
                                   [](const RepositoryInstallIntentTarget&
                                          target) {
                                       return std::holds_alternative<
                                           RepositorySystemUpgradeIntent>(
                                           target);
                                   });
                    }
                    return !typed_intent.targets.empty();
            }
            return false;
        },
        intent);
}

std::optional<std::size_t> observation_phase_rank(
    UnifiedPlanObservationPhase phase) noexcept {
    for(std::size_t index = 0;
        index < UNIFIED_PLAN_OBSERVATION_PHASE_ORDER.size(); ++index) {
        if(UNIFIED_PLAN_OBSERVATION_PHASE_ORDER[index] == phase) return index;
    }
    return std::nullopt;
}

void add_invariant_issue(
    std::vector<UnifiedPlanObservationInvariantIssue>& issues,
    UnifiedPlanObservationInvariantIssueKind kind,
    std::optional<std::size_t> primary_index = std::nullopt,
    std::optional<std::size_t> secondary_index = std::nullopt) {
    issues.push_back(UnifiedPlanObservationInvariantIssue{
        kind, primary_index, secondary_index});
}

} // namespace

UnifiedPlanRootReference::UnifiedPlanRootReference(
    RootTargetIdentity invocation_correlation,
    UnifiedPlanRootIdentity source_identity,
    UnifiedPlanRootRouteKind route_kind)
    : invocation_correlation_(std::move(invocation_correlation)),
      source_identity_(std::move(source_identity)), route_kind_(route_kind) {
}

const RootTargetIdentity&
UnifiedPlanRootReference::invocation_correlation() const noexcept {
    return invocation_correlation_;
}

const UnifiedPlanRootIdentity&
UnifiedPlanRootReference::source_identity() const noexcept {
    return source_identity_;
}

UnifiedPlanRootSourceKind UnifiedPlanRootReference::source_kind()
    const noexcept {
    if(std::holds_alternative<RepositoryRootPackageIdentity>(
           source_identity_) ||
       std::holds_alternative<RepositorySourceBuildRootIdentity>(
           source_identity_)) {
        return UnifiedPlanRootSourceKind::Repository;
    }
    if(std::holds_alternative<AurRootPackageIdentity>(source_identity_)) {
        return UnifiedPlanRootSourceKind::Aur;
    }
    return UnifiedPlanRootSourceKind::Local;
}

UnifiedPlanRootRouteKind UnifiedPlanRootReference::route_kind()
    const noexcept {
    return route_kind_;
}

bool UnifiedPlanRootReference::has_complete_identity() const noexcept {
    if(invocation_correlation_.requested_name.empty()) return false;

    const bool identity_is_complete = std::visit(
        [](const auto& identity) {
            using Identity = std::decay_t<decltype(identity)>;
            if constexpr(std::is_same_v<
                             Identity,
                             RepositoryRootPackageIdentity>) {
                return !identity.repository_name.empty() &&
                       !identity.package_name.empty();
            } else if constexpr(std::is_same_v<
                                    Identity,
                                    RepositorySourceBuildRootIdentity>) {
                return has_complete_repository_source_build_identity(
                    identity);
            } else if constexpr(std::is_same_v<
                                    Identity,
                                    AurRootPackageIdentity>) {
                return !identity.package_name.empty() &&
                       !identity.package_base.empty();
            } else {
                return has_complete_local_source_identity(identity);
            }
        },
        source_identity_);
    return identity_is_complete && route_matches_source(source_kind(), route_kind_);
}

UnifiedPlanConfiguredRepositoryOrderReference::
    UnifiedPlanConfiguredRepositoryOrderReference(
        std::reference_wrapper<const std::vector<std::string>>
            configured_order) noexcept
    : configured_order_(configured_order) {
}

const std::vector<std::string>&
UnifiedPlanConfiguredRepositoryOrderReference::configured_order()
    const noexcept {
    return configured_order_.get();
}

UnifiedPlanDependencyAuthorityReference::
    UnifiedPlanDependencyAuthorityReference(Authority authority) noexcept
    : authority_(authority) {
}

UnifiedPlanDependencyAuthorityReference
UnifiedPlanDependencyAuthorityReference::from_build_plan(
    const BuildPlan& plan) noexcept {
    return UnifiedPlanDependencyAuthorityReference(std::cref(plan));
}

UnifiedPlanDependencyAuthorityReference
UnifiedPlanDependencyAuthorityReference::from_local_build_plan(
    const LocalBuildPlan& plan) noexcept {
    return UnifiedPlanDependencyAuthorityReference(std::cref(plan));
}

const BuildPlan* UnifiedPlanDependencyAuthorityReference::build_plan()
    const noexcept {
    const auto* reference =
        std::get_if<std::reference_wrapper<const BuildPlan>>(&authority_);
    return reference == nullptr ? nullptr : &reference->get();
}

const LocalBuildPlan*
UnifiedPlanDependencyAuthorityReference::local_build_plan() const noexcept {
    const auto* reference =
        std::get_if<std::reference_wrapper<const LocalBuildPlan>>(
            &authority_);
    return reference == nullptr ? nullptr : &reference->get();
}

AurPackageBaseBuildUnitReference::AurPackageBaseBuildUnitReference(
    std::reference_wrapper<const BuildPlan> authority,
    std::size_t build_plan_order_index) noexcept
    : authority_(authority), build_plan_order_index_(build_plan_order_index) {
}

const BuildPlan& AurPackageBaseBuildUnitReference::authority() const noexcept {
    return authority_.get();
}

std::size_t AurPackageBaseBuildUnitReference::build_plan_order_index()
    const noexcept {
    return build_plan_order_index_;
}

const BuildPlanEntry* AurPackageBaseBuildUnitReference::entry()
    const noexcept {
    const BuildPlan& plan = authority_.get();
    if(build_plan_order_index_ >= plan.order.size()) return nullptr;
    return &plan.order[build_plan_order_index_];
}

bool AurPackageBaseBuildUnitReference::has_complete_identity() const noexcept {
    const BuildPlanEntry* build_unit = entry();
    if(build_unit == nullptr || build_unit->package_base.empty() ||
       build_unit->package_names.empty()) {
        return false;
    }
    return std::all_of(
        build_unit->package_names.begin(), build_unit->package_names.end(),
        [](const std::string& package_name) {
            return !package_name.empty();
        });
}

LocalSourceBuildUnitReference::LocalSourceBuildUnitReference(
    LocalSourceRootObservationIdentity source_root,
    std::reference_wrapper<const LocalPackageMetadata> metadata)
    : source_root_(std::move(source_root)), metadata_(metadata) {
}

const LocalSourceRootObservationIdentity&
LocalSourceBuildUnitReference::source_root() const noexcept {
    return source_root_;
}

const LocalPackageMetadata& LocalSourceBuildUnitReference::metadata()
    const noexcept {
    return metadata_.get();
}

bool LocalSourceBuildUnitReference::has_complete_identity() const noexcept {
    const LocalPackageMetadata& local_metadata = metadata_.get();
    if(!has_complete_local_source_identity(source_root_) ||
       local_metadata.package_base.empty() || local_metadata.children.empty()) {
        return false;
    }
    return std::all_of(
        local_metadata.children.begin(), local_metadata.children.end(),
        [](const LocalPackageMetadataChild& child) {
            return !child.name.empty();
        });
}

PreparedRemoteSourceBuildUnitReference::
    PreparedRemoteSourceBuildUnitReference(
        std::reference_wrapper<
            const ResolvedSourceBuildIdentity>
            source,
        std::reference_wrapper<
            const ProductionSourceBuildWorkItem>
            work_item) noexcept
    : source_(source), work_item_(work_item) {
}

const ResolvedSourceBuildIdentity&
PreparedRemoteSourceBuildUnitReference::source() const noexcept {
    return source_.get();
}

const ProductionSourceBuildWorkItem&
PreparedRemoteSourceBuildUnitReference::work_item() const noexcept {
    return work_item_.get();
}

const std::vector<RequiredPackageArtifactTarget>&
PreparedRemoteSourceBuildUnitReference::required_targets() const noexcept {
    return work_item_.get().required_targets;
}

bool PreparedRemoteSourceBuildUnitReference::has_complete_identity()
    const noexcept {
    const ResolvedSourceBuildIdentity& source = source_.get();
    const ProductionSourceBuildWorkItem& work = work_item_.get();
    const ResolvedRepositorySourceBuildIdentity* repository =
        source.repository_identity();
    if(repository == nullptr || source.requested_name().empty() ||
       source.package_base().empty() || source.canonical_source_key().empty() ||
       source.git_url().empty() ||
       work.request.package_name != source.requested_name() ||
       work.request.checkout_name != source.package_base() ||
       work.request.git_url != source.git_url() ||
       work.required_target_provenance !=
           RequiredTargetProvenance::RepositoryExactPackageProjection ||
       (work.artifact_lifecycle_intent !=
            ArtifactLifecycleIntent::SingularCompatibility &&
        work.artifact_lifecycle_intent !=
            ArtifactLifecycleIntent::PackageBaseSet) ||
       work.repository_identity !=
           std::optional<ResolvedRepositorySourceBuildIdentity>{
               *repository} ||
       work.required_targets.size() != 1) {
        return false;
    }
    const RequiredPackageArtifactTarget& target =
        work.required_targets.front();
    return target.package_base == source.package_base() &&
           target.package_name == source.requested_name() &&
           is_known_install_reason(target.desired_reason);
}

PreparedSystemSourceBuildUnitReference::
    PreparedSystemSourceBuildUnitReference(
        std::reference_wrapper<
            const RegisteredSourcePreferenceSnapshot>
            source,
        std::reference_wrapper<const std::string>
            requested_package_name,
        std::reference_wrapper<const std::string>
            checkout_package_base,
        RequiredTargetProvenance required_target_provenance,
        ArtifactLifecycleIntent artifact_lifecycle_intent,
        bool uses_system_update_baseline,
        std::reference_wrapper<const std::vector<
            RequiredPackageArtifactTarget>>
            required_targets) noexcept
    : source_(source), required_targets_(required_targets),
      requested_package_name_(requested_package_name),
      checkout_package_base_(checkout_package_base),
      required_target_provenance_(required_target_provenance),
      artifact_lifecycle_intent_(artifact_lifecycle_intent),
      uses_system_update_baseline_(uses_system_update_baseline) {
}

const RegisteredSourcePreferenceSnapshot&
PreparedSystemSourceBuildUnitReference::source() const noexcept {
    return source_.get();
}

const std::vector<RequiredPackageArtifactTarget>&
PreparedSystemSourceBuildUnitReference::required_targets() const noexcept {
    return required_targets_.get();
}

const std::string&
PreparedSystemSourceBuildUnitReference::requested_package_name()
    const noexcept {
    return requested_package_name_.get();
}

const std::string&
PreparedSystemSourceBuildUnitReference::checkout_package_base()
    const noexcept {
    return checkout_package_base_.get();
}

RequiredTargetProvenance
PreparedSystemSourceBuildUnitReference::required_target_provenance()
    const noexcept {
    return required_target_provenance_;
}

ArtifactLifecycleIntent
PreparedSystemSourceBuildUnitReference::artifact_lifecycle_intent()
    const noexcept {
    return artifact_lifecycle_intent_;
}

bool PreparedSystemSourceBuildUnitReference::uses_system_update_baseline()
    const noexcept {
    return uses_system_update_baseline_;
}

bool PreparedSystemSourceBuildUnitReference::has_complete_identity()
    const noexcept {
    const RegisteredSourcePreferenceSnapshot& prepared_source = source_.get();
    const auto& targets = required_targets_.get();
    const bool is_repository =
        prepared_source.source_kind ==
        std::optional<SourceBuildSourceKind>{
            SourceBuildSourceKind::Repository};
    const ArtifactLifecycleIntent expected_lifecycle = is_repository
                                                           ? ArtifactLifecycleIntent::PackageBaseSet
                                                           : ArtifactLifecycleIntent::SingularCompatibility;
    if(prepared_source.preference_package_name.empty() ||
       !prepared_source.resolved_package_base.has_value() ||
       prepared_source.resolved_package_base->empty() ||
       !prepared_source.canonical_source_identity_key.has_value() ||
       prepared_source.canonical_source_identity_key->empty() ||
       !prepared_source.source_kind.has_value() ||
       (prepared_source.source_kind.value() !=
            SourceBuildSourceKind::Repository &&
        prepared_source.source_kind.value() != SourceBuildSourceKind::Aur) ||
       targets.empty() ||
       requested_package_name_.get().empty() ||
       checkout_package_base_.get().empty() ||
       artifact_lifecycle_intent_ != expected_lifecycle ||
       prepared_source.required_target_provenance !=
           std::optional<RequiredTargetProvenance>{
               required_target_provenance_} ||
       prepared_source.artifact_lifecycle_intent !=
           std::optional<ArtifactLifecycleIntent>{
               artifact_lifecycle_intent_} ||
       requested_package_name_.get() !=
           prepared_source.preference_package_name ||
       checkout_package_base_.get() !=
           prepared_source.resolved_package_base.value() ||
       targets.size() != 1 || uses_system_update_baseline_ != is_repository) {
        return false;
    }
    if(is_repository) {
        if(required_target_provenance_ !=
               RequiredTargetProvenance::
                   RepositoryExactPackageProjection ||
           !prepared_source.repository_identity.has_value()) {
            return false;
        }
    } else if(required_target_provenance_ !=
                  RequiredTargetProvenance::AurBuildPlanProjection ||
              prepared_source.repository_identity.has_value()) {
        return false;
    }
    const RequiredPackageArtifactTarget& target = targets.front();
    return target.package_base == checkout_package_base_.get() &&
           target.package_name == requested_package_name_.get() &&
           is_known_install_reason(target.desired_reason);
}

RequiredArtifactTargetReference::RequiredArtifactTargetReference(
    UnifiedPlanBuildUnitReference build_unit,
    std::reference_wrapper<const RequiredPackageArtifactTarget> target)
    : build_unit_(std::move(build_unit)), target_(target) {
}

const UnifiedPlanBuildUnitReference&
RequiredArtifactTargetReference::build_unit() const noexcept {
    return build_unit_;
}

RequiredPackageArtifactTarget RequiredArtifactTargetReference::target()
    const {
    return target_.get();
}

bool RequiredArtifactTargetReference::matches_build_unit() const noexcept {
    const RequiredPackageArtifactTarget& required = target_.get();
    if(required.package_base.empty() || required.package_name.empty() ||
       !is_known_install_reason(required.desired_reason)) {
        return false;
    }

    return std::visit(
        [&required](const auto& build_unit) {
            using BuildUnit = std::decay_t<decltype(build_unit)>;
            if(!build_unit.has_complete_identity()) return false;
            if constexpr(std::is_same_v<
                             BuildUnit,
                             AurPackageBaseBuildUnitReference>) {
                const BuildPlanEntry* entry = build_unit.entry();
                return entry != nullptr &&
                       entry->package_base == required.package_base &&
                       std::find(
                           entry->package_names.begin(),
                           entry->package_names.end(),
                           required.package_name) !=
                           entry->package_names.end();
            } else if constexpr(std::is_same_v<
                                    BuildUnit,
                                    LocalSourceBuildUnitReference>) {
                const LocalPackageMetadata& metadata =
                    build_unit.metadata();
                return metadata.package_base == required.package_base &&
                       std::any_of(
                           metadata.children.begin(),
                           metadata.children.end(),
                           [&required](
                               const LocalPackageMetadataChild&
                                   child) {
                               return child.name ==
                                      required.package_name;
                           });
            } else if constexpr(std::is_same_v<
                                    BuildUnit,
                                    PreparedRemoteSourceBuildUnitReference>) {
                return std::any_of(
                    build_unit.required_targets().begin(),
                    build_unit.required_targets().end(),
                    [&required](
                        const RequiredPackageArtifactTarget&
                            target) {
                        return &target == &required;
                    });
            } else {
                const RegisteredSourcePreferenceSnapshot& source =
                    build_unit.source();
                const auto& targets = build_unit.required_targets();
                return source.resolved_package_base.has_value() &&
                       source.resolved_package_base.value() ==
                           required.package_base &&
                       std::any_of(
                           targets.begin(), targets.end(),
                           [&required](
                               const RequiredPackageArtifactTarget&
                                   target) {
                               return &target == &required;
                           });
            }
        },
        build_unit_);
}

UnifiedPlanObservation::UnifiedPlanObservation(
    UnifiedPlanObservationInput input)
    : input_(std::move(input)) {
}

UnifiedPlanObservationStatus UnifiedPlanObservation::status() const noexcept {
    return input_.status;
}

const std::vector<UnifiedPlanRootReference>& UnifiedPlanObservation::roots()
    const noexcept {
    return input_.roots;
}

const std::vector<UnifiedPlanRootMetadataAuthorityReference>&
UnifiedPlanObservation::root_metadata() const noexcept {
    return input_.root_metadata;
}

const UnifiedPlanConfiguredRepositoryOrderReference*
UnifiedPlanObservation::configured_repository_order() const noexcept {
    return input_.configured_repository_order.has_value()
               ? &input_.configured_repository_order.value()
               : nullptr;
}

const std::vector<UnifiedPlanDependencyAuthorityReference>&
UnifiedPlanObservation::dependency_authorities() const noexcept {
    return input_.dependency_authorities;
}

const std::vector<UnifiedPlanRoutePreflightAuthorityReference>&
UnifiedPlanObservation::route_preflight_authorities() const noexcept {
    return input_.route_preflight_authorities;
}

const std::vector<UnifiedPlanBuildUnitReference>&
UnifiedPlanObservation::build_units() const noexcept {
    return input_.build_units;
}

const std::vector<RequiredArtifactTargetReference>&
UnifiedPlanObservation::required_artifacts() const noexcept {
    return input_.required_artifacts;
}

const std::vector<UnifiedPlanTransactionIntent>&
UnifiedPlanObservation::transaction_intents() const noexcept {
    return input_.transaction_intents;
}

const std::vector<UnifiedPlanPhaseReference>& UnifiedPlanObservation::phases()
    const noexcept {
    return input_.phases;
}

const std::vector<UnifiedPlanBlocker>& UnifiedPlanObservation::blockers()
    const noexcept {
    return input_.blockers;
}

UnifiedPlanObservationResult::UnifiedPlanObservationResult(
    UnifiedPlanObservation observation)
    : outcome_(std::move(observation)) {
}

UnifiedPlanObservationResult::UnifiedPlanObservationResult(
    InvalidUnifiedPlanObservation failure)
    : outcome_(std::move(failure)) {
}

bool UnifiedPlanObservationResult::is_valid() const noexcept {
    return std::holds_alternative<UnifiedPlanObservation>(outcome_);
}

const UnifiedPlanObservation* UnifiedPlanObservationResult::observation()
    const noexcept {
    return std::get_if<UnifiedPlanObservation>(&outcome_);
}

const InvalidUnifiedPlanObservation* UnifiedPlanObservationResult::failure()
    const noexcept {
    return std::get_if<InvalidUnifiedPlanObservation>(&outcome_);
}

UnifiedPlanObservationResult make_unified_plan_observation(
    UnifiedPlanObservationInput input) {
    std::vector<UnifiedPlanObservationInvariantIssue> issues;

    switch(input.status) {
        case UnifiedPlanObservationStatus::Ready:
            if(!input.blockers.empty()) {
                add_invariant_issue(
                    issues,
                    UnifiedPlanObservationInvariantIssueKind::
                        ReadyHasBlockers);
            }
            break;
        case UnifiedPlanObservationStatus::NoOp:
            if(!input.blockers.empty()) {
                add_invariant_issue(
                    issues,
                    UnifiedPlanObservationInvariantIssueKind::
                        NoOpHasBlockers);
            }
            if(!input.transaction_intents.empty()) {
                add_invariant_issue(
                    issues,
                    UnifiedPlanObservationInvariantIssueKind::
                        NoOpHasMutationIntent);
            }
            break;
        case UnifiedPlanObservationStatus::Blocked:
            if(input.blockers.empty()) {
                add_invariant_issue(
                    issues,
                    UnifiedPlanObservationInvariantIssueKind::
                        BlockedWithoutBlocker);
            }
            if(!input.transaction_intents.empty()) {
                add_invariant_issue(
                    issues,
                    UnifiedPlanObservationInvariantIssueKind::
                        BlockedHasMutationIntent);
            }
            break;
        default:
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::UnknownStatus);
            break;
    }

    if(input.status != UnifiedPlanObservationStatus::Blocked) {
        for(std::size_t index = 0; index < input.roots.size(); ++index) {
            if(input.roots[index].has_complete_identity()) continue;
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    NonBlockedRootIdentityIncomplete,
                index);
        }
        for(std::size_t index = 0; index < input.build_units.size(); ++index) {
            if(has_complete_build_unit_identity(input.build_units[index])) {
                continue;
            }
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    NonBlockedBuildUnitIdentityIncomplete,
                index);
        }
    }

    for(std::size_t index = 0; index < input.required_artifacts.size();
        ++index) {
        const RequiredArtifactTargetReference& artifact =
            input.required_artifacts[index];
        const RequiredPackageArtifactTarget target = artifact.target();
        if(target.package_base.empty() || target.package_name.empty() ||
           !is_known_install_reason(target.desired_reason)) {
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    RequiredArtifactTargetIncomplete,
                index);
        } else if(!artifact.matches_build_unit()) {
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    RequiredArtifactBuildUnitMismatch,
                index);
        }
        if(!is_observed_build_unit(input.build_units, artifact.build_unit())) {
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    RequiredArtifactBuildUnitNotObserved,
                index);
        }
    }

    for(std::size_t intent_index = 0;
        intent_index < input.transaction_intents.size(); ++intent_index) {
        const UnifiedPlanTransactionIntent& intent =
            input.transaction_intents[intent_index];
        if(!transaction_stage_matches_intent(intent)) {
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    TransactionIntentStageInvalid,
                intent_index);
        }
        std::visit(
            [&](const auto& typed_intent) {
                using Intent = std::decay_t<decltype(typed_intent)>;
                if constexpr(std::is_same_v<
                                 Intent,
                                 RepositoryPackageTransactionIntent>) {
                    if(typed_intent.targets.empty()) {
                        add_invariant_issue(
                            issues,
                            UnifiedPlanObservationInvariantIssueKind::
                                RepositoryTransactionHasNoTarget,
                            intent_index);
                    }
                    for(std::size_t target_index = 0;
                        target_index < typed_intent.targets.size();
                        ++target_index) {
                        if(is_valid_repository_transaction_target(
                               typed_intent.targets[target_index])) {
                            continue;
                        }
                        add_invariant_issue(
                            issues,
                            UnifiedPlanObservationInvariantIssueKind::
                                RepositoryTransactionTargetInvalid,
                            intent_index, target_index);
                    }
                } else {
                    if(typed_intent.targets.empty()) {
                        add_invariant_issue(
                            issues,
                            UnifiedPlanObservationInvariantIssueKind::
                                SourceArtifactInstallHasNoTarget,
                            intent_index);
                    }
                    for(std::size_t target_index = 0;
                        target_index < typed_intent.targets.size();
                        ++target_index) {
                        const SourceArtifactInstallIntentTarget& target =
                            typed_intent.targets[target_index];
                        if(!is_valid_source_artifact_install_target(
                               target,
                               input.required_artifacts)) {
                            add_invariant_issue(
                                issues,
                                UnifiedPlanObservationInvariantIssueKind::
                                    SourceArtifactInstallTargetInvalid,
                                intent_index, target_index);
                        }
                        if(source_intent_artifact_index(target) >=
                           input.required_artifacts.size()) {
                            add_invariant_issue(
                                issues,
                                UnifiedPlanObservationInvariantIssueKind::
                                    SourceArtifactInstallTargetNotObserved,
                                intent_index, target_index);
                        }
                    }
                }
            },
            intent);
    }

    std::optional<std::size_t> previous_phase_rank;
    for(std::size_t index = 0; index < input.phases.size(); ++index) {
        const std::optional<std::size_t> current_phase_rank =
            observation_phase_rank(input.phases[index].observation_phase);
        if(!current_phase_rank.has_value() ||
           (previous_phase_rank.has_value() &&
            current_phase_rank.value() < previous_phase_rank.value())) {
            add_invariant_issue(
                issues,
                UnifiedPlanObservationInvariantIssueKind::
                    ObservationPhaseOrderInvalid,
                index);
        }
        if(current_phase_rank.has_value()) {
            previous_phase_rank = current_phase_rank;
        }
    }

    if(!issues.empty()) {
        return UnifiedPlanObservationResult(
            InvalidUnifiedPlanObservation{std::move(issues)});
    }
    return UnifiedPlanObservationResult(
        UnifiedPlanObservation(std::move(input)));
}
