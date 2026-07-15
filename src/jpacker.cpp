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

#include "aur_rpc.hpp"
#include "dependency_plan.hpp"
#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "pkgbuild_export.hpp"
#include "process.hpp"
#include "repository_query.hpp"
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
#include <limits>
#include <map>
#include <optional>
#include <regex>
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
const std::string CONFIG_FILE = "/etc/jpacker/jpacker.conf";
// POLICY: source-build preference の永続化場所。意味を変える場合は互換性影響として扱う。
#ifdef JPACKER_ENABLE_TEST_OVERRIDES
const std::string PACKAGE_BUILD_DIR = [] {
    const char* test_package_build_dir = std::getenv("JPACKER_TEST_PACKAGE_BUILD_DIR");
    if(test_package_build_dir && test_package_build_dir[0] != '\0') {
        return std::string(test_package_build_dir);
    }
    return std::string("/etc/jpacker/package.build");
}();
#else
const std::string PACKAGE_BUILD_DIR = "/etc/jpacker/package.build";
#endif

} // namespace

// jpacker.conf と CLI option を反映した、1回の実行中の設定状態。
struct AppConfig {
    bool        no_edit = false;
    bool        no_diff = false;
    bool        no_confirm = false;
    bool        rebuild = false;
    bool        clean_build = false;
    bool        rm_deps = false;
    std::string editor = "nano";
    std::string log_file = "";
};

enum class PackageSourceSelection {
    Auto,
    AurOnly,
    RepoOnly,
};

enum class JpackerGlobalOption {
    NoEdit,
    NoDiff,
    NoConfirm,
    Rebuild,
    CleanBuild,
    RmDeps,
    Aur,
    Repo,
};

enum class SourceSelectableSyncOperation {
    Install,
    Search,
    Info,
    Unsupported,
};

enum class CliTokenRole {
    JpackerGlobalOption,
    Operation,
    PacmanOption,
    PacmanOptionValue,
    EndOfOptions,
    Target,
    OpaqueOperand,
};

struct ParsedCliToken {
    std::string  value;
    size_t       argv_index;
    CliTokenRole role;
};

// CLI tokenの構文上の役割と、routing用view / pacman委譲用viewを同じparse結果に束ねる。
struct ParsedCliArguments {
    std::string                 operation;
    std::vector<ParsedCliToken> tokens;
    std::vector<std::string>    ordered_pacman_args;
    std::vector<std::string>    consumed_global_options;
    std::vector<std::string>    flags;
    std::vector<std::string>    targets;
    std::vector<size_t>         target_token_indices;
    std::optional<std::string>  pending_option;
    bool                        end_of_options = false;
    PackageSourceSelection      source_selection = PackageSourceSelection::Auto;
};

// pacman-compatible sync optionのうち、source buildへ意味を保って変換できるinvocation-level policy。
struct SourceSyncOptions {
    bool needed = false;
};

enum class PkgbuildExportMode {
    Tree,
    PkgbuildStdout,
};

namespace {

AppConfig g_config;

const std::array<std::pair<const char*, JpackerGlobalOption>, 8> JPACKER_GLOBAL_OPTIONS = {{
        {"--noedit", JpackerGlobalOption::NoEdit},
        {"--nodiff", JpackerGlobalOption::NoDiff},
        {"--noconfirm", JpackerGlobalOption::NoConfirm},
        {"--rebuild", JpackerGlobalOption::Rebuild},
        {"--cleanbuild", JpackerGlobalOption::CleanBuild},
        {"--rmdeps", JpackerGlobalOption::RmDeps},
        {"--aur", JpackerGlobalOption::Aur},
        {"--repo", JpackerGlobalOption::Repo},
}};

} // namespace

struct MakepkgBuildOptions {
    bool rebuild = false;
    bool clean_build = false;
    bool rm_deps = false;
    bool needed = false;
};

enum class PromptDefault {
    Yes,
    No,
    None,
};

enum class UpdateCheckResult {
    NeedsBuild,
    UpToDate,
    Unknown,
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
bool is_jpacker_global_option(const std::string& arg);
bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]);
bool apply_jpacker_global_option(const std::string& arg, ParsedCliArguments& parsed);
std::optional<ParsedCliArguments> parse_cli_arguments(int argc, char* argv[]);
std::optional<PkgbuildExportMode> pkgbuild_export_mode(const ParsedCliArguments& parsed);
bool validate_pkgbuild_export_invocation(const ParsedCliArguments& parsed);
bool parsed_has_semantic_pacman_option(
        const ParsedCliArguments& parsed, const std::string& option);
SourceSyncOptions parse_source_sync_options(const ParsedCliArguments& parsed);
std::string package_source_selection_option(PackageSourceSelection selection);
SourceSelectableSyncOperation source_selectable_sync_operation(const ParsedCliArguments& parsed);
bool validate_source_selection_operation(const ParsedCliArguments& parsed);

// 文字列 / path / config
std::string trim(const std::string& str);
bool remote_url_matches_expected(const std::string& current_url, const std::string& expected_url);
std::string to_lower(std::string str);
std::string unquote(const std::string& str);
std::string strip_comment(const std::string& line);
std::string shell_quote(const std::string& str);
bool is_valid_env_key(const std::string& key);
bool split_env_assignment(const std::string& arg, std::string& key, std::string& value);
std::string expand_config_vars(std::string val, const std::map<std::string, std::string>& vars);
bool is_safe_command_token(const std::string& token);
std::vector<std::string> split_command_words(const std::string& command);
fs::path expand_path(const std::string& path_str);
void load_config();

// shell引数 / command construction
std::string join_shell_args(const std::vector<std::string>& args);
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);
std::vector<std::string> ordered_pacman_args_excluding_targets(
        const ParsedCliArguments& parsed, const std::set<size_t>& excluded_target_token_indices);
std::string makepkg_install_command(const MakepkgBuildOptions& options);
std::string build_editor_command(const std::string& editor, const fs::path& target);

// pacman / repository補助
bool pacman_option_takes_value(const std::string& arg);
bool pacman_operation_requests_refresh(
        const std::string& operation, const std::vector<std::string>& flags);
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags);
std::optional<std::string> unsupported_source_sync_option(
        const ParsedCliArguments& parsed);
bool is_force_source(const std::string& pkg_name);
std::string get_package_env(const std::string& pkg_name);
std::string get_git_branch();
std::vector<std::string> split_lines(const std::string& text);
std::vector<fs::path> find_install_scripts(const fs::path& pkg_dir);
std::vector<std::string> git_changed_files(const std::string& range);
bool is_review_sensitive_file(const std::string& path);
void log_update_diff_guidance(const std::string& range);
void log_review_targets(const fs::path& pkg_dir, const std::vector<fs::path>& install_scripts);
void review_build_files(const fs::path& pkg_dir);
std::optional<std::string> read_srcinfo_version(const fs::path& pkg_dir);
UpdateCheckResult check_update_status(const std::string& pkg_name, const fs::path& pkg_dir);
bool aur_version_is_newer(const std::string& aur_version, const std::string& installed_version);
bool has_local_package_artifact(const fs::path& pkg_dir);
bool has_local_srcdir(const fs::path& pkg_dir);
MakepkgBuildOptions resolve_makepkg_build_options(
        const fs::path& pkg_dir, const SourceSyncOptions& source_sync_options);

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

// dependency分類 / recursive dependency tree
std::string provider_display(const ProvidedDependency& provider);
std::string dependency_display_name(const std::string& dependency, const std::string& package_name);
std::string dependency_kind_display(DependencyKind kind);
void print_recursive_dependency_node(const RecursiveDependencyNode& node, size_t indent);
void print_recursive_dependency_tree(const std::vector<RecursiveDependencyNode>& nodes);

// build plan / fetch plan
void add_unique_value(std::vector<std::string>& values, const std::string& value);
void print_build_plan(const BuildPlan& plan);
void print_dependency_group(const std::string& label, const std::vector<std::string>& dependencies);
void print_ambiguous_provider_group(
        const std::string& label, const std::vector<AmbiguousProvidedDependency>& dependencies);
