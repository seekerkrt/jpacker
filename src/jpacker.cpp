/**
 * jpacker - A full-featured Pacman wrapper and AUR helper
 * * Design Philosophy:
 * - Pass-through: Arguments not handled by jpacker are passed directly to pacman.
 * - Sudo-smart: Automatically applies sudo for write operations (-S, -R, -U).
 * - Hybrid: Combines Repo and AUR operations seamlessly.
 */

#include <algorithm>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// --- 設定 ---
const std::string AUR_RPC_URL = "https://aur.archlinux.org/rpc/v5/search/";
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string USER_AGENT = "jpacker/4.0.1";

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

// 引数リストを文字列に結合 (join_strs は廃止し join_args に統一)
std::string join_args(const std::vector<std::string>& args) {
    std::stringstream ss;
    for(size_t i = 0; i < args.size(); ++i) {
        if(i > 0) ss << " ";
        ss << args[i];
    }
    return ss.str();
}

// 公式リポジトリに存在するかチェック
bool is_repo_package(const std::string& pkg_name) {
    std::string cmd = "pacman -Si " + pkg_name + " > /dev/null 2>&1";
    return (std::system(cmd.c_str()) == 0);
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

// AURビルドロジック (未使用の extra_flags 引数を削除)
void install_aur(const std::string& pkg_name) {
    std::cout << ":: Building AUR package: " << pkg_name << "..." << std::endl;
    fs::path build_base = fs::temp_directory_path() / "jpacker_build";
    fs::path pkg_dir = build_base / pkg_name;

    if(fs::exists(pkg_dir)) fs::remove_all(pkg_dir);
    fs::create_directories(build_base);

    {
        WorkDirGuard wd(build_base);
        if(run_command("git clone " + AUR_BASE_URL + pkg_name + ".git") != 0) {
            throw std::runtime_error("Failed to clone " + pkg_name);
        }
    }

    {
        WorkDirGuard wd(pkg_dir);
        if(run_command("makepkg -sic") != 0) {
            throw std::runtime_error("Build failed for " + pkg_name);
        }
    }
}

// --- コマンド解析と実行 ---

int main(int argc, char* argv[]) {
    CurlGlobal curl_global;

    if(argc < 2) {
        return run_command("pacman");
    }

    std::vector<std::string> args;
    for(int i = 1; i < argc; ++i)
        args.push_back(argv[i]);

    std::string operation = args[0];

    // 動作モード判定
    bool is_sync = (operation.find("-S") == 0);
    bool is_remove = (operation.find("-R") == 0);
    bool is_upgrade = (operation.find("-U") == 0);
    bool is_database = (operation.find("-D") == 0);

    bool needs_sudo = (is_sync || is_remove || is_upgrade || is_database);

    bool is_search = (is_sync && operation.find('s') != std::string::npos);
    bool is_info = (is_sync && operation.find('i') != std::string::npos);
    bool is_clean = (is_sync && operation.find('c') != std::string::npos);
    bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));

    std::vector<std::string> targets;
    std::vector<std::string> flags;
    flags.push_back(operation);

    for(size_t i = 1; i < args.size(); ++i) {
        if(args[i][0] == '-') {
            flags.push_back(args[i]);
        } else {
            targets.push_back(args[i]);
        }
    }

    try {
        // Case A: 検索 (-Ss)
        if(is_search) {
            run_command("pacman " + join_args(args));
            std::cout << ":: Searching AUR..." << std::endl;
            search_aur(targets);
            return 0;
        }

        // Case B: 情報 (-Si)
        if(is_info) {
            return run_command("pacman " + join_args(args));
        }

        // Case C: インストール (-S / -Syu)
        if(is_sync && !is_clean && !is_info) {
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

            // 1. 公式リポジトリ
            if(!repo_targets.empty() || is_sys_upgrade) {
                std::string cmd = "sudo pacman " + join_args(flags);
                if(!repo_targets.empty()) {
                    // ここを修正: join_strs -> join_args
                    cmd += " " + join_args(repo_targets);
                }

                if(run_command(cmd) != 0) {
                    throw std::runtime_error("Pacman operation failed.");
                }
            }

            // 2. AUR
            if(!aur_targets.empty()) {
                for(const auto& pkg : aur_targets) {
                    // ここを修正: 引数を削除
                    install_aur(pkg);
                }
            }
            return 0;
        }

        // Case D: その他 (Pass-through)
        std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
        return run_command(cmd_prefix + join_args(args));

    } catch(const std::exception& e) {
        std::cerr << "\033[1;31mError:\033[0m " << e.what() << std::endl;
        return 1;
    }
}
