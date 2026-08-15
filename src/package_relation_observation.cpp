#include "package_relation_observation.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {

bool root_attribution_less(
        const PackageRelationRootAttribution& lhs,
        const PackageRelationRootAttribution& rhs) noexcept {
    if(lhs.invocation_index != rhs.invocation_index) {
        return lhs.invocation_index < rhs.invocation_index;
    }
    return lhs.requested_name < rhs.requested_name;
}

ObservedVersionSource exact_version_source(
        const PackageRelationSourceIdentity& source) noexcept {
    if(std::holds_alternative<
               PackageRelationInstalledDatabaseIdentity>(source)) {
        return ObservedVersionSource::InstalledExactPackage;
    }
    if(std::holds_alternative<ConfiguredRepositoryIdentity>(source)) {
        return ObservedVersionSource::RepositoryExactPackage;
    }
    if(std::holds_alternative<PackageRelationAurSourceIdentity>(source)) {
        return ObservedVersionSource::AurExactPackage;
    }
    return ObservedVersionSource::LocalExactPackage;
}

ObservedVersionSource provided_version_source(
        const PackageRelationSourceIdentity& source) noexcept {
    if(std::holds_alternative<
               PackageRelationInstalledDatabaseIdentity>(source)) {
        return ObservedVersionSource::InstalledProviderCapability;
    }
    if(std::holds_alternative<ConfiguredRepositoryIdentity>(source)) {
        return ObservedVersionSource::RepositoryProviderCapability;
    }
    if(std::holds_alternative<PackageRelationAurSourceIdentity>(source)) {
        return ObservedVersionSource::AurProviderCapability;
    }
    return ObservedVersionSource::LocalProviderCapability;
}

std::optional<PackageRelationMatchInvalidReason> validate_source_role(
        const PackageRelationObservedPackage& package) noexcept {
    if(std::holds_alternative<
               PackageRelationInstalledDatabaseIdentity>(package.source)) {
        return package.role == PackageRelationObservationRole::Installed
                ? std::nullopt
                : std::optional<PackageRelationMatchInvalidReason>(
                          PackageRelationMatchInvalidReason::
                                  SourceRoleMismatch);
    }
    if(std::holds_alternative<ConfiguredRepositoryIdentity>(package.source)) {
        return package.role == PackageRelationObservationRole::PlannedTarget ||
                       package.role ==
                               PackageRelationObservationRole::
                                       RepositoryCandidate
                ? std::nullopt
                : std::optional<PackageRelationMatchInvalidReason>(
                          PackageRelationMatchInvalidReason::
                                  SourceRoleMismatch);
    }
    return package.role == PackageRelationObservationRole::PlannedTarget
            ? std::nullopt
            : std::optional<PackageRelationMatchInvalidReason>(
                      PackageRelationMatchInvalidReason::SourceRoleMismatch);
}

std::optional<PackageRelationMatchInvalidReason> validate_source_identity(
        const PackageRelationObservedPackage& package) noexcept {
    if(const auto* installed =
               std::get_if<PackageRelationInstalledDatabaseIdentity>(
                       &package.source);
       installed != nullptr) {
        if(installed->root_dir.empty() || installed->database_path.empty()) {
            return PackageRelationMatchInvalidReason::SourceIdentityMismatch;
        }
        if(package.package_base.has_value()) {
            return PackageRelationMatchInvalidReason::SourceIdentityMismatch;
        }
        return std::nullopt;
    }
    if(std::holds_alternative<ConfiguredRepositoryIdentity>(package.source)) {
        const auto& repository =
                std::get<ConfiguredRepositoryIdentity>(package.source);
        if(repository.repository_name.empty()) {
            return PackageRelationMatchInvalidReason::SourceIdentityMismatch;
        }
        if(!package.package_base.has_value() || package.package_base->empty()) {
            return PackageRelationMatchInvalidReason::MissingPackageBase;
        }
        return std::nullopt;
    }
    if(const auto* aur =
               std::get_if<PackageRelationAurSourceIdentity>(&package.source);
       aur != nullptr) {
        if(!package.package_base.has_value() || package.package_base->empty()) {
            return PackageRelationMatchInvalidReason::MissingPackageBase;
        }
        if(aur->package_name != package.package_name ||
           aur->package_base != package.package_base.value()) {
            return PackageRelationMatchInvalidReason::SourceIdentityMismatch;
        }
        return std::nullopt;
    }
    const auto& local =
            std::get<PackageRelationLocalSourceIdentity>(package.source);
    if(local.canonical_root.empty()) {
        return PackageRelationMatchInvalidReason::SourceIdentityMismatch;
    }
    if(!package.package_base.has_value() || package.package_base->empty()) {
        return PackageRelationMatchInvalidReason::MissingPackageBase;
    }
    return std::nullopt;
}

