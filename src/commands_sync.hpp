#pragma once

#include "cli_parser.hpp"
#include "dependency_plan.hpp"
#include "root_package_route_projection.hpp"
#include "root_package_search.hpp"
#include "source_install.hpp"

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

// productionはTTY gateをcandidate queryより先に確定する。
RootPackageInstallPreparation prepare_root_package_install(
        const ParsedCliArguments& parsed,
        RootPackageSelectionInvocation invocation,
        const AppConfig& config);

// exact repository transactionを先に1回だけ実行し、成功時だけAUR lifecycleへ進む。
int execute_prepared_root_package_install(
        PreparedRootPackageInstall prepared,
        const AppConfig& config);
