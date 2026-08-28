#pragma once

#include "build_plan_artifact_target_projection.hpp"
#include "commands_inspect.hpp"
#include "commands_sync.hpp"
#include "local_source_build.hpp"
#include "source_install.hpp"
#include "system_source_upgrade.hpp"
#include "unified_plan_observation.hpp"
#include "upgrade_all_operation.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

struct AurUpdateExecutionPreflight;
struct AurUpdateQueryResult;
struct AurUpdateSourceBuildPreparation;
class PreparedFilteredAurUpdateOperation;
class PreparedUpgradeAllAurPreflight;

struct FetchUnifiedPlanProjectionInput {
    std::reference_wrapper<const FetchPreparation> source;
};

using SyncInstallUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const PreparedSyncInstall>,
    std::reference_wrapper<const SyncInstallPreparationFailure>>;

struct SyncInstallUnifiedPlanProjectionInput {
    SyncInstallUnifiedPlanProjectionSource source;
};

using RemoteSourceBuildUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const PreparedRemoteSourceBuild>,
    std::reference_wrapper<const RemoteSourceBuildPlanFailure>>;

struct RemoteSourceBuildUnifiedPlanProjectionInput {
    RemoteSourceBuildUnifiedPlanProjectionSource source;
};
// Ready prepared ownerとBlocked production resultだけを受けるread-only sum。
// reference_wrapperによりtemporary authorityは拒否し、fake work itemを
// Blocked armへ要求しない。すべてのownerはprojectionを破棄するまでmove、
// mutation、destroyしてはならない。
using RootPackageUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const PreparedRootPackageInstall>,
    std::reference_wrapper<const RootPackageInstallPreparationFailure>>;

struct RootPackageUnifiedPlanProjectionInput {
    RootPackageUnifiedPlanProjectionSource source;
};

struct LocalSourceBuildPlanFailureProjectionInput {
    std::reference_wrapper<const LocalSourceRoot> source_root;
    std::reference_wrapper<const LocalBuildPlan> local_build_plan;
};

struct LocalSourceMetadataEvaluationProjectionInput {
    std::reference_wrapper<const LocalSourceRoot> source_root;
};

using LocalSourceUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const LocalSourceBuildProjectionAuthority>,
    LocalSourceBuildPlanFailureProjectionInput,
    LocalSourceMetadataEvaluationProjectionInput>;

struct LocalSourceUnifiedPlanProjectionInput {
    LocalSourceUnifiedPlanProjectionSource source;
    bool needed = false;
};

struct AurUpdateUnifiedPlanProjectionInput {
    std::reference_wrapper<const AurUpdateQueryResult> query_result;
    std::reference_wrapper<const AurUpdateExecutionPreflight> preflight;
    bool needed = false;
    std::optional<std::reference_wrapper<
        const AurUpdateSourceBuildPreparation>>
        source_build_preparation = std::nullopt;
    std::optional<std::reference_wrapper<
        const PreparedFilteredAurUpdateOperation>>
        filtered_operation = std::nullopt;
};

using SystemSourceUpgradeUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const SystemSourceUpgradeProjectionAuthority>,
    std::reference_wrapper<const SystemSourceUpgradeResult>>;

struct SystemSourceUpgradeUnifiedPlanProjectionInput {
    SystemSourceUpgradeUnifiedPlanProjectionSource source;
};

using UpgradeAllUnifiedPlanProjectionSource = std::variant<
    std::reference_wrapper<const UpgradeAllOperationProjectionAuthority>,
    std::reference_wrapper<const UpgradeAllOperationResult>>;

struct UpgradeAllUnifiedPlanProjectionInput {
    UpgradeAllUnifiedPlanProjectionSource source;
    std::optional<std::reference_wrapper<const AurUpdateQueryResult>>
        aur_query_result = std::nullopt;
    std::optional<std::reference_wrapper<const AurUpdateExecutionPreflight>>
        aur_preflight = std::nullopt;
    std::optional<std::reference_wrapper<
        const std::vector<UpgradeAllOperationIssue>>>
        issues = std::nullopt;
    std::optional<std::reference_wrapper<
        const AurUpdateSourceBuildPreparation>>
        aur_source_build_preparation = std::nullopt;
    std::optional<std::reference_wrapper<
        const PreparedUpgradeAllAurPreflight>>
        aur_operation_preflight = std::nullopt;
};

