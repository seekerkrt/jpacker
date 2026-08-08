#include "aur_update_execution_preflight.hpp"
#include "aur_update_query.hpp"
#include "commands_sync.hpp"
#include "local_dependency_plan_projection.hpp"
#include "root_package_route_projection.hpp"
#include "system_source_upgrade.hpp"
#include "unified_plan_projection.hpp"
#include "upgrade_all_operation.hpp"

#include <algorithm>
#include <concepts>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

struct UnifiedPlanProjectionTestAccess {
    static LocalSourceBuildProjectionAuthority make_local_source(
            const LocalSourceRoot& source_root,
            const LocalBuildPlan& local_plan,
            const SourceBuildEnvironment& source_environment) {
        return LocalSourceBuildProjectionAuthority(
                source_root, local_plan, local_plan.local_metadata(),
                source_environment, local_plan.effective_architecture(),
                LocalSourceBuildMetadataProvenance::ExistingSrcinfo,
                source_root.directory_identity(), source_root.pkgbuild());
    }

    static SystemSourceUpgradeProjectionAuthority make_system_source(
            const SystemSourceUpgradePreparedSnapshot& snapshot,
            const BuildPlan* aur_plan,
            const std::vector<SystemSourceUpgradeIssue>& issues,
            const std::vector<ProductionSourceBuildWorkItem>& work_items) {
        if(snapshot.registered_sources.size() != work_items.size()) {
            throw std::invalid_argument(
                    "test source/work fixture size mismatch");
        }
        std::vector<PreparedSystemSourceWorkReference> references;
        references.reserve(work_items.size());
        for(std::size_t index = 0; index < work_items.size(); ++index) {
            references.push_back(PreparedSystemSourceWorkReference(
                    snapshot.registered_sources[index],
                    work_items[index]));
        }
        return SystemSourceUpgradeProjectionAuthority(
                snapshot, aur_plan, issues, std::move(references));
    }

    static UpgradeAllOperationProjectionAuthority make_upgrade_all(
            const UpgradeAllOperationPreparedSnapshot& snapshot,
            const SystemSourceUpgradeProjectionAuthority& system_source) {
        return UpgradeAllOperationProjectionAuthority(
                snapshot, system_source);
    }
};

