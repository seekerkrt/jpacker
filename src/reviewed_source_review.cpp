#include "reviewed_source_review.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

template<std::size_t WordCount>
std::string lowercase_hex_digest(
        const std::array<std::uint32_t, WordCount>& words) {
    constexpr std::array<char, 16> HEX{
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string digest;
    digest.reserve(WordCount * 8);
    for(const std::uint32_t word : words) {
        for(int shift = 28; shift >= 0; shift -= 4) {
            digest.push_back(HEX[static_cast<std::size_t>(
                    (word >> shift) & 0x0fU)]);
        }
    }
    return digest;
}

std::uint32_t load_big_endian_word(
        const std::array<std::uint8_t, 64>& block,
        std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(block[offset]) << 24U) |
           (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(block[offset + 3]);
}

class Sha1 final {
public:
    void update(std::string_view input) {
        total_size_ += input.size();
        while(!input.empty()) {
            const std::size_t copied = std::min(
                    input.size(), block_.size() - block_size_);
            for(std::size_t index = 0; index < copied; ++index) {
                block_[block_size_ + index] = static_cast<std::uint8_t>(
                        static_cast<unsigned char>(input[index]));
            }
            block_size_ += copied;
            input.remove_prefix(copied);
            if(block_size_ == block_.size()) {
                process_block();
                block_size_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bit_size = total_size_ * 8U;
        block_[block_size_++] = 0x80U;
        if(block_size_ > 56) {
            std::fill(block_.begin() + block_size_, block_.end(), 0U);
            process_block();
            block_size_ = 0;
        }
        std::fill(block_.begin() + block_size_, block_.begin() + 56, 0U);
        for(std::size_t index = 0; index < 8; ++index) {
            block_[63 - index] = static_cast<std::uint8_t>(
                    bit_size >> (index * 8U));
        }
        process_block();
        return lowercase_hex_digest(state_);
    }

private:
    void process_block() noexcept {
        std::array<std::uint32_t, 80> words{};
        for(std::size_t index = 0; index < 16; ++index) {
            words[index] = load_big_endian_word(block_, index * 4);
        }
        for(std::size_t index = 16; index < words.size(); ++index) {
            words[index] = std::rotl(
                    words[index - 3] ^ words[index - 8] ^
                            words[index - 14] ^ words[index - 16],
                    1);
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        for(std::size_t index = 0; index < words.size(); ++index) {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if(index < 20) {
                function = (b & c) | (~b & d);
                constant = 0x5a827999U;
            } else if(index < 40) {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1U;
            } else if(index < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdcU;
            } else {
                function = b ^ c ^ d;
                constant = 0xca62c1d6U;
            }
            const std::uint32_t next = std::rotl(a, 5) + function + e +
                    constant + words[index];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = next;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<std::uint32_t, 5> state_{
            0x67452301U, 0xefcdab89U, 0x98badcfeU,
            0x10325476U, 0xc3d2e1f0U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t total_size_ = 0;
};

class Sha256 final {
public:
    void update(std::string_view input) {
        total_size_ += input.size();
        while(!input.empty()) {
            const std::size_t copied = std::min(
                    input.size(), block_.size() - block_size_);
            for(std::size_t index = 0; index < copied; ++index) {
                block_[block_size_ + index] = static_cast<std::uint8_t>(
                        static_cast<unsigned char>(input[index]));
            }
            block_size_ += copied;
            input.remove_prefix(copied);
            if(block_size_ == block_.size()) {
                process_block();
                block_size_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bit_size = total_size_ * 8U;
        block_[block_size_++] = 0x80U;
        if(block_size_ > 56) {
            std::fill(block_.begin() + block_size_, block_.end(), 0U);
            process_block();
            block_size_ = 0;
        }
        std::fill(block_.begin() + block_size_, block_.begin() + 56, 0U);
        for(std::size_t index = 0; index < 8; ++index) {
            block_[63 - index] = static_cast<std::uint8_t>(
                    bit_size >> (index * 8U));
        }
        process_block();
        return lowercase_hex_digest(state_);
    }

private:
    void process_block() noexcept {
        constexpr std::array<std::uint32_t, 64> CONSTANTS{
                0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
                0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
                0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
                0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
                0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
                0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
                0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
                0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
                0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
                0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
                0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
                0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
                0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
                0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
                0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        std::array<std::uint32_t, 64> words{};
        for(std::size_t index = 0; index < 16; ++index) {
            words[index] = load_big_endian_word(block_, index * 4);
        }
        for(std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t lower_sigma0 =
                    std::rotr(words[index - 15], 7) ^
                    std::rotr(words[index - 15], 18) ^
                    (words[index - 15] >> 3U);
            const std::uint32_t lower_sigma1 =
                    std::rotr(words[index - 2], 17) ^
                    std::rotr(words[index - 2], 19) ^
                    (words[index - 2] >> 10U);
            words[index] = words[index - 16] + lower_sigma0 +
                    words[index - 7] + lower_sigma1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for(std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t upper_sigma1 = std::rotr(e, 6) ^
                    std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t first = h + upper_sigma1 + choose +
                    CONSTANTS[index] + words[index];
            const std::uint32_t upper_sigma0 = std::rotr(a, 2) ^
                    std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority =
                    (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = upper_sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
            0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t total_size_ = 0;
};

std::string canonical_blob_object_id(
        GitObjectFormat format, std::string_view payload) {
    std::string header = "blob " + std::to_string(payload.size());
    header.push_back('\0');
    switch(format) {
    case GitObjectFormat::Sha1: {
        Sha1 hash;
        hash.update(header);
        hash.update(payload);
        return hash.finish();
    }
    case GitObjectFormat::Sha256: {
        Sha256 hash;
        hash.update(header);
        hash.update(payload);
        return hash.finish();
    }
    }
    return {};
}

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
        const std::string_view payload = output.substr(offset, payload_size);
        // POLICY(#411): cat-file's header is framing, not content-address
        // authority. Hash canonical blob bytes from the actual payload size
        // before copying, NUL classification, or review-state preparation.
        if(canonical_blob_object_id(request.object_id.format(), payload) !=
           request.object_id.value()) {
            return review_failure(
                    ReviewedSourceReviewFailureReason::
                            BlobContentHashMismatch,
                    0, index);
        }
        blobs.push_back(ReviewedSourceRawBlob{
                request.object_id,
                std::string(payload)});
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
