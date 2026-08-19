#include "reviewed_source_state_store.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view SHA1_A =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view SHA1_B =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view SHA1_C =
        "cccccccccccccccccccccccccccccccccccccccc";

class TemporaryDirectory final {
    fs::path path_;

public:
    TemporaryDirectory() {
        const fs::path pattern =
                fs::temp_directory_path() / "moguet-reviewed-store-XXXXXX";
        std::string raw = pattern.string();
        if(::mkdtemp(raw.data()) == nullptr) {
            throw std::runtime_error("Failed to create store test directory.");
        }
        path_ = raw;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }
};

class ScopedEnvironmentVariable final {
    std::string name_;
    std::optional<std::string> previous_;

public:
    ScopedEnvironmentVariable(std::string name, const std::string& value)
        : name_(std::move(name)) {
        if(const char* previous = std::getenv(name_.c_str());
           previous != nullptr) {
            previous_ = previous;
        }
        if(::setenv(name_.c_str(), value.c_str(), 1) != 0) {
            throw std::runtime_error("Failed to set store test environment.");
        }
    }

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_.has_value()) {
            static_cast<void>(::setenv(name_.c_str(), previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_.c_str()));
        }
    }
};

class StoreTestHome final {
    TemporaryDirectory temporary_;
    ScopedEnvironmentVariable state_home_;
    ScopedEnvironmentVariable home_;

public:
    StoreTestHome()
        : temporary_(),
          state_home_(
                  "XDG_STATE_HOME",
                  (temporary_.path() / "state").string()),
          home_("HOME", (temporary_.path() / "home").string()) {
        fs::create_directory(temporary_.path() / "state");
        fs::create_directory(temporary_.path() / "home");
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        reset_reviewed_source_state_store_test_hooks();
#endif
    }

    const fs::path& root() const noexcept {
        return temporary_.path();
    }
};

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Arm, typename Variant>
const Arm& require_arm(const Variant& value, std::string_view message) {
    const Arm* arm = std::get_if<Arm>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

PackageBaseIdentity aur_package_base(
        const std::string& package_base = "example-base",
        const std::string& remote =
                "https://aur.archlinux.org/example-base.git") {
    return PackageBaseIdentity::make(
            PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(remote)),
            package_base);
}

ReviewedSourceState aur_state(
        const std::string& package_base = "example-base",
        const std::string& remote =
                "https://aur.archlinux.org/example-base.git",
        const std::string& commit = std::string(SHA1_A)) {
    return ReviewedSourceState::make(
            aur_package_base(package_base, remote),
            SourceRevisionIdentity::git_commit(commit));
}

mode_t file_mode(const fs::path& path) {
    struct stat status {};
    if(::lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error("lstat failed for " + path.string());
    }
    return status.st_mode & 07777;
}

struct FileIdentity {
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
};

FileIdentity file_identity(const fs::path& path) {
    struct stat status {};
    if(::lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error("lstat failed for " + path.string());
    }
    return FileIdentity{
            static_cast<std::uintmax_t>(status.st_dev),
            static_cast<std::uintmax_t>(status.st_ino)};
}

void write_bytes(const fs::path& path, std::string_view contents, mode_t mode) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error("Failed to open fixture " + path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error("Failed to write fixture " + path.string());
    }
    if(::chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error("Failed to chmod fixture " + path.string());
    }
}

// Install contents as a new inode at destination. Used to model non-cooperative
// replacement of an authoritative name.
void replace_path_with_new_inode(
        const fs::path& destination, std::string_view contents, mode_t mode) {
    const fs::path sibling =
            destination.parent_path() /
            (destination.filename().string() + ".replacement");
    write_bytes(sibling, contents, mode);
    fs::rename(sibling, destination);
}