std::optional<PackageRelationMatchInvalidReason> validate_roots(
        const PackageRelationObservedPackage& package) noexcept {
    if(package.role == PackageRelationObservationRole::PlannedTarget) {
        if(package.roots.empty() ||
           std::any_of(
                   package.roots.begin(), package.roots.end(),
                   [](const PackageRelationRootAttribution& root) {
                       return !is_valid_package_name(root.requested_name);
                   })) {
            return PackageRelationMatchInvalidReason::InvalidRootAttribution;
        }
        return std::nullopt;
    }
    return package.roots.empty()
            ? std::nullopt
            : std::optional<PackageRelationMatchInvalidReason>(
                      PackageRelationMatchInvalidReason::
                              InvalidRootAttribution);
}

std::optional<PackageRelationMatchInvalidReason> validate_capability(
        const PackageRelationObservedPackage& package,
        const PackageRelationObservedCapability& provided) noexcept {
    const ObservedVersionSource expected_source =
            provided_version_source(package.source);
    if(!is_valid_package_name(provided.capability.package_name())) {
        return PackageRelationMatchInvalidReason::MalformedCapability;
    }
    if(provided.observed_version.source() != expected_source) {
        return PackageRelationMatchInvalidReason::VersionSourceMismatch;
    }
    if(provided.observed_version.invalid_reason() != nullptr) {
        return PackageRelationMatchInvalidReason::InvalidVersionMetadata;
    }

    if(provided.capability.version().has_value()) {
        const std::string* observed_version =
                provided.observed_version.version();
        if(observed_version == nullptr ||
           *observed_version != provided.capability.version().value()) {
            return PackageRelationMatchInvalidReason::MalformedCapability;
        }
        return std::nullopt;
    }

    const ObservedVersionUnknownReason* unknown_reason =
            provided.observed_version.unknown_reason();
    if(unknown_reason == nullptr ||
       *unknown_reason != ObservedVersionUnknownReason::
                                  UnversionedProviderCapability) {
        return PackageRelationMatchInvalidReason::MalformedCapability;
    }
    return std::nullopt;
}

std::optional<PackageRelationMatchInvalidReason> validate_capabilities(
        const PackageRelationObservedPackage& package) noexcept {
    for(const auto& provided : package.provides) {
        if(const auto invalid = validate_capability(package, provided);
           invalid.has_value()) {
            return invalid;
        }
    }
    return std::nullopt;
}

std::optional<PackageRelationMatchInvalidReason>
validate_observation_without_capabilities(
        const PackageRelationObservedPackage& package) noexcept {
    if(!is_valid_package_name(package.package_name)) {
        return PackageRelationMatchInvalidReason::InvalidPackageIdentity;
    }
    if(package.package_base.has_value() &&
       !is_valid_package_name(package.package_base.value())) {
        return PackageRelationMatchInvalidReason::InvalidPackageBase;
    }
    if(package.package_version.source() !=
       exact_version_source(package.source)) {
        return PackageRelationMatchInvalidReason::VersionSourceMismatch;
    }
    if(package.package_version.invalid_reason() != nullptr) {
        return PackageRelationMatchInvalidReason::InvalidVersionMetadata;
    }
    if(const auto invalid = validate_source_role(package);
       invalid.has_value()) {
        return invalid;
    }
    if(const auto invalid = validate_source_identity(package);
       invalid.has_value()) {
        return invalid;
    }
    if(const auto invalid = validate_roots(package); invalid.has_value()) {
        return invalid;
    }
    return std::nullopt;
}

PackageRelationVersionMatchKind evaluate_version(
        const DeclaredPackageRelation& relation,
        const ObservedVersion& observed_version) noexcept {
    if(!relation.constraint().has_value()) {
        return PackageRelationVersionMatchKind::Unconstrained;
    }
    if(observed_version.invalid_reason() != nullptr) {
        return PackageRelationVersionMatchKind::Invalid;
    }
    const std::string* version = observed_version.version();
    if(version == nullptr) {
        return PackageRelationVersionMatchKind::Unavailable;
    }
    return declared_package_relation_version_matches(
                   relation, std::optional<std::string>(*version))
            ? PackageRelationVersionMatchKind::Matched
            : PackageRelationVersionMatchKind::NotMatched;
}

