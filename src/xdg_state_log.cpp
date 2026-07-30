#include "xdg_state_log.hpp"

#include "application_identity.hpp"
#include "logging.hpp"
#include "xdg_directory_safety.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace xdg_state_log {
namespace {

namespace fs = std::filesystem;

constexpr mode_t PRIVATE_LOG_MODE = S_IRUSR | S_IWUSR;
constexpr mode_t DIRECTORY_REQUIRED_OWNER_PERMISSIONS =
        S_IRUSR | S_IWUSR | S_IXUSR;
constexpr mode_t DIRECTORY_FORBIDDEN_WRITE_PERMISSIONS =
        S_IWGRP | S_IWOTH;

class OwnedFileDescriptor final {
    int descriptor_ = -1;

public:
    explicit OwnedFileDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {
    }

    OwnedFileDescriptor(const OwnedFileDescriptor&) = delete;
    OwnedFileDescriptor& operator=(const OwnedFileDescriptor&) = delete;

    OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }

    OwnedFileDescriptor& operator=(OwnedFileDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }

    ~OwnedFileDescriptor() noexcept {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

struct DuplicatedDirectory {
    OwnedFileDescriptor descriptor;
    std::uintmax_t      device = 0;
    std::uintmax_t      inode = 0;
    std::uintmax_t      owner = 0;
};

struct OpenedLogFile {
    OwnedFileDescriptor descriptor;
    struct stat         status {};
    bool                created = false;
};

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
using TestOverrides = StateLogTestOverrides;
#else
struct TestOverrides final {};
#endif

std::string default_log_filename() {
    std::string filename(application_identity::XDG_IDENTITY);
    filename += ".log";
    return filename;
}

std::string_view state_log_stage_name(StateLogStage stage) {
    switch(stage) {
    case StateLogStage::BoundaryValidation:
        return "boundary validation";
    case StateLogStage::DirectoryRevalidation:
        return "state-directory revalidation";
    case StateLogStage::DirectoryDescriptorDuplication:
        return "state-directory descriptor duplication";
    case StateLogStage::FileInspection:
        return "file inspection";
    case StateLogStage::FileCreation:
        return "file creation";
    case StateLogStage::FileOpen:
        return "file open";
    case StateLogStage::DescriptorValidation:
        return "descriptor validation";
    case StateLogStage::NameRevalidation:
        return "parent-relative name revalidation";
    case StateLogStage::DescriptorAdoption:
        return "logger descriptor adoption";
    case StateLogStage::RecordWrite:
        return "log record write";
    case StateLogStage::DescriptorClose:
        return "descriptor close";
    }
    throw std::logic_error("Unknown state log stage.");
}

std::string_view state_log_error_description(StateLogErrorCode code) {
    switch(code) {
    case StateLogErrorCode::InvalidStateLogBoundary:
        return "the state log boundary is invalid";
    case StateLogErrorCode::Symlink:
        return "the default state log must not be a symlink";
    case StateLogErrorCode::NotRegularFile:
        return "the default state log is not a regular file";
    case StateLogErrorCode::OwnershipMismatch:
        return "file ownership does not match the state-directory authority";
    case StateLogErrorCode::UnsafePermissions:
        return "file permissions are not private mode 0600";
    case StateLogErrorCode::MultipleHardLinks:
        return "the default state log has multiple hard links";
    case StateLogErrorCode::PermissionDenied:
        return "filesystem permission was denied";
    case StateLogErrorCode::OpenFailed:
        return "the default state log could not be opened safely";
    case StateLogErrorCode::MetadataFailure:
        return "filesystem metadata could not be obtained safely";
    case StateLogErrorCode::ConcurrentReplacement:
        return "the default state log changed during validation";
    case StateLogErrorCode::DescriptorAdoptionFailure:
        return "the validated descriptor cannot be adopted by the logger";
    case StateLogErrorCode::WriteFailure:
        return "the validated descriptor could not accept a log record";
    case StateLogErrorCode::CloseFailure:
        return "the validated descriptor could not be closed cleanly";
    }
    throw std::logic_error("Unknown state log error code.");
}

std::string state_log_diagnostic(const StateLogFailure& failure) {
    std::string description;
    if(failure.code == StateLogErrorCode::OwnershipMismatch &&
       (failure.stage == StateLogStage::DirectoryRevalidation ||
        failure.stage == StateLogStage::DirectoryDescriptorDuplication)) {
        description =
                "state-directory ownership no longer matches its authority";
    } else if(
            failure.code == StateLogErrorCode::UnsafePermissions &&
            (failure.stage == StateLogStage::DirectoryRevalidation ||
             failure.stage ==
                     StateLogStage::DirectoryDescriptorDuplication)) {
        description = "state-directory permissions are unsafe";
    } else {
        description = state_log_error_description(failure.code);
    }
    std::string diagnostic =
            "Cannot safely use " +
            std::string(application_identity::PROJECT_NAME) +
            " default state log during " +
            std::string(state_log_stage_name(failure.stage)) + ": " +
            description + ".";
    if(failure.system_error.has_value()) {
        diagnostic +=
                " System error: " + failure.system_error->message() + ".";
    }
    return diagnostic;
}

[[noreturn]] void throw_state_log_error(
        StateLogStage stage, StateLogErrorCode code,
        std::optional<int> error_number = std::nullopt) {
    std::optional<std::error_code> system_error;
    if(error_number.has_value()) {
        system_error =
                std::error_code(error_number.value(), std::generic_category());
    }
    throw StateLogError(StateLogFailure{
            xdg_paths::DirectoryKind::State, stage, code, system_error});
}

bool is_permission_error(int error_number) {
    return error_number == EACCES || error_number == EPERM ||
           error_number == EROFS;
}

bool is_replacement_error(int error_number) {
    return error_number == ENOENT || error_number == ENOTDIR ||
           error_number == ELOOP || error_number == EISDIR ||
           error_number == ENXIO;
}

std::uintmax_t status_device(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_dev);
}

std::uintmax_t status_inode(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_ino);
}

