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

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPKG_VERSION
#define JPKG_VERSION "unknown"
#endif

namespace {

const std::string VERSION = JPKG_VERSION;
const std::string AUR_RPC_URL = "https://aur.archlinux.org/rpc/v5/search/";
const std::string AUR_RPC_INFO_BASE_URL = "https://aur.archlinux.org/rpc/";
const std::string AUR_RPC_INFO_URL = AUR_RPC_INFO_BASE_URL + "?v=5&type=info&arg%5B%5D=";
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";
const std::string USER_AGENT = "jpacker/" + VERSION;
const std::string CONFIG_FILE = "/etc/jpacker/jpacker.conf";
// POLICY: source-build preference の永続化場所。意味を変える場合は互換性影響として扱う。
const std::string PACKAGE_BUILD_DIR = "/etc/jpacker/package.build";

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

namespace {

AppConfig g_config;

} // namespace

struct MakepkgBuildOptions {
    bool rebuild = false;
    bool clean_build = false;
    bool rm_deps = false;
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

// AUR RPC の package info response を、依存解決や表示で扱いやすくした型。
// NOTE: メンバ名は AUR RPC JSON key と 1:1 で対応させるため PascalCase のまま維持する。
struct AurPackageInfo {
    std::string                    Name;
    std::string                    PackageBase;
    std::string                    Version;
    std::string                    Description;
    std::vector<std::string>       Depends;
    std::vector<std::string>       MakeDepends;
    std::vector<std::string>       CheckDepends;
    std::vector<std::string>       OptDepends;
    std::vector<std::string>       Provides;
    std::vector<std::string>       Conflicts;
    std::vector<std::string>       Replaces;
    std::string                    Maintainer;
    std::optional<long long>       OutOfDate;
};

// requested package から、実際に取得する PackageBase と git URL を結びつける型。
struct PackageBuildSource {
    std::string requested_name;
    std::string clone_name;
    std::string git_url;
    bool        is_aur = false;
    bool        has_distinct_package_base = false;
};

// pacman local database から読んだ installed package の最小情報。
struct InstalledPackage {
    std::string name;
    std::string version;
};

// AUR dependency string の raw/name/operator/version を失わないための最小表現。
// POLICY: v1.x では version compare は行わず、constraint の検出と表示だけを担当する。
struct ParsedDependency {
    std::string                raw;
    std::string                name;
    std::optional<std::string> op;
    std::optional<std::string> version;

    bool has_constraint() const {
        return op.has_value();
    }

    bool has_parseable_constraint() const {
        return op.has_value() && version.has_value() && !version->empty() &&
               version->find_first_of("<>=") != 0;
    }

    bool has_malformed_constraint() const {
        return has_constraint() && !has_parseable_constraint();
    }
};

// 依存名を満たす provider package と、その所属 repository。
struct ProvidedDependency {
    std::string repository;
    std::string package_name;
};

// 複数 provider がある依存。#97 ではここで止め、暗黙選択しない。
struct AmbiguousProvidedDependency {
    std::string dependency;
    std::vector<ProvidedDependency> candidates;
};

// 依存を official repo / AUR / provider / unknown に分けた結果。
struct DependencyClassification {
    std::vector<std::string> repo;
    std::vector<std::string> aur;
    std::vector<std::string> provided;
    std::vector<AmbiguousProvidedDependency> ambiguous_providers;
    std::vector<std::string> unknown;
};

enum class DependencyKind {
    Repo,
    Aur,
    Provided,
    AmbiguousProvider,
    Unknown
};

// recursive dependency tree の 1 node。表示と循環検出結果を同じ単位で持つ。
struct RecursiveDependencyNode {
    std::string                          dependency;
    std::string                          package_name;
    std::string                          package_base;
    std::optional<ProvidedDependency>    provided_by;
    std::vector<ProvidedDependency>      provider_candidates;
    DependencyKind                       kind = DependencyKind::Unknown;
    bool                                 already_visited = false;
    bool                                 max_depth_reached = false;
    std::vector<RecursiveDependencyNode> children;
};

// build plan 内で、同じ PackageBase から生成される package 群を束ねる。
struct BuildPlanEntry {
    std::string              package_base;
    std::vector<std::string> package_names;
};

// install 実行時に、PackageBase とは別の package name を明示選択する必要がある target。
struct BuildPlanSplitPackageTarget {
    std::string package_base;
    std::string package_name;
};

// build plan 上で provider により解決された依存を記録する。
struct BuildPlanProvidedDependency {
    std::string        dependency;
    ProvidedDependency provider;
};

// AUR build / fetch の順序、未解決依存、循環検出結果をまとめる計画。
struct BuildPlan {
    std::vector<BuildPlanEntry> order;
    std::vector<BuildPlanSplitPackageTarget> split_package_targets;
    std::vector<BuildPlanProvidedDependency> provided;
    std::vector<AmbiguousProvidedDependency> ambiguous_providers;
    std::vector<std::string> unresolved;
    std::vector<std::string> cycles;
};

namespace {

const int MAX_RECURSIVE_DEP_DEPTH = 16;

} // namespace

// --- 内部クラス ---
namespace {

// CLI 表示と log file 出力をまとめる薄い logger。
class Logger {
    static std::ofstream logFile;
    static bool          initialized;
    static std::string   get_timestamp() {
        auto              now = std::chrono::system_clock::now();
        auto              in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

public:
    static void init(const fs::path& path) {
        if(path.has_parent_path() && !fs::exists(path.parent_path())) {
            fs::create_directories(path.parent_path());
        }
        if(logFile.is_open()) logFile.close();
        logFile.clear();
        logFile.open(path, std::ios::app);
        initialized = logFile.is_open();
    }
    static void info(const std::string& msg) {
        std::cout << "\033[1;32m::\033[0m " << msg << std::endl;
        if(initialized) logFile << "[" << get_timestamp() << "] [INFO] " << msg << std::endl;
    }
    static void warn(const std::string& msg) {
        std::cout << "\033[1;33m:: Warning:\033[0m " << msg << std::endl;
        if(initialized) logFile << "[" << get_timestamp() << "] [WARN] " << msg << std::endl;
    }
    static void error(const std::string& msg) {
        std::cerr << "\033[1;31m:: Error:\033[0m " << msg << std::endl;
        if(initialized) logFile << "[" << get_timestamp() << "] [ERROR] " << msg << std::endl;
    }
    static void raw_cmd(const std::string& cmd) {
        std::cout << "\033[1;33m::\033[0m Running: " << cmd << std::endl;
        if(initialized) logFile << "[" << get_timestamp() << "] [EXEC] " << cmd << std::endl;
    }
};
std::ofstream Logger::logFile;
bool          Logger::initialized = false;

// libcurl の global init/cleanup を 1 実行の寿命に束ねる RAII guard。
class CurlGlobal {
public:
    CurlGlobal() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};

// CURL easy handle の確保と解放を 1 request の寿命に束ねる RAII wrapper。
class CurlHandle {
    CURL* curl_;

public:
    CurlHandle() {
        curl_ = curl_easy_init();
        if(!curl_) throw std::runtime_error("Failed to initialize cURL handle.");
    }
    ~CurlHandle() {
        if(curl_) curl_easy_cleanup(curl_);
    }
    CURL* get() const {
        return curl_;
    }
};

// build/fetch 中の current directory 変更を、scope exit で元に戻す guard。
class WorkDirGuard {
    fs::path original_path_;

public:
    explicit WorkDirGuard(const fs::path& target_path) : original_path_(fs::current_path()) {
        if(!fs::exists(target_path)) fs::create_directories(target_path);
        fs::current_path(target_path);
    }
    ~WorkDirGuard() {
        try {
            if(fs::exists(original_path_)) fs::current_path(original_path_);
        } catch(...) {
        }
    }
};

// 途中失敗した clone/build 用 directory を rollback するための guard。
class DirCleanupGuard {
    fs::path path_;
    bool     committed_ = false;

public:
    explicit DirCleanupGuard(const fs::path& path) : path_(path) {
    }
    void commit() {
        committed_ = true;
    }
    ~DirCleanupGuard() {
        if(!committed_ && fs::exists(path_)) {
            // LANDMINE: 途中失敗した新規 clone/build directory だけを rollback する前提。
            Logger::warn("Rolling back: cleaning up " + path_.string());
            try {
                fs::remove_all(path_);
            } catch(...) {
            }
        }
    }
};

// AUR RPC access をまとめる client。JSON解析の詳細とは分けて扱う。
class AurClient {
public:
    static std::string get_url(const std::string& url);
    static std::string search_query(const std::string& query);
    static std::vector<std::string> search_names_by_provides(const std::string& provided_name);
    static std::optional<AurPackageInfo> info(const std::string& pkg_name);
    static std::map<std::string, AurPackageInfo> info_many(const std::vector<std::string>& pkg_names);
};

} // namespace

// --- 関数宣言 ---

// CLI入口 / help
int run_jpacker(int argc, char* argv[]);
void print_help();
bool handle_info_only_option(int argc, char* argv[]);

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
fs::path get_cache_dir();
void load_config();

// コマンド実行 / shell引数
std::string exec_command(const char* cmd);
int command_status(const std::string& cmd);
int run_command(const std::string& cmd);
std::string join_shell_args(const std::vector<std::string>& args);
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);
std::string makepkg_install_command(const MakepkgBuildOptions& options);
std::string build_editor_command(const std::string& editor, const fs::path& target);

// pacman / repository補助
bool pacman_option_takes_value(const std::string& arg);
bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags);
std::optional<std::string> unsupported_source_sync_option(
        const std::string& operation, const std::vector<std::string>& flags);
bool is_valid_package_name(const std::string& name);
ParsedDependency parse_dependency_string(const std::string& dependency);
std::string dependency_package_name(const std::string& dependency);
std::string provided_dependency_name(const std::string& provided);
std::string dependency_constraint_note(const std::string& dependency);
std::string dependency_constraint_unresolved_reason(const std::string& dependency);
std::string dependency_display_with_constraint_note(const std::string& display, const std::string& dependency);
void warn_unverified_version_constraint(const std::string& dependency);
void require_valid_package_name(const std::string& name);
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
bool is_installed_package(const std::string& pkg_name);
std::optional<std::string> read_srcinfo_version(const fs::path& pkg_dir);
UpdateCheckResult check_update_status(const std::string& pkg_name, const fs::path& pkg_dir);
bool is_repo_package(const std::string& pkg_name);
std::string repo_name_from_sync_db(const fs::path& db_path);
void add_repo_provider(
        std::map<std::string, std::vector<ProvidedDependency>>& providers, const std::string& provided,
        const std::string& repository, const std::string& package_name);
void parse_repo_sync_desc(
        const std::string& desc, const std::string& repository,
        std::map<std::string, std::vector<ProvidedDependency>>& providers);
const std::map<std::string, std::vector<ProvidedDependency>>& repo_providers();
std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name);
std::vector<InstalledPackage> get_foreign_packages();
std::set<std::string> get_foreign_package_names();
bool aur_version_is_newer(const std::string& aur_version, const std::string& installed_version);
bool has_local_package_artifact(const fs::path& pkg_dir);
bool has_local_srcdir(const fs::path& pkg_dir);
MakepkgBuildOptions resolve_makepkg_build_options(const fs::path& pkg_dir);

