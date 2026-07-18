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
#include "commands_inspect.hpp"
#include "commands_source_maintenance.hpp"
#include "dependency_plan.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPKG_VERSION
#define JPKG_VERSION "unknown"
#endif

namespace {

const std::string VERSION = JPKG_VERSION;

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

// 文字列 / path
std::string shell_quote(const std::string& str);

// shell引数 / command construction
std::string join_shell_args(const std::vector<std::string>& args);
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);

// pacman / repository補助
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags);
// AUR検索 / info表示
void preflight_aur_search_schema(const std::vector<std::string>& keywords);
bool search_aur(const std::vector<std::string>& keywords, bool query_installed_state = true);
std::string join_display_values(const std::vector<std::string>& values);
bool is_orphaned(const AurPackageInfo& pkg);
std::string installed_display(const AurPackageInfo& pkg);
std::string orphaned_display(const AurPackageInfo& pkg);
std::string out_of_date_display(const std::optional<long long>& out_of_date);
void print_aur_info(const AurPackageInfo& pkg);

// source build / AUR install
void require_valid_aur_package_target(const std::string& target);
void install_aur_build_plan(
        const std::string& target, const SourceSyncOptions& source_sync_options);

// コマンド処理
int cmd_sync_search(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_info(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_install(
        const ParsedCliArguments& parsed, bool is_sys_upgrade,
        PackageSourceSelection source_selection);
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

    const std::vector<std::string> optionless_operations = {
            "build", "upgrade", "clean", "add-src", "del-src", "revert", "edit-src", "list-src"};
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
            if(targets.empty()) {
                Logger::error("Missing search query.");
                return 1;
            }
            return cmd_sync_search(parsed, requests_refresh, parsed.source_selection);
        }
        if(is_info) {
            if(parsed.source_selection == PackageSourceSelection::Auto && requests_refresh) {
                auto unqualified_target = std::find_if(targets.begin(), targets.end(), [](const std::string& target) {
                    return target.find('/') == std::string::npos;
                });
                if(unqualified_target != targets.end()) {
                    // POLICY(#172): refresh 後に AUR fallback すると official DB の更新だけが先行する。
                    // refresh 付き info は repository-qualified target に限定し、分類前に停止する。
                    Logger::error(
                            "Cannot combine pacman refresh with AUR info fallback for unqualified target: " +
                            *unqualified_target);
                    Logger::error(
                            "Use a repository-qualified target such as repo/package, or run refresh and -Si separately.");
                    return 1;
                }
            }
            return cmd_sync_info(parsed, requests_refresh, parsed.source_selection);
        }
        if(is_clean) return run_command("sudo pacman " + join_pacman_args(args));

        if(is_sync) {
            return cmd_sync_install(parsed, is_sys_upgrade, parsed.source_selection);
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
    std::cout << "    \033[1m--rebuild\033[0m           Pass -f to makepkg build/install" << std::endl;
    std::cout << "    \033[1m--cleanbuild\033[0m        Pass -C to makepkg build/install" << std::endl;
    std::cout << "    \033[1m--rmdeps\033[0m            Pass -r to makepkg build/install" << std::endl;
    std::cout << "    \033[1m--aur\033[0m               Limit -S/-Ss/-Si to AUR; no repository fallback" << std::endl;
    std::cout << "    \033[1m--repo\033[0m              Limit -S/-Ss/-Si to official binary repositories; no AUR/source-build fallback" << std::endl;
    std::cout << "\033[1mCONFIG\033[0m" << std::endl;
    std::cout << "    jpacker.conf: EDITOR=..., LOGFILE=..., NOEDIT=..., NODIFF=..." << std::endl;
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

// 文字列 / path
std::string shell_quote(const std::string& str) {
    // POLICY: 外部コマンド引数は、validation 済みの値でも shell 境界では必ず quote する。
    std::string quoted = "'";
    for(char ch : str) {
        if(ch == '\'')
            quoted += "'\\''";
        else
            quoted += ch;
    }
    quoted += "'";
    return quoted;
}

// shell引数 / command construction
std::string join_shell_args(const std::vector<std::string>& args) {
    std::stringstream ss;
    for(size_t i = 0; i < args.size(); ++i) {
        if(i > 0) ss << " ";
        ss << shell_quote(args[i]);
    }
    return ss.str();
}

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
    return join_shell_args(pacman_args_with_global_options(args));
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

// AUR検索 / info表示
void preflight_aur_search_schema(const std::vector<std::string>& keywords) {
    // POLICY(#174): refresh付きsearchはAUR responseのschemaをDB mutationより先に検証する。
    for(const auto& keyword : keywords) {
        if(keyword.empty() || keyword[0] == '-') continue;
        try {
            static_cast<void>(AurClient::search(keyword));
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception&) {
            // transport/その他の既存search契約は、pacman実行後の通常search phaseへ委ねる。
        }
    }
}

bool search_aur(const std::vector<std::string>& keywords, bool query_installed_state) {
    bool                  found = false;
    // POLICY(#168): AurOnly search must not invoke pacman, even for the optional [installed] annotation.
    std::set<std::string> installed_foreign_packages =
            query_installed_state ? get_foreign_package_names() : std::set<std::string>{};
    for(const auto& pkg_name : keywords) {
        if(pkg_name.empty()) continue;
        if(pkg_name[0] == '-') continue;
        for(const auto& info : AurClient::search(pkg_name)) {
            found = true;
            const std::string& name = info.Name;
            std::cout << "\033[1;35maur\033[0m/\033[1m" << name << "\033[0m \033[1;32m"
                      << info.Version << "\033[0m";
            if(installed_foreign_packages.contains(name)) {
                std::cout << " \033[1;36m[installed]\033[0m";
            }
            if(info.OutOfDate.has_value()) {
                std::cout << " \033[1;31m[out-of-date]\033[0m";
            }
            if(is_orphaned(info)) {
                std::cout << " \033[1;33m[orphaned]\033[0m";
            }
            std::cout << std::endl;
            if(!info.Description.empty()) std::cout << "    " << info.Description << std::endl;
        }
    }
    return found;
}

std::string join_display_values(const std::vector<std::string>& values) {
    if(values.empty()) return "None";
    std::stringstream ss;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) ss << "  ";
        ss << values[i];
    }
    return ss.str();
}

