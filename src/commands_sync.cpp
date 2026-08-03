#include "commands_sync.hpp"

#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "cache_authority.hpp"
#include "cli_routing.hpp"
#include "dependency_plan.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "shell_words.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// sync search / info / installのselector別policyと、専用presentation / command constructionを所有する。
// POLICY: runnerのconfig ownerとtop-level catchは維持し、source_installへCLI policyを逆流させない。
namespace {

std::vector<std::string> pacman_args_with_global_options(
        std::vector<std::string> args, const AppConfig& config) {
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
        const std::vector<std::string>& args, const AppConfig& config) {
    return shell_words::join(pacman_args_with_global_options(args, config));
}

void preflight_aur_search_schema(const std::vector<std::string>& keywords) {
    // POLICY(#174): refresh付きsearchはAUR responseのschemaをDB mutationより先に検証する。
    for(const auto& keyword : keywords) {
        if(keyword.empty() || keyword[0] == '-') continue;
        try {
            static_cast<void>(AurClient::search(keyword));
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception&) {
            // transport/その他の既存search契約は、pacman実行後の通常search phaseへ委ねる。
        }
    }
}

bool is_orphaned(const AurPackageInfo& pkg) {
    return pkg.Maintainer.empty();
}

bool search_aur(
        const std::vector<std::string>& keywords, bool query_installed_state = true) {
    bool                  found = false;
    // POLICY(#168): AurOnly search must not invoke pacman, even for the optional [installed] annotation.
    std::set<std::string> installed_foreign_packages =
            query_installed_state ? get_foreign_package_names() : std::set<std::string>{};
    for(const auto& pkg_name : keywords) {
        if(pkg_name.empty()) continue;
        if(pkg_name[0] == '-') continue;
        for(const auto& info : AurClient::search(pkg_name)) {
            found = true;
            const std::string& name = info.Name;
            // NO_TRANSLATE(Issue #308): "aur/" is the stable repository
            // namespace prefix; name and version are package identities.
            std::cout << "\033[1;35maur\033[0m/\033[1m" << name << "\033[0m \033[1;32m"
                      << info.Version << "\033[0m";
            if(installed_foreign_packages.contains(name)) {
                std::cout << " \033[1;36m"
                          << localization::translate_message("[installed]")
                          << "\033[0m";
            }
            if(info.OutOfDate.has_value()) {
                std::cout << " \033[1;31m"
                          << localization::translate_message("[out-of-date]")
                          << "\033[0m";
            }
            if(is_orphaned(info)) {
                std::cout << " \033[1;33m"
                          << localization::translate_message("[orphaned]")
                          << "\033[0m";
            }
            std::cout << std::endl;
            if(!info.Description.empty()) std::cout << "    " << info.Description << std::endl;
        }
    }
    return found;
}

std::string join_display_values(const std::vector<std::string>& values) {
    if(values.empty()) return localization::translate_message("None");
    std::stringstream ss;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) ss << "  ";
        ss << values[i];
    }
    return ss.str();
}

std::string installed_display(const AurPackageInfo& pkg) {
    if(!is_installed_package(pkg.Name)) {
        return localization::translate_message("no");
    }
    return "\033[1;36m" + localization::translate_message("yes") +
            "\033[0m";
}

std::string orphaned_display(const AurPackageInfo& pkg) {
    if(!is_orphaned(pkg)) return localization::translate_message("no");
    return "\033[1;33m" + localization::translate_message("yes") +
            "\033[0m";
}

std::string out_of_date_display(const std::optional<long long>& out_of_date) {
    if(!out_of_date.has_value()) return localization::translate_message("no");
    return "\033[1;31m" + localization::translate_message("yes") +
            "\033[0m";
}

void print_aur_info(const AurPackageInfo& pkg) {
    std::cout << localization::format_translated_message(
                         "Repository      : {}", "aur")
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Name            : {}", pkg.Name)
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Package Base    : {}", pkg.PackageBase)
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Version         : {}", pkg.Version)
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Description     : {}",
                         pkg.Description.empty()
                                 ? localization::translate_message("None")
                                 : pkg.Description)
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Depends On      : {}",
                         join_display_values(pkg.Depends))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Make Deps       : {}",
                         join_display_values(pkg.MakeDepends))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Check Deps      : {}",
                         join_display_values(pkg.CheckDepends))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Optional Deps   : {}",
                         join_display_values(pkg.OptDepends))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Provides        : {}",
                         join_display_values(pkg.Provides))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Conflicts With  : {}",
                         join_display_values(pkg.Conflicts))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Replaces        : {}",
                         join_display_values(pkg.Replaces))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Maintainer      : {}",
                         pkg.Maintainer.empty()
                                 ? localization::translate_message("None")
                                 : pkg.Maintainer)
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Installed       : {}", installed_display(pkg))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Orphaned        : {}", orphaned_display(pkg))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "Out of Date     : {}",
                         out_of_date_display(pkg.OutOfDate))
              << std::endl;
}

