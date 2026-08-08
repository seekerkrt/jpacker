#include "aur_update_execution_preflight.hpp"
#include "aur_update_query.hpp"
#include "commands_sync.hpp"
#include "local_dependency_plan_projection.hpp"
#include "stubs/local-dependency-plan/query_stub.hpp"
#include "system_source_upgrade.hpp"
#include "unified_plan_renderer.hpp"
#include "upgrade_all_operation.hpp"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace query_stub = local_dependency_plan_query_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_contains(
        const std::string& output, std::string_view expected,
        const std::string& context) {
    expect(
            output.find(expected) != std::string::npos,
            context + " is missing: " + std::string(expected));
}

void expect_before(
        const std::string& output, std::string_view first,
        std::string_view second, const std::string& context) {
    const std::size_t first_position = output.find(first);
    const std::size_t second_position = output.find(second);
    expect(
            first_position != std::string::npos,
            context + " is missing first value: " + std::string(first));
    expect(
            second_position != std::string::npos,
            context + " is missing second value: " + std::string(second));
    expect(
            first_position < second_position,
            context + " changed the observed order");
}

const UnifiedPlanObservation& expect_valid(
        const UnifiedPlanObservationResult& result,
        std::string_view context) {
    expect(
            result.is_valid(),
            std::string(context) + " failed observation validation");
    expect(
            result.observation() != nullptr,
            std::string(context) + " has no observation");
    return *result.observation();
}

LocalSourceRootObservationIdentity local_source_identity() {
    return LocalSourceRootObservationIdentity{
            "/work/local-suite",
            LocalSourceDirectoryIdentity{
                    LocalSourceNodeType::Directory, 41, 73, 1000, 0755}};
}

LocalPackageMetadata local_metadata_fixture() {
    return LocalPackageMetadata{
            "local-base",
            std::nullopt,
            "2.0",
            "1",
            {"x86_64"},
            {LocalPackageMetadataChild{
                    "local-child", false, false, {}}},
            {}};
}

BuildPlan build_plan_fixture() {
    BuildPlan plan;
    plan.order.push_back(BuildPlanEntry{
            "aur-base", {"aur-child", "aur-tools"}});
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "aur-child",
            "aur-base",
            "repo-dependency>=1",
            PackageRole::RuntimeDependency,
            DependencyKind::Repo,
            "repo-dependency",
            std::nullopt,
            std::nullopt,
            ProviderResolutionKind::Unique,
            std::nullopt,
            ResolvedDependencyCandidate{RepositoryExactPackage{
                    ConfiguredRepositoryIdentity{"zeta", 0},
                    "repo-dependency",
                    ObservedVersion::available(
                            ObservedVersionSource::RepositoryExactPackage,
                            "1.4"),
                    {}}},
            ConstraintEvaluation::satisfied()});
    return plan;
}

BuildPlanDependencyEdge unknown_dependency_fixture() {
    return BuildPlanDependencyEdge{
            "blocked-child",
            "blocked-base",
            "missing-runtime>=3",
            PackageRole::RuntimeDependency,
            DependencyKind::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            ProviderResolutionKind::Unique,
            std::nullopt,
            std::nullopt,
            ConstraintEvaluation::unknown(
                    ObservedVersionUnknownReason::MetadataQueryFailure)};
}

UnifiedPlanRenderingResult render_blocked(UnifiedPlanBlocker blocker) {
    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Blocked;
    input.blockers.push_back(std::move(blocker));
    const UnifiedPlanObservationResult result =
            make_unified_plan_observation(std::move(input));
    return render_unified_plan_observation(
            expect_valid(result, "single blocker fixture"));
}

LocalPackageMetadata local_build_plan_metadata_fixture() {
    using Comparison = LocalPackageMetadataComparison;
    using RelationKind = LocalPackageMetadataRelationKind;
    using ScopeKind = LocalPackageMetadataScopeKind;
    using TargetKind = LocalPackageMetadataRelationTargetKind;

    return LocalPackageMetadata{
            "local-authority-base",
            std::nullopt,
            "1.0",
            "1",
            {"x86_64"},
            {
                    LocalPackageMetadataChild{
                            "local-provider", false, false, {}},
                    LocalPackageMetadataChild{
                            "local-consumer", false, false, {}},
            },
            {
                    LocalPackageMetadataRelation{
                            RelationKind::Provides,
                            "virtual-api=1",
                            LocalPackageMetadataRelationTarget{
                                    TargetKind::Package, "virtual-api",
                                    Comparison::Equal, "1"},
                            LocalPackageMetadataScope{
                                    ScopeKind::ChildPackage,
                                    "local-provider"},
                            std::nullopt,
                            std::nullopt,
                            false},
                    LocalPackageMetadataRelation{
                            RelationKind::Depends,
                            "virtual-api>=2",
                            LocalPackageMetadataRelationTarget{
                                    TargetKind::Package, "virtual-api",
                                    Comparison::GreaterThanOrEqual, "2"},
                            LocalPackageMetadataScope{
                                    ScopeKind::ChildPackage,
                                    "local-consumer"},
                            std::nullopt,
                            std::nullopt,
                            false},
            }};
}

