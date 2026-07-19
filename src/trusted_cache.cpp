#include "trusted_cache.hpp"

#include "logging.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
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