// prompt / ユーザー確認
bool ask_user(const std::string& question, PromptDefault default_answer);

// AUR RPC / JSON解析
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
json parse_aur_rpc_response(const std::string& response, const std::string& context);
const json& aur_rpc_results_array(const json& response, const std::string& context);
std::string json_string_or_empty(const json& obj, const std::string& key);
std::vector<std::string> json_string_array_or_empty(const json& obj, const std::string& key);
std::optional<long long> json_optional_long_long(const json& obj, const std::string& key);
AurPackageInfo parse_aur_package_info(const json& pkg);
AurPackageInfo parse_aur_rpc_package_info(const json& pkg, const std::string& context);

// AUR provider / build source解決
bool aur_package_provides(const AurPackageInfo& info, const std::string& dependency_name);
std::vector<ProvidedDependency> find_aur_providers(const std::string& dependency_name);
std::vector<ProvidedDependency> find_dependency_providers(const std::string& dependency_name);
PackageBuildSource resolve_build_source(const std::string& pkg_name);
void require_supported_build_source_install_target(const PackageBuildSource& source);
void require_executable_build_source_plan(const PackageBuildSource& source);

// AUR検索 / info表示
bool search_aur(const std::vector<std::string>& keywords);
std::string join_display_values(const std::vector<std::string>& values);
std::string join_comma_display_values(const std::vector<std::string>& values);
bool is_orphaned(const AurPackageInfo& pkg);
std::string installed_display(const AurPackageInfo& pkg);
std::string orphaned_display(const AurPackageInfo& pkg);
std::string out_of_date_display(const std::optional<long long>& out_of_date);
void print_aur_info(const AurPackageInfo& pkg);

// dependency分類 / recursive dependency tree
void add_dependency(std::vector<std::string>& dependencies, std::set<std::string>& seen, const std::string& dependency);
std::vector<std::string> collect_build_dependencies(const AurPackageInfo& pkg);
void add_classified_dependency(std::vector<std::string>& dependencies, const std::string& dependency, const std::string& package_name);
std::string package_base_or_name(const AurPackageInfo& info);
bool has_distinct_package_base(const AurPackageInfo& info);
void add_classified_aur_dependency(std::vector<std::string>& dependencies, const std::string& dependency, const AurPackageInfo& info);
std::string provided_dependency_display(const std::string& dependency, const ProvidedDependency& provider);
std::string provider_display(const ProvidedDependency& provider);
DependencyClassification classify_dependencies(const std::vector<std::string>& dependencies);
std::string dependency_display_name(const std::string& dependency, const std::string& package_name);
std::string dependency_kind_display(DependencyKind kind);
std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg, std::set<std::string>& visited, int depth, int max_depth);
RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited, int depth, int max_depth);
void print_recursive_dependency_node(const RecursiveDependencyNode& node, size_t indent);
void print_recursive_dependency_tree(const std::vector<RecursiveDependencyNode>& nodes);

// build plan / fetch plan
void add_unique_value(std::vector<std::string>& values, const std::string& value);
void add_build_plan_split_package_target(BuildPlan& plan, const AurPackageInfo& info);
void add_build_plan_entry(BuildPlan& plan, const AurPackageInfo& info);
void add_build_plan_provided_dependency(
        BuildPlan& plan, const std::string& dependency, const ProvidedDependency& provider);
void add_build_plan_ambiguous_provider(
        BuildPlan& plan, const std::string& dependency, const std::vector<ProvidedDependency>& candidates);
void collect_aur_build_plan(
        const std::string& package_name, BuildPlan& plan, std::set<std::string>& visited,
        std::set<std::string>& visiting, int depth, int max_depth, bool traverse_aur_providers = true);
void print_build_plan(const BuildPlan& plan);
void require_executable_build_plan(const std::string& target, const BuildPlan& plan);
void require_executable_install_plan(const std::string& target, const BuildPlan& plan);
BuildPlan resolve_build_plan(const std::string& target);
BuildPlan resolve_fetch_plan(const std::string& target);
void print_dependency_group(const std::string& label, const std::vector<std::string>& dependencies);
void print_ambiguous_provider_group(
        const std::string& label, const std::vector<AmbiguousProvidedDependency>& dependencies);
std::string ambiguous_provider_dependency_summary(const AmbiguousProvidedDependency& dependency);
std::string join_ambiguous_provider_summaries(const std::vector<AmbiguousProvidedDependency>& dependencies);
std::string split_package_target_summary(const BuildPlanSplitPackageTarget& target);
std::string join_split_package_target_summaries(const std::vector<BuildPlanSplitPackageTarget>& targets);
std::string aur_git_url_for_package_base(const std::string& package_base);
void print_fetch_plan(const BuildPlan& plan);
void fetch_aur_package_base(const std::string& package_base);

// source build / AUR install
void build_from_git(const std::string& pkg_name, const std::string& clone_name, const std::string& git_url, const std::string& custom_env, bool only_if_updated);
void require_executable_sync_install_target(const std::string& pkg_name);
void install_smart_source(const std::string& pkg_name, bool only_if_updated);
void install_aur_build_plan(const std::string& target);