// Rewrite destination through the same directory entry. The test requires the
// inode to stay put so the fixture is a contents-only mutation.
void rewrite_path_in_place(
        const fs::path& destination, std::string_view contents, mode_t mode) {
    const FileIdentity before = file_identity(destination);
    write_bytes(destination, contents, mode);
    const FileIdentity after = file_identity(destination);
    require(before.device == after.device && before.inode == after.inode,
            "In-place rewrite fixture replaced the inode.");
}

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

std::vector<std::string> regular_leaf_names(const fs::path& directory) {
    std::vector<std::string> names;
    if(!fs::exists(directory)) return names;
    for(const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        names.push_back(entry.path().filename().string());
    }
    return names;
}

bool contains_leaf(
        const std::vector<std::string>& names, const std::string& leaf) {
    for(const std::string& name : names) {
        if(name == leaf) return true;
    }
    return false;
}

fs::path origin_path(const PackageBaseIdentity& package_base) {
    return reviewed_source_state_store_entry_path(package_base) /
           reviewed_source_state_store_origin_leaf();
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
struct OccupyingPublication {
    std::string contents;
    static OccupyingPublication* instance;

    static void handler(const ReviewedSourceStateStoreTestRaceContext& context) {
        require(instance != nullptr, "OccupyingPublication was not armed.");
        write_bytes(context.publication_path, instance->contents, 0600);
    }
};
OccupyingPublication* OccupyingPublication::instance = nullptr;

struct ReplacingCurrent {
    std::string contents;
    static ReplacingCurrent* instance;

    static void handler(const ReviewedSourceStateStoreTestRaceContext& context) {
        require(instance != nullptr, "ReplacingCurrent was not armed.");
        require(context.current_path.has_value(),
                "Replacement race had no current generation.");
        replace_path_with_new_inode(
                *context.current_path, instance->contents, 0600);
    }
};
ReplacingCurrent* ReplacingCurrent::instance = nullptr;

struct RewritingCurrentInPlace {
    std::string contents;
    static RewritingCurrentInPlace* instance;

    static void handler(const ReviewedSourceStateStoreTestRaceContext& context) {
        require(instance != nullptr, "RewritingCurrentInPlace was not armed.");
        require(context.current_path.has_value(),
                "Same-inode rewrite race had no current generation.");
        rewrite_path_in_place(
                *context.current_path, instance->contents, 0600);
    }
};
RewritingCurrentInPlace* RewritingCurrentInPlace::instance = nullptr;

struct ReplacingCleanupTarget {
    std::string contents;
    fs::path surviving_path;
    static ReplacingCleanupTarget* instance;

    static void handler(const ReviewedSourceStateStoreTestRaceContext& context) {
        require(instance != nullptr, "ReplacingCleanupTarget was not armed.");
        require(!context.temporary_leaf.empty(),
                "Cleanup race had no temporary leaf.");
        instance->surviving_path =
                context.package_directory / context.temporary_leaf;
        replace_path_with_new_inode(
                instance->surviving_path, instance->contents, 0600);
    }
};
ReplacingCleanupTarget* ReplacingCleanupTarget::instance = nullptr;
#endif

void test_missing_lookup_does_not_create() {
    StoreTestHome home;
    const PackageBaseIdentity expected = aur_package_base();
    const auto result = read_reviewed_source_state(expected);
    const auto& loaded = require_arm<ReviewedSourceStateStoreRead>(
            result, "Missing lookup was not a store read.");
    require(std::holds_alternative<ReviewedSourceStateMissing>(
                    loaded.observation),
            "Absent store was not Missing.");
    require(!loaded.observed.has_value(), "Missing lookup carried bytes.");
    require(!fs::exists(home.root() / "state" / "moguet"),
            "Lookup created the XDG state application directory.");
}

void test_first_create_is_0600_and_round_trips() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "First publication failed.");
    require(published.state == state, "Published state drifted.");
    require(published.observed.raw_contents ==
                    encode_reviewed_source_state(state),
            "Publication did not write canonical contents.");
    require(published.observed.generation == 1,
            "First publication was not generation 1.");
    require(published.observed.leaf_name ==
                    reviewed_source_state_store_origin_leaf(),
            "First publication leaf drifted.");

    const fs::path package_dir =
            reviewed_source_state_store_entry_path(state.package_base());
    const fs::path origin = origin_path(state.package_base());
    require(file_mode(origin) == 0600, "Published file mode was not 0600.");
    require(file_mode(package_dir) == 0700,
            "Package directory mode was not 0700.");
    require(file_mode(package_dir.parent_path()) == 0700,
            "Store directory mode was not 0700.");
    require(read_bytes(origin) == published.observed.raw_contents,
            "On-disk bytes drifted from observed publication.");

    const auto read_back = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Read after publish failed.");
    const auto& loaded = require_arm<ReviewedSourceStateLoaded>(
            read_back.observation, "Published state was not Loaded.");
    require(loaded.state == state, "Read-back lost reviewed state.");
    require(read_back.observed.has_value() &&
                    read_back.observed->raw_contents ==
                            published.observed.raw_contents,
            "Read-back lost observed raw bytes.");
}

