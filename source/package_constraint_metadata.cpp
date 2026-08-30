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

std::string provider_metadata_diagnostic(
    const RepositoryProvidedPackageMetadata& metadata) {
    const std::string package_name = metadata.package_name.value_or("");
    const std::string version = metadata.version.value_or("");
    switch(metadata.relation) {
        case RepositoryProvidedPackageRelation::Unversioned:
            return version.empty()
                       ? package_name
                       : package_name + "=" + version;
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
            return version.empty()
                       ? package_name
                       : package_name + "=" + version;
    }
    return version.empty()
               ? package_name
               : package_name + "=" + version;
}

} // namespace

ProviderCapabilityMetadataProjectionResult
project_package_metadata_provides(
    const std::vector<RepositoryProvidedPackageMetadata>& metadata,
    ObservedVersionSource version_source) {
    std::vector<RepositoryProviderCapability> capabilities;
    capabilities.reserve(metadata.size());
    for(const auto& provided : metadata) {
        const std::string package_name =
            provided.package_name.value_or("");
        ProviderCapabilityParseResult identity_result =
            make_provider_capability_from_metadata(
                package_name, std::nullopt);
        if(const auto* failure = identity_result.failure();
           failure != nullptr) {
            return *failure;
        }

        std::optional<std::string> version;
        switch(provided.relation) {
            case RepositoryProvidedPackageRelation::Unversioned:
                if(provided.version.has_value() &&
                   !provided.version->empty()) {
                    return DependencyConstraintParseFailure{
                        DependencyConstraintParseFailureKind::
                            InvalidVersion,
                        provider_metadata_diagnostic(provided)};
                }
                break;
            case RepositoryProvidedPackageRelation::Equal:
                if(!provided.version.has_value() ||
                   provided.version->empty()) {
                    return DependencyConstraintParseFailure{
                        DependencyConstraintParseFailureKind::
                            MissingVersion,
                        provider_metadata_diagnostic(provided)};
                }
                version = provided.version;
                break;
            case RepositoryProvidedPackageRelation::GreaterThanOrEqual:
            case RepositoryProvidedPackageRelation::LessThanOrEqual:
            case RepositoryProvidedPackageRelation::GreaterThan:
            case RepositoryProvidedPackageRelation::LessThan:
            case RepositoryProvidedPackageRelation::Unsupported:
            default:
                return DependencyConstraintParseFailure{
                    DependencyConstraintParseFailureKind::
                        UnsupportedProviderOperator,
                    provider_metadata_diagnostic(provided)};
        }

        ProviderCapabilityParseResult projection_result =
            make_provider_capability_from_metadata(
                package_name, std::move(version));
        if(const auto* failure = projection_result.failure();
           failure != nullptr) {
            return *failure;
        }

        const ProviderCapability* capability =
            projection_result.capability();
        if(capability == nullptr) {
            return DependencyConstraintParseFailure{
                DependencyConstraintParseFailureKind::InvalidPackageIdentity,
                provider_metadata_diagnostic(provided)};
        }

        ObservedVersion provided_version =
            ObservedVersion::from_provider_capability(
                version_source, *capability);
        capabilities.push_back(RepositoryProviderCapability{
            *capability,
            std::move(provided_version)});
    }
    return capabilities;
}

namespace {

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
            ProviderCapabilityMetadataProjectionResult provides_result =
                project_package_metadata_provides(
                    metadata->provides,
                    ObservedVersionSource::
                        RepositoryProviderCapability);
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
                std::move(metadata->package_base),
                exact_package_version(
                    ObservedVersionSource::RepositoryExactPackage,
                    metadata->version),
                std::get<std::vector<RepositoryProviderCapability>>(
                    std::move(provides_result)),
                std::move(metadata->architecture)});
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
                ProviderCapabilityMetadataProjectionResult provides_result =
                    project_package_metadata_provides(
                        metadata.provides,
                        ObservedVersionSource::
                            RepositoryProviderCapability);
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
                    std::move(metadata.package_base),
                    exact_package_version(
                        ObservedVersionSource::RepositoryExactPackage,
                        metadata.version),
                    std::get<std::vector<RepositoryProviderCapability>>(
                        std::move(provides_result)),
                    std::move(metadata.architecture)});
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