// コマンド処理
int cmd_deps(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_plan(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_fetch(const std::vector<std::string>& targets, const std::vector<std::string>& flags);
int cmd_sync_info(const std::vector<std::string>& args, const std::vector<std::string>& flags, const std::vector<std::string>& targets);
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
    if(geteuid() == 0) {
        if(handle_info_only_option(argc, argv)) {
            return 0;
        }

        Logger::error("Do not run jpacker as root or with sudo.");
        Logger::error("Run jpacker as a normal user; jpacker will invoke sudo/pacman when needed.");
        return 1;
    }

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if(arg == "--noedit" || arg == "--nodiff" || arg == "--noconfirm" || arg == "--rebuild" ||
           arg == "--cleanbuild" || arg == "--rmdeps") {
            continue;
        }
        if(arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        }
        if(arg == "-V" || arg == "--version") {
            std::cout << "jpacker v" << VERSION << std::endl;
            return 0;
        }
        break;
    }

    CurlGlobal curl_global;
    try {
        load_config();
        fs::path log_path = g_config.log_file.empty() ? (get_cache_dir() / "jpacker.log") : expand_path(g_config.log_file);
        Logger::init(log_path);
        Logger::info("Started jpacker v" + VERSION);
    } catch(const std::exception& e) {
        std::cerr << "Warning: Failed to initialize log: " << e.what() << std::endl;
    } catch(...) {
        std::cerr << "Warning: Failed to initialize log: unknown error." << std::endl;
    }

    if(argc < 2) {
        print_help();
        return 1;
    }
    int operation_index = 1;
    for(; operation_index < argc; ++operation_index) {
        std::string arg = argv[operation_index];
        if(arg == "--noedit") {
            g_config.no_edit = true;
            continue;
        }
        if(arg == "--nodiff") {
            g_config.no_diff = true;
            continue;
        }
        if(arg == "--noconfirm") {
            g_config.no_confirm = true;
            continue;
        }
        if(arg == "--rebuild") {
            g_config.rebuild = true;
            continue;
        }
        if(arg == "--cleanbuild") {
            g_config.clean_build = true;
            continue;
        }
        if(arg == "--rmdeps") {
            g_config.rm_deps = true;
            continue;
        }
        break;
    }
    if(operation_index >= argc) {
        print_help();
        return 1;
    }
    const std::string first_arg = argv[operation_index];
    if(first_arg == "-h" || first_arg == "--help") {
        print_help();
        return 0;
    }

    std::vector<std::string> args, targets, flags;
    const std::string&       operation = first_arg;
    flags.push_back(operation);
    bool                     option_value_expected = false;
    bool                     end_of_options = false;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg.empty()) {
            Logger::error("Empty arguments are not supported.");
            return 1;
        }
        if(arg == "--noedit") {
            g_config.no_edit = true;
            continue;
        }
        if(arg == "--nodiff") {
            g_config.no_diff = true;
            continue;
        }
        if(arg == "--noconfirm") {
            g_config.no_confirm = true;
            continue;
        }
        if(arg == "--rebuild") {
            g_config.rebuild = true;
            continue;
        }
        if(arg == "--cleanbuild") {
            g_config.clean_build = true;
            continue;
        }
        if(arg == "--rmdeps") {
            g_config.rm_deps = true;
            continue;
        }
        if(i > operation_index) {
            if(option_value_expected) {
                flags.push_back(arg);
                option_value_expected = false;
            } else if(end_of_options) {
                targets.push_back(arg);
            } else if(arg == "--") {
                flags.push_back(arg);
                end_of_options = true;
            } else if(!arg.empty() && arg[0] == '-') {
                flags.push_back(arg);
                option_value_expected = pacman_option_takes_value(arg);
            } else {
                targets.push_back(arg);
            }
        }
        args.push_back(arg);
    }
    if(option_value_expected) {
        Logger::error("Missing value for option " + flags.back());
        return 1;
    }

    try {
        const std::vector<std::string> optionless_operations = {
                "build", "upgrade", "clean", "add-src", "del-src", "revert", "edit-src", "list-src"};
        if(std::find(optionless_operations.begin(), optionless_operations.end(), operation) != optionless_operations.end() &&
           !validate_optionless_jpacker_operation(operation, flags)) {
            return 1;
        }

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

        bool is_sync = operation.starts_with("-S");
        bool is_query = operation.starts_with("-Q");
        bool is_foreign_updates = (is_query && operation.find('u') != std::string::npos && operation.find('a') != std::string::npos);
        bool is_search = (is_sync && operation.find('s') != std::string::npos);
        bool is_info = (is_sync && operation.find('i') != std::string::npos);
        bool is_clean = (is_sync && operation.find('c') != std::string::npos);
        bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));
        bool needs_sudo = (is_sync || operation.starts_with("-R") || operation.starts_with("-U") || operation.starts_with("-D"));

        if(is_foreign_updates) return cmd_query_foreign_updates();

        if(is_search) {
            if(targets.empty()) {
                Logger::error("Missing search query.");
                return 1;
            }
            int pacman_ret = run_command("pacman " + join_pacman_args(args));
            Logger::info("Searching AUR...");
            bool aur_found = search_aur(targets);
            return (pacman_ret == 0 || aur_found) ? 0 : 1;
        }
        if(is_info) return cmd_sync_info(args, flags, targets);
        if(is_clean) return run_command("sudo pacman " + join_pacman_args(args));

        if(is_sync) {
            if(targets.empty()) return run_command("sudo pacman " + join_pacman_args(args));
            std::vector<std::string> repo_targets, aur_targets;
            for(const auto& t : targets) {
                require_valid_package_name(t);
                if(is_force_source(t))
                    aur_targets.push_back(t);
                else if(is_repo_package(t))
                    repo_targets.push_back(t);
                else
                    aur_targets.push_back(t);
            }
            if(!aur_targets.empty()) {
                std::optional<std::string> unsupported_option = unsupported_source_sync_option(operation, flags);
                if(unsupported_option.has_value()) {
                    Logger::error(
                            "Unsupported pacman option for AUR/source-build target: " + unsupported_option.value());
                    Logger::error(
                            "Split official repository and AUR/source-build targets, or rerun without this option.");
                    return 1;
                }
            }
            for(const auto& pkg : aur_targets) {
                require_executable_sync_install_target(pkg);
            }
            if(!repo_targets.empty() || is_sys_upgrade) {
                std::string cmd = "sudo pacman " + join_pacman_args(flags);
                if(!repo_targets.empty()) cmd += " " + join_shell_args(repo_targets);
                if(run_command(cmd) != 0) throw std::runtime_error("Pacman failed.");
            }
            if(!aur_targets.empty()) {
                // aur_targets also contains official repo packages with source-build preferences.
                // Those must keep the existing source-build path instead of AUR build-plan execution.
                for(const auto& pkg : aur_targets) {
                    if(is_repo_package(pkg))
                        install_smart_source(pkg, false);
                    else
                        install_aur_build_plan(pkg);
                }
            }
            return 0;
        }
        std::vector<std::string> cmd_args;
        for(const auto& arg : args) {
            if(arg == "--noedit") continue;
            if(arg == "--nodiff") continue;
            if(arg == "--noconfirm") continue;
            if(arg == "--rebuild") continue;
            if(arg == "--cleanbuild") continue;
            if(arg == "--rmdeps") continue;
            cmd_args.push_back(arg);
        }
        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + join_pacman_args(cmd_args));
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
    std::cout << "    \033[1m--rebuild\033[0m           Pass -f to makepkg build/install" << std::endl;
    std::cout << "    \033[1m--cleanbuild\033[0m        Pass -C to makepkg build/install" << std::endl;
    std::cout << "    \033[1m--rmdeps\033[0m            Pass -r to makepkg build/install" << std::endl;
    std::cout << "\033[1mCONFIG\033[0m" << std::endl;
    std::cout << "    jpacker.conf: EDITOR=..., LOGFILE=..., NOEDIT=..., NODIFF=..." << std::endl;
}

bool handle_info_only_option(int argc, char* argv[]) {
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg == "-h" || arg == "--help") {
            print_help();
            return true;
        }
        if(arg == "-V" || arg == "--version") {
            std::cout << "jpacker v" << VERSION << std::endl;
            return true;
        }
    }
    return false;
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

fs::path get_cache_dir() {
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    fs::path    base;
    if(xdg_cache && std::string(xdg_cache).length() > 0) {
        base = xdg_cache;
    } else {
        const char* home = std::getenv("HOME");
        if(!home) throw std::runtime_error("HOME environment variable not set.");
        base = fs::path(home) / ".cache";
    }
    return base / "jpacker";
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

// コマンド実行 / shell引数
std::string exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string           result;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if(!pipe) return "";
    while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return trim(result);
}

int command_status(const std::string& cmd) {
    int status = std::system(cmd.c_str());
    if(status == -1) return 127;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

int run_command(const std::string& cmd) {
    Logger::raw_cmd(cmd);
    return command_status(cmd);
}

std::string join_shell_args(const std::vector<std::string>& args) {
    std::stringstream ss;
    for(size_t i = 0; i < args.size(); ++i) {
        if(i > 0) ss << " ";
        ss << shell_quote(args[i]);
    }
    return ss.str();
}

std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args) {
    // POLICY: --noconfirm は pacman/makepkg へ委譲するだけで、未解決依存の自動突破には使わない。
    if(g_config.no_confirm && std::find(args.begin(), args.end(), "--noconfirm") == args.end()) {
        args.push_back("--noconfirm");
    }
    return args;
}

