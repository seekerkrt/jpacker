/**
 * Moguet - A full-featured Pacman wrapper and AUR helper
 *
 * Features:
 * - Smart Upgrade: Skips rebuilding packages if the version hasn't changed during 'upgrade'.
 * - Strict typed user configuration with CLI override composition.
 * - '--nodiff' option support.
 * - '--noconfirm' option support.
 */

// このファイルは、Moguet の CLI 入口、pacman wrapper、AUR/source build 補助をまとめる実装単位。
// 関数宣言と詳細実装は、将来の分割候補が見えるように section comment で大まかな責務ごとに分類する。

#include "application_identity.hpp"
#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "cli_authority.hpp"
#include "cli_parser.hpp"
#include "cli_routing.hpp"
#include "commands_aur_update.hpp"
#include "commands_inspect.hpp"
#include "commands_source_maintenance.hpp"
#include "commands_sync.hpp"
#include "commands_upgrade_all.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "process.hpp"
#include "shell_words.hpp"
#include "source_install.hpp"
#include "user_config.hpp"
#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"
#include "xdg_state_log.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

AppConfig g_config;

constexpr std::size_t HELP_DESCRIPTION_COLUMN = 42;

void print_help_section(const std::string& heading) {
    std::cout << "\033[1m" << heading << "\033[0m" << std::endl;
}

void print_help_entry(
        std::string_view syntax, const std::string& description) {
    std::cout << "    \033[1m" << syntax << "\033[0m";
    const std::size_t visible_width = 4 + syntax.size();
    if(visible_width >= HELP_DESCRIPTION_COLUMN) {
        std::cout << '\n'
                  << std::string(HELP_DESCRIPTION_COLUMN, ' ');
    } else {
        std::cout << std::string(
                HELP_DESCRIPTION_COLUMN - visible_width, ' ');
    }
    std::cout << description << std::endl;
}

void print_help_continuation(const std::string& description) {
    std::cout << std::string(HELP_DESCRIPTION_COLUMN, ' ')
              << description << std::endl;
}

} // namespace

// --- 関数宣言 ---

// CLI入口 / help
int run_moguet(int argc, char* argv[]);
void print_help();
bool handle_info_only_option(int argc, char* argv[]);
bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]);

// shell引数 / command construction
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args);
std::string join_pacman_args(const std::vector<std::string>& args);

// pacman / repository補助
bool validate_optionless_moguet_operation(const std::string& operation, const std::vector<std::string>& flags);
namespace {

UserConfig load_invocation_user_config() {
#ifdef MOGUET_ENABLE_TEST_CONFIG_PATH
    // POLICY: productionはXDG authorityを使い、専用test binaryだけが
    // strict TOML loaderの明示pathを選ぶ。
    const char* test_config_path = std::getenv("MOGUET_TEST_CONFIG_FILE");
    if(test_config_path && test_config_path[0] != '\0') {
        return load_user_config(test_config_path);
    }
#endif
    const xdg_paths::ConfigPaths config_paths =
            xdg_paths::resolve_config_process_environment();
    return load_user_config(config_paths.config_file);
}

bool is_known_moguet_operation(const std::string& operation) {
    return cli_authority::find_moguet_operation(operation) != nullptr;
}

bool operation_requires_target(const std::string& operation) {
    const cli_authority::OperationSpec* spec =
            cli_authority::find_moguet_operation(operation);
    return spec != nullptr && spec->requires_target;
}

bool validate_pre_log_operation_route(const ParsedCliArguments& parsed) {
    if(!parsed.operation.empty() && parsed.operation.front() != '-' &&
       !is_known_moguet_operation(parsed.operation)) {
        Logger::error("Unknown operation: " + parsed.operation);
        return false;
    }
    if(operation_requires_target(parsed.operation) && parsed.targets.empty()) {
        if(parsed.operation ==
           cli_authority::operation_spec(
                   cli_authority::OperationId::Build)
                   .token) {
            Logger::error("Usage: moguet build <pkg> [VAR=VAL...]");
        } else if(parsed.operation ==
                  cli_authority::operation_spec(
                          cli_authority::OperationId::Deps)
                          .token) {
            Logger::error("Usage: moguet deps [--recursive] <pkg>");
        } else if(parsed.operation ==
                  cli_authority::operation_spec(
                          cli_authority::OperationId::Plan)
                          .token) {
            Logger::error("Usage: moguet plan <pkg>");
        } else if(parsed.operation ==
                  cli_authority::operation_spec(
                          cli_authority::OperationId::Fetch)
                          .token) {
            Logger::error("Usage: moguet fetch <pkg>");
        } else {
            Logger::error("Missing target for " + parsed.operation);
        }
        return false;
    }
    return true;
}

} // namespace

