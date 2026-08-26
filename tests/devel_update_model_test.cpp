#include "devel_update_model.hpp"

#include "aur_update_plan.hpp"
#include "devel_package_classification.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_default_constructible_v<DevelUpdateAssessment>);
static_assert(!std::is_convertible_v<DevelUpdateAssessment, bool>);
static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              AurUpdateClassification>);
static_assert(!std::is_convertible_v<
              AurUpdateClassification,
              DevelUpdateAssessment>);

static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              DevelPackageClassification>);
static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              DevelEvidenceLevel>);
static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              DevelSourceFormDisposition>);
static_assert(!std::is_convertible_v<
              DevelPackageClassification,
              DevelUpdateAssessment>);

using UnknownFactory =
        DevelUpdateAssessment (*)(DevelUnknownReason);
using RequiresCheckFactory =
        DevelUpdateAssessment (*)(DevelRequiresCheckReason);
using UnsupportedFactory =
        DevelUpdateAssessment (*)(DevelUnsupportedReason);

static_assert(std::is_same_v<
              decltype(&DevelUpdateAssessment::unknown),
              UnknownFactory>);
static_assert(std::is_same_v<
              decltype(&DevelUpdateAssessment::requires_check),
              RequiresCheckFactory>);
static_assert(std::is_same_v<
              decltype(&DevelUpdateAssessment::unsupported),
              UnsupportedFactory>);
static_assert(std::is_invocable_v<UnknownFactory, DevelUnknownReason>);
static_assert(!std::is_invocable_v<
              UnknownFactory,
              DevelRequiresCheckReason>);
static_assert(!std::is_invocable_v<
              UnknownFactory,
              DevelUnsupportedReason>);
static_assert(std::is_invocable_v<
              RequiresCheckFactory,
              DevelRequiresCheckReason>);
static_assert(!std::is_invocable_v<
              RequiresCheckFactory,
              DevelUnknownReason>);
static_assert(!std::is_invocable_v<
              RequiresCheckFactory,
              DevelUnsupportedReason>);
static_assert(std::is_invocable_v<
              UnsupportedFactory,
              DevelUnsupportedReason>);
static_assert(!std::is_invocable_v<
              UnsupportedFactory,
              DevelUnknownReason>);
static_assert(!std::is_invocable_v<
              UnsupportedFactory,
              DevelRequiresCheckReason>);

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument.");
}

void test_all_assessment_states_are_distinct() {
    const std::array<DevelUpdateAssessment, 6> assessments = {
            DevelUpdateAssessment::not_applicable(),
            DevelUpdateAssessment::update_available(),
            DevelUpdateAssessment::up_to_date(),
            DevelUpdateAssessment::unknown(
                    DevelUnknownReason::RemoteObservationFailed),
            DevelUpdateAssessment::requires_check(
                    DevelRequiresCheckReason::
                            NoAuthoritativeBuildProvenance),
            DevelUpdateAssessment::unsupported(
                    DevelUnsupportedReason::UnsupportedVcs)};
    const std::array<DevelUpdateAssessmentState, 6> expected_states = {
            DevelUpdateAssessmentState::NotApplicable,
            DevelUpdateAssessmentState::UpdateAvailable,
            DevelUpdateAssessmentState::UpToDate,
            DevelUpdateAssessmentState::Unknown,
            DevelUpdateAssessmentState::RequiresCheck,
            DevelUpdateAssessmentState::Unsupported};

    for(std::size_t index = 0; index < assessments.size(); ++index) {
        require(assessments[index].state() == expected_states[index],
                "Devel assessment state differs.");
        for(std::size_t other = index + 1; other < assessments.size();
            ++other) {
            require(assessments[index] != assessments[other],
                    "Distinct devel assessment states were flattened.");
        }
    }
}

void test_success_states_have_no_reason() {
    for(const DevelUpdateAssessment& assessment : {
                DevelUpdateAssessment::not_applicable(),
                DevelUpdateAssessment::update_available(),
                DevelUpdateAssessment::up_to_date()}) {
        require(assessment.unknown_reason() == nullptr &&
                        assessment.requires_check_reason() == nullptr &&
                        assessment.unsupported_reason() == nullptr,
                "Success assessment carried a non-success reason.");
    }
}