std::uintmax_t status_owner(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_uid);
}

std::uintmax_t status_permissions(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_mode & 07777);
}

bool same_filesystem_identity(
        const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev &&
           expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

void validate_state_log_boundary(
        const xdg_paths::StatePaths& paths,
        const xdg_directory_safety::PreparedDirectory& directory) {
    const std::string filename = default_log_filename();
    if(directory.directory_kind() != xdg_paths::DirectoryKind::State ||
       directory.path() != paths.directory ||
       paths.default_log_file.parent_path() != paths.directory ||
       paths.default_log_file.filename() != filename ||
       paths.default_log_file != paths.directory / filename) {
        throw_state_log_error(
                StateLogStage::BoundaryValidation,
                StateLogErrorCode::InvalidStateLogBoundary);
    }
}

void translate_directory_revalidation_error(
        const xdg_directory_safety::PreparationError& error) {
    const xdg_directory_safety::PreparationFailure& failure = error.failure();
    StateLogErrorCode code = StateLogErrorCode::MetadataFailure;
    switch(failure.code) {
    case xdg_directory_safety::PreparationErrorCode::OwnershipMismatch:
        code = StateLogErrorCode::OwnershipMismatch;
        break;
    case xdg_directory_safety::PreparationErrorCode::UnsafePermissions:
        code = StateLogErrorCode::UnsafePermissions;
        break;
    case xdg_directory_safety::PreparationErrorCode::PermissionDenied:
        code = StateLogErrorCode::PermissionDenied;
        break;
    case xdg_directory_safety::PreparationErrorCode::Symlink:
    case xdg_directory_safety::PreparationErrorCode::NotDirectory:
    case xdg_directory_safety::PreparationErrorCode::ConcurrentReplacement:
    case xdg_directory_safety::PreparationErrorCode::MissingAnchor:
        code = StateLogErrorCode::ConcurrentReplacement;
        break;
    case xdg_directory_safety::PreparationErrorCode::CreationFailed:
    case xdg_directory_safety::PreparationErrorCode::MetadataFailure:
    case xdg_directory_safety::PreparationErrorCode::InvalidCreationBoundary:
        code = StateLogErrorCode::MetadataFailure;
        break;
    }
    throw StateLogError(StateLogFailure{
            xdg_paths::DirectoryKind::State,
            StateLogStage::DirectoryRevalidation, code,
            failure.system_error});
}

void require_prepared_directory_unchanged(
        const xdg_directory_safety::PreparedDirectory& directory) {
    try {
        directory.require_unchanged_identity();
    } catch(const xdg_directory_safety::PreparationError& error) {
        translate_directory_revalidation_error(error);
    }
}

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
bool inject_failure(
        const TestOverrides* overrides,
        StateLogTestFailurePoint failure_point) {
    if(overrides == nullptr || !overrides->injected_failure.has_value())
        return false;
    const StateLogInjectedFailure& failure =
            overrides->injected_failure.value();
    if(failure.failure_point != failure_point) return false;
    errno = failure.error_number;
    return true;
}

void emit_test_event(
        const TestOverrides* overrides, StateLogTestEvent event,
        const fs::path& path) {
    if(overrides != nullptr && overrides->event_hook)
        overrides->event_hook(event, path);
}

void apply_observed_type_override(
        struct stat& status, const TestOverrides* overrides) {
    if(overrides == nullptr || !overrides->observed_type.has_value()) return;
    switch(overrides->observed_type.value()) {
    case StateLogTestObservedType::CharacterDevice:
        status.st_mode =
                (status.st_mode & ~static_cast<mode_t>(S_IFMT)) | S_IFCHR;
        return;
    case StateLogTestObservedType::Socket:
        status.st_mode =
                (status.st_mode & ~static_cast<mode_t>(S_IFMT)) | S_IFSOCK;
        return;
    }
}
#else
void emit_test_event(const TestOverrides*, int, const fs::path&) {
}

void apply_observed_type_override(struct stat&, const TestOverrides*) {
}
#endif

std::uintmax_t observed_owner(
        const struct stat& status, const TestOverrides* overrides) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    if(overrides != nullptr && overrides->observed_owner.has_value())
        return overrides->observed_owner.value();
#else
    static_cast<void>(overrides);
#endif
    return status_owner(status);
}

