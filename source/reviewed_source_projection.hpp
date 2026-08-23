#pragma once

#include "source_package_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

inline constexpr std::size_t REVIEWED_SOURCE_MACHINE_STREAM_LIMIT =
        16U * 1024U * 1024U;
inline constexpr std::uintmax_t REVIEWED_SOURCE_SINGLE_BLOB_LIMIT =
        64U * 1024U * 1024U;
inline constexpr std::uintmax_t REVIEWED_SOURCE_AGGREGATE_BLOB_LIMIT =
        256U * 1024U * 1024U;
inline constexpr std::size_t REVIEWED_SOURCE_RENAME_CANDIDATE_LIMIT = 1000;

// Git tree paths are opaque bytes. They are not decoded as UTF-8 or converted
// to filesystem::path; presentation must use escaped_display().
class ReviewedSourcePath final {
public:
    ReviewedSourcePath() = delete;

    [[nodiscard]] static ReviewedSourcePath make(std::string raw_bytes);

    [[nodiscard]] const std::string& raw_bytes() const noexcept;
    [[nodiscard]] std::string escaped_display() const;

    bool operator==(const ReviewedSourcePath&) const = default;

private:
    explicit ReviewedSourcePath(std::string raw_bytes) noexcept;

    std::string raw_bytes_;
};

class ReviewedSourceObjectId final {
public:
    ReviewedSourceObjectId() = delete;

    [[nodiscard]] static ReviewedSourceObjectId make(std::string object_id);

    [[nodiscard]] GitObjectFormat format() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;

    bool operator==(const ReviewedSourceObjectId&) const = default;

private:
    ReviewedSourceObjectId(
            GitObjectFormat format, std::string object_id) noexcept;

    GitObjectFormat format_;
    std::string     object_id_;
};

enum class ReviewedSourceFileMode {
    Regular,
    Executable,
    SymbolicLink,
    Gitlink,
};

enum class ReviewedSourceFileType {
    Regular,
    SymbolicLink,
    Gitlink,
};

enum class ReviewedSourceFileClassification {
    TrackedSource,
    GeneratedMetadata,
};

[[nodiscard]] ReviewedSourceFileType reviewed_source_file_type(
        ReviewedSourceFileMode mode) noexcept;

class ReviewedSourceFileVersion final {
public:
    ReviewedSourceFileVersion() = delete;

    [[nodiscard]] static ReviewedSourceFileVersion make(
            ReviewedSourcePath path,
            ReviewedSourceFileMode mode,
            ReviewedSourceObjectId object_id,
            std::optional<std::uintmax_t> blob_size);

    [[nodiscard]] const ReviewedSourcePath& path() const noexcept;
    [[nodiscard]] ReviewedSourceFileClassification classification()
            const noexcept;
    [[nodiscard]] ReviewedSourceFileMode mode() const noexcept;
    [[nodiscard]] const ReviewedSourceObjectId& object_id() const noexcept;
    [[nodiscard]] const std::optional<std::uintmax_t>& blob_size()
            const noexcept;

    bool operator==(const ReviewedSourceFileVersion&) const = default;

private:
    ReviewedSourceFileVersion(
            ReviewedSourcePath path,
            ReviewedSourceFileClassification classification,
            ReviewedSourceFileMode mode,
            ReviewedSourceObjectId object_id,
            std::optional<std::uintmax_t> blob_size) noexcept;

    ReviewedSourcePath               path_;
    ReviewedSourceFileClassification classification_;
    ReviewedSourceFileMode           mode_;
    ReviewedSourceObjectId           object_id_;
    std::optional<std::uintmax_t>     blob_size_;
};

struct ReviewedSourceTextChange {
    std::uintmax_t added_lines = 0;
    std::uintmax_t deleted_lines = 0;

    bool operator==(const ReviewedSourceTextChange&) const = default;
};

struct ReviewedSourceBinaryChange {
    bool operator==(const ReviewedSourceBinaryChange&) const = default;
};

using ReviewedSourceContentChange = std::variant<
        ReviewedSourceTextChange,
        ReviewedSourceBinaryChange>;

struct ReviewedSourceAdded {
    ReviewedSourceFileVersion new_version;
    ReviewedSourceContentChange content;

    bool operator==(const ReviewedSourceAdded&) const = default;
};

struct ReviewedSourceModified {
    ReviewedSourceFileVersion old_version;
    ReviewedSourceFileVersion new_version;
    ReviewedSourceContentChange content;

    bool operator==(const ReviewedSourceModified&) const = default;
};

struct ReviewedSourceDeleted {
    ReviewedSourceFileVersion old_version;
    ReviewedSourceContentChange content;

    bool operator==(const ReviewedSourceDeleted&) const = default;
};

struct ReviewedSourceRenamed {
    ReviewedSourceFileVersion old_version;
    ReviewedSourceFileVersion new_version;
    std::uint8_t similarity = 0;
    ReviewedSourceContentChange content;

    bool operator==(const ReviewedSourceRenamed&) const = default;
};

struct ReviewedSourceTypeChanged {
    ReviewedSourceFileVersion old_version;
    ReviewedSourceFileVersion new_version;
    ReviewedSourceContentChange content;

    bool operator==(const ReviewedSourceTypeChanged&) const = default;
};

using ReviewedSourceFileChange = std::variant<
        ReviewedSourceAdded,
        ReviewedSourceModified,
        ReviewedSourceDeleted,
        ReviewedSourceRenamed,
        ReviewedSourceTypeChanged>;

struct ReviewedSourceInitialFullReview {
    SourceRevisionIdentity target;
    std::vector<ReviewedSourceFileChange> changes;

    bool operator==(const ReviewedSourceInitialFullReview&) const = default;
};

struct ReviewedSourceAlreadyReviewed {
    SourceRevisionIdentity revision;

    bool operator==(const ReviewedSourceAlreadyReviewed&) const = default;
};

enum class ReviewedSourceHistoryRelation {
    Ancestor,
    NonAncestor,
};

struct ReviewedSourceUpdateReview {
    SourceRevisionIdentity baseline;
    SourceRevisionIdentity target;
    ReviewedSourceHistoryRelation relation;
    std::vector<ReviewedSourceFileChange> changes;

    bool operator==(const ReviewedSourceUpdateReview&) const = default;
};

enum class ReviewedSourceBaselineUnavailableReason {
    MissingOrNotCommit,
};

struct ReviewedSourceRebaselineFullReview {
    SourceRevisionIdentity unavailable_baseline;
    SourceRevisionIdentity target;
    ReviewedSourceBaselineUnavailableReason reason;
    std::vector<ReviewedSourceFileChange> changes;

    bool operator==(const ReviewedSourceRebaselineFullReview&) const = default;
};

using ReviewedSourceProjection = std::variant<
        ReviewedSourceInitialFullReview,
        ReviewedSourceAlreadyReviewed,
        ReviewedSourceUpdateReview,
        ReviewedSourceRebaselineFullReview>;

[[nodiscard]] ReviewedSourceObjectId reviewed_source_empty_tree_object_id(
        GitObjectFormat format);