void test_successor_leaf_binds_inode_and_content_digest() {
    ReviewedSourceStateRecordIdentity identity;
    identity.device = 8;
    identity.inode = 16;
    const std::string empty_leaf = reviewed_source_state_store_successor_leaf(
            2, identity, "");
    require(empty_leaf ==
                    "2.8-16.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.toml",
            "Successor leaf did not bind SHA-256 of empty predecessor contents.");
    const std::string abc_leaf = reviewed_source_state_store_successor_leaf(
            3, identity, "abc");
    require(abc_leaf ==
                    "3.8-16.ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad.toml",
            "Successor leaf did not bind SHA-256 of predecessor contents.");
    require(empty_leaf != abc_leaf,
            "Content digest was omitted from generation identity.");
}

void test_cas_replacement_and_stale_writer() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_A));
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));

    const auto published_first = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Baseline publication failed.");
    const auto published_second = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(second, published_first.observed),
            "Valid CAS replacement failed.");
    require(published_second.state == second, "CAS replacement lost new state.");
    require(published_second.observed.generation == 2,
            "Successor publication was not generation 2.");

    const fs::path package_dir =
            reviewed_source_state_store_entry_path(second.package_base());
    const fs::path successor =
            package_dir / published_second.observed.leaf_name;
    require(read_bytes(successor) == published_second.observed.raw_contents,
            "Successor generation lost B's bytes.");
    require(read_bytes(origin_path(first.package_base())) ==
                    published_first.observed.raw_contents,
            "CAS successor mutated the origin generation.");

    const auto stale = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(first, published_first.observed),
            "Stale writer was not a store failure.");
    require(stale.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Stale writer was not ConcurrentReplacement.");
    require(read_bytes(successor) == published_second.observed.raw_contents,
            "Stale writer mutated B's authoritative generation.");

    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(second.package_base()),
            "Post-conflict read failed.");
    const auto& loaded = require_arm<ReviewedSourceStateLoaded>(
            current.observation, "B's state was not preserved.");
    require(loaded.state == second, "Stale writer rolled back reviewed state.");
}

void test_missing_create_is_no_replace() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_A));
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));

    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "First creator failed.");
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, std::nullopt),
            "Second Missing-create was not a failure.");
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Second creator was not ConcurrentReplacement.");

    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post no-replace read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "First creator state was lost.")
                    .state == first,
            "No-replace create overwrote the first publication.");
    require(read_bytes(origin_path(first.package_base())) ==
                    encode_reviewed_source_state(first),
            "Second Missing-create mutated the origin generation.");
}

