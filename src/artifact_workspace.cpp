#include "artifact_workspace.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

// query/build-onlyのexact context lineageだけを共有するimmutable token。
// global registryやmutable stateを持たず、context move後もExpected側のproofを保つ。
struct ArtifactMakepkgContextProvenance final {
};

// invocation-owned artifact workspaceのcreation、identity、command environment、
// freshness validation、cleanupをこのmoduleへ閉じ込める。
namespace {

namespace fs = std::filesystem;

constexpr mode_t ARTIFACT_WORKSPACE_MODE = 0700;
constexpr char ARTIFACT_WORKSPACE_PREFIX[] = ".artifact-workspace~-";
constexpr std::size_t ARTIFACT_DIAGNOSTIC_VALUE_LIMIT = 96;

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

    OwnedFileDescriptor& operator=(OwnedFileDescriptor&&) = delete;

    ~OwnedFileDescriptor() noexcept {
        if(descriptor_ >= 0) close(descriptor_);
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

// command間でpathnameを再解決せず、prepare時に開いたcheckout inodeへchdirする。
// named pathとのidentity照合はcallerが前後で行う。
class FileDescriptorWorkDirGuard final {
    OwnedFileDescriptor original_directory_;

public:
    explicit FileDescriptorWorkDirGuard(int target_descriptor)
        : original_directory_(open(
                  ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) {
        if(original_directory_.get() < 0) {
            throw std::runtime_error(
                    "Failed to retain the current working directory: " +
                    std::string(std::strerror(errno)));
        }
        if(fchdir(target_descriptor) != 0) {
            throw std::runtime_error(
                    "Failed to enter the retained source checkout: " +
                    std::string(std::strerror(errno)));
        }
    }

    FileDescriptorWorkDirGuard(const FileDescriptorWorkDirGuard&) = delete;
    FileDescriptorWorkDirGuard& operator=(
            const FileDescriptorWorkDirGuard&) = delete;

    ~FileDescriptorWorkDirGuard() noexcept {
        if(original_directory_.get() >= 0)
            static_cast<void>(fchdir(original_directory_.get()));
    }
};

struct DirectoryIdentity {
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
};

std::uintmax_t status_device(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_dev);
}

std::uintmax_t status_inode(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_ino);
}

std::uintmax_t status_owner(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_uid);
}