void validate_log_file_status(
        const struct stat& status, std::uintmax_t expected_owner,
        StateLogStage stage, const TestOverrides* overrides) {
    if(S_ISLNK(status.st_mode)) {
        throw_state_log_error(stage, StateLogErrorCode::Symlink);
    }
    if(!S_ISREG(status.st_mode)) {
        throw_state_log_error(stage, StateLogErrorCode::NotRegularFile);
    }
    if(observed_owner(status, overrides) != expected_owner) {
        throw_state_log_error(
                stage, StateLogErrorCode::OwnershipMismatch);
    }
    if((status.st_mode & 07777) != PRIVATE_LOG_MODE) {
        throw_state_log_error(
                stage, StateLogErrorCode::UnsafePermissions);
    }
    if(status.st_nlink != 1) {
        throw_state_log_error(
                stage, StateLogErrorCode::MultipleHardLinks);
    }
}

std::optional<struct stat> inspect_log_name(
        int directory_descriptor, const std::string& filename,
        const TestOverrides* overrides, bool allow_type_override) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    const bool injected_metadata_failure = inject_failure(
            overrides, StateLogTestFailurePoint::InitialMetadata);
#else
    static_cast<void>(allow_type_override);
    const bool injected_metadata_failure = false;
#endif
    struct stat status {};
    if(!injected_metadata_failure &&
       fstatat(
               directory_descriptor, filename.c_str(), &status,
               AT_SYMLINK_NOFOLLOW) == 0) {
        if(allow_type_override)
            apply_observed_type_override(status, overrides);
        return status;
    }
    const int metadata_error = errno;
    if(metadata_error == ENOENT) return std::nullopt;
    throw_state_log_error(
            StateLogStage::FileInspection,
            is_permission_error(metadata_error)
                    ? StateLogErrorCode::PermissionDenied
                    : StateLogErrorCode::MetadataFailure,
            metadata_error);
}

struct stat descriptor_status(
        int file_descriptor, const TestOverrides* overrides) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    const bool injected_metadata_failure = inject_failure(
            overrides, StateLogTestFailurePoint::DescriptorMetadata);
#else
    static_cast<void>(overrides);
    const bool injected_metadata_failure = false;
#endif
    struct stat status {};
    if(injected_metadata_failure || fstat(file_descriptor, &status) != 0) {
        const int metadata_error = errno;
        throw_state_log_error(
                StateLogStage::DescriptorValidation,
                is_permission_error(metadata_error)
                        ? StateLogErrorCode::PermissionDenied
                        : StateLogErrorCode::MetadataFailure,
                metadata_error);
    }
    return status;
}

struct stat revalidate_log_name(
        int directory_descriptor, const std::string& filename,
        const TestOverrides* overrides) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    const bool injected_metadata_failure = inject_failure(
            overrides, StateLogTestFailurePoint::NameRevalidation);
#else
    static_cast<void>(overrides);
    const bool injected_metadata_failure = false;
#endif
    struct stat status {};
    if(injected_metadata_failure ||
       fstatat(
               directory_descriptor, filename.c_str(), &status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        const int metadata_error = errno;
        throw_state_log_error(
                StateLogStage::NameRevalidation,
                is_replacement_error(metadata_error)
                        ? StateLogErrorCode::ConcurrentReplacement
                        : (is_permission_error(metadata_error)
                                   ? StateLogErrorCode::PermissionDenied
                                   : StateLogErrorCode::MetadataFailure),
                metadata_error);
    }
    return status;
}

