#include "reviewed_source_acceptance.hpp"

#include <ios>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <stdexcept>
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

std::optional<ReviewedSourceOperationStop> acceptance_precondition_stop(
        const PresentedReviewedSourceTarget& target) {
    if(!target.valid()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::InvalidCapability);
    }
    switch(target.readiness()) {
    case ReviewedSourceReviewReadiness::Complete:
        return std::nullopt;
    case ReviewedSourceReviewReadiness::ManualInspectionRequired:
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        ManualInspectionRequired);
    case ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable:
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::
                        SensitiveSourceUnrenderable);
    }
    return ReviewedSourceOperationStop::make(
            ReviewedSourceOperationStopReason::InvalidCapability);
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
            ExplicitConfirmationAcceptance confirmation) {
        return AcceptedReviewedSourceTarget(
                std::move(target), std::move(confirmation));
    }

    static ReviewedSourceCompatibilityBuildWithoutReview compatibility(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceCompatibilityBuildReason reason) {
        return ReviewedSourceCompatibilityBuildWithoutReview(
                std::move(identity), reason);
    }
};

struct ReviewedSourceVerifiedLifecycleTarget::State {
    ReviewedSourceReviewRequirement          requirement;
    ReviewedSourceIntegrationLifecycle       lifecycle;
    ReviewedSourceVerifiedMaterializedReview verified_review;
    ReviewedSourceReviewReadiness            readiness;
};

struct PresentedReviewedSourceTarget::State {
    ReviewedSourceVerifiedLifecycleTarget target;
};

struct AcceptedReviewedSourceTarget::State {
    PresentedReviewedSourceTarget    target;
    ExplicitConfirmationAcceptance confirmation;
};

ReviewedSourceVerifiedLifecycleTarget::
        ReviewedSourceVerifiedLifecycleTarget(
                ReviewedSourceReviewRequirement requirement,
                ReviewedSourceIntegrationLifecycle lifecycle,
                ReviewedSourceVerifiedMaterializedReview verified_review,
                ReviewedSourceReviewReadiness readiness)
    : state_(std::make_unique<State>(State{
              std::move(requirement), std::move(lifecycle),
              std::move(verified_review), readiness})) {}

ReviewedSourceVerifiedLifecycleTarget::
        ReviewedSourceVerifiedLifecycleTarget(
                ReviewedSourceVerifiedLifecycleTarget&& other) noexcept =
                default;

ReviewedSourceVerifiedLifecycleTarget&
ReviewedSourceVerifiedLifecycleTarget::operator=(
        ReviewedSourceVerifiedLifecycleTarget&& other) noexcept = default;

ReviewedSourceVerifiedLifecycleTarget::~ReviewedSourceVerifiedLifecycleTarget() =
        default;

bool ReviewedSourceVerifiedLifecycleTarget::valid() const noexcept {
    return state_ != nullptr;
}

const ReviewedSourceVerifiedLifecycleTarget::State&
ReviewedSourceVerifiedLifecycleTarget::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from verified reviewed source capability has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
ReviewedSourceVerifiedLifecycleTarget::identity() const {
    return require_state().requirement.identity();
}

const ReviewedSourceIntegrationLifecycle&
ReviewedSourceVerifiedLifecycleTarget::lifecycle() const {
    return require_state().lifecycle;
}

const ReviewedSourceVerifiedMaterializedReview&
ReviewedSourceVerifiedLifecycleTarget::verified_review() const {
    return require_state().verified_review;
}

ReviewedSourceReviewReadiness
ReviewedSourceVerifiedLifecycleTarget::readiness() const {
    return require_state().readiness;
}

const ReviewedSourceExpectedStateObservation&
ReviewedSourceVerifiedLifecycleTarget::expected_state_observation() const {
    return require_state().requirement.expected_state_observation();
}

PresentedReviewedSourceTarget::PresentedReviewedSourceTarget(
        ReviewedSourceVerifiedLifecycleTarget target) {
    if(!target.valid()) {
        throw std::logic_error(
                "Cannot present an invalid reviewed source capability.");
    }
    state_ = std::make_unique<State>(State{std::move(target)});
}

PresentedReviewedSourceTarget::PresentedReviewedSourceTarget(
        PresentedReviewedSourceTarget&& other) noexcept = default;

PresentedReviewedSourceTarget& PresentedReviewedSourceTarget::operator=(
        PresentedReviewedSourceTarget&& other) noexcept = default;

PresentedReviewedSourceTarget::~PresentedReviewedSourceTarget() = default;

bool PresentedReviewedSourceTarget::valid() const noexcept {
    return state_ != nullptr;
}

const PresentedReviewedSourceTarget::State&
PresentedReviewedSourceTarget::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from presented reviewed source capability has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
PresentedReviewedSourceTarget::identity() const {
    return require_state().target.identity();
}

const ReviewedSourceIntegrationLifecycle&
PresentedReviewedSourceTarget::lifecycle() const {
    return require_state().target.lifecycle();
}