// BuildPlan routeのRequiredPackageArtifactTargetはowned projection resultまたは
// route-specific reason viewへ残し、prepared system/source routeはproduction
// work itemを直接borrowする。
// result/observationはcopyできず、内部targetをborrowするviewをbundle外へ
// value escapeさせない。このbundleはexternal production authorityをcopyしない。
class UnifiedPlanProjection final {
public:
    UnifiedPlanProjection(const UnifiedPlanProjection&) = delete;
    UnifiedPlanProjection& operator=(const UnifiedPlanProjection&) = delete;
    UnifiedPlanProjection(UnifiedPlanProjection&&) = delete;
    UnifiedPlanProjection& operator=(UnifiedPlanProjection&&) = delete;
    ~UnifiedPlanProjection() = default;

    [[nodiscard]] const UnifiedPlanObservationResult& observation_result()
        const noexcept;

private:
    explicit UnifiedPlanProjection(
        std::vector<BuildPlanArtifactTargetProjectionResult>
            artifact_target_projections,
        std::vector<ProjectedBuildPlanArtifactTargets>
            route_artifact_targets);

    std::vector<BuildPlanArtifactTargetProjectionResult>
        artifact_target_projections_;
    std::vector<ProjectedBuildPlanArtifactTargets> route_artifact_targets_;
    std::optional<UnifiedPlanObservationResult> observation_result_;

    static std::unique_ptr<UnifiedPlanProjection> make(
        std::vector<BuildPlanArtifactTargetProjectionResult>
            artifact_target_projections,
        UnifiedPlanObservationInput observation_input);
    static std::unique_ptr<UnifiedPlanProjection> make(
        std::vector<BuildPlanArtifactTargetProjectionResult>
            artifact_target_projections,
        std::vector<ProjectedBuildPlanArtifactTargets>
            route_artifact_targets,
        UnifiedPlanObservationInput observation_input);

    friend std::unique_ptr<UnifiedPlanProjection>
    project_root_package_unified_plan(
        RootPackageUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_fetch_unified_plan(FetchUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_sync_install_unified_plan(
        SyncInstallUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_remote_source_build_unified_plan(
        RemoteSourceBuildUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_local_source_unified_plan(
        LocalSourceUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_aur_update_unified_plan(
        AurUpdateUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_filtered_aur_update_unified_plan(
        const PreparedFilteredAurUpdateOperation& prepared);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_system_source_upgrade_unified_plan(
        SystemSourceUpgradeUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_upgrade_all_unified_plan(
        UpgradeAllUnifiedPlanProjectionInput input);
    friend std::unique_ptr<UnifiedPlanProjection>
    project_upgrade_all_unified_plan(
        const UpgradeAllOperationProjectionAuthority& prepared,
        const PreparedUpgradeAllAurPreflight& aur_preflight);
};

std::unique_ptr<UnifiedPlanProjection> project_root_package_unified_plan(
    RootPackageUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_fetch_unified_plan(
    FetchUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_sync_install_unified_plan(
    SyncInstallUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection>
project_remote_source_build_unified_plan(
    RemoteSourceBuildUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_local_source_unified_plan(
    LocalSourceUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_aur_update_unified_plan(
    AurUpdateUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_filtered_aur_update_unified_plan(
    const PreparedFilteredAurUpdateOperation& prepared);
std::unique_ptr<UnifiedPlanProjection> project_system_source_upgrade_unified_plan(
    SystemSourceUpgradeUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_upgrade_all_unified_plan(
    UpgradeAllUnifiedPlanProjectionInput input);
std::unique_ptr<UnifiedPlanProjection> project_upgrade_all_unified_plan(
    const UpgradeAllOperationProjectionAuthority& prepared,
    const PreparedUpgradeAllAurPreflight& aur_preflight);
