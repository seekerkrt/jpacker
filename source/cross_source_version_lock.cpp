#include "cross_source_version_lock.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace {

CrossSourceVersionLockAssessment make_assessment(
        CrossSourceVersionLockStatus status,
        const CrossSourceVersionLockCandidateEvidence& evidence) {
    return CrossSourceVersionLockAssessment{
            status,
            evidence,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt};
}

CrossSourceVersionLockAssessment finish_assessment(
        CrossSourceVersionLockAssessment assessment,
        CrossSourceVersionLockStatus status) {
    assessment.status = status;
    return assessment;
}

bool has_available_version(
        const ObservedVersion& version,
        ObservedVersionSource expected_source) noexcept {
    return version.source() == expected_source &&
           version.invalid_reason() == nullptr && version.version() != nullptr;
}

bool is_valid_repository_candidate_identity(
        const RepositoryPackagePresent& candidate) noexcept {
    return !candidate.repository_name.empty() &&
           is_valid_package_name(candidate.package_name) &&
           is_valid_package_name(candidate.package_base);
}

std::vector<const ConsumerDependencyRequirement*>
matching_runtime_requirements(
        const AurPackageConstraintMetadata& candidate,
        const std::string& repository_package_name) {
    std::vector<const ConsumerDependencyRequirement*> matches;
    for(const DependencyRequirement& dependency : candidate.depends) {
        const auto* requirement =
                std::get_if<ConsumerDependencyRequirement>(&dependency);
        if(requirement != nullptr &&
           requirement->package_name() == repository_package_name) {
            matches.push_back(requirement);
        }
    }
    return matches;
}

} // namespace

