#include "reviewed_source_review.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

ReviewedSourceReviewFailure review_failure(
        ReviewedSourceReviewFailureReason reason,
        std::size_t entry_index = 0,
        std::size_t record_index = 0) {
    return ReviewedSourceReviewFailure{
            reason, std::nullopt, entry_index, record_index};
}

ReviewedSourceReviewFailure resource_failure(
        ReviewedSourceReviewResourceKind resource,
        std::uintmax_t observed,
        std::uintmax_t limit,
        std::size_t entry_index = 0) {
    return ReviewedSourceReviewFailure{
            ReviewedSourceReviewFailureReason::ResourceLimitExceeded,
            resource, entry_index, 0, 0, observed, limit};
}

const std::vector<ReviewedSourceFileChange>* projection_changes(
        const ReviewedSourceProjection& projection) {
    return std::visit(
            [](const auto& value) -> const std::vector<ReviewedSourceFileChange>* {
                using Value = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Value, ReviewedSourceAlreadyReviewed>) {
                    return nullptr;
                } else {
                    return &value.changes;
                }
            },
            projection);
}

const ReviewedSourceFileVersion* old_version(
        const ReviewedSourceFileChange& change) {
    return std::visit(
            [](const auto& value) -> const ReviewedSourceFileVersion* {
                using Value = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Value, ReviewedSourceAdded>) {
                    return nullptr;
                } else {
                    return &value.old_version;
                }
            },
            change);
}

const ReviewedSourceFileVersion* new_version(
        const ReviewedSourceFileChange& change) {
    return std::visit(
            [](const auto& value) -> const ReviewedSourceFileVersion* {
                using Value = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Value, ReviewedSourceDeleted>) {
                    return nullptr;
                } else {
                    return &value.new_version;
                }
            },
            change);
}

bool add_without_overflow(
        std::uintmax_t value,
        std::uintmax_t& aggregate,
        std::uintmax_t limit) {
    if(value > limit - aggregate) return false;
    aggregate += value;
    return true;
}

std::variant<ReviewedSourceTextContent, ReviewedSourceReviewFailure>
split_reviewable_blob(std::string_view blob, std::size_t entry_index) {
    ReviewedSourceTextContent content;
    std::size_t offset = 0;
    while(offset < blob.size()) {
        const std::size_t end = blob.find('\n', offset);
        const bool has_newline = end != std::string_view::npos;
        const std::size_t line_end = has_newline ? end : blob.size();
        const std::size_t line_size = line_end - offset;
        if(line_size > REVIEWED_SOURCE_LOGICAL_LINE_LIMIT) {
            return resource_failure(
                    ReviewedSourceReviewResourceKind::LogicalLine,
                    line_size, REVIEWED_SOURCE_LOGICAL_LINE_LIMIT,
                    entry_index);
        }
        content.lines.push_back(ReviewedSourceTextLine{
                std::string(blob.substr(offset, line_size)), has_newline});
        if(!has_newline) break;
        offset = end + 1;
    }
    return content;
}

bool path_is_sensitive(const ReviewedSourcePath& path) noexcept {
    const std::string& raw = path.raw_bytes();
    constexpr std::string_view install_suffix = ".install";
    return raw == "PKGBUILD" ||
           (raw.find('/') == std::string::npos &&
            raw.size() > install_suffix.size() &&
            raw.ends_with(install_suffix));
}

bool version_is_sensitive_unrenderable(
        const ReviewedSourceFileVersion* version,
        const std::optional<ReviewedSourceBlobObservation>& observation) {
    if(version == nullptr || !path_is_sensitive(version->path())) return false;
    if(!observation.has_value() ||
       observation->kind != ReviewedSourceBlobContentKind::LineReviewable) {
        return true;
    }
    return version->mode() != ReviewedSourceFileMode::Regular &&
           version->mode() != ReviewedSourceFileMode::Executable;
}

