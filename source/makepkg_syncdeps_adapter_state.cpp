#include "makepkg_syncdeps_adapter_state.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <poll.h>
#include <set>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include <linux/fs.h>

namespace {

constexpr mode_t PRIVATE_DIRECTORY_MODE = 0700;
constexpr mode_t PRIVATE_FILE_MODE = 0600;
constexpr std::size_t MAXIMUM_ACTIVE_SESSIONS = 128;
constexpr std::size_t MAXIMUM_USED_SESSIONS = 4096;
constexpr std::string_view SESSION_FILE = "session";
constexpr std::string_view BINDING_FILE = "binding";
constexpr std::string_view TRANSACTIONS_DIRECTORY = "transactions";
constexpr std::string_view CURRENT_TRANSACTION_DIRECTORY = "current";
constexpr std::string_view PREPARED_TRANSACTION_FILE = "prepared";
constexpr std::string_view OBSERVATION_FILE = "observation";
constexpr std::string_view OUTCOME_FILE = "outcome";
constexpr std::string_view UNSUPPORTED_FILE = "unsupported";
constexpr std::string_view TERMINAL_FILE = "terminal";
constexpr std::string_view PARTIAL_SUFFIX = ".partial";

class OwnedDescriptor final {
public:
    OwnedDescriptor() noexcept = default;
    explicit OwnedDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {
    }
    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        reset();
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }
    ~OwnedDescriptor() {
        reset();
    }

    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] int release() noexcept {
        return std::exchange(descriptor_, -1);
    }

private:
    void reset() noexcept {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = -1;
    }

    int descriptor_ = -1;
};

[[noreturn]] void throw_state_error(const std::string& action) {
    const int error_number = errno;
    throw MakepkgSyncdepsAdapterStateError(
        action + ": " + std::strerror(error_number));
}

[[noreturn]] void throw_state_error_message(const std::string& message) {
    throw MakepkgSyncdepsAdapterStateError(message);
}

bool same_identity(const struct stat& lhs, const struct stat& rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_mode == rhs.st_mode && lhs.st_uid == rhs.st_uid;
}

bool same_pidfd_identity(
    const MakepkgSyncdepsPidfdIdentity& lhs,
    const MakepkgSyncdepsPidfdIdentity& rhs) noexcept {
    return lhs.pid == rhs.pid && lhs.device == rhs.device &&
           lhs.inode == rhs.inode;
}

struct stat require_descriptor_metadata(
    int descriptor, uid_t expected_owner, mode_t expected_type,
    std::optional<mode_t> exact_permissions,
    const std::string& description) {
    struct stat metadata{};
    if(fstat(descriptor, &metadata) == -1) {
        throw_state_error("unable to inspect " + description);
    }
    if((metadata.st_mode & S_IFMT) != expected_type ||
       metadata.st_uid != expected_owner) {
        throw_state_error_message(
            description + " has an unexpected type or owner");
    }
    const mode_t permissions = metadata.st_mode & 07777;
    if(exact_permissions.has_value()) {
        if(permissions != *exact_permissions) {
            throw_state_error_message(
                description + " has unexpected permissions");
        }
    } else if((permissions & (S_IWGRP | S_IWOTH)) != 0) {
        throw_state_error_message(
            description + " is group- or world-writable");
    }
    return metadata;
}

void require_named_identity(
    int parent_fd, const std::string& name,
    const struct stat& expected_metadata,
    const std::string& description) {
    struct stat named_metadata{};
    if(fstatat(
           parent_fd, name.c_str(), &named_metadata,
           AT_SYMLINK_NOFOLLOW) == -1) {
        throw_state_error("unable to revalidate " + description);
    }
    if(!same_identity(expected_metadata, named_metadata)) {
        throw_state_error_message(description + " identity changed");
    }
}

OwnedDescriptor open_directory_at(
    int parent_fd, const std::string& name, uid_t expected_owner,
    mode_t exact_permissions, const std::string& description) {
    const int descriptor = openat(
        parent_fd, name.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor == -1) {
        throw_state_error("unable to open " + description);
    }
    OwnedDescriptor owned(descriptor);
    const struct stat metadata = require_descriptor_metadata(
        descriptor, expected_owner, S_IFDIR, exact_permissions,
        description);
    require_named_identity(parent_fd, name, metadata, description);
    return owned;
}

OwnedDescriptor ensure_private_directory(
    int parent_fd, const std::string& name, uid_t expected_owner,
    const std::string& description) {
    if(mkdirat(parent_fd, name.c_str(), PRIVATE_DIRECTORY_MODE) == -1 &&
       errno != EEXIST) {
        throw_state_error("unable to create " + description);
    }
    return open_directory_at(
        parent_fd, name, expected_owner, PRIVATE_DIRECTORY_MODE,
        description);
}

bool entry_exists(int parent_fd, const std::string& name) {
    struct stat metadata{};
    if(fstatat(
           parent_fd, name.c_str(), &metadata,
           AT_SYMLINK_NOFOLLOW) == 0) {
        return true;
    }
    if(errno == ENOENT) return false;
    throw_state_error("unable to inspect runtime state entry");
}

void require_entry_absent(
    int parent_fd, const std::string& name,
    const std::string& description) {
    if(entry_exists(parent_fd, name)) {
        throw_state_error_message(description + " already exists");
    }
}