void print_metadata_risk_group(const std::vector<BuildPlanMetadataRisk>& risks);
std::string aur_git_url_for_package_base(const std::string& package_base);
void print_fetch_plan(const BuildPlan& plan);
void fetch_aur_package_base(const std::string& package_base);

// source build / AUR install
void build_from_git(
        const std::string& pkg_name, const std::string& clone_name, const std::string& git_url,
        const std::string& custom_env, bool only_if_updated,
        const SourceSyncOptions& source_sync_options);
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
int cmd_deps(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_plan(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_fetch(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_export_pkgbuild_tree(const std::string& target);
int cmd_print_pkgbuild(const std::string& target);
int cmd_sync_search(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_info(
        const ParsedCliArguments& parsed, bool use_sudo, PackageSourceSelection source_selection);
int cmd_sync_install(
        const ParsedCliArguments& parsed, bool is_sys_upgrade,
        PackageSourceSelection source_selection);
int cmd_query_foreign_updates();
int cmd_build(const std::vector<std::string>& args);
int cmd_add_src(const std::vector<std::string>& args);
int cmd_edit_src(const std::vector<std::string>& targets);
void cmd_list_src();
int cmd_del_src(const std::vector<std::string>& targets);
void cmd_revert(const std::vector<std::string>& targets);
int cmd_clean();
int cmd_upgrade();

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

    // Config is read-only here; CLI parsing remains before default cache/log creation.
    try {
        load_config();
    } catch(const std::exception& e) {
        std::cerr << "Warning: Failed to load config: " << e.what() << std::endl;
    } catch(...) {
        std::cerr << "Warning: Failed to load config: unknown error." << std::endl;
    }

    std::optional<ParsedCliArguments> parsed_result = parse_cli_arguments(argc, argv);
    if(!parsed_result.has_value()) return 1;

    const ParsedCliArguments&        parsed = parsed_result.value();
    std::optional<PkgbuildExportMode> export_mode = pkgbuild_export_mode(parsed);
    if(export_mode.has_value()) {
        // POLICY(#167): export/print は cache log 初期化より前に分岐し、内部 build cache を作らない。
        Logger::set_diagnostics_to_stderr();
        if(!validate_pkgbuild_export_invocation(parsed)) return 1;

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
    if(!validate_source_selection_operation(parsed)) return 1;

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
            log_path = expand_path(g_config.log_file);
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

int main(int argc, char* argv[]) {
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

std::optional<JpackerGlobalOption> jpacker_global_option_kind(const std::string& arg) {
    auto option = std::find_if(
            JPACKER_GLOBAL_OPTIONS.begin(), JPACKER_GLOBAL_OPTIONS.end(),
            [&arg](const auto& entry) { return arg == entry.first; });
    if(option == JPACKER_GLOBAL_OPTIONS.end()) return std::nullopt;
    return option->second;
}

bool is_jpacker_global_option(const std::string& arg) {
    return jpacker_global_option_kind(arg).has_value();
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

bool apply_jpacker_global_option(const std::string& arg, ParsedCliArguments& parsed) {
    std::optional<JpackerGlobalOption> option = jpacker_global_option_kind(arg);
    if(!option.has_value()) throw std::logic_error("Unknown jpacker global option: " + arg);

    switch(option.value()) {
    case JpackerGlobalOption::NoEdit:
        g_config.no_edit = true;
        break;
    case JpackerGlobalOption::NoDiff:
        g_config.no_diff = true;
        break;
    case JpackerGlobalOption::NoConfirm:
        g_config.no_confirm = true;
        break;
    case JpackerGlobalOption::Rebuild:
        g_config.rebuild = true;
        break;
    case JpackerGlobalOption::CleanBuild:
        g_config.clean_build = true;
        break;
    case JpackerGlobalOption::RmDeps:
        g_config.rm_deps = true;
        break;
    case JpackerGlobalOption::Aur:
        if(parsed.source_selection == PackageSourceSelection::RepoOnly) {
            Logger::error("Cannot combine --aur and --repo.");
            return false;
        }
        parsed.source_selection = PackageSourceSelection::AurOnly;
        break;
    case JpackerGlobalOption::Repo:
        if(parsed.source_selection == PackageSourceSelection::AurOnly) {
            Logger::error("Cannot combine --aur and --repo.");
            return false;
        }
        parsed.source_selection = PackageSourceSelection::RepoOnly;
        break;
    }
    return true;
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

std::optional<ParsedCliArguments> parse_cli_arguments(int argc, char* argv[]) {
    ParsedCliArguments parsed;
    bool               has_operation = false;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg.empty()) {
            Logger::error("Empty arguments are not supported.");
            return std::nullopt;
        }

        if(!has_operation) {
            if(is_jpacker_global_option(arg)) {
                if(!apply_jpacker_global_option(arg, parsed)) return std::nullopt;
                parsed.tokens.push_back(
                        ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::JpackerGlobalOption});
                parsed.consumed_global_options.push_back(arg);
                continue;
            }

            has_operation = true;
            parsed.operation = arg;
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::Operation});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            continue;
        }

        // POLICY(#173): pacmanの構文状態を確定してから、通常位置のjpacker optionだけを消費する。
        if(parsed.pending_option.has_value()) {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::PacmanOptionValue});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            parsed.pending_option.reset();
            continue;
        }
        if(parsed.end_of_options) {
            size_t token_index = parsed.tokens.size();
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::OpaqueOperand});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.targets.push_back(arg);
            parsed.target_token_indices.push_back(token_index);
            continue;
        }
        if(arg == "--") {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::EndOfOptions});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            parsed.end_of_options = true;
            continue;
        }
        if(is_jpacker_global_option(arg)) {
            if(!apply_jpacker_global_option(arg, parsed)) return std::nullopt;
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::JpackerGlobalOption});
            parsed.consumed_global_options.push_back(arg);
            continue;
        }
        if(arg[0] == '-') {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::PacmanOption});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            if(pacman_option_takes_value(arg)) parsed.pending_option = arg;
            continue;
        }

        size_t token_index = parsed.tokens.size();
        parsed.tokens.push_back(
                ParsedCliToken{arg, static_cast<size_t>(i), CliTokenRole::Target});
        parsed.ordered_pacman_args.push_back(arg);
        parsed.targets.push_back(arg);
        parsed.target_token_indices.push_back(token_index);
    }

    if(!has_operation) {
        print_help();
        return std::nullopt;
    }
    if(parsed.pending_option.has_value()) {
        Logger::error("Missing value for option " + parsed.pending_option.value());
        return std::nullopt;
    }
    return parsed;
}

std::optional<PkgbuildExportMode> pkgbuild_export_mode(const ParsedCliArguments& parsed) {
    if(parsed.operation == "-G") return PkgbuildExportMode::Tree;
    if(parsed.operation == "-Gp") return PkgbuildExportMode::PkgbuildStdout;
    return std::nullopt;
}

bool validate_pkgbuild_export_invocation(const ParsedCliArguments& parsed) {
    // POLICY(#173): operation/target 以外の role は、綴りを再解釈せず元 argv 位置のまま拒否する。
    for(const auto& token : parsed.tokens) {
        if(token.role == CliTokenRole::Operation || token.role == CliTokenRole::Target) continue;

        Logger::error(
                "Unsupported option " + token.value + " for operation " + parsed.operation + ".");
        return false;
    }

    if(parsed.targets.size() != 1) {
        Logger::error(
                "Operation " + parsed.operation + " requires exactly one AUR package target.");
        Logger::error("Usage: jpacker " + parsed.operation + " <pkg>");
        return false;
    }

    return true;
}

bool parsed_has_semantic_pacman_option(
        const ParsedCliArguments& parsed, const std::string& option) {
    return std::any_of(parsed.tokens.begin(), parsed.tokens.end(), [&](const ParsedCliToken& token) {
        return token.role == CliTokenRole::PacmanOption && token.value == option;
    });
}

