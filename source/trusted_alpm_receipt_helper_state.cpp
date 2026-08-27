#include "trusted_alpm_receipt_helper_state.hpp"

#include "trusted_alpm_receipt_protocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <optional>
#include <set>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include <linux/fs.h>

#ifndef MOGUET_ALPM_RECEIPT_HELPER_PATH
#error "MOGUET_ALPM_RECEIPT_HELPER_PATH is required"
#endif

namespace {

constexpr mode_t PRIVATE_DIRECTORY_MODE = 0700;
constexpr mode_t PRIVATE_FILE_MODE = 0600;
constexpr std::string_view PREPARED_FILE = "prepared";
constexpr std::string_view HOOK_DIRECTORY = "hooks";
constexpr std::string_view RECEIPT_FILE = "receipt";
constexpr std::string_view PARTIAL_RECEIPT_FILE = "receipt.partial";

class OwnedDescriptor final {
public:
    OwnedDescriptor() noexcept = default;
    explicit OwnedDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {}
    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}
    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        reset();
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }
    ~OwnedDescriptor() { reset(); }

    [[nodiscard]] int get() const noexcept { return descriptor_; }
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
    throw TrustedAlpmReceiptStateError(
            action + ": " + std::strerror(error_number));
}

[[noreturn]] void throw_state_error_message(const std::string& message) {
    throw TrustedAlpmReceiptStateError(message);
}

bool same_identity(const struct stat& lhs, const struct stat& rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_mode == rhs.st_mode && lhs.st_uid == rhs.st_uid;
}

struct stat require_descriptor_metadata(
        int descriptor, uid_t expected_owner, mode_t expected_type,
        std::optional<mode_t> exact_permissions,
        const std::string& description) {
    struct stat metadata {};
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
    struct stat named_metadata {};
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
    struct stat metadata {};
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
    std::string            bytes;
    std::array<char, 4096> buffer {};
    while(true) {
        const ssize_t count = read(descriptor, buffer.data(), buffer.size());
        if(count > 0) {
            const std::size_t count_size = static_cast<std::size_t>(count);
            if(count_size > maximum_bytes -
                                    std::min(maximum_bytes, bytes.size())) {
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
            "renameat2 is unavailable for trusted ALPM receipt publication");
#endif
}

std::string preparing_name(const std::string& transaction_token) {
    return "." + transaction_token + ".preparing";
}

std::string hook_contents(const std::string& transaction_token) {
    // Exec has no shell and contains only a configure-time absolute trusted
    // helper path plus the validated token and fixed owner value.
    return "[Trigger]\n"
           "Operation = Install\n"
           "Type = Package\n"
           "Target = *\n"
           "\n"
           "[Action]\n"
           "Description = Record Moguet transaction-local package installs\n"
           "When = PostTransaction\n"
           "Exec = " MOGUET_ALPM_RECEIPT_HELPER_PATH " record " +
           transaction_token + " " +
           std::string(
                   trusted_alpm_receipt_selected_repository_provider_owner()) +
           "\nNeedsTargets\n";
}

bool unlink_if_present(int parent_fd, const std::string& name, int flags = 0) {
    if(unlinkat(parent_fd, name.c_str(), flags) == 0) return true;
    return errno == ENOENT;
}

bool cleanup_preparing_directory(
        int active_fd, const std::string& staging_name,
        const std::string& transaction_token) noexcept {
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
                    hooks.get(),
                    trusted_alpm_receipt_hook_filename(transaction_token)));
        }
        static_cast<void>(unlink_if_present(
                staging.get(), std::string(HOOK_DIRECTORY), AT_REMOVEDIR));
        static_cast<void>(
                unlink_if_present(staging.get(), std::string(PREPARED_FILE)));
        return unlink_if_present(active_fd, staging_name, AT_REMOVEDIR);
    } catch(...) {
        return false;
    }
}

struct OpenTransaction {
    OwnedDescriptor descriptor;
    struct stat     metadata {};
};

