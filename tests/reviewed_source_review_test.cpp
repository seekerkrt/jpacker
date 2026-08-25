#include "reviewed_source_review.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

constexpr const char* SHA1_A =
        "1111111111111111111111111111111111111111";
constexpr const char* SHA1_B =
        "2222222222222222222222222222222222222222";
constexpr const char* SHA1_C =
        "3333333333333333333333333333333333333333";
constexpr const char* SHA1_D =
        "4444444444444444444444444444444444444444";
constexpr const char* SHA1_E =
        "5555555555555555555555555555555555555555";
constexpr const char* GIT_SHA1_EMPTY =
        "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
constexpr const char* GIT_SHA256_EMPTY =
        "473a0f4c3be8a93681a267e3b1e9a7dcda1185436fe141f7749120a303721813";
constexpr const char* GIT_SHA1_ABC =
        "f2ba8f84ab5c1bce84a7b441cb1959cfc7093b7f";
constexpr const char* GIT_SHA256_ABC =
        "c1cf6e465077930e88dc5136641d402f72a229ddd996f627d60e9639eaba35a6";
constexpr const char* GIT_SHA1_NUL =
        "20b5be91886d0b6f26dc98a225c0dac05fe2c86e";
constexpr const char* GIT_SHA1_AAAA =
        "a9a22e66dbef55a4bfba528dacaa2253145dc44d";
constexpr const char* GIT_SHA256_AAAA =
        "0348244081bc1dc0e613ecfdefffd7a98d1f74864e7faa7b9db9d770d33f309b";
constexpr const char* GIT_SHA1_128_X =
        "e9570ba66749101d9fb6b9bfdd9ae7bcc44b640f";
constexpr const char* GIT_SHA256_128_X =
        "7cb275110f56217f954ce3607841481b4420dc1c73221d3670da2e33c2a86b70";

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Expected, typename Variant>
const Expected& require_arm(
        const Variant& value, std::string_view message) {
    const Expected* result = std::get_if<Expected>(&value);
    if(result == nullptr) throw std::runtime_error(std::string(message));
    return *result;
}

ReviewedSourceFileVersion version(
        std::string path,
        ReviewedSourceFileMode mode,
        const char* oid,
        std::optional<std::uintmax_t> size) {
    return ReviewedSourceFileVersion::make(
            ReviewedSourcePath::make(std::move(path)), mode,
            ReviewedSourceObjectId::make(oid), size);
}

ReviewedSourceContentChange git_marker(bool binary) {
    return binary
            ? ReviewedSourceContentChange{ReviewedSourceBinaryChange{}}
            : ReviewedSourceContentChange{ReviewedSourceTextChange{1, 1}};
}

std::string patch_header(const std::string& old_oid, const std::string& new_oid) {
    return "diff --git " + old_oid + " " + new_oid + "\n" +
           "index " + old_oid + ".." + new_oid + " 100644\n" +
           "--- " + old_oid + "\n" +
           "+++ " + new_oid + "\n";
}

std::string one_line_patch(
        const std::string& old_oid,
        const std::string& new_oid,
        std::string_view old_line,
        std::string_view new_line) {
    return patch_header(old_oid, new_oid) +
           "@@ -1 +1 @@\n-" + std::string(old_line) +
           "\n+" + std::string(new_line) + "\n";
}

std::string blob_batch_record(
        const ReviewedSourceBlobRequest& request,
        std::string_view payload) {
    std::string record = request.object_id.value() + " blob " +
            std::to_string(payload.size());
    record.push_back('\0');
    record.append(payload);
    record.push_back('\0');
    return record;
}