namespace {

template<typename T>
concept HasExecuteMember = requires(T value) { value.execute(); };

template<typename T>
concept HasCommandMember = requires(T value) { value.command; };

template<typename T>
concept HasArgvMember = requires(T value) { value.argv; };

template<typename T>
concept HasArtifactProjectionAccessor = requires(const T& value) {
    value.artifact_target_projections();
};

template<typename T>
concept HasRepositoryConfigurationMember = requires(T value) {
    value.repository_configuration;
};

template<typename T>
concept HasLocalSourceRootMember = requires(T value) {
    value.source_root;
};

template<typename T>
concept HasLocalBuildPlanMember = requires(T value) {
    value.local_build_plan;
};

template<typename T>
concept HasNeededMember = requires(T value) {
    value.needed;
};

using ProjectionResultAccess = decltype(
        std::declval<const UnifiedPlanProjection&>().observation_result());
using ObservationAccess = decltype(
        std::declval<const UnifiedPlanObservationResult&>().observation());
using ObservationBorrow = decltype(
        *std::declval<const UnifiedPlanObservationResult&>().observation());

static_assert(!HasExecuteMember<UnifiedPlanProjection>);
static_assert(!HasCommandMember<UnifiedPlanProjection>);
static_assert(!HasArgvMember<UnifiedPlanProjection>);
static_assert(!HasExecuteMember<SystemSourceUpgradeProjectionAuthority>);
static_assert(!HasCommandMember<SystemSourceUpgradeProjectionAuthority>);
static_assert(!HasArgvMember<SystemSourceUpgradeProjectionAuthority>);
static_assert(!HasExecuteMember<UpgradeAllOperationProjectionAuthority>);
static_assert(!HasCommandMember<UpgradeAllOperationProjectionAuthority>);
static_assert(!HasArgvMember<UpgradeAllOperationProjectionAuthority>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanProjection>);
static_assert(!std::is_move_constructible_v<UnifiedPlanProjection>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanObservationResult>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanObservation>);
static_assert(!std::is_copy_constructible_v<RequiredArtifactTargetReference>);
static_assert(!std::is_constructible_v<
              RequiredArtifactTargetReference,
              const RequiredArtifactTargetReference&&>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanBlocker>);
static_assert(!std::is_constructible_v<
              UnifiedPlanBlocker, const UnifiedPlanBlocker&&>);
static_assert(!std::is_copy_constructible_v<UnifiedPlanTransactionIntent>);
static_assert(!std::is_copy_constructible_v<
              UnifiedPlanBuildUnitReference>);
static_assert(!std::is_copy_constructible_v<
              UnifiedPlanRootMetadataAuthorityReference>);
static_assert(!std::is_copy_constructible_v<
              UnifiedPlanRoutePreflightAuthorityReference>);
static_assert(!std::is_copy_constructible_v<
              PreparedSystemSourceWorkReference>);
static_assert(!std::is_constructible_v<
              PreparedSystemSourceWorkReference,
              const PreparedSystemSourceWorkReference&&>);
static_assert(!HasArtifactProjectionAccessor<UnifiedPlanProjection>);
static_assert(!HasRepositoryConfigurationMember<
              LocalSourceUnifiedPlanProjectionInput>);
static_assert(!HasRepositoryConfigurationMember<
              AurUpdateUnifiedPlanProjectionInput>);
static_assert(!HasRepositoryConfigurationMember<
              SystemSourceUpgradeUnifiedPlanProjectionInput>);
static_assert(!HasRepositoryConfigurationMember<
              UpgradeAllUnifiedPlanProjectionInput>);
static_assert(!HasLocalSourceRootMember<
              LocalSourceUnifiedPlanProjectionInput>);
static_assert(!HasLocalBuildPlanMember<
              LocalSourceUnifiedPlanProjectionInput>);
static_assert(!HasNeededMember<UpgradeAllUnifiedPlanProjectionInput>);
using NestedProviderBorrow = decltype(
        std::declval<const RepositoryProviderInstallIntent&>().provider);
static_assert(!std::is_copy_constructible_v<NestedProviderBorrow>);
static_assert(!std::is_constructible_v<
              NestedProviderBorrow, const NestedProviderBorrow&&>);
static_assert(std::same_as<
              ProjectionResultAccess,
              const UnifiedPlanObservationResult&>);
static_assert(std::same_as<
              ObservationAccess,
              const UnifiedPlanObservation*>);
static_assert(!std::constructible_from<
              UnifiedPlanObservationResult,
              ProjectionResultAccess>);
static_assert(!std::constructible_from<
              UnifiedPlanObservation,
              ObservationBorrow>);
static_assert(!std::is_constructible_v<UnifiedPlanBlocker, std::string>);
static_assert(!std::constructible_from<
              std::reference_wrapper<const PreparedRootPackageInstall>,
              PreparedRootPackageInstall&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<
                      const RootPackageInstallPreparationFailure>,
              RootPackageInstallPreparationFailure&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<const BuildPlan>, BuildPlan&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<const LocalBuildPlan>,
              LocalBuildPlan&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<
                      const LocalSourceBuildProjectionAuthority>,
              LocalSourceBuildProjectionAuthority&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<
                      const SystemSourceUpgradeProjectionAuthority>,
              SystemSourceUpgradeProjectionAuthority&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<const SystemSourceUpgradeResult>,
              SystemSourceUpgradeResult&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<
                      const UpgradeAllOperationProjectionAuthority>,
              UpgradeAllOperationProjectionAuthority&&>);
static_assert(!std::constructible_from<
              std::reference_wrapper<const UpgradeAllOperationResult>,
              UpgradeAllOperationResult&&>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Callback>
void expect_invalid_argument(Callback&& callback, std::string_view context) {
    bool rejected = false;
    try {
        std::forward<Callback>(callback)();
    } catch(const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, std::string(context) + " was not rejected");
}

const UnifiedPlanObservation& require_observation(
        const UnifiedPlanProjection& projection,
        std::string_view context) {
    const UnifiedPlanObservationResult& result =
            projection.observation_result();
    expect(
            result.is_valid(),
            std::string(context) + " observation is invalid");
    expect(
            result.observation() != nullptr,
            std::string(context) + " observation is missing");
    return *result.observation();
}

bool has_phase(
        const UnifiedPlanObservation& observation,
        UnifiedPlanObservationPhase phase,
        std::optional<UnifiedPlanAuthorityOwner> owner = std::nullopt) {
    for(const UnifiedPlanPhaseReference& candidate : observation.phases()) {
        if(candidate.observation_phase == phase &&
           (!owner.has_value() || candidate.owner == owner.value())) {
            return true;
        }
    }
    return false;
}

template<typename Phase>
bool has_existing_phase(
        const UnifiedPlanObservation& observation,
        UnifiedPlanObservationPhase phase,
        Phase existing) {
    for(const UnifiedPlanPhaseReference& candidate : observation.phases()) {
        if(candidate.observation_phase != phase ||
           !candidate.existing_route_phase.has_value()) {
            continue;
        }
        const auto* typed = std::get_if<Phase>(
                &candidate.existing_route_phase.value());
        if(typed != nullptr && *typed == existing) return true;
    }
    return false;
}

RootPackageSearchCandidate repository_candidate(
        const std::string& repository, const std::string& package_name) {
    RootPackageCandidateValidationResult result =
            make_repository_root_package_candidate(
                    repository, package_name, "1.0", "repository root");
    expect(result.is_valid(), "repository candidate fixture is invalid");
    return RootPackageSearchCandidate{*result.candidate(), {}};
}

RootPackageSearchCandidate aur_candidate(
        const std::string& package_name, const std::string& package_base) {
    RootPackageCandidateValidationResult result =
            make_aur_root_package_candidate(
                    package_name, package_base, "2.0", "AUR root");
    expect(result.is_valid(), "AUR candidate fixture is invalid");
    return RootPackageSearchCandidate{*result.candidate(), {}};
}

RootPackageSelection require_selection(
        RootPackageSelectionExpressionResult result) {
    auto* selection = std::get_if<RootPackageSelection>(&result);
    expect(selection != nullptr, "root selection fixture failed");
    return std::move(*selection);
}

BuildPlan build_plan_fixture(
        std::string root_name = "suite-child",
        std::string package_base = "suite-base") {
    BuildPlan plan;
    const RootTargetIdentity root{0, root_name};
    plan.root_targets.push_back(root);
    plan.order.push_back(BuildPlanEntry{package_base, {root_name}});
    plan.package_targets.push_back(PlannedPackageTarget{
            root_name, package_base, {PackageRole::Root}, {root}});

    const ConsumerDependencyRequirement requirement(
            "virtual-runtime>=2", "virtual-runtime",
            DependencyVersionConstraint{
                    DependencyVersionRelation::GreaterThanOrEqual, "2"});
    const ProvidedDependency provider =
            ProvidedDependency::from_repository_constraint_metadata(
                    "extra", 1, "runtime-provider",
                    ProviderConstraintMetadata{
                            ProviderCapability(
                                    "virtual-runtime=2", "virtual-runtime",
                                    "2"),
                            ObservedVersion::available(
                                    ObservedVersionSource::
                                            RepositoryExactPackage,
                                    "2.1"),
                            ObservedVersion::available(
                                    ObservedVersionSource::
                                            RepositoryProviderCapability,
                                    "2")});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            root_name,
            package_base,
            "virtual-runtime>=2",
            PackageRole::RuntimeDependency,
            DependencyKind::Provided,
            std::nullopt,
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
    plan.provided.push_back(BuildPlanProvidedDependency{
            "virtual-runtime", provider,
            ProviderResolutionKind::UserSelected});
    plan.configured_repository_order =
            std::vector<std::string>{"core", "extra", "multilib"};
    return plan;
}

ProductionSourceBuildWorkItem source_work_item(
        std::string package_base,
        std::string package_name,
        DesiredInstallReason reason = DesiredInstallReason::Explicit,
        bool is_build_plan_entry = false,
        SourceBuildSourceKind source_kind =
                SourceBuildSourceKind::Repository,
        bool needed = false) {
    ProductionSourceBuildWorkItem work;
    work.request.package_name = package_name;
    work.request.checkout_name = package_base;
    work.request.needed = needed;
    work.required_targets.push_back(RequiredPackageArtifactTarget{
            std::move(package_base), std::move(package_name), reason});
    work.is_build_plan_entry = is_build_plan_entry;
    work.uses_system_update_baseline =
            source_kind == SourceBuildSourceKind::Repository;
    return work;
}

PreparedRootPackageInstall make_root_prepared(
        bool include_repository,
        bool include_aur,
        std::optional<std::vector<std::string>> repository_order) {
    RootPackageSearchSnapshot discovery;
    discovery.repository_order = std::move(repository_order);
    if(include_repository) {
        discovery.candidates.push_back(
                repository_candidate("core", "repo-root"));
    }
    if(include_aur) {
        discovery.candidates.push_back(
                aur_candidate("suite-child", "suite-base"));
    }

    std::string expression;
    for(std::size_t index = 0; index < discovery.candidates.size(); ++index) {
        if(!expression.empty()) expression += ' ';
        expression += std::to_string(index + 1);
    }
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection(expression, discovery));
    const RootPackageRoutingProjectionResult routing =
            project_root_package_routing(selection);
    expect(routing.is_valid(), "root routing fixture is invalid");

    PreparedRootPackageInstall prepared;
    prepared.needed = true;
    prepared.discovery_snapshot.emplace(std::move(discovery));
    prepared.routing_projection.emplace(*routing.projection());
    for(const RepositoryRootPackageRouteTarget& target :
        prepared.routing_projection->repository_targets()) {
        prepared.exact_repository_targets.push_back(
                target.exact_package_target());
    }
    if(include_aur) {
        prepared.aur_build_plan.emplace(build_plan_fixture());
        prepared.source_invocation.emplace();
        prepared.source_invocation->work_items.push_back(
                source_work_item(
                        "suite-base", "suite-child",
                        DesiredInstallReason::Explicit, true,
                        SourceBuildSourceKind::Aur, prepared.needed));
        prepared.source_invocation->work_items.front()
                .configured_repository_order =
                prepared.aur_build_plan->configured_repository_order;
        prepared.source_invocation->work_items.front()
                .selected_repository_providers.push_back(
                        prepared.aur_build_plan->provided.front().provider);
    }
    return prepared;
}

AurUpdatePlanEntry update_entry(
        AurUpdateClassification classification =
                AurUpdateClassification::UpdateAvailable,
        InstalledPackageReason install_reason =
                InstalledPackageReason::Explicit) {
    const AurVersionRelation relation =
            classification == AurUpdateClassification::UpdateAvailable
            ? AurVersionRelation::NewerThanInstalled
            : AurVersionRelation::SameAsInstalled;
    return AurUpdatePlanEntry{
            "suite-child",
            "1.0",
            install_reason,
            AurUpdateRemotePackage{
                    "suite-child", "suite-base", "2.0", relation},
            classification};
}

AurUpdateQueryResult update_query(
        AurUpdateClassification classification =
                AurUpdateClassification::UpdateAvailable,
        InstalledPackageReason install_reason =
                InstalledPackageReason::Explicit) {
    AurUpdateQueryResult query;
    query.plan.entries.push_back(
            update_entry(classification, install_reason));
    return query;
}

AurUpdateExecutionPreflight executable_update_preflight(
        BuildPlan plan,
        InstalledPackageReason install_reason =
                InstalledPackageReason::Explicit) {
    AurUpdateExecutionTarget target;
    target.update_plan_index = 0;
    target.build_plan_root_index = 0;
    target.update = update_entry(
            AurUpdateClassification::UpdateAvailable, install_reason);
    target.status = AurUpdateExecutionTargetStatus::Executable;
    target.desired_install_reason =
            install_reason == InstalledPackageReason::Dependency
            ? DesiredInstallReason::Dependency
            : DesiredInstallReason::Explicit;
    return AurUpdateExecutionPreflight{{std::move(target)}, std::move(plan)};
}

AurUpdateExecutionPreflight no_op_update_preflight() {
    AurUpdateExecutionTarget target;
    target.update_plan_index = 0;
    target.update = update_entry(AurUpdateClassification::UpToDate);
    target.status = AurUpdateExecutionTargetStatus::Skipped;
    return AurUpdateExecutionPreflight{{std::move(target)}, std::nullopt};
}

RegisteredSourcePreferenceSnapshot registered_source(
        std::size_t index,
        std::string package_name,
        std::string package_base,
        SourceBuildSourceKind kind) {
    const std::string key =
            (kind == SourceBuildSourceKind::Aur ? "aur:" : "repo:") +
            package_base;
    return RegisteredSourcePreferenceSnapshot{
            index,
            std::move(package_name),
            "/tmp/source-preference",
            SourceBuildEnvironment{},
            key,
            std::move(package_base),
            {},
            kind};
}

RegisteredSourceUpgradeResult not_attempted_source_result(
        const RegisteredSourcePreferenceSnapshot& source) {
    RegisteredSourceUpgradeResult result;
    result.original_preference_index = source.original_preference_index;
    result.preference_package_name = source.preference_package_name;
    result.canonical_source_identity_key =
            source.canonical_source_identity_key;
    result.resolved_package_base = source.resolved_package_base;
    result.status = RegisteredSourceUpgradeStatus::NotAttempted;
    return result;
}

SystemSourceUpgradeIssue system_issue(
        SystemSourceUpgradeIssueKind kind,
        SystemSourceUpgradeIssueImpact impact,
        SystemSourceUpgradePhase phase) {
    SystemSourceUpgradeIssue issue;
    issue.kind = kind;
    issue.impact = impact;
    issue.phase = phase;
    return issue;
}

void test_projection_lifetime_and_root_authorities() {
    const PreparedRootPackageInstall prepared = make_root_prepared(
            true, true,
            std::vector<std::string>{"core", "extra", "multilib"});
    const std::unique_ptr<UnifiedPlanProjection> projection =
            project_root_package_unified_plan(
                    RootPackageUnifiedPlanProjectionInput{
                            std::cref(prepared)});
    const UnifiedPlanObservation& observation =
            require_observation(*projection, "root");
    expect(
            observation.status() == UnifiedPlanObservationStatus::Ready,
            "root projection was not Ready");
    expect(observation.roots().size() == 2, "root identities were lost");
    expect(
            std::holds_alternative<RepositoryRootPackageIdentity>(
                    observation.roots()[0].source_identity()) &&
                    std::get<AurRootPackageIdentity>(
                            observation.roots()[1].source_identity()) ==
                            AurRootPackageIdentity{
                                    "suite-child", "suite-base"},
            "repository/AUR root identity was flattened");
    expect(
            observation.dependency_authorities().front().build_plan() ==
                    &prepared.aur_build_plan.value(),
            "BuildPlan was copied instead of borrowed");
    expect(
            observation.configured_repository_order() != nullptr &&
                    &observation.configured_repository_order()
                             ->configured_order() ==
                            &prepared.discovery_snapshot->repository_order
                                     .value() &&
                    observation.configured_repository_order()
                                    ->configured_order() ==
                            std::vector<std::string>{
                                    "core", "extra", "multilib"},
            "configured repository order was reordered or copied");
    expect(
            observation.route_preflight_authorities().size() == 1 &&
                    &std::get<UnifiedPlanBorrowedAuthorityReference<
                            RootPackageRoutingProjection>>(
                             observation.route_preflight_authorities().front())
                             .get() ==
                            &prepared.routing_projection.value(),
            "root route authority was copied or omitted");

    const RequiredPackageArtifactTarget artifact =
            observation.required_artifacts().front().target();
    expect(
            artifact.package_base == "suite-base" &&
                    artifact.package_name == "suite-child" &&
                    artifact.desired_reason ==
                            DesiredInstallReason::Explicit,
            "bundle-owned artifact target identity was not retained");

    bool selected_provider_borrowed = false;
    for(const UnifiedPlanTransactionIntent& transaction :
        observation.transaction_intents()) {
        const auto* repository =
                std::get_if<RepositoryPackageTransactionIntent>(&transaction);
        if(repository == nullptr) continue;
        for(const RepositoryInstallIntentTarget& target :
            repository->targets) {
            const auto* provider =
                    std::get_if<RepositoryProviderInstallIntent>(&target);
            if(provider != nullptr &&
               &provider->provider.get() ==
                       &prepared.aur_build_plan->provided.front().provider) {
                selected_provider_borrowed = true;
            }
        }
    }
    expect(
            selected_provider_borrowed,
            "typed selected provider identity was reconstructed or omitted");

    // observation_result() exposes only a const borrow. The compile-time
    // assertions above make both result and observation copy escape invalid;
    // the projection therefore remains the sole owner of this internal target.
}

void test_root_repository_order_known_and_unknown() {
    const PreparedRootPackageInstall repository_only = make_root_prepared(
            true, false, std::vector<std::string>{"core", "extra"});
    const std::unique_ptr<UnifiedPlanProjection> repository_projection =
            project_root_package_unified_plan(
                    RootPackageUnifiedPlanProjectionInput{
                            std::cref(repository_only)});
    const UnifiedPlanObservation& repository_observation =
            require_observation(
                    *repository_projection, "repository-only root");
    expect(
            repository_observation.configured_repository_order() != nullptr &&
                    repository_observation.configured_repository_order()
                                    ->configured_order() ==
                            std::vector<std::string>{"core", "extra"},
            "queried repository order was not retained");
    expect(
            has_phase(
                    repository_observation,
                    UnifiedPlanObservationPhase::MetadataDiscovery,
                    UnifiedPlanAuthorityOwner::Libalpm) &&
                    has_phase(
                            repository_observation,
                            UnifiedPlanObservationPhase::
                                    RepositoryTransaction) &&
                    !has_phase(
                            repository_observation,
                            UnifiedPlanObservationPhase::SourceRetrieval) &&
                    !has_phase(
                            repository_observation,
                            UnifiedPlanObservationPhase::SourceBuild),
            "repository-only route fabricated source phases");

    const PreparedRootPackageInstall aur_only =
            make_root_prepared(false, true, std::nullopt);
    const std::unique_ptr<UnifiedPlanProjection> aur_projection =
            project_root_package_unified_plan(
                    RootPackageUnifiedPlanProjectionInput{
                            std::cref(aur_only)});
    const UnifiedPlanObservation& aur_observation =
            require_observation(*aur_projection, "AUR-only root");
    expect(
            aur_observation.configured_repository_order() == nullptr,
            "unqueried repository authority was flattened to known empty");
    expect(
            has_phase(
                    aur_observation,
                    UnifiedPlanObservationPhase::MetadataDiscovery,
                    UnifiedPlanAuthorityOwner::AurRpc) &&
                    has_phase(
                            aur_observation,
                            UnifiedPlanObservationPhase::SourceRetrieval) &&
                    has_phase(
                            aur_observation,
                            UnifiedPlanObservationPhase::SourceBuild),
            "AUR route lost its actual source phases");
}

void test_root_and_update_correlation_fail_closed() {
    PreparedRootPackageInstall missing_plan =
            make_root_prepared(false, true, std::nullopt);
    missing_plan.aur_build_plan.reset();
    expect_invalid_argument(
            [&missing_plan] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(missing_plan)});
            },
            "AUR root without BuildPlan");

    PreparedRootPackageInstall mismatched_plan =
            make_root_prepared(false, true, std::nullopt);
    mismatched_plan.aur_build_plan->root_targets.front().requested_name =
            "other-invocation";
    expect_invalid_argument(
            [&mismatched_plan] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(mismatched_plan)});
            },
            "mismatched root invocation");

    PreparedRootPackageInstall mismatched_source_work =
            make_root_prepared(false, true, std::nullopt);
    mismatched_source_work.source_invocation->work_items.front()
            .required_targets.front()
            .package_name = "other-work-item";
    expect_invalid_argument(
            [&mismatched_source_work] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(mismatched_source_work)});
            },
            "mismatched root source work");

    PreparedRootPackageInstall mismatched_needed =
            make_root_prepared(false, true, std::nullopt);
    mismatched_needed.source_invocation->work_items.front().request.needed =
            false;
    expect_invalid_argument(
            [&mismatched_needed] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(mismatched_needed)});
            },
            "root source work with a different needed policy");

    AurUpdateQueryResult query = update_query();
    AurUpdateExecutionPreflight mismatched_preflight =
            executable_update_preflight(build_plan_fixture());
    mismatched_preflight.targets.front().update.installed_version = "0.9";
    expect_invalid_argument(
            [&query, &mismatched_preflight] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query),
                                std::cref(mismatched_preflight),
                                false});
            },
            "mismatched AUR query/preflight");

    AurUpdateExecutionPreflight missing_preflight_plan;
    AurUpdateExecutionTarget executable;
    executable.update_plan_index = 0;
    executable.build_plan_root_index = 0;
    executable.update = update_entry();
    executable.status = AurUpdateExecutionTargetStatus::Executable;
    missing_preflight_plan.targets.push_back(std::move(executable));
    expect_invalid_argument(
            [&query, &missing_preflight_plan] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query),
                                std::cref(missing_preflight_plan),
                                false});
            },
            "executable AUR update without BuildPlan");
}

