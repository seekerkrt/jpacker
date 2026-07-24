#include "trusted_cache.hpp"

#include "logging.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

// trusted cache root / entryのcanonical containment、cwd ownership、failed-clone rollbackを所有する。
// POLICY(#197): persistent checkout descendant policyはconsumer側で検証する。
namespace {

namespace fs = std::filesystem;

struct ValidatedCacheRootState {
    fs::path path;
    fs::path canonical_path;
};

struct ValidatedCachePathState {
    fs::path path;
    fs::path canonical_path;
    bool     exists = false;
    bool     is_directory = false;
};

class OwnedCacheRootDescriptor final {
    int descriptor_ = -1;

public:
    explicit OwnedCacheRootDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {
    }

    OwnedCacheRootDescriptor(const OwnedCacheRootDescriptor&) = delete;
    OwnedCacheRootDescriptor& operator=(const OwnedCacheRootDescriptor&) = delete;

    OwnedCacheRootDescriptor(OwnedCacheRootDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }

    OwnedCacheRootDescriptor& operator=(OwnedCacheRootDescriptor&&) = delete;

    ~OwnedCacheRootDescriptor() noexcept {
        if(descriptor_ >= 0) close(descriptor_);
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

struct PreparedPrivateCacheRootState {
    ValidatedCacheRootState     trusted_state;
    std::optional<struct stat> created_status;
};

struct OpenedPrivateCacheRootState {
    OwnedCacheRootDescriptor descriptor;
    struct stat              status {};
};

std::uintmax_t cache_status_device(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_dev);
}

std::uintmax_t cache_status_inode(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_ino);
}

std::uintmax_t cache_status_owner(const struct stat& status) {
    return static_cast<std::uintmax_t>(status.st_uid);
}

bool same_cache_root_identity(
        const struct stat& expected, const struct stat& actual) {
    return S_ISDIR(expected.st_mode) && S_ISDIR(actual.st_mode) &&
           expected.st_dev == actual.st_dev && expected.st_ino == actual.st_ino;
}

bool has_private_cache_root_mode(const struct stat& status) {
    const bool is_group_or_world_writable =
            (status.st_mode & (S_IWGRP | S_IWOTH)) != 0;
    const bool has_sticky_bit = (status.st_mode & S_ISVTX) != 0;
    return !is_group_or_world_writable || has_sticky_bit;
}

void require_private_cache_root_status(
        const struct stat& status, const fs::path& root_path,
        std::uintmax_t expected_effective_user,
        bool require_new_root_mode) {
    if(!S_ISDIR(status.st_mode)) {
        throw std::runtime_error(
                "Private jpacker cache root must be a directory: " +
                root_path.string());
    }
    if(cache_status_owner(status) != expected_effective_user) {
        throw std::runtime_error(
                "Private jpacker cache root owner must match the effective user: " +
                root_path.string());
    }
    if(require_new_root_mode && (status.st_mode & 07777) != 0700) {
        throw std::runtime_error(
                "New private jpacker cache root must have mode 0700: " +
                root_path.string());
    }
    if(!has_private_cache_root_mode(status)) {
        throw std::runtime_error(
                "Private jpacker cache root is group/world writable without the "
                "sticky bit; set its mode to 0700 explicitly: " +
                root_path.string());
    }
}

fs::path get_cache_dir() {
    const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
    fs::path    base;
    if(xdg_cache && std::string(xdg_cache).length() > 0) {
        base = xdg_cache;
    } else {
        const char* home = std::getenv("HOME");
        if(!home) throw std::runtime_error("HOME environment variable not set.");
        base = fs::path(home) / ".cache";
    }
    return base / "jpacker";
}

fs::path absolute_cache_path(const fs::path& path) {
    std::error_code ec;
    fs::path        absolute_path = fs::absolute(path, ec);
    if(ec) {
        throw std::runtime_error(
                "Unable to resolve jpacker cache path " + path.string() + ": " + ec.message());
    }

    // LANDMINE(#175): lexical normalization before this check could erase a symlink/.. boundary.
    for(const auto& component : absolute_path.relative_path()) {
        if(component == "." || component == "..") {
            throw std::runtime_error(
                    "Unsafe jpacker cache path " + absolute_path.string() +
                    ": dot path components are not allowed.");
        }
    }
    return absolute_path.lexically_normal();
}

fs::file_status cache_symlink_status(const fs::path& component, const fs::path& target_path) {
    std::error_code ec;
    fs::file_status status = fs::symlink_status(component, ec);
    if(ec == std::errc::no_such_file_or_directory) {
        return fs::file_status(fs::file_type::not_found);
    }
    if(ec) {
        throw std::runtime_error(
                "Unable to inspect jpacker cache path " + target_path.string() + " at " +
                component.string() + ": " + ec.message());
    }
    return status;
}

void require_no_symlink_components(const fs::path& absolute_path, bool final_may_be_nondirectory) {
    fs::path current_path = absolute_path.root_path();
    fs::path relative_path = absolute_path.relative_path();
    auto     component = relative_path.begin();
    auto     end = relative_path.end();
    for(; component != end; ++component) {
        current_path /= *component;
        fs::file_status status = cache_symlink_status(current_path, absolute_path);
        if(fs::is_symlink(status)) {
            throw std::runtime_error(
                    "Unsafe jpacker cache path " + absolute_path.string() +
                    ": symlink component is not allowed: " + current_path.string());
        }

        auto next = component;
        ++next;
        bool is_final_component = next == end;
        if(fs::exists(status) && (!is_final_component || !final_may_be_nondirectory) &&
           !fs::is_directory(status)) {
            throw std::runtime_error(
                    "Unsafe jpacker cache path " + absolute_path.string() +
                    ": non-directory path component: " + current_path.string());
        }
    }
}

bool is_path_contained(const fs::path& root, const fs::path& candidate, bool allow_root) {
    auto root_component = root.begin();
    auto candidate_component = candidate.begin();
    for(; root_component != root.end(); ++root_component, ++candidate_component) {
        if(candidate_component == candidate.end() || *root_component != *candidate_component) return false;
    }
    return allow_root || candidate_component != candidate.end();
}

ValidatedCacheRootState validate_cache_root_path(const fs::path& raw_root, bool create_if_missing) {
    fs::path root_path = absolute_cache_path(raw_root);
    require_no_symlink_components(root_path, false);

    fs::file_status root_status = cache_symlink_status(root_path, root_path);
    if(!fs::exists(root_status)) {
        if(!create_if_missing) {
            throw std::runtime_error("Trusted jpacker cache root is missing: " + root_path.string());
        }

        std::error_code ec;
        fs::create_directories(root_path, ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to create jpacker cache root " + root_path.string() + ": " + ec.message());
        }

        // POLICY(#175): creation is followed by the same no-follow component check before trust is granted.
        require_no_symlink_components(root_path, false);
        root_status = cache_symlink_status(root_path, root_path);
    }
    if(!fs::is_directory(root_status)) {
        throw std::runtime_error(
                "Unsafe jpacker cache root " + root_path.string() + ": expected a regular directory.");
    }

    std::error_code ec;
    fs::path        canonical_root = fs::canonical(root_path, ec);
    if(ec) {
        throw std::runtime_error(
                "Failed to canonicalize jpacker cache root " + root_path.string() + ": " + ec.message());
    }
    return ValidatedCacheRootState{root_path, canonical_root};
}

PreparedPrivateCacheRootState prepare_private_cache_root_path() {
    fs::path root_path = absolute_cache_path(get_cache_dir());
    fs::path parent_path = root_path.parent_path();

    // POLICY(#242): legacy root factoryは触らず、private factoryだけがfinal
    // componentをmkdirat(0700)する。上位parentの作成は既存no-symlink契約で再検証する。
    require_no_symlink_components(parent_path, false);
    std::error_code parent_error;
    fs::create_directories(parent_path, parent_error);
    if(parent_error) {
        throw std::runtime_error(
                "Failed to create jpacker cache parent " +
                parent_path.string() + ": " + parent_error.message());
    }
    require_no_symlink_components(parent_path, false);
    fs::file_status parent_status = cache_symlink_status(parent_path, root_path);
    if(!fs::is_directory(parent_status)) {
        throw std::runtime_error(
                "Private jpacker cache root requires an existing directory parent: " +
                parent_path.string());
    }

    int parent_descriptor = open(
            parent_path.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(parent_descriptor < 0) {
        throw std::runtime_error(
                "Failed to retain private jpacker cache parent " +
                parent_path.string() + ": " + std::strerror(errno));
    }
    OwnedCacheRootDescriptor opened_parent(parent_descriptor);

    struct stat opened_parent_status {};
    struct stat named_parent_status {};
    if(fstat(opened_parent.get(), &opened_parent_status) != 0 ||
       fstatat(
               AT_FDCWD, parent_path.c_str(), &named_parent_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_cache_root_identity(opened_parent_status, named_parent_status)) {
        throw std::runtime_error(
                "Private jpacker cache parent changed during validation: " +
                parent_path.string());
    }

    const std::string root_leaf = root_path.filename().string();
    if(root_leaf.empty()) {
        throw std::logic_error(
                "Private jpacker cache root must have a final path component.");
    }

    std::optional<struct stat> created_status;
    struct stat                initial_status {};
    if(fstatat(
               opened_parent.get(), root_leaf.c_str(), &initial_status,
               AT_SYMLINK_NOFOLLOW) != 0) {
        if(errno != ENOENT) {
            throw std::runtime_error(
                    "Failed to inspect private jpacker cache root " +
                    root_path.string() + ": " + std::strerror(errno));
        }

        if(mkdirat(opened_parent.get(), root_leaf.c_str(), 0700) == 0) {
            struct stat created_candidate {};
            if(fstatat(
                       opened_parent.get(), root_leaf.c_str(),
                       &created_candidate, AT_SYMLINK_NOFOLLOW) != 0) {
                // POLICY(#242): identityを証明できないpersistent root候補は削除しない。
                throw std::runtime_error(
                        "Failed to inspect newly created private jpacker cache root " +
                        root_path.string() + ": " + std::strerror(errno));
            }
            created_status = created_candidate;
        } else if(errno != EEXIST) {
            throw std::runtime_error(
                    "Failed to create private jpacker cache root " +
                    root_path.string() + ": " + std::strerror(errno));
        }
    }

    // Creation raceのEEXISTもexisting pathとして、同じno-follow contractへ通す。
    ValidatedCacheRootState trusted_state =
            validate_cache_root_path(root_path, false);
    return PreparedPrivateCacheRootState{
            std::move(trusted_state), std::move(created_status)};
}

OpenedPrivateCacheRootState open_private_cache_root(
        const ValidatedCacheRoot& trusted_root,
        std::uintmax_t expected_effective_user,
        const std::optional<struct stat>& created_status = std::nullopt) {
    int root_descriptor = open(
            trusted_root.canonical_path().c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_descriptor < 0) {
        throw std::runtime_error(
                "Failed to retain private jpacker cache root " +
                trusted_root.canonical_path().string() + ": " +
                std::strerror(errno));
    }
    OwnedCacheRootDescriptor opened_root(root_descriptor);

    struct stat opened_status {};
    if(fstat(opened_root.get(), &opened_status) != 0) {
        throw std::runtime_error(
                "Failed to inspect retained private jpacker cache root " +
                trusted_root.canonical_path().string() + ": " +
                std::strerror(errno));
    }
    require_private_cache_root_status(
            opened_status, trusted_root.canonical_path(),
            expected_effective_user, created_status.has_value());

    struct stat named_status {};
    if(fstatat(
               AT_FDCWD, trusted_root.canonical_path().c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_cache_root_identity(opened_status, named_status)) {
        throw std::runtime_error(
                "Private jpacker cache root path changed identity: " +
                trusted_root.canonical_path().string());
    }
    require_private_cache_root_status(
            named_status, trusted_root.canonical_path(),
            expected_effective_user, created_status.has_value());

    if(created_status.has_value() &&
       !same_cache_root_identity(created_status.value(), opened_status)) {
        // mkdiratで作成したcandidateとretained FDが一致しなければ、replacementを所有しない。
        throw std::runtime_error(
                "New private jpacker cache root changed identity before validation: " +
                trusted_root.canonical_path().string());
    }

    return OpenedPrivateCacheRootState{
            std::move(opened_root), opened_status};
}

fs::path cache_path_for_root(const ValidatedCacheRoot& root, const fs::path& path) {
    if(path.is_absolute()) return path;
    return root.path() / path;
}

ValidatedCachePathState validate_cache_path_against_root(
        const ValidatedCacheRoot& root, const fs::path& raw_path,
        CachePathRequirement requirement) {
    fs::path path = absolute_cache_path(raw_path);
    require_no_symlink_components(path, true);

    fs::file_status status = cache_symlink_status(path, path);
    bool            path_exists = fs::exists(status);
    bool            path_is_directory = path_exists && fs::is_directory(status);

    if(requirement == CachePathRequirement::Existing && !path_exists) {
        throw std::runtime_error("Trusted jpacker cache path is missing: " + path.string());
    }
    if(requirement == CachePathRequirement::ExistingDirectory && !path_is_directory) {
        throw std::runtime_error(
                "Unsafe jpacker cache path " + path.string() + ": expected an existing directory.");
    }
    if(requirement == CachePathRequirement::Missing && path_exists) {
        throw std::runtime_error(
                "Unsafe jpacker cache path " + path.string() + ": expected the path to be missing.");
    }

    fs::path resolved_path;
    if(path_exists) {
        std::error_code ec;
        resolved_path = fs::canonical(path, ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to canonicalize jpacker cache path " + path.string() + ": " + ec.message());
        }
    } else {
        fs::path parent_path = path.parent_path();
        require_no_symlink_components(parent_path, false);
        fs::file_status parent_status = cache_symlink_status(parent_path, path);
        if(!fs::is_directory(parent_status)) {
            throw std::runtime_error(
                    "Unsafe jpacker cache path " + path.string() +
                    ": existing parent directory is required before creation.");
        }

        std::error_code ec;
        fs::path        canonical_parent = fs::canonical(parent_path, ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to canonicalize jpacker cache parent " + parent_path.string() + ": " + ec.message());
        }
        if(!is_path_contained(root.canonical_path(), canonical_parent, true)) {
            throw std::runtime_error(
                    "Unsafe jpacker cache path " + path.string() +
                    ": parent resolves outside trusted cache root " + root.canonical_path().string());
        }

        // POLICY(#175): weakly_canonical is used only for a missing leaf whose existing parent was verified above.
        resolved_path = fs::weakly_canonical(path, ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to resolve missing jpacker cache path " + path.string() + ": " + ec.message());
        }
    }

    // POLICY(#175): containment is path-component based; similarly prefixed sibling directories are not trusted.
    if(!is_path_contained(root.canonical_path(), resolved_path, false)) {
        throw std::runtime_error(
                "Unsafe jpacker cache path " + path.string() + ": canonical path " +
                resolved_path.string() + " is outside trusted cache root " +
                root.canonical_path().string());
    }

    return ValidatedCachePathState{path, resolved_path, path_exists, path_is_directory};
}

bool rollback_trusted_cache_path(
        const ValidatedCacheRoot& expected_root, const fs::path& expected_path,
        const ValidatedCachePath& expected_capability) {
    require_unchanged_cache_root(expected_root);
    fs::file_status status = cache_symlink_status(expected_path, expected_path);
    if(!fs::exists(status)) return false;

    remove_trusted_cache_path(expected_capability);
    return true;
}

fs::path change_working_directory(const fs::path& target_path) {
    fs::path original_path = fs::current_path();
    fs::current_path(target_path);
    return original_path;
}

} // namespace

ValidatedPrivateCacheRoot::ValidatedPrivateCacheRoot(
        ValidatedCacheRoot trusted_root, int directory_descriptor,
        std::uintmax_t device, std::uintmax_t inode,
        std::uintmax_t owner) noexcept
    : trusted_root_(std::move(trusted_root)),
      directory_descriptor_(directory_descriptor), device_(device), inode_(inode),
      owner_(owner) {
}

ValidatedPrivateCacheRoot::ValidatedPrivateCacheRoot(
        ValidatedPrivateCacheRoot&& other) noexcept
    : trusted_root_(std::move(other.trusted_root_)),
      directory_descriptor_(std::exchange(other.directory_descriptor_, -1)),
      device_(other.device_), inode_(other.inode_), owner_(other.owner_) {
}

ValidatedPrivateCacheRoot::~ValidatedPrivateCacheRoot() noexcept {
    if(directory_descriptor_ >= 0) close(directory_descriptor_);
}

void ValidatedPrivateCacheRoot::require_unchanged_identity_for_owner(
        std::uintmax_t expected_effective_user) const {
    if(directory_descriptor_ < 0) {
        throw std::runtime_error(
                "Private jpacker cache root is no longer owned.");
    }

    ValidatedCacheRoot current_root =
            require_unchanged_cache_root(trusted_root_);
    struct stat retained_status {};
    if(fstat(directory_descriptor_, &retained_status) != 0) {
        throw std::runtime_error(
                "Failed to inspect retained private jpacker cache root " +
                current_root.canonical_path().string() + ": " +
                std::strerror(errno));
    }
    if(!S_ISDIR(retained_status.st_mode) ||
       cache_status_device(retained_status) != device_ ||
       cache_status_inode(retained_status) != inode_ ||
       cache_status_owner(retained_status) != owner_ ||
       owner_ != expected_effective_user) {
        throw std::runtime_error(
                "Retained private jpacker cache root changed identity or owner: " +
                current_root.canonical_path().string());
    }
    require_private_cache_root_status(
            retained_status, current_root.canonical_path(),
            expected_effective_user, false);

    struct stat named_status {};
    if(fstatat(
               AT_FDCWD, current_root.canonical_path().c_str(), &named_status,
               AT_SYMLINK_NOFOLLOW) != 0 ||
       !same_cache_root_identity(retained_status, named_status) ||
       cache_status_device(named_status) != device_ ||
       cache_status_inode(named_status) != inode_ ||
       cache_status_owner(named_status) != owner_) {
        throw std::runtime_error(
                "Private jpacker cache root path changed identity or owner: " +
                current_root.canonical_path().string());
    }
    require_private_cache_root_status(
            named_status, current_root.canonical_path(),
            expected_effective_user, false);
}

void ValidatedPrivateCacheRoot::require_unchanged_identity() const {
    require_unchanged_identity_for_owner(
            static_cast<std::uintmax_t>(geteuid()));
}

ValidatedPrivateCacheRoot prepare_private_trusted_cache_root() {
    PreparedPrivateCacheRootState prepared = prepare_private_cache_root_path();
    ValidatedCacheRoot trusted_root(
            std::move(prepared.trusted_state.path),
            std::move(prepared.trusted_state.canonical_path));
    OpenedPrivateCacheRootState opened = open_private_cache_root(
            trusted_root, static_cast<std::uintmax_t>(geteuid()),
            prepared.created_status);

    ValidatedPrivateCacheRoot private_root(
            std::move(trusted_root), opened.descriptor.get(),
            cache_status_device(opened.status),
            cache_status_inode(opened.status),
            cache_status_owner(opened.status));
    static_cast<void>(opened.descriptor.release());
    return private_root;
}

#ifdef JPACKER_ENABLE_TRUSTED_CACHE_TEST_HOOKS
void require_private_cache_root_identity_for_test(
        const ValidatedPrivateCacheRoot& root,
        std::uintmax_t expected_effective_user) {
    root.require_unchanged_identity_for_owner(expected_effective_user);
}
#endif

ValidatedCacheRoot prepare_trusted_cache_root() {
    ValidatedCacheRootState root = validate_cache_root_path(get_cache_dir(), true);
    return ValidatedCacheRoot(std::move(root.path), std::move(root.canonical_path));
}

ValidatedCacheRoot require_unchanged_cache_root(const ValidatedCacheRoot& expected_root) {
    ValidatedCacheRootState current_root = validate_cache_root_path(expected_root.path(), false);
    if(current_root.canonical_path != expected_root.canonical_path()) {
        throw std::runtime_error(
                "Unsafe jpacker cache root " + expected_root.path().string() +
                ": canonical path changed after validation.");
    }
    return ValidatedCacheRoot(std::move(current_root.path), std::move(current_root.canonical_path));
}

ValidatedCachePath require_trusted_cache_path(
        const ValidatedCacheRoot& root, const fs::path& path,
        CachePathRequirement requirement) {
    ValidatedCacheRoot      current_root = require_unchanged_cache_root(root);
    ValidatedCachePathState current_path = validate_cache_path_against_root(
            current_root, cache_path_for_root(current_root, path), requirement);
    return ValidatedCachePath(
            std::move(current_root), std::move(current_path.path),
            std::move(current_path.canonical_path), current_path.exists,
            current_path.is_directory);
}

ValidatedCachePath revalidate_trusted_cache_path(
        const ValidatedCachePath& expected_path, CachePathRequirement requirement) {
    ValidatedCachePath current_path = require_trusted_cache_path(
            expected_path.root_, expected_path.path_, requirement);
    if(current_path.canonical_path() != expected_path.canonical_path_) {
        throw std::runtime_error(
                "Unsafe jpacker cache path " + expected_path.path_.string() +
                ": canonical path changed after validation.");
    }
    return current_path;
}

void remove_trusted_cache_path(const ValidatedCachePath& expected_path) {
    ValidatedCachePath current_path =
            revalidate_trusted_cache_path(expected_path, CachePathRequirement::Existing);
    std::error_code ec;
    fs::remove_all(current_path.canonical_path(), ec);
    if(ec) {
        throw std::runtime_error(
                "Failed to remove trusted jpacker cache path " + current_path.path().string() +
                ": " + ec.message());
    }
}

std::vector<ValidatedCachePath> preflight_cache_cleanup(
        const ValidatedCacheRoot& expected_root) {
    ValidatedCacheRoot              root = require_unchanged_cache_root(expected_root);
    std::vector<ValidatedCachePath> targets;
    for(const auto& entry : fs::directory_iterator(root.canonical_path())) {
        fs::path filename = entry.path().filename();
        if(filename == "jpacker.log") continue;
        targets.push_back(require_trusted_cache_path(
                root, filename, CachePathRequirement::Existing));
    }
    return targets;
}

WorkDirGuard::WorkDirGuard(const ValidatedCacheRoot& target)
    : original_path_(change_working_directory(
              require_unchanged_cache_root(target).canonical_path())) {
}

WorkDirGuard::WorkDirGuard(const ValidatedCachePath& target)
    : original_path_(change_working_directory(
              revalidate_trusted_cache_path(
                      target, CachePathRequirement::ExistingDirectory)
                      .canonical_path())) {
}

WorkDirGuard::~WorkDirGuard() {
    try {
        if(fs::exists(original_path_)) fs::current_path(original_path_);
    } catch(...) {
    }
}

DirCleanupGuard::DirCleanupGuard(const ValidatedCachePath& path) : path_(path) {
}

void DirCleanupGuard::commit() {
    committed_ = true;
}

DirCleanupGuard::~DirCleanupGuard() {
    if(committed_) return;

    // LANDMINE(#175): clone前に検証したpathを再検証できた場合だけrollbackする。
    try {
        if(rollback_trusted_cache_path(path_.root_, path_.path_, path_)) {
            Logger::warn("Rolled back failed clone: cleaned up " + path_.path_.string());
        }
    } catch(const std::exception& e) {
        Logger::warn("Refusing unsafe clone rollback for " + path_.path_.string() + ": " + e.what());
    } catch(...) {
        Logger::warn("Refusing unsafe clone rollback for " + path_.path_.string() + ": unknown error");
    }
}