// --- CLI 入口 ---
int run_moguet(int argc, char* argv[]) {
    if(handle_info_only_option(argc, argv)) return 0;

    // POLICY(#167): parser/root-check failureも -Gp のmachine-readable stdoutへ混ぜない。
    if(argv_requests_pkgbuild_export_diagnostics(argc, argv)) {
        Logger::set_diagnostics_to_stderr();
    }

    if(geteuid() == 0) {
        Logger::error(
                localization::format_translated_message(
                        "Do not run {} as root or with sudo.",
                        application_identity::PROJECT_NAME));
        Logger::error(
                localization::format_translated_message(
                        "Run {} as a normal user; {} will invoke sudo/pacman when needed.",
                        application_identity::COMMAND_NAME,
                        application_identity::PROJECT_NAME));
        return 1;
    }

    if(argc < 2) {
        print_help();
        return 1;
    }

    // Configはparse成功までlocalに保持し、invalid inputから最終実行設定への
    // partial publishを防ぐ。missing leafだけがbuilt-in defaultになる。
    UserConfig user_config;
    try {
        user_config = load_invocation_user_config();
    } catch(const std::exception& e) {
        Logger::error(e.what());
        return 1;
    } catch(...) {
        Logger::error("Failed to load user config: unknown error.");
        return 1;
    }

    std::optional<ParsedCliArguments> parsed_result = parse_cli_arguments(argc, argv);
    if(!parsed_result.has_value()) return 1;

    const ParsedCliArguments&        parsed = parsed_result.value();
    if(parsed.operation.empty()) {
        // POLICY: usage presentationはrunnerが所有し、CLI override publish前に終了する。
        print_help();
        return 1;
    }
    UserConfig final_user_config = compose_user_config(
            std::move(user_config), parsed.cli_overrides);
    g_config = make_app_config(
            std::move(final_user_config),
            parsed.cli_overrides.no_confirm,
            parsed.cli_overrides.rm_deps);

    if(parsed.operation ==
       cli_authority::operation_spec(
               cli_authority::OperationId::UpgradeAll)
               .token) {
        // POLICY(#281): upgrade-all is target-less and does not inherit
        // pacman operands. Reject misuse before default state log
        // initialization or any source/cache/inventory/AUR preparation.
        const std::vector<std::string> validation_errors =
                validate_upgrade_all_invocation(parsed);
        if(!validation_errors.empty()) {
            for(const std::string& error : validation_errors) {
                Logger::error(error);
            }
            return 1;
        }
        try {
            // Config-file RMDEPS must also fail before preparation, including
            // invocations with no registered source preferences.
            require_supported_production_source_build_options(g_config);
        } catch(const std::exception& error) {
            Logger::error(error.what());
            return 1;
        }
    }

    std::optional<PkgbuildExportMode> export_mode = pkgbuild_export_mode(parsed);
    if(export_mode.has_value()) {
        // POLICY(#167,#305): export/print はdefault state log初期化より前に
        // 分岐し、内部build cacheを作らない。
        Logger::set_diagnostics_to_stderr();
        const std::vector<std::string> validation_errors =
                validate_pkgbuild_export_invocation(parsed);
        if(!validation_errors.empty()) {
            for(const auto& error : validation_errors) Logger::error(error);
            return 1;
        }

        try {
            if(export_mode.value() == PkgbuildExportMode::Tree) {
                return cmd_export_pkgbuild_tree(parsed.targets.front());
            }
            return cmd_print_pkgbuild(parsed.targets.front());
        } catch(const std::exception& e) {
            Logger::error(e.what());
            return 1;
        }
    }

    // POLICY(#168,#305): selector conflict / scope errors must stop before
    // the default state directory or log file is created.
    std::optional<std::string> source_selection_error =
            validate_source_selection_operation(parsed);
    if(source_selection_error.has_value()) {
        Logger::error(source_selection_error.value());
        return 1;
    }

    if(parsed.operation ==
       cli_authority::operation_spec(
               cli_authority::OperationId::UpgradeAur)
               .token) {
        if(!parsed.targets.empty()) {
            Logger::error("upgrade-aur does not accept target operands.");
            return 1;
        }
        try {
            // POLICY(#267): NoUpdatesでも--rmdepsを成功扱いせず、queryやlog/cache
            // 初期化より前に既存separated lifecycleのoption契約で拒否する。
            require_supported_production_source_build_options(g_config);
        } catch(const std::exception& error) {
            Logger::error(error.what());
            return 1;
        }
    }

    const cli_authority::OperationSpec* custom_operation =
            cli_authority::find_moguet_operation(parsed.operation);
    if(custom_operation != nullptr &&
       custom_operation->rejects_options_before_dispatch &&
       !validate_optionless_moguet_operation(parsed.operation, parsed.flags)) {
        // POLICY(#169): unsupported custom-operation options must stop before cache or source mutation.
        return 1;
    }

    if(!validate_pre_log_operation_route(parsed)) return 1;

    try {
        // POLICY(#305,#306): stateだけを解決し、validated directory
        // descriptorからfixed default logをopenしてLoggerへownershipを移す。
        xdg_paths::StatePaths state_paths =
                xdg_paths::resolve_state_process_environment();
        xdg_directory_safety::PreparedDirectory state_directory =
                xdg_directory_safety::prepare_directory(state_paths);
        xdg_state_log::PreparedLogFile state_log =
                xdg_state_log::open_default_state_log(
                        state_paths, state_directory);
        Logger::init(
                std::move(state_log),
                "Started " +
                        std::string(application_identity::PROJECT_NAME) +
                        " v" +
                        std::string(application_identity::VERSION));
    } catch(const std::exception& error) {
        // Default state authorityのfailureはoperation dispatch前にfatal。
        // Logger adoption後のwrite failureでも同じbackendへ再書込しない。
        std::cerr << "\033[1;31m:: Error:\033[0m " << error.what()
                  << std::endl;
        return 1;
    } catch(...) {
        std::cerr
                << "\033[1;31m:: Error:\033[0m Cannot initialize "
                << application_identity::PROJECT_NAME
                << " default state log: unknown error." << std::endl;
        return 1;
    }

#ifdef MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
    // Test-only seam: closed-stdin regression cases need fd 0 to become
    // available again after production state-log initialization.
    const char* release_state_log =
            std::getenv("MOGUET_TEST_RELEASE_STATE_LOG_BEFORE_DISPATCH");
    if(release_state_log && release_state_log[0] != '\0') {
        try {
            Logger::shutdown();
        } catch(const std::exception& error) {
            std::cerr << "\033[1;31m:: Error:\033[0m " << error.what()
                      << std::endl;
            return 1;
        } catch(...) {
            std::cerr
                    << "\033[1;31m:: Error:\033[0m Cannot release "
                    << application_identity::PROJECT_NAME
                    << " test state log: unknown error." << std::endl;
            return 1;
        }
    }
#endif

    const std::string&               operation = parsed.operation;
    const std::vector<std::string>&  args = parsed.ordered_pacman_args;
    const std::vector<std::string>&  targets = parsed.targets;
    const std::vector<std::string>&  flags = parsed.flags;

    const int operation_status = [&]() -> int {
        try {
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Build)
                       .token) {
                return cmd_build(targets, g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Upgrade)
                       .token) {
                return cmd_upgrade(g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::UpgradeAur)
                       .token) {
                return cmd_upgrade_aur(g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::UpgradeAll)
                       .token) {
                return cmd_upgrade_all(g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Clean)
                       .token) {
                return cmd_clean(g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Deps)
                       .token) {
                return cmd_deps(targets, flags);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Plan)
                       .token) {
                return cmd_plan(targets, flags);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::Fetch)
                       .token) {
                return cmd_fetch(targets, flags);
            }
            if(operation ==
                       cli_authority::operation_spec(
                               cli_authority::OperationId::AddSource)
                               .token &&
               !targets.empty()) {
                return cmd_add_src(targets);
            }
            if(operation ==
                       cli_authority::operation_spec(
                               cli_authority::OperationId::DeleteSource)
                               .token &&
               !targets.empty()) {
                return cmd_del_src(targets);
            }
            if(operation ==
                       cli_authority::operation_spec(
                               cli_authority::OperationId::Revert)
                               .token &&
               !targets.empty()) {
                cmd_revert(targets, g_config);
                return 0;
            }
            if(operation ==
                       cli_authority::operation_spec(
                               cli_authority::OperationId::EditSource)
                               .token &&
               !targets.empty()) {
                return cmd_edit_src(targets, g_config);
            }
            if(operation ==
               cli_authority::operation_spec(
                       cli_authority::OperationId::ListSources)
                       .token) {
                cmd_list_src();
                return 0;
            }

            bool requests_refresh = pacman_operation_requests_refresh(operation, flags);
            bool is_sync = operation.starts_with("-S");
            bool is_query = operation.starts_with("-Q");
            bool is_foreign_updates = (is_query && operation.find('u') != std::string::npos && operation.find('a') != std::string::npos);
            SourceSelectableSyncOperation selected_sync_operation = source_selectable_sync_operation(parsed);
            bool is_search = parsed.source_selection == PackageSourceSelection::Auto
                                     ? (is_sync && operation.find('s') != std::string::npos)
                                     : selected_sync_operation == SourceSelectableSyncOperation::Search;
            bool is_info = parsed.source_selection == PackageSourceSelection::Auto
                                   ? (is_sync && operation.find('i') != std::string::npos)
                                   : selected_sync_operation == SourceSelectableSyncOperation::Info;
            bool is_clean = (is_sync && operation.find('c') != std::string::npos);
            bool is_sys_upgrade = (is_sync && (operation.find('u') != std::string::npos || operation.find('y') != std::string::npos));
            bool needs_sudo =
                    is_sync || operation.starts_with("-R") || operation.starts_with("-U") ||
                    operation.starts_with("-D") || (operation.starts_with("-F") && requests_refresh);

            if(is_foreign_updates) return cmd_query_foreign_updates();

            if(is_search) {
                return cmd_sync_search(
                        parsed, requests_refresh, parsed.source_selection, g_config);
            }
            if(is_info) {
                return cmd_sync_info(
                        parsed, requests_refresh, parsed.source_selection, g_config);
            }
            if(is_clean) return run_command("sudo pacman " + join_pacman_args(args));

            if(is_sync) {
                return cmd_sync_install(
                        parsed, is_sys_upgrade, parsed.source_selection, g_config);
            }
            std::string cmd_prefix = needs_sudo ? "sudo pacman " : "pacman ";
            return run_command(cmd_prefix + join_pacman_args(args));
        } catch(const std::exception& e) {
            try {
                Logger::error(e.what());
            } catch(...) {
                // A pending checked state-log failure is reported by shutdown().
            }
            return 1;
        }
    }();

    try {
        Logger::shutdown();
    } catch(const std::exception& error) {
        std::cerr << "\033[1;31m:: Error:\033[0m " << error.what()
                  << std::endl;
        return 1;
    } catch(...) {
        std::cerr
                << "\033[1;31m:: Error:\033[0m Cannot finalize "
                << application_identity::PROJECT_NAME
                << " default state log: unknown error." << std::endl;
        return 1;
    }
    return operation_status;
}

