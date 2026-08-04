#include "local_source_workspace.hpp"

#include "trusted_cache_test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

static_assert(!std::is_copy_constructible_v<LocalSourceWorkspace>);
static_assert(std::is_nothrow_move_constructible_v<LocalSourceWorkspace>);
static_assert(!std::is_copy_assignable_v<LocalSourceWorkspace>);
static_assert(!std::is_move_assignable_v<LocalSourceWorkspace>);

namespace {

namespace fs = std::filesystem;

constexpr std::string_view PKGBUILD_CONTENT =
        "pkgname=local-workspace-test\n"
        "pkgver=1\n"
        "pkgrel=1\n"
        "arch=('any')\n";
constexpr std::string_view SRCINFO_CONTENT =
        "pkgbase = local-workspace-test\n"
        "\tpkgver = 1\n"
        "\tpkgrel = 1\n"
        "\tarch = any\n"
        "pkgname = local-workspace-test\n";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void set_mode(const fs::path& path, mode_t mode) {
    if(::chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error(
                "Failed to set fixture mode: " + path.string());
    }
}

void create_fixture_directory(const fs::path& path, mode_t mode = 0700) {
    fs::create_directory(path);
    set_mode(path, mode);
}

void write_file(
        const fs::path& path, std::string_view contents,
        mode_t mode = 0644) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error(
                "Failed to open fixture file: " + path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error(
                "Failed to finish fixture file: " + path.string());
    }
    set_mode(path, mode);
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) {
        throw std::runtime_error(
                "Failed to read fixture file: " + path.string());
    }
    return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

struct NodeSnapshot {
    std::string    relative_path;
    std::uintmax_t type = 0;
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::uintmax_t owner = 0;
    std::uintmax_t mode = 0;
    std::uintmax_t link_count = 0;
    std::intmax_t  size = 0;
    std::intmax_t  modification_seconds = 0;
    std::intmax_t  modification_nanoseconds = 0;
    std::intmax_t  status_change_seconds = 0;
    std::intmax_t  status_change_nanoseconds = 0;
    std::string    payload;

    bool operator==(const NodeSnapshot&) const = default;
};

NodeSnapshot snapshot_node(
        const fs::path& path, const std::string& relative_path) {
    struct stat status {};
    if(::lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error(
                "Failed to inspect fixture node: " + path.string());
    }

    std::string payload;
    if(S_ISREG(status.st_mode)) {
        payload = read_file(path);
    } else if(S_ISLNK(status.st_mode)) {
        payload = fs::read_symlink(path).generic_string();
    }
    return NodeSnapshot{
            relative_path,
            static_cast<std::uintmax_t>(status.st_mode & S_IFMT),
            static_cast<std::uintmax_t>(status.st_dev),
            static_cast<std::uintmax_t>(status.st_ino),
            static_cast<std::uintmax_t>(status.st_uid),
            static_cast<std::uintmax_t>(status.st_mode & 07777),
            static_cast<std::uintmax_t>(status.st_nlink),
            static_cast<std::intmax_t>(status.st_size),
            static_cast<std::intmax_t>(status.st_mtim.tv_sec),
            static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
            static_cast<std::intmax_t>(status.st_ctim.tv_sec),
            static_cast<std::intmax_t>(status.st_ctim.tv_nsec),
            std::move(payload)};
}

std::vector<NodeSnapshot> snapshot_tree(const fs::path& root) {
    std::vector<NodeSnapshot> snapshot;
    snapshot.push_back(snapshot_node(root, "."));
    for(const fs::directory_entry& entry :
        fs::recursive_directory_iterator(root)) {
        snapshot.push_back(snapshot_node(
                entry.path(),
                entry.path().lexically_relative(root).generic_string()));
    }
    std::sort(
            snapshot.begin(), snapshot.end(),
            [](const NodeSnapshot& left, const NodeSnapshot& right) {
                return left.relative_path < right.relative_path;
            });
    return snapshot;
}