void test_ready_rendering_and_identity_boundaries() {
    BuildPlan plan = build_plan_fixture();
    LocalPackageMetadata local_metadata = local_metadata_fixture();
    const LocalSourceRootObservationIdentity local_identity =
            local_source_identity();
    const std::vector<std::string> configured_repositories = {
            "zeta", "core"};
    const std::vector<RequiredPackageArtifactTarget> artifacts = {
            RequiredPackageArtifactTarget{
                    "aur-base", "aur-child",
                    DesiredInstallReason::Dependency},
            RequiredPackageArtifactTarget{
                    "local-base", "local-child",
                    DesiredInstallReason::Explicit}};
    const RepositoryExactPackage repository_dependency{
            ConfiguredRepositoryIdentity{"zeta", 0},
            "repo-dependency",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage, "1.4"),
            {}};
    const ProvidedDependency repository_provider =
            ProvidedDependency::from_repository(
                    "core", 1, "provider-package");

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.roots.emplace_back(
            RootTargetIdentity{0, "same-name"},
            RepositoryRootPackageIdentity{"zeta", "same-name"},
            UnifiedPlanRootRouteKind::RepositoryTransaction);
    input.roots.emplace_back(
            RootTargetIdentity{1, "same-name"},
            AurRootPackageIdentity{"same-name", "aur-base"},
            UnifiedPlanRootRouteKind::AurSourceBuild);
    input.roots.emplace_back(
            RootTargetIdentity{2, "local-child"}, local_identity,
            UnifiedPlanRootRouteKind::LocalSourceBuild);
    input.configured_repository_order.emplace(
            std::cref(configured_repositories));
    input.dependency_authorities.push_back(
            UnifiedPlanDependencyAuthorityReference::from_build_plan(plan));
    input.build_units.push_back(AurPackageBaseBuildUnitReference(
            std::cref(plan), 0));
    input.build_units.push_back(LocalSourceBuildUnitReference(
            local_identity, std::cref(local_metadata)));
    input.required_artifacts.emplace_back(
            AurPackageBaseBuildUnitReference(std::cref(plan), 0),
            std::cref(artifacts[0]));
    input.required_artifacts.emplace_back(
            LocalSourceBuildUnitReference(
                    local_identity, std::cref(local_metadata)),
            std::cref(artifacts[1]));

    RepositoryPackageTransactionIntent repository_transaction;
    repository_transaction.policy.needed = true;
    repository_transaction.targets.push_back(RepositoryRootInstallIntent{
            UnifiedPlanRootReference(
                    RootTargetIdentity{0, "same-name"},
                    RepositoryRootPackageIdentity{"zeta", "same-name"},
                    UnifiedPlanRootRouteKind::RepositoryTransaction)});
    repository_transaction.targets.push_back(
            RepositoryDependencyInstallIntent{
                    UnifiedPlanBorrowedAuthorityReference<
                            RepositoryExactPackage>(repository_dependency)});
    repository_transaction.targets.push_back(
            RepositoryProviderInstallIntent{
                    UnifiedPlanBorrowedAuthorityReference<ProvidedDependency>(
                            repository_provider)});
    repository_transaction.targets.push_back(
            RepositorySystemUpgradeIntent{});
    input.transaction_intents.push_back(std::move(repository_transaction));

    SourceBuiltArtifactInstallBoundaryIntent source_transaction;
    source_transaction.needed = false;
    source_transaction.targets.push_back(
            SourceDependencyArtifactInstallIntent{0});
    source_transaction.targets.push_back(SourceRootArtifactInstallIntent{1});
    input.transaction_intents.push_back(std::move(source_transaction));

    input.phases = {
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::RequestDiscovery,
                    UnifiedPlanAuthorityOwner::Moguet,
                    ExistingRoutePhaseReference{
                            SystemSourceUpgradePhase::Preparation}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::MetadataDiscovery,
                    UnifiedPlanAuthorityOwner::Libalpm,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::ForeignInventory}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::ProviderDecision,
                    UnifiedPlanAuthorityOwner::Moguet,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::AurPreparation}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::ExecutionProjection,
                    UnifiedPlanAuthorityOwner::Moguet,
                    ExistingRoutePhaseReference{
                            SystemSourceUpgradePhase::Preparation}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::RepositoryTransaction,
                    UnifiedPlanAuthorityOwner::Pacman,
                    ExistingRoutePhaseReference{
                            SystemSourceUpgradePhase::System}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::SourceRetrieval,
                    UnifiedPlanAuthorityOwner::Git,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::AurExecution}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::SourceBuild,
                    UnifiedPlanAuthorityOwner::Makepkg,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::AurExecution}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::ArtifactValidation,
                    UnifiedPlanAuthorityOwner::Moguet,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::AurExecution}},
            UnifiedPlanPhaseReference{
                    UnifiedPlanObservationPhase::SourceArtifactInstall,
                    UnifiedPlanAuthorityOwner::Pacman,
                    ExistingRoutePhaseReference{
                            UpgradeAllOperationPhase::AurExecution}}};

    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanObservation& observation =
            expect_valid(observation_result, "Ready fixture");
    const UnifiedPlanObservationStatus status_before = observation.status();
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(observation);
    const UnifiedPlanRenderingResult rendered_again =
            render_unified_plan_observation(observation);

    expect(rendered.is_complete(), "Ready rendering is incomplete");
    expect(
            observation.status() == status_before &&
                    status_before == UnifiedPlanObservationStatus::Ready,
            "renderer changed Ready status");
    expect(
            rendered.text == rendered_again.text &&
                    rendered.issues == rendered_again.issues,
            "renderer output is not deterministic");
    expect_contains(rendered.text, "  Status: Ready", "Ready status");
    expect_before(
            rendered.text, "Identity: zeta/same-name",
            "Identity: AUR/same-name (PackageBase: aur-base)",
            "repository/AUR root order");
    expect_before(
            rendered.text,
            "Identity: AUR/same-name (PackageBase: aur-base)",
            "Identity: /work/local-suite (device: 41; inode: 73)",
            "AUR/local root order");
    expect_contains(
            rendered.text, "Route: repository transaction",
            "repository root route");
    expect_contains(
            rendered.text, "Route: AUR source build", "AUR root route");
    expect_contains(
            rendered.text, "Route: local source build", "local root route");
    expect_contains(
            rendered.text, "External owner: libalpm", "external owner");
    expect_before(
            rendered.text, "  1. request / root discovery",
            "  2. metadata / dependency candidate discovery",
            "early phase order");
    expect_before(
            rendered.text, "  5. repository transaction",
            "  6. source retrieval", "mutation phase order");
    expect_contains(
            rendered.text, "Route phase: system/source: preparation",
            "system/source route phase");
    expect_contains(
            rendered.text, "Route phase: upgrade-all: AUR execution",
            "upgrade-all route phase");
    expect_contains(
            rendered.text,
            "Configured repository order:\n  1. zeta\n  2. core\n",
            "configured repository order");
    expect_contains(
            rendered.text,
            "aur-child (PackageBase: aur-base) -> repo-dependency>=1 [repository; role: runtime dependency]",
            "dependency authority");
    expect_contains(
            rendered.text, "Selected identity: zeta/repo-dependency",
            "repository dependency identity");
    expect_contains(
            rendered.text, "Stored constraint result: Satisfied",
            "stored constraint display");
    expect_contains(
            rendered.text,
            "AUR BuildPlan unit #1 (PackageBase: aur-base)",
            "AUR build unit");
    expect_contains(
            rendered.text, "Child packages: aur-child, aur-tools",
            "PackageBase child identities");
    expect_contains(
            rendered.text,
            "local source /work/local-suite (device: 41; inode: 73; PackageBase: local-base)",
            "local build unit");
    expect_contains(
            rendered.text,
            "Required target: PackageBase: aur-base; child package: aur-child; install reason: dependency",
            "required artifact target");
    expect_contains(
            rendered.text,
            "Repository package transaction (pacman --needed policy: yes)",
            "repository transaction intent");
    expect_contains(
            rendered.text, "dependency: zeta/repo-dependency",
            "repository dependency intent");
    expect_contains(
            rendered.text, "selected provider: core/provider-package",
            "repository provider intent");
    expect_contains(rendered.text, "system upgrade", "system intent");
    expect_contains(
            rendered.text,
            "Source-built artifact install boundary (pacman --needed policy: no)",
            "source artifact boundary");
    expect_contains(
            rendered.text,
            "dependency required artifact #1: AUR BuildPlan unit #1 (PackageBase: aur-base); target: aur-base/aur-child",
            "dependency artifact install intent");
    expect_contains(
            rendered.text,
            "root required artifact #2: local source /work/local-suite (device: 41; inode: 73; PackageBase: local-base); target: local-base/local-child",
            "root artifact install intent");
}