void test_aur_update_install_reason_parity() {
    const AurUpdateQueryResult query = update_query(
            AurUpdateClassification::UpdateAvailable,
            InstalledPackageReason::Dependency);
    const AurUpdateExecutionPreflight preflight =
            executable_update_preflight(
                    build_plan_fixture(),
                    InstalledPackageReason::Dependency);
    const std::unique_ptr<UnifiedPlanProjection> standalone_projection =
            project_aur_update_unified_plan(
                    AurUpdateUnifiedPlanProjectionInput{
                            std::cref(query), std::cref(preflight), false});
    const UnifiedPlanObservation& standalone = require_observation(
            *standalone_projection, "dependency-installed AUR update");
    expect(
            standalone.status() == UnifiedPlanObservationStatus::Ready &&
                    standalone.required_artifacts().size() == 1 &&
                    standalone.required_artifacts().front()
                                    .target()
                                    .desired_reason ==
                            DesiredInstallReason::Dependency,
            "standalone AUR update promoted dependency install reason");
    const auto* standalone_install = std::get_if<
            SourceBuiltArtifactInstallBoundaryIntent>(
            &standalone.transaction_intents().back());
    expect(
            standalone_install != nullptr &&
                    standalone_install->targets.size() == 1 &&
                    std::holds_alternative<
                            SourceDependencyArtifactInstallIntent>(
                            standalone_install->targets.front()),
            "standalone AUR update promoted dependency install intent");

    SystemSourceUpgradePreparedSnapshot system_snapshot;
    const std::vector<SystemSourceUpgradeIssue> system_issues;
    const std::vector<ProductionSourceBuildWorkItem> system_work;
    const SystemSourceUpgradeProjectionAuthority system_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    system_snapshot, nullptr, system_issues, system_work);
    const UpgradeAllOperationPreparedSnapshot aggregate_snapshot{
            system_snapshot, {}, {}};
    const UpgradeAllOperationProjectionAuthority aggregate_authority =
            UnifiedPlanProjectionTestAccess::make_upgrade_all(
                    aggregate_snapshot, system_authority);
    const std::vector<UpgradeAllOperationIssue> aggregate_issues;
    const std::unique_ptr<UnifiedPlanProjection> upgrade_projection =
            project_upgrade_all_unified_plan(
                    UpgradeAllUnifiedPlanProjectionInput{
                            std::cref(aggregate_authority),
                            std::cref(query), std::cref(preflight),
                            std::cref(aggregate_issues)});
    const UnifiedPlanObservation& upgrade = require_observation(
            *upgrade_projection,
            "dependency-installed upgrade-all AUR update");
    expect(
            upgrade.status() == UnifiedPlanObservationStatus::Ready &&
                    upgrade.required_artifacts().size() == 1 &&
                    upgrade.required_artifacts().front()
                                    .target()
                                    .desired_reason ==
                            DesiredInstallReason::Dependency,
            "upgrade-all AUR update promoted dependency install reason");
    const auto* upgrade_install =
            std::get_if<SourceBuiltArtifactInstallBoundaryIntent>(
                    &upgrade.transaction_intents().back());
    expect(
            upgrade_install != nullptr &&
                    upgrade_install->targets.size() == 1 &&
                    std::holds_alternative<
                            SourceDependencyArtifactInstallIntent>(
                            upgrade_install->targets.front()),
            "upgrade-all AUR update promoted dependency install intent");
}