void test_raw_byte_guard_is_not_reencoded() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Canonical publication failed.");

    const fs::path origin = origin_path(state.package_base());
    const std::string equivalent =
            "reviewed_commit = \"" + std::string(SHA1_A) +
            "\"\n"
            "# semantically equal, different bytes\n"
            "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
            "package_base = \"example-base\"\n"
            "source_kind = \"aur\"\n"
            "schema_version = 1\n";
    write_bytes(origin, equivalent, 0600);

    const auto reread = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Equivalent-document read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    reread.observation, "Equivalent document was not Loaded.")
                    .state == state,
            "Equivalent layout changed typed state.");
    require(reread.observed.has_value() &&
                    reread.observed->raw_contents == equivalent,
            "Read did not retain observed raw bytes.");

    const auto stale = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(state, published.observed),
            "Re-encode CAS guard was accepted.");
    require(stale.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Raw-byte mismatch was not ConcurrentReplacement.");
    require(read_bytes(origin) == equivalent,
            "Re-encode publication overwrote observed bytes.");
}

void test_future_schema_is_not_overwritten() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");

    const fs::path origin = origin_path(state.package_base());
    const std::string future = "schema_version = 2\nnext_field = true\n";
    write_bytes(origin, future, 0600);

    const auto read_future = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Future schema read failed.");
    require(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                    read_future.observation),
            "Future schema was not UnsupportedFuture.");
    require(read_future.observed.has_value() &&
                    read_future.observed->raw_contents == future,
            "Future schema raw bytes were lost.");

    const auto refused = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(state, read_future.observed),
            "Future overwrite was not a failure.");
    require(refused.kind ==
                    ReviewedSourceStateStoreFailureKind::
                            FutureSchemaOverwriteRefused,
            "Future schema overwrite was not refused.");
    require(read_bytes(origin) == future,
            "Future schema bytes were overwritten.");
}

void test_source_mismatch_is_not_loaded() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state(
            "example-base",
            "https://aur.archlinux.org/other-name.git");
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Mismatch seed publication failed.");

    const auto read_back = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(aur_package_base()),
            "Mismatch lookup failed.");
    require(std::holds_alternative<ReviewedSourceStateSourceMismatch>(
                    read_back.observation),
            "Different remote in PackageBase directory was treated as Loaded.");
}

void test_invalid_and_corrupted_are_not_missing() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");
    const fs::path origin = origin_path(state.package_base());

    write_bytes(origin, "schema_version = 1\n", 0600);
    const auto invalid = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Invalid document read failed.");
    require(std::holds_alternative<ReviewedSourceStateInvalid>(
                    invalid.observation),
            "Invalid current-schema document was flattened.");

    write_bytes(origin, "schema_version = [\n", 0600);
    const auto corrupted = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Corrupted document read failed.");
    require(std::holds_alternative<ReviewedSourceStateCorrupted>(
                    corrupted.observation),
            "Corrupted document was flattened to Missing.");
}

void test_symlink_hardlink_and_mode_are_failures() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");
    const fs::path package_dir =
            reviewed_source_state_store_entry_path(state.package_base());
    const fs::path origin = origin_path(state.package_base());
    const fs::path sibling = package_dir / "other.toml";

    fs::remove(origin);
    fs::create_symlink(sibling, origin);
    const auto symlink = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "Symlink was not a store failure.");
    require(symlink.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
            "Symlink was flattened to Missing.");
    require(symlink.observed_file_type == fs::file_type::symlink,
            "Symlink type was lost.");
    fs::remove(origin);

    write_bytes(origin, encode_reviewed_source_state(state), 0600);
    write_bytes(sibling, "other", 0600);
    if(::link(sibling.c_str(), (origin.string() + ".link").c_str()) != 0) {
        throw std::runtime_error("Failed to create hardlink fixture.");
    }
    fs::remove(origin);
    if(::link((origin.string() + ".link").c_str(), origin.c_str()) != 0) {
        throw std::runtime_error("Failed to install hardlinked destination.");
    }
    const auto hardlink = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "Hardlink was not a store failure.");
    require(hardlink.kind ==
                    ReviewedSourceStateStoreFailureKind::MultipleHardLinks,
            "Hardlink count violation was flattened.");
    fs::remove(origin);
    fs::remove(origin.string() + ".link");
    fs::remove(sibling);

    write_bytes(origin, encode_reviewed_source_state(state), 0644);
    const auto mode = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "0644 file was not a store failure.");
    require(mode.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsafePermissions,
            "Unsafe mode was flattened to Missing.");

    fs::remove(origin);
    if(::mkfifo(origin.c_str(), 0600) != 0) {
        throw std::runtime_error("Failed to create FIFO fixture.");
    }
    const auto fifo = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "FIFO was not a store failure.");
    require(fifo.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
            "FIFO was flattened to Missing.");
}

