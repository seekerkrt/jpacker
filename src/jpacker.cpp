/**
 * jpacker - A full-featured Pacman wrapper and AUR helper
 *
 * Features:
 * - Smart Upgrade: Skips rebuilding packages if the version hasn't changed during 'upgrade'.
 * - Variable expansion support in config files.
 * - '--nodiff' option support.
 * - '--noconfirm' option support.
 */

// このファイルは、jpacker の CLI 入口、pacman wrapper、AUR/source build 補助をまとめる実装単位。
// 関数宣言と詳細実装は、将来の分割候補が見えるように section comment で大まかな責務ごとに分類する。

#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "cli_parser.hpp"
#include "cli_routing.hpp"
#include "commands_aur_update.hpp"
#include "commands_inspect.hpp"
#include "commands_source_maintenance.hpp"
#include "commands_sync.hpp"
#include "commands_upgrade_all.hpp"
#include "logging.hpp"
#include "process.hpp"
#include "shell_words.hpp"
#include "source_install.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPACKER_VERSION
#define JPACKER_VERSION "unknown"
#endif

namespace {

const std::string VERSION = JPACKER_VERSION;

} // namespace

namespace {

AppConfig g_config;

} // namespace

// --- 関数宣言 ---

// CLI入口 / help
int run_jpacker(int argc, char* argv[]);
void print_help();
bool handle_info_only_option(int argc, char* argv[]);
bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]);

// shell引数 / command construction
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);

// pacman / repository補助
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags);
namespace {

AppConfig load_invocation_app_config() {
#ifdef JPACKER_ENABLE_TEST_CONFIG_PATH
    // POLICY: productionはfixed pathを使い、専用test binaryだけが正規の明示path loaderを選ぶ。
    const char* test_config_path = std::getenv("JPACKER_TEST_CONFIG_FILE");
    if(test_config_path && test_config_path[0] != '\0') {
        return load_app_config(test_config_path);
    }
#endif
    return load_default_app_config();
}

void apply_cli_overrides(AppConfig& config, const CliOverrides& overrides) {
    // POLICY: CLI global optionはenable-only。未指定fieldでconfig fileのtrueを消さない。
    if(overrides.no_edit) config.no_edit = true;
    if(overrides.no_diff) config.no_diff = true;
    if(overrides.no_confirm) config.no_confirm = true;
    if(overrides.rebuild) config.rebuild = true;
    if(overrides.clean_build) config.clean_build = true;
    if(overrides.rm_deps) config.rm_deps = true;
}

} // namespace

