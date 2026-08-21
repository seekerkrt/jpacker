#include "reviewed_source_acceptance.hpp"

#include <ios>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>

namespace {

ReviewedSourceOperationStop identity_mismatch(
        const AurReviewedSourceReviewIdentity& expected,
        const AurReviewedSourceReviewIdentity& observed) {
    if(expected.package_base().package_base() !=
       observed.package_base().package_base()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::PackageBaseMismatch);
    }
    if(expected.source() != observed.source()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::SourceIdentityMismatch);
    }
    if(expected.git_object_format() != observed.git_object_format()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::GitObjectFormatMismatch);
    }
    if(expected.target_revision() != observed.target_revision()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::TargetRevisionMismatch);
    }
    return ReviewedSourceOperationStop::make(
            ReviewedSourceOperationStopReason::LifecycleMismatch);
}

ReviewedSourceOperationStop revision_mismatch(
        const SourceRevisionIdentity& expected,
        const SourceRevisionIdentity& observed,
        ReviewedSourceOperationStopReason value_mismatch_reason) {
    const GitObjectFormat* expected_format = expected.git_object_format();
    const GitObjectFormat* observed_format = observed.git_object_format();
    if(expected_format == nullptr || observed_format == nullptr ||
       *expected_format != *observed_format) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::GitObjectFormatMismatch);
    }
    return ReviewedSourceOperationStop::make(value_mismatch_reason);
}

const SourceRevisionIdentity& materialized_target_revision(
        const ReviewedSourceMaterializedReview& review) {
    return std::visit(
            [](const auto& value) -> const SourceRevisionIdentity& {
                using Review = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<
                                     Review,
                                     ReviewedSourceMaterializedAlreadyReviewed>) {
                    return value.revision;
                } else {
                    return value.target;
                }
            },
            review);
}

const ReviewedSourceReviewBody* materialized_review_body(
        const ReviewedSourceMaterializedReview& review) noexcept {
    return std::visit(
            [](const auto& value) -> const ReviewedSourceReviewBody* {
                using Review = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<
                                     Review,
                                     ReviewedSourceMaterializedAlreadyReviewed>) {
                    return nullptr;
                } else {
                    return &value.review;
                }
            },
            review);
}

ReviewedSourceCompatibilityBuildReason compatibility_reason(
        ReviewedSourceReviewBypassReason reason) noexcept {
    switch(reason) {
    case ReviewedSourceReviewBypassReason::NoDiff:
        return ReviewedSourceCompatibilityBuildReason::NoDiff;
    case ReviewedSourceReviewBypassReason::NoConfirm:
        return ReviewedSourceCompatibilityBuildReason::NoConfirm;
    case ReviewedSourceReviewBypassReason::NonInteractiveInput:
        return ReviewedSourceCompatibilityBuildReason::NonInteractiveInput;
    }
    return ReviewedSourceCompatibilityBuildReason::NoDiff;
}

} // namespace

class ReviewedSourceAcceptanceAuthority final {
public:
    static ReviewedSourceVerifiedLifecycleTarget verified_target(
            ReviewedSourceReviewRequirement requirement,
            ReviewedSourceIntegrationLifecycle lifecycle,
            ReviewedSourceVerifiedMaterializedReview verified_review,
            ReviewedSourceReviewReadiness readiness) {
        return ReviewedSourceVerifiedLifecycleTarget(
                std::move(requirement), std::move(lifecycle),
                std::move(verified_review), readiness);
    }

    static PresentedReviewedSourceTarget presented_target(
            ReviewedSourceVerifiedLifecycleTarget target) {
        return PresentedReviewedSourceTarget(std::move(target));
    }

    static AcceptedReviewedSourceTarget accepted_target(
            PresentedReviewedSourceTarget target,
            ConfirmationDecisionOrigin confirmation_origin) {
        return AcceptedReviewedSourceTarget(
                std::move(target), confirmation_origin);
    }

    static ReviewedSourceCompatibilityBuildWithoutReview compatibility(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceCompatibilityBuildReason reason) {
        return ReviewedSourceCompatibilityBuildWithoutReview(
                std::move(identity), reason);
    }
};

