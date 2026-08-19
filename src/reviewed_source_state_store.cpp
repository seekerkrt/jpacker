#include "reviewed_source_state_store.hpp"

#include "xdg_directory_safety.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct ReviewedSourceStateDirectoryAccess {
    static int descriptor(
            const xdg_directory_safety::PreparedDirectory& directory) {
        directory.require_unchanged_identity();
        return directory.directory_descriptor_;
    }
};

namespace {

namespace fs = std::filesystem;

constexpr mode_t REVIEWED_SOURCE_STATE_FILE_MODE = 0600;
constexpr std::string_view ENTRY_SUFFIX = ".toml";
constexpr std::string_view ATOMIC_TEMP_PREFIX = "-.moguet-reviewed-source-";

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
std::optional<ReviewedSourceStateStoreTestFailurePoint> g_injected_failure;
ReviewedSourceStateStoreTestRacePoint g_race_point =
        ReviewedSourceStateStoreTestRacePoint::BeforePublication;
ReviewedSourceStateStoreTestRaceHandler g_race_handler = nullptr;
bool g_race_pending = false;

bool consume_injected_failure(
        ReviewedSourceStateStoreTestFailurePoint point) {
    if(!g_injected_failure.has_value() || *g_injected_failure != point) {
        return false;
    }
    g_injected_failure.reset();
    return true;
}

void invoke_race(ReviewedSourceStateStoreTestRacePoint point,
        const fs::path& entry_path) {
    if(!g_race_pending || g_race_handler == nullptr || g_race_point != point) {
        return;
    }
    g_race_pending = false;
    g_race_handler(entry_path);
}
#endif

std::atomic<std::uint64_t> g_atomic_temp_sequence{0};

class OwnedDescriptor final {
    int descriptor_ = -1;

public:
    explicit OwnedDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}

    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;

    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}

    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) static_cast<void>(::close(descriptor_));
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }

    ~OwnedDescriptor() noexcept {
        if(descriptor_ >= 0) static_cast<void>(::close(descriptor_));
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

void require_aur_known_package_base(const PackageBaseIdentity& package_base) {
    const PackageSourceIdentity& source = package_base.source();
    if(source.kind() != PackageSourceKind::Aur) {
        throw std::invalid_argument(
                "Reviewed source state store requires an AUR PackageBase identity.");
    }
    const SourceLocationIdentity& location = source.location();
    if(location.kind() != SourceLocationKind::GitRemote ||
       location.state() != SourceLocationState::Known ||
       location.value() == nullptr) {
        throw std::invalid_argument(
                "Reviewed source state store requires a known AUR Git remote.");
    }
}

std::string entry_leaf_name(const PackageBaseIdentity& package_base) {
    return package_base.package_base() + std::string(ENTRY_SUFFIX);
}

std::string atomic_temp_leaf() {
    const std::uint64_t sequence =
            g_atomic_temp_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    return std::string(ATOMIC_TEMP_PREFIX) + std::to_string(::getpid()) +
           "-" + std::to_string(sequence);
}

fs::file_type file_type_from_mode(mode_t mode) {
    if(S_ISREG(mode)) return fs::file_type::regular;
    if(S_ISDIR(mode)) return fs::file_type::directory;
    if(S_ISLNK(mode)) return fs::file_type::symlink;
    if(S_ISBLK(mode)) return fs::file_type::block;
    if(S_ISCHR(mode)) return fs::file_type::character;
    if(S_ISFIFO(mode)) return fs::file_type::fifo;
    if(S_ISSOCK(mode)) return fs::file_type::socket;
    return fs::file_type::unknown;
}

std::error_code current_system_error() {
    return std::error_code(errno, std::generic_category());
}

ReviewedSourceStateStoreFailure store_failure(
        ReviewedSourceStateStoreFailureKind kind,
        const fs::path& entry_path,
        std::optional<std::error_code> system_error = std::nullopt,
        std::optional<fs::file_type> observed_file_type = std::nullopt) {
    return ReviewedSourceStateStoreFailure{
            kind, entry_path, std::move(system_error),
            std::move(observed_file_type)};
}

ReviewedSourceStateRecordIdentity record_identity_from_status(
        const struct stat& status) {
    return ReviewedSourceStateRecordIdentity{
            static_cast<std::uintmax_t>(status.st_dev),
            static_cast<std::uintmax_t>(status.st_ino),
            static_cast<std::uintmax_t>(status.st_uid),
            static_cast<std::uintmax_t>(status.st_mode & 07777),
            static_cast<std::intmax_t>(status.st_size),
            static_cast<std::intmax_t>(status.st_mtim.tv_sec),
            static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
            static_cast<std::intmax_t>(status.st_ctim.tv_sec),
            static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
}

bool same_filesystem_identity(
        const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev &&
           expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

bool same_record_state(
        const struct stat& expected, const struct stat& actual) {
    return same_filesystem_identity(expected, actual) &&
           expected.st_uid == actual.st_uid &&
           (expected.st_mode & 07777) == (actual.st_mode & 07777) &&
           expected.st_size == actual.st_size &&
           expected.st_mtim.tv_sec == actual.st_mtim.tv_sec &&
           expected.st_mtim.tv_nsec == actual.st_mtim.tv_nsec &&
           expected.st_ctim.tv_sec == actual.st_ctim.tv_sec &&
           expected.st_ctim.tv_nsec == actual.st_ctim.tv_nsec;
}

// rename(2) may update ctime. Post-publication compares the stable
// security/content fields and allows only that relocation timestamp to differ.
bool same_relocated_record_state(
        const struct stat& expected, const struct stat& actual) {
    return same_filesystem_identity(expected, actual) &&
           expected.st_uid == actual.st_uid &&
           (expected.st_mode & 07777) == (actual.st_mode & 07777) &&
           expected.st_size == actual.st_size &&
           expected.st_mtim.tv_sec == actual.st_mtim.tv_sec &&
           expected.st_mtim.tv_nsec == actual.st_mtim.tv_nsec;
}

bool matches_record_identity(
        const struct stat& status,
        const ReviewedSourceStateRecordIdentity& identity) {
    return record_identity_from_status(status) == identity;
}

std::optional<ReviewedSourceStateStoreFailure> validate_entry_status(
        const struct stat& status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
    const fs::file_type file_type = file_type_from_mode(status.st_mode);
    if(file_type != fs::file_type::regular) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
                entry_path, std::nullopt, file_type);
    }
    if(static_cast<std::uintmax_t>(status.st_uid) != expected_owner) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OwnershipMismatch,
                entry_path);
    }
    if((status.st_mode & 07777) != REVIEWED_SOURCE_STATE_FILE_MODE) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsafePermissions,
                entry_path);
    }
    if(status.st_nlink != 1) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::MultipleHardLinks,
                entry_path);
    }
    return std::nullopt;
}

