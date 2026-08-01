#include "commands_source_maintenance.hpp"

#include "application_identity.hpp"
#include "app_config.hpp"
#include "cache_authority.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_metadata.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "separated_source_build.hpp"
#include "shell_words.hpp"
#include "source_environment.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"
#include "system_source_upgrade.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum class PromptDefault {
    Yes,
    No,
    None,
};

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return str;
}

bool is_safe_command_token(const std::string& token) {
    if(token.empty()) return false;
    return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '/' || ch == '.' || ch == '_' || ch == '+' || ch == '-' || ch == '=' ||
               ch == ':' || ch == '@' || ch == '%';
    });
}

std::vector<std::string> split_command_words(const std::string& command) {
    std::stringstream        ss(command);
    std::string              word;
    std::vector<std::string> words;
    while(ss >> word) {
        if(!is_safe_command_token(word)) {
            // TRANSLATORS: The placeholder is an editor command token.
            throw std::runtime_error(localization::format_translated_message(
                    "Unsafe command token: {}", word));
        }
        words.push_back(word);
    }
    if(words.empty()) {
        throw std::runtime_error(localization::translate_message(
                "Editor command is empty."));
    }
    return words;
}

std::vector<std::string> pacman_args_with_global_options(
        std::vector<std::string> args,
        const AppConfig& config) {
    if(config.no_confirm) {
        // POLICY(#173): generated optionはoperation直後へ置き、semantic `--`やoption valueを再解釈しない。
        // 認識済みglobal tokenはordered viewから除外済みなので、ここでは常に1件だけ生成する。
        if(args.empty())
            args.push_back("--noconfirm");
        else
            args.insert(args.begin() + 1, "--noconfirm");
    }
    return args;
}

std::string join_pacman_args(
        const std::vector<std::string>& args,
        const AppConfig& config) {
    return shell_words::join(pacman_args_with_global_options(args, config));
}

std::string build_editor_command(const std::string& editor, const fs::path& target) {
    std::vector<std::string> args = split_command_words(editor);
    args.push_back(target.string());
    return shell_words::join(args);
}

std::optional<bool> prompt_default_value(PromptDefault default_answer) {
    switch(default_answer) {
        case PromptDefault::Yes:
            return true;
        case PromptDefault::No:
            return false;
        case PromptDefault::None:
            return std::nullopt;
    }
    return std::nullopt;
}

std::string prompt_suffix(PromptDefault default_answer) {
    // NO_TRANSLATE: These tokens define the accepted/default prompt input.
    switch(default_answer) {
        case PromptDefault::Yes:
            return "[Y/n]";
        case PromptDefault::No:
            return "[y/N]";
        case PromptDefault::None:
            return "[y/n]";
    }
    return "[y/n]";
}

