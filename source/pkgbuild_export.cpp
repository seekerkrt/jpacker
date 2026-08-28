#include "pkgbuild_export.hpp"

#include "aur_rpc.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "trusted_git.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fcntl.h>
#include <linux/fs.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

// NO_TRANSLATE: Protocol endpoint identity.
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";

// export lifecycleをpersistent source checkoutから独立させるためのlocal security guard群。
// POLICY(#196): path / URL validation policyはgeneric helperへ持ち上げず、owner内に保持する。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool remote_url_matches_expected(
    const std::string& current_url, const std::string& expected_url) {
    return trim(current_url) == trim(expected_url);
}

bool is_path_contained(const fs::path& root, const fs::path& candidate, bool allow_root) {
    auto root_component = root.begin();
    auto candidate_component = candidate.begin();
    for(; root_component != root.end(); ++root_component, ++candidate_component) {
        if(candidate_component == candidate.end() || *root_component != *candidate_component) return false;
    }
    return allow_root || candidate_component != candidate.end();
}

bool is_valid_aur_export_identifier(const std::string& name) {
    // POLICY(#167): package identifierであっても、filesystemのdot componentはexport名にしない。
    return name != "." && name != ".." && is_valid_package_name(name);
}

void require_valid_aur_export_target(
    const std::string& target, const std::string& operation) {
    if(target.find('/') != std::string::npos || !is_valid_aur_export_identifier(target)) {
        // POLICY(#196): validation ownerをmoduleへ移しても既存CLI diagnosticは変えない。
        // TRANSLATORS: The placeholders are the literal AUR identity, a literal CLI operation, and an AUR package target.
        throw std::runtime_error(localization::format_translated_message(
            "Invalid {} target for operation {}: {}",
            "AUR", operation, target));
    }
}

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}

// requested AUR packageと、export対象になるPackageBase repositoryを結びつける。
struct AurExportSource {
    std::string requested_name;
    std::string package_base;
    std::string git_url;
};

class OwnedFileDescriptor {
    int descriptor_ = -1;

public:
    explicit OwnedFileDescriptor(int descriptor) : descriptor_(descriptor) {
    }
    OwnedFileDescriptor(const OwnedFileDescriptor&) = delete;
    OwnedFileDescriptor& operator=(const OwnedFileDescriptor&) = delete;
    OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept
        : descriptor_(other.release()) {
    }
    OwnedFileDescriptor& operator=(OwnedFileDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) close(descriptor_);
        descriptor_ = other.release();
        return *this;
    }
    ~OwnedFileDescriptor() {
        if(descriptor_ >= 0) close(descriptor_);
    }
    int get() const {
        return descriptor_;
    }
    int release() {
        return std::exchange(descriptor_, -1);
    }
};

struct DirectoryIdentity {
    dev_t device;
    ino_t inode;
};

enum class DirectoryIdentityContext {
    ExportParent,
    TemporaryParent,
    TemporaryExport,
    ExportedGit,
};

DirectoryIdentity require_directory_identity(
    int descriptor,
    DirectoryIdentityContext context,
    const fs::path& display_path) {
    struct stat status{};
    if(fstat(descriptor, &status) != 0) {
        switch(context) {
            case DirectoryIdentityContext::ExportParent:
                // TRANSLATORS: The placeholders are an export parent path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed to inspect export directory {}: {}",
                    display_path.string(),
                    std::strerror(errno)));
            case DirectoryIdentityContext::TemporaryParent:
                // TRANSLATORS: The placeholders are a directory path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed to inspect temporary directory parent {}: {}",
                    display_path.string(),
                    std::strerror(errno)));
            case DirectoryIdentityContext::TemporaryExport:
                // TRANSLATORS: The placeholders are a directory path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed to inspect temporary export directory {}: {}",
                    display_path.string(),
                    std::strerror(errno)));
            case DirectoryIdentityContext::ExportedGit:
                // TRANSLATORS: The placeholders are the literal .git name, its directory path, and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed to inspect exported {} directory {}: {}",
                    ".git", display_path.string(),
                    std::strerror(errno)));
        }
        throw std::logic_error(localization::translate_message(
            "Unknown export directory identity context."));
    }
    if(!S_ISDIR(status.st_mode)) {
        switch(context) {
            case DirectoryIdentityContext::ExportParent:
                // TRANSLATORS: The placeholder is an export parent path.
                throw std::runtime_error(localization::format_translated_message(
                    "Export path {} is not a directory.",
                    display_path.string()));
            case DirectoryIdentityContext::TemporaryParent:
                // TRANSLATORS: The placeholder is a directory path.
                throw std::runtime_error(localization::format_translated_message(
                    "Temporary directory parent {} is not a directory.",
                    display_path.string()));
            case DirectoryIdentityContext::TemporaryExport:
                // TRANSLATORS: The placeholder is a directory path.
                throw std::runtime_error(localization::format_translated_message(
                    "Temporary export path {} is not a directory.",
                    display_path.string()));
            case DirectoryIdentityContext::ExportedGit:
                // TRANSLATORS: The placeholders are the literal .git name and its path.
                throw std::runtime_error(localization::format_translated_message(
                    "Exported {} path {} is not a directory.",
                    ".git", display_path.string()));
        }
        throw std::logic_error(localization::translate_message(
            "Unknown export directory identity context."));
    }
    return DirectoryIdentity{status.st_dev, status.st_ino};
}