void test_build_plan_partial_failure_remains_typed() {
    PreparedRootPackageInstall prepared =
            make_root_prepared(false, true, std::nullopt);
    BuildPlan& plan = prepared.aur_build_plan.value();
    plan.incomplete_provider_candidate_sets.push_back(
            IncompleteProviderCandidateSet{
                    "partial-provider",
                    {ProvidedDependency::from_repository(
                            "core", 0, "observed-provider")},
                    ObservedVersionUnknownReason::PartialSourceFailure});
    plan.dependency_edges.front().constraint_evaluation =
            ConstraintEvaluation::unsatisfied();
    const std::string shared_dependency =
            plan.dependency_edges.front().dependency_spec;
    const std::optional<DependencyRequirement> shared_requirement =
            plan.dependency_edges.front().requirement;
    const ProvidedDependency similar_provider =
            ProvidedDependency::from_repository_constraint_metadata(
                    "core", 0, "similar-runtime-provider",
                    ProviderConstraintMetadata{
                            ProviderCapability(
                                    "virtual-runtime=1", "virtual-runtime",
                                    "1"),
                            ObservedVersion::available(
                                    ObservedVersionSource::
                                            RepositoryExactPackage,
                                    "1.1"),
                            ObservedVersion::available(
                                    ObservedVersionSource::
                                            RepositoryProviderCapability,
                                    "1")});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            plan.root_targets.front().requested_name,
            plan.order.front().package_base,
            shared_dependency,
            PackageRole::BuildDependency,
            DependencyKind::Provided,
            "similar-runtime-provider",
            std::nullopt,
            similar_provider,
            ProviderResolutionKind::Unique,
            shared_requirement,
            ResolvedDependencyCandidate{ProviderResolvedDependencyCandidate{
                    similar_provider,
                    ObservedVersion::available(
                            ObservedVersionSource::
                                    RepositoryProviderCapability,
                            "1")}},
            ConstraintEvaluation::unknown(
                    ObservedVersionUnknownReason::
                            CandidateVersionUnavailable)});

    const std::unique_ptr<UnifiedPlanProjection> projection =
            project_root_package_unified_plan(
                    RootPackageUnifiedPlanProjectionInput{
                            std::cref(prepared)});
    const UnifiedPlanObservation& observation =
            require_observation(*projection, "partial BuildPlan failure");
    expect(
            observation.status() == UnifiedPlanObservationStatus::Blocked &&
                    observation.transaction_intents().empty(),
            "partial BuildPlan failure exposed mutation intent");
    bool partial_failure_borrowed = false;
    std::vector<const BuildPlanDependencyEdge*> constraint_failure_edges;
    for(const UnifiedPlanBlocker& blocker : observation.blockers()) {
        if(const auto* constraint =
                   std::get_if<ConstraintFailureUnifiedPlanBlocker>(
                           &blocker);
           constraint != nullptr) {
            constraint_failure_edges.push_back(&constraint->detail.get());
        }
        const auto* source =
                std::get_if<SourceFailureUnifiedPlanBlocker>(&blocker);
        if(source == nullptr) continue;
        const auto* partial = std::get_if<
                UnifiedPlanBorrowedAuthorityReference<
                        IncompleteProviderCandidateSet>>(&source->detail);
        if(partial != nullptr &&
           &partial->get() ==
                   &plan.incomplete_provider_candidate_sets.front() &&
           partial->get().reason ==
                   ObservedVersionUnknownReason::PartialSourceFailure) {
            partial_failure_borrowed = true;
        }
    }
    expect(
            partial_failure_borrowed &&
                    constraint_failure_edges.size() == 2 &&
                    constraint_failure_edges[0] ==
                            &plan.dependency_edges[0] &&
                    constraint_failure_edges[1] ==
                            &plan.dependency_edges[1] &&
                    constraint_failure_edges[0]->dependency_spec ==
                            constraint_failure_edges[1]->dependency_spec &&
                    constraint_failure_edges[0]
                            ->resolved_provider.has_value() &&
                    constraint_failure_edges[0]
                                    ->resolved_provider->package_name ==
                            "runtime-provider" &&
                    constraint_failure_edges[0]
                            ->constraint_evaluation.has_value() &&
                    &constraint_failure_edges[0]
                             ->constraint_evaluation.value() ==
                            &plan.dependency_edges[0]
                                     .constraint_evaluation.value() &&
                    constraint_failure_edges[0]
                                    ->constraint_evaluation->satisfaction() ==
                            ConstraintSatisfaction::Unsatisfied &&
                    constraint_failure_edges[1]
                            ->resolved_provider.has_value() &&
                    constraint_failure_edges[1]
                                    ->resolved_provider->package_name ==
                            "similar-runtime-provider" &&
                    constraint_failure_edges[1]
                            ->constraint_evaluation.has_value() &&
                    &constraint_failure_edges[1]
                             ->constraint_evaluation.value() ==
                            &plan.dependency_edges[1]
                                     .constraint_evaluation.value() &&
                    constraint_failure_edges[1]
                                    ->constraint_evaluation->unknown_reason() !=
                            nullptr &&
                    *constraint_failure_edges[1]
                             ->constraint_evaluation->unknown_reason() ==
                            ObservedVersionUnknownReason::
                                    CandidateVersionUnavailable,
            "typed partial-source/constraint authority was flattened");
}

