#pragma once

#include "reviewed_source_patch.hpp"
#include "reviewed_source_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class ValidatedCachePath;
class TrustedAurReviewedSourceProjection;

inline constexpr std::size_t REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT = 4096;
inline constexpr std::uintmax_t REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT =
        8U * 1024U * 1024U;
inline constexpr std::uintmax_t
        REVIEWED_SOURCE_AGGREGATE_LINE_REVIEWABLE_BLOB_LIMIT =
                32U * 1024U * 1024U;
inline constexpr std::size_t REVIEWED_SOURCE_AGGREGATE_RAW_PATCH_LIMIT =
        32U * 1024U * 1024U;

enum class ReviewedSourceReviewResourceKind {
    ReviewEntries,
    LineReviewableBlob,
    AggregateLineReviewableBlobs,
    LogicalLine,
    SingleRawPatch,
    AggregateRawPatches,
};

enum class ReviewedSourceReviewFailureReason {
    MalformedBlobBatchOutput,
    InconsistentProjectionAndBlob,
    BlobContentHashMismatch,
    ResourceLimitExceeded,
    MalformedPatchOutput,
    InconsistentProjectionAndPatch,
};

struct ReviewedSourceReviewFailure {
    ReviewedSourceReviewFailureReason reason;
    std::optional<ReviewedSourceReviewResourceKind> resource;
    std::size_t entry_index = 0;
    std::size_t record_index = 0;
    std::size_t hunk_index = 0;
    std::uintmax_t observed = 0;
    std::uintmax_t limit = 0;

    bool operator==(const ReviewedSourceReviewFailure&) const = default;
};

using ReviewedSourceReviewResourceResult = std::variant<
        std::monostate,
        ReviewedSourceReviewFailure>;

[[nodiscard]] std::uintmax_t reviewed_source_review_resource_limit(
        ReviewedSourceReviewResourceKind resource) noexcept;

[[nodiscard]] ReviewedSourceReviewResourceResult
preflight_reviewed_source_review_resource(
        ReviewedSourceReviewResourceKind resource,
        std::uintmax_t observed);

struct ReviewedSourceBlobRequest {
    ReviewedSourceObjectId object_id;
    std::uintmax_t expected_size = 0;

    bool operator==(const ReviewedSourceBlobRequest&) const = default;
};

struct ReviewedSourceRawBlob {
    ReviewedSourceObjectId object_id;
    std::string bytes;

    bool operator==(const ReviewedSourceRawBlob&) const = default;
};

using ReviewedSourceBlobRequestPlanResult = std::variant<
        std::vector<ReviewedSourceBlobRequest>,
        ReviewedSourceReviewFailure>;

using ReviewedSourceBlobBatchParseResult = std::variant<
        std::vector<ReviewedSourceRawBlob>,
        ReviewedSourceReviewFailure>;

using ReviewedSourceBlobBatchSizeResult = std::variant<
        std::size_t,
        ReviewedSourceReviewFailure>;

[[nodiscard]] ReviewedSourceBlobRequestPlanResult
plan_reviewed_source_blob_requests(
        const ReviewedSourceProjection& projection);

[[nodiscard]] ReviewedSourceBlobBatchSizeResult
reviewed_source_blob_batch_capture_size(
        const std::vector<ReviewedSourceBlobRequest>& requests);

[[nodiscard]] ReviewedSourceBlobBatchParseResult
parse_reviewed_source_blob_batch_output(
        const std::vector<ReviewedSourceBlobRequest>& requests,
        std::string_view output);

enum class ReviewedSourceBlobContentKind {
    LineReviewable,
    ContainsNul,
    Gitlink,
};

// POLICY(#411): this observation is derived from the complete exact blob, not
// from the target-controlled Git numstat marker retained inside `change`.
// LineReviewable means line-oriented materialization is bounded and possible;
// it does not mean the bytes are safe to write directly to a terminal.
struct ReviewedSourceBlobObservation {
    ReviewedSourceBlobContentKind kind =
            ReviewedSourceBlobContentKind::ContainsNul;
    std::shared_ptr<const ReviewedSourceTextContent> text;

    bool operator==(const ReviewedSourceBlobObservation& other) const;
};

enum class ReviewedSourceReviewEmphasis {
    Ordinary,
    Sensitive,
};

enum class ReviewedSourceReviewRepresentation {
    CompleteTextPatch,
    CompleteFullText,
    NoContentChange,
    ContainsNul,
    GitlinkMetadata,
    MixedTextAndNonText,
};

enum class ReviewedSourceReviewReadiness {
    Complete,
    ManualInspectionRequired,
    SensitiveSourceUnrenderable,
};

struct ReviewedSourceReviewEntry {
    // Slice 3A remains the authority for status, raw paths, modes, OIDs, sizes,
    // rename similarity, file classification, and the advisory Git marker.
    ReviewedSourceFileChange change;
    ReviewedSourceReviewEmphasis emphasis =
            ReviewedSourceReviewEmphasis::Ordinary;
    std::optional<ReviewedSourceBlobObservation> old_observation;
    std::optional<ReviewedSourceBlobObservation> new_observation;
    ReviewedSourceReviewRepresentation representation =
            ReviewedSourceReviewRepresentation::NoContentChange;
    ReviewedSourceReviewReadiness readiness =
            ReviewedSourceReviewReadiness::Complete;
    std::optional<ReviewedSourceTextPatch> patch;

    bool operator==(const ReviewedSourceReviewEntry&) const = default;
};

struct ReviewedSourcePreparedBlob {
    ReviewedSourceRawBlob raw;
    ReviewedSourceBlobObservation observation;
};