OpenedLogFile finish_opened_log_file(
        OwnedFileDescriptor opened,
        const struct stat* initial_status,
        int directory_descriptor, const std::string& filename,
        std::uintmax_t expected_owner, bool created,
        const TestOverrides* overrides, const fs::path& logical_path) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    emit_test_event(
            overrides, StateLogTestEvent::AfterFileOpen, logical_path);
#else
    emit_test_event(overrides, 0, logical_path);
#endif
    const struct stat opened_status =
            descriptor_status(opened.get(), overrides);
    if(initial_status != nullptr &&
       !same_filesystem_identity(*initial_status, opened_status)) {
        throw_state_log_error(
                StateLogStage::DescriptorValidation,
                StateLogErrorCode::ConcurrentReplacement);
    }
    validate_log_file_status(
            opened_status, expected_owner,
            StateLogStage::DescriptorValidation, overrides);

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    emit_test_event(
            overrides, StateLogTestEvent::BeforeNameRevalidation,
            logical_path);
#else
    emit_test_event(overrides, 0, logical_path);
#endif
    const struct stat named_status = revalidate_log_name(
            directory_descriptor, filename, overrides);
    if(!same_filesystem_identity(opened_status, named_status)) {
        throw_state_log_error(
                StateLogStage::NameRevalidation,
                StateLogErrorCode::ConcurrentReplacement);
    }
    validate_log_file_status(
            named_status, expected_owner,
            StateLogStage::NameRevalidation, overrides);
    return OpenedLogFile{
            std::move(opened), opened_status, created};
}

OpenedLogFile open_existing_log_file(
        int directory_descriptor, const std::string& filename,
        const struct stat& initial_status, std::uintmax_t expected_owner,
        const TestOverrides* overrides, const fs::path& logical_path) {
    validate_log_file_status(
            initial_status, expected_owner,
            StateLogStage::FileInspection, overrides);

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    const bool injected_open_failure =
            inject_failure(overrides, StateLogTestFailurePoint::FileOpen);
#else
    const bool injected_open_failure = false;
#endif
    const int descriptor = injected_open_failure
                                   ? -1
                                   : openat(
                                             directory_descriptor,
                                             filename.c_str(),
                                             O_WRONLY | O_APPEND | O_CLOEXEC |
                                                     O_NOFOLLOW | O_NONBLOCK);
    if(descriptor < 0) {
        const int open_error = errno;
        throw_state_log_error(
                StateLogStage::FileOpen,
                is_replacement_error(open_error)
                        ? StateLogErrorCode::ConcurrentReplacement
                        : (is_permission_error(open_error)
                                   ? StateLogErrorCode::PermissionDenied
                                   : StateLogErrorCode::OpenFailed),
                open_error);
    }
    return finish_opened_log_file(
            OwnedFileDescriptor(descriptor), &initial_status,
            directory_descriptor, filename, expected_owner, false,
            overrides, logical_path);
}

OpenedLogFile create_log_file(
        int directory_descriptor, const std::string& filename,
        std::uintmax_t expected_owner, const TestOverrides* overrides,
        const fs::path& logical_path) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    const bool injected_creation_failure = inject_failure(
            overrides, StateLogTestFailurePoint::FileCreation);
#else
    const bool injected_creation_failure = false;
#endif
    const int descriptor = injected_creation_failure
                                   ? -1
                                   : openat(
                                             directory_descriptor,
                                             filename.c_str(),
                                             O_WRONLY | O_APPEND | O_CLOEXEC |
                                                     O_NOFOLLOW | O_NONBLOCK |
                                                     O_CREAT | O_EXCL,
                                             PRIVATE_LOG_MODE);
    if(descriptor < 0) {
        const int creation_error = errno;
        if(creation_error == EEXIST) {
            std::optional<struct stat> appeared = inspect_log_name(
                    directory_descriptor, filename, overrides, true);
            if(!appeared.has_value()) {
                throw_state_log_error(
                        StateLogStage::FileCreation,
                        StateLogErrorCode::ConcurrentReplacement);
            }
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
            emit_test_event(
                    overrides, StateLogTestEvent::AfterInitialMetadata,
                    logical_path);
#else
            emit_test_event(overrides, 0, logical_path);
#endif
            return open_existing_log_file(
                    directory_descriptor, filename, appeared.value(),
                    expected_owner, overrides, logical_path);
        }
        throw_state_log_error(
                StateLogStage::FileCreation,
                is_permission_error(creation_error)
                        ? StateLogErrorCode::PermissionDenied
                        : (is_replacement_error(creation_error)
                                   ? StateLogErrorCode::ConcurrentReplacement
                                   : StateLogErrorCode::OpenFailed),
                creation_error);
    }
    return finish_opened_log_file(
            OwnedFileDescriptor(descriptor), nullptr,
            directory_descriptor, filename, expected_owner, true,
            overrides, logical_path);
}

