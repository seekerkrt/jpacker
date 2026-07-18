#pragma once

#include "cli_parser.hpp"

struct AppConfig;

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
