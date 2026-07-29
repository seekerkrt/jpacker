#include "commands_source_maintenance.hpp"

#include "application_identity.hpp"
#include "app_config.hpp"
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
            throw std::runtime_error("Unsafe command token: " + word);
        }
        words.push_back(word);
    }
    if(words.empty()) {
        throw std::runtime_error("Editor command is empty.");
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

std::string prompt_answer_label(bool answer) {
    return answer ? "yes" : "no";
}

bool ask_user(
        const std::string& question,
        PromptDefault default_answer,
        const AppConfig& config) {
    std::optional<bool> default_value = prompt_default_value(default_answer);

    if(config.no_confirm) {
        // POLICY: --noconfirm でも default を持たない prompt は自動回答しない。
        if(default_value.has_value()) {
            Logger::info("Skipping prompt (--noconfirm): " + question + " -> " + prompt_answer_label(default_value.value()));
            return default_value.value();
        }
        throw std::runtime_error("Cannot answer prompt without interaction (--noconfirm): " + question);
    }

    if(!isatty(STDIN_FILENO)) {
        // LANDMINE: 非対話 stdin では、破壊的になり得る yes default を安全に選べない。
        if(default_value.has_value() && default_value.value() == false) {
            Logger::info("Skipping prompt (non-interactive stdin): " + question + " -> no");
            return false;
        }
        throw std::runtime_error("Cannot safely answer prompt with non-interactive stdin: " + question);
    }

    for(;;) {
        std::cout << ":: " << question << " " << prompt_suffix(default_answer) << " ";
        std::string input;
        if(!std::getline(std::cin, input)) {
            throw std::runtime_error("Failed to read prompt input: " + question);
        }

        input = to_lower(trim(input));
        if(input.empty()) {
            if(default_value.has_value()) return default_value.value();
            Logger::warn("Please answer yes or no.");
            continue;
        }
        if(input == "y" || input == "yes") return true;
        if(input == "n" || input == "no") return false;

        Logger::warn("Please answer yes or no.");
    }
}

void present_system_source_upgrade_event(
        const SystemSourceUpgradeEvent& event) {
    switch(event.kind) {
        case SystemSourceUpgradeEventKind::LoadingSourcePreference:
            if(!event.entry_path.has_value()) {
                throw std::logic_error(
                        "Source preference load event has no entry path.");
            }
            Logger::info(
                    "Loading custom build flags from " +
                    event.entry_path->string());
            return;
        case SystemSourceUpgradeEventKind::SourcePreferenceWarning:
        case SystemSourceUpgradeEventKind::InvalidPreferenceWarning:
            Logger::warn(event.diagnostic);
            return;
        case SystemSourceUpgradeEventKind::SystemUpgradeStarting:
            Logger::info("System upgrade...");
            return;
        case SystemSourceUpgradeEventKind::CheckingSourcePackages:
            Logger::info("Checking source packages...");
            return;
    }
    throw std::logic_error("Unknown system/source upgrade event kind.");
}

[[noreturn]] void throw_system_source_upgrade_failure(
        const SystemSourceUpgradeResult& result) {
    std::optional<std::string> diagnostic = result.failure_diagnostic();
    if(diagnostic.has_value()) throw std::runtime_error(*diagnostic);
    throw std::logic_error(
            "System/source upgrade stopped without a failure diagnostic.");
}

} // namespace

