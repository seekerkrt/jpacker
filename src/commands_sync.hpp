#pragma once

#include "cli_parser.hpp"
#include "source_install.hpp"

#include <optional>
#include <string>
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
};

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
std::optional<PreparedRootPackageInstall> prepare_root_package_install(
        const ParsedCliArguments& parsed,
        RootPackageSelectionInvocation invocation,
        const AppConfig& config);

// exact repository transactionを先に1回だけ実行し、成功時だけAUR lifecycleへ進む。
int execute_prepared_root_package_install(
        PreparedRootPackageInstall prepared,
        const AppConfig& config);