OpenTransaction open_transaction(
        int active_fd, uid_t expected_owner,
        const std::string& transaction_token) {
    OwnedDescriptor descriptor = open_directory_at(
            active_fd, transaction_token, expected_owner,
            PRIVATE_DIRECTORY_MODE, "active ALPM receipt transaction");
    const struct stat metadata = require_descriptor_metadata(
            descriptor.get(), expected_owner, S_IFDIR,
            PRIVATE_DIRECTORY_MODE, "active ALPM receipt transaction");
    return OpenTransaction{std::move(descriptor), metadata};
}

TrustedAlpmReceiptPreparedState validate_prepared_state(
        int transaction_fd, uid_t expected_owner,
        const std::string& transaction_token) {
    OwnedDescriptor prepared = open_private_file(
            transaction_fd, std::string(PREPARED_FILE), expected_owner,
            "prepared ALPM receipt state");
    const std::string prepared_protocol = read_bounded(
            prepared.get(), TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES,
            "prepared ALPM receipt state");
    const TrustedAlpmReceiptPreparedStateResult parsed =
            parse_trusted_alpm_receipt_prepared_state(prepared_protocol);
    const auto* state =
            std::get_if<TrustedAlpmReceiptPreparedState>(&parsed);
    if(state == nullptr || state->transaction_token != transaction_token) {
        throw_state_error_message(
                "prepared ALPM receipt state is malformed or mismatched");
    }

    OwnedDescriptor hooks = open_directory_at(
            transaction_fd, std::string(HOOK_DIRECTORY), expected_owner,
            PRIVATE_DIRECTORY_MODE, "transaction-local ALPM hook directory");
    const std::string hook_filename =
            trusted_alpm_receipt_hook_filename(transaction_token);
    require_exact_entries(
            hooks.get(), {hook_filename},
            "transaction-local ALPM hook directory");
    OwnedDescriptor hook = open_private_file(
            hooks.get(), hook_filename, expected_owner,
            "transaction-local ALPM hook");
    const std::string observed_hook = read_bounded(
            hook.get(), TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES,
            "transaction-local ALPM hook");
    if(observed_hook != hook_contents(transaction_token)) {
        throw_state_error_message(
                "transaction-local ALPM hook content changed");
    }
    return *state;
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
            used_fd, transaction_token, "used transaction token");
    rename_noreplace(
            active_fd, transaction_token, used_fd, transaction_token,
            "used transaction tombstone");
    synchronize_file(active_fd, "active receipt directory");
    synchronize_file(used_fd, "used receipt directory");

    OwnedDescriptor retired = open_directory_at(
            used_fd, transaction_token, expected_owner,
            PRIVATE_DIRECTORY_MODE, "used transaction tombstone");
    const struct stat retired_metadata = require_descriptor_metadata(
            retired.get(), expected_owner, S_IFDIR, PRIVATE_DIRECTORY_MODE,
            "used transaction tombstone");
    if(!same_identity(transaction.metadata, retired_metadata)) {
        throw_state_error_message(
                "retired transaction identity changed during publication");
    }
    return OpenTransaction{std::move(retired), retired_metadata};
}