std::vector<std::string> relative_entry_names(const fs::path& root) {
    std::vector<std::string> names;
    for(const fs::directory_entry& entry :
        fs::recursive_directory_iterator(root)) {
        names.push_back(
                entry.path().lexically_relative(root).generic_string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> direct_child_names(const fs::path& root) {
    std::vector<std::string> names;
    for(const fs::directory_entry& entry : fs::directory_iterator(root)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

struct stat node_status(const fs::path& path) {
    struct stat status {};
    if(::lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error(
                "Failed to stat copied node: " + path.string());
    }
    return status;
}

class ScopedXdgCacheHome final {
    std::optional<std::string> previous_;

public:
    explicit ScopedXdgCacheHome(const fs::path& path) {
        const char* previous = std::getenv("XDG_CACHE_HOME");
        if(previous != nullptr) previous_ = previous;
        if(::setenv("XDG_CACHE_HOME", path.c_str(), 1) != 0) {
            throw std::runtime_error("Failed to set fixture XDG_CACHE_HOME.");
        }
    }

    ScopedXdgCacheHome(const ScopedXdgCacheHome&) = delete;
    ScopedXdgCacheHome& operator=(const ScopedXdgCacheHome&) = delete;

    ~ScopedXdgCacheHome() noexcept {
        if(previous_.has_value()) {
            static_cast<void>(::setenv(
                    "XDG_CACHE_HOME", previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv("XDG_CACHE_HOME"));
        }
    }
};

class TemporaryTree final {
    fs::path base_path_;

public:
    TemporaryTree() {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "moguet-local-source-workspace-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = ::mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create local source workspace test directory.");
        }
        base_path_ = created_path;
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    ~TemporaryTree() noexcept {
        std::error_code error;
        fs::remove_all(base_path_, error);
    }

    const fs::path& path() const noexcept {
        return base_path_;
    }
};

fs::path make_source_root(
        const TemporaryTree& tree, std::string_view name = "source") {
    const fs::path root = tree.path() / std::string(name);
    create_fixture_directory(root);
    write_file(root / "PKGBUILD", PKGBUILD_CONTENT);
    write_file(root / ".SRCINFO", SRCINFO_CONTENT);
    return root;
}

class WorkspaceFixture final {
    TemporaryTree      tree_;
    fs::path           source_path_;
    fs::path           cache_home_path_;
    ScopedXdgCacheHome cache_environment_;

public:
    WorkspaceFixture()
        : source_path_(make_source_root(tree_)),
          cache_home_path_(tree_.path() / "cache-home"),
          cache_environment_([this]() {
              create_fixture_directory(cache_home_path_);
              return cache_home_path_;
          }()) {}

    WorkspaceFixture(const WorkspaceFixture&) = delete;
    WorkspaceFixture& operator=(const WorkspaceFixture&) = delete;

    const fs::path& source_path() const noexcept {
        return source_path_;
    }

    LocalSourceRoot open_source_root() const {
        return open_local_source_root(source_path_, false);
    }

    ValidatedCacheRoot prepare_cache_root() const {
        return prepare_test_trusted_cache_root();
    }
};

class ScopedWorkspaceTestHook final {
public:
    explicit ScopedWorkspaceTestHook(LocalSourceWorkspaceTestHook hook) {
        set_local_source_workspace_test_hook(std::move(hook));
    }

    ScopedWorkspaceTestHook(const ScopedWorkspaceTestHook&) = delete;
    ScopedWorkspaceTestHook& operator=(
            const ScopedWorkspaceTestHook&) = delete;

    ~ScopedWorkspaceTestHook() noexcept {
        set_local_source_workspace_test_hook({});
    }
};

LocalSourceWorkspaceFailure require_workspace_failure(
        const LocalSourceRoot& source_root,
        const ValidatedCacheRoot& cache_root,
        LocalSourceWorkspaceStage expected_stage,
        LocalSourceWorkspaceErrorCode expected_code,
        const fs::path& expected_relative_path,
        const std::string& context) {
    try {
        LocalSourceWorkspace workspace =
                materialize_local_source_workspace(source_root, cache_root);
        workspace.cleanup();
    } catch(const LocalSourceWorkspaceError& error) {
        expect(
                error.failure().stage == expected_stage,
                context + ": failure stage differs");
        expect(
                error.failure().code == expected_code,
                context + ": failure code differs");
        expect(
                error.failure().relative_path == expected_relative_path,
                context + ": failure relative path differs");
        return error.failure();
    }
    throw std::runtime_error(context + ": operation unexpectedly succeeded");
}

void populate_representative_source_tree(const fs::path& root) {
    create_fixture_directory(root / "nested", 0750);
    write_file(root / "nested" / "tool", "#!/bin/sh\nexit 0\n", 0755);
    write_file(root / "payload", "payload\n");
    if(::symlink("../payload", (root / "nested" / "payload-link").c_str()) !=
       0) {
        throw std::runtime_error("Failed to create safe symlink fixture.");
    }

    create_fixture_directory(root / ".git");
    write_file(root / ".git" / "config", "[core]\n\tbare = false\n");
    create_fixture_directory(root / "src");
    write_file(root / "src" / "existing", "existing src\n");
    create_fixture_directory(root / "pkg");
    write_file(root / "pkg" / "existing", "existing pkg\n");

    write_file(root / "hardlink-a", "hardlink payload\n");
    if(::link(
               (root / "hardlink-a").c_str(),
               (root / "nested" / "hardlink-b").c_str()) != 0) {
        throw std::runtime_error("Failed to create hardlink fixture.");
    }
}

void expect_representative_snapshot(
        const fs::path& source_root, const fs::path& workspace_root) {
    expect(
            relative_entry_names(source_root) ==
                    relative_entry_names(workspace_root),
            "Workspace snapshot entry set differs from the local tree");
    expect(
            read_file(workspace_root / "nested" / "tool") ==
                    "#!/bin/sh\nexit 0\n",
            "Executable content differs in workspace snapshot");
    expect(
            (node_status(workspace_root / "nested" / "tool").st_mode &
             07777) == 0755,
            "Executable mode was not preserved");
    expect(
            (node_status(workspace_root / "nested").st_mode & 07777) == 0750,
            "Directory mode was not preserved");
    expect(
            fs::is_symlink(
                    fs::symlink_status(
                            workspace_root / "nested" / "payload-link")) &&
                    fs::read_symlink(
                            workspace_root / "nested" / "payload-link") ==
                            fs::path("../payload"),
            "Safe relative symlink was not preserved as a symlink");
    expect(
            read_file(workspace_root / ".git" / "config") ==
                            "[core]\n\tbare = false\n" &&
                    read_file(workspace_root / "src" / "existing") ==
                            "existing src\n" &&
                    read_file(workspace_root / "pkg" / "existing") ==
                            "existing pkg\n",
            ".git/src/pkg entries were filtered or changed");

    const struct stat source_a = node_status(source_root / "hardlink-a");
    const struct stat source_b =
            node_status(source_root / "nested" / "hardlink-b");
    const struct stat copied_a = node_status(workspace_root / "hardlink-a");
    const struct stat copied_b =
            node_status(workspace_root / "nested" / "hardlink-b");
    expect(
            source_a.st_dev == source_b.st_dev &&
                    source_a.st_ino == source_b.st_ino,
            "Source hardlink fixture does not share an inode");
    expect(
            copied_a.st_dev != copied_b.st_dev ||
                    copied_a.st_ino != copied_b.st_ino,
            "Workspace preserved source hardlink identity");
    expect(
            read_file(workspace_root / "hardlink-a") ==
                            "hardlink payload\n" &&
                    read_file(
                            workspace_root / "nested" / "hardlink-b") ==
                            "hardlink payload\n",
            "Hardlink content was not independently copied");
}

void test_full_snapshot_and_explicit_cleanup() {
    WorkspaceFixture fixture;
    populate_representative_source_tree(fixture.source_path());
    const std::vector<NodeSnapshot> original =
            snapshot_tree(fixture.source_path());
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());

    LocalSourceWorkspace workspace =
            materialize_local_source_workspace(source_root, cache_root);
    const fs::path workspace_path = workspace.path();
    expect(
            workspace_path.parent_path() == cache_root.canonical_path(),
            "Source workspace is outside the trusted cache root");
    workspace.require_unchanged_identity();
    expect_representative_snapshot(fixture.source_path(), workspace_path);
    expect(
            snapshot_tree(fixture.source_path()) == original,
            "Materialization changed the original local tree");

    workspace.cleanup();
    expect(!fs::exists(workspace_path), "Explicit cleanup left a workspace");
    workspace.cleanup();
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Explicit cleanup left cache entries");
    expect(
            snapshot_tree(fixture.source_path()) == original,
            "Explicit cleanup changed the original local tree");
}

void test_destructor_cleanup() {
    WorkspaceFixture fixture;
    const std::vector<NodeSnapshot> original =
            snapshot_tree(fixture.source_path());
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());
    fs::path workspace_path;
    {
        LocalSourceWorkspace workspace =
                materialize_local_source_workspace(source_root, cache_root);
        workspace_path = workspace.path();
        expect(fs::exists(workspace_path), "Workspace was not materialized");
    }
    expect(
            !fs::exists(workspace_path),
            "LocalSourceWorkspace destructor left a workspace");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Destructor cleanup left cache entries");
    expect(
            snapshot_tree(fixture.source_path()) == original,
            "Destructor cleanup changed the original local tree");
}

void test_read_only_source_directory_remains_cleanup_capable() {
    WorkspaceFixture fixture;
    const fs::path read_only_directory =
            fixture.source_path() / "read-only";
    create_fixture_directory(read_only_directory);
    write_file(read_only_directory / "payload", "read-only payload\n");
    set_mode(read_only_directory, 0555);
    const std::vector<NodeSnapshot> original =
            snapshot_tree(fixture.source_path());
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());

    LocalSourceWorkspace workspace =
            materialize_local_source_workspace(source_root, cache_root);
    const fs::path workspace_path = workspace.path();
    expect(
            (node_status(read_only_directory).st_mode & 07777) == 0555,
            "Materialization changed the read-only source directory mode");
    expect(
            (node_status(workspace_path / "read-only").st_mode & 07777) ==
                    0755,
            "Snapshot did not add owner cleanup permissions to a 0555 directory");
    expect(
            read_file(workspace_path / "read-only" / "payload") ==
                    "read-only payload\n",
            "Read-only directory content was not copied");
    workspace.cleanup();

    expect(
            !fs::exists(workspace_path),
            "Read-only directory prevented explicit workspace cleanup");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Read-only directory cleanup left cache entries");
    expect(
            snapshot_tree(fixture.source_path()) == original,
            "Read-only snapshot handling changed the original tree");
}

void test_unsafe_symlinks_and_special_file_are_rejected() {
    struct Scenario {
        std::string                   name;
        fs::path                      relative_path;
        LocalSourceWorkspaceErrorCode code;
        void (*prepare)(const fs::path& root);
    };
    const std::vector<Scenario> scenarios = {
            {"absolute symlink", "absolute-link",
             LocalSourceWorkspaceErrorCode::SymlinkEscape,
             [](const fs::path& root) {
                 if(::symlink(
                            "/tmp/moguet-outside",
                            (root / "absolute-link").c_str()) != 0) {
                     throw std::runtime_error(
                             "Failed to create absolute symlink fixture.");
                 }
             }},
            {"escaping symlink", "nested/escape-link",
             LocalSourceWorkspaceErrorCode::SymlinkEscape,
             [](const fs::path& root) {
                 create_fixture_directory(root / "nested");
                 if(::symlink(
                            "../../outside",
                            (root / "nested" / "escape-link").c_str()) != 0) {
                     throw std::runtime_error(
                             "Failed to create escaping symlink fixture.");
                 }
             }},
            {"FIFO", "named-pipe",
             LocalSourceWorkspaceErrorCode::UnsupportedFileType,
             [](const fs::path& root) {
                 if(::mkfifo((root / "named-pipe").c_str(), 0600) != 0) {
                     throw std::runtime_error("Failed to create FIFO fixture.");
                 }
             }},
    };

    for(const Scenario& scenario : scenarios) {
        WorkspaceFixture fixture;
        scenario.prepare(fixture.source_path());
        const std::vector<NodeSnapshot> original =
                snapshot_tree(fixture.source_path());
        LocalSourceRoot source_root = fixture.open_source_root();
        ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
        const auto initial_cache_entries =
                direct_child_names(cache_root.canonical_path());

        require_workspace_failure(
                source_root, cache_root,
                LocalSourceWorkspaceStage::SourceInspection, scenario.code,
                scenario.relative_path, scenario.name);
        expect(
                direct_child_names(cache_root.canonical_path()) ==
                        initial_cache_entries,
                scenario.name + ": partial workspace was not cleaned");
        expect(
                snapshot_tree(fixture.source_path()) == original,
                scenario.name + ": failure changed the original tree");
    }
}

void test_group_or_other_writable_entries_are_rejected() {
    struct Scenario {
        std::string name;
        fs::path    relative_path;
        bool        is_directory;
        mode_t      mode;
    };
    const std::vector<Scenario> scenarios = {
            {"group-writable file", "unsafe-file", false, 0664},
            {"other-writable file", "unsafe-file", false, 0646},
            {"group-writable directory", "unsafe-directory", true, 0775},
            {"other-writable directory", "unsafe-directory", true, 0757},
    };

    for(const Scenario& scenario : scenarios) {
        WorkspaceFixture fixture;
        if(scenario.is_directory) {
            create_fixture_directory(
                    fixture.source_path() / scenario.relative_path,
                    scenario.mode);
        } else {
            write_file(
                    fixture.source_path() / scenario.relative_path,
                    "unsafe\n", scenario.mode);
        }
        const std::vector<NodeSnapshot> original =
                snapshot_tree(fixture.source_path());
        LocalSourceRoot source_root = fixture.open_source_root();
        ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
        const auto initial_cache_entries =
                direct_child_names(cache_root.canonical_path());

        require_workspace_failure(
                source_root, cache_root,
                LocalSourceWorkspaceStage::SourceInspection,
                LocalSourceWorkspaceErrorCode::UnsafePermissions,
                scenario.relative_path, scenario.name);
        expect(
                direct_child_names(cache_root.canonical_path()) ==
                        initial_cache_entries,
                scenario.name + ": partial workspace was not cleaned");
        expect(
                snapshot_tree(fixture.source_path()) == original,
                scenario.name + ": failure changed the original tree");
    }
}

void test_file_content_race_is_detected() {
    WorkspaceFixture fixture;
    write_file(fixture.source_path() / "mutable", "before\n");
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());
    bool        hook_ran = false;
    std::string hook_failure;
    ScopedWorkspaceTestHook hook(
            [&](LocalSourceWorkspaceTestEvent event, const fs::path& path) {
                if(event !=
                           LocalSourceWorkspaceTestEvent::AfterFileDataCopied ||
                   path != fs::path("mutable") || hook_ran) {
                    return;
                }
                hook_ran = true;
                try {
                    write_file(
                            fixture.source_path() / "mutable",
                            "changed while copying\n");
                } catch(const std::exception& error) {
                    hook_failure = error.what();
                }
            });

    require_workspace_failure(
            source_root, cache_root,
            LocalSourceWorkspaceStage::SourceRevalidation,
            LocalSourceWorkspaceErrorCode::ContentChanged, "mutable",
            "file content race");
    expect(hook_ran, "File content race hook did not run");
    expect(hook_failure.empty(), "File race hook failed: " + hook_failure);
    expect(
            read_file(fixture.source_path() / "mutable") ==
                    "changed while copying\n",
            "File race fixture mutation was not applied");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "File content race left a partial workspace");
}

