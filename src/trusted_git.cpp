#include "trusted_git.hpp"

#include "logging.hpp"
#include "persistent_checkout.hpp"
#include "process.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::size_t MAX_LOCAL_CONFIG_OUTPUT = 1024 * 1024;

constexpr std::array<const char*, 8> TRUSTED_PROXY_ENVIRONMENT_VARIABLES{
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "no_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
        "NO_PROXY",
};

constexpr std::array<const char*, 4> TRUSTED_CA_ENVIRONMENT_VARIABLES{
        "SSL_CERT_FILE",
        "SSL_CERT_DIR",
        "GIT_SSL_CAINFO",
        "GIT_SSL_CAPATH",
};

class TrustedGitEnvironmentError final : public std::runtime_error {
public:
    explicit TrustedGitEnvironmentError(const std::string& variable_name)
        : std::runtime_error(
                  "Refusing non-absolute custom CA path in trusted Git "
                  "environment: " +
                  variable_name + ".") {}
};

bool is_absolute_ssl_cert_directory_list(std::string_view value) {
    // OpenSSL treats SSL_CERT_DIR as a POSIX ':'-separated directory list.
    // Validate each component without changing the raw value passed to envp.
    while(true) {
        const std::size_t separator = value.find(':');
        const std::string_view component = value.substr(0, separator);
        if(component.empty() || !fs::path(component).is_absolute()) {
            return false;
        }
        if(separator == std::string_view::npos) return true;
        value.remove_prefix(separator + 1);
    }
}

struct LocalGitConfiguration {
    std::string remote_origin_url;
};

struct BranchConfigurationState {
    bool has_remote = false;
    bool has_merge = false;
};