const ReviewedSourceVerifiedMaterializedReview&
PresentedReviewedSourceTarget::verified_review() const {
    return require_state().target.verified_review();
}

ReviewedSourceReviewReadiness
PresentedReviewedSourceTarget::readiness() const {
    return require_state().target.readiness();
}

const ReviewedSourceExpectedStateObservation&
PresentedReviewedSourceTarget::expected_state_observation() const {
    return require_state().target.expected_state_observation();
}

AcceptedReviewedSourceTarget::AcceptedReviewedSourceTarget(
        PresentedReviewedSourceTarget target,
        ExplicitConfirmationAcceptance confirmation) {
    if(!target.valid() || !confirmation.valid()) {
        throw std::logic_error(
                "Cannot accept an invalid reviewed source capability.");
    }
    state_ = std::make_unique<State>(
            State{std::move(target), std::move(confirmation)});
}

AcceptedReviewedSourceTarget::AcceptedReviewedSourceTarget(
        AcceptedReviewedSourceTarget&& other) noexcept = default;

AcceptedReviewedSourceTarget& AcceptedReviewedSourceTarget::operator=(
        AcceptedReviewedSourceTarget&& other) noexcept = default;

AcceptedReviewedSourceTarget::~AcceptedReviewedSourceTarget() = default;

bool AcceptedReviewedSourceTarget::valid() const noexcept {
    return state_ != nullptr;
}

const AcceptedReviewedSourceTarget::State&
AcceptedReviewedSourceTarget::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from accepted reviewed source capability has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
AcceptedReviewedSourceTarget::identity() const {
    return require_state().target.identity();
}

const SourceRevisionIdentity&
AcceptedReviewedSourceTarget::reviewed_upstream_base_revision() const {
    return identity().target_revision();
}

const ReviewedSourceIntegrationLifecycle&
AcceptedReviewedSourceTarget::lifecycle() const {
    return require_state().target.lifecycle();
}

const ReviewedSourceVerifiedMaterializedReview&
AcceptedReviewedSourceTarget::verified_review() const {
    return require_state().target.verified_review();
}

ReviewedSourceReviewReadiness
AcceptedReviewedSourceTarget::readiness() const {
    return require_state().target.readiness();
}

const ReviewedSourceExpectedStateObservation&
AcceptedReviewedSourceTarget::expected_state_observation() const {
    return require_state().target.expected_state_observation();
}

ConfirmationDecisionOrigin
AcceptedReviewedSourceTarget::confirmation_origin() const {
    const State& state = require_state();
    if(!state.confirmation.valid()) {
        throw std::logic_error(
                "Accepted reviewed source lost explicit confirmation provenance.");
    }
    return ConfirmationDecisionOrigin::ExplicitToken;
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
        TrustedAurReviewedSourceReview trusted_review) {
    if(!trusted_review.valid()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::InvalidCapability);
    }
    const AurReviewedSourceReviewIdentity& verified_identity =
            trusted_review.identity();
    if(requirement.identity() != verified_identity) {
        return identity_mismatch(requirement.identity(), verified_identity);
    }

    ReviewedSourceVerifiedMaterializedReview verified_review =
            trusted_review.verified_review();
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
    if(!target.valid()) {
        return ReviewedSourceOperationStop::make(
                ReviewedSourceOperationStopReason::InvalidCapability);
    }
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
    } catch(const std::bad_alloc&) {
        // Resource exhaustion remains the process/CLI exception boundary's
        // responsibility; ordinary/custom stream failures are normalized here.
        throw;
    } catch(...) {
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
        ExplicitConfirmationResult confirmation) {
    if(auto stop = acceptance_precondition_stop(target)) {
        return std::move(*stop);
    }

    if(auto* accepted = std::get_if<ExplicitConfirmationAcceptance>(
               &confirmation)) {
        if(!accepted->valid()) {
            return ReviewedSourceOperationStop::make(
                    ReviewedSourceOperationStopReason::InvalidCapability);
        }
        return ReviewedSourceAcceptanceAuthority::accepted_target(
                std::move(target), std::move(*accepted));
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

ReviewedSourceAcceptanceDisposition decide_reviewed_source_unsealed_confirmation(
        PresentedReviewedSourceTarget target,
        const ConfirmationResult& confirmation) {
    if(auto stop = acceptance_precondition_stop(target)) {
        return std::move(*stop);
    }

    if(const auto* accepted = std::get_if<ConfirmationAccepted>(
               &confirmation)) {
        switch(accepted->origin) {
        case ConfirmationDecisionOrigin::NoConfirm:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::NoConfirm);
        case ConfirmationDecisionOrigin::NonInteractiveDefault:
            return ReviewedSourceAcceptanceAuthority::compatibility(
                    target.identity(),
                    ReviewedSourceCompatibilityBuildReason::
                            NonInteractiveInput);
        case ConfirmationDecisionOrigin::ExplicitToken:
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