void test_no_op_and_blocked_rendering() {
    UnifiedPlanObservationInput no_op_input;
    no_op_input.status = UnifiedPlanObservationStatus::NoOp;
    const UnifiedPlanObservationResult no_op_result =
            make_unified_plan_observation(std::move(no_op_input));
    const UnifiedPlanObservation& no_op =
            expect_valid(no_op_result, "NoOp fixture");
    const UnifiedPlanRenderingResult no_op_rendered =
            render_unified_plan_observation(no_op);
    expect(no_op_rendered.is_complete(), "NoOp rendering is incomplete");
    expect_contains(
            no_op_rendered.text, "  Status: NoOp", "NoOp status");
    expect_contains(
            no_op_rendered.text, "Transaction intents:\n  None",
            "NoOp transaction boundary");

    const BuildPlanDependencyEdge unknown = unknown_dependency_fixture();
    UnifiedPlanObservationInput blocked_input;
    blocked_input.status = UnifiedPlanObservationStatus::Blocked;
    blocked_input.blockers.push_back(UnknownUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlanDependencyEdge>(
                    unknown)});
    const UnifiedPlanObservationResult blocked_result =
            make_unified_plan_observation(std::move(blocked_input));
    const UnifiedPlanObservation& blocked =
            expect_valid(blocked_result, "Blocked fixture");
    const UnifiedPlanObservationStatus status_before = blocked.status();
    const UnifiedPlanRenderingResult blocked_rendered =
            render_unified_plan_observation(blocked);
    expect(blocked_rendered.is_complete(), "Blocked rendering is incomplete");
    expect(
            blocked.status() == status_before &&
                    status_before == UnifiedPlanObservationStatus::Blocked,
            "renderer changed Blocked status");
    expect_contains(
            blocked_rendered.text, "  Status: Blocked", "Blocked status");
    expect_contains(
            blocked_rendered.text,
            "unknown dependency blocker (UnknownUnifiedPlanBlocker); dependency: missing-runtime>=3; parent: blocked-child (PackageBase: blocked-base)",
            "typed blocker category");
}

