#include "checkout_fetch.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "persistent_checkout.hpp"
#include "process.hpp"
#include "trusted_cache.hpp"

#include <stdexcept>
#include <string>

// fetch command向けのstrict checkout transactionだけを所有する。
// POLICY: build consumerのremove / reclone / reset / review / build判断はここへ持ち込まない。
namespace {

std::string trim(const std::string& str) {
    std::string::size_type first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    std::string::size_type last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string shell_quote(const std::string& str) {
    // POLICY: 外部コマンド引数は、validation 済みの値でも shell 境界では必ず quote する。
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

} // namespace

void fetch_persistent_checkout(
        const std::string& package_base,
        const std::string& expected_remote_url) {
    require_valid_package_name(package_base);
    ValidatedCacheRoot cache_root = prepare_trusted_cache_root();
    ValidatedCachePath repo_path = require_trusted_cache_path(
            cache_root, package_base,
            CachePathRequirement::ExistingOrMissing);

    if(repo_path.exists()) {
        if(!repo_path.is_directory()) {
            throw std::runtime_error(repo_path.path().string() + " exists but is not a directory.");
        }
        require_safe_persistent_checkout_descendants(repo_path);

        // POLICY: fetch command は既存 clone で git fetch まで。worktree update/pull/reset/build/install はしない。
        WorkDirGuard wd_repo(repo_path);
        std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, expected_remote_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ": " + current_url);
        }

        Logger::info("Fetching " + package_base + "...");
        repo_path = revalidate_trusted_cache_path(
                repo_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(repo_path);
        if(run_command("git fetch origin") != 0) throw std::runtime_error("Failed to fetch " + package_base + ".");
        return;
    }

    Logger::info("Cloning " + package_base + "...");
    ValidatedCachePath clone_path =
            revalidate_trusted_cache_path(repo_path, CachePathRequirement::Missing);
    WorkDirGuard    wd_cache(cache_root);
    DirCleanupGuard cleanup_guard(clone_path);
    if(run_command(
               "git clone " + shell_quote(expected_remote_url) + " " + shell_quote(package_base)) != 0) {
        throw std::runtime_error("Failed to clone " + package_base + ".");
    }

    // POLICY(#175): clone が生成した entry も、成功扱いする前に同じ cache boundary で検証する。
    ValidatedCachePath cloned_path = require_trusted_cache_path(
            cache_root, package_base, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(cloned_path);
    {
        WorkDirGuard wd_repo(cloned_path);
        std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
        if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + package_base + ".");
        if(!remote_url_matches_expected(current_url, expected_remote_url)) {
            throw std::runtime_error("Remote URL mismatch for " + package_base + ": " + current_url);
        }
    }
    cleanup_guard.commit();
}
