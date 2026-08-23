#pragma once

#include "reviewed_source_state.hpp"
#include "xdg_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>


// POLICY(#411): this module owns XDG snapshot lookup and CAS publication for
// reviewed AUR source state. It does not own Git projection, acceptance, or
// production lifecycle.
//
// Lookup never creates directories or files. Unsafe, malformed, future,
// mismatched, forked, gapped, and orphaned records are never flattened into
// Missing or ordinary Loaded.
//
// Existing records are immutable generation files. Publication never replaces,
// exchanges, or unlinks an authoritative name. The kernel commit point is
// linkat of a retained O_TMPFILE inode onto a successor leaf derived from the
// observed generation, filesystem identity, status-change time, and raw
// contents. Destination occupancy fails with EEXIST (no-replace). Lock is
// cooperative only and is not CAS authority.
//
// Authority model:
// - The authority is the PackageBase directory reached through a retained XDG
//   lineage descriptor chain whose named links are reproven at every result
//   boundary. No separate head, manifest, index, or current pointer exists.
// - An accepted history is a single complete chain 1..N: exactly one record per
//   generation, no gap, no extra parsed record, no unrecognized managed entry,
//   and every successor leaf binding the predecessor record actually read.
// - The binding is authoritative through the raw-content digest and the
//   fail-closed single-chain proof. The predecessor device / inode /
//   status-change time in the leaf are additional discriminators; correctness
//   never depends on a filesystem giving every mutation a distinct ctime.
// - A record's security status - regular, same owner, 0600, single link - is
//   part of that proof, not an admission check performed once at open time. It
//   is re-proven after the bytes of every record are read, and the link count
//   travels inside the record identity the proof token and the CAS guard
//   compare. A hardlink added outside the PackageBase directory changes no
//   other field and never appears in a directory rescan, so nothing but a
//   direct link-count comparison can report it.
// - A named record read proves both sides of its authority after the bytes are
//   in hand: the opened descriptor is still safe, and an exact nofollow lookup
//   of the generation leaf still names that descriptor's filesystem object. A
//   hardlink followed by a rename-over can return the detached opened inode to
//   one link, so descriptor status alone cannot prove the leaf-to-inode binding.
// - A proof is not a momentary observation. Ordinary Loaded, Missing, and
//   Published are returned only when the complete proof is re-derived and found
//   identical at the result boundary. Any divergence is a typed unsafe history
//   or a typed failure, never a flattened success.
// - The kernel commit point splits the failure taxonomy. Before it, no record
//   exists, so definite failures are ordinary failures. After it, the record is
//   permanent, so a store operation never reports an ordinary definite failure;
//   it reports PublishedUncertain. That is what keeps a record the caller was
//   told did not happen from later becoming authority, on any filesystem.
//
// Crash artifacts:
// - unpublished O_TMPFILE inodes are unnamed and discarded on close
// - leftover names with the internal temp prefix are non-authoritative
// - published-looking orphans, forks, gaps, unrecognized managed names,
//   and future-owned artifacts are typed unsafe history, never ignored
// - named unlink is never used as cleanup

inline constexpr std::size_t reviewed_source_state_store_max_record_bytes =
        65536;

struct ReviewedSourceStateRecordIdentity {
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::uintmax_t owner = 0;
    std::uintmax_t mode = 0;
    // POLICY(#411): an authoritative record is reachable through exactly one
    // name. Keeping the observed link count in the identity is what lets a
    // proof token and a CAS guard state that, instead of leaving it to a ctime
    // side effect no filesystem is required to make distinguishable.
    std::uintmax_t link_count = 0;
    std::intmax_t  size = 0;
    std::intmax_t  modification_time_seconds = 0;
    std::intmax_t  modification_time_nanoseconds = 0;
    std::intmax_t  status_change_time_seconds = 0;
    std::intmax_t  status_change_time_nanoseconds = 0;

    bool operator==(const ReviewedSourceStateRecordIdentity&) const = default;
};