void test_all_unknown_reasons_are_typed_and_lossless() {
    const std::array reasons = {
            DevelUnknownReason::RemoteObservationFailed,
            DevelUnknownReason::RemoteObservationTimedOut,
            DevelUnknownReason::RemoteRefNotFound,
            DevelUnknownReason::RemoteResultMalformed,
            DevelUnknownReason::RemoteResultAmbiguous};

    for(DevelUnknownReason reason : reasons) {
        const DevelUpdateAssessment assessment =
                DevelUpdateAssessment::unknown(reason);
        require(assessment.state() == DevelUpdateAssessmentState::Unknown &&
                        assessment.unknown_reason() != nullptr &&
                        *assessment.unknown_reason() == reason &&
                        assessment.requires_check_reason() == nullptr &&
                        assessment.unsupported_reason() == nullptr,
                "Unknown reason was flattened or entered another state.");
    }
}

void test_all_requires_check_reasons_are_typed_and_lossless() {
    const std::array reasons = {
            DevelRequiresCheckReason::SuffixCandidateOnly,
            DevelRequiresCheckReason::NoAuthoritativeBuildProvenance,
            DevelRequiresCheckReason::InstalledArtifactDrift,
            DevelRequiresCheckReason::AurRecipeAdvanced,
            DevelRequiresCheckReason::SourceMetadataMissing,
            DevelRequiresCheckReason::SourceMetadataMalformed,
            DevelRequiresCheckReason::SourceIdentityChanged,
            DevelRequiresCheckReason::TransportRequiresCheck,
            DevelRequiresCheckReason::SelectorRequiresCheck,
            DevelRequiresCheckReason::MultipleFloatingSources,
            DevelRequiresCheckReason::ArchitectureSpecificSourceUnresolved,
            DevelRequiresCheckReason::ProvenanceMissing,
            DevelRequiresCheckReason::ProvenanceInvalid,
            DevelRequiresCheckReason::ProvenanceCorrupted,
            DevelRequiresCheckReason::ProvenanceFutureSchema,
            DevelRequiresCheckReason::BuildSourceProofUnavailable};

    for(DevelRequiresCheckReason reason : reasons) {
        const DevelUpdateAssessment assessment =
                DevelUpdateAssessment::requires_check(reason);
        require(assessment.state() ==
                                DevelUpdateAssessmentState::RequiresCheck &&
                        assessment.requires_check_reason() != nullptr &&
                        *assessment.requires_check_reason() == reason &&
                        assessment.unknown_reason() == nullptr &&
                        assessment.unsupported_reason() == nullptr,
                "Check-required reason was flattened or entered another state.");
    }
}

void test_all_unsupported_reasons_are_typed_and_lossless() {
    const std::array reasons = {
            DevelUnsupportedReason::UnsupportedVcs,
            DevelUnsupportedReason::UnsupportedSourceForm};

    for(DevelUnsupportedReason reason : reasons) {
        const DevelUpdateAssessment assessment =
                DevelUpdateAssessment::unsupported(reason);
        require(assessment.state() ==
                                DevelUpdateAssessmentState::Unsupported &&
                        assessment.unsupported_reason() != nullptr &&
                        *assessment.unsupported_reason() == reason &&
                        assessment.unknown_reason() == nullptr &&
                        assessment.requires_check_reason() == nullptr,
                "Unsupported reason was flattened or entered another state.");
    }
}

void test_invalid_reason_values_are_rejected() {
    expect_invalid_argument([] {
        static_cast<void>(DevelUpdateAssessment::unknown(
                static_cast<DevelUnknownReason>(-1)));
    });
    expect_invalid_argument([] {
        static_cast<void>(DevelUpdateAssessment::requires_check(
                static_cast<DevelRequiresCheckReason>(-1)));
    });
    expect_invalid_argument([] {
        static_cast<void>(DevelUpdateAssessment::unsupported(
                static_cast<DevelUnsupportedReason>(-1)));
    });
}

} // namespace

void run_devel_update_model_tests() {
    test_all_assessment_states_are_distinct();
    test_success_states_have_no_reason();
    test_all_unknown_reasons_are_typed_and_lossless();
    test_all_requires_check_reasons_are_typed_and_lossless();
    test_all_unsupported_reasons_are_typed_and_lossless();
    test_invalid_reason_values_are_rejected();
}