ReviewedSourceReviewReadiness aggregate_readiness(
        const std::vector<ReviewedSourceReviewEntry>& entries) noexcept {
    ReviewedSourceReviewReadiness result =
            ReviewedSourceReviewReadiness::Complete;
    for(const ReviewedSourceReviewEntry& entry : entries) {
        if(entry.readiness ==
           ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable) {
            return entry.readiness;
        }
        if(entry.readiness ==
           ReviewedSourceReviewReadiness::ManualInspectionRequired) {
            result = entry.readiness;
        }
    }
    return result;
}

ReviewedSourceReviewRepresentation one_sided_representation(
        ReviewedSourceBlobContentKind kind) noexcept {
    switch(kind) {
    case ReviewedSourceBlobContentKind::LineReviewable:
        return ReviewedSourceReviewRepresentation::CompleteFullText;
    case ReviewedSourceBlobContentKind::ContainsNul:
        return ReviewedSourceReviewRepresentation::ContainsNul;
    case ReviewedSourceBlobContentKind::Gitlink:
        return ReviewedSourceReviewRepresentation::GitlinkMetadata;
    }
    return ReviewedSourceReviewRepresentation::MixedTextAndNonText;
}

bool representation_requires_manual_inspection(
        ReviewedSourceReviewRepresentation representation) noexcept {
    return representation == ReviewedSourceReviewRepresentation::ContainsNul ||
           representation ==
                   ReviewedSourceReviewRepresentation::GitlinkMetadata ||
           representation ==
                   ReviewedSourceReviewRepresentation::MixedTextAndNonText;
}

ReviewedSourceReviewFailure map_patch_failure(
        const ReviewedSourcePatchFailure& failure,
        std::size_t entry_index) {
    switch(failure.reason) {
    case ReviewedSourcePatchFailureReason::RawPatchLimitExceeded:
        return resource_failure(
                ReviewedSourceReviewResourceKind::SingleRawPatch,
                failure.observed, failure.limit, entry_index);
    case ReviewedSourcePatchFailureReason::LogicalLineLimitExceeded:
        return resource_failure(
                ReviewedSourceReviewResourceKind::LogicalLine,
                failure.observed, failure.limit, entry_index);
    case ReviewedSourcePatchFailureReason::MalformedPatchOutput: {
        ReviewedSourceReviewFailure result = review_failure(
                ReviewedSourceReviewFailureReason::MalformedPatchOutput,
                entry_index);
        result.hunk_index = failure.hunk_index;
        return result;
    }
    case ReviewedSourcePatchFailureReason::ReplayMismatch: {
        ReviewedSourceReviewFailure result = review_failure(
                ReviewedSourceReviewFailureReason::
                        InconsistentProjectionAndPatch,
                entry_index);
        result.hunk_index = failure.hunk_index;
        return result;
    }
    }
    return review_failure(
            ReviewedSourceReviewFailureReason::MalformedPatchOutput,
            entry_index);
}

} // namespace

std::uintmax_t reviewed_source_review_resource_limit(
        ReviewedSourceReviewResourceKind resource) noexcept {
    switch(resource) {
    case ReviewedSourceReviewResourceKind::ReviewEntries:
        return REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT;
    case ReviewedSourceReviewResourceKind::LineReviewableBlob:
        return REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT;
    case ReviewedSourceReviewResourceKind::AggregateLineReviewableBlobs:
        return REVIEWED_SOURCE_AGGREGATE_LINE_REVIEWABLE_BLOB_LIMIT;
    case ReviewedSourceReviewResourceKind::LogicalLine:
        return REVIEWED_SOURCE_LOGICAL_LINE_LIMIT;
    case ReviewedSourceReviewResourceKind::SingleRawPatch:
        return REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT;
    case ReviewedSourceReviewResourceKind::AggregateRawPatches:
        return REVIEWED_SOURCE_AGGREGATE_RAW_PATCH_LIMIT;
    }
    return 0;
}

ReviewedSourceReviewResourceResult preflight_reviewed_source_review_resource(
        ReviewedSourceReviewResourceKind resource,
        std::uintmax_t observed) {
    const std::uintmax_t limit = reviewed_source_review_resource_limit(resource);
    if(observed > limit) {
        return resource_failure(resource, observed, limit);
    }
    return std::monostate{};
}

