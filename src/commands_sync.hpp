#pragma once

#include "cli_parser.hpp"
#include "dependency_plan.hpp"
#include "repository_query.hpp"
#include "root_package_route_projection.hpp"
#include "root_package_search.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct AppConfig;
struct RootPackageSelectionInvocation;

// Root selection後の全static preflightを通過したinstall invocation。
// repository targetはexact repo/package、source invocationはcache未activateの
// capabilityとして保持し、execute ownerだけがmutationへ進める。
struct PreparedRootPackageInstall {
    std::vector<std::string> exact_repository_targets;
    std::optional<PreparedProductionSourceBuildInvocation> source_invocation;
    bool needed = false;
    // These production authorities are retained by move/copy of their
    // existing value models; the unified projection only borrows them.
    std::optional<RootPackageSearchSnapshot> discovery_snapshot;
    std::optional<RootPackageRoutingProjection> routing_projection;
    std::optional<BuildPlan> aur_build_plan;
};

enum class RootPackageInstallPreparationIssueKind {
    EmptyQuery,
    RemoveDependenciesUnsupported,
    InputGateUnavailable,
    SelectionUnavailable,
    SelectionCancelled,
    SourceOptionsWithoutAurTarget,
    BuildPlanPreparationFailed,
    SourceWorkPreparationFailed,
};

struct RootPackageInstallPreparationIssue {
    RootPackageInstallPreparationIssueKind kind =
            RootPackageInstallPreparationIssueKind::EmptyQuery;
    std::optional<RootPackageSelectionInputGate> input_gate = std::nullopt;
    std::optional<RootPackageSelectionUnavailableReason>
            selection_unavailable_reason = std::nullopt;
    std::optional<RootPackageSelectionCancellationReason>
            selection_cancellation_reason = std::nullopt;
    std::string diagnostic;
};

using RootPackageInstallPreparationFailureDetail = std::variant<
        RootPackageInstallPreparationIssue,
        RepositoryRootPackageSearchFailure,
        AurRootPackageSearchFailure,
        InvalidRootPackageSearchSnapshot,
        InvalidRootPackageRoutingProjection>;

// preparation failureもtyped authorityを失わず所有し、successful prepared
// capabilityやfake work itemと同居させない。
struct RootPackageInstallPreparationFailure {
    std::vector<RootPackageInstallPreparationFailureDetail> details;
    std::optional<RootPackageSearchSnapshot> discovery_snapshot;
    std::optional<RootPackageRoutingProjection> routing_projection;
    std::optional<BuildPlan> aur_build_plan;
};

using RootPackageInstallPreparation = std::variant<
        PreparedRootPackageInstall,
        RootPackageInstallPreparationFailure>;

// ordinary -S rootを元CLI ordinalとstrict repository authorityへ結ぶ。
// repository source-buildはbinary transactionと別routeとして保持する。
struct SyncRepositoryTransactionRoot {
    RootTargetIdentity        invocation_correlation;
    RepositoryPackagePresent package;
};

struct SyncRepositorySourceRoot {
    RootTargetIdentity             invocation_correlation;
    RepositoryPackagePresent      package;
    ResolvedSourceBuildIdentity   source;
    // preparationが確定したexact source invocation ownerへのtyped relation。
    // failure snapshotでは未確定を保持できるが、successful projectionは必須。
    std::optional<std::size_t> source_work_item_index = std::nullopt;
};

struct SyncAurRoot {
    RootTargetIdentity invocation_correlation;
    // Auto fallbackだけがstrict confirmed absenceを保持する。
    std::optional<RepositoryPackageNotFound> repository_absence;
    std::size_t build_plan_root_index = 0;
};

using SyncInstallRoot = std::variant<
        SyncRepositoryTransactionRoot,
        SyncRepositorySourceRoot,
        SyncAurRoot>;

enum class SyncInstallPreparationIssueKind {
    UnsupportedSourceSelection,
    MissingAurTarget,
    InvalidTarget,
    TargetCorrelationFailed,
    UnsupportedSourceOption,
    SourceBuildOptionsUnsupported,
    RepositoryAuthorityChanged,
    BuildPlanResolutionFailed,
    BuildPlanBlocked,
    BuildPlanCorrelationFailed,
    SourceWorkPreparationFailed,
    EmptyPreparedRoute,
};

struct SyncInstallPreparationIssue {
    SyncInstallPreparationIssueKind kind =
            SyncInstallPreparationIssueKind::InvalidTarget;
    std::optional<RootTargetIdentity> root;
    std::optional<std::string> option;
    std::string diagnostic;
};

struct SyncRepositoryMetadataReadFailure {
    RootTargetIdentity        root;
    RepositoryMetadataFailure failure;
};

using SyncInstallPreparationFailureDetail = std::variant<
        SyncInstallPreparationIssue,
        SyncRepositoryMetadataReadFailure,
        SourcePreferenceFailure>;

struct PreparedSyncInstall {
    std::vector<SyncInstallRoot> ordered_roots;
    std::vector<std::string> repository_pacman_args;
    PackageSourceSelection source_selection = PackageSourceSelection::Auto;
    bool repository_transaction_required = false;
    bool system_update = false;
    bool needed = false;
    std::optional<BuildPlan> aur_build_plan;
    std::optional<PreparedProductionSourceBuildInvocation> source_invocation;
};

struct SyncInstallPreparationFailure {
    std::vector<SyncInstallPreparationFailureDetail> details;
    std::vector<SyncInstallRoot> ordered_roots;
    PackageSourceSelection source_selection = PackageSourceSelection::Auto;
    bool system_update = false;
    bool needed = false;
    std::optional<BuildPlan> aur_build_plan;
};

using SyncInstallPreparation = std::variant<
        PreparedSyncInstall,
        SyncInstallPreparationFailure>;

int cmd_sync_search(
        const ParsedCliArguments& parsed,
        bool use_sudo,
        PackageSourceSelection source_selection,
        const AppConfig& config);

int cmd_sync_info(
        const ParsedCliArguments& parsed,
        bool use_sudo,
        PackageSourceSelection source_selection,
        const AppConfig& config);

int cmd_sync_install(
        const ParsedCliArguments& parsed,
        bool is_sys_upgrade,
        PackageSourceSelection source_selection,
        const AppConfig& config);

// Auto/AurOnlyまたは明示system-updateのcache-free production preflight。
// RepoOnlyとplain targetless -Sはgeneric pacman ownerなので受理しない。
SyncInstallPreparation prepare_sync_install(
        const ParsedCliArguments& parsed,
        bool system_update,
        PackageSourceSelection source_selection,
        const AppConfig& config);

int execute_prepared_sync_install(
        PreparedSyncInstall prepared,
        const AppConfig& config);

// productionはTTY gateをcandidate queryより先に確定する。
RootPackageInstallPreparation prepare_root_package_install(
        const ParsedCliArguments& parsed,
        RootPackageSelectionInvocation invocation,
        const AppConfig& config);

// exact repository transactionを先に1回だけ実行し、成功時だけAUR lifecycleへ進む。
int execute_prepared_root_package_install(
        PreparedRootPackageInstall prepared,
        const AppConfig& config);