bool is_orphaned(const AurPackageInfo& pkg) {
    return pkg.Maintainer.empty();
}

std::string installed_display(const AurPackageInfo& pkg) {
    return is_installed_package(pkg.Name) ? "\033[1;36myes\033[0m" : "no";
}

std::string orphaned_display(const AurPackageInfo& pkg) {
    return is_orphaned(pkg) ? "\033[1;33myes\033[0m" : "no";
}

std::string out_of_date_display(const std::optional<long long>& out_of_date) {
    return out_of_date.has_value() ? "\033[1;31myes\033[0m" : "no";
}

void print_aur_info(const AurPackageInfo& pkg) {
    std::cout << "Repository      : aur" << std::endl;
    std::cout << "Name            : " << pkg.Name << std::endl;
    std::cout << "Package Base    : " << pkg.PackageBase << std::endl;
    std::cout << "Version         : " << pkg.Version << std::endl;
    std::cout << "Description     : " << (pkg.Description.empty() ? "None" : pkg.Description) << std::endl;
    std::cout << "Depends On      : " << join_display_values(pkg.Depends) << std::endl;
    std::cout << "Make Deps       : " << join_display_values(pkg.MakeDepends) << std::endl;
    std::cout << "Check Deps      : " << join_display_values(pkg.CheckDepends) << std::endl;
    std::cout << "Optional Deps   : " << join_display_values(pkg.OptDepends) << std::endl;
    std::cout << "Provides        : " << join_display_values(pkg.Provides) << std::endl;
    std::cout << "Conflicts With  : " << join_display_values(pkg.Conflicts) << std::endl;
    std::cout << "Replaces        : " << join_display_values(pkg.Replaces) << std::endl;
    std::cout << "Maintainer      : " << (pkg.Maintainer.empty() ? "None" : pkg.Maintainer) << std::endl;
    std::cout << "Installed       : " << installed_display(pkg) << std::endl;
    std::cout << "Orphaned        : " << orphaned_display(pkg) << std::endl;
    std::cout << "Out of Date     : " << out_of_date_display(pkg.OutOfDate) << std::endl;
}

