#include "artifact_workspace.hpp"

#include "package_identifier.hpp"
#include "trusted_cache_test_support.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <linux/openat2.h>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<ArtifactWorkspace>);
static_assert(!std::is_copy_assignable_v<ArtifactWorkspace>);
static_assert(std::is_move_constructible_v<ArtifactWorkspace>);
static_assert(std::is_nothrow_move_constructible_v<ArtifactWorkspace>);
static_assert(!std::is_move_assignable_v<ArtifactWorkspace>);
static_assert(std::is_nothrow_destructible_v<ArtifactWorkspace>);
static_assert(
        !std::is_constructible_v<ArtifactWorkspace, std::filesystem::path>);

static_assert(!std::is_copy_constructible_v<ValidatedPackageArtifactPath>);
static_assert(!std::is_copy_assignable_v<ValidatedPackageArtifactPath>);
static_assert(std::is_move_constructible_v<ValidatedPackageArtifactPath>);
static_assert(
        std::is_nothrow_move_constructible_v<ValidatedPackageArtifactPath>);
static_assert(!std::is_move_assignable_v<ValidatedPackageArtifactPath>);
static_assert(std::is_nothrow_destructible_v<ValidatedPackageArtifactPath>);
static_assert(!std::is_constructible_v<
              ValidatedPackageArtifactPath, std::filesystem::path>);
static_assert(!std::is_constructible_v<
              ExpectedPackageArtifactPath, std::filesystem::path>);
static_assert(!std::is_copy_constructible_v<ArtifactMakepkgContext>);
static_assert(!std::is_copy_assignable_v<ArtifactMakepkgContext>);
static_assert(std::is_move_constructible_v<ArtifactMakepkgContext>);
static_assert(std::is_nothrow_move_constructible_v<ArtifactMakepkgContext>);
static_assert(!std::is_move_assignable_v<ArtifactMakepkgContext>);
static_assert(std::is_nothrow_destructible_v<ArtifactMakepkgContext>);
static_assert(!std::is_copy_constructible_v<ValidatedPrivateCacheRoot>);
static_assert(!std::is_copy_assignable_v<ValidatedPrivateCacheRoot>);
static_assert(std::is_move_constructible_v<ValidatedPrivateCacheRoot>);
static_assert(
        std::is_nothrow_move_constructible_v<ValidatedPrivateCacheRoot>);
static_assert(!std::is_move_assignable_v<ValidatedPrivateCacheRoot>);
static_assert(std::is_nothrow_destructible_v<ValidatedPrivateCacheRoot>);
static_assert(!std::is_constructible_v<
              ValidatedPrivateCacheRoot, ValidatedCacheRoot>);

namespace {

namespace fs = std::filesystem;

constexpr const char* ARTIFACT_WORKSPACE_PREFIX = ".artifact-workspace~-";
constexpr mode_t      ARTIFACT_WORKSPACE_MODE = 0700;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_equal(
        const std::string& context, const std::string& actual,
        const std::string& expected) {
    if(actual == expected) return;
    throw std::runtime_error(
            context + ": expected [" + expected + "], actual [" + actual + "]");
}

template <typename Callable>
std::string expect_runtime_error(
        Callable&& callable, const std::string& context,
        const std::string& expected_fragment = "") {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::runtime_error& error) {
        if(!expected_fragment.empty() &&
           std::string(error.what()).find(expected_fragment) == std::string::npos) {
            throw std::runtime_error(
                    context + ": unexpected error [" + error.what() + "]");
        }
        return error.what();
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected runtime_error");
}

class ScopedUmask final {
    mode_t previous_mode_;

public:
    explicit ScopedUmask(mode_t mode) noexcept : previous_mode_(umask(mode)) {
    }

    ScopedUmask(const ScopedUmask&) = delete;
    ScopedUmask& operator=(const ScopedUmask&) = delete;

    ~ScopedUmask() noexcept {
        static_cast<void>(umask(previous_mode_));
    }
};

class ScopedCleanupChildOpen final {
public:
    explicit ScopedCleanupChildOpen(
            ArtifactWorkspaceCleanupChildOpenForTest open_child) noexcept {
        set_artifact_workspace_cleanup_child_open_for_test(open_child);
    }

    ScopedCleanupChildOpen(const ScopedCleanupChildOpen&) = delete;
    ScopedCleanupChildOpen& operator=(const ScopedCleanupChildOpen&) = delete;

    ~ScopedCleanupChildOpen() noexcept {
        set_artifact_workspace_cleanup_child_open_for_test(nullptr);
    }
};

void write_file(const fs::path& path, const std::string& contents = "fixture") {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file) {
        throw std::runtime_error(
                "Failed to create test fixture file: " + path.string());
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if(!file) {
        throw std::runtime_error(
                "Failed to finish test fixture file: " + path.string());
    }
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) {
        throw std::runtime_error(
                "Failed to read test fixture file: " + path.string());
    }
    return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
}

struct stat require_path_status(const fs::path& path) {
    struct stat status {};
    if(lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error(
                "Failed to inspect test fixture path: " + path.string());
    }
    return status;
}

bool path_exists_no_follow(const fs::path& path) {
    struct stat status {};
    if(lstat(path.c_str(), &status) == 0) return true;
    if(errno == ENOENT) return false;
    throw std::runtime_error(
            "Failed to inspect test fixture path: " + path.string());
}

class ScopedEnvironmentVariable final {
    std::string                key_;
    std::optional<std::string> previous_value_;

public:
    ScopedEnvironmentVariable(
            std::string key, const std::optional<std::string>& value)
        : key_(std::move(key)) {
        const char* previous = std::getenv(key_.c_str());
        if(previous != nullptr) previous_value_ = previous;

        int result = value.has_value()
                           ? setenv(key_.c_str(), value->c_str(), 1)
                           : unsetenv(key_.c_str());
        if(result != 0) {
            throw std::runtime_error(
                    "Failed to set test environment variable: " + key_);
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(
            const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_value_.has_value())
            static_cast<void>(setenv(
                    key_.c_str(), previous_value_->c_str(), 1));
        else
            static_cast<void>(unsetenv(key_.c_str()));
    }
};

class ScopedStreamCapture final {
    std::ostream&       stream_;
    std::ostringstream buffer_;
    std::streambuf*     previous_buffer_ = nullptr;

public:
    explicit ScopedStreamCapture(std::ostream& stream)
        : stream_(stream), previous_buffer_(stream_.rdbuf(buffer_.rdbuf())) {
    }

    ScopedStreamCapture(const ScopedStreamCapture&) = delete;
    ScopedStreamCapture& operator=(const ScopedStreamCapture&) = delete;

    ~ScopedStreamCapture() noexcept {
        stream_.rdbuf(previous_buffer_);
    }

    std::string str() const {
        return buffer_.str();
    }
};

class TestEnvironment final {
    fs::path path_;
    fs::path packagelist_output_file_;
    fs::path command_log_;
    fs::path argv_log_;
    fs::path environment_log_;
    fs::path cwd_log_;
    std::vector<std::unique_ptr<ScopedEnvironmentVariable>> variables_;

    void set_variable(
            const std::string& key,
            const std::optional<std::string>& value) {
        variables_.push_back(
                std::make_unique<ScopedEnvironmentVariable>(key, value));
    }

public:
    explicit TestEnvironment(const fs::path& makepkg_stub_directory) {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "moguet-artifact-workspace-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create artifact workspace test directory.");
        }
        path_ = created_path;