void cleanup_retired_transaction(
        OpenTransaction& retired, uid_t expected_owner,
        const std::string& transaction_token,
        bool has_receipt, bool has_partial_receipt) {
    std::vector<std::string> expected{
            std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    if(has_partial_receipt) expected.emplace_back(PARTIAL_RECEIPT_FILE);
    require_exact_entries(
            retired.descriptor.get(), expected,
            "retired ALPM receipt transaction");

    OwnedDescriptor hooks = open_directory_at(
            retired.descriptor.get(), std::string(HOOK_DIRECTORY),
            expected_owner, PRIVATE_DIRECTORY_MODE,
            "retired transaction-local ALPM hook directory");
    const std::string hook_filename =
            trusted_alpm_receipt_hook_filename(transaction_token);
    require_exact_entries(
            hooks.get(), {hook_filename},
            "retired transaction-local ALPM hook directory");
    static_cast<void>(open_private_file(
            hooks.get(), hook_filename, expected_owner,
            "retired transaction-local ALPM hook"));
    if(unlinkat(hooks.get(), hook_filename.c_str(), 0) == -1) {
        throw_state_error("unable to remove retired ALPM hook");
    }
    if(unlinkat(
               retired.descriptor.get(),
               std::string(HOOK_DIRECTORY).c_str(), AT_REMOVEDIR) == -1) {
        throw_state_error("unable to remove retired ALPM hook directory");
    }

    static_cast<void>(open_private_file(
            retired.descriptor.get(), std::string(PREPARED_FILE),
            expected_owner, "retired prepared receipt state"));
    if(unlinkat(
               retired.descriptor.get(),
               std::string(PREPARED_FILE).c_str(), 0) == -1) {
        throw_state_error("unable to remove retired prepared receipt state");
    }
    for(const std::string_view optional_file :
        {RECEIPT_FILE, PARTIAL_RECEIPT_FILE}) {
        const bool should_remove = optional_file == RECEIPT_FILE
                ? has_receipt
                : has_partial_receipt;
        if(!should_remove) continue;
        static_cast<void>(open_private_file(
                retired.descriptor.get(), std::string(optional_file),
                expected_owner, "retired receipt state"));
        if(unlinkat(
                   retired.descriptor.get(),
                   std::string(optional_file).c_str(), 0) == -1) {
            throw_state_error("unable to remove retired receipt state");
        }
    }
    require_exact_entries(
            retired.descriptor.get(), {}, "used transaction tombstone");
    synchronize_file(
            retired.descriptor.get(), "used transaction tombstone");
}

} // namespace

struct TrustedAlpmReceiptStateStore::Implementation {
    uid_t           expected_owner;
    OwnedDescriptor receipt_root;
    OwnedDescriptor active;
    OwnedDescriptor used;
};

TrustedAlpmReceiptStateStore::TrustedAlpmReceiptStateStore(
        std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}

TrustedAlpmReceiptStateStore
TrustedAlpmReceiptStateStore::open_below_runtime_parent(
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
    OwnedDescriptor receipt_root = ensure_private_directory(
            moguet.get(), "alpm-receipts", expected_owner,
            "ALPM receipt runtime directory");
    OwnedDescriptor active = ensure_private_directory(
            receipt_root.get(), "active", expected_owner,
            "active ALPM receipt directory");
    OwnedDescriptor used = ensure_private_directory(
            receipt_root.get(), "used", expected_owner,
            "used ALPM receipt directory");
    require_exact_entries(
            receipt_root.get(), {"active", "used"},
            "ALPM receipt runtime directory");

    return TrustedAlpmReceiptStateStore(std::make_unique<Implementation>(
            Implementation{
                    expected_owner, std::move(receipt_root),
                    std::move(active), std::move(used)}));
}

TrustedAlpmReceiptStateStore::TrustedAlpmReceiptStateStore(
        TrustedAlpmReceiptStateStore&&) noexcept = default;

TrustedAlpmReceiptStateStore& TrustedAlpmReceiptStateStore::operator=(
        TrustedAlpmReceiptStateStore&&) noexcept = default;

TrustedAlpmReceiptStateStore::~TrustedAlpmReceiptStateStore() = default;

