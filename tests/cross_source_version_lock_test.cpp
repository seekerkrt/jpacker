#include "cross_source_version_lock.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

void run_cross_source_version_lock_observation_tests();

static_assert(
    !std::is_pointer_v<decltype(RepositoryUpgradeCandidate::repository_candidate)>);
static_assert(
    !std::is_pointer_v<decltype(InstalledCrossSourceVersionLockConsumer::package)>);
static_assert(
    !std::is_pointer_v<decltype(CrossSourceVersionLockAssessment::evidence)>);

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ConsumerDependencyRequirement require_consumer_requirement(
    const std::string& specification) {
    const DependencyRequirementParseResult result =
        parse_dependency_requirement(specification);
    expect(
        result.failure() == nullptr,
        "Unexpected dependency parse failure: " + specification);
    const DependencyRequirement* requirement = result.requirement();
    expect(requirement != nullptr, "Typed dependency requirement is missing");
    const auto* consumer =
        std::get_if<ConsumerDependencyRequirement>(requirement);
    expect(consumer != nullptr, "Expected a consumer dependency requirement");
    return *consumer;
}

DependencyRequirement dependency_requirement(
    const std::string& specification) {
    return DependencyRequirement{require_consumer_requirement(specification)};
}

RepositoryUpgradeCandidate virtualbox_repository_candidate() {
    return RepositoryUpgradeCandidate{
        InstalledExactPackage{
            "virtualbox",
            ObservedVersion::available(
                ObservedVersionSource::InstalledExactPackage,
                "7.2.14-1")},
        RepositoryPackagePresent{
            "extra",
            0,
            "virtualbox",
            "virtualbox",
            ObservedVersion::available(
                ObservedVersionSource::RepositoryExactPackage,
                "7.2.16-1"),
            std::vector<std::string>{"core", "extra"},
            {}}};
}

InstalledCrossSourceVersionLockConsumer installed_virtualbox_consumer() {
    return InstalledCrossSourceVersionLockConsumer{
        PackageRelationObservedPackage{
            "virtualbox-ext-oracle",
            std::nullopt,
            ObservedVersion::available(
                ObservedVersionSource::InstalledExactPackage,
                "7.2.14-1"),
            {},
            PackageRelationInstalledDatabaseIdentity{
                std::filesystem::path("/"),
                std::filesystem::path("/var/lib/pacman")},
            PackageRelationObservationRole::Installed,
            {}},
        require_consumer_requirement("virtualbox=7.2.14")};
}

AurPackageConstraintMetadata virtualbox_replacement(
    std::string version,
    std::vector<DependencyRequirement> depends,
    std::string package_base = "virtualbox-ext-oracle") {
    return AurPackageConstraintMetadata{
        "virtualbox-ext-oracle",
        std::move(package_base),
        ObservedVersion::available(
            ObservedVersionSource::AurExactPackage,
            std::move(version)),
        std::move(depends),
        {},
        {},
        {},
        {}};
}

CrossSourceVersionLockCandidateEvidence candidate_evidence(
    AurReplacementCandidateQueryResult aur_replacement) {
    return CrossSourceVersionLockCandidateEvidence{
        virtualbox_repository_candidate(),
        installed_virtualbox_consumer(),
        std::move(aur_replacement)};
}

void expect_satisfaction(
    const std::optional<ConstraintEvaluation>& evaluation,
    ConstraintSatisfaction expected,
    const std::string& context) {
    expect(evaluation.has_value(), context + ": evaluation is missing");
    expect(
        evaluation->satisfaction() == expected,
        context + ": satisfaction differs");
}

void test_virtualbox_compatible_replacement() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQuerySuccess{{virtualbox_replacement(
                "7.2.16-1",
                {dependency_requirement(
                    "virtualbox=7.2.16")})}}));

    expect(
        assessment.status ==
            CrossSourceVersionLockStatus::CompatibleReplacement,
        "The virtualbox replacement candidate was not compatible");
    expect_satisfaction(
        assessment.installed_requirement_against_installed_version,
        ConstraintSatisfaction::Satisfied,
        "Installed virtualbox lock");
    expect_satisfaction(
        assessment.installed_requirement_against_repository_candidate,
        ConstraintSatisfaction::Unsatisfied,
        "Repository virtualbox candidate against installed lock");
    expect_satisfaction(
        assessment
            .replacement_requirement_against_repository_candidate,
        ConstraintSatisfaction::Satisfied,
        "AUR replacement lock against repository candidate");
    expect(
        assessment.replacement_requirement.has_value() &&
            assessment.replacement_requirement->raw_specification() ==
                "virtualbox=7.2.16",
        "The typed replacement requirement was not retained");
}