void test_oversized_record_is_typed_failure() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");
    const fs::path origin = origin_path(state.package_base());
    write_bytes(
            origin,
            std::string(reviewed_source_state_store_max_record_bytes + 1, 'x'),
            0600);
    const auto oversized = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "Oversized record was not a store failure.");
    require(oversized.kind ==
                    ReviewedSourceStateStoreFailureKind::RecordTooLarge,
            "Oversized record was not RecordTooLarge.");
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
void test_fsync_and_rename_failures_preserve_destination() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const fs::path origin = origin_path(first.package_base());
    const std::string original = read_bytes(origin);
    const FileIdentity original_identity = file_identity(origin);

    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::Sync);
    const auto sync_failed = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, published.observed),
            "Injected fsync failure was not reported.");
    require(sync_failed.kind ==
                    ReviewedSourceStateStoreFailureKind::SyncFailed,
            "Injected fsync failure kind drifted.");
    require(read_bytes(origin) == original,
            "File fsync failure mutated the published destination.");
    require(file_identity(origin).inode == original_identity.inode,
            "File fsync failure replaced the origin inode.");

    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::Rename);
    const auto rename_failed = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, published.observed),
            "Injected rename failure was not reported.");
    require(rename_failed.kind ==
                    ReviewedSourceStateStoreFailureKind::RenameFailed,
            "Injected rename failure kind drifted.");
    require(read_bytes(origin) == original,
            "rename failure mutated the published destination.");
    require(file_identity(origin).inode == original_identity.inode,
            "rename failure replaced the origin inode.");
}

void test_directory_fsync_failure_is_published_uncertain() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::DirectorySync);
    const auto uncertain =
            require_arm<ReviewedSourceStateStorePublishedUncertain>(
                    publish_reviewed_source_state(second, published.observed),
                    "Directory fsync failure was flattened to a pre-commit failure.");
    require(uncertain.issue ==
                    ReviewedSourceStatePostPublicationIssue::
                            DirectorySyncUncertain,
            "Directory fsync failure issue drifted.");
    require(uncertain.failure_kind ==
                    ReviewedSourceStateStoreFailureKind::SyncFailed,
            "Directory fsync failure kind drifted.");
    require(uncertain.observed.has_value(),
            "Directory fsync failure lost the published token.");
    require(uncertain.observed->raw_contents ==
                    encode_reviewed_source_state(second),
            "Directory fsync failure lost the new state bytes.");

    const fs::path successor =
            reviewed_source_state_store_entry_path(second.package_base()) /
            uncertain.observed->leaf_name;
    require(read_bytes(successor) == encode_reviewed_source_state(second),
            "Directory fsync failure did not leave the new generation authoritative.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(second.package_base()),
            "Post directory-fsync-failure read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "New generation was not Loaded.")
                    .state == second,
            "Directory fsync failure hid the published state.");
}

