#include "repository_query.hpp"

#include "dependency_provider.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <cstdlib>
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
        return RepositoryPackagePresent{
                "test", 0, package_name, package_name,
                ObservedVersion::available(
                        ObservedVersionSource::RepositoryExactPackage,
                        "1.0-1")};
    }
    return RepositoryPackageNotFound{};
}

InstalledExactPackageObservationResult query_installed_exact_package_strict(
        const std::string& package_name) {
    const char* scenario = std::getenv("MOGUET_TEST_INSPECTION_SCENARIO");
    if(scenario != nullptr &&
       std::string(scenario) == "deps-installed-query-failure" &&
       package_name == "installed-query-failure") {
        return InstalledExactPackageQueryFailure{
                package_name,
                PackageMetadataFailure{
                        PackageMetadataErrorCode::QueryFailed,
                        "installed database query failure"}};
    }
    if(is_installed_package(package_name)) {
        return InstalledExactPackage{
                package_name,
                ObservedVersion::unknown(
                        ObservedVersionSource::InstalledExactPackage,
                        ObservedVersionUnknownReason::MissingVersionMetadata)};
    }
    return InstalledExactPackageAbsent{package_name};
}

std::vector<ProvidedDependency> find_repo_providers(
        const std::string& dependency_name) {
    if(!is_valid_package_name(dependency_name)) return {};

    if(dependency_name == "identity-same-virtual") {
        return {ProvidedDependency::from_repository(
                "extra", "same-package", dependency_name,
                dependency_name, std::nullopt)};
    }
    if(dependency_name == "identity-different-virtual") {
        return {ProvidedDependency::from_repository(
                "extra", "different-package", dependency_name,
                dependency_name, std::nullopt)};
    }
    if(dependency_name == "identity-stale-virtual") {
        return {ProvidedDependency::from_repository(
                "stale", "stale-package", dependency_name,
                dependency_name, std::nullopt)};
    }
    if(dependency_name == "identity-repository-aur-virtual") {
        return {ProvidedDependency::from_repository(
                "aur", "repository-aur-package", dependency_name,
                dependency_name, std::nullopt)};
    }
    if(dependency_name == "identity-ambiguous-virtual" ||
       dependency_name == "ambiguous-only-virtual") {
        return {
                ProvidedDependency::from_repository(
                        "core", "ambiguous-provider-a", dependency_name,
                        dependency_name, std::nullopt),
                ProvidedDependency::from_repository(
                        "extra", "ambiguous-provider-b", dependency_name,
                        dependency_name, std::nullopt)};
    }
    if(dependency_name == "public-conflict-virtual") {
        return {
                ProvidedDependency::from_repository(
                        "core", "public-conflict-provider-a",
                        "public-conflict-virtual",
                        "public-conflict-virtual=2",
                        std::optional<std::string>{"2.0-1"}),
                ProvidedDependency::from_repository(
                        "extra", "public-conflict-provider-b",
                        "public-conflict-virtual",
                        "public-conflict-virtual=3",
                        std::optional<std::string>{"3.0-1"}),
        };
    }

    // AUR provider/unknown fixturesと既存provider-order fixtureは、AUR seamへ委譲する。
    return {};
}

StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name) {
    return RepositoryProviderQuerySnapshot{
            find_repo_providers(dependency_name), {}};
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