void test_local_build_plan_dependency_authority() {
    query_stub::reset_repository_stub();
    query_stub::reset_aur_stub();
    const LocalPackageMetadata metadata =
            local_build_plan_metadata_fixture();
    const LocalBuildPlan local_plan =
            resolve_local_build_plan(metadata, "x86_64");

    expect(
            local_plan.build_plan().dependency_edges.size() == 1 &&
                    local_plan.internal_edges().size() == 1 &&
                    local_plan.failures().size() == 1,
            "production LocalBuildPlan fixture lost typed dependency state");

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Blocked;
    input.dependency_authorities.push_back(
            UnifiedPlanDependencyAuthorityReference::from_local_build_plan(
                    local_plan));
    input.blockers.push_back(LocalDependencyPlanUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<
                    LocalDependencyPlanFailure>(
                    local_plan.failures().front())});
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(expect_valid(
                    observation_result, "LocalBuildPlan authority fixture"));

    expect(rendered.is_complete(), "LocalBuildPlan rendering is incomplete");
    expect_contains(
            rendered.text, "  1. LocalBuildPlan",
            "LocalBuildPlan technical authority label");
    expect_contains(
            rendered.text,
            "local-consumer (PackageBase: local-authority-base) -> virtual-api>=2 [local; role: runtime dependency]",
            "LocalBuildPlan dependency edge");
    expect_contains(
            rendered.text,
            "Selected identity: local/local-provider (PackageBase: local-authority-base)",
            "LocalBuildPlan selected candidate");
    expect_contains(
            rendered.text, "Stored constraint result: Unsatisfied",
            "LocalBuildPlan stored constraint result");
    expect_contains(
            rendered.text, "Local internal dependencies:",
            "LocalBuildPlan internal edge section");
    expect_contains(
            rendered.text,
            "LocalDependencyPlanFailureKind::ConstraintMismatch",
            "LocalBuildPlan typed failure detail");
    expect(
            query_stub::repository_query_history().empty() &&
                    query_stub::aur_query_history().empty(),
            "local authority fixture unexpectedly queried a remote source");
}

void test_constraint_second_authority_canary() {
    const ConsumerDependencyRequirement raw_requirement(
            "constraint-canary>=9", "constraint-canary",
            DependencyVersionConstraint(
                    DependencyVersionRelation::GreaterThanOrEqual, "9"));
    const ObservedVersion observed_version = ObservedVersion::available(
            ObservedVersionSource::RepositoryExactPackage, "1");
    const ConstraintEvaluation reevaluated =
            evaluate_consumer_dependency_requirement(
                    raw_requirement, observed_version);
    expect(
            reevaluated.satisfaction() ==
                    ConstraintSatisfaction::Unsatisfied,
            "constraint canary does not differ from stored authority");

    BuildPlan plan;
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "constraint-parent",
            "constraint-base",
            "constraint-canary>=9",
            PackageRole::RuntimeDependency,
            DependencyKind::Repo,
            "constraint-canary",
            std::nullopt,
            std::nullopt,
            ProviderResolutionKind::Unique,
            DependencyRequirement{raw_requirement},
            ResolvedDependencyCandidate{RepositoryExactPackage{
                    ConfiguredRepositoryIdentity{"core", 0},
                    "constraint-canary",
                    observed_version,
                    {}}},
            ConstraintEvaluation::satisfied()});

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.dependency_authorities.push_back(
            UnifiedPlanDependencyAuthorityReference::from_build_plan(plan));
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(expect_valid(
                    observation_result, "constraint authority canary"));

    expect(rendered.is_complete(), "constraint canary rendering is incomplete");
    expect_contains(
            rendered.text, "Stored constraint result: Satisfied",
            "renderer did not preserve stored constraint authority");
    expect(
            rendered.text.find("Stored constraint result: Unsatisfied") ==
                    std::string::npos,
            "renderer exposed a second constraint authority");
}

