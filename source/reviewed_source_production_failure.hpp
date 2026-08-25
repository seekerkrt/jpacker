#pragma once

#include "artifact_workspace.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// Production-only classification. Stage and reason are typed authority;
// localized diagnostics are a one-way projection and are never parsed back.
enum class ReviewedSourceProductionFailureStage {
    FatalStatePreflight,
    LeaseAcquisition,
    TargetResolution,
    LifecyclePlanning,
    ExactCheckoutMaterialization,
    ReviewProjection,
    ReviewMaterialization,
    ReviewBinding,
    Presentation,
    Acceptance,
    EditorOverlayObservation,
    StatePublication,
    PostPublicationCheckoutRevalidation,
    BuildBoundaryRevalidation,
};

enum class ReviewedSourceProductionFailureReason {
    UnsupportedFuture,
    UnsafeHistory,
    StateStoreFailure,
    InconsistentStateObservation,
    LeaseContended,
    LeaseUnavailable,
    TargetResolutionFailure,
    LifecycleFailure,
    ExactCheckoutFailure,
    ReviewProjectionFailure,
    ReviewMaterializationFailure,
    ReviewOperationStopped,
    OverlayObservationFailure,
    CheckoutRevalidationFailure,
    PublicationConflict,
    PublishedUncertain,
    PublicationUnsafeHistory,
    PublicationFailure,
    PostPublicationCheckoutFailure,
    BuildBoundaryRevalidationFailure,
};

using ReviewedSourceProductionFailureDetail = std::variant<
        std::monostate,
        ReviewedSourceFatalStateFailure,
        ReviewedSourceOperationStop,
        TrustedGitReviewFailure,
        ReviewedSourceReviewFailure,
        ReviewedSourcePinnedCheckoutFailure,
        ReviewedSourcePublicationConflict,
        ReviewedSourcePublicationUncertain,
        ReviewedSourcePublicationUnsafeHistory,
        ReviewedSourcePublicationFailure,
        ReviewedSourcePostPublicationCheckoutFailure,
        TrustedGitPinnedCheckoutFailure>;

struct ReviewedSourceProductionFailure {
    ReviewedSourceProductionFailureStage stage =
            ReviewedSourceProductionFailureStage::LifecyclePlanning;
    ReviewedSourceProductionFailureReason reason =
            ReviewedSourceProductionFailureReason::LifecycleFailure;
    ReviewedSourceProductionFailureDetail detail;
};

// User-visible projection for one typed failure. Every returned string is a
// complete localized diagnostic; no raw English stage label is interpolated.
std::string reviewed_source_production_failure_diagnostic(
        const ReviewedSourceProductionFailure& failure);

class ReviewedSourceProductionError final : public std::runtime_error {
public:
    explicit ReviewedSourceProductionError(
            ReviewedSourceProductionFailure failure);

    [[nodiscard]] const ReviewedSourceProductionFailure& failure()
            const noexcept {
        return failure_;
    }

    [[nodiscard]] const std::optional<ProductionSourceBuildStagedOutcome>&
    production_outcome() const noexcept {
        return production_outcome_;
    }

    void attach_production_outcome(
            ProductionSourceBuildStagedOutcome production_outcome) {
        if(production_outcome_.has_value()) {
            throw std::logic_error(
                    "Reviewed production failure already has a staged outcome.");
        }
        production_outcome_.emplace(std::move(production_outcome));
    }

private:
    ReviewedSourceProductionFailure failure_;
    std::optional<ProductionSourceBuildStagedOutcome>
            production_outcome_;
};
