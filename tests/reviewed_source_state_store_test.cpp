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

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

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

    const fs::path entry = reviewed_source_state_store_entry_path(
            state.package_base());
    require(file_mode(entry) == 0600, "Published file mode was not 0600.");
    require(file_mode(entry.parent_path()) == 0700,
            "Store directory mode was not 0700.");
    require(read_bytes(entry) == published.observed.raw_contents,
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

    const auto stale = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(first, published_first.observed),
            "Stale writer was not a store failure.");
    require(stale.kind ==
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
            "Stale writer was not ConcurrentReplacement.");

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
}

void test_raw_byte_guard_is_not_reencoded() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Canonical publication failed.");

    const fs::path entry = reviewed_source_state_store_entry_path(
            state.package_base());
    const std::string equivalent =
            "reviewed_commit = \"" + std::string(SHA1_A) +
            "\"\n"
            "# semantically equal, different bytes\n"
            "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
            "package_base = \"example-base\"\n"
            "source_kind = \"aur\"\n"
            "schema_version = 1\n";
    write_bytes(entry, equivalent, 0600);

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
    require(read_bytes(entry) == equivalent,
            "Re-encode publication overwrote observed bytes.");
}

void test_future_schema_is_not_overwritten() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");

    const fs::path entry = reviewed_source_state_store_entry_path(
            state.package_base());
    const std::string future = "schema_version = 2\nnext_field = true\n";
    write_bytes(entry, future, 0600);

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
    require(read_bytes(entry) == future,
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
            "Different remote in foo.toml was treated as Loaded.");
}

void test_invalid_and_corrupted_are_not_missing() {
    StoreTestHome home;
    const ReviewedSourceState state = aur_state();
    require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(state, std::nullopt),
            "Seed publication failed.");
    const fs::path entry = reviewed_source_state_store_entry_path(
            state.package_base());

    write_bytes(entry, "schema_version = 1\n", 0600);
    const auto invalid = require_arm<ReviewedSourceStateStoreRead>(
            read_reviewed_source_state(state.package_base()),
            "Invalid document read failed.");
    require(std::holds_alternative<ReviewedSourceStateInvalid>(
                    invalid.observation),
            "Invalid current-schema document was flattened.");

    write_bytes(entry, "schema_version = [\n", 0600);
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
    const fs::path entry = reviewed_source_state_store_entry_path(
            state.package_base());
    const fs::path sibling = entry.parent_path() / "other.toml";

    fs::remove(entry);
    fs::create_symlink(sibling, entry);
    const auto symlink = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "Symlink was not a store failure.");
    require(symlink.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
            "Symlink was flattened to Missing.");
    require(symlink.observed_file_type == fs::file_type::symlink,
            "Symlink type was lost.");
    fs::remove(entry);

    write_bytes(entry, encode_reviewed_source_state(state), 0600);
    write_bytes(sibling, "other", 0600);
    if(::link(sibling.c_str(), (entry.string() + ".link").c_str()) != 0) {
        throw std::runtime_error("Failed to create hardlink fixture.");
    }
    fs::remove(entry);
    if(::link((entry.string() + ".link").c_str(), entry.c_str()) != 0) {
        throw std::runtime_error("Failed to install hardlinked destination.");
    }
    const auto hardlink = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "Hardlink was not a store failure.");
    require(hardlink.kind ==
                    ReviewedSourceStateStoreFailureKind::MultipleHardLinks,
            "Hardlink count violation was flattened.");
    fs::remove(entry);
    fs::remove(entry.string() + ".link");
    fs::remove(sibling);

    write_bytes(entry, encode_reviewed_source_state(state), 0644);
    const auto mode = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "0644 file was not a store failure.");
    require(mode.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsafePermissions,
            "Unsafe mode was flattened to Missing.");

    fs::remove(entry);
    if(::mkfifo(entry.c_str(), 0600) != 0) {
        throw std::runtime_error("Failed to create FIFO fixture.");
    }
    const auto fifo = require_arm<ReviewedSourceStateStoreFailure>(
            read_reviewed_source_state(state.package_base()),
            "FIFO was not a store failure.");
    require(fifo.kind ==
                    ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
            "FIFO was flattened to Missing.");
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
void test_fsync_and_rename_failures_preserve_destination() {
    StoreTestHome home;
    const ReviewedSourceState first = aur_state();
    const auto published = require_arm<ReviewedSourceStateStorePublished>(
            publish_reviewed_source_state(first, std::nullopt),
            "Seed publication failed.");
    const fs::path entry = reviewed_source_state_store_entry_path(
            first.package_base());
    const std::string original = read_bytes(entry);

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
    require(read_bytes(entry) == original,
            "fsync failure mutated the published destination.");

    fail_next_reviewed_source_state_store_operation_for_test(
            ReviewedSourceStateStoreTestFailurePoint::Rename);
    const auto rename_failed = require_arm<ReviewedSourceStateStoreFailure>(
            publish_reviewed_source_state(second, published.observed),
            "Injected rename failure was not reported.");
    require(rename_failed.kind ==
                    ReviewedSourceStateStoreFailureKind::RenameFailed,
            "Injected rename failure kind drifted.");
    require(read_bytes(entry) == original,
            "rename failure mutated the published destination.");
}
#endif

} // namespace

int main() {
    try {
        test_missing_lookup_does_not_create();
        test_first_create_is_0600_and_round_trips();
        test_cas_replacement_and_stale_writer();
        test_missing_create_is_no_replace();
        test_raw_byte_guard_is_not_reencoded();
        test_future_schema_is_not_overwritten();
        test_source_mismatch_is_not_loaded();
        test_invalid_and_corrupted_are_not_missing();
        test_symlink_hardlink_and_mode_are_failures();
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        test_fsync_and_rename_failures_preserve_destination();
#endif
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "reviewed source state store tests: all checks passed\n";
    return 0;
}