void TrustedAlpmReceiptStateStore::prepare(
        const std::string& transaction_token,
        const std::vector<std::string>& requested_package_names) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw_state_error_message("transaction token is invalid");
    }
    const std::string prepared_protocol =
            serialize_trusted_alpm_receipt_prepared_state(
                    TrustedAlpmReceiptPreparedState{
                            transaction_token, requested_package_names});
    Implementation& state = *implementation_;
    require_entry_absent(
            state.used.get(), transaction_token,
            "used transaction token");
    require_entry_absent(
            state.active.get(), transaction_token,
            "active transaction token");
    const std::string staging_name = preparing_name(transaction_token);
    require_entry_absent(
            state.active.get(), staging_name,
            "preparing transaction token");

    if(mkdirat(
               state.active.get(), staging_name.c_str(),
               PRIVATE_DIRECTORY_MODE) == -1) {
        throw_state_error("unable to create preparing transaction state");
    }
    bool published = false;
    try {
        OwnedDescriptor staging = open_directory_at(
                state.active.get(), staging_name, state.expected_owner,
                PRIVATE_DIRECTORY_MODE, "preparing transaction state");
        OwnedDescriptor prepared = create_private_file(
                staging.get(), std::string(PREPARED_FILE),
                state.expected_owner, "prepared transaction state");
        write_all(prepared.get(), prepared_protocol);
        synchronize_file(prepared.get(), "prepared transaction state");

        OwnedDescriptor hooks = ensure_private_directory(
                staging.get(), std::string(HOOK_DIRECTORY),
                state.expected_owner, "transaction-local hook directory");
        const std::string hook_filename =
                trusted_alpm_receipt_hook_filename(transaction_token);
        OwnedDescriptor hook = create_private_file(
                hooks.get(), hook_filename, state.expected_owner,
                "transaction-local ALPM hook");
        write_all(hook.get(), hook_contents(transaction_token));
        synchronize_file(hook.get(), "transaction-local ALPM hook");
        synchronize_file(hooks.get(), "transaction-local hook directory");
        require_exact_entries(
                staging.get(),
                {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY)},
                "preparing transaction state");
        synchronize_file(staging.get(), "preparing transaction state");

        rename_noreplace(
                state.active.get(), staging_name, state.active.get(),
                transaction_token, "active ALPM receipt transaction");
        published = true;
        synchronize_file(state.active.get(), "active receipt directory");
    } catch(...) {
        if(published) {
            try {
                OpenTransaction transaction = open_transaction(
                        state.active.get(), state.expected_owner,
                        transaction_token);
                static_cast<void>(validate_prepared_state(
                        transaction.descriptor.get(), state.expected_owner,
                        transaction_token));
                OpenTransaction retired = retire_transaction(
                        state.active.get(), state.used.get(),
                        state.expected_owner, transaction_token,
                        std::move(transaction));
                cleanup_retired_transaction(
                        retired, state.expected_owner, transaction_token,
                        false, false);
            } catch(...) {
                throw_state_error_message(
                        "prepare failed and exact published-state cleanup failed");
            }
        } else if(!cleanup_preparing_directory(
                          state.active.get(), staging_name,
                          transaction_token)) {
            throw_state_error_message(
                    "prepare failed and exact staging cleanup failed");
        }
        throw;
    }
}

void TrustedAlpmReceiptStateStore::record(
        const std::string& transaction_token,
        int needs_targets_input_fd) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token) ||
       needs_targets_input_fd < 0) {
        throw_state_error_message("record request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
            state.active.get(), state.expected_owner, transaction_token);
    static_cast<void>(validate_prepared_state(
            transaction.descriptor.get(), state.expected_owner,
            transaction_token));
    require_exact_entries(
            transaction.descriptor.get(),
            {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY)},
            "recordable ALPM receipt transaction");

    const std::string input = read_bounded(
            needs_targets_input_fd, TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES,
            "ALPM NeedsTargets input");
    const TrustedAlpmReceiptNeedsTargetsResult parsed =
            parse_trusted_alpm_receipt_needs_targets(input);
    const auto* packages = std::get_if<std::vector<std::string>>(&parsed);
    if(packages == nullptr) {
        throw_state_error_message("ALPM NeedsTargets input is invalid");
    }
    const std::string receipt_protocol =
            serialize_trusted_alpm_receipt_machine_receipt(
                    TrustedAlpmReceiptMachineReceipt{
                            TrustedAlpmReceiptMachineState::Complete,
                            transaction_token, *packages});

    OwnedDescriptor partial = create_private_file(
            transaction.descriptor.get(),
            std::string(PARTIAL_RECEIPT_FILE), state.expected_owner,
            "partial ALPM receipt");
    write_all(partial.get(), receipt_protocol);
    synchronize_file(partial.get(), "partial ALPM receipt");
    require_named_identity(
            state.active.get(), transaction_token, transaction.metadata,
            "active ALPM receipt transaction");
    rename_noreplace(
            transaction.descriptor.get(),
            std::string(PARTIAL_RECEIPT_FILE),
            transaction.descriptor.get(), std::string(RECEIPT_FILE),
            "complete ALPM receipt");
    synchronize_file(
            transaction.descriptor.get(), "active ALPM receipt transaction");
    require_exact_entries(
            transaction.descriptor.get(),
            {std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY),
             std::string(RECEIPT_FILE)},
            "recorded ALPM receipt transaction");
}