void test_local_and_no_op_phases() {
    const LocalSourceRoot source_root = open_local_source_root(
            "tests/fixtures/pkgbuild-export");
    const LocalPackageMetadataParseResult* parse_result =
            source_root.metadata().parse_result();
    expect(
            parse_result != nullptr && parse_result->is_success(),
            "local metadata fixture is unavailable");
    const LocalBuildPlan local_plan = resolve_local_build_plan(
            *parse_result->metadata(), "x86_64");
    const SourceBuildEnvironment source_environment;
    const LocalSourceBuildProjectionAuthority local_authority =
            UnifiedPlanProjectionTestAccess::make_local_source(
                    source_root, local_plan, source_environment);
    const std::unique_ptr<UnifiedPlanProjection> local_projection =
            project_local_source_unified_plan(
                    LocalSourceUnifiedPlanProjectionInput{
                            std::cref(local_authority), false});
    const UnifiedPlanObservation& local_observation =
            require_observation(*local_projection, "local source");
    expect(
            local_observation.status() == UnifiedPlanObservationStatus::Ready &&
                    local_observation.dependency_authorities()
                                    .front()
                                    .local_build_plan() == &local_plan,
            "LocalBuildPlan was copied or omitted");
    expect(
            local_observation.route_preflight_authorities().size() == 1 &&
                    &std::get<UnifiedPlanBorrowedAuthorityReference<
                            LocalSourceBuildProjectionAuthority>>(
                             local_observation
                                     .route_preflight_authorities()
                                     .front())
                             .get() == &local_authority &&
                    &local_authority.accepted_metadata() ==
                            &std::get<UnifiedPlanBorrowedAuthorityReference<
                                    LocalPackageMetadata>>(
                                     local_observation.root_metadata()
                                             .front())
                                     .get(),
            "local prepared invocation authority was copied or omitted");
    expect(
            has_phase(
                    local_observation,
                    UnifiedPlanObservationPhase::MetadataDiscovery,
                    UnifiedPlanAuthorityOwner::Makepkg) &&
                    !has_phase(
                            local_observation,
                            UnifiedPlanObservationPhase::MetadataDiscovery,
                            UnifiedPlanAuthorityOwner::AurRpc),
            "local route fabricated AUR metadata discovery");

    const AurUpdateQueryResult query = update_query(
            AurUpdateClassification::UpToDate);
    const AurUpdateExecutionPreflight preflight = no_op_update_preflight();
    const std::unique_ptr<UnifiedPlanProjection> no_op_projection =
            project_aur_update_unified_plan(
                    AurUpdateUnifiedPlanProjectionInput{
                            std::cref(query), std::cref(preflight),
                            false});
    const UnifiedPlanObservation& no_op =
            require_observation(*no_op_projection, "AUR NoOp");
    expect(
            no_op.status() == UnifiedPlanObservationStatus::NoOp &&
                    no_op.transaction_intents().empty(),
            "NoOp AUR update exposed mutation intent");
    expect(
            !has_phase(
                    no_op,
                    UnifiedPlanObservationPhase::RepositoryTransaction) &&
                    !has_phase(
                            no_op,
                            UnifiedPlanObservationPhase::SourceBuild) &&
                    !has_phase(
                            no_op,
                            UnifiedPlanObservationPhase::
                                    SourceArtifactInstall),
            "NoOp route fabricated mutation phases");
}