bool directory_identity_matches(
    const DirectoryIdentity& expected, const struct stat& actual) {
    return S_ISDIR(actual.st_mode) && expected.device == actual.st_dev &&
           expected.inode == actual.st_ino;
}

bool filesystem_identity_matches(
    const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

OwnedFileDescriptor open_export_directory_components(
    int base_descriptor, const fs::path& traversal_path,
    const fs::path& display_path, bool is_revalidation) {
    int retained_descriptor = fcntl(base_descriptor, F_DUPFD_CLOEXEC, 0);
    if(retained_descriptor < 0) {
        // TRANSLATORS: The placeholders are an export parent path and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to retain the resolution base for export directory {}: {}",
            display_path.string(), std::strerror(errno)));
    }
    OwnedFileDescriptor current(retained_descriptor);

    for(const fs::path& component : traversal_path) {
        if(component.empty() || component == "/" || component == ".") {
            continue;
        }
        int next_descriptor = openat(
            current.get(), component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(next_descriptor < 0) {
            const int open_error = errno;
            if(is_revalidation) {
                // TRANSLATORS: The placeholders are an export parent path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Unable to safely revalidate export directory {}: {}",
                    display_path.string(), std::strerror(open_error)));
            }
            // TRANSLATORS: The placeholders are an export parent path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Unable to safely open existing export directory {}: {}",
                display_path.string(), std::strerror(open_error)));
        }
        current = OwnedFileDescriptor(next_descriptor);
    }
    return current;
}

OwnedFileDescriptor open_absolute_export_directory(
    const fs::path& absolute_path, bool is_revalidation) {
    int root_descriptor = open(
        "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_descriptor < 0) {
        // TRANSLATORS: The placeholder is a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to open the filesystem root for export resolution: {}",
            std::strerror(errno)));
    }
    OwnedFileDescriptor root(root_descriptor);
    return open_export_directory_components(
        root.get(), absolute_path.relative_path(), absolute_path,
        is_revalidation);
}

fs::path proc_file_descriptor_path(int descriptor) {
    return fs::path("/proc") / std::to_string(getpid()) / "fd" /
           std::to_string(descriptor);
}

class AnchoredDirectory {
    fs::path display_path_;
    OwnedFileDescriptor descriptor_;
    DirectoryIdentity identity_;
    std::optional<fs::path> named_path_;
    std::optional<fs::path> resolution_base_named_path_;
    std::optional<DirectoryIdentity> resolution_base_identity_;

    AnchoredDirectory(
        fs::path display_path, int descriptor, DirectoryIdentity identity,
        std::optional<fs::path> named_path = std::nullopt,
        std::optional<fs::path> resolution_base_named_path =
            std::nullopt,
        std::optional<DirectoryIdentity> resolution_base_identity =
            std::nullopt)
        : display_path_(std::move(display_path)), descriptor_(descriptor),
          identity_(identity), named_path_(std::move(named_path)),
          resolution_base_named_path_(
              std::move(resolution_base_named_path)),
          resolution_base_identity_(resolution_base_identity) {
    }

public:
    AnchoredDirectory(const AnchoredDirectory&) = delete;
    AnchoredDirectory& operator=(const AnchoredDirectory&) = delete;
    AnchoredDirectory(AnchoredDirectory&&) noexcept = default;
    AnchoredDirectory& operator=(AnchoredDirectory&&) noexcept = default;