#ifdef MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
namespace {

int verify_parse_failure_does_not_publish_cli_overrides() {
    char program[] = "moguet";
    char no_edit[] = "--noedit";
    char no_diff[] = "--nodiff";
    char no_confirm[] = "--noconfirm";
    char build_mode[] = "--build-mode=rebuild";
    char rebuild_alias[] = "--rebuild";
    char rm_deps[] = "--rmdeps";
    char operation[] = "-Q";
    char missing_value_option[] = "--config";
    std::array<char*, 9> parse_failure_argv = {
            program, no_edit, no_diff, no_confirm, build_mode,
            rebuild_alias, rm_deps, operation, missing_value_option};

    if(run_moguet(static_cast<int>(parse_failure_argv.size()), parse_failure_argv.data()) != 1) {
        std::cerr << "Expected CLI parse failure." << std::endl;
        return 1;
    }
    if(g_config.user_config.review.pkgbuild != ReviewPolicy::Prompt ||
       g_config.user_config.review.diff != ReviewPolicy::Prompt ||
       g_config.user_config.build.mode != BuildMode::Normal ||
       g_config.no_confirm || g_config.rm_deps) {
        std::cerr << "CLI parse failure published partial config overrides." << std::endl;
        return 1;
    }
    return 0;
}

} // namespace
#endif

