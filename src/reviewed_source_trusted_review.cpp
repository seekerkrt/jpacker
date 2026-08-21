#include "reviewed_source_trusted_review.hpp"

#include <stdexcept>
#include <type_traits>
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
namespace {

const SourceRevisionIdentity& trusted_review_fixture_target(
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

} // namespace

TrustedAurReviewedSourceReview
make_trusted_aur_reviewed_source_review_fixture_for_test(
        ReviewedSourceVerifiedMaterializedReview verified_review) {
    const SourceRevisionIdentity target = trusted_review_fixture_target(
            verified_review.review());
    AurReviewedSourceReviewIdentity identity =
            AurReviewedSourceReviewIdentity::make(
                    PackageBaseIdentity::make(
                            PackageSourceIdentity::aur(
                                    SourceLocationIdentity::known_git_remote(
                                            "https://aur.archlinux.org/example-base.git")),
                            "example-base"),
                    target);
    return TrustedAurReviewedSourceReview(
            std::move(identity), std::move(verified_review));
}
#endif