SourceSyncOptions parse_source_sync_options(const ParsedCliArguments& parsed) {
    SourceSyncOptions options;
    // POLICY(#173): option valueや`--`後のoperandをsemantic optionへ昇格させない。
    options.needed = parsed_has_semantic_pacman_option(parsed, "--needed");
    return options;
}

std::string package_source_selection_option(PackageSourceSelection selection) {
    switch(selection) {
    case PackageSourceSelection::Auto:
        return "automatic source selection";
    case PackageSourceSelection::AurOnly:
        return "--aur";
    case PackageSourceSelection::RepoOnly:
        return "--repo";
    }
    throw std::logic_error("Unknown package source selection.");
}

SourceSelectableSyncOperation source_selectable_sync_operation(const ParsedCliArguments& parsed) {
    if(!parsed.operation.starts_with("-S")) return SourceSelectableSyncOperation::Unsupported;

    bool has_search = false;
    bool has_info = false;
    bool has_unsupported_modifier = false;

    auto inspect_short_modifiers = [&](const std::string& option, size_t first_modifier) {
        if(option.size() <= first_modifier || option[0] != '-' || option.starts_with("--")) return;
        for(size_t i = first_modifier; i < option.size(); ++i) {
            switch(option[i]) {
            case 's':
                has_search = true;
                break;
            case 'i':
                has_info = true;
                break;
            case 'c':
            case 'g':
            case 'l':
            case 'u':
                has_unsupported_modifier = true;
                break;
            default:
                break;
            }
        }
    };

    inspect_short_modifiers(parsed.operation, 2);
    for(const auto& token : parsed.tokens) {
        if(token.role != CliTokenRole::PacmanOption) continue;
        if(token.value == "--search")
            has_search = true;
        else if(token.value == "--info")
            has_info = true;
        else if(token.value == "--clean" || token.value == "--groups" ||
                token.value == "--list" || token.value == "--sysupgrade")
            has_unsupported_modifier = true;
        else
            inspect_short_modifiers(token.value, 1);
    }

    if(has_unsupported_modifier || (has_search && has_info)) {
        return SourceSelectableSyncOperation::Unsupported;
    }
    if(has_search) return SourceSelectableSyncOperation::Search;
    if(has_info) return SourceSelectableSyncOperation::Info;
    return SourceSelectableSyncOperation::Install;
}

bool validate_source_selection_operation(const ParsedCliArguments& parsed) {
    if(parsed.source_selection == PackageSourceSelection::Auto) return true;

    const std::string selector = package_source_selection_option(parsed.source_selection);
    const bool requests_refresh = pacman_operation_requests_refresh(parsed.operation, parsed.flags);
    SourceSelectableSyncOperation sync_operation = source_selectable_sync_operation(parsed);

    if(parsed.source_selection == PackageSourceSelection::AurOnly && requests_refresh) {
        Logger::error(
                "Cannot combine --aur with pacman refresh for operation " + parsed.operation + ".");
        return false;
    }
    if(sync_operation == SourceSelectableSyncOperation::Unsupported ||
       (sync_operation == SourceSelectableSyncOperation::Install && requests_refresh)) {
        Logger::error(selector + " is not supported for operation " + parsed.operation + ".");
        return false;
    }
    return true;
}

// 文字列 / path / config
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool remote_url_matches_expected(const std::string& current_url, const std::string& expected_url) {
    // LANDMINE: cache directory の再利用可否を決める guard。曖昧一致にすると別 remote を上書きし得る。
    return trim(current_url) == trim(expected_url);
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return str;
}

std::string unquote(const std::string& str) {
    if(str.length() >= 2) {
        char first = str.front();
        char last = str.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return str.substr(1, str.length() - 2);
        }
    }
    return str;
}