    static AnchoredDirectory open_temporary_parent(const fs::path& path) {
        int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(descriptor < 0) {
            // TRANSLATORS: The placeholders are a directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to open temporary directory parent {}: {}",
                path.string(),
                std::strerror(errno)));
        }
        OwnedFileDescriptor opened_directory(descriptor);
        DirectoryIdentity identity = require_directory_identity(
            descriptor,
            DirectoryIdentityContext::TemporaryParent,
            path);

        struct stat named_status{};
        if(fstatat(AT_FDCWD, path.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0) {
            // TRANSLATORS: The placeholders are a directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to revalidate temporary directory parent {}: {}",
                path.string(),
                std::strerror(errno)));
        }
        if(!directory_identity_matches(identity, named_status)) {
            // TRANSLATORS: The placeholder is a directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Refusing changed temporary directory parent path: {}",
                path.string()));
        }

        return AnchoredDirectory(path, opened_directory.release(), identity);
    }

    static AnchoredDirectory adopt_export_parent(
        fs::path display_path, int descriptor, DirectoryIdentity identity,
        fs::path named_path,
        std::optional<fs::path> resolution_base_named_path =
            std::nullopt,
        std::optional<DirectoryIdentity> resolution_base_identity =
            std::nullopt) {
        return AnchoredDirectory(
            std::move(display_path), descriptor, identity,
            std::move(named_path),
            std::move(resolution_base_named_path),
            resolution_base_identity);
    }

    int descriptor() const {
        return descriptor_.get();
    }

    const DirectoryIdentity& identity() const {
        return identity_;
    }

    const fs::path& display_path() const {
        return display_path_;
    }

    void require_named_identity() const {
        if(!named_path_.has_value()) return;

        const DirectoryIdentity open_identity = require_directory_identity(
            descriptor_.get(), DirectoryIdentityContext::ExportParent,
            display_path_);
        if(open_identity.device != identity_.device ||
           open_identity.inode != identity_.inode) {
            // TRANSLATORS: The placeholder is an export parent path.
            throw std::runtime_error(localization::format_translated_message(
                "Export directory descriptor changed identity: {}",
                display_path_.string()));
        }

        if(resolution_base_named_path_.has_value() &&
           resolution_base_identity_.has_value()) {
            OwnedFileDescriptor reopened_base =
                open_absolute_export_directory(
                    resolution_base_named_path_.value(), true);
            const DirectoryIdentity actual_base = require_directory_identity(
                reopened_base.get(),
                DirectoryIdentityContext::ExportParent,
                resolution_base_named_path_.value());
            if(actual_base.device != resolution_base_identity_->device ||
               actual_base.inode != resolution_base_identity_->inode) {
                // TRANSLATORS: The placeholder is the command-start current directory path.
                throw std::runtime_error(localization::format_translated_message(
                    "Refusing changed command-start current directory path: {}",
                    resolution_base_named_path_->string()));
            }
        }

        OwnedFileDescriptor reopened = open_absolute_export_directory(
            named_path_.value(), true);
        const DirectoryIdentity named_identity = require_directory_identity(
            reopened.get(), DirectoryIdentityContext::ExportParent,
            named_path_.value());
        if(named_identity.device != identity_.device ||
           named_identity.inode != identity_.inode) {
            // TRANSLATORS: The placeholder is an export parent path.
            throw std::runtime_error(localization::format_translated_message(
                "Refusing changed export directory path: {}",
                display_path_.string()));
        }
    }
};

void remove_directory_contents_at(
    int directory_descriptor, const fs::path& display_path) {
    int scan_descriptor = openat(
        directory_descriptor, ".",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(scan_descriptor < 0) {
        // TRANSLATORS: The placeholders are a temporary directory path and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to scan temporary directory {}: {}",
            display_path.string(),
            std::strerror(errno)));
    }

    DIR* raw_stream = fdopendir(scan_descriptor);
    if(!raw_stream) {
        int error = errno;
        close(scan_descriptor);
        // TRANSLATORS: The placeholders are a temporary directory path and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to read temporary directory {}: {}",
            display_path.string(),
            std::strerror(error)));
    }
    std::unique_ptr<DIR, int (*)(DIR*)> stream(raw_stream, closedir);

    while(true) {
        errno = 0;
        dirent* entry = readdir(stream.get());
        if(!entry) {
            if(errno != 0) {
                // TRANSLATORS: The placeholders are a temporary directory path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed while reading temporary directory {}: {}",
                    display_path.string(),
                    std::strerror(errno)));
            }
            break;
        }

        std::string leaf_name = entry->d_name;
        if(leaf_name == "." || leaf_name == "..") continue;
        fs::path entry_display_path = display_path / leaf_name;

        struct stat observed_status{};
        if(fstatat(
               directory_descriptor, leaf_name.c_str(), &observed_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
            if(errno == ENOENT) continue;
            // TRANSLATORS: The placeholders are a temporary entry path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to inspect temporary entry {}: {}",
                entry_display_path.string(),
                std::strerror(errno)));
        }

        if(S_ISDIR(observed_status.st_mode)) {
            int child_descriptor = openat(
                directory_descriptor, leaf_name.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if(child_descriptor < 0) {
                // TRANSLATORS: The placeholders are a temporary entry path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Refusing changed temporary directory entry {}: {}",
                    entry_display_path.string(),
                    std::strerror(errno)));
            }
            OwnedFileDescriptor child(child_descriptor);

            struct stat opened_status{};
            if(fstat(child.get(), &opened_status) != 0 ||
               !filesystem_identity_matches(observed_status, opened_status)) {
                // TRANSLATORS: The placeholder is a temporary directory entry path.
                throw std::runtime_error(localization::format_translated_message(
                    "Refusing changed temporary directory entry: {}",
                    entry_display_path.string()));
            }

            remove_directory_contents_at(child.get(), entry_display_path);

            struct stat final_status{};
            if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &final_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
               !filesystem_identity_matches(opened_status, final_status)) {
                // TRANSLATORS: The placeholder is a temporary directory entry path.
                throw std::runtime_error(localization::format_translated_message(
                    "Refusing changed temporary directory entry: {}",
                    entry_display_path.string()));
            }
            if(unlinkat(directory_descriptor, leaf_name.c_str(), AT_REMOVEDIR) != 0) {
                // TRANSLATORS: The placeholders are a temporary directory entry path and a system error message.
                throw std::runtime_error(localization::format_translated_message(
                    "Failed to remove temporary directory entry {}: {}",
                    entry_display_path.string(),
                    std::strerror(errno)));
            }
            continue;
        }

        struct stat final_status{};
        if(fstatat(
               directory_descriptor, leaf_name.c_str(), &final_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
           !filesystem_identity_matches(observed_status, final_status)) {
            // TRANSLATORS: The placeholder is a temporary entry path.
            throw std::runtime_error(localization::format_translated_message(
                "Refusing changed temporary entry: {}",
                entry_display_path.string()));
        }
        if(unlinkat(directory_descriptor, leaf_name.c_str(), 0) != 0) {
            // TRANSLATORS: The placeholders are a temporary entry path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to remove temporary entry {}: {}",
                entry_display_path.string(),
                std::strerror(errno)));
        }
    }
}