OpenedLogFile open_log_file(
        int directory_descriptor, const std::string& filename,
        std::uintmax_t expected_owner, const TestOverrides* overrides,
        const fs::path& logical_path) {
    std::optional<struct stat> initial_status = inspect_log_name(
            directory_descriptor, filename, overrides, true);
    if(!initial_status.has_value()) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
        emit_test_event(
                overrides, StateLogTestEvent::AfterMissingObservation,
                logical_path);
#else
        emit_test_event(overrides, 0, logical_path);
#endif
        return create_log_file(
                directory_descriptor, filename, expected_owner, overrides,
                logical_path);
    }
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    emit_test_event(
            overrides, StateLogTestEvent::AfterInitialMetadata,
            logical_path);
#else
    emit_test_event(overrides, 0, logical_path);
#endif
    return open_existing_log_file(
            directory_descriptor, filename, initial_status.value(),
            expected_owner, overrides, logical_path);
}

void validate_retained_directory(
        int directory_descriptor, std::uintmax_t expected_device,
        std::uintmax_t expected_inode, std::uintmax_t expected_owner) {
    struct stat status {};
    if(fstat(directory_descriptor, &status) != 0) {
        const int metadata_error = errno;
        throw_state_log_error(
                StateLogStage::DirectoryRevalidation,
                is_permission_error(metadata_error)
                        ? StateLogErrorCode::PermissionDenied
                        : StateLogErrorCode::MetadataFailure,
                metadata_error);
    }
    if(!S_ISDIR(status.st_mode) || status_device(status) != expected_device ||
       status_inode(status) != expected_inode) {
        throw_state_log_error(
                StateLogStage::DirectoryRevalidation,
                StateLogErrorCode::ConcurrentReplacement);
    }
    const mode_t permissions = status.st_mode & 07777;
    if(status_owner(status) != expected_owner) {
        throw_state_log_error(
                StateLogStage::DirectoryRevalidation,
                StateLogErrorCode::OwnershipMismatch);
    }
    if((permissions & DIRECTORY_REQUIRED_OWNER_PERMISSIONS) !=
               DIRECTORY_REQUIRED_OWNER_PERMISSIONS ||
       (permissions & DIRECTORY_FORBIDDEN_WRITE_PERMISSIONS) != 0) {
        throw_state_log_error(
                StateLogStage::DirectoryRevalidation,
                StateLogErrorCode::UnsafePermissions);
    }
}

} // namespace

struct StateLogDirectoryAccess {
    static DuplicatedDirectory duplicate(
            const xdg_directory_safety::PreparedDirectory& directory,
            const TestOverrides* overrides) {
        require_prepared_directory_unchanged(directory);

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
        const bool injected_duplication_failure = inject_failure(
                overrides,
                StateLogTestFailurePoint::DirectoryDescriptorDuplication);
#else
        static_cast<void>(overrides);
        const bool injected_duplication_failure = false;
#endif
        const int descriptor = injected_duplication_failure
                                       ? -1
                                       : fcntl(
                                                 directory.directory_descriptor_,
                                                 F_DUPFD_CLOEXEC, 0);
        if(descriptor < 0) {
            const int duplication_error = errno;
            throw_state_log_error(
                    StateLogStage::DirectoryDescriptorDuplication,
                    is_permission_error(duplication_error)
                            ? StateLogErrorCode::PermissionDenied
                            : StateLogErrorCode::MetadataFailure,
                    duplication_error);
        }
        OwnedFileDescriptor duplicated(descriptor);
        struct stat status {};
        if(fstat(duplicated.get(), &status) != 0) {
            const int metadata_error = errno;
            throw_state_log_error(
                    StateLogStage::DirectoryDescriptorDuplication,
                    is_permission_error(metadata_error)
                            ? StateLogErrorCode::PermissionDenied
                            : StateLogErrorCode::MetadataFailure,
                    metadata_error);
        }
        if(!S_ISDIR(status.st_mode) ||
           status_device(status) != directory.device_ ||
           status_inode(status) != directory.inode_) {
            throw_state_log_error(
                    StateLogStage::DirectoryDescriptorDuplication,
                    StateLogErrorCode::ConcurrentReplacement);
        }
        if(status_owner(status) != directory.filesystem_owner_) {
            throw_state_log_error(
                    StateLogStage::DirectoryDescriptorDuplication,
                    StateLogErrorCode::OwnershipMismatch);
        }
        return DuplicatedDirectory{
                std::move(duplicated), status_device(status),
                status_inode(status), status_owner(status)};
    }
};