// --- CLI 入口 ---
int run_jpacker(int argc, char* argv[]) {
    if(handle_info_only_option(argc, argv)) return 0;

    // POLICY(#167): parser/root-check failureも -Gp のmachine-readable stdoutへ混ぜない。
    if(argv_requests_pkgbuild_export_diagnostics(argc, argv)) {
        Logger::set_diagnostics_to_stderr();
    }

    if(geteuid() == 0) {
        Logger::error("Do not run jpacker as root or with sudo.");
        Logger::error("Run jpacker as a normal user; jpacker will invoke sudo/pacman when needed.");
        return 1;
    }

    if(argc < 2) {
        print_help();
        return 1;
    }

    // Configはparse成功までlocalに保持し、invalid CLIから最終実行設定へのpartial publishを防ぐ。
    AppConfig config;
    try {
        config = load_invocation_app_config();
    } catch(const std::exception& e) {
        std::cerr << "Warning: Failed to load config: " << e.what() << std::endl;
    } catch(...) {
        std::cerr << "Warning: Failed to load config: unknown error." << std::endl;
    }

    std::optional<ParsedCliArguments> parsed_result = parse_cli_arguments(argc, argv);
    if(!parsed_result.has_value()) return 1;

    const ParsedCliArguments&        parsed = parsed_result.value();
    if(parsed.operation.empty()) {
        // POLICY: usage presentationはrunnerが所有し、CLI override publish前に終了する。
        print_help();
        return 1;
    }
    apply_cli_overrides(config, parsed.cli_overrides);
    g_config = std::move(config);

    if(parsed.operation == "upgrade-all") {
        // POLICY(#281): upgrade-all is target-less and does not inherit
        // pacman operands. Reject misuse before log/cache initialization or
        // any source/inventory/AUR preparation.
        const std::vector<std::string> validation_errors =
                validate_upgrade_all_invocation(parsed);
        if(!validation_errors.empty()) {
            for(const std::string& error : validation_errors) {
                Logger::error(error);
            }
            return 1;
        }
        try {
            // Config-file RMDEPS must also fail before preparation, including
            // invocations with no registered source preferences.
            require_supported_production_source_build_options(g_config);
        } catch(const std::exception& error) {
            Logger::error(error.what());
            return 1;
        }
    }

    std::optional<PkgbuildExportMode> export_mode = pkgbuild_export_mode(parsed);
    if(export_mode.has_value()) {
        // POLICY(#167): export/print は cache log 初期化より前に分岐し、内部 build cache を作らない。
        Logger::set_diagnostics_to_stderr();
        const std::vector<std::string> validation_errors =
                validate_pkgbuild_export_invocation(parsed);
        if(!validation_errors.empty()) {
            for(const auto& error : validation_errors) Logger::error(error);
            return 1;
        }

        CurlGlobal curl_global;
        try {
            if(export_mode.value() == PkgbuildExportMode::Tree) {
                return cmd_export_pkgbuild_tree(parsed.targets.front());
            }
            return cmd_print_pkgbuild(parsed.targets.front());
        } catch(const std::exception& e) {
            Logger::error(e.what());
            return 1;
        }
    }

    // POLICY(#168): selector conflict / scope errors must stop before the default log creates the cache root.
    std::optional<std::string> source_selection_error =
            validate_source_selection_operation(parsed);
    if(source_selection_error.has_value()) {
        Logger::error(source_selection_error.value());
        return 1;
    }

    if(parsed.operation == "upgrade-aur") {
        if(!parsed.targets.empty()) {
            Logger::error("upgrade-aur does not accept target operands.");
            return 1;
        }
        try {
            // POLICY(#267): NoUpdatesでも--rmdepsを成功扱いせず、queryやlog/cache
            // 初期化より前に既存separated lifecycleのoption契約で拒否する。
            require_supported_production_source_build_options(g_config);
        } catch(const std::exception& error) {
            Logger::error(error.what());
            return 1;
        }
    }

    const std::vector<std::string> optionless_operations = {
            "build", "upgrade", "upgrade-aur", "upgrade-all", "clean", "add-src", "del-src", "revert", "edit-src", "list-src"};
    if(std::find(optionless_operations.begin(), optionless_operations.end(), parsed.operation) !=
                       optionless_operations.end() &&
       !validate_optionless_jpacker_operation(parsed.operation, parsed.flags)) {
        // POLICY(#169): unsupported custom-operation options must stop before cache or source mutation.
        return 1;
    }

    CurlGlobal curl_global;
    try {
        fs::path log_path;
        if(g_config.log_file.empty()) {
            // POLICY(#175): default log must not create or open a file through an unsafe cache root/symlink.
            ValidatedCacheRoot cache_root = prepare_trusted_cache_root();
            ValidatedCachePath cache_log = require_trusted_cache_path(
                    cache_root, "jpacker.log",
                    CachePathRequirement::ExistingOrMissing);
            if(cache_log.exists() && !fs::is_regular_file(cache_log.canonical_path())) {
                throw std::runtime_error(
                        "Unsafe jpacker cache log path " + cache_log.path().string() +
                        ": expected a regular file.");
            }
            log_path = cache_log.canonical_path();
        } else {
            log_path = expand_config_path(g_config.log_file);
        }
        Logger::init(log_path);
        Logger::info("Started jpacker v" + VERSION);
    } catch(const std::exception& e) {
        std::cerr << "Warning: Failed to initialize log: " << e.what() << std::endl;
    } catch(...) {
        std::cerr << "Warning: Failed to initialize log: unknown error." << std::endl;
    }

    const std::string&               operation = parsed.operation;
    const std::vector<std::string>&  args = parsed.ordered_pacman_args;
    const std::vector<std::string>&  targets = parsed.targets;
    const std::vector<std::string>&  flags = parsed.flags;

    try {
        if(operation == "build") {
            return cmd_build(targets, g_config);
        }
        if(operation == "upgrade") {
            return cmd_upgrade(g_config);
        }
        if(operation == "upgrade-aur") {
            return cmd_upgrade_aur(g_config);
        }
        if(operation == "upgrade-all") {
            return cmd_upgrade_all(g_config);
        }
        if(operation == "clean") {
            return cmd_clean(g_config);
        }
        if(operation == "deps") {
            return cmd_deps(targets, flags);
        }
        if(operation == "plan") {
            return cmd_plan(targets, flags);
        }
        if(operation == "fetch") {
            return cmd_fetch(targets, flags);
        }
        if((operation == "add-src" || operation == "del-src" || operation == "revert" || operation == "edit-src") && targets.empty()) {
            Logger::error("Missing target for " + operation);
            return 1;
        }
        if(operation == "add-src" && !targets.empty()) {
            return cmd_add_src(targets);
        }
        if(operation == "del-src" && !targets.empty()) {
            return cmd_del_src(targets);
        }
        if(operation == "revert" && !targets.empty()) {
            cmd_revert(targets, g_config);
            return 0;
        }
        if(operation == "edit-src" && !targets.empty()) {
            return cmd_edit_src(targets, g_config);
        }
        if(operation == "list-src") {
            cmd_list_src();
            return 0;
        }

        bool requests_refresh = pacman_operation_requests_refresh(operation, flags);
        bool is_sync = operation.starts_with("-S");
        bool is_query = operation.starts_with("-Q");
        bool is_foreign_updates = (is_query && operation.find('u') != std::string::npos && operation.find('a') != std::string::npos);
        SourceSelectableSyncOperation selected_sync_operation = source_selectable_sync_operation(parsed);
        bool is_search = parsed.source_selection == PackageSourceSelection::Auto
                                 ? (is_sync && operation.find('s') != std::string::npos)
                                 : selected_sync_operation == SourceSelectableSyncOperation::Search;
        bool is_info = parsed.source_selection == PackageSourceSelection::Auto
                               ? (is_sync && operation.find('i') != std::string::npos)
                               : selected_sync_operation == SourceSelectableSyncOperation::Info;
        bool is_clean = (is_sync && operation.find('c') != std::string::npos);
        bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));
        bool needs_sudo =
                is_sync || operation.starts_with("-R") || operation.starts_with("-U") ||
                operation.starts_with("-D") || (operation.starts_with("-F") && requests_refresh);

        if(is_foreign_updates) return cmd_query_foreign_updates();

        if(is_search) {
            return cmd_sync_search(
                    parsed, requests_refresh, parsed.source_selection, g_config);
        }
        if(is_info) {
            return cmd_sync_info(
                    parsed, requests_refresh, parsed.source_selection, g_config);
        }
        if(is_clean) return run_command("sudo pacman " + join_pacman_args(args));

        if(is_sync) {
            return cmd_sync_install(
                    parsed, is_sys_upgrade, parsed.source_selection, g_config);
        }
        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + join_pacman_args(args));
    } catch(const std::exception& e) {
        Logger::error(e.what());
        return 1;
    }
}