PackageRelationVersionMatchKind aggregate_provided_version_matches(
        const std::vector<PackageRelationProvidedCapabilityMatchEvidence>&
                capability_evidence) noexcept {
    const auto has_result = [&capability_evidence](
                                    PackageRelationVersionMatchKind result) {
        return std::any_of(
                capability_evidence.begin(), capability_evidence.end(),
                [result](const auto& evidence) {
                    return evidence.version_match == result;
                });
    };
    if(has_result(PackageRelationVersionMatchKind::Matched)) {
        return PackageRelationVersionMatchKind::Matched;
    }
    if(has_result(PackageRelationVersionMatchKind::Unconstrained)) {
        return PackageRelationVersionMatchKind::Unconstrained;
    }
    if(has_result(PackageRelationVersionMatchKind::Invalid)) {
        return PackageRelationVersionMatchKind::Invalid;
    }
    if(has_result(PackageRelationVersionMatchKind::Unavailable)) {
        return PackageRelationVersionMatchKind::Unavailable;
    }
    if(!capability_evidence.empty() &&
       std::all_of(
               capability_evidence.begin(), capability_evidence.end(),
               [](const auto& evidence) {
                   return evidence.version_match ==
                           PackageRelationVersionMatchKind::NotMatched;
               })) {
        return PackageRelationVersionMatchKind::NotMatched;
    }
    return PackageRelationVersionMatchKind::NotApplicable;
}

bool is_confirmed_match(
        const PackageRelationMatchEvidence& evidence) noexcept {
    if(evidence.identity_match ==
       PackageRelationIdentityMatchKind::ExactPackage) {
        return evidence.version_match ==
                       PackageRelationVersionMatchKind::Unconstrained ||
               evidence.version_match ==
                       PackageRelationVersionMatchKind::Matched;
    }
    if(evidence.identity_match !=
       PackageRelationIdentityMatchKind::ProvidedComponent) {
        return false;
    }
    return std::any_of(
            evidence.provided_capability_evidence.begin(),
            evidence.provided_capability_evidence.end(),
            [](const auto& capability) {
                return capability.version_match ==
                               PackageRelationVersionMatchKind::Unconstrained ||
                       capability.version_match ==
                               PackageRelationVersionMatchKind::Matched;
            });
}

bool is_confirmed_nonmatch(
        const PackageRelationMatchEvidence& evidence) noexcept {
    if(evidence.invalid_reason.has_value()) return false;
    if(evidence.identity_match ==
       PackageRelationIdentityMatchKind::NoIdentityMatch) {
        return true;
    }
    if(evidence.identity_match ==
       PackageRelationIdentityMatchKind::ExactPackage) {
        return evidence.version_match ==
                PackageRelationVersionMatchKind::NotMatched;
    }
    const bool is_provided_component =
            evidence.identity_match ==
            PackageRelationIdentityMatchKind::ProvidedComponent;
    return is_provided_component &&
           !evidence.provided_capability_evidence.empty() &&
           std::all_of(
                   evidence.provided_capability_evidence.begin(),
                   evidence.provided_capability_evidence.end(),
                   [](const auto& capability) {
                       return capability.version_match ==
                               PackageRelationVersionMatchKind::NotMatched;
                   });
}

bool source_has_complete_identity_coverage(
        const PackageRelationMatchingEvidence& evidence,
        const PackageRelationSourceIdentity& source) noexcept {
    bool exact_package_identity = false;
    bool provided_component_identity = false;
    for(const auto& coverage : evidence.source_identity_coverage) {
        if(coverage.source != source) continue;
        exact_package_identity =
                exact_package_identity || coverage.exact_package_identity;
        provided_component_identity = provided_component_identity ||
                coverage.provided_component_identity;
    }
    return exact_package_identity && provided_component_identity;
}

} // namespace