void test_resource_boundaries() {
    for(const ReviewedSourceReviewResourceKind resource : {
                ReviewedSourceReviewResourceKind::ReviewEntries,
                ReviewedSourceReviewResourceKind::LineReviewableBlob,
                ReviewedSourceReviewResourceKind::
                        AggregateLineReviewableBlobs,
                ReviewedSourceReviewResourceKind::LogicalLine,
                ReviewedSourceReviewResourceKind::SingleRawPatch,
                ReviewedSourceReviewResourceKind::AggregateRawPatches}) {
        const std::uintmax_t limit =
                reviewed_source_review_resource_limit(resource);
        require(std::holds_alternative<std::monostate>(
                        preflight_reviewed_source_review_resource(
                                resource, limit)),
                "Exact review resource limit was rejected");
        const auto one_over = preflight_reviewed_source_review_resource(
                resource, limit + 1);
        const auto& failure = require_arm<ReviewedSourceReviewFailure>(
                one_over, "One-over review resource limit was accepted");
        require(failure.reason ==
                            ReviewedSourceReviewFailureReason::
                                    ResourceLimitExceeded &&
                        failure.resource == resource &&
                        failure.observed == limit + 1 &&
                        failure.limit == limit,
                "Review resource failure detail drifted");
    }
}

std::string bounded_text_blob(std::size_t size) {
    std::string blob(size, 'x');
    for(std::size_t offset = REVIEWED_SOURCE_LOGICAL_LINE_LIMIT;
        offset <= blob.size();
        offset += REVIEWED_SOURCE_LOGICAL_LINE_LIMIT) {
        blob[offset - 1] = '\n';
    }
    return blob;
}

ReviewedSourceProjection added_projection(
        const std::vector<ReviewedSourceBlobRequest>& requests) {
    std::vector<ReviewedSourceFileChange> changes;
    changes.reserve(requests.size());
    for(std::size_t index = 0; index < requests.size(); ++index) {
        changes.push_back(ReviewedSourceAdded{
                version(
                        "resource-" + std::to_string(index),
                        ReviewedSourceFileMode::Regular,
                        requests[index].object_id.value().c_str(),
                        requests[index].expected_size),
                git_marker(false)});
    }
    return ReviewedSourceInitialFullReview{
            SourceRevisionIdentity::git_commit(std::string(40, 'a')),
            std::move(changes)};
}