struct ReviewedSourceStateObservedRecord {
    std::uint64_t                     generation = 0;
    std::string                       leaf_name;
    ReviewedSourceStateRecordIdentity identity;
    std::string                       raw_contents;

    bool operator==(const ReviewedSourceStateObservedRecord&) const = default;
};

struct ReviewedSourceStateStoreRead {
    ReviewedSourceStateObservation                   observation;
    std::optional<ReviewedSourceStateObservedRecord> observed;

    bool operator==(const ReviewedSourceStateStoreRead&) const = default;
};

struct ReviewedSourceStateStorePublished {
    ReviewedSourceState               state;
    ReviewedSourceStateObservedRecord observed;

    bool operator==(const ReviewedSourceStateStorePublished&) const = default;
};

enum class ReviewedSourceStateStoreFailureKind {
    AuthorityUnavailable,
    DirectoryPreparationFailed,
    DirectoryUnavailable,
    UnsupportedFileType,
    OwnershipMismatch,
    UnsafePermissions,
    MultipleHardLinks,
    OpenFailed,
    LockFailed,
    ReadFailed,
    WriteFailed,
    SyncFailed,
    RenameFailed,
    CloseFailed,
    ConcurrentReplacement,
    FutureSchemaOverwriteRefused,
    RecordTooLarge,
};

struct ReviewedSourceStateStoreFailure {
    ReviewedSourceStateStoreFailureKind       kind;
    std::filesystem::path                     entry_path;
    std::optional<std::error_code>            system_error;
    std::optional<std::filesystem::file_type> observed_file_type;
    std::optional<std::filesystem::path>      leftover_artifact;

    bool operator==(const ReviewedSourceStateStoreFailure&) const = default;
};

enum class ReviewedSourceStatePostPublicationIssue {
    DirectorySyncUncertain,
    PublishedIdentityUncertain,
    DirectoryCloseFailed,
    LineageRevalidationFailed,
    PredecessorObservationUncertain,
    PackageDirectoryIdentityUncertain,
    // The successor was linked, but the complete chain could not be reproven
    // afterwards. The record exists and may or may not be the authority.
    AuthoritativeHistoryUncertain,
};

struct ReviewedSourceStateStorePublishedUncertain {
    ReviewedSourceState                          state;
    std::optional<ReviewedSourceStateObservedRecord> observed;
    ReviewedSourceStatePostPublicationIssue      issue;
    ReviewedSourceStateStoreFailureKind          failure_kind;
    std::filesystem::path                        entry_path;
    std::optional<std::filesystem::path>         leftover_artifact;
    std::optional<std::error_code>               system_error;

    bool operator==(const ReviewedSourceStateStorePublishedUncertain&) const =
            default;
};

enum class ReviewedSourceStateStoreHistoryIssue {
    ForkDetected,
    OrphanSuccessor,
    ChainGap,
    MissingOriginWithArtifacts,
    UnrecognizedManagedEntry,
    FutureOwnedArtifact,
    HigherGenerationUnreachable,
};

struct ReviewedSourceStateStoreUnsafeHistory {
    ReviewedSourceStateStoreHistoryIssue issue;
    std::filesystem::path                package_path;
    std::vector<std::string>             observed_leaves;
    std::optional<std::uint64_t>         matching_generation;
    std::optional<std::uint64_t>         highest_generation;

    bool operator==(const ReviewedSourceStateStoreUnsafeHistory&) const =
            default;
};

using ReviewedSourceStateStoreReadResult = std::variant<
        ReviewedSourceStateStoreRead,
        ReviewedSourceStateStoreUnsafeHistory,
        ReviewedSourceStateStoreFailure>;

using ReviewedSourceStateStorePublishResult = std::variant<
        ReviewedSourceStateStorePublished,
        ReviewedSourceStateStorePublishedUncertain,
        ReviewedSourceStateStoreUnsafeHistory,
        ReviewedSourceStateStoreFailure>;

// Process XDG_STATE_HOME / HOME only. The resolver itself does not touch
// the filesystem.
xdg_paths::ReviewedSourceStatePaths reviewed_source_state_store_paths();
std::filesystem::path reviewed_source_state_store_directory();