struct StateLogFileAccess {
    static PreparedLogFile open(
            const xdg_paths::StatePaths& paths,
            const xdg_directory_safety::PreparedDirectory& directory,
            const TestOverrides* overrides) {
        validate_state_log_boundary(paths, directory);
        DuplicatedDirectory duplicated =
                StateLogDirectoryAccess::duplicate(directory, overrides);
        const std::string filename = default_log_filename();
        OpenedLogFile opened = open_log_file(
                duplicated.descriptor.get(), filename, duplicated.owner,
                overrides, paths.default_log_file);

        // Directoryのnamed chainとfileのparent-relative nameを、return直前に
        // もう一度固定する。file自体はabsolute pathから再openしない。
        require_prepared_directory_unchanged(directory);
        validate_retained_directory(
                duplicated.descriptor.get(), duplicated.device,
                duplicated.inode, duplicated.owner);
        const struct stat named_status = revalidate_log_name(
                duplicated.descriptor.get(), filename, overrides);
        if(!same_filesystem_identity(opened.status, named_status)) {
            throw_state_log_error(
                    StateLogStage::NameRevalidation,
                    StateLogErrorCode::ConcurrentReplacement);
        }
        validate_log_file_status(
                named_status, duplicated.owner,
                StateLogStage::NameRevalidation, overrides);

        PreparedLogFile result(
                paths.default_log_file, filename,
                duplicated.descriptor.get(), opened.descriptor.get(),
                duplicated.device, duplicated.inode,
                status_owner(opened.status),
                status_permissions(opened.status),
                status_device(opened.status), status_inode(opened.status),
                opened.created);
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
        if(overrides != nullptr && overrides->injected_failure.has_value()) {
            const StateLogInjectedFailure& failure =
                    overrides->injected_failure.value();
            switch(failure.failure_point) {
            case StateLogTestFailurePoint::RecordWrite:
                result.test_record_write_error_ = failure.error_number;
                break;
            case StateLogTestFailurePoint::FileClose:
                result.test_file_close_error_ = failure.error_number;
                break;
            case StateLogTestFailurePoint::DirectoryClose:
                result.test_directory_close_error_ = failure.error_number;
                break;
            case StateLogTestFailurePoint::DirectoryDescriptorDuplication:
            case StateLogTestFailurePoint::InitialMetadata:
            case StateLogTestFailurePoint::FileCreation:
            case StateLogTestFailurePoint::FileOpen:
            case StateLogTestFailurePoint::DescriptorMetadata:
            case StateLogTestFailurePoint::NameRevalidation:
                break;
            }
        }
#endif
        static_cast<void>(duplicated.descriptor.release());
        static_cast<void>(opened.descriptor.release());
        return result;
    }
};

StateLogError::StateLogError(StateLogFailure failure)
    : std::runtime_error(state_log_diagnostic(failure)),
      failure_(std::move(failure)) {
}

PreparedLogFile::PreparedLogFile(
        fs::path logical_path, std::string filename,
        int directory_descriptor, int file_descriptor,
        std::uintmax_t directory_device, std::uintmax_t directory_inode,
        std::uintmax_t owner, std::uintmax_t permissions,
        std::uintmax_t device, std::uintmax_t inode, bool created) noexcept
    : logical_path_(std::move(logical_path)),
      filename_(std::move(filename)),
      directory_descriptor_(directory_descriptor),
      file_descriptor_(file_descriptor), directory_device_(directory_device),
      directory_inode_(directory_inode), owner_(owner),
      permissions_(permissions), device_(device), inode_(inode),
      created_(created) {
}

PreparedLogFile::PreparedLogFile(PreparedLogFile&& other) noexcept
    : directory_kind_(other.directory_kind_),
      logical_path_(std::move(other.logical_path_)),
      filename_(std::move(other.filename_)),
      directory_descriptor_(
              std::exchange(other.directory_descriptor_, -1)),
      file_descriptor_(std::exchange(other.file_descriptor_, -1)),
      directory_device_(other.directory_device_),
      directory_inode_(other.directory_inode_), owner_(other.owner_),
      permissions_(other.permissions_), device_(other.device_),
      inode_(other.inode_), created_(other.created_) {
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    test_record_write_error_ =
            std::exchange(other.test_record_write_error_, 0);
    test_file_close_error_ =
            std::exchange(other.test_file_close_error_, 0);
    test_directory_close_error_ =
            std::exchange(other.test_directory_close_error_, 0);
#endif
}

