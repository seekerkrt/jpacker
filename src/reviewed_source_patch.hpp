#pragma once

#include "reviewed_source_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

inline constexpr std::size_t REVIEWED_SOURCE_LOGICAL_LINE_LIMIT =
        1U * 1024U * 1024U;
inline constexpr std::size_t REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT =
        16U * 1024U * 1024U;

struct ReviewedSourceTextLine {
    std::string bytes;
    bool        has_newline = true;

    bool operator==(const ReviewedSourceTextLine&) const = default;
};

struct ReviewedSourceTextContent {
    std::vector<ReviewedSourceTextLine> lines;

    bool operator==(const ReviewedSourceTextContent&) const = default;
};

enum class ReviewedSourcePatchLineKind {
    Context,
    Removed,
    Added,
};

struct ReviewedSourcePatchLine {
    ReviewedSourcePatchLineKind kind = ReviewedSourcePatchLineKind::Context;
    ReviewedSourceTextLine      line;

    bool operator==(const ReviewedSourcePatchLine&) const = default;
};

struct ReviewedSourcePatchHunk {
    std::size_t old_start = 0;
    std::size_t old_count = 0;
    std::size_t new_start = 0;
    std::size_t new_count = 0;
    std::vector<ReviewedSourcePatchLine> lines;

    bool operator==(const ReviewedSourcePatchHunk&) const = default;
};

struct ReviewedSourceTextPatch {
    ReviewedSourceObjectId old_object_id;
    ReviewedSourceObjectId new_object_id;
    std::vector<ReviewedSourcePatchHunk> hunks;

    bool operator==(const ReviewedSourceTextPatch&) const = default;
};

// Reuses the strict parser's typed replay contract without accepting or
// reparsing raw Git patch output. This is a defense-in-depth check for a
// materialized review that has already crossed the trusted 3B1 finalizer.
[[nodiscard]] bool reviewed_source_text_patch_replays(
        const ReviewedSourceTextPatch& patch,
        const ReviewedSourceTextContent& old_content,
        const ReviewedSourceTextContent& new_content);

enum class ReviewedSourcePatchFailureReason {
    RawPatchLimitExceeded,
    LogicalLineLimitExceeded,
    MalformedPatchOutput,
    ReplayMismatch,
};

struct ReviewedSourcePatchFailure {
    ReviewedSourcePatchFailureReason reason;
    std::size_t hunk_index = 0;
    std::uintmax_t observed = 0;
    std::uintmax_t limit = 0;

    bool operator==(const ReviewedSourcePatchFailure&) const = default;
};

using ReviewedSourcePatchParseResult = std::variant<
        ReviewedSourceTextPatch,
        ReviewedSourcePatchFailure>;

// Parses the fixed, pathless blob-to-blob Git patch protocol and proves the
// parsed hunks by replaying them against old_blob. Raw patch bytes are never
// retained in the success value.
[[nodiscard]] ReviewedSourcePatchParseResult
parse_and_verify_reviewed_source_patch(
        std::string_view patch_output,
        const ReviewedSourceObjectId& old_object_id,
        const ReviewedSourceObjectId& new_object_id,
        std::string_view old_blob,
        std::string_view new_blob);
