#pragma once

#include "aur_constraint_metadata.hpp"
#include "package_relation_observation.hpp"

#include <vector>

PlannedPackageRelationObservation project_aur_relation_observation(
    const AurPackageConstraintMetadata& metadata,
    std::vector<PackageRelationRootAttribution> roots);

PackageRelationObservedPackage project_repository_relation_observation(
    const RepositoryExactPackage& package,
    PackageRelationObservationRole role,
    std::vector<PackageRelationRootAttribution> roots = {});

PlannedPackageRelationObservation
project_repository_planned_relation_observation(
    const RepositoryExactPackage& package,
    std::vector<PackageRelationRootAttribution> roots);

PackageRelationObservationSet
project_repository_exact_relation_observations(
    const RepositoryExactPackageObservationResult& observation);

PackageRelationObservationSet
project_repository_provider_relation_observations(
    const RepositoryProviderObservationResult& observation);

// Both inputs are the existing authoritative queries for one target component.
// Their source-local coverage is combined without rerunning repository reads.
PackageRelationObservationSet project_repository_relation_observations(
    const RepositoryExactPackageObservationResult& exact_observation,
    const RepositoryProviderObservationResult& provider_observation);