void test_entry_set_race_is_detected() {
    WorkspaceFixture fixture;
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());
    bool        hook_ran = false;
    std::string hook_failure;
    ScopedWorkspaceTestHook hook(
            [&](LocalSourceWorkspaceTestEvent event, const fs::path& path) {
                if(event != LocalSourceWorkspaceTestEvent::
                                    BeforeDirectoryRevalidation ||
                   !path.empty() || hook_ran) {
                    return;
                }
                hook_ran = true;
                try {
                    write_file(
                            fixture.source_path() / "late-entry", "late\n");
                } catch(const std::exception& error) {
                    hook_failure = error.what();
                }
            });

    require_workspace_failure(
            source_root, cache_root,
            LocalSourceWorkspaceStage::SourceRevalidation,
            LocalSourceWorkspaceErrorCode::ConcurrentMutation, {},
            "entry-set race");
    expect(hook_ran, "Entry-set race hook did not run");
    expect(hook_failure.empty(), "Entry-set race hook failed: " + hook_failure);
    expect(
            read_file(fixture.source_path() / "late-entry") == "late\n",
            "Entry-set race fixture mutation was not applied");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Entry-set race left a partial workspace");
}

void test_tree_wide_manifest_detects_late_same_size_change() {
    WorkspaceFixture fixture;
    write_file(fixture.source_path() / "early-file", "before\n");
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());
    bool        hook_ran = false;
    std::string hook_failure;
    ScopedWorkspaceTestHook hook(
            [&](LocalSourceWorkspaceTestEvent event, const fs::path& path) {
                if(event != LocalSourceWorkspaceTestEvent::
                                    BeforeDirectoryRevalidation ||
                   !path.empty() || hook_ran) {
                    return;
                }
                hook_ran = true;
                try {
                    write_file(
                            fixture.source_path() / "early-file", "after!\n");
                } catch(const std::exception& error) {
                    hook_failure = error.what();
                }
            });

    require_workspace_failure(
            source_root, cache_root,
            LocalSourceWorkspaceStage::SourceRevalidation,
            LocalSourceWorkspaceErrorCode::ContentChanged, "early-file",
            "tree-wide same-size content race");
    expect(hook_ran, "Tree-wide manifest race hook did not run");
    expect(
            hook_failure.empty(),
            "Tree-wide manifest race hook failed: " + hook_failure);
    expect(
            read_file(fixture.source_path() / "early-file") == "after!\n",
            "Tree-wide manifest race mutation was not applied");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Tree-wide manifest race left a partial workspace");
}

