#include "package_relation_observation_adapter.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

std::vector<PackageRelationObservedCapability> aur_provides(
        const std::vector<AurProviderCapabilityMetadata>& provides) {
    std::vector<PackageRelationObservedCapability> result;
    result.reserve(provides.size());
    for(const auto& provided : provides) {
        result.push_back(PackageRelationObservedCapability{
                provided.capability, provided.provided_version});
    }
    return result;
}

std::vector<PackageRelationObservedCapability> repository_provides(
        const std::vector<RepositoryProviderCapability>& provides) {
    std::vector<PackageRelationObservedCapability> result;
    result.reserve(provides.size());
    for(const auto& provided : provides) {
        result.push_back(PackageRelationObservedCapability{
                provided.capability, provided.provided_version});
    }
    return result;
}

PackageRelationObservationFailureKind metadata_failure_kind(
        const PackageMetadataFailure& failure) noexcept {
    if(failure.code == PackageMetadataErrorCode::ConfigurationMalformed ||
       failure.code == PackageMetadataErrorCode::InvalidPackageName) {
        return PackageRelationObservationFailureKind::InvalidIdentity;
    }
    return failure.code == PackageMetadataErrorCode::MalformedMetadata
            ? PackageRelationObservationFailureKind::MalformedMetadata
            : PackageRelationObservationFailureKind::PartialSourceFailure;
}

void retain_failure_completeness(
        PackageRelationObservationSet& observations,
        PackageRelationObservationFailureKind failure_kind) noexcept {
    if(failure_kind ==
               PackageRelationObservationFailureKind::MalformedMetadata ||
       failure_kind ==
               PackageRelationObservationFailureKind::InvalidIdentity) {
        observations.completeness =
                PackageRelationObservationCompleteness::Invalid;
        return;
    }
    if(observations.completeness !=
       PackageRelationObservationCompleteness::Invalid) {
        observations.completeness =
                PackageRelationObservationCompleteness::Partial;
    }
}

void add_repository_sources(
        PackageRelationObservationSet& observations,
        const std::vector<std::string>& configured_order) {
    observations.required_sources.reserve(configured_order.size());
    observations.source_identity_coverage.reserve(configured_order.size());
    for(std::size_t index = 0; index < configured_order.size(); ++index) {
        PackageRelationSourceIdentity source =
                ConfiguredRepositoryIdentity{configured_order[index], index};
        observations.required_sources.push_back(source);
        observations.source_identity_coverage.push_back(
                PackageRelationSourceIdentityCoverage{
                        std::move(source), false, false});
    }
}

PackageRelationSourceIdentityCoverage& repository_source_coverage(
        PackageRelationObservationSet& observations,
        const PackageRelationSourceIdentity& source) {
    const auto found = std::find_if(
            observations.source_identity_coverage.begin(),
            observations.source_identity_coverage.end(),
            [&source](const auto& coverage) {
                return coverage.source == source;
            });
    if(found != observations.source_identity_coverage.end()) {
        return *found;
    }
    observations.source_identity_coverage.push_back(
            PackageRelationSourceIdentityCoverage{source, false, false});
    return observations.source_identity_coverage.back();
}

void retain_required_source(
        PackageRelationObservationSet& observations,
        const PackageRelationSourceIdentity& source) {
    if(std::find(
               observations.required_sources.begin(),
               observations.required_sources.end(), source) ==
       observations.required_sources.end()) {
        observations.required_sources.push_back(source);
    }
    repository_source_coverage(observations, source);
}

void mark_repository_identity_coverage(
        PackageRelationObservationSet& observations,
        const ConfiguredRepositoryIdentity& repository,
        bool exact_package_identity) {
    PackageRelationSourceIdentityCoverage& coverage =
            repository_source_coverage(
                    observations,
                    PackageRelationSourceIdentity{repository});
    if(exact_package_identity) {
        coverage.exact_package_identity = true;
    } else {
        coverage.provided_component_identity = true;
    }
}

void add_repository_failure(
        PackageRelationObservationSet& observations,
        const ConfiguredRepositoryIdentity& repository,
        std::optional<std::string> package_name,
        const RepositoryExactPackageSourceFailureReason& reason) {
    PackageRelationObservationFailure failure = std::visit(
            [&repository, &package_name](const auto& typed_reason) {
                using Reason = std::decay_t<decltype(typed_reason)>;
                if constexpr(std::is_same_v<
                                     Reason,
                                     PackageMetadataFailure>) {
                    return PackageRelationObservationFailure{
                            metadata_failure_kind(typed_reason),
                            PackageRelationObservationRole::
                                    RepositoryCandidate,
                            PackageRelationSourceIdentity{repository},
                            package_name,
                            typed_reason.diagnostic};
                } else {
                    return PackageRelationObservationFailure{
                            PackageRelationObservationFailureKind::
                                    MalformedMetadata,
                            PackageRelationObservationRole::
                                    RepositoryCandidate,
                            PackageRelationSourceIdentity{repository},
                            package_name,
                            "Repository provided capability metadata is invalid."};
                }
            },
            reason);
    retain_failure_completeness(observations, failure.kind);
    observations.failures.push_back(std::move(failure));
}

