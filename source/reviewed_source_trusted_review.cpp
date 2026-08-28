#include "reviewed_source_trusted_review.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

const SourceRevisionIdentity& trusted_projection_target(
    const ReviewedSourceProjection& projection) {
    return std::visit(
        [](const auto& value) -> const SourceRevisionIdentity& {
            using Projection = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<
                             Projection,
                             ReviewedSourceAlreadyReviewed>) {
                return value.revision;
            } else {
                return value.target;
            }
        },
        projection);
}

bool trusted_projection_baseline_matches(
    const ReviewedSourceProjection& projection,
    const std::optional<SourceRevisionIdentity>& baseline) {
    return std::visit(
        [&baseline](const auto& value) {
            using Projection = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<
                             Projection,
                             ReviewedSourceInitialFullReview>) {
                return !baseline.has_value();
            } else if constexpr(std::is_same_v<
                                    Projection,
                                    ReviewedSourceAlreadyReviewed>) {
                return baseline.has_value() &&
                       *baseline == value.revision;
            } else if constexpr(std::is_same_v<
                                    Projection,
                                    ReviewedSourceUpdateReview>) {
                return baseline.has_value() &&
                       *baseline == value.baseline;
            } else {
                return baseline.has_value() &&
                       *baseline == value.unavailable_baseline;
            }
        },
        projection);
}

bool trusted_projection_object_formats_match(
    const ReviewedSourceProjection& projection,
    GitObjectFormat expected_format) {
    const auto version_matches =
        [expected_format](const ReviewedSourceFileVersion& value) {
            return value.object_id().format() == expected_format;
        };
    const auto change_matches =
        [&version_matches](const ReviewedSourceFileChange& change) {
            return std::visit(
                [&version_matches](const auto& value) {
                    using Change = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Change, ReviewedSourceAdded>) {
                        return version_matches(value.new_version);
                    } else if constexpr(std::is_same_v<
                                            Change,
                                            ReviewedSourceDeleted>) {
                        return version_matches(value.old_version);
                    } else {
                        return version_matches(value.old_version) &&
                               version_matches(value.new_version);
                    }
                },
                change);
        };

    return std::visit(
        [&change_matches](const auto& value) {
            using Projection = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<
                             Projection,
                             ReviewedSourceAlreadyReviewed>) {
                return true;
            } else {
                for(const ReviewedSourceFileChange& change : value.changes) {
                    if(!change_matches(change)) return false;
                }
                return true;
            }
        },
        projection);
}

} // namespace

struct TrustedAurReviewedSourceProjection::State {
    const AurReviewedSourceReviewIdentity identity;
    const std::optional<SourceRevisionIdentity> baseline;
    const ReviewedSourceProjection projection;

    State(
        AurReviewedSourceReviewIdentity value_identity,
        std::optional<SourceRevisionIdentity> value_baseline,
        ReviewedSourceProjection value_projection)
        : identity(std::move(value_identity)),
          baseline(std::move(value_baseline)),
          projection(std::move(value_projection)) {
    }
};

TrustedAurReviewedSourceProjection::TrustedAurReviewedSourceProjection(
    AurReviewedSourceReviewIdentity identity,
    std::optional<SourceRevisionIdentity> baseline,
    ReviewedSourceProjection projection) {
    if(trusted_projection_target(projection) != identity.target_revision() ||
       !trusted_projection_baseline_matches(projection, baseline) ||
       !trusted_projection_object_formats_match(
           projection, identity.git_object_format())) {
        throw std::logic_error(
            "Trusted reviewed source projection binding is inconsistent.");
    }
    state_ = std::make_unique<State>(
        std::move(identity), std::move(baseline), std::move(projection));
}

TrustedAurReviewedSourceProjection::TrustedAurReviewedSourceProjection(
    TrustedAurReviewedSourceProjection&& other) noexcept = default;

TrustedAurReviewedSourceProjection&
TrustedAurReviewedSourceProjection::operator=(
    TrustedAurReviewedSourceProjection&& other) noexcept = default;

TrustedAurReviewedSourceProjection::~TrustedAurReviewedSourceProjection() =
    default;

bool TrustedAurReviewedSourceProjection::valid() const noexcept {
    return state_ != nullptr;
}

const TrustedAurReviewedSourceProjection::State&
TrustedAurReviewedSourceProjection::require_state() const {
    if(!state_) {
        throw std::logic_error(
            "A moved-from trusted reviewed source projection has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
TrustedAurReviewedSourceProjection::identity() const {
    return require_state().identity;
}

const std::optional<SourceRevisionIdentity>&
TrustedAurReviewedSourceProjection::baseline() const {
    return require_state().baseline;
}

const ReviewedSourceProjection&
TrustedAurReviewedSourceProjection::projection() const {
    return require_state().projection;
}

struct TrustedAurReviewedSourceReview::State {
    const AurReviewedSourceReviewIdentity identity;
    const ReviewedSourceVerifiedMaterializedReview verified_review;

    State(
        AurReviewedSourceReviewIdentity value_identity,
        ReviewedSourceVerifiedMaterializedReview value_verified_review)
        : identity(std::move(value_identity)),
          verified_review(std::move(value_verified_review)) {
    }
};

TrustedAurReviewedSourceReview::TrustedAurReviewedSourceReview(
    AurReviewedSourceReviewIdentity identity,
    ReviewedSourceVerifiedMaterializedReview verified_review)
    : state_(std::make_unique<State>(
          std::move(identity), std::move(verified_review))) {
}

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