void test_materialization_resource_integration() {
    const ReviewedSourceBlobRequest exact_single{
            ReviewedSourceObjectId::make(SHA1_A),
            REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT};
    const ReviewedSourceProjection exact_projection =
            added_projection({exact_single});
    require(std::holds_alternative<ReviewedSourceReviewPreparation>(
                    prepare_reviewed_source_review(
                            exact_projection,
                            {{exact_single.object_id,
                              bounded_text_blob(
                                      REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT)}})),
            "Exact line-reviewable blob limit was rejected by materialization");

    const ReviewedSourceBlobRequest oversized_single{
            ReviewedSourceObjectId::make(SHA1_A),
            REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT + 1};
    const auto oversized_result = prepare_reviewed_source_review(
            added_projection({oversized_single}),
            {{oversized_single.object_id,
              bounded_text_blob(
                      REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT + 1)}});
    const auto& oversized_failure = require_arm<ReviewedSourceReviewFailure>(
            oversized_result,
            "One-over line-reviewable blob limit was accepted");
    require(oversized_failure.resource ==
                    ReviewedSourceReviewResourceKind::LineReviewableBlob,
            "One-over blob integration failure resource drifted");

    const std::vector<const char*> aggregate_oids{
            SHA1_A, SHA1_B, SHA1_C, SHA1_D};
    std::vector<ReviewedSourceBlobRequest> exact_requests;
    std::vector<ReviewedSourceRawBlob> exact_blobs;
    for(const char* oid : aggregate_oids) {
        ReviewedSourceBlobRequest request{
                ReviewedSourceObjectId::make(oid),
                REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT};
        exact_blobs.push_back(ReviewedSourceRawBlob{
                request.object_id,
                bounded_text_blob(
                        REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT)});
        exact_requests.push_back(std::move(request));
    }
    require(std::holds_alternative<ReviewedSourceReviewPreparation>(
                    prepare_reviewed_source_review(
                            added_projection(exact_requests),
                            std::move(exact_blobs))),
            "Exact aggregate line-reviewable blob limit was rejected");

    exact_requests.push_back(ReviewedSourceBlobRequest{
            ReviewedSourceObjectId::make(SHA1_E), 1});
    std::vector<ReviewedSourceRawBlob> oversized_blobs;
    for(const ReviewedSourceBlobRequest& request : exact_requests) {
        oversized_blobs.push_back(ReviewedSourceRawBlob{
                request.object_id,
                request.expected_size == 1
                        ? std::string("x")
                        : bounded_text_blob(
                                  REVIEWED_SOURCE_LINE_REVIEWABLE_BLOB_LIMIT)});
    }
    const auto aggregate_result = prepare_reviewed_source_review(
            added_projection(exact_requests), std::move(oversized_blobs));
    const auto& aggregate_failure = require_arm<ReviewedSourceReviewFailure>(
            aggregate_result,
            "One-over aggregate line-reviewable limit was accepted");
    require(aggregate_failure.resource ==
                            ReviewedSourceReviewResourceKind::
                                    AggregateLineReviewableBlobs &&
                    aggregate_failure.observed ==
                            REVIEWED_SOURCE_AGGREGATE_LINE_REVIEWABLE_BLOB_LIMIT +
                                    1,
            "Aggregate line-reviewable failure detail drifted");

    std::vector<ReviewedSourceFileChange> entry_changes;
    entry_changes.reserve(REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT + 1);
    const ReviewedSourceFileVersion shared = version(
            "shared", ReviewedSourceFileMode::Regular, SHA1_A, 1);
    for(std::size_t index = 0;
        index < REVIEWED_SOURCE_REVIEW_ENTRY_LIMIT + 1; ++index) {
        entry_changes.push_back(ReviewedSourceAdded{
                ReviewedSourceFileVersion::make(
                        ReviewedSourcePath::make(
                                "entry-" + std::to_string(index)),
                        shared.mode(), shared.object_id(), shared.blob_size()),
                git_marker(false)});
    }
    const auto exact_entry_plan = plan_reviewed_source_blob_requests(
            ReviewedSourceInitialFullReview{
                    SourceRevisionIdentity::git_commit(std::string(40, 'a')),
                    std::vector<ReviewedSourceFileChange>(
                            entry_changes.begin(), entry_changes.end() - 1)});
    require(std::holds_alternative<std::vector<ReviewedSourceBlobRequest>>(
                    exact_entry_plan),
            "Exact review-entry limit was rejected");
    const auto one_over_entry_plan = plan_reviewed_source_blob_requests(
            ReviewedSourceInitialFullReview{
                    SourceRevisionIdentity::git_commit(std::string(40, 'a')),
                    std::move(entry_changes)});
    require(require_arm<ReviewedSourceReviewFailure>(
                    one_over_entry_plan,
                    "One-over review-entry limit was accepted")
                            .resource ==
                    ReviewedSourceReviewResourceKind::ReviewEntries,
            "One-over review-entry failure resource drifted");
}

void test_sensitive_path_policy() {
    require(reviewed_source_review_emphasis(
                    ReviewedSourcePath::make("PKGBUILD")) ==
                    ReviewedSourceReviewEmphasis::Sensitive,
            "Root PKGBUILD was not sensitive");
    require(reviewed_source_review_emphasis(
                    ReviewedSourcePath::make("pkg.install")) ==
                    ReviewedSourceReviewEmphasis::Sensitive,
            "Root install script was not sensitive");
    require(reviewed_source_review_emphasis(
                    ReviewedSourcePath::make("nested/PKGBUILD")) ==
                    ReviewedSourceReviewEmphasis::Ordinary &&
                    reviewed_source_review_emphasis(
                            ReviewedSourcePath::make(
                                    "nested/pkg.install")) ==
                            ReviewedSourceReviewEmphasis::Ordinary &&
                    reviewed_source_review_emphasis(
                            ReviewedSourcePath::make(".install")) ==
                            ReviewedSourceReviewEmphasis::Ordinary,
            "Nested or empty-prefix install emphasis drifted");
}