PreparedLogFile& PreparedLogFile::operator=(PreparedLogFile&& other) noexcept {
    if(this == &other) return *this;
    close_descriptors();
    directory_kind_ = other.directory_kind_;
    logical_path_ = std::move(other.logical_path_);
    filename_ = std::move(other.filename_);
    directory_descriptor_ =
            std::exchange(other.directory_descriptor_, -1);
    file_descriptor_ = std::exchange(other.file_descriptor_, -1);
    directory_device_ = other.directory_device_;
    directory_inode_ = other.directory_inode_;
    owner_ = other.owner_;
    permissions_ = other.permissions_;
    device_ = other.device_;
    inode_ = other.inode_;
    created_ = other.created_;
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    test_record_write_error_ =
            std::exchange(other.test_record_write_error_, 0);
    test_file_close_error_ =
            std::exchange(other.test_file_close_error_, 0);
    test_directory_close_error_ =
            std::exchange(other.test_directory_close_error_, 0);
#endif
    return *this;
}

PreparedLogFile::~PreparedLogFile() noexcept {
    close_descriptors();
}

void PreparedLogFile::close_descriptors() noexcept {
    if(file_descriptor_ >= 0) {
        static_cast<void>(close(file_descriptor_));
        file_descriptor_ = -1;
    }
    if(directory_descriptor_ >= 0) {
        static_cast<void>(close(directory_descriptor_));
        directory_descriptor_ = -1;
    }
}

void PreparedLogFile::require_unchanged_identity() const {
    if(directory_descriptor_ < 0 || file_descriptor_ < 0) {
        throw_state_log_error(
                StateLogStage::DescriptorValidation,
                StateLogErrorCode::MetadataFailure);
    }
    validate_retained_directory(
            directory_descriptor_, directory_device_, directory_inode_,
            owner_);

    struct stat retained_status {};
    if(fstat(file_descriptor_, &retained_status) != 0) {
        const int metadata_error = errno;
        throw_state_log_error(
                StateLogStage::DescriptorValidation,
                is_permission_error(metadata_error)
                        ? StateLogErrorCode::PermissionDenied
                        : StateLogErrorCode::MetadataFailure,
                metadata_error);
    }
    if(!S_ISREG(retained_status.st_mode) ||
       status_device(retained_status) != device_ ||
       status_inode(retained_status) != inode_) {
        throw_state_log_error(
                StateLogStage::DescriptorValidation,
                StateLogErrorCode::ConcurrentReplacement);
    }
    validate_log_file_status(
            retained_status, owner_, StateLogStage::DescriptorValidation,
            nullptr);

    const struct stat named_status = revalidate_log_name(
            directory_descriptor_, filename_, nullptr);
    if(!same_filesystem_identity(retained_status, named_status)) {
        throw_state_log_error(
                StateLogStage::NameRevalidation,
                StateLogErrorCode::ConcurrentReplacement);
    }
    validate_log_file_status(
            named_status, owner_, StateLogStage::NameRevalidation, nullptr);
}

void PreparedLogFile::require_adoption_ready_for_logger() const {
    require_unchanged_identity();
    errno = 0;
    const int file_descriptor_flags = fcntl(file_descriptor_, F_GETFD);
    if(file_descriptor_flags < 0 ||
       (file_descriptor_flags & FD_CLOEXEC) == 0) {
        const int descriptor_error = errno == 0 ? EINVAL : errno;
        throw_state_log_error(
                StateLogStage::DescriptorAdoption,
                StateLogErrorCode::DescriptorAdoptionFailure,
                descriptor_error);
    }
    errno = 0;
    const int directory_descriptor_flags =
            fcntl(directory_descriptor_, F_GETFD);
    if(directory_descriptor_flags < 0 ||
       (directory_descriptor_flags & FD_CLOEXEC) == 0) {
        const int descriptor_error = errno == 0 ? EINVAL : errno;
        throw_state_log_error(
                StateLogStage::DescriptorAdoption,
                StateLogErrorCode::DescriptorAdoptionFailure,
                descriptor_error);
    }
    errno = 0;
    const int status_flags = fcntl(file_descriptor_, F_GETFL);
    if(status_flags < 0 || (status_flags & O_ACCMODE) != O_WRONLY ||
       (status_flags & O_APPEND) == 0 ||
       (status_flags & O_NONBLOCK) == 0) {
        const int descriptor_error = errno == 0 ? EINVAL : errno;
        throw_state_log_error(
                StateLogStage::DescriptorAdoption,
                StateLogErrorCode::DescriptorAdoptionFailure,
                descriptor_error);
    }
}