void test_post_commit_verify_failure_is_published_uncertain() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::PostCommitVerify);
    const auto uncertain =
            require_arm<ReviewedSourceStateStorePublishedUncertain>(
                    publish_reviewed_source_state(second, published.observed),
                    "Post-commit verify failure was flattened.");
    require(uncertain.issue ==
                    ReviewedSourceStatePostPublicationIssue::
                            PublishedIdentityUncertain,
            "Post-commit verify issue drifted.");
    require(uncertain.observed.has_value(),
            "Post-commit verify failure lost the published token.");
    const fs::path successor =
            reviewed_source_state_store_entry_path(second.package_base()) /
            uncertain.observed->leaf_name;
    require(read_bytes(successor) == encode_reviewed_source_state(second),
            "Post-commit verify failure rolled back the new generation.");
}

void test_publication_boundary_occupancy_preserves_replacement() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string replacement = encode_reviewed_source_state(second);
    OccupyingPublication occupier{replacement};
    OccupyingPublication::instance = &occupier;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &OccupyingPublication::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_C)),
                    published.observed),
            "Occupied successor was not a conflict.");
    OccupyingPublication::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Occupied successor was not ConcurrentReplacement.");

    const fs::path package_dir =
            reviewed_source_state_store_entry_path(first.package_base());
    const fs::path successor =
            package_dir /
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    require(read_bytes(successor) == replacement,
            "A displaced B from the successor name.");
    require(read_bytes(origin_path(first.package_base())) ==
                    published.observed.raw_contents,
            "Occupied-successor race mutated the origin generation.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post occupancy read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "Replacement was not Loaded.")
                    .state == second,
            "Occupied successor did not remain authoritative.");
}

void test_publication_boundary_inode_replacement_preserves_b() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const FileIdentity original_identity =
            file_identity(origin_path(first.package_base()));
    const ReviewedSourceState replacement_state = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string replacement =
            encode_reviewed_source_state(replacement_state);
    ReplacingCurrent replacer{replacement};
    ReplacingCurrent::instance = &replacer;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &ReplacingCurrent::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_C)),
                    published.observed),
            "Replaced destination was not a conflict.");
    ReplacingCurrent::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Replaced destination was not ConcurrentReplacement.");

    const fs::path origin = origin_path(first.package_base());
    require(read_bytes(origin) == replacement,
            "A displaced B's replacement bytes from the origin name.");
    require(file_identity(origin).inode != original_identity.inode,
            "Replacement fixture did not install a new inode.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post replacement read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "Replacement was not Loaded.")
                    .state == replacement_state,
            "Destination replacement did not remain authoritative.");
    const std::string successor_leaf =
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    const fs::path successor =
            reviewed_source_state_store_entry_path(first.package_base()) /
            successor_leaf;
    if(fs::exists(successor)) {
        const auto tip = require_arm<ReviewedSourceStateStoreRead>(
                read_reviewed_source_state(first.package_base()),
                "Orphan successor changed lookup.");
        require(require_arm<ReviewedSourceStateLoaded>(
                        tip.observation, "Orphan successor became tip.")
                        .state == replacement_state,
                "Orphan successor became authoritative.");
    }
}

void test_future_schema_boundary_preserves_future_bytes() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const std::string future = "schema_version = 2\nnext_field = true\n";
    OccupyingPublication occupier{future};
    OccupyingPublication::instance = &occupier;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &OccupyingPublication::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_B)),
                    published.observed),
            "Future occupancy was not a conflict.");
    OccupyingPublication::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Future occupancy was not ConcurrentReplacement.");

    const fs::path successor =
            reviewed_source_state_store_entry_path(first.package_base()) /
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    require(read_bytes(successor) == future,
            "Future schema bytes were displaced from the successor name.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post future-occupancy read failed.");
    require(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                    current.observation),
            "Future occupancy was not the authoritative observation.");
    require(current.observed.has_value() &&
                    current.observed->raw_contents == future,
            "Future occupancy lost raw bytes.");
}