std::string join_pacman_args(const std::vector<std::string>& args) {
    return join_shell_args(pacman_args_with_global_options(args));
}

std::string makepkg_install_command(const MakepkgBuildOptions& options) {
    std::vector<std::string> args = {"makepkg", "-sic"};
    if(g_config.no_confirm) args.push_back("--noconfirm");
    if(options.rebuild) args.push_back("-f");
    if(options.clean_build) args.push_back("-C");
    // POLICY(#123): 削除対象の判断と実行は makepkg -s/-r に委ね、jpacker では再実装しない。
    if(options.rm_deps) args.push_back("-r");
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

bool validate_optionless_jpacker_operation(const std::string& operation, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == operation) continue;

        Logger::error("Unsupported " + operation + " option: " + flag);
        return false;
    }
    return true;
}

std::optional<std::string> unsupported_source_sync_option(
        const std::string& operation, const std::vector<std::string>& flags) {
    // POLICY(#56): -S の y/u modifier は official repository update にだけ作用する。
    // AUR/source-build 側へ意味を移せない他の pacman option は、黙って無視せず build 前に止める。
    if(operation.size() < 2 || operation[0] != '-' || operation[1] != 'S' ||
       !std::all_of(operation.begin() + 2, operation.end(), [](char modifier) {
           return modifier == 'y' || modifier == 'u';
       })) {
        return operation;
    }

    for(const auto& flag : flags) {
        if(flag == operation || flag == "--") continue;
        return flag;
    }
    return std::nullopt;
}

bool is_valid_package_name(const std::string& name) {
    if(name.empty() || name[0] == '-') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '@' || ch == '.' || ch == '_' || ch == '+' || ch == '-';
    });
}

ParsedDependency parse_dependency_string(const std::string& dependency) {
    ParsedDependency parsed;
    parsed.raw = trim(dependency);

    size_t pos = parsed.raw.find_first_of("<>=");
    if(pos == std::string::npos) {
        parsed.name = parsed.raw;
        return parsed;
    }

    parsed.name = trim(parsed.raw.substr(0, pos));
    if((parsed.raw[pos] == '<' || parsed.raw[pos] == '>') && pos + 1 < parsed.raw.size() &&
       parsed.raw[pos + 1] == '=') {
        parsed.op = parsed.raw.substr(pos, 2);
        parsed.version = trim(parsed.raw.substr(pos + 2));
    } else {
        parsed.op = parsed.raw.substr(pos, 1);
        parsed.version = trim(parsed.raw.substr(pos + 1));
    }

    return parsed;
}

std::string dependency_package_name(const std::string& dependency) {
    return parse_dependency_string(dependency).name;
}

std::string provided_dependency_name(const std::string& provided) {
    return dependency_package_name(provided);
}

std::string dependency_constraint_note(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(!parsed.has_parseable_constraint()) return "";
    return " [constraint: " + parsed.op.value() + " " + parsed.version.value() + ", not verified]";
}

std::string dependency_constraint_unresolved_reason(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(parsed.has_malformed_constraint()) return parsed.raw + " (invalid version constraint)";
    // POLICY(#96): dependency の version constraint は表示・警告まで。jpacker 側で比較解決しない。
    if(parsed.has_constraint()) return parsed.raw + " (version constraint is not verified)";
    return parsed.raw;
}

std::string dependency_display_with_constraint_note(const std::string& display, const std::string& dependency) {
    return display + dependency_constraint_note(dependency);
}

void warn_unverified_version_constraint(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(!parsed.has_parseable_constraint()) return;
    Logger::warn("version constraint for " + parsed.raw + " is not verified");
}

