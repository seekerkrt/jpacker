#include "system_source_upgrade.hpp"
#include "unified_plan_renderer.hpp"
#include "upgrade_all_operation.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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
            "aur-child (PackageBase: aur-base) -> repo-dependency>=1 [repository]",
            "dependency authority");
    expect_contains(
            rendered.text, "Selected identity: zeta/repo-dependency",
            "repository dependency identity");
    expect_contains(
            rendered.text, "Constraint: Satisfied", "constraint display");
    expect_contains(
            rendered.text, "AUR PackageBase: aur-base",
            "AUR build unit");
    expect_contains(
            rendered.text, "Package children: aur-child, aur-tools",
            "PackageBase child identities");
    expect_contains(
            rendered.text, "Local PackageBase: local-base",
            "local build unit");
    expect_contains(
            rendered.text,
            "PackageBase: aur-base; package child: aur-child; install reason: dependency",
            "required artifact target");
    expect_contains(
            rendered.text,
            "Repository package transaction (needed: yes)",
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
            "Source-built artifact install boundary (needed: no)",
            "source artifact boundary");
    expect_contains(
            rendered.text, "dependency artifact #1: aur-base/aur-child",
            "dependency artifact install intent");
    expect_contains(
            rendered.text, "root artifact #2: local-base/local-child",
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
            "unknown dependency: missing-runtime>=3 required by blocked-child (PackageBase: blocked-base)",
            "typed blocker category");
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
        test_rendering_issue_is_isolated_from_execution_status();
        std::cout << "unified plan renderer tests passed" << std::endl;
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "unified plan renderer test failed: " << error.what()
                  << std::endl;
        return 1;
    }
}
