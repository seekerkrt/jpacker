#include "installed_package_relation_inventory.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

PackageRelationInstalledDatabaseIdentity installed_source_identity(
    const PacmanDatabasePaths& paths) {
    return PackageRelationInstalledDatabaseIdentity{
        paths.root_dir.lexically_normal(),
        paths.db_path.lexically_normal()};
}

ObservedVersion installed_package_version(
    const std::optional<std::string>& version) {
    if(!version.has_value() || version->empty()) {
        return ObservedVersion::unknown(
            ObservedVersionSource::InstalledExactPackage,
            ObservedVersionUnknownReason::MissingVersionMetadata);
    }
    return ObservedVersion::available(
        ObservedVersionSource::InstalledExactPackage,
        *version);
}

PackageRelationObservationFailureKind inventory_failure_kind(
    const InstalledPackageRelationInventoryFailureReason& reason) {
    if(const auto* metadata = std::get_if<PackageMetadataFailure>(&reason);
       metadata != nullptr) {
        switch(metadata->code) {
            case PackageMetadataErrorCode::InitializationFailed:
            case PackageMetadataErrorCode::LocalDatabaseUnavailable:
            case PackageMetadataErrorCode::ConfigurationUnavailable:
                return PackageRelationObservationFailureKind::SourceUnavailable;
            case PackageMetadataErrorCode::ConfigurationMalformed:
            case PackageMetadataErrorCode::InvalidPackageName:
                return PackageRelationObservationFailureKind::InvalidIdentity;
            case PackageMetadataErrorCode::MalformedMetadata:
                return PackageRelationObservationFailureKind::MalformedMetadata;
            case PackageMetadataErrorCode::QueryFailed:
            case PackageMetadataErrorCode::SyncDatabaseUnavailable:
            case PackageMetadataErrorCode::RepositoryNotConfigured:
                return PackageRelationObservationFailureKind::
                    PartialSourceFailure;
        }
    }
    return PackageRelationObservationFailureKind::MalformedMetadata;
}

std::string inventory_failure_diagnostic(
    const InstalledPackageRelationInventoryFailureReason& reason) {
    if(const auto* metadata = std::get_if<PackageMetadataFailure>(&reason);
       metadata != nullptr) {
        return metadata->diagnostic;
    }
    return "Installed package provides metadata is malformed.";
}

PackageRelationObservationSet unavailable_configuration_observations(
    PackageRelationObservationFailureKind kind,
    std::string diagnostic) {
    const PackageRelationObservationCompleteness completeness =
        kind == PackageRelationObservationFailureKind::MalformedMetadata ||
                kind == PackageRelationObservationFailureKind::
                            InvalidIdentity
            ? PackageRelationObservationCompleteness::Invalid
            : PackageRelationObservationCompleteness::Unavailable;
    return PackageRelationObservationSet{
        completeness,
        {},
        {},
        {},
        {PackageRelationObservationFailure{
            kind,
            PackageRelationObservationRole::Installed,
            std::nullopt,
            std::nullopt,
            std::move(diagnostic)}}};
}

} // namespace