PackageRelationObservationSet top_level_repository_failure(
        const PackageMetadataFailure& failure,
        std::optional<std::string> package_name) {
    const PackageRelationObservationFailureKind kind =
            failure.code == PackageMetadataErrorCode::ConfigurationMalformed ||
                    failure.code == PackageMetadataErrorCode::
                                            InvalidPackageName
            ? PackageRelationObservationFailureKind::InvalidIdentity
            : (failure.code == PackageMetadataErrorCode::MalformedMetadata
                       ? PackageRelationObservationFailureKind::
                                 MalformedMetadata
                       : PackageRelationObservationFailureKind::
                                 SourceUnavailable);
    return PackageRelationObservationSet{
            kind == PackageRelationObservationFailureKind::MalformedMetadata ||
                            kind == PackageRelationObservationFailureKind::
                                            InvalidIdentity
                    ? PackageRelationObservationCompleteness::Invalid
                    : PackageRelationObservationCompleteness::Unavailable,
            {},
            {},
            {},
            {PackageRelationObservationFailure{
                    kind,
                    PackageRelationObservationRole::RepositoryCandidate,
                    std::nullopt,
                    std::move(package_name),
                    failure.diagnostic}}};
}

std::optional<std::size_t> repository_source_order(
        const PackageRelationSourceIdentity& source) noexcept {
    const auto* repository =
            std::get_if<ConfiguredRepositoryIdentity>(&source);
    return repository == nullptr
            ? std::nullopt
            : std::optional<std::size_t>(repository->configured_order);
}

bool repository_source_less(
        const PackageRelationSourceIdentity& lhs,
        const PackageRelationSourceIdentity& rhs) noexcept {
    const std::optional<std::size_t> lhs_order = repository_source_order(lhs);
    const std::optional<std::size_t> rhs_order = repository_source_order(rhs);
    if(lhs_order.has_value() != rhs_order.has_value()) {
        return lhs_order.has_value();
    }
    return lhs_order.has_value() && lhs_order.value() != rhs_order.value()
            ? lhs_order.value() < rhs_order.value()
            : false;
}

void merge_repository_observations(
        PackageRelationObservationSet& result,
        const PackageRelationObservationSet& channel) {
    for(const auto& source : channel.required_sources) {
        retain_required_source(result, source);
    }
    for(const auto& channel_coverage : channel.source_identity_coverage) {
        PackageRelationSourceIdentityCoverage& coverage =
                repository_source_coverage(
                        result, channel_coverage.source);
        coverage.exact_package_identity =
                coverage.exact_package_identity ||
                channel_coverage.exact_package_identity;
        coverage.provided_component_identity =
                coverage.provided_component_identity ||
                channel_coverage.provided_component_identity;
    }
    for(const auto& package : channel.packages) {
        if(std::find(result.packages.begin(), result.packages.end(), package) ==
           result.packages.end()) {
            result.packages.push_back(package);
        }
    }
    result.failures.insert(
            result.failures.end(), channel.failures.begin(),
            channel.failures.end());
}

bool repository_identity_coverage_is_complete(
        const PackageRelationObservationSet& observations) noexcept {
    if(observations.required_sources.empty()) return false;
    return std::all_of(
            observations.required_sources.begin(),
            observations.required_sources.end(),
            [&observations](const auto& source) {
                const auto coverage = std::find_if(
                        observations.source_identity_coverage.begin(),
                        observations.source_identity_coverage.end(),
                        [&source](const auto& item) {
                            return item.source == source;
                        });
                return coverage !=
                               observations.source_identity_coverage.end() &&
                        coverage->exact_package_identity &&
                        coverage->provided_component_identity;
            });
}

void order_repository_observations(
        PackageRelationObservationSet& observations) {
    std::stable_sort(
            observations.required_sources.begin(),
            observations.required_sources.end(), repository_source_less);
    std::stable_sort(
            observations.source_identity_coverage.begin(),
            observations.source_identity_coverage.end(),
            [](const auto& lhs, const auto& rhs) {
                return repository_source_less(lhs.source, rhs.source);
            });
    std::stable_sort(
            observations.packages.begin(), observations.packages.end(),
            [](const auto& lhs, const auto& rhs) {
                return repository_source_less(lhs.source, rhs.source);
            });
    std::stable_sort(
            observations.failures.begin(), observations.failures.end(),
            [](const auto& lhs, const auto& rhs) {
                if(lhs.source.has_value() != rhs.source.has_value()) {
                    return lhs.source.has_value();
                }
                return lhs.source.has_value()
                        ? repository_source_less(
                                  lhs.source.value(), rhs.source.value())
                        : false;
            });
}

} // namespace

PlannedPackageRelationObservation project_aur_relation_observation(
        const AurPackageConstraintMetadata& metadata,
        std::vector<PackageRelationRootAttribution> roots) {
    return PlannedPackageRelationObservation{
            PackageRelationObservedPackage{
                    metadata.package_name,
                    metadata.package_base,
                    metadata.package_version,
                    aur_provides(metadata.provides),
                    PackageRelationAurSourceIdentity{
                            metadata.package_name, metadata.package_base},
                    PackageRelationObservationRole::PlannedTarget,
                    std::move(roots)},
            metadata.relations};
}

