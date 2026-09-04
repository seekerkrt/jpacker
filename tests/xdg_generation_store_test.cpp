#include "xdg_generation_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <variant>

namespace fs = std::filesystem;

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        std::string pattern =
            (fs::temp_directory_path() / "moguet-generation-store-XXXXXX")
                .string();
        if(::mkdtemp(pattern.data()) == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = std::move(pattern);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

void require(bool condition, std::string_view message) {
    if(!condition) throw std::runtime_error(std::string(message));
}

template <typename Arm, typename Variant>
const Arm& require_arm(const Variant& value, std::string_view message) {
    const Arm* arm = std::get_if<Arm>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

bool is_future(std::string_view document) {
    return document.starts_with("schema=2");
}

struct StoreFixture {
    TemporaryDirectory temporary;
    fs::path state_home;
    XdgGenerationStoreConfiguration configuration;

    explicit StoreFixture(std::size_t max_record_bytes = 1024)
        : state_home(temporary.path() / "state"),
          configuration{
              xdg_paths::resolve_devel_build_provenance(
                  xdg_paths::EnvironmentSnapshot{
                      .xdg_config_home = std::nullopt,
                      .xdg_state_home = state_home.string(),
                      .xdg_cache_home = std::nullopt,
                      .home = (temporary.path() / "home").string()}),
              "unit-a", "-.moguet-generation-test-", max_record_bytes,
              &is_future} {
        fs::create_directory(state_home);
        fs::create_directory(temporary.path() / "home");
        reset_xdg_generation_store_test_hooks();
    }

    fs::path unit_path() const {
        return xdg_generation_store_entry_path(configuration);
    }
};

void write_bytes(const fs::path& path, std::string_view bytes, mode_t mode) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) throw std::runtime_error("fixture open failed");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if(!output || ::chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error("fixture write failed");
    }
}

void test_read_missing_does_not_create() {
    StoreFixture fixture;
    require_arm<XdgGenerationStoreMissing>(
        read_xdg_generation_store(fixture.configuration),
        "missing lookup was not Missing");
    require(!fs::exists(fixture.configuration.paths.directory),
            "missing lookup created the store");
}

void test_publish_replace_and_stale_cas() {
    StoreFixture fixture;
    const auto first = require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            fixture.configuration, "schema=1\nvalue=a\n", std::nullopt),
        "first publication failed");
    require(first.observed.generation == 1 &&
                first.observed.leaf_name == "1.toml" &&
                first.observed.raw_contents == "schema=1\nvalue=a\n",
            "first publication token drifted");

    const XdgGenerationObservedRecord stale = first.observed;
    const auto second = require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            fixture.configuration, "schema=1\nvalue=b\n", first.observed),
        "replacement publication failed");
    require(second.observed.generation == 2,
            "replacement did not advance the store-owned generation");
    const auto conflict = require_arm<XdgGenerationStoreFailure>(
        publish_xdg_generation_store(
            fixture.configuration, "schema=1\nvalue=c\n", stale),
        "stale writer was not rejected");
    require(conflict.kind ==
                XdgGenerationStoreFailureKind::ConcurrentReplacement,
            "stale writer returned the wrong failure");
    const auto loaded = require_arm<XdgGenerationStoreLoaded>(
        read_xdg_generation_store(fixture.configuration),
        "current record was not loaded");
    require(loaded.observed == second.observed,
            "stale writer rolled back the newer generation");
}

void test_bounds_and_precommit_cleanup() {
    StoreFixture exact(32);
    const std::string exact_bytes(32, 'x');
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            exact.configuration, exact_bytes, std::nullopt),
        "exact-limit record was rejected");

    StoreFixture over(32);
    const auto too_large = require_arm<XdgGenerationStoreFailure>(
        publish_xdg_generation_store(
            over.configuration, std::string(33, 'x'), std::nullopt),
        "over-limit publication was not rejected");
    require(too_large.kind == XdgGenerationStoreFailureKind::RecordTooLarge &&
                !fs::exists(over.configuration.paths.directory),
            "over-limit publication created state");

    StoreFixture failed;
    fail_next_xdg_generation_store_operation_for_test(
        XdgGenerationStoreTestFailurePoint::Sync);
    const auto sync_failure = require_arm<XdgGenerationStoreFailure>(
        publish_xdg_generation_store(
            failed.configuration, "schema=1\n", std::nullopt),
        "precommit sync failure was not definite");
    require(sync_failure.kind == XdgGenerationStoreFailureKind::SyncFailed,
            "precommit sync failure kind drifted");
    require(fs::exists(failed.unit_path()) &&
                fs::is_empty(failed.unit_path()),
            "unnamed precommit artifact became visible");
}