void test_destination_manifest_rejects_same_content_replacement() {
    WorkspaceFixture fixture;
    write_file(fixture.source_path() / "early-file", "stable\n");
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    const auto initial_cache_entries =
            direct_child_names(cache_root.canonical_path());
    bool        hook_ran = false;
    std::string hook_failure;
    ScopedWorkspaceTestHook hook(
            [&](LocalSourceWorkspaceTestEvent event, const fs::path& path) {
                if(event != LocalSourceWorkspaceTestEvent::
                                    BeforeDirectoryRevalidation ||
                   !path.empty() || hook_ran) {
                    return;
                }
                hook_ran = true;
                try {
                    fs::path workspace_path;
                    for(const fs::directory_entry& entry :
                        fs::directory_iterator(cache_root.canonical_path())) {
                        if(entry.path().filename().string().starts_with(
                                   ".local-source-workspace~-")) {
                            workspace_path = entry.path();
                            break;
                        }
                    }
                    if(workspace_path.empty()) {
                        throw std::runtime_error(
                                "Source workspace was not found");
                    }
                    const fs::path replacement =
                            workspace_path / "replacement";
                    write_file(replacement, "stable\n");
                    fs::rename(replacement, workspace_path / "early-file");
                } catch(const std::exception& error) {
                    hook_failure = error.what();
                }
            });

    require_workspace_failure(
            source_root, cache_root,
            LocalSourceWorkspaceStage::WorkspaceRevalidation,
            LocalSourceWorkspaceErrorCode::ConcurrentMutation,
            "early-file", "destination identity replacement");
    expect(hook_ran, "Destination identity hook did not run");
    expect(
            hook_failure.empty(),
            "Destination identity hook failed: " + hook_failure);
    expect(
            read_file(fixture.source_path() / "early-file") == "stable\n",
            "Destination identity fixture changed the source tree");
    expect(
            direct_child_names(cache_root.canonical_path()) ==
                    initial_cache_entries,
            "Destination identity failure left a partial workspace");
}