void test_actual_prepared_source_work_is_artifact_authority() {
    SystemSourceUpgradePreparedSnapshot snapshot;
    snapshot.preference_root_exists = true;
    snapshot.registered_sources.push_back(registered_source(
            3, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    snapshot.registered_sources.push_back(registered_source(
            7, "suite-child", "suite-base", SourceBuildSourceKind::Aur));

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(source_work_item("repo-base", "repo-child"));
    work_items.push_back(source_work_item(
            "suite-base", "suite-child",
            DesiredInstallReason::Explicit, false,
            SourceBuildSourceKind::Aur));
    BuildPlan aur_plan = build_plan_fixture();
    aur_plan.order.insert(
            aur_plan.order.begin(),
            BuildPlanEntry{"dependency-base", {"dependency-child"}});
    work_items[1].configured_repository_order =
            aur_plan.configured_repository_order;
    work_items[1].selected_repository_providers.push_back(
            aur_plan.provided.front().provider);
    const std::vector<SystemSourceUpgradeIssue> issues;
    const SystemSourceUpgradeProjectionAuthority authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    snapshot, &aur_plan, issues, work_items);

    const std::unique_ptr<UnifiedPlanProjection> projection =
            project_system_source_upgrade_unified_plan(
                    SystemSourceUpgradeUnifiedPlanProjectionInput{
                            std::cref(authority)});
    const UnifiedPlanObservation& observation =
            require_observation(*projection, "prepared source work");
    expect(
            observation.status() == UnifiedPlanObservationStatus::Ready,
            "prepared system/source projection was not Ready");
    expect(
            observation.required_artifacts().size() == 2 &&
                    &std::get<PreparedSystemSourceBuildUnitReference>(
                             observation.required_artifacts()[0].build_unit())
                             .required_targets() ==
                            &work_items[0].required_targets &&
                    &std::get<PreparedSystemSourceBuildUnitReference>(
                             observation.required_artifacts()[1].build_unit())
                             .required_targets() ==
                            &work_items[1].required_targets,
            "actual prepared required_targets authority was replaced");
    const bool projected_dependency_unit = std::any_of(
            observation.required_artifacts().begin(),
            observation.required_artifacts().end(),
            [](const RequiredArtifactTargetReference& artifact) {
                return artifact.target().package_name == "dependency-child";
            });
    expect(
            !projected_dependency_unit,
            "a non-work BuildPlan unit became source install intent");
    expect(
            std::holds_alternative<RepositorySourceBuildRootIdentity>(
                    observation.roots()[0].source_identity()) &&
                    std::holds_alternative<AurRootPackageIdentity>(
                            observation.roots()[1].source_identity()),
            "registered repository/AUR source identity was flattened");
    expect(
            has_existing_phase(
                    observation,
                    UnifiedPlanObservationPhase::RepositoryTransaction,
                    SystemSourceUpgradePhase::System) &&
                    has_existing_phase(
                            observation,
                            UnifiedPlanObservationPhase::SourceBuild,
                            SystemSourceUpgradePhase::RegisteredSource),
            "system/source typed route phases were not projected");
}

void test_system_issue_impact_and_blocked_phases() {
    SystemSourceUpgradePreparedSnapshot snapshot;
    snapshot.registered_sources.push_back(registered_source(
            0, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    const std::vector<ProductionSourceBuildWorkItem> work_items{
            source_work_item("repo-base", "repo-child")};
    const std::vector<SystemSourceUpgradeIssue> non_blocking_issues{
            system_issue(
                    SystemSourceUpgradeIssueKind::
                            SystemPackageSnapshotUnavailable,
                    SystemSourceUpgradeIssueImpact::ObservabilityOnly,
                    SystemSourceUpgradePhase::System),
            system_issue(
                    SystemSourceUpgradeIssueKind::
                            PostSystemSourceSnapshotUnavailable,
                    SystemSourceUpgradeIssueImpact::AffectsSuccess,
                    SystemSourceUpgradePhase::RegisteredSource)};
    const SystemSourceUpgradeProjectionAuthority non_blocking_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    snapshot, nullptr, non_blocking_issues, work_items);
    const std::unique_ptr<UnifiedPlanProjection> ready_projection =
            project_system_source_upgrade_unified_plan(
                    SystemSourceUpgradeUnifiedPlanProjectionInput{
                            std::cref(non_blocking_authority)});
    const UnifiedPlanObservation& ready =
            require_observation(*ready_projection, "non-blocking issues");
    expect(
            ready.status() == UnifiedPlanObservationStatus::Ready &&
                    ready.blockers().empty() &&
                    non_blocking_authority.issues().size() == 2,
            "non-blocking typed issues were promoted to blockers");
    expect(
            !has_phase(
                    ready, UnifiedPlanObservationPhase::MetadataDiscovery,
                    UnifiedPlanAuthorityOwner::AurRpc),
            "repository registered source fabricated an AUR phase");

    const std::vector<SystemSourceUpgradeIssue> blocking_issues{
            system_issue(
                    SystemSourceUpgradeIssueKind::CacheAuthorityInvalid,
                    SystemSourceUpgradeIssueImpact::BlocksExecution,
                    SystemSourceUpgradePhase::Preparation)};
    SystemSourceUpgradeResult blocking_result;
    blocking_result.status =
            SystemSourceUpgradeStatus::BlockedBeforeMutation;
    blocking_result.stopped_phase = SystemSourceUpgradePhase::Preparation;
    blocking_result.prepared_snapshot = snapshot;
    blocking_result.registered_source_results.push_back(
            not_attempted_source_result(
                    blocking_result.prepared_snapshot.registered_sources
                            .front()));
    blocking_result.issues = blocking_issues;
    const std::unique_ptr<UnifiedPlanProjection> blocked_projection =
            project_system_source_upgrade_unified_plan(
                    SystemSourceUpgradeUnifiedPlanProjectionInput{
                            std::cref(blocking_result)});
    const UnifiedPlanObservation& blocked =
            require_observation(*blocked_projection, "blocking issue");
    expect(
            blocked.status() == UnifiedPlanObservationStatus::Blocked &&
                    blocked.blockers().size() == 1 &&
                    blocked.transaction_intents().empty() &&
                    blocked.build_units().empty() &&
                    blocked.required_artifacts().empty(),
            "BlocksExecution did not produce a mutation-free Blocked view");
    expect(
            !has_phase(
                    blocked,
                    UnifiedPlanObservationPhase::RepositoryTransaction) &&
                    !has_phase(
                            blocked,
                            UnifiedPlanObservationPhase::SourceBuild),
            "Blocked route fabricated mutation phases");
}

void test_partial_failures_and_upgrade_all_authorities() {
    SystemSourceUpgradePreparedSnapshot system_snapshot;
    system_snapshot.registered_sources.push_back(registered_source(
            0, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    const std::vector<ProductionSourceBuildWorkItem> work_items{
            source_work_item("repo-base", "repo-child")};
    const std::vector<SystemSourceUpgradeIssue> system_issues;
    const SystemSourceUpgradeProjectionAuthority system_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    system_snapshot, nullptr, system_issues, work_items);
    const UpgradeAllOperationPreparedSnapshot aggregate_snapshot{
            system_snapshot, {}, {}};
    const UpgradeAllOperationProjectionAuthority aggregate_authority =
            UnifiedPlanProjectionTestAccess::make_upgrade_all(
                    aggregate_snapshot, system_authority);

    AurUpdateQueryResult query = update_query();
    const AurUpdateExecutionPreflight preflight =
            executable_update_preflight(build_plan_fixture());
    const std::vector<UpgradeAllOperationIssue> aggregate_issues;
    {
        const std::unique_ptr<UnifiedPlanProjection> ready_projection =
                project_upgrade_all_unified_plan(
                        UpgradeAllUnifiedPlanProjectionInput{
                                std::cref(aggregate_authority),
                                std::cref(query), std::cref(preflight),
                                std::cref(aggregate_issues)});
        const UnifiedPlanObservation& ready = require_observation(
                *ready_projection, "upgrade-all ready routes");
        expect(
                ready.status() == UnifiedPlanObservationStatus::Ready &&
                        ready.required_artifacts().size() == 2 &&
                        &std::get<
                                 PreparedSystemSourceBuildUnitReference>(
                                 ready.required_artifacts()
                                         .front()
                                         .build_unit())
                                 .required_targets() ==
                                &work_items.front().required_targets,
                "upgrade-all did not combine actual registered work with AUR intent");
        expect(
                has_existing_phase(
                        ready, UnifiedPlanObservationPhase::SourceBuild,
                        UpgradeAllOperationPhase::RegisteredSource) &&
                        has_existing_phase(
                                ready,
                                UnifiedPlanObservationPhase::SourceBuild,
                                UpgradeAllOperationPhase::AurExecution),
                "upgrade-all source routes lost typed phase identity");
    }

    query.recoverable_failures.push_back(AurUpdateQueryFailure{
            {"failed-source"}, "fixture partial query failure"});
    const std::unique_ptr<UnifiedPlanProjection> projection =
            project_upgrade_all_unified_plan(
                    UpgradeAllUnifiedPlanProjectionInput{
                            std::cref(aggregate_authority),
                            std::cref(query), std::cref(preflight),
                            std::cref(aggregate_issues)});
    const UnifiedPlanObservation& observation =
            require_observation(*projection, "upgrade-all partial failure");
    expect(
            observation.status() == UnifiedPlanObservationStatus::Blocked &&
                    observation.transaction_intents().empty(),
            "enabled-source partial failure was flattened to Ready");
    bool query_failure_borrowed = false;
    for(const UnifiedPlanBlocker& blocker : observation.blockers()) {
        const auto* source =
                std::get_if<SourceFailureUnifiedPlanBlocker>(&blocker);
        if(source == nullptr) continue;
        const auto* failure = std::get_if<
                UnifiedPlanBorrowedAuthorityReference<
                        AurUpdateQueryFailure>>(
                &source->detail);
        if(failure != nullptr &&
           &failure->get() == &query.recoverable_failures.front()) {
            query_failure_borrowed = true;
        }
    }
    expect(
            query_failure_borrowed,
            "typed AUR query failure was copied or omitted");
    expect(
            observation.required_artifacts().size() == 2 &&
                    &std::get<PreparedSystemSourceBuildUnitReference>(
                             observation.required_artifacts()
                                     .front()
                                     .build_unit())
                             .required_targets() ==
                            &work_items.front().required_targets,
            "upgrade-all lost actual registered-source work authority");
    expect(
            !has_phase(
                    observation, UnifiedPlanObservationPhase::SourceBuild),
            "blocked upgrade-all fabricated source mutation phases");
    expect(
            observation.route_preflight_authorities().size() == 2 &&
                    &std::get<UnifiedPlanBorrowedAuthorityReference<
                            UpgradeAllOperationProjectionAuthority>>(
                             observation.route_preflight_authorities()[0])
                             .get() == &aggregate_authority,
            "upgrade-all prepared authority was copied or omitted");
}

void test_full_identity_correlation_fail_closed() {
    const LocalSourceRoot local_source_a = open_local_source_root(
            "tests/fixtures/pkgbuild-export");
    const LocalSourceRoot local_source_b = open_local_source_root(
            "tests/fixtures/pkgbuild-export");
    const LocalPackageMetadata* local_metadata_b =
            local_source_b.metadata().parse_result()->metadata();
    const LocalBuildPlan local_plan_b = resolve_local_build_plan(
            *local_metadata_b, "x86_64");
    expect(
            *local_source_a.metadata().parse_result()->metadata() ==
                    local_plan_b.local_metadata(),
            "local mixed-invocation fixture metadata values differ");
    expect_invalid_argument(
            [&local_source_a, &local_plan_b] {
                (void)project_local_source_unified_plan(
                        LocalSourceUnifiedPlanProjectionInput{
                                LocalSourceBuildPlanFailureProjectionInput{
                                        std::cref(local_source_a),
                                        std::cref(local_plan_b)},
                                false});
            },
            "same-metadata local source/plan from independent invocations");

    PreparedRootPackageInstall different_package_base =
            make_root_prepared(false, true, std::nullopt);
    different_package_base.aur_build_plan->package_targets.front()
            .package_base = "other-package-base";
    expect_invalid_argument(
            [&different_package_base] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(different_package_base)});
            },
            "same root name with different PackageBase");

    AurUpdateQueryResult query = update_query();
    AurUpdateExecutionPreflight same_index_different_base =
            executable_update_preflight(build_plan_fixture());
    same_index_different_base.build_plan->package_targets.front()
            .package_base = "different-update-base";
    expect_invalid_argument(
            [&query, &same_index_different_base] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query),
                                std::cref(same_index_different_base),
                                false});
            },
            "same invocation index with different PackageBase");

    PreparedRootPackageInstall same_child_different_base =
            make_root_prepared(false, true, std::nullopt);
    same_child_different_base.aur_build_plan->order.front().package_base =
            "different-order-base";
    expect_invalid_argument(
            [&same_child_different_base] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(same_child_different_base)});
            },
            "same child name with different BuildPlan PackageBase");

    PreparedRootPackageInstall mismatched_actual_work =
            make_root_prepared(false, true, std::nullopt);
    mismatched_actual_work.source_invocation->work_items.front()
            .request.checkout_name = "different-work-base";
    expect_invalid_argument(
            [&mismatched_actual_work] {
                (void)project_root_package_unified_plan(
                        RootPackageUnifiedPlanProjectionInput{
                                std::cref(mismatched_actual_work)});
            },
            "actual work item with different plan identity");

    SystemSourceUpgradePreparedSnapshot source_kind_snapshot;
    source_kind_snapshot.registered_sources.push_back(registered_source(
            0, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    std::vector<ProductionSourceBuildWorkItem> source_kind_work{
            source_work_item("repo-base", "repo-child")};
    source_kind_work.front().uses_system_update_baseline = false;
    const std::vector<SystemSourceUpgradeIssue> no_issues;
    const SystemSourceUpgradeProjectionAuthority source_kind_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    source_kind_snapshot, nullptr, no_issues,
                    source_kind_work);
    expect_invalid_argument(
            [&source_kind_authority] {
                (void)project_system_source_upgrade_unified_plan(
                        SystemSourceUpgradeUnifiedPlanProjectionInput{
                                std::cref(source_kind_authority)});
            },
            "registered source kind mismatch");

    SystemSourceUpgradePreparedSnapshot artifact_identity_snapshot;
    artifact_identity_snapshot.registered_sources.push_back(
            registered_source(
                    0, "suite-child", "suite-base",
                    SourceBuildSourceKind::Aur));
    BuildPlan artifact_identity_plan = build_plan_fixture();
    std::vector<ProductionSourceBuildWorkItem> artifact_identity_work{
            source_work_item(
                    "suite-base", "suite-child",
                    DesiredInstallReason::Dependency, false,
                    SourceBuildSourceKind::Aur)};
    artifact_identity_work.front().configured_repository_order =
            artifact_identity_plan.configured_repository_order;
    artifact_identity_work.front().selected_repository_providers.push_back(
            artifact_identity_plan.provided.front().provider);
    const SystemSourceUpgradeProjectionAuthority artifact_identity_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    artifact_identity_snapshot, &artifact_identity_plan,
                    no_issues, artifact_identity_work);
    expect_invalid_argument(
            [&artifact_identity_authority] {
                (void)project_system_source_upgrade_unified_plan(
                        SystemSourceUpgradeUnifiedPlanProjectionInput{
                                std::cref(artifact_identity_authority)});
            },
            "actual required artifact target with different root identity");

    SystemSourceUpgradePreparedSnapshot nested_snapshot;
    nested_snapshot.registered_sources.push_back(registered_source(
            0, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    const std::vector<ProductionSourceBuildWorkItem> nested_work{
            source_work_item("repo-base", "repo-child")};
    const SystemSourceUpgradeProjectionAuthority nested_authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    nested_snapshot, nullptr, no_issues, nested_work);
    UpgradeAllOperationPreparedSnapshot mixed_outer{
            nested_snapshot, {}, {}};
    mixed_outer.system_source.registered_sources.front()
            .resolved_package_base = "other-invocation-base";
    const UpgradeAllOperationProjectionAuthority mixed_authority =
            UnifiedPlanProjectionTestAccess::make_upgrade_all(
                    mixed_outer, nested_authority);
    const AurUpdateExecutionPreflight update_preflight =
            executable_update_preflight(build_plan_fixture());
    const std::vector<UpgradeAllOperationIssue> upgrade_issues;
    expect_invalid_argument(
            [&mixed_authority, &query, &update_preflight, &upgrade_issues] {
                (void)project_upgrade_all_unified_plan(
                        UpgradeAllUnifiedPlanProjectionInput{
                                std::cref(mixed_authority),
                                std::cref(query),
                                std::cref(update_preflight),
                                std::cref(upgrade_issues)});
            },
            "upgrade-all nested different invocation");

    UpgradeAllOperationPreparedSnapshot mixed_options{
            nested_snapshot, {}, {}};
    mixed_options.system_source.options.no_confirm = true;
    const UpgradeAllOperationProjectionAuthority mixed_options_authority =
            UnifiedPlanProjectionTestAccess::make_upgrade_all(
                    mixed_options, nested_authority);
    expect_invalid_argument(
            [&mixed_options_authority, &query, &update_preflight,
             &upgrade_issues] {
                (void)project_upgrade_all_unified_plan(
                        UpgradeAllUnifiedPlanProjectionInput{
                                std::cref(mixed_options_authority),
                                std::cref(query),
                                std::cref(update_preflight),
                                std::cref(upgrade_issues)});
            },
            "upgrade-all nested different option snapshot");

    std::vector<ProductionSourceBuildWorkItem> mismatched_needed_work{
            source_work_item("repo-base", "repo-child")};
    mismatched_needed_work.front().request.needed = true;
    const SystemSourceUpgradeProjectionAuthority
            mismatched_needed_system_authority =
                    UnifiedPlanProjectionTestAccess::make_system_source(
                            nested_snapshot, nullptr, no_issues,
                            mismatched_needed_work);
    const UpgradeAllOperationPreparedSnapshot matching_outer{
            nested_snapshot, {}, {}};
    const UpgradeAllOperationProjectionAuthority
            mismatched_needed_authority =
                    UnifiedPlanProjectionTestAccess::make_upgrade_all(
                            matching_outer,
                            mismatched_needed_system_authority);
    expect_invalid_argument(
            [&mismatched_needed_authority, &query, &update_preflight,
             &upgrade_issues] {
                (void)project_upgrade_all_unified_plan(
                        UpgradeAllUnifiedPlanProjectionInput{
                                std::cref(mismatched_needed_authority),
                                std::cref(query),
                                std::cref(update_preflight),
                                std::cref(upgrade_issues)});
            },
            "upgrade-all source work with a different needed policy");
}