void test_cross_source_required_artifact_identity() {
    const std::string package_base = "shared-base";
    const std::string package_name = "shared-child";

    BuildPlan aur_plan;
    aur_plan.order.push_back(
            BuildPlanEntry{package_base, {package_name}});
    const LocalPackageMetadata local_metadata{
            package_base,
            std::nullopt,
            "1",
            "1",
            {"x86_64"},
            {LocalPackageMetadataChild{
                    package_name, false, false, {}}},
            {}};
    const LocalSourceRootObservationIdentity local_identity{
            "/work/cross-source-local",
            LocalSourceDirectoryIdentity{
                    LocalSourceNodeType::Directory, 701, 902, 1000, 0755}};
    RegisteredSourcePreferenceSnapshot prepared_source;
    prepared_source.preference_package_name = package_name;
    prepared_source.canonical_source_identity_key =
            "repository-source-key";
    prepared_source.resolved_package_base = package_base;
    prepared_source.source_kind = SourceBuildSourceKind::Repository;
    const std::string prepared_requested_package = package_name;
    const std::string prepared_checkout_package_base = package_base;
    const std::vector<RequiredPackageArtifactTarget> prepared_targets = {
            RequiredPackageArtifactTarget{
                    package_base, package_name,
                    DesiredInstallReason::Explicit}};
    const std::vector<RequiredPackageArtifactTarget> other_targets = {
            RequiredPackageArtifactTarget{
                    package_base, package_name,
                    DesiredInstallReason::Explicit},
            RequiredPackageArtifactTarget{
                    package_base, package_name,
                    DesiredInstallReason::Explicit}};

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.build_units.push_back(AurPackageBaseBuildUnitReference(
            std::cref(aur_plan), 0));
    input.build_units.push_back(LocalSourceBuildUnitReference(
            local_identity, std::cref(local_metadata)));
    input.build_units.push_back(PreparedSystemSourceBuildUnitReference(
            std::cref(prepared_source),
            std::cref(prepared_requested_package),
            std::cref(prepared_checkout_package_base), false, true,
            std::cref(prepared_targets)));
    input.required_artifacts.emplace_back(
            AurPackageBaseBuildUnitReference(std::cref(aur_plan), 0),
            std::cref(other_targets[0]));
    input.required_artifacts.emplace_back(
            LocalSourceBuildUnitReference(
                    local_identity, std::cref(local_metadata)),
            std::cref(other_targets[1]));
    input.required_artifacts.emplace_back(
            PreparedSystemSourceBuildUnitReference(
                    std::cref(prepared_source),
                    std::cref(prepared_requested_package),
                    std::cref(prepared_checkout_package_base), false, true,
                    std::cref(prepared_targets)),
            std::cref(prepared_targets[0]));

    SourceBuiltArtifactInstallBoundaryIntent install_boundary;
    install_boundary.targets.push_back(
            SourceRootArtifactInstallIntent{0});
    install_boundary.targets.push_back(
            SourceRootArtifactInstallIntent{1});
    install_boundary.targets.push_back(
            SourceRootArtifactInstallIntent{2});
    input.transaction_intents.push_back(std::move(install_boundary));

    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(expect_valid(
                    observation_result, "cross-source artifact fixture"));

    expect(rendered.is_complete(), "cross-source rendering is incomplete");
    expect_contains(
            rendered.text,
            "Source/build unit: AUR BuildPlan unit #1 (PackageBase: shared-base)",
            "AUR artifact source identity");
    expect_contains(
            rendered.text,
            "Source/build unit: local source /work/cross-source-local (device: 701; inode: 902; PackageBase: shared-base)",
            "local artifact source identity");
    expect_contains(
            rendered.text,
            "Source/build unit: repository source key repository-source-key (requested package: shared-child; checkout PackageBase: shared-base)",
            "prepared artifact source identity");
    expect_contains(
            rendered.text,
            "root required artifact #1: AUR BuildPlan unit #1",
            "AUR source install boundary identity");
    expect_contains(
            rendered.text,
            "root required artifact #2: local source /work/cross-source-local",
            "local source install boundary identity");
    expect_contains(
            rendered.text,
            "root required artifact #3: repository source key repository-source-key",
            "prepared source install boundary identity");
}