        try {
            fs::path absolute_stub_directory =
                    fs::absolute(makepkg_stub_directory).lexically_normal();
            expect(
                    fs::is_regular_file(absolute_stub_directory / "makepkg"),
                    "makepkg test stub is missing");

            packagelist_output_file_ = path_ / "packagelist-output";
            command_log_ = path_ / "command-log";
            argv_log_ = path_ / "argv-log";
            environment_log_ = path_ / "environment-log";
            cwd_log_ = path_ / "cwd-log";
            write_file(packagelist_output_file_, "");
            write_file(command_log_, "");
            write_file(argv_log_, "");
            write_file(environment_log_, "");
            write_file(cwd_log_, "");
            fs::create_directory(path_ / "cache-home");
            fs::permissions(
                    path_ / "cache-home", fs::perms::owner_all,
                    fs::perm_options::replace);

            std::string command_path = absolute_stub_directory.string();
            const char* previous_path = std::getenv("PATH");
            if(previous_path != nullptr && std::string(previous_path).length() > 0)
                command_path += ":" + std::string(previous_path);

            set_variable("XDG_CACHE_HOME", (path_ / "cache-home").string());
            set_variable("PATH", command_path);
            set_variable("PKGDEST", std::nullopt);
            set_variable("FIRST", std::nullopt);
            set_variable("EMPTY", std::nullopt);
            set_variable("DUP", std::nullopt);
            set_variable("MOGUET_TEST_COMMAND_LOG", command_log_.string());
            set_variable("MOGUET_TEST_MAKEPKG_ARGV_LOG", argv_log_.string());
            set_variable(
                    "MOGUET_TEST_MAKEPKG_ENV_LOG",
                    environment_log_.string());
            set_variable(
                    "MOGUET_TEST_MAKEPKG_ENV_KEYS",
                    std::string("FIRST EMPTY DUP PKGDEST"));
            set_variable("MOGUET_TEST_MAKEPKG_CWD_LOG", cwd_log_.string());
            set_variable(
                    "MOGUET_TEST_MAKEPKG_PACKAGELIST_OUTPUT_FILE",
                    packagelist_output_file_.string());
            set_variable(
                    "MOGUET_TEST_MAKEPKG_PACKAGELIST_EXIT_CODE",
                    std::string("0"));
            set_variable("MOGUET_TEST_MAKEPKG_EXIT_CODE", std::string("0"));
        } catch(...) {
            variables_.clear();
            std::error_code error;
            fs::remove_all(path_, error);
            throw;
        }
    }

    TestEnvironment(const TestEnvironment&) = delete;
    TestEnvironment& operator=(const TestEnvironment&) = delete;

    ~TestEnvironment() noexcept {
        variables_.clear();
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

    const fs::path& packagelist_output_file() const noexcept {
        return packagelist_output_file_;
    }

    const fs::path& command_log() const noexcept {
        return command_log_;
    }

    const fs::path& argv_log() const noexcept {
        return argv_log_;
    }

    const fs::path& environment_log() const noexcept {
        return environment_log_;
    }

    const fs::path& cwd_log() const noexcept {
        return cwd_log_;
    }

    void clear_makepkg_logs() const {
        write_file(command_log_, "");
        write_file(argv_log_, "");
        write_file(environment_log_, "");
        write_file(cwd_log_, "");
    }
};

enum class WorkspaceCreationObserverAction {
    ThrowOnly,
    LeaveNonEmpty,
    ReplaceWithSymlink,
    MoveOutside,
};

WorkspaceCreationObserverAction g_workspace_creation_observer_action =
        WorkspaceCreationObserverAction::ThrowOnly;
fs::path g_workspace_creation_observed_path;
fs::path g_workspace_creation_auxiliary_path;
bool     g_workspace_creation_observer_called = false;

void fail_workspace_creation_for_test(const fs::path& workspace_path) {
    g_workspace_creation_observer_called = true;
    g_workspace_creation_observed_path = workspace_path;

    switch(g_workspace_creation_observer_action) {
    case WorkspaceCreationObserverAction::ThrowOnly:
        break;
    case WorkspaceCreationObserverAction::LeaveNonEmpty:
        write_file(workspace_path / "rollback-blocker");
        break;
    case WorkspaceCreationObserverAction::ReplaceWithSymlink: {
        fs::path moved_original = workspace_path;
        moved_original += ".rollback-original";
        fs::rename(workspace_path, moved_original);
        write_file(moved_original / "original-sentinel");
        fs::create_directory_symlink(
                g_workspace_creation_auxiliary_path, workspace_path);
        break;
    }
    case WorkspaceCreationObserverAction::MoveOutside:
        fs::rename(workspace_path, g_workspace_creation_auxiliary_path);
        write_file(
                g_workspace_creation_auxiliary_path / "original-sentinel");
        break;
    }

    throw std::runtime_error(
            "Injected artifact workspace creation failure.");
}

class ScopedArtifactWorkspaceCreationObserver final {
public:
    explicit ScopedArtifactWorkspaceCreationObserver(
            ArtifactWorkspaceCreationObserverForTest observer) {
        set_artifact_workspace_creation_observer_for_test(observer);
    }

    ScopedArtifactWorkspaceCreationObserver(
            const ScopedArtifactWorkspaceCreationObserver&) = delete;
    ScopedArtifactWorkspaceCreationObserver& operator=(
            const ScopedArtifactWorkspaceCreationObserver&) = delete;

    ~ScopedArtifactWorkspaceCreationObserver() noexcept {
        set_artifact_workspace_creation_observer_for_test(nullptr);
    }
};

fs::path g_cleanup_observer_target;
fs::path g_cleanup_observer_destination;
bool     g_cleanup_observer_called = false;

void replace_cleanup_file_with_symlink_for_test(
        const fs::path& workspace_path) {
    g_cleanup_observer_called = true;
    const fs::path target = workspace_path / g_cleanup_observer_target;
    fs::remove(target);
    fs::create_symlink(g_cleanup_observer_destination, target);
}

void move_cleanup_directory_outside_for_test(
        const fs::path& workspace_path) {
    g_cleanup_observer_called = true;
    const fs::path target = workspace_path / g_cleanup_observer_target;
    fs::rename(target, g_cleanup_observer_destination);
    fs::create_directory_symlink(g_cleanup_observer_destination, target);
}

void move_cleanup_workspace_outside_for_test(
        const fs::path& workspace_path) {
    g_cleanup_observer_called = true;
    fs::rename(workspace_path, g_cleanup_observer_destination);
}

class ScopedArtifactWorkspaceCleanupObserver final {
public:
    explicit ScopedArtifactWorkspaceCleanupObserver(
            ArtifactWorkspaceCleanupPreDeleteObserverForTest observer) {
        set_artifact_workspace_cleanup_pre_delete_observer_for_test(observer);
    }

    ScopedArtifactWorkspaceCleanupObserver(
            const ScopedArtifactWorkspaceCleanupObserver&) = delete;
    ScopedArtifactWorkspaceCleanupObserver& operator=(
            const ScopedArtifactWorkspaceCleanupObserver&) = delete;

    ~ScopedArtifactWorkspaceCleanupObserver() noexcept {
        set_artifact_workspace_cleanup_pre_delete_observer_for_test(nullptr);
    }
};

ValidatedCachePath prepare_checkout(const ValidatedCacheRoot& root) {
    fs::path checkout_path = root.path() / "source-checkout";
    fs::create_directory(checkout_path);
    return require_trusted_cache_path(
            root, checkout_path, CachePathRequirement::ExistingDirectory);
}

std::uintmax_t different_user_id() {
    std::uintmax_t current_user = static_cast<std::uintmax_t>(geteuid());
    return current_user == 0 ? 1 : 0;
}

ArtifactWorkspace create_test_artifact_workspace(
        const ValidatedCacheRoot& expected_root) {
    ValidatedPrivateCacheRoot root =
            prepare_private_trusted_cache_root(expected_root);
    expect(
            root.canonical_path() == expected_root.canonical_path(),
            "Private and trusted cache root paths differ");
    return create_artifact_workspace(std::move(root));
}

ExpectedPackageArtifactPath declare_expected_artifact(
        const ArtifactWorkspace& workspace, const std::string& leaf_name) {
    return validate_makepkg_packagelist_output(
            workspace, (workspace.path() / leaf_name).string() + "\n");
}

fs::path move_workspace_aside(
        ArtifactWorkspace& workspace, const std::string& suffix) {
    fs::path moved_path = workspace.path();
    moved_path += suffix;
    fs::rename(workspace.path(), moved_path);
    return moved_path;
}