ReviewedSourceVerifiedLifecycleTarget::
        ReviewedSourceVerifiedLifecycleTarget(
                ReviewedSourceReviewRequirement requirement,
                ReviewedSourceIntegrationLifecycle lifecycle,
                ReviewedSourceVerifiedMaterializedReview verified_review,
                ReviewedSourceReviewReadiness readiness) noexcept
    : requirement_(std::move(requirement)), lifecycle_(std::move(lifecycle)),
      verified_review_(std::move(verified_review)), readiness_(readiness) {}

const AurReviewedSourceReviewIdentity&
ReviewedSourceVerifiedLifecycleTarget::identity() const noexcept {
    return requirement_.identity();
}

const ReviewedSourceIntegrationLifecycle&
ReviewedSourceVerifiedLifecycleTarget::lifecycle() const noexcept {
    return lifecycle_;
}

const ReviewedSourceVerifiedMaterializedReview&
ReviewedSourceVerifiedLifecycleTarget::verified_review() const noexcept {
    return verified_review_;
}

ReviewedSourceReviewReadiness
ReviewedSourceVerifiedLifecycleTarget::readiness() const noexcept {
    return readiness_;
}

const ReviewedSourceExpectedStateObservation&
ReviewedSourceVerifiedLifecycleTarget::expected_state_observation()
        const noexcept {
    return requirement_.expected_state_observation();
}

PresentedReviewedSourceTarget::PresentedReviewedSourceTarget(
        ReviewedSourceVerifiedLifecycleTarget target) noexcept
    : target_(std::move(target)) {}

const AurReviewedSourceReviewIdentity&
PresentedReviewedSourceTarget::identity() const noexcept {
    return target_.identity();
}

const ReviewedSourceIntegrationLifecycle&
PresentedReviewedSourceTarget::lifecycle() const noexcept {
    return target_.lifecycle();
}

const ReviewedSourceVerifiedMaterializedReview&
PresentedReviewedSourceTarget::verified_review() const noexcept {
    return target_.verified_review();
}

ReviewedSourceReviewReadiness
PresentedReviewedSourceTarget::readiness() const noexcept {
    return target_.readiness();
}

const ReviewedSourceExpectedStateObservation&
PresentedReviewedSourceTarget::expected_state_observation() const noexcept {
    return target_.expected_state_observation();
}

AcceptedReviewedSourceTarget::AcceptedReviewedSourceTarget(
        PresentedReviewedSourceTarget target,
        ConfirmationDecisionOrigin confirmation_origin) noexcept
    : target_(std::move(target)), confirmation_origin_(confirmation_origin) {}

const AurReviewedSourceReviewIdentity&
AcceptedReviewedSourceTarget::identity() const noexcept {
    return target_.identity();
}

const SourceRevisionIdentity&
AcceptedReviewedSourceTarget::reviewed_upstream_base_revision()
        const noexcept {
    return identity().target_revision();
}

const ReviewedSourceIntegrationLifecycle&
AcceptedReviewedSourceTarget::lifecycle() const noexcept {
    return target_.lifecycle();
}

const ReviewedSourceVerifiedMaterializedReview&
AcceptedReviewedSourceTarget::verified_review() const noexcept {
    return target_.verified_review();
}

ReviewedSourceReviewReadiness
AcceptedReviewedSourceTarget::readiness() const noexcept {
    return target_.readiness();
}

const ReviewedSourceExpectedStateObservation&
AcceptedReviewedSourceTarget::expected_state_observation() const noexcept {
    return target_.expected_state_observation();
}

ConfirmationDecisionOrigin
AcceptedReviewedSourceTarget::confirmation_origin() const noexcept {
    return confirmation_origin_;
}

ReviewedSourceCompatibilityBuildWithoutReview::
        ReviewedSourceCompatibilityBuildWithoutReview(
                AurReviewedSourceReviewIdentity identity,
                ReviewedSourceCompatibilityBuildReason reason) noexcept
    : identity_(std::move(identity)), reason_(reason) {}

const AurReviewedSourceReviewIdentity&
ReviewedSourceCompatibilityBuildWithoutReview::identity() const noexcept {
    return identity_;
}