#ifdef JPACKER_ENABLE_APP_CONFIG_TEST_HOOKS
namespace {

int verify_parse_failure_does_not_publish_cli_overrides() {
    char program[] = "jpacker";
    char no_edit[] = "--noedit";
    char no_diff[] = "--nodiff";
    char no_confirm[] = "--noconfirm";
    char rebuild[] = "--rebuild";
    char clean_build[] = "--cleanbuild";
    char rm_deps[] = "--rmdeps";
    char operation[] = "-Q";
    char missing_value_option[] = "--config";
    std::array<char*, 9> parse_failure_argv = {
            program, no_edit, no_diff, no_confirm, rebuild,
            clean_build, rm_deps, operation, missing_value_option};

    if(run_jpacker(static_cast<int>(parse_failure_argv.size()), parse_failure_argv.data()) != 1) {
        std::cerr << "Expected CLI parse failure." << std::endl;
        return 1;
    }
    if(g_config.no_edit || g_config.no_diff || g_config.no_confirm || g_config.rebuild ||
       g_config.clean_build || g_config.rm_deps) {
        std::cerr << "CLI parse failure published partial config overrides." << std::endl;
        return 1;
    }
    return 0;
}

} // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef JPACKER_ENABLE_APP_CONFIG_TEST_HOOKS
    const char* app_config_test_case = std::getenv("JPACKER_TEST_APP_CONFIG_CASE");
    if(app_config_test_case && std::string(app_config_test_case) == "parse-failure-cli-overrides") {
        return verify_parse_failure_does_not_publish_cli_overrides();
    }
