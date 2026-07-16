#include "persistent_checkout.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// Persistent trusted cache checkout固有のdescendant policyを所有する。
// POLICY(#197): containment / cwd / remove / rollbackはtrusted_cacheの責務として再実装しない。
namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool has_safe_git_directory(const fs::path& checkout_dir) {
    fs::path        git_path = checkout_dir / ".git";
    std::error_code ec;
    fs::file_status status = fs::symlink_status(git_path, ec);
    if(ec == std::errc::no_such_file_or_directory) return false;
    if(ec) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": non-directory .git (inspection failed: " + ec.message() + ").");
    }
    if(status.type() == fs::file_type::not_found) return false;
    if(fs::is_symlink(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() + ": symlink.");
    }
    if(fs::is_regular_file(status)) {
        // POLICY(#197): persistent checkoutではgitfile/worktree redirectを対応対象にしない。
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": gitfile / redirect.");
    }
    if(!fs::is_directory(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + git_path.string() +
                ": non-directory .git.");
    }
    return true;
}

void require_safe_git_directory(const fs::path& checkout_dir) {
    if(has_safe_git_directory(checkout_dir)) return;

    fs::path git_path = checkout_dir / ".git";
    throw std::runtime_error(
            "Unsafe persistent checkout descendant " + git_path.string() +
            ": non-directory .git.");
}

void require_safe_artifact(const fs::path& artifact_path) {
    std::error_code ec;
    fs::file_status status = fs::symlink_status(artifact_path, ec);
    if(ec == std::errc::no_such_file_or_directory) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file.");
    }
    if(ec) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file (inspection failed: " + ec.message() + ").");
    }
    if(fs::is_symlink(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() + ": symlink.");
    }
    if(!fs::is_regular_file(status)) {
        throw std::runtime_error(
                "Unsafe persistent checkout descendant " + artifact_path.string() +
                ": non-regular file.");
    }
}

std::vector<fs::path> find_install_scripts(const fs::path& checkout_dir) {
    std::vector<fs::path> scripts;
    std::error_code       ec;
    fs::directory_iterator entry(checkout_dir, ec);
    if(ec) {
        throw std::runtime_error(
                "Failed to inspect persistent checkout artifacts in " + checkout_dir.string() + ": " +
                ec.message());
    }

    const fs::directory_iterator end;
    while(entry != end) {
        fs::path artifact_path = entry->path();
        // POLICY(#197): 名前を先に対象化し、symlinkやspecial fileも検証対象から落とさない。
        if(artifact_path.extension() == ".install") {
            require_safe_artifact(artifact_path);
            scripts.push_back(artifact_path.filename());
        }

        entry.increment(ec);
        if(ec) {
            throw std::runtime_error(
                    "Failed to inspect persistent checkout artifacts in " + checkout_dir.string() + ": " +
                    ec.message());
        }
    }
    std::sort(scripts.begin(), scripts.end());
    return scripts;
}

} // namespace

bool has_safe_persistent_checkout_git_directory(const ValidatedCachePath& checkout) {
    return has_safe_git_directory(checkout.canonical_path());
}

std::vector<std::filesystem::path> require_safe_persistent_checkout_descendants(
        const ValidatedCachePath& checkout) {
    const fs::path& checkout_dir = checkout.canonical_path();
    require_safe_git_directory(checkout_dir);
    require_safe_artifact(checkout_dir / "PKGBUILD");
    return find_install_scripts(checkout_dir);
}

void require_safe_persistent_checkout_review_targets(
        const ValidatedCachePath& checkout,
        const std::vector<std::filesystem::path>& install_scripts) {
    // LANDMINE(#197): 再列挙だけでは、review開始後に消えた既存targetを見落とす。
    require_safe_persistent_checkout_descendants(checkout);
    for(const auto& install_script : install_scripts) {
        require_safe_artifact(checkout.canonical_path() / install_script);
    }
}

bool remote_url_matches_expected(
        const std::string& current_url, const std::string& expected_url) {
    // LANDMINE: cache directoryの再利用可否を決めるguard。曖昧一致にすると別remoteを上書きし得る。
    return trim(current_url) == trim(expected_url);
}