ReviewedSourceCompatibilityBuildReason
ReviewedSourceCompatibilityBuildWithoutReview::reason() const noexcept {
    return reason_;
}

ReviewedSourceVerifiedLifecycleResult bind_reviewed_source_verified_review(
        ReviewedSourceReviewRequirement requirement,
        AurReviewedSourceReviewIdentity verified_identity,
        ReviewedSourceVerifiedMaterializedReview verified_review) {
    if(requirement.identity() != verified_identity) {
        return identity_mismatch(requirement.identity(), verified_identity);
    }

    const ReviewedSourceMaterializedReview& materialized =
            verified_review.review();
    const SourceRevisionIdentity& observed_target =
            materialized_target_revision(materialized);
    if(observed_target != verified_identity.target_revision()) {
        return revision_mismatch(
                verified_identity.target_revision(), observed_target,
                ReviewedSourceOperationStopReason::TargetRevisionMismatch);
    }

    const ReviewedSourceReviewBody* review_body =
            materialized_review_body(materialized);
    if(review_body == nullptr) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::LifecycleMismatch);
    }
    const ReviewedSourceReviewReadiness readiness = review_body->readiness;

    ReviewedSourceIntegrationLifecycle lifecycle =
            ReviewedSourceLifecycleInitialFullReview{};
    switch(requirement.kind()) {
    case ReviewedSourceReviewRequirementKind::InitialFullReview:
        if(!std::holds_alternative<
                   ReviewedSourceMaterializedInitialFullReview>(materialized)) {
            return ReviewedSourceOperationStop::make(
                    ReviewedSourceOperationStopReason::LifecycleMismatch);
        }
        lifecycle = ReviewedSourceLifecycleInitialFullReview{};
        break;
    case ReviewedSourceReviewRequirementKind::
            AbnormalStateRebindFullReview:
        if(!std::holds_alternative<
                   ReviewedSourceMaterializedInitialFullReview>(materialized) ||
           requirement.abnormal_reason() == nullptr) {
            return ReviewedSourceOperationStop::make(
                    ReviewedSourceOperationStopReason::LifecycleMismatch);
        }
        lifecycle =
                ReviewedSourceLifecycleAbnormalStateRebindFullReview{
                        *requirement.abnormal_reason()};
        break;
    case ReviewedSourceReviewRequirementKind::UpdateReview: {
        const SourceRevisionIdentity* expected_baseline =
                requirement.baseline();
        if(expected_baseline == nullptr) {
            return ReviewedSourceOperationStop::make(
                    ReviewedSourceOperationStopReason::LifecycleMismatch);
        }
        if(const auto* update = std::get_if<
                   ReviewedSourceMaterializedUpdateReview>(&materialized)) {
            if(update->baseline != *expected_baseline) {
                return revision_mismatch(
                        *expected_baseline, update->baseline,
                        ReviewedSourceOperationStopReason::BaselineMismatch);
            }
            lifecycle = ReviewedSourceLifecycleUpdateReview{
                    update->baseline, update->relation};
            break;
        }
        if(const auto* rebaseline = std::get_if<
                   ReviewedSourceMaterializedRebaselineFullReview>(
                           &materialized)) {
            if(rebaseline->unavailable_baseline != *expected_baseline) {
                return revision_mismatch(
                        *expected_baseline,
                        rebaseline->unavailable_baseline,
                        ReviewedSourceOperationStopReason::BaselineMismatch);
            }
            lifecycle = ReviewedSourceLifecycleRebaselineFullReview{
                    rebaseline->unavailable_baseline, rebaseline->reason};
            break;
        }
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::LifecycleMismatch);
    }
    }

    return ReviewedSourceAcceptanceAuthority::verified_target(
            std::move(requirement), std::move(lifecycle),
            std::move(verified_review), readiness);
}