void require_valid_aur_package_target(const std::string& target) {
    if(target.find('/') != std::string::npos || !is_valid_package_name(target)) {
        throw std::runtime_error(
                localization::format_translated_message(
                        // TRANSLATORS: The placeholders are the literal AUR identity and a package target.
                        "Invalid {} package target: {}", "AUR", target));
    }
}

void append_source_build_work_items(
        std::vector<ProductionSourceBuildWorkItem>& destination,
        std::vector<ProductionSourceBuildWorkItem> source) {
    destination.reserve(destination.size() + source.size());
    for(auto& work_item : source) {
        destination.push_back(std::move(work_item));
    }
}

ValidatedCacheRoot prepare_sync_source_build_cache_root() {
    return prepare_process_cache_root();
}

void assign_source_build_cache_root(
        std::vector<ProductionSourceBuildWorkItem>& work_items,
        const ValidatedCacheRoot& cache_root) {
    for(auto& work_item : work_items) {
        work_item.cache_root = cache_root;
    }
}

int execute_sync_source_build_invocation(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config) {
    try {
        execute_prepared_source_build_invocation(invocation, config);
        return 0;
    } catch(const SeparatedPackageBaseSourceBuildCleanupError& error) {
        // Direct source routeは利用者がretained workspaceを手動確認できる
        // 既存contractを維持する。AUR update resultのpath firewallとは別境界。
        Logger::error(error.what());
        return 1;
    }
}

} // namespace

int cmd_sync_search(
        const ParsedCliArguments& parsed, bool use_sudo,
        PackageSourceSelection source_selection, const AppConfig& config) {
    if(parsed.targets.empty()) {
        Logger::error(localization::translate_message("Missing search query."));
        return 1;
    }

    std::string pacman_prefix = use_sudo ? "sudo pacman " : "pacman ";
    switch(source_selection) {
    case PackageSourceSelection::Auto: {
        if(use_sudo) preflight_aur_search_schema(parsed.targets);
        int pacman_status =
                run_command(pacman_prefix + join_pacman_args(parsed.ordered_pacman_args, config));
        Logger::info(localization::format_translated_message(
                "Searching {}...", "AUR"));
        bool aur_found = search_aur(parsed.targets);
        return (pacman_status == 0 || aur_found) ? 0 : 1;
    }
    case PackageSourceSelection::AurOnly:
        if(parsed_has_semantic_pacman_option(parsed, "--needed")) {
            Logger::error(localization::format_translated_message(
                    "Unsupported {} option for {} search: {}",
                    "pacman", "AUR", "--needed"));
            return 1;
        }
        Logger::info(localization::format_translated_message(
                "Searching {}...", "AUR"));
        return search_aur(parsed.targets, false) ? 0 : 1;
    case PackageSourceSelection::RepoOnly:
        return run_command(
                pacman_prefix + join_pacman_args(parsed.ordered_pacman_args, config));
    }
    throw std::logic_error(localization::translate_message(
            "Unknown package source selection."));
}