void test_cache_root_same_or_descendant_is_rejected_pre_creation() {
    {
        TemporaryTree tree;
        const fs::path source_path = make_source_root(tree, "moguet");
        ScopedXdgCacheHome cache_environment(tree.path());
        ValidatedCacheRoot cache_root = prepare_test_trusted_cache_root();
        LocalSourceRoot source_root =
                open_local_source_root(source_path, false);
        expect(
                cache_root.canonical_path() == source_root.canonical_path(),
                "Same-root fixture did not share source/cache path");
        const std::vector<NodeSnapshot> original = snapshot_tree(source_path);

        require_workspace_failure(
                source_root, cache_root,
                LocalSourceWorkspaceStage::BoundaryValidation,
                LocalSourceWorkspaceErrorCode::CacheInsideSource, {},
                "cache root equals source root");
        expect(
                snapshot_tree(source_path) == original,
                "Same-root rejection changed the original tree");
    }

    {
        TemporaryTree tree;
        const fs::path source_path = make_source_root(tree);
        ScopedXdgCacheHome cache_environment(source_path);
        ValidatedCacheRoot cache_root = prepare_test_trusted_cache_root();
        LocalSourceRoot source_root =
                open_local_source_root(source_path, false);
        expect(
                cache_root.canonical_path().parent_path() ==
                        source_root.canonical_path(),
                "Descendant fixture did not place cache below source root");
        const std::vector<NodeSnapshot> original = snapshot_tree(source_path);

        require_workspace_failure(
                source_root, cache_root,
                LocalSourceWorkspaceStage::BoundaryValidation,
                LocalSourceWorkspaceErrorCode::CacheInsideSource, {},
                "cache root below source root");
        expect(
                snapshot_tree(source_path) == original,
                "Descendant-cache rejection changed the original tree");
    }
}

