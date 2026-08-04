#pragma once

#include "local_source_root.hpp"
#include "trusted_cache.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>

#ifdef MOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS
#include <functional>
#endif

enum class LocalSourceWorkspaceStage {
    BoundaryValidation,
    NameGeneration,
    WorkspaceCreation,
    SourceEnumeration,
    SourceInspection,
    SourceOpen,
    SourceRead,
    DestinationCreation,
    DestinationWrite,
    MetadataPreservation,
    SourceRevalidation,
    WorkspaceRevalidation,
    Cleanup
};

enum class LocalSourceWorkspaceErrorCode {
    CacheInsideSource,
    RandomnessUnavailable,
    NameCollision,
    UnsafeName,
    UnsupportedFileType,
    OwnershipMismatch,
    UnsafePermissions,
    FilesystemBoundary,
    SymlinkEscape,
    ConcurrentMutation,
    ContentChanged,
    PermissionDenied,
    MetadataFailure,
    ReadFailure,
    WriteFailure,
    InvalidState,
    CleanupFailure
};

struct LocalSourceWorkspaceFailure {
    LocalSourceWorkspaceStage stage =
            LocalSourceWorkspaceStage::SourceInspection;
    LocalSourceWorkspaceErrorCode code =
            LocalSourceWorkspaceErrorCode::MetadataFailure;
    std::filesystem::path          relative_path;
    std::optional<std::error_code> system_error;

    bool operator==(const LocalSourceWorkspaceFailure&) const = default;
};

// Presentation is owned by the CLI composition boundary. This exception
// carries typed control-flow data and a stable internal what() token only.
class LocalSourceWorkspaceError final : public std::runtime_error {
    LocalSourceWorkspaceFailure failure_;

public:
    explicit LocalSourceWorkspaceError(LocalSourceWorkspaceFailure failure);

    const LocalSourceWorkspaceFailure& failure() const noexcept {
        return failure_;
    }
};

struct LocalSourceBuildAccess;

class LocalSourceWorkspace final {
    enum class State {
        Active,
        Cleaned,
        MovedFrom
    };

    // The retained directory closes before the rollback guard runs.
    std::unique_ptr<DirCleanupGuard> cleanup_guard_;
    RetainedTrustedCacheDirectory   directory_;
    State                           state_ = State::Active;

    LocalSourceWorkspace(
            std::unique_ptr<DirCleanupGuard> cleanup_guard,
            RetainedTrustedCacheDirectory directory) noexcept;

    friend LocalSourceWorkspace materialize_local_source_workspace(
            const LocalSourceRoot& source_root,
            const ValidatedCacheRoot& cache_root);
    friend struct LocalSourceBuildAccess;

public:
    LocalSourceWorkspace(const LocalSourceWorkspace&) = delete;
    LocalSourceWorkspace& operator=(const LocalSourceWorkspace&) = delete;
    LocalSourceWorkspace(LocalSourceWorkspace&& other) noexcept;
    LocalSourceWorkspace& operator=(LocalSourceWorkspace&&) = delete;
    ~LocalSourceWorkspace() noexcept;

    const std::filesystem::path& path() const noexcept;

    void require_unchanged_identity() const;
    void cleanup();
};

LocalSourceWorkspace materialize_local_source_workspace(
        const LocalSourceRoot& source_root,
        const ValidatedCacheRoot& cache_root);

#ifdef MOGUET_ENABLE_LOCAL_SOURCE_WORKSPACE_TEST_HOOKS
enum class LocalSourceWorkspaceTestEvent {
    AfterFileDataCopied,
    BeforeDirectoryRevalidation,
    BeforeCleanupRemoval
};

using LocalSourceWorkspaceTestHook = std::function<void(
        LocalSourceWorkspaceTestEvent event,
        const std::filesystem::path& relative_path)>;

void set_local_source_workspace_test_hook(
        LocalSourceWorkspaceTestHook hook);

void require_cache_identity_outside_source_tree_for_test(
        const LocalSourceRoot& source_root,
        std::uintmax_t cache_device, std::uintmax_t cache_inode);
#endif
