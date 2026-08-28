#pragma once

#include "reviewed_source_lifecycle.hpp"
#include "reviewed_source_review.hpp"

#include <memory>
#include <optional>

class ValidatedCachePath;

// Move-only proof that one exact Slice 3A projection was observed from the
// canonical AUR source bound to this PackageBase/target/baseline identity.
// Callers may inspect the immutable projection but cannot mutate or reseal a
// copied raw ReviewedSourceProjection as trusted materialization input.
class TrustedAurReviewedSourceProjection final {
public:
    TrustedAurReviewedSourceProjection() = delete;
    TrustedAurReviewedSourceProjection(
        const TrustedAurReviewedSourceProjection&) = delete;
    TrustedAurReviewedSourceProjection(
        TrustedAurReviewedSourceProjection&& other) noexcept;
    TrustedAurReviewedSourceProjection& operator=(
        const TrustedAurReviewedSourceProjection&) = delete;
    TrustedAurReviewedSourceProjection& operator=(
        TrustedAurReviewedSourceProjection&& other) noexcept;
    ~TrustedAurReviewedSourceProjection();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity() const;
    [[nodiscard]] const std::optional<SourceRevisionIdentity>& baseline()
        const;
    [[nodiscard]] const ReviewedSourceProjection& projection() const;

private:
    // Exact non-inline Slice 3A boundary. It derives the complete projection
    // from the bound checkout/remote/target/baseline before construction.
    friend TrustedAurReviewedSourceProjection
    project_aur_reviewed_source_from_trusted_git(
        const ValidatedCachePath& checkout,
        AurReviewedSourceReviewIdentity identity,
        std::optional<SourceRevisionIdentity> baseline);

    struct State;

    TrustedAurReviewedSourceProjection(
        AurReviewedSourceReviewIdentity identity,
        std::optional<SourceRevisionIdentity> baseline,
        ReviewedSourceProjection projection);

    [[nodiscard]] const State& require_state() const;

    std::unique_ptr<State> state_;
};

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
        TrustedAurReviewedSourceProjection projection);
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