void test_workspace_creation_contract(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path&    workspace_path = workspace.path();
    struct stat        status = require_path_status(workspace_path);

    expect(workspace_path.is_absolute(), "Workspace path is not absolute");
    expect(
            workspace_path.parent_path() == root.canonical_path(),
            "Workspace is not a direct child of the trusted cache root");
    expect(
            workspace.canonical_path() == workspace_path,
            "Workspace path is not canonical");
    expect(
            workspace_path.filename().string().starts_with(
                    ARTIFACT_WORKSPACE_PREFIX),
            "Workspace namespace prefix differs");
    expect(
            !is_valid_package_name(workspace_path.filename().string()),
            "Workspace namespace is a valid package name");
    expect(S_ISDIR(status.st_mode), "Workspace is not a directory");
    expect(!S_ISLNK(status.st_mode), "Workspace is a symlink");
    expect(
            (status.st_mode & 07777) == ARTIFACT_WORKSPACE_MODE,
            "Workspace mode is not 0700");
    expect(
            status.st_uid == geteuid(),
            "Workspace owner differs from the effective user");
    workspace.require_unchanged_identity();
}

void test_fresh_workspace_creation(const ValidatedCacheRoot& root) {
    ArtifactWorkspace first = create_test_artifact_workspace(root);
    ArtifactWorkspace second = create_test_artifact_workspace(root);
    expect(first.path() != second.path(), "Fresh workspaces reused one path");
    expect(fs::exists(first.path()), "First fresh workspace is missing");
    expect(fs::exists(second.path()), "Second fresh workspace is missing");
}

mode_t read_process_umask() noexcept {
    const mode_t current_mode = umask(0);
    static_cast<void>(umask(current_mode));
    return current_mode;
}

void set_path_mode(const fs::path& path, mode_t mode) {
    if(chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error(
                "Failed to set test fixture mode: " + path.string());
    }
}

void test_private_cache_root_umask_matrix(const fs::path& test_root) {
    const std::vector<mode_t> masks = {0022, 0077, 0002};
    for(mode_t mask : masks) {
        const mode_t original_mask = read_process_umask();
        {
            fs::path cache_home =
                    test_root / ("private-root-umask-" +
                                 std::to_string(mask));
            fs::create_directory(cache_home);
            set_path_mode(cache_home, 0700);
            ScopedEnvironmentVariable cache_home_environment(
                    "XDG_CACHE_HOME", cache_home.string());
            ScopedUmask scoped_umask(mask);
            ValidatedCacheRoot trusted_root =
                    prepare_test_trusted_cache_root();
            ValidatedPrivateCacheRoot root =
                    prepare_private_trusted_cache_root(trusted_root);
            const struct stat status = require_path_status(root.path());
            expect(
                    (status.st_mode & 07777) == 0700,
                    "New private cache root mode depends on umask");
            root.require_unchanged_identity();
        }
        expect(
                read_process_umask() == original_mask,
                "Private cache root umask test did not restore the process umask");
    }
}

void test_existing_private_cache_root_modes(const fs::path& test_root) {
    const std::vector<std::pair<mode_t, bool>> cases = {
            {0700, true}, {0755, true}, {0775, false}, {01775, false}};
    for(const auto& [mode, should_accept] : cases) {
        fs::path cache_home =
                test_root / ("private-root-existing-" +
                             std::to_string(mode));
        fs::path root_path = cache_home / "moguet";
        fs::create_directories(root_path);
        set_path_mode(cache_home, 0700);
        set_path_mode(root_path, mode);
        ScopedEnvironmentVariable cache_home_environment(
                "XDG_CACHE_HOME", cache_home.string());

        if(should_accept) {
            ValidatedCacheRoot trusted_root =
                    prepare_test_trusted_cache_root();
            ValidatedPrivateCacheRoot root =
                    prepare_private_trusted_cache_root(trusted_root);
            root.require_unchanged_identity();
        } else {
            expect_runtime_error(
                    []() {
                        static_cast<void>(
                                prepare_test_trusted_cache_root());
                    },
                    "unsafe existing private cache root mode",
                    "permissions are unsafe");
            expect(
                    (require_path_status(root_path).st_mode & 07777) == mode,
                    "Private cache root factory silently changed an unsafe mode");
        }
    }
}

void test_private_cache_root_wrong_owner_seam(const fs::path& test_root) {
    fs::path cache_home = test_root / "private-root-wrong-owner";
    fs::create_directory(cache_home);
    set_path_mode(cache_home, 0700);
    ScopedEnvironmentVariable cache_home_environment(
            "XDG_CACHE_HOME", cache_home.string());
    ValidatedCacheRoot trusted_root = prepare_test_trusted_cache_root();
    ValidatedPrivateCacheRoot root =
            prepare_private_trusted_cache_root(trusted_root);
    expect_runtime_error(
            [&root]() {
                require_private_cache_root_identity_for_test(
                        root, different_user_id());
            },
            "private cache root owner mismatch", "owner");
}

void test_private_cache_root_symlink_rejected(const fs::path& test_root) {
    fs::path cache_home = test_root / "private-root-symlink";
    fs::path target = test_root / "private-root-symlink-target";
    fs::create_directory(cache_home);
    set_path_mode(cache_home, 0700);
    fs::create_directory(target);
    fs::create_directory_symlink(target, cache_home / "moguet");
    ScopedEnvironmentVariable cache_home_environment(
            "XDG_CACHE_HOME", cache_home.string());
    expect_runtime_error(
            []() {
                static_cast<void>(prepare_test_trusted_cache_root());
            },
            "private cache root symlink", "symlink");
}

void test_private_cache_root_replacement_rejected(const fs::path& test_root) {
    fs::path cache_home = test_root / "private-root-replacement";
    fs::create_directory(cache_home);
    set_path_mode(cache_home, 0700);
    ScopedEnvironmentVariable cache_home_environment(
            "XDG_CACHE_HOME", cache_home.string());
    ValidatedCacheRoot trusted_root = prepare_test_trusted_cache_root();
    ValidatedPrivateCacheRoot root =
            prepare_private_trusted_cache_root(trusted_root);
    fs::path moved_root = root.path();
    moved_root += ".original";
    fs::rename(root.path(), moved_root);
    fs::create_directory(root.path());
    set_path_mode(root.path(), 0700);

    expect_runtime_error(
            [&root]() { root.require_unchanged_identity(); },
            "private cache root replacement", "concurrent replacement");
}

void test_trusted_and_private_cache_root_identity(
        const fs::path& test_root) {
    const mode_t original_mask = read_process_umask();
    {
        fs::path cache_home = test_root / "shared-root-umask-0002";
        fs::create_directory(cache_home);
        set_path_mode(cache_home, 0700);
        ScopedEnvironmentVariable cache_home_environment(
                "XDG_CACHE_HOME", cache_home.string());
        ScopedUmask scoped_umask(0002);
        ValidatedCacheRoot root = prepare_test_trusted_cache_root();
        const struct stat status = require_path_status(root.path());
        expect(
                (status.st_mode & 07777) == 0700,
                "Trusted cache root creation did not establish mode 0700");
        ValidatedPrivateCacheRoot private_root =
                prepare_private_trusted_cache_root(root);
        expect(
                private_root.device() == root.device() &&
                        private_root.inode() == root.inode(),
                "Private cache root did not retain the trusted root identity");
        private_root.require_unchanged_identity();
    }
    expect(
            read_process_umask() == original_mask,
            "Trusted/private identity test did not restore the process umask");
}

void test_workspace_symlink_rejected(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path moved_path = move_workspace_aside(workspace, ".symlink-original");
    fs::create_directory_symlink(moved_path, workspace.path());

    expect_runtime_error(
            [&workspace]() { workspace.require_unchanged_identity(); },
            "symlink workspace", "changed identity");
    workspace.retain_for_diagnostics();
}

void test_workspace_inode_change_rejected(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    static_cast<void>(move_workspace_aside(workspace, ".inode-original"));
    fs::create_directory(workspace.path());
    fs::permissions(
            workspace.path(), fs::perms::owner_all,
            fs::perm_options::replace);

    expect_runtime_error(
            [&workspace]() { workspace.require_unchanged_identity(); },
            "workspace inode change", "changed identity");
    workspace.retain_for_diagnostics();
}

void test_workspace_replacement_rejected(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    static_cast<void>(move_workspace_aside(workspace, ".replacement-original"));
    write_file(workspace.path(), "replacement");

    expect_runtime_error(
            [&workspace]() { workspace.require_unchanged_identity(); },
            "workspace replacement", "changed identity");
    workspace.retain_for_diagnostics();
}