bool ask_user(
        const std::string& question,
        PromptDefault default_answer,
        const AppConfig& config) {
    std::optional<bool> default_value = prompt_default_value(default_answer);

    if(config.no_confirm) {
        // POLICY: --noconfirm でも default を持たない prompt は自動回答しない。
        if(default_value.has_value()) {
            if(default_value.value()) {
                // TRANSLATORS: The placeholders are the literal --noconfirm option and a complete prompt question.
                Logger::info(localization::format_translated_message(
                        "Skipping prompt ({}): {} -> yes",
                        "--noconfirm",
                        question));
            } else {
                // TRANSLATORS: The placeholders are the literal --noconfirm option and a complete prompt question.
                Logger::info(localization::format_translated_message(
                        "Skipping prompt ({}): {} -> no",
                        "--noconfirm",
                        question));
            }
            return default_value.value();
        }
        // TRANSLATORS: The placeholders are the literal --noconfirm option and a complete prompt question.
        throw std::runtime_error(localization::format_translated_message(
                "Cannot answer prompt without interaction ({}): {}",
                "--noconfirm",
                question));
    }

    if(!isatty(STDIN_FILENO)) {
        // LANDMINE: 非対話 stdin では、破壊的になり得る yes default を安全に選べない。
        if(default_value.has_value() && default_value.value() == false) {
            // TRANSLATORS: The placeholders are the literal stdin identity and a complete prompt question.
            Logger::info(localization::format_translated_message(
                    "Skipping prompt (non-interactive {}): {} -> no",
                    "stdin", question));
            return false;
        }
        // TRANSLATORS: The placeholder is a complete prompt question.
        throw std::runtime_error(localization::format_translated_message(
                "Cannot safely answer prompt with non-interactive standard input: {}",
                question));
    }

    for(;;) {
        // NO_TRANSLATE: Prompt framing and response tokens are fixed UI syntax;
        // question is a complete translated sentence.
        std::cout << ":: " << question << " " << prompt_suffix(default_answer) << " ";
        std::string input;
        if(!std::getline(std::cin, input)) {
            // TRANSLATORS: The placeholder is a complete prompt question.
            throw std::runtime_error(localization::format_translated_message(
                    "Failed to read the answer to this prompt: {}", question));
        }

        input = to_lower(trim(input));
        if(input.empty()) {
            if(default_value.has_value()) return default_value.value();
            Logger::warn(localization::translate_message(
                    "Please answer yes or no."));
            continue;
        }
        if(input == "y" || input == "yes") return true;
        if(input == "n" || input == "no") return false;

        Logger::warn(localization::translate_message(
                "Please answer yes or no."));
    }
}

void present_system_source_upgrade_event(
        const SystemSourceUpgradeEvent& event) {
    switch(event.kind) {
        case SystemSourceUpgradeEventKind::LoadingSourcePreference:
            if(!event.entry_path.has_value()) {
                throw std::logic_error(
                        localization::translate_message(
                                "Source preference load event has no entry path."));
            }
            // TRANSLATORS: The placeholder is a source preference file path.
            Logger::info(localization::format_translated_message(
                    "Loading custom build flags from {}.",
                    event.entry_path->string()));
            return;
        case SystemSourceUpgradeEventKind::SourcePreferenceWarning:
        case SystemSourceUpgradeEventKind::InvalidPreferenceWarning:
            Logger::warn(event.diagnostic);
            return;
        case SystemSourceUpgradeEventKind::SystemUpgradeStarting:
            Logger::info(localization::translate_message(
                    "System upgrade..."));
            return;
        case SystemSourceUpgradeEventKind::CheckingSourcePackages:
            Logger::info(localization::translate_message(
                    "Checking source packages..."));
            return;
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system/source upgrade event kind."));
}

[[noreturn]] void throw_system_source_upgrade_failure(
        const SystemSourceUpgradeResult& result) {
    std::optional<std::string> diagnostic = result.failure_diagnostic();
    if(diagnostic.has_value()) throw std::runtime_error(*diagnostic);
    throw std::logic_error(
            localization::translate_message(
                    "System/source upgrade stopped without a failure diagnostic."));
}

} // namespace

int cmd_build(
        const std::vector<std::string>& args,
        const AppConfig& config) {
    if(args.empty()) {
        // TRANSLATORS: The placeholders are literal CLI command, operation, and operand syntax tokens.
        Logger::error(localization::format_translated_message(
                "Usage: {} {} {} {}",
                application_identity::COMMAND_NAME,
                "build", "<pkg>", "[VAR=VAL...]"));
        return 1;
    }
    std::string            pkg_name;
    SourceBuildEnvironment custom_env;
    for(const auto& arg : args) {
        std::string key, val;
        if(split_env_assignment(arg, key, val))
            custom_env.ordered_assignments.push_back({key, val});
        else if(arg.find('=') != std::string::npos) {
            // TRANSLATORS: The placeholder is a literal environment assignment.
            Logger::error(localization::format_translated_message(
                    "Invalid environment assignment: {}", arg));
            return 1;
        } else if(pkg_name.empty())
            pkg_name = arg;
        else
            // TRANSLATORS: The placeholder is a literal CLI argument.
            Logger::warn(localization::format_translated_message(
                    "Ignoring extra arg '{}'", arg));
    }
    if(pkg_name.empty()) {
        Logger::error(localization::translate_message(
                "No package specified."));
        return 1;
    }
    require_valid_package_name(pkg_name);

    try {
        build_source_target(pkg_name, custom_env, config);
    } catch(const SeparatedPackageBaseSourceBuildCleanupError& error) {
        // Direct buildはretained workspaceを手動確認できる既存contractを
        // 維持し、transaction成功済みcleanup failureとしてそのまま表示する。
        Logger::error(error.what());
        return 1;
    } catch(const SeparatedSourceBuildCleanupError& error) {
        // Package transactionは成功済みなので、generic Build Error prefixを付けない。
        Logger::error(error.what());
        return 1;
    } catch(const std::exception& e) {
        // TRANSLATORS: The placeholder is a diagnostic from the failed build.
        Logger::error(localization::format_translated_message(
                "Build Error: {}", e.what()));
        return 1;
    }
    return 0;
}