int cmd_build(
        const std::vector<std::string>& args,
        const AppConfig& config) {
    if(args.empty()) {
        Logger::error(
                "Usage: " + std::string(application_identity::COMMAND_NAME) +
                " build <pkg> [VAR=VAL...]");
        return 1;
    }
    std::string            pkg_name;
    SourceBuildEnvironment custom_env;
    for(const auto& arg : args) {
        std::string key, val;
        if(split_env_assignment(arg, key, val))
            custom_env.ordered_assignments.push_back({key, val});
        else if(arg.find('=') != std::string::npos) {
            Logger::error("Invalid environment assignment: " + arg);
            return 1;
        } else if(pkg_name.empty())
            pkg_name = arg;
        else
            Logger::warn("Ignoring extra arg '" + arg + "'");
    }
    if(pkg_name.empty()) {
        Logger::error("No package specified.");
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
        Logger::error(std::string("Build Error: ") + e.what());
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
                Logger::error("Failed to add " + arg);
                failed = true;
            } else {
                Logger::info("Added " + arg + " to source-build list.");
                current_pkgs.push_back(p.string());
            }
        } else if(split_env_assignment(arg, key, val)) {
            if(current_pkgs.empty()) {
                Logger::error("Environment assignment requires a preceding package: " + arg);
                failed = true;
                continue;
            }
            for(const auto& pkg_path : current_pkgs) {
                Logger::info("   -> Appending " + arg + " to " + pkg_path);
                if(run_command("printf '%s\\n' " + shell_words::quote(key + "=" + val) + " | sudo tee -a " + shell_words::quote(pkg_path) + " > /dev/null") != 0) {
                    Logger::error("Failed to append " + key + " to " + pkg_path);
                    failed = true;
                }
            }
        } else {
            Logger::error("Invalid environment assignment: " + arg);
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

int cmd_edit_src(
        const std::vector<std::string>& targets,
        const AppConfig& config) {
    bool        failed = false;
    const char* env_editor = std::getenv("EDITOR");
    std::string editor_cmd = (env_editor) ? std::string(env_editor) : config.editor;
    for(const auto& pkg : targets) {
        fs::path    p = source_preference_entry_path(pkg);
        std::string temp_template = "/tmp/moguet-edit-src-" + pkg + ".XXXXXX";
        std::vector<char> temp_name(temp_template.begin(), temp_template.end());
        temp_name.push_back('\0');

        int fd = mkstemp(temp_name.data());
        if(fd == -1) {
            Logger::error("Failed to create temporary file: " + std::string(std::strerror(errno)));
            failed = true;
            continue;
        }

        fs::path temp_path = temp_name.data();
        if(close(fd) != 0) {
            Logger::error("Failed to close temporary file " + temp_path.string() + ": " + std::string(std::strerror(errno)));
            std::error_code ec;
            fs::remove(temp_path, ec);
            failed = true;
            continue;
        }

        auto cleanup_temp = [&temp_path]() {
            std::error_code ec;
            fs::remove(temp_path, ec);
            if(ec) Logger::warn("Failed to remove temporary file " + temp_path.string() + ": " + ec.message());
        };

        if(fs::exists(p)) {
            std::ifstream src(p, std::ios::binary);
            if(!src) {
                Logger::error("Failed to read " + p.string());
                cleanup_temp();
                failed = true;
                continue;
            }

            std::ofstream dst(temp_path, std::ios::binary | std::ios::trunc);
            if(!dst) {
                Logger::error("Failed to write temporary file " + temp_path.string());
                cleanup_temp();
                failed = true;
                continue;
            }

            dst << src.rdbuf();
            dst.close();
            if(!dst) {
                Logger::error("Failed to copy " + p.string() + " to " + temp_path.string());
                cleanup_temp();
                failed = true;
                continue;
            }
        }

        if(run_command(build_editor_command(editor_cmd, temp_path)) != 0) {
            Logger::error("Editor failed for " + p.string());
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
            Logger::error(
                    "Failed to open edited temporary file " + temp_path.string() + ": " +
                    std::string(std::strerror(open_error)));
            failed = true;
            cleanup_temp();
            continue;
        }

        auto close_source = [&]() {
            if(close(source_fd) == 0) return true;
            int close_error = errno;
            Logger::error(
                    "Failed to close edited temporary file " + temp_path.string() + ": " +
                    std::string(std::strerror(close_error)));
            return false;
        };

        struct stat source_status {};
        if(fstat(source_fd, &source_status) != 0) {
            int stat_error = errno;
            Logger::error(
                    "Failed to inspect edited temporary file " + temp_path.string() + ": " +
                    std::string(std::strerror(stat_error)));
            close_source();
            failed = true;
            cleanup_temp();
            continue;
        }
        if(!S_ISREG(source_status.st_mode)) {
            Logger::error("Edited temporary file is not a regular file: " + temp_path.string());
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
            Logger::error("Failed to install edited source-build preference to " + p.string() + "; edited file kept at " + temp_path.string());
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
        std::cout << "No source-build packages registered." << std::endl;
        return;
    }
    std::cout << "\033[1mRegistered Source Packages:\033[0m" << std::endl;
    bool found = false;
    for(const auto& entry : source_preference_entries()) {
        if(entry.is_regular_file()) {
            found = true;
            std::string pkg = entry.path().filename().string();
            std::cout << "  \033[1;36m" << pkg << "\033[0m" << std::endl;
            read_source_preference_entry(
                    entry.path(),
                    [](const std::string& line) {
                        std::cout << "    " << line << std::endl;
                    });
        }
    }
    if(!found) std::cout << "  (none)" << std::endl;
}

int cmd_del_src(const std::vector<std::string>& targets) {
    bool failed = false;
    for(const auto& pkg : targets) {
        fs::path p = source_preference_entry_path(pkg);
        Logger::info("Removing " + pkg + " from list...");
        if(run_command("sudo rm -f " + shell_words::quote(p.string())) != 0) {
            Logger::error("Failed to remove " + pkg);
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
            Logger::info("Unmarking source-build for " + pkg);
            if(run_command("sudo rm -f " + shell_words::quote(p.string())) != 0) {
                Logger::error("Failed to remove " + pkg);
                failed = true;
                continue;
            }
        } else
            Logger::warn(pkg + " was not marked.");
        if(is_repo_package(pkg)) {
            Logger::info(pkg + " exists in official repos. Will reinstall binary.");
            reinstall_targets.push_back(pkg);
        } else
            Logger::info(pkg + " is likely an AUR package. Config removed only.");
    }
    if(!reinstall_targets.empty()) {
        std::string pkg_list = shell_words::join(reinstall_targets);
        std::vector<std::string> pacman_args = {"-S"};
        pacman_args.insert(pacman_args.end(), reinstall_targets.begin(), reinstall_targets.end());
        Logger::info("Reinstalling binaries: " + pkg_list);
        if(run_command("sudo pacman " + join_pacman_args(pacman_args, config)) != 0) throw std::runtime_error("Failed to reinstall binaries.");
    }
    if(failed) throw std::runtime_error("Failed to revert one or more packages.");
}

int cmd_clean(const AppConfig& config) {
    // POLICY(#175): validate every cache deletion target before pacman mutation, then revalidate before remove_all.
    // Safe-path UX remains pacman clean -> Moguet cache prompt; unsafe cache state stops before either mutation.
    ValidatedCacheRoot              cache = prepare_trusted_cache_root();
    std::vector<ValidatedCachePath> cleanup_targets = preflight_cache_cleanup(cache);
    bool                            cache_has_entries = !fs::is_empty(cache.canonical_path());
    bool                            failed = false;
    Logger::info("Cleaning package caches...");
    if(run_command("sudo pacman " + join_pacman_args({"-Sc"}, config)) != 0) {
        Logger::warn("Pacman clean failed or cancelled.");
        failed = true;
    }
    if(cache_has_entries) {
        if(ask_user("Clean Moguet build cache (" + cache.path().string() + ")?", PromptDefault::No, config)) {
            Logger::info("Removing cached build files...");
            bool cleanup_failed = false;
            for(const auto& target : cleanup_targets) {
                try {
                    remove_trusted_cache_path(target);
                } catch(const std::exception& e) {
                    Logger::error("Failed to remove " + target.path().string() + ": " + e.what());
                    cleanup_failed = true;
                }
            }
            if(cleanup_failed) {
                failed = true;
                Logger::warn("Moguet cache cleanup was incomplete.");
            } else {
                Logger::info("Moguet cache cleaned.");
            }
        } else
            Logger::info("Skipped Moguet cache cleaning.");
    } else
        Logger::info("Moguet cache is empty.");
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
