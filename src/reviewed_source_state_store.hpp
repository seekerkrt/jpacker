#pragma once

#include "reviewed_source_state.hpp"
#include "xdg_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <variant>


// POLICY(#411): this module owns XDG snapshot lookup and CAS publication for
// reviewed AUR source state. It does not own Git projection, acceptance, or
// production lifecycle.
//
// Lookup never creates directories or files. Unsafe, malformed, future, and
// mismatched records are never flattened into Missing.
//
// CAS authority is the observed raw destination bytes plus filesystem
// identity. Re-encoded canonical TOML is not a content guard.
// Unsupported future schema is never overwritten.

struct ReviewedSourceStateRecordIdentity {
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::uintmax_t owner = 0;
    std::uintmax_t mode = 0;
    std::intmax_t  size = 0;
    std::intmax_t  modification_time_seconds = 0;
    std::intmax_t  modification_time_nanoseconds = 0;
    std::intmax_t  status_change_time_seconds = 0;
    std::intmax_t  status_change_time_nanoseconds = 0;

    bool operator==(const ReviewedSourceStateRecordIdentity&) const = default;
};

struct ReviewedSourceStateObservedRecord {
    ReviewedSourceStateRecordIdentity identity;
    std::string                       raw_contents;

    bool operator==(const ReviewedSourceStateObservedRecord&) const = default;
};

struct ReviewedSourceStateStoreRead {
    ReviewedSourceStateObservation                 observation;
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
};

struct ReviewedSourceStateStoreFailure {
    ReviewedSourceStateStoreFailureKind        kind;
    std::filesystem::path                      entry_path;
    std::optional<std::error_code>             system_error;
    std::optional<std::filesystem::file_type>  observed_file_type;

    bool operator==(const ReviewedSourceStateStoreFailure&) const = default;
};

using ReviewedSourceStateStoreReadResult = std::variant<
        ReviewedSourceStateStoreRead,
        ReviewedSourceStateStoreFailure>;

using ReviewedSourceStateStorePublishResult = std::variant<
        ReviewedSourceStateStorePublished,
        ReviewedSourceStateStoreFailure>;

// Process XDG_STATE_HOME / HOME only. The resolver itself does not touch
// the filesystem.
xdg_paths::ReviewedSourceStatePaths reviewed_source_state_store_paths();
std::filesystem::path reviewed_source_state_store_directory();
std::filesystem::path reviewed_source_state_store_entry_path(
        const PackageBaseIdentity& package_base);

// Read-no-create lookup. Missing is returned only when the destination name
// is absent after a safe directory open, or when the managed directory tree
// itself is absent.
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
    Rename,
    Lock,
};

enum class ReviewedSourceStateStoreTestRacePoint {
    BeforePublication,
    AtPublicationBoundary,
};

using ReviewedSourceStateStoreTestRaceHandler = void (*)(
        const std::filesystem::path& entry_path);

void fail_next_reviewed_source_state_store_operation_for_test(
        ReviewedSourceStateStoreTestFailurePoint failure_point);
void run_reviewed_source_state_store_race_once_for_test(
        ReviewedSourceStateStoreTestRacePoint race_point,
        ReviewedSourceStateStoreTestRaceHandler handler);
void reset_reviewed_source_state_store_test_hooks();
#endif