bool same_filesystem_identity(
        const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

bool same_directory_identity(
        const DirectoryIdentity& expected, const struct stat& actual) {
    return S_ISDIR(actual.st_mode) && expected.device == status_device(actual) &&
           expected.inode == status_inode(actual);
}

struct stat require_descriptor_status(
        int descriptor, const std::string& context) {
    struct stat status {};
    if(fstat(descriptor, &status) != 0) {
        throw std::runtime_error(
                "Failed to inspect " + context + ": " + std::strerror(errno));
    }
    return status;
}

fs::path proc_file_descriptor_path(int descriptor) {
    return fs::path("/proc") / std::to_string(getpid()) / "fd" /
           std::to_string(descriptor);
}

void require_no_inherited_pkgdest() {
    // getenv()はdefined-emptyでもnon-nullを返す。makepkg.confはこの境界では解析しない。
    if(std::getenv("PKGDEST") != nullptr) {
        throw std::runtime_error(
                "Inherited PKGDEST conflicts with invocation-owned artifact workspace.");
    }
}

std::string bounded_artifact_diagnostic_value(std::string_view value) {
    constexpr char HEX_DIGITS[] = "0123456789abcdef";

    std::string bounded;
    bounded.reserve(ARTIFACT_DIAGNOSTIC_VALUE_LIMIT * 4 + 3);
    const std::size_t copied_size =
            std::min(value.size(), ARTIFACT_DIAGNOSTIC_VALUE_LIMIT);
    for(std::size_t index = 0; index < copied_size; ++index) {
        const unsigned char character =
                static_cast<unsigned char>(value[index]);
        if(character >= 0x20 && character <= 0x7e && character != '\\') {
            bounded.push_back(static_cast<char>(character));
        } else if(character == '\\') {
            bounded += "\\\\";
        } else {
            bounded += "\\x";
            bounded.push_back(HEX_DIGITS[character >> 4]);
            bounded.push_back(HEX_DIGITS[character & 0x0f]);
        }
    }
    if(value.size() > copied_size) bounded += "...";
    return bounded;
}

std::string bounded_artifact_diagnostic_path(const fs::path& path) {
    return bounded_artifact_diagnostic_value(path.string());
}

std::optional<struct stat> entry_status_at(
        int directory_descriptor, const std::string& leaf_name,
        const fs::path& display_path) {
    struct stat status {};
    if(fstatat(
               directory_descriptor, leaf_name.c_str(), &status,
               AT_SYMLINK_NOFOLLOW) == 0) {
        return status;
    }
    if(errno == ENOENT) return std::nullopt;
    throw std::runtime_error(
            "Unable to inspect artifact path " +
            bounded_artifact_diagnostic_path(display_path) + ": " +
            std::strerror(errno));
}

void remove_directory_contents_at(
        int directory_descriptor, const fs::path& display_path,
        std::uintmax_t workspace_device) {
    int scan_descriptor = openat(
            directory_descriptor, ".",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(scan_descriptor < 0) {
        throw std::runtime_error(
                "Failed to scan artifact workspace " + display_path.string() +
                ": " + std::strerror(errno));
    }

    DIR* raw_stream = fdopendir(scan_descriptor);
    if(!raw_stream) {
        int open_error = errno;
        close(scan_descriptor);
        throw std::runtime_error(
                "Failed to read artifact workspace " + display_path.string() +
                ": " + std::strerror(open_error));
    }
    std::unique_ptr<DIR, int (*)(DIR*)> stream(raw_stream, closedir);

    while(true) {
        errno = 0;
        dirent* entry = readdir(stream.get());
        if(!entry) {
            if(errno != 0) {
                throw std::runtime_error(
                        "Failed while reading artifact workspace " +
                        display_path.string() + ": " + std::strerror(errno));
            }
            break;
        }

        std::string leaf_name = entry->d_name;
        if(leaf_name == "." || leaf_name == "..") continue;
        fs::path entry_path = display_path / leaf_name;

        struct stat observed_status {};
        if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &observed_status,
                   AT_SYMLINK_NOFOLLOW) != 0) {
            if(errno == ENOENT) continue;
            throw std::runtime_error(
                    "Failed to inspect artifact workspace entry " +
                    entry_path.string() + ": " + std::strerror(errno));
        }

        if(S_ISDIR(observed_status.st_mode)) {
            // LANDMINE(#242): mount boundaryをcleanupで横断しない。
            if(status_device(observed_status) != workspace_device) {
                throw std::runtime_error(
                        "Refusing to cross filesystem boundary while cleaning " +
                        entry_path.string());
            }

            int child_descriptor = openat(
                    directory_descriptor, leaf_name.c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if(child_descriptor < 0) {
                throw std::runtime_error(
                        "Refusing changed artifact workspace directory " +
                        entry_path.string() + ": " + std::strerror(errno));
            }
            OwnedFileDescriptor child(child_descriptor);

            struct stat opened_status = require_descriptor_status(
                    child.get(), "artifact workspace directory " + entry_path.string());
            if(!same_filesystem_identity(observed_status, opened_status)) {
                throw std::runtime_error(
                        "Refusing changed artifact workspace directory: " +
                        entry_path.string());
            }

            remove_directory_contents_at(
                    child.get(), entry_path, workspace_device);

            struct stat final_status {};
            if(fstatat(
                       directory_descriptor, leaf_name.c_str(), &final_status,
                       AT_SYMLINK_NOFOLLOW) != 0 ||
               !same_filesystem_identity(opened_status, final_status)) {
                throw std::runtime_error(
                        "Refusing changed artifact workspace directory: " +
                        entry_path.string());
            }
            if(unlinkat(
                       directory_descriptor, leaf_name.c_str(),
                       AT_REMOVEDIR) != 0) {
                throw std::runtime_error(
                        "Failed to remove artifact workspace directory " +
                        entry_path.string() + ": " + std::strerror(errno));
            }
            continue;
        }

        struct stat final_status {};
        if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &final_status,
                   AT_SYMLINK_NOFOLLOW) != 0 ||
           !same_filesystem_identity(observed_status, final_status)) {
            throw std::runtime_error(
                    "Refusing changed artifact workspace entry: " +
                    entry_path.string());
        }
        // Symlinkやspecial fileもfollowせず、workspace内のdirectory entryだけを外す。
        if(unlinkat(directory_descriptor, leaf_name.c_str(), 0) != 0) {
            throw std::runtime_error(
                    "Failed to remove artifact workspace entry " +
                    entry_path.string() + ": " + std::strerror(errno));
        }
    }
}

class CreatedWorkspaceRollback final {
    int         root_descriptor_ = -1;
    std::string leaf_name_;
    struct stat created_status_ {};
    bool        active_ = true;

public:
    CreatedWorkspaceRollback(
            int root_descriptor, std::string leaf_name,
            const struct stat& created_status)
        : root_descriptor_(root_descriptor), leaf_name_(std::move(leaf_name)),
          created_status_(created_status) {
    }

    CreatedWorkspaceRollback(const CreatedWorkspaceRollback&) = delete;
    CreatedWorkspaceRollback& operator=(const CreatedWorkspaceRollback&) = delete;

    void release() noexcept {
        active_ = false;
    }

    ~CreatedWorkspaceRollback() noexcept {
        if(!active_) return;
        struct stat current_status {};
        if(fstatat(
                   root_descriptor_, leaf_name_.c_str(), &current_status,
                   AT_SYMLINK_NOFOLLOW) == 0 &&
           same_filesystem_identity(created_status_, current_status)) {
            unlinkat(root_descriptor_, leaf_name_.c_str(), AT_REMOVEDIR);
        }
    }
};

bool is_blank_packagelist_line(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](char character) {
        return character == ' ' || character == '\t';
    });
}

