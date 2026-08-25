#pragma once

#include "reviewed_source_review.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

inline constexpr std::uintmax_t REVIEWED_SOURCE_RENDERED_OUTPUT_LIMIT =
        32U * 1024U * 1024U;

enum class ReviewedSourcePresentationFailureReason {
    RenderedOutputLimitExceeded,
    InconsistentMaterializedReview,
};

// Presentation failures retain only bounded structural metadata. Raw paths,
// content bytes, patch bytes, and machine output never cross this boundary.
struct ReviewedSourcePresentationFailure {
    ReviewedSourcePresentationFailureReason reason;
    std::size_t entry_index = 0;
    std::uintmax_t observed = 0;
    std::uintmax_t limit = 0;

    bool operator==(const ReviewedSourcePresentationFailure&) const = default;
};

struct ReviewedSourceRenderedPresentation {
    std::string text;

    bool operator==(const ReviewedSourceRenderedPresentation&) const = default;
};

using ReviewedSourcePresentationResult = std::variant<
        ReviewedSourceRenderedPresentation,
        ReviewedSourcePresentationFailure>;

// Renders only the verified Slice 3B1 materialized model. The success text is
// terminal-safe and all-or-nothing; it never contains raw Git patch/blob/path
// bytes or terminal control bytes from reviewed content.
[[nodiscard]] ReviewedSourcePresentationResult
render_reviewed_source_presentation(
        const ReviewedSourceVerifiedMaterializedReview& review);

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
using ReviewedSourcePresentationSizeCheckResult = std::variant<
        std::uintmax_t,
        ReviewedSourcePresentationFailure>;

[[nodiscard]] ReviewedSourcePresentationSizeCheckResult
checked_reviewed_source_presentation_size_for_test(
        std::uintmax_t current,
        std::uintmax_t additional,
        std::uintmax_t limit);

[[nodiscard]] ReviewedSourcePresentationResult
render_reviewed_source_presentation_with_limit_for_test(
        const ReviewedSourceVerifiedMaterializedReview& review,
        std::uintmax_t limit);
#endif