void test_future_and_unsafe_history() {
    StoreFixture future;
    const auto first = require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            future.configuration, "schema=2\nfuture=true\n", std::nullopt),
        "future fixture publication failed");
    const auto refused = require_arm<XdgGenerationStoreFailure>(
        publish_xdg_generation_store(
            future.configuration, "schema=1\n", first.observed),
        "future record was overwritten");
    require(refused.kind ==
                XdgGenerationStoreFailureKind::FutureSchemaOverwriteRefused,
            "future overwrite refusal was flattened");

    StoreFixture malformed;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            malformed.configuration, "schema=1\n", std::nullopt),
        "malformed-history seed failed");
    write_bytes(malformed.unit_path() / "unexpected", "x", 0600);
    const auto unsafe = require_arm<XdgGenerationStoreUnsafeHistory>(
        read_xdg_generation_store(malformed.configuration),
        "unrecognized history was not unsafe");
    require(unsafe.issue ==
                XdgGenerationStoreHistoryIssue::UnrecognizedManagedEntry,
            "unrecognized history kind drifted");

    StoreFixture gap;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            gap.configuration, "schema=1\n", std::nullopt),
        "gap seed failed");
    write_bytes(
        gap.unit_path() /
            ("3.0-0-0-0." + std::string(64, 'a') + ".toml"),
        "schema=1\n", 0600);
    const auto gap_result = require_arm<XdgGenerationStoreUnsafeHistory>(
        read_xdg_generation_store(gap.configuration),
        "generation gap was not unsafe");
    require(gap_result.issue == XdgGenerationStoreHistoryIssue::ChainGap,
            "generation gap kind drifted");
}

void test_file_safety_and_nofollow() {
    StoreFixture symlink;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            symlink.configuration, "schema=1\n", std::nullopt),
        "symlink seed failed");
    const fs::path origin = symlink.unit_path() / "1.toml";
    fs::remove(origin);
    fs::create_symlink(symlink.temporary.path() / "outside", origin);
    const auto symlink_failure = require_arm<XdgGenerationStoreFailure>(
        read_xdg_generation_store(symlink.configuration),
        "symlink was followed or flattened");
    require(symlink_failure.kind ==
                XdgGenerationStoreFailureKind::UnsupportedFileType,
            "symlink failure kind drifted");

    StoreFixture mode;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            mode.configuration, "schema=1\n", std::nullopt),
        "mode seed failed");
    require(::chmod((mode.unit_path() / "1.toml").c_str(), 0644) == 0,
            "chmod fixture failed");
    require(require_arm<XdgGenerationStoreFailure>(
                read_xdg_generation_store(mode.configuration),
                "unsafe mode was flattened")
                    .kind == XdgGenerationStoreFailureKind::UnsafePermissions,
            "unsafe mode failure kind drifted");

    StoreFixture hardlink;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            hardlink.configuration, "schema=1\n", std::nullopt),
        "hardlink seed failed");
    require(::link(
                (hardlink.unit_path() / "1.toml").c_str(),
                (hardlink.temporary.path() / "alias").c_str()) == 0,
            "hardlink fixture failed");
    require(require_arm<XdgGenerationStoreFailure>(
                read_xdg_generation_store(hardlink.configuration),
                "hardlink was flattened")
                    .kind == XdgGenerationStoreFailureKind::MultipleHardLinks,
            "hardlink failure kind drifted");

    StoreFixture fifo;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            fifo.configuration, "schema=1\n", std::nullopt),
        "FIFO seed failed");
    fs::remove(fifo.unit_path() / "1.toml");
    require(::mkfifo((fifo.unit_path() / "1.toml").c_str(), 0600) == 0,
            "FIFO fixture failed");
    require(require_arm<XdgGenerationStoreFailure>(
                read_xdg_generation_store(fifo.configuration),
                "FIFO was flattened")
                    .kind ==
                XdgGenerationStoreFailureKind::UnsupportedFileType,
            "FIFO failure kind drifted");

    StoreFixture directory;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            directory.configuration, "schema=1\n", std::nullopt),
        "directory seed failed");
    fs::remove(directory.unit_path() / "1.toml");
    fs::create_directory(directory.unit_path() / "1.toml");
    require(require_arm<XdgGenerationStoreFailure>(
                read_xdg_generation_store(directory.configuration),
                "record directory was flattened")
                    .kind ==
                XdgGenerationStoreFailureKind::UnsupportedFileType,
            "record directory failure kind drifted");
}