int main(int argc, char* argv[]) {
    if(!localization::initialize_runtime_catalog()) {
        Logger::error("Failed to initialize the Moguet message catalog.");
        return 1;
    }
#ifdef MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS
    const char* app_config_test_case = std::getenv("MOGUET_TEST_APP_CONFIG_CASE");
    if(app_config_test_case && std::string(app_config_test_case) == "parse-failure-cli-overrides") {
        return verify_parse_failure_does_not_publish_cli_overrides();
    }
#endif
    return run_moguet(argc, argv);
}

// --- 関数実装 ---

// CLI入口 / help
void print_help() {
    using cli_authority::GlobalOptionId;
    using cli_authority::OperationId;

    std::cout << "\033[1;36m" << application_identity::PROJECT_NAME
              << "\033[0m v"
              << application_identity::VERSION << "\n"
              << std::endl;
    print_help_section(localization::translate_message("USAGE"));
    std::cout << "    " << application_identity::COMMAND_NAME
              << " <op> [options] [targets...]\n"
              << std::endl;
    print_help_section(localization::translate_message("OPERATIONS"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Build).help_syntax,
            localization::translate_message(
                    "Build one package from source without saving a preference"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Upgrade).help_syntax,
            localization::translate_message(
                    "Update the system and rebuild configured source packages"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal pacman-compatible -Syu token.
                    "Check registered source-build preferences after {}",
                    cli_authority::PACMAN_SYSTEM_UPGRADE_SYNTAX));
    print_help_entry(
            cli_authority::operation_spec(OperationId::UpgradeAur).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "Update installed {} packages only", "AUR"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal pacman-compatible -Syu token.
                    "Do not run {}; source-build preferences are optional",
                    cli_authority::PACMAN_SYSTEM_UPGRADE_SYNTAX));
    print_help_entry(
            cli_authority::operation_spec(OperationId::UpgradeAll).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "Update the system, registered source packages, and remaining installed {} packages",
                    "AUR"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal PackageBase identity.
                    "Give explicit source preferences priority and avoid duplicate package/{} builds",
                    "PackageBase"));
    print_help_continuation(localization::translate_message(
            "Do not accept target operands"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Clean).help_syntax,
            localization::translate_message("Clean package and build caches"));
    std::cout << std::endl;
    print_help_section(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "{} INSPECTION", "AUR"));
    print_help_entry(
            cli_authority::PKGBUILD_EXPORT_SYNTAX,
            localization::format_translated_message(
                    // TRANSLATORS: AUR, PackageBase, and the destination path are literal identities.
                    "Export one {} {} repository to {} without building or installing",
                    "AUR", "PackageBase", "./<PackageBase>"));
    print_help_entry(
            cli_authority::PKGBUILD_PRINT_SYNTAX,
            localization::format_translated_message(
                    // TRANSLATORS: AUR, PackageBase, PKGBUILD, and stdout are literal identities.
                    "Print one {} {} {} to {} without keeping a checkout",
                    "AUR", "PackageBase", "PKGBUILD", "stdout"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Deps).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "Classify {} dependencies", "AUR"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Plan).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "Show the {} build-order plan", "AUR"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Fetch).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholders are literal git commands and the AUR identity.
                    "Safely run {} or {} for {} build repositories",
                    "git clone", "git fetch", "AUR"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholders are literal git commands.
                    "For existing clones, run {} only; do not update the working tree or run {}, {}, build, or install",
                    "git fetch", "git pull", "git reset"));
    std::cout << std::endl;
    print_help_section(
            localization::translate_message("SOURCE BUILD PREFERENCES"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::AddSource).help_syntax,
            localization::translate_message(
                    "Enable a source-build preference for a package"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::EditSource).help_syntax,
            localization::translate_message(
                    "Edit a source-build preference"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::ListSources).help_syntax,
            localization::translate_message(
                    "List source-build preferences"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::DeleteSource).help_syntax,
            localization::translate_message(
                    "Remove a source-build preference"));
    print_help_entry(
            cli_authority::operation_spec(OperationId::Revert).help_syntax,
            localization::translate_message(
                    "Remove a preference and reinstall the binary package"));
    std::cout << std::endl;
    print_help_section(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal pacman program identity.
                    "{} COMPATIBILITY", "pacman"));
    print_help_entry(
            cli_authority::PACMAN_SYNC_INSTALL_SYNTAX,
            localization::translate_message("Install packages"));
    print_help_entry(
            cli_authority::PACMAN_SYSTEM_UPGRADE_SYNTAX,
            localization::translate_message("Upgrade the system"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal pacman program identity.
                    "Remain compatible with {}; do not scan all source-build preferences",
                    "pacman"));
    print_help_entry(
            cli_authority::PACMAN_SYNC_SEARCH_SYNTAX,
            localization::translate_message("Search for packages"));
    print_help_entry(
            cli_authority::PACMAN_SYNC_INFO_SYNTAX,
            localization::translate_message("Show package information"));
    print_help_entry(
            cli_authority::PACMAN_FOREIGN_UPDATES_SYNTAX,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "Check {} and foreign-package updates", "AUR"));
    std::cout << std::endl;
    print_help_section(localization::translate_message("OPTIONS"));
    print_help_entry(
            cli_authority::HELP_OPTION_SYNTAX,
            localization::translate_message(
                    "Show this help message and exit"));
    print_help_entry(
            cli_authority::VERSION_OPTION_SYNTAX,
            localization::translate_message(
                    "Show version information and exit"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::Edit).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: PKGBUILD and .install are literal artifact names.
                    "Prompt to review {} and {} files", "PKGBUILD", ".install"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::NoEdit).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: PKGBUILD and .install are literal artifact names.
                    "Skip {} and {} review", "PKGBUILD", ".install"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::Diff).help_syntax,
            localization::translate_message(
                    "Prompt to view repository update diffs"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::NoDiff).help_syntax,
            localization::translate_message(
                    "Skip the repository update diff prompt"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::NoConfirm).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: pacman and makepkg are literal external-program identities.
                    "Pass this option to {} and {}", "pacman", "makepkg"));
    print_help_entry(
            cli_authority::PACMAN_NEEDED_OPTION_SYNTAX,
            localization::format_translated_message(
                    // TRANSLATORS: pacman, AUR, and -S are literal identities or CLI tokens.
                    "Pass this option to {}; on {} or source-build {} routes, apply it only to final installation",
                    "pacman", "AUR", "-S"));
    print_help_continuation(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal Moguet project identity.
                    "Do not skip {} build, review, or plan steps",
                    application_identity::PROJECT_NAME));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::BuildMode).help_syntax,
            localization::translate_message("Select the source-build mode"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::Rebuild).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is a literal compatibility option form.
                    "Compatibility alias for {}",
                    cli_authority::BUILD_MODE_REBUILD_OPTION));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::CleanBuild).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is a literal compatibility option form.
                    "Compatibility alias for {}",
                    cli_authority::BUILD_MODE_CLEAN_OPTION));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::RmDeps).help_syntax,
            localization::translate_message(
                    "Unsupported for separated source builds; no dependency cleanup is performed"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::Aur).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: -S, -Ss, -Si, and AUR are literal CLI/project identities.
                    "Limit {}, {}, and {} to {}; do not fall back to repositories",
                    "-S", "-Ss", "-Si", "AUR"));
    print_help_entry(
            cli_authority::global_option_spec(GlobalOptionId::Repo).help_syntax,
            localization::format_translated_message(
                    // TRANSLATORS: -S, -Ss, -Si, and AUR are literal CLI/project identities.
                    "Limit {}, {}, and {} to official binary repositories; do not use {} or source builds",
                    "-S", "-Ss", "-Si", "AUR"));
    std::cout << std::endl;
    print_help_section(localization::translate_message("CONFIGURATION"));
    print_help_entry(
            "$XDG_CONFIG_HOME/moguet/config.toml",
            localization::translate_message(
                    "Read-only typed configuration; a missing file uses built-in defaults"));
    print_help_entry(
            "schema_version = 1",
            localization::translate_message("Required schema version"));
    print_help_entry(
            "review.pkgbuild = prompt|skip",
            localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal PKGBUILD artifact identity.
                    "{} review policy", "PKGBUILD"));
    print_help_entry(
            "review.diff = prompt|skip",
            localization::translate_message("Repository update diff policy"));
    print_help_entry(
            "build.mode = normal|rebuild|clean",
            localization::translate_message("Source-build mode"));
}

