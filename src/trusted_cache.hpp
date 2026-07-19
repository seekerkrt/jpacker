#pragma once

#include <filesystem>
#include <utility>
#include <vector>

// trusted cache root / entry の検証条件。
enum class CachePathRequirement {
    Existing,
    ExistingDirectory,
    Missing,
    ExistingOrMissing,
};

// validation済みのcache rootだけを表すcapability。
// POLICY(#175): raw pathからの生成はfactoryに限定し、consumerにはread-only viewだけを渡す。
class ValidatedCacheRoot final {
    std::filesystem::path path_;
    std::filesystem::path canonical_path_;

    ValidatedCacheRoot(std::filesystem::path path, std::filesystem::path canonical_path)
        : path_(std::move(path)), canonical_path_(std::move(canonical_path)) {
    }

    friend ValidatedCacheRoot prepare_trusted_cache_root();
    friend ValidatedCacheRoot require_unchanged_cache_root(const ValidatedCacheRoot& root);

public:
    const std::filesystem::path& path() const {
        return path_;
    }
    const std::filesystem::path& canonical_path() const {
        return canonical_path_;
    }
};

// validation済みのrootとchild pathのownershipを一体で保持するcapability。
class ValidatedCachePath final {
    ValidatedCacheRoot   root_;
    std::filesystem::path path_;
    std::filesystem::path canonical_path_;
    bool                  exists_ = false;
    bool                  is_directory_ = false;

    ValidatedCachePath(
            ValidatedCacheRoot root, std::filesystem::path path,
            std::filesystem::path canonical_path, bool exists, bool is_directory)
        : root_(std::move(root)), path_(std::move(path)),
          canonical_path_(std::move(canonical_path)), exists_(exists),
          is_directory_(is_directory) {
    }

    friend ValidatedCachePath require_trusted_cache_path(
            const ValidatedCacheRoot& root, const std::filesystem::path& path,
            CachePathRequirement requirement);
    friend ValidatedCachePath revalidate_trusted_cache_path(
            const ValidatedCachePath& path, CachePathRequirement requirement);
    friend class DirCleanupGuard;

public:
    const std::filesystem::path& path() const {
        return path_;
    }
    const std::filesystem::path& canonical_path() const {
        return canonical_path_;
    }
    bool exists() const {
        return exists_;
    }
    bool is_directory() const {
        return is_directory_;
    }
};

ValidatedCacheRoot prepare_trusted_cache_root();
ValidatedCacheRoot require_unchanged_cache_root(const ValidatedCacheRoot& root);

// Relative pathはtrusted root配下へ組み立て、absolute pathは既存contractどおりそのまま検証する。
ValidatedCachePath require_trusted_cache_path(
        const ValidatedCacheRoot& root, const std::filesystem::path& path,
        CachePathRequirement requirement);
ValidatedCachePath revalidate_trusted_cache_path(
        const ValidatedCachePath& path, CachePathRequirement requirement);
void remove_trusted_cache_path(const ValidatedCachePath& path);
std::vector<ValidatedCachePath> preflight_cache_cleanup(const ValidatedCacheRoot& root);

// build/fetch中のcurrent directory変更をscope exitで元に戻すguard。
// POLICY(#175): trusted cache moduleが再検証したexisting directoryだけを受け取る。
class WorkDirGuard final {
    std::filesystem::path original_path_;

public:
    explicit WorkDirGuard(const ValidatedCacheRoot& target);
    explicit WorkDirGuard(const ValidatedCachePath& target);
    WorkDirGuard(const WorkDirGuard&) = delete;
    WorkDirGuard& operator=(const WorkDirGuard&) = delete;
    WorkDirGuard(WorkDirGuard&&) = delete;
    WorkDirGuard& operator=(WorkDirGuard&&) = delete;
    ~WorkDirGuard();
};

// failed cloneが所有するcache entryを、成功確定までrollback可能に保つguard。
// POLICY(#175): callerはclone直前にMissingとして再検証したcapabilityを渡す。
class DirCleanupGuard final {
    ValidatedCachePath path_;
    bool               committed_ = false;

public:
    explicit DirCleanupGuard(const ValidatedCachePath& path);
    DirCleanupGuard(const DirCleanupGuard&) = delete;
    DirCleanupGuard& operator=(const DirCleanupGuard&) = delete;
    DirCleanupGuard(DirCleanupGuard&&) = delete;
    DirCleanupGuard& operator=(DirCleanupGuard&&) = delete;
    void commit();
    ~DirCleanupGuard();
};