void test_workspace_move_outside_containment_rejected(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path moved_path = outside_root /
                          (workspace.path().filename().string() + ".moved-outside");
    fs::rename(workspace.path(), moved_path);
    fs::create_directory_symlink(moved_path, workspace.path());

    expect_runtime_error(
            [&workspace]() { workspace.require_unchanged_identity(); },
            "workspace moved outside containment");
    workspace.retain_for_diagnostics();
}

void test_workspace_owner_test_seam(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    expect_runtime_error(
            [&workspace]() {
                require_artifact_workspace_identity_for_test(
                        workspace, different_user_id());
            },
            "workspace owner mismatch", "owner");
}

void test_explicit_cleanup_success(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path           workspace_path = workspace.path();
    fs::create_directory(workspace_path / "nested");
    write_file(workspace_path / "nested" / "file");
    write_file(workspace_path / "plain-file");
    fs::create_symlink("plain-file", workspace_path / "link");

    workspace.cleanup();
    expect(!fs::exists(workspace_path), "Explicit cleanup left workspace behind");
    workspace.cleanup();
}

void test_cleanup_identity_mismatch_refuses_delete(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path           original_path =
            move_workspace_aside(workspace, ".cleanup-original");
    fs::create_directory(workspace.path());
    fs::permissions(
            workspace.path(), fs::perms::owner_all,
            fs::perm_options::replace);
    fs::path replacement_sentinel = workspace.path() / "do-not-delete";
    write_file(replacement_sentinel);

    expect_runtime_error(
            [&workspace]() { workspace.cleanup(); },
            "cleanup identity mismatch", "changed identity");
    expect(
            fs::exists(replacement_sentinel),
            "Cleanup deleted replacement workspace contents");
    expect(
            fs::is_directory(original_path),
            "Cleanup deleted the original workspace reached by its descriptor");
    workspace.retain_for_diagnostics();
}

void test_cleanup_preflight_failure_deletes_zero_entries(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path     preserved_file = workspace.path() / "preserved-file";
    const fs::path     blocked_directory = workspace.path() / "blocked";
    write_file(preserved_file, "preserve-on-preflight-failure");
    fs::create_directory(blocked_directory);
    write_file(blocked_directory / "nested-sentinel");
    fs::permissions(
            blocked_directory, fs::perms::none,
            fs::perm_options::replace);

    expect_runtime_error(
            [&workspace]() { workspace.cleanup(); },
            "cleanup full-tree preflight failure", "workspace directory");
    expect(
            path_exists_no_follow(preserved_file),
            "Cleanup preflight failure deleted an already scanned entry");
    expect(
            path_exists_no_follow(blocked_directory),
            "Cleanup preflight failure deleted the blocked directory");

    fs::permissions(
            blocked_directory, fs::perms::owner_all,
            fs::perm_options::replace);
    workspace.cleanup();
}

bool          g_cleanup_child_open_called = false;
std::string   g_cleanup_child_open_leaf;
std::uint64_t g_cleanup_child_open_flags = 0;
std::uint64_t g_cleanup_child_open_resolve = 0;

int refuse_cleanup_child_open_as_mount_for_test(
        int, const std::string& leaf_name, std::uint64_t flags,
        std::uint64_t resolve) {
    g_cleanup_child_open_called = true;
    g_cleanup_child_open_leaf = leaf_name;
    g_cleanup_child_open_flags = flags;
    g_cleanup_child_open_resolve = resolve;
    errno = EXDEV;
    return -1;
}

void test_cleanup_mount_boundary_refuses_before_delete(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path nested = workspace.path() / "mounted-directory";
    const fs::path sibling = workspace.path() / "ordinary-sibling";
    fs::create_directory(nested);
    write_file(nested / "outside-sentinel", "preserved");
    write_file(sibling, "preserved");
    g_cleanup_child_open_called = false;
    g_cleanup_child_open_leaf.clear();
    g_cleanup_child_open_flags = 0;
    g_cleanup_child_open_resolve = 0;

    {
        ScopedCleanupChildOpen mount_boundary(
                refuse_cleanup_child_open_as_mount_for_test);
        expect_runtime_error(
                [&workspace]() { workspace.cleanup(); },
                "cleanup mount boundary", "filesystem boundary");
    }

    expect(
            g_cleanup_child_open_called &&
                    g_cleanup_child_open_leaf == "mounted-directory",
            "Cleanup did not inspect the simulated mount boundary.");
    const std::uint64_t expected_flags =
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    const std::uint64_t expected_resolve =
            RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    expect(
            g_cleanup_child_open_flags == expected_flags &&
                    g_cleanup_child_open_resolve == expected_resolve,
            "Cleanup child open did not request the no-follow/no-xdev boundary.");
    expect(
            read_file(nested / "outside-sentinel") == "preserved",
            "Cleanup crossed the simulated bind-mount boundary.");
    expect(
            read_file(sibling) == "preserved",
            "Cleanup deleted an entry before mount-boundary preflight finished.");
    workspace.cleanup();
}

void test_cleanup_predelete_symlink_replacement_deletes_zero_entries(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    const fs::path outside_target =
            outside_root / "cleanup-symlink-outside-target";
    write_file(outside_target, "outside-sentinel");

    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path     preserved_file = workspace.path() / "preserved-file";
    const fs::path     replaced_file = workspace.path() / "replace-me";
    write_file(preserved_file, "preserved");
    write_file(replaced_file, "original");

    g_cleanup_observer_target = "replace-me";
    g_cleanup_observer_destination = outside_target;
    g_cleanup_observer_called = false;
    {
        ScopedArtifactWorkspaceCleanupObserver observer(
                replace_cleanup_file_with_symlink_for_test);
        expect_runtime_error(
                [&workspace]() { workspace.cleanup(); },
                "cleanup pre-delete symlink replacement", "changed");
    }

    expect(
            g_cleanup_observer_called,
            "Cleanup pre-delete replacement observer was not called");
    expect(
            path_exists_no_follow(preserved_file),
            "Cleanup deleted an unrelated entry before refusing replacement");
    expect(
            S_ISLNK(require_path_status(replaced_file).st_mode),
            "Cleanup deleted the symlink replacement");
    expect_equal(
            "cleanup outside symlink target", read_file(outside_target),
            "outside-sentinel");
    workspace.retain_for_diagnostics();
    fs::remove(replaced_file);
    fs::remove_all(workspace.path());
    fs::remove(outside_target);
}

void test_cleanup_predelete_nested_root_out_deletes_zero_entries(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    const fs::path moved_directory =
            outside_root / "cleanup-nested-moved-outside";
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path     preserved_file = workspace.path() / "preserved-file";
    const fs::path     nested_directory = workspace.path() / "nested";
    write_file(preserved_file, "preserved");
    fs::create_directory(nested_directory);
    write_file(nested_directory / "nested-sentinel", "nested-preserved");

    g_cleanup_observer_target = "nested";
    g_cleanup_observer_destination = moved_directory;
    g_cleanup_observer_called = false;
    {
        ScopedArtifactWorkspaceCleanupObserver observer(
                move_cleanup_directory_outside_for_test);
        expect_runtime_error(
                [&workspace]() { workspace.cleanup(); },
                "cleanup nested root-out replacement", "changed");
    }

    expect(
            g_cleanup_observer_called,
            "Cleanup nested root-out observer was not called");
    expect(
            path_exists_no_follow(preserved_file),
            "Cleanup deleted an unrelated entry before refusing nested move");
    expect(
            S_ISLNK(require_path_status(nested_directory).st_mode),
            "Cleanup deleted the nested symlink replacement");
    expect_equal(
            "moved nested directory sentinel",
            read_file(moved_directory / "nested-sentinel"),
            "nested-preserved");
    workspace.retain_for_diagnostics();
    fs::remove(nested_directory);
    fs::remove_all(workspace.path());
    fs::remove_all(moved_directory);
}

