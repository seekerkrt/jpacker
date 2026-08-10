#include "package_constraint_metadata.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

ObservedVersion exact_package_version(
        ObservedVersionSource source,
        const std::optional<std::string>& version) {
    if(!version.has_value() || version->empty()) {
        return ObservedVersion::unknown(
                source,
                ObservedVersionUnknownReason::MissingVersionMetadata);
    }
    return ObservedVersion::available(source, *version);
}

std::string provider_capability_specification(
        const RepositoryProvidedPackageMetadata& metadata) {
    const std::string package_name = metadata.package_name.value_or("");
    const std::string version = metadata.version.value_or("");
    switch(metadata.relation) {
        case RepositoryProvidedPackageRelation::Unversioned:
            // A version attached to an unversioned relation is malformed rather
            // than an equality capability inferred by the adapter.
            return !metadata.version.has_value()
                    ? package_name
                    : package_name + ">" + version;
        case RepositoryProvidedPackageRelation::Equal:
            return package_name + "=" + version;
        case RepositoryProvidedPackageRelation::GreaterThanOrEqual:
            return package_name + ">=" + version;
        case RepositoryProvidedPackageRelation::LessThanOrEqual:
            return package_name + "<=" + version;
        case RepositoryProvidedPackageRelation::GreaterThan:
            return package_name + ">" + version;
        case RepositoryProvidedPackageRelation::LessThan:
            return package_name + "<" + version;
        case RepositoryProvidedPackageRelation::Unsupported:
            return package_name + ">" + version;
    }
    return package_name + ">" + version;
}

using RepositoryProviderCapabilityProjectionResult = std::variant<
        std::vector<RepositoryProviderCapability>,
        DependencyConstraintParseFailure>;

RepositoryProviderCapabilityProjectionResult project_repository_provides(
        const std::vector<RepositoryProvidedPackageMetadata>& metadata) {
    std::vector<RepositoryProviderCapability> capabilities;
    capabilities.reserve(metadata.size());
    for(const auto& provided : metadata) {
        ProviderCapabilityParseResult parse_result = parse_provider_capability(
                provider_capability_specification(provided));
        if(const auto* failure = parse_result.failure(); failure != nullptr) {
            return *failure;
        }

        const ProviderCapability* capability = parse_result.capability();
        if(capability == nullptr) {
            return DependencyConstraintParseFailure{
                    DependencyConstraintParseFailureKind::InvalidPackageIdentity,
                    provider_capability_specification(provided)};
        }

        ObservedVersion provided_version = capability->version().has_value()
                ? ObservedVersion::available(
                          ObservedVersionSource::RepositoryProviderCapability,
                          capability->version().value())
                : ObservedVersion::unknown(
                          ObservedVersionSource::RepositoryProviderCapability,
                          ObservedVersionUnknownReason::
                                  UnversionedProviderCapability);
        capabilities.push_back(RepositoryProviderCapability{
                *capability,
                std::move(provided_version)});
    }
    return capabilities;
}

ConfiguredRepositoryIdentity repository_identity(
        std::size_t configured_order,
        const std::string& repository_name) {
    return ConfiguredRepositoryIdentity{repository_name, configured_order};
}

} // namespace

InstalledExactPackageObservationResult observe_installed_exact_package(
        const PackageMetadataSession& session,
        const std::string& package_name) {
    InstalledExactPackageMetadataQueryResult metadata_result =
            session.query_installed_exact_package_metadata(package_name);
    if(const auto* metadata =
               std::get_if<InstalledExactPackageMetadata>(&metadata_result);
       metadata != nullptr) {
        return InstalledExactPackage{
                metadata->package_name,
                exact_package_version(
                        ObservedVersionSource::InstalledExactPackage,
                        metadata->version)};
    }
    if(std::holds_alternative<PackageNotFound>(metadata_result)) {
        return InstalledExactPackageAbsent{package_name};
    }
    return InstalledExactPackageQueryFailure{
            package_name,
            std::get<PackageMetadataFailure>(std::move(metadata_result))};
}