#endif
    return run_jpacker(argc, argv);
}

// --- 関数実装 ---

// CLI入口 / help
void print_help() {
    std::cout << "\033[1;36mjpacker\033[0m v" << VERSION << "\n"
              << std::endl;
    std::cout << "\033[1mUSAGE\033[0m" << std::endl;
    std::cout << "    jpacker <op> [options] [targets...]\n"
              << std::endl;
    std::cout << "\033[1mOPERATIONS\033[0m" << std::endl;
    std::cout << "    \033[1mbuild\033[0m <pkg> [V=K]  One-off source build" << std::endl;
    std::cout << "    \033[1mupgrade\033[0m              System update and configured rebuilds" << std::endl;
    std::cout << "                              Checks registered source-build preferences after -Syu" << std::endl;
    std::cout << "    \033[1mupgrade-aur\033[0m          Update installed AUR packages only" << std::endl;
    std::cout << "                              Does not run -Syu; source-build preferences are optional" << std::endl;
    std::cout << "    \033[1mupgrade-all\033[0m          Update system, registered source packages, and remaining installed AUR packages" << std::endl;
    std::cout << "                              Explicit source preferences take priority and prevent duplicate package/PackageBase builds" << std::endl;
    std::cout << "                              Accepts --noedit, --nodiff, --noconfirm, --rebuild, --cleanbuild; no target operands" << std::endl;
    std::cout << "    \033[1mclean\033[0m                Clean package/build caches" << std::endl;
    std::cout << std::endl;
    std::cout << "\033[1mAUR INSPECTION\033[0m" << std::endl;
    std::cout << "    \033[1m-G\033[0m <pkg>             Export one AUR PackageBase repository to ./<PackageBase> (no build/install)" << std::endl;
    std::cout << "    \033[1m-Gp\033[0m <pkg>            Print only one AUR PackageBase PKGBUILD to stdout (no persistent checkout)" << std::endl;
    std::cout << "    \033[1mdeps\033[0m [--recursive] <pkg> Classify AUR dependencies" << std::endl;
    std::cout << "    \033[1mplan\033[0m <pkg>           Show AUR build order plan" << std::endl;
    std::cout << "    \033[1mfetch\033[0m <pkg>          Safely clone/fetch AUR build repositories" << std::endl;
    std::cout << "                              Existing clones: git fetch only; no worktree update/pull/reset/build/install" << std::endl;
    std::cout << std::endl;
    std::cout << "\033[1mSOURCE BUILD PREFERENCES\033[0m" << std::endl;
    std::cout << "    \033[1madd-src\033[0m <pkg> [V=K]  Enable source-build preference for pkg" << std::endl;
    std::cout << "    \033[1medit-src\033[0m <pkg>       Edit source-build preference" << std::endl;
    std::cout << "    \033[1mlist-src\033[0m             List source-build preferences" << std::endl;
    std::cout << "    \033[1mdel-src\033[0m <pkg>        Remove source-build preference" << std::endl;
    std::cout << "    \033[1mrevert\033[0m <pkg>         Remove preference and reinstall binary package" << std::endl;
    std::cout << std::endl;
    std::cout << "\033[1mPACMAN COMPATIBILITY\033[0m" << std::endl;
    std::cout << "    \033[1m-S\033[0m <pkg>              Install package" << std::endl;
    std::cout << "    \033[1m-Syu\033[0m                  System upgrade" << std::endl;
    std::cout << "                              Pacman-compatible; does not scan all source-build preferences" << std::endl;
    std::cout << "    \033[1m-Ss\033[0m <query>          Search packages" << std::endl;
    std::cout << "    \033[1m-Si\033[0m <pkg>             Show package info" << std::endl;
    std::cout << "    \033[1m-Qua\033[0m                 Check AUR/foreign updates" << std::endl;
    std::cout << std::endl;
    std::cout << "\033[1mOPTIONS\033[0m" << std::endl;
    std::cout << "    \033[1m--noedit\033[0m             Skip PKGBUILD/.install review" << std::endl;
    std::cout << "    \033[1m--nodiff\033[0m             Skip update diff prompt" << std::endl;
    std::cout << "    \033[1m--noconfirm\033[0m         Pass --noconfirm to pacman/makepkg" << std::endl;
    std::cout << "    \033[1m--needed\033[0m            Pass to pacman; AUR/source -S applies it only to final install" << std::endl;
    std::cout << "                              Adds no jpacker build/review/plan skip" << std::endl;
    std::cout << "    \033[1m--rebuild\033[0m           Pass -f to build-only makepkg" << std::endl;
    std::cout << "    \033[1m--cleanbuild\033[0m        Pass -C to build-only makepkg" << std::endl;
    std::cout << "    \033[1m--rmdeps\033[0m            Unsupported for separated source builds; no dependency cleanup is performed" << std::endl;
    std::cout << "    \033[1m--aur\033[0m               Limit -S/-Ss/-Si to AUR; no repository fallback" << std::endl;
    std::cout << "    \033[1m--repo\033[0m              Limit -S/-Ss/-Si to official binary repositories; no AUR/source-build fallback" << std::endl;
    std::cout << "\033[1mCONFIG\033[0m" << std::endl;
    std::cout << "    jpacker.conf: EDITOR=..., LOGFILE=..., NOEDIT=..., NODIFF=..., RMDEPS=..." << std::endl;
}

bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]) {
    // POLICY(#173): operation 前のglobal optionだけを飛ばし、option value/opaque operandは見ない。
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(is_jpacker_global_option(arg)) continue;
        return arg == "-G" || arg == "-Gp";
    }
    return false;
}

bool handle_info_only_option(int argc, char* argv[]) {
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(is_jpacker_global_option(arg)) continue;
        if(arg == "-h" || arg == "--help") {
            print_help();
            return true;
        }
        if(arg == "-V" || arg == "--version") {
            std::cout << "jpacker v" << VERSION << std::endl;
            return true;
        }
        // POLICY(#173): help/versionはoperation位置だけで扱い、option valueやopaque operandを横取りしない。
        return false;
    }
    return false;
}

// shell引数 / command construction
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args) {
    if(g_config.no_confirm) {
        // POLICY(#173): generated optionはoperation直後へ置き、semantic `--`やoption valueを再解釈しない。
        // 認識済みglobal tokenはordered viewから除外済みなので、ここでは常に1件だけ生成する。
        if(args.empty())
            args.push_back("--noconfirm");
        else
            args.insert(args.begin() + 1, "--noconfirm");
    }
    return args;
}

std::string join_pacman_args(const std::vector<std::string>& args) {
    return shell_words::join(pacman_args_with_global_options(args));
}

// pacman / repository補助
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == operation) continue;

        Logger::error("Unsupported " + operation + " option: " + flag);
        return false;
    }
    return true;
}