void require_valid_package_name(const std::string& name) {
    if(!is_valid_package_name(name)) {
        throw std::runtime_error("Invalid package name: " + name);
    }
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

std::vector<fs::path> find_install_scripts(const fs::path& pkg_dir) {
    std::vector<fs::path> scripts;
    std::error_code       ec;
    if(!fs::is_directory(pkg_dir, ec) || ec) return scripts;

    for(const auto& entry : fs::directory_iterator(pkg_dir, ec)) {
        if(ec) break;
        if(!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if(entry.path().extension() == ".install") {
            scripts.push_back(entry.path().filename());
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
    std::vector<fs::path> install_scripts = find_install_scripts(pkg_dir);

    if(g_config.no_edit) {
        Logger::info("Skipping PKGBUILD/.install review (--noedit).");
        return;
    }

    log_review_targets(pkg_dir, install_scripts);

    const char* env_editor = std::getenv("EDITOR");
    std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;
    bool        edited = false;

    if(ask_user("Edit PKGBUILD?", PromptDefault::No)) {
        if(run_command(build_editor_command(editor_cmd, "PKGBUILD")) != 0) {
            throw std::runtime_error("Editor failed.");
        }
        edited = true;
    }

    for(const auto& install_script : install_scripts) {
        if(ask_user("Edit install script " + install_script.string() + "?", PromptDefault::No)) {
            if(run_command(build_editor_command(editor_cmd, install_script)) != 0) {
                throw std::runtime_error("Editor failed.");
            }
            edited = true;
        }
    }

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

bool is_installed_package(const std::string& pkg_name) {
    if(pkg_name.empty()) return false;
    return command_status("pacman -Q " + shell_quote(pkg_name) + " > /dev/null 2>&1") == 0;
}

bool is_repo_package(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    std::string cmd = "pacman -Si " + shell_quote(pkg_name) + " > /dev/null 2>&1";
    return (command_status(cmd) == 0);
}

std::string repo_name_from_sync_db(const fs::path& db_path) {
    std::string filename = db_path.filename().string();
    const std::string suffix = ".db";
    if(filename.length() > suffix.length() && filename.substr(filename.length() - suffix.length()) == suffix) {
        return filename.substr(0, filename.length() - suffix.length());
    }
    return db_path.stem().string();
}

bool same_provider(const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.repository == rhs.repository && lhs.package_name == rhs.package_name;
}

void add_provider_candidate(std::vector<ProvidedDependency>& candidates, const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider(existing, provider);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) != candidates.end()) return;
    candidates.push_back(provider);
}

void add_repo_provider(
        std::map<std::string, std::vector<ProvidedDependency>>& providers, const std::string& provided,
        const std::string& repository, const std::string& package_name) {
    std::string provided_name = provided_dependency_name(provided);
    if(provided_name.empty() || !is_valid_package_name(provided_name)) return;
    add_provider_candidate(providers[provided_name], ProvidedDependency{repository, package_name});
}

void parse_repo_sync_desc(
        const std::string& desc, const std::string& repository,
        std::map<std::string, std::vector<ProvidedDependency>>& providers) {
    std::stringstream       ss(desc);
    std::string             line;
    std::string             package_name;
    std::vector<std::string> package_provides;
    std::string             section;

    auto flush_package = [&]() {
        if(package_name.empty()) return;
        for(const auto& provided : package_provides) {
            add_repo_provider(providers, provided, repository, package_name);
        }
        package_name.clear();
        package_provides.clear();
    };

    while(std::getline(ss, line)) {
        line = trim(line);
        if(line.empty()) continue;

        if(line == "%FILENAME%") {
            flush_package();
            section = line;
            continue;
        }

        if(line.length() >= 2 && line.front() == '%' && line.back() == '%') {
            section = line;
            continue;
        }

        if(section == "%NAME%") {
            package_name = line;
        } else if(section == "%PROVIDES%") {
            package_provides.push_back(line);
        }
    }
    flush_package();
}

std::vector<fs::path> repo_sync_db_paths(const fs::path& sync_dir) {
    std::vector<fs::path> paths;
    std::string           repo_list = exec_command("pacman-conf --repo-list 2>/dev/null");
    std::set<std::string> seen;

    std::stringstream ss(repo_list);
    std::string       repo;
    while(std::getline(ss, repo)) {
        repo = trim(repo);
        if(repo.empty()) continue;
        fs::path db_path = sync_dir / (repo + ".db");
        if(fs::exists(db_path) && fs::is_regular_file(db_path)) {
            paths.push_back(db_path);
            seen.insert(db_path.filename().string());
        }
    }

    std::vector<fs::path> fallback_paths;
    for(const auto& entry : fs::directory_iterator(sync_dir)) {
        if(!entry.is_regular_file() || entry.path().extension() != ".db") continue;
        if(seen.count(entry.path().filename().string()) > 0) continue;
        fallback_paths.push_back(entry.path());
    }
    std::sort(fallback_paths.begin(), fallback_paths.end());
    for(const auto& path : fallback_paths) {
        paths.push_back(path);
    }
    return paths;
}

const std::map<std::string, std::vector<ProvidedDependency>>& repo_providers() {
    // POLICY: repo provider 情報は pacman sync DB から作る 1 process 内 cache。
    // ここは単一 thread 前提で、実行中に外部状態を再読込しない。
    static std::map<std::string, std::vector<ProvidedDependency>> s_providers;
    static bool                                                   s_loaded = false;
    if(s_loaded) return s_providers;
    s_loaded = true;

    fs::path sync_dir = "/var/lib/pacman/sync";
    if(!fs::exists(sync_dir)) return s_providers;

    for(const auto& db_path : repo_sync_db_paths(sync_dir)) {
        std::string cmd = "bsdtar -xOf " + shell_quote(db_path.string()) + " '*/desc' 2>/dev/null";
        std::string desc = exec_command(cmd.c_str());
        if(desc.empty()) continue;

        parse_repo_sync_desc(desc, repo_name_from_sync_db(db_path), s_providers);
    }

    return s_providers;
}

std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name) {
    if(!is_valid_package_name(dependency_name)) return {};
    const auto& providers = repo_providers();
    auto        it = providers.find(dependency_name);
    if(it == providers.end()) return {};
    return it->second;
}

std::vector<InstalledPackage> get_foreign_packages() {
    std::vector<InstalledPackage> packages;
    std::string                   output = exec_command("pacman -Qm 2>/dev/null");
    if(output.empty()) return packages;

    std::stringstream ss(output);
    std::string       line;
    while(std::getline(ss, line)) {
        line = trim(line);
        if(line.empty()) continue;

        std::stringstream line_ss(line);
        InstalledPackage  pkg;
        if(line_ss >> pkg.name >> pkg.version) {
            require_valid_package_name(pkg.name);
            packages.push_back(pkg);
        }
    }
    return packages;
}

std::set<std::string> get_foreign_package_names() {
    std::set<std::string> names;
    for(const auto& pkg : get_foreign_packages()) {
        names.insert(pkg.name);
    }
    return names;
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

MakepkgBuildOptions resolve_makepkg_build_options(const fs::path& pkg_dir) {
    MakepkgBuildOptions options;
    bool                has_artifact = has_local_package_artifact(pkg_dir);

    options.rm_deps = g_config.rm_deps;

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

// AUR RPC / JSON解析
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto*  buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), total_size);
    return total_size;
}

json parse_aur_rpc_response(const std::string& response, const std::string& context) {
    try {
        json parsed = json::parse(response);
        if(!parsed.is_object()) {
            throw std::runtime_error("top-level JSON value is not an object");
        }
        return parsed;
    } catch(const json::parse_error& e) {
        throw std::runtime_error(
                "AUR RPC response parse failed for " + context + ": " + std::string(e.what()));
    } catch(const std::runtime_error& e) {
        throw std::runtime_error(
                "AUR RPC response parse failed for " + context + ": unexpected response: " + e.what());
    }
}

const json& aur_rpc_results_array(const json& response, const std::string& context) {
    if(!response.contains("results") || !response["results"].is_array()) {
        throw std::runtime_error(
                "AUR RPC response parse failed for " + context + ": unexpected response: missing results array");
    }
    return response["results"];
}

std::string json_string_or_empty(const json& obj, const std::string& key) {
    if(!obj.is_object() || !obj.contains(key) || obj[key].is_null() || !obj[key].is_string()) return "";
    return obj[key].get<std::string>();
}

std::vector<std::string> json_string_array_or_empty(const json& obj, const std::string& key) {
    std::vector<std::string> values;
    if(!obj.is_object() || !obj.contains(key) || !obj[key].is_array()) return values;
    for(const auto& item : obj[key]) {
        if(item.is_string()) values.push_back(item.get<std::string>());
    }
    return values;
}

std::optional<long long> json_optional_long_long(const json& obj, const std::string& key) {
    if(!obj.is_object() || !obj.contains(key) || obj[key].is_null() || !obj[key].is_number()) return std::nullopt;
    try {
        return obj[key].get<long long>();
    } catch(...) {
        return std::nullopt;
    }
}

AurPackageInfo parse_aur_package_info(const json& pkg) {
    AurPackageInfo info;
    info.Name = json_string_or_empty(pkg, "Name");
    info.PackageBase = json_string_or_empty(pkg, "PackageBase");
    info.Version = json_string_or_empty(pkg, "Version");
    info.Description = json_string_or_empty(pkg, "Description");
    info.Depends = json_string_array_or_empty(pkg, "Depends");
    info.MakeDepends = json_string_array_or_empty(pkg, "MakeDepends");
    info.CheckDepends = json_string_array_or_empty(pkg, "CheckDepends");
    info.OptDepends = json_string_array_or_empty(pkg, "OptDepends");
    info.Provides = json_string_array_or_empty(pkg, "Provides");
    info.Conflicts = json_string_array_or_empty(pkg, "Conflicts");
    info.Replaces = json_string_array_or_empty(pkg, "Replaces");
    info.Maintainer = json_string_or_empty(pkg, "Maintainer");
    info.OutOfDate = json_optional_long_long(pkg, "OutOfDate");
    return info;
}

AurPackageInfo parse_aur_rpc_package_info(const json& pkg, const std::string& context) {
    if(!pkg.is_object()) {
        throw std::runtime_error(
                "AUR RPC response parse failed for " + context +
                ": unexpected response: package info entry is not an object");
    }

    AurPackageInfo info = parse_aur_package_info(pkg);
    if(info.Name.empty()) {
        throw std::runtime_error(
                "AUR RPC response parse failed for " + context +
                ": unexpected response: package info entry is missing Name");
    }
    return info;
}

std::string AurClient::get_url(const std::string& url) {
    CurlHandle  handle;
    std::string readBuffer;
    char        errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, USER_AGENT.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, errorBuffer);
    CURLcode res = curl_easy_perform(handle.get());
    if(res != CURLE_OK) {
        // NOTE: 呼び出し側は空 response を「取得不能/未検出」として扱い、CLI 境界で文脈付きに変換する。
        std::string error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
        Logger::warn("AUR request failed: " + error);
        return "";
    }
    return readBuffer;
}

std::string AurClient::search_query(const std::string& query) {
    CurlHandle  handle;
    char* escaped = curl_easy_escape(handle.get(), query.c_str(), static_cast<int>(query.length()));
    if(!escaped) return "";
    std::string url = AUR_RPC_URL + escaped;
    curl_free(escaped);
    return get_url(url);
}

std::vector<std::string> AurClient::search_names_by_provides(const std::string& provided_name) {
    std::vector<std::string> names;
    CurlHandle               handle;
    char* escaped = curl_easy_escape(handle.get(), provided_name.c_str(), static_cast<int>(provided_name.length()));
    if(!escaped) return names;
    std::string url = AUR_RPC_URL + escaped + "?by=provides";
    curl_free(escaped);

    std::string response = get_url(url);
    if(response.empty()) return names;

    std::string context = "provides search " + provided_name;
    json        j = parse_aur_rpc_response(response, context);
    const json& results = aur_rpc_results_array(j, context);

    for(const auto& pkg : results) {
        AurPackageInfo info = parse_aur_rpc_package_info(pkg, context);
        names.push_back(info.Name);
    }
    return names;
}

std::optional<AurPackageInfo> AurClient::info(const std::string& pkg_name) {
    CurlHandle handle;
    char*      escaped = curl_easy_escape(handle.get(), pkg_name.c_str(), static_cast<int>(pkg_name.length()));
    if(!escaped) return std::nullopt;
    std::string url = AUR_RPC_INFO_URL + escaped;
    curl_free(escaped);

    std::string response = get_url(url);
    if(response.empty()) return std::nullopt;

    std::string context = "package info " + pkg_name;
    json        j = parse_aur_rpc_response(response, context);
    const json& results = aur_rpc_results_array(j, context);
    if(results.empty()) {
        return std::nullopt;
    }
    return parse_aur_rpc_package_info(results[0], context);
}