RepositoryExactPackageObservationResult observe_repository_exact_package(
        const PacmanRepositoryConfiguration& configuration,
        const std::string& package_name) {
    RepositoryExactPackageMetadataQueryResult metadata_result =
            query_configured_repository_exact_package_metadata(
                    configuration,
                    package_name);
    if(const auto* failure =
               std::get_if<PackageMetadataFailure>(&metadata_result);
       failure != nullptr) {
        return RepositoryExactPackageObservationFailure{
                package_name,
                *failure};
    }

    RepositoryExactPackageMetadataSnapshot metadata_snapshot =
            std::get<RepositoryExactPackageMetadataSnapshot>(
                    std::move(metadata_result));
    RepositoryExactPackageObservation observation;
    observation.configured_repository_order =
            std::move(metadata_snapshot.repository_order);
    observation.source_results.reserve(metadata_snapshot.source_results.size());

    for(auto& source_result : metadata_snapshot.source_results) {
        if(auto* metadata =
                   std::get_if<RepositoryExactPackageMetadata>(&source_result);
           metadata != nullptr) {
            RepositoryProviderCapabilityProjectionResult provides_result =
                    project_repository_provides(metadata->provides);
            if(const auto* failure =
                       std::get_if<DependencyConstraintParseFailure>(
                               &provides_result);
               failure != nullptr) {
                observation.source_results.push_back(
                        RepositoryExactPackageSourceFailure{
                                repository_identity(
                                        metadata->configured_repository_order,
                                        metadata->repository_name),
                                metadata->package_name,
                                *failure});
                continue;
            }

            observation.source_results.push_back(RepositoryExactPackage{
                    repository_identity(
                            metadata->configured_repository_order,
                            metadata->repository_name),
                    std::move(metadata->package_name),
                    exact_package_version(
                            ObservedVersionSource::RepositoryExactPackage,
                            metadata->version),
                    std::get<std::vector<RepositoryProviderCapability>>(
                            std::move(provides_result))});
            continue;
        }

        if(auto* absent =
                   std::get_if<RepositoryExactPackageMetadataNotFound>(
                           &source_result);
           absent != nullptr) {
            observation.source_results.push_back(RepositoryExactPackageAbsent{
                    repository_identity(
                            absent->configured_repository_order,
                            absent->repository_name),
                    std::move(absent->package_name)});
            continue;
        }

        auto& failure =
                std::get<RepositoryExactPackageMetadataSourceFailure>(
                        source_result);
        observation.source_results.push_back(
                RepositoryExactPackageSourceFailure{
                        repository_identity(
                                failure.configured_repository_order,
                                failure.repository_name),
                        std::move(failure.package_name),
                        std::move(failure.failure)});
    }
    return observation;
}

RepositoryProviderObservationResult observe_repository_providers(
        const PacmanRepositoryConfiguration& configuration,
        const std::string& dependency_name) {
    RepositoryProviderPackageMetadataQueryResult metadata_result =
            query_configured_repository_provider_package_metadata(
                    configuration,
                    dependency_name);
    if(const auto* failure =
               std::get_if<PackageMetadataFailure>(&metadata_result);
       failure != nullptr) {
        return RepositoryProviderObservationFailure{
                dependency_name,
                *failure};
    }

    RepositoryProviderPackageMetadataSnapshot metadata_snapshot =
            std::get<RepositoryProviderPackageMetadataSnapshot>(
                    std::move(metadata_result));
    RepositoryProviderObservation observation;
    observation.configured_repository_order =
            std::move(metadata_snapshot.repository_order);
    observation.source_results.reserve(metadata_snapshot.source_results.size());

    for(auto& source_result : metadata_snapshot.source_results) {
        if(auto* source =
                   std::get_if<RepositoryProviderPackageMetadataSourceSnapshot>(
                           &source_result);
           source != nullptr) {
            RepositoryProviderSourceObservation projected{
                    repository_identity(
                            source->configured_repository_order,
                            source->repository_name),
                    {}};
            projected.packages.reserve(source->packages.size());
            std::optional<DependencyConstraintParseFailure> parse_failure;
            for(auto& metadata : source->packages) {
                RepositoryProviderCapabilityProjectionResult provides_result =
                        project_repository_provides(metadata.provides);
                if(const auto* failure =
                           std::get_if<DependencyConstraintParseFailure>(
                                   &provides_result);
                   failure != nullptr) {
                    parse_failure = *failure;
                    break;
                }
                projected.packages.push_back(RepositoryExactPackage{
                        repository_identity(
                                metadata.configured_repository_order,
                                metadata.repository_name),
                        std::move(metadata.package_name),
                        exact_package_version(
                                ObservedVersionSource::RepositoryExactPackage,
                                metadata.version),
                        std::get<std::vector<RepositoryProviderCapability>>(
                                std::move(provides_result))});
            }
            if(parse_failure.has_value()) {
                observation.source_results.push_back(
                        RepositoryProviderSourceFailure{
                                projected.repository,
                                std::move(parse_failure.value())});
            } else {
                observation.source_results.push_back(std::move(projected));
            }
            continue;
        }

        auto& failure =
                std::get<RepositoryProviderPackageMetadataSourceFailure>(
                        source_result);
        observation.source_results.push_back(
                RepositoryProviderSourceFailure{
                        repository_identity(
                                failure.configured_repository_order,
                                failure.repository_name),
                        std::move(failure.failure)});
    }
    return observation;
}