// cache とは独立した、一回の export だけが所有する secure temporary directory。
// POLICY(#167): 保存identityとの不一致を観測した場合は、named pathのcleanup/publishを拒否する。
class TemporaryDirectoryGuard {
    OwnedFileDescriptor parent_descriptor_;
    OwnedFileDescriptor directory_descriptor_;
    DirectoryIdentity parent_identity_;
    DirectoryIdentity directory_identity_;
    std::string leaf_name_;
    fs::path display_path_;
    bool owns_path_ = true;

    TemporaryDirectoryGuard(
        int parent_descriptor, int directory_descriptor,
        DirectoryIdentity parent_identity, DirectoryIdentity directory_identity,
        std::string leaf_name, fs::path display_path)
        : parent_descriptor_(parent_descriptor), directory_descriptor_(directory_descriptor),
          parent_identity_(parent_identity), directory_identity_(directory_identity),
          leaf_name_(std::move(leaf_name)),
          display_path_(std::move(display_path)) {
    }

public:
    TemporaryDirectoryGuard(const TemporaryDirectoryGuard&) = delete;
    TemporaryDirectoryGuard& operator=(const TemporaryDirectoryGuard&) = delete;

    static TemporaryDirectoryGuard create(const AnchoredDirectory& parent) {
        int retained_parent_descriptor =
            fcntl(parent.descriptor(), F_DUPFD_CLOEXEC, 0);
        if(retained_parent_descriptor < 0) {
            // TRANSLATORS: The placeholders are a directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to retain temporary directory parent {}: {}",
                parent.display_path().string(),
                std::strerror(errno)));
        }
        OwnedFileDescriptor retained_parent(retained_parent_descriptor);
        DirectoryIdentity retained_parent_identity = require_directory_identity(
            retained_parent.get(),
            DirectoryIdentityContext::TemporaryParent,
            parent.display_path());
        if(retained_parent_identity.device != parent.identity().device ||
           retained_parent_identity.inode != parent.identity().inode) {
            // TRANSLATORS: The placeholder is a directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Temporary directory parent changed identity: {}",
                parent.display_path().string()));
        }

        std::string template_path =
            (proc_file_descriptor_path(retained_parent.get()) /
             ".moguet-pkgbuild-XXXXXX")
                .string();
        std::vector<char> path_buffer(template_path.begin(), template_path.end());
        path_buffer.push_back('\0');

        char* created_path = mkdtemp(path_buffer.data());
        if(!created_path) {
            // TRANSLATORS: The placeholders are a parent directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to create a temporary directory under {}: {}",
                parent.display_path().string(),
                std::strerror(errno)));
        }

        std::string leaf_name = fs::path(created_path).filename().string();
        struct stat created_status{};
        if(fstatat(
               retained_parent.get(), leaf_name.c_str(), &created_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
           !S_ISDIR(created_status.st_mode)) {
            // TRANSLATORS: The placeholder is a temporary directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to inspect created temporary directory {}.",
                (parent.display_path() / leaf_name).string()));
        }

        int directory_descriptor = openat(
            retained_parent.get(), leaf_name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(directory_descriptor < 0) {
            int open_error = errno;
            struct stat current_status{};
            if(fstatat(
                   retained_parent.get(), leaf_name.c_str(), &current_status,
                   AT_SYMLINK_NOFOLLOW) == 0 &&
               filesystem_identity_matches(created_status, current_status)) {
                unlinkat(retained_parent.get(), leaf_name.c_str(), AT_REMOVEDIR);
            }
            // TRANSLATORS: The placeholders are a temporary directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to open temporary directory {}: {}",
                (parent.display_path() / leaf_name).string(),
                std::strerror(open_error)));
        }
        OwnedFileDescriptor opened_directory(directory_descriptor);
        DirectoryIdentity directory_identity = require_directory_identity(
            directory_descriptor,
            DirectoryIdentityContext::TemporaryExport,
            parent.display_path() / leaf_name);

        struct stat named_status{};
        if(fstatat(
               retained_parent.get(), leaf_name.c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
           !directory_identity_matches(directory_identity, named_status) ||
           !filesystem_identity_matches(created_status, named_status)) {
            // TRANSLATORS: The placeholder is a temporary directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Refusing changed temporary directory path: {}",
                (parent.display_path() / leaf_name).string()));
        }

        return TemporaryDirectoryGuard(
            retained_parent.release(), opened_directory.release(),
            retained_parent_identity, directory_identity, leaf_name,
            parent.display_path() / leaf_name);
    }

    int parent_descriptor() const {
        return parent_descriptor_.get();
    }

    int directory_descriptor() const {
        return directory_descriptor_.get();
    }

    const std::string& leaf_name() const {
        return leaf_name_;
    }

    const fs::path& display_path() const {
        return display_path_;
    }

    fs::path anchored_path() const {
        return proc_file_descriptor_path(directory_descriptor_.get());
    }

    void require_owned_identity() const {
        DirectoryIdentity parent_identity = require_directory_identity(
            parent_descriptor_.get(),
            DirectoryIdentityContext::TemporaryParent,
            display_path_.parent_path());
        if(parent_identity.device != parent_identity_.device ||
           parent_identity.inode != parent_identity_.inode) {
            // TRANSLATORS: The placeholder is a directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Temporary directory parent changed identity: {}",
                display_path_.parent_path().string()));
        }

        DirectoryIdentity open_identity = require_directory_identity(
            directory_descriptor_.get(),
            DirectoryIdentityContext::TemporaryExport,
            display_path_);
        if(open_identity.device != directory_identity_.device ||
           open_identity.inode != directory_identity_.inode) {
            // TRANSLATORS: The placeholder is a temporary export directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Temporary export directory descriptor changed identity: {}",
                display_path_.string()));
        }

        struct stat named_status{};
        if(fstatat(
               parent_descriptor_.get(), leaf_name_.c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
           !directory_identity_matches(directory_identity_, named_status)) {
            // TRANSLATORS: The placeholder is a temporary directory path.
            throw std::runtime_error(localization::format_translated_message(
                "Refusing changed temporary directory path: {}",
                display_path_.string()));
        }
    }

    void release() {
        owns_path_ = false;
    }

    void cleanup() {
        if(!owns_path_) return;
        // LANDMINE(#167): identity mismatchを観測した場合はreplacementを消さずartifactを残す。
        require_owned_identity();
        remove_directory_contents_at(directory_descriptor_.get(), display_path_);
        require_owned_identity();
        if(unlinkat(
               parent_descriptor_.get(), leaf_name_.c_str(),
               AT_REMOVEDIR) != 0) {
            // TRANSLATORS: The placeholders are a temporary directory path and a system error message.
            throw std::runtime_error(localization::format_translated_message(
                "Failed to remove temporary directory {}: {}",
                display_path_.string(),
                std::strerror(errno)));
        }
        owns_path_ = false;
    }

    ~TemporaryDirectoryGuard() noexcept {
        if(!owns_path_) return;
        try {
            cleanup();
        } catch(const std::exception& e) {
            Logger::warn_noexcept([&e]() {
                return std::string(e.what());
            });
        } catch(...) {
            Logger::warn_noexcept([]() {
                return localization::translate_message(
                    "Refusing unsafe temporary directory cleanup because of an unknown error.");
            });
        }
    }
};

