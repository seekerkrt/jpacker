#include "repository_query.hpp"

#include "dependency_spec.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// repository query固有のoutput parsingは、汎用string utilityへ持ち上げず局所保持する。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string repo_name_from_sync_db(const fs::path& db_path) {
    std::string filename = db_path.filename().string();
    const std::string suffix = ".db";
    if(filename.length() > suffix.length() && filename.substr(filename.length() - suffix.length()) == suffix) {
        return filename.substr(0, filename.length() - suffix.length());
    }
    return db_path.stem().string();
}

bool same_repo_provider(const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.repository == rhs.repository && lhs.package_name == rhs.package_name;
}

void add_repo_provider_candidate(
        std::vector<ProvidedDependency>& candidates, const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_repo_provider(existing, provider);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) != candidates.end()) return;
    candidates.push_back(provider);
}

void add_repo_provider(
        std::map<std::string, std::vector<ProvidedDependency>>& providers, const std::string& provided,
        const std::string& repository, const std::string& package_name) {
    std::string provided_name = provided_dependency_name(provided);
    if(provided_name.empty() || !is_valid_package_name(provided_name)) return;
    add_repo_provider_candidate(providers[provided_name], ProvidedDependency{repository, package_name});
}

void parse_repo_sync_desc(
        const std::string& desc, const std::string& repository,
        std::map<std::string, std::vector<ProvidedDependency>>& providers) {
    std::stringstream        ss(desc);
    std::string              line;
    std::string              package_name;
    std::vector<std::string> package_provides;
    std::string              section;

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
        std::string cmd = "bsdtar -xOf " + shell_words::quote(db_path.string()) + " '*/desc' 2>/dev/null";
        std::string desc = exec_command(cmd.c_str());
        if(desc.empty()) continue;

        parse_repo_sync_desc(desc, repo_name_from_sync_db(db_path), s_providers);
    }

    return s_providers;
}

} // namespace

bool is_installed_package(const std::string& pkg_name) {
    if(pkg_name.empty()) return false;
    return command_status("pacman -Q " + shell_words::quote(pkg_name) + " > /dev/null 2>&1") == 0;
}

std::optional<std::string> get_installed_package_version(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    CapturedCommandResult query_result = capture_command_output(
            ("pacman -Q " + shell_words::quote(pkg_name) + " 2>/dev/null").c_str());
    if(query_result.output.empty()) {
        if(query_result.exit_code == 0 || query_result.exit_code == 1) return std::nullopt;
        throw std::runtime_error("Failed to query installed package version for " + pkg_name + ".");
    }
    if(query_result.exit_code != 0) {
        throw std::runtime_error("Failed to query installed package version for " + pkg_name + ".");
    }

    std::stringstream output_stream(query_result.output);
    std::string       output_package_name;
    std::string       installed_version;
    std::string       unexpected_field;
    if(!(output_stream >> output_package_name >> installed_version) ||
       output_package_name != pkg_name || output_stream >> unexpected_field) {
        throw std::runtime_error("Invalid pacman -Q output for " + pkg_name + ".");
    }
    return installed_version;
}

bool is_repo_package(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    std::string cmd = "pacman -Si " + shell_words::quote(pkg_name) + " > /dev/null 2>&1";
    return (command_status(cmd) == 0);
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