void test_cache_root_identity_alias_is_rejected_pre_creation() {
    TemporaryTree tree;
    const fs::path source_path = make_source_root(tree);
    const fs::path alias_target = source_path / "cache-alias-target";
    create_fixture_directory(alias_target);
    LocalSourceRoot source_root =
            open_local_source_root(source_path, false);
    const struct stat alias_status = node_status(alias_target);
    const std::vector<NodeSnapshot> original = snapshot_tree(source_path);

    bool rejected = false;
    try {
        require_cache_identity_outside_source_tree_for_test(
                source_root,
                static_cast<std::uintmax_t>(alias_status.st_dev),
                static_cast<std::uintmax_t>(alias_status.st_ino));
    } catch(const LocalSourceWorkspaceError& error) {
        rejected = true;
        expect(
                error.failure().stage ==
                        LocalSourceWorkspaceStage::BoundaryValidation,
                "Identity-alias rejection used the wrong stage");
        expect(
                error.failure().code ==
                        LocalSourceWorkspaceErrorCode::CacheInsideSource,
                "Identity-alias rejection used the wrong code");
        expect(
                error.failure().relative_path == "cache-alias-target",
                "Identity-alias rejection lost the source-relative path");
    }
    expect(rejected, "Source descendant cache identity alias was accepted");
    expect(
            snapshot_tree(source_path) == original,
            "Identity-alias rejection changed the original tree");
}

