#include "trusted_git.hpp"

#include "localization.hpp"
#include "logging.hpp"
#include "persistent_checkout.hpp"
#include "process.hpp"
#include "reviewed_source_git_parser.hpp"

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
constexpr std::size_t MAX_COMMIT_OID_OUTPUT = 65;
constexpr std::size_t MAX_OBJECT_TYPE_OUTPUT = 7;
constexpr std::size_t MAX_EMPTY_COMMAND_OUTPUT = 1;
constexpr std::size_t MAX_SHALLOW_OUTPUT = 6;

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_GIT_TEST_HOOKS
std::optional<std::size_t> g_review_machine_stream_limit;
#endif

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
        // TRANSLATORS: The placeholders are the Git program identity and an environment variable name. Its value is intentionally not exposed.
        : std::runtime_error(localization::format_translated_message(
                  "Refusing a non-absolute custom CA path in the trusted {} environment variable {}.",
                  "Git", variable_name)) {}
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
    GitObjectFormat object_format = GitObjectFormat::Sha1;
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
    throw std::runtime_error(localization::format_translated_message(
            "Refusing unsafe local {} configuration in the managed checkout.",
            "Git"));
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
    std::optional<std::string> repository_format_version;
    std::optional<std::string> extension_object_format;

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
            repository_format_version = trim(value);
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
        if(key == "extensions.objectformat") {
            require_unique_key(key_counts, key);
            extension_object_format = trim(value);
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
    if(repository_format_version == std::optional<std::string>("0") &&
       !extension_object_format.has_value()) {
        configuration.object_format = GitObjectFormat::Sha1;
    } else if(
            repository_format_version == std::optional<std::string>("1") &&
            extension_object_format == std::optional<std::string>("sha256")) {
        configuration.object_format = GitObjectFormat::Sha256;
    } else {
        throw_unsafe_local_configuration();
    }
    return configuration;
}