// source build / AUR install
void require_valid_aur_package_target(const std::string& target) {
    if(target.find('/') != std::string::npos || !is_valid_package_name(target)) {
        throw std::runtime_error("Invalid AUR package target: " + target);
    }
}

void install_aur_build_plan(
        const std::string& target, const SourceSyncOptions& source_sync_options) {
    BuildPlan plan = resolve_build_plan(target);
    require_executable_install_plan(target, plan);
    execute_aur_build_plan(
            plan, true, source_sync_options.needed, g_config);
}

// コマンド処理
int cmd_sync_search(
        const ParsedCliArguments& parsed, bool use_sudo,
        PackageSourceSelection source_selection) {
    std::string pacman_prefix = use_sudo ? "sudo pacman " : "pacman ";
    switch(source_selection) {
    case PackageSourceSelection::Auto: {
        if(use_sudo) preflight_aur_search_schema(parsed.targets);
        int pacman_status =
                run_command(pacman_prefix + join_pacman_args(parsed.ordered_pacman_args));
        Logger::info("Searching AUR...");
        bool aur_found = search_aur(parsed.targets);
        return (pacman_status == 0 || aur_found) ? 0 : 1;
    }
    case PackageSourceSelection::AurOnly:
        if(parsed_has_semantic_pacman_option(parsed, "--needed")) {
            Logger::error("Unsupported pacman option for AUR search: --needed");
            return 1;
        }
        Logger::info("Searching AUR...");
        return search_aur(parsed.targets, false) ? 0 : 1;
    case PackageSourceSelection::RepoOnly:
        return run_command(pacman_prefix + join_pacman_args(parsed.ordered_pacman_args));
    }
    throw std::logic_error("Unknown package source selection.");
}

int cmd_sync_install(
        const ParsedCliArguments& parsed, bool is_sys_upgrade,
        PackageSourceSelection source_selection) {
    const SourceSyncOptions source_sync_options = parse_source_sync_options(parsed);

    if(source_selection == PackageSourceSelection::RepoOnly) {
        // POLICY(#168): RepoOnly is one ordered binary repository transaction; no classification probe.
        return run_command("sudo pacman " + join_pacman_args(parsed.ordered_pacman_args));
    }

    if(source_selection == PackageSourceSelection::AurOnly) {
        if(parsed.targets.empty()) {
            Logger::error("Missing AUR package target.");
            return 1;
        }
        for(const auto& target : parsed.targets) {
            require_valid_aur_package_target(target);
        }

        std::optional<std::string> unsupported_option = unsupported_source_sync_option(parsed);
        if(unsupported_option.has_value()) {
            Logger::error(
                    "Unsupported pacman option for AUR/source-build target: " +
                    unsupported_option.value());
            Logger::error("Rerun --aur without this option.");
            return 1;
        }

        std::vector<BuildPlan> plans;
        plans.reserve(parsed.targets.size());
        for(const auto& target : parsed.targets) {
            BuildPlan plan = resolve_build_plan(target);
            require_executable_install_plan(target, plan);
            plans.push_back(std::move(plan));
        }

        // POLICY(#168): every root target is fully planned and guarded before clone/build/install starts.
        for(const auto& plan : plans) {
            execute_aur_build_plan(
                    plan, false, source_sync_options.needed, g_config);
        }
        return 0;
    }

    if(parsed.targets.empty()) {
        return run_command("sudo pacman " + join_pacman_args(parsed.ordered_pacman_args));
    }

    std::vector<std::string> repo_targets;
    std::vector<std::string> aur_targets;
    std::set<size_t>         aur_target_token_indices;
    for(size_t i = 0; i < parsed.targets.size(); ++i) {
        const std::string& target = parsed.targets[i];
        require_valid_package_name(target);
        if(is_force_source(target)) {
            aur_targets.push_back(target);
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        } else if(is_repo_package(target)) {
            repo_targets.push_back(target);
        } else {
            aur_targets.push_back(target);
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        }
    }
    if(!aur_targets.empty()) {
        std::optional<std::string> unsupported_option = unsupported_source_sync_option(parsed);
        if(unsupported_option.has_value()) {
            Logger::error(
                    "Unsupported pacman option for AUR/source-build target: " +
                    unsupported_option.value());
            Logger::error(
                    "Split official repository and AUR/source-build targets, or rerun without this option.");
            return 1;
        }
    }
    for(const auto& package : aur_targets) {
        require_executable_source_install_target(package);
    }
    if(!repo_targets.empty() || is_sys_upgrade) {
        // POLICY(#173): AUR targetのtokenだけを除き、option/value/official targetの元順序を維持する。
        std::vector<std::string> pacman_args =
                ordered_pacman_args_excluding_targets(parsed, aur_target_token_indices);
        if(run_command("sudo pacman " + join_pacman_args(pacman_args)) != 0) {
            throw std::runtime_error("Pacman failed.");
        }
    }
    if(!aur_targets.empty()) {
        // Auto keeps the legacy source-build preference and AUR classification behavior.
        for(const auto& package : aur_targets) {
            if(is_repo_package(package))
                install_smart_source(
                        package, false, source_sync_options.needed, g_config);
            else
                install_aur_build_plan(package, source_sync_options);
        }
    }
    return 0;
}

