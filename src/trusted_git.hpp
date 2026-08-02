#pragma once

#include "trusted_cache.hpp"

#include <filesystem>
#include <string>

// Moguet-owned persistent checkoutで許可するGit operationだけを公開する。
// Filesystem mutation authorityはValidatedCachePath側に残し、Gitへ渡すpathは
// explicit repository/worktree binding用のlogical viewとしてのみ使用する。
std::string trusted_git_remote_origin_url(
        const ValidatedCachePath& checkout);
int trusted_git_fetch_origin(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url);
std::string trusted_git_detect_remote_branch(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url);
int trusted_git_diff_quiet(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
std::string trusted_git_diff_name_only(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_show_diff(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_reset_hard(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_clone_persistent_checkout(
        const ValidatedCachePath& destination,
        const std::string& remote_url);

// PKGBUILD exportはcache checkoutとは別のdescriptor-anchored temporary
// lifecycle。Git process isolationだけを共有し、cache capabilityへは昇格しない。
int trusted_git_clone_aur_export(
        const std::string& remote_url,
        const std::filesystem::path& anchored_destination);
std::string trusted_git_aur_export_remote_origin_url(
        const std::filesystem::path& anchored_checkout);
