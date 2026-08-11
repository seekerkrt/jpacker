#include "repository_query.hpp"

#include "localization.hpp"
#include "package_identifier.hpp"
#include "package_metadata.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

RepositoryMetadataFailureKind repository_failure_kind(
        PackageMetadataErrorCode code,
        bool has_repository_identity) noexcept {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return RepositoryMetadataFailureKind::ConfigurationMalformed;
    case PackageMetadataErrorCode::MalformedMetadata:
        return RepositoryMetadataFailureKind::SyncDatabaseMalformed;
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return RepositoryMetadataFailureKind::SyncDatabaseUnavailable;
    case PackageMetadataErrorCode::ConfigurationUnavailable:
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return RepositoryMetadataFailureKind::ConfigurationUnavailable;
    case PackageMetadataErrorCode::InitializationFailed:
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
    case PackageMetadataErrorCode::InvalidPackageName:
    case PackageMetadataErrorCode::QueryFailed:
        return has_repository_identity
                ? RepositoryMetadataFailureKind::SyncDatabaseUnavailable
                : RepositoryMetadataFailureKind::ConfigurationUnavailable;
    }
    return has_repository_identity
            ? RepositoryMetadataFailureKind::SyncDatabaseUnavailable
            : RepositoryMetadataFailureKind::ConfigurationUnavailable;
}

RepositoryMetadataFailure repository_failure(
        const PackageMetadataFailure& failure,
        std::optional<std::string> repository_name = std::nullopt) {
    return RepositoryMetadataFailure{
            repository_failure_kind(
                    failure.code, repository_name.has_value()),
            std::move(repository_name),
            failure.diagnostic};
}

RepositoryMetadataFailure repository_failure(
        const DependencyConstraintParseFailure&,
        const std::string& repository_name) {
    return RepositoryMetadataFailure{
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            repository_name,
            localization::translate_message(
                    "Repository provider capability metadata is invalid.")};
}

void add_provider_candidate(
        std::vector<ProvidedDependency>& candidates,
        const ProvidedDependency& candidate) {
    const auto same = [&candidate](const ProvidedDependency& existing) {
        return same_provider_identity(existing, candidate);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) ==
       candidates.end()) {
        candidates.push_back(candidate);
    }
}

void add_repository_provider_candidates(
        std::vector<ProvidedDependency>& candidates,
        const RepositoryExactPackage& package,
        const std::string& dependency_name) {
    for(const auto& provided : package.provides) {
        if(provided.capability.package_name() != dependency_name) continue;
        add_provider_candidate(
                candidates,
                ProvidedDependency::from_repository_constraint_metadata(
                        package.repository.repository_name,
                        package.repository.configured_order,
                        package.package_name,
                        ProviderConstraintMetadata{
                                provided.capability,
                                package.package_version,
                                provided.provided_version}));
    }
}

} // namespace

bool is_installed_package(const std::string& pkg_name) {
    if(pkg_name.empty()) return false;
    return command_status(
                   "pacman -Q " + shell_words::quote(pkg_name) +
                   " > /dev/null 2>&1") == 0;
}

bool is_repo_package(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    const std::string command =
            "pacman -Si " + shell_words::quote(pkg_name) +
            " > /dev/null 2>&1";
    return command_status(command) == 0;
}

