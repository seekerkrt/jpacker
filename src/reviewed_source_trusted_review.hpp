#pragma once

#include "reviewed_source_lifecycle.hpp"
#include "reviewed_source_review.hpp"

#include <memory>

class ValidatedCachePath;

// Identity and the verified 3B review are sealed together by the trusted Git
// materialization boundary. Presentation/acceptance callers cannot attach a
// replacement PackageBase/source sidecar afterwards.
class TrustedAurReviewedSourceReview final {
public:
    TrustedAurReviewedSourceReview() = delete;
    TrustedAurReviewedSourceReview(
            const TrustedAurReviewedSourceReview&) = delete;
    TrustedAurReviewedSourceReview(
            TrustedAurReviewedSourceReview&& other) noexcept;
    TrustedAurReviewedSourceReview& operator=(
            const TrustedAurReviewedSourceReview&) = delete;
    TrustedAurReviewedSourceReview& operator=(
            TrustedAurReviewedSourceReview&& other) noexcept;
    ~TrustedAurReviewedSourceReview();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity() const;
    [[nodiscard]] const ReviewedSourceVerifiedMaterializedReview&
    verified_review() const;

private:
    // Exact non-inline trusted Git boundary. Identity is checked against the
    // checkout, object format, exact projection target, and verified 3B
    // materialization before this composite can be constructed.
    friend TrustedAurReviewedSourceReview
    materialize_aur_reviewed_source_from_trusted_git(
            const ValidatedCachePath& checkout,
            AurReviewedSourceReviewIdentity identity,
            const ReviewedSourceProjection& projection);
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
    // Test fixture derives its fixed PackageBase/source identity from the
    // verified review target. Tests cannot supply a replacement sidecar.
    friend TrustedAurReviewedSourceReview
    make_trusted_aur_reviewed_source_review_fixture_for_test(
            ReviewedSourceVerifiedMaterializedReview verified_review);
#endif

    struct State;

    TrustedAurReviewedSourceReview(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceVerifiedMaterializedReview verified_review);

    [[nodiscard]] const State& require_state() const;

    std::unique_ptr<State> state_;
};

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
// Acceptance tests need a trusted carrier without a Git process. The fixture
// has one fixed canonical AUR PackageBase and derives the exact target from the
// verified review; there is deliberately no identity argument.
[[nodiscard]] TrustedAurReviewedSourceReview
make_trusted_aur_reviewed_source_review_fixture_for_test(
        ReviewedSourceVerifiedMaterializedReview verified_review);
#endif