void test_cleanup_predelete_workspace_root_out_deletes_zero_entries(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    const fs::path moved_workspace =
            outside_root / "cleanup-workspace-moved-outside";
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    const fs::path     original_workspace_path = workspace.path();
    write_file(workspace.path() / "workspace-sentinel", "root-preserved");

    g_cleanup_observer_destination = moved_workspace;
    g_cleanup_observer_called = false;
    {
        ScopedArtifactWorkspaceCleanupObserver observer(
                move_cleanup_workspace_outside_for_test);
        expect_runtime_error(
                [&workspace]() { workspace.cleanup(); },
                "cleanup workspace root-out", "changed");
    }

    expect(
            g_cleanup_observer_called,
            "Cleanup workspace root-out observer was not called");
    expect(
            !path_exists_no_follow(original_workspace_path),
            "Cleanup workspace root-out test unexpectedly restored the name");
    expect_equal(
            "moved workspace sentinel",
            read_file(moved_workspace / "workspace-sentinel"),
            "root-preserved");
    workspace.retain_for_diagnostics();
    fs::remove_all(moved_workspace);
}

void test_failed_creation_rollback_success(
        const ValidatedCacheRoot& root) {
    g_workspace_creation_observer_action =
            WorkspaceCreationObserverAction::ThrowOnly;
    g_workspace_creation_observed_path.clear();
    g_workspace_creation_observer_called = false;
    {
        ScopedArtifactWorkspaceCreationObserver observer(
                fail_workspace_creation_for_test);
        expect_runtime_error(
                [&root]() {
                    static_cast<void>(create_test_artifact_workspace(root));
                },
                "failed workspace creation rollback",
                "Injected artifact workspace creation failure");
    }

    expect(
            g_workspace_creation_observer_called,
            "Workspace creation failure observer was not called");
    expect(
            !path_exists_no_follow(g_workspace_creation_observed_path),
            "Failed workspace creation rollback left its original directory");
}

void test_failed_creation_rollback_failure_is_noexcept_and_warned(
        const ValidatedCacheRoot& root) {
    g_workspace_creation_observer_action =
            WorkspaceCreationObserverAction::LeaveNonEmpty;
    g_workspace_creation_observed_path.clear();
    g_workspace_creation_observer_called = false;
    ScopedStreamCapture warning_capture(std::cout);
    {
        ScopedArtifactWorkspaceCreationObserver observer(
                fail_workspace_creation_for_test);
        expect_runtime_error(
                [&root]() {
                    static_cast<void>(create_test_artifact_workspace(root));
                },
                "failed nonempty workspace creation rollback",
                "Injected artifact workspace creation failure");
    }

    expect(
            g_workspace_creation_observer_called,
            "Nonempty creation rollback observer was not called");
    expect(
            path_exists_no_follow(
                    g_workspace_creation_observed_path / "rollback-blocker"),
            "Failed creation rollback removed a nonempty original directory");
    expect(
            warning_capture.str().find(
                    "Refusing unsafe artifact workspace creation rollback") !=
                    std::string::npos,
            "Failed creation rollback did not emit a noexcept warning");
    expect(
            warning_capture.str().find(root.path().string()) ==
                    std::string::npos,
            "Failed creation rollback warning disclosed the cache root");
    fs::remove_all(g_workspace_creation_observed_path);
}

void test_failed_creation_rollback_symlink_replacement_is_preserved(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    const fs::path outside_target =
            outside_root / "creation-rollback-symlink-target";
    fs::create_directory(outside_target);
    write_file(outside_target / "outside-sentinel", "outside-preserved");
    g_workspace_creation_observer_action =
            WorkspaceCreationObserverAction::ReplaceWithSymlink;
    g_workspace_creation_auxiliary_path = outside_target;
    g_workspace_creation_observed_path.clear();
    g_workspace_creation_observer_called = false;
    ScopedStreamCapture warning_capture(std::cout);
    {
        ScopedArtifactWorkspaceCreationObserver observer(
                fail_workspace_creation_for_test);
        expect_runtime_error(
                [&root]() {
                    static_cast<void>(create_test_artifact_workspace(root));
                },
                "failed creation rollback symlink replacement",
                "Injected artifact workspace creation failure");
    }

    fs::path moved_original = g_workspace_creation_observed_path;
    moved_original += ".rollback-original";
    expect(
            S_ISLNK(require_path_status(
                            g_workspace_creation_observed_path)
                            .st_mode),
            "Failed creation rollback deleted the symlink replacement");
    expect_equal(
            "failed creation rollback original sentinel",
            read_file(moved_original / "original-sentinel"), "fixture");
    expect_equal(
            "failed creation rollback outside sentinel",
            read_file(outside_target / "outside-sentinel"),
            "outside-preserved");
    expect(
            warning_capture.str().find(
                    "Refusing unsafe artifact workspace creation rollback") !=
                    std::string::npos,
            "Symlink creation rollback refusal did not emit a warning");
    fs::remove(g_workspace_creation_observed_path);
    fs::remove_all(moved_original);
    fs::remove_all(outside_target);
}

void test_failed_creation_rollback_root_out_is_preserved(
        const ValidatedCacheRoot& root, const fs::path& outside_root) {
    const fs::path moved_workspace =
            outside_root / "creation-rollback-moved-outside";
    g_workspace_creation_observer_action =
            WorkspaceCreationObserverAction::MoveOutside;
    g_workspace_creation_auxiliary_path = moved_workspace;
    g_workspace_creation_observed_path.clear();
    g_workspace_creation_observer_called = false;
    ScopedStreamCapture warning_capture(std::cout);
    {
        ScopedArtifactWorkspaceCreationObserver observer(
                fail_workspace_creation_for_test);
        expect_runtime_error(
                [&root]() {
                    static_cast<void>(create_test_artifact_workspace(root));
                },
                "failed creation rollback root-out",
                "Injected artifact workspace creation failure");
    }

    expect(
            !path_exists_no_follow(g_workspace_creation_observed_path),
            "Failed creation rollback followed a moved directory by name");
    expect_equal(
            "failed creation rollback moved sentinel",
            read_file(moved_workspace / "original-sentinel"), "fixture");
    expect(
            warning_capture.str().find(
                    "Refusing unsafe artifact workspace creation rollback") !=
                    std::string::npos,
            "Root-out creation rollback refusal did not emit a warning");
    fs::remove_all(moved_workspace);
}

void test_default_scope_cleanup(const ValidatedCacheRoot& root) {
    fs::path workspace_path;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        workspace_path = workspace.path();
        write_file(workspace_path / "scope-file");
    }
    expect(!fs::exists(workspace_path), "Scope exit did not clean workspace");
}

void test_explicit_retention(const ValidatedCacheRoot& root) {
    fs::path retained_path;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        retained_path = workspace.path();
        write_file(retained_path / "diagnostic-file");
        workspace.retain_for_diagnostics();
    }
    expect(fs::exists(retained_path), "Diagnostic retention removed workspace");
}

void test_retained_workspace_is_not_reused(const ValidatedCacheRoot& root) {
    fs::path retained_path;
    {
        ArtifactWorkspace retained = create_test_artifact_workspace(root);
        retained_path = retained.path();
        retained.retain_for_diagnostics();
    }

    ArtifactWorkspace next = create_test_artifact_workspace(root);
    expect(
            next.path() != retained_path,
            "Fresh creation reused a retained workspace");
    expect(fs::exists(retained_path), "Retained workspace disappeared");
}

void test_structured_pkgdest_rejections() {
    const std::vector<std::pair<std::string, SourceBuildEnvironment>> cases = {
            {"structured PKGDEST value", {{{"PKGDEST", "/tmp/claimed"}}}},
            {"structured PKGDEST empty", {{{"PKGDEST", ""}}}},
            {"duplicate PKGDEST",
             {{{"PKGDEST", "/tmp/first"}, {"PKGDEST", ""}}}},
            {"expanded-empty PKGDEST",
             {{{"SOURCE", ""}, {"PKGDEST", ""}}}},
    };

    for(const auto& [context, environment] : cases) {
        expect(environment.defines("PKGDEST"), context + " lost definition");
        expect_runtime_error(
                [&environment]() {
                    require_unclaimed_artifact_pkgdest(environment);
                },
                context, "Source environment PKGDEST");
    }

}

