#include "aur_constraint_metadata.hpp"

#include "aur_rpc.hpp"

#include <algorithm>
#include <utility>

namespace {

using DependencyArrayProjectionResult = std::variant<
        std::vector<DependencyRequirement>,
        AurConstraintMetadataProjectionFailure>;

using ProvidesProjectionResult = std::variant<
        std::vector<AurProviderCapabilityMetadata>,
        AurConstraintMetadataProjectionFailure>;

ObservedVersion aur_exact_package_version(const std::string& version) {
    if(version.empty()) {
        return ObservedVersion::unknown(
                ObservedVersionSource::AurExactPackage,
                ObservedVersionUnknownReason::MissingVersionMetadata);
    }
    return ObservedVersion::available(
            ObservedVersionSource::AurExactPackage, version);
}

DependencyArrayProjectionResult project_dependency_array(
        const AurPackageInfo& package,
        const std::vector<std::string>& specifications,
        AurConstraintMetadataField field) {
    std::vector<DependencyRequirement> requirements;
    requirements.reserve(specifications.size());
    for(std::size_t index = 0; index < specifications.size(); ++index) {
        DependencyRequirementParseResult parse_result =
                parse_dependency_requirement(specifications[index]);
        if(const auto* failure = parse_result.failure(); failure != nullptr) {
            return AurConstraintMetadataProjectionFailure{
                    package.Name, package.PackageBase, field, index, *failure};
        }
        const DependencyRequirement* requirement = parse_result.requirement();
        if(requirement == nullptr) {
            return AurConstraintMetadataProjectionFailure{
                    package.Name,
                    package.PackageBase,
                    field,
                    index,
                    DependencyConstraintParseFailure{
                            DependencyConstraintParseFailureKind::
                                    InvalidPackageIdentity,
                            specifications[index]}};
        }
        requirements.push_back(*requirement);
    }
    return requirements;
}

ProvidesProjectionResult project_provides(const AurPackageInfo& package) {
    std::vector<AurProviderCapabilityMetadata> capabilities;
    capabilities.reserve(package.Provides.size());
    for(std::size_t index = 0; index < package.Provides.size(); ++index) {
        ProviderCapabilityParseResult parse_result =
                parse_provider_capability(package.Provides[index]);
        if(const auto* failure = parse_result.failure(); failure != nullptr) {
            return AurConstraintMetadataProjectionFailure{
                    package.Name,
                    package.PackageBase,
                    AurConstraintMetadataField::Provides,
                    index,
                    *failure};
        }
        const ProviderCapability* capability = parse_result.capability();
        if(capability == nullptr) {
            return AurConstraintMetadataProjectionFailure{
                    package.Name,
                    package.PackageBase,
                    AurConstraintMetadataField::Provides,
                    index,
                    DependencyConstraintParseFailure{
                            DependencyConstraintParseFailureKind::
                                    InvalidPackageIdentity,
                            package.Provides[index]}};
        }
        ObservedVersion provided_version = capability->version().has_value()
                ? ObservedVersion::available(
                          ObservedVersionSource::AurProviderCapability,
                          capability->version().value())
                : ObservedVersion::unknown(
                          ObservedVersionSource::AurProviderCapability,
                          ObservedVersionUnknownReason::
                                  UnversionedProviderCapability);
        capabilities.push_back(AurProviderCapabilityMetadata{
                *capability, std::move(provided_version)});
    }
    return capabilities;
}

AurProviderDependencyProjectionFailure invalid_candidate_failure(
        const ConsumerDependencyRequirement& requirement,
        const AurConstraintMetadataProjectionFailure& failure) {
    return AurProviderDependencyProjectionFailure{
            requirement,
            failure.package_name,
            failure.package_base,
            AurProviderProjectionFailureKind::InvalidCandidateMetadata,
            failure.reason};
}

AurProviderDependencyProjectionResult project_available_aur_provider(
        const ConsumerDependencyRequirement& requirement,
        const AurPackageConstraintMetadata& metadata) {
    const auto matching = std::find_if(
            metadata.provides.begin(), metadata.provides.end(),
            [&requirement](const AurProviderCapabilityMetadata& capability) {
                return capability.capability.package_name() ==
                        requirement.package_name();
            });
    if(matching == metadata.provides.end()) {
        return AurProviderDependencyProjectionFailure{
                requirement,
                metadata.package_name,
                metadata.package_base,
                AurProviderProjectionFailureKind::MatchingCapabilityMissing,
                std::nullopt};
    }

    ProviderConstraintMetadata constraint_metadata{
            matching->capability,
            metadata.package_version,
            matching->provided_version};
    ProvidedDependency provider =
            ProvidedDependency::from_aur_constraint_metadata(
                    metadata.package_name,
                    metadata.package_base,
                    std::move(constraint_metadata));
    ConstraintEvaluation evaluation =
            evaluate_consumer_dependency_requirement(
                    requirement, matching->provided_version);
    return AurProviderDependencyProjection{
            requirement, std::move(provider), std::move(evaluation)};
}

} // namespace

