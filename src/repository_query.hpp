#pragma once

#include "dependency_provider.hpp"

#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

// pacman local database から読んだ installed package の最小情報。
struct InstalledPackage {
    std::string name;
    std::string version;
};

// strict repository readで、confirmed absenceとmetadata read failureを分離する。
enum class RepositoryMetadataFailureKind {
    ConfigurationUnavailable,
    ConfigurationMalformed,
    SyncDatabaseUnavailable,
    SyncDatabaseMalformed
};

struct RepositoryMetadataFailure {
    RepositoryMetadataFailureKind kind;
    std::optional<std::string>     repository_name;
    std::string                    diagnostic;

    bool operator==(const RepositoryMetadataFailure&) const = default;
};

struct RepositoryPackagePresent {
    std::string repository_name;

    bool operator==(const RepositoryPackagePresent&) const = default;
};

struct RepositoryPackageNotFound {
    bool operator==(const RepositoryPackageNotFound&) const = default;
};

using StrictRepositoryPackageQueryResult = std::variant<
        RepositoryPackagePresent,
        RepositoryPackageNotFound,
        RepositoryMetadataFailure>;

using StrictRepositoryProvidersQueryResult = std::variant<
        std::vector<ProvidedDependency>,
        RepositoryMetadataFailure>;

bool is_installed_package(const std::string& pkg_name);
bool is_repo_package(const std::string& pkg_name);
std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name);
StrictRepositoryPackageQueryResult query_repository_package_strict(
        const std::string& package_name);
StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name);
std::vector<InstalledPackage> get_foreign_packages();
std::set<std::string> get_foreign_package_names();

#ifdef MOGUET_ENABLE_REPOSITORY_QUERY_TEST_HOOKS
std::vector<ProvidedDependency>
parse_legacy_repository_provider_candidates_for_test(
        const std::string& description,
        const std::string& repository_name,
        const std::string& dependency_name);
#endif