ReviewedSourceBlobRequestPlanResult plan_reviewed_source_blob_requests(
        const ReviewedSourceProjection& projection) {
    const std::vector<ReviewedSourceFileChange>* changes =
            projection_changes(projection);
    if(changes == nullptr) return std::vector<ReviewedSourceBlobRequest>{};
    if(changes->size() > REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT) {
        return resource_failure(
                ReviewedSourceReviewResourceKind::ReviewEntries,
                changes->size(), REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT);
    }

    std::map<std::string, ReviewedSourceBlobRequest> requests;
    const auto add_version = [&](const ReviewedSourceFileVersion* version)
            -> std::optional<ReviewedSourceReviewFailure> {
        if(version == nullptr ||
           version->mode() == ReviewedSourceFileMode::Gitlink) {
            return std::nullopt;
        }
        if(!version->blob_size().has_value()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob);
        }
        const std::string& oid = version->object_id().value();
        const auto [position, inserted] = requests.emplace(
                oid,
                ReviewedSourceBlobRequest{
                        version->object_id(), *version->blob_size()});
        if(!inserted &&
           position->second.expected_size != *version->blob_size()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob);
        }
        return std::nullopt;
    };

    for(std::size_t index = 0; index < changes->size(); ++index) {
        if(const auto failure = add_version(old_version((*changes)[index]))) {
            ReviewedSourceReviewFailure result = *failure;
            result.entry_index = index;
            return result;
        }
        if(const auto failure = add_version(new_version((*changes)[index]))) {
            ReviewedSourceReviewFailure result = *failure;
            result.entry_index = index;
            return result;
        }
    }

    std::vector<ReviewedSourceBlobRequest> result;
    result.reserve(requests.size());
    for(auto& [oid, request] : requests) {
        static_cast<void>(oid);
        result.push_back(std::move(request));
    }
    return result;
}

ReviewedSourceBlobBatchSizeResult reviewed_source_blob_batch_capture_size(
        const std::vector<ReviewedSourceBlobRequest>& requests) {
    std::size_t total = 0;
    for(std::size_t index = 0; index < requests.size(); ++index) {
        const ReviewedSourceBlobRequest& request = requests[index];
        if(request.expected_size > std::numeric_limits<std::size_t>::max()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
                    0, index);
        }
        const std::size_t payload_size =
                static_cast<std::size_t>(request.expected_size);
        const std::size_t framing_size = request.object_id.value().size() +
                std::string_view(" blob ").size() +
                std::to_string(request.expected_size).size() + 2;
        if(payload_size > std::numeric_limits<std::size_t>::max() - framing_size ||
           payload_size + framing_size >
                   std::numeric_limits<std::size_t>::max() - total) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
                    0, index);
        }
        total += payload_size + framing_size;
    }
    return total;
}

ReviewedSourceBlobBatchParseResult parse_reviewed_source_blob_batch_output(
        const std::vector<ReviewedSourceBlobRequest>& requests,
        std::string_view output) {
    std::vector<ReviewedSourceRawBlob> blobs;
    blobs.reserve(requests.size());
    std::size_t offset = 0;
    for(std::size_t index = 0; index < requests.size(); ++index) {
        const ReviewedSourceBlobRequest& request = requests[index];
        const std::size_t header_end = output.find('\0', offset);
        const std::string expected_header =
                request.object_id.value() + " blob " +
                std::to_string(request.expected_size);
        if(header_end == std::string_view::npos) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            MalformedBlobBatchOutput,
                    0, index);
        }
        const std::string_view observed_header =
                output.substr(offset, header_end - offset);
        if(observed_header != expected_header) {
            const std::string expected_prefix =
                    request.object_id.value() + " ";
            return review_failure(
                    observed_header.starts_with(expected_prefix)
                            ? ReviewedSourceReviewFailureReason::
                                      InconsistentProjectionAndBlob
                            : ReviewedSourceReviewFailureReason::
                                      MalformedBlobBatchOutput,
                    0, index);
        }
        if(request.expected_size > std::numeric_limits<std::size_t>::max()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
                    0, index);
        }
        offset = header_end + 1;
        const std::size_t payload_size =
                static_cast<std::size_t>(request.expected_size);
        if(payload_size > output.size() - offset ||
           output.size() - offset - payload_size < 1 ||
           output[offset + payload_size] != '\0') {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            MalformedBlobBatchOutput,
                    0, index);
        }
        blobs.push_back(ReviewedSourceRawBlob{
                request.object_id,
                std::string(output.substr(offset, payload_size))});
        offset += payload_size + 1;
    }
    if(offset != output.size()) {
        return review_failure(
                ReviewedSourceReviewFailureReason::MalformedBlobBatchOutput,
                0, requests.size());
    }
    return blobs;
}