std::string trim(const std::string& value) {
    const std::string::size_type first =
            value.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    const std::string::size_type last =
            value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

std::string to_lower(std::string value) {
    std::transform(
            value.begin(), value.end(), value.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
    return value;
}

[[noreturn]] void throw_unsafe_local_configuration() {
    // Do not include config values or the checkout/cache path in diagnostics.
    throw std::runtime_error(
            "Refusing unsafe local Git configuration in managed checkout.");
}

bool is_boolean_value(const std::string& value) {
    const std::string lowered = to_lower(trim(value));
    return lowered == "true" || lowered == "false" || lowered == "yes" ||
           lowered == "no" || lowered == "on" || lowered == "off" ||
           lowered == "1" || lowered == "0";
}

bool is_false_value(const std::string& value) {
    const std::string lowered = to_lower(trim(value));
    return lowered == "false" || lowered == "no" || lowered == "off" ||
           lowered == "0";
}

bool has_control_character(const std::string& value) {
    return std::any_of(
            value.begin(), value.end(), [](unsigned char character) {
                return character < 0x20 || character == 0x7f;
            });
}

bool is_safe_branch_component(const std::string& value) {
    if(value.empty() || value.front() == '-' || value.front() == '/' ||
       value.back() == '/' || value.back() == '.' ||
       value.find("..") != std::string::npos ||
       value.find("//") != std::string::npos ||
       value.find("@{") != std::string::npos) {
        return false;
    }
    return std::all_of(
            value.begin(), value.end(), [](unsigned char character) {
                return std::isalnum(character) || character == '-' ||
                       character == '_' || character == '.' ||
                       character == '/';
            });
}

void require_unique_key(
        std::map<std::string, std::size_t>& key_counts,
        const std::string& key) {
    if(++key_counts[key] != 1) throw_unsafe_local_configuration();
}

LocalGitConfiguration parse_local_configuration(
        const CapturedCommandResult& result) {
    if(result.exit_code != 0 || result.output.empty() ||
       result.stdout_capture_limit_exceeded ||
       result.output.size() > MAX_LOCAL_CONFIG_OUTPUT ||
       result.output.back() != '\0') {
        throw_unsafe_local_configuration();
    }

    std::map<std::string, std::size_t> key_counts;
    std::map<std::string, BranchConfigurationState> branch_states;
    LocalGitConfiguration configuration;
    bool has_repository_format = false;
    bool has_file_mode = false;
    bool has_bare = false;
    bool has_log_all_ref_updates = false;
    bool has_origin_fetch = false;

    std::size_t offset = 0;
    while(offset < result.output.size()) {
        const std::size_t end = result.output.find('\0', offset);
        if(end == std::string::npos || end == offset) {
            throw_unsafe_local_configuration();
        }
        const std::string record = result.output.substr(offset, end - offset);
        offset = end + 1;

        const std::size_t separator = record.find('\n');
        if(separator == std::string::npos || separator == 0) {
            throw_unsafe_local_configuration();
        }
        const std::string key = to_lower(record.substr(0, separator));
        const std::string value = record.substr(separator + 1);
        if(has_control_character(key) || has_control_character(value)) {
            throw_unsafe_local_configuration();
        }

        if(key == "core.repositoryformatversion") {
            require_unique_key(key_counts, key);
            if(trim(value) != "0") throw_unsafe_local_configuration();
            has_repository_format = true;
            continue;
        }
        if(key == "core.filemode") {
            require_unique_key(key_counts, key);
            if(!is_boolean_value(value)) throw_unsafe_local_configuration();
            has_file_mode = true;
            continue;
        }
        if(key == "core.bare") {
            require_unique_key(key_counts, key);
            if(!is_false_value(value)) throw_unsafe_local_configuration();
            has_bare = true;
            continue;
        }
        if(key == "core.logallrefupdates") {
            require_unique_key(key_counts, key);
            if(!is_boolean_value(value)) throw_unsafe_local_configuration();
            has_log_all_ref_updates = true;
            continue;
        }
        if(key == "core.ignorecase" || key == "core.symlinks" ||
           key == "core.precomposeunicode") {
            require_unique_key(key_counts, key);
            if(!is_boolean_value(value)) throw_unsafe_local_configuration();
            continue;
        }
        if(key == "remote.origin.url") {
            require_unique_key(key_counts, key);
            if(trim(value).empty()) throw_unsafe_local_configuration();
            configuration.remote_origin_url = value;
            continue;
        }
        if(key == "remote.origin.fetch") {
            require_unique_key(key_counts, key);
            if(trim(value) != "+refs/heads/*:refs/remotes/origin/*") {
                throw_unsafe_local_configuration();
            }
            has_origin_fetch = true;
            continue;
        }

        constexpr std::string_view branch_prefix = "branch.";
        constexpr std::string_view remote_suffix = ".remote";
        constexpr std::string_view merge_suffix = ".merge";
        if(key.starts_with(branch_prefix) &&
           (key.ends_with(remote_suffix) || key.ends_with(merge_suffix))) {
            const bool is_remote = key.ends_with(remote_suffix);
            const std::size_t suffix_size =
                    is_remote ? remote_suffix.size() : merge_suffix.size();
            const std::string branch = key.substr(
                    branch_prefix.size(),
                    key.size() - branch_prefix.size() - suffix_size);
            if(!is_safe_branch_component(branch)) {
                throw_unsafe_local_configuration();
            }
            require_unique_key(key_counts, key);
            BranchConfigurationState& state = branch_states[branch];
            if(is_remote) {
                if(trim(value) != "origin") throw_unsafe_local_configuration();
                state.has_remote = true;
            } else {
                const std::string expected_merge = "refs/heads/" + branch;
                if(trim(value) != expected_merge) {
                    throw_unsafe_local_configuration();
                }
                state.has_merge = true;
            }
            continue;
        }

        // Moguet owns these clones. Unknown keys are refused rather than
        // attempting to maintain an incomplete Git command-injection denylist.
        throw_unsafe_local_configuration();
    }

    if(!has_repository_format || !has_file_mode || !has_bare ||
       !has_log_all_ref_updates || configuration.remote_origin_url.empty() ||
       !has_origin_fetch) {
        throw_unsafe_local_configuration();
    }
    for(const auto& [branch, state] : branch_states) {
        static_cast<void>(branch);
        if(!state.has_remote || !state.has_merge) {
            throw_unsafe_local_configuration();
        }
    }
    return configuration;
}

std::vector<std::string> trusted_git_environment(
        const std::optional<std::string>& test_display_command) {
    std::vector<std::string> environment{
            "PATH=/usr/bin:/bin",
            "LC_ALL=C",
            "LANG=C",
            "GIT_CONFIG_NOSYSTEM=1",
            "GIT_CONFIG_SYSTEM=/dev/null",
            "GIT_CONFIG_GLOBAL=/dev/null",
            "GIT_TERMINAL_PROMPT=0",
            "GIT_ASKPASS=/bin/false",
            "SSH_ASKPASS=/bin/false",
            "GIT_PAGER=cat",
            "PAGER=cat",
            "GIT_ATTR_NOSYSTEM=1",
    };

    // Start from a complete allowlist. Proxy values stay opaque and retain
    // their exact variable spelling; no shell or Git-config routing is added.
    for(const char* variable_name : TRUSTED_PROXY_ENVIRONMENT_VARIABLES) {
        const char* value = std::getenv(variable_name);
        if(value != nullptr) {
            environment.emplace_back(
                    std::string(variable_name) + "=" + value);
        }
    }
    for(const char* variable_name : TRUSTED_CA_ENVIRONMENT_VARIABLES) {
        const char* value = std::getenv(variable_name);
        if(value == nullptr || value[0] == '\0') continue;
        const bool is_absolute =
                std::string_view(variable_name) == "SSL_CERT_DIR"
                ? is_absolute_ssl_cert_directory_list(value)
                : fs::path(value).is_absolute();
        if(!is_absolute) {
            // Do not expose the path value in a user-facing diagnostic.
            throw TrustedGitEnvironmentError(variable_name);
        }
        environment.emplace_back(std::string(variable_name) + "=" + value);
    }

#ifdef MOGUET_ENABLE_TEST_OVERRIDES
    for(char** current = ::environ;
        current != nullptr && *current != nullptr;
        ++current) {
        std::string assignment(*current);
        const std::size_t separator = assignment.find('=');
        if(separator == std::string::npos) continue;
        const std::string name = assignment.substr(0, separator);
        if(!name.starts_with("MOGUET_TEST_") ||
           name == "MOGUET_TEST_TRUSTED_GIT_DISPLAY_COMMAND" ||
           name == "MOGUET_TEST_TRUSTED_GIT_BOUNDARY") {
            continue;
        }
        environment.push_back(std::move(assignment));
    }
    environment.push_back("MOGUET_TEST_TRUSTED_GIT_BOUNDARY=1");
    if(test_display_command.has_value()) {
        environment.push_back(
                "MOGUET_TEST_TRUSTED_GIT_DISPLAY_COMMAND=" +
                test_display_command.value());
    }
#else
    static_cast<void>(test_display_command);
#endif
    return environment;
}

std::string trusted_git_executable() {
#ifdef MOGUET_ENABLE_TEST_OVERRIDES
    const char* explicit_executable =
            std::getenv("MOGUET_TEST_GIT_EXECUTABLE");
    if(explicit_executable != nullptr && explicit_executable[0] != '\0') {
        const fs::path candidate(explicit_executable);
        if(candidate.is_absolute() && access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
        throw std::runtime_error("Invalid trusted Git test executable.");
    }

    const char* path_value = std::getenv("PATH");
    if(path_value != nullptr) {
        std::string path(path_value);
        std::size_t offset = 0;
        while(offset <= path.size()) {
            const std::size_t end = path.find(':', offset);
            const std::string component = path.substr(
                    offset,
                    end == std::string::npos ? std::string::npos
                                             : end - offset);
            const fs::path directory(component);
            if(directory.is_absolute()) {
                const fs::path candidate = directory / "git";
                if(access(candidate.c_str(), X_OK) == 0) {
                    return candidate.string();
                }
            }
            if(end == std::string::npos) break;
            offset = end + 1;
        }
    }
    throw std::runtime_error("Unable to locate trusted Git test executable.");
#else
    return "/usr/bin/git";
#endif
}

std::vector<std::string> common_git_arguments() {
    return {
            "--no-pager",
            "-c", "core.hooksPath=/dev/null",
            "-c", "core.fsmonitor=false",
            "-c", "core.sshCommand=/bin/false",
            "-c", "credential.helper=",
            "-c", "core.askPass=/bin/false",
            "-c", "core.pager=cat",
            "-c", "pager.diff=false",
            "-c", "diff.external=",
            "-c", "protocol.allow=never",
            "-c", "protocol.http.allow=always",
            "-c", "protocol.https.allow=always",
            "-c", "submodule.recurse=false",
    };
}

ExplicitProcessInvocation isolated_invocation(
        std::vector<std::string> git_arguments,
        const std::optional<std::string>& test_display_command) {
    return ExplicitProcessInvocation{
            trusted_git_executable(), std::move(git_arguments),
            trusted_git_environment(test_display_command)};
}

std::vector<std::string> bound_git_arguments(
        const fs::path& checkout,
        std::vector<std::string> operation_arguments) {
    std::vector<std::string> arguments = common_git_arguments();
    arguments.push_back("--git-dir=" + (checkout / ".git").string());
    arguments.push_back("--work-tree=" + checkout.string());
    arguments.insert(
            arguments.end(),
            std::make_move_iterator(operation_arguments.begin()),
            std::make_move_iterator(operation_arguments.end()));
    return arguments;
}

CapturedCommandResult inspect_local_configuration_output(
        const fs::path& checkout,
        const std::optional<std::string>& test_display_command) {
    const std::vector<std::string> arguments = bound_git_arguments(
            checkout,
            {"config", "--local", "--no-includes", "--null", "--list"});
    ExplicitProcessInvocation invocation =
            isolated_invocation(arguments, test_display_command);
    invocation.stdout_capture_limit = MAX_LOCAL_CONFIG_OUTPUT + 1;
    return capture_explicit_process_output_raw(invocation, true);
}

LocalGitConfiguration inspect_managed_checkout_configuration(
        const ValidatedCachePath& checkout,
        const std::optional<std::string>& test_display_command =
                std::nullopt) {
    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(current);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    LocalGitConfiguration configuration = parse_local_configuration(
            inspect_local_configuration_output(
                    current.canonical_path(), test_display_command));
    retained.require_unchanged_identity();
    current = revalidate_trusted_cache_path(
            current, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(current);
    return configuration;
}

void require_expected_remote(
        const LocalGitConfiguration& configuration,
        const std::string& expected_remote_url) {
    if(!remote_url_matches_expected(
               configuration.remote_origin_url, expected_remote_url)) {
        throw std::runtime_error(
                "Remote URL changed before managed Git operation.");
    }
}

CapturedCommandResult capture_managed_git(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        std::vector<std::string> operation_arguments,
        const std::string& display_command,
        bool suppress_standard_error = false) {
    require_expected_remote(
            inspect_managed_checkout_configuration(checkout),
            expected_remote_url);
    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    CapturedCommandResult result = capture_explicit_process_output_raw(
            isolated_invocation(
                    bound_git_arguments(
                            current.canonical_path(),
                            std::move(operation_arguments)),
                    display_command),
            suppress_standard_error);
    retained.require_unchanged_identity();
    current = revalidate_trusted_cache_path(
            current, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(current);
    return result;
}

int run_managed_git(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        std::vector<std::string> operation_arguments,
        const std::string& display_command) {
    require_expected_remote(
            inspect_managed_checkout_configuration(checkout),
            expected_remote_url);
    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    Logger::raw_cmd(display_command);
    const int status = run_explicit_process(isolated_invocation(
            bound_git_arguments(
                    current.canonical_path(),
                    std::move(operation_arguments)),
            display_command));
    retained.require_unchanged_identity();
    current = revalidate_trusted_cache_path(
            current, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(current);
    return status;
}

std::string remote_ref_for_branch(const std::string& branch) {
    if(!is_safe_branch_component(branch)) {
        throw std::runtime_error("Refusing invalid managed Git branch name.");
    }
    return "origin/" + branch;
}

std::string diff_range_for_branch(const std::string& branch) {
    return "HEAD.." + remote_ref_for_branch(branch);
}

LocalGitConfiguration inspect_aur_export_configuration(
        const fs::path& anchored_checkout,
        const std::string& display_command) {
    return parse_local_configuration(inspect_local_configuration_output(
            anchored_checkout, display_command));
}

} // namespace

std::string trusted_git_remote_origin_url(
        const ValidatedCachePath& checkout) {
    return trim(inspect_managed_checkout_configuration(
                        checkout,
                        "git config --get remote.origin.url")
                        .remote_origin_url);
}

int trusted_git_fetch_origin(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url) {
    return run_managed_git(
            checkout, expected_remote_url,
            {"fetch", "--no-recurse-submodules", "origin"},
            "git fetch origin");
}

std::string trusted_git_detect_remote_branch(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url) {
    CapturedCommandResult remote_head = capture_managed_git(
            checkout, expected_remote_url,
            {"symbolic-ref", "--quiet", "--short",
             "refs/remotes/origin/HEAD"},
            "git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null",
            true);
    constexpr std::string_view prefix = "origin/";
    const std::string trimmed_head = trim(remote_head.output);
    if(remote_head.exit_code == 0 && trimmed_head.starts_with(prefix)) {
        const std::string branch = trimmed_head.substr(prefix.size());
        static_cast<void>(remote_ref_for_branch(branch));
        return branch;
    }

    for(const std::string& branch : {std::string("main"), std::string("master")}) {
        const std::string ref = "refs/remotes/origin/" + branch;
        const int status = run_managed_git(
                checkout, expected_remote_url,
                {"show-ref", "--verify", "--quiet", ref},
                "git show-ref --verify --quiet " + ref);
        if(status == 0) return branch;
    }
    return "master";
}

int trusted_git_diff_quiet(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const std::string range = diff_range_for_branch(branch);
    return run_managed_git(
            checkout, expected_remote_url,
            {"diff", "--quiet", "--no-ext-diff", "--no-textconv", range,
             "--"},
            "git diff --quiet " + range);
}

std::string trusted_git_diff_name_only(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const std::string range = diff_range_for_branch(branch);
    CapturedCommandResult result = capture_managed_git(
            checkout, expected_remote_url,
            {"diff", "--name-only", "--no-ext-diff", "--no-textconv",
             range, "--"},
            "git diff --name-only " + range +
                    " 2>/dev/null",
            true);
    return result.exit_code == 0 ? result.output : "";
}

int trusted_git_show_diff(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const std::string range = diff_range_for_branch(branch);
    return run_managed_git(
            checkout, expected_remote_url,
            {"diff", "--no-ext-diff", "--no-textconv", "--color=always",
             range, "--"},
            "git diff " + range + " --color=always");
}

int trusted_git_reset_hard(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const std::string remote_ref = remote_ref_for_branch(branch);
    return run_managed_git(
            checkout, expected_remote_url,
            {"reset", "--hard", remote_ref, "--"},
            "git reset --hard " + remote_ref);
}

int trusted_git_clone_persistent_checkout(
        const ValidatedCachePath& destination,
        const std::string& remote_url) {
    ValidatedCachePath current = revalidate_trusted_cache_path(
            destination, CachePathRequirement::ExistingDirectory);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    const std::string leaf = current.path().filename().string();
    const std::string display_command =
            "git clone " + remote_url + " " + leaf;
    Logger::raw_cmd(display_command);
    std::vector<std::string> arguments = common_git_arguments();
    arguments.insert(
            arguments.end(),
            {"clone", "--no-recurse-submodules", "--", remote_url,
             current.canonical_path().string()});
    const int status = run_explicit_process(isolated_invocation(
            std::move(arguments), display_command));
    retained.require_unchanged_identity();
    return status;
}

int trusted_git_clone_aur_export(
        const std::string& remote_url,
        const std::filesystem::path& anchored_destination) {
    const std::string display_command =
            "git clone --quiet -- " + remote_url + " " +
            anchored_destination.string() +
            " > /dev/null";
    Logger::raw_cmd(display_command);
    std::vector<std::string> arguments = common_git_arguments();
    arguments.insert(
            arguments.end(),
            {"clone", "--quiet", "--no-recurse-submodules", "--",
             remote_url, anchored_destination.string()});
    return run_explicit_process(
            isolated_invocation(std::move(arguments), display_command),
            true);
}

std::string trusted_git_aur_export_remote_origin_url(
        const std::filesystem::path& anchored_checkout) {
    const std::string display_command =
            "git -C " + anchored_checkout.string() +
            " config --local --get remote.origin.url";
    return trim(inspect_aur_export_configuration(
                        anchored_checkout, display_command)
                        .remote_origin_url);
}