std::vector<std::string> parse_packagelist_records(
        const std::string& raw_output) {
    for(unsigned char character : raw_output) {
        if(character == '\r') {
            throw std::runtime_error(
                    "makepkg --packagelist output contains a carriage return.");
        }
        if((character < 0x20 && character != '\n') || character == 0x7f) {
            throw std::runtime_error(
                    "makepkg --packagelist output contains an invalid control character.");
        }
    }

    std::vector<std::string> records;
    std::set<std::string>    seen_paths;
    std::size_t              line_start = 0;
    std::size_t              line_number = 1;
    while(line_start <= raw_output.size()) {
        std::size_t line_end = raw_output.find('\n', line_start);
        std::string line = raw_output.substr(
                line_start,
                line_end == std::string::npos
                        ? std::string::npos
                        : line_end - line_start);
        if(!is_blank_packagelist_line(line)) {
            if(!seen_paths.insert(line).second) {
                throw std::runtime_error(
                        "makepkg --packagelist returned a duplicate artifact "
                        "path at record " + std::to_string(line_number) + ".");
            }
            records.push_back(std::move(line));
        }
        if(line_end == std::string::npos) break;
        line_start = line_end + 1;
        ++line_number;
    }
    return records;
}

void require_direct_expected_path(
        const ArtifactWorkspace& workspace, const fs::path& candidate) {
    if(!candidate.is_absolute()) {
        throw std::runtime_error(
                "makepkg --packagelist returned a relative artifact path.");
    }
    for(const auto& component : candidate) {
        if(component == "." || component == "..") {
            throw std::runtime_error(
                    "makepkg --packagelist returned an artifact path with a "
                    "dot component.");
        }
    }
    if(candidate == workspace.canonical_path() || candidate.filename().empty() ||
       candidate.parent_path() != workspace.canonical_path()) {
        throw std::runtime_error(
                "makepkg --packagelist artifact is not a direct child of the "
                "artifact workspace.");
    }
}

class InspectedArtifact final {
    OwnedFileDescriptor descriptor_;

public:
    struct stat status {};

    InspectedArtifact(int descriptor, const struct stat& artifact_status)
        : descriptor_(descriptor), status(artifact_status) {
    }

    InspectedArtifact(const InspectedArtifact&) = delete;
    InspectedArtifact& operator=(const InspectedArtifact&) = delete;
    InspectedArtifact(InspectedArtifact&&) noexcept = default;
    InspectedArtifact& operator=(InspectedArtifact&&) = delete;

    int descriptor() const noexcept {
        return descriptor_.get();
    }

    int release_descriptor() noexcept {
        return descriptor_.release();
    }
};

void require_regular_owned_entry(
        const struct stat& status, std::uintmax_t expected_effective_user,
        const fs::path& entry_path, const std::string& kind) {
    if(S_ISLNK(status.st_mode)) {
        throw std::runtime_error(
                kind + " must not be a symlink: " + entry_path.string());
    }
    if(!S_ISREG(status.st_mode)) {
        throw std::runtime_error(
                kind + " must be a regular file: " + entry_path.string());
    }
    if(status_owner(status) != expected_effective_user) {
        throw std::runtime_error(
                kind + " owner does not match the effective user: " +
                entry_path.string());
    }
}