void test_repository_configuration_correlation() {
    const AurUpdateQueryResult query = update_query();
    const AurUpdateExecutionPreflight matching =
            executable_update_preflight(build_plan_fixture());
    const std::unique_ptr<UnifiedPlanProjection> matching_projection =
            project_aur_update_unified_plan(
                    AurUpdateUnifiedPlanProjectionInput{
                            std::cref(query), std::cref(matching), false});
    const UnifiedPlanObservation& matching_observation =
            require_observation(
                    *matching_projection,
                    "matching repository configuration");
    expect(
            matching_observation.configured_repository_order() != nullptr &&
                    &matching_observation.configured_repository_order()
                             ->configured_order() ==
                            &matching.build_plan
                                     ->configured_repository_order.value(),
            "actual BuildPlan repository configuration was not borrowed");

    AurUpdateExecutionPreflight wrong_index =
            executable_update_preflight(build_plan_fixture());
    std::get<RepositoryProviderOrigin>(
            wrong_index.build_plan->provided.front().provider.origin)
            .configured_order = 0;
    expect_invalid_argument(
            [&query, &wrong_index] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query), std::cref(wrong_index),
                                false});
            },
            "same repository name with different configured index");

    AurUpdateExecutionPreflight missing_repository =
            executable_update_preflight(build_plan_fixture());
    auto& missing_origin = std::get<RepositoryProviderOrigin>(
            missing_repository.build_plan->provided.front().provider.origin);
    missing_origin.repository_name = "unconfigured";
    missing_origin.configured_order = 1;
    expect_invalid_argument(
            [&query, &missing_repository] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query),
                                std::cref(missing_repository), false});
            },
            "provider repository absent from resolution configuration");

    AurUpdateExecutionPreflight unknown_configuration =
            executable_update_preflight(build_plan_fixture());
    unknown_configuration.build_plan->configured_repository_order.reset();
    expect_invalid_argument(
            [&query, &unknown_configuration] {
                (void)project_aur_update_unified_plan(
                        AurUpdateUnifiedPlanProjectionInput{
                                std::cref(query),
                                std::cref(unknown_configuration), false});
            },
            "repository provider with unknown configuration");
}