void test_inherited_pkgdest_rejections() {
    SourceBuildEnvironment environment;
    {
        ScopedEnvironmentVariable inherited("PKGDEST", std::string("/tmp/claimed"));
        expect_runtime_error(
                [&environment]() {
                    require_unclaimed_artifact_pkgdest(environment);
                },
                "inherited nonempty PKGDEST", "Inherited PKGDEST");
    }
    {
        ScopedEnvironmentVariable inherited("PKGDEST", std::string(""));
        expect_runtime_error(
                [&environment]() {
                    require_unclaimed_artifact_pkgdest(environment);
                },
                "inherited empty PKGDEST", "Inherited PKGDEST");
    }
    require_unclaimed_artifact_pkgdest(environment);
}

void expect_assignment(
        const SourceBuildEnvironment& environment, std::size_t index,
        const std::string& key, const std::string& value) {
    expect(
            index < environment.ordered_assignments.size(),
            "Source environment assignment is missing");
    expect_equal(
            "source assignment key", environment.ordered_assignments[index].key,
            key);
    expect_equal(
            "source assignment value",
            environment.ordered_assignments[index].value, value);
}

void test_owned_pkgdest_and_environment_policy(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout) {
    SourceBuildEnvironment source_environment{{
            {"FIRST", "alpha value"},
            {"EMPTY", ""},
            {"DUP", "first"},
            {"DUP", "second'value"},
    }};
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);

    ArtifactMakepkgContext forward_context = prepare_artifact_makepkg_context(
            checkout, workspace, source_environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    const SourceBuildEnvironment& forward_environment =
            forward_context.command_environment();
    expect(
            forward_environment.ordered_assignments.size() == 5,
            "Owned PKGDEST assignment count differs");
    expect_assignment(forward_environment, 0, "FIRST", "alpha value");
    expect_assignment(forward_environment, 1, "EMPTY", "");
    expect_assignment(forward_environment, 2, "DUP", "first");
    expect_assignment(forward_environment, 3, "DUP", "second'value");
    expect_assignment(
            forward_environment, 4, "PKGDEST",
            workspace.canonical_path().string());
    expect(
            fs::path(forward_environment.ordered_assignments.back().value)
                    .is_absolute(),
            "Owned PKGDEST is not absolute");
    expect(
            forward_context.workspace_path() == workspace.canonical_path(),
            "Context workspace path differs");
    expect(
            forward_context.checkout_path() == checkout.canonical_path(),
            "Context checkout path differs");
    expect_equal(
            "Forward empty-value policy",
            forward_context.command_environment_prefix(),
            serialize_source_build_environment(
                    forward_environment,
                    SourceEnvironmentEmptyValuePolicy::Forward));

    ArtifactMakepkgContext omit_context = prepare_artifact_makepkg_context(
            checkout, workspace, source_environment,
            SourceEnvironmentEmptyValuePolicy::Omit);
    expect_equal(
            "Omit empty-value policy", omit_context.command_environment_prefix(),
            serialize_source_build_environment(
                    omit_context.command_environment(),
                    SourceEnvironmentEmptyValuePolicy::Omit));
    expect(
            omit_context.command_environment_prefix().find("EMPTY=") ==
                    std::string::npos,
            "Omit policy forwarded an empty assignment");
    expect(
            forward_context.command_environment_prefix().find("EMPTY=''") !=
                    std::string::npos,
            "Forward policy omitted an empty assignment");
}

void test_shared_makepkg_context_and_packagelist_adapter(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    test_environment.clear_makepkg_logs();
    SourceBuildEnvironment source_environment{{
            {"FIRST", "alpha value"},
            {"EMPTY", ""},
            {"DUP", "first"},
            {"DUP", "second'value"},
    }};
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, source_environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    fs::path artifact_path =
            workspace.path() / "sample-package-1-1-x86_64.pkg.tar.zst";
    write_file(
            test_environment.packagelist_output_file(),
            artifact_path.string() + "\n");

    ExpectedPackageArtifactPath expected =
            query_makepkg_packagelist(workspace, context);
    expect(expected.path() == artifact_path, "Packagelist adapter path differs");
    expect(
            context.run_makepkg_build_only(
                    workspace, expected, ArtifactMakepkgBuildOptions{}) == 0,
            "Future build-only makepkg fake failed");

    expect_equal(
            "makepkg argv log", read_file(test_environment.argv_log()),
            "argv-begin\n"
            "arg[0]=<--packagelist>\n"
            "argv-end\n"
            "argv-begin\n"
            "arg[0]=<-sc>\n"
            "argv-end\n");
    expect_equal(
            "makepkg cwd log", read_file(test_environment.cwd_log()),
            checkout.canonical_path().string() + "\n" +
                    checkout.canonical_path().string() + "\n");

    const std::string environment_record =
            "env-begin\n"
            "env[FIRST]=<alpha value>\n"
            "env[EMPTY]=<>\n"
            "env[DUP]=<second'value>\n"
            "env[PKGDEST]=<" + workspace.canonical_path().string() +
            ">\n"
            "env-end\n";
    expect_equal(
            "makepkg environment log",
            read_file(test_environment.environment_log()),
            environment_record + environment_record);
    expect_equal(
            "makepkg command log", read_file(test_environment.command_log()),
            "makepkg --packagelist\nmakepkg -sc\n");
}

void test_build_only_requires_query_bound_expected(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    ExpectedPackageArtifactPath unbound = declare_expected_artifact(
            workspace, "unbound.pkg.tar.zst");

    test_environment.clear_makepkg_logs();
    expect_runtime_error(
            [&workspace, &context, &unbound]() {
                static_cast<void>(context.run_makepkg_build_only(
                        workspace, unbound, ArtifactMakepkgBuildOptions{}));
            },
            "unbound expected build", "does not belong");
    expect_equal(
            "unbound expected command count",
            read_file(test_environment.command_log()), "");
}

void test_build_only_rejects_different_context(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment{{{"FIRST", "same value"}}};
    ArtifactMakepkgContext query_context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    ArtifactMakepkgContext other_context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    fs::path artifact_path = workspace.path() / "other-context.pkg.tar.zst";
    write_file(
            test_environment.packagelist_output_file(),
            artifact_path.string() + "\n");
    ExpectedPackageArtifactPath expected =
            query_makepkg_packagelist(workspace, query_context);

    test_environment.clear_makepkg_logs();
    expect_runtime_error(
            [&workspace, &other_context, &expected]() {
                static_cast<void>(other_context.run_makepkg_build_only(
                        workspace, expected, ArtifactMakepkgBuildOptions{}));
            },
            "different makepkg context", "does not belong");
    expect_equal(
            "different context command count",
            read_file(test_environment.command_log()), "");
}

void test_makepkg_context_rejects_checkout_replacement(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    fs::path artifact_path = workspace.path() / "checkout-replaced.pkg.tar.zst";
    write_file(
            test_environment.packagelist_output_file(),
            artifact_path.string() + "\n");
    ExpectedPackageArtifactPath expected =
            query_makepkg_packagelist(workspace, context);

    fs::path moved_checkout = checkout.canonical_path();
    moved_checkout += ".original";
    fs::rename(checkout.canonical_path(), moved_checkout);
    fs::create_directory(checkout.canonical_path());

    test_environment.clear_makepkg_logs();
    expect_runtime_error(
            [&workspace, &context, &expected]() {
                static_cast<void>(context.run_makepkg_build_only(
                        workspace, expected, ArtifactMakepkgBuildOptions{}));
            },
            "checkout replacement", "concurrent replacement");
    expect_equal(
            "checkout replacement command count",
            read_file(test_environment.command_log()), "");

    fs::remove(checkout.canonical_path());
    fs::rename(moved_checkout, checkout.canonical_path());
}

void test_packagelist_command_failure(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);
    fs::path artifact_path = workspace.path() / "failed.pkg.tar.zst";
    write_file(
            test_environment.packagelist_output_file(),
            artifact_path.string() + "\n");
    ScopedEnvironmentVariable exit_code(
            "MOGUET_TEST_MAKEPKG_PACKAGELIST_EXIT_CODE", std::string("23"));

    expect_runtime_error(
            [&workspace, &context]() {
                static_cast<void>(query_makepkg_packagelist(workspace, context));
            },
            "packagelist command failure", "exit status 23");
}