// PackageBase directory under the AUR store. Generation files live inside.
std::filesystem::path reviewed_source_state_store_entry_path(
        const PackageBaseIdentity& package_base);

// Origin generation is always 1.toml. Later generations bind the predecessor
// inode, status-change time, and raw-content digest, so a replacement or a
// same-inode rewrite of the predecessor takes the successor off the proven
// chain instead of letting it stay the tip over a predecessor it never saw.
//
// NOTE: restoring the exact predecessor inode and bytes can put that successor
// back on the chain. That is consistent, not a hole: the successor was
// committed, so the operation reported PublishedUncertain rather than disowning
// a record that exists, and a later reader answers from the chain it can prove
// at that moment. The binding does not rely on the filesystem giving the
// rewrite and the restore distinguishable ctimes.
std::string reviewed_source_state_store_origin_leaf();
std::string reviewed_source_state_store_successor_leaf(
        std::uint64_t next_generation,
        const ReviewedSourceStateRecordIdentity& predecessor,
        std::string_view predecessor_raw_contents);

// Read-no-create lookup. Missing is returned only when the package directory
// is absent after a safe directory open, the managed directory tree itself is
// absent, or the package directory contains only internal temp residue.
ReviewedSourceStateStoreReadResult read_reviewed_source_state(
        const PackageBaseIdentity& expected_package_base);

// Publish next_state under expected_package_base. expected_observed is the
// CAS guard from a prior read: nullopt means the caller observed Missing.
ReviewedSourceStateStorePublishResult publish_reviewed_source_state(
        const ReviewedSourceState& next_state,
        const std::optional<ReviewedSourceStateObservedRecord>&
                expected_observed);

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
enum class ReviewedSourceStateStoreTestFailurePoint {
    Status,
    Open,
    Read,
    Write,
    Sync,
    DirectorySync,
    Rename,
    Lock,
    PostCommitVerify,
    PostCommitPredecessorStatus,
    PostCommitPredecessorRead,
};

enum class ReviewedSourceStateStoreTestRacePoint {
    BeforePublication,
    AtPublicationBoundary,
    // Between the pre-commit authority reproof and linkat. Mutations injected
    // here are only observable to the post-commit reproof.
    AfterAuthorityProof,
    AfterPublication,
    BeforeCleanup,
    // Lookup: after the chain proof read and closed every ancestor, before the
    // boundary reproof and the final lineage revalidation.
    AfterReadAuthorityProof,
    // Inside a record read: the bytes are already in hand and the post-read
    // descriptor status and named-leaf proofs have not run yet. This is the
    // only window in which a security status change or a leaf-to-inode rebind
    // can hide behind the bytes already read. record_path names the record, and
    // a handler that is not interested in it can re-arm this point for the next
    // record the same reproof reads.
    AfterRecordContentsRead,
};

struct ReviewedSourceStateStoreTestRaceContext {
    std::filesystem::path package_directory;
    std::filesystem::path publication_path;
    std::string           publication_leaf;
    std::optional<std::filesystem::path> current_path;
    std::string           temporary_leaf;
    std::uint64_t         next_generation = 0;
    std::uintmax_t        source_device = 0;
    std::uintmax_t        source_inode = 0;
    // Only AfterRecordContentsRead fills this in.
    std::optional<std::filesystem::path> record_path = std::nullopt;
};

using ReviewedSourceStateStoreTestRaceHandler = void (*)(
        const ReviewedSourceStateStoreTestRaceContext& context);

void fail_next_reviewed_source_state_store_operation_for_test(
        ReviewedSourceStateStoreTestFailurePoint failure_point);
void run_reviewed_source_state_store_race_once_for_test(
        ReviewedSourceStateStoreTestRacePoint race_point,
        ReviewedSourceStateStoreTestRaceHandler handler);
// Treat record ctime comparison as equal so tests can prove that direct
// status/identity fields, rather than timestamp side effects, close a race.
void simulate_coarse_reviewed_source_state_store_timestamps_for_test();
void reset_reviewed_source_state_store_test_hooks();
#endif