std::optional<struct stat> export_entry_status_at(
    int parent_descriptor, const std::string& leaf_name,
    const fs::path& display_path) {
    struct stat status{};
    if(fstatat(
           parent_descriptor, leaf_name.c_str(), &status,
           AT_SYMLINK_NOFOLLOW) == 0) {
        return status;
    }
    if(errno == ENOENT) return std::nullopt;
    // TRANSLATORS: The placeholders are an export path and a system error message.
    throw std::runtime_error(localization::format_translated_message(
        "Unable to inspect export path {}: {}",
        display_path.string(),
        std::strerror(errno)));
}

void require_regular_export_pkgbuild(
    int checkout_descriptor, const fs::path& pkgbuild_path) {
    std::optional<struct stat> status = export_entry_status_at(
        checkout_descriptor, "PKGBUILD", pkgbuild_path);
    if(!status.has_value() || !S_ISREG(status->st_mode)) {
        // TRANSLATORS: The placeholders are the literal PKGBUILD name and its path.
        throw std::runtime_error(localization::format_translated_message(
            "Exported {} is not a regular non-symlink file: {}",
            "PKGBUILD", pkgbuild_path.string()));
    }
}

void require_export_git_directory(const TemporaryDirectoryGuard& checkout) {
    fs::path git_path = checkout.display_path() / ".git";
    int git_descriptor = openat(
        checkout.directory_descriptor(), ".git",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(git_descriptor < 0) {
        // TRANSLATORS: The placeholders are the literal AUR and .git identities and an AUR checkout directory path.
        throw std::runtime_error(localization::format_translated_message(
            "Cloned {} repository is missing a regular {} directory: {}",
            "AUR", ".git",
            checkout.display_path().string()));
    }
    OwnedFileDescriptor git_directory(git_descriptor);
    require_directory_identity(
        git_directory.get(),
        DirectoryIdentityContext::ExportedGit,
        git_path);
}

AurExportSource resolve_aur_export_source(const std::string& target) {
    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info_strict(target);
    } catch(const std::exception& e) {
        // TRANSLATORS: The placeholders are the literal AUR identity, an AUR package name, and an AUR diagnostic.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to resolve {} package {}: {}",
            "AUR", target, e.what()));
    }
    if(!info.has_value()) {
        // TRANSLATORS: The placeholders are the literal AUR identity and an AUR package name.
        throw std::runtime_error(localization::format_translated_message(
            "{} package not found: {}", "AUR", target));
    }
    if(!is_valid_aur_export_identifier(info->PackageBase)) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities and a PackageBase name.
        throw std::runtime_error(localization::format_translated_message(
            "Invalid {} {} for export: {}",
            "AUR", "PackageBase", info->PackageBase));
    }

    return AurExportSource{
        target, info->PackageBase, aur_git_url_for_package_base(info->PackageBase)};
}