OwnedFileDescriptor open_and_revalidate_regular_entry(
        int directory_descriptor, const std::string& leaf_name,
        const struct stat& named_status, const fs::path& entry_path,
        const std::string& kind) {
    int descriptor = openat(
            directory_descriptor, leaf_name.c_str(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(descriptor < 0) {
        throw std::runtime_error(
                "Failed to open " + kind + " " + entry_path.string() + ": " +
                std::strerror(errno));
    }
    OwnedFileDescriptor opened(descriptor);
    struct stat opened_status = require_descriptor_status(
            opened.get(), kind + " " + entry_path.string());
    if(!same_filesystem_identity(named_status, opened_status)) {
        throw std::runtime_error(
                "Refusing changed " + kind + ": " + entry_path.string());
    }
    return opened;
}

void require_only_expected_workspace_entries(
        int directory_descriptor, const fs::path& workspace_path,
        const std::string& artifact_leaf, const std::string& signature_leaf) {
    int scan_descriptor = openat(
            directory_descriptor, ".",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(scan_descriptor < 0) {
        throw std::runtime_error(
                "Failed to enumerate artifact workspace " +
                workspace_path.string() + ": " + std::strerror(errno));
    }
    DIR* raw_stream = fdopendir(scan_descriptor);
    if(!raw_stream) {
        int open_error = errno;
        close(scan_descriptor);
        throw std::runtime_error(
                "Failed to enumerate artifact workspace " +
                workspace_path.string() + ": " + std::strerror(open_error));
    }
    std::unique_ptr<DIR, int (*)(DIR*)> stream(raw_stream, closedir);

    while(true) {
        errno = 0;
        dirent* entry = readdir(stream.get());
        if(!entry) {
            if(errno != 0) {
                throw std::runtime_error(
                        "Failed while enumerating artifact workspace " +
                        workspace_path.string() + ": " + std::strerror(errno));
            }
            break;
        }
        std::string leaf_name = entry->d_name;
        if(leaf_name == "." || leaf_name == ".." ||
           leaf_name == artifact_leaf || leaf_name == signature_leaf) {
            continue;
        }

        fs::path extra_path = workspace_path / leaf_name;
        struct stat extra_status {};
        if(fstatat(
                   directory_descriptor, leaf_name.c_str(), &extra_status,
                   AT_SYMLINK_NOFOLLOW) != 0) {
            if(errno == ENOENT) continue;
            throw std::runtime_error(
                    "Failed to inspect unexpected artifact workspace entry " +
                    extra_path.string() + ": " + std::strerror(errno));
        }
        if(S_ISDIR(extra_status.st_mode)) {
            throw std::runtime_error(
                    "Unexpected directory in artifact workspace: " +
                    extra_path.string());
        }
        if(leaf_name.ends_with(".sig")) {
            throw std::runtime_error(
                    "Unmatched signature in artifact workspace: " +
                    extra_path.string());
        }
        throw std::runtime_error(
                "Unexpected entry in artifact workspace: " +
                extra_path.string());
    }
}

InspectedArtifact inspect_post_build_artifact(
        int directory_descriptor, const fs::path& workspace_path,
        const std::string& artifact_leaf,
        std::uintmax_t expected_effective_user) {
    fs::path artifact_path = workspace_path / artifact_leaf;
    std::optional<struct stat> artifact_status = entry_status_at(
            directory_descriptor, artifact_leaf, artifact_path);
    if(!artifact_status.has_value()) {
        throw std::runtime_error(
                "Expected package artifact is missing: " +
                artifact_path.string());
    }
    require_regular_owned_entry(
            artifact_status.value(), expected_effective_user, artifact_path,
            "Package artifact");
    OwnedFileDescriptor artifact_descriptor = open_and_revalidate_regular_entry(
            directory_descriptor, artifact_leaf, artifact_status.value(),
            artifact_path, "package artifact");

    std::string signature_leaf = artifact_leaf + ".sig";
    fs::path signature_path = workspace_path / signature_leaf;
    std::optional<struct stat> signature_status = entry_status_at(
            directory_descriptor, signature_leaf, signature_path);
    if(signature_status.has_value()) {
        require_regular_owned_entry(
                signature_status.value(), expected_effective_user,
                signature_path, "Package signature");
        static_cast<void>(open_and_revalidate_regular_entry(
                directory_descriptor, signature_leaf,
                signature_status.value(), signature_path,
                "package signature"));
    }

    require_only_expected_workspace_entries(
            directory_descriptor, workspace_path, artifact_leaf,
            signature_leaf);

    std::optional<struct stat> final_artifact_status = entry_status_at(
            directory_descriptor, artifact_leaf, artifact_path);
    if(!final_artifact_status.has_value() ||
       !same_filesystem_identity(
               artifact_status.value(), final_artifact_status.value())) {
        throw std::runtime_error(
                "Refusing changed package artifact: " + artifact_path.string());
    }
    std::optional<struct stat> final_signature_status = entry_status_at(
            directory_descriptor, signature_leaf, signature_path);
    if(signature_status.has_value() != final_signature_status.has_value() ||
       (signature_status.has_value() &&
        !same_filesystem_identity(
                signature_status.value(), final_signature_status.value()))) {
        throw std::runtime_error(
                "Refusing changed package signature: " +
                signature_path.string());
    }
    return InspectedArtifact(
            artifact_descriptor.release(), artifact_status.value());
}

} // namespace

ArtifactWorkspace::ArtifactWorkspace(
        ValidatedPrivateCacheRoot root, fs::path path,
        fs::path canonical_path, std::string leaf_name,
        int directory_descriptor, std::uintmax_t device,
        std::uintmax_t inode, std::uintmax_t owner) noexcept
    : root_(std::move(root)), path_(std::move(path)),
      canonical_path_(std::move(canonical_path)),
      leaf_name_(std::move(leaf_name)),
      directory_descriptor_(directory_descriptor), device_(device),
      inode_(inode), owner_(owner), owns_path_(true) {
}

ArtifactWorkspace::ArtifactWorkspace(ArtifactWorkspace&& other) noexcept
    : root_(std::move(other.root_)), path_(std::move(other.path_)),
      canonical_path_(std::move(other.canonical_path_)),
      leaf_name_(std::move(other.leaf_name_)),
      directory_descriptor_(std::exchange(other.directory_descriptor_, -1)),
      device_(other.device_), inode_(other.inode_), owner_(other.owner_),
      owns_path_(std::exchange(other.owns_path_, false)),
      cleanup_on_destruction_(other.cleanup_on_destruction_) {
}

ArtifactWorkspace::~ArtifactWorkspace() noexcept {
    if(owns_path_ && cleanup_on_destruction_) {
        try {
            cleanup();
        } catch(const std::exception& error) {
            Logger::warn(
                    "Refusing unsafe artifact workspace cleanup for " +
                    path_.string() + ": " + error.what());
        } catch(...) {
            Logger::warn(
                    "Refusing unsafe artifact workspace cleanup for " +
                    path_.string() + ": unknown error");
        }
    }
    if(directory_descriptor_ >= 0) close(directory_descriptor_);
}

void ArtifactWorkspace::require_unchanged_identity_for_owner(
        std::uintmax_t expected_effective_user) const {
    if(!owns_path_ || directory_descriptor_ < 0) {
        throw std::runtime_error(
                "Artifact workspace is no longer owned: " + path_.string());
    }

    root_.require_unchanged_identity();
    if(path_.parent_path() != root_.canonical_path() ||
       canonical_path_.parent_path() != root_.canonical_path() ||
       path_.filename().string() != leaf_name_) {
        throw std::runtime_error(
                "Artifact workspace is not a direct child of the trusted cache root: " +
                path_.string());
    }

    struct stat open_workspace_status = require_descriptor_status(
            directory_descriptor_, "artifact workspace " + path_.string());
    DirectoryIdentity expected_workspace{device_, inode_};
    if(!same_directory_identity(expected_workspace, open_workspace_status) ||
       status_owner(open_workspace_status) != owner_ ||
       owner_ != expected_effective_user ||
       (open_workspace_status.st_mode & 07777) != ARTIFACT_WORKSPACE_MODE) {
        throw std::runtime_error(
                "Artifact workspace descriptor changed identity, owner, or mode: " +
                path_.string());
    }

    struct stat named_workspace_status {};
    if(fstatat(
               root_.directory_descriptor(), leaf_name_.c_str(),
               &named_workspace_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_directory_identity(expected_workspace, named_workspace_status) ||
       status_owner(named_workspace_status) != owner_ ||
       (named_workspace_status.st_mode & 07777) != ARTIFACT_WORKSPACE_MODE) {
        throw std::runtime_error(
                "Artifact workspace path changed identity, owner, or mode: " +
                path_.string());
    }

    std::error_code canonical_error;
    fs::path current_canonical = fs::canonical(path_, canonical_error);
    if(canonical_error || current_canonical != canonical_path_ ||
       current_canonical.parent_path() != root_.canonical_path()) {
        throw std::runtime_error(
                "Artifact workspace canonical containment changed: " +
                path_.string());
    }
}

void ArtifactWorkspace::require_unchanged_identity() const {
    require_unchanged_identity_for_owner(
            static_cast<std::uintmax_t>(geteuid()));
}

void ArtifactWorkspace::retain_for_diagnostics() noexcept {
    cleanup_on_destruction_ = false;
}

void ArtifactWorkspace::cleanup() {
    if(!owns_path_) return;
    // POLICY(#242): cleanup entry時にidentityを証明できなければ何も削除しない。
    // NOTE: same-euid processによるcheck後の同時mutationまでは脅威modelに含めない。
    require_unchanged_identity();
    remove_directory_contents_at(
            directory_descriptor_, canonical_path_, device_);
    require_unchanged_identity();
    if(unlinkat(
               root_.directory_descriptor(), leaf_name_.c_str(),
               AT_REMOVEDIR) != 0) {
        throw std::runtime_error(
                "Failed to remove artifact workspace " + path_.string() + ": " +
                std::strerror(errno));
    }
    owns_path_ = false;
    if(directory_descriptor_ >= 0) {
        close(directory_descriptor_);
        directory_descriptor_ = -1;
    }
}

ArtifactWorkspace create_artifact_workspace(
        ValidatedPrivateCacheRoot root) {
    root.require_unchanged_identity();
    const int root_descriptor = root.directory_descriptor();

    std::string template_path =
            (proc_file_descriptor_path(root_descriptor) /
             (std::string(ARTIFACT_WORKSPACE_PREFIX) + "XXXXXX"))
                    .string();
    std::vector<char> path_buffer(template_path.begin(), template_path.end());
    path_buffer.push_back('\0');
    char* created_path = mkdtemp(path_buffer.data());
    if(!created_path) {
        throw std::runtime_error(
                "Failed to create artifact workspace under " +
                root.canonical_path().string() + ": " + std::strerror(errno));
    }

    std::string leaf_name = fs::path(created_path).filename().string();
    fs::path display_path = root.canonical_path() / leaf_name;
    struct stat created_status {};
    if(fstatat(
               root_descriptor, leaf_name.c_str(), &created_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        int inspect_error = errno;
        // POLICY(#242): identityを一度も証明できていないnamed leafは削除しない。
        // replacementを消すより、fresh workspace候補をfail-safeに残す。
        throw std::runtime_error(
                "Failed to inspect created artifact workspace " +
                display_path.string() + ": " +
                std::strerror(inspect_error));
    }
    CreatedWorkspaceRollback rollback(
            root_descriptor, leaf_name, created_status);

    if(!S_ISDIR(created_status.st_mode) || S_ISLNK(created_status.st_mode) ||
       status_owner(created_status) !=
               static_cast<std::uintmax_t>(geteuid()) ||
       (created_status.st_mode & 07777) != ARTIFACT_WORKSPACE_MODE) {
        throw std::runtime_error(
                "Created artifact workspace has an unsafe type, owner, or mode: " +
                display_path.string());
    }
    if(!leaf_name.starts_with(ARTIFACT_WORKSPACE_PREFIX) ||
       is_valid_package_name(leaf_name)) {
        throw std::logic_error(
                "Artifact workspace namespace collides with package identifiers: " +
                leaf_name);
    }

    int directory_descriptor = openat(
            root_descriptor, leaf_name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(directory_descriptor < 0) {
        throw std::runtime_error(
                "Failed to open created artifact workspace " +
                display_path.string() + ": " + std::strerror(errno));
    }
    OwnedFileDescriptor opened_directory(directory_descriptor);
    struct stat opened_status = require_descriptor_status(
            opened_directory.get(), "artifact workspace " + display_path.string());
    if(!same_filesystem_identity(created_status, opened_status)) {
        throw std::runtime_error(
                "Created artifact workspace changed identity: " +
                display_path.string());
    }

    std::error_code canonical_error;
    fs::path canonical_path = fs::canonical(display_path, canonical_error);
    if(canonical_error || canonical_path.parent_path() != root.canonical_path() ||
       canonical_path != display_path) {
        throw std::runtime_error(
                "Created artifact workspace is outside the trusted cache root: " +
                display_path.string());
    }

    ArtifactWorkspace workspace(
            std::move(root), std::move(display_path),
            std::move(canonical_path), std::move(leaf_name),
            opened_directory.get(),
            status_device(created_status), status_inode(created_status),
            status_owner(created_status));
    static_cast<void>(opened_directory.release());
    rollback.release();
    return workspace;
}

void require_unclaimed_artifact_pkgdest(
        const SourceBuildEnvironment& environment) {
    if(environment.defines("PKGDEST")) {
        throw std::runtime_error(
                "Source environment PKGDEST conflicts with invocation-owned "
                "artifact workspace.");
    }
    require_no_inherited_pkgdest();
}

ArtifactMakepkgContext::ArtifactMakepkgContext(
        ValidatedCachePath checkout,
        SourceBuildEnvironment command_environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy,
        int checkout_descriptor, std::uintmax_t checkout_device,
        std::uintmax_t checkout_inode,
        fs::path workspace_path, std::uintmax_t workspace_device,
        std::uintmax_t workspace_inode)
    : checkout_(std::move(checkout)),
      command_environment_(std::move(command_environment)),
      empty_value_policy_(empty_value_policy),
      checkout_descriptor_(checkout_descriptor),
      checkout_device_(checkout_device), checkout_inode_(checkout_inode),
      provenance_(std::make_shared<const ArtifactMakepkgContextProvenance>()),
      workspace_path_(std::move(workspace_path)),
      workspace_device_(workspace_device), workspace_inode_(workspace_inode) {
}

ArtifactMakepkgContext::ArtifactMakepkgContext(
        ArtifactMakepkgContext&& other) noexcept
    : checkout_(std::move(other.checkout_)),
      command_environment_(std::move(other.command_environment_)),
      empty_value_policy_(other.empty_value_policy_),
      checkout_descriptor_(std::exchange(other.checkout_descriptor_, -1)),
      checkout_device_(other.checkout_device_),
      checkout_inode_(other.checkout_inode_),
      provenance_(std::move(other.provenance_)),
      workspace_path_(std::move(other.workspace_path_)),
      workspace_device_(other.workspace_device_),
      workspace_inode_(other.workspace_inode_) {
}

ArtifactMakepkgContext::~ArtifactMakepkgContext() noexcept {
    if(checkout_descriptor_ >= 0) close(checkout_descriptor_);
}

void ArtifactMakepkgContext::require_unchanged_checkout() const {
    if(checkout_descriptor_ < 0) {
        throw std::runtime_error("Artifact makepkg checkout is no longer owned.");
    }

    ValidatedCachePath current_checkout = revalidate_trusted_cache_path(
            checkout_, CachePathRequirement::ExistingDirectory);
    struct stat retained_status = require_descriptor_status(
            checkout_descriptor_, "retained source checkout " +
                                          checkout_.canonical_path().string());
    DirectoryIdentity expected_checkout{checkout_device_, checkout_inode_};
    if(!same_directory_identity(expected_checkout, retained_status)) {
        throw std::runtime_error(
                "Retained source checkout changed identity: " +
                checkout_.canonical_path().string());
    }

    struct stat named_status {};
    if(fstatat(
               AT_FDCWD, current_checkout.canonical_path().c_str(),
               &named_status, AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_directory_identity(expected_checkout, named_status)) {
        throw std::runtime_error(
                "Source checkout path changed identity: " +
                checkout_.canonical_path().string());
    }
}

void ArtifactMakepkgContext::require_matching_workspace(
        const ArtifactWorkspace& workspace) const {
    workspace.require_unchanged_identity();
    if(workspace.path_ != workspace_path_ ||
       workspace.device_ != workspace_device_ ||
       workspace.inode_ != workspace_inode_) {
        throw std::runtime_error(
                "Artifact makepkg context does not match the supplied workspace.");
    }
}

std::string ArtifactMakepkgContext::command_environment_prefix() const {
    return serialize_source_build_environment(
            command_environment_, empty_value_policy_);
}

std::string ArtifactMakepkgContext::makepkg_command(
        const std::vector<std::string>& makepkg_arguments) const {
    std::vector<std::string> arguments;
    arguments.reserve(makepkg_arguments.size() + 1);
    arguments.push_back("makepkg");
    arguments.insert(
            arguments.end(), makepkg_arguments.begin(),
            makepkg_arguments.end());
    return command_environment_prefix() + shell_words::join(arguments);
}

CapturedCommandResult ArtifactMakepkgContext::capture_makepkg_output(
        const ArtifactWorkspace& workspace,
        const std::vector<std::string>& makepkg_arguments) const {
    require_no_inherited_pkgdest();
    require_matching_workspace(workspace);
    require_unchanged_checkout();
    FileDescriptorWorkDirGuard working_directory(checkout_descriptor_);
    std::string command = makepkg_command(makepkg_arguments);
    Logger::raw_cmd(command);
    CapturedCommandResult result = capture_command_output_raw(command.c_str());
    require_unchanged_checkout();
    require_matching_workspace(workspace);
    return result;
}

int ArtifactMakepkgContext::run_makepkg_build_only(
        const ArtifactWorkspace& workspace,
        const ExpectedPackageArtifactPath& expected) const {
    require_no_inherited_pkgdest();
    require_matching_workspace(workspace);
    expected.require_matching_makepkg_context(*this);
    require_unchanged_checkout();
    FileDescriptorWorkDirGuard working_directory(checkout_descriptor_);
    int exit_code = run_command(makepkg_command({"-sc"}));
    require_unchanged_checkout();
    require_matching_workspace(workspace);
    return exit_code;
}

ArtifactMakepkgContext prepare_artifact_makepkg_context(
        const ValidatedCachePath& checkout,
        const ArtifactWorkspace& workspace,
        const SourceBuildEnvironment& environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy) {
    require_unclaimed_artifact_pkgdest(environment);
    workspace.require_unchanged_identity();
    if(!workspace.path().is_absolute()) {
        throw std::logic_error("Artifact workspace path must be absolute.");
    }

    ValidatedCachePath current_checkout = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    int checkout_descriptor = open(
            current_checkout.canonical_path().c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(checkout_descriptor < 0) {
        throw std::runtime_error(
                "Failed to retain source checkout " +
                current_checkout.canonical_path().string() + ": " +
                std::strerror(errno));
    }
    OwnedFileDescriptor opened_checkout(checkout_descriptor);
    struct stat checkout_status = require_descriptor_status(
            opened_checkout.get(), "source checkout " +
                                           current_checkout.canonical_path().string());
    struct stat named_checkout_status {};
    if(!S_ISDIR(checkout_status.st_mode) ||
       fstatat(
               AT_FDCWD, current_checkout.canonical_path().c_str(),
               &named_checkout_status, AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_filesystem_identity(checkout_status, named_checkout_status)) {
        throw std::runtime_error(
                "Source checkout changed while preparing artifact makepkg context: " +
                current_checkout.canonical_path().string());
    }
    SourceBuildEnvironment command_environment = environment;
    command_environment.ordered_assignments.push_back(
            SourceEnvironmentAssignment{
                    "PKGDEST", workspace.canonical_path().string()});
    ArtifactMakepkgContext context(
            std::move(current_checkout), std::move(command_environment),
            empty_value_policy, opened_checkout.get(),
            status_device(checkout_status), status_inode(checkout_status),
            workspace.canonical_path(), workspace.device_, workspace.inode_);
    static_cast<void>(opened_checkout.release());
    return context;
}

ExpectedPackageArtifactPath::ExpectedPackageArtifactPath(
        fs::path path, std::string leaf_name,
        std::uintmax_t workspace_device, std::uintmax_t workspace_inode)
    : path_(std::move(path)), leaf_name_(std::move(leaf_name)),
      workspace_device_(workspace_device), workspace_inode_(workspace_inode) {
}

void ExpectedPackageArtifactPath::bind_makepkg_context(
        const ArtifactMakepkgContext& context) {
    if(makepkg_context_bound_) {
        throw std::logic_error(
                "Expected artifact is already bound to a makepkg context.");
    }
    makepkg_context_provenance_ = context.provenance_;
    makepkg_context_bound_ = true;
}

void ExpectedPackageArtifactPath::require_matching_makepkg_context(
        const ArtifactMakepkgContext& context) const {
    if(!makepkg_context_bound_ ||
       workspace_device_ != context.workspace_device_ ||
       workspace_inode_ != context.workspace_inode_ ||
       !makepkg_context_provenance_ ||
       makepkg_context_provenance_ != context.provenance_) {
        throw std::runtime_error(
                "Expected artifact does not belong to this makepkg context.");
    }
}

ExpectedPackageArtifactPath validate_makepkg_packagelist_output(
        const ArtifactWorkspace& workspace, const std::string& raw_output) {
    workspace.require_unchanged_identity();
    std::vector<std::string> records = parse_packagelist_records(raw_output);
    if(records.size() != 1) {
        throw std::runtime_error(
                "Expected exactly one makepkg --packagelist artifact path, got " +
                std::to_string(records.size()) + ".");
    }

    fs::path candidate(records.front());
    require_direct_expected_path(workspace, candidate);
    std::string leaf_name = candidate.filename().string();
    if(entry_status_at(
               workspace.directory_descriptor_, leaf_name, candidate)
               .has_value()) {
        throw std::runtime_error(
                "Expected package artifact already exists before build.");
    }

    std::string signature_leaf = leaf_name + ".sig";
    fs::path signature_path = workspace.canonical_path_ / signature_leaf;
    if(entry_status_at(
               workspace.directory_descriptor_, signature_leaf,
               signature_path)
               .has_value()) {
        throw std::runtime_error(
                "Expected package signature already exists before build.");
    }
    workspace.require_unchanged_identity();
    return ExpectedPackageArtifactPath(
            std::move(candidate), std::move(leaf_name), workspace.device_,
            workspace.inode_);
}

ExpectedPackageArtifactPath query_makepkg_packagelist(
        const ArtifactWorkspace& workspace,
        const ArtifactMakepkgContext& context) {
    CapturedCommandResult result = context.capture_makepkg_output(
            workspace, {"--packagelist"});
    if(result.exit_code != 0) {
        throw std::runtime_error(
                "makepkg --packagelist failed with exit status " +
                std::to_string(result.exit_code) + ".");
    }
    ExpectedPackageArtifactPath expected =
            validate_makepkg_packagelist_output(workspace, result.output);
    expected.bind_makepkg_context(context);
    return expected;
}

ValidatedPackageArtifactPath::ValidatedPackageArtifactPath(
        ArtifactWorkspace&& workspace, fs::path path, std::string leaf_name,
        int artifact_descriptor, std::uintmax_t device,
        std::uintmax_t inode, std::uintmax_t owner) noexcept
    : workspace_(std::move(workspace)), path_(std::move(path)),
      leaf_name_(std::move(leaf_name)),
      artifact_descriptor_(artifact_descriptor), device_(device), inode_(inode),
      owner_(owner) {
}

ValidatedPackageArtifactPath::ValidatedPackageArtifactPath(
        ValidatedPackageArtifactPath&& other) noexcept
    : workspace_(std::move(other.workspace_)), path_(std::move(other.path_)),
      leaf_name_(std::move(other.leaf_name_)),
      artifact_descriptor_(std::exchange(other.artifact_descriptor_, -1)),
      device_(other.device_), inode_(other.inode_), owner_(other.owner_) {
}

ValidatedPackageArtifactPath::~ValidatedPackageArtifactPath() noexcept {
    if(artifact_descriptor_ >= 0) close(artifact_descriptor_);
}

ValidatedPackageArtifactPath ValidatedPackageArtifactPath::validate_for_owners(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactPath& expected,
        std::uintmax_t expected_workspace_owner,
        std::uintmax_t expected_artifact_owner) {
    workspace.require_unchanged_identity_for_owner(expected_workspace_owner);
    if(expected.workspace_device_ != workspace.device_ ||
       expected.workspace_inode_ != workspace.inode_ ||
       expected.path_.parent_path() != workspace.canonical_path_ ||
       expected.path_.filename().string() != expected.leaf_name_) {
        throw std::runtime_error(
                "Expected artifact path does not belong to this artifact workspace.");
    }

    InspectedArtifact inspected = inspect_post_build_artifact(
            workspace.directory_descriptor_, workspace.canonical_path_,
            expected.leaf_name_, expected_artifact_owner);
    workspace.require_unchanged_identity_for_owner(expected_workspace_owner);

    fs::path artifact_path = expected.path_;
    std::string artifact_leaf = expected.leaf_name_;
    ValidatedPackageArtifactPath artifact(
            std::move(workspace), std::move(artifact_path),
            std::move(artifact_leaf), inspected.descriptor(),
            status_device(inspected.status), status_inode(inspected.status),
            status_owner(inspected.status));
    static_cast<void>(inspected.release_descriptor());
    return artifact;
}

void ValidatedPackageArtifactPath::require_validity_for_owner(
        std::uintmax_t expected_effective_user) const {
    workspace_.require_unchanged_identity_for_owner(expected_effective_user);
    if(artifact_descriptor_ < 0) {
        throw std::runtime_error(
                "Validated package artifact descriptor is closed: " +
                path_.string());
    }

    struct stat retained_status = require_descriptor_status(
            artifact_descriptor_, "validated package artifact " + path_.string());
    if(!S_ISREG(retained_status.st_mode) ||
       status_device(retained_status) != device_ ||
       status_inode(retained_status) != inode_ ||
       status_owner(retained_status) != owner_ ||
       owner_ != expected_effective_user) {
        throw std::runtime_error(
                "Validated package artifact descriptor changed identity or owner: " +
                path_.string());
    }

    InspectedArtifact inspected = inspect_post_build_artifact(
            workspace_.directory_descriptor_, workspace_.canonical_path_,
            leaf_name_, expected_effective_user);
    if(status_device(inspected.status) != device_ ||
       status_inode(inspected.status) != inode_ ||
       status_owner(inspected.status) != owner_) {
        throw std::runtime_error(
                "Validated package artifact path changed identity or owner: " +
                path_.string());
    }
    workspace_.require_unchanged_identity_for_owner(expected_effective_user);
}

void ValidatedPackageArtifactPath::require_validity() const {
    require_validity_for_owner(static_cast<std::uintmax_t>(geteuid()));
}

void ValidatedPackageArtifactPath::retain_workspace_for_diagnostics() noexcept {
    workspace_.retain_for_diagnostics();
}

void ValidatedPackageArtifactPath::cleanup_workspace() {
    workspace_.cleanup();
    if(artifact_descriptor_ >= 0) {
        close(artifact_descriptor_);
        artifact_descriptor_ = -1;
    }
}

ValidatedPackageArtifactPath validate_post_build_package_artifact(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactPath& expected) {
    const std::uintmax_t effective_user =
            static_cast<std::uintmax_t>(geteuid());
    return ValidatedPackageArtifactPath::validate_for_owners(
            std::move(workspace), expected,
            effective_user, effective_user);
}

#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
void require_artifact_workspace_identity_for_test(
        const ArtifactWorkspace& workspace,
        std::uintmax_t expected_effective_user) {
    workspace.require_unchanged_identity_for_owner(expected_effective_user);
}

ValidatedPackageArtifactPath validate_post_build_package_artifact_for_test(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactPath& expected,
        std::uintmax_t expected_artifact_owner) {
    return ValidatedPackageArtifactPath::validate_for_owners(
            std::move(workspace), expected,
            static_cast<std::uintmax_t>(geteuid()),
            expected_artifact_owner);
}
#endif