CrossSourceVersionLockAssessment assess_cross_source_version_lock_candidate(
        const CrossSourceVersionLockCandidateEvidence& evidence) {
    CrossSourceVersionLockAssessment assessment = make_assessment(
            CrossSourceVersionLockStatus::Unknown, evidence);

    const InstalledExactPackage& installed_repository_package =
            evidence.repository_upgrade.installed_package;
    const RepositoryPackagePresent& repository_candidate =
            evidence.repository_upgrade.repository_candidate;
    const InstalledCrossSourceVersionLockConsumer& installed_consumer =
            evidence.installed_consumer;

    if(!is_valid_repository_candidate_identity(repository_candidate) ||
       !is_valid_package_name(installed_repository_package.package_name) ||
       installed_repository_package.package_name !=
               repository_candidate.package_name ||
       installed_consumer.requirement.package_name() !=
               repository_candidate.package_name ||
       installed_consumer.package.package_name ==
               repository_candidate.package_name) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }

    if(validate_package_relation_observation(installed_consumer.package)
               .has_value()) {
        return assessment;
    }
    if(installed_consumer.package.role !=
               PackageRelationObservationRole::Installed ||
       !has_available_version(
               installed_consumer.package.package_version,
               ObservedVersionSource::InstalledExactPackage)) {
        return assessment;
    }
    if(!installed_consumer.requirement.constraint().has_value()) {
        return assessment;
    }

    if(!has_available_version(
               installed_repository_package.observed_version,
               ObservedVersionSource::InstalledExactPackage)) {
        return assessment;
    }
    if(!repository_candidate.package_version.has_value()) {
        return assessment;
    }
    const ObservedVersion& candidate_version =
            repository_candidate.package_version.value();
    if(candidate_version.source() !=
       ObservedVersionSource::RepositoryExactPackage) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }
    if(!has_available_version(
               candidate_version,
               ObservedVersionSource::RepositoryExactPackage)) {
        return assessment;
    }

    const std::string& installed_version =
            *installed_repository_package.observed_version.version();
    const std::string& repository_version = *candidate_version.version();
    if(compare_arch_package_versions(repository_version, installed_version) !=
       ArchVersionOrdering::Greater) {
        return assessment;
    }

    assessment.installed_requirement_against_installed_version =
            evaluate_consumer_dependency_requirement(
                    installed_consumer.requirement,
                    installed_repository_package.observed_version);
    assessment.installed_requirement_against_repository_candidate =
            evaluate_consumer_dependency_requirement(
                    installed_consumer.requirement, candidate_version);
    if(assessment.installed_requirement_against_installed_version
                       ->satisfaction() !=
               ConstraintSatisfaction::Satisfied ||
       assessment.installed_requirement_against_repository_candidate
                       ->satisfaction() !=
               ConstraintSatisfaction::Unsatisfied) {
        return assessment;
    }

    if(const auto* failure =
               std::get_if<AurReplacementCandidateQueryFailure>(
                       &evidence.aur_replacement);
       failure != nullptr) {
        return finish_assessment(
                std::move(assessment),
                std::find(
                        failure->package_names.begin(),
                        failure->package_names.end(),
                        installed_consumer.package.package_name) !=
                                failure->package_names.end()
                        ? CrossSourceVersionLockStatus::QueryFailure
                        : CrossSourceVersionLockStatus::Ambiguous);
    }
    if(const auto* missing =
               std::get_if<AurReplacementCandidateNotFound>(
                       &evidence.aur_replacement);
       missing != nullptr) {
        return finish_assessment(
                std::move(assessment),
                is_valid_package_name(missing->package_name) &&
                                missing->package_name ==
                                        installed_consumer.package.package_name
                        ? CrossSourceVersionLockStatus::MissingReplacement
                        : CrossSourceVersionLockStatus::Ambiguous);
    }
    if(const auto* unavailable =
               std::get_if<AurReplacementCandidateMetadataUnavailable>(
                       &evidence.aur_replacement);
       unavailable != nullptr) {
        return finish_assessment(
                std::move(assessment),
                is_valid_package_name(unavailable->package_name) &&
                                unavailable->package_name ==
                                        installed_consumer.package.package_name
                        ? CrossSourceVersionLockStatus::Unknown
                        : CrossSourceVersionLockStatus::Ambiguous);
    }

    const auto* success =
            std::get_if<AurReplacementCandidateQuerySuccess>(
                    &evidence.aur_replacement);
    if(success == nullptr || success->candidates.empty()) {
        return assessment;
    }
    if(success->candidates.size() != 1U) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }

    const AurPackageConstraintMetadata& replacement =
            success->candidates.front();
    if(!is_valid_package_name(replacement.package_name) ||
       !is_valid_package_name(replacement.package_base)) {
        return assessment;
    }
    if(replacement.package_name != installed_consumer.package.package_name) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }
    if(replacement.package_version.source() !=
       ObservedVersionSource::AurExactPackage) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }
    if(!has_available_version(
               replacement.package_version,
               ObservedVersionSource::AurExactPackage)) {
        return assessment;
    }

    const std::vector<const ConsumerDependencyRequirement*> requirements =
            matching_runtime_requirements(
                    replacement, repository_candidate.package_name);
    if(requirements.empty()) {
        // A provided/indirect relation needs provider resolution. Absence of a
        // direct typed requirement is not optimistic compatibility evidence.
        return assessment;
    }
    if(requirements.size() != 1U) {
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::Ambiguous);
    }

    assessment.replacement_requirement = *requirements.front();
    assessment.replacement_requirement_against_repository_candidate =
            evaluate_consumer_dependency_requirement(
                    assessment.replacement_requirement.value(),
                    candidate_version);
    switch(assessment.replacement_requirement_against_repository_candidate
                   ->satisfaction()) {
    case ConstraintSatisfaction::Unconstrained:
    case ConstraintSatisfaction::Satisfied:
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::CompatibleReplacement);
    case ConstraintSatisfaction::Unsatisfied:
        return finish_assessment(
                std::move(assessment),
                CrossSourceVersionLockStatus::IncompatibleReplacement);
    case ConstraintSatisfaction::Unknown:
    case ConstraintSatisfaction::Invalid:
    case ConstraintSatisfaction::Conflicting:
        return assessment;
    }
    return assessment;
}