bool ReviewedSourceBlobObservation::operator==(
        const ReviewedSourceBlobObservation& other) const {
    if(kind != other.kind || static_cast<bool>(text) !=
                                      static_cast<bool>(other.text)) {
        return false;
    }
    return !text || *text == *other.text;
}

ReviewedSourceReviewEmphasis reviewed_source_review_emphasis(
        const ReviewedSourcePath& path) noexcept {
    return path_is_sensitive(path)
            ? ReviewedSourceReviewEmphasis::Sensitive
            : ReviewedSourceReviewEmphasis::Ordinary;
}

ReviewedSourceReviewPreparationResult prepare_reviewed_source_review(
        const ReviewedSourceProjection& projection,
        std::vector<ReviewedSourceRawBlob> blobs) {
    ReviewedSourceBlobRequestPlanResult planned =
            plan_reviewed_source_blob_requests(projection);
    if(std::holds_alternative<ReviewedSourceReviewFailure>(planned)) {
        return std::get<ReviewedSourceReviewFailure>(planned);
    }
    const auto& requests = std::get<std::vector<ReviewedSourceBlobRequest>>(
            planned);
    if(requests.size() != blobs.size()) {
        return review_failure(
                ReviewedSourceReviewFailureReason::
                        InconsistentProjectionAndBlob);
    }

    ReviewedSourceReviewPreparation preparation{
            projection, {}, {}, {}};
    preparation.blobs.reserve(blobs.size());
    std::map<std::string, std::size_t> blob_indices;
    std::uintmax_t aggregate_reviewable_size = 0;
    for(std::size_t index = 0; index < requests.size(); ++index) {
        ReviewedSourceRawBlob& blob = blobs[index];
        const ReviewedSourceBlobRequest& request = requests[index];
        if(blob.object_id != request.object_id ||
           blob.bytes.size() != request.expected_size) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
                    0, index);
        }

        ReviewedSourceBlobObservation observation;
        if(blob.bytes.find('\0') != std::string::npos) {
            observation.kind = ReviewedSourceBlobContentKind::ContainsNul;
            blob.bytes.clear();
        } else {
            if(blob.bytes.size() >
               REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT) {
                return resource_failure(
                        ReviewedSourceReviewResourceKind::LineReviewableBlob,
                        blob.bytes.size(),
                        REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT);
            }
            if(!add_without_overflow(
                       blob.bytes.size(), aggregate_reviewable_size,
                       REVIEWED_SOURCE_AGGREGATE_LINE_REVIEWABLE_BLOB_LIMIT)) {
                const std::uintmax_t observed = aggregate_reviewable_size +
                        static_cast<std::uintmax_t>(blob.bytes.size());
                return resource_failure(
                        ReviewedSourceReviewResourceKind::
                                AggregateLineReviewableBlobs,
                        observed,
                        REVIEWED_SOURCE_AGGREGATE_LINE_REVIEWABLE_BLOB_LIMIT);
            }
            auto split = split_reviewable_blob(blob.bytes, 0);
            if(std::holds_alternative<ReviewedSourceReviewFailure>(split)) {
                return std::get<ReviewedSourceReviewFailure>(split);
            }
            observation.kind = ReviewedSourceBlobContentKind::LineReviewable;
            observation.text = std::make_shared<const ReviewedSourceTextContent>(
                    std::get<ReviewedSourceTextContent>(std::move(split)));
        }
        blob_indices.emplace(blob.object_id.value(), index);
        preparation.blobs.push_back(ReviewedSourcePreparedBlob{
                std::move(blob), std::move(observation)});
    }

    const std::vector<ReviewedSourceFileChange>* changes =
            projection_changes(projection);
    if(changes == nullptr) return preparation;
    preparation.entries.reserve(changes->size());

    for(std::size_t entry_index = 0;
        entry_index < changes->size(); ++entry_index) {
        const ReviewedSourceFileChange& change = (*changes)[entry_index];
        const ReviewedSourceFileVersion* old_file = old_version(change);
        const ReviewedSourceFileVersion* new_file = new_version(change);

        const auto observe = [&](const ReviewedSourceFileVersion* version)
                -> std::optional<ReviewedSourceBlobObservation> {
            if(version == nullptr) return std::nullopt;
            if(version->mode() == ReviewedSourceFileMode::Gitlink) {
                return ReviewedSourceBlobObservation{
                        ReviewedSourceBlobContentKind::Gitlink, nullptr};
            }
            const auto found = blob_indices.find(version->object_id().value());
            if(found == blob_indices.end()) return std::nullopt;
            return preparation.blobs[found->second].observation;
        };

        const auto old_observation = observe(old_file);
        const auto new_observation = observe(new_file);
        if((old_file != nullptr && !old_observation.has_value()) ||
           (new_file != nullptr && !new_observation.has_value())) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
                    entry_index);
        }

        ReviewedSourceReviewRepresentation representation;
        std::optional<ReviewedSourcePatchRequest> patch_request;
        if(old_file == nullptr) {
            representation = one_sided_representation(new_observation->kind);
        } else if(new_file == nullptr) {
            representation = one_sided_representation(old_observation->kind);
        } else if(old_observation->kind ==
                          ReviewedSourceBlobContentKind::LineReviewable &&
                  new_observation->kind ==
                          ReviewedSourceBlobContentKind::LineReviewable) {
            if(old_file->object_id() == new_file->object_id()) {
                representation =
                        ReviewedSourceReviewRepresentation::NoContentChange;
            } else {
                representation =
                        ReviewedSourceReviewRepresentation::CompleteTextPatch;
                patch_request = ReviewedSourcePatchRequest{
                        entry_index,
                        blob_indices.at(old_file->object_id().value()),
                        blob_indices.at(new_file->object_id().value()),
                        old_file->object_id(), new_file->object_id()};
            }
        } else if(old_observation->kind ==
                          ReviewedSourceBlobContentKind::ContainsNul &&
                  new_observation->kind ==
                          ReviewedSourceBlobContentKind::ContainsNul) {
            representation = ReviewedSourceReviewRepresentation::ContainsNul;
        } else if(old_observation->kind ==
                          ReviewedSourceBlobContentKind::Gitlink &&
                  new_observation->kind ==
                          ReviewedSourceBlobContentKind::Gitlink) {
            representation =
                    ReviewedSourceReviewRepresentation::GitlinkMetadata;
        } else {
            representation =
                    ReviewedSourceReviewRepresentation::MixedTextAndNonText;
        }

        ReviewedSourceReviewEmphasis emphasis =
                ReviewedSourceReviewEmphasis::Ordinary;
        if((old_file != nullptr && path_is_sensitive(old_file->path())) ||
           (new_file != nullptr && path_is_sensitive(new_file->path()))) {
            emphasis = ReviewedSourceReviewEmphasis::Sensitive;
        }
        ReviewedSourceReviewReadiness readiness =
                ReviewedSourceReviewReadiness::Complete;
        if(version_is_sensitive_unrenderable(old_file, old_observation) ||
           version_is_sensitive_unrenderable(new_file, new_observation)) {
            readiness =
                    ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable;
        } else if(representation_requires_manual_inspection(representation)) {
            readiness =
                    ReviewedSourceReviewReadiness::ManualInspectionRequired;
        }

        preparation.entries.push_back(ReviewedSourceReviewEntry{
                change, emphasis, old_observation, new_observation,
                representation, readiness, std::nullopt});
        if(patch_request.has_value()) {
            preparation.patch_requests.push_back(std::move(*patch_request));
        }
    }
    return preparation;
}