void test_blob_batch_protocol() {
    const ReviewedSourceBlobRequest sha1_text_request{
            ReviewedSourceObjectId::make(GIT_SHA1_ABC), 3};
    const ReviewedSourceBlobRequest sha256_text_request{
            ReviewedSourceObjectId::make(GIT_SHA256_ABC), 3};
    const ReviewedSourceBlobRequest sha1_empty_request{
            ReviewedSourceObjectId::make(GIT_SHA1_EMPTY), 0};
    const ReviewedSourceBlobRequest sha256_empty_request{
            ReviewedSourceObjectId::make(GIT_SHA256_EMPTY), 0};
    const ReviewedSourceBlobRequest nul_request{
            ReviewedSourceObjectId::make(GIT_SHA1_NUL), 3};
    const ReviewedSourceBlobRequest sha1_multiblock_request{
            ReviewedSourceObjectId::make(GIT_SHA1_128_X), 128};
    const ReviewedSourceBlobRequest sha256_multiblock_request{
            ReviewedSourceObjectId::make(GIT_SHA256_128_X), 128};
    const std::vector<ReviewedSourceBlobRequest> requests{
            sha1_text_request, sha256_text_request,
            sha1_empty_request, sha256_empty_request, nul_request,
            sha1_multiblock_request, sha256_multiblock_request};
    const std::string nul_payload("a\0b", 3);
    const std::string multiblock_payload(128, 'x');
    std::string output = blob_batch_record(sha1_text_request, "abc");
    output += blob_batch_record(sha256_text_request, "abc");
    output += blob_batch_record(sha1_empty_request, "");
    output += blob_batch_record(sha256_empty_request, "");
    output += blob_batch_record(nul_request, nul_payload);
    output += blob_batch_record(
            sha1_multiblock_request, multiblock_payload);
    output += blob_batch_record(
            sha256_multiblock_request, multiblock_payload);

    const auto size = reviewed_source_blob_batch_capture_size(requests);
    require(require_arm<std::size_t>(
                    size, "Valid blob batch size was unavailable") ==
                    output.size(),
            "Blob batch exact capture size drifted");
    const auto parsed = parse_reviewed_source_blob_batch_output(
            requests, output);
    const auto& blobs = require_arm<std::vector<ReviewedSourceRawBlob>>(
            parsed, "Valid blob batch did not parse");
    require(blobs.size() == 7 && blobs[0].bytes == "abc" &&
                    blobs[1].bytes == "abc" && blobs[2].bytes.empty() &&
                    blobs[3].bytes.empty() && blobs[4].bytes == nul_payload &&
                    blobs[5].bytes == multiblock_payload &&
                    blobs[6].bytes == multiblock_payload,
            "SHA-1/SHA-256 incremental blob payloads were not preserved");

    const ReviewedSourceProjection nul_projection =
            ReviewedSourceInitialFullReview{
                    SourceRevisionIdentity::git_commit(std::string(40, 'a')),
                    {ReviewedSourceAdded{
                            version(
                                    "binary.dat",
                                    ReviewedSourceFileMode::Regular,
                                    GIT_SHA1_NUL, 3),
                            git_marker(false)}}};
    const ReviewedSourceReviewPreparationResult nul_prepared =
            prepare_reviewed_source_review(nul_projection, {blobs[4]});
    const auto& nul_preparation =
            require_arm<ReviewedSourceReviewPreparation>(
                    nul_prepared,
                    "Hash-verified NUL blob did not prepare");
    require(nul_preparation.entries.size() == 1 &&
                    nul_preparation.entries[0].new_observation.has_value() &&
                    nul_preparation.entries[0].new_observation->kind ==
                            ReviewedSourceBlobContentKind::ContainsNul,
            "Hash-verified NUL blob was not classified as ContainsNul");

    output.pop_back();
    require(require_arm<ReviewedSourceReviewFailure>(
                    parse_reviewed_source_blob_batch_output(requests, output),
                    "Missing blob batch terminator was accepted")
                            .reason ==
                    ReviewedSourceReviewFailureReason::
                            MalformedBlobBatchOutput,
            "Malformed blob batch failure reason drifted");

    std::string missing = std::string(SHA1_A) + " missing";
    missing.push_back('\0');
    require(require_arm<ReviewedSourceReviewFailure>(
                    parse_reviewed_source_blob_batch_output(
                            {{ReviewedSourceObjectId::make(SHA1_A), 3}},
                            missing),
                    "Missing exact blob was accepted")
                            .reason ==
                    ReviewedSourceReviewFailureReason::
                            InconsistentProjectionAndBlob,
            "Missing exact blob failure reason drifted");

    const ReviewedSourceBlobRequest sha1_identity_request{
            ReviewedSourceObjectId::make(GIT_SHA1_AAAA), 4};
    const ReviewedSourceBlobBatchParseResult sha1_mismatch_result =
            parse_reviewed_source_blob_batch_output(
                    {sha1_identity_request},
                    blob_batch_record(sha1_identity_request, "BBBB"));
    const auto& sha1_mismatch = require_arm<ReviewedSourceReviewFailure>(
            sha1_mismatch_result,
            "Same-size SHA-1 payload identity mismatch was accepted");
    require(sha1_mismatch.reason ==
                            ReviewedSourceReviewFailureReason::
                                    BlobContentHashMismatch &&
                    sha1_mismatch.record_index == 0,
            "SHA-1 payload identity mismatch failure detail drifted");

    const ReviewedSourceBlobRequest sha256_identity_request{
            ReviewedSourceObjectId::make(GIT_SHA256_AAAA), 4};
    require(require_arm<ReviewedSourceReviewFailure>(
                    parse_reviewed_source_blob_batch_output(
                            {sha256_identity_request},
                            blob_batch_record(
                                    sha256_identity_request, "BBBB")),
                    "Same-size SHA-256 payload identity mismatch was accepted")
                            .reason ==
                    ReviewedSourceReviewFailureReason::
                            BlobContentHashMismatch,
            "SHA-256 payload identity mismatch failure reason drifted");

    const ReviewedSourceProjection sensitive_projection =
            ReviewedSourceInitialFullReview{
                    SourceRevisionIdentity::git_commit(std::string(40, 'a')),
                    {ReviewedSourceAdded{
                            version(
                                    "PKGBUILD",
                                    ReviewedSourceFileMode::Regular,
                                    GIT_SHA1_AAAA, 4),
                            git_marker(false)}}};
    const ReviewedSourceBlobRequestPlanResult sensitive_plan =
            plan_reviewed_source_blob_requests(sensitive_projection);
    const auto& sensitive_requests =
            require_arm<std::vector<ReviewedSourceBlobRequest>>(
                    sensitive_plan,
                    "Sensitive PKGBUILD blob request planning failed");
    std::string partial_output = blob_batch_record(
            sha1_text_request, "abc");
    partial_output += blob_batch_record(
            sensitive_requests.front(), "BBBB");
    const ReviewedSourceBlobBatchParseResult partial_result =
            parse_reviewed_source_blob_batch_output(
                    {sha1_text_request, sensitive_requests.front()},
                    partial_output);
    const auto& partial_failure = require_arm<ReviewedSourceReviewFailure>(
            partial_result,
            "Sensitive PKGBUILD mismatch published a partial blob result");
    require(partial_failure.reason ==
                            ReviewedSourceReviewFailureReason::
                                    BlobContentHashMismatch &&
                    partial_failure.record_index == 1,
            "Sensitive PKGBUILD mismatch did not fail the complete batch");
}