int cmd_add_src(const std::vector<std::string>& args) {
    bool                     failed = false;
    std::vector<std::string> current_pkgs;
    for(const auto& arg : args) {
        std::string key, val;
        if(arg.find('=') == std::string::npos) {
            // POLICY: 1 package = 1 preference file。ファイル名は package name validation で固定する。
            fs::path p = source_preference_entry_path(arg);
            if(run_command("sudo touch " + shell_words::quote(p.string())) != 0) {
                // TRANSLATORS: The placeholder is a package name.
                Logger::error(localization::format_translated_message(
                        "Failed to add {}", arg));
                failed = true;
            } else {
                // TRANSLATORS: The placeholder is a package name.
                Logger::info(localization::format_translated_message(
                        "Added {} to source-build list.", arg));
                current_pkgs.push_back(p.string());
            }
        } else if(split_env_assignment(arg, key, val)) {
            if(current_pkgs.empty()) {
                // TRANSLATORS: The placeholder is a literal environment assignment.
                Logger::error(localization::format_translated_message(
                        "Environment assignment requires a preceding package: {}",
                        arg));
                failed = true;
                continue;
            }
            for(const auto& pkg_path : current_pkgs) {
                // TRANSLATORS: The placeholders are a literal environment assignment and a source preference file path.
                Logger::info(localization::format_translated_message(
                        "Appending {} to {}", arg, pkg_path));
                if(run_command("printf '%s\\n' " + shell_words::quote(key + "=" + val) + " | sudo tee -a " + shell_words::quote(pkg_path) + " > /dev/null") != 0) {
                    // TRANSLATORS: The placeholders are an environment key and a source preference file path.
                    Logger::error(localization::format_translated_message(
                            "Failed to append environment key {} to {}.",
                            key,
                            pkg_path));
                    failed = true;
                }
            }
        } else {
            // TRANSLATORS: The placeholder is a literal environment assignment.
            Logger::error(localization::format_translated_message(
                    "Invalid environment assignment: {}", arg));
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

int cmd_edit_src(
        const std::vector<std::string>& targets,
        const AppConfig& config) {
    bool failed = false;
    for(const auto& pkg : targets) {
        fs::path    p = source_preference_entry_path(pkg);
        std::string temp_template = "/tmp/moguet-edit-src-" + pkg + ".XXXXXX";
        std::vector<char> temp_name(temp_template.begin(), temp_template.end());
        temp_name.push_back('\0');

        int fd = mkstemp(temp_name.data());
        if(fd == -1) {
            // TRANSLATORS: The placeholder is a system error message.
            Logger::error(localization::format_translated_message(
                    "Failed to create a temporary source preference file: {}",
                    std::strerror(errno)));
            failed = true;
            continue;
        }

        fs::path temp_path = temp_name.data();
        if(close(fd) != 0) {
            // TRANSLATORS: The placeholders are a temporary file path and a system error message.
            Logger::error(localization::format_translated_message(
                    "Failed to close temporary file {}: {}",
                    temp_path.string(),
                    std::strerror(errno)));
            std::error_code ec;
            fs::remove(temp_path, ec);
            failed = true;
            continue;
        }

        auto cleanup_temp = [&temp_path]() {
            std::error_code ec;
            fs::remove(temp_path, ec);
            if(ec) {
                // TRANSLATORS: The placeholders are a temporary file path and a system error message.
                Logger::warn(localization::format_translated_message(
                        "Failed to remove temporary file {}: {}",
                        temp_path.string(),
                        ec.message()));
            }
        };

        if(fs::exists(p)) {
            std::ifstream src(p, std::ios::binary);
            if(!src) {
                // TRANSLATORS: The placeholder is a source preference file path.
                Logger::error(localization::format_translated_message(
                        "Failed to read source preference file {}.",
                        p.string()));
                cleanup_temp();
                failed = true;
                continue;
            }

            std::ofstream dst(temp_path, std::ios::binary | std::ios::trunc);
            if(!dst) {
                // TRANSLATORS: The placeholder is a temporary file path.
                Logger::error(localization::format_translated_message(
                        "Failed to write temporary file {}.",
                        temp_path.string()));
                cleanup_temp();
                failed = true;
                continue;
            }

            dst << src.rdbuf();
            dst.close();
            if(!dst) {
                // TRANSLATORS: The placeholders are the source preference path and temporary file path.
                Logger::error(localization::format_translated_message(
                        "Failed to copy source preference file {} to temporary file {}.",
                        p.string(),
                        temp_path.string()));
                cleanup_temp();
                failed = true;
                continue;
            }
        }

        if(run_command(build_editor_command(config.editor, temp_path)) != 0) {
            // TRANSLATORS: The placeholder is a source preference file path.
            Logger::error(localization::format_translated_message(
                    "Editor failed for {}",
                    p.string()));
            failed = true;
            cleanup_temp();
            continue;
        }

        // POLICY: editorのrename型saveを許容しつつ、最終pathnameは権限昇格前にnofollowでpinする。
        int source_fd = open(
                temp_path.c_str(),
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if(source_fd == -1) {
            int open_error = errno;
            // TRANSLATORS: The placeholders are a temporary file path and a system error message.
            Logger::error(localization::format_translated_message(
                    "Failed to open edited temporary file {}: {}",
                    temp_path.string(),
                    std::strerror(open_error)));
            failed = true;
            cleanup_temp();
            continue;
        }

        auto close_source = [&]() {
            if(close(source_fd) == 0) return true;
            int close_error = errno;
            // TRANSLATORS: The placeholders are a temporary file path and a system error message.
            Logger::error(localization::format_translated_message(
                    "Failed to close edited temporary file {}: {}",
                    temp_path.string(),
                    std::strerror(close_error)));
            return false;
        };

        struct stat source_status {};
        if(fstat(source_fd, &source_status) != 0) {
            int stat_error = errno;
            // TRANSLATORS: The placeholders are a temporary file path and a system error message.
            Logger::error(localization::format_translated_message(
                    "Failed to inspect edited temporary file {}: {}",
                    temp_path.string(),
                    std::strerror(stat_error)));
            close_source();
            failed = true;
            cleanup_temp();
            continue;
        }
        if(!S_ISREG(source_status.st_mode)) {
            // TRANSLATORS: The placeholder is a temporary file path.
            Logger::error(localization::format_translated_message(
                    "Edited temporary file is not a regular file: {}",
                    temp_path.string()));
            close_source();
            failed = true;
            cleanup_temp();
            continue;
        }

        // POLICY: root側は固定済みstdinから読み、validated preference destinationへのwriteだけを担当する。
        int install_status = run_command_with_stdin_fd(
                "sudo install -Dm644 -- /dev/stdin " + shell_words::quote(p.string()),
                source_fd);
        bool source_closed = close_source();
        if(install_status != 0) {
            // TRANSLATORS: The placeholders are the destination and retained temporary file paths.
            Logger::error(localization::format_translated_message(
                    "Failed to install the edited source-build preference at {}; the edited file was kept at {}.",
                    p.string(),
                    temp_path.string()));
            failed = true;
            continue;
        }

        if(!source_closed) failed = true;
        cleanup_temp();
    }
    return failed ? 1 : 0;
}

void cmd_list_src() {
    if(!fs::exists(source_preference_root())) {
        std::cout << localization::translate_message(
                             "No source-build packages registered.")
                  << std::endl;
        return;
    }
    std::cout << "\033[1m"
              << localization::translate_message(
                         "Registered Source Packages:")
              << "\033[0m" << std::endl;
    bool found = false;
    for(const auto& entry : source_preference_entries()) {
        if(entry.is_regular_file()) {
            found = true;
            std::string pkg = entry.path().filename().string();
            // NO_TRANSLATE: Package identity and stored environment key/value
            // lines are runtime data and must remain byte-for-byte unchanged.
            std::cout << "  \033[1;36m" << pkg << "\033[0m" << std::endl;
            read_source_preference_entry(
                    entry.path(),
                    [](const std::string& line) {
                        std::cout << "    " << line << std::endl;
                    });
        }
    }
    if(!found) {
        std::cout << "  " << localization::translate_message("(none)")
                  << std::endl;
    }
}

int cmd_del_src(const std::vector<std::string>& targets) {
    bool failed = false;
    for(const auto& pkg : targets) {
        fs::path p = source_preference_entry_path(pkg);
        // TRANSLATORS: The placeholder is a package name.
        Logger::info(localization::format_translated_message(
                "Removing {} from list...", pkg));
        if(run_command("sudo rm -f " + shell_words::quote(p.string())) != 0) {
            // TRANSLATORS: The placeholder is a package name.
            Logger::error(localization::format_translated_message(
                    "Failed to remove {}", pkg));
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

void cmd_revert(
        const std::vector<std::string>& targets,
        const AppConfig& config) {
    bool                     failed = false;
    std::vector<std::string> reinstall_targets;
    for(const auto& pkg : targets) {
        fs::path p = source_preference_entry_path(pkg);
        if(fs::exists(p)) {
            // TRANSLATORS: The placeholder is a package name.
            Logger::info(localization::format_translated_message(
                    "Unmarking source-build for {}", pkg));
            if(run_command("sudo rm -f " + shell_words::quote(p.string())) != 0) {
                // TRANSLATORS: The placeholder is a package name.
                Logger::error(localization::format_translated_message(
                        "Failed to remove {}",
                        pkg));
                failed = true;
                continue;
            }
        } else {
            // TRANSLATORS: The placeholder is a package name.
            Logger::warn(localization::format_translated_message(
                    "{} was not marked.", pkg));
        }
        if(is_repo_package(pkg)) {
            // TRANSLATORS: The placeholder is a package name.
            Logger::info(localization::format_translated_message(
                    "{} exists in official repos. Will reinstall binary.",
                    pkg));
            reinstall_targets.push_back(pkg);
        } else {
            // TRANSLATORS: The placeholders are a package name and the AUR project identity.
            Logger::info(localization::format_translated_message(
                    "{} is likely an {} package. Config removed only.",
                    pkg, "AUR"));
        }
    }
    if(!reinstall_targets.empty()) {
        std::string pkg_list = shell_words::join(reinstall_targets);
        std::vector<std::string> pacman_args = {"-S"};
        pacman_args.insert(pacman_args.end(), reinstall_targets.begin(), reinstall_targets.end());
        // TRANSLATORS: The placeholder is a shell-escaped list of package names.
        Logger::info(localization::format_translated_message(
                "Reinstalling binaries: {}", pkg_list));
        if(run_command("sudo pacman " + join_pacman_args(pacman_args, config)) != 0) {
            throw std::runtime_error(localization::translate_message(
                    "Failed to reinstall binaries."));
        }
    }
    if(failed) {
        throw std::runtime_error(localization::translate_message(
                "Failed to revert one or more packages."));
    }
}

int cmd_clean(const AppConfig& config) {
    // POLICY(#175,#305): new Moguet XDG cache rootだけをadoptし、全targetの
    // descriptor-relative preflight capabilityをpacman/promptより前に構築する。
    // 同じmove-only capabilityをconsumeまで保持し、original inodeをpinする。
    ValidatedCacheRoot cache = prepare_process_cache_root();
    PreparedCacheCleanup cleanup =
            preflight_cache_cleanup(cache);
    const bool cache_has_entries = !cleanup.empty();
    bool       failed = false;
    Logger::info(localization::translate_message(
            "Cleaning package caches..."));
    if(run_command("sudo pacman " + join_pacman_args({"-Sc"}, config)) != 0) {
        // TRANSLATORS: The placeholder is the literal pacman program identity, capitalized to preserve the existing CLI contract.
        Logger::warn(localization::format_translated_message(
                "{} clean failed or cancelled.", "Pacman"));
        failed = true;
    }
    if(cache_has_entries) {
        // TRANSLATORS: The placeholders are the Moguet identity and build cache path.
        const std::string clean_question =
                localization::format_translated_message(
                        "Clean {} build cache ({})?",
                        application_identity::PROJECT_NAME,
                        cleanup.cache_path().string());
        if(ask_user(clean_question, PromptDefault::No, config)) {
            Logger::info(localization::translate_message(
                    "Removing cached build files..."));
            bool cleanup_failed = false;
            try {
                // new root内のlegacy-named logもordinary cache entry。legacy sibling
                // rootやXDG state/configは列挙・削除対象へ入らない。
                remove_preflighted_cache_paths(std::move(cleanup));
            } catch(const std::exception& e) {
                // TRANSLATORS: The placeholders are the Moguet identity and a cache cleanup diagnostic.
                Logger::error(localization::format_translated_message(
                        "Failed to clean the {} cache: {}",
                        application_identity::PROJECT_NAME,
                        e.what()));
                failed = true;
                cleanup_failed = true;
                Logger::warn(localization::format_translated_message(
                        "{} cache cleanup was incomplete.",
                        application_identity::PROJECT_NAME));
            }
            if(!cleanup_failed) {
                Logger::info(localization::format_translated_message(
                        "{} cache cleaned.",
                        application_identity::PROJECT_NAME));
            }
        } else {
            Logger::info(localization::format_translated_message(
                    "Skipped {} cache cleaning.",
                    application_identity::PROJECT_NAME));
        }
    } else {
        Logger::info(localization::format_translated_message(
                "{} cache is empty.",
                application_identity::PROJECT_NAME));
    }
    return failed ? 1 : 0;
}

int cmd_upgrade(const AppConfig& config) {
    SystemSourceUpgradePreparation preparation =
            prepare_system_source_upgrade(
                    config, present_system_source_upgrade_event);
    if(const auto* blocked =
               std::get_if<SystemSourceUpgradeResult>(&preparation)) {
        throw_system_source_upgrade_failure(*blocked);
    }

    SystemSourceUpgradeResult result =
            execute_prepared_system_source_upgrade(
                    std::move(
                            std::get<PreparedSystemSourceUpgrade>(
                                    preparation)),
                    config,
                    present_system_source_upgrade_event);
    if(result.status != SystemSourceUpgradeStatus::Completed) {
        throw_system_source_upgrade_failure(result);
    }

    // POLICY(#281): typed Incomplete（従来のunknown update status skip）は
    // aggregateへ残すが、legacy upgradeのexit statusは変えない。
    const bool has_invalid_preference = std::any_of(
            result.registered_source_results.begin(),
            result.registered_source_results.end(),
            [](const RegisteredSourceUpgradeResult& source) {
                return source.failure_kind ==
                        RegisteredSourceUpgradeFailureKind::
                                InvalidPreferenceName;
            });
    return has_invalid_preference ? 1 : 0;
}
