#pragma once

#include "source_package_identity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// POLICY(#411): this module owns the pure reviewed-revision value and its
// current-schema document. It does not own fetched / built / installed state,
// XDG publication, CAS, Git projection, or production lifecycle.
//
// Production behavior is AUR Git source only. Official repository and local
// sources must not be persisted as reviewed-source state.
//
// The state unit is PackageBase. Package child, cache path, mutable ref,
// .SRCINFO, artifact version, canonical source key, and URL leaf are not
// identity authorities.
//
// Missing is a store observation. The document codec never flattens unsafe,
// malformed, mismatched, or future state into Missing.
//
// encode() is the current-schema publication form. decode accepts any valid
// current-schema document, not only encode() output. A later CAS store must
// retain observed raw bytes separately and must not re-encode as the content
// guard.

inline constexpr std::int64_t reviewed_source_state_schema_version = 1;

class ReviewedSourceState final {
public:
    ReviewedSourceState() = delete;
    ReviewedSourceState(const ReviewedSourceState&) = default;
    ReviewedSourceState(ReviewedSourceState&&) noexcept = default;
    ReviewedSourceState& operator=(const ReviewedSourceState&) = default;
    ReviewedSourceState& operator=(ReviewedSourceState&&) noexcept = default;
    ~ReviewedSourceState() = default;

    [[nodiscard]] static ReviewedSourceState make(
            PackageBaseIdentity package_base,
            SourceRevisionIdentity reviewed_revision);

    [[nodiscard]] std::int64_t schema_version() const noexcept;
    [[nodiscard]] const PackageBaseIdentity& package_base() const noexcept;
    [[nodiscard]] const SourceRevisionIdentity& reviewed_revision()
            const noexcept;

    bool operator==(const ReviewedSourceState&) const = default;

private:
    ReviewedSourceState(
            std::int64_t schema_version,
            PackageBaseIdentity package_base,
            SourceRevisionIdentity reviewed_revision) noexcept;

    std::int64_t           schema_version_;
    PackageBaseIdentity    package_base_;
    SourceRevisionIdentity reviewed_revision_;
};

struct ReviewedSourceStateMissing {
    bool operator==(const ReviewedSourceStateMissing&) const = default;
};

struct ReviewedSourceStateLoaded {
    ReviewedSourceState state;

    bool operator==(const ReviewedSourceStateLoaded&) const = default;
};

enum class ReviewedSourceStateInvalidReason {
    MissingSchemaVersion,
    MalformedSchemaVersion,
    MissingSourceKind,
    UnsupportedSourceKind,
    MissingPackageBase,
    MalformedPackageBase,
    MissingCanonicalGitRemote,
    MalformedCanonicalGitRemote,
    MissingReviewedCommit,
    MalformedReviewedCommit,
    UnknownKey,
    UnexpectedRepresentation,
};

struct ReviewedSourceStateInvalid {
    ReviewedSourceStateInvalidReason reason;
    std::optional<std::string>       field;

    bool operator==(const ReviewedSourceStateInvalid&) const = default;
};

enum class ReviewedSourceStateCorruptedReason {
    EmptyDocument,
    InvalidUtf8,
    UnparseableDocument,
};

struct ReviewedSourceStateCorrupted {
    ReviewedSourceStateCorruptedReason reason;

    bool operator==(const ReviewedSourceStateCorrupted&) const = default;
};

struct ReviewedSourceStateUnsupportedFuture {
    std::int64_t schema_version;

    bool operator==(const ReviewedSourceStateUnsupportedFuture&) const =
            default;
};

enum class ReviewedSourceStateMismatchReason {
    SourceKindMismatch,
    CanonicalGitRemoteMismatch,
    PackageBaseMismatch,
};

struct ReviewedSourceStateSourceMismatch {
    ReviewedSourceState                            observed;
    PackageBaseIdentity                            expected;
    std::vector<ReviewedSourceStateMismatchReason> reasons;

    bool operator==(const ReviewedSourceStateSourceMismatch&) const = default;
};

// Observed file contents only. Absence of a file is not represented here.
using ReviewedSourceStateDocument = std::variant<
        ReviewedSourceStateLoaded,
        ReviewedSourceStateInvalid,
        ReviewedSourceStateCorrupted,
        ReviewedSourceStateUnsupportedFuture>;

using ReviewedSourceStateMatch = std::variant<
        ReviewedSourceStateLoaded,
        ReviewedSourceStateSourceMismatch>;

using ReviewedSourceStateInterpretation = std::variant<
        ReviewedSourceStateLoaded,
        ReviewedSourceStateInvalid,
        ReviewedSourceStateCorrupted,
        ReviewedSourceStateSourceMismatch,
        ReviewedSourceStateUnsupportedFuture>;

// Store-facing vocabulary. Missing is produced only by a later no-create
// lookup, never by decode or interpret of observed contents.
using ReviewedSourceStateObservation = std::variant<
        ReviewedSourceStateMissing,
        ReviewedSourceStateLoaded,
        ReviewedSourceStateInvalid,
        ReviewedSourceStateCorrupted,
        ReviewedSourceStateSourceMismatch,
        ReviewedSourceStateUnsupportedFuture>;

[[nodiscard]] std::string encode_reviewed_source_state(
        const ReviewedSourceState& state);

[[nodiscard]] ReviewedSourceStateDocument decode_reviewed_source_state(
        std::string_view document);

[[nodiscard]] ReviewedSourceStateMatch match_reviewed_source_state(
        const ReviewedSourceState& state,
        const PackageBaseIdentity& expected_package_base);

[[nodiscard]] ReviewedSourceStateInterpretation
interpret_reviewed_source_state(
        std::string_view document,
        const PackageBaseIdentity& expected_package_base);
