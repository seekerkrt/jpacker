/**
 * jpacker - A full-featured Pacman wrapper and AUR helper
 * v4.2.0 Features:
 * - Improved Help/Usage display with ANSI colors
 * - Configuration file support (/etc/jpacker/jpacker.conf)
 * - PKGBUILD review/edit feature
 */

#include <algorithm>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 設定 (Defaults) ---
// Makefileから渡されなかった場合のフォールバック
#ifndef JPKG_VERSION
#define JPKG_VERSION "0.0.0-dev"
#endif

const std::string VERSION = JPKG_VERSION;
const std::string AUR_RPC_URL = "https://aur.archlinux.org/rpc/v5/search/";
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string USER_AGENT = "jpacker/" + VERSION;
const std::string CONFIG_FILE = "/etc/jpacker/jpacker.conf";

// --- グローバル設定保持用 ---
struct AppConfig {
    bool        no_edit = false;
    std::string editor = "nano";
};

AppConfig g_config;

// --- 文字列ユーティリティ ---
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

// --- 設定ファイル読み込み ---
void load_config() {
    if(!fs::exists(CONFIG_FILE)) return;

    std::ifstream file(CONFIG_FILE);
    std::string   line;
    while(std::getline(file, line)) {
        size_t comment_pos = line.find('#');
        if(comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        if(trim(line).empty()) continue;

        std::stringstream ss(line);
        std::string       key, val;
        if(std::getline(ss, key, '=') && std::getline(ss, val)) {
            key = to_lower(trim(key));
            val = trim(val);

            if(key == "noedit") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") {
                    g_config.no_edit = true;
                }
            } else if(key == "editor") {
                if(!val.empty()) g_config.editor = val;
            }
        }
    }
}

// --- RAII Classes ---
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

// --- ヘルプ表示 (New!) ---
void print_help() {
    std::cout << "\033[1;36mjpacker\033[0m v" << VERSION << " - A minimal AUR helper & Pacman wrapper\n"
              << std::endl;

    std::cout << "\033[1mUSAGE\033[0m" << std::endl;
    std::cout << "    jpacker <operation> [options] [targets...]\n"
              << std::endl;

    std::cout << "\033[1mOPERATIONS\033[0m" << std::endl;
    std::cout << "    \033[1m-S, -Syu\033[0m       Install packages (Repo/AUR) or update system" << std::endl;
    std::cout << "    \033[1m-Ss\033[0m <query>    Search for packages in official repos and AUR" << std::endl;
    std::cout << "    \033[1m-R, -Rs\033[0m        Remove packages (wraps pacman -R)" << std::endl;
    std::cout << "    \033[1m-Q, -Qi\033[0m        Query local packages (wraps pacman -Q)" << std::endl;
    std::cout << "    \033[1m...\033[0m            Any other pacman operations are passed through\n"
              << std::endl;

    std::cout << "\033[1mOPTIONS\033[0m" << std::endl;
    std::cout << "    \033[1m--noedit\033[0m       Skip reviewing/editing PKGBUILD files" << std::endl;
    std::cout << "    \033[1m-h, --help\033[0m     Display this help message\n"
              << std::endl;

    std::cout << "\033[1mCONFIGURATION\033[0m" << std::endl;
    std::cout << "    File: " << CONFIG_FILE << std::endl;
    std::cout << "    Vars: NOEDIT=true|false, EDITOR=nano|vim\n"
              << std::endl;

    std::cout << "\033[1mEXAMPLES\033[0m" << std::endl;
    std::cout << "    jpacker -Syu google-chrome" << std::endl;
    std::cout << "    jpacker -Ss visual studio code" << std::endl;
    std::cout << "    jpacker -S firefox --noedit" << std::endl;
}

// --- ヘルパー関数 ---

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    ((std::string*)userp)->append((char*)contents, total_size);
    return total_size;
}

