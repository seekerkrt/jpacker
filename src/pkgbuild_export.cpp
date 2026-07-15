#include "pkgbuild_export.hpp"

#include "aur_rpc.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"

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

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";

// export lifecycleをpersistent source checkoutから独立させるためのlocal helper群。
// POLICY(#196): generic helperへ持ち上げず、各ownerが必要なpolicyをprivateに保持する。
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

std::string shell_quote(const std::string& str) {
    // POLICY: 外部コマンド引数は、validation済みの値でもshell境界では必ずquoteする。
    std::string quoted = "'";
    for(char ch : str) {
        if(ch == '\'')
            quoted += "'\\''";
        else
            quoted += ch;
    }
    quoted += "'";
    return quoted;
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
        throw std::runtime_error(
                "Invalid AUR target for operation " + operation + ": " + target);
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

DirectoryIdentity require_directory_identity(int descriptor, const std::string& context) {
    struct stat status {};
    if(fstat(descriptor, &status) != 0) {
        throw std::runtime_error("Failed to inspect " + context + ": " + std::strerror(errno));
    }
    if(!S_ISDIR(status.st_mode)) {
        throw std::runtime_error(context + " is not a directory.");
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

fs::path proc_file_descriptor_path(int descriptor) {
    return fs::path("/proc") / std::to_string(getpid()) / "fd" /
           std::to_string(descriptor);
}

class AnchoredDirectory {
    fs::path            display_path_;
    OwnedFileDescriptor descriptor_;
    DirectoryIdentity   identity_;

    AnchoredDirectory(
            fs::path display_path, int descriptor, DirectoryIdentity identity)
        : display_path_(std::move(display_path)), descriptor_(descriptor), identity_(identity) {
    }

public:
    AnchoredDirectory(const AnchoredDirectory&) = delete;
    AnchoredDirectory& operator=(const AnchoredDirectory&) = delete;

    static AnchoredDirectory open_path(
            const fs::path& path, const std::string& context) {
        int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(descriptor < 0) {
            throw std::runtime_error(
                    "Failed to open " + context + " " + path.string() + ": " +
                    std::strerror(errno));
        }
        OwnedFileDescriptor opened_directory(descriptor);
        DirectoryIdentity identity = require_directory_identity(descriptor, context);

        struct stat named_status {};
        if(fstatat(AT_FDCWD, path.c_str(), &named_status, AT_SYMLINK_NOFOLLOW) != 0) {
            throw std::runtime_error(
                    "Failed to revalidate " + context + " " + path.string() + ": " +
                    std::strerror(errno));
        }
        if(!directory_identity_matches(identity, named_status)) {
            throw std::runtime_error(
                    "Refusing changed " + context + " path: " + path.string());
        }

        return AnchoredDirectory(path, opened_directory.release(), identity);
    }

    static AnchoredDirectory adopt_current_directory(
            fs::path display_path, int descriptor, DirectoryIdentity identity) {
        return AnchoredDirectory(std::move(display_path), descriptor, identity);
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
};

void remove_directory_contents_at(
        int directory_descriptor, const fs::path& display_path) {
    int scan_descriptor = openat(
            directory_descriptor, ".",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(scan_descriptor < 0) {
        throw std::runtime_error(
                "Failed to scan temporary directory " + display_path.string() + ": " +
                std::strerror(errno));
    }

    DIR* raw_stream = fdopendir(scan_descriptor);
    if(!raw_stream) {
        int error = errno;
        close(scan_descriptor);
        throw std::runtime_error(
                "Failed to read temporary directory " + display_path.string() + ": " +
                std::strerror(error));
    }
    std::unique_ptr<DIR, int (*)(DIR*)> stream(raw_stream, closedir);

    while(true) {
        errno = 0;
        dirent* entry = readdir(stream.get());
        if(!entry) {
            if(errno != 0) {
                throw std::runtime_error(
                        "Failed while reading temporary directory " +
                        display_path.string() + ": " + std::strerror(errno));
            }
            break;
        }

        std::string leaf_name = entry->d_name;
        if(leaf_name == "." || leaf_name == "..") continue;
        fs::path entry_display_path = display_path / leaf_name;

        struct stat observed_status {};
        if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &observed_status,
                   AT_SYMLINK_NOFOLLOW) != 0) {
            if(errno == ENOENT) continue;
            throw std::runtime_error(
                    "Failed to inspect temporary entry " +
                    entry_display_path.string() + ": " + std::strerror(errno));
        }

        if(S_ISDIR(observed_status.st_mode)) {
            int child_descriptor = openat(
                    directory_descriptor, leaf_name.c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if(child_descriptor < 0) {
                throw std::runtime_error(
                        "Refusing changed temporary directory entry " +
                        entry_display_path.string() + ": " + std::strerror(errno));
            }
            OwnedFileDescriptor child(child_descriptor);

            struct stat opened_status {};
            if(fstat(child.get(), &opened_status) != 0 ||
               !filesystem_identity_matches(observed_status, opened_status)) {
                throw std::runtime_error(
                        "Refusing changed temporary directory entry: " +
                        entry_display_path.string());
            }

            remove_directory_contents_at(child.get(), entry_display_path);

            struct stat final_status {};
            if(fstatat(
                       directory_descriptor, leaf_name.c_str(), &final_status,
                       AT_SYMLINK_NOFOLLOW) != 0 ||
               !filesystem_identity_matches(opened_status, final_status)) {
                throw std::runtime_error(
                        "Refusing changed temporary directory entry: " +
                        entry_display_path.string());
            }
            if(unlinkat(directory_descriptor, leaf_name.c_str(), AT_REMOVEDIR) != 0) {
                throw std::runtime_error(
                        "Failed to remove temporary directory entry " +
                        entry_display_path.string() + ": " + std::strerror(errno));
            }
            continue;
        }

        struct stat final_status {};
        if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &final_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
           !filesystem_identity_matches(observed_status, final_status)) {
            throw std::runtime_error(
                    "Refusing changed temporary entry: " +
                    entry_display_path.string());
        }
        if(unlinkat(directory_descriptor, leaf_name.c_str(), 0) != 0) {
            throw std::runtime_error(
                    "Failed to remove temporary entry " + entry_display_path.string() +
                    ": " + std::strerror(errno));
        }
    }
}

// cache とは独立した、一回の export だけが所有する secure temporary directory。
// POLICY(#167): 保存identityとの不一致を観測した場合は、named pathのcleanup/publishを拒否する。
class TemporaryDirectoryGuard {
    OwnedFileDescriptor parent_descriptor_;
    OwnedFileDescriptor directory_descriptor_;
    DirectoryIdentity   parent_identity_;
    DirectoryIdentity   directory_identity_;
    std::string         leaf_name_;
    fs::path            display_path_;
    bool                owns_path_ = true;

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
            throw std::runtime_error(
                    "Failed to retain temporary directory parent " +
                    parent.display_path().string() + ": " + std::strerror(errno));
        }
        OwnedFileDescriptor retained_parent(retained_parent_descriptor);
        DirectoryIdentity retained_parent_identity = require_directory_identity(
                retained_parent.get(), "temporary directory parent");
        if(retained_parent_identity.device != parent.identity().device ||
           retained_parent_identity.inode != parent.identity().inode) {
            throw std::runtime_error(
                    "Temporary directory parent changed identity: " +
                    parent.display_path().string());
        }

        std::string template_path =
                (proc_file_descriptor_path(retained_parent.get()) /
                 ".jpacker-pkgbuild-XXXXXX")
                        .string();
        std::vector<char> path_buffer(template_path.begin(), template_path.end());
        path_buffer.push_back('\0');

        char* created_path = mkdtemp(path_buffer.data());
        if(!created_path) {
            throw std::runtime_error(
                    "Failed to create temporary directory under " +
                    parent.display_path().string() + ": " + std::strerror(errno));
        }

        std::string leaf_name = fs::path(created_path).filename().string();
        struct stat created_status {};
        if(fstatat(
                   retained_parent.get(), leaf_name.c_str(), &created_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
           !S_ISDIR(created_status.st_mode)) {
            throw std::runtime_error(
                    "Failed to inspect created temporary directory " +
                    (parent.display_path() / leaf_name).string());
        }

        int directory_descriptor = openat(
                retained_parent.get(), leaf_name.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(directory_descriptor < 0) {
            int open_error = errno;
            struct stat current_status {};
            if(fstatat(
                       retained_parent.get(), leaf_name.c_str(), &current_status,
                       AT_SYMLINK_NOFOLLOW) == 0 &&
               filesystem_identity_matches(created_status, current_status)) {
                unlinkat(retained_parent.get(), leaf_name.c_str(), AT_REMOVEDIR);
            }
            throw std::runtime_error(
                    "Failed to open temporary directory " +
                    (parent.display_path() / leaf_name).string() + ": " +
                    std::strerror(open_error));
        }
        OwnedFileDescriptor opened_directory(directory_descriptor);
        DirectoryIdentity directory_identity = require_directory_identity(
                directory_descriptor, "temporary export directory");

        struct stat named_status {};
        if(fstatat(
                   retained_parent.get(), leaf_name.c_str(), &named_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
           !directory_identity_matches(directory_identity, named_status) ||
           !filesystem_identity_matches(created_status, named_status)) {
            throw std::runtime_error(
                    "Refusing changed temporary directory path: " +
                    (parent.display_path() / leaf_name).string());
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
                parent_descriptor_.get(), "temporary directory parent");
        if(parent_identity.device != parent_identity_.device ||
           parent_identity.inode != parent_identity_.inode) {
            throw std::runtime_error(
                    "Temporary directory parent changed identity: " +
                    display_path_.parent_path().string());
        }

        DirectoryIdentity open_identity = require_directory_identity(
                directory_descriptor_.get(), "temporary export directory");
        if(open_identity.device != directory_identity_.device ||
           open_identity.inode != directory_identity_.inode) {
            throw std::runtime_error(
                    "Temporary export directory descriptor changed identity: " +
                    display_path_.string());
        }

        struct stat named_status {};
        if(fstatat(
                   parent_descriptor_.get(), leaf_name_.c_str(), &named_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
           !directory_identity_matches(directory_identity_, named_status)) {
            throw std::runtime_error(
                    "Refusing changed temporary directory path: " +
                    display_path_.string());
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
            throw std::runtime_error(
                    "Failed to remove temporary directory " + display_path_.string() +
                    ": " + std::strerror(errno));
        }
        owns_path_ = false;
    }

    ~TemporaryDirectoryGuard() {
        if(!owns_path_) return;
        try {
            cleanup();
        } catch(const std::exception& e) {
            Logger::warn(e.what());
        } catch(...) {
            Logger::warn(
                    "Refusing unsafe temporary directory cleanup: unknown error");
        }
    }
};

std::optional<struct stat> export_entry_status_at(
        int parent_descriptor, const std::string& leaf_name,
        const fs::path& display_path) {
    struct stat status {};
    if(fstatat(
               parent_descriptor, leaf_name.c_str(), &status,
               AT_SYMLINK_NOFOLLOW) == 0) {
        return status;
    }
    if(errno == ENOENT) return std::nullopt;
    throw std::runtime_error(
            "Unable to inspect export path " + display_path.string() + ": " +
            std::strerror(errno));
}

void require_regular_export_pkgbuild(
        int checkout_descriptor, const fs::path& pkgbuild_path) {
    std::optional<struct stat> status = export_entry_status_at(
            checkout_descriptor, "PKGBUILD", pkgbuild_path);
    if(!status.has_value() || !S_ISREG(status->st_mode)) {
        throw std::runtime_error(
                "Exported PKGBUILD is not a regular non-symlink file: " +
                pkgbuild_path.string());
    }
}

void require_export_git_directory(const TemporaryDirectoryGuard& checkout) {
    fs::path git_path = checkout.display_path() / ".git";
    int git_descriptor = openat(
            checkout.directory_descriptor(), ".git",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(git_descriptor < 0) {
        throw std::runtime_error(
                "Cloned AUR repository is missing a regular .git directory: " +
                checkout.display_path().string());
    }
    OwnedFileDescriptor git_directory(git_descriptor);
    require_directory_identity(
            git_directory.get(), "exported .git directory " + git_path.string());
}

AurExportSource resolve_aur_export_source(const std::string& target) {
    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info_strict(target);
    } catch(const std::exception& e) {
        throw std::runtime_error("Failed to resolve AUR package " + target + ": " + e.what());
    }
    if(!info.has_value()) {
        throw std::runtime_error("AUR package not found: " + target);
    }
    if(!is_valid_aur_export_identifier(info->PackageBase)) {
        throw std::runtime_error(
                "Invalid AUR PackageBase for export: " + info->PackageBase);
    }

    return AurExportSource{
            target, info->PackageBase, aur_git_url_for_package_base(info->PackageBase)};
}

AnchoredDirectory require_export_current_directory() {
    int descriptor = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor < 0) {
        throw std::runtime_error(
                "Unable to open current directory: " + std::string(std::strerror(errno)));
    }
    OwnedFileDescriptor current_directory_descriptor(descriptor);
    DirectoryIdentity current_identity = require_directory_identity(
            descriptor, "current export directory");

    std::error_code ec;
    fs::path        current_directory = fs::current_path(ec);
    if(ec) {
        throw std::runtime_error("Unable to read current directory: " + ec.message());
    }
    current_directory = fs::canonical(current_directory, ec);
    if(ec) {
        throw std::runtime_error("Unable to canonicalize current directory: " + ec.message());
    }

    struct stat named_status {};
    if(fstatat(
               AT_FDCWD, current_directory.c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        throw std::runtime_error(
                "Unable to revalidate current directory " + current_directory.string() +
                ": " + std::strerror(errno));
    }
    if(!directory_identity_matches(current_identity, named_status)) {
        throw std::runtime_error(
                "Refusing changed current directory path: " +
                current_directory.string());
    }
    return AnchoredDirectory::adopt_current_directory(
            current_directory, current_directory_descriptor.release(), current_identity);
}

AnchoredDirectory require_export_temporary_parent() {
    std::error_code ec;
    fs::path temporary_parent = fs::temp_directory_path(ec);
    if(ec) {
        throw std::runtime_error("Unable to resolve temporary directory: " + ec.message());
    }
    temporary_parent = fs::canonical(temporary_parent, ec);
    if(ec) {
        throw std::runtime_error("Unable to canonicalize temporary directory: " + ec.message());
    }
    return AnchoredDirectory::open_path(temporary_parent, "temporary directory");
}

fs::path require_missing_export_destination(
        const AnchoredDirectory& current_directory, const std::string& package_base) {
    if(!is_valid_aur_export_identifier(package_base)) {
        throw std::runtime_error("Invalid AUR PackageBase for export: " + package_base);
    }

    fs::path destination_path =
            (current_directory.display_path() / package_base).lexically_normal();
    // POLICY(#167): export destination は command 開始時 cwd の direct child に固定する。
    if(destination_path.parent_path() != current_directory.display_path() ||
       !is_path_contained(current_directory.display_path(), destination_path, false)) {
        throw std::runtime_error(
                "Export destination resolves outside current directory: " + destination_path.string());
    }

    if(export_entry_status_at(
               current_directory.descriptor(), package_base,
               destination_path)
               .has_value()) {
        throw std::runtime_error("Export destination already exists: " + destination_path.string());
    }
    return destination_path;
}

void validate_aur_export_checkout(
        const AurExportSource& source, const TemporaryDirectoryGuard& checkout) {
    checkout.require_owned_identity();
    require_export_git_directory(checkout);
    std::string remote_command =
            "git -C " + shell_quote(checkout.anchored_path().string()) +
            " config --local --get remote.origin.url 2>/dev/null";
    CapturedCommandResult remote_result =
            capture_command_output(remote_command.c_str());
    if(remote_result.exit_code != 0) {
        throw std::runtime_error(
                "Failed to read local remote.origin.url for AUR PackageBase " +
                source.package_base + " (git config exit " +
                std::to_string(remote_result.exit_code) + ").");
    }
    if(remote_result.output.empty()) {
        throw std::runtime_error(
                "Missing remote.origin.url for AUR PackageBase " + source.package_base + ".");
    }
    if(!remote_url_matches_expected(remote_result.output, source.git_url)) {
        throw std::runtime_error(
                "Remote URL mismatch for AUR PackageBase " + source.package_base +
                ": " + remote_result.output);
    }

    require_regular_export_pkgbuild(
            checkout.directory_descriptor(), checkout.display_path() / "PKGBUILD");
    checkout.require_owned_identity();
}

void clone_and_validate_aur_export(
        const AurExportSource& source, const TemporaryDirectoryGuard& checkout) {
    checkout.require_owned_identity();
    std::string clone_command =
            "git clone --quiet -- " + shell_quote(source.git_url) + " " +
            shell_quote(checkout.anchored_path().string()) + " > /dev/null";
    if(run_command(clone_command) != 0) {
        throw std::runtime_error("Failed to clone AUR PackageBase " + source.package_base + ".");
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
        throw std::runtime_error(
                "Failed to open PKGBUILD " + pkgbuild_path.string() + ": " + std::strerror(errno));
    }
    OwnedFileDescriptor file(descriptor);

    struct stat file_status {};
    if(fstat(file.get(), &file_status) != 0) {
        throw std::runtime_error(
                "Failed to inspect PKGBUILD " + pkgbuild_path.string() + ": " + std::strerror(errno));
    }
    if(!S_ISREG(file_status.st_mode)) {
        throw std::runtime_error(
                "Exported PKGBUILD is not a regular file: " + pkgbuild_path.string());
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
        throw std::runtime_error(
                "Failed to read PKGBUILD " + pkgbuild_path.string() + ": " + std::strerror(errno));
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
        throw std::runtime_error(
                "Export destination already exists: " +
                destination_display_path.string());
    }
    temporary_directory.require_owned_identity();

    // LANDMINE(#167): std::filesystem::rename may replace an entry created after preflight.
    // directory fd と renameat2(NOREPLACE) で、開始時 cwd 以外や existing path へ publish しない。
    if(syscall(
               SYS_renameat2, temporary_directory.parent_descriptor(),
               temporary_directory.leaf_name().c_str(), destination_parent.descriptor(),
               destination_name.c_str(), RENAME_NOREPLACE) != 0) {
        if(errno == EEXIST || errno == ENOTEMPTY) {
            throw std::runtime_error(
                    "Export destination already exists: " +
                    destination_display_path.string());
        }
        throw std::runtime_error(
                "Failed to publish AUR export to " + destination_display_path.string() + ": " +
                std::strerror(errno));
    }
    temporary_directory.release();
}

} // namespace

void export_pkgbuild_tree(const std::string& target) {
    require_valid_aur_export_target(target, "-G");
    // command開始時のcwdをRPC/cloneより前に固定し、後続path計算の親として使い続ける。
    AnchoredDirectory current_directory = require_export_current_directory();
    AurExportSource   source = resolve_aur_export_source(target);
    fs::path destination_path =
            require_missing_export_destination(current_directory, source.package_base);

    if(source.requested_name != source.package_base) {
        Logger::info(
                source.requested_name + " -> PackageBase " + source.package_base);
    }

    TemporaryDirectoryGuard temporary_directory =
            TemporaryDirectoryGuard::create(current_directory);
    clone_and_validate_aur_export(source, temporary_directory);
    // LANDMINE(#167): clone後のtreeをpublish直前にもfd基準で再検証する。
    validate_aur_export_checkout(source, temporary_directory);
    rename_export_without_replacement(
            temporary_directory, current_directory, source.package_base,
            destination_path);

    Logger::info(
            "Exported AUR PackageBase " + source.package_base +
            " in the command-start current directory.");
}

std::string load_pkgbuild_for_stdout(const std::string& target) {
    require_valid_aur_export_target(target, "-Gp");
    AurExportSource source = resolve_aur_export_source(target);
    if(source.requested_name != source.package_base) {
        Logger::info(
                source.requested_name + " -> PackageBase " + source.package_base);
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