std::string strip_comment(const std::string& line) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for(size_t i = 0; i < line.length(); ++i) {
        char ch = line[i];
        if(escaped) {
            escaped = false;
            continue;
        }
        if(ch == '\\' && in_double_quote) {
            escaped = true;
            continue;
        }
        if(ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if(ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if(ch == '#' && !in_single_quote && !in_double_quote) {
            return line.substr(0, i);
        }
    }
    return line;
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

bool is_valid_env_key(const std::string& key) {
    if(key.empty()) return false;
    if(!(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_')) return false;
    return std::all_of(key.begin() + 1, key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

bool split_env_assignment(const std::string& arg, std::string& key, std::string& value) {
    size_t eq_pos = arg.find('=');
    if(eq_pos == std::string::npos) return false;
    key = trim(arg.substr(0, eq_pos));
    value = unquote(trim(arg.substr(eq_pos + 1)));
    // POLICY: makepkg へ渡す環境変数名は shell identifier 相当に制限する。
    return is_valid_env_key(key);
}

std::string expand_config_vars(std::string val, const std::map<std::string, std::string>& vars) {
    std::regex  re_brace(R"(\$\{([A-Za-z0-9_]+)\})");
    std::regex  re_simple(R"(\$([A-Za-z0-9_]+))");
    std::smatch match;

    for(int i = 0; i < 32 && std::regex_search(val, match, re_brace); ++i) {
        std::string var_name = match[1];
        std::string replacement = vars.count(var_name) ? vars.at(var_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == val) break;
        val = next;
    }
    for(int i = 0; i < 32 && std::regex_search(val, match, re_simple); ++i) {
        std::string var_name = match[1];
        std::string replacement = vars.count(var_name) ? vars.at(var_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == val) break;
        val = next;
    }
    return val;
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

fs::path expand_path(const std::string& path_str) {
    if(path_str.empty()) return "";
    if(path_str[0] == '~') {
        const char* home = std::getenv("HOME");
        if(!home) throw std::runtime_error("HOME environment variable not set.");
        if(path_str.length() == 1) return fs::path(home);
        if(path_str[1] == '/') return fs::path(home) / path_str.substr(2);
        throw std::runtime_error("Unsupported home expansion: " + path_str);
    }
    return fs::path(path_str);
}


void load_config() {
    if(!fs::exists(CONFIG_FILE)) return;
    std::ifstream file(CONFIG_FILE);
    std::string   line;
    while(std::getline(file, line)) {
        line = strip_comment(line);
        if(trim(line).empty()) continue;
        std::stringstream ss(line);
        std::string       key, val;
        if(std::getline(ss, key, '=') && std::getline(ss, val)) {
            key = to_lower(trim(key));
            val = unquote(trim(val));
            if(key == "noedit") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") g_config.no_edit = true;
            } else if(key == "nodiff") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") g_config.no_diff = true;
            } else if(key == "editor") {
                if(!val.empty()) g_config.editor = val;
            } else if(key == "logfile") {
                if(!val.empty()) g_config.log_file = val;
            }
        }
    }
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

std::vector<std::string> ordered_pacman_args_excluding_targets(
        const ParsedCliArguments& parsed, const std::set<size_t>& excluded_target_token_indices) {
    std::vector<std::string> args;
    for(size_t i = 0; i < parsed.tokens.size(); ++i) {
        const ParsedCliToken& token = parsed.tokens[i];
        if(token.role == CliTokenRole::JpackerGlobalOption) continue;
        if((token.role == CliTokenRole::Target || token.role == CliTokenRole::OpaqueOperand) &&
           excluded_target_token_indices.contains(i)) {
            continue;
        }
        args.push_back(token.value);
    }
    return args;
}

std::string makepkg_install_command(const MakepkgBuildOptions& options) {
    std::vector<std::string> args = {"makepkg", "-sic"};
    if(g_config.no_confirm) args.push_back("--noconfirm");
    if(options.rebuild) args.push_back("-f");
    if(options.clean_build) args.push_back("-C");
    // POLICY(#123): 削除対象の判断と実行は makepkg -s/-r に委ね、jpacker では再実装しない。
    if(options.rm_deps) args.push_back("-r");
    // POLICY(#169): jpacker独自のbuild skip判定は追加せず、再install要否だけをmakepkg/pacmanへ委ねる。
    if(options.needed) args.push_back("--needed");
    return join_shell_args(args);
}

std::string build_editor_command(const std::string& editor, const fs::path& target) {
    std::vector<std::string> args = split_command_words(editor);
    args.push_back(target.string());
    return join_shell_args(args);
}

// pacman / repository補助
bool pacman_option_takes_value(const std::string& arg) {
    // POLICY: 固定の pacman option table。process lifetime の保持だが mutable な副作用は持たない。
    static const std::vector<std::string> s_long_opts = {
            "--arch", "--assume-installed", "--cachedir", "--color", "--config", "--dbpath",
            "--gpgdir", "--hookdir", "--ignore", "--ignoregroup", "--logfile", "--overwrite",
            "--print-format", "--root", "--sysroot"};
    static const std::vector<std::string> s_short_opts = {"-b", "-r"};

    if(arg.find('=') != std::string::npos) return false;
    if(std::find(s_long_opts.begin(), s_long_opts.end(), arg) != s_long_opts.end()) return true;
    return std::find(s_short_opts.begin(), s_short_opts.end(), arg) != s_short_opts.end();
}

bool pacman_operation_requests_refresh(
        const std::string& operation, const std::vector<std::string>& flags) {
    auto short_option_requests_refresh = [](const std::string& option) {
        if(option.size() < 2 || option[0] != '-' || option[1] == '-') return false;
        return std::find(option.begin() + 1, option.end(), 'y') != option.end();
    };

    if(short_option_requests_refresh(operation)) return true;

    bool option_value_expected = false;
    // POLICY(#172): flags[0] は operation。残りは元の option 順で、値を取る option の次は判定対象外にする。
    for(size_t i = 1; i < flags.size(); ++i) {
        const std::string& flag = flags[i];
        if(option_value_expected) {
            option_value_expected = false;
            continue;
        }
        if(flag == "--") break;
        if(flag == "--refresh" || short_option_requests_refresh(flag)) return true;
        option_value_expected = pacman_option_takes_value(flag);
    }
    return false;
}

bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == operation) continue;

        Logger::error("Unsupported " + operation + " option: " + flag);
        return false;
    }
    return true;
}

std::optional<std::string> unsupported_source_sync_option(
        const ParsedCliArguments& parsed) {
    // POLICY(#56): -S の y/u modifier は official repository update にだけ作用する。
    // AUR/source-build 側へ意味を移せない他の pacman option は、黙って無視せず build 前に止める。
    const std::string& operation = parsed.operation;
    if(operation.size() < 2 || operation[0] != '-' || operation[1] != 'S' ||
       !std::all_of(operation.begin() + 2, operation.end(), [](char modifier) {
           return modifier == 'y' || modifier == 'u';
       })) {
        return operation;
    }

    for(const auto& token : parsed.tokens) {
        if(token.role != CliTokenRole::PacmanOption) continue;
        if(token.value == "--needed") continue;
        return token.value;
    }
    return std::nullopt;
}

bool is_force_source(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    fs::path target = fs::path(PACKAGE_BUILD_DIR) / pkg_name;
    return fs::exists(target);
}

std::string get_package_env(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg_name;
    if(!fs::exists(p)) return "";
    std::ifstream                      file(p);
    std::string                        line;
    std::string                        env_str = "";
    std::map<std::string, std::string> vars;
    Logger::info("Loading custom build flags from " + p.string());
    while(std::getline(file, line)) {
        line = strip_comment(line);
        if(trim(line).empty()) continue;
        std::string key, val;
        if(split_env_assignment(line, key, val)) {
            try {
                val = expand_config_vars(val, vars);
            } catch(const std::exception& e) {
                Logger::warn("Failed to expand variables for " + key + ": " + e.what());
            }
            vars[key] = val;
            if(!val.empty()) {
                env_str += key + "=" + shell_quote(val) + " ";
            }
        } else if(line.find('=') != std::string::npos) {
            Logger::warn("Ignoring invalid environment assignment: " + trim(line));
        }
    }
    return env_str;
}

std::string get_git_branch() {
    std::string remote_head = exec_command("git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null");
    const std::string prefix = "origin/";
    if(remote_head.starts_with(prefix) && remote_head.length() > prefix.length()) {
        return remote_head.substr(prefix.length());
    }
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/main") == 0) return "main";
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/master") == 0) return "master";
    return "master";
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream        stream(text);
    std::string              line;
    while(std::getline(stream, line)) {
        line = trim(line);
        if(!line.empty()) lines.push_back(line);
    }
    return lines;
}

namespace {

bool has_safe_persistent_checkout_git_directory(const fs::path& pkg_dir) {
    fs::path        git_path = pkg_dir / ".git";
    std::error_code ec;
    fs::file_status status = fs::symlink_status(git_path, ec);
    if(ec == std::errc::no_such_file_or_directory) return false;
    if(ec) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": non-directory .git (inspection failed: " + ec.message() + ").");
    }
    if(status.type() == fs::file_type::not_found) return false;
    if(fs::is_symlink(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() + ": symlink.");
    }
    if(fs::is_regular_file(status)) {
        // POLICY(#197): persistent checkout では gitfile/worktree redirect を対応対象にしない。
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": gitfile / redirect.");
    }
    if(!fs::is_directory(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": non-directory .git.");
    }
    return true;
}

void require_safe_persistent_checkout_git_directory(const fs::path& pkg_dir) {
    if(has_safe_persistent_checkout_git_directory(pkg_dir)) return;

    fs::path git_path = pkg_dir / ".git";
    throw std::runtime_error(
            "Unsafe persistent checkout descendant " + git_path.string() +
            ": non-directory .git.");
}

void require_safe_persistent_checkout_artifact(const fs::path& artifact_path) {
    std::error_code ec;
    fs::file_status status = fs::symlink_status(artifact_path, ec);
    if(ec == std::errc::no_such_file_or_directory) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file.");
    }
    if(ec) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file (inspection failed: " + ec.message() + ").");
    }
    if(fs::is_symlink(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() + ": symlink.");
    }
    if(!fs::is_regular_file(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file.");
    }
}

std::vector<fs::path> require_safe_persistent_checkout_descendants(const fs::path& pkg_dir) {
    // POLICY(#197): descendant の契約は generic cache path ではなく persistent checkout consumer が持つ。
    require_safe_persistent_checkout_git_directory(pkg_dir);
    require_safe_persistent_checkout_artifact(pkg_dir / "PKGBUILD");
    return find_install_scripts(pkg_dir);
}

void require_safe_persistent_checkout_review_targets(
        const fs::path& pkg_dir, const std::vector<fs::path>& install_scripts) {
    // LANDMINE(#197): 再列挙だけでは、review開始後に消えた既存targetを見落とす。
    require_safe_persistent_checkout_descendants(pkg_dir);
    for(const auto& install_script : install_scripts) {
        require_safe_persistent_checkout_artifact(pkg_dir / install_script);
    }
}

} // namespace

std::vector<fs::path> find_install_scripts(const fs::path& pkg_dir) {
    std::vector<fs::path> scripts;
    std::error_code       ec;
    fs::directory_iterator entry(pkg_dir, ec);
    if(ec) {
        throw std::runtime_error(
                "Failed to inspect persistent checkout artifacts in " + pkg_dir.string() + ": " +
                ec.message());
    }

    const fs::directory_iterator end;
    while(entry != end) {
        fs::path artifact_path = entry->path();
        // POLICY(#197): 名前を先に対象化し、symlinkやspecial fileも検証対象から落とさない。
        if(artifact_path.extension() == ".install") {
            require_safe_persistent_checkout_artifact(artifact_path);
            scripts.push_back(artifact_path.filename());
        }

        entry.increment(ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to inspect persistent checkout artifacts in " + pkg_dir.string() + ": " +
                    ec.message());
        }
    }
    std::sort(scripts.begin(), scripts.end());
    return scripts;
}

std::vector<std::string> git_changed_files(const std::string& range) {
    std::string cmd = "git diff --name-only " + shell_quote(range) + " 2>/dev/null";
    return split_lines(exec_command(cmd.c_str()));
}

bool is_review_sensitive_file(const std::string& path) {
    fs::path file_path(path);
    return file_path.filename() == "PKGBUILD" || file_path.extension() == ".install";
}