ReviewedSourceStateStoreFailure map_resolution_error(
        const xdg_paths::ResolutionError&, const fs::path& entry_path) {
    return store_failure(
            ReviewedSourceStateStoreFailureKind::AuthorityUnavailable,
            entry_path);
}

ReviewedSourceStateStoreFailure map_preparation_error(
        const xdg_directory_safety::PreparationError& error,
        const fs::path& entry_path) {
    const xdg_directory_safety::PreparationErrorCode code = error.failure().code;
    ReviewedSourceStateStoreFailureKind kind =
            ReviewedSourceStateStoreFailureKind::DirectoryPreparationFailed;
    switch(code) {
    case xdg_directory_safety::PreparationErrorCode::MissingAnchor:
    case xdg_directory_safety::PreparationErrorCode::InvalidCreationBoundary:
        kind = ReviewedSourceStateStoreFailureKind::AuthorityUnavailable;
        break;
    case xdg_directory_safety::PreparationErrorCode::Symlink:
    case xdg_directory_safety::PreparationErrorCode::NotDirectory:
        kind = ReviewedSourceStateStoreFailureKind::DirectoryUnavailable;
        break;
    case xdg_directory_safety::PreparationErrorCode::OwnershipMismatch:
        kind = ReviewedSourceStateStoreFailureKind::OwnershipMismatch;
        break;
    case xdg_directory_safety::PreparationErrorCode::UnsafePermissions:
        kind = ReviewedSourceStateStoreFailureKind::UnsafePermissions;
        break;
    case xdg_directory_safety::PreparationErrorCode::ConcurrentReplacement:
        kind = ReviewedSourceStateStoreFailureKind::ConcurrentReplacement;
        break;
    case xdg_directory_safety::PreparationErrorCode::PermissionDenied:
    case xdg_directory_safety::PreparationErrorCode::CreationFailed:
    case xdg_directory_safety::PreparationErrorCode::MetadataFailure:
        kind = ReviewedSourceStateStoreFailureKind::DirectoryPreparationFailed;
        break;
    }
    return store_failure(kind, entry_path, error.failure().system_error);
}

