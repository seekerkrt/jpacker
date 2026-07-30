#include "checkout_fetch.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "persistent_checkout.hpp"
#include "trusted_git.hpp"
#include "trusted_cache.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

// fetch command向けのstrict checkout transactionだけを所有する。
// POLICY: build consumerのremove / reclone / reset / review / build判断はここへ持ち込まない。
namespace {

std::string trim(const std::string& str) {
    std::string::size_type first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    std::string::size_type last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

class ScopedPrivateUmask final {
    mode_t previous_;

public:
    ScopedPrivateUmask() noexcept : previous_(umask(0077)) {
    }

    ScopedPrivateUmask(const ScopedPrivateUmask&) = delete;
    ScopedPrivateUmask& operator=(const ScopedPrivateUmask&) = delete;

    ~ScopedPrivateUmask() noexcept {
        static_cast<void>(umask(previous_));
    }
};

} // namespace

void fetch_persistent_checkout(
        const ValidatedCacheRoot& cache_root,
        const std::string& package_base,
        const std::string& expected_remote_url) {
    require_valid_package_name(package_base);
    ValidatedCachePath repo_path = require_trusted_cache_path(
            cache_root, package_base,
            CachePathRequirement::ExistingOrMissing);

    if(repo_path.exists()) {
        if(!repo_path.is_directory()) {
            throw TrustedCacheError(TrustedCacheFailure{
                    TrustedCacheStage::ChildValidation,
                    TrustedCacheErrorCode::NotDirectory,
                    std::nullopt});
        }
        require_safe_persistent_checkout_descendants(repo_path);

        // POLICY: fetch command は既存 clone で git fetch まで。worktree update/pull/reset/build/install はしない。
        std::string current_url;
        {
            WorkDirGuard wd_repo(repo_path);
            current_url = trim(trusted_git_remote_origin_url(repo_path));
        }
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, expected_remote_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ".");
        }

        Logger::info("Fetching " + package_base + "...");
        repo_path = revalidate_trusted_cache_path(
                repo_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(repo_path);
        WorkDirGuard wd_repo(repo_path);
        ScopedPrivateUmask private_umask;
        if(trusted_git_fetch_origin(repo_path, expected_remote_url) != 0) {
            throw std::runtime_error("Failed to fetch " + package_base + ".");
        }
        repo_path = revalidate_trusted_cache_path(
                repo_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(repo_path);
        return;
    }

    Logger::info("Cloning " + package_base + "...");
    revalidate_trusted_cache_path(repo_path, CachePathRequirement::Missing);
    ValidatedCachePath clone_path = create_trusted_cache_directory(
            cache_root, package_base);
    WorkDirGuard    wd_cache(cache_root);
    DirCleanupGuard cleanup_guard(clone_path);
    ScopedPrivateUmask private_umask;
    if(trusted_git_clone_persistent_checkout(
               clone_path, expected_remote_url) != 0) {
        throw std::runtime_error("Failed to clone " + package_base + ".");
    }

    // POLICY(#175): clone が生成した entry も、成功扱いする前に同じ cache boundary で検証する。
    ValidatedCachePath cloned_path = revalidate_trusted_cache_path(
            clone_path, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(cloned_path);
    {
        WorkDirGuard wd_repo(cloned_path);
        std::string current_url = trim(
                trusted_git_remote_origin_url(cloned_path));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, expected_remote_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ".");
        }
    }
    cloned_path = revalidate_trusted_cache_path(
            clone_path, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(cloned_path);
    cleanup_guard.commit();
}