PresentedReviewedSourceTargetResult present_reviewed_source_target(
        ReviewedSourceVerifiedLifecycleTarget target,
        std::ostream& output) {
    ReviewedSourcePresentationResult rendered =
            render_reviewed_source_presentation(target.verified_review());
    const auto* presentation =
            std::get_if<ReviewedSourceRenderedPresentation>(&rendered);
    if(presentation == nullptr) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::PresentationFailure);
    }
    if(presentation->text.size() >
       static_cast<std::size_t>(
               std::numeric_limits<std::streamsize>::max())) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        PresentationOutputFailure);
    }

    try {
        output.write(
                presentation->text.data(),
                static_cast<std::streamsize>(presentation->text.size()));
        output.flush();
    } catch(const std::ios_base::failure&) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        PresentationOutputFailure);
    }
    if(!output) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        PresentationOutputFailure);
    }
    return ReviewedSourceAcceptanceAuthority::presented_target(
            std::move(target));
}

ReviewedSourceAcceptanceDisposition decide_reviewed_source_acceptance(
        PresentedReviewedSourceTarget target,
        const ConfirmationResult& confirmation) {
    switch(target.readiness()) {
    case ReviewedSourceReviewReadiness::Complete:
        break;
    case ReviewedSourceReviewReadiness::ManualInspectionRequired:
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        ManualInspectionRequired);
    case ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable:
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        SensitiveSourceUnrenderable);
    }

    if(const auto* accepted = std::get_if<ConfirmationAccepted>(
               &confirmation)) {
        switch(accepted->origin) {
        case ConfirmationDecisionOrigin::ExplicitToken:
            return ReviewedSourceAcceptanceAuthority::accepted_target(
                    std::move(target), accepted->origin);
        case ConfirmationDecisionOrigin::NoConfirm:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::NoConfirm);
        case ConfirmationDecisionOrigin::NonInteractiveDefault:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::
                            NonInteractiveInput);
        case ConfirmationDecisionOrigin::Default:
            return ReviewedSourceOperationStop::make(
                    ReviewedSourceOperationStopReason::
                            NonExplicitAcceptance);
        }
    }
    if(const auto* declined = std::get_if<ConfirmationDeclined>(
               &confirmation)) {
        switch(declined->origin) {
        case ConfirmationDecisionOrigin::ExplicitToken:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::
                            ExplicitReviewDecline);
        case ConfirmationDecisionOrigin::Default:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::
                            DefaultReviewDecline);
        case ConfirmationDecisionOrigin::NoConfirm:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::NoConfirm);
        case ConfirmationDecisionOrigin::NonInteractiveDefault:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::
                            NonInteractiveInput);
        }
    }
    if(const auto* cancelled = std::get_if<ConfirmationCancelled>(
               &confirmation)) {
        return ReviewedSourceOperationStop::make(
                cancelled->reason ==
                                ConfirmationCancellationReason::EndOfInput
                        ? ReviewedSourceOperationStopReason::EndOfInput
                        : ReviewedSourceOperationStopReason::
                                  ExplicitCancellation);
    }
    if(const auto* unavailable = std::get_if<ConfirmationUnavailable>(
               &confirmation)) {
        return ReviewedSourceAcceptanceAuthority::compatibility(
                target.identity(),
                unavailable->reason ==
                                ConfirmationUnavailableReason::NoConfirm
                        ? ReviewedSourceCompatibilityBuildReason::NoConfirm
                        : ReviewedSourceCompatibilityBuildReason::
                                  NonInteractiveInput);
    }
    if(std::holds_alternative<ConfirmationInputFailure>(confirmation)) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::InputFailure);
    }
    return ReviewedSourceOperationStop::make(
            ReviewedSourceOperationStopReason::NonExplicitAcceptance);
}

ReviewedSourceCompatibilityBuildWithoutReview
continue_reviewed_source_without_review(
        ReviewedSourceReviewRequirement requirement,
        ReviewedSourceReviewBypassReason reason) {
    return ReviewedSourceAcceptanceAuthority::compatibility(
            requirement.identity(), compatibility_reason(reason));
}

ReviewedSourceOperationStop
stop_after_reviewed_source_materialization_failure(
        ReviewedSourceReviewRequirement requirement,
        const ReviewedSourceReviewFailure& failure) noexcept {
    static_cast<void>(requirement);
    static_cast<void>(failure);
    return ReviewedSourceOperationStop::make(
            ReviewedSourceOperationStopReason::MaterializationFailure);
}
