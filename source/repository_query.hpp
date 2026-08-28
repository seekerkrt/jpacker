#pragma once

#include "dependency_provider.hpp"
#include "package_constraint_metadata.hpp"

#include <cstddef>
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
    std::optional<std::string> repository_name;
    std::string diagnostic;
    std::optional<std::vector<std::string>> configured_repository_order =
        std::nullopt;

    bool operator==(const RepositoryMetadataFailure&) const = default;
};

struct RepositoryPackagePresent {
    std::string repository_name;
    std::size_t configured_order = 0;
    std::string package_name;
    std::string package_base;
    std::optional<ObservedVersion> package_version = std::nullopt;
    std::optional<std::vector<std::string>> configured_repository_order =
        std::nullopt;
    std::vector<RepositoryProviderCapability> provides = {};

    bool operator==(const RepositoryPackagePresent&) const = default;
};

struct RepositoryPackageNotFound {
    std::optional<std::vector<std::string>> configured_repository_order =
        std::nullopt;

    bool operator==(const RepositoryPackageNotFound&) const = default;
};

using StrictRepositoryPackageQueryResult = std::variant<
    RepositoryPackagePresent,
    RepositoryPackageNotFound,
    RepositoryMetadataFailure>;

struct RepositoryProviderQuerySnapshot {
    std::vector<ProvidedDependency> candidates;
    std::vector<RepositoryMetadataFailure> source_failures;
    std::optional<std::vector<std::string>> configured_repository_order =
        std::nullopt;
    std::vector<RepositoryExactPackage> observed_packages = {};

    [[nodiscard]] bool is_complete() const noexcept {
        return source_failures.empty();
    }
};

using StrictRepositoryProvidersQueryResult = std::variant<
    RepositoryProviderQuerySnapshot,
    RepositoryMetadataFailure>;

bool is_installed_package(const std::string& pkg_name);
bool is_repo_package(const std::string& pkg_name);
std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name);
StrictRepositoryPackageQueryResult query_repository_package_strict(
    const std::string& package_name);
// Reuses one already-resolved read-only repository configuration. The result
// remains repository metadata evidence and is not a pacman transaction target.
StrictRepositoryPackageQueryResult query_repository_package_strict(
    const PacmanRepositoryConfiguration& configuration,
    const std::string& package_name);
StrictRepositoryProvidersQueryResult query_repository_providers_strict(
    const std::string& dependency_name);
InstalledExactPackageObservationResult query_installed_exact_package_strict(
    const std::string& package_name);
std::vector<InstalledPackage> get_foreign_packages();
std::set<std::string> get_foreign_package_names();