StrictRepositoryPackageQueryResult query_repository_package_strict(
        const std::string& package_name) {
    require_valid_package_name(package_name);

    PacmanRepositoryConfiguration configuration;
    try {
        configuration = resolve_pacman_repository_configuration();
    } catch(const PackageMetadataError& error) {
        return repository_failure(error.failure());
    }

    RepositoryExactPackageObservationResult result =
            observe_repository_exact_package(configuration, package_name);
    if(const auto* failure =
               std::get_if<RepositoryExactPackageObservationFailure>(&result);
       failure != nullptr) {
        RepositoryMetadataFailure projected =
                repository_failure(failure->failure);
        projected.configured_repository_order =
                configuration.repository_names;
        return projected;
    }

    const RepositoryExactPackageObservation& observation =
            std::get<RepositoryExactPackageObservation>(result);
    for(const auto& source_result : observation.source_results) {
        if(const auto* package =
                   std::get_if<RepositoryExactPackage>(&source_result);
           package != nullptr) {
            return RepositoryPackagePresent{
                    package->repository.repository_name,
                    package->repository.configured_order,
                    package->package_name,
                    package->package_base,
                    package->package_version,
                    observation.configured_repository_order};
        }
        if(std::holds_alternative<RepositoryExactPackageAbsent>(
                   source_result)) {
            continue;
        }

        const RepositoryExactPackageSourceFailure& failure =
                std::get<RepositoryExactPackageSourceFailure>(source_result);
        return std::visit(
                [&failure, &observation](const auto& reason) {
                    RepositoryMetadataFailure projected = repository_failure(
                            reason,
                            failure.repository.repository_name);
                    projected.configured_repository_order =
                            observation.configured_repository_order;
                    return projected;
                },
                failure.reason);
    }
    return RepositoryPackageNotFound{
            observation.configured_repository_order};
}

StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name) {
    require_valid_package_name(dependency_name);

    PacmanRepositoryConfiguration configuration;
    try {
        configuration = resolve_pacman_repository_configuration();
    } catch(const PackageMetadataError& error) {
        return repository_failure(error.failure());
    }

    RepositoryProviderObservationResult result =
            observe_repository_providers(configuration, dependency_name);
    if(const auto* failure =
               std::get_if<RepositoryProviderObservationFailure>(&result);
       failure != nullptr) {
        RepositoryMetadataFailure projected =
                repository_failure(failure->failure);
        projected.configured_repository_order =
                configuration.repository_names;
        return projected;
    }

    RepositoryProviderQuerySnapshot snapshot;
    const RepositoryProviderObservation& observation =
            std::get<RepositoryProviderObservation>(result);
    snapshot.configured_repository_order =
            observation.configured_repository_order;
    for(const auto& source_result : observation.source_results) {
        if(const auto* source =
                   std::get_if<RepositoryProviderSourceObservation>(
                           &source_result);
           source != nullptr) {
            for(const auto& package : source->packages) {
                add_repository_provider_candidates(
                        snapshot.candidates, package, dependency_name);
            }
            continue;
        }

        const RepositoryProviderSourceFailure& failure =
                std::get<RepositoryProviderSourceFailure>(source_result);
        snapshot.source_failures.push_back(std::visit(
                [&failure](const auto& reason) {
                    return repository_failure(
                            reason,
                            failure.repository.repository_name);
                },
                failure.reason));
    }
    return snapshot;
}

std::vector<ProvidedDependency> find_repo_providers(
        const std::string& dependency_name) {
    if(!is_valid_package_name(dependency_name)) return {};
    StrictRepositoryProvidersQueryResult result =
            query_repository_providers_strict(dependency_name);
    if(const auto* snapshot =
               std::get_if<RepositoryProviderQuerySnapshot>(&result);
       snapshot != nullptr) {
        return snapshot->candidates;
    }
    return {};
}

InstalledExactPackageObservationResult query_installed_exact_package_strict(
        const std::string& package_name) {
    require_valid_package_name(package_name);
    try {
        const PacmanDatabasePaths paths = resolve_pacman_database_paths();
        PackageMetadataSession session = PackageMetadataSession::open(paths);
        return observe_installed_exact_package(session, package_name);
    } catch(const PackageMetadataError& error) {
        return InstalledExactPackageQueryFailure{
                package_name, error.failure()};
    }
}

std::vector<InstalledPackage> get_foreign_packages() {
    std::vector<InstalledPackage> packages;
    const std::string output = exec_command("pacman -Qm 2>/dev/null");
    if(output.empty()) return packages;

    std::stringstream stream(output);
    std::string line;
    while(std::getline(stream, line)) {
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
    for(const auto& package : get_foreign_packages()) {
        names.insert(package.name);
    }
    return names;
}