void test_packagelist_adapter_preserves_binary_control(
        const ValidatedCacheRoot& root, const ValidatedCachePath& checkout,
        const TestEnvironment& test_environment) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    SourceBuildEnvironment environment;
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
            checkout, workspace, environment,
            SourceEnvironmentEmptyValuePolicy::Forward);

    std::string output = (workspace.path() / "nul.pkg.tar.zst").string();
    output.push_back('\0');
    output += "hidden-tail\n";
    write_file(test_environment.packagelist_output_file(), output);

    expect_runtime_error(
            [&workspace, &context]() {
                static_cast<void>(query_makepkg_packagelist(workspace, context));
            },
            "packagelist adapter NUL", "control character");
}

void test_packagelist_parser_success_and_blank_lines(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path artifact_path = workspace.path() / "sample.pkg.tar.zst";
    ExpectedPackageArtifactPath expected =
            validate_makepkg_packagelist_output(
                    workspace,
                    "\n  \n" + artifact_path.string() + "\n \n");
    expect(expected.path() == artifact_path, "Parsed artifact path differs");
}

void test_packagelist_parser_malformed_output(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, ""));
                },
                "empty packagelist", "got 0");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, " \n  \n"));
                },
                "blank-only packagelist", "got 0");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        std::string output = (workspace.path() / "cr.pkg.tar.zst").string();
        output += "\r\n";
        expect_runtime_error(
                [&workspace, &output]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, output));
                },
                "packagelist carriage return", "carriage return");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        std::string output = (workspace.path() / "nul.pkg.tar.zst").string();
        output.push_back('\0');
        output += "tail\n";
        expect_runtime_error(
                [&workspace, &output]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, output));
                },
                "packagelist NUL", "control character");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        std::string output =
                (workspace.path() / "vertical-tab.pkg.tar.zst").string();
        output.push_back('\v');
        output += "tail\n";
        expect_runtime_error(
                [&workspace, &output]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, output));
                },
                "packagelist vertical tab", "control character");
    }
}

void test_packagelist_parser_path_rejections(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, "relative.pkg.tar.zst\n"));
                },
                "relative artifact path", "relative artifact path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        fs::path outside_path =
                root.canonical_path().parent_path() / "outside.pkg.tar.zst";
        expect_runtime_error(
                [&workspace, &outside_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, outside_path.string() + "\n"));
                },
                "outside artifact path", "not a direct child");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        fs::path nested_path =
                workspace.path() / "nested" / "nested.pkg.tar.zst";
        expect_runtime_error(
                [&workspace, &nested_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, nested_path.string() + "\n"));
                },
                "nested artifact path", "not a direct child");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        fs::path dot_path = workspace.path() / ".." /
                            workspace.path().filename() / "dot.pkg.tar.zst";
        expect_runtime_error(
                [&workspace, &dot_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, dot_path.string() + "\n"));
                },
                "lexical dot-dot artifact path", "dot component");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        expect_runtime_error(
                [&workspace]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, workspace.path().string() + "\n"));
                },
                "workspace as artifact", "not a direct child");
    }
}

void test_packagelist_parser_cardinality_rejections(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string path =
                (workspace.path() / "duplicate.pkg.tar.zst").string();
        expect_runtime_error(
                [&workspace, &path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, path + "\n" + path + "\n"));
                },
                "duplicate artifact path", "duplicate artifact path");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string first =
                (workspace.path() / "main.pkg.tar.zst").string();
        const std::string second =
                (workspace.path() / "main-debug.pkg.tar.zst").string();
        expect_runtime_error(
                [&workspace, &first, &second]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, first + "\n" + second + "\n"));
                },
                "multiple split or debug artifacts", "got 2");
    }
}

void test_packagelist_parser_bounded_diagnostics(
        const ValidatedCacheRoot& root) {
    constexpr std::size_t MAX_EXPECTED_DIAGNOSTIC_SIZE = 256;
    const std::string     tail_marker = "UNBOUNDED-DIAGNOSTIC-TAIL";
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string long_relative_path =
                std::string(16 * 1024, 'r') + tail_marker;
        const std::string message = expect_runtime_error(
                [&workspace, &long_relative_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, long_relative_path + "\n"));
                },
                "long relative artifact path", "relative artifact path");
        expect(
                message.size() <= MAX_EXPECTED_DIAGNOSTIC_SIZE,
                "Long relative path produced an unbounded diagnostic");
        expect(
                message.find(tail_marker) == std::string::npos,
                "Long relative path tail leaked into its diagnostic");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string long_duplicate_path =
                (workspace.path() / std::string(16 * 1024, 'd')).string() +
                tail_marker;
        const std::string message = expect_runtime_error(
                [&workspace, &long_duplicate_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace,
                            long_duplicate_path + "\n" +
                                    long_duplicate_path + "\n"));
                },
                "long duplicate artifact path", "duplicate artifact path");
        expect(
                message.size() <= MAX_EXPECTED_DIAGNOSTIC_SIZE,
                "Long duplicate path produced an unbounded diagnostic");
        expect(
                message.find(tail_marker) == std::string::npos,
                "Long duplicate path tail leaked into its diagnostic");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        const std::string long_non_ascii_leaf =
                std::string(16 * 1024, static_cast<char>(0xc3)) + tail_marker;
        const fs::path long_direct_path =
                workspace.path() / long_non_ascii_leaf;
        const std::string message = expect_runtime_error(
                [&workspace, &long_direct_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, long_direct_path.string() + "\n"));
                },
                "long non-ASCII artifact path",
                "Unable to inspect artifact path");
        expect(
                message.size() <= 512,
                "Long non-ASCII path produced an unbounded diagnostic");
        expect(
                message.find(tail_marker) == std::string::npos,
                "Long non-ASCII path tail leaked into its diagnostic");
        expect(
                std::all_of(
                        message.begin(), message.end(), [](unsigned char byte) {
                            return byte <= 0x7f;
                        }),
                "Long non-ASCII path diagnostic contains raw high bytes");
    }
}

void test_packagelist_parser_preexisting_paths(
        const ValidatedCacheRoot& root) {
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        fs::path artifact_path = workspace.path() / "preexisting.pkg.tar.zst";
        write_file(artifact_path);
        expect_runtime_error(
                [&workspace, &artifact_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, artifact_path.string() + "\n"));
                },
                "preexisting artifact", "already exists before build");
    }
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        fs::path artifact_path =
                workspace.path() / "signature-preexists.pkg.tar.zst";
        write_file(fs::path(artifact_path.string() + ".sig"));
        expect_runtime_error(
                [&workspace, &artifact_path]() {
                    static_cast<void>(validate_makepkg_packagelist_output(
                            workspace, artifact_path.string() + "\n"));
                },
                "preexisting signature", "signature already exists");
    }
}

void test_post_build_regular_artifact_and_explicit_cleanup(
        const ValidatedCacheRoot& root) {
    fs::path workspace_path;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        workspace_path = workspace.path();
        ExpectedPackageArtifactPath expected = declare_expected_artifact(
                workspace, "sample-package-1-1-x86_64.pkg.tar.zst");
        write_file(expected.path(), "package archive");

        ValidatedPackageArtifactPath artifact =
                validate_post_build_package_artifact(
                        std::move(workspace), expected);
        expect(artifact.path() == expected.path(), "Validated artifact path differs");
        expect(
                artifact.workspace_path() == workspace_path,
                "Validated artifact lost workspace ownership");
        artifact.require_validity();
        artifact.cleanup_workspace();
        expect(
                !fs::exists(workspace_path),
                "Validated artifact explicit cleanup left workspace behind");
    }
}

void test_post_build_missing_artifact(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "missing.pkg.tar.zst");
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "missing artifact", "artifact is missing");
}

void test_post_build_artifact_symlink(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "symlink.pkg.tar.zst");
    fs::create_symlink("/dev/null", expected.path());
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "artifact symlink", "must not be a symlink");
}

void test_post_build_artifact_directory(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "directory.pkg.tar.zst");
    fs::create_directory(expected.path());
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "artifact directory", "regular file");
}

void test_post_build_owner_test_seam(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "wrong-owner.pkg.tar.zst");
    write_file(expected.path());
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(
                        validate_post_build_package_artifact_for_test(
                                std::move(workspace), expected,
                                different_user_id()));
            },
            "post-build owner mismatch", "owner");
}