void test_actual_production_blocked_results() {
    PreparedRootPackageInstall root_ready =
            make_root_prepared(false, true, std::nullopt);
    root_ready.aur_build_plan->dependency_edges.front()
            .constraint_evaluation = ConstraintEvaluation::unsatisfied();
    RootPackageInstallPreparationFailure root_failure;
    root_failure.details.push_back(RootPackageInstallPreparationIssue{
            RootPackageInstallPreparationIssueKind::
                    BuildPlanPreparationFailed,
            std::nullopt, std::nullopt, std::nullopt,
            "fixture production BuildPlan blocker"});
    root_failure.discovery_snapshot =
            std::move(root_ready.discovery_snapshot);
    root_failure.routing_projection =
            std::move(root_ready.routing_projection);
    root_failure.aur_build_plan =
            std::move(root_ready.aur_build_plan);
    const std::unique_ptr<UnifiedPlanProjection> root_projection =
            project_root_package_unified_plan(
                    RootPackageUnifiedPlanProjectionInput{
                            std::cref(root_failure)});
    const UnifiedPlanObservation& root_observation =
            require_observation(*root_projection, "blocked root result");
    expect(
            root_observation.status() ==
                            UnifiedPlanObservationStatus::Blocked &&
                    root_observation.transaction_intents().empty() &&
                    root_observation.required_artifacts().empty() &&
                    std::any_of(
                            root_observation.blockers().begin(),
                            root_observation.blockers().end(),
                            [](const UnifiedPlanBlocker& blocker) {
                                return std::holds_alternative<
                                        RootPackagePreparationUnifiedPlanBlocker>(
                                        blocker);
                            }) &&
                    std::any_of(
                            root_observation.blockers().begin(),
                            root_observation.blockers().end(),
                            [](const UnifiedPlanBlocker& blocker) {
                                return std::holds_alternative<
                                        ConstraintFailureUnifiedPlanBlocker>(
                                        blocker);
                            }),
            "actual root preparation failure lost typed blockers");
    expect(
            !has_phase(
                    root_observation,
                    UnifiedPlanObservationPhase::RepositoryTransaction) &&
                    !has_phase(
                            root_observation,
                            UnifiedPlanObservationPhase::SourceBuild),
            "blocked root result exposed mutation phases");

    const LocalSourceRoot source_root = open_local_source_root(
            "tests/fixtures/unified-plan-local-blocked");
    const LocalPackageMetadata* metadata =
            source_root.metadata().parse_result()->metadata();
    const LocalBuildPlan blocked_local_plan =
            resolve_local_build_plan(*metadata, "unsupported-architecture");
    const std::unique_ptr<UnifiedPlanProjection> local_projection =
            project_local_source_unified_plan(
                    LocalSourceUnifiedPlanProjectionInput{
                            LocalSourceBuildPlanFailureProjectionInput{
                                    std::cref(source_root),
                                    std::cref(blocked_local_plan)},
                            false});
    const UnifiedPlanObservation& local_observation =
            require_observation(*local_projection, "blocked local result");
    expect(
            local_observation.status() ==
                            UnifiedPlanObservationStatus::Blocked &&
                    !blocked_local_plan.failures().empty() &&
                    local_observation.transaction_intents().empty(),
            "actual LocalBuildPlan failure did not reach Blocked observation");

    AurUpdateQueryResult route_query = update_query();
    AurUpdateExecutionTarget route_target;
    route_target.update_plan_index = 0;
    route_target.update = update_entry();
    route_target.status = AurUpdateExecutionTargetStatus::Unsupported;
    route_target.issues.push_back(AurUpdateExecutionIssue{
            AurUpdateExecutionReason::BuildPlanInconsistent,
            "suite-child", "suite-base", std::nullopt,
            "fixture route preflight blocker"});
    const AurUpdateExecutionPreflight route_preflight{
            {route_target}, std::nullopt};
    const std::unique_ptr<UnifiedPlanProjection> route_projection =
            project_aur_update_unified_plan(
                    AurUpdateUnifiedPlanProjectionInput{
                            std::cref(route_query),
                            std::cref(route_preflight), false});
    const UnifiedPlanObservation& route_observation =
            require_observation(*route_projection, "route preflight result");
    expect(
            route_observation.status() ==
                            UnifiedPlanObservationStatus::Blocked &&
                    route_observation.transaction_intents().empty(),
            "actual route preflight issue was not projected as Blocked");

    SystemSourceUpgradePreparedSnapshot nested_snapshot;
    nested_snapshot.registered_sources.push_back(registered_source(
            0, "repo-child", "repo-base",
            SourceBuildSourceKind::Repository));
    SystemSourceUpgradeResult nested_failure;
    nested_failure.status =
            SystemSourceUpgradeStatus::BlockedBeforeMutation;
    nested_failure.stopped_phase = SystemSourceUpgradePhase::Preparation;
    nested_failure.prepared_snapshot = nested_snapshot;
    nested_failure.registered_source_results.push_back(
            not_attempted_source_result(
                    nested_failure.prepared_snapshot.registered_sources
                            .front()));
    nested_failure.issues.push_back(system_issue(
            SystemSourceUpgradeIssueKind::SourceWorkItemPreparationFailed,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            SystemSourceUpgradePhase::Preparation));

    UpgradeAllOperationResult upgrade_failure;
    upgrade_failure.status =
            UpgradeAllOperationStatus::BlockedBeforeMutation;
    upgrade_failure.stopped_phase = UpgradeAllOperationPhase::Preparation;
    upgrade_failure.prepared_snapshot =
            UpgradeAllOperationPreparedSnapshot{nested_snapshot, {}, {}};
    upgrade_failure.system_source = std::move(nested_failure);
    UpgradeAllOperationIssue nested_issue;
    nested_issue.kind =
            UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete;
    nested_issue.phase = UpgradeAllOperationPhase::Preparation;
    nested_issue.diagnostic = "fixture nested source failure";
    upgrade_failure.issues.push_back(std::move(nested_issue));

    upgrade_failure.prepared_snapshot.system_source
            .registered_sources.front()
            .resolved_package_base = "other-blocked-invocation-base";
    expect_invalid_argument(
            [&upgrade_failure] {
                (void)project_upgrade_all_unified_plan(
                        UpgradeAllUnifiedPlanProjectionInput{
                                std::cref(upgrade_failure)});
            },
            "blocked upgrade-all nested different invocation");
    upgrade_failure.prepared_snapshot.system_source
            .registered_sources.front()
            .resolved_package_base = "repo-base";

    const std::unique_ptr<UnifiedPlanProjection> upgrade_projection =
            project_upgrade_all_unified_plan(
                    UpgradeAllUnifiedPlanProjectionInput{
                            std::cref(upgrade_failure)});
    const UnifiedPlanObservation& upgrade_observation =
            require_observation(
                    *upgrade_projection, "blocked upgrade-all result");
    expect(
            upgrade_observation.status() ==
                            UnifiedPlanObservationStatus::Blocked &&
                    upgrade_observation.transaction_intents().empty() &&
                    upgrade_observation.required_artifacts().empty() &&
                    has_existing_phase(
                            upgrade_observation,
                            UnifiedPlanObservationPhase::ExecutionProjection,
                            UpgradeAllOperationPhase::Preparation) &&
                    !has_phase(
                            upgrade_observation,
                            UnifiedPlanObservationPhase::SourceBuild),
            "upgrade-all nested production failure exposed mutation intent");
}

void test_missing_system_source_plan_is_rejected() {
    SystemSourceUpgradePreparedSnapshot snapshot;
    snapshot.registered_sources.push_back(registered_source(
            0, "suite-child", "suite-base", SourceBuildSourceKind::Aur));
    const std::vector<ProductionSourceBuildWorkItem> work_items{
            source_work_item(
                    "suite-base", "suite-child",
                    DesiredInstallReason::Explicit, false,
                    SourceBuildSourceKind::Aur)};
    const std::vector<SystemSourceUpgradeIssue> issues;
    const SystemSourceUpgradeProjectionAuthority authority =
            UnifiedPlanProjectionTestAccess::make_system_source(
                    snapshot, nullptr, issues, work_items);
    expect_invalid_argument(
            [&authority] {
                (void)project_system_source_upgrade_unified_plan(
                        SystemSourceUpgradeUnifiedPlanProjectionInput{
                                std::cref(authority)});
            },
            "AUR registered source without BuildPlan");
}

} // namespace

int main() {
    try {
        test_projection_lifetime_and_root_authorities();
        test_root_repository_order_known_and_unknown();
        test_root_and_update_correlation_fail_closed();
        test_aur_update_install_reason_parity();
        test_build_plan_partial_failure_remains_typed();
        test_local_and_no_op_phases();
        test_actual_prepared_source_work_is_artifact_authority();
        test_system_issue_impact_and_blocked_phases();
        test_partial_failures_and_upgrade_all_authorities();
        test_full_identity_correlation_fail_closed();
        test_repository_configuration_correlation();
        test_actual_production_blocked_results();
        test_missing_system_source_plan_is_rejected();
        std::cout << "unified plan projection tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "unified plan projection test failed: " << error.what()
                  << '\n';
        return 1;
    }
}
