#include "reviewed_source_production_failure.hpp"

#include "localization.hpp"

#include <utility>

std::string reviewed_source_production_failure_diagnostic(
        const ReviewedSourceProductionFailure& failure) {
    switch(failure.reason) {
    case ReviewedSourceProductionFailureReason::UnsupportedFuture:
        return localization::translate_message(
                "Reviewed source state uses an unsupported future schema; the build was not started.");
    case ReviewedSourceProductionFailureReason::UnsafeHistory:
        return localization::translate_message(
                "Reviewed source state history is unsafe; the build was not started.");
    case ReviewedSourceProductionFailureReason::StateStoreFailure:
        return localization::translate_message(
                "Reviewed source state could not be read safely; the build was not started.");
    case ReviewedSourceProductionFailureReason::InconsistentStateObservation:
        return localization::translate_message(
                "Reviewed source state observation was inconsistent; the build was not started.");
    case ReviewedSourceProductionFailureReason::LeaseContended:
        return localization::format_translated_message(
                "Reviewed source {} lease is already held; the build was not started.",
                "PackageBase");
    case ReviewedSourceProductionFailureReason::LeaseUnavailable:
        return localization::format_translated_message(
                "Reviewed source {} lease could not be acquired; the build was not started.",
                "PackageBase");
    case ReviewedSourceProductionFailureReason::TargetResolutionFailure:
        return localization::translate_message(
                "Reviewed source target revision resolution failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::LifecycleFailure:
        return localization::translate_message(
                "Reviewed source lifecycle planning failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::ExactCheckoutFailure:
        return localization::translate_message(
                "Reviewed source exact checkout materialization failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::ReviewProjectionFailure:
        return localization::translate_message(
                "Reviewed source review projection failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::ReviewMaterializationFailure:
        return localization::translate_message(
                "Reviewed source review materialization failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::ReviewOperationStopped:
        return localization::translate_message(
                "Reviewed source review could not produce explicit acceptance; the build was not started.");
    case ReviewedSourceProductionFailureReason::OverlayObservationFailure:
        return localization::translate_message(
                "Reviewed source editor overlay could not be proven; the build was not started.");
    case ReviewedSourceProductionFailureReason::CheckoutRevalidationFailure:
        return localization::translate_message(
                "Reviewed source checkout revalidation failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::PublicationConflict:
        return localization::translate_message(
                "Reviewed source state publication conflicted with current state; the build was not started.");
    case ReviewedSourceProductionFailureReason::PublishedUncertain:
        return localization::translate_message(
                "Reviewed source state publication outcome is uncertain; the build was not started and automatic retry is forbidden.");
    case ReviewedSourceProductionFailureReason::PublicationUnsafeHistory:
        return localization::translate_message(
                "Reviewed source state publication encountered unsafe history; the build was not started.");
    case ReviewedSourceProductionFailureReason::PublicationFailure:
        return localization::translate_message(
                "Reviewed source state publication failed; the build was not started.");
    case ReviewedSourceProductionFailureReason::PostPublicationCheckoutFailure:
        return localization::translate_message(
                "Reviewed source checkout changed after state publication; the build was not started.");
    case ReviewedSourceProductionFailureReason::BuildBoundaryRevalidationFailure:
        return localization::format_translated_message(
                "Reviewed source checkout changed before {}; the build was not started.",
                "makepkg");
    }
    return localization::translate_message(
            "Reviewed source production preparation failed; the build was not started.");
}

ReviewedSourceProductionError::ReviewedSourceProductionError(
        ReviewedSourceProductionFailure failure)
    : std::runtime_error(
              reviewed_source_production_failure_diagnostic(failure)),
      failure_(std::move(failure)) {}