xdg_paths::ReviewedSourceStatePaths resolve_store_paths(
        std::optional<ReviewedSourceStateStoreFailure>& failure,
        const fs::path& diagnostic_path) {
    try {
        return xdg_paths::resolve_reviewed_source_state_process_environment();
    } catch(const xdg_paths::ResolutionError& error) {
        failure = map_resolution_error(error, diagnostic_path);
        return xdg_paths::ReviewedSourceStatePaths{};
    }
}

std::optional<struct stat> inspect_named_entry(
        int directory_descriptor,
        const std::string& leaf_name,
        const fs::path& entry_path,
        std::optional<ReviewedSourceStateStoreFailure>& failure) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Status)) {
        failure = store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::make_error_code(std::errc::permission_denied));
        return std::nullopt;
    }
#endif
    struct stat status {};
    int status_result = -1;
    do {
        status_result = ::fstatat(
                directory_descriptor, leaf_name.c_str(), &status,
                AT_SYMLINK_NOFOLLOW);
    } while(status_result != 0 && errno == EINTR);
    if(status_result != 0) {
        const int status_error = errno;
        if(status_error == ENOENT) return std::nullopt;
        failure = store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::error_code(status_error, std::generic_category()));
        return std::nullopt;
    }
    return status;
}

int renameat2_checked(
        int old_directory_descriptor,
        const char* old_leaf,
        int new_directory_descriptor,
        const char* new_leaf,
        unsigned int flags) {
#ifdef SYS_renameat2
    int rename_result = -1;
    do {
        rename_result = static_cast<int>(::syscall(
                SYS_renameat2, old_directory_descriptor, old_leaf,
                new_directory_descriptor, new_leaf, flags));
    } while(rename_result != 0 && errno == EINTR);
    return rename_result;
#else
    static_cast<void>(old_directory_descriptor);
    static_cast<void>(old_leaf);
    static_cast<void>(new_directory_descriptor);
    static_cast<void>(new_leaf);
    static_cast<void>(flags);
    errno = ENOSYS;
    return -1;
#endif
}

std::optional<ReviewedSourceStateStoreFailure> close_descriptor_checked(
        OwnedDescriptor& descriptor, const fs::path& entry_path) {
    const int raw_descriptor = descriptor.release();
    if(raw_descriptor < 0) return std::nullopt;
    if(::close(raw_descriptor) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::CloseFailed, entry_path,
                current_system_error());
    }
    return std::nullopt;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
lock_store_directory(
        const xdg_directory_safety::PreparedDirectory& directory,
        int lock_operation,
        const fs::path& entry_path) {
#ifndef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    static_cast<void>(entry_path);
#endif
    const int directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(directory);
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Lock)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::LockFailed, entry_path,
                std::make_error_code(std::errc::resource_unavailable_try_again));
    }
