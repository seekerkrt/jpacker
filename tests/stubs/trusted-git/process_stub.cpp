#include "trusted_git.hpp"

#include "process.hpp"
#include "shell_words.hpp"

#include <filesystem>
#include <string>

namespace {

std::string trim(const std::string& value) {
    const std::string::size_type first =
            value.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    const std::string::size_type last =
            value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

std::string remote_ref(const std::string& branch) {
    return "origin/" + branch;
}

} // namespace

std::string trusted_git_remote_origin_url(
        const ValidatedCachePath&) {
    return trim(exec_command("git config --get remote.origin.url"));
}

int trusted_git_fetch_origin(
        const ValidatedCachePath&,
        const std::string&) {
    return run_command("git fetch origin");
}

std::string trusted_git_detect_remote_branch(
        const ValidatedCachePath&,
        const std::string&) {
    const std::string remote_head = trim(exec_command(
            "git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null"));
    constexpr const char* prefix = "origin/";
    if(remote_head.starts_with(prefix) &&
       remote_head.size() > std::string(prefix).size()) {
        return remote_head.substr(std::string(prefix).size());
    }
    if(command_status(
               "git show-ref --verify --quiet refs/remotes/origin/main") ==
       0) {
        return "main";
    }
    if(command_status(
               "git show-ref --verify --quiet refs/remotes/origin/master") ==
       0) {
        return "master";
    }
    return "master";
}

int trusted_git_diff_quiet(
        const ValidatedCachePath&,
        const std::string&,
        const std::string& branch) {
    return run_command(
            "git diff --quiet " +
            shell_words::quote("HEAD.." + remote_ref(branch)));
}

std::string trusted_git_diff_name_only(
        const ValidatedCachePath&,
        const std::string&,
        const std::string& branch) {
    const std::string command =
            "git diff --name-only " +
            shell_words::quote("HEAD.." + remote_ref(branch)) +
            " 2>/dev/null";
    return exec_command(command.c_str());
}

int trusted_git_show_diff(
        const ValidatedCachePath&,
        const std::string&,
        const std::string& branch) {
    return run_command(
            "git diff " +
            shell_words::quote("HEAD.." + remote_ref(branch)) +
            " --color=always");
}

int trusted_git_reset_hard(
        const ValidatedCachePath&,
        const std::string&,
        const std::string& branch) {
    return run_command(
            "git reset --hard " + shell_words::quote(remote_ref(branch)));
}

int trusted_git_clone_persistent_checkout(
        const ValidatedCachePath& destination,
        const std::string& remote_url) {
    return run_command(
            "git clone " + shell_words::quote(remote_url) + " " +
            shell_words::quote(destination.path().filename().string()));
}

int trusted_git_clone_aur_export(
        const std::string& remote_url,
        const std::filesystem::path& anchored_destination) {
    return run_command(
            "git clone --quiet -- " + shell_words::quote(remote_url) + " " +
            shell_words::quote(anchored_destination.string()) +
            " > /dev/null");
}

std::string trusted_git_aur_export_remote_origin_url(
        const std::filesystem::path& anchored_checkout) {
    const std::string command =
            "git -C " + shell_words::quote(anchored_checkout.string()) +
            " config --local --get remote.origin.url 2>/dev/null";
    return trim(exec_command(command.c_str()));
}