bool argv_requests_pkgbuild_export_diagnostics(int argc, char* argv[]) {
    // POLICY(#173): operation 前のglobal optionだけを飛ばし、option value/opaque operandは見ない。
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(is_moguet_global_option(arg)) continue;
        return arg == cli_authority::PKGBUILD_EXPORT_OPERATION ||
               arg == cli_authority::PKGBUILD_PRINT_OPERATION;
    }
    return false;
}

bool handle_info_only_option(int argc, char* argv[]) {
    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(is_moguet_global_option(arg)) continue;
        if(arg == cli_authority::HELP_SHORT_OPTION ||
           arg == cli_authority::HELP_LONG_OPTION) {
            print_help();
            return true;
        }
        if(arg == cli_authority::VERSION_SHORT_OPTION ||
           arg == cli_authority::VERSION_LONG_OPTION) {
            std::cout << application_identity::PROJECT_NAME << " v"
                      << application_identity::VERSION << std::endl;
            return true;
        }
        // POLICY(#173): help/versionはoperation位置だけで扱い、option valueやopaque operandを横取りしない。
        return false;
    }
    return false;
}

// shell引数 / command construction
std::vector<std::string> pacman_args_with_global_options(std::vector<std::string> args) {
    if(g_config.no_confirm) {
        // POLICY(#173): generated optionはoperation直後へ置き、semantic `--`やoption valueを再解釈しない。
        // 認識済みglobal tokenはordered viewから除外済みなので、ここでは常に1件だけ生成する。
        if(args.empty())
            args.push_back("--noconfirm");
        else
            args.insert(args.begin() + 1, "--noconfirm");
    }
    return args;
}

std::string join_pacman_args(const std::vector<std::string>& args) {
    return shell_words::join(pacman_args_with_global_options(args));
}

// pacman / repository補助
bool validate_optionless_moguet_operation(const std::string& operation, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == operation) continue;

        Logger::error("Unsupported " + operation + " option: " + flag);
        return false;
    }
    return true;
}