#endif
    int lock_descriptor = -1;
    do {
        lock_descriptor = ::openat(
                directory_descriptor, ".",
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    } while(lock_descriptor < 0 && errno == EINTR);
    if(lock_descriptor < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed,
                directory.path(), current_system_error());
    }
    OwnedDescriptor descriptor(lock_descriptor);
    int lock_result = -1;
    do {
        lock_result = ::flock(descriptor.get(), lock_operation);
    } while(lock_result != 0 && errno == EINTR);
    if(lock_result != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::LockFailed,
                directory.path(), current_system_error());
    }
    try {
        directory.require_unchanged_identity();
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, directory.path());
    }
    return descriptor;
}

std::optional<ReviewedSourceStateStoreFailure> fstat_descriptor(
        int descriptor,
        struct stat& status,
        const fs::path& entry_path) {
    int status_result = -1;
    do {
        status_result = ::fstat(descriptor, &status);
    } while(status_result != 0 && errno == EINTR);
    if(status_result != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                current_system_error());
    }
    return std::nullopt;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
open_existing_entry(
        int directory_descriptor,
        const std::string& leaf_name,
        const struct stat& observed_status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Open)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::make_error_code(std::errc::permission_denied));
    }
#endif
    int raw_descriptor = -1;
    do {
        raw_descriptor = ::openat(
                directory_descriptor, leaf_name.c_str(),
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while(raw_descriptor < 0 && errno == EINTR);
    if(raw_descriptor < 0) {
        const int open_error = errno;
        if(open_error == ENOENT || open_error == ELOOP ||
           open_error == ENOTDIR) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::error_code(open_error, std::generic_category()));
    }
    OwnedDescriptor descriptor(raw_descriptor);
    struct stat opened_status {};
    if(auto failure =
               fstat_descriptor(descriptor.get(), opened_status, entry_path)) {
        return *failure;
    }
    if(auto failure = validate_entry_status(
               opened_status, entry_path, expected_owner)) {
        return *failure;
    }
    if(!same_record_state(observed_status, opened_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    return descriptor;
}

std::variant<std::string, ReviewedSourceStateStoreFailure> read_all_bytes(
        int descriptor,
        const fs::path& entry_path,
        const struct stat& expected_status) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Read)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ReadFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    std::string contents;
    if(expected_status.st_size > 0) {
        contents.reserve(static_cast<std::size_t>(expected_status.st_size));
    }
    std::array<char, 4096> buffer {};
    while(true) {
        ssize_t count = -1;
        do {
            count = ::read(descriptor, buffer.data(), buffer.size());
        } while(count < 0 && errno == EINTR);
        if(count < 0) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ReadFailed,
                    entry_path, current_system_error());
        }
        if(count == 0) break;
        contents.append(buffer.data(), static_cast<std::size_t>(count));
    }

    struct stat after_status {};
    if(auto failure = fstat_descriptor(descriptor, after_status, entry_path)) {
        return *failure;
    }
    if(!same_record_state(expected_status, after_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    return contents;
}

ReviewedSourceStateObservation interpret_observed_contents(
        const std::string& raw_contents,
        const PackageBaseIdentity& expected_package_base) {
    const ReviewedSourceStateInterpretation interpreted =
            interpret_reviewed_source_state(raw_contents, expected_package_base);
    return std::visit(
            [](const auto& arm) -> ReviewedSourceStateObservation {
                return arm;
            },
            interpreted);
}

ReviewedSourceStateStoreRead make_present_read(
        ReviewedSourceStateObservation observation,
        const struct stat& status,
        std::string raw_contents) {
    return ReviewedSourceStateStoreRead{
            std::move(observation),
            ReviewedSourceStateObservedRecord{
                    record_identity_from_status(status),
                    std::move(raw_contents)}};
}

std::optional<ReviewedSourceStateStoreFailure> write_all_bytes(
        int descriptor,
        std::string_view contents,
        const fs::path& entry_path) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Write)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    while(!contents.empty()) {
        ssize_t count = -1;
        do {
            count = ::write(
                    descriptor, contents.data(), contents.size());
        } while(count < 0 && errno == EINTR);
        if(count <= 0) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::WriteFailed,
                    entry_path,
                    count < 0 ? current_system_error()
                              : std::make_error_code(std::errc::io_error));
        }
        contents.remove_prefix(static_cast<std::size_t>(count));
    }
    return std::nullopt;
}