void test_representation_and_readiness() {
    std::vector<ReviewedSourceFileChange> changes;
    changes.push_back(ReviewedSourceModified{
            version("PKGBUILD", ReviewedSourceFileMode::Regular, SHA1_A, 4),
            version("PKGBUILD", ReviewedSourceFileMode::Regular, SHA1_B, 4),
            git_marker(true)});
    changes.push_back(ReviewedSourceModified{
            version("pkg.install", ReviewedSourceFileMode::Regular, SHA1_C, 3),
            version("pkg.install", ReviewedSourceFileMode::Regular, SHA1_D, 3),
            git_marker(false)});
    changes.push_back(ReviewedSourceModified{
            version("generic.bin", ReviewedSourceFileMode::Regular, SHA1_C, 3),
            version("generic.bin", ReviewedSourceFileMode::Regular, SHA1_D, 3),
            git_marker(true)});
    changes.push_back(ReviewedSourceModified{
            version("mode-only", ReviewedSourceFileMode::Regular, SHA1_A, 4),
            version("mode-only", ReviewedSourceFileMode::Executable, SHA1_A, 4),
            git_marker(false)});
    changes.push_back(ReviewedSourceAdded{
            version(".SRCINFO", ReviewedSourceFileMode::Regular, SHA1_B, 4),
            git_marker(false)});
    changes.push_back(ReviewedSourceAdded{
            version("missing-submodule", ReviewedSourceFileMode::Gitlink,
                    SHA1_C, std::nullopt),
            git_marker(false)});
    changes.push_back(ReviewedSourceTypeChanged{
            version("mixed", ReviewedSourceFileMode::Regular, SHA1_A, 4),
            version("mixed", ReviewedSourceFileMode::Gitlink,
                    SHA1_D, std::nullopt),
            git_marker(false)});
    changes.push_back(ReviewedSourceRenamed{
            version("old-name", ReviewedSourceFileMode::Regular, SHA1_A, 4),
            version("new-name", ReviewedSourceFileMode::Regular, SHA1_B, 4),
            75, git_marker(false)});
    changes.push_back(ReviewedSourceAdded{
            version("symlink.install", ReviewedSourceFileMode::SymbolicLink,
                    SHA1_A, 4),
            git_marker(false)});

    const ReviewedSourceProjection projection = ReviewedSourceUpdateReview{
            SourceRevisionIdentity::git_commit(std::string(40, 'a')),
            SourceRevisionIdentity::git_commit(std::string(40, 'b')),
            ReviewedSourceHistoryRelation::Ancestor,
            changes};
    ReviewedSourceBlobRequestPlanResult plan =
            plan_reviewed_source_blob_requests(projection);
    const auto& requests = require_arm<std::vector<ReviewedSourceBlobRequest>>(
            plan, "Review blob request plan failed");
    require(requests.size() == 4,
            "Review blob request deduplication drifted");

    std::vector<ReviewedSourceRawBlob> blobs;
    for(const ReviewedSourceBlobRequest& request : requests) {
        std::string bytes;
        if(request.object_id.value() == SHA1_A) bytes = "old\n";
        if(request.object_id.value() == SHA1_B) bytes = "new\n";
        if(request.object_id.value() == SHA1_C) bytes = std::string("a\0b", 3);
        if(request.object_id.value() == SHA1_D) bytes = std::string("c\0d", 3);
        blobs.push_back(ReviewedSourceRawBlob{request.object_id, bytes});
    }

    ReviewedSourceReviewPreparationResult prepared_result =
            prepare_reviewed_source_review(projection, std::move(blobs));
    ReviewedSourceReviewPreparation preparation =
            require_arm<ReviewedSourceReviewPreparation>(
                    prepared_result, "Review preparation failed");
    require(preparation.entries.size() == changes.size(),
            "Prepared review entry count drifted");
    require(preparation.entries[0].representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    preparation.entries[0].emphasis ==
                            ReviewedSourceReviewEmphasis::Sensitive &&
                    preparation.entries[0].readiness ==
                            ReviewedSourceReviewReadiness::Complete,
            "Git-binary/textual PKGBUILD was not independently reviewable");
    require(preparation.entries[1].representation ==
                            ReviewedSourceReviewRepresentation::ContainsNul &&
                    preparation.entries[1].readiness ==
                            ReviewedSourceReviewReadiness::
                                    SensitiveSourceUnrenderable,
            "Git-text/NUL install script was not blocked");
    require(preparation.entries[2].representation ==
                            ReviewedSourceReviewRepresentation::ContainsNul &&
                    preparation.entries[2].readiness ==
                            ReviewedSourceReviewReadiness::
                                    ManualInspectionRequired,
            "Generic NUL content did not require manual inspection");
    require(preparation.entries[3].representation ==
                            ReviewedSourceReviewRepresentation::NoContentChange &&
                    preparation.entries[3].readiness ==
                            ReviewedSourceReviewReadiness::Complete,
            "Mode-only change lost NoContentChange");
    require(std::get<ReviewedSourceAdded>(
                    preparation.entries[4].change)
                            .new_version.classification() ==
                    ReviewedSourceFileClassification::GeneratedMetadata,
            ".SRCINFO classification was lost");
    require(preparation.entries[5].representation ==
                            ReviewedSourceReviewRepresentation::GitlinkMetadata &&
                    preparation.entries[5].readiness ==
                            ReviewedSourceReviewReadiness::
                                    ManualInspectionRequired,
            "Gitlink metadata readiness drifted");
    require(preparation.entries[6].representation ==
                            ReviewedSourceReviewRepresentation::
                                    MixedTextAndNonText,
            "TypeChanged mixed representation drifted");
    require(preparation.entries[7].representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    std::holds_alternative<ReviewedSourceRenamed>(
                            preparation.entries[7].change),
            "Renamed textual change was flattened");
    require(preparation.entries[8].readiness ==
                    ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable,
            "Sensitive symlink was ordinary review-ready");

    std::vector<ReviewedSourceRawPatch> patches;
    for(const ReviewedSourcePatchRequest& request :
        preparation.patch_requests) {
        const std::string old_bytes =
                preparation.blobs[request.old_blob_index].raw.bytes;
        const std::string new_bytes =
                preparation.blobs[request.new_blob_index].raw.bytes;
        require(old_bytes.ends_with('\n') && new_bytes.ends_with('\n'),
                "Patch fixture unexpectedly lacked final newline");
        patches.push_back(ReviewedSourceRawPatch{
                request.entry_index,
                request.old_object_id, request.new_object_id,
                one_line_patch(
                        request.old_object_id.value(),
                        request.new_object_id.value(),
                        std::string_view(old_bytes).substr(0, old_bytes.size() - 1),
                        std::string_view(new_bytes).substr(0, new_bytes.size() - 1))});
    }
    const auto finalized = finalize_reviewed_source_review(
            std::move(preparation), std::move(patches));
    const auto& materialized = require_arm<ReviewedSourceMaterializedReview>(
            finalized, "Prepared review did not finalize");
    const auto& update = require_arm<ReviewedSourceMaterializedUpdateReview>(
            materialized, "Final review lifecycle was not UpdateReview");
    require(update.review.readiness ==
                    ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable,
            "Aggregate review readiness did not retain sensitive blocker");
    require(update.review.entries[0].patch.has_value() &&
                    update.review.entries[7].patch.has_value(),
            "Textual patches were not retained in their typed entries");
}