void log_update_diff_guidance(const std::string& range) {
    std::vector<std::string> changed_files = git_changed_files(range);
    if(changed_files.empty()) return;

    Logger::info("Update diff range: " + range + " (existing cache repository).");

    std::vector<std::string> review_sensitive_files;
    for(const auto& file : changed_files) {
        if(is_review_sensitive_file(file)) review_sensitive_files.push_back(file);
    }
    if(!review_sensitive_files.empty()) {
        Logger::warn("Review-sensitive file changes: " + join_comma_display_values(review_sensitive_files));
    }
}

void log_review_targets(const fs::path& pkg_dir, const std::vector<fs::path>& install_scripts) {
    Logger::info("Review target: PKGBUILD");
    if(install_scripts.empty()) return;

    std::vector<std::string> names;
    for(const auto& script : install_scripts) {
        names.push_back(script.string());
    }
    // POLICY: PKGBUILD はここで評価しない。作業ツリーにある *.install だけを、見落とし防止として案内する。
    Logger::warn("Install script(s) present; review before build: " + join_comma_display_values(names));
    Logger::info("Review directory: " + pkg_dir.string());
}

void review_build_files(const fs::path& pkg_dir) {
    std::vector<fs::path> install_scripts =
            require_safe_persistent_checkout_descendants(pkg_dir);

    if(g_config.no_edit) {
        Logger::info("Skipping PKGBUILD/.install review (--noedit).");
        return;
    }

    log_review_targets(pkg_dir, install_scripts);

    const char* env_editor = std::getenv("EDITOR");
    std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;
    bool        edited = false;

    if(ask_user("Edit PKGBUILD?", PromptDefault::No)) {
        require_safe_persistent_checkout_review_targets(pkg_dir, install_scripts);
        if(run_command(build_editor_command(editor_cmd, "PKGBUILD")) != 0) {
            throw std::runtime_error("Editor failed.");
        }
        require_safe_persistent_checkout_review_targets(pkg_dir, install_scripts);
        edited = true;
    }

    for(const auto& install_script : install_scripts) {
        if(ask_user("Edit install script " + install_script.string() + "?", PromptDefault::No)) {
            require_safe_persistent_checkout_review_targets(pkg_dir, install_scripts);
            if(run_command(build_editor_command(editor_cmd, install_script)) != 0) {
                throw std::runtime_error("Editor failed.");
            }
            require_safe_persistent_checkout_review_targets(pkg_dir, install_scripts);
            edited = true;
        }
    }

    // LANDMINE(#197): editor はreview対象を置換できるため、review開始時の検証結果を持ち越さない。
    require_safe_persistent_checkout_review_targets(pkg_dir, install_scripts);
    if(edited && !ask_user("Proceed with build?", PromptDefault::Yes)) throw std::runtime_error("Aborted.");
}

std::optional<std::string> read_srcinfo_version(const fs::path& pkg_dir) {
    fs::path        srcinfo_path = pkg_dir / ".SRCINFO";
    std::error_code ec;
    if(!fs::is_regular_file(srcinfo_path, ec) || ec) return std::nullopt;

    std::ifstream file(srcinfo_path);
    if(!file) return std::nullopt;

    std::string pkgver;
    std::string pkgrel;
    std::string line;
    while(std::getline(file, line)) {
        std::string trimmed = trim(line);
        if(trimmed.starts_with("pkgver =")) {
            pkgver = trim(trimmed.substr(trimmed.find('=') + 1));
        } else if(trimmed.starts_with("pkgrel =")) {
            pkgrel = trim(trimmed.substr(trimmed.find('=') + 1));
        }
    }

    if(pkgver.empty() || pkgrel.empty()) return std::nullopt;
    return pkgver + "-" + pkgrel;
}

UpdateCheckResult check_update_status(const std::string& pkg_name, const fs::path& pkg_dir) {
    require_valid_package_name(pkg_name);
    std::string installed_full = exec_command(("pacman -Q " + shell_quote(pkg_name) + " 2>/dev/null").c_str());
    if(installed_full.empty()) {
        return UpdateCheckResult::NeedsBuild;// インストールされていないのでビルド必要
    }
    size_t      space_pos = installed_full.find(' ');
    std::string installed_ver = (space_pos != std::string::npos) ? installed_full.substr(space_pos + 1) : "";

    // POLICY: upgrade の pre-review 更新判定では PKGBUILD を評価しない。
    // 既存 .SRCINFO が読めない場合は呼び出し元で対話確認または skip へ進める。
    std::optional<std::string> new_ver = read_srcinfo_version(pkg_dir);
    if(!new_ver.has_value()) return UpdateCheckResult::Unknown;

    std::string cmp_cmd = "vercmp " + shell_quote(new_ver.value()) + " " + shell_quote(installed_ver) + " 2>/dev/null";
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    try {
        if(std::stoi(cmp_res) > 0) return UpdateCheckResult::NeedsBuild;
    } catch(...) {
        return UpdateCheckResult::Unknown;
    }

    Logger::info(pkg_name + " is up to date (" + installed_ver + "). Skipping.");
    return UpdateCheckResult::UpToDate;
}

bool aur_version_is_newer(const std::string& aur_version, const std::string& installed_version) {
    std::string cmp_cmd = "vercmp " + shell_quote(aur_version) + " " + shell_quote(installed_version);
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    try {
        return std::stoi(cmp_res) > 0;
    } catch(...) {
        Logger::warn("Failed to compare versions: " + installed_version + " -> " + aur_version);
        return false;
    }
}