void test_cleanup_replacement_is_not_deleted() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string replacement = "replacement-cleanup-target\n";
    ReplacingCleanupTarget replacer{replacement, {}};
    ReplacingCleanupTarget::instance = &replacer;
    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::Sync);
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::BeforeCleanup,
            &ReplacingCleanupTarget::handler);
    const auto failed = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, published.observed),
            "Cleanup race was not a pre-publication failure.");
    ReplacingCleanupTarget::instance = nullptr;
    require(failed.kind == ReviewedSourceStateStoreFailureKind::SyncFailed,
            "Cleanup race flattened the injected file-sync failure.");
    require(fs::exists(replacer.surviving_path),
            "Cleanup race deleted the replacement path.");
    require(read_bytes(replacer.surviving_path) == replacement,
            "Cleanup race deleted or mutated the replacement bytes.");
    require(read_bytes(origin_path(first.package_base())) ==
                    published.observed.raw_contents,
            "Cleanup race mutated the authoritative origin.");
}

void test_missing_missing_boundary_preserves_first_writer() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_A));
    const ReviewedSourceState second = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string first_bytes = encode_reviewed_source_state(first);
    OccupyingPublication occupier{first_bytes};
    OccupyingPublication::instance = &occupier;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &OccupyingPublication::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, std::nullopt),
            "Missing/Missing occupancy was not a conflict.");
    OccupyingPublication::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Missing/Missing occupancy was not ConcurrentReplacement.");
    require(read_bytes(origin_path(first.package_base())) == first_bytes,
            "Second Missing publisher overwrote the first origin.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post Missing/Missing read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "First Missing publisher was lost.")
                    .state == first,
            "First Missing publisher did not remain authoritative.");
}

void test_loaded_loaded_boundary_preserves_newer_writer() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const ReviewedSourceState newer = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string newer_bytes = encode_reviewed_source_state(newer);
    OccupyingPublication occupier{newer_bytes};
    OccupyingPublication::instance = &occupier;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &OccupyingPublication::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_C)),
                    published.observed),
            "Loaded/Loaded occupancy was not a conflict.");
    OccupyingPublication::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Loaded/Loaded occupancy was not ConcurrentReplacement.");
    const fs::path successor =
            reviewed_source_state_store_entry_path(first.package_base()) /
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    require(read_bytes(successor) == newer_bytes,
            "Stale writer displaced the newer successor.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post Loaded/Loaded read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "Newer writer was not Loaded.")
                    .state == newer,
            "Stale writer rolled back the newer reviewed state.");
}

void test_publication_boundary_same_inode_rewrite_preserves_b() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const fs::path origin = origin_path(first.package_base());
    const FileIdentity original_identity = file_identity(origin);
    const ReviewedSourceState rewritten_state = aur_state(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            std::string(SHA1_B));
    const std::string rewritten = encode_reviewed_source_state(rewritten_state);
    RewritingCurrentInPlace rewriter{rewritten};
    RewritingCurrentInPlace::instance = &rewriter;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &RewritingCurrentInPlace::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_C)),
                    published.observed),
            "Same-inode rewrite was not a conflict.");
    RewritingCurrentInPlace::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Same-inode rewrite was not ConcurrentReplacement.");
    require(read_bytes(origin) == rewritten,
            "A displaced B's rewritten bytes from the origin name.");
    require(file_identity(origin).device == original_identity.device &&
                    file_identity(origin).inode == original_identity.inode,
            "Same-inode rewrite fixture did not keep the origin inode.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post same-inode rewrite read failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "Rewritten contents were not Loaded.")
                    .state == rewritten_state,
            "A's stale successor became authoritative.");
    require(current.observed.has_value() &&
                    current.observed->raw_contents == rewritten,
            "Lookup lost B's rewritten raw bytes.");
    const fs::path successor =
            reviewed_source_state_store_entry_path(first.package_base()) /
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    if(fs::exists(successor)) {
        require(require_arm<ReviewedSourceStateLoaded>(
                        current.observation,
                        "Orphan successor after same-inode rewrite.")
                        .state == rewritten_state,
                "Orphan successor became the chain tip.");
    }
}