ReviewedSourceReviewFinalizationResult finalize_reviewed_source_review(
        ReviewedSourceReviewPreparation preparation,
        std::vector<ReviewedSourceRawPatch> patches) {
    if(preparation.patch_requests.size() != patches.size()) {
        return review_failure(
                ReviewedSourceReviewFailureReason::
                        InconsistentProjectionAndPatch);
    }

    std::uintmax_t aggregate_patch_size = 0;
    for(std::size_t index = 0; index < patches.size(); ++index) {
        const ReviewedSourcePatchRequest& request =
                preparation.patch_requests[index];
        ReviewedSourceRawPatch& captured = patches[index];
        if(captured.entry_index != request.entry_index ||
           captured.old_object_id != request.old_object_id ||
           captured.new_object_id != request.new_object_id ||
           request.entry_index >= preparation.entries.size() ||
           request.old_blob_index >= preparation.blobs.size() ||
           request.new_blob_index >= preparation.blobs.size()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndPatch,
                    request.entry_index);
        }
        if(captured.output.size() > REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT) {
            return resource_failure(
                    ReviewedSourceReviewResourceKind::SingleRawPatch,
                    captured.output.size(),
                    REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT,
                    request.entry_index);
        }
        if(!add_without_overflow(
                   captured.output.size(), aggregate_patch_size,
                   REVIEWED_SOURCE_AGGREGATE_RAW_PATCH_LIMIT)) {
            const std::uintmax_t observed = aggregate_patch_size +
                    static_cast<std::uintmax_t>(captured.output.size());
            return resource_failure(
                    ReviewedSourceReviewResourceKind::AggregateRawPatches,
                    observed,
                    REVIEWED_SOURCE_AGGREGATE_RAW_PATCH_LIMIT,
                    request.entry_index);
        }

        const ReviewedSourcePreparedBlob& old_blob =
                preparation.blobs[request.old_blob_index];
        const ReviewedSourcePreparedBlob& new_blob =
                preparation.blobs[request.new_blob_index];
        ReviewedSourcePatchParseResult parsed =
                parse_and_verify_reviewed_source_patch(
                        captured.output,
                        request.old_object_id, request.new_object_id,
                        old_blob.raw.bytes, new_blob.raw.bytes);
        if(std::holds_alternative<ReviewedSourcePatchFailure>(parsed)) {
            return map_patch_failure(
                    std::get<ReviewedSourcePatchFailure>(parsed),
                    request.entry_index);
        }
        preparation.entries[request.entry_index].patch =
                std::get<ReviewedSourceTextPatch>(std::move(parsed));
    }

    for(std::size_t index = 0; index < preparation.entries.size(); ++index) {
        const ReviewedSourceReviewEntry& entry = preparation.entries[index];
        const bool needs_patch = entry.representation ==
                ReviewedSourceReviewRepresentation::CompleteTextPatch;
        if(needs_patch != entry.patch.has_value()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndPatch,
                    index);
        }
    }

    ReviewedSourceReviewBody body{
            aggregate_readiness(preparation.entries),
            std::move(preparation.entries)};
    return std::visit(
            [&body](const auto& value) -> ReviewedSourceMaterializedReview {
                using Value = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<
                                     Value,
                                     ReviewedSourceInitialFullReview>) {
                    return ReviewedSourceMaterializedInitialFullReview{
                            value.target, std::move(body)};
                } else if constexpr(std::is_same_v<
                                            Value,
                                            ReviewedSourceAlreadyReviewed>) {
                    return ReviewedSourceMaterializedAlreadyReviewed{
                            value.revision};
                } else if constexpr(std::is_same_v<
                                            Value,
                                            ReviewedSourceUpdateReview>) {
                    return ReviewedSourceMaterializedUpdateReview{
                            value.baseline, value.target, value.relation,
                            std::move(body)};
                } else {
                    return ReviewedSourceMaterializedRebaselineFullReview{
                            value.unavailable_baseline, value.target,
                            value.reason, std::move(body)};
                }
            },
            preparation.projection);
}