bool has_local_package_artifact(const fs::path& pkg_dir) {
    if(!fs::exists(pkg_dir) || !fs::is_directory(pkg_dir)) return false;

    for(const auto& entry : fs::directory_iterator(pkg_dir)) {
        if(!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if(filename.size() >= 4 && filename.substr(filename.size() - 4) == ".sig") continue;
        if(filename.find(".pkg.tar") != std::string::npos) return true;
    }
    return false;
}

bool has_local_srcdir(const fs::path& pkg_dir) {
    fs::path src_dir = pkg_dir / "src";
    return fs::exists(src_dir) && fs::is_directory(src_dir);
}

MakepkgBuildOptions resolve_makepkg_build_options(
        const fs::path& pkg_dir, const SourceSyncOptions& source_sync_options) {
    MakepkgBuildOptions options;
    bool                has_artifact = has_local_package_artifact(pkg_dir);

    options.rm_deps = g_config.rm_deps;
    options.needed = source_sync_options.needed;

    if(g_config.clean_build) {
        options.clean_build = true;
    } else if(has_local_srcdir(pkg_dir)) {
        options.clean_build = ask_user("Clean build existing build directory?", PromptDefault::No);
    }

    if(g_config.rebuild) {
        options.rebuild = true;
    } else if(options.clean_build && has_artifact) {
        options.rebuild = true;
    } else if(has_artifact) {
        options.rebuild = ask_user("Rebuild package?", PromptDefault::No);
    }

    return options;
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

// dependency分類 / recursive dependency tree
std::string provider_display(const ProvidedDependency& provider) {
    return provider.repository + "/" + provider.package_name;
}

std::string dependency_display_name(const std::string& dependency, const std::string& package_name) {
    std::string display;
    if(package_name.empty() || dependency == package_name)
        display = dependency;
    else
        display = dependency + " (" + package_name + ")";
    return dependency_display_with_constraint_note(display, dependency);
}

std::string dependency_kind_display(DependencyKind kind) {
    switch(kind) {
    case DependencyKind::Repo:
        return "repo";
    case DependencyKind::Aur:
        return "aur";
    case DependencyKind::Provided:
        return "provided";
    case DependencyKind::AmbiguousProvider:
        return "ambiguous-provider";
    case DependencyKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

void print_recursive_dependency_node(const RecursiveDependencyNode& node, size_t indent) {
    std::cout << std::string(indent, ' ') << "- "
              << dependency_display_name(node.dependency, node.package_name) << " ["
              << dependency_kind_display(node.kind) << "]";
    if(node.kind == DependencyKind::Aur && !node.package_base.empty() && node.package_base != node.package_name) {
        std::cout << " base: " << node.package_base;
    }
    if(node.provided_by.has_value()) {
        std::cout << " by " << node.provided_by->repository << "/" << node.provided_by->package_name;
    }
    if(!node.provider_candidates.empty()) {
        std::cout << " candidates: ";
        for(size_t i = 0; i < node.provider_candidates.size(); ++i) {
            if(i > 0) std::cout << ", ";
            std::cout << provider_display(node.provider_candidates[i]);
        }
    }
    if(node.already_visited) std::cout << " (already visited)";
    if(node.max_depth_reached) std::cout << " (max depth reached)";
    std::cout << std::endl;

    for(const auto& child : node.children) {
        print_recursive_dependency_node(child, indent + 2);
    }
}

void print_recursive_dependency_tree(const std::vector<RecursiveDependencyNode>& nodes) {
    std::cout << "Recursive dependency tree:" << std::endl;
    if(nodes.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }
    for(const auto& node : nodes) {
        print_recursive_dependency_node(node, 2);
    }
}

// build plan / fetch plan
void add_unique_value(std::vector<std::string>& values, const std::string& value) {
    std::string trimmed = trim(value);
    if(trimmed.empty()) return;
    if(std::find(values.begin(), values.end(), trimmed) == values.end()) values.push_back(trimmed);
}

void print_metadata_risk_group(const std::vector<BuildPlanMetadataRisk>& risks) {
    std::cout << "Metadata conflicts/replaces:" << std::endl;
    for(const auto& risk : risks) {
        std::cout << "  " << risk.package_name;
        if(risk.package_base != risk.package_name) std::cout << " (base: " << risk.package_base << ")";
        std::cout << std::endl;
        if(!risk.conflicts.empty())
            std::cout << "    conflicts: " << join_comma_display_values(risk.conflicts) << std::endl;
        if(!risk.replaces.empty())
            std::cout << "    replaces: " << join_comma_display_values(risk.replaces) << std::endl;
    }
}

void print_build_plan(const BuildPlan& plan) {
    std::cout << "Build plan:" << std::endl;
    if(plan.order.empty()) {
        std::cout << "  None" << std::endl;
    } else {
        for(size_t i = 0; i < plan.order.size(); ++i) {
            const BuildPlanEntry& entry = plan.order[i];
            std::cout << "  " << (i + 1) << ". " << entry.package_base;
            std::cout << std::endl;
            std::vector<std::string> distinct_targets;
            for(const auto& package_name : entry.package_names) {
                if(package_name != entry.package_base) add_unique_value(distinct_targets, package_name);
            }
            if(!distinct_targets.empty()) {
                std::cout << "     target package";
                if(distinct_targets.size() > 1) std::cout << "s";
                std::cout << ": " << join_comma_display_values(distinct_targets) << std::endl;
            }
        }
    }

    if(!plan.provided.empty()) {
        std::cout << std::endl;
        std::cout << "Provided dependencies:" << std::endl;
        for(const auto& dependency : plan.provided) {
            std::cout << "  - "
                      << dependency_display_with_constraint_note(dependency.dependency, dependency.dependency)
                      << " -> " << dependency.provider.repository << "/" << dependency.provider.package_name
                      << std::endl;
        }
    }

    if(!plan.ambiguous_providers.empty()) {
        std::cout << std::endl;
        print_ambiguous_provider_group("Ambiguous provided dependencies:", plan.ambiguous_providers);
    }

    if(!plan.split_package_targets.empty()) {
        std::cout << std::endl;
        std::cout << "Split package install targets:" << std::endl;
        for(const auto& target : plan.split_package_targets) {
            std::cout << "  - " << target.package_name << " (base: " << target.package_base << ")" << std::endl;
        }
    }

    if(!plan.metadata_risks.empty()) {
        std::cout << std::endl;
        print_metadata_risk_group(plan.metadata_risks);
    }

    if(!plan.unresolved.empty()) {
        std::cout << std::endl;
        std::cout << "Unresolved dependencies:" << std::endl;
        for(const auto& dependency : plan.unresolved) {
            std::cout << "  - " << dependency << std::endl;
        }
    }

    if(!plan.cycles.empty()) {
        std::cout << std::endl;
        std::cout << "Cyclic dependencies:" << std::endl;
        for(const auto& dependency : plan.cycles) {
            std::cout << "  - " << dependency << std::endl;
        }
    }

    if(!plan.unresolved.empty() || !plan.ambiguous_providers.empty() || !plan.cycles.empty() ||
       !plan.split_package_targets.empty() || !plan.metadata_risks.empty()) {
        std::cout << std::endl;
        std::cout << "Plan status: incomplete" << std::endl;
        if(!plan.unresolved.empty()) std::cout << "  unresolved dependencies remain" << std::endl;
        if(!plan.ambiguous_providers.empty()) std::cout << "  ambiguous providers are not selected" << std::endl;
        if(!plan.cycles.empty()) std::cout << "  cyclic dependencies detected" << std::endl;
        if(!plan.split_package_targets.empty())
            std::cout << "  split package install target selection is not implemented" << std::endl;
        if(!plan.metadata_risks.empty())
            std::cout << "  conflicts/replaces metadata is not resolved automatically" << std::endl;
    }
}

void print_dependency_group(const std::string& label, const std::vector<std::string>& dependencies) {
    std::cout << label << std::endl;
    if(dependencies.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }
    for(const auto& dep : dependencies) {
        std::cout << "  " << dep << std::endl;
    }
}

void print_ambiguous_provider_group(
        const std::string& label, const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::cout << label << std::endl;
    if(dependencies.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }

    for(const auto& dependency : dependencies) {
        std::cout << "  " << dependency_display_with_constraint_note(dependency.dependency, dependency.dependency)
                  << std::endl;
        std::cout << "    candidates:" << std::endl;
        for(size_t i = 0; i < dependency.candidates.size(); ++i) {
            std::cout << "      " << (i + 1) << ". " << provider_display(dependency.candidates[i]) << std::endl;
        }
    }
}

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}


void print_fetch_plan(const BuildPlan& plan) {
    std::cout << "Fetch targets:" << std::endl;
    if(plan.order.empty()) {
        std::cout << "  None" << std::endl;
    } else {
        for(size_t i = 0; i < plan.order.size(); ++i) {
            const BuildPlanEntry& entry = plan.order[i];
            std::cout << "  " << (i + 1) << ". " << entry.package_base << " -> "
                      << aur_git_url_for_package_base(entry.package_base) << std::endl;
        }
    }

    if(!plan.unresolved.empty()) {
        std::cout << std::endl;
        std::cout << "Unresolved dependencies:" << std::endl;
        for(const auto& dependency : plan.unresolved) {
            Logger::warn(dependency);
        }
    }

    if(!plan.ambiguous_providers.empty()) {
        std::cout << std::endl;
        print_ambiguous_provider_group("Ambiguous provided dependencies:", plan.ambiguous_providers);
    }

    if(!plan.cycles.empty()) {
        std::cout << std::endl;
        std::cout << "Cyclic dependencies:" << std::endl;
        for(const auto& dependency : plan.cycles) {
            Logger::warn(dependency);
        }
    }

    if(!plan.metadata_risks.empty()) {
        std::cout << std::endl;
        print_metadata_risk_group(plan.metadata_risks);
        Logger::warn(
                "Conflicts/replaces metadata requires manual review before build/install; fetch is allowed.");
    }
}

void fetch_aur_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    ValidatedCacheRoot cache_root = prepare_trusted_cache_root();
    ValidatedCachePath repo_path = require_trusted_cache_path(
            cache_root, package_base,
            CachePathRequirement::ExistingOrMissing);
    std::string git_url = aur_git_url_for_package_base(package_base);

    if(repo_path.exists()) {
        if(!repo_path.is_directory()) {
            throw std::runtime_error(repo_path.path().string() + " exists but is not a directory.");
        }
        require_safe_persistent_checkout_descendants(repo_path.canonical_path());

        // POLICY: fetch command は既存 clone で git fetch まで。worktree update/pull/reset/build/install はしない。
        WorkDirGuard wd_repo(repo_path);
        std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, git_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ": " + current_url);
        }

        Logger::info("Fetching " + package_base + "...");
        repo_path = revalidate_trusted_cache_path(
                repo_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(repo_path.canonical_path());
        if(run_command("git fetch origin") != 0) throw std::runtime_error("Failed to fetch " + package_base + ".");
        return;
    }

    Logger::info("Cloning " + package_base + "...");
    ValidatedCachePath clone_path =
            revalidate_trusted_cache_path(repo_path, CachePathRequirement::Missing);
    WorkDirGuard    wd_cache(cache_root);
    DirCleanupGuard cleanup_guard(clone_path);
    if(run_command("git clone " + shell_quote(git_url) + " " + shell_quote(package_base)) != 0) {
        throw std::runtime_error("Failed to clone " + package_base + ".");
    }

    // POLICY(#175): clone が生成した entry も、成功扱いする前に同じ cache boundary で検証する。
    ValidatedCachePath cloned_path = require_trusted_cache_path(
            cache_root, package_base, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(cloned_path.canonical_path());
    {
        WorkDirGuard wd_repo(cloned_path);
        std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, git_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ": " + current_url);
        }
    }
    cleanup_guard.commit();
}