bool remove_named_if_descriptor_matches(
        int directory_descriptor,
        const std::string& leaf_name,
        int owned_descriptor,
        const fs::path& entry_path,
        const std::optional<struct stat>& expected_status,
        std::optional<ReviewedSourceStateStoreFailure>& failure) {
    struct stat descriptor_status {};
    if(auto status_failure =
               fstat_descriptor(owned_descriptor, descriptor_status, entry_path)) {
        failure = *status_failure;
        return false;
    }
    if(expected_status.has_value() &&
       !same_record_state(*expected_status, descriptor_status)) {
        return false;
    }
    struct stat named_status {};
    int status_result = -1;
    do {
        status_result = ::fstatat(
                directory_descriptor, leaf_name.c_str(), &named_status,
                AT_SYMLINK_NOFOLLOW);
    } while(status_result != 0 && errno == EINTR);
    if(status_result != 0) {
        if(errno == ENOENT) return false;
        failure = store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                current_system_error());
        return false;
    }
    if((expected_status.has_value() &&
        !same_record_state(*expected_status, named_status)) ||
       !same_record_state(descriptor_status, named_status)) {
        return false;
    }
    if(::unlinkat(directory_descriptor, leaf_name.c_str(), 0) == 0) return true;
    const int remove_error = errno;
    if(remove_error == ENOENT || remove_error == ENOTDIR ||
       remove_error == ELOOP) {
        return false;
    }
    failure = store_failure(
            ReviewedSourceStateStoreFailureKind::RenameFailed, entry_path,
            std::error_code(remove_error, std::generic_category()));
    return false;
}

} // namespace

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
void fail_next_reviewed_source_state_store_operation_for_test(
        ReviewedSourceStateStoreTestFailurePoint failure_point) {
    g_injected_failure = failure_point;
}

void run_reviewed_source_state_store_race_once_for_test(
        ReviewedSourceStateStoreTestRacePoint race_point,
        ReviewedSourceStateStoreTestRaceHandler handler) {
    g_race_point = race_point;
    g_race_handler = handler;
    g_race_pending = handler != nullptr;
}

void reset_reviewed_source_state_store_test_hooks() {
    g_injected_failure.reset();
    g_race_handler = nullptr;
    g_race_pending = false;
}
#endif

xdg_paths::ReviewedSourceStatePaths reviewed_source_state_store_paths() {
    return xdg_paths::resolve_reviewed_source_state_process_environment();
}

std::filesystem::path reviewed_source_state_store_directory() {
    return reviewed_source_state_store_paths().directory;
}

std::filesystem::path reviewed_source_state_store_entry_path(
        const PackageBaseIdentity& package_base) {
    require_aur_known_package_base(package_base);
    return reviewed_source_state_store_directory() /
           entry_leaf_name(package_base);
}