std::map<std::string, AurPackageInfo> AurClient::info_many(const std::vector<std::string>& pkg_names) {
    std::map<std::string, AurPackageInfo> results;
    if(pkg_names.empty()) return results;

    CurlHandle  handle;
    std::string url = AUR_RPC_INFO_BASE_URL + "?v=5&type=info";
    bool        has_arg = false;
    for(size_t i = 0; i < pkg_names.size(); ++i) {
        char* escaped = curl_easy_escape(handle.get(), pkg_names[i].c_str(), static_cast<int>(pkg_names[i].length()));
        if(!escaped) continue;
        url += "&";
        url += "arg%5B%5D=";
        url += escaped;
        has_arg = true;
        curl_free(escaped);
    }
    if(!has_arg) return results;

    std::string response = get_url(url);
    if(response.empty()) return results;

    std::string context = "multiinfo";
    json        j = parse_aur_rpc_response(response, context);
    const json& aur_results = aur_rpc_results_array(j, context);

    for(const auto& pkg : aur_results) {
        AurPackageInfo pkg_info = parse_aur_rpc_package_info(pkg, context);
        results[pkg_info.Name] = pkg_info;
    }
    return results;
}

// AUR provider / build source解決
bool aur_package_provides(const AurPackageInfo& info, const std::string& dependency_name) {
    for(const auto& provided : info.Provides) {
        if(provided_dependency_name(provided) == dependency_name) return true;
    }
    return false;
}

std::vector<ProvidedDependency> find_aur_providers(const std::string& dependency_name) {
    std::vector<ProvidedDependency> providers;
    if(!is_valid_package_name(dependency_name)) return providers;

    std::vector<std::string> candidates;
    try {
        candidates = AurClient::search_names_by_provides(dependency_name);
    } catch(const std::exception& e) {
        Logger::warn("Failed to search AUR providers for " + dependency_name + ": " + e.what());
        return providers;
    }
    for(const auto& candidate : candidates) {
        if(!is_valid_package_name(candidate)) continue;
        try {
            std::optional<AurPackageInfo> info = AurClient::info(candidate);
            if(info.has_value() && aur_package_provides(info.value(), dependency_name)) {
                add_provider_candidate(providers, ProvidedDependency{"aur", info->Name});
            }
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR provider " + candidate + ": " + e.what());
        }
    }

    return providers;
}

std::vector<ProvidedDependency> find_dependency_providers(const std::string& dependency_name) {
    std::vector<ProvidedDependency> repo_provider = find_repo_providers(dependency_name);
    // POLICY: pacman-first。official repo provider が見つかる場合は AUR provider を混ぜない。
    if(!repo_provider.empty()) return repo_provider;
    return find_aur_providers(dependency_name);
}