void test_publication_boundary_same_inode_future_rewrite_is_not_downgraded() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const fs::path origin = origin_path(first.package_base());
    const FileIdentity original_identity = file_identity(origin);
    const std::string future = "schema_version = 2\nnext_field = true\n";
    RewritingCurrentInPlace rewriter{future};
    RewritingCurrentInPlace::instance = &rewriter;
    run_reviewed_source_state_store_race_once_for_test(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            &RewritingCurrentInPlace::handler);
    const auto conflict = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(
                    aur_state(
                            "example-base",
                            "https://aur.archlinux.org/example-base.git",
                            std::string(SHA1_B)),
                    published.observed),
            "Same-inode future rewrite was not a conflict.");
    RewritingCurrentInPlace::instance = nullptr;
    require(conflict.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Same-inode future rewrite was not ConcurrentReplacement.");
    require(read_bytes(origin) == future,
            "A displaced future-schema bytes from the origin name.");
    require(file_identity(origin).device == original_identity.device &&
                    file_identity(origin).inode == original_identity.inode,
            "Future in-place rewrite replaced the origin inode.");
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(first.package_base()),
            "Post same-inode future rewrite read failed.");
    require(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                    current.observation),
            "Future in-place rewrite was downgraded by a stale successor.");
    require(current.observed.has_value() &&
                    current.observed->raw_contents == future,
            "Future in-place rewrite lost raw bytes.");
    const fs::path successor =
            reviewed_source_state_store_entry_path(first.package_base()) /
            reviewed_source_state_store_successor_leaf(
                    2, published.observed.identity,
                    published.observed.raw_contents);
    if(fs::exists(successor)) {
        require(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                        current.observation),
                "Stale successor downgraded future-schema origin.");
    }
}

void test_internal_temp_is_not_authoritative() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");
    const fs::path package_dir =
            reviewed_source_state_store_entry_path(state.package_base());
    write_bytes(
            package_dir / "-.moguet-reviewed-source-999-1",
            "schema_version = 2\nstale_temp = true\n", 0600);
    const auto current = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Lookup with leftover temp failed.");
    require(require_arm<ReviewedSourceStateLoaded>(
                    current.observation, "Leftover temp changed authority.")
                    .state == state,
            "Internal temp was treated as authoritative.");
    require(contains_leaf(
                    regular_leaf_names(package_dir),
                    "-.moguet-reviewed-source-999-1"),
            "Lookup unlinked an unpublished temp.");
}
#endif

} // namespace

int main() {
    try {
        test_missing_lookup_does_not_create();
        test_first_create_is_0600_and_round_trips();
        test_successor_leaf_binds_inode_and_content_digest();
        test_cas_replacement_and_stale_writer();
        test_missing_create_is_no_replace();
        test_raw_byte_guard_is_not_reencoded();
        test_future_schema_is_not_overwritten();
        test_source_mismatch_is_not_loaded();
        test_invalid_and_corrupted_are_not_missing();
        test_symlink_hardlink_and_mode_are_failures();
        test_oversized_record_is_typed_failure();
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        test_fsync_and_rename_failures_preserve_destination();
        test_directory_fsync_failure_is_published_uncertain();
        test_post_commit_verify_failure_is_published_uncertain();
        test_publication_boundary_occupancy_preserves_replacement();
        test_publication_boundary_inode_replacement_preserves_b();
        test_future_schema_boundary_preserves_future_bytes();
        test_cleanup_replacement_is_not_deleted();
        test_missing_missing_boundary_preserves_first_writer();
        test_loaded_loaded_boundary_preserves_newer_writer();
        test_publication_boundary_same_inode_rewrite_preserves_b();
        test_publication_boundary_same_inode_future_rewrite_is_not_downgraded();
        test_internal_temp_is_not_authoritative();
#endif
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "reviewed source state store tests: all checks passed\n";
    return 0;
}