void PreparedLogFile::append_record_for_logger(
        std::string_view record) const {
    require_unchanged_identity();
#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    if(test_record_write_error_ != 0) {
        throw_state_log_error(
                StateLogStage::RecordWrite,
                StateLogErrorCode::WriteFailure,
                test_record_write_error_);
    }
#endif
    std::size_t offset = 0;
    while(offset < record.size()) {
        const ssize_t written = write(
                file_descriptor_, record.data() + offset,
                record.size() - offset);
        if(written < 0) {
            if(errno == EINTR) continue;
            const int write_error = errno;
            throw_state_log_error(
                    StateLogStage::RecordWrite,
                    StateLogErrorCode::WriteFailure, write_error);
        }
        if(written == 0) {
            throw_state_log_error(
                    StateLogStage::RecordWrite,
                    StateLogErrorCode::WriteFailure, EIO);
        }
        offset += static_cast<std::size_t>(written);
    }
}

void PreparedLogFile::close_checked_for_logger() {
    std::optional<int> first_close_error;
    const auto close_checked = [&first_close_error](
                                       int& descriptor,
                                       int injected_error) {
        if(descriptor < 0) return;
        const int owned_descriptor = std::exchange(descriptor, -1);
        const int close_result = close(owned_descriptor);
        if(injected_error != 0) {
            if(!first_close_error.has_value())
                first_close_error = injected_error;
            return;
        }
        if(close_result != 0 && !first_close_error.has_value())
            first_close_error = errno;
    };

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    close_checked(file_descriptor_, test_file_close_error_);
    close_checked(directory_descriptor_, test_directory_close_error_);
#else
    close_checked(file_descriptor_, 0);
    close_checked(directory_descriptor_, 0);
#endif
    if(first_close_error.has_value()) {
        throw_state_log_error(
                StateLogStage::DescriptorClose,
                StateLogErrorCode::CloseFailure,
                first_close_error.value());
    }
}

class StateLogLoggerBackend final
    : public logging_detail::StateLogBackend {
    PreparedLogFile log_file_;

public:
    explicit StateLogLoggerBackend(PreparedLogFile&& log_file) noexcept
        : log_file_(std::move(log_file)) {
    }

    void append_record(std::string_view record) override {
        log_file_.append_record_for_logger(record);
    }

    void close_checked() override {
        log_file_.require_unchanged_identity();
        log_file_.close_checked_for_logger();
    }

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
    int descriptor_for_test() const noexcept override {
        return log_file_.file_descriptor_;
    }
#endif
};

PreparedLogFile open_default_state_log(
        const xdg_paths::StatePaths& paths,
        const xdg_directory_safety::PreparedDirectory& directory) {
    return StateLogFileAccess::open(paths, directory, nullptr);
}

#ifdef MOGUET_TEST_XDG_STATE_LOG_HOOKS
PreparedLogFile open_default_state_log_for_test(
        const xdg_paths::StatePaths& paths,
        const xdg_directory_safety::PreparedDirectory& directory,
        const StateLogTestOverrides& overrides) {
    return StateLogFileAccess::open(paths, directory, &overrides);
}

struct StateLogTestAccess {
    static int file_descriptor(const PreparedLogFile& log_file) noexcept {
        return log_file.file_descriptor_;
    }

    static int directory_descriptor(
            const PreparedLogFile& log_file) noexcept {
        return log_file.directory_descriptor_;
    }
};

int state_log_file_descriptor_for_test(
        const PreparedLogFile& log_file) noexcept {
    return StateLogTestAccess::file_descriptor(log_file);
}

int state_log_directory_descriptor_for_test(
        const PreparedLogFile& log_file) noexcept {
    return StateLogTestAccess::directory_descriptor(log_file);
}
#endif

} // namespace xdg_state_log

void Logger::init(
        xdg_state_log::PreparedLogFile&& log_file,
        const std::string& initial_info_message) {
    log_file.require_adoption_ready_for_logger();
    auto backend = std::make_unique<xdg_state_log::StateLogLoggerBackend>(
            std::move(log_file));
    adopt_state_log_backend(std::move(backend), initial_info_message);
}