AurConstraintMetadataProjectionResult project_aur_constraint_metadata(
        const AurPackageInfo& package) {
    DependencyArrayProjectionResult depends = project_dependency_array(
            package, package.Depends, AurConstraintMetadataField::Depends);
    if(const auto* failure =
               std::get_if<AurConstraintMetadataProjectionFailure>(&depends);
       failure != nullptr) {
        return *failure;
    }

    DependencyArrayProjectionResult make_depends = project_dependency_array(
            package,
            package.MakeDepends,
            AurConstraintMetadataField::MakeDepends);
    if(const auto* failure =
               std::get_if<AurConstraintMetadataProjectionFailure>(
                       &make_depends);
       failure != nullptr) {
        return *failure;
    }

    DependencyArrayProjectionResult check_depends = project_dependency_array(
            package,
            package.CheckDepends,
            AurConstraintMetadataField::CheckDepends);
    if(const auto* failure =
               std::get_if<AurConstraintMetadataProjectionFailure>(
                       &check_depends);
       failure != nullptr) {
        return *failure;
    }

    ProvidesProjectionResult provides = project_provides(package);
    if(const auto* failure =
               std::get_if<AurConstraintMetadataProjectionFailure>(&provides);
       failure != nullptr) {
        return *failure;
    }

    return AurPackageConstraintMetadata{
            package.Name,
            package.PackageBase,
            aur_exact_package_version(package.Version),
            std::get<std::vector<DependencyRequirement>>(std::move(depends)),
            std::get<std::vector<DependencyRequirement>>(
                    std::move(make_depends)),
            std::get<std::vector<DependencyRequirement>>(
                    std::move(check_depends)),
            std::get<std::vector<AurProviderCapabilityMetadata>>(
                    std::move(provides))};
}

AurProviderDependencyProjectionResult project_aur_provider_dependency(
        const ConsumerDependencyRequirement& requirement,
        const AurProviderCandidateMetadata& candidate_metadata) {
    if(const auto* metadata =
               std::get_if<AurPackageConstraintMetadata>(&candidate_metadata);
       metadata != nullptr) {
        return project_available_aur_provider(requirement, *metadata);
    }
    if(const auto* unavailable =
               std::get_if<AurProviderMetadataUnavailable>(
                       &candidate_metadata);
       unavailable != nullptr) {
        return AurProviderDependencyUnknown{
                requirement,
                unavailable->package_name,
                unavailable->package_base,
                unavailable->reason};
    }
    return invalid_candidate_failure(
            requirement,
            std::get<AurConstraintMetadataProjectionFailure>(
                    candidate_metadata));
}

std::vector<AurProviderDependencyProjectionResult>
project_aur_provider_dependencies(
        const ConsumerDependencyRequirement& requirement,
        const std::vector<AurProviderCandidateMetadata>& candidates) {
    std::vector<AurProviderDependencyProjectionResult> projections;
    projections.reserve(candidates.size());
    for(const auto& candidate : candidates) {
        projections.push_back(
                project_aur_provider_dependency(requirement, candidate));
    }
    return projections;
}

AurProviderDependencyProjectionResult refresh_aur_provider_dependency(
        const AurProviderDependencyProjection& selected,
        const AurProviderCandidateMetadata& current_metadata) {
    if(const auto* current =
               std::get_if<AurPackageConstraintMetadata>(&current_metadata);
       current != nullptr) {
        if(!std::holds_alternative<AurProviderOrigin>(
                   selected.provider.origin) ||
           selected.provider.package_name != current->package_name ||
           selected.provider.package_base != current->package_base) {
            return AurProviderDependencyProjectionFailure{
                    selected.requirement,
                    current->package_name,
                    current->package_base,
                    AurProviderProjectionFailureKind::ProviderIdentityChanged,
                    std::nullopt};
        }
    }
    // Re-run the projection from the current capability list. The previously
    // selected capability/version is never accepted as refresh input.
    return project_aur_provider_dependency(
            selected.requirement, current_metadata);
}