void test_cleanup_rebuilds_named_lineage_before_removal() {
    WorkspaceFixture fixture;
    LocalSourceRoot source_root = fixture.open_source_root();
    ValidatedCacheRoot cache_root = fixture.prepare_cache_root();
    LocalSourceWorkspace workspace = materialize_local_source_workspace(
            source_root, cache_root);
    const fs::path workspace_path = workspace.path();
    const fs::path outside_parent =
            cache_root.canonical_path().parent_path();
    const fs::path moved_workspace = outside_parent / "moved-workspace";
    const fs::path replacement_backup =
            outside_parent / "replacement-workspace";
    bool        hook_ran = false;
    std::string hook_failure;
    bool        cleanup_failed = false;

    {
        ScopedWorkspaceTestHook hook(
                [&](LocalSourceWorkspaceTestEvent event, const fs::path&) {
                    if(event != LocalSourceWorkspaceTestEvent::
                                        BeforeCleanupRemoval ||
                       hook_ran) {
                        return;
                    }
                    hook_ran = true;
                    try {
                        fs::rename(workspace_path, moved_workspace);
                        create_fixture_directory(workspace_path);
                        write_file(
                                workspace_path / "replacement-marker",
                                "replacement\n");
                    } catch(const std::exception& error) {
                        hook_failure = error.what();
                    }
                });
        try {
            workspace.cleanup();
        } catch(const LocalSourceWorkspaceError& error) {
            cleanup_failed = true;
            expect(
                    error.failure().stage ==
                                    LocalSourceWorkspaceStage::Cleanup &&
                            error.failure().code ==
                                    LocalSourceWorkspaceErrorCode::
                                            CleanupFailure,
                    "Moved-lineage cleanup used the wrong typed failure");
        }
    }

    expect(hook_ran, "Cleanup lineage hook did not run");
    expect(
            hook_failure.empty(),
            "Cleanup lineage hook failed: " + hook_failure);
    expect(cleanup_failed, "Moved cleanup lineage was accepted");
    expect(
            read_file(moved_workspace / "PKGBUILD") == PKGBUILD_CONTENT,
            "Cleanup deleted content through a moved retained workspace FD");
    expect(
            read_file(workspace_path / "replacement-marker") ==
                    "replacement\n",
            "Cleanup changed the replacement at the original cache name");

    fs::rename(workspace_path, replacement_backup);
    fs::rename(moved_workspace, workspace_path);
    workspace.cleanup();
    expect(
            !fs::exists(workspace_path),
            "Restored source workspace was not cleaned");
    expect(
            read_file(replacement_backup / "replacement-marker") ==
                    "replacement\n",
            "Cleanup changed the displaced replacement directory");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "full snapshot and explicit cleanup",
                test_full_snapshot_and_explicit_cleanup);
        run_case("destructor cleanup", test_destructor_cleanup);
        run_case(
                "read-only directory cleanup capability",
                test_read_only_source_directory_remains_cleanup_capable);
        run_case(
                "unsafe symlinks and FIFO",
                test_unsafe_symlinks_and_special_file_are_rejected);
        run_case(
                "unsafe regular entry permissions",
                test_group_or_other_writable_entries_are_rejected);
        run_case("file content race", test_file_content_race_is_detected);
        run_case("entry-set race", test_entry_set_race_is_detected);
        run_case(
                "tree-wide same-size content race",
                test_tree_wide_manifest_detects_late_same_size_change);
        run_case(
                "destination identity replacement",
                test_destination_manifest_rejects_same_content_replacement);
        run_case(
                "cache root source containment",
                test_cache_root_same_or_descendant_is_rejected_pre_creation);
        run_case(
                "cache root identity alias containment",
                test_cache_root_identity_alias_is_rejected_pre_creation);
        run_case(
                "cleanup named lineage reconstruction",
                test_cleanup_rebuilds_named_lineage_before_removal);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "local source workspace tests: all checks passed\n";
    return 0;
}