AnchoredDirectory require_export_current_directory() {
    int descriptor = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor < 0) {
        // TRANSLATORS: The placeholder is a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to open the current directory: {}",
            std::strerror(errno)));
    }
    OwnedFileDescriptor current_directory_descriptor(descriptor);
    const DirectoryIdentity current_identity = require_directory_identity(
        descriptor,
        DirectoryIdentityContext::ExportParent,
        ".");

    std::error_code ec;
    fs::path current_directory = fs::current_path(ec);
    if(ec) {
        // TRANSLATORS: The placeholder is a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to read the current directory: {}", ec.message()));
    }
    current_directory = current_directory.lexically_normal();
    AnchoredDirectory anchored = AnchoredDirectory::adopt_export_parent(
        current_directory, current_directory_descriptor.release(),
        current_identity, current_directory);
    anchored.require_named_identity();
    return anchored;
}

AnchoredDirectory require_export_parent(
    const std::optional<std::string>& output_directory) {
    if(!output_directory.has_value()) {
        return require_export_current_directory();
    }

    const fs::path requested_path(output_directory.value());
    if(requested_path.empty()) {
        // TRANSLATORS: The placeholder is the literal PKGBUILD artifact identity.
        throw std::invalid_argument(localization::format_translated_message(
            "The {} export directory must not be empty.",
            "PKGBUILD"));
    }

    if(requested_path.is_absolute()) {
        const fs::path display_path = requested_path.lexically_normal();
        OwnedFileDescriptor opened =
            open_absolute_export_directory(requested_path, false);
        const DirectoryIdentity identity = require_directory_identity(
            opened.get(), DirectoryIdentityContext::ExportParent,
            display_path);
        AnchoredDirectory anchored = AnchoredDirectory::adopt_export_parent(
            display_path, opened.release(), identity, requested_path);
        anchored.require_named_identity();
        return anchored;
    }

    AnchoredDirectory command_start = require_export_current_directory();
    const fs::path display_path =
        (command_start.display_path() / requested_path)
            .lexically_normal();
    const fs::path named_path =
        command_start.display_path() / requested_path;
    OwnedFileDescriptor opened = open_export_directory_components(
        command_start.descriptor(), requested_path, display_path, false);
    const DirectoryIdentity identity = require_directory_identity(
        opened.get(), DirectoryIdentityContext::ExportParent,
        display_path);
    AnchoredDirectory anchored = AnchoredDirectory::adopt_export_parent(
        display_path, opened.release(), identity, named_path,
        command_start.display_path(), command_start.identity());
    anchored.require_named_identity();
    return anchored;
}

AnchoredDirectory require_export_temporary_parent() {
    std::error_code ec;
    fs::path temporary_parent = fs::temp_directory_path(ec);
    if(ec) {
        // TRANSLATORS: The placeholder is a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to resolve the temporary directory: {}", ec.message()));
    }
    temporary_parent = fs::canonical(temporary_parent, ec);
    if(ec) {
        // TRANSLATORS: The placeholder is a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Unable to canonicalize the temporary directory: {}",
            ec.message()));
    }
    return AnchoredDirectory::open_temporary_parent(temporary_parent);
}