// source build / AUR install
void build_from_git(
        const std::string& pkg_name, const std::string& clone_name, const std::string& git_url,
        const std::string& custom_env, bool only_if_updated,
        const SourceSyncOptions& source_sync_options) {
    require_valid_package_name(pkg_name);
    require_valid_package_name(clone_name);
    Logger::info("Processing " + pkg_name + "...");
    ValidatedCacheRoot build_root = prepare_trusted_cache_root();
    ValidatedCachePath pkg_path = require_trusted_cache_path(
            build_root, clone_name,
            CachePathRequirement::ExistingOrMissing);

    {
        WorkDirGuard wd(build_root);
        bool         needs_clone = true;

        if(pkg_path.exists() && pkg_path.is_directory() &&
           has_safe_persistent_checkout_git_directory(pkg_path.canonical_path())) {
            require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
            {
                WorkDirGuard wd_repo(pkg_path);
                std::string  current_url = exec_command("git config --get remote.origin.url");
                if(!remote_url_matches_expected(current_url, git_url)) {
                    Logger::warn("Remote URL mismatch. Re-cloning...");
                } else {
                    needs_clone = false;
                }
            }

            if(!needs_clone) {
                Logger::info("Updating repository...");
                WorkDirGuard wd_repo(pkg_path);
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
                if(run_command("git fetch origin") != 0) throw std::runtime_error("Failed to fetch updates.");

                std::string branch = get_git_branch();
                Logger::info("Detected branch: " + branch);

                if(!g_config.no_diff) {
                    std::string remote_ref = "origin/" + branch;
                    int diff_ret = run_command("git diff --quiet " + shell_quote("HEAD.." + remote_ref));
                    if(diff_ret > 1) {
                        throw std::runtime_error("Failed to compare repository changes.");
                    }
                    if(diff_ret == 1) {
                        log_update_diff_guidance("HEAD.." + remote_ref);
                        if(ask_user("Updates detected in existing cache repository. View git diff?", PromptDefault::No)) {
                            run_command("git diff " + shell_quote("HEAD.." + remote_ref) + " --color=always");
                        }
                    }
                }

                // LANDMINE: reset は build/install 経路だけで許可する。fetch 経路へ持ち込まない。
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
                if(run_command("git reset --hard " + shell_quote("origin/" + branch)) != 0) {
                    throw std::runtime_error("Failed to reset repository.");
                }
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
            }
        }

        if(needs_clone) {
            if(pkg_path.exists()) {
                // POLICY(#175): remote mismatch/non-repository cleanup is limited to the validated cache entry.
                remove_trusted_cache_path(pkg_path);
            }
            pkg_path = require_trusted_cache_path(
                    build_root, clone_name, CachePathRequirement::Missing);
            Logger::info("Cloning repository...");
            DirCleanupGuard cleanup_guard(pkg_path);
            if(run_command("git clone " + shell_quote(git_url) + " " + shell_quote(clone_name)) != 0) {
                throw std::runtime_error("Failed to clone " + clone_name);
            }

            pkg_path = require_trusted_cache_path(
                    build_root, clone_name, CachePathRequirement::ExistingDirectory);
            require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
            {
                WorkDirGuard wd_repo(pkg_path);
                std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
                if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + clone_name + ".");
                if(!remote_url_matches_expected(current_url, git_url)) {
                    throw std::runtime_error("Remote URL mismatch for " + clone_name + ": " + current_url);
                }
            }
            cleanup_guard.commit();
        }
    }

    {
        pkg_path = revalidate_trusted_cache_path(
                pkg_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
        WorkDirGuard wd(pkg_path);

        if(only_if_updated) {
            UpdateCheckResult update_check = check_update_status(pkg_name, pkg_path.canonical_path());
            if(update_check == UpdateCheckResult::UpToDate) {
                return;// 更新不要なので終了
            }
            if(update_check == UpdateCheckResult::Unknown) {
                Logger::warn("Unable to determine update status from .SRCINFO for " + pkg_name + ".");
                Logger::warn("Skipping pre-review PKGBUILD evaluation.");
                if(g_config.no_confirm) {
                    Logger::warn("Skipping " + pkg_name + ": update status is unknown and --noconfirm is set.");
                    return;
                }
                if(!isatty(STDIN_FILENO)) {
                    Logger::warn("Skipping " + pkg_name + ": update status is unknown and stdin is non-interactive.");
                    return;
                }
                if(!ask_user("Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?",
                            PromptDefault::No)) {
                    return;
                }
            }
        }

        review_build_files(pkg_path.canonical_path());
        MakepkgBuildOptions makepkg_options =
                resolve_makepkg_build_options(pkg_path.canonical_path(), source_sync_options);
        std::string build_cmd;
        if(!trim(custom_env).empty()) {
            Logger::info("Applying custom build flags: " + custom_env);
            build_cmd = custom_env + makepkg_install_command(makepkg_options);
        } else {
            Logger::info("Using default makepkg.conf settings.");
            build_cmd = makepkg_install_command(makepkg_options);
        }
        // LANDMINE(#175,#197): review 後は cache entry と build artifact の両方を再検証する。
        pkg_path = revalidate_trusted_cache_path(
                pkg_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(pkg_path.canonical_path());
        if(run_command(build_cmd) != 0) throw std::runtime_error("Build failed.");
    }
}

void install_smart_source(
        const std::string& pkg_name, bool only_if_updated,
        const SourceSyncOptions& source_sync_options) {
    std::string env = get_package_env(pkg_name);
    PackageBuildSource source = resolve_build_source(pkg_name);
    require_executable_build_source_plan(source);

    build_from_git(
            source.requested_name, source.clone_name, source.git_url, env, only_if_updated,
            source_sync_options);
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
            env = get_package_env(pkg_name);
            if(env.empty() && pkg_name != entry.package_base) env = get_package_env(entry.package_base);
        }

        try {
            build_from_git(
                    pkg_name, entry.package_base, aur_git_url_for_package_base(entry.package_base),
                    env, false, source_sync_options);
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
    if(!fs::exists(PACKAGE_BUILD_DIR)) return;

    // POLICY(#174): upgradeの既存pacman-first実行は維持するが、schema violationだけは
    // system transactionより前に全source packageのplanを横断して拒否する。
    for(const auto& entry : fs::directory_iterator(PACKAGE_BUILD_DIR)) {
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
int cmd_deps(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    bool recursive = false;
    for(const auto& flag : flags) {
        if(flag == "deps") continue;
        if(flag == "--recursive") {
            recursive = true;
            continue;
        }
        Logger::error("Unsupported deps option: " + flag);
        Logger::error("Usage: jpacker deps [--recursive] <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker deps [--recursive] <pkg>");
        return 1;
    }

    bool failed = false;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            std::optional<AurPackageInfo> info = AurClient::info(target);
            if(!info.has_value()) {
                Logger::error("AUR package not found: " + target);
                failed = true;
                continue;
            }

            std::vector<std::string> dependencies = collect_build_dependencies(info.value());
            DependencyClassification classified = classify_dependencies(dependencies);

            if(i > 0) std::cout << std::endl;
            std::cout << "Package         : " << info->Name << std::endl;
            std::cout << "Package Base    : " << info->PackageBase << std::endl;
            std::cout << "Dependencies    : " << dependencies.size() << std::endl;
            std::cout << std::endl;
            print_dependency_group("Official repo dependencies:", classified.repo);
            std::cout << std::endl;
            print_dependency_group("AUR dependencies:", classified.aur);
            std::cout << std::endl;
            print_dependency_group("Provided dependencies:", classified.provided);
            std::cout << std::endl;
            print_ambiguous_provider_group("Ambiguous provided dependencies:", classified.ambiguous_providers);
            std::cout << std::endl;
            print_dependency_group("Unknown dependencies:", classified.unknown);
            std::vector<BuildPlanMetadataRisk> metadata_risks =
                    collect_build_plan_metadata_risks(info.value());
            if(!metadata_risks.empty()) {
                std::cout << std::endl;
                print_metadata_risk_group(metadata_risks);
                Logger::warn("Conflicts/replaces metadata is separate from dependency resolution and requires manual review.");
            }
            if(recursive) {
                std::vector<RecursiveDependencyNode> recursive_nodes =
                        resolve_recursive_dependencies(info.value());
                std::cout << std::endl;
                print_recursive_dependency_tree(recursive_nodes);
            }
        } catch(const std::exception& e) {
            Logger::error("Failed to inspect dependencies for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_plan(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == "plan") continue;
        Logger::error("Unsupported plan option: " + flag);
        Logger::error("Usage: jpacker plan <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker plan <pkg>");
        return 1;
    }

    bool failed = false;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_build_plan(target);

            if(i > 0) std::cout << std::endl;
            print_build_plan(plan);
        } catch(const std::exception& e) {
            Logger::error("Failed to plan build order for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_fetch(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == "fetch") continue;
        Logger::error("Unsupported fetch option: " + flag);
        Logger::error("Usage: jpacker fetch <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker fetch <pkg>");
        return 1;
    }

    bool                                           failed = false;
    std::vector<std::pair<std::string, BuildPlan>> plans;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_fetch_plan(target);

            if(i > 0) std::cout << std::endl;
            print_fetch_plan(plan);
            // POLICY(#150): fetch は read-only retrieval stage。metadata risk は表示するが取得を妨げない。
            require_fetchable_build_plan(target, plan);
            plans.emplace_back(target, std::move(plan));
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch repositories for " + target + ": " + e.what());
            failed = true;
        }
    }

    // POLICY(#174): 全targetのschema/semantic preflightが成功するまでclone/fetchへ進まない。
    if(failed) return 1;

    for(const auto& [target, plan] : plans) {
        for(const auto& entry : plan.order) {
            try {
                fetch_aur_package_base(entry.package_base);
            } catch(const std::exception& e) {
                Logger::error("Failed to fetch repositories for " + target + ": " + e.what());
                failed = true;
            }
        }
    }

    return failed ? 1 : 0;
}

int cmd_export_pkgbuild_tree(const std::string& target) {
    export_pkgbuild_tree(target);
    return 0;
}

int cmd_print_pkgbuild(const std::string& target) {
    std::string pkgbuild = load_pkgbuild_for_stdout(target);
    if(pkgbuild.size() >
       static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("PKGBUILD is too large to write to stdout.");
    }

    // POLICY(#167/#196): moduleがtemporary checkoutをcleanupしてから返したbytesだけを出力する。
    std::cout.write(pkgbuild.data(), static_cast<std::streamsize>(pkgbuild.size()));
    std::cout.flush();
    if(!std::cout) {
        throw std::runtime_error("Failed to write PKGBUILD to stdout.");
    }
    return 0;
}

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

int cmd_query_foreign_updates() {
    bool failed = false;

    std::vector<InstalledPackage> packages = get_foreign_packages();
    if(packages.empty()) {
        Logger::info("No foreign packages found.");
        return 0;
    }

    std::vector<std::string> package_names;
    for(const auto& local_pkg : packages) {
        package_names.push_back(local_pkg.name);
    }

    Logger::info("Checking AUR updates for " + std::to_string(packages.size()) + " foreign packages...");

    std::map<std::string, AurPackageInfo> aur_packages;
    const size_t                          batch_size = 100;
    for(size_t offset = 0; offset < package_names.size(); offset += batch_size) {
        size_t end = std::min(offset + batch_size, package_names.size());
        Logger::info("Fetching AUR info for packages " + std::to_string(offset + 1) + "-" + std::to_string(end) + " of " +
                     std::to_string(package_names.size()) + "...");

        std::vector<std::string> batch(package_names.begin() + offset, package_names.begin() + end);
        try {
            std::map<std::string, AurPackageInfo> batch_results = AurClient::info_many(batch);
            if(batch_results.empty()) {
                Logger::warn("Bulk AUR info returned no results. Falling back to per-package checks for this batch.");
                for(const auto& package_name : batch) {
                    std::optional<AurPackageInfo> aur_pkg = AurClient::info(package_name);
                    if(aur_pkg.has_value()) {
                        batch_results[aur_pkg->Name] = aur_pkg.value();
                    }
                }
            }
            aur_packages.insert(batch_results.begin(), batch_results.end());
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch AUR info: " + std::string(e.what()));
            failed = true;
        }
    }

    for(size_t i = 0; i < packages.size(); ++i) {
        const auto& local_pkg = packages[i];
        Logger::info("Checking package " + std::to_string(i + 1) + "/" + std::to_string(packages.size()) + ": " + local_pkg.name);

        auto aur_pkg = aur_packages.find(local_pkg.name);
        if(aur_pkg == aur_packages.end()) {
            Logger::warn("Foreign package not found in AUR: " + local_pkg.name);
            continue;
        }

        if(aur_version_is_newer(aur_pkg->second.Version, local_pkg.version)) {
            std::cout << local_pkg.name << " " << local_pkg.version << " -> " << aur_pkg->second.Version << std::endl;
        }
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
        build_from_git(
                source.requested_name, source.clone_name, source.git_url, custom_env, false,
                SourceSyncOptions{});
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
            require_valid_package_name(arg);
            // POLICY: 1 package = 1 preference file。ファイル名は package name validation で固定する。
            fs::path p = fs::path(PACKAGE_BUILD_DIR) / arg;
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
        require_valid_package_name(pkg);
        fs::path    p = fs::path(PACKAGE_BUILD_DIR) / pkg;
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
    if(!fs::exists(PACKAGE_BUILD_DIR)) {
        std::cout << "No source-build packages registered." << std::endl;
        return;
    }
    std::cout << "\033[1mRegistered Source Packages:\033[0m" << std::endl;
    bool found = false;
    for(const auto& entry : fs::directory_iterator(PACKAGE_BUILD_DIR)) {
        if(entry.is_regular_file()) {
            found = true;
            std::string pkg = entry.path().filename().string();
            std::cout << "  \033[1;36m" << pkg << "\033[0m" << std::endl;
            std::ifstream file(entry.path());
            std::string   line;
            while(std::getline(file, line)) {
                line = strip_comment(line);
                if(!trim(line).empty()) std::cout << "    " << trim(line) << std::endl;
            }
        }
    }
    if(!found) std::cout << "  (none)" << std::endl;
}

int cmd_del_src(const std::vector<std::string>& targets) {
    bool failed = false;
    for(const auto& pkg : targets) {
        require_valid_package_name(pkg);
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
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
        require_valid_package_name(pkg);
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
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
    if(fs::exists(PACKAGE_BUILD_DIR)) {
        Logger::info("Checking source packages...");
        for(const auto& entry : fs::directory_iterator(PACKAGE_BUILD_DIR)) {
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
