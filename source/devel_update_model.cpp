#include "devel_update_model.hpp"

#include <stdexcept>
#include <utility>

namespace {

void require_unknown_reason(DevelUnknownReason reason) {
    switch(reason) {
        case DevelUnknownReason::RemoteObservationFailed:
        case DevelUnknownReason::RemoteObservationTimedOut:
        case DevelUnknownReason::RemoteRefNotFound:
        case DevelUnknownReason::RemoteResultMalformed:
        case DevelUnknownReason::RemoteResultAmbiguous:
            return;
    }
    throw std::invalid_argument("Devel unknown reason is invalid.");
}

void require_unsupported_reason(DevelUnsupportedReason reason) {
    switch(reason) {
        case DevelUnsupportedReason::UnsupportedVcs:
        case DevelUnsupportedReason::UnsupportedSourceForm:
            return;
    }
    throw std::invalid_argument("Devel unsupported reason is invalid.");
}

} // namespace

DevelUpdateAssessment DevelUpdateAssessment::update_available() noexcept {
    return DevelUpdateAssessment(
        DevelUpdateAssessmentState::UpdateAvailable, std::monostate{});
}

DevelUpdateAssessment DevelUpdateAssessment::up_to_date() noexcept {
    return DevelUpdateAssessment(
        DevelUpdateAssessmentState::UpToDate, std::monostate{});
}

DevelUpdateAssessment DevelUpdateAssessment::unknown(
    DevelUnknownReason reason) {
    require_unknown_reason(reason);
    return DevelUpdateAssessment(
        DevelUpdateAssessmentState::Unknown, reason);
}

DevelUpdateAssessment DevelUpdateAssessment::unsupported(
    DevelUnsupportedReason reason) {
    require_unsupported_reason(reason);
    return DevelUpdateAssessment(
        DevelUpdateAssessmentState::Unsupported, reason);
}

DevelUpdateAssessmentState DevelUpdateAssessment::state() const noexcept {
    return state_;
}

const DevelUnknownReason* DevelUpdateAssessment::unknown_reason()
    const noexcept {
    return std::get_if<DevelUnknownReason>(&reason_);
}

const DevelRequiresCheckReason*
DevelUpdateAssessment::requires_check_reason() const noexcept {
    return std::get_if<DevelRequiresCheckReason>(&reason_);
}

const DevelUnsupportedReason* DevelUpdateAssessment::unsupported_reason()
    const noexcept {
    return std::get_if<DevelUnsupportedReason>(&reason_);
}