fs::path require_missing_export_destination(
    const AnchoredDirectory& export_parent,
    const std::string& package_base) {
    if(!is_valid_aur_export_identifier(package_base)) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities and a PackageBase name.
        throw std::runtime_error(localization::format_translated_message(
            "Invalid {} {} for export: {}",
            "AUR", "PackageBase", package_base));
    }

    fs::path destination_path =
        (export_parent.display_path() / package_base).lexically_normal();
    // POLICY(#167,#434): validated PackageBaseはsafe export parentの
    // direct-child leafに固定する。
    if(destination_path.parent_path() != export_parent.display_path() ||
       !is_path_contained(export_parent.display_path(), destination_path, false)) {
        // TRANSLATORS: The placeholder is an export destination path.
        throw std::runtime_error(localization::format_translated_message(
            "Export destination resolves outside the export directory: {}",
            destination_path.string()));
    }

    if(export_entry_status_at(
           export_parent.descriptor(), package_base,
           destination_path)
           .has_value()) {
        // TRANSLATORS: The placeholder is an export destination path.
        throw std::runtime_error(localization::format_translated_message(
            "Export destination already exists: {}",
            destination_path.string()));
    }
    return destination_path;
}

void validate_aur_export_checkout(
    const AurExportSource& source, const TemporaryDirectoryGuard& checkout) {
    checkout.require_owned_identity();
    require_export_git_directory(checkout);
    std::string current_remote_url;
    try {
        current_remote_url = trusted_git_aur_export_remote_origin_url(
            checkout.anchored_path());
    } catch(const std::exception&) {
        // TRANSLATORS: The placeholders are the literal remote.origin.url, AUR, and PackageBase identities and a PackageBase name.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to read local {} for {} {} {}.",
            "remote.origin.url", "AUR", "PackageBase",
            source.package_base));
    }
    if(current_remote_url.empty()) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities, a PackageBase name, and the literal remote.origin.url key.
        throw std::runtime_error(localization::format_translated_message(
            "{} {} {} has no {}.",
            "AUR", "PackageBase", source.package_base,
            "remote.origin.url"));
    }
    if(!remote_url_matches_expected(current_remote_url, source.git_url)) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities and a PackageBase name.
        throw std::runtime_error(localization::format_translated_message(
            "Remote URL mismatch for {} {} {}.",
            "AUR", "PackageBase", source.package_base));
    }

    require_regular_export_pkgbuild(
        checkout.directory_descriptor(), checkout.display_path() / "PKGBUILD");
    checkout.require_owned_identity();
}

void clone_and_validate_aur_export(
    const AurExportSource& source, const TemporaryDirectoryGuard& checkout) {
    checkout.require_owned_identity();
    if(trusted_git_clone_aur_export(
           source.git_url, checkout.anchored_path()) != 0) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities and a PackageBase name.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to clone {} {} {}.",
            "AUR", "PackageBase", source.package_base));
    }
    validate_aur_export_checkout(source, checkout);
}

std::string read_pkgbuild_bytes(const TemporaryDirectoryGuard& checkout) {
    checkout.require_owned_identity();
    fs::path pkgbuild_path = checkout.display_path() / "PKGBUILD";
    require_regular_export_pkgbuild(checkout.directory_descriptor(), pkgbuild_path);

    int descriptor = openat(
        checkout.directory_descriptor(), "PKGBUILD",
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(descriptor < 0) {
        // TRANSLATORS: The placeholders are the literal PKGBUILD name, its path, and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to open {} {}: {}",
            "PKGBUILD", pkgbuild_path.string(),
            std::strerror(errno)));
    }
    OwnedFileDescriptor file(descriptor);

    struct stat file_status{};
    if(fstat(file.get(), &file_status) != 0) {
        // TRANSLATORS: The placeholders are the literal PKGBUILD name, its path, and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to inspect {} {}: {}",
            "PKGBUILD", pkgbuild_path.string(),
            std::strerror(errno)));
    }
    if(!S_ISREG(file_status.st_mode)) {
        // TRANSLATORS: The placeholders are the literal PKGBUILD name and its path.
        throw std::runtime_error(localization::format_translated_message(
            "Exported {} is not a regular file: {}",
            "PKGBUILD", pkgbuild_path.string()));
    }

    std::string contents;
    if(file_status.st_size > 0 &&
       static_cast<unsigned long long>(file_status.st_size) <=
           static_cast<unsigned long long>(contents.max_size())) {
        contents.reserve(static_cast<size_t>(file_status.st_size));
    }

    std::array<char, 8192> buffer;
    while(true) {
        ssize_t bytes_read = ::read(file.get(), buffer.data(), buffer.size());
        if(bytes_read > 0) {
            contents.append(buffer.data(), static_cast<size_t>(bytes_read));
            continue;
        }
        if(bytes_read == 0) break;
        if(errno == EINTR) continue;
        // TRANSLATORS: The placeholders are the literal PKGBUILD name, its path, and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to read {} {}: {}",
            "PKGBUILD", pkgbuild_path.string(),
            std::strerror(errno)));
    }
    checkout.require_owned_identity();
    return contents;
}