void test_build_plan_order_is_preserved() {
    BuildPlan plan;
    plan.order = {
            BuildPlanEntry{"zeta-unit", {"zeta-child"}},
            BuildPlanEntry{"alpha-unit", {"alpha-child"}},
    };

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.build_units.push_back(AurPackageBaseBuildUnitReference(
            std::cref(plan), 0));
    input.build_units.push_back(AurPackageBaseBuildUnitReference(
            std::cref(plan), 1));
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(expect_valid(
                    observation_result, "BuildPlan order fixture"));

    expect(rendered.is_complete(), "BuildPlan order rendering is incomplete");
    expect_before(
            rendered.text, "PackageBase: zeta-unit",
            "PackageBase: alpha-unit", "BuildPlan authority order");
}

void test_blocker_variant_details() {
    const BuildPlanDependencyEdge unknown = unknown_dependency_fixture();
    const AmbiguousProvidedDependency ambiguous{
            "virtual-blocker",
            {ProvidedDependency::from_aur(
                    "aur-provider", "aur-provider-base",
                    "virtual-blocker", "virtual-blocker=1", "1")}};
    const MixedPackageBaseInstallReasonUnsupported unsupported{
            "mixed-base",
            {MixedPackageBaseInstallReasonArtifact{
                    ArtifactPackageIdentity{"mixed-child", "1-1"},
                    DesiredInstallReason::Dependency,
                    InstalledVersionState::SameVersion,
                    ExistingInstallReason::Explicit,
                    InstallReasonDirective::AsDependency}}};
    const LocalSourceRootFailure local_source_failure{
            LocalSourceRootStage::PkgbuildRead,
            LocalSourceRootErrorCode::PermissionDenied,
            "/work/blocked-local/PKGBUILD",
            std::make_error_code(std::errc::permission_denied)};
    const ConstraintEvaluation constraint =
            ConstraintEvaluation::unsatisfied();
    const BuildPlanMetadataRisk metadata_risk{
            "risk-child", "risk-base", {"conflict-a"}, {"replace-b"}};
    const LocalDependencyPlanFailure local_dependency_failure{
            LocalDependencyPlanFailureKind::ConstraintMismatch,
            "local-parent",
            "virtual-local>=2",
            std::nullopt,
            {LocalDependencyPlanCandidate{
                    "local-provider", "virtual-local=1", "1",
                    std::nullopt, ConstraintEvaluation::unsatisfied()}}};
    RootPackageInstallPreparationFailure root_preparation_failure;
    root_preparation_failure.details.push_back(
            RootPackageInstallPreparationIssue{
                    RootPackageInstallPreparationIssueKind::
                            SourceWorkPreparationFailed,
                    RootPackageSelectionInputGate::Interactive,
                    std::nullopt,
                    std::nullopt,
                    "root work preparation diagnostic"});
    const BuildPlanArtifactTargetProjectionIssue artifact_projection_issue{
            BuildPlanArtifactTargetProjectionIssueKind::PackageBaseMismatch,
            3,
            1,
            {2},
            "expected-base",
            "artifact-child",
            {RootTargetIdentity{4, "artifact-root"}},
            "artifact projection diagnostic"};
    BuildPlan state_plan;
    state_plan.unresolved.push_back("state-missing");
    const AurUpdateExecutionIssue route_issue{
            AurUpdateExecutionReason::ProviderMetadataUnavailable,
            "route-child",
            "route-base",
            "route-dependency>=1",
            "route preflight diagnostic"};

    const auto expect_blocker = [](
                                        UnifiedPlanBlocker blocker,
                                        std::string_view expected,
                                        std::string_view context) {
        const UnifiedPlanRenderingResult rendered =
                render_blocked(std::move(blocker));
        expect(
                rendered.is_complete(),
                std::string(context) + " rendering is incomplete");
        expect_contains(
                rendered.text, expected, std::string(context));
    };

    expect_blocker(
            UnknownUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            BuildPlanDependencyEdge>(unknown)},
            "UnknownUnifiedPlanBlocker", "unknown blocker");
    expect_blocker(
            AmbiguousUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            AmbiguousProvidedDependency>(ambiguous)},
            "AmbiguousUnifiedPlanBlocker", "ambiguous blocker");
    expect_blocker(
            UnsupportedUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            MixedPackageBaseInstallReasonUnsupported>(
                            unsupported)},
            "MixedPackageBaseInstallReasonUnsupported",
            "unsupported blocker");
    expect_blocker(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            LocalSourceRootFailure>(local_source_failure)},
            "LocalSourceRootFailure", "source failure blocker");
    expect_blocker(
            ConstraintFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            ConstraintEvaluation>(constraint)},
            "ConstraintFailureUnifiedPlanBlocker",
            "constraint blocker");
    expect_blocker(
            MetadataRiskUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            BuildPlanMetadataRisk>(metadata_risk)},
            "MetadataRiskUnifiedPlanBlocker", "metadata blocker");
    expect_blocker(
            LocalDependencyPlanUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            LocalDependencyPlanFailure>(
                            local_dependency_failure)},
            "LocalDependencyPlanFailureKind::ConstraintMismatch",
            "local dependency blocker");
    expect_blocker(
            RootPackagePreparationUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            RootPackageInstallPreparationFailure>(
                            root_preparation_failure)},
            "RootPackageInstallPreparationIssueKind::SourceWorkPreparationFailed",
            "root preparation blocker");
    expect_blocker(
            BuildPlanArtifactProjectionUnifiedPlanBlocker{
                    artifact_projection_issue},
            "BuildPlanArtifactTargetProjectionIssueKind::PackageBaseMismatch",
            "artifact projection blocker");
    expect_blocker(
            BuildPlanStateUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<BuildPlan>(
                            state_plan),
                    BuildPlanStateUnifiedPlanBlockerKind::
                            UnresolvedDependency,
                    0},
            "BuildPlanStateUnifiedPlanBlockerKind::UnresolvedDependency",
            "BuildPlan state blocker");
    expect_blocker(
            RoutePreflightUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            AurUpdateExecutionIssue>(route_issue)},
            "AurUpdateExecutionReason::ProviderMetadataUnavailable",
            "route preflight blocker");
}