void test_replacement_missing() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateNotFound{
                "virtualbox-ext-oracle"}));
    expect(
        assessment.status ==
            CrossSourceVersionLockStatus::MissingReplacement,
        "Confirmed AUR replacement absence was not retained");
    expect(
        !assessment.replacement_requirement.has_value(),
        "Missing replacement acquired a dependency requirement");
}

void test_replacement_incompatible() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQuerySuccess{{virtualbox_replacement(
                "7.2.16-1",
                {dependency_requirement(
                    "virtualbox=7.2.14")})}}));
    expect(
        assessment.status ==
            CrossSourceVersionLockStatus::IncompatibleReplacement,
        "An old AUR dependency lock was not incompatible");
    expect_satisfaction(
        assessment
            .replacement_requirement_against_repository_candidate,
        ConstraintSatisfaction::Unsatisfied,
        "Incompatible AUR replacement lock");
}

void test_replacement_metadata_unknown() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateMetadataUnavailable{
                "virtualbox-ext-oracle",
                std::string("virtualbox-ext-oracle"),
                ObservedVersionUnknownReason::
                    PartialSourceFailure}));
    expect(
        assessment.status == CrossSourceVersionLockStatus::Unknown,
        "Incomplete replacement metadata was treated optimistically");
}

void test_aur_query_failure_is_not_missing() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQueryFailure{
                {"virtualbox-ext-oracle"},
                "fixture AUR query failure"}));
    expect(
        assessment.status == CrossSourceVersionLockStatus::QueryFailure,
        "AUR query failure was flattened into another result");
    expect(
        assessment.status !=
            CrossSourceVersionLockStatus::MissingReplacement,
        "AUR query failure was treated as replacement absence");
}

void test_unrelated_aur_query_failure_is_ambiguous() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQueryFailure{
                {"unrelated-package"},
                "fixture unrelated AUR query failure"}));
    expect(
        assessment.status == CrossSourceVersionLockStatus::Ambiguous,
        "An unrelated AUR query failure was accepted as correlated");
}

void test_ambiguous_replacement_candidates() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQuerySuccess{{virtualbox_replacement(
                                                     "7.2.16-1",
                                                     {dependency_requirement(
                                                         "virtualbox=7.2.16")}),
                                                 virtualbox_replacement(
                                                     "7.2.16-1",
                                                     {dependency_requirement(
                                                         "virtualbox=7.2.16")},
                                                     "virtualbox-ext-oracle-alternate")}}));
    expect(
        assessment.status == CrossSourceVersionLockStatus::Ambiguous,
        "Multiple AUR replacement identities were selected implicitly");
}

void test_indirect_dependency_fails_closed() {
    const CrossSourceVersionLockAssessment assessment =
        assess_cross_source_version_lock_candidate(candidate_evidence(
            AurReplacementCandidateQuerySuccess{{virtualbox_replacement(
                "7.2.16-1",
                {dependency_requirement(
                    "virtualbox-provider=7.2.16")})}}));
    expect(
        assessment.status == CrossSourceVersionLockStatus::Unknown,
        "Unsupported provider resolution became compatible");
}

} // namespace

int main() {
    try {
        test_virtualbox_compatible_replacement();
        test_replacement_missing();
        test_replacement_incompatible();
        test_replacement_metadata_unknown();
        test_aur_query_failure_is_not_missing();
        test_unrelated_aur_query_failure_is_ambiguous();
        test_ambiguous_replacement_candidates();
        test_indirect_dependency_fails_closed();
        run_cross_source_version_lock_observation_tests();
        std::cout << "cross-source version-lock tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "cross-source version-lock tests: " << error.what()
                  << '\n';
        return 1;
    }
}
