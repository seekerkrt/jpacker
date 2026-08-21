#pragma once

#include "reviewed_source_lifecycle.hpp"
#include "reviewed_source_review.hpp"

#include <memory>

class ReviewedSourceTrustedMaterializationAuthority;

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
    friend class ReviewedSourceTrustedMaterializationAuthority;
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_ACCEPTANCE_TEST_HOOKS
    friend TrustedAurReviewedSourceReview
    seal_trusted_aur_reviewed_source_review_for_test(
            AurReviewedSourceReviewIdentity identity,
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
[[nodiscard]] TrustedAurReviewedSourceReview
seal_trusted_aur_reviewed_source_review_for_test(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceVerifiedMaterializedReview verified_review);
#endif