void test_source_failure_and_route_preflight_subtypes() {
    const BuildPlanResolutionFailure build_plan_failure{
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            "source-parent",
            "source-base",
            "repository-subject",
            "source-dependency>=1",
            {RootTargetIdentity{2, "source-root"}},
            "BuildPlan source diagnostic"};
    const IncompleteProviderCandidateSet incomplete_providers{
            "partial-provider",
            {ProvidedDependency::from_aur(
                    "partial-package", "partial-base",
                    "partial-provider", "partial-provider=1", "1")},
            ObservedVersionUnknownReason::PartialSourceFailure};
    const RepositoryExactPackageSourceFailure repository_package_failure{
            ConfiguredRepositoryIdentity{"core", 0},
            "repository-child",
            PackageMetadataFailure{
                    PackageMetadataErrorCode::QueryFailed,
                    "repository package diagnostic"}};
    const RepositoryProviderSourceFailure repository_provider_failure{
            ConfiguredRepositoryIdentity{"extra", 1},
            DependencyConstraintParseFailure{
                    DependencyConstraintParseFailureKind::InvalidVersion,
                    "virtual-provider=>"}};
    const LocalSourceRootFailure local_failure{
            LocalSourceRootStage::SrcinfoRead,
            LocalSourceRootErrorCode::ReadFailure,
            "/work/local/.SRCINFO",
            std::make_error_code(std::errc::io_error)};
    const AurUpdateQueryFailure aur_failure{
            {"aur-one", "aur-two"}, "AUR query diagnostic"};
    const SystemSourceUpgradeIssue system_issue{
            SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed,
            SystemSourceUpgradeIssueImpact::BlocksExecution,
            SystemSourceUpgradePhase::Preparation,
            5,
            "registered-source",
            "registered-base",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "system/source diagnostic"};
    const UpgradeAllOperationIssue upgrade_issue{
            UpgradeAllOperationIssueKind::AurQueryFailed,
            UpgradeAllOperationPhase::AurQuery,
            1,
            5,
            3,
            2,
            "upgrade-child",
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "upgrade-all diagnostic"};

    const auto expect_source = [](
                                     SourceFailureUnifiedPlanBlocker blocker,
                                     std::string_view expected) {
        const UnifiedPlanRenderingResult rendered = render_blocked(
                UnifiedPlanBlocker{std::move(blocker)});
        expect(rendered.is_complete(), "source subtype rendering incomplete");
        expect_contains(rendered.text, expected, "source subtype");
    };
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            BuildPlanResolutionFailure>(build_plan_failure)},
            "BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable");
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            IncompleteProviderCandidateSet>(
                            incomplete_providers)},
            "ObservedVersionUnknownReason::PartialSourceFailure");
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            RepositoryExactPackageSourceFailure>(
                            repository_package_failure)},
            "RepositoryExactPackageSourceFailure");
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            RepositoryProviderSourceFailure>(
                            repository_provider_failure)},
            "DependencyConstraintParseFailureKind::InvalidVersion");
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            LocalSourceRootFailure>(local_failure)},
            "LocalSourceRootStage::SrcinfoRead");
    expect_source(
            SourceFailureUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            AurUpdateQueryFailure>(aur_failure)},
            "AurUpdateQueryFailure");

    const UnifiedPlanRenderingResult system_rendered = render_blocked(
            RoutePreflightUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            SystemSourceUpgradeIssue>(system_issue)});
    expect(
            system_rendered.is_complete(),
            "system/source route issue rendering incomplete");
    expect_contains(
            system_rendered.text,
            "SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed",
            "system/source route issue kind");
    expect_contains(
            system_rendered.text, "registered-source",
            "system/source route package identity");

    const UnifiedPlanRenderingResult upgrade_rendered = render_blocked(
            RoutePreflightUnifiedPlanBlocker{
                    UnifiedPlanBorrowedAuthorityReference<
                            UpgradeAllOperationIssue>(upgrade_issue)});
    expect(
            upgrade_rendered.is_complete(),
            "upgrade-all route issue rendering incomplete");
    expect_contains(
            upgrade_rendered.text,
            "UpgradeAllOperationIssueKind::AurQueryFailed",
            "upgrade-all route issue kind");
    expect_contains(
            upgrade_rendered.text, "BuildPlan unit index: 2",
            "upgrade-all BuildPlan identity");
}

