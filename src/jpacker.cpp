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
#include "dependency_plan.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "source_build.hpp"
#include "source_preference.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPKG_VERSION
#define JPKG_VERSION "unknown"
#endif

namespace {

const std::string VERSION = JPKG_VERSION;
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";

} // namespace

namespace {

AppConfig g_config;

} // namespace

enum class PromptDefault {
    Yes,
    No,
    None,
};

// requested package から、実際に取得する PackageBase と git URL を結びつける型。
struct PackageBuildSource {
    std::string requested_name;
    std::string clone_name;
    std::string git_url;
    bool        is_aur = false;
    bool        has_distinct_package_base = false;
};

// --- 関数宣言 ---

// CLI入口 / help
int run_jpacker(int argc, char* argv[]);
void print_help();
bool handle_info_only_option(int argc, char* argv[]);
bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]);

// 文字列 / path
std::string trim(const std::string& str);
std::string to_lower(std::string str);
std::string shell_quote(const std::string& str);
bool is_safe_command_token(const std::string& token);
std::vector<std::string> split_command_words(const std::string& command);

// shell引数 / command construction
std::string join_shell_args(const std::vector<std::string>& args);
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);
std::string build_editor_command(const std::string& editor, const fs::path& target);

// pacman / repository補助
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags);
std::string load_source_preference_environment(const std::string& package_name);

// prompt / ユーザー確認
bool ask_user(const std::string& question, PromptDefault default_answer);

// AUR provider / build source解決
bool has_distinct_package_base(const AurPackageInfo& info);
PackageBuildSource resolve_build_source(const std::string& pkg_name);
void require_supported_build_source_install_target(const PackageBuildSource& source);
void require_executable_build_source_plan(const PackageBuildSource& source);

// AUR検索 / info表示
void preflight_aur_search_schema(const std::vector<std::string>& keywords);
bool search_aur(const std::vector<std::string>& keywords, bool query_installed_state = true);
std::string join_display_values(const std::vector<std::string>& values);
std::string join_comma_display_values(const std::vector<std::string>& values);
bool is_orphaned(const AurPackageInfo& pkg);
std::string installed_display(const AurPackageInfo& pkg);
std::string orphaned_display(const AurPackageInfo& pkg);
std::string out_of_date_display(const std::optional<long long>& out_of_date);
void print_aur_info(const AurPackageInfo& pkg);

std::string aur_git_url_for_package_base(const std::string& package_base);

// source build / AUR install
void require_valid_aur_package_target(const std::string& target);
void require_executable_sync_install_target(const std::string& pkg_name);
void install_smart_source(
        const std::string& pkg_name, bool only_if_updated,
        const SourceSyncOptions& source_sync_options);
void execute_aur_build_plan(
        const BuildPlan& plan, bool use_source_build_preferences,
        const SourceSyncOptions& source_sync_options);
void install_aur_build_plan(
        const std::string& target, const SourceSyncOptions& source_sync_options);
void preflight_upgrade_source_metadata();

// コマンド処理
int cmd_sync_search(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_info(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_install(
        const ParsedCliArguments& parsed, bool is_sys_upgrade,
        PackageSourceSelection source_selection);
int cmd_build(const std::vector<std::string>& args);
int cmd_add_src(const std::vector<std::string>& args);
int cmd_edit_src(const std::vector<std::string>& targets);
void cmd_list_src();
int cmd_del_src(const std::vector<std::string>& targets);
void cmd_revert(const std::vector<std::string>& targets);
int cmd_clean();
int cmd_upgrade();

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
            return cmd_build(targets);
        }
        if(operation == "upgrade") {
            return cmd_upgrade();
        }
        if(operation == "clean") {
            return cmd_clean();
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
            cmd_revert(targets);
            return 0;
        }
        if(operation == "edit-src" && !targets.empty()) {
            return cmd_edit_src(targets);
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
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return str;
}

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

bool is_safe_command_token(const std::string& token) {
    if(token.empty()) return false;
    return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '/' || ch == '.' || ch == '_' || ch == '+' || ch == '-' || ch == '=' ||
               ch == ':' || ch == '@' || ch == '%';
    });
}