std::vector<std::string> trusted_git_environment(
        const std::optional<std::string>& test_display_command,
        bool read_only_projection = false) {
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
    if(read_only_projection) {
        environment.push_back("GIT_OPTIONAL_LOCKS=0");
    }

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
        // NO_TRANSLATE: Test-only override validation; unreachable in production builds.
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
    // NO_TRANSLATE: Test-only harness setup failure; unreachable in production builds.
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
        const std::optional<std::string>& test_display_command,
        bool read_only_projection = false) {
    return ExplicitProcessInvocation{
            trusted_git_executable(), std::move(git_arguments),
            trusted_git_environment(
                    test_display_command, read_only_projection)};
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

std::vector<std::string> bound_review_git_arguments(
        const fs::path& checkout,
        std::vector<std::string> operation_arguments,
        const std::optional<std::string>& attribute_source = std::nullopt) {
    std::vector<std::string> arguments = common_git_arguments();
    arguments.push_back("--no-replace-objects");
    if(attribute_source.has_value()) {
        arguments.push_back("--attr-source=" + *attribute_source);
    }
    arguments.push_back("--git-dir=" + (checkout / ".git").string());
    arguments.push_back("--work-tree=" + checkout.string());
    arguments.insert(
            arguments.end(),
            std::make_move_iterator(operation_arguments.begin()),
            std::make_move_iterator(operation_arguments.end()));
    return arguments;
}

std::size_t review_machine_stream_limit() noexcept {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_GIT_TEST_HOOKS
    if(g_review_machine_stream_limit.has_value()) {
        return *g_review_machine_stream_limit;
    }
#endif
    return REVIEWED_SOURCE_MACHINE_STREAM_LIMIT;
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

LocalGitConfiguration inspect_review_checkout_configuration(
        const ValidatedCachePath& checkout,
        const std::optional<std::string>& test_display_command =
                std::nullopt) {
    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_git_metadata(current);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    LocalGitConfiguration configuration = parse_local_configuration(
            inspect_local_configuration_output(
                    current.canonical_path(), test_display_command));
    retained.require_unchanged_identity();
    current = revalidate_trusted_cache_path(
            current, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_git_metadata(current);
    return configuration;
}

void require_expected_remote(
        const LocalGitConfiguration& configuration,
        const std::string& expected_remote_url) {
    if(!remote_url_matches_expected(
               configuration.remote_origin_url, expected_remote_url)) {
        throw std::runtime_error(localization::format_translated_message(
                "The remote URL changed before the managed {} operation.",
                "Git"));
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

CapturedCommandResult capture_review_git(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        std::vector<std::string> operation_arguments,
        const std::string& display_command,
        std::size_t stdout_limit,
        const std::optional<std::string>& attribute_source = std::nullopt) {
    require_expected_remote(
            inspect_review_checkout_configuration(checkout),
            expected_remote_url);
    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    RetainedTrustedCacheDirectory retained =
            retain_trusted_cache_directory(current);
    retained.require_unchanged_identity();
    ExplicitProcessInvocation invocation = isolated_invocation(
            bound_review_git_arguments(
                    current.canonical_path(),
                    std::move(operation_arguments), attribute_source),
            display_command, true);
    invocation.stdout_capture_limit = stdout_limit;
    CapturedCommandResult result =
            capture_explicit_process_output_raw(invocation, true);
    retained.require_unchanged_identity();
    current = revalidate_trusted_cache_path(
            current, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_git_metadata(current);
    return result;
}

TrustedGitReviewFailure review_failure(
        TrustedGitReviewFailureReason reason,
        TrustedGitReviewStage stage) {
    return TrustedGitReviewFailure{reason, stage, std::nullopt};
}

TrustedGitReviewFailure command_failure(
        TrustedGitReviewStage stage, int exit_code) {
    TrustedGitReviewFailure failure = review_failure(
            TrustedGitReviewFailureReason::CommandFailed, stage);
    failure.exit_code = exit_code;
    return failure;
}

TrustedGitReviewFailure capture_limit_failure(
        TrustedGitReviewStage stage, std::size_t limit) {
    TrustedGitReviewFailure failure = review_failure(
            TrustedGitReviewFailureReason::CaptureLimitExceeded, stage);
    failure.limit = limit;
    return failure;
}

TrustedGitReviewStage review_stage_for_stream(
        ReviewedSourceMachineStream stream) noexcept {
    switch(stream) {
    case ReviewedSourceMachineStream::CommitResolution:
        return TrustedGitReviewStage::TargetValidation;
    case ReviewedSourceMachineStream::BaselineTree:
        return TrustedGitReviewStage::BaselineTree;
    case ReviewedSourceMachineStream::TargetTree:
        return TrustedGitReviewStage::TargetTree;
    case ReviewedSourceMachineStream::NameStatus:
        return TrustedGitReviewStage::NameStatus;
    case ReviewedSourceMachineStream::Numstat:
        return TrustedGitReviewStage::Numstat;
    case ReviewedSourceMachineStream::CrossStream:
        return TrustedGitReviewStage::CrossStream;
    case ReviewedSourceMachineStream::ResourcePreflight:
        return TrustedGitReviewStage::ResourcePreflight;
    }
    return TrustedGitReviewStage::CrossStream;
}

TrustedGitReviewFailure map_projection_failure(
        const ReviewedSourceProjectionFailure& source) {
    TrustedGitReviewFailureReason reason =
            TrustedGitReviewFailureReason::MalformedMachineOutput;
    switch(source.reason) {
    case ReviewedSourceProjectionFailureReason::MalformedMachineOutput:
        reason = TrustedGitReviewFailureReason::MalformedMachineOutput;
        break;
    case ReviewedSourceProjectionFailureReason::InconsistentMachineOutput:
        reason = TrustedGitReviewFailureReason::InconsistentMachineOutput;
        break;
    case ReviewedSourceProjectionFailureReason::RenameCandidateLimitExceeded:
        reason = TrustedGitReviewFailureReason::RenameCandidateLimitExceeded;
        break;
    case ReviewedSourceProjectionFailureReason::SingleBlobSizeLimitExceeded:
        reason = TrustedGitReviewFailureReason::SingleBlobSizeLimitExceeded;
        break;
    case ReviewedSourceProjectionFailureReason::AggregateBlobSizeLimitExceeded:
        reason = TrustedGitReviewFailureReason::AggregateBlobSizeLimitExceeded;
        break;
    }
    TrustedGitReviewFailure failure = review_failure(
            reason, review_stage_for_stream(source.stream));
    failure.record_index = source.record_index;
    failure.observed = source.observed;
    failure.limit = source.limit;
    return failure;
}

const std::string& require_known_commit(
        const SourceRevisionIdentity& revision) {
    if(revision.state() != SourceRevisionState::Known ||
       revision.git_commit() == nullptr ||
       revision.git_object_format() == nullptr) {
        throw std::invalid_argument(
                "Reviewed source Git operation requires a known complete commit.");
    }
    return *revision.git_commit();
}

struct ExactCommitUnavailable {};

using ExactTargetCommitValidationResult = std::variant<
        SourceRevisionIdentity,
        TrustedGitReviewFailure>;

using ExactBaselineCommitValidationResult = std::variant<
        SourceRevisionIdentity,
        ExactCommitUnavailable,
        TrustedGitReviewFailure>;

ExactTargetCommitValidationResult validate_exact_target_commit(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const SourceRevisionIdentity& expected,
        GitObjectFormat repository_format,
        TrustedGitReviewStage stage) {
    const std::string& object_id = require_known_commit(expected);
    CapturedCommandResult result = capture_review_git(
            checkout, expected_remote_url,
            {"rev-parse", "--verify", "--quiet",
             "--output-object-format=storage", "--end-of-options",
             object_id + "^{commit}"},
            "git rev-parse --verify <pinned-commit>^{commit}",
            MAX_COMMIT_OID_OUTPUT);
    if(result.stdout_capture_limit_exceeded) {
        return capture_limit_failure(stage, MAX_COMMIT_OID_OUTPUT);
    }
    if(result.exit_code != 0) {
        return command_failure(stage, result.exit_code);
    }
    ReviewedSourceCommitParseResult parsed =
            parse_reviewed_source_commit_output(result.output);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed)) {
        TrustedGitReviewFailure failure = map_projection_failure(
                std::get<ReviewedSourceProjectionFailure>(parsed));
        failure.stage = stage;
        return failure;
    }
    SourceRevisionIdentity observed =
            std::get<SourceRevisionIdentity>(std::move(parsed));
    if(observed.git_object_format() == nullptr ||
       *observed.git_object_format() != repository_format) {
        return review_failure(
                TrustedGitReviewFailureReason::ObjectFormatMismatch, stage);
    }
    if(observed != expected) {
        return review_failure(
                TrustedGitReviewFailureReason::ObjectFormatMismatch, stage);
    }
    return observed;
}

ExactBaselineCommitValidationResult validate_exact_baseline_commit(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const SourceRevisionIdentity& expected,
        GitObjectFormat repository_format,
        const std::string& target_oid) {
    constexpr TrustedGitReviewStage STAGE =
            TrustedGitReviewStage::BaselineValidation;
    const std::string& object_id = require_known_commit(expected);
    if(expected.git_object_format() == nullptr ||
       *expected.git_object_format() != repository_format) {
        return ExactCommitUnavailable{};
    }

    CapturedCommandResult existence = capture_review_git(
            checkout, expected_remote_url,
            {"cat-file", "-e", object_id},
            "git cat-file -e <baseline-object>", MAX_EMPTY_COMMAND_OUTPUT);
    if(existence.stdout_capture_limit_exceeded) {
        return capture_limit_failure(STAGE, MAX_EMPTY_COMMAND_OUTPUT);
    }
    if(!existence.output.empty()) {
        return review_failure(
                TrustedGitReviewFailureReason::MalformedMachineOutput, STAGE);
    }
    if(existence.exit_code == 1) {
        // LANDMINE(#411): cat-file -e also returns 1 when a pack is present
        // but unreadable. Exact disambiguation still observes its index entry;
        // an unreadable index is caught by the descriptor-safe read proof.
        CapturedCommandResult indexed = capture_review_git(
                checkout, expected_remote_url,
                {"rev-parse", "--disambiguate=" + object_id},
                "git rev-parse --disambiguate=<baseline-object>",
                MAX_COMMIT_OID_OUTPUT);
        if(indexed.stdout_capture_limit_exceeded) {
            return capture_limit_failure(STAGE, MAX_COMMIT_OID_OUTPUT);
        }
        if(indexed.exit_code != 0) {
            return command_failure(STAGE, indexed.exit_code);
        }
        if(!indexed.output.empty()) {
            ReviewedSourceCommitParseResult parsed =
                    parse_reviewed_source_commit_output(indexed.output);
            if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed)) {
                TrustedGitReviewFailure failure = map_projection_failure(
                        std::get<ReviewedSourceProjectionFailure>(parsed));
                failure.stage = STAGE;
                return failure;
            }
            const SourceRevisionIdentity& indexed_revision =
                    std::get<SourceRevisionIdentity>(parsed);
            if(indexed_revision != expected) {
                return review_failure(
                        TrustedGitReviewFailureReason::MalformedMachineOutput,
                        STAGE);
            }
            return command_failure(STAGE, existence.exit_code);
        }

        try {
            require_readable_persistent_checkout_git_metadata(checkout);
        } catch(const TrustedCacheError& error) {
            if(error.failure().code == TrustedCacheErrorCode::PermissionDenied ||
               error.failure().code == TrustedCacheErrorCode::MetadataFailure) {
                return command_failure(STAGE, existence.exit_code);
            }
            throw;
        }

        const std::size_t integrity_limit = review_machine_stream_limit();
        CapturedCommandResult integrity = capture_review_git(
                checkout, expected_remote_url,
                {"fsck", "--full", "--no-dangling", "--no-progress",
                 "--no-reflogs", "--no-cache", "--no-references",
                 target_oid},
                "git fsck --full <pinned-target>",
                integrity_limit);
        if(integrity.stdout_capture_limit_exceeded) {
            return capture_limit_failure(STAGE, integrity_limit);
        }
        if(integrity.exit_code != 0) {
            return command_failure(STAGE, integrity.exit_code);
        }
        if(!integrity.output.empty()) {
            return review_failure(
                    TrustedGitReviewFailureReason::MalformedMachineOutput,
                    STAGE);
        }
        return ExactCommitUnavailable{};
    }
    if(existence.exit_code != 0) {
        return command_failure(STAGE, existence.exit_code);
    }

    CapturedCommandResult type = capture_review_git(
            checkout, expected_remote_url,
            {"cat-file", "-t", object_id},
            "git cat-file -t <baseline-object>", MAX_OBJECT_TYPE_OUTPUT);
    if(type.stdout_capture_limit_exceeded) {
        return capture_limit_failure(STAGE, MAX_OBJECT_TYPE_OUTPUT);
    }
    if(type.exit_code != 0) {
        return command_failure(STAGE, type.exit_code);
    }
    if(type.output == "commit\n") return expected;
    if(type.output == "blob\n" || type.output == "tree\n" ||
       type.output == "tag\n") {
        return ExactCommitUnavailable{};
    }
    return review_failure(
            TrustedGitReviewFailureReason::MalformedMachineOutput, STAGE);
}

std::optional<TrustedGitReviewFailure> require_non_shallow_repository(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url) {
    CapturedCommandResult result = capture_review_git(
            checkout, expected_remote_url,
            {"rev-parse", "--is-shallow-repository"},
            "git rev-parse --is-shallow-repository", MAX_SHALLOW_OUTPUT);
    if(result.stdout_capture_limit_exceeded) {
        return capture_limit_failure(
                TrustedGitReviewStage::ShallowRepositoryCheck,
                MAX_SHALLOW_OUTPUT);
    }
    if(result.exit_code != 0) {
        return command_failure(
                TrustedGitReviewStage::ShallowRepositoryCheck,
                result.exit_code);
    }
    if(result.output == "true\n") {
        return review_failure(
                TrustedGitReviewFailureReason::
                        ShallowRepositoryUnsupported,
                TrustedGitReviewStage::ShallowRepositoryCheck);
    }
    if(result.output != "false\n") {
        return review_failure(
                TrustedGitReviewFailureReason::MalformedMachineOutput,
                TrustedGitReviewStage::ShallowRepositoryCheck);
    }
    return std::nullopt;
}

std::optional<TrustedGitReviewFailure> require_no_attribute_override(
        const ValidatedCachePath& checkout) {
    if(observe_persistent_checkout_review_overrides(checkout).has_attributes) {
        return review_failure(
                TrustedGitReviewFailureReason::LocalAttributeOverride,
                TrustedGitReviewStage::AttributeGuard);
    }
    return std::nullopt;
}

std::optional<TrustedGitReviewFailure> require_no_history_override(
        const ValidatedCachePath& checkout) {
    if(observe_persistent_checkout_review_overrides(checkout).has_grafts) {
        return review_failure(
                TrustedGitReviewFailureReason::LocalHistoryOverride,
                TrustedGitReviewStage::HistoryGuard);
    }
    return std::nullopt;
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
        throw std::runtime_error(localization::format_translated_message(
                "Refusing an invalid managed {} branch name.", "Git"));
    }
    // NO_TRANSLATE: Literal Git remote-ref identity.
    return "origin/" + branch;
}

std::string full_remote_ref_for_branch(const std::string& branch) {
    return "refs/remotes/" + remote_ref_for_branch(branch);
}

std::vector<std::string> reviewed_source_diff_arguments(
        const std::string& output_option,
        const std::string& baseline,
        const std::string& target,
        bool detect_renames) {
    std::vector<std::string> arguments{
            "diff-tree", "--no-commit-id", "-r", "-z", output_option,
            "--no-relative", "--no-renames"};
    if(detect_renames) {
        arguments.push_back("--find-renames=50%");
        arguments.push_back("-l1000");
    }
    arguments.insert(
            arguments.end(),
            {"--diff-algorithm=myers", "--no-ext-diff", "--no-textconv",
             "--ignore-submodules=none", baseline, target, "--"});
    return arguments;
}

std::string diff_range_for_branch(const std::string& branch) {
    // NO_TRANSLATE: Literal Git revision range.
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
            {"fetch", "--no-auto-maintenance", "--no-recurse-submodules",
             "origin"},
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

TrustedGitCommitResolutionResult trusted_git_resolve_remote_commit(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const LocalGitConfiguration configuration =
            inspect_review_checkout_configuration(checkout);
    require_expected_remote(configuration, expected_remote_url);
    const std::string ref = full_remote_ref_for_branch(branch);
    CapturedCommandResult result = capture_review_git(
            checkout, expected_remote_url,
            {"rev-parse", "--verify", "--output-object-format=storage",
             "--end-of-options", ref + "^{commit}"},
            "git rev-parse --verify " + ref + "^{commit}",
            MAX_COMMIT_OID_OUTPUT);
    if(result.stdout_capture_limit_exceeded) {
        return capture_limit_failure(
                TrustedGitReviewStage::TargetResolution,
                MAX_COMMIT_OID_OUTPUT);
    }
    if(result.exit_code != 0) {
        return command_failure(
                TrustedGitReviewStage::TargetResolution,
                result.exit_code);
    }
    ReviewedSourceCommitParseResult parsed =
            parse_reviewed_source_commit_output(result.output);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed)) {
        TrustedGitReviewFailure failure = map_projection_failure(
                std::get<ReviewedSourceProjectionFailure>(parsed));
        failure.stage = TrustedGitReviewStage::TargetResolution;
        return failure;
    }
    SourceRevisionIdentity revision =
            std::get<SourceRevisionIdentity>(std::move(parsed));
    if(revision.git_object_format() == nullptr ||
       *revision.git_object_format() != configuration.object_format) {
        return review_failure(
                TrustedGitReviewFailureReason::ObjectFormatMismatch,
                TrustedGitReviewStage::TargetResolution);
    }
    return revision;
}