std::string TrustedAlpmReceiptStateStore::consume(
        const std::string& transaction_token) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw_state_error_message("consume request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
            state.active.get(), state.expected_owner, transaction_token);
    static_cast<void>(validate_prepared_state(
            transaction.descriptor.get(), state.expected_owner,
            transaction_token));

    const bool has_receipt = entry_exists(
            transaction.descriptor.get(), std::string(RECEIPT_FILE));
    if(entry_exists(
               transaction.descriptor.get(),
               std::string(PARTIAL_RECEIPT_FILE))) {
        throw_state_error_message(
                "partial ALPM receipt cannot be consumed");
    }
    std::vector<std::string> expected{
            std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    require_exact_entries(
            transaction.descriptor.get(), expected,
            "consumable ALPM receipt transaction");

    TrustedAlpmReceiptMachineReceipt receipt{
            TrustedAlpmReceiptMachineState::Missing, transaction_token, {}};
    if(has_receipt) {
        OwnedDescriptor receipt_file = open_private_file(
                transaction.descriptor.get(), std::string(RECEIPT_FILE),
                state.expected_owner, "complete ALPM receipt");
        const std::string protocol = read_bounded(
                receipt_file.get(), TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES,
                "complete ALPM receipt");
        const TrustedAlpmReceiptMachineReceiptResult parsed =
                parse_trusted_alpm_receipt_machine_receipt(protocol);
        const auto* complete =
                std::get_if<TrustedAlpmReceiptMachineReceipt>(&parsed);
        if(complete == nullptr ||
           complete->state != TrustedAlpmReceiptMachineState::Complete ||
           complete->transaction_token != transaction_token) {
            throw_state_error_message(
                    "complete ALPM receipt is malformed or mismatched");
        }
        receipt = *complete;
    }
    require_named_identity(
            state.active.get(), transaction_token, transaction.metadata,
            "active ALPM receipt transaction");
    OpenTransaction retired = retire_transaction(
            state.active.get(), state.used.get(), state.expected_owner,
            transaction_token, std::move(transaction));
    cleanup_retired_transaction(
            retired, state.expected_owner, transaction_token, has_receipt,
            false);
    return serialize_trusted_alpm_receipt_machine_receipt(receipt);
}

void TrustedAlpmReceiptStateStore::abort(
        const std::string& transaction_token) {
    if(implementation_ == nullptr ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw_state_error_message("abort request is invalid");
    }
    Implementation& state = *implementation_;
    OpenTransaction transaction = open_transaction(
            state.active.get(), state.expected_owner, transaction_token);
    static_cast<void>(validate_prepared_state(
            transaction.descriptor.get(), state.expected_owner,
            transaction_token));
    const bool has_receipt = entry_exists(
            transaction.descriptor.get(), std::string(RECEIPT_FILE));
    const bool has_partial_receipt = entry_exists(
            transaction.descriptor.get(),
            std::string(PARTIAL_RECEIPT_FILE));
    if(has_receipt && has_partial_receipt) {
        throw_state_error_message(
                "complete and partial receipts coexist unexpectedly");
    }
    validate_optional_private_file(
            transaction.descriptor.get(), state.expected_owner,
            std::string(RECEIPT_FILE), "abortable complete receipt");
    validate_optional_private_file(
            transaction.descriptor.get(), state.expected_owner,
            std::string(PARTIAL_RECEIPT_FILE), "abortable partial receipt");
    std::vector<std::string> expected{
            std::string(PREPARED_FILE), std::string(HOOK_DIRECTORY)};
    if(has_receipt) expected.emplace_back(RECEIPT_FILE);
    if(has_partial_receipt) expected.emplace_back(PARTIAL_RECEIPT_FILE);
    require_exact_entries(
            transaction.descriptor.get(), expected,
            "abortable ALPM receipt transaction");
    require_named_identity(
            state.active.get(), transaction_token, transaction.metadata,
            "active ALPM receipt transaction");
    OpenTransaction retired = retire_transaction(
            state.active.get(), state.used.get(), state.expected_owner,
            transaction_token, std::move(transaction));
    cleanup_retired_transaction(
            retired, state.expected_owner, transaction_token, has_receipt,
            has_partial_receipt);
}