int run_command(const std::string& cmd) {
    std::cout << "\033[1;33m::\033[0m Running: " << cmd << std::endl;
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

// --- AUR クライアント ---
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

// --- コア機能 ---

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

void install_aur(const std::string& pkg_name) {
    std::cout << ":: Processing AUR package: " << pkg_name << "..." << std::endl;
    fs::path build_base = fs::temp_directory_path() / "jpacker_build";
    fs::path pkg_dir = build_base / pkg_name;

    if(fs::exists(pkg_dir)) fs::remove_all(pkg_dir);
    fs::create_directories(build_base);

    // 1. Clone
    {
        WorkDirGuard wd(build_base);
        if(run_command("git clone " + AUR_BASE_URL + pkg_name + ".git") != 0) {
            throw std::runtime_error("Failed to clone " + pkg_name);
        }
    }

    // 2. Edit / Review
    {
        WorkDirGuard wd(pkg_dir);

        if(!g_config.no_edit) {
            if(ask_user("Edit PKGBUILD?")) {
                const char* env_editor = std::getenv("EDITOR");
                std::string editor_cmd = (env_editor) ? std::string(env_editor) : g_config.editor;

                run_command(editor_cmd + " PKGBUILD");

                if(!ask_user("Proceed with installation?")) {
                    throw std::runtime_error("Installation aborted by user.");
                }
            }
        } else {
            std::cout << ":: Skipping PKGBUILD review (--noedit is active)." << std::endl;
        }

        // 3. Build
        if(run_command("makepkg -sic") != 0) {
            throw std::runtime_error("Build failed for " + pkg_name);
        }
    }
}

// --- コマンド解析と実行 ---

int main(int argc, char* argv[]) {
    CurlGlobal curl_global;
    load_config();

    // 引数なしの場合
    if(argc < 2) {
        print_help();
        return 1;
    }

    // ヘルプフラグの確認
    std::string first_arg = argv[1];
    if(first_arg == "-h" || first_arg == "--help") {
        print_help();
        return 0;
    }

    // 引数解析
    std::vector<std::string> args;
    std::vector<std::string> targets;
    std::vector<std::string> flags;
    std::string              operation = first_arg;
    flags.push_back(operation);

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if(arg == "--noedit") {
            g_config.no_edit = true;
            continue;
        }

        if(i > 1) {
            if(arg[0] == '-') {
                flags.push_back(arg);
            } else {
                targets.push_back(arg);
            }
        }
        args.push_back(arg);
    }

    bool is_sync = (operation.find("-S") == 0);
    bool is_remove = (operation.find("-R") == 0);
    bool is_upgrade = (operation.find("-U") == 0);
    bool is_database = (operation.find("-D") == 0);
    bool needs_sudo = (is_sync || is_remove || is_upgrade || is_database);

    bool is_search = (is_sync && operation.find('s') != std::string::npos);
    bool is_info = (is_sync && operation.find('i') != std::string::npos);
    bool is_clean = (is_sync && operation.find('c') != std::string::npos);
    bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));

    try {
        if(is_search) {
            run_command("pacman " + join_args(args));
            std::cout << ":: Searching AUR..." << std::endl;
            search_aur(targets);
            return 0;
        }

        if(is_info || is_clean) {
            return run_command("pacman " + join_args(args));
        }

        if(is_sync) {
            if(targets.empty()) {
                return run_command("sudo pacman " + join_args(args));
            }

            std::vector<std::string> repo_targets;
            std::vector<std::string> aur_targets;

            for(const auto& t : targets) {
                if(is_repo_package(t))
                    repo_targets.push_back(t);
                else
                    aur_targets.push_back(t);
            }

            if(!repo_targets.empty() || is_sys_upgrade) {
                std::string cmd = "sudo pacman " + join_args(flags);
                if(!repo_targets.empty()) {
                    cmd += " " + join_args(repo_targets);
                }
                if(run_command(cmd) != 0) {
                    throw std::runtime_error("Pacman operation failed.");
                }
            }

            if(!aur_targets.empty()) {
                for(const auto& pkg : aur_targets) {
                    install_aur(pkg);
                }
            }
            return 0;
        }

        std::string cmd_args = "";
        for(const auto& arg : args) {
            if(arg == "--noedit") continue;
            if(!cmd_args.empty()) cmd_args += " ";
            cmd_args += arg;
        }

        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + cmd_args);

    } catch(const std::exception& e) {
        std::cerr << "\033[1;31mError:\033[0m " << e.what() << std::endl;
        return 1;
    }
}
