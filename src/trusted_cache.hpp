#pragma once

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

class ArtifactWorkspace;
class ValidatedPrivateCacheRoot;

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
    friend ValidatedPrivateCacheRoot prepare_private_trusted_cache_root();

public:
    const std::filesystem::path& path() const {
        return path_;
    }
    const std::filesystem::path& canonical_path() const {
        return canonical_path_;
    }
};

// artifact workspace等、named childを別uidのrename/unlinkから守る必要がある
// consumer専用のprivate cache root capability。
// POLICY(#242): legacy ValidatedCacheRootを暗黙upgradeせず、専用factoryだけが
// owner/mode/retained FD identityを証明して生成する。
class ValidatedPrivateCacheRoot final {
    ValidatedCacheRoot trusted_root_;
    int                directory_descriptor_ = -1;
    std::uintmax_t     device_ = 0;
    std::uintmax_t     inode_ = 0;
    std::uintmax_t     owner_ = 0;

    ValidatedPrivateCacheRoot(
            ValidatedCacheRoot trusted_root, int directory_descriptor,
            std::uintmax_t device, std::uintmax_t inode,
            std::uintmax_t owner) noexcept;

    void require_unchanged_identity_for_owner(
            std::uintmax_t expected_effective_user) const;

    // Retained FDはworkspace implementationだけがborrowし、一般consumerへは
    // raw descriptorを公開しない。
    int directory_descriptor() const noexcept {
        return directory_descriptor_;
    }

    friend ValidatedPrivateCacheRoot prepare_private_trusted_cache_root();
    friend class ArtifactWorkspace;
    friend ArtifactWorkspace create_artifact_workspace(
            ValidatedPrivateCacheRoot root);
#ifdef MOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS
    friend void require_private_cache_root_identity_for_test(
            const ValidatedPrivateCacheRoot& root,
            std::uintmax_t expected_effective_user);
#endif

public:
    ValidatedPrivateCacheRoot(const ValidatedPrivateCacheRoot&) = delete;
    ValidatedPrivateCacheRoot& operator=(
            const ValidatedPrivateCacheRoot&) = delete;
    ValidatedPrivateCacheRoot(ValidatedPrivateCacheRoot&& other) noexcept;
    ValidatedPrivateCacheRoot& operator=(ValidatedPrivateCacheRoot&&) = delete;
    ~ValidatedPrivateCacheRoot() noexcept;

    const std::filesystem::path& path() const noexcept {
        return trusted_root_.path();
    }

    const std::filesystem::path& canonical_path() const noexcept {
        return trusted_root_.canonical_path();
    }

    std::uintmax_t device() const noexcept {
        return device_;
    }

    std::uintmax_t inode() const noexcept {
        return inode_;
    }

    std::uintmax_t owner() const noexcept {
        return owner_;
    }

    void require_unchanged_identity() const;
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

// Missing final rootはmkdirat(..., 0700)で作り、existing rootは黙ってchmodしない。
// group/world writable rootはsticky bitがentry renameを防ぐ場合だけ許容する。
ValidatedPrivateCacheRoot prepare_private_trusted_cache_root();

#ifdef MOGUET_ENABLE_TRUSTED_CACHE_TEST_HOOKS
void require_private_cache_root_identity_for_test(
        const ValidatedPrivateCacheRoot& root,
        std::uintmax_t expected_effective_user);
#endif

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
    ~DirCleanupGuard() noexcept;
};