void test_same_generation_fork_is_unsafe() {
    StoreFixture fixture;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            fixture.configuration, "schema=1\n", std::nullopt),
        "fork seed failed");
    write_bytes(
        fixture.unit_path() /
            ("2.0-0-0-0." + std::string(64, 'a') + ".toml"),
        "schema=1\n", 0600);
    write_bytes(
        fixture.unit_path() /
            ("2.0-1-0-0." + std::string(64, 'a') + ".toml"),
        "schema=1\n", 0600);
    const auto fork = require_arm<XdgGenerationStoreUnsafeHistory>(
        read_xdg_generation_store(fixture.configuration),
        "same-generation fork was not unsafe");
    require(fork.issue == XdgGenerationStoreHistoryIssue::ForkDetected,
            "fork history kind drifted");
}

void test_post_commit_uncertainty_and_path_validation() {
    StoreFixture fixture;
    fail_next_xdg_generation_store_operation_for_test(
        XdgGenerationStoreTestFailurePoint::DirectorySync);
    const auto uncertain =
        require_arm<XdgGenerationStorePublishedUncertain>(
            publish_xdg_generation_store(
                fixture.configuration, "schema=1\n", std::nullopt),
            "directory fsync failure was flattened");
    require(uncertain.issue ==
                    XdgGenerationPostPublicationIssue::DirectorySyncUncertain &&
                uncertain.observed.has_value() &&
                uncertain.observed->generation == 1,
            "post-commit uncertainty lost the committed token");

    StoreFixture close_fixture;
    fail_next_xdg_generation_store_operation_for_test(
        XdgGenerationStoreTestFailurePoint::Close);
    const auto close_uncertain =
        require_arm<XdgGenerationStorePublishedUncertain>(
            publish_xdg_generation_store(
                close_fixture.configuration, "schema=1\n", std::nullopt),
            "post-commit close failure was flattened");
    require(close_uncertain.failure_kind ==
                    XdgGenerationStoreFailureKind::CloseFailed &&
                close_uncertain.observed.has_value(),
            "post-commit close failure lost committed authority");

    StoreFixture traversal;
    traversal.configuration.unit_leaf = "../escape";
    bool rejected = false;
    try {
        static_cast<void>(
            read_xdg_generation_store(traversal.configuration));
    } catch(const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && !fs::exists(traversal.state_home / "escape"),
            "path traversal reached the filesystem");
}

void test_record_owner_mismatch_is_typed() {
    StoreFixture fixture;
    require_arm<XdgGenerationStorePublished>(
        publish_xdg_generation_store(
            fixture.configuration, "schema=1\n", std::nullopt),
        "owner-mismatch seed failed");
    fail_next_xdg_generation_store_operation_for_test(
        XdgGenerationStoreTestFailurePoint::RecordOwnership);
    const auto mismatch = require_arm<XdgGenerationStoreFailure>(
        read_xdg_generation_store(fixture.configuration),
        "record owner mismatch was flattened");
    require(mismatch.kind ==
                XdgGenerationStoreFailureKind::OwnershipMismatch,
            "record owner mismatch kind drifted");
}

} // namespace

int main() {
    try {
        test_read_missing_does_not_create();
        test_publish_replace_and_stale_cas();
        test_bounds_and_precommit_cleanup();
        test_future_and_unsafe_history();
        test_file_safety_and_nofollow();
        test_same_generation_fork_is_unsafe();
        test_post_commit_uncertainty_and_path_validation();
        test_record_owner_mismatch_is_typed();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "XDG generation store tests passed\n";
    return 0;
}