void test_post_build_unexpected_package_artifact(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "main.pkg.tar.zst");
    write_file(expected.path());
    write_file(workspace.path() / "main-debug.pkg.tar.zst");
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "unexpected extra package artifact", "Unexpected entry");
}

void test_post_build_unexpected_unrelated_file(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "main.pkg.tar.zst");
    write_file(expected.path());
    write_file(workspace.path() / "build.log");
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "unexpected unrelated file", "Unexpected entry");
}

void test_post_build_matching_signature(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "signed.pkg.tar.zst");
    write_file(expected.path());
    write_file(fs::path(expected.path().string() + ".sig"), "signature");

    ValidatedPackageArtifactPath artifact =
            validate_post_build_package_artifact(
                    std::move(workspace), expected);
    expect(
            artifact.path() == expected.path(),
            "Signature changed the pacman artifact target");
    artifact.require_validity();
}

void test_validated_artifact_retention_and_cleanup(
        const ValidatedCacheRoot& root) {
    fs::path retained_workspace_path;
    fs::path retained_artifact_path;
    {
        ArtifactWorkspace workspace = create_test_artifact_workspace(root);
        retained_workspace_path = workspace.path();
        ExpectedPackageArtifactPath expected =
                declare_expected_artifact(workspace, "retained.pkg.tar.zst");
        retained_artifact_path = expected.path();
        write_file(expected.path());
        ValidatedPackageArtifactPath artifact =
                validate_post_build_package_artifact(
                        std::move(workspace), expected);
        artifact.retain_workspace_for_diagnostics();
    }
    expect(
            fs::exists(retained_workspace_path),
            "Validated artifact retention removed workspace");
    expect(
            fs::is_regular_file(retained_artifact_path),
            "Validated artifact retention removed package artifact");

    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    fs::path           cleanup_path = workspace.path();
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "retained-then-cleaned.pkg.tar.zst");
    write_file(expected.path());
    ValidatedPackageArtifactPath artifact =
            validate_post_build_package_artifact(
                    std::move(workspace), expected);
    artifact.retain_workspace_for_diagnostics();
    artifact.cleanup_workspace();
    expect(
            !fs::exists(cleanup_path),
            "Explicit cleanup did not override diagnostic retention");
}

void test_post_build_signature_symlink(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "signed.pkg.tar.zst");
    write_file(expected.path());
    fs::create_symlink(
            "/dev/null", fs::path(expected.path().string() + ".sig"));
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "signature symlink", "must not be a symlink");
}

void test_post_build_unmatched_signature(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "main.pkg.tar.zst");
    write_file(expected.path());
    write_file(workspace.path() / "other.pkg.tar.zst.sig", "signature");
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "unmatched signature", "Unmatched signature");
}

void test_post_build_extra_directory(const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "main.pkg.tar.zst");
    write_file(expected.path());
    fs::create_directory(workspace.path() / "nested");
    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "extra directory", "Unexpected directory");
}

void test_post_build_workspace_identity_change(
        const ValidatedCacheRoot& root) {
    ArtifactWorkspace workspace = create_test_artifact_workspace(root);
    ExpectedPackageArtifactPath expected =
            declare_expected_artifact(workspace, "identity.pkg.tar.zst");
    write_file(expected.path());
    static_cast<void>(move_workspace_aside(workspace, ".post-build-original"));
    fs::create_directory(workspace.path());
    fs::permissions(
            workspace.path(), fs::perms::owner_all,
            fs::perm_options::replace);

    expect_runtime_error(
            [&workspace, &expected]() {
                static_cast<void>(validate_post_build_package_artifact(
                        std::move(workspace), expected));
            },
            "post-build workspace identity change", "changed identity");
    workspace.retain_for_diagnostics();
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const char* configured_stub =
                std::getenv("MOGUET_TEST_MAKEPKG_STUB");
        fs::path makepkg_stub_directory = argc >= 2
                                                  ? fs::path(argv[1])
                                          : configured_stub != nullptr
                                                  ? fs::path(configured_stub)
                                                            .parent_path()
                                                  : fs::path("tests/stubs");
        if(argc > 2) {
            throw std::runtime_error(
                    "Usage: artifact-workspace-test [makepkg-stub-directory]");
        }

        TestEnvironment test_environment(makepkg_stub_directory);
        test_private_cache_root_umask_matrix(test_environment.path());
        test_existing_private_cache_root_modes(test_environment.path());
        test_private_cache_root_wrong_owner_seam(test_environment.path());
        test_private_cache_root_symlink_rejected(test_environment.path());
        test_private_cache_root_replacement_rejected(
                test_environment.path());
        test_trusted_and_private_cache_root_identity(
                test_environment.path());

        // Test processをumask 0002で起動しても、default fixture rootはXDG
        // preparationが0700で確立し、private capabilityは同じinodeを保持する。
        {
            ValidatedCacheRoot trusted_root =
                    prepare_test_trusted_cache_root();
            ValidatedPrivateCacheRoot private_root =
                    prepare_private_trusted_cache_root(trusted_root);
            private_root.require_unchanged_identity();
        }
        ValidatedCacheRoot root = prepare_test_trusted_cache_root();
        ValidatedCachePath checkout = prepare_checkout(root);

        test_workspace_creation_contract(root);
        test_fresh_workspace_creation(root);
        test_workspace_symlink_rejected(root);
        test_workspace_inode_change_rejected(root);
        test_workspace_replacement_rejected(root);
        test_workspace_move_outside_containment_rejected(
                root, test_environment.path());
        test_workspace_owner_test_seam(root);
        test_explicit_cleanup_success(root);
        test_cleanup_identity_mismatch_refuses_delete(root);
        test_cleanup_preflight_failure_deletes_zero_entries(root);
        test_cleanup_mount_boundary_refuses_before_delete(root);
        test_cleanup_predelete_symlink_replacement_deletes_zero_entries(
                root, test_environment.path());
        test_cleanup_predelete_nested_root_out_deletes_zero_entries(
                root, test_environment.path());
        test_cleanup_predelete_workspace_root_out_deletes_zero_entries(
                root, test_environment.path());
        test_failed_creation_rollback_success(root);
        test_failed_creation_rollback_failure_is_noexcept_and_warned(root);
        test_failed_creation_rollback_symlink_replacement_is_preserved(
                root, test_environment.path());
        test_failed_creation_rollback_root_out_is_preserved(
                root, test_environment.path());
        test_default_scope_cleanup(root);
        test_explicit_retention(root);
        test_retained_workspace_is_not_reused(root);

        test_structured_pkgdest_rejections();
        test_inherited_pkgdest_rejections();
        test_owned_pkgdest_and_environment_policy(root, checkout);
        test_shared_makepkg_context_and_packagelist_adapter(
                root, checkout, test_environment);
        test_build_only_requires_query_bound_expected(
                root, checkout, test_environment);
        test_build_only_rejects_different_context(
                root, checkout, test_environment);
        test_makepkg_context_rejects_checkout_replacement(
                root, checkout, test_environment);
        test_packagelist_command_failure(root, checkout, test_environment);
        test_packagelist_adapter_preserves_binary_control(
                root, checkout, test_environment);

        test_packagelist_parser_success_and_blank_lines(root);
        test_packagelist_parser_malformed_output(root);
        test_packagelist_parser_path_rejections(root);
        test_packagelist_parser_cardinality_rejections(root);
        test_packagelist_parser_bounded_diagnostics(root);
        test_packagelist_parser_preexisting_paths(root);

        test_post_build_regular_artifact_and_explicit_cleanup(root);
        test_post_build_missing_artifact(root);
        test_post_build_artifact_symlink(root);
        test_post_build_artifact_directory(root);
        test_post_build_owner_test_seam(root);
        test_post_build_unexpected_package_artifact(root);
        test_post_build_unexpected_unrelated_file(root);
        test_post_build_matching_signature(root);
        test_validated_artifact_retention_and_cleanup(root);
        test_post_build_signature_symlink(root);
        test_post_build_unmatched_signature(root);
        test_post_build_extra_directory(root);
        test_post_build_workspace_identity_change(root);
    } catch(const std::exception& error) {
        std::cerr << "artifact workspace test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact workspace tests passed\n";
    return 0;
}