int cmd_sync_install(
        const ParsedCliArguments& parsed, bool is_sys_upgrade,
        PackageSourceSelection source_selection, const AppConfig& config) {
    const SourceSyncOptions source_sync_options = parse_source_sync_options(parsed);
    ProviderSelectionCallback select_provider =
            provider_selection_callback(config);

    if(source_selection == PackageSourceSelection::RepoOnly) {
        // POLICY(#168): RepoOnly is one ordered binary repository transaction; no classification probe.
        return run_command(
                "sudo pacman " + join_pacman_args(parsed.ordered_pacman_args, config));
    }

    if(source_selection == PackageSourceSelection::AurOnly) {
        if(parsed.targets.empty()) {
            Logger::error(localization::format_translated_message(
                    "Missing {} package target.", "AUR"));
            return 1;
        }
        for(const auto& target : parsed.targets) {
            require_valid_aur_package_target(target);
        }

        std::optional<std::string> unsupported_option = unsupported_source_sync_option(parsed);
        if(unsupported_option.has_value()) {
            Logger::error(localization::format_translated_message(
                    "Unsupported {} option for {}/source-build target: {}",
                    "pacman", "AUR", unsupported_option.value()));
            Logger::error(localization::format_translated_message(
                    "Rerun {} without this option.", "--aur"));
            return 1;
        }
        require_supported_production_source_build_options(config);

        std::vector<BuildPlan> plans;
        plans.reserve(parsed.targets.size());
        for(const auto& target : parsed.targets) {
            BuildPlan plan = resolve_build_plan(target, select_provider);
            require_executable_build_plan(target, plan);
            plans.push_back(std::move(plan));
        }

        std::vector<ProductionSourceBuildWorkItem> work_items;
        for(const auto& plan : plans) {
            append_source_build_work_items(
                    work_items,
                    prepare_aur_source_build_work_items(
                            plan, false, source_sync_options.needed));
        }
        ValidatedCacheRoot cache_root =
                prepare_sync_source_build_cache_root();
        assign_source_build_cache_root(work_items, cache_root);
        // POLICY(#168,#242): every per-root plan keeps its existing order, while
        // all roots complete static preflight and one database-path resolution
        // before the first checkout/workspace/build/install mutation.
        PreparedProductionSourceBuildInvocation invocation =
                prepare_production_source_build_invocation(
                        std::move(work_items), config);
        return execute_sync_source_build_invocation(invocation, config);
    }

    if(parsed.targets.empty()) {
        return run_command(
                "sudo pacman " + join_pacman_args(parsed.ordered_pacman_args, config));
    }

    std::vector<std::string> repo_targets;
    std::vector<std::string> aur_targets;
    std::set<size_t>         aur_target_token_indices;
    for(const std::string& target : parsed.targets) {
        require_valid_package_name(target);
    }
    for(size_t i = 0; i < parsed.targets.size(); ++i) {
        const std::string& target = parsed.targets[i];
        if(is_force_source(target)) {
            aur_targets.push_back(target);
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        } else if(is_repo_package(target)) {
            repo_targets.push_back(target);
        } else {
            aur_targets.push_back(target);
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        }
    }
    if(!aur_targets.empty()) {
        std::optional<std::string> unsupported_option = unsupported_source_sync_option(parsed);
        if(unsupported_option.has_value()) {
            Logger::error(localization::format_translated_message(
                    "Unsupported {} option for {}/source-build target: {}",
                    "pacman", "AUR", unsupported_option.value()));
            Logger::error(localization::format_translated_message(
                    "Split official repository and {}/source-build targets, or rerun without this option.",
                    "AUR"));
            return 1;
        }
        require_supported_production_source_build_options(config);
    }
    std::vector<ProductionSourceBuildWorkItem> source_work_items;
    for(const auto& package : aur_targets) {
        if(is_repo_package(package)) {
            source_work_items.push_back(
                    prepare_smart_source_build_work_item(
                            package, false, source_sync_options.needed,
                            select_provider));
            continue;
        }

        BuildPlan plan = resolve_build_plan(package, select_provider);
        require_executable_build_plan(package, plan);
        append_source_build_work_items(
                source_work_items,
                prepare_aur_source_build_work_items(
                        plan, true, source_sync_options.needed));
    }
    std::optional<PreparedProductionSourceBuildInvocation> source_invocation;
    if(!source_work_items.empty()) {
        ValidatedCacheRoot cache_root =
                prepare_sync_source_build_cache_root();
        assign_source_build_cache_root(
                source_work_items, cache_root);
        source_invocation = prepare_production_source_build_invocation(
                std::move(source_work_items), config);
    }
    if(!repo_targets.empty() || is_sys_upgrade) {
        // POLICY(#173): AUR targetのtokenだけを除き、option/value/official targetの元順序を維持する。
        std::vector<std::string> pacman_args =
                ordered_pacman_args_excluding_targets(parsed, aur_target_token_indices);
        if(run_command("sudo pacman " + join_pacman_args(pacman_args, config)) != 0) {
            throw std::runtime_error(localization::format_translated_message(
                    "{} failed.", "Pacman"));
        }
    }
    if(source_invocation.has_value()) {
        return execute_sync_source_build_invocation(
                source_invocation.value(), config);
    }
    return 0;
}