void test_blocked_partial_identity_is_incomplete_rendering() {
    BuildPlan plan;
    plan.unresolved.push_back("partial-missing-dependency");

    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Blocked;
    input.roots.emplace_back(
            RootTargetIdentity{0, "partial-request"},
            AurRootPackageIdentity{"", ""},
            UnifiedPlanRootRouteKind::AurSourceBuild);
    input.build_units.push_back(AurPackageBaseBuildUnitReference(
            std::cref(plan), 7));
    input.blockers.push_back(BuildPlanStateUnifiedPlanBlocker{
            UnifiedPlanBorrowedAuthorityReference<BuildPlan>(plan),
            BuildPlanStateUnifiedPlanBlockerKind::UnresolvedDependency,
            0});

    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanObservation& observation = expect_valid(
            observation_result, "Blocked partial identity fixture");
    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(observation);

    expect(
            observation.status() == UnifiedPlanObservationStatus::Blocked,
            "renderer changed partial Blocked status");
    expect(
            !rendered.is_complete(),
            "partial Blocked rendering was reported complete");
    expect(
            !rendered.issues.empty(),
            "partial Blocked rendering has no renderer-local issue");
    bool has_missing_reference = false;
    for(const UnifiedPlanRenderingIssue& issue : rendered.issues) {
        if(issue.kind ==
           UnifiedPlanRenderingIssueKind::MissingReferencedValue) {
            has_missing_reference = true;
        }
    }
    expect(
            has_missing_reference,
            "partial Blocked rendering lost MissingReferencedValue");
    expect_contains(
            rendered.text, "unavailable",
            "partial Blocked fallback display");
    expect_contains(
            rendered.text, "  Status: Blocked",
            "partial Blocked status display");
}

void test_rendering_issue_is_isolated_from_execution_status() {
    UnifiedPlanObservationInput input;
    input.status = UnifiedPlanObservationStatus::Ready;
    input.phases.push_back(UnifiedPlanPhaseReference{
            UnifiedPlanObservationPhase::RequestDiscovery,
            static_cast<UnifiedPlanAuthorityOwner>(999), std::nullopt});
    const UnifiedPlanObservationResult observation_result =
            make_unified_plan_observation(std::move(input));
    const UnifiedPlanObservation& observation =
            expect_valid(observation_result, "unsupported owner fixture");

    const UnifiedPlanRenderingResult rendered =
            render_unified_plan_observation(observation);
    expect(!rendered.is_complete(), "unsupported owner rendered as complete");
    expect(rendered.issues.size() == 1, "unexpected rendering issue count");
    expect(
            rendered.issues.front().kind ==
                            UnifiedPlanRenderingIssueKind::UnsupportedValue &&
                    rendered.issues.front().section ==
                            UnifiedPlanRenderingSection::Phases,
            "unsupported owner issue lost typed presentation location");
    expect_contains(
            rendered.text, "  Status: Ready",
            "rendering issue status isolation");
    expect_contains(
            rendered.text, "External owner: unsupported",
            "unsupported owner fallback");
    expect(
            observation.status() == UnifiedPlanObservationStatus::Ready,
            "rendering issue changed execution status");
}

} // namespace

int main() {
    try {
        test_ready_rendering_and_identity_boundaries();
        test_no_op_and_blocked_rendering();
        test_local_build_plan_dependency_authority();
        test_constraint_second_authority_canary();
        test_cross_source_required_artifact_identity();
        test_build_plan_order_is_preserved();
        test_blocker_variant_details();
        test_source_failure_and_route_preflight_subtypes();
        test_blocked_partial_identity_is_incomplete_rendering();
        test_rendering_issue_is_isolated_from_execution_status();
        std::cout << "unified plan renderer tests passed" << std::endl;
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "unified plan renderer test failed: " << error.what()
                  << std::endl;
        return 1;
    }
}
