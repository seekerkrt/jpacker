#pragma once

#include "xdg_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
#include <functional>
#endif

class Logger;

namespace xdg_directory_safety {
class PreparedDirectory;
}

namespace xdg_state_log {

enum class StateLogStage {
    BoundaryValidation,
    DirectoryRevalidation,
    DirectoryDescriptorDuplication,
    FileInspection,
    FileCreation,
    FileOpen,
    DescriptorValidation,
    NameRevalidation,
    DescriptorAdoption,
    RecordWrite,
    DescriptorClose,
};

enum class StateLogErrorCode {
    InvalidStateLogBoundary,
    Symlink,
    NotRegularFile,
    OwnershipMismatch,
    UnsafePermissions,
    MultipleHardLinks,
    PermissionDenied,
    OpenFailed,
    MetadataFailure,
    ConcurrentReplacement,
    DescriptorAdoptionFailure,
    WriteFailure,
    CloseFailure,
};

struct StateLogFailure {
    xdg_paths::DirectoryKind directory_kind =
            xdg_paths::DirectoryKind::State;
    StateLogStage     stage = StateLogStage::BoundaryValidation;
    StateLogErrorCode code =
            StateLogErrorCode::InvalidStateLogBoundary;
    std::optional<std::error_code> system_error;
};

class StateLogError final : public std::runtime_error {
    StateLogFailure failure_;

public:
    explicit StateLogError(StateLogFailure failure);

    const StateLogFailure& failure() const noexcept {
        return failure_;
    }
};

struct StateLogDirectoryAccess;
struct StateLogFileAccess;
struct StateLogTestAccess;
class StateLogLoggerBackend;

// Validated state directoryから固定moguet.logをopenした結果。fileと
// parent directoryのdescriptorを一体で保持し、path-based reopenを許さない。
class PreparedLogFile final {
    xdg_paths::DirectoryKind directory_kind_ =
            xdg_paths::DirectoryKind::State;
    std::filesystem::path logical_path_;
    std::string           filename_;
    int                   directory_descriptor_ = -1;
    int                   file_descriptor_ = -1;
    std::uintmax_t        directory_device_ = 0;
    std::uintmax_t        directory_inode_ = 0;
    std::uintmax_t        owner_ = 0;
    std::uintmax_t        permissions_ = 0;
    std::uintmax_t        device_ = 0;
    std::uintmax_t        inode_ = 0;
    bool                  created_ = false;
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    int test_record_write_error_ = 0;
    int test_file_close_error_ = 0;
    int test_directory_close_error_ = 0;
#endif

    PreparedLogFile(
            std::filesystem::path logical_path, std::string filename,
            int directory_descriptor, int file_descriptor,
            std::uintmax_t directory_device,
            std::uintmax_t directory_inode, std::uintmax_t owner,
            std::uintmax_t permissions, std::uintmax_t device,
            std::uintmax_t inode, bool created) noexcept;

    void close_descriptors() noexcept;
    void require_adoption_ready_for_logger() const;
    void append_record_for_logger(std::string_view record) const;
    void close_checked_for_logger();

    friend struct StateLogFileAccess;
    friend struct StateLogTestAccess;
    friend class StateLogLoggerBackend;
    friend class ::Logger;

public:
    PreparedLogFile(const PreparedLogFile&) = delete;
    PreparedLogFile& operator=(const PreparedLogFile&) = delete;
    PreparedLogFile(PreparedLogFile&& other) noexcept;
    PreparedLogFile& operator=(PreparedLogFile&& other) noexcept;
    ~PreparedLogFile() noexcept;

    xdg_paths::DirectoryKind directory_kind() const noexcept {
        return directory_kind_;
    }

    const std::filesystem::path& logical_path() const noexcept {
        return logical_path_;
    }

    std::uintmax_t owner() const noexcept {
        return owner_;
    }

    std::uintmax_t permissions() const noexcept {
        return permissions_;
    }

    std::uintmax_t device() const noexcept {
        return device_;
    }

    std::uintmax_t inode() const noexcept {
        return inode_;
    }

    bool created() const noexcept {
        return created_;
    }

    void require_unchanged_identity() const;
};

// arbitrary filenameを受け取らず、StatePathsが固定したmoguet.logだけを
// PreparedDirectoryのretained descriptorからopenする。
PreparedLogFile open_default_state_log(
        const xdg_paths::StatePaths& paths,
        const xdg_directory_safety::PreparedDirectory& directory);

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
enum class StateLogTestEvent {
    AfterMissingObservation,
    AfterInitialMetadata,
    AfterFileOpen,
    BeforeNameRevalidation,
};

enum class StateLogTestFailurePoint {
    DirectoryDescriptorDuplication,
    InitialMetadata,
    FileCreation,
    FileOpen,
    DescriptorMetadata,
    NameRevalidation,
    RecordWrite,
    FileClose,
    DirectoryClose,
};

enum class StateLogTestObservedType {
    CharacterDevice,
    Socket,
};

struct StateLogInjectedFailure {
    StateLogTestFailurePoint failure_point;
    int                      error_number;
};

using StateLogTestEventHook = std::function<void(
        StateLogTestEvent, const std::filesystem::path&)>;

// Focused test binaryだけがowner observation、syscall failure、race timing、
// unprivilegedに作れないfile typeを差し替える。
struct StateLogTestOverrides {
    std::optional<std::uintmax_t> observed_owner;
    std::optional<StateLogInjectedFailure> injected_failure;
    std::optional<StateLogTestObservedType> observed_type;
    StateLogTestEventHook event_hook;
};

PreparedLogFile open_default_state_log_for_test(
        const xdg_paths::StatePaths& paths,
        const xdg_directory_safety::PreparedDirectory& directory,
        const StateLogTestOverrides& overrides);

int state_log_file_descriptor_for_test(
        const PreparedLogFile& log_file) noexcept;
int state_log_directory_descriptor_for_test(
        const PreparedLogFile& log_file) noexcept;
#endif

} // namespace xdg_state_log
