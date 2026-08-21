#pragma once

#include "interactive_confirmation.hpp"
#include "reviewed_source_lifecycle.hpp"
#include "reviewed_source_presentation.hpp"

#include <iosfwd>
#include <variant>

// POLICY(#411): Slice 4A acceptance acknowledges only the reviewed upstream
// base commit. Invocation-local editor overlays are neither accepted build
// bytes nor authority to advance/invalidate historical reviewed state.

class ReviewedSourceAcceptanceAuthority;

// Binds a lifecycle observation to the verified 3B provenance without
// permitting PackageBase/source/revision substitution.
class ReviewedSourceVerifiedLifecycleTarget final {
public:
    ReviewedSourceVerifiedLifecycleTarget() = delete;
    ReviewedSourceVerifiedLifecycleTarget(
            const ReviewedSourceVerifiedLifecycleTarget&) = delete;
    ReviewedSourceVerifiedLifecycleTarget(
            ReviewedSourceVerifiedLifecycleTarget&&) noexcept = default;
    ReviewedSourceVerifiedLifecycleTarget& operator=(
            const ReviewedSourceVerifiedLifecycleTarget&) = delete;
    ReviewedSourceVerifiedLifecycleTarget& operator=(
            ReviewedSourceVerifiedLifecycleTarget&&) noexcept = default;
    ~ReviewedSourceVerifiedLifecycleTarget() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] const ReviewedSourceIntegrationLifecycle& lifecycle()
            const noexcept;
    [[nodiscard]] const ReviewedSourceVerifiedMaterializedReview&
    verified_review() const noexcept;
    [[nodiscard]] ReviewedSourceReviewReadiness readiness() const noexcept;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const noexcept;

private:
    friend class ReviewedSourceAcceptanceAuthority;

    ReviewedSourceVerifiedLifecycleTarget(
            ReviewedSourceReviewRequirement requirement,
            ReviewedSourceIntegrationLifecycle lifecycle,
            ReviewedSourceVerifiedMaterializedReview verified_review,
            ReviewedSourceReviewReadiness readiness) noexcept;

    ReviewedSourceReviewRequirement          requirement_;
    ReviewedSourceIntegrationLifecycle       lifecycle_;
    ReviewedSourceVerifiedMaterializedReview verified_review_;
    ReviewedSourceReviewReadiness            readiness_;
};

// This capability exists only after the complete bounded 3B rendering was
// written and flushed successfully. The rendered string itself is not review
// acceptance authority.
class PresentedReviewedSourceTarget final {
public:
    PresentedReviewedSourceTarget() = delete;
    PresentedReviewedSourceTarget(
            const PresentedReviewedSourceTarget&) = delete;
    PresentedReviewedSourceTarget(
            PresentedReviewedSourceTarget&&) noexcept = default;
    PresentedReviewedSourceTarget& operator=(
            const PresentedReviewedSourceTarget&) = delete;
    PresentedReviewedSourceTarget& operator=(
            PresentedReviewedSourceTarget&&) noexcept = default;
    ~PresentedReviewedSourceTarget() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] const ReviewedSourceIntegrationLifecycle& lifecycle()
            const noexcept;
    [[nodiscard]] const ReviewedSourceVerifiedMaterializedReview&
    verified_review() const noexcept;
    [[nodiscard]] ReviewedSourceReviewReadiness readiness() const noexcept;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const noexcept;

private:
    friend class ReviewedSourceAcceptanceAuthority;

    explicit PresentedReviewedSourceTarget(
            ReviewedSourceVerifiedLifecycleTarget target) noexcept;

    ReviewedSourceVerifiedLifecycleTarget target_;
};

class AcceptedReviewedSourceTarget final {
public:
    AcceptedReviewedSourceTarget() = delete;
    AcceptedReviewedSourceTarget(
            const AcceptedReviewedSourceTarget&) = delete;
    AcceptedReviewedSourceTarget(
            AcceptedReviewedSourceTarget&&) noexcept = default;
    AcceptedReviewedSourceTarget& operator=(
            const AcceptedReviewedSourceTarget&) = delete;
    AcceptedReviewedSourceTarget& operator=(
            AcceptedReviewedSourceTarget&&) noexcept = default;
    ~AcceptedReviewedSourceTarget() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] const SourceRevisionIdentity&
    reviewed_upstream_base_revision() const noexcept;
    [[nodiscard]] const ReviewedSourceIntegrationLifecycle& lifecycle()
            const noexcept;
    [[nodiscard]] const ReviewedSourceVerifiedMaterializedReview&
    verified_review() const noexcept;
    [[nodiscard]] ReviewedSourceReviewReadiness readiness() const noexcept;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const noexcept;
    [[nodiscard]] ConfirmationDecisionOrigin confirmation_origin()
            const noexcept;