InstalledPackageRelationInventoryResult observe_installed_package_relations(
    const PackageMetadataSession& session,
    PackageRelationInstalledDatabaseIdentity source) {
    InstalledPackageRelationMetadataInventoryResult metadata_result =
        session.snapshot_installed_package_relation_metadata();
    InstalledPackageRelationInventory inventory{std::move(source), {}};
    std::optional<InstalledPackageRelationMetadataInventoryFailure>
        metadata_failure;
    InstalledPackageRelationMetadataInventory metadata;
    if(auto* failure =
           std::get_if<
               InstalledPackageRelationMetadataInventoryFailure>(
               &metadata_result);
       failure != nullptr) {
        metadata = std::move(failure->observed_packages);
        metadata_failure = InstalledPackageRelationMetadataInventoryFailure{
            {}, failure->package_index, std::move(failure->failure)};
    } else {
        metadata = std::get<InstalledPackageRelationMetadataInventory>(
            std::move(metadata_result));
    }
    inventory.packages.reserve(metadata.size());
    for(std::size_t package_index = 0;
        package_index < metadata.size();
        ++package_index) {
        auto& package = metadata[package_index];
        ProviderCapabilityMetadataProjectionResult provides_result =
            project_package_metadata_provides(
                package.provides,
                ObservedVersionSource::
                    InstalledProviderCapability);
        if(const auto* failure =
               std::get_if<DependencyConstraintParseFailure>(
                   &provides_result);
           failure != nullptr) {
            return InstalledPackageRelationInventoryFailure{
                inventory.source,
                package.package_name,
                package_index,
                std::move(inventory.packages),
                *failure};
        }

        std::vector<PackageRelationObservedCapability> provides;
        auto projected =
            std::get<std::vector<RepositoryProviderCapability>>(
                std::move(provides_result));
        provides.reserve(projected.size());
        for(auto& capability : projected) {
            provides.push_back(PackageRelationObservedCapability{
                std::move(capability.capability),
                std::move(capability.provided_version)});
        }

        inventory.packages.push_back(PackageRelationObservedPackage{
            std::move(package.package_name),
            std::nullopt,
            installed_package_version(package.version),
            std::move(provides),
            inventory.source,
            PackageRelationObservationRole::Installed,
            {}});
    }
    if(metadata_failure.has_value()) {
        return InstalledPackageRelationInventoryFailure{
            inventory.source,
            std::nullopt,
            metadata_failure->package_index,
            std::move(inventory.packages),
            std::move(metadata_failure->failure)};
    }
    return inventory;
}

InstalledPackageRelationInventoryResult query_installed_package_relations(
    const PacmanDatabasePaths& paths) {
    PackageRelationInstalledDatabaseIdentity source =
        installed_source_identity(paths);
    try {
        PackageMetadataSession session = PackageMetadataSession::open(paths);
        return observe_installed_package_relations(session, std::move(source));
    } catch(const PackageMetadataError& error) {
        return InstalledPackageRelationInventoryFailure{
            std::move(source),
            std::nullopt,
            std::nullopt,
            {},
            error.failure()};
    }
}

PackageRelationObservationSet project_installed_relation_observations(
    const InstalledPackageRelationInventoryResult& inventory) {
    if(const auto* observed =
           std::get_if<InstalledPackageRelationInventory>(&inventory);
       observed != nullptr) {
        return PackageRelationObservationSet{
            PackageRelationObservationCompleteness::Complete,
            {observed->source},
            {PackageRelationSourceIdentityCoverage{
                PackageRelationSourceIdentity{observed->source},
                true,
                true}},
            observed->packages,
            {}};
    }

    const auto& failure =
        std::get<InstalledPackageRelationInventoryFailure>(inventory);
    const PackageRelationObservationFailureKind kind =
        inventory_failure_kind(failure.reason);
    const PackageRelationObservationCompleteness completeness =
        kind == PackageRelationObservationFailureKind::MalformedMetadata ||
                kind == PackageRelationObservationFailureKind::
                            InvalidIdentity
            ? PackageRelationObservationCompleteness::Invalid
            : (kind == PackageRelationObservationFailureKind::
                           SourceUnavailable
                   ? PackageRelationObservationCompleteness::Unavailable
                   : PackageRelationObservationCompleteness::Partial);
    return PackageRelationObservationSet{
        completeness,
        {failure.source},
        {PackageRelationSourceIdentityCoverage{
            PackageRelationSourceIdentity{failure.source},
            false,
            false}},
        failure.observed_packages,
        {PackageRelationObservationFailure{
            kind,
            PackageRelationObservationRole::Installed,
            PackageRelationSourceIdentity{failure.source},
            failure.package_name,
            inventory_failure_diagnostic(failure.reason)}}};
}

PackageRelationObservationSet query_installed_package_relation_observations() {
    try {
        const PacmanDatabasePaths paths = resolve_pacman_database_paths();
        return project_installed_relation_observations(
            query_installed_package_relations(paths));
    } catch(const PackageMetadataError& error) {
        const InstalledPackageRelationInventoryFailureReason reason =
            error.failure();
        return unavailable_configuration_observations(
            inventory_failure_kind(reason),
            inventory_failure_diagnostic(reason));
    } catch(const std::exception& error) {
        return unavailable_configuration_observations(
            PackageRelationObservationFailureKind::SourceUnavailable,
            error.what());
    }
}
