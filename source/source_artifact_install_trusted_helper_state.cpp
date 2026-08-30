#include "source_artifact_install_trusted_helper_state.hpp"

#include "trusted_alpm_receipt_protocol.hpp"

#include <alpm.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <linux/fs.h>

#ifndef MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH
#error "MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH is required"
#endif

namespace {

constexpr mode_t PRIVATE_DIRECTORY_MODE = 0700;
constexpr mode_t PRIVATE_FILE_MODE = 0600;
constexpr std::string_view PREPARED_FILE = "prepared";
constexpr std::string_view HOOK_DIRECTORY = "hooks";
constexpr std::string_view ARTIFACT_DIRECTORY = "artifacts";
constexpr std::string_view RECEIPT_FILE = "receipt";
constexpr std::string_view PARTIAL_RECEIPT_FILE = "receipt.partial";

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

private:
    void reset() noexcept {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = -1;
    }

    int descriptor_ = -1;
};

struct AlpmHandleDeleter {
    void operator()(alpm_handle_t* handle) const noexcept {
        if(handle != nullptr) static_cast<void>(alpm_release(handle));
    }
};

struct AlpmPackageDeleter {
    void operator()(alpm_pkg_t* package) const noexcept {
        if(package != nullptr) static_cast<void>(alpm_pkg_free(package));
    }
};

using AlpmHandle = std::unique_ptr<alpm_handle_t, AlpmHandleDeleter>;
using AlpmPackage = std::unique_ptr<alpm_pkg_t, AlpmPackageDeleter>;

[[noreturn]] void throw_state_error(const std::string& action) {
    const int error_number = errno;
    throw SourceArtifactInstallTrustedStateError(
        action + ": " + std::strerror(error_number));
}

[[noreturn]] void throw_state_error_message(const std::string& message) {
    throw SourceArtifactInstallTrustedStateError(message);
}

bool same_identity(const struct stat& lhs, const struct stat& rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_mode == rhs.st_mode && lhs.st_uid == rhs.st_uid;
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
    throw_state_error("unable to inspect source-artifact state entry");
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
        throw_state_error("unable to duplicate source-artifact directory");
    }
    DIR* directory = fdopendir(duplicate);
    if(directory == nullptr) {
        const int error_number = errno;
        static_cast<void>(close(duplicate));
        errno = error_number;
        throw_state_error("unable to enumerate source-artifact directory");
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
        throw_state_error("unable to close source-artifact enumeration");
    }
    if(read_error != 0) {
        errno = read_error;
        throw_state_error("unable to enumerate source-artifact directory");
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
    const std::string& description,
    std::optional<std::uint64_t> expected_size = std::nullopt) {
    const int descriptor = openat(
        parent_fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor == -1) {
        throw_state_error("unable to open " + description);
    }
    OwnedDescriptor owned(descriptor);
    const struct stat metadata = require_descriptor_metadata(
        descriptor, expected_owner, S_IFREG, PRIVATE_FILE_MODE,
        description);
    if(expected_size.has_value() &&
       (metadata.st_size < 0 ||
        static_cast<std::uint64_t>(metadata.st_size) !=
            *expected_size)) {
        throw_state_error_message(description + " has an unexpected size");
    }
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
        throw_state_error("unable to write source-artifact state");
    }
}

void copy_exact_range(
    int source_fd, std::uint64_t source_offset,
    int destination_fd, std::uint64_t byte_count) {
    std::array<char, 64U * 1024U> buffer{};
    std::uint64_t copied = 0;
    while(copied < byte_count) {
        const std::uint64_t remaining = byte_count - copied;
        const std::size_t request_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        const std::uint64_t absolute_offset = source_offset + copied;
        if(absolute_offset >
           static_cast<std::uint64_t>(
               std::numeric_limits<off_t>::max())) {
            throw_state_error_message(
                "sealed source-artifact input offset is too large");
        }
        const ssize_t count = pread(
            source_fd, buffer.data(), request_size,
            static_cast<off_t>(absolute_offset));
        if(count > 0) {
            std::size_t written_offset = 0;
            const std::size_t count_size = static_cast<std::size_t>(count);
            while(written_offset < count_size) {
                const ssize_t written = write(
                    destination_fd, buffer.data() + written_offset,
                    count_size - written_offset);
                if(written > 0) {
                    written_offset += static_cast<std::size_t>(written);
                    continue;
                }
                if(written == -1 && errno == EINTR) continue;
                throw_state_error("unable to stage source-artifact bytes");
            }
            copied += count_size;
            continue;
        }
        if(count == -1 && errno == EINTR) continue;
        if(count == 0) {
            throw_state_error_message(
                "sealed source-artifact input ended unexpectedly");
        }
        throw_state_error("unable to read sealed source-artifact input");
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
        "renameat2 is unavailable for source-artifact publication");
#endif
}