PackageBuildSource resolve_build_source(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);

    if(is_repo_package(pkg_name)) {
        return PackageBuildSource{pkg_name, pkg_name, ARCH_GIT_BASE + pkg_name + ".git", false, false};
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(pkg_name);
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
bool search_aur(const std::vector<std::string>& keywords) {
    bool                  found = false;
    std::set<std::string> installed_foreign_packages = get_foreign_package_names();
    for(const auto& pkg_name : keywords) {
        if(pkg_name.empty()) continue;
        if(pkg_name[0] == '-') continue;
        std::string response = AurClient::search_query(pkg_name);
        if(response.empty()) continue;
        try {
            std::string context = "search query " + pkg_name;
            json        j = parse_aur_rpc_response(response, context);
            const json& results = aur_rpc_results_array(j, context);
            for(const auto& pkg : results) {
                AurPackageInfo info = parse_aur_rpc_package_info(pkg, context);
                found = true;
                std::string    name = info.Name;
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
                std::string description = json_string_or_empty(pkg, "Description");
                if(!description.empty()) std::cout << "    " << description << std::endl;
            }
        } catch(const std::exception& e) {
            Logger::warn(e.what());
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
void add_dependency(std::vector<std::string>& dependencies, std::set<std::string>& seen, const std::string& dependency) {
    std::string dep = trim(dependency);
    if(dep.empty()) return;
    if(seen.insert(dep).second) dependencies.push_back(dep);
}

std::vector<std::string> collect_build_dependencies(const AurPackageInfo& pkg) {
    std::vector<std::string> dependencies;
    std::set<std::string>    seen;

    for(const auto& dep : pkg.Depends)
        add_dependency(dependencies, seen, dep);
    for(const auto& dep : pkg.MakeDepends)
        add_dependency(dependencies, seen, dep);
    for(const auto& dep : pkg.CheckDepends)
        add_dependency(dependencies, seen, dep);

    return dependencies;
}

void add_classified_dependency(std::vector<std::string>& dependencies, const std::string& dependency, const std::string& package_name) {
    std::string display;
    if(dependency == package_name)
        display = dependency;
    else
        display = dependency + " (" + package_name + ")";
    dependencies.push_back(dependency_display_with_constraint_note(display, dependency));
}

std::string package_base_or_name(const AurPackageInfo& info) {
    return info.PackageBase.empty() ? info.Name : info.PackageBase;
}

bool has_distinct_package_base(const AurPackageInfo& info) {
    return !info.PackageBase.empty() && info.PackageBase != info.Name;
}

void add_classified_aur_dependency(std::vector<std::string>& dependencies, const std::string& dependency, const AurPackageInfo& info) {
    std::string display;
    if(info.Name.empty() || dependency == info.Name)
        display = dependency;
    else
        display = dependency + " (" + info.Name + ")";
    if(has_distinct_package_base(info)) display += " (base: " + info.PackageBase + ")";
    dependencies.push_back(dependency_display_with_constraint_note(display, dependency));
}

std::string provided_dependency_display(const std::string& dependency, const ProvidedDependency& provider) {
    return dependency + " [provided by " + provider.repository + "/" + provider.package_name + "]" +
           dependency_constraint_note(dependency);
}

std::string provider_display(const ProvidedDependency& provider) {
    return provider.repository + "/" + provider.package_name;
}

void add_ambiguous_provider_dependency(
        std::vector<AmbiguousProvidedDependency>& dependencies, const std::string& dependency,
        const std::vector<ProvidedDependency>& candidates) {
    std::string trimmed = trim(dependency);
    if(trimmed.empty() || candidates.empty()) return;

    // POLICY(#97/#143): 複数 provider はここで集約し、暗黙選択しない。
    auto same_dependency = [&trimmed](const AmbiguousProvidedDependency& existing) {
        return existing.dependency == trimmed;
    };
    auto it = std::find_if(dependencies.begin(), dependencies.end(), same_dependency);
    if(it == dependencies.end()) {
        dependencies.push_back(AmbiguousProvidedDependency{trimmed, {}});
        it = std::prev(dependencies.end());
    }
    for(const auto& candidate : candidates) {
        add_provider_candidate(it->candidates, candidate);
    }
}

DependencyClassification classify_dependencies(const std::vector<std::string>& dependencies) {
    DependencyClassification result;

    for(const auto& dependency : dependencies) {
        ParsedDependency parsed = parse_dependency_string(dependency);
        std::string      package_name = parsed.name;
        if(!is_valid_package_name(package_name)) {
            result.unknown.push_back(dependency);
            continue;
        }
        if(parsed.has_malformed_constraint()) {
            result.unknown.push_back(dependency_constraint_unresolved_reason(dependency));
            continue;
        }
        warn_unverified_version_constraint(dependency);

        if(is_repo_package(package_name)) {
            add_classified_dependency(result.repo, dependency, package_name);
            continue;
        }

        try {
            std::optional<AurPackageInfo> info = AurClient::info(package_name);
            if(info.has_value()) {
                add_classified_aur_dependency(result.aur, dependency, info.value());
            } else {
                std::vector<ProvidedDependency> providers = find_dependency_providers(package_name);
                if(providers.size() == 1)
                    result.provided.push_back(provided_dependency_display(dependency, providers.front()));
                else if(providers.size() > 1)
                    add_ambiguous_provider_dependency(result.ambiguous_providers, dependency, providers);
                else
                    result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
            }
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR dependency " + package_name + ": " + e.what());
            std::vector<ProvidedDependency> providers = find_repo_providers(package_name);
            if(providers.size() == 1)
                result.provided.push_back(provided_dependency_display(dependency, providers.front()));
            else if(providers.size() > 1)
                add_ambiguous_provider_dependency(result.ambiguous_providers, dependency, providers);
            else
                result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
        }
    }

    return result;
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

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg, std::set<std::string>& visited, int depth, int max_depth) {
    std::vector<RecursiveDependencyNode> nodes;
    for(const auto& dependency : collect_build_dependencies(pkg)) {
        nodes.push_back(resolve_recursive_dependency(dependency, visited, depth, max_depth));
    }
    return nodes;
}

RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited, int depth, int max_depth) {
    RecursiveDependencyNode node;
    node.dependency = dependency;
    ParsedDependency parsed = parse_dependency_string(dependency);
    node.package_name = parsed.name;

    if(!is_valid_package_name(node.package_name) || parsed.has_malformed_constraint()) {
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(is_repo_package(node.package_name)) {
        node.kind = DependencyKind::Repo;
        return node;
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(node.package_name);
    } catch(const std::exception& e) {
        Logger::warn("Failed to check AUR dependency " + node.package_name + ": " + e.what());
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(!info.has_value()) {
        std::vector<ProvidedDependency> providers = find_dependency_providers(node.package_name);
        if(providers.size() == 1) {
            node.kind = DependencyKind::Provided;
            node.provided_by = providers.front();
        } else if(providers.size() > 1) {
            node.kind = DependencyKind::AmbiguousProvider;
            node.provider_candidates = providers;
        } else {
            node.kind = DependencyKind::Unknown;
        }
        return node;
    }

    node.kind = DependencyKind::Aur;
    node.package_base = package_base_or_name(info.value());
    if(!visited.insert(node.package_base).second) {
        node.already_visited = true;
        return node;
    }
    if(depth >= max_depth) {
        node.max_depth_reached = true;
        return node;
    }

    node.children = resolve_recursive_dependencies(info.value(), visited, depth + 1, max_depth);
    return node;
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

void add_build_plan_split_package_target(BuildPlan& plan, const AurPackageInfo& info) {
    if(!has_distinct_package_base(info)) return;

    auto same_target = [&info](const BuildPlanSplitPackageTarget& existing) {
        return existing.package_base == info.PackageBase && existing.package_name == info.Name;
    };
    if(std::find_if(plan.split_package_targets.begin(), plan.split_package_targets.end(), same_target) !=
       plan.split_package_targets.end())
        return;

    plan.split_package_targets.push_back(BuildPlanSplitPackageTarget{info.PackageBase, info.Name});
}

void add_build_plan_entry(BuildPlan& plan, const AurPackageInfo& info) {
    std::string package_base = package_base_or_name(info);
    auto        same_base = [&package_base](const BuildPlanEntry& existing) { return existing.package_base == package_base; };
    auto        it = std::find_if(plan.order.begin(), plan.order.end(), same_base);
    add_build_plan_split_package_target(plan, info);
    if(it == plan.order.end()) {
        plan.order.push_back(BuildPlanEntry{package_base, {info.Name}});
        return;
    }
    add_unique_value(it->package_names, info.Name);
}

void add_build_plan_provided_dependency(
        BuildPlan& plan, const std::string& dependency, const ProvidedDependency& provider) {
    std::string trimmed = trim(dependency);
    if(trimmed.empty()) return;

    auto same_dependency = [&](const BuildPlanProvidedDependency& existing) {
        return existing.dependency == trimmed && existing.provider.repository == provider.repository &&
               existing.provider.package_name == provider.package_name;
    };
    if(std::find_if(plan.provided.begin(), plan.provided.end(), same_dependency) != plan.provided.end()) return;
    plan.provided.push_back(BuildPlanProvidedDependency{trimmed, provider});
}

void add_build_plan_ambiguous_provider(
        BuildPlan& plan, const std::string& dependency, const std::vector<ProvidedDependency>& candidates) {
    add_ambiguous_provider_dependency(plan.ambiguous_providers, dependency, candidates);
}

void collect_aur_build_plan(
        const std::string& package_name, BuildPlan& plan, std::set<std::string>& visited,
        std::set<std::string>& visiting, int depth, int max_depth, bool traverse_aur_providers) {
    if(depth > max_depth) {
        add_unique_value(plan.unresolved, package_name + " (max depth reached)");
        return;
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(package_name);
    } catch(const std::exception& e) {
        Logger::warn("Failed to fetch AUR info for " + package_name + ": " + e.what());
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    if(!info.has_value()) {
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    std::string build_unit = package_base_or_name(info.value());
    if(visited.count(build_unit) > 0) return;
    if(visiting.count(build_unit) > 0) {
        add_unique_value(plan.cycles, build_unit);
        return;
    }

    visiting.insert(build_unit);

    for(const auto& dependency : collect_build_dependencies(info.value())) {
        ParsedDependency parsed = parse_dependency_string(dependency);
        std::string      dep_name = parsed.name;
        if(!is_valid_package_name(dep_name)) {
            add_unique_value(plan.unresolved, dependency);
            continue;
        }
        if(parsed.has_malformed_constraint()) {
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
            continue;
        }
        if(parsed.has_constraint()) {
            // POLICY(#96): plan は未検証 constraint を解決済み扱いにしない。
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
        }
        if(is_repo_package(dep_name)) continue;

        std::optional<AurPackageInfo> dependency_info;
        try {
            dependency_info = AurClient::info(dep_name);
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR dependency " + dep_name + ": " + e.what());
        }

        if(dependency_info.has_value()) {
            collect_aur_build_plan(dep_name, plan, visited, visiting, depth + 1, max_depth, traverse_aur_providers);
            continue;
        }

        std::vector<ProvidedDependency> providers = find_dependency_providers(dep_name);
        if(providers.size() == 1) {
            const ProvidedDependency& provider = providers.front();
            add_build_plan_provided_dependency(plan, dependency, provider);
            if(traverse_aur_providers && provider.repository == "aur") {
                collect_aur_build_plan(provider.package_name, plan, visited, visiting, depth + 1, max_depth, traverse_aur_providers);
            }
        } else if(providers.size() > 1) {
            add_build_plan_ambiguous_provider(plan, dependency, providers);
        } else {
            add_unique_value(plan.unresolved, dependency);
        }
    }

    visiting.erase(build_unit);
    visited.insert(build_unit);
    add_build_plan_entry(plan, info.value());
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
       !plan.split_package_targets.empty()) {
        std::cout << std::endl;
        std::cout << "Plan status: incomplete" << std::endl;
        if(!plan.unresolved.empty()) std::cout << "  unresolved dependencies remain" << std::endl;
        if(!plan.ambiguous_providers.empty()) std::cout << "  ambiguous providers are not selected" << std::endl;
        if(!plan.cycles.empty()) std::cout << "  cyclic dependencies detected" << std::endl;
        if(!plan.split_package_targets.empty())
            std::cout << "  split package install target selection is not implemented" << std::endl;
    }
}

void require_executable_build_plan(const std::string& target, const BuildPlan& plan) {
    if(!plan.unresolved.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; unresolved dependencies: " +
                join_comma_display_values(plan.unresolved));
    }
    if(!plan.ambiguous_providers.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; ambiguous providers: " +
                join_ambiguous_provider_summaries(plan.ambiguous_providers));
    }
    if(!plan.cycles.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; cyclic dependencies: " +
                join_comma_display_values(plan.cycles));
    }
}

void require_executable_install_plan(const std::string& target, const BuildPlan& plan) {
    require_executable_build_plan(target, plan);
    if(!plan.split_package_targets.empty()) {
        throw std::runtime_error(
                "Cannot execute install plan for " + target +
                "; split package install target selection is not implemented: " +
                join_split_package_target_summaries(plan.split_package_targets));
    }
}

BuildPlan resolve_build_plan(const std::string& target) {
    require_valid_package_name(target);
    if(!AurClient::info(target).has_value()) throw std::runtime_error("AUR package not found: " + target);

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    collect_aur_build_plan(target, plan, visited, visiting, 0, MAX_RECURSIVE_DEP_DEPTH);
    return plan;
}

BuildPlan resolve_fetch_plan(const std::string& target) {
    require_valid_package_name(target);
    if(!AurClient::info(target).has_value()) throw std::runtime_error("AUR package not found: " + target);

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    // POLICY: fetch は取得対象の列挙まで。AUR provider を辿って暗黙に追加取得しない。
    collect_aur_build_plan(target, plan, visited, visiting, 0, MAX_RECURSIVE_DEP_DEPTH, false);
    return plan;
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

std::string ambiguous_provider_dependency_summary(const AmbiguousProvidedDependency& dependency) {
    std::vector<std::string> candidates;
    for(const auto& candidate : dependency.candidates) {
        candidates.push_back(provider_display(candidate));
    }
    return dependency.dependency + " (" + join_comma_display_values(candidates) + ")";
}

std::string join_ambiguous_provider_summaries(const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::vector<std::string> values;
    for(const auto& dependency : dependencies) {
        values.push_back(ambiguous_provider_dependency_summary(dependency));
    }
    return join_comma_display_values(values);
}

std::string split_package_target_summary(const BuildPlanSplitPackageTarget& target) {
    return target.package_name + " (base: " + target.package_base + ")";
}

std::string join_split_package_target_summaries(const std::vector<BuildPlanSplitPackageTarget>& targets) {
    std::vector<std::string> values;
    for(const auto& target : targets) {
        values.push_back(split_package_target_summary(target));
    }
    return join_comma_display_values(values);
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
}

void fetch_aur_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    fs::path cache_dir = get_cache_dir();
    fs::path repo_dir = cache_dir / package_base;
    std::string git_url = aur_git_url_for_package_base(package_base);

    if(!fs::exists(cache_dir)) fs::create_directories(cache_dir);

    if(fs::exists(repo_dir)) {
        if(!fs::is_directory(repo_dir)) throw std::runtime_error(repo_dir.string() + " exists but is not a directory.");
        if(!fs::exists(repo_dir / ".git")) throw std::runtime_error(repo_dir.string() + " exists but is not a git repository.");

        // POLICY: fetch command は既存 clone で git fetch まで。worktree update/pull/reset/build/install はしない。
        WorkDirGuard wd_repo(repo_dir);
        std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, git_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ": " + current_url);
        }

        Logger::info("Fetching " + package_base + "...");
        if(run_command("git fetch origin") != 0) throw std::runtime_error("Failed to fetch " + package_base + ".");
        return;
    }

    Logger::info("Cloning " + package_base + "...");
    WorkDirGuard    wd_cache(cache_dir);
    DirCleanupGuard cleanup_guard(repo_dir);
    if(run_command("git clone " + shell_quote(git_url) + " " + shell_quote(package_base)) != 0) {
        throw std::runtime_error("Failed to clone " + package_base + ".");
    }
    cleanup_guard.commit();
}

// source build / AUR install
void build_from_git(const std::string& pkg_name, const std::string& clone_name, const std::string& git_url, const std::string& custom_env, bool only_if_updated) {
    require_valid_package_name(pkg_name);
    require_valid_package_name(clone_name);
    Logger::info("Processing " + pkg_name + "...");
    fs::path build_base = get_cache_dir();
    fs::path pkg_dir = build_base / clone_name;
    if(!fs::exists(build_base)) fs::create_directories(build_base);

    {
        WorkDirGuard wd(build_base);
        bool         needs_clone = true;

        if(fs::exists(pkg_dir) && fs::exists(pkg_dir / ".git")) {
            {
                WorkDirGuard wd_repo(pkg_dir);
                std::string  current_url = exec_command("git config --get remote.origin.url");
                if(!remote_url_matches_expected(current_url, git_url)) {
                    Logger::warn("Remote URL mismatch. Re-cloning...");
                } else {
                    needs_clone = false;
                }
            }

            if(!needs_clone) {
                Logger::info("Updating repository...");
                WorkDirGuard wd_repo(pkg_dir);
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
                if(run_command("git reset --hard " + shell_quote("origin/" + branch)) != 0) {
                    throw std::runtime_error("Failed to reset repository.");
                }
            }
        }

        if(needs_clone) {
            if(fs::exists(pkg_dir)) fs::remove_all(pkg_dir);
            Logger::info("Cloning repository...");
            DirCleanupGuard cleanup_guard(pkg_dir);
            if(run_command("git clone " + shell_quote(git_url) + " " + shell_quote(clone_name)) != 0) {
                throw std::runtime_error("Failed to clone " + clone_name);
            }
            cleanup_guard.commit();
        }
    }

    {
        WorkDirGuard wd(pkg_dir);

        if(only_if_updated) {
            UpdateCheckResult update_check = check_update_status(pkg_name, pkg_dir);
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

        review_build_files(pkg_dir);
        MakepkgBuildOptions makepkg_options = resolve_makepkg_build_options(pkg_dir);
        std::string build_cmd;
        if(!trim(custom_env).empty()) {
            Logger::info("Applying custom build flags: " + custom_env);
            build_cmd = custom_env + makepkg_install_command(makepkg_options);
        } else {
            Logger::info("Using default makepkg.conf settings.");
            build_cmd = makepkg_install_command(makepkg_options);
        }
        if(run_command(build_cmd) != 0) throw std::runtime_error("Build failed.");
    }
}

void install_smart_source(const std::string& pkg_name, bool only_if_updated) {
    std::string env = get_package_env(pkg_name);
    PackageBuildSource source = resolve_build_source(pkg_name);
    require_executable_build_source_plan(source);

    build_from_git(source.requested_name, source.clone_name, source.git_url, env, only_if_updated);
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

void install_aur_build_plan(const std::string& target) {
    BuildPlan plan = resolve_build_plan(target);
    require_executable_install_plan(target, plan);

    for(const auto& entry : plan.order) {
        std::string package_names = join_comma_display_values(entry.package_names);
        Logger::info("Building AUR PackageBase: " + entry.package_base);
        Logger::info("Target package(s): " + package_names);

        std::string pkg_name = entry.package_names.empty() ? entry.package_base : entry.package_names.front();
        std::string env = get_package_env(pkg_name);
        if(env.empty() && pkg_name != entry.package_base) env = get_package_env(entry.package_base);

        try {
            build_from_git(pkg_name, entry.package_base, aur_git_url_for_package_base(entry.package_base), env, false);
        } catch(const std::exception& e) {
            throw std::runtime_error(
                    "Failed while building/installing PackageBase " + entry.package_base + " (" + package_names +
                    "): " + e.what());
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
            if(recursive) {
                std::set<std::string> visited;
                visited.insert(package_base_or_name(info.value()));
                std::vector<RecursiveDependencyNode> recursive_nodes =
                        resolve_recursive_dependencies(info.value(), visited, 1, MAX_RECURSIVE_DEP_DEPTH);
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

    bool failed = false;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_fetch_plan(target);

            if(i > 0) std::cout << std::endl;
            print_fetch_plan(plan);
            require_executable_build_plan(target, plan);

            for(const auto& entry : plan.order) {
                try {
                    fetch_aur_package_base(entry.package_base);
                } catch(const std::exception& e) {
                    Logger::error(e.what());
                    failed = true;
                }
            }
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch repositories for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_sync_info(const std::vector<std::string>& args, const std::vector<std::string>& flags, const std::vector<std::string>& targets) {
    if(targets.empty()) return run_command("pacman " + join_pacman_args(args));

    bool                     failed = false;
    std::vector<std::string> repo_targets;
    std::vector<AurPackageInfo> aur_infos;

    for(const auto& target : targets) {
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
            } else {
                Logger::error("Package not found in repos or AUR: " + target);
                failed = true;
            }
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch AUR info for " + target + ": " + e.what());
            failed = true;
        }
    }

    if(!repo_targets.empty()) {
        std::vector<std::string> pacman_args = flags;
        pacman_args.insert(pacman_args.end(), repo_targets.begin(), repo_targets.end());
        if(run_command("pacman " + join_pacman_args(pacman_args)) != 0) failed = true;
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
        build_from_git(source.requested_name, source.clone_name, source.git_url, custom_env, false);
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
    bool failed = false;
    Logger::info("Cleaning package caches...");
    if(run_command("sudo pacman " + join_pacman_args({"-Sc"})) != 0) {
        Logger::warn("Pacman clean failed or cancelled.");
        failed = true;
    }
    fs::path cache = get_cache_dir();
    if(fs::exists(cache) && !fs::is_empty(cache)) {
        if(ask_user("Clean jpacker build cache (" + cache.string() + ")?", PromptDefault::No)) {
            Logger::info("Removing cached build files...");
            for(const auto& entry : fs::directory_iterator(cache)) {
                if(entry.path().filename() == "jpacker.log") continue;
                try {
                    fs::remove_all(entry.path());
                } catch(const std::exception& e) {
                    Logger::error("Failed to remove " + entry.path().string() + ": " + e.what());
                }
            }
            Logger::info("jpacker cache cleaned.");
        } else
            Logger::info("Skipped jpacker cache cleaning.");
    } else
        Logger::info("jpacker cache is empty.");
    return failed ? 1 : 0;
}

int cmd_upgrade() {
    bool failed = false;
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
                    install_smart_source(pkg_name, true);
                } catch(const std::exception& e) {
                    Logger::error("Error updating " + pkg_name + ": " + e.what());
                    failed = true;
                }
            }
        }
    }
    return failed ? 1 : 0;
}