int cmd_sync_info(
        const ParsedCliArguments& parsed, bool use_sudo,
        PackageSourceSelection source_selection, const AppConfig& config) {
    if(source_selection == PackageSourceSelection::Auto && use_sudo) {
        auto unqualified_target =
                std::find_if(parsed.targets.begin(), parsed.targets.end(), [](const std::string& target) {
                    return target.find('/') == std::string::npos;
                });
        if(unqualified_target != parsed.targets.end()) {
            // POLICY(#172): refresh 後に AUR fallback すると official DB の更新だけが先行する。
            // refresh 付き info は repository-qualified target に限定し、分類前に停止する。
            Logger::error(localization::format_translated_message(
                    "Cannot combine {} refresh with {} info fallback for unqualified target: {}",
                    "pacman", "AUR", *unqualified_target));
            Logger::error(localization::format_translated_message(
                    "Use a repository-qualified target such as {}, or run refresh and {} separately.",
                    "repo/package", "-Si"));
            return 1;
        }
    }

    std::string pacman_prefix = use_sudo ? "sudo pacman " : "pacman ";

    if(source_selection == PackageSourceSelection::RepoOnly) {
        return run_command(
                pacman_prefix + join_pacman_args(parsed.ordered_pacman_args, config));
    }

    if(source_selection == PackageSourceSelection::AurOnly) {
        if(parsed_has_semantic_pacman_option(parsed, "--needed")) {
            Logger::error(localization::format_translated_message(
                    "Unsupported {} option for {} info: {}",
                    "pacman", "AUR", "--needed"));
            return 1;
        }
        if(parsed.targets.empty()) {
            Logger::error(localization::format_translated_message(
                    "Missing {} package target.", "AUR"));
            return 1;
        }
        for(const auto& target : parsed.targets) {
            require_valid_aur_package_target(target);
        }

        bool                        failed = false;
        std::vector<AurPackageInfo> aur_infos;
        for(const auto& target : parsed.targets) {
            try {
                std::optional<AurPackageInfo> info = AurClient::info(target);
                if(info.has_value())
                    aur_infos.push_back(info.value());
                else {
                    Logger::error(localization::format_translated_message(
                            "{} package not found: {}", "AUR", target));
                    failed = true;
                }
            } catch(const std::exception& e) {
                Logger::error(localization::format_translated_message(
                        "Failed to fetch {} info for {}: {}",
                        "AUR", target, e.what()));
                failed = true;
            }
        }
        for(size_t i = 0; i < aur_infos.size(); ++i) {
            if(i > 0) std::cout << std::endl;
            print_aur_info(aur_infos[i]);
        }
        return failed ? 1 : 0;
    }

    if(parsed.targets.empty()) {
        return run_command(
                pacman_prefix + join_pacman_args(parsed.ordered_pacman_args, config));
    }

    bool                        failed = false;
    std::vector<std::string>    repo_targets;
    std::set<size_t>            aur_target_token_indices;
    std::vector<AurPackageInfo> aur_infos;

    for(size_t i = 0; i < parsed.targets.size(); ++i) {
        const std::string& target = parsed.targets[i];
        if(target.find('/') != std::string::npos) {
            repo_targets.push_back(target);
            continue;
        }

        require_valid_package_name(target);
        if(is_repo_package(target)) {
            repo_targets.push_back(target);
            continue;
        }

        try {
            std::optional<AurPackageInfo> info = AurClient::info(target);
            if(info.has_value()) {
                aur_infos.push_back(info.value());
                aur_target_token_indices.insert(parsed.target_token_indices[i]);
            } else {
                Logger::error(localization::format_translated_message(
                        "Package not found in repos or {}: {}",
                        "AUR", target));
                failed = true;
                aur_target_token_indices.insert(parsed.target_token_indices[i]);
            }
        } catch(const std::exception& e) {
            Logger::error(localization::format_translated_message(
                    "Failed to fetch {} info for {}: {}",
                    "AUR", target, e.what()));
            failed = true;
            aur_target_token_indices.insert(parsed.target_token_indices[i]);
        }
    }

    if(!repo_targets.empty()) {
        // POLICY(#173): 同名のoption valueを残し、AUR targetのtoken位置だけを除外する。
        std::vector<std::string> pacman_args =
                ordered_pacman_args_excluding_targets(parsed, aur_target_token_indices);
        if(run_command(pacman_prefix + join_pacman_args(pacman_args, config)) != 0) failed = true;
        if(!aur_infos.empty()) std::cout << std::endl;
    }

    for(size_t i = 0; i < aur_infos.size(); ++i) {
        if(i > 0) std::cout << std::endl;
        print_aur_info(aur_infos[i]);
    }

    return failed ? 1 : 0;
}