ReviewedSourceStateStoreReadResult read_reviewed_source_state(
        const PackageBaseIdentity& expected_package_base) {
    require_aur_known_package_base(expected_package_base);
    const fs::path diagnostic_path(entry_leaf_name(expected_package_base));
    std::optional<ReviewedSourceStateStoreFailure> resolve_failure;
    const xdg_paths::ReviewedSourceStatePaths paths =
            resolve_store_paths(resolve_failure, diagnostic_path);
    if(resolve_failure.has_value()) return *resolve_failure;
    const fs::path entry_path =
            paths.directory / entry_leaf_name(expected_package_base);

    std::unique_ptr<xdg_directory_safety::PreparedDirectory> directory;
    try {
        std::optional<xdg_directory_safety::PreparedDirectory> opened =
                xdg_directory_safety::open_existing_directory(paths);
        if(!opened.has_value()) {
            return ReviewedSourceStateStoreRead{
                    ReviewedSourceStateMissing{}, std::nullopt};
        }
        directory = std::make_unique<xdg_directory_safety::PreparedDirectory>(
                std::move(*opened));
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, entry_path);
    }
    if(!directory) {
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }

    auto lock = lock_store_directory(*directory, LOCK_SH, entry_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&lock)) {
        return *failure;
    }
    OwnedDescriptor lock_descriptor =
            std::get<OwnedDescriptor>(std::move(lock));

    const std::string leaf = entry_leaf_name(expected_package_base);
    const int directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(*directory);
    std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
    const std::optional<struct stat> named_status = inspect_named_entry(
            directory_descriptor, leaf, entry_path, inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!named_status.has_value()) {
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }
    if(auto failure = validate_entry_status(
               *named_status, entry_path, directory->owner())) {
        return *failure;
    }

    auto opened = open_existing_entry(
            directory_descriptor, leaf, *named_status, entry_path,
            directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
        return *failure;
    }
    OwnedDescriptor file_descriptor =
            std::get<OwnedDescriptor>(std::move(opened));

    std::optional<struct stat> revalidated;
    inspect_failure.reset();
    revalidated = inspect_named_entry(
            directory_descriptor, leaf, entry_path, inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!revalidated.has_value() ||
       !same_record_state(*named_status, *revalidated)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }

    auto contents = read_all_bytes(
            file_descriptor.get(), entry_path, *named_status);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
        return *failure;
    }
    std::string raw = std::get<std::string>(std::move(contents));
    if(auto close_failure =
               close_descriptor_checked(file_descriptor, entry_path)) {
        return *close_failure;
    }
    if(auto close_failure =
               close_descriptor_checked(lock_descriptor, directory->path())) {
        return *close_failure;
    }

    const ReviewedSourceStateObservation observation =
            interpret_observed_contents(raw, expected_package_base);
    return make_present_read(
            std::move(observation), *named_status, std::move(raw));
}

ReviewedSourceStateStorePublishResult publish_reviewed_source_state(
        const ReviewedSourceState& next_state,
        const std::optional<ReviewedSourceStateObservedRecord>&
                expected_observed) {
    const PackageBaseIdentity& expected_package_base = next_state.package_base();
    require_aur_known_package_base(expected_package_base);
    if(next_state.schema_version() != reviewed_source_state_schema_version) {
        throw std::invalid_argument(
                "Reviewed source state store cannot publish a non-current schema.");
    }

    const fs::path diagnostic_path(entry_leaf_name(expected_package_base));
    std::optional<ReviewedSourceStateStoreFailure> resolve_failure;
    const xdg_paths::ReviewedSourceStatePaths paths =
            resolve_store_paths(resolve_failure, diagnostic_path);
    if(resolve_failure.has_value()) return *resolve_failure;
    const fs::path entry_path =
            paths.directory / entry_leaf_name(expected_package_base);

    std::unique_ptr<xdg_directory_safety::PreparedDirectory> directory;
    try {
        directory = std::make_unique<xdg_directory_safety::PreparedDirectory>(
                xdg_directory_safety::prepare_directory(paths));
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, entry_path);
    }

    auto lock = lock_store_directory(*directory, LOCK_EX, entry_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&lock)) {
        return *failure;
    }
    OwnedDescriptor directory_sync =
            std::get<OwnedDescriptor>(std::move(lock));

    const std::string leaf = entry_leaf_name(expected_package_base);
    const int directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(*directory);
    std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
    const std::optional<struct stat> named_status = inspect_named_entry(
            directory_descriptor, leaf, entry_path, inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;

    if(expected_observed.has_value() != named_status.has_value()) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }

    OwnedDescriptor retained_entry;
    std::string existing_raw;
    if(named_status.has_value()) {
        if(auto failure = validate_entry_status(
                   *named_status, entry_path, directory->owner())) {
            return *failure;
        }
        if(!matches_record_identity(*named_status, expected_observed->identity)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        auto opened = open_existing_entry(
                directory_descriptor, leaf, *named_status, entry_path,
                directory->owner());
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
            return *failure;
        }
        retained_entry = std::get<OwnedDescriptor>(std::move(opened));
        auto contents = read_all_bytes(
                retained_entry.get(), entry_path, *named_status);
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
            return *failure;
        }
        existing_raw = std::get<std::string>(std::move(contents));
        if(existing_raw != expected_observed->raw_contents) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        const ReviewedSourceStateDocument decoded =
                decode_reviewed_source_state(existing_raw);
        if(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                   decoded)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::
                            FutureSchemaOverwriteRefused,
                    entry_path);
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::BeforePublication,
            entry_path);
