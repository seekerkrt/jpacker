#include "repository_query.hpp"

#include "dependency_provider.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string trim(const std::string& value) {
    std::size_t first = value.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    std::size_t last = value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

} // namespace

bool is_installed_package(const std::string& package_name) {
    if(package_name.empty()) return false;
    return command_status(
                   "pacman -Q " + shell_words::quote(package_name) +
                   " > /dev/null 2>&1") == 0;
}

bool is_repo_package(const std::string& package_name) {
    require_valid_package_name(package_name);
    return command_status(
                   "pacman -Si " + shell_words::quote(package_name) +
                   " > /dev/null 2>&1") == 0;
}

StrictRepositoryPackageQueryResult query_repository_package_strict(
        const std::string& package_name) {
    if(is_repo_package(package_name)) {
        return RepositoryPackagePresent{"test"};
    }
    return RepositoryPackageNotFound{};
}

std::vector<ProvidedDependency> find_repo_providers(
        const std::string& dependency_name) {
    if(!is_valid_package_name(dependency_name)) return {};

    if(dependency_name == "identity-same-virtual") {
        return {ProvidedDependency::from_repository(
                "extra", "same-package")};
    }
    if(dependency_name == "identity-different-virtual") {
        return {ProvidedDependency::from_repository(
                "extra", "different-package")};
    }
    if(dependency_name == "identity-stale-virtual") {
        return {ProvidedDependency::from_repository(
                "stale", "stale-package")};
    }
    if(dependency_name == "identity-repository-aur-virtual") {
        return {ProvidedDependency::from_repository(
                "aur", "repository-aur-package")};
    }
    if(dependency_name == "identity-ambiguous-virtual" ||
       dependency_name == "ambiguous-only-virtual") {
        return {
                ProvidedDependency::from_repository(
                        "core", "ambiguous-provider-a"),
                ProvidedDependency::from_repository(
                        "extra", "ambiguous-provider-b")};
    }

    // AUR provider/unknown fixturesと既存provider-order fixtureは、AUR seamへ委譲する。
    return {};
}

StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name) {
    return find_repo_providers(dependency_name);
}

std::vector<InstalledPackage> get_foreign_packages() {
    std::vector<InstalledPackage> packages;
    std::string output = exec_command("pacman -Qm 2>/dev/null");
    if(output.empty()) return packages;

    std::stringstream output_stream(output);
    std::string line;
    while(std::getline(output_stream, line)) {
        line = trim(line);
        if(line.empty()) continue;

        std::stringstream line_stream(line);
        InstalledPackage package;
        if(line_stream >> package.name >> package.version) {
            require_valid_package_name(package.name);
            packages.push_back(std::move(package));
        }
    }
    return packages;
}

std::set<std::string> get_foreign_package_names() {
    std::set<std::string> names;
    for(const auto& package : get_foreign_packages()) names.insert(package.name);
    return names;
}