struct ReviewedSourcePatchRequest {
    std::size_t entry_index = 0;
    std::size_t old_blob_index = 0;
    std::size_t new_blob_index = 0;
    ReviewedSourceObjectId old_object_id;
    ReviewedSourceObjectId new_object_id;

    bool operator==(const ReviewedSourcePatchRequest&) const = default;
};

struct ReviewedSourceReviewPreparation {
    ReviewedSourceProjection projection;
    std::vector<ReviewedSourcePreparedBlob> blobs;
    std::vector<ReviewedSourceReviewEntry> entries;
    std::vector<ReviewedSourcePatchRequest> patch_requests;
};

struct ReviewedSourceRawPatch {
    std::size_t entry_index = 0;
    ReviewedSourceObjectId old_object_id;
    ReviewedSourceObjectId new_object_id;
    std::string output;
};

using ReviewedSourceReviewPreparationResult = std::variant<
        ReviewedSourceReviewPreparation,
        ReviewedSourceReviewFailure>;

struct ReviewedSourceReviewBody {
    ReviewedSourceReviewReadiness readiness =
            ReviewedSourceReviewReadiness::Complete;
    std::vector<ReviewedSourceReviewEntry> entries;

    bool operator==(const ReviewedSourceReviewBody&) const = default;
};

struct ReviewedSourceMaterializedInitialFullReview {
    SourceRevisionIdentity target;
    ReviewedSourceReviewBody review;

    bool operator==(const ReviewedSourceMaterializedInitialFullReview&) const =
            default;
};

struct ReviewedSourceMaterializedAlreadyReviewed {
    SourceRevisionIdentity revision;

    bool operator==(const ReviewedSourceMaterializedAlreadyReviewed&) const =
            default;
};

struct ReviewedSourceMaterializedUpdateReview {
    SourceRevisionIdentity baseline;
    SourceRevisionIdentity target;
    ReviewedSourceHistoryRelation relation;
    ReviewedSourceReviewBody review;

    bool operator==(const ReviewedSourceMaterializedUpdateReview&) const =
            default;
};

struct ReviewedSourceMaterializedRebaselineFullReview {
    SourceRevisionIdentity unavailable_baseline;
    SourceRevisionIdentity target;
    ReviewedSourceBaselineUnavailableReason reason;
    ReviewedSourceReviewBody review;

    bool operator==(
            const ReviewedSourceMaterializedRebaselineFullReview&) const =
            default;
};

using ReviewedSourceMaterializedReview = std::variant<
        ReviewedSourceMaterializedInitialFullReview,
        ReviewedSourceMaterializedAlreadyReviewed,
        ReviewedSourceMaterializedUpdateReview,
        ReviewedSourceMaterializedRebaselineFullReview>;

// Capability proving that the materialized model crossed the trusted Git 3B1
// content-address, strict patch replay, and readiness finalization path. The
// contained model is immutable through this interface and cannot be sealed by
// ordinary production callers.
class ReviewedSourceVerifiedMaterializedReview final {
public:
    ReviewedSourceVerifiedMaterializedReview(
            const ReviewedSourceVerifiedMaterializedReview&) = default;
    ReviewedSourceVerifiedMaterializedReview(
            ReviewedSourceVerifiedMaterializedReview&&) noexcept = default;
    ReviewedSourceVerifiedMaterializedReview& operator=(
            const ReviewedSourceVerifiedMaterializedReview&) = default;
    ReviewedSourceVerifiedMaterializedReview& operator=(
            ReviewedSourceVerifiedMaterializedReview&&) noexcept = default;
    ~ReviewedSourceVerifiedMaterializedReview() = default;

    [[nodiscard]] const ReviewedSourceMaterializedReview& review()
            const noexcept;

    bool operator==(
            const ReviewedSourceVerifiedMaterializedReview&) const = default;

private:
    // Exact non-inline trusted Git boundary. It consumes a sealed Slice 3A
    // projection, performs materialization, and only then constructs this
    // capability.
    friend ReviewedSourceVerifiedMaterializedReview
    materialize_verified_review_from_trusted_git(
            const ValidatedCachePath& checkout,
            TrustedAurReviewedSourceProjection projection);
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
    friend ReviewedSourceVerifiedMaterializedReview
    seal_reviewed_source_materialized_review_for_test(
            ReviewedSourceMaterializedReview review);
#endif

    explicit ReviewedSourceVerifiedMaterializedReview(
            ReviewedSourceMaterializedReview review) noexcept;

    ReviewedSourceMaterializedReview review_;
};

// Returns the first inconsistent entry. A zero index is also used for a
// lifecycle/body invariant that is not attributable to one existing entry.
[[nodiscard]] std::optional<std::size_t>
reviewed_source_materialized_review_inconsistent_entry(
        const ReviewedSourceMaterializedReview& review);

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
// Test-only seam for exercising the renderer's defense-in-depth rejection.
// Production builds have no general sealing function.
[[nodiscard]] ReviewedSourceVerifiedMaterializedReview
seal_reviewed_source_materialized_review_for_test(
        ReviewedSourceMaterializedReview review);
#endif

using ReviewedSourceReviewFinalizationResult = std::variant<
        ReviewedSourceMaterializedReview,
        ReviewedSourceReviewFailure>;

[[nodiscard]] ReviewedSourceReviewEmphasis reviewed_source_review_emphasis(
        const ReviewedSourcePath& path) noexcept;

[[nodiscard]] ReviewedSourceReviewPreparationResult
prepare_reviewed_source_review(
        const ReviewedSourceProjection& projection,
        std::vector<ReviewedSourceRawBlob> blobs);

[[nodiscard]] ReviewedSourceReviewFinalizationResult
finalize_reviewed_source_review(
        ReviewedSourceReviewPreparation preparation,
        std::vector<ReviewedSourceRawPatch> patches);