std::string preparing_name(const std::string& transaction_token) {
    return "." + transaction_token + ".preparing";
}

std::string staged_artifact_filename(std::size_t ordinal) {
    return "artifact-" + std::to_string(ordinal) + ".pkg.tar.zst";
}

std::string staged_signature_filename(std::size_t ordinal) {
    return staged_artifact_filename(ordinal) + ".sig";
}

std::string hook_contents(const std::string& transaction_token) {
    return "[Trigger]\n"
           "Operation = Install\n"
           "Type = Package\n"
           "Target = *\n"
           "\n"
           "[Action]\n"
           "Description = Record Moguet source-artifact installs\n"
           "When = PostTransaction\n"
           "Exec = " MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH
           " record " +
           transaction_token + "\nNeedsTargets\n";
}

bool unlink_if_present(int parent_fd, const std::string& name, int flags = 0) {
    if(unlinkat(parent_fd, name.c_str(), flags) == 0) return true;
    return errno == ENOENT;
}

std::vector<std::string> expected_artifact_entries(
    const SourceArtifactInstallRootPrepareRequest& request) {
    std::vector<std::string> entries;
    entries.reserve(request.artifacts.size() * 2);
    for(std::size_t index = 0; index < request.artifacts.size(); ++index) {
        entries.push_back(staged_artifact_filename(index));
        if(request.artifacts[index].signature_size > 0) {
            entries.push_back(staged_signature_filename(index));
        }
    }
    return entries;
}

