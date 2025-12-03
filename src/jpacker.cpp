/**
 * jpacker - A full-featured Pacman wrapper and AUR helper
 * v1.0.2 Features:
 * - Fix: Automatically resets cache if remote URL changes (e.g., AUR -> Official)
 * - Fix: Branch detection logic improved
 */

#include <algorithm>
#include <array>// New
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>// New
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 設定 ---
#ifndef JPKG_VERSION
#define JPKG_VERSION "1.0.2"
#endif

const std::string VERSION = JPKG_VERSION;
const std::string AUR_RPC_URL = "https://aur.archlinux.org/rpc/v5/search/";
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

// --- ユーティリティ ---
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

fs::path expand_path(const std::string& path_str) {
    if(path_str.empty()) return "";
    if(path_str[0] == '~') {
        const char* home = std::getenv("HOME");
        if(!home) throw std::runtime_error("HOME environment variable not set.");
        return fs::path(home) / path_str.substr(1);
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
        size_t comment_pos = line.find('#');
        if(comment_pos != std::string::npos) line = line.substr(0, comment_pos);
        if(trim(line).empty()) continue;
        std::stringstream ss(line);
        std::string       key, val;
        if(std::getline(ss, key, '=') && std::getline(ss, val)) {
            key = to_lower(trim(key));
            val = trim(val);
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

// --- Helpers ---
bool is_force_source(const std::string& pkg_name) {
    fs::path target = fs::path(PACKAGE_BUILD_DIR) / pkg_name;
    return fs::exists(target);
}

std::string get_package_env(const std::string& pkg_name) {
    fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg_name;
    if(!fs::exists(p)) return "";
    std::ifstream file(p);
    std::string   line, env_str;
    Logger::info("Loading custom build flags from " + p.string());
    while(std::getline(file, line)) {
        size_t comment = line.find('#');
        if(comment != std::string::npos) line = line.substr(0, comment);
        if(trim(line).empty()) continue;
        size_t eq_pos = line.find('=');
        if(eq_pos != std::string::npos) {
            std::string key = trim(line.substr(0, eq_pos));
            std::string val = trim(line.substr(eq_pos + 1));
            if(!val.empty()) {
                if(val.front() != '"' && val.front() != '\'') val = "\"" + val + "\"";
                env_str += key + "=" + val + " ";
            }
        }
    }
    return env_str;
}

// コマンド出力を取得するヘルパー
std::string exec_command(const char* cmd) {
    std::array<char, 128>                    buffer;
    std::string                              result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if(!pipe) return "";
    while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return trim(result);
}

std::string get_git_branch() {
    if(std::system("git show-ref --verify --quiet refs/remotes/origin/main") == 0) return "main";
    if(std::system("git show-ref --verify --quiet refs/remotes/origin/master") == 0) return "master";
    return "master";
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
int run_command(const std::string& cmd) {
    Logger::raw_cmd(cmd);
    return std::system(cmd.c_str());
}
std::string join_args(const std::vector<std::string>& args) {
    std::stringstream ss;
    for(size_t i = 0; i < args.size(); ++i) {
        if(i > 0) ss << " ";
        ss << args[i];
    }
    return ss.str();
}
bool is_repo_package(const std::string& pkg_name) {
    std::string cmd = "pacman -Si " + pkg_name + " > /dev/null 2>&1";
    return (std::system(cmd.c_str()) == 0);
}
bool ask_user(const std::string& question) {
    std::cout << ":: " << question << " [Y/n] ";
    std::string input;
    std::getline(std::cin, input);
    return (input.empty() || to_lower(input) == "y" || to_lower(input) == "yes");
}

class AurClient {
public:
    static std::string search_query(const std::string& query) {
        CurlHandle  handle;
        std::string readBuffer;
        std::string url = AUR_RPC_URL + query;
        curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, USER_AGENT.c_str());
        CURLcode res = curl_easy_perform(handle.get());
        if(res != CURLE_OK) return "";
        return readBuffer;
    }
};
void search_aur(const std::vector<std::string>& keywords) {
    for(const auto& pkg_name : keywords) {
        if(pkg_name[0] == '-') continue;
        std::string response = AurClient::search_query(pkg_name);
        if(response.empty()) continue;
        try {
            auto j = json::parse(response);
            if(j.contains("results") && j["results"].is_array()) {
                for(const auto& pkg : j["results"]) {
                    std::cout << "\033[1;35maur\033[0m/\033[1m" << pkg["Name"].get<std::string>()
                              << "\033[0m \033[1;32m" << pkg["Version"].get<std::string>() << "\033[0m" << std::endl;
                    if(pkg.contains("Description") && !pkg["Description"].is_null())
                        std::cout << "    " << pkg["Description"].get<std::string>() << std::endl;
                }
            }
        } catch(...) {
        }
    }
}

// --- Build Logic ---
void build_from_git(const std::string& pkg_name, const std::string& git_url, const std::string& custom_env) {
    Logger::info("Building " + pkg_name + " from source (" + git_url + ")...");
    fs::path build_base = get_cache_dir();
    fs::path pkg_dir = build_base / pkg_name;
    if(!fs::exists(build_base)) fs::create_directories(build_base);

    {
        WorkDirGuard wd(build_base);
        bool         needs_clone = true;

        if(fs::exists(pkg_dir) && fs::exists(pkg_dir / ".git")) {
            // 【New!】URLの整合性チェック
            {
                WorkDirGuard wd_repo(pkg_dir);
                std::string  current_url = exec_command("git config --get remote.origin.url");
                // 末尾の.gitの有無などの揺らぎを吸収するため、部分一致や単純比較を行う
                // ここでは単純化のため、不一致なら再クローンとする
                if(current_url.find(git_url) == std::string::npos && git_url.find(current_url) == std::string::npos) {
                    Logger::warn("Remote URL mismatch. Re-cloning...");
                    Logger::info("Old: " + current_url);
                    Logger::info("New: " + git_url);
                    // ここで削除するが、WorkDirGuardでpkg_dirに入っている状態だと削除できない可能性があるため
                    // 一旦フラグを立てて、ブロックを抜けた後で削除するなどの工夫がいるが
                    // 単純に親ディレクトリ(build_base)にいる状態(=ここ)で削除すればOK
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
                    int diff_ret = std::system(("git diff --quiet HEAD..origin/" + branch).c_str());
                    if(diff_ret != 0) {
                        if(ask_user("Updates detected. View diff?")) {
                            run_command("git diff HEAD..origin/" + branch + " --color=always");
                        }
                    }
                } else {
                    Logger::info("Skipping diff review (--nodiff).");
                }

                if(run_command("git reset --hard origin/" + branch) != 0) {
                    throw std::runtime_error("Failed to reset repository.");
                }
            }
        }

        if(needs_clone) {
            if(fs::exists(pkg_dir)) fs::remove_all(pkg_dir);// 不整合がある場合もここで消える
            Logger::info("Cloning repository...");
            DirCleanupGuard cleanup_guard(pkg_dir);
            if(run_command("git clone " + git_url + " " + pkg_name) != 0) {
                throw std::runtime_error("Failed to clone " + pkg_name);
            }
            cleanup_guard.commit();
        }
    }

    {
        WorkDirGuard wd(pkg_dir);
        if(!g_config.no_edit) {
            if(ask_user("Edit PKGBUILD?")) {
                const char* env_editor = std::getenv("EDITOR");
                std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;
                run_command(editor_cmd + " PKGBUILD");
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

void install_smart_source(const std::string& pkg_name) {
    std::string env = get_package_env(pkg_name);
    std::string git_url;

    if(is_repo_package(pkg_name)) {
        git_url = ARCH_GIT_BASE + pkg_name + ".git";
        Logger::info("Target is official repo package. Using: " + git_url);
    } else {
        git_url = AUR_BASE_URL + pkg_name + ".git";
        Logger::info("Target is AUR package. Using: " + git_url);
    }

    build_from_git(pkg_name, git_url, env);
}

// --- Commands ---
void cmd_build(const std::vector<std::string>& args) {
    if(args.empty()) {
        Logger::error("Usage: jpacker build <pkg> [VAR=VAL...]");
        return;
    }
    std::string pkg_name, custom_env;
    for(const auto& arg : args) {
        if(arg.find('=') != std::string::npos)
            custom_env += arg + " ";
        else if(pkg_name.empty())
            pkg_name = arg;
        else
            Logger::warn("Ignoring extra arg '" + arg + "'");
    }
    if(pkg_name.empty()) {
        Logger::error("No package specified.");
        return;
    }

    std::string git_url;
    if(is_repo_package(pkg_name)) {
        Logger::info("Package found in official repos.");
        git_url = ARCH_GIT_BASE + pkg_name + ".git";
    } else {
        Logger::info("Package assumed to be in AUR.");
        git_url = AUR_BASE_URL + pkg_name + ".git";
    }
    try {
        build_from_git(pkg_name, git_url, custom_env);
    } catch(const std::exception& e) {
        Logger::error(std::string("Build Error: ") + e.what());
        if(is_repo_package(pkg_name)) Logger::warn("Hint: Official repo names might differ from pkg names.");
    }
}

void cmd_add_src(const std::vector<std::string>& args) {
    std::vector<std::string> current_pkgs;
    for(const auto& arg : args) {
        if(arg.find('=') == std::string::npos) {
            fs::path p = fs::path(PACKAGE_BUILD_DIR) / arg;
            if(run_command("sudo touch " + p.string()) != 0)
                Logger::error("Failed to add " + arg);
            else {
                Logger::info("Added " + arg + " to source-build list.");
                current_pkgs.push_back(p.string());
            }
        } else {
            if(current_pkgs.empty()) continue;
            for(const auto& pkg_path : current_pkgs) {
                Logger::info("   -> Appending " + arg + " to " + pkg_path);
                run_command("echo '" + arg + "' | sudo tee -a " + pkg_path + " > /dev/null");
            }
        }
    }
}
void cmd_edit_src(const std::vector<std::string>& targets) {
    const char* env_editor = std::getenv("EDITOR");
    std::string editor = (env_editor) ? std::string(env_editor) : g_config.editor;
    for(const auto& pkg : targets) {
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
        if(!fs::exists(p)) run_command("sudo touch " + p.string());
        run_command("sudo " + editor + " " + p.string());
    }
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
                size_t comment = line.find('#');
                if(comment != std::string::npos) line = line.substr(0, comment);
                if(!trim(line).empty()) std::cout << "    " << trim(line) << std::endl;
            }
        }
    }
    if(!found) std::cout << "  (none)" << std::endl;
}
void cmd_del_src(const std::vector<std::string>& targets) {
    for(const auto& pkg : targets) {
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
        Logger::info("Removing " + pkg + " from list...");
        run_command("sudo rm -f " + p.string());
    }
}
void cmd_revert(const std::vector<std::string>& targets) {
    std::vector<std::string> reinstall_targets;
    for(const auto& pkg : targets) {
        fs::path p = fs::path(PACKAGE_BUILD_DIR) / pkg;
        if(fs::exists(p)) {
            Logger::info("Unmarking source-build for " + pkg);
            run_command("sudo rm -f " + p.string());
        } else
            Logger::warn(pkg + " was not marked.");
        if(is_repo_package(pkg)) {
            Logger::info(pkg + " exists in official repos. Will reinstall binary.");
            reinstall_targets.push_back(pkg);
        } else
            Logger::info(pkg + " is likely an AUR package. Config removed only.");
    }
    if(!reinstall_targets.empty()) {
        std::string pkg_list = join_args(reinstall_targets);
        Logger::info("Reinstalling binaries: " + pkg_list);
        if(run_command("sudo pacman -S " + pkg_list) != 0) throw std::runtime_error("Failed to reinstall binaries.");
    }
}
void cmd_clean() {
    Logger::info("Cleaning package caches...");
    if(run_command("sudo pacman -Sc") != 0) Logger::warn("Pacman clean failed or cancelled.");
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
}
void cmd_upgrade() {
    Logger::info("System upgrade...");
    if(run_command("sudo pacman -Syu") != 0) throw std::runtime_error("Update failed.");
    if(fs::exists(PACKAGE_BUILD_DIR)) {
        Logger::info("Rebuilding source packages...");
        for(const auto& entry : fs::directory_iterator(PACKAGE_BUILD_DIR)) {
            if(entry.is_regular_file()) {
                std::string pkg_name = entry.path().filename().string();
                try {
                    install_smart_source(pkg_name);
                } catch(const std::exception& e) {
                    Logger::error("Error updating " + pkg_name + ": " + e.what());
                }
            }
        }
    }
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
    std::cout << "    \033[1madd-src\033[0m <pkg> [V=K]  Mark pkg for source build" << std::endl;
    std::cout << "    \033[1medit-src\033[0m <pkg>       Edit config" << std::endl;
    std::cout << "    \033[1mlist-src\033[0m             List registered source pkgs" << std::endl;
    std::cout << "    \033[1mdel-src\033[0m <pkg>        Unmark pkg" << std::endl;
    std::cout << "    \033[1mrevert\033[0m <pkg>         Unmark & reinstall binary" << std::endl;
    std::cout << "    \033[1m-S, -Syu\033[0m             Install/Update" << std::endl;
    std::cout << "    \033[1m-Ss\033[0m <query>          Search" << std::endl;
    std::cout << "\033[1mOPTIONS\033[0m" << std::endl;
    std::cout << "    \033[1m--noedit\033[0m             Skip review" << std::endl;
    std::cout << "    \033[1m--nodiff\033[0m             Skip diff prompt" << std::endl;
    std::cout << "\033[1mCONFIG\033[0m" << std::endl;
    std::cout << "    jpacker.conf: LOGFILE=..., NOEDIT=..., NODIFF=..." << std::endl;
}

int main(int argc, char* argv[]) {
    CurlGlobal curl_global;
    try {
        fs::path log_dir = get_cache_dir();
        if(!fs::exists(log_dir)) fs::create_directories(log_dir);
        Logger::init(log_dir / "jpacker.log");
        load_config();
        if(!g_config.log_file.empty()) Logger::init(expand_path(g_config.log_file));
        Logger::info("Started jpacker v" + VERSION);
    } catch(...) {
        std::cerr << "Warning: Failed to initialize log." << std::endl;
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

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg == "--noedit") {
            g_config.no_edit = true;
            continue;
        }
        if(arg == "--nodiff") {
            g_config.no_diff = true;
            continue;
        }
        if(i > 1) {
            if(arg[0] == '-')
                flags.push_back(arg);
            else
                targets.push_back(arg);
        }
        args.push_back(arg);
    }

    try {
        if(operation == "build") {
            cmd_build(targets);
            return 0;
        }
        if(operation == "upgrade") {
            cmd_upgrade();
            return 0;
        }
        if(operation == "clean") {
            cmd_clean();
            return 0;
        }
        if(operation == "add-src" && !targets.empty()) {
            cmd_add_src(targets);
            return 0;
        }
        if(operation == "del-src" && !targets.empty()) {
            cmd_del_src(targets);
            return 0;
        }
        if(operation == "revert" && !targets.empty()) {
            cmd_revert(targets);
            return 0;
        }
        if(operation == "edit-src" && !targets.empty()) {
            cmd_edit_src(targets);
            return 0;
        }
        if(operation == "list-src") {
            cmd_list_src();
            return 0;
        }

        bool is_sync = (operation.find("-S") == 0);
        bool is_search = (is_sync && operation.find('s') != std::string::npos);
        bool is_info = (is_sync && operation.find('i') != std::string::npos);
        bool is_clean = (is_sync && operation.find('c') != std::string::npos);
        bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));
        bool needs_sudo = (is_sync || operation.find("-R") == 0 || operation.find("-U") == 0 || operation.find("-D") == 0);

        if(is_search) {
            run_command("pacman " + join_args(args));
            Logger::info("Searching AUR...");
            search_aur(targets);
            return 0;
        }
        if(is_info || is_clean) return run_command("pacman " + join_args(args));

        if(is_sync) {
            if(targets.empty()) return run_command("sudo pacman " + join_args(args));
            std::vector<std::string> repo_targets, aur_targets;
            for(const auto& t : targets) {
                if(is_force_source(t))
                    aur_targets.push_back(t);
                else if(is_repo_package(t))
                    repo_targets.push_back(t);
                else
                    aur_targets.push_back(t);
            }
            if(!repo_targets.empty() || is_sys_upgrade) {
                std::string cmd = "sudo pacman " + join_args(flags);
                if(!repo_targets.empty()) cmd += " " + join_args(repo_targets);
                if(run_command(cmd) != 0) throw std::runtime_error("Pacman failed.");
            }
            if(!aur_targets.empty()) {
                for(const auto& pkg : aur_targets)
                    install_smart_source(pkg);
            }
            return 0;
        }
        std::string cmd_args = "";
        for(const auto& arg : args) {
            if(arg == "--noedit") continue;
            if(arg == "--nodiff") continue;
            if(!cmd_args.empty()) cmd_args += " ";
            cmd_args += arg;
        }
        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + cmd_args);
    } catch(const std::exception& e) {
        Logger::error(e.what());
        return 1;
    }
}
