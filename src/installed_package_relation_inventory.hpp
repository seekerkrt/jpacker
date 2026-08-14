#pragma once

#include "package_relation_observation.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

struct InstalledPackageRelationInventory {
    PackageRelationInstalledDatabaseIdentity source;
    std::vector<PackageRelationObservedPackage> packages;

    bool operator==(const InstalledPackageRelationInventory&) const = default;
};

using InstalledPackageRelationInventoryFailureReason = std::variant<
        PackageMetadataFailure,
        DependencyConstraintParseFailure>;

struct InstalledPackageRelationInventoryFailure {
    PackageRelationInstalledDatabaseIdentity source;
    std::optional<std::string> package_name;
    std::optional<std::size_t> package_index;
    std::vector<PackageRelationObservedPackage> observed_packages;
    InstalledPackageRelationInventoryFailureReason reason;

    bool operator==(const InstalledPackageRelationInventoryFailure&) const =
            default;
};

using InstalledPackageRelationInventoryResult = std::variant<
        InstalledPackageRelationInventory,
        InstalledPackageRelationInventoryFailure>;

InstalledPackageRelationInventoryResult observe_installed_package_relations(
        const PackageMetadataSession& session,
        PackageRelationInstalledDatabaseIdentity source);

InstalledPackageRelationInventoryResult query_installed_package_relations(
        const PacmanDatabasePaths& paths);

PackageRelationObservationSet project_installed_relation_observations(
        const InstalledPackageRelationInventoryResult& inventory);