void test_initial_already_and_rebaseline_lifecycle() {
    const auto target = SourceRevisionIdentity::git_commit(std::string(40, 'b'));
    const ReviewedSourceProjection already =
            ReviewedSourceAlreadyReviewed{target};
    auto already_prepared = prepare_reviewed_source_review(already, {});
    auto already_finalized = finalize_reviewed_source_review(
            require_arm<ReviewedSourceReviewPreparation>(
                    already_prepared, "AlreadyReviewed preparation failed"),
            {});
    require(std::holds_alternative<ReviewedSourceMaterializedAlreadyReviewed>(
                    require_arm<ReviewedSourceMaterializedReview>(
                            already_finalized,
                            "AlreadyReviewed finalization failed")),
            "AlreadyReviewed lifecycle was flattened");

    const ReviewedSourceProjection rebaseline =
            ReviewedSourceRebaselineFullReview{
                    SourceRevisionIdentity::git_commit(std::string(40, 'f')),
                    target,
                    ReviewedSourceBaselineUnavailableReason::MissingOrNotCommit,
                    {}};
    auto rebaseline_prepared = prepare_reviewed_source_review(rebaseline, {});
    auto rebaseline_finalized = finalize_reviewed_source_review(
            require_arm<ReviewedSourceReviewPreparation>(
                    rebaseline_prepared, "Rebaseline preparation failed"),
            {});
    require(std::holds_alternative<
                    ReviewedSourceMaterializedRebaselineFullReview>(
                    require_arm<ReviewedSourceMaterializedReview>(
                            rebaseline_finalized,
                            "Rebaseline finalization failed")),
            "Rebaseline lifecycle was flattened");
}

void run_tests() {
    test_resource_boundaries();
    test_materialization_resource_integration();
    test_sensitive_path_policy();
    test_blob_batch_protocol();
    test_representation_and_readiness();
    test_initial_already_and_rebaseline_lifecycle();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "reviewed source review tests: all checks passed\n";
    return 0;
}