std::vector<std::string> list_directory_entries(int directory_fd) {
    const int duplicate = openat(
        directory_fd, ".",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(duplicate == -1) {
        throw_state_error("unable to duplicate runtime directory");
    }
    DIR* directory = fdopendir(duplicate);
    if(directory == nullptr) {
        const int error_number = errno;
        static_cast<void>(close(duplicate));
        errno = error_number;
        throw_state_error("unable to enumerate runtime directory");
    }
    std::vector<std::string> entries;
    errno = 0;
    while(dirent* entry = readdir(directory)) {
        const std::string_view name(entry->d_name);
        if(name != "." && name != "..") entries.emplace_back(name);
        errno = 0;
    }
    const int read_error = errno;
    if(closedir(directory) == -1 && read_error == 0) {
        throw_state_error("unable to close runtime directory enumeration");
    }
    if(read_error != 0) {
        errno = read_error;
        throw_state_error("unable to enumerate runtime directory");
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

void require_exact_entries(
    int directory_fd, std::vector<std::string> expected,
    const std::string& description) {
    std::sort(expected.begin(), expected.end());
    if(list_directory_entries(directory_fd) != expected) {
        throw_state_error_message(
            description + " contains unexpected runtime state");
    }
}

OwnedDescriptor create_private_file(
    int parent_fd, const std::string& name, uid_t expected_owner,
    const std::string& description) {
    const int descriptor = openat(
        parent_fd, name.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        PRIVATE_FILE_MODE);
    if(descriptor == -1) {
        throw_state_error("unable to create " + description);
    }
    OwnedDescriptor owned(descriptor);
    if(fchmod(descriptor, PRIVATE_FILE_MODE) == -1) {
        throw_state_error("unable to set permissions on " + description);
    }
    const struct stat metadata = require_descriptor_metadata(
        descriptor, expected_owner, S_IFREG, PRIVATE_FILE_MODE,
        description);
    require_named_identity(parent_fd, name, metadata, description);
    return owned;
}

OwnedDescriptor open_private_file(
    int parent_fd, const std::string& name, uid_t expected_owner,
    const std::string& description) {
    const int descriptor = openat(
        parent_fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor == -1) {
        throw_state_error("unable to open " + description);
    }
    OwnedDescriptor owned(descriptor);
    const struct stat metadata = require_descriptor_metadata(
        descriptor, expected_owner, S_IFREG, PRIVATE_FILE_MODE,
        description);
    require_named_identity(parent_fd, name, metadata, description);
    return owned;
}

void write_all(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while(offset < bytes.size()) {
        const ssize_t written = write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if(written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if(written == -1 && errno == EINTR) continue;
        throw_state_error("unable to write runtime state");
    }
}

std::string read_bounded(
    int descriptor, std::size_t maximum_bytes,
    const std::string& description) {
    std::string bytes;
    std::array<char, 4096> buffer{};
    while(true) {
        const ssize_t count = read(descriptor, buffer.data(), buffer.size());
        if(count > 0) {
            const std::size_t count_size = static_cast<std::size_t>(count);
            if(bytes.size() > maximum_bytes ||
               count_size > maximum_bytes - bytes.size()) {
                throw_state_error_message(description + " is too large");
            }
            bytes.append(buffer.data(), count_size);
            continue;
        }
        if(count == 0) return bytes;
        if(errno == EINTR) continue;
        throw_state_error("unable to read " + description);
    }
}

void synchronize_file(int descriptor, const std::string& description) {
    if(fsync(descriptor) == -1) {
        throw_state_error("unable to synchronize " + description);
    }
}

void rename_noreplace(
    int old_parent_fd, const std::string& old_name,
    int new_parent_fd, const std::string& new_name,
    const std::string& description) {
#ifdef SYS_renameat2
    long result;
    do {
        result = syscall(
            SYS_renameat2, old_parent_fd, old_name.c_str(),
            new_parent_fd, new_name.c_str(), RENAME_NOREPLACE);
    } while(result == -1 && errno == EINTR);
    if(result == 0) return;
    throw_state_error("unable to atomically publish " + description);
#else
    static_cast<void>(old_parent_fd);
    static_cast<void>(old_name);
    static_cast<void>(new_parent_fd);
    static_cast<void>(new_name);
    throw_state_error_message(
        "renameat2 is unavailable for makepkg syncdeps publication");
#endif
}

void publish_private_file(
    int parent_fd, uid_t expected_owner, const std::string& final_name,
    std::string_view contents, const std::string& description) {
    const std::string partial_name = final_name + std::string(PARTIAL_SUFFIX);
    require_entry_absent(parent_fd, final_name, description);
    require_entry_absent(parent_fd, partial_name, description + " partial");
    OwnedDescriptor partial = create_private_file(
        parent_fd, partial_name, expected_owner, description + " partial");
    try {
        write_all(partial.get(), contents);
        synchronize_file(partial.get(), description + " partial");
        rename_noreplace(
            parent_fd, partial_name, parent_fd, final_name,
            description);
        synchronize_file(parent_fd, description + " parent directory");
    } catch(...) {
        static_cast<void>(unlinkat(parent_fd, partial_name.c_str(), 0));
        throw;
    }
}

template <typename Parsed, typename Result, typename Parser>
Parsed read_and_parse_state(
    int parent_fd, uid_t expected_owner, const std::string& name,
    const std::string& description, Parser&& parser) {
    OwnedDescriptor file = open_private_file(
        parent_fd, name, expected_owner, description);
    const std::string protocol = read_bounded(
        file.get(), MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES,
        description);
    const Result parsed = std::forward<Parser>(parser)(protocol);
    const auto* state = std::get_if<Parsed>(&parsed);
    if(state == nullptr) {
        throw_state_error_message(description + " is malformed");
    }
    return *state;
}

std::string staging_session_name(const std::string& session_token) {
    return "." + session_token + ".preparing";
}

bool is_staging_session_name(
    const std::string& name, std::string& token) noexcept {
    constexpr std::string_view suffix = ".preparing";
    if(name.size() !=
           MAKEPKG_SYNCDEPS_ADAPTER_TOKEN_HEX_LENGTH +
               suffix.size() + 1U ||
       name.front() != '.' || !name.ends_with(suffix)) {
        return false;
    }
    token = name.substr(
        1, MAKEPKG_SYNCDEPS_ADAPTER_TOKEN_HEX_LENGTH);
    return is_valid_makepkg_syncdeps_adapter_token(token);
}

void acquire_exclusive_lock(int descriptor, const std::string& description) {
    int result;
    do {
        result = flock(descriptor, LOCK_EX);
    } while(result == -1 && errno == EINTR);
    if(result == -1) {
        throw_state_error("unable to lock " + description);
    }
}

void release_lock(int descriptor) noexcept {
    static_cast<void>(flock(descriptor, LOCK_UN));
}

class DescriptorLock final {
public:
    DescriptorLock(int descriptor, const std::string& description)
        : descriptor_(descriptor) {
        acquire_exclusive_lock(descriptor_, description);
    }
    DescriptorLock(const DescriptorLock&) = delete;
    DescriptorLock& operator=(const DescriptorLock&) = delete;
    ~DescriptorLock() {
        release_lock(descriptor_);
    }

private:
    int descriptor_;
};

struct ProcStatus {
    pid_t parent_pid = -1;
    pid_t tracer_pid = -1;
    uid_t uid = 0;
};

std::optional<unsigned long> parse_status_number(
    std::string_view value) noexcept {
    while(!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    const std::size_t end = value.find_first_of(" \t\n");
    const std::string_view number = value.substr(0, end);
    unsigned long parsed = 0;
    const auto result = std::from_chars(
        number.data(), number.data() + number.size(), parsed);
    if(number.empty() || result.ec != std::errc() ||
       result.ptr != number.data() + number.size()) {
        return std::nullopt;
    }
    return parsed;
}

ProcStatus parse_proc_status(std::string_view status) {
    std::optional<unsigned long> parent;
    std::optional<unsigned long> tracer;
    std::array<std::optional<unsigned long>, 4> uids;
    std::size_t offset = 0;
    while(offset < status.size()) {
        const std::size_t newline = status.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos
                                    ? status.size()
                                    : newline;
        const std::string_view line = status.substr(offset, end - offset);
        if(line.starts_with("PPid:")) {
            parent = parse_status_number(line.substr(5));
        } else if(line.starts_with("TracerPid:")) {
            tracer = parse_status_number(line.substr(10));
        } else if(line.starts_with("Uid:")) {
            std::string_view values = line.substr(4);
            for(std::size_t index = 0; index < uids.size(); ++index) {
                while(!values.empty() &&
                      (values.front() == ' ' || values.front() == '\t')) {
                    values.remove_prefix(1);
                }
                const std::size_t separator =
                    values.find_first_of(" \t");
                const std::string_view value = values.substr(0, separator);
                uids[index] = parse_status_number(value);
                if(separator == std::string_view::npos) {
                    values = {};
                } else {
                    values.remove_prefix(separator);
                }
            }
        }
        if(newline == std::string_view::npos) break;
        offset = newline + 1;
    }
    if(!parent.has_value() || !tracer.has_value() ||
       *parent > static_cast<unsigned long>(
                     std::numeric_limits<pid_t>::max()) ||
       *tracer > static_cast<unsigned long>(
                     std::numeric_limits<pid_t>::max()) ||
       std::any_of(
           uids.begin(), uids.end(),
           [](const auto& uid) { return !uid.has_value(); }) ||
       *uids[1] > static_cast<unsigned long>(
                      std::numeric_limits<uid_t>::max())) {
        throw_state_error_message("process status identity is invalid");
    }
    return ProcStatus{
        static_cast<pid_t>(*parent), static_cast<pid_t>(*tracer),
        static_cast<uid_t>(*uids[1])};
}

OwnedDescriptor open_proc_process_directory(pid_t pid) {
    const int proc_fd = open(
        "/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(proc_fd == -1) throw_state_error("unable to open /proc");
    OwnedDescriptor proc(proc_fd);
    const int process_fd = openat(
        proc.get(), std::to_string(pid).c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(process_fd == -1) {
        throw_state_error("unable to open process directory");
    }
    return OwnedDescriptor(process_fd);
}

ProcStatus read_process_status(pid_t pid) {
    OwnedDescriptor process = open_proc_process_directory(pid);
    const int status_fd = openat(
        process.get(), "status", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(status_fd == -1) {
        throw_state_error("unable to open process status");
    }
    OwnedDescriptor status(status_fd);
    return parse_proc_status(
        read_bounded(status.get(), 64U * 1024U, "process status"));
}

MakepkgSyncdepsInstalledExecutableIdentity read_process_executable_identity(
    pid_t pid) {
    OwnedDescriptor process = open_proc_process_directory(pid);
    // /proc/<pid>/exe is a kernel-owned magic link to the executable object.
    // Following only this fixed entry is intentional; the pidfd checks around
    // the read reject exit/reuse races.
    const int executable_fd = openat(
        process.get(), "exe", O_RDONLY | O_CLOEXEC);
    if(executable_fd == -1) {
        throw_state_error("unable to open process executable");
    }
    OwnedDescriptor executable(executable_fd);
    struct stat metadata{};
    if(fstat(executable.get(), &metadata) == -1) {
        throw_state_error("unable to inspect process executable");
    }
    if(!S_ISREG(metadata.st_mode)) {
        throw_state_error_message("process executable is not regular");
    }
    return MakepkgSyncdepsInstalledExecutableIdentity{
        static_cast<std::uint64_t>(metadata.st_dev),
        static_cast<std::uint64_t>(metadata.st_ino)};
}

MakepkgSyncdepsPidfdIdentity basic_pidfd_identity(
    int descriptor, pid_t pid, uid_t uid) {
    struct stat metadata{};
    if(fstat(descriptor, &metadata) == -1) {
        throw_state_error("unable to inspect pidfd");
    }
    if(metadata.st_dev == 0 || metadata.st_ino == 0) {
        throw_state_error_message("pidfd identity is unavailable");
    }
    return MakepkgSyncdepsPidfdIdentity{
        pid, static_cast<std::uint64_t>(metadata.st_dev),
        static_cast<std::uint64_t>(metadata.st_ino),
        static_cast<std::uint32_t>(uid)};
}

bool descriptor_is_alive(int descriptor) {
    struct pollfd event{
        descriptor, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    int result;
    do {
        result = poll(&event, 1, 0);
    } while(result == -1 && errno == EINTR);
    if(result == -1) throw_state_error("unable to poll pidfd");
    return result == 0;
}

bool is_exact_stored_process_alive(
    const MakepkgSyncdepsPidfdIdentity& identity) {
#ifdef SYS_pidfd_open
    long descriptor;
    do {
        descriptor = syscall(SYS_pidfd_open, identity.pid, 0U);
    } while(descriptor == -1 && errno == EINTR);
    if(descriptor == -1) {
        if(errno == ESRCH) return false;
        throw_state_error("unable to reopen pidfd for stale-state check");
    }
    OwnedDescriptor pidfd(static_cast<int>(descriptor));
    const MakepkgSyncdepsPidfdIdentity observed =
        basic_pidfd_identity(pidfd.get(), identity.pid, identity.uid);
    if(!same_pidfd_identity(observed, identity)) return false;
    return descriptor_is_alive(pidfd.get());
#else
    static_cast<void>(identity);
    throw_state_error_message(
        "pidfd_open is unavailable for makepkg syncdeps state");
#endif
}

struct OpenSession {
    OwnedDescriptor descriptor;
    struct stat metadata{};
};

OpenSession open_session(
    int parent_fd, uid_t expected_owner, const std::string& name,
    const std::string& description) {
    OwnedDescriptor descriptor = open_directory_at(
        parent_fd, name, expected_owner, PRIVATE_DIRECTORY_MODE,
        description);
    const struct stat metadata = require_descriptor_metadata(
        descriptor.get(), expected_owner, S_IFDIR,
        PRIVATE_DIRECTORY_MODE, description);
    return OpenSession{std::move(descriptor), metadata};
}

std::vector<std::string> retirement_transaction_files(
    int transaction_fd) {
    const std::set<std::string> allowed{
        std::string(PREPARED_TRANSACTION_FILE),
        std::string(OBSERVATION_FILE),
        std::string(OBSERVATION_FILE) + std::string(PARTIAL_SUFFIX),
        std::string(OUTCOME_FILE),
        std::string(OUTCOME_FILE) + std::string(PARTIAL_SUFFIX),
    };
    const std::vector<std::string> entries =
        list_directory_entries(transaction_fd);
    if(std::any_of(
           entries.begin(), entries.end(), [&](const std::string& entry) {
               return !allowed.contains(entry);
           })) {
        throw_state_error_message(
            "retired transaction contains unexpected state");
    }
    return entries;
}

void remove_validated_file_if_present(
    int parent_fd, uid_t expected_owner, const std::string& name,
    const std::string& description) {
    if(!entry_exists(parent_fd, name)) return;
    static_cast<void>(open_private_file(
        parent_fd, name, expected_owner, description));
    if(unlinkat(parent_fd, name.c_str(), 0) == -1) {
        throw_state_error("unable to remove " + description);
    }
}

void validate_transaction_directory_for_cleanup(
    int parent_fd, uid_t expected_owner, const std::string& name,
    const std::string& description) {
    OwnedDescriptor transaction = open_directory_at(
        parent_fd, name, expected_owner, PRIVATE_DIRECTORY_MODE,
        description);
    const std::vector<std::string> expected =
        retirement_transaction_files(transaction.get());
    require_exact_entries(transaction.get(), expected, description);
    for(const std::string& file : expected) {
        static_cast<void>(open_private_file(
            transaction.get(), file, expected_owner,
            description + " file"));
    }
}

void cleanup_transaction_directory(
    int parent_fd, uid_t expected_owner, const std::string& name,
    const std::string& description) {
    OwnedDescriptor transaction = open_directory_at(
        parent_fd, name, expected_owner, PRIVATE_DIRECTORY_MODE,
        description);
    const std::vector<std::string> expected =
        retirement_transaction_files(transaction.get());
    require_exact_entries(transaction.get(), expected, description);
    for(const std::string& file : expected) {
        remove_validated_file_if_present(
            transaction.get(), expected_owner, file,
            description + " file");
    }
    require_exact_entries(transaction.get(), {}, description);
    if(unlinkat(parent_fd, name.c_str(), AT_REMOVEDIR) == -1) {
        throw_state_error("unable to remove " + description);
    }
}

std::vector<std::string> transaction_ledger_entries(
    int transactions_fd, uid_t expected_owner) {
    const std::vector<std::string> entries =
        list_directory_entries(transactions_fd);
    if(entries.size() > 2) {
        throw_state_error_message(
            "transaction ledger contains more than two entries");
    }
    for(std::size_t index = 0; index < entries.size(); ++index) {
        if(entries[index] != std::to_string(index + 1U)) {
            throw_state_error_message(
                "transaction ledger ordinal inventory is invalid");
        }
        static_cast<void>(open_directory_at(
            transactions_fd, entries[index], expected_owner,
            PRIVATE_DIRECTORY_MODE, "transaction ledger entry"));
    }
    return entries;
}

std::vector<std::string> retirement_transaction_ledger_entries(
    int transactions_fd, uid_t expected_owner) {
    const std::vector<std::string> entries =
        list_directory_entries(transactions_fd);
    if(entries.size() > 2 ||
       std::any_of(
           entries.begin(), entries.end(), [](const std::string& entry) {
               return entry != "1" && entry != "2";
           })) {
        throw_state_error_message(
            "retired transaction ledger inventory is invalid");
    }
    for(const std::string& entry : entries) {
        static_cast<void>(open_directory_at(
            transactions_fd, entry, expected_owner,
            PRIVATE_DIRECTORY_MODE, "retired transaction ledger entry"));
    }
    return entries;
}

void validate_session_contents_for_cleanup(
    int session_fd, uid_t expected_owner, bool require_terminal) {
    const std::set<std::string> allowed_regular_files{
        std::string(SESSION_FILE),
        std::string(SESSION_FILE) + std::string(PARTIAL_SUFFIX),
        std::string(BINDING_FILE),
        std::string(BINDING_FILE) + std::string(PARTIAL_SUFFIX),
        std::string(UNSUPPORTED_FILE),
        std::string(UNSUPPORTED_FILE) + std::string(PARTIAL_SUFFIX),
        std::string(TERMINAL_FILE),
        std::string(TERMINAL_FILE) + std::string(PARTIAL_SUFFIX),
    };
    const std::set<std::string> allowed_directories{
        std::string(TRANSACTIONS_DIRECTORY),
        std::string(CURRENT_TRANSACTION_DIRECTORY),
        ".current.preparing",
    };
    const std::vector<std::string> entries =
        list_directory_entries(session_fd);
    for(const std::string& entry : entries) {
        if(allowed_regular_files.contains(entry)) {
            static_cast<void>(open_private_file(
                session_fd, entry, expected_owner,
                "retired session transition file"));
            continue;
        }
        if(!allowed_directories.contains(entry)) {
            throw_state_error_message(
                "retired session contains unexpected state");
        }
        if(entry == TRANSACTIONS_DIRECTORY) {
            OwnedDescriptor transactions = open_directory_at(
                session_fd, entry, expected_owner,
                PRIVATE_DIRECTORY_MODE,
                "retired transaction ledger");
            const std::vector<std::string> ledger_entries =
                retirement_transaction_ledger_entries(
                    transactions.get(), expected_owner);
            for(const std::string& ledger_entry : ledger_entries) {
                validate_transaction_directory_for_cleanup(
                    transactions.get(), expected_owner, ledger_entry,
                    "retired transaction ledger entry");
            }
        } else {
            validate_transaction_directory_for_cleanup(
                session_fd, expected_owner, entry,
                "retired current transaction");
        }
    }
    if(require_terminal &&
       !entry_exists(session_fd, std::string(TERMINAL_FILE))) {
        throw_state_error_message("consumed session has no terminal state");
    }
}

void cleanup_session_contents(
    int session_fd, uid_t expected_owner, bool require_terminal) {
    // Validate the entire bounded owner-specific shape before the first
    // unlink. A malformed replacement blocks retirement instead of turning
    // cleanup into a partial or recursive delete.
    validate_session_contents_for_cleanup(
        session_fd, expected_owner, require_terminal);
    remove_validated_file_if_present(
        session_fd, expected_owner, std::string(SESSION_FILE),
        "retired session state");
    remove_validated_file_if_present(
        session_fd, expected_owner,
        std::string(SESSION_FILE) + std::string(PARTIAL_SUFFIX),
        "retired partial session state");
    remove_validated_file_if_present(
        session_fd, expected_owner, std::string(BINDING_FILE),
        "retired child binding");
    remove_validated_file_if_present(
        session_fd, expected_owner,
        std::string(BINDING_FILE) + std::string(PARTIAL_SUFFIX),
        "retired partial child binding");
    remove_validated_file_if_present(
        session_fd, expected_owner, std::string(UNSUPPORTED_FILE),
        "retired unsupported marker");
    remove_validated_file_if_present(
        session_fd, expected_owner,
        std::string(UNSUPPORTED_FILE) + std::string(PARTIAL_SUFFIX),
        "retired partial unsupported marker");
    if(require_terminal && !entry_exists(session_fd, std::string(TERMINAL_FILE))) {
        throw_state_error_message("consumed session has no terminal state");
    }
    remove_validated_file_if_present(
        session_fd, expected_owner, std::string(TERMINAL_FILE),
        "retired terminal state");
    remove_validated_file_if_present(
        session_fd, expected_owner,
        std::string(TERMINAL_FILE) + std::string(PARTIAL_SUFFIX),
        "retired partial terminal state");

    if(entry_exists(session_fd, std::string(CURRENT_TRANSACTION_DIRECTORY))) {
        cleanup_transaction_directory(
            session_fd, expected_owner,
            std::string(CURRENT_TRANSACTION_DIRECTORY),
            "retired current transaction");
    }
    const std::string current_staging =
        ".current.preparing";
    if(entry_exists(session_fd, current_staging)) {
        cleanup_transaction_directory(
            session_fd, expected_owner, current_staging,
            "retired preparing transaction");
    }
    if(entry_exists(session_fd, std::string(TRANSACTIONS_DIRECTORY))) {
        OwnedDescriptor transactions = open_directory_at(
            session_fd, std::string(TRANSACTIONS_DIRECTORY),
            expected_owner, PRIVATE_DIRECTORY_MODE,
            "retired transaction ledger");
        const std::vector<std::string> entries =
            retirement_transaction_ledger_entries(
                transactions.get(), expected_owner);
        for(const std::string& entry : entries) {
            cleanup_transaction_directory(
                transactions.get(), expected_owner, entry,
                "retired transaction ledger entry");
        }
        require_exact_entries(
            transactions.get(), {}, "retired transaction ledger");
        if(unlinkat(
               session_fd, std::string(TRANSACTIONS_DIRECTORY).c_str(),
               AT_REMOVEDIR) == -1) {
            throw_state_error("unable to remove retired transaction ledger");
        }
    }

    require_exact_entries(session_fd, {}, "retired session tombstone");
    synchronize_file(session_fd, "retired session tombstone");
}

OpenSession retire_session(
    int active_fd, int used_fd, uid_t expected_owner,
    const std::string& session_token, OpenSession session,
    bool active_directory_is_locked = false) {
    std::unique_ptr<DescriptorLock> active_lock;
    if(!active_directory_is_locked) {
        active_lock = std::make_unique<DescriptorLock>(
            active_fd, "active makepkg syncdeps directory");
    }
    require_entry_absent(used_fd, session_token, "used session token");
    rename_noreplace(
        active_fd, session_token, used_fd, session_token,
        "used session tombstone");
    synchronize_file(active_fd, "active makepkg syncdeps directory");
    synchronize_file(used_fd, "used makepkg syncdeps directory");
    OpenSession retired = open_session(
        used_fd, expected_owner, session_token,
        "used makepkg syncdeps session tombstone");
    if(!same_identity(session.metadata, retired.metadata)) {
        throw_state_error_message(
            "retired session identity changed during publication");
    }
    return retired;
}

struct RecoverableUsedTombstone {
    std::string session_token;
    OpenSession session;
    bool needs_cleanup = false;
};

void recover_used_tombstones(int used_fd, uid_t expected_owner) {
    const std::vector<std::string> entries = list_directory_entries(used_fd);
    if(entries.size() > MAXIMUM_USED_SESSIONS) {
        throw_state_error_message(
            "makepkg syncdeps replay tombstone limit reached");
    }
    std::vector<RecoverableUsedTombstone> tombstones;
    tombstones.reserve(entries.size());
    for(const std::string& entry : entries) {
        if(!is_valid_makepkg_syncdeps_adapter_token(entry)) {
            throw_state_error_message(
                "used makepkg syncdeps directory contains invalid state");
        }
        OpenSession tombstone = open_session(
            used_fd, expected_owner, entry,
            "used makepkg syncdeps tombstone");
        const bool needs_cleanup =
            !list_directory_entries(tombstone.descriptor.get()).empty();
        if(needs_cleanup) {
            // A used name already permanently denies replay. Validate every
            // remaining known object before any recovery unlink so an
            // unknown or replaced entry cannot turn recovery into blind
            // cleanup or partially affect another tombstone.
            validate_session_contents_for_cleanup(
                tombstone.descriptor.get(), expected_owner, false);
        }
        tombstones.push_back(RecoverableUsedTombstone{
            entry, std::move(tombstone), needs_cleanup});
    }
    for(RecoverableUsedTombstone& tombstone : tombstones) {
        if(!tombstone.needs_cleanup) continue;
        cleanup_session_contents(
            tombstone.session.descriptor.get(), expected_owner, false);
    }
    synchronize_file(used_fd, "used makepkg syncdeps directory");
}

void retire_stale_active_sessions(
    int active_fd, int used_fd, uid_t expected_owner) {
    const std::vector<std::string> entries = list_directory_entries(active_fd);
    if(entries.size() > MAXIMUM_ACTIVE_SESSIONS) {
        throw_state_error_message(
            "active makepkg syncdeps session limit reached");
    }
    for(const std::string& entry : entries) {
        std::string session_token;
        const bool staging = is_staging_session_name(entry, session_token);
        if(!staging) {
            session_token = entry;
            if(!is_valid_makepkg_syncdeps_adapter_token(session_token)) {
                throw_state_error_message(
                    "active makepkg syncdeps directory contains invalid state");
            }
        }
        OpenSession session = open_session(
            active_fd, expected_owner, entry,
            staging ? "preparing makepkg syncdeps session"
                    : "active makepkg syncdeps session");
        if(staging) {
            // A publisher holds the active-directory flock until the staging
            // directory is renamed. Seeing it while holding that same lock
            // therefore proves an abandoned, never-published prepare. No
            // token response or positive authority existed yet.
            cleanup_session_contents(
                session.descriptor.get(), expected_owner, false);
            if(unlinkat(active_fd, entry.c_str(), AT_REMOVEDIR) == -1) {
                throw_state_error(
                    "unable to remove stale preparing session");
            }
            synchronize_file(active_fd, "active makepkg syncdeps directory");
            continue;
        }
        bool supervisor_alive = false;
        if(entry_exists(session.descriptor.get(), std::string(SESSION_FILE))) {
            const MakepkgSyncdepsPreparedSessionState prepared =
                read_and_parse_state<
                    MakepkgSyncdepsPreparedSessionState,
                    MakepkgSyncdepsPreparedSessionStateResult>(
                    session.descriptor.get(), expected_owner,
                    std::string(SESSION_FILE), "prepared session state",
                    parse_makepkg_syncdeps_prepared_session_state);
            if(prepared.session_token != session_token) {
                throw_state_error_message(
                    "stale session token does not match its state");
            }
            supervisor_alive =
                is_exact_stored_process_alive(prepared.supervisor);
        }
        if(supervisor_alive) continue;

        validate_session_contents_for_cleanup(
            session.descriptor.get(), expected_owner, false);
        OpenSession retired = retire_session(
            active_fd, used_fd, expected_owner, session_token,
            std::move(session), true);
        cleanup_session_contents(
            retired.descriptor.get(), expected_owner, false);
    }
}

MakepkgSyncdepsPreparedSessionState validate_prepared_session(
    int session_fd, uid_t expected_owner,
    const std::string& session_token) {
    const MakepkgSyncdepsPreparedSessionState prepared =
        read_and_parse_state<
            MakepkgSyncdepsPreparedSessionState,
            MakepkgSyncdepsPreparedSessionStateResult>(
            session_fd, expected_owner, std::string(SESSION_FILE),
            "prepared makepkg syncdeps session",
            parse_makepkg_syncdeps_prepared_session_state);
    if(prepared.session_token != session_token) {
        throw_state_error_message("prepared session token mismatch");
    }
    return prepared;
}

MakepkgSyncdepsBoundChildState validate_binding(
    int session_fd, uid_t expected_owner,
    const std::string& session_token) {
    const MakepkgSyncdepsBoundChildState binding =
        read_and_parse_state<
            MakepkgSyncdepsBoundChildState,
            MakepkgSyncdepsBoundChildStateResult>(
            session_fd, expected_owner, std::string(BINDING_FILE),
            "bound makepkg child state",
            parse_makepkg_syncdeps_bound_child_state);
    if(binding.session_token != session_token) {
        throw_state_error_message("bound child session token mismatch");
    }
    return binding;
}

struct OpenCurrentTransaction {
    OwnedDescriptor descriptor;
    MakepkgSyncdepsPreparedTransactionState prepared;
};

OpenCurrentTransaction open_current_transaction(
    int session_fd, uid_t expected_owner,
    const std::string& session_token, std::size_t ordinal,
    const std::string& transaction_token) {
    OwnedDescriptor current = open_directory_at(
        session_fd, std::string(CURRENT_TRANSACTION_DIRECTORY),
        expected_owner, PRIVATE_DIRECTORY_MODE,
        "current makepkg syncdeps transaction");
    const MakepkgSyncdepsPreparedTransactionState prepared =
        read_and_parse_state<
            MakepkgSyncdepsPreparedTransactionState,
            MakepkgSyncdepsPreparedTransactionStateResult>(
            current.get(), expected_owner,
            std::string(PREPARED_TRANSACTION_FILE),
            "prepared current transaction",
            parse_makepkg_syncdeps_prepared_transaction_state);
    if(prepared.session_token != session_token ||
       prepared.ordinal != ordinal ||
       prepared.transaction_token != transaction_token) {
        throw_state_error_message(
            "current transaction identity mismatch");
    }
    return OpenCurrentTransaction{std::move(current), prepared};
}

void publish_unsupported_marker(
    int session_fd, uid_t expected_owner,
    const std::string& session_token) {
    if(entry_exists(session_fd, std::string(UNSUPPORTED_FILE))) return;
    publish_private_file(
        session_fd, expected_owner, std::string(UNSUPPORTED_FILE),
        "MOGUET-MAKEPKG-SYNCDEPS-UNSUPPORTED\t1\nSESSION\t" +
            session_token + "\nOWNER\t" +
            std::string(makepkg_syncdeps_adapter_owner()) + "\nEND\n",
        "unsupported session marker");
}

std::vector<MakepkgSyncdepsTransactionManifestEntry>
read_transaction_manifest_entries(
    int transactions_fd, uid_t expected_owner,
    const std::string& session_token) {
    const std::vector<std::string> entries =
        transaction_ledger_entries(transactions_fd, expected_owner);
    std::vector<MakepkgSyncdepsTransactionManifestEntry> transactions;
    for(std::size_t index = 0; index < entries.size(); ++index) {
        OwnedDescriptor transaction = open_directory_at(
            transactions_fd, entries[index], expected_owner,
            PRIVATE_DIRECTORY_MODE, "transaction ledger entry");
        require_exact_entries(
            transaction.get(),
            {std::string(PREPARED_TRANSACTION_FILE),
             std::string(OBSERVATION_FILE), std::string(OUTCOME_FILE)},
            "complete transaction ledger entry");
        const auto prepared = read_and_parse_state<
            MakepkgSyncdepsPreparedTransactionState,
            MakepkgSyncdepsPreparedTransactionStateResult>(
            transaction.get(), expected_owner,
            std::string(PREPARED_TRANSACTION_FILE),
            "prepared transaction ledger state",
            parse_makepkg_syncdeps_prepared_transaction_state);
        const auto observation = read_and_parse_state<
            MakepkgSyncdepsTransactionObservationState,
            MakepkgSyncdepsTransactionObservationStateResult>(
            transaction.get(), expected_owner,
            std::string(OBSERVATION_FILE),
            "transaction observation ledger state",
            parse_makepkg_syncdeps_transaction_observation_state);
        const auto outcome = read_and_parse_state<
            MakepkgSyncdepsTransactionOutcomeState,
            MakepkgSyncdepsTransactionOutcomeStateResult>(
            transaction.get(), expected_owner,
            std::string(OUTCOME_FILE),
            "transaction outcome ledger state",
            parse_makepkg_syncdeps_transaction_outcome_state);
        const std::size_t expected_ordinal = index + 1U;
        if(prepared.session_token != session_token ||
           observation.session_token != session_token ||
           outcome.session_token != session_token ||
           prepared.ordinal != expected_ordinal ||
           observation.ordinal != expected_ordinal ||
           outcome.ordinal != expected_ordinal ||
           prepared.transaction_token != observation.transaction_token ||
           prepared.transaction_token != outcome.transaction_token) {
            throw_state_error_message(
                "transaction ledger identity is inconsistent");
        }
        transactions.push_back(
            MakepkgSyncdepsTransactionManifestEntry{
                prepared, observation, outcome});
    }
    return transactions;
}

} // namespace

MakepkgSyncdepsPidfd::MakepkgSyncdepsPidfd(
    int descriptor, MakepkgSyncdepsPidfdIdentity identity) noexcept
    : descriptor_(descriptor), identity_(identity) {
}

MakepkgSyncdepsPidfd MakepkgSyncdepsPidfd::open(pid_t pid) {
    if(pid <= 0) throw_state_error_message("process identifier is invalid");
#ifdef SYS_pidfd_open
    long descriptor;
    do {
        descriptor = syscall(SYS_pidfd_open, pid, 0U);
    } while(descriptor == -1 && errno == EINTR);
    if(descriptor == -1) throw_state_error("unable to open pidfd");
    OwnedDescriptor owned(static_cast<int>(descriptor));
    const ProcStatus status = read_process_status(pid);
    const MakepkgSyncdepsPidfdIdentity identity =
        basic_pidfd_identity(owned.get(), pid, status.uid);
    if(!descriptor_is_alive(owned.get())) {
        throw_state_error_message("process exited before pidfd binding");
    }
    return MakepkgSyncdepsPidfd(owned.release(), identity);
#else
    static_cast<void>(pid);
    throw_state_error_message(
        "pidfd_open is unavailable for makepkg syncdeps binding");
#endif
}

MakepkgSyncdepsPidfd::MakepkgSyncdepsPidfd(
    MakepkgSyncdepsPidfd&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      identity_(other.identity_) {
}

MakepkgSyncdepsPidfd& MakepkgSyncdepsPidfd::operator=(
    MakepkgSyncdepsPidfd&& other) noexcept {
    if(this == &other) return *this;
    if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
    descriptor_ = std::exchange(other.descriptor_, -1);
    identity_ = other.identity_;
    return *this;
}

MakepkgSyncdepsPidfd::~MakepkgSyncdepsPidfd() {
    if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
}

int MakepkgSyncdepsPidfd::descriptor() const noexcept {
    return descriptor_;
}

const MakepkgSyncdepsPidfdIdentity& MakepkgSyncdepsPidfd::identity()
    const noexcept {
    return identity_;
}

bool MakepkgSyncdepsPidfd::is_alive() const {
    if(descriptor_ < 0) return false;
    return descriptor_is_alive(descriptor_);
}

MakepkgSyncdepsObservedProcess observe_makepkg_syncdeps_process(pid_t pid) {
    MakepkgSyncdepsPidfd first = MakepkgSyncdepsPidfd::open(pid);
    const ProcStatus status = read_process_status(pid);
    const MakepkgSyncdepsInstalledExecutableIdentity executable =
        read_process_executable_identity(pid);
    MakepkgSyncdepsPidfd second = MakepkgSyncdepsPidfd::open(pid);
    if(!same_pidfd_identity(first.identity(), second.identity()) ||
       first.identity().uid != status.uid || !first.is_alive()) {
        throw_state_error_message(
            "process identity changed during pidfd observation");
    }
    return MakepkgSyncdepsObservedProcess{
        std::move(first), status.parent_pid, status.tracer_pid, executable};
}

MakepkgSyncdepsRetainedProcessObservation
observe_makepkg_syncdeps_retained_process(
    const MakepkgSyncdepsPidfd& process) {
    if(!process.is_alive()) {
        throw_state_error_message(
            "retained process exited before observation");
    }
    const ProcStatus status = read_process_status(process.identity().pid);
    const MakepkgSyncdepsInstalledExecutableIdentity executable =
        read_process_executable_identity(process.identity().pid);
    if(!process.is_alive() || process.identity().uid != status.uid) {
        throw_state_error_message(
            "retained process identity changed during observation");
    }
    return MakepkgSyncdepsRetainedProcessObservation{
        status.parent_pid, status.tracer_pid,
        static_cast<std::uint32_t>(status.uid), executable};
}

bool makepkg_syncdeps_pidfd_identity_matches(
    const MakepkgSyncdepsPidfdIdentity& lhs,
    const MakepkgSyncdepsPidfdIdentity& rhs) noexcept {
    return lhs.uid == rhs.uid && same_pidfd_identity(lhs, rhs);
}

bool makepkg_syncdeps_executable_identity_matches(
    const MakepkgSyncdepsInstalledExecutableIdentity& lhs,
    const MakepkgSyncdepsInstalledExecutableIdentity& rhs) noexcept {
    return lhs.device != 0 && lhs.inode != 0 && lhs.device == rhs.device &&
           lhs.inode == rhs.inode;
}

struct MakepkgSyncdepsAdapterStateStore::Implementation {
    uid_t expected_owner;
    OwnedDescriptor state_root;
    OwnedDescriptor active;
    OwnedDescriptor used;
    OpenSession session;
    MakepkgSyncdepsPreparedSessionState prepared;
    struct stat session_file_metadata{};
    bool retired = false;
};

MakepkgSyncdepsAdapterStateStore::MakepkgSyncdepsAdapterStateStore(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {
}

MakepkgSyncdepsAdapterStateStore
MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
    int runtime_parent_fd, uid_t expected_owner,
    const MakepkgSyncdepsPreparedSessionState& prepared_state) {
    if(runtime_parent_fd < 0 ||
       !is_valid_makepkg_syncdeps_adapter_token(
           prepared_state.session_token)) {
        throw_state_error_message("session create request is invalid");
    }
    static_cast<void>(require_descriptor_metadata(
        runtime_parent_fd, expected_owner, S_IFDIR, std::nullopt,
        "runtime parent"));
    OwnedDescriptor moguet = ensure_private_directory(
        runtime_parent_fd, "moguet", expected_owner,
        "Moguet runtime directory");
    OwnedDescriptor state_root = ensure_private_directory(
        moguet.get(), "makepkg-syncdeps", expected_owner,
        "makepkg syncdeps runtime directory");
    OwnedDescriptor active = ensure_private_directory(
        state_root.get(), "active", expected_owner,
        "active makepkg syncdeps directory");
    OwnedDescriptor used = ensure_private_directory(
        state_root.get(), "used", expected_owner,
        "used makepkg syncdeps directory");
    require_exact_entries(
        state_root.get(), {"active", "used"},
        "makepkg syncdeps runtime directory");

    DescriptorLock active_lock(
        active.get(), "active makepkg syncdeps directory");
    recover_used_tombstones(used.get(), expected_owner);
    retire_stale_active_sessions(
        active.get(), used.get(), expected_owner);
    recover_used_tombstones(used.get(), expected_owner);
    if(list_directory_entries(active.get()).size() >=
       MAXIMUM_ACTIVE_SESSIONS) {
        throw_state_error_message(
            "active makepkg syncdeps session limit reached");
    }
    require_entry_absent(
        active.get(), prepared_state.session_token,
        "active session token");
    require_entry_absent(
        used.get(), prepared_state.session_token,
        "used session token");
    const std::string staging_name =
        staging_session_name(prepared_state.session_token);
    require_entry_absent(
        active.get(), staging_name, "preparing session token");

    if(mkdirat(
           active.get(), staging_name.c_str(),
           PRIVATE_DIRECTORY_MODE) == -1) {
        throw_state_error("unable to create preparing session");
    }
    bool published = false;
    struct stat session_file_metadata{};
    try {
        OpenSession staging = open_session(
            active.get(), expected_owner, staging_name,
            "preparing makepkg syncdeps session");
        OwnedDescriptor session_file = create_private_file(
            staging.descriptor.get(), std::string(SESSION_FILE),
            expected_owner, "prepared session state");
        write_all(
            session_file.get(),
            serialize_makepkg_syncdeps_prepared_session_state(
                prepared_state));
        synchronize_file(session_file.get(), "prepared session state");
        session_file_metadata = require_descriptor_metadata(
            session_file.get(), expected_owner, S_IFREG,
            PRIVATE_FILE_MODE, "prepared session state");
        static_cast<void>(ensure_private_directory(
            staging.descriptor.get(),
            std::string(TRANSACTIONS_DIRECTORY), expected_owner,
            "session transaction ledger"));
        require_exact_entries(
            staging.descriptor.get(),
            {std::string(SESSION_FILE),
             std::string(TRANSACTIONS_DIRECTORY)},
            "preparing makepkg syncdeps session");
        synchronize_file(
            staging.descriptor.get(), "preparing makepkg syncdeps session");
        rename_noreplace(
            active.get(), staging_name, active.get(),
            prepared_state.session_token,
            "active makepkg syncdeps session");
        published = true;
        synchronize_file(active.get(), "active makepkg syncdeps directory");
        OpenSession session = open_session(
            active.get(), expected_owner, prepared_state.session_token,
            "active makepkg syncdeps session");
        if(!same_identity(staging.metadata, session.metadata)) {
            throw_state_error_message(
                "published session identity changed");
        }
        return MakepkgSyncdepsAdapterStateStore(
            std::make_unique<Implementation>(Implementation{
                expected_owner, std::move(state_root), std::move(active),
                std::move(used), std::move(session), prepared_state,
                session_file_metadata, false}));
    } catch(...) {
        try {
            const std::string cleanup_name =
                published ? prepared_state.session_token : staging_name;
            OpenSession cleanup = open_session(
                active.get(), expected_owner, cleanup_name,
                "failed makepkg syncdeps session publication");
            cleanup_session_contents(
                cleanup.descriptor.get(), expected_owner, false);
            if(unlinkat(
                   active.get(), cleanup_name.c_str(),
                   AT_REMOVEDIR) == -1) {
                throw_state_error(
                    "unable to remove failed session publication");
            }
            synchronize_file(active.get(), "active makepkg syncdeps directory");
        } catch(...) {
            throw_state_error_message(
                "session publication failed and exact cleanup failed");
        }
        throw;
    }
}

MakepkgSyncdepsAdapterStateStore::MakepkgSyncdepsAdapterStateStore(
    MakepkgSyncdepsAdapterStateStore&&) noexcept = default;

MakepkgSyncdepsAdapterStateStore&
MakepkgSyncdepsAdapterStateStore::operator=(
    MakepkgSyncdepsAdapterStateStore&&) noexcept = default;

MakepkgSyncdepsAdapterStateStore::~MakepkgSyncdepsAdapterStateStore() = default;

const std::string& MakepkgSyncdepsAdapterStateStore::session_token()
    const noexcept {
    static const std::string EMPTY;
    return implementation_ == nullptr
               ? EMPTY
               : implementation_->prepared.session_token;
}

const MakepkgSyncdepsPreparedSessionState&
MakepkgSyncdepsAdapterStateStore::prepared_state() const noexcept {
    return implementation_->prepared;
}

void MakepkgSyncdepsAdapterStateStore::bind_child(
    const MakepkgSyncdepsBoundChildState& binding) {
    if(implementation_ == nullptr || implementation_->retired ||
       binding.session_token != session_token() ||
       binding.child.uid != implementation_->prepared.invoking_uid ||
       binding.transaction_adapter.uid !=
           implementation_->prepared.invoking_uid ||
       makepkg_syncdeps_pidfd_identity_matches(
           binding.child, binding.transaction_adapter)) {
        throw_state_error_message("child bind request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    static_cast<void>(validate_prepared_session(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    if(entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE)) ||
       entry_exists(state.session.descriptor.get(), std::string(BINDING_FILE))) {
        throw_state_error_message(
            "child binding is not a one-shot active transition");
    }
    publish_private_file(
        state.session.descriptor.get(), state.expected_owner,
        std::string(BINDING_FILE),
        serialize_makepkg_syncdeps_bound_child_state(binding),
        "bound makepkg child state");
}

MakepkgSyncdepsTransactionPrepareResponse
MakepkgSyncdepsAdapterStateStore::prepare_transaction(
    std::size_t ordinal,
    const std::vector<std::string>& dependency_specifications) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("transaction prepare request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    static_cast<void>(validate_prepared_session(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    static_cast<void>(validate_binding(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    if(entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE)) ||
       entry_exists(state.session.descriptor.get(), std::string(UNSUPPORTED_FILE))) {
        throw_state_error_message(
            "post-finalize or unsupported transaction mutation rejected");
    }
    OwnedDescriptor transactions = open_directory_at(
        state.session.descriptor.get(),
        std::string(TRANSACTIONS_DIRECTORY), state.expected_owner,
        PRIVATE_DIRECTORY_MODE, "session transaction ledger");
    const std::vector<std::string> ledger_entries =
        transaction_ledger_entries(transactions.get(), state.expected_owner);
    const std::size_t expected_ordinal = ledger_entries.size() + 1U;
    if(entry_exists(
           state.session.descriptor.get(),
           std::string(CURRENT_TRANSACTION_DIRECTORY)) ||
       ordinal != expected_ordinal || ordinal > 2) {
        publish_unsupported_marker(
            state.session.descriptor.get(), state.expected_owner,
            session_token());
        throw_state_error_message(
            "concurrent, reentrant, ordinal-mismatched, or third transaction rejected");
    }

    const std::optional<std::string> token =
        generate_makepkg_syncdeps_adapter_token();
    if(!token.has_value()) {
        throw_state_error_message(
            "cryptographic transaction token generation failed");
    }
    for(const std::string& entry : ledger_entries) {
        OwnedDescriptor transaction = open_directory_at(
            transactions.get(), entry, state.expected_owner,
            PRIVATE_DIRECTORY_MODE, "transaction ledger entry");
        const auto prepared = read_and_parse_state<
            MakepkgSyncdepsPreparedTransactionState,
            MakepkgSyncdepsPreparedTransactionStateResult>(
            transaction.get(), state.expected_owner,
            std::string(PREPARED_TRANSACTION_FILE),
            "prepared transaction ledger state",
            parse_makepkg_syncdeps_prepared_transaction_state);
        if(prepared.transaction_token == *token) {
            throw_state_error_message(
                "cryptographic transaction token collision");
        }
    }
    const MakepkgSyncdepsPreparedTransactionState prepared{
        session_token(), ordinal, *token, dependency_specifications};
    const std::string staging_name = ".current.preparing";
    require_entry_absent(
        state.session.descriptor.get(), staging_name,
        "preparing transaction state");
    if(mkdirat(
           state.session.descriptor.get(), staging_name.c_str(),
           PRIVATE_DIRECTORY_MODE) == -1) {
        throw_state_error("unable to create preparing transaction state");
    }
    bool published = false;
    try {
        OwnedDescriptor staging = open_directory_at(
            state.session.descriptor.get(), staging_name,
            state.expected_owner, PRIVATE_DIRECTORY_MODE,
            "preparing transaction state");
        OwnedDescriptor prepared_file = create_private_file(
            staging.get(), std::string(PREPARED_TRANSACTION_FILE),
            state.expected_owner, "prepared transaction state");
        write_all(
            prepared_file.get(),
            serialize_makepkg_syncdeps_prepared_transaction_state(
                prepared));
        synchronize_file(prepared_file.get(), "prepared transaction state");
        require_exact_entries(
            staging.get(), {std::string(PREPARED_TRANSACTION_FILE)},
            "preparing transaction state");
        synchronize_file(staging.get(), "preparing transaction state");
        rename_noreplace(
            state.session.descriptor.get(), staging_name,
            state.session.descriptor.get(),
            std::string(CURRENT_TRANSACTION_DIRECTORY),
            "current transaction state");
        published = true;
        synchronize_file(
            state.session.descriptor.get(),
            "active makepkg syncdeps session");
    } catch(...) {
        const std::string cleanup_name =
            published ? std::string(CURRENT_TRANSACTION_DIRECTORY)
                      : staging_name;
        try {
            cleanup_transaction_directory(
                state.session.descriptor.get(), state.expected_owner,
                cleanup_name, "failed preparing transaction state");
        } catch(...) {
            throw_state_error_message(
                "transaction prepare failed and exact cleanup failed");
        }
        throw;
    }
    return MakepkgSyncdepsTransactionPrepareResponse{
        session_token(), ordinal, *token};
}

void MakepkgSyncdepsAdapterStateStore::record_transaction(
    std::size_t ordinal, const std::string& transaction_token,
    MakepkgSyncdepsSyntheticObservation observation) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("transaction record request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    if(entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE))) {
        throw_state_error_message("post-finalize mutation rejected");
    }
    OpenCurrentTransaction current = open_current_transaction(
        state.session.descriptor.get(), state.expected_owner,
        session_token(), ordinal, transaction_token);
    require_exact_entries(
        current.descriptor.get(),
        {std::string(PREPARED_TRANSACTION_FILE)},
        "recordable current transaction");
    publish_private_file(
        current.descriptor.get(), state.expected_owner,
        std::string(OBSERVATION_FILE),
        serialize_makepkg_syncdeps_transaction_observation_state(
            MakepkgSyncdepsTransactionObservationState{
                session_token(), ordinal, transaction_token,
                observation}),
        "synthetic transaction observation");
}

void MakepkgSyncdepsAdapterStateStore::finalize_transaction(
    std::size_t ordinal, const std::string& transaction_token,
    MakepkgSyncdepsCommandOutcome outcome, int exit_code) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("transaction finalize request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    if(entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE))) {
        throw_state_error_message("post-finalize mutation rejected");
    }
    OpenCurrentTransaction current = open_current_transaction(
        state.session.descriptor.get(), state.expected_owner,
        session_token(), ordinal, transaction_token);
    require_exact_entries(
        current.descriptor.get(),
        {std::string(PREPARED_TRANSACTION_FILE),
         std::string(OBSERVATION_FILE)},
        "finalizable current transaction");
    publish_private_file(
        current.descriptor.get(), state.expected_owner,
        std::string(OUTCOME_FILE),
        serialize_makepkg_syncdeps_transaction_outcome_state(
            MakepkgSyncdepsTransactionOutcomeState{
                session_token(), ordinal, transaction_token,
                outcome, exit_code}),
        "transaction command outcome");
}

void MakepkgSyncdepsAdapterStateStore::consume_transaction(
    std::size_t ordinal, const std::string& transaction_token) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("transaction consume request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    if(entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE))) {
        throw_state_error_message("post-finalize mutation rejected");
    }
    OpenCurrentTransaction current = open_current_transaction(
        state.session.descriptor.get(), state.expected_owner,
        session_token(), ordinal, transaction_token);
    require_exact_entries(
        current.descriptor.get(),
        {std::string(PREPARED_TRANSACTION_FILE),
         std::string(OBSERVATION_FILE), std::string(OUTCOME_FILE)},
        "consumable current transaction");
    static_cast<void>(read_and_parse_state<
                      MakepkgSyncdepsTransactionObservationState,
                      MakepkgSyncdepsTransactionObservationStateResult>(
        current.descriptor.get(), state.expected_owner,
        std::string(OBSERVATION_FILE), "transaction observation",
        parse_makepkg_syncdeps_transaction_observation_state));
    static_cast<void>(read_and_parse_state<
                      MakepkgSyncdepsTransactionOutcomeState,
                      MakepkgSyncdepsTransactionOutcomeStateResult>(
        current.descriptor.get(), state.expected_owner,
        std::string(OUTCOME_FILE), "transaction outcome",
        parse_makepkg_syncdeps_transaction_outcome_state));
    OwnedDescriptor transactions = open_directory_at(
        state.session.descriptor.get(),
        std::string(TRANSACTIONS_DIRECTORY), state.expected_owner,
        PRIVATE_DIRECTORY_MODE, "session transaction ledger");
    require_entry_absent(
        transactions.get(), std::to_string(ordinal),
        "transaction ordinal");
    rename_noreplace(
        state.session.descriptor.get(),
        std::string(CURRENT_TRANSACTION_DIRECTORY), transactions.get(),
        std::to_string(ordinal), "transaction ledger entry");
    synchronize_file(transactions.get(), "session transaction ledger");
    synchronize_file(
        state.session.descriptor.get(), "active makepkg syncdeps session");
}

void MakepkgSyncdepsAdapterStateStore::abort_transaction(
    std::size_t ordinal, const std::string& transaction_token) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("transaction abort request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    OpenCurrentTransaction current = open_current_transaction(
        state.session.descriptor.get(), state.expected_owner,
        session_token(), ordinal, transaction_token);
    const std::vector<std::string> entries =
        list_directory_entries(current.descriptor.get());
    if(entries != std::vector<std::string>{
                      std::string(PREPARED_TRANSACTION_FILE)}) {
        throw_state_error_message(
            "transaction abort cannot overwrite recorded or partial evidence");
    }
    publish_private_file(
        current.descriptor.get(), state.expected_owner,
        std::string(OBSERVATION_FILE),
        serialize_makepkg_syncdeps_transaction_observation_state(
            MakepkgSyncdepsTransactionObservationState{
                session_token(), ordinal, transaction_token,
                MakepkgSyncdepsSyntheticObservation::Missing}),
        "aborted transaction observation");
    publish_private_file(
        current.descriptor.get(), state.expected_owner,
        std::string(OUTCOME_FILE),
        serialize_makepkg_syncdeps_transaction_outcome_state(
            MakepkgSyncdepsTransactionOutcomeState{
                session_token(), ordinal, transaction_token,
                MakepkgSyncdepsCommandOutcome::NotAttempted,
                std::nullopt}),
        "aborted transaction outcome");
    consume_transaction(ordinal, transaction_token);
}

void MakepkgSyncdepsAdapterStateStore::finalize_session(
    MakepkgSyncdepsCommandOutcome makepkg_outcome,
    int makepkg_exit_code) {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("session finalize request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    static_cast<void>(validate_prepared_session(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    static_cast<void>(validate_binding(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    if(entry_exists(
           state.session.descriptor.get(),
           std::string(CURRENT_TRANSACTION_DIRECTORY)) ||
       entry_exists(state.session.descriptor.get(), std::string(TERMINAL_FILE))) {
        throw_state_error_message(
            "session cannot finalize with an active transaction or twice");
    }
    OwnedDescriptor transactions = open_directory_at(
        state.session.descriptor.get(),
        std::string(TRANSACTIONS_DIRECTORY), state.expected_owner,
        PRIVATE_DIRECTORY_MODE, "session transaction ledger");
    const std::size_t transaction_count =
        read_transaction_manifest_entries(
            transactions.get(), state.expected_owner,
            session_token())
            .size();
    const bool unsupported = entry_exists(
        state.session.descriptor.get(), std::string(UNSUPPORTED_FILE));
    const MakepkgSyncdepsTerminalSessionState terminal{
        session_token(),
        unsupported ? MakepkgSyncdepsTerminalState::Unsupported
                    : MakepkgSyncdepsTerminalState::Complete,
        unsupported ? MakepkgSyncdepsAdapterCoverage::Unsupported
                    : MakepkgSyncdepsAdapterCoverage::Complete,
        makepkg_outcome, makepkg_exit_code, transaction_count};
    publish_private_file(
        state.session.descriptor.get(), state.expected_owner,
        std::string(TERMINAL_FILE),
        serialize_makepkg_syncdeps_terminal_session_state(terminal),
        "terminal makepkg syncdeps session");
}

std::string MakepkgSyncdepsAdapterStateStore::consume_session() {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("session consume request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    const MakepkgSyncdepsPreparedSessionState prepared =
        validate_prepared_session(
            state.session.descriptor.get(), state.expected_owner,
            session_token());
    const MakepkgSyncdepsBoundChildState binding = validate_binding(
        state.session.descriptor.get(), state.expected_owner,
        session_token());
    const MakepkgSyncdepsTerminalSessionState terminal =
        read_and_parse_state<
            MakepkgSyncdepsTerminalSessionState,
            MakepkgSyncdepsTerminalSessionStateResult>(
            state.session.descriptor.get(), state.expected_owner,
            std::string(TERMINAL_FILE), "terminal session state",
            parse_makepkg_syncdeps_terminal_session_state);
    OwnedDescriptor transactions = open_directory_at(
        state.session.descriptor.get(),
        std::string(TRANSACTIONS_DIRECTORY), state.expected_owner,
        PRIVATE_DIRECTORY_MODE, "session transaction ledger");
    std::vector<MakepkgSyncdepsTransactionManifestEntry> transaction_entries =
        read_transaction_manifest_entries(
            transactions.get(), state.expected_owner, session_token());
    if(terminal.session_token != session_token() ||
       terminal.transaction_count != transaction_entries.size() ||
       entry_exists(
           state.session.descriptor.get(),
           std::string(CURRENT_TRANSACTION_DIRECTORY))) {
        throw_state_error_message(
            "terminal session state is incomplete or mismatched");
    }
    std::vector<std::string> expected{
        std::string(SESSION_FILE), std::string(BINDING_FILE),
        std::string(TRANSACTIONS_DIRECTORY),
        std::string(TERMINAL_FILE)};
    if(entry_exists(state.session.descriptor.get(), std::string(UNSUPPORTED_FILE))) {
        expected.emplace_back(UNSUPPORTED_FILE);
    }
    require_exact_entries(
        state.session.descriptor.get(), expected,
        "consumable makepkg syncdeps session");
    const std::string manifest =
        serialize_makepkg_syncdeps_session_manifest(
            MakepkgSyncdepsSessionManifest{
                prepared, binding, terminal,
                MakepkgSyncdepsEvidenceKind::Synthetic,
                std::move(transaction_entries)});
    validate_session_contents_for_cleanup(
        state.session.descriptor.get(), state.expected_owner, true);
    DescriptorLock active_lock(
        state.active.get(), "active makepkg syncdeps directory");
    OpenSession retired = retire_session(
        state.active.get(), state.used.get(), state.expected_owner,
        session_token(), std::move(state.session), true);
    cleanup_session_contents(
        retired.descriptor.get(), state.expected_owner, true);
    state.session = std::move(retired);
    state.retired = true;
    return manifest;
}

void MakepkgSyncdepsAdapterStateStore::abort_session() {
    if(implementation_ == nullptr || implementation_->retired) {
        throw_state_error_message("session abort request is invalid");
    }
    Implementation& state = *implementation_;
    require_named_identity(
        state.active.get(), session_token(), state.session.metadata,
        "active makepkg syncdeps session");
    require_named_identity(
        state.session.descriptor.get(), std::string(SESSION_FILE),
        state.session_file_metadata, "prepared session state");
    static_cast<void>(validate_prepared_session(
        state.session.descriptor.get(), state.expected_owner,
        session_token()));
    validate_session_contents_for_cleanup(
        state.session.descriptor.get(), state.expected_owner, false);
    DescriptorLock active_lock(
        state.active.get(), "active makepkg syncdeps directory");
    OpenSession retired = retire_session(
        state.active.get(), state.used.get(), state.expected_owner,
        session_token(), std::move(state.session), true);
    cleanup_session_contents(
        retired.descriptor.get(), state.expected_owner, false);
    state.session = std::move(retired);
    state.retired = true;
}