PackageRelationObservedPackage project_repository_relation_observation(
        const RepositoryExactPackage& package,
        PackageRelationObservationRole role,
        std::vector<PackageRelationRootAttribution> roots) {
    return PackageRelationObservedPackage{
            package.package_name,
            package.package_base,
            package.package_version,
            repository_provides(package.provides),
            package.repository,
            role,
            std::move(roots)};
}

PlannedPackageRelationObservation
project_repository_planned_relation_observation(
        const RepositoryExactPackage& package,
        std::vector<PackageRelationRootAttribution> roots) {
    return PlannedPackageRelationObservation{
            project_repository_relation_observation(
                    package,
                    PackageRelationObservationRole::PlannedTarget,
                    std::move(roots)),
            {}};
}

PackageRelationObservationSet
project_repository_exact_relation_observations(
        const RepositoryExactPackageObservationResult& observation) {
    if(const auto* failure =
               std::get_if<RepositoryExactPackageObservationFailure>(
                       &observation);
       failure != nullptr) {
        return top_level_repository_failure(
                failure->failure, failure->package_name);
    }

    const auto& snapshot =
            std::get<RepositoryExactPackageObservation>(observation);
    PackageRelationObservationSet result{
            PackageRelationObservationCompleteness::Complete,
            {},
            {},
            {},
            {}};
    add_repository_sources(result, snapshot.configured_repository_order);
    for(const auto& source_result : snapshot.source_results) {
        if(const auto* package =
                   std::get_if<RepositoryExactPackage>(&source_result);
           package != nullptr) {
            result.packages.push_back(
                    project_repository_relation_observation(
                            *package,
                            PackageRelationObservationRole::
                                    RepositoryCandidate));
            mark_repository_identity_coverage(
                    result, package->repository, true);
            continue;
        }
        if(const auto* absent =
                   std::get_if<RepositoryExactPackageAbsent>(&source_result);
           absent != nullptr) {
            mark_repository_identity_coverage(
                    result, absent->repository, true);
            continue;
        }
        const auto& failure =
                std::get<RepositoryExactPackageSourceFailure>(source_result);
        add_repository_failure(
                result, failure.repository, failure.package_name,
                failure.reason);
    }
    return result;
}

PackageRelationObservationSet
project_repository_provider_relation_observations(
        const RepositoryProviderObservationResult& observation) {
    if(const auto* failure =
               std::get_if<RepositoryProviderObservationFailure>(
                       &observation);
       failure != nullptr) {
        return top_level_repository_failure(
                failure->failure, failure->dependency_name);
    }

    const auto& snapshot =
            std::get<RepositoryProviderObservation>(observation);
    PackageRelationObservationSet result{
            PackageRelationObservationCompleteness::Complete,
            {},
            {},
            {},
            {}};
    add_repository_sources(result, snapshot.configured_repository_order);
    for(const auto& source_result : snapshot.source_results) {
        if(const auto* source =
                   std::get_if<RepositoryProviderSourceObservation>(
                           &source_result);
           source != nullptr) {
            mark_repository_identity_coverage(
                    result, source->repository, false);
            for(const auto& package : source->packages) {
                result.packages.push_back(
                        project_repository_relation_observation(
                                package,
                                PackageRelationObservationRole::
                                        RepositoryCandidate));
            }
            continue;
        }
        const auto& failure =
                std::get<RepositoryProviderSourceFailure>(source_result);
        add_repository_failure(
                result, failure.repository, std::nullopt, failure.reason);
    }
    return result;
}

PackageRelationObservationSet project_repository_relation_observations(
        const RepositoryExactPackageObservationResult& exact_observation,
        const RepositoryProviderObservationResult& provider_observation) {
    const PackageRelationObservationSet exact =
            project_repository_exact_relation_observations(
                    exact_observation);
    const PackageRelationObservationSet provider =
            project_repository_provider_relation_observations(
                    provider_observation);
    PackageRelationObservationSet result;
    merge_repository_observations(result, exact);
    merge_repository_observations(result, provider);
    order_repository_observations(result);

    const bool has_invalid_channel =
            exact.completeness ==
                    PackageRelationObservationCompleteness::Invalid ||
            provider.completeness ==
                    PackageRelationObservationCompleteness::Invalid;
    if(has_invalid_channel) {
        result.completeness =
                PackageRelationObservationCompleteness::Invalid;
    } else if(result.failures.empty() &&
              repository_identity_coverage_is_complete(result)) {
        result.completeness =
                PackageRelationObservationCompleteness::Complete;
    } else if(result.required_sources.empty() && result.packages.empty()) {
        result.completeness =
                PackageRelationObservationCompleteness::Unavailable;
    } else {
        result.completeness =
                PackageRelationObservationCompleteness::Partial;
    }
    return result;
}