private:
    friend class ReviewedSourceAcceptanceAuthority;

    AcceptedReviewedSourceTarget(
            PresentedReviewedSourceTarget target,
            ConfirmationDecisionOrigin confirmation_origin) noexcept;

    PresentedReviewedSourceTarget target_;
    ConfirmationDecisionOrigin    confirmation_origin_;
};

enum class ReviewedSourceCompatibilityBuildReason {
    NoDiff,
    NoConfirm,
    NonInteractiveInput,
    ExplicitReviewDecline,
    DefaultReviewDecline,
};

enum class ReviewedSourceReviewBypassReason {
    NoDiff,
    NoConfirm,
    NonInteractiveInput,
};

// Compatibility continuation deliberately drops verified/presented provenance
// and the expected store observation. It can never authorize state publication.
class ReviewedSourceCompatibilityBuildWithoutReview final {
public:
    ReviewedSourceCompatibilityBuildWithoutReview() = delete;
    ReviewedSourceCompatibilityBuildWithoutReview(
            const ReviewedSourceCompatibilityBuildWithoutReview&) = delete;
    ReviewedSourceCompatibilityBuildWithoutReview(
            ReviewedSourceCompatibilityBuildWithoutReview&&) noexcept =
            default;
    ReviewedSourceCompatibilityBuildWithoutReview& operator=(
            const ReviewedSourceCompatibilityBuildWithoutReview&) = delete;
    ReviewedSourceCompatibilityBuildWithoutReview& operator=(
            ReviewedSourceCompatibilityBuildWithoutReview&&) noexcept =
            default;
    ~ReviewedSourceCompatibilityBuildWithoutReview() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] ReviewedSourceCompatibilityBuildReason reason()
            const noexcept;

private:
    friend class ReviewedSourceAcceptanceAuthority;

    ReviewedSourceCompatibilityBuildWithoutReview(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceCompatibilityBuildReason reason) noexcept;

    AurReviewedSourceReviewIdentity          identity_;
    ReviewedSourceCompatibilityBuildReason   reason_;
};

using ReviewedSourceVerifiedLifecycleResult = std::variant<
        ReviewedSourceVerifiedLifecycleTarget,
        ReviewedSourceOperationStop>;

using PresentedReviewedSourceTargetResult = std::variant<
        PresentedReviewedSourceTarget,
        ReviewedSourceOperationStop>;

using ReviewedSourceAcceptanceDisposition = std::variant<
        AcceptedReviewedSourceTarget,
        ReviewedSourceCompatibilityBuildWithoutReview,
        ReviewedSourceOperationStop>;

[[nodiscard]] ReviewedSourceVerifiedLifecycleResult
bind_reviewed_source_verified_review(
        ReviewedSourceReviewRequirement requirement,
        AurReviewedSourceReviewIdentity verified_identity,
        ReviewedSourceVerifiedMaterializedReview verified_review);

[[nodiscard]] PresentedReviewedSourceTargetResult
present_reviewed_source_target(
        ReviewedSourceVerifiedLifecycleTarget target,
        std::ostream& output);

[[nodiscard]] ReviewedSourceAcceptanceDisposition
decide_reviewed_source_acceptance(
        PresentedReviewedSourceTarget target,
        const ConfirmationResult& confirmation);

[[nodiscard]] ReviewedSourceCompatibilityBuildWithoutReview
continue_reviewed_source_without_review(
        ReviewedSourceReviewRequirement requirement,
        ReviewedSourceReviewBypassReason reason);

[[nodiscard]] ReviewedSourceOperationStop
stop_after_reviewed_source_materialization_failure(
        ReviewedSourceReviewRequirement requirement,
        const ReviewedSourceReviewFailure& failure) noexcept;