void rename_export_without_replacement(
    TemporaryDirectoryGuard& temporary_directory,
    const AnchoredDirectory& destination_parent, const std::string& destination_name,
    const fs::path& destination_display_path) {
    if(export_entry_status_at(
           destination_parent.descriptor(), destination_name,
           destination_display_path)
           .has_value()) {
        // TRANSLATORS: The placeholder is an export destination path.
        throw std::runtime_error(localization::format_translated_message(
            "Export destination already exists: {}",
            destination_display_path.string()));
    }
    temporary_directory.require_owned_identity();
    // POLICY(#434): held fdだけでなく、利用者が指定したnamed parent pathが
    // publish直前にも同じfilesystem objectを指すことを確認する。
    // この後にpost-publish checkは置かず、failure表示後の既publishを作らない。
    destination_parent.require_named_identity();

    // LANDMINE(#167): std::filesystem::rename may replace an entry created after preflight.
    // directory fd と renameat2(NOREPLACE) で、anchored parent以外や
    // existing pathへpublishしない。
    if(syscall(
           SYS_renameat2, temporary_directory.parent_descriptor(),
           temporary_directory.leaf_name().c_str(), destination_parent.descriptor(),
           destination_name.c_str(), RENAME_NOREPLACE) != 0) {
        if(errno == EEXIST || errno == ENOTEMPTY) {
            // TRANSLATORS: The placeholder is an export destination path.
            throw std::runtime_error(localization::format_translated_message(
                "Export destination already exists: {}",
                destination_display_path.string()));
        }
        // TRANSLATORS: The placeholders are the literal AUR identity, an export destination path, and a system error message.
        throw std::runtime_error(localization::format_translated_message(
            "Failed to publish the {} export to {}: {}",
            "AUR", destination_display_path.string(),
            std::strerror(errno)));
    }
    temporary_directory.release();
}

} // namespace

void export_pkgbuild_tree(
    const std::string& target,
    const std::optional<std::string>& output_directory) {
    require_valid_aur_export_target(target, "-G");
    // Invalid parentはRPC/cloneより前に拒否する。relative指定はこの時点の
    // command-start CWD fdをauthorityとして解決する。
    AnchoredDirectory export_parent =
        require_export_parent(output_directory);
    AurExportSource source = resolve_aur_export_source(target);
    fs::path destination_path =
        require_missing_export_destination(
            export_parent, source.package_base);

    if(source.requested_name != source.package_base) {
        // TRANSLATORS: The placeholders are the literal AUR identity, an AUR package name, the literal PackageBase identity, and its name.
        Logger::info(localization::format_translated_message(
            "{} package mapping: {} -> {} {}.",
            "AUR", source.requested_name,
            "PackageBase",
            source.package_base));
    }

    TemporaryDirectoryGuard temporary_directory =
        TemporaryDirectoryGuard::create(export_parent);
    clone_and_validate_aur_export(source, temporary_directory);
    // LANDMINE(#167): clone後のtreeをpublish直前にもfd基準で再検証する。
    validate_aur_export_checkout(source, temporary_directory);
    rename_export_without_replacement(
        temporary_directory, export_parent, source.package_base,
        destination_path);

    if(output_directory.has_value()) {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities, a PackageBase name, and an export parent path.
        Logger::info(localization::format_translated_message(
            "Exported {} {} {} under {}.",
            "AUR", "PackageBase", source.package_base,
            export_parent.display_path().string()));
    } else {
        // TRANSLATORS: The placeholders are the literal AUR and PackageBase identities and a PackageBase name.
        Logger::info(localization::format_translated_message(
            "Exported {} {} {} in the command-start current directory.",
            "AUR", "PackageBase", source.package_base));
    }
}

std::string load_pkgbuild_for_stdout(const std::string& target) {
    require_valid_aur_export_target(target, "-Gp");
    AurExportSource source = resolve_aur_export_source(target);
    if(source.requested_name != source.package_base) {
        // TRANSLATORS: The placeholders are the literal AUR identity, an AUR package name, the literal PackageBase identity, and its name.
        Logger::info(localization::format_translated_message(
            "{} package mapping: {} -> {} {}.",
            "AUR", source.requested_name,
            "PackageBase",
            source.package_base));
    }

    AnchoredDirectory temporary_parent = require_export_temporary_parent();
    TemporaryDirectoryGuard temporary_directory =
        TemporaryDirectoryGuard::create(temporary_parent);
    clone_and_validate_aur_export(source, temporary_directory);
    std::string pkgbuild = read_pkgbuild_bytes(temporary_directory);

    // POLICY(#167): consumerへbytesを返す前にtemporary checkoutを必ずcleanupする。
    temporary_directory.cleanup();
    return pkgbuild;
}