PackageRelationMatchEvidence match_declared_package_relation(
        const DeclaredPackageRelation& relation,
        const PackageRelationObservedPackage& observed_package) {
    if(!is_valid_package_name(relation.target_component())) {
        return PackageRelationMatchEvidence{
                observed_package,
                PackageRelationIdentityMatchKind::InvalidInput,
                PackageRelationVersionMatchKind::Invalid,
                {},
                PackageRelationMatchInvalidReason::InvalidDeclarationTarget};
    }
    if(const auto invalid =
               validate_observation_without_capabilities(observed_package);
       invalid.has_value()) {
        return PackageRelationMatchEvidence{
                observed_package,
                PackageRelationIdentityMatchKind::InvalidInput,
                PackageRelationVersionMatchKind::Invalid,
                {},
                invalid};
    }

    if(relation.target_component() == observed_package.package_name) {
        if(const auto invalid = validate_capabilities(observed_package);
           invalid.has_value()) {
            return PackageRelationMatchEvidence{
                    observed_package,
                    PackageRelationIdentityMatchKind::InvalidInput,
                    PackageRelationVersionMatchKind::Invalid,
                    {},
                    invalid};
        }
        return PackageRelationMatchEvidence{
                observed_package,
                PackageRelationIdentityMatchKind::ExactPackage,
                evaluate_version(relation, observed_package.package_version),
                {},
                std::nullopt};
    }

    std::vector<PackageRelationProvidedCapabilityMatchEvidence>
            capability_evidence;
    capability_evidence.reserve(observed_package.provides.size());
    std::optional<PackageRelationMatchInvalidReason> capability_invalid;
    for(std::size_t index = 0; index < observed_package.provides.size();
        ++index) {
        const PackageRelationObservedCapability& capability =
                observed_package.provides[index];
        const std::optional<PackageRelationMatchInvalidReason> invalid =
                validate_capability(observed_package, capability);
        if(invalid.has_value() && !capability_invalid.has_value()) {
            capability_invalid = invalid;
        }
        if(capability.capability.package_name() !=
           relation.target_component()) {
            continue;
        }
        capability_evidence.push_back(
                PackageRelationProvidedCapabilityMatchEvidence{
                        index,
                        invalid.has_value()
                                ? PackageRelationVersionMatchKind::Invalid
                                : evaluate_version(
                                          relation,
                                          capability.observed_version)});
    }
    if(!capability_evidence.empty()) {
        const PackageRelationVersionMatchKind version_match =
                aggregate_provided_version_matches(capability_evidence);
        return PackageRelationMatchEvidence{
                observed_package,
                PackageRelationIdentityMatchKind::ProvidedComponent,
                version_match,
                std::move(capability_evidence),
                capability_invalid};
    }
    if(capability_invalid.has_value()) {
        return PackageRelationMatchEvidence{
                observed_package,
                PackageRelationIdentityMatchKind::InvalidInput,
                PackageRelationVersionMatchKind::Invalid,
                {},
                capability_invalid};
    }

    return PackageRelationMatchEvidence{
            observed_package,
            PackageRelationIdentityMatchKind::NoIdentityMatch,
            PackageRelationVersionMatchKind::NotApplicable,
            {},
            std::nullopt};
}

PackageRelationMatchingEvidence match_declared_package_relation(
        const DeclaredPackageRelation& relation,
        const PackageRelationObservationSet& observations) {
    PackageRelationMatchingEvidence evidence{
            observations.completeness,
            observations.required_sources,
            observations.source_identity_coverage,
            {},
            observations.failures};
    evidence.package_evidence.reserve(observations.packages.size());
    for(const auto& package : observations.packages) {
        evidence.package_evidence.push_back(
                match_declared_package_relation(relation, package));
    }
    return evidence;
}

bool package_relation_has_confirmed_match(
        const PackageRelationMatchingEvidence& evidence) noexcept {
    return std::any_of(
            evidence.package_evidence.begin(),
            evidence.package_evidence.end(), is_confirmed_match);
}

bool package_relation_has_complete_identity_coverage(
        const PackageRelationMatchingEvidence& evidence) noexcept {
    return !evidence.required_sources.empty() &&
           std::all_of(
                   evidence.required_sources.begin(),
                   evidence.required_sources.end(),
                   [&evidence](const auto& source) {
                       return source_has_complete_identity_coverage(
                               evidence, source);
                   });
}

bool package_relation_confirms_no_match(
        const PackageRelationMatchingEvidence& evidence) noexcept {
    return evidence.observation_completeness ==
                   PackageRelationObservationCompleteness::Complete &&
           package_relation_has_complete_identity_coverage(evidence) &&
           evidence.observation_failures.empty() &&
           std::all_of(
                   evidence.package_evidence.begin(),
                   evidence.package_evidence.end(), is_confirmed_nonmatch);
}

void add_package_relation_root_attribution(
        PackageRelationObservedPackage& package,
        PackageRelationRootAttribution root) {
    if(std::find(package.roots.begin(), package.roots.end(), root) !=
       package.roots.end()) {
        return;
    }
    package.roots.push_back(std::move(root));
    std::sort(
            package.roots.begin(), package.roots.end(),
            root_attribution_less);
}
