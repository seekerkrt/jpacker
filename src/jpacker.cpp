/**
 * jpacker - A full-featured Pacman wrapper and AUR helper
 * v1.3.0 Features:
 * - Smart Upgrade: Skips rebuilding packages if the version hasn't changed during 'upgrade'.
 * - Variable expansion support in config files.
 * - '--nodiff' option support.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
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
#include <sys/wait.h>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPKG_VERSION
#define JPKG_VERSION "1.3.0"
#endif

const std::string VERSION = JPKG_VERSION;
const std::string AUR_RPC_URL = "https://aur.archlinux.org/rpc/v5/search/";
const std::string AUR_RPC_INFO_BASE_URL = "https://aur.archlinux.org/rpc/";
const std::string AUR_RPC_INFO_URL = AUR_RPC_INFO_BASE_URL + "?v=5&type=info&arg%5B%5D=";
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";
const std::string USER_AGENT = "jpacker/" + VERSION;
const std::string CONFIG_FILE = "/etc/jpacker/jpacker.conf";
const std::string PACKAGE_BUILD_DIR = "/etc/jpacker/package.build";

struct AppConfig {
    bool        no_edit = false;
    bool        no_diff = false;
    std::string editor = "nano";
    std::string log_file = "";
};

AppConfig g_config;

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

struct PackageBuildSource {
    std::string requested_name;
    std::string clone_name;
    std::string git_url;
};

struct InstalledPackage {
    std::string name;
    std::string version;
};

struct DependencyClassification {
    std::vector<std::string> repo;
    std::vector<std::string> aur;
    std::vector<std::string> unknown;
};

// --- ユーティリティ ---
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

std::string unquote(std::string str) {
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

bool is_valid_package_name(const std::string& name) {
    if(name.empty() || name[0] == '-') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '@' || ch == '.' || ch == '_' || ch == '+' || ch == '-';
    });
}

std::string dependency_package_name(const std::string& dependency) {
    std::string dep = trim(dependency);
    size_t      pos = dep.find_first_of("<>=");
    if(pos != std::string::npos) dep = dep.substr(0, pos);
    return trim(dep);
}

void require_valid_package_name(const std::string& name) {
    if(!is_valid_package_name(name)) {
        throw std::runtime_error("Invalid package name: " + name);
    }
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

bool pacman_option_takes_value(const std::string& arg) {
    static const std::vector<std::string> long_opts = {
            "--arch", "--assume-installed", "--cachedir", "--color", "--config", "--dbpath",
            "--gpgdir", "--hookdir", "--ignore", "--ignoregroup", "--logfile", "--overwrite",
            "--print-format", "--root", "--sysroot"};
    static const std::vector<std::string> short_opts = {"-b", "-r"};

    if(arg.find('=') != std::string::npos) return false;
    if(std::find(long_opts.begin(), long_opts.end(), arg) != long_opts.end()) return true;
    return std::find(short_opts.begin(), short_opts.end(), arg) != short_opts.end();
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

// コマンド出力を取得するヘルパー
std::string exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string           result;
    // 修正: decltype(&pclose) を int(*)(FILE*) に変更して警告を回避
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if(!pipe) return "";
    while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return trim(result);
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

int command_status(const std::string& cmd);

// --- Helpers ---
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
    if(remote_head.find(prefix) == 0 && remote_head.length() > prefix.length()) {
        return remote_head.substr(prefix.length());
    }
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/main") == 0) return "main";
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/master") == 0) return "master";
    return "master";
}

// 【New!】更新が必要かチェックする関数
// 戻り値: true (更新必要 or インストールされていない), false (最新版)
bool is_update_needed(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    // 1. インストール済みバージョン取得
    std::string installed_full = exec_command(("pacman -Q " + shell_quote(pkg_name) + " 2>/dev/null").c_str());
    if(installed_full.empty()) {
        return true;// インストールされていないのでビルド必要
    }
    // "package 1.0.0-1" -> "1.0.0-1"
    size_t      space_pos = installed_full.find(' ');
    std::string installed_ver = (space_pos != std::string::npos) ? installed_full.substr(space_pos + 1) : "";

    // 2. PKGBUILDから最新バージョン取得 (.SRCINFO生成)
    // NOTE: ディレクトリ移動は呼び出し元(WorkDirGuard)で制御されている前提
    std::string srcinfo = exec_command("makepkg --printsrcinfo 2>/dev/null");
    std::string pkgver, pkgrel;

    std::stringstream ss(srcinfo);
    std::string       line;
    while(std::getline(ss, line)) {
        if(line.find("pkgver =") != std::string::npos) {
            pkgver = trim(line.substr(line.find('=') + 1));
        } else if(line.find("pkgrel =") != std::string::npos) {
            pkgrel = trim(line.substr(line.find('=') + 1));
        }
    }

    if(pkgver.empty() || pkgrel.empty()) return true;// 取得失敗時は安全側に倒してビルド
    std::string new_ver = pkgver + "-" + pkgrel;

    // 3. vercmp で比較
    std::string cmp_cmd = "vercmp " + shell_quote(new_ver) + " " + shell_quote(installed_ver);
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    // vercmp > 0 なら new_ver の方が新しい
    try {
        if(std::stoi(cmp_res) > 0) return true;
    } catch(...) {
        return true;
    }

    Logger::info(pkg_name + " is up to date (" + installed_ver + "). Skipping.");
    return false;
}

class CurlGlobal {
public:
    CurlGlobal() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};
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
class WorkDirGuard {
    fs::path original_path_;

public:
    explicit WorkDirGuard(const fs::path& target_path) {
        original_path_ = fs::current_path();
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
            Logger::warn("Rolling back: cleaning up " + path_.string());
            try {
                fs::remove_all(path_);
            } catch(...) {
            }
        }
    }
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    ((std::string*)userp)->append((char*)contents, total_size);
    return total_size;
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
std::string build_editor_command(const std::string& editor, const fs::path& target) {
    std::vector<std::string> args = split_command_words(editor);
    args.push_back(target.string());
    return join_shell_args(args);
}
bool is_repo_package(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    std::string cmd = "pacman -Si " + shell_quote(pkg_name) + " > /dev/null 2>&1";
    return (command_status(cmd) == 0);
}
bool ask_user(const std::string& question) {
    std::cout << ":: " << question << " [Y/n] ";
    std::string input;
    std::getline(std::cin, input);
    return (input.empty() || to_lower(input) == "y" || to_lower(input) == "yes");
}

std::string json_string_or_empty(const json& obj, const std::string& key) {
    if(!obj.contains(key) || obj[key].is_null()) return "";
    return obj[key].get<std::string>();
}

std::vector<std::string> json_string_array_or_empty(const json& obj, const std::string& key) {
    std::vector<std::string> values;
    if(!obj.contains(key) || !obj[key].is_array()) return values;
    for(const auto& item : obj[key]) {
        if(!item.is_null()) values.push_back(item.get<std::string>());
    }
    return values;
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
    if(pkg.contains("OutOfDate") && !pkg["OutOfDate"].is_null()) {
        info.OutOfDate = pkg["OutOfDate"].get<long long>();
    }
    return info;
}

class AurClient {
public:
    static std::string get_url(const std::string& url) {
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
            std::string error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
            Logger::warn("AUR request failed: " + error);
            return "";
        }
        return readBuffer;
    }

    static std::string search_query(const std::string& query) {
        CurlHandle  handle;
        char* escaped = curl_easy_escape(handle.get(), query.c_str(), static_cast<int>(query.length()));
        if(!escaped) return "";
        std::string url = AUR_RPC_URL + escaped;
        curl_free(escaped);
        return get_url(url);
    }

    static std::optional<AurPackageInfo> info(const std::string& pkg_name) {
        CurlHandle handle;
        char*      escaped = curl_easy_escape(handle.get(), pkg_name.c_str(), static_cast<int>(pkg_name.length()));
        if(!escaped) return std::nullopt;
        std::string url = AUR_RPC_INFO_URL + escaped;
        curl_free(escaped);

        std::string response = get_url(url);
        if(response.empty()) return std::nullopt;

        auto j = json::parse(response);
        if(!j.contains("results") || !j["results"].is_array() || j["results"].empty()) {
            return std::nullopt;
        }
        return parse_aur_package_info(j["results"][0]);
    }

    static std::map<std::string, AurPackageInfo> info_many(const std::vector<std::string>& pkg_names) {
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

        auto j = json::parse(response);
        if(!j.contains("results") || !j["results"].is_array()) {
            return results;
        }

        for(const auto& pkg : j["results"]) {
            AurPackageInfo info = parse_aur_package_info(pkg);
            if(!info.Name.empty()) results[info.Name] = info;
        }
        return results;
    }
};

PackageBuildSource resolve_build_source(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);

    if(is_repo_package(pkg_name)) {
        return PackageBuildSource{pkg_name, pkg_name, ARCH_GIT_BASE + pkg_name + ".git"};
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

    return PackageBuildSource{pkg_name, info->PackageBase, AUR_BASE_URL + info->PackageBase + ".git"};
}

bool search_aur(const std::vector<std::string>& keywords) {
    bool found = false;
    for(const auto& pkg_name : keywords) {
        if(pkg_name.empty()) continue;
        if(pkg_name[0] == '-') continue;
        std::string response = AurClient::search_query(pkg_name);
        if(response.empty()) continue;
        try {
            auto j = json::parse(response);
            if(j.contains("results") && j["results"].is_array()) {
                for(const auto& pkg : j["results"]) {
                    found = true;
                    std::cout << "\033[1;35maur\033[0m/\033[1m" << pkg["Name"].get<std::string>()
                              << "\033[0m \033[1;32m" << pkg["Version"].get<std::string>() << "\033[0m" << std::endl;
                    if(pkg.contains("Description") && !pkg["Description"].is_null())
                        std::cout << "    " << pkg["Description"].get<std::string>() << std::endl;
                }
            }
        } catch(...) {
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

std::string out_of_date_display(const std::optional<long long>& out_of_date) {
    if(!out_of_date.has_value()) return "No";
    return std::to_string(out_of_date.value());
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
    std::cout << "Out Of Date     : " << out_of_date_display(pkg.OutOfDate) << std::endl;
}

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
    if(dependency == package_name)
        dependencies.push_back(dependency);
    else
        dependencies.push_back(dependency + " (" + package_name + ")");
}

DependencyClassification classify_dependencies(const std::vector<std::string>& dependencies) {
    DependencyClassification result;

    for(const auto& dependency : dependencies) {
        std::string package_name = dependency_package_name(dependency);
        if(!is_valid_package_name(package_name)) {
            result.unknown.push_back(dependency);
            continue;
        }

        if(is_repo_package(package_name)) {
            add_classified_dependency(result.repo, dependency, package_name);
            continue;
        }

        try {
            if(AurClient::info(package_name).has_value()) {
                add_classified_dependency(result.aur, dependency, package_name);
            } else {
                result.unknown.push_back(dependency);
            }
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR dependency " + package_name + ": " + e.what());
            result.unknown.push_back(dependency);
        }
    }

    return result;
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

int cmd_deps(const std::vector<std::string>& targets) {
    if(targets.empty()) {
        Logger::error("Usage: jpacker deps <pkg>");
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
            print_dependency_group("Unknown dependencies:", classified.unknown);
        } catch(const std::exception& e) {
            Logger::error("Failed to inspect dependencies for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_sync_info(const std::vector<std::string>& args, const std::vector<std::string>& flags, const std::vector<std::string>& targets) {
    if(targets.empty()) return run_command("pacman " + join_shell_args(args));

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
        if(run_command("pacman " + join_shell_args(pacman_args)) != 0) failed = true;
        if(!aur_infos.empty()) std::cout << std::endl;
    }

    for(size_t i = 0; i < aur_infos.size(); ++i) {
        if(i > 0) std::cout << std::endl;
        print_aur_info(aur_infos[i]);
    }

    return failed ? 1 : 0;
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

// --- Build Logic ---
// 【Updated】 only_if_updated フラグを追加
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
                if(current_url.find(git_url) == std::string::npos && git_url.find(current_url) == std::string::npos) {
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
                        if(ask_user("Updates detected. View diff?")) {
                            run_command("git diff " + shell_quote("HEAD.." + remote_ref) + " --color=always");
                        }
                    }
                }

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

        // 【New!】 更新チェック (only_if_updated = true の場合のみ)
        if(only_if_updated) {
            if(!is_update_needed(pkg_name)) {
                return;// 更新不要なので終了
            }
        }

        if(!g_config.no_edit) {
            if(ask_user("Edit PKGBUILD?")) {
                const char* env_editor = std::getenv("EDITOR");
                std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;
                if(run_command(build_editor_command(editor_cmd, "PKGBUILD")) != 0) {
                    throw std::runtime_error("Editor failed.");
                }
                if(!ask_user("Proceed with build?")) throw std::runtime_error("Aborted.");
            }
        } else {
            Logger::info("Skipping PKGBUILD review (--noedit).");
        }
        std::string build_cmd;
        if(!trim(custom_env).empty()) {
            Logger::info("Applying custom build flags: " + custom_env);
            build_cmd = custom_env + "makepkg -sic";
        } else {
            Logger::info("Using default makepkg.conf settings.");
            build_cmd = "makepkg -sic";
        }
        if(run_command(build_cmd) != 0) throw std::runtime_error("Build failed.");
    }
}

// 【Updated】引数追加
void install_smart_source(const std::string& pkg_name, bool only_if_updated) {
    std::string env = get_package_env(pkg_name);
    PackageBuildSource source = resolve_build_source(pkg_name);

    build_from_git(source.requested_name, source.clone_name, source.git_url, env, only_if_updated);
}

// --- Commands ---
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
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
        if(!fs::exists(p) && run_command("sudo touch " + shell_quote(p.string())) != 0) {
            Logger::error("Failed to create " + p.string());
            failed = true;
            continue;
        }
        if(run_command("sudo " + build_editor_command(editor_cmd, p)) != 0) {
            Logger::error("Editor failed for " + p.string());
            failed = true;
        }
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
        Logger::info("Reinstalling binaries: " + pkg_list);
        if(run_command("sudo pacman -S " + pkg_list) != 0) throw std::runtime_error("Failed to reinstall binaries.");
    }
    if(failed) throw std::runtime_error("Failed to revert one or more packages.");
}
int cmd_clean() {
    bool failed = false;
    Logger::info("Cleaning package caches...");
    if(run_command("sudo pacman -Sc") != 0) {
        Logger::warn("Pacman clean failed or cancelled.");
        failed = true;
    }
    fs::path cache = get_cache_dir();
    if(fs::exists(cache) && !fs::is_empty(cache)) {
        if(ask_user("Clean jpacker build cache (" + cache.string() + ")?")) {
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
    if(run_command("sudo pacman -Syu") != 0) throw std::runtime_error("Update failed.");
    if(fs::exists(PACKAGE_BUILD_DIR)) {
        Logger::info("Checking source packages...");
        for(const auto& entry : fs::directory_iterator(PACKAGE_BUILD_DIR)) {
            if(entry.is_regular_file()) {
                std::string pkg_name = entry.path().filename().string();
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

void print_help() {
    std::cout << "\033[1;36mjpacker\033[0m v" << VERSION << "\n"
              << std::endl;
    std::cout << "\033[1mUSAGE\033[0m" << std::endl;
    std::cout << "    jpacker <op> [options] [targets...]\n"
              << std::endl;
    std::cout << "\033[1mOPERATIONS\033[0m" << std::endl;
    std::cout << "    \033[1mbuild\033[0m <pkg> [V=K]  One-off build" << std::endl;
    std::cout << "    \033[1mupgrade\033[0m              System update & rebuilds" << std::endl;
    std::cout << "    \033[1mclean\033[0m                Clean package caches" << std::endl;
    std::cout << "    \033[1mdeps\033[0m <pkg>           Classify AUR dependencies" << std::endl;
    std::cout << "    \033[1madd-src\033[0m <pkg> [V=K]  Mark pkg for source build" << std::endl;
    std::cout << "    \033[1medit-src\033[0m <pkg>       Edit config" << std::endl;
    std::cout << "    \033[1mlist-src\033[0m             List registered source pkgs" << std::endl;
    std::cout << "    \033[1mdel-src\033[0m <pkg>        Unmark pkg" << std::endl;
    std::cout << "    \033[1mrevert\033[0m <pkg>         Unmark & reinstall binary" << std::endl;
    std::cout << "    \033[1m-S, -Syu\033[0m             Install/Update" << std::endl;
    std::cout << "    \033[1m-Ss\033[0m <query>          Search" << std::endl;
    std::cout << "    \033[1m-Qua\033[0m                 Check AUR/foreign updates" << std::endl;
    std::cout << "\033[1mOPTIONS\033[0m" << std::endl;
    std::cout << "    \033[1m--noedit\033[0m             Skip review" << std::endl;
    std::cout << "    \033[1m--nodiff\033[0m             Skip diff prompt" << std::endl;
    std::cout << "\033[1mCONFIG\033[0m" << std::endl;
    std::cout << "    jpacker.conf: LOGFILE=..., NOEDIT=..., NODIFF=..." << std::endl;
}

int main(int argc, char* argv[]) {
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
    std::string first_arg = argv[1];
    if(first_arg == "-h" || first_arg == "--help") {
        print_help();
        return 0;
    }

    std::vector<std::string> args, targets, flags;
    std::string              operation = first_arg;
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
        if(i > 1) {
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
            return cmd_deps(targets);
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

        bool is_sync = (operation.find("-S") == 0);
        bool is_query = (operation.find("-Q") == 0);
        bool is_foreign_updates = (is_query && operation.find('u') != std::string::npos && operation.find('a') != std::string::npos);
        bool is_search = (is_sync && operation.find('s') != std::string::npos);
        bool is_info = (is_sync && operation.find('i') != std::string::npos);
        bool is_clean = (is_sync && operation.find('c') != std::string::npos);
        bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));
        bool needs_sudo = (is_sync || operation.find("-R") == 0 || operation.find("-U") == 0 || operation.find("-D") == 0);

        if(is_foreign_updates) return cmd_query_foreign_updates();

        if(is_search) {
            if(targets.empty()) {
                Logger::error("Missing search query.");
                return 1;
            }
            int pacman_ret = run_command("pacman " + join_shell_args(args));
            Logger::info("Searching AUR...");
            bool aur_found = search_aur(targets);
            return (pacman_ret == 0 || aur_found) ? 0 : 1;
        }
        if(is_info) return cmd_sync_info(args, flags, targets);
        if(is_clean) return run_command("pacman " + join_shell_args(args));

        if(is_sync) {
            if(targets.empty()) return run_command("sudo pacman " + join_shell_args(args));
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
            if(!repo_targets.empty() || is_sys_upgrade) {
                std::string cmd = "sudo pacman " + join_shell_args(flags);
                if(!repo_targets.empty()) cmd += " " + join_shell_args(repo_targets);
                if(run_command(cmd) != 0) throw std::runtime_error("Pacman failed.");
            }
            if(!aur_targets.empty()) {
                // 通常インストール(-S)は false (強制的にインストール/再ビルド)
                for(const auto& pkg : aur_targets)
                    install_smart_source(pkg, false);
            }
            return 0;
        }
        std::string cmd_args = "";
        for(const auto& arg : args) {
            if(arg == "--noedit") continue;
            if(arg == "--nodiff") continue;
            if(!cmd_args.empty()) cmd_args += " ";
            cmd_args += shell_quote(arg);
        }
        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + cmd_args);
    } catch(const std::exception& e) {
        Logger::error(e.what());
        return 1;
    }
}