TrustedGitReviewedSourceProjectionResult trusted_git_project_reviewed_source(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const SourceRevisionIdentity& target,
        const std::optional<SourceRevisionIdentity>& baseline) {
    const std::string& target_oid = require_known_commit(target);
    const LocalGitConfiguration configuration =
            inspect_review_checkout_configuration(checkout);
    require_expected_remote(configuration, expected_remote_url);

    ValidatedCachePath current = revalidate_trusted_cache_path(
            checkout, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_git_metadata(current);
    RetainedTrustedCacheDirectory outer =
            retain_trusted_cache_directory(current);
    outer.require_unchanged_identity();

    const auto finish = [&outer, &current](
                                TrustedGitReviewedSourceProjectionResult result) {
        outer.require_unchanged_identity();
        current = revalidate_trusted_cache_path(
                current, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_git_metadata(current);
        return result;
    };

    const PersistentCheckoutReviewOverrides initial_overrides =
            observe_persistent_checkout_review_overrides(current);
    if(initial_overrides.has_attributes) {
        return finish(review_failure(
                TrustedGitReviewFailureReason::LocalAttributeOverride,
                TrustedGitReviewStage::AttributeGuard));
    }
    if(initial_overrides.has_grafts) {
        return finish(review_failure(
                TrustedGitReviewFailureReason::LocalHistoryOverride,
                TrustedGitReviewStage::HistoryGuard));
    }
    if(target.git_object_format() == nullptr ||
       *target.git_object_format() != configuration.object_format) {
        return finish(review_failure(
                TrustedGitReviewFailureReason::ObjectFormatMismatch,
                TrustedGitReviewStage::TargetValidation));
    }

    ExactTargetCommitValidationResult target_validation =
            validate_exact_target_commit(
                    current, expected_remote_url, target,
                    configuration.object_format,
                    TrustedGitReviewStage::TargetValidation);
    if(std::holds_alternative<TrustedGitReviewFailure>(target_validation)) {
        return finish(std::get<TrustedGitReviewFailure>(target_validation));
    }
    if(const auto shallow_failure = require_non_shallow_repository(
               current, expected_remote_url)) {
        return finish(*shallow_failure);
    }

    if(baseline.has_value() && *baseline == target) {
        return finish(ReviewedSourceAlreadyReviewed{target});
    }

    bool is_rebaseline = false;
    bool detect_renames = false;
    std::optional<ReviewedSourceHistoryRelation> history_relation;
    if(baseline.has_value()) {
        ExactBaselineCommitValidationResult baseline_validation =
                validate_exact_baseline_commit(
                        current, expected_remote_url, *baseline,
                        configuration.object_format, target_oid);
        if(std::holds_alternative<TrustedGitReviewFailure>(
                   baseline_validation)) {
            return finish(std::get<TrustedGitReviewFailure>(
                    baseline_validation));
        }
        if(std::holds_alternative<ExactCommitUnavailable>(
                   baseline_validation)) {
            is_rebaseline = true;
        } else {
            detect_renames = true;
            if(const auto history_override =
                       require_no_history_override(current)) {
                return finish(*history_override);
            }
            CapturedCommandResult ancestry = capture_review_git(
                    current, expected_remote_url,
                    {"merge-base", "--is-ancestor",
                     require_known_commit(*baseline), target_oid},
                    "git merge-base --is-ancestor <baseline> <target>", 1);
            if(const auto history_override =
                       require_no_history_override(current)) {
                return finish(*history_override);
            }
            if(ancestry.stdout_capture_limit_exceeded) {
                return finish(capture_limit_failure(
                        TrustedGitReviewStage::AncestryCheck, 1));
            }
            if(!ancestry.output.empty()) {
                return finish(review_failure(
                        TrustedGitReviewFailureReason::MalformedMachineOutput,
                        TrustedGitReviewStage::AncestryCheck));
            }
            if(ancestry.exit_code == 0) {
                history_relation = ReviewedSourceHistoryRelation::Ancestor;
            } else if(ancestry.exit_code == 1) {
                history_relation = ReviewedSourceHistoryRelation::NonAncestor;
            } else {
                return finish(command_failure(
                        TrustedGitReviewStage::AncestryCheck,
                        ancestry.exit_code));
            }
        }
    }

    using InventoryReadResult = std::variant<
            ReviewedSourceTreeInventory,
            TrustedGitReviewFailure>;
    const std::size_t stream_limit = review_machine_stream_limit();
    const auto read_inventory = [&](
                                        const std::string& object_id,
                                        ReviewedSourceMachineStream stream,
                                        TrustedGitReviewStage stage)
            -> InventoryReadResult {
        CapturedCommandResult captured = capture_review_git(
                current, expected_remote_url,
                {"ls-tree", "-r", "-z", "--full-tree", "--no-abbrev",
                 "--format=%(objectmode)%x00%(objecttype)%x00%(objectname)%x00%(objectsize)",
                 object_id, "--"},
                "git ls-tree -r -z --full-tree <pinned-commit> metadata",
                stream_limit);
        if(captured.stdout_capture_limit_exceeded) {
            return capture_limit_failure(stage, stream_limit);
        }
        if(captured.exit_code != 0) {
            return command_failure(stage, captured.exit_code);
        }
        // LANDMINE(#411): Git 2.55 custom %(path) formatting C-quotes some
        // non-UTF-8/control-byte names even with -z. Keep metadata path-free
        // and bind it by record order to the separate raw --name-only -z stream.
        CapturedCommandResult captured_paths = capture_review_git(
                current, expected_remote_url,
                {"ls-tree", "-r", "-z", "--full-tree", "--name-only",
                 object_id, "--"},
                "git ls-tree -r -z --full-tree <pinned-commit> paths",
                stream_limit);
        if(captured_paths.stdout_capture_limit_exceeded) {
            return capture_limit_failure(stage, stream_limit);
        }
        if(captured_paths.exit_code != 0) {
            return command_failure(stage, captured_paths.exit_code);
        }
        ReviewedSourceTreeParseResult parsed =
                parse_reviewed_source_tree_output(
                        captured.output, captured_paths.output,
                        configuration.object_format, stream);
        if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed)) {
            return map_projection_failure(
                    std::get<ReviewedSourceProjectionFailure>(parsed));
        }
        return std::get<ReviewedSourceTreeInventory>(std::move(parsed));
    };

    InventoryReadResult target_read = read_inventory(
            target_oid, ReviewedSourceMachineStream::TargetTree,
            TrustedGitReviewStage::TargetTree);
    if(std::holds_alternative<TrustedGitReviewFailure>(target_read)) {
        return finish(std::get<TrustedGitReviewFailure>(target_read));
    }
    ReviewedSourceTreeInventory target_inventory =
            std::get<ReviewedSourceTreeInventory>(std::move(target_read));

    ReviewedSourceTreeInventory baseline_inventory;
    std::string diff_baseline = reviewed_source_empty_tree_object_id(
                                        configuration.object_format)
                                        .value();
    if(baseline.has_value() && !is_rebaseline) {
        diff_baseline = require_known_commit(*baseline);
        InventoryReadResult baseline_read = read_inventory(
                diff_baseline, ReviewedSourceMachineStream::BaselineTree,
                TrustedGitReviewStage::BaselineTree);
        if(std::holds_alternative<TrustedGitReviewFailure>(baseline_read)) {
            return finish(std::get<TrustedGitReviewFailure>(baseline_read));
        }
        baseline_inventory =
                std::get<ReviewedSourceTreeInventory>(
                        std::move(baseline_read));
    }

    ReviewedSourceResourcePreflightResult resource_preflight =
            preflight_reviewed_source_projection_resources(
                    baseline_inventory, target_inventory, detect_renames);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(
               resource_preflight)) {
        return finish(map_projection_failure(
                std::get<ReviewedSourceProjectionFailure>(
                        resource_preflight)));
    }

    using DiffReadResult = std::variant<std::string, TrustedGitReviewFailure>;
    const auto read_diff = [&](
                                   const std::string& output_option,
                                   TrustedGitReviewStage stage)
            -> DiffReadResult {
        if(const auto attribute_override =
                   require_no_attribute_override(current)) {
            return *attribute_override;
        }
        CapturedCommandResult captured = capture_review_git(
                current, expected_remote_url,
                reviewed_source_diff_arguments(
                        output_option, diff_baseline, target_oid,
                        detect_renames),
                output_option == "--name-status"
                        ? "git diff-tree -z --name-status <baseline> <target>"
                        : "git diff-tree -z --numstat <baseline> <target>",
                stream_limit, target_oid);
        if(const auto attribute_override =
                   require_no_attribute_override(current)) {
            return *attribute_override;
        }
        if(captured.stdout_capture_limit_exceeded) {
            return capture_limit_failure(stage, stream_limit);
        }
        if(captured.exit_code != 0) {
            return command_failure(stage, captured.exit_code);
        }
        return std::move(captured.output);
    };

    DiffReadResult name_status = read_diff(
            "--name-status", TrustedGitReviewStage::NameStatus);
    if(std::holds_alternative<TrustedGitReviewFailure>(name_status)) {
        return finish(std::get<TrustedGitReviewFailure>(name_status));
    }
    DiffReadResult numstat = read_diff(
            "--numstat", TrustedGitReviewStage::Numstat);
    if(std::holds_alternative<TrustedGitReviewFailure>(numstat)) {
        return finish(std::get<TrustedGitReviewFailure>(numstat));
    }

    ReviewedSourceChangeAssemblyResult assembled =
            assemble_reviewed_source_changes(
                    baseline_inventory, target_inventory,
                    std::get<std::string>(name_status),
                    std::get<std::string>(numstat), detect_renames);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(assembled)) {
        return finish(map_projection_failure(
                std::get<ReviewedSourceProjectionFailure>(assembled)));
    }
    std::vector<ReviewedSourceFileChange> changes =
            std::get<std::vector<ReviewedSourceFileChange>>(
                    std::move(assembled));

    if(!baseline.has_value()) {
        return finish(ReviewedSourceInitialFullReview{
                target, std::move(changes)});
    }
    if(is_rebaseline) {
        return finish(ReviewedSourceRebaselineFullReview{
                *baseline, target,
                ReviewedSourceBaselineUnavailableReason::MissingOrNotCommit,
                std::move(changes)});
    }
    if(!history_relation.has_value()) {
        return finish(review_failure(
                TrustedGitReviewFailureReason::InconsistentMachineOutput,
                TrustedGitReviewStage::CrossStream));
    }
    return finish(ReviewedSourceUpdateReview{
            *baseline, target, *history_relation, std::move(changes)});
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_GIT_TEST_HOOKS
void set_trusted_git_review_machine_stream_limit_for_test(
        std::optional<std::size_t> limit) {
    g_review_machine_stream_limit = limit;
}
#endif

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