bool cleanup_preparing_directory(
    int active_fd, const std::string& staging_name,
    const SourceArtifactInstallRootPrepareRequest& request) noexcept {
    try {
        const int staging_descriptor = openat(
            active_fd, staging_name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(staging_descriptor == -1) return errno == ENOENT;
        OwnedDescriptor staging(staging_descriptor);

        const int hooks_descriptor = openat(
            staging.get(), std::string(HOOK_DIRECTORY).c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(hooks_descriptor >= 0) {
            OwnedDescriptor hooks(hooks_descriptor);
            static_cast<void>(unlink_if_present(
                hooks.get(), source_artifact_install_hook_filename(
                                 request.transaction_token)));
        }
        static_cast<void>(unlink_if_present(
            staging.get(), std::string(HOOK_DIRECTORY), AT_REMOVEDIR));

        const int artifacts_descriptor = openat(
            staging.get(), std::string(ARTIFACT_DIRECTORY).c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(artifacts_descriptor >= 0) {
            OwnedDescriptor artifacts(artifacts_descriptor);
            for(const std::string& name : expected_artifact_entries(request)) {
                static_cast<void>(unlink_if_present(artifacts.get(), name));
            }
        }
        static_cast<void>(unlink_if_present(
            staging.get(), std::string(ARTIFACT_DIRECTORY),
            AT_REMOVEDIR));
        static_cast<void>(unlink_if_present(
            staging.get(), std::string(PREPARED_FILE)));
        return unlink_if_present(active_fd, staging_name, AT_REMOVEDIR);
    } catch(...) {
        return false;
    }
}

struct OpenTransaction {
    OwnedDescriptor descriptor;
    struct stat metadata{};
};

OpenTransaction open_transaction(
    int active_fd, uid_t expected_owner,
    const std::string& transaction_token) {
    OwnedDescriptor descriptor = open_directory_at(
        active_fd, transaction_token, expected_owner,
        PRIVATE_DIRECTORY_MODE,
        "active source-artifact transaction");
    const struct stat metadata = require_descriptor_metadata(
        descriptor.get(), expected_owner, S_IFDIR,
        PRIVATE_DIRECTORY_MODE,
        "active source-artifact transaction");
    return OpenTransaction{std::move(descriptor), metadata};
}

void require_archive_identity(
    int artifact_fd,
    const SourceArtifactInstallRootArtifactExpectation& expected) {
    alpm_errno_t initialization_error = ALPM_ERR_OK;
    AlpmHandle handle(
        alpm_initialize("/", "/var/lib/pacman", &initialization_error));
    if(handle == nullptr) {
        throw_state_error_message(
            "unable to initialize libalpm for staged artifact metadata");
    }
    const std::string descriptor_path =
        "/proc/self/fd/" + std::to_string(artifact_fd);
    alpm_pkg_t* loaded_package = nullptr;
    if(alpm_pkg_load(
           handle.get(), descriptor_path.c_str(), 0, 0,
           &loaded_package) != 0 ||
       loaded_package == nullptr) {
        throw_state_error_message(
            "unable to read staged artifact metadata");
    }
    AlpmPackage package(loaded_package);
    const char* package_name = alpm_pkg_get_name(package.get());
    const char* full_version = alpm_pkg_get_version(package.get());
    const char* package_base = alpm_pkg_get_base(package.get());
    const char* architecture = alpm_pkg_get_arch(package.get());
    if(package_name == nullptr || full_version == nullptr ||
       package_base == nullptr || architecture == nullptr ||
       expected.package_name != package_name ||
       expected.full_version != full_version ||
       expected.package_base != package_base ||
       expected.architecture != architecture) {
        throw_state_error_message(
            "staged artifact metadata does not match the prepared identity");
    }
}

SourceArtifactInstallRootPrepareRequest validate_prepared_state(
    int transaction_fd, uid_t expected_owner,
    const std::string& transaction_token) {
    OwnedDescriptor prepared = open_private_file(
        transaction_fd, std::string(PREPARED_FILE), expected_owner,
        "prepared source-artifact state");
    const std::string prepared_protocol = read_bounded(
        prepared.get(), SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
        "prepared source-artifact state");
    const SourceArtifactInstallRootPreparedStateResult parsed =
        parse_source_artifact_install_root_prepared_state(
            prepared_protocol);
    const auto* request =
        std::get_if<SourceArtifactInstallRootPrepareRequest>(&parsed);
    if(request == nullptr ||
       request->transaction_token != transaction_token) {
        throw_state_error_message(
            "prepared source-artifact state is malformed or mismatched");
    }

    OwnedDescriptor hooks = open_directory_at(
        transaction_fd, std::string(HOOK_DIRECTORY), expected_owner,
        PRIVATE_DIRECTORY_MODE,
        "source-artifact transaction hook directory");
    const std::string hook_filename =
        source_artifact_install_hook_filename(transaction_token);
    require_exact_entries(
        hooks.get(), {hook_filename},
        "source-artifact transaction hook directory");
    OwnedDescriptor hook = open_private_file(
        hooks.get(), hook_filename, expected_owner,
        "source-artifact transaction hook");
    if(read_bounded(
           hook.get(), SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
           "source-artifact transaction hook") !=
       hook_contents(transaction_token)) {
        throw_state_error_message(
            "source-artifact transaction hook content changed");
    }

    OwnedDescriptor artifacts = open_directory_at(
        transaction_fd, std::string(ARTIFACT_DIRECTORY), expected_owner,
        PRIVATE_DIRECTORY_MODE,
        "source-artifact staging directory");
    require_exact_entries(
        artifacts.get(), expected_artifact_entries(*request),
        "source-artifact staging directory");
    for(std::size_t index = 0; index < request->artifacts.size(); ++index) {
        const auto& expected = request->artifacts[index];
        OwnedDescriptor artifact = open_private_file(
            artifacts.get(), staged_artifact_filename(index),
            expected_owner, "staged package artifact",
            expected.artifact_size);
        require_archive_identity(artifact.get(), expected);
        if(expected.signature_size > 0) {
            static_cast<void>(open_private_file(
                artifacts.get(), staged_signature_filename(index),
                expected_owner, "staged package signature",
                expected.signature_size));
        }
    }
    return *request;
}

void validate_optional_private_file(
    int transaction_fd, uid_t expected_owner,
    const std::string& name, const std::string& description) {
    if(!entry_exists(transaction_fd, name)) return;
    static_cast<void>(open_private_file(
        transaction_fd, name, expected_owner, description));
}

OpenTransaction retire_transaction(
    int active_fd, int used_fd, uid_t expected_owner,
    const std::string& transaction_token,
    OpenTransaction transaction) {
    require_entry_absent(
        used_fd, transaction_token,
        "used source-artifact transaction token");
    rename_noreplace(
        active_fd, transaction_token, used_fd, transaction_token,
        "used source-artifact transaction tombstone");
    synchronize_file(active_fd, "active source-artifact directory");
    synchronize_file(used_fd, "used source-artifact directory");

    OwnedDescriptor retired = open_directory_at(
        used_fd, transaction_token, expected_owner,
        PRIVATE_DIRECTORY_MODE,
        "used source-artifact transaction tombstone");
    const struct stat retired_metadata = require_descriptor_metadata(
        retired.get(), expected_owner, S_IFDIR, PRIVATE_DIRECTORY_MODE,
        "used source-artifact transaction tombstone");
    if(!same_identity(transaction.metadata, retired_metadata)) {
        throw_state_error_message(
            "retired source-artifact transaction identity changed");
    }
    return OpenTransaction{std::move(retired), retired_metadata};
}

void cleanup_retired_transaction(
    OpenTransaction& retired, uid_t expected_owner,
    const SourceArtifactInstallRootPrepareRequest& request,
    bool has_receipt, bool has_partial_receipt) {
    std::vector<std::string> expected{
        std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
        std::string(ARTIFACT_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    if(has_partial_receipt) expected.emplace_back(PARTIAL_RECEIPT_FILE);
    require_exact_entries(
        retired.descriptor.get(), expected,
        "retired source-artifact transaction");

    OwnedDescriptor hooks = open_directory_at(
        retired.descriptor.get(), std::string(HOOK_DIRECTORY),
        expected_owner, PRIVATE_DIRECTORY_MODE,
        "retired source-artifact hook directory");
    const std::string hook_filename =
        source_artifact_install_hook_filename(request.transaction_token);
    require_exact_entries(
        hooks.get(), {hook_filename},
        "retired source-artifact hook directory");
    static_cast<void>(open_private_file(
        hooks.get(), hook_filename, expected_owner,
        "retired source-artifact hook"));
    if(unlinkat(hooks.get(), hook_filename.c_str(), 0) == -1) {
        throw_state_error("unable to remove retired source-artifact hook");
    }
    if(unlinkat(
           retired.descriptor.get(),
           std::string(HOOK_DIRECTORY).c_str(), AT_REMOVEDIR) == -1) {
        throw_state_error(
            "unable to remove retired source-artifact hook directory");
    }

    OwnedDescriptor artifacts = open_directory_at(
        retired.descriptor.get(), std::string(ARTIFACT_DIRECTORY),
        expected_owner, PRIVATE_DIRECTORY_MODE,
        "retired source-artifact staging directory");
    require_exact_entries(
        artifacts.get(), expected_artifact_entries(request),
        "retired source-artifact staging directory");
    for(std::size_t index = 0; index < request.artifacts.size(); ++index) {
        const auto& expected_artifact = request.artifacts[index];
        const std::string artifact_name = staged_artifact_filename(index);
        static_cast<void>(open_private_file(
            artifacts.get(), artifact_name, expected_owner,
            "retired staged artifact", expected_artifact.artifact_size));
        if(unlinkat(artifacts.get(), artifact_name.c_str(), 0) == -1) {
            throw_state_error("unable to remove retired staged artifact");
        }
        if(expected_artifact.signature_size > 0) {
            const std::string signature_name =
                staged_signature_filename(index);
            static_cast<void>(open_private_file(
                artifacts.get(), signature_name, expected_owner,
                "retired staged signature",
                expected_artifact.signature_size));
            if(unlinkat(
                   artifacts.get(), signature_name.c_str(), 0) == -1) {
                throw_state_error(
                    "unable to remove retired staged signature");
            }
        }
    }
    require_exact_entries(
        artifacts.get(), {},
        "retired source-artifact staging directory");
    if(unlinkat(
           retired.descriptor.get(),
           std::string(ARTIFACT_DIRECTORY).c_str(),
           AT_REMOVEDIR) == -1) {
        throw_state_error(
            "unable to remove retired source-artifact staging directory");
    }

    static_cast<void>(open_private_file(
        retired.descriptor.get(), std::string(PREPARED_FILE),
        expected_owner, "retired prepared source-artifact state"));
    if(unlinkat(
           retired.descriptor.get(),
           std::string(PREPARED_FILE).c_str(), 0) == -1) {
        throw_state_error(
            "unable to remove retired prepared source-artifact state");
    }
    for(const std::string_view optional_file :
        {RECEIPT_FILE, PARTIAL_RECEIPT_FILE}) {
        const bool should_remove = optional_file == RECEIPT_FILE
                                       ? has_receipt
                                       : has_partial_receipt;
        if(!should_remove) continue;
        static_cast<void>(open_private_file(
            retired.descriptor.get(), std::string(optional_file),
            expected_owner, "retired source-artifact receipt"));
        if(unlinkat(
               retired.descriptor.get(),
               std::string(optional_file).c_str(), 0) == -1) {
            throw_state_error(
                "unable to remove retired source-artifact receipt");
        }
    }
    require_exact_entries(
        retired.descriptor.get(), {},
        "used source-artifact transaction tombstone");
    synchronize_file(
        retired.descriptor.get(),
        "used source-artifact transaction tombstone");
}

std::uint64_t require_sealed_input(
    int input_fd,
    const SourceArtifactInstallRootPrepareRequest& request) {
    if(input_fd < 0) {
        throw_state_error_message(
            "sealed source-artifact input descriptor is invalid");
    }
    struct stat metadata{};
    if(fstat(input_fd, &metadata) == -1 ||
       !S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
        throw_state_error_message(
            "source-artifact input is not a regular sealed file");
    }
    const int seals = fcntl(input_fd, F_GET_SEALS);
    constexpr int REQUIRED_SEALS =
        F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
    if(seals == -1 || (seals & REQUIRED_SEALS) != REQUIRED_SEALS) {
        throw_state_error_message(
            "source-artifact input is not write-sealed");
    }
    std::uint64_t expected_size = 0;
    for(const auto& artifact : request.artifacts) {
        expected_size += artifact.artifact_size;
        expected_size += artifact.signature_size;
    }
    if(static_cast<std::uint64_t>(metadata.st_size) != expected_size) {
        throw_state_error_message(
            "sealed source-artifact input has an unexpected size");
    }
    return expected_size;
}

} // namespace

struct SourceArtifactInstallTrustedStateStore::Implementation {
    uid_t expected_owner;
    OwnedDescriptor state_root;
    OwnedDescriptor active;
    OwnedDescriptor used;
};

SourceArtifactInstallTrustedStateStore::
    SourceArtifactInstallTrustedStateStore(
        std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {
}

SourceArtifactInstallTrustedStateStore
SourceArtifactInstallTrustedStateStore::open_below_runtime_parent(
    int runtime_parent_fd, uid_t expected_owner) {
    if(runtime_parent_fd < 0) {
        throw_state_error_message("runtime parent descriptor is invalid");
    }
    static_cast<void>(require_descriptor_metadata(
        runtime_parent_fd, expected_owner, S_IFDIR, std::nullopt,
        "runtime parent"));

    OwnedDescriptor moguet = ensure_private_directory(
        runtime_parent_fd, "moguet", expected_owner,
        "Moguet runtime directory");
    OwnedDescriptor state_root = ensure_private_directory(
        moguet.get(), "source-artifact-installs", expected_owner,
        "source-artifact install runtime directory");
    OwnedDescriptor active = ensure_private_directory(
        state_root.get(), "active", expected_owner,
        "active source-artifact directory");
    OwnedDescriptor used = ensure_private_directory(
        state_root.get(), "used", expected_owner,
        "used source-artifact directory");
    require_exact_entries(
        state_root.get(), {"active", "used"},
        "source-artifact install runtime directory");

    return SourceArtifactInstallTrustedStateStore(
        std::make_unique<Implementation>(Implementation{
            expected_owner, std::move(state_root), std::move(active),
            std::move(used)}));
}

SourceArtifactInstallTrustedStateStore::
    SourceArtifactInstallTrustedStateStore(
        SourceArtifactInstallTrustedStateStore&&) noexcept = default;

SourceArtifactInstallTrustedStateStore&
SourceArtifactInstallTrustedStateStore::operator=(
    SourceArtifactInstallTrustedStateStore&&) noexcept = default;

SourceArtifactInstallTrustedStateStore::~SourceArtifactInstallTrustedStateStore() =
    default;

SourceArtifactInstallRootPrepareResponse
SourceArtifactInstallTrustedStateStore::prepare(
    const SourceArtifactInstallRootPrepareRequest& request,
    int sealed_artifact_input_fd) {
    if(implementation_ == nullptr ||
       !is_valid_source_artifact_install_root_request(request)) {
        throw_state_error_message(
            "source-artifact prepare request is invalid");
    }
    static_cast<void>(require_sealed_input(
        sealed_artifact_input_fd, request));
    const std::string prepared_protocol =
        serialize_source_artifact_install_root_prepared_state(request);
    Implementation& state = *implementation_;
    require_entry_absent(
        state.used.get(), request.transaction_token,
        "used source-artifact transaction token");
    require_entry_absent(
        state.active.get(), request.transaction_token,
        "active source-artifact transaction token");
    const std::string staging_name =
        preparing_name(request.transaction_token);
    require_entry_absent(
        state.active.get(), staging_name,
        "preparing source-artifact transaction token");

    if(mkdirat(
           state.active.get(), staging_name.c_str(),
           PRIVATE_DIRECTORY_MODE) == -1) {
        throw_state_error(
            "unable to create preparing source-artifact state");
    }
    bool published = false;
    try {
        OwnedDescriptor staging = open_directory_at(
            state.active.get(), staging_name, state.expected_owner,
            PRIVATE_DIRECTORY_MODE,
            "preparing source-artifact state");
        OwnedDescriptor prepared = create_private_file(
            staging.get(), std::string(PREPARED_FILE),
            state.expected_owner,
            "prepared source-artifact state");
        write_all(prepared.get(), prepared_protocol);
        synchronize_file(prepared.get(), "prepared source-artifact state");

        OwnedDescriptor hooks = ensure_private_directory(
            staging.get(), std::string(HOOK_DIRECTORY),
            state.expected_owner,
            "source-artifact transaction hook directory");
        const std::string hook_filename =
            source_artifact_install_hook_filename(
                request.transaction_token);
        OwnedDescriptor hook = create_private_file(
            hooks.get(), hook_filename, state.expected_owner,
            "source-artifact transaction hook");
        write_all(hook.get(), hook_contents(request.transaction_token));
        synchronize_file(hook.get(), "source-artifact transaction hook");
        synchronize_file(
            hooks.get(), "source-artifact transaction hook directory");

        OwnedDescriptor artifacts = ensure_private_directory(
            staging.get(), std::string(ARTIFACT_DIRECTORY),
            state.expected_owner,
            "source-artifact staging directory");
        std::uint64_t input_offset = 0;
        for(std::size_t index = 0; index < request.artifacts.size(); ++index) {
            const auto& expected = request.artifacts[index];
            OwnedDescriptor artifact = create_private_file(
                artifacts.get(), staged_artifact_filename(index),
                state.expected_owner, "staged package artifact");
            copy_exact_range(
                sealed_artifact_input_fd, input_offset, artifact.get(),
                expected.artifact_size);
            input_offset += expected.artifact_size;
            synchronize_file(artifact.get(), "staged package artifact");

            if(expected.signature_size > 0) {
                OwnedDescriptor signature = create_private_file(
                    artifacts.get(), staged_signature_filename(index),
                    state.expected_owner, "staged package signature");
                copy_exact_range(
                    sealed_artifact_input_fd, input_offset,
                    signature.get(), expected.signature_size);
                input_offset += expected.signature_size;
                synchronize_file(
                    signature.get(), "staged package signature");
            }
        }
        require_exact_entries(
            artifacts.get(), expected_artifact_entries(request),
            "source-artifact staging directory");
        synchronize_file(artifacts.get(), "source-artifact staging directory");

        // Reopen every root-owned staged file and query the same descriptor
        // with libalpm before publication. Package names, versions,
        // PackageBase, and architecture are never filled from upper context.
        for(std::size_t index = 0; index < request.artifacts.size(); ++index) {
            const auto& expected = request.artifacts[index];
            OwnedDescriptor artifact = open_private_file(
                artifacts.get(), staged_artifact_filename(index),
                state.expected_owner, "staged package artifact",
                expected.artifact_size);
            require_archive_identity(artifact.get(), expected);
        }

        require_exact_entries(
            staging.get(),
            {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
             std::string(ARTIFACT_DIRECTORY)},
            "preparing source-artifact state");
        synchronize_file(staging.get(), "preparing source-artifact state");

        rename_noreplace(
            state.active.get(), staging_name, state.active.get(),
            request.transaction_token,
            "active source-artifact transaction");
        published = true;
        synchronize_file(state.active.get(), "active source-artifact directory");

        OpenTransaction transaction = open_transaction(
            state.active.get(), state.expected_owner,
            request.transaction_token);
        const SourceArtifactInstallRootPrepareRequest validated =
            validate_prepared_state(
                transaction.descriptor.get(), state.expected_owner,
                request.transaction_token);
        if(validated != request) {
            throw_state_error_message(
                "published source-artifact state changed identity");
        }

        SourceArtifactInstallRootPrepareResponse response{
            request.transaction_token,
            source_artifact_install_hook_directory(
                request.transaction_token),
            {}};
        response.artifacts.reserve(request.artifacts.size());
        for(std::size_t index = 0; index < request.artifacts.size(); ++index) {
            response.artifacts.push_back(
                SourceArtifactInstallStagedArtifact{
                    request.artifacts[index].artifact_index,
                    source_artifact_install_staged_artifact_path(
                        request.transaction_token, index)});
        }
        return response;
    } catch(...) {
        if(published) {
            try {
                OpenTransaction transaction = open_transaction(
                    state.active.get(), state.expected_owner,
                    request.transaction_token);
                const SourceArtifactInstallRootPrepareRequest validated =
                    validate_prepared_state(
                        transaction.descriptor.get(), state.expected_owner,
                        request.transaction_token);
                OpenTransaction retired = retire_transaction(
                    state.active.get(), state.used.get(),
                    state.expected_owner, request.transaction_token,
                    std::move(transaction));
                cleanup_retired_transaction(
                    retired, state.expected_owner, validated, false,
                    false);
            } catch(...) {
                throw_state_error_message(
                    "source-artifact prepare failed and exact published-state cleanup failed");
            }
        } else if(!cleanup_preparing_directory(
                      state.active.get(), staging_name, request)) {
            throw_state_error_message(
                "source-artifact prepare failed and exact staging cleanup failed");
        }
        throw;
    }
}

void SourceArtifactInstallTrustedStateStore::record(
    const std::string& transaction_token,
    int needs_targets_input_fd) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token) ||
       needs_targets_input_fd < 0) {
        throw_state_error_message("source-artifact record request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
        state.active.get(), state.expected_owner, transaction_token);
    static_cast<void>(validate_prepared_state(
        transaction.descriptor.get(), state.expected_owner,
        transaction_token));
    require_exact_entries(
        transaction.descriptor.get(),
        {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
         std::string(ARTIFACT_DIRECTORY)},
        "recordable source-artifact transaction");

    const std::string input = read_bounded(
        needs_targets_input_fd,
        SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
        "source-artifact ALPM NeedsTargets input");
    const TrustedAlpmReceiptNeedsTargetsResult parsed =
        parse_trusted_alpm_receipt_needs_targets(input);
    const auto* packages =
        std::get_if<std::vector<std::string>>(&parsed);
    if(packages == nullptr) {
        throw_state_error_message(
            "source-artifact ALPM NeedsTargets input is invalid");
    }
    const std::string receipt_protocol =
        serialize_source_artifact_install_root_receipt(
            SourceArtifactInstallRootReceipt{
                SourceArtifactInstallRootReceiptState::Complete,
                transaction_token, *packages});

    OwnedDescriptor partial = create_private_file(
        transaction.descriptor.get(),
        std::string(PARTIAL_RECEIPT_FILE), state.expected_owner,
        "partial source-artifact receipt");
    write_all(partial.get(), receipt_protocol);
    synchronize_file(partial.get(), "partial source-artifact receipt");
    require_named_identity(
        state.active.get(), transaction_token, transaction.metadata,
        "active source-artifact transaction");
    rename_noreplace(
        transaction.descriptor.get(),
        std::string(PARTIAL_RECEIPT_FILE),
        transaction.descriptor.get(), std::string(RECEIPT_FILE),
        "complete source-artifact receipt");
    synchronize_file(
        transaction.descriptor.get(),
        "active source-artifact transaction");
    require_exact_entries(
        transaction.descriptor.get(),
        {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
         std::string(ARTIFACT_DIRECTORY), std::string(RECEIPT_FILE)},
        "recorded source-artifact transaction");
}

std::string SourceArtifactInstallTrustedStateStore::consume(
    const std::string& transaction_token) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw_state_error_message("source-artifact consume request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
        state.active.get(), state.expected_owner, transaction_token);
    const SourceArtifactInstallRootPrepareRequest request =
        validate_prepared_state(
            transaction.descriptor.get(), state.expected_owner,
            transaction_token);

    const bool has_receipt = entry_exists(
        transaction.descriptor.get(), std::string(RECEIPT_FILE));
    if(entry_exists(
           transaction.descriptor.get(),
           std::string(PARTIAL_RECEIPT_FILE))) {
        throw_state_error_message(
            "partial source-artifact receipt cannot be consumed");
    }
    std::vector<std::string> expected{
        std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
        std::string(ARTIFACT_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    require_exact_entries(
        transaction.descriptor.get(), expected,
        "consumable source-artifact transaction");

    SourceArtifactInstallRootReceipt receipt{
        SourceArtifactInstallRootReceiptState::Missing,
        transaction_token,
        {}};
    if(has_receipt) {
        OwnedDescriptor receipt_file = open_private_file(
            transaction.descriptor.get(), std::string(RECEIPT_FILE),
            state.expected_owner, "complete source-artifact receipt");
        const std::string protocol = read_bounded(
            receipt_file.get(),
            SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
            "complete source-artifact receipt");
        const SourceArtifactInstallRootReceiptResult parsed =
            parse_source_artifact_install_root_receipt(protocol);
        const auto* complete =
            std::get_if<SourceArtifactInstallRootReceipt>(&parsed);
        if(complete == nullptr ||
           complete->state !=
               SourceArtifactInstallRootReceiptState::Complete ||
           complete->transaction_token != transaction_token) {
            throw_state_error_message(
                "complete source-artifact receipt is malformed or mismatched");
        }
        receipt = *complete;
    }
    require_named_identity(
        state.active.get(), transaction_token, transaction.metadata,
        "active source-artifact transaction");
    OpenTransaction retired = retire_transaction(
        state.active.get(), state.used.get(), state.expected_owner,
        transaction_token, std::move(transaction));
    cleanup_retired_transaction(
        retired, state.expected_owner, request, has_receipt, false);
    return serialize_source_artifact_install_root_receipt(receipt);
}

void SourceArtifactInstallTrustedStateStore::abort(
    const std::string& transaction_token) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw_state_error_message("source-artifact abort request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
        state.active.get(), state.expected_owner, transaction_token);
    const SourceArtifactInstallRootPrepareRequest request =
        validate_prepared_state(
            transaction.descriptor.get(), state.expected_owner,
            transaction_token);
    const bool has_receipt = entry_exists(
        transaction.descriptor.get(), std::string(RECEIPT_FILE));
    const bool has_partial_receipt = entry_exists(
        transaction.descriptor.get(),
        std::string(PARTIAL_RECEIPT_FILE));
    if(has_receipt && has_partial_receipt) {
        throw_state_error_message(
            "complete and partial source-artifact receipts coexist");
    }
    validate_optional_private_file(
        transaction.descriptor.get(), state.expected_owner,
        std::string(RECEIPT_FILE),
        "abortable complete source-artifact receipt");
    validate_optional_private_file(
        transaction.descriptor.get(), state.expected_owner,
        std::string(PARTIAL_RECEIPT_FILE),
        "abortable partial source-artifact receipt");
    std::vector<std::string> expected{
        std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
        std::string(ARTIFACT_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    if(has_partial_receipt) expected.emplace_back(PARTIAL_RECEIPT_FILE);
    require_exact_entries(
        transaction.descriptor.get(), expected,
        "abortable source-artifact transaction");
    require_named_identity(
        state.active.get(), transaction_token, transaction.metadata,
        "active source-artifact transaction");
    OpenTransaction retired = retire_transaction(
        state.active.get(), state.used.get(), state.expected_owner,
        transaction_token, std::move(transaction));
    cleanup_retired_transaction(
        retired, state.expected_owner, request, has_receipt,
        has_partial_receipt);
}