#endif

    const std::string publication = encode_reviewed_source_state(next_state);
    OwnedDescriptor temporary;
    std::string temporary_leaf;
    std::optional<struct stat> cleanup_temporary_status;
    for(std::size_t attempt = 0; attempt < 128; ++attempt) {
        temporary_leaf = atomic_temp_leaf();
        const int temporary_descriptor = ::openat(
                directory_descriptor, temporary_leaf.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                REVIEWED_SOURCE_STATE_FILE_MODE);
        if(temporary_descriptor >= 0) {
            temporary = OwnedDescriptor(temporary_descriptor);
            break;
        }
        if(errno != EEXIST) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::OpenFailed,
                    entry_path, current_system_error());
        }
    }
    if(temporary.get() < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::make_error_code(std::errc::file_exists));
    }

    bool was_published = false;
    auto cleanup_temporary = [&]() {
        if(was_published || temporary_leaf.empty() || temporary.get() < 0) {
            return;
        }
        std::optional<ReviewedSourceStateStoreFailure> cleanup_failure;
        static_cast<void>(remove_named_if_descriptor_matches(
                directory_descriptor, temporary_leaf, temporary.get(),
                entry_path, cleanup_temporary_status, cleanup_failure));
    };

    if(::fchmod(temporary.get(), REVIEWED_SOURCE_STATE_FILE_MODE) != 0) {
        cleanup_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, entry_path,
                current_system_error());
    }
    if(auto write_failure =
               write_all_bytes(temporary.get(), publication, entry_path)) {
        cleanup_temporary();
        return *write_failure;
    }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Sync)) {
        cleanup_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    if(::fsync(temporary.get()) != 0) {
        cleanup_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed, entry_path,
                current_system_error());
    }

    struct stat temporary_status {};
    if(auto failure =
               fstat_descriptor(temporary.get(), temporary_status, entry_path)) {
        cleanup_temporary();
        return *failure;
    }
    cleanup_temporary_status = temporary_status;
    if(auto failure = validate_entry_status(
               temporary_status, entry_path, directory->owner())) {
        cleanup_temporary();
        return *failure;
    }

    inspect_failure.reset();
    const std::optional<struct stat> pre_rename_status = inspect_named_entry(
            directory_descriptor, leaf, entry_path, inspect_failure);
    if(inspect_failure.has_value()) {
        cleanup_temporary();
        return *inspect_failure;
    }
    if(pre_rename_status.has_value() != named_status.has_value() ||
       (named_status.has_value() &&
        !same_record_state(*named_status, *pre_rename_status))) {
        cleanup_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            entry_path);
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Rename)) {
        cleanup_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif

    struct stat published_descriptor_status {};
    if(named_status.has_value()) {
        if(renameat2_checked(
                   directory_descriptor, temporary_leaf.c_str(),
                   directory_descriptor, leaf.c_str(),
                   RENAME_EXCHANGE) != 0) {
            const int rename_error = errno;
            cleanup_temporary();
            if(rename_error == ENOENT || rename_error == ENOTDIR ||
               rename_error == ELOOP) {
                return store_failure(
                        ReviewedSourceStateStoreFailureKind::
                                ConcurrentReplacement,
                        entry_path);
            }
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::RenameFailed,
                    entry_path,
                    std::error_code(rename_error, std::generic_category()));
        }
        was_published = true;
        if(auto failure = fstat_descriptor(
                   temporary.get(), published_descriptor_status, entry_path)) {
            return *failure;
        }
        struct stat displaced_status {};
        if(auto failure = fstat_descriptor(
                   retained_entry.get(), displaced_status, entry_path)) {
            return *failure;
        }
        struct stat named_published {};
        int named_result = -1;
        do {
            named_result = ::fstatat(
                    directory_descriptor, leaf.c_str(), &named_published,
                    AT_SYMLINK_NOFOLLOW);
        } while(named_result != 0 && errno == EINTR);
        struct stat named_displaced {};
        int displaced_result = -1;
        do {
            displaced_result = ::fstatat(
                    directory_descriptor, temporary_leaf.c_str(),
                    &named_displaced, AT_SYMLINK_NOFOLLOW);
        } while(displaced_result != 0 && errno == EINTR);
        const bool publication_matches =
                named_result == 0 && displaced_result == 0 &&
                same_record_state(published_descriptor_status, named_published) &&
                same_record_state(displaced_status, named_displaced) &&
                same_relocated_record_state(
                        temporary_status, published_descriptor_status) &&
                same_relocated_record_state(*named_status, displaced_status);
        if(!publication_matches) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        std::optional<ReviewedSourceStateStoreFailure> remove_failure;
        if(!remove_named_if_descriptor_matches(
                   directory_descriptor, temporary_leaf, retained_entry.get(),
                   entry_path, displaced_status, remove_failure)) {
            if(remove_failure.has_value()) return *remove_failure;
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        temporary_leaf.clear();
    } else if(renameat2_checked(
                      directory_descriptor, temporary_leaf.c_str(),
                      directory_descriptor, leaf.c_str(),
                      RENAME_NOREPLACE) != 0) {
        const int rename_error = errno;
        cleanup_temporary();
        if(rename_error == EEXIST || rename_error == ENOTEMPTY ||
           rename_error == ENOENT || rename_error == ENOTDIR ||
           rename_error == ELOOP) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed, entry_path,
                std::error_code(rename_error, std::generic_category()));
    } else {
        was_published = true;
        if(auto failure = fstat_descriptor(
                   temporary.get(), published_descriptor_status, entry_path)) {
            return *failure;
        }
        struct stat named_published {};
        int named_result = -1;
        do {
            named_result = ::fstatat(
                    directory_descriptor, leaf.c_str(), &named_published,
                    AT_SYMLINK_NOFOLLOW);
        } while(named_result != 0 && errno == EINTR);
        if(named_result != 0 ||
           !same_relocated_record_state(
                   temporary_status, published_descriptor_status) ||
           !same_record_state(published_descriptor_status, named_published)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        temporary_leaf.clear();
    }

    inspect_failure.reset();
    const std::optional<struct stat> published_named = inspect_named_entry(
            directory_descriptor, leaf, entry_path, inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!published_named.has_value() ||
       !same_record_state(published_descriptor_status, *published_named)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    if(auto failure = validate_entry_status(
               *published_named, entry_path, directory->owner())) {
        return *failure;
    }
    if(auto close_failure = close_descriptor_checked(temporary, entry_path)) {
        return *close_failure;
    }
    if(::fsync(directory_sync.get()) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed,
                directory->path(), current_system_error());
    }
    if(auto close_failure =
               close_descriptor_checked(directory_sync, directory->path())) {
        return *close_failure;
    }
    try {
        directory->require_unchanged_identity();
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, entry_path);
    }

    return ReviewedSourceStateStorePublished{
            next_state,
            ReviewedSourceStateObservedRecord{
                    record_identity_from_status(published_descriptor_status),
                    publication}};
}
