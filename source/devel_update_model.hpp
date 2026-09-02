#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

enum class DevelUpdateAssessmentState {
    NotApplicable,
    UpdateAvailable,
    UpToDate,
    Unknown,
    RequiresCheck,
    Unsupported,
};

// Reasons describe evidence boundaries only. Observation adapters remain
// responsible for producing them; this module performs no I/O. Separate enums
// prevent transient, check-required, and permanently unsupported outcomes from
// being exchanged at an assessment factory.
enum class DevelUnknownReason {
    RemoteObservationFailed,
    RemoteObservationTimedOut,
    RemoteRefNotFound,
    RemoteResultMalformed,
    RemoteResultAmbiguous,
};

enum class DevelRequiresCheckReason {
    SuffixCandidateOnly,
    NoAuthoritativeBuildProvenance,
    InstalledArtifactDrift,
    AurRecipeAdvanced,
    SourceMetadataMissing,
    SourceMetadataMalformed,
    SourceIdentityChanged,
    TransportRequiresCheck,
    SelectorRequiresCheck,
    MultipleFloatingSources,
    ArchitectureSpecificSourceUnresolved,
    ProvenanceMissing,
    ProvenanceInvalid,
    ProvenanceCorrupted,
    ProvenanceFutureSchema,
    BuildSourceProofUnavailable,
};

enum class DevelUnsupportedReason {
    UnsupportedVcs,
    UnsupportedSourceForm,
};

class DevelUpdateAssessment final {
public:
    DevelUpdateAssessment() = delete;
    DevelUpdateAssessment(const DevelUpdateAssessment&) = default;
    DevelUpdateAssessment(DevelUpdateAssessment&&) noexcept = default;
    DevelUpdateAssessment& operator=(const DevelUpdateAssessment&) = default;
    DevelUpdateAssessment& operator=(DevelUpdateAssessment&&) noexcept =
        default;
    ~DevelUpdateAssessment() = default;

    [[nodiscard]] static DevelUpdateAssessment not_applicable() noexcept {
        return DevelUpdateAssessment(
            DevelUpdateAssessmentState::NotApplicable,
            std::monostate{});
    }
    [[nodiscard]] static DevelUpdateAssessment update_available() noexcept;
    [[nodiscard]] static DevelUpdateAssessment up_to_date() noexcept;
    [[nodiscard]] static DevelUpdateAssessment unknown(
        DevelUnknownReason reason);
    [[nodiscard]] static DevelUpdateAssessment requires_check(
        DevelRequiresCheckReason reason) {
        switch(reason) {
            case DevelRequiresCheckReason::SuffixCandidateOnly:
            case DevelRequiresCheckReason::NoAuthoritativeBuildProvenance:
            case DevelRequiresCheckReason::InstalledArtifactDrift:
            case DevelRequiresCheckReason::AurRecipeAdvanced:
            case DevelRequiresCheckReason::SourceMetadataMissing:
            case DevelRequiresCheckReason::SourceMetadataMalformed:
            case DevelRequiresCheckReason::SourceIdentityChanged:
            case DevelRequiresCheckReason::TransportRequiresCheck:
            case DevelRequiresCheckReason::SelectorRequiresCheck:
            case DevelRequiresCheckReason::MultipleFloatingSources:
            case DevelRequiresCheckReason::
                ArchitectureSpecificSourceUnresolved:
            case DevelRequiresCheckReason::ProvenanceMissing:
            case DevelRequiresCheckReason::ProvenanceInvalid:
            case DevelRequiresCheckReason::ProvenanceCorrupted:
            case DevelRequiresCheckReason::ProvenanceFutureSchema:
            case DevelRequiresCheckReason::BuildSourceProofUnavailable:
                return DevelUpdateAssessment(
                    DevelUpdateAssessmentState::RequiresCheck,
                    reason);
        }
        throw std::invalid_argument(
            "Devel check-required reason is invalid.");
    }
    [[nodiscard]] static DevelUpdateAssessment unsupported(
        DevelUnsupportedReason reason);

    [[nodiscard]] DevelUpdateAssessmentState state() const noexcept;
    [[nodiscard]] const DevelUnknownReason* unknown_reason() const noexcept;
    [[nodiscard]] const DevelRequiresCheckReason* requires_check_reason()
        const noexcept;
    [[nodiscard]] const DevelUnsupportedReason* unsupported_reason()
        const noexcept;

    bool operator==(const DevelUpdateAssessment&) const = default;

private:
    using Reason = std::variant<
        std::monostate,
        DevelUnknownReason,
        DevelRequiresCheckReason,
        DevelUnsupportedReason>;

    DevelUpdateAssessment(
        DevelUpdateAssessmentState state,
        Reason reason) noexcept
        : state_(state), reason_(std::move(reason)) {
    }

    DevelUpdateAssessmentState state_;
    Reason reason_;
};
