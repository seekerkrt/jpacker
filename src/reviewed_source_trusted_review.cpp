#include "reviewed_source_trusted_review.hpp"

#include <stdexcept>
#include <utility>

struct TrustedAurReviewedSourceReview::State {
    AurReviewedSourceReviewIdentity          identity;
    ReviewedSourceVerifiedMaterializedReview verified_review;
};

TrustedAurReviewedSourceReview::TrustedAurReviewedSourceReview(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceVerifiedMaterializedReview verified_review)
    : state_(std::make_unique<State>(
              State{std::move(identity), std::move(verified_review)})) {}

TrustedAurReviewedSourceReview::TrustedAurReviewedSourceReview(
        TrustedAurReviewedSourceReview&& other) noexcept = default;

TrustedAurReviewedSourceReview& TrustedAurReviewedSourceReview::operator=(
        TrustedAurReviewedSourceReview&& other) noexcept = default;

TrustedAurReviewedSourceReview::~TrustedAurReviewedSourceReview() = default;

bool TrustedAurReviewedSourceReview::valid() const noexcept {
    return state_ != nullptr;
}

const TrustedAurReviewedSourceReview::State&
TrustedAurReviewedSourceReview::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from trusted reviewed source capability has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
TrustedAurReviewedSourceReview::identity() const {
    return require_state().identity;
}

const ReviewedSourceVerifiedMaterializedReview&
TrustedAurReviewedSourceReview::verified_review() const {
    return require_state().verified_review;
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
TrustedAurReviewedSourceReview
seal_trusted_aur_reviewed_source_review_for_test(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceVerifiedMaterializedReview verified_review) {
    return TrustedAurReviewedSourceReview(
            std::move(identity), std::move(verified_review));
}
#endif