int cmd_sync_info(
        const ParsedCliArguments& parsed, bool use_sudo,
        PackageSourceSelection source_selection) {
    std::string pacman_prefix = use_sudo ? "sudo pacman " : "pacman ";

    if(source_selection == PackageSourceSelection::RepoOnly) {
        return run_command(pacman_prefix + join_pacman_args(parsed.ordered_pacman_args));
    }

    if(source_selection == PackageSourceSelection::AurOnly) {
        if(parsed_has_semantic_pacman_option(parsed, "--needed")) {
            Logger::error("Unsupported pacman option for AUR info: --needed");
            return 1;
        }
        if(parsed.targets.empty()) {
            Logger::error("Missing AUR package target.");
            return 1;
        }
        for(const auto& target : parsed.targets) {
            require_valid_aur_package_target(target);
        }

        bool                        failed = false;
        std::vector<AurPackageInfo> aur_infos;
        for(const auto& target : parsed.targets) {
            try {
                std::optional<AurPackageInfo> info = AurClient::info(target);
                if(info.has_value())
                    aur_infos.push_back(info.value());
                else {
                    Logger::error("AUR package not found: " + target);
                    failed = true;
                }
            } catch(const std::exception& e) {
                Logger::error("Failed to fetch AUR info for " + target + ": " + e.what());
                failed = true;
            }
        }
        for(size_t i = 0; i < aur_infos.size(); ++i) {
            if(i > 0) std::cout << std::endl;
            print_aur_info(aur_infos[i]);
        }
        return failed ? 1 : 0;
    }

    if(parsed.targets.empty()) {
        return run_command(pacman_prefix + join_pacman_args(parsed.ordered_pacman_args));
    }

    bool                        failed = false;
    std::vector<std::string>    repo_targets;
    std::set<size_t>            aur_target_token_indices;
    std::vector<AurPackageInfo> aur_infos;

    for(size_t i = 0; i < parsed.targets.size(); ++i) {
        const std::string& target = parsed.targets[i];
        if(target.find('/') != std::string::npos) {
            repo_targets.push_back(target);
            continue;
        }

        require_valid_package_name(target);
        if(is_repo_package(target)) {
            repo_targets.push_back(target);
            continue;
        }

        try {
            std::optional<AurPackageInfo> info = AurClient::info(target);
            if(info.has_value()) {
                aur_infos.push_back(info.value());
                aur_target_token_indices.insert(parsed.target_token_indices[i]);
            } else {
                Logger::error("Package not found in repos or AUR: " + target);
                failed = true;
                aur_target_token_indices.insert(parsed.target_token_indices[i]);
            }
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch AUR info for " + target + ": " + e.what());
            failed = true;
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        }
    }

    if(!repo_targets.empty()) {
        // POLICY(#173): 同名のoption valueを残し、AUR targetのtoken位置だけを除外する。
        std::vector<std::string> pacman_args =
                ordered_pacman_args_excluding_targets(parsed, aur_target_token_indices);
        if(run_command(pacman_prefix + join_pacman_args(pacman_args)) != 0) failed = true;
        if(!aur_infos.empty()) std::cout << std::endl;
    }

    for(size_t i = 0; i < aur_infos.size(); ++i) {
        if(i > 0) std::cout << std::endl;
        print_aur_info(aur_infos[i]);
    }

    return failed ? 1 : 0;
}