std::vector<std::string> split_command_words(const std::string& command) {
    std::stringstream        ss(command);
    std::string              word;
    std::vector<std::string> words;
    while(ss >> word) {
        if(!is_safe_command_token(word)) {
            throw std::runtime_error("Unsafe command token: " + word);
        }
        words.push_back(word);
    }
    if(words.empty()) {
        throw std::runtime_error("Editor command is empty.");
    }
    return words;
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

std::string build_editor_command(const std::string& editor, const fs::path& target) {
    std::vector<std::string> args = split_command_words(editor);
    args.push_back(target.string());
    return join_shell_args(args);
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

std::string load_source_preference_environment(const std::string& package_name) {
    return get_package_env(
            package_name,
            [](const fs::path& entry_path) {
                Logger::info("Loading custom build flags from " + entry_path.string());
            },
            [](const std::string& warning) {
                Logger::warn(warning);
            });
}

// prompt / ユーザー確認
std::optional<bool> prompt_default_value(PromptDefault default_answer) {
    switch(default_answer) {
        case PromptDefault::Yes:
            return true;
        case PromptDefault::No:
            return false;
        case PromptDefault::None:
            return std::nullopt;
    }
    return std::nullopt;
}

std::string prompt_suffix(PromptDefault default_answer) {
    switch(default_answer) {
        case PromptDefault::Yes:
            return "[Y/n]";
        case PromptDefault::No:
            return "[y/N]";
        case PromptDefault::None:
            return "[y/n]";
    }
    return "[y/n]";
}

std::string prompt_answer_label(bool answer) {
    return answer ? "yes" : "no";
}

bool ask_user(const std::string& question, PromptDefault default_answer) {
    std::optional<bool> default_value = prompt_default_value(default_answer);

    if(g_config.no_confirm) {
        // POLICY: --noconfirm でも default を持たない prompt は自動回答しない。
        if(default_value.has_value()) {
            Logger::info("Skipping prompt (--noconfirm): " + question + " -> " + prompt_answer_label(default_value.value()));
            return default_value.value();
        }
        throw std::runtime_error("Cannot answer prompt without interaction (--noconfirm): " + question);
    }

    if(!isatty(STDIN_FILENO)) {
        // LANDMINE: 非対話 stdin では、破壊的になり得る yes default を安全に選べない。
        if(default_value.has_value() && default_value.value() == false) {
            Logger::info("Skipping prompt (non-interactive stdin): " + question + " -> no");
            return false;
        }
        throw std::runtime_error("Cannot safely answer prompt with non-interactive stdin: " + question);
    }

    for(;;) {
        std::cout << ":: " << question << " " << prompt_suffix(default_answer) << " ";
        std::string input;
        if(!std::getline(std::cin, input)) {
            throw std::runtime_error("Failed to read prompt input: " + question);
        }

        input = to_lower(trim(input));
        if(input.empty()) {
            if(default_value.has_value()) return default_value.value();
            Logger::warn("Please answer yes or no.");
            continue;
        }
        if(input == "y" || input == "yes") return true;
        if(input == "n" || input == "no") return false;

        Logger::warn("Please answer yes or no.");
    }
}

// AUR provider / build source解決
bool has_distinct_package_base(const AurPackageInfo& info) {
    return info.PackageBase != info.Name;
}

PackageBuildSource resolve_build_source(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);

    if(is_repo_package(pkg_name)) {
        return PackageBuildSource{pkg_name, pkg_name, ARCH_GIT_BASE + pkg_name + ".git", false, false};
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(pkg_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        throw std::runtime_error("Failed to fetch AUR info for " + pkg_name + ": " + e.what());
    }

    if(!info.has_value()) {
        throw std::runtime_error("Package not found in repos or AUR: " + pkg_name);
    }
    if(info->PackageBase.empty()) {
        throw std::runtime_error("AUR info for " + pkg_name + " does not include PackageBase.");
    }
    require_valid_package_name(info->PackageBase);

    return PackageBuildSource{
            pkg_name, info->PackageBase, AUR_BASE_URL + info->PackageBase + ".git", true,
            has_distinct_package_base(info.value())};
}

void require_supported_build_source_install_target(const PackageBuildSource& source) {
    // POLICY(#98): makepkg -i 経路では split package の install 対象を jpacker が個別選択できない。
    // v1.9.0 では、PackageBase と requested package name が異なる AUR target は安全側で停止する。
    if(source.is_aur && source.has_distinct_package_base) {
        throw std::runtime_error(
                "Cannot build/install split AUR package " + source.requested_name + " from PackageBase " +
                source.clone_name + "; explicit split package install target selection is not implemented.");
    }
}

void require_executable_build_source_plan(const PackageBuildSource& source) {
    require_supported_build_source_install_target(source);
    if(!source.is_aur) return;

    // POLICY(#99): build/source-build は makepkg -i まで進む実行系なので、
    // clone/fetch/build/install 前に unresolved / ambiguous / cyclic / split target を拒否する。
    BuildPlan plan = resolve_build_plan(source.requested_name);
    require_executable_install_plan(source.requested_name, plan);
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

std::string join_comma_display_values(const std::vector<std::string>& values) {
    std::stringstream ss;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) ss << ", ";
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

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}


// source build / AUR install
void install_smart_source(
        const std::string& pkg_name, bool only_if_updated,
        const SourceSyncOptions& source_sync_options) {
    std::string env = load_source_preference_environment(pkg_name);
    PackageBuildSource source = resolve_build_source(pkg_name);
    require_executable_build_source_plan(source);

    SourceBuildRequest request;
    request.package_name = source.requested_name;
    request.checkout_name = source.clone_name;
    request.git_url = source.git_url;
    request.custom_environment = env;
    request.only_if_updated = only_if_updated;
    request.needed = source_sync_options.needed;
    execute_source_build(request, g_config);
}

void require_valid_aur_package_target(const std::string& target) {
    if(target.find('/') != std::string::npos || !is_valid_package_name(target)) {
        throw std::runtime_error("Invalid AUR package target: " + target);
    }
}

void require_executable_sync_install_target(const std::string& pkg_name) {
    if(is_repo_package(pkg_name)) {
        PackageBuildSource source = resolve_build_source(pkg_name);
        require_executable_build_source_plan(source);
        return;
    }

    BuildPlan plan = resolve_build_plan(pkg_name);
    require_executable_install_plan(pkg_name, plan);
}

void execute_aur_build_plan(
        const BuildPlan& plan, bool use_source_build_preferences,
        const SourceSyncOptions& source_sync_options) {
    for(const auto& entry : plan.order) {
        std::string package_names = join_comma_display_values(entry.package_names);
        Logger::info("Building AUR PackageBase: " + entry.package_base);
        Logger::info("Target package(s): " + package_names);

        std::string pkg_name = entry.package_names.empty() ? entry.package_base : entry.package_names.front();
        std::string env;
        if(use_source_build_preferences) {
            env = load_source_preference_environment(pkg_name);
            if(env.empty() && pkg_name != entry.package_base) {
                env = load_source_preference_environment(entry.package_base);
            }
        }

        try {
            SourceBuildRequest request;
            request.package_name = pkg_name;
            request.checkout_name = entry.package_base;
            request.git_url = aur_git_url_for_package_base(entry.package_base);
            request.custom_environment = env;
            request.needed = source_sync_options.needed;
            execute_source_build(request, g_config);
        } catch(const std::exception& e) {
            throw std::runtime_error(
                    "Failed while building/installing PackageBase " + entry.package_base + " (" + package_names +
                    "): " + e.what());
        }
    }
}

void install_aur_build_plan(
        const std::string& target, const SourceSyncOptions& source_sync_options) {
    BuildPlan plan = resolve_build_plan(target);
    require_executable_install_plan(target, plan);
    execute_aur_build_plan(plan, true, source_sync_options);
}

void preflight_upgrade_source_metadata() {
    if(!fs::exists(source_preference_root())) return;

    // POLICY(#174): upgradeの既存pacman-first実行は維持するが、schema violationだけは
    // system transactionより前に全source packageのplanを横断して拒否する。
    for(const auto& entry : source_preference_entries()) {
        if(!entry.is_regular_file()) continue;

        std::string pkg_name = entry.path().filename().string();
        if(!is_valid_package_name(pkg_name)) continue;
        try {
            PackageBuildSource source = resolve_build_source(pkg_name);
            if(source.is_aur) {
                // LANDMINE(#174): split/install guardより先にplan全体のschemaを検証する。
                // preflightでは実行可能性を判定せず、ordinary plan errorは実行phaseへ委ねる。
                static_cast<void>(resolve_build_plan(source.requested_name));
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception&) {
            // not-found/transport/通常plan errorは、従来どおりsystem upgrade後の実行phaseで報告する。
        }
    }
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
            execute_aur_build_plan(plan, false, source_sync_options);
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
        require_executable_sync_install_target(package);
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
                install_smart_source(package, false, source_sync_options);
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

int cmd_build(const std::vector<std::string>& args) {
    if(args.empty()) {
        Logger::error("Usage: jpacker build <pkg> [VAR=VAL...]");
        return 1;
    }
    std::string pkg_name, custom_env;
    for(const auto& arg : args) {
        std::string key, val;
        if(split_env_assignment(arg, key, val))
            custom_env += key + "=" + shell_quote(val) + " ";
        else if(arg.find('=') != std::string::npos) {
            Logger::error("Invalid environment assignment: " + arg);
            return 1;
        } else if(pkg_name.empty())
            pkg_name = arg;
        else
            Logger::warn("Ignoring extra arg '" + arg + "'");
    }
    if(pkg_name.empty()) {
        Logger::error("No package specified.");
        return 1;
    }
    require_valid_package_name(pkg_name);

    try {
        PackageBuildSource source = resolve_build_source(pkg_name);
        require_executable_build_source_plan(source);
        // build コマンドは常にビルドする (only_if_updated = false)
        SourceBuildRequest request;
        request.package_name = source.requested_name;
        request.checkout_name = source.clone_name;
        request.git_url = source.git_url;
        request.custom_environment = custom_env;
        execute_source_build(request, g_config);
    } catch(const std::exception& e) {
        Logger::error(std::string("Build Error: ") + e.what());
        return 1;
    }
    return 0;
}

int cmd_add_src(const std::vector<std::string>& args) {
    bool                     failed = false;
    std::vector<std::string> current_pkgs;
    for(const auto& arg : args) {
        std::string key, val;
        if(arg.find('=') == std::string::npos) {
            // POLICY: 1 package = 1 preference file。ファイル名は package name validation で固定する。
            fs::path p = source_preference_entry_path(arg);
            if(run_command("sudo touch " + shell_quote(p.string())) != 0) {
                Logger::error("Failed to add " + arg);
                failed = true;
            } else {
                Logger::info("Added " + arg + " to source-build list.");
                current_pkgs.push_back(p.string());
            }
        } else if(split_env_assignment(arg, key, val)) {
            if(current_pkgs.empty()) {
                Logger::error("Environment assignment requires a preceding package: " + arg);
                failed = true;
                continue;
            }
            for(const auto& pkg_path : current_pkgs) {
                Logger::info("   -> Appending " + arg + " to " + pkg_path);
                if(run_command("printf '%s\\n' " + shell_quote(key + "=" + val) + " | sudo tee -a " + shell_quote(pkg_path) + " > /dev/null") != 0) {
                    Logger::error("Failed to append " + key + " to " + pkg_path);
                    failed = true;
                }
            }
        } else {
            Logger::error("Invalid environment assignment: " + arg);
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

int cmd_edit_src(const std::vector<std::string>& targets) {
    bool        failed = false;
    const char* env_editor = std::getenv("EDITOR");
    std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;
    for(const auto& pkg : targets) {
        fs::path    p = source_preference_entry_path(pkg);
        std::string temp_template = "/tmp/jpacker-edit-src-" + pkg + ".XXXXXX";
        std::vector<char> temp_name(temp_template.begin(), temp_template.end());
        temp_name.push_back('\0');

        int fd = mkstemp(temp_name.data());
        if(fd == -1) {
            Logger::error("Failed to create temporary file: " + std::string(std::strerror(errno)));
            failed = true;
            continue;
        }

        fs::path temp_path = temp_name.data();
        if(close(fd) != 0) {
            Logger::error("Failed to close temporary file " + temp_path.string() + ": " + std::string(std::strerror(errno)));
            std::error_code ec;
            fs::remove(temp_path, ec);
            failed = true;
            continue;
        }

        auto cleanup_temp = [&temp_path]() {
            std::error_code ec;
            fs::remove(temp_path, ec);
            if(ec) Logger::warn("Failed to remove temporary file " + temp_path.string() + ": " + ec.message());
        };

        if(fs::exists(p)) {
            std::ifstream src(p, std::ios::binary);
            if(!src) {
                Logger::error("Failed to read " + p.string());
                cleanup_temp();
                failed = true;
                continue;
            }

            std::ofstream dst(temp_path, std::ios::binary | std::ios::trunc);
            if(!dst) {
                Logger::error("Failed to write temporary file " + temp_path.string());
                cleanup_temp();
                failed = true;
                continue;
            }

            dst << src.rdbuf();
            dst.close();
            if(!dst) {
                Logger::error("Failed to copy " + p.string() + " to " + temp_path.string());
                cleanup_temp();
                failed = true;
                continue;
            }
        }

        if(run_command(build_editor_command(editor_cmd, temp_path)) != 0) {
            Logger::error("Editor failed for " + p.string());
            failed = true;
            cleanup_temp();
            continue;
        }

        // POLICY: /etc 配下の preference 更新だけ sudo に委譲し、編集本体は通常ユーザーの一時ファイルで行う。
        if(run_command("sudo install -Dm644 " + shell_quote(temp_path.string()) + " " + shell_quote(p.string())) != 0) {
            Logger::error("Failed to install edited source-build preference to " + p.string() + "; edited file kept at " + temp_path.string());
            failed = true;
            continue;
        }

        cleanup_temp();
    }
    return failed ? 1 : 0;
}

void cmd_list_src() {
    if(!fs::exists(source_preference_root())) {
        std::cout << "No source-build packages registered." << std::endl;
        return;
    }
    std::cout << "\033[1mRegistered Source Packages:\033[0m" << std::endl;
    bool found = false;
    for(const auto& entry : source_preference_entries()) {
        if(entry.is_regular_file()) {
            found = true;
            std::string pkg = entry.path().filename().string();
            std::cout << "  \033[1;36m" << pkg << "\033[0m" << std::endl;
            read_source_preference_entry(
                    entry.path(),
                    [](const std::string& line) {
                        std::cout << "    " << line << std::endl;
                    });
        }
    }
    if(!found) std::cout << "  (none)" << std::endl;
}

int cmd_del_src(const std::vector<std::string>& targets) {
    bool failed = false;
    for(const auto& pkg : targets) {
        fs::path p = source_preference_entry_path(pkg);
        Logger::info("Removing " + pkg + " from list...");
        if(run_command("sudo rm -f " + shell_quote(p.string())) != 0) {
            Logger::error("Failed to remove " + pkg);
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

void cmd_revert(const std::vector<std::string>& targets) {
    bool                     failed = false;
    std::vector<std::string> reinstall_targets;
    for(const auto& pkg : targets) {
        fs::path p = source_preference_entry_path(pkg);
        if(fs::exists(p)) {
            Logger::info("Unmarking source-build for " + pkg);
            if(run_command("sudo rm -f " + shell_quote(p.string())) != 0) {
                Logger::error("Failed to remove " + pkg);
                failed = true;
                continue;
            }
        } else
            Logger::warn(pkg + " was not marked.");
        if(is_repo_package(pkg)) {
            Logger::info(pkg + " exists in official repos. Will reinstall binary.");
            reinstall_targets.push_back(pkg);
        } else
            Logger::info(pkg + " is likely an AUR package. Config removed only.");
    }
    if(!reinstall_targets.empty()) {
        std::string pkg_list = join_shell_args(reinstall_targets);
        std::vector<std::string> pacman_args = {"-S"};
        pacman_args.insert(pacman_args.end(), reinstall_targets.begin(), reinstall_targets.end());
        Logger::info("Reinstalling binaries: " + pkg_list);
        if(run_command("sudo pacman " + join_pacman_args(pacman_args)) != 0) throw std::runtime_error("Failed to reinstall binaries.");
    }
    if(failed) throw std::runtime_error("Failed to revert one or more packages.");
}

int cmd_clean() {
    // POLICY(#175): validate every cache deletion target before pacman mutation, then revalidate before remove_all.
    // Safe-path UX remains pacman clean -> jpacker cache prompt; unsafe cache state stops before either mutation.
    ValidatedCacheRoot              cache = prepare_trusted_cache_root();
    std::vector<ValidatedCachePath> cleanup_targets = preflight_cache_cleanup(cache);
    bool                            cache_has_entries = !fs::is_empty(cache.canonical_path());
    bool                            failed = false;
    Logger::info("Cleaning package caches...");
    if(run_command("sudo pacman " + join_pacman_args({"-Sc"})) != 0) {
        Logger::warn("Pacman clean failed or cancelled.");
        failed = true;
    }
    if(cache_has_entries) {
        if(ask_user("Clean jpacker build cache (" + cache.path().string() + ")?", PromptDefault::No)) {
            Logger::info("Removing cached build files...");
            bool cleanup_failed = false;
            for(const auto& target : cleanup_targets) {
                try {
                    remove_trusted_cache_path(target);
                } catch(const std::exception& e) {
                    Logger::error("Failed to remove " + target.path().string() + ": " + e.what());
                    cleanup_failed = true;
                }
            }
            if(cleanup_failed) {
                failed = true;
                Logger::warn("jpacker cache cleanup was incomplete.");
            } else {
                Logger::info("jpacker cache cleaned.");
            }
        } else
            Logger::info("Skipped jpacker cache cleaning.");
    } else
        Logger::info("jpacker cache is empty.");
    return failed ? 1 : 0;
}

int cmd_upgrade() {
    bool failed = false;
    preflight_upgrade_source_metadata();
    Logger::info("System upgrade...");
    if(run_command("sudo pacman " + join_pacman_args({"-Syu"})) != 0) throw std::runtime_error("Update failed.");
    if(fs::exists(source_preference_root())) {
        Logger::info("Checking source packages...");
        for(const auto& entry : source_preference_entries()) {
            if(entry.is_regular_file()) {
                std::string pkg_name = entry.path().filename().string();
                if(!is_valid_package_name(pkg_name)) {
                    Logger::warn("Ignoring invalid source-build preference filename: " + pkg_name);
                    failed = true;
                    continue;
                }
                try {
                    // upgrade 時は true (更新がある場合のみビルド)
                    install_smart_source(pkg_name, true, SourceSyncOptions{});
                } catch(const AurRpcResponseError&) {
                    // POLICY(#174): schema violation検出後は後続source packageのmutationへ進まない。
                    throw;
                } catch(const std::exception& e) {
                    Logger::error("Error updating " + pkg_name + ": " + e.what());
                    failed = true;
                }
            }
        }
    }
    return failed ? 1 : 0;
}
