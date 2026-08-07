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
#include "root_package_route_projection.hpp"
#include "root_package_search.hpp"
#include "root_package_selection.hpp"
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
#include <type_traits>
#include <utility>
#include <variant>
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

std::size_t earliest_root_index_for_source_build_work_item(
        const BuildPlan& plan,
        const ProductionSourceBuildWorkItem& work_item) {
    std::optional<std::size_t> earliest_root;
    for(const auto& target : plan.package_targets) {
        if(target.package_base != work_item.request.checkout_name) continue;
        for(const auto& root : target.roots) {
            if(!earliest_root.has_value() ||
               root.invocation_index < earliest_root.value()) {
                earliest_root = root.invocation_index;
            }
        }
    }
    if(!earliest_root.has_value() ||
       earliest_root.value() >= plan.root_targets.size()) {
        throw std::logic_error(localization::format_translated_message(
                "{} work item has no invocation root ownership: {}",
                "BuildPlan", work_item.request.checkout_name));
    }
    return earliest_root.value();
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

std::string root_package_presentation_value(
        const std::optional<std::string>& value) {
    return value.has_value() ? value.value() : "-";
}

void present_root_package_candidate(
        std::ostream& output,
        std::size_t index,
        const RootPackageSearchCandidate& candidate) {
    // NO_TRANSLATE: source/repository/package/PackageBase/version/groups are
    // stable machine-readable candidate field labels.
    output << index << ") ";
    if(const auto* repository =
               std::get_if<RepositoryRootPackageIdentity>(
                       &candidate.candidate.identity());
       repository != nullptr) {
        output << "source=repository"
               << " repository=" << repository->repository_name
               << " package=" << repository->package_name;
    } else {
        const auto& aur = std::get<AurRootPackageIdentity>(
                candidate.candidate.identity());
        output << "source=AUR"
               << " package=" << aur.package_name
               << " PackageBase=" << aur.package_base;
    }
    output << " version="
           << root_package_presentation_value(
                      candidate.candidate.presentation().version);
    if(!candidate.selectable_group_names.empty()) {
        output << " groups=";
        for(std::size_t group_index = 0;
            group_index < candidate.selectable_group_names.size();
            ++group_index) {
            if(group_index > 0) output << ',';
            output << '@' << candidate.selectable_group_names[group_index];
        }
    }
    output << '\n';
    if(candidate.candidate.presentation().description.has_value()) {
        output << "    "
               << candidate.candidate.presentation().description.value()
               << '\n';
    }
}

void present_invalid_root_package_selection_issue(
        std::ostream& output,
        const RootPackageSelectionIssue& issue) {
    std::visit(
            [&output](const auto& typed_issue) {
                using Issue = std::decay_t<decltype(typed_issue)>;
                output << ":: ";
                if constexpr(std::is_same_v<
                                     Issue,
                                     MalformedRootPackageSelectionToken>) {
                    output << localization::translate_message(
                            "Invalid package selection token.");
                } else if constexpr(std::is_same_v<
                                            Issue,
                                            RootPackageSelectionIndexOutOfRange>) {
                    // TRANSLATORS: The placeholder is the number of displayed package candidates.
                    output << localization::format_translated_message(
                            "Package selection index is outside the displayed range 1-{}.",
                            typed_issue.candidate_count);
                } else if constexpr(std::is_same_v<
                                            Issue,
                                            DescendingRootPackageSelectionRange>) {
                    output << localization::translate_message(
                            "Package selection ranges must use ascending endpoints.");
                } else if constexpr(std::is_same_v<
                                            Issue,
                                            UnknownRootPackageSelectionGroup>) {
                    output << localization::translate_message(
                            "Package selection names an unknown displayed group.");
                } else if constexpr(std::is_same_v<
                                            Issue,
                                            MixedRootPackageSelectionCancellationToken>) {
                    output << localization::translate_message(
                            "A package selection cancellation token cannot be combined with selectors.");
                } else if constexpr(std::is_same_v<
                                            Issue,
                                            ConflictingRootPackageSelectionAlternatives>) {
                    // package_name comes from the validated candidate snapshot; raw input tokens are not echoed.
                    // TRANSLATORS: The placeholder is a validated package name.
                    output << localization::format_translated_message(
                            "Package {} was selected from more than one source; select exactly one source.",
                            typed_issue.package_name);
                }
                output << '\n';
            },
            issue);
}

RootPackageSelectionInteractionCallback
root_package_selection_interaction() {
    return [](const RootPackageSelectionInteractionEvent& event,
              const RootPackageSearchSnapshot& snapshot) {
        if(std::holds_alternative<
                   PresentRootPackageSelectionCandidates>(event)) {
            std::cout << ":: "
                      << localization::translate_message(
                                 "Package candidates:")
                      << '\n';
            for(std::size_t index = 0; index < snapshot.candidates.size();
                ++index) {
                present_root_package_candidate(
                        std::cout, index + 1, snapshot.candidates[index]);
            }
            return;
        }
        if(std::holds_alternative<PromptForRootPackageSelection>(event)) {
            // TRANSLATORS: Enter and q/quit/cancel are literal input tokens; @group is fixed selector syntax.
            std::cout << ":: "
                      << localization::format_translated_message(
                                 "Select package numbers, ascending ranges, or displayed {}; press {} or enter {} to cancel:",
                                 "@group", "Enter", "q/quit/cancel")
                      << ' ' << std::flush;
            return;
        }

        const auto& invalid =
                std::get<InvalidRootPackageSelectionAttempt>(event);
        for(const auto& issue : invalid.selection.issues) {
            present_invalid_root_package_selection_issue(
                    std::cout, issue);
        }
    };
}

void report_root_package_selection_input_gate(
        RootPackageSelectionInputGate input_gate) {
    switch(input_gate) {
    case RootPackageSelectionInputGate::NonTty:
        Logger::error(localization::translate_message(
                "Interactive package selection requires a TTY on standard input."));
        return;
    case RootPackageSelectionInputGate::NoConfirm:
        // TRANSLATORS: The placeholder is the literal CLI option --noconfirm.
        Logger::error(localization::format_translated_message(
                "Interactive package selection is not available with {}.",
                "--noconfirm"));
        return;
    case RootPackageSelectionInputGate::Interactive:
        throw std::logic_error(localization::translate_message(
                "Interactive package selection has an inconsistent input gate."));
    }
    throw std::logic_error(localization::translate_message(
            "Interactive package selection has an unknown input gate."));
}

RootPackageSearchScope root_package_search_scope(
        PackageSourceSelection source_selection) {
    switch(source_selection) {
    case PackageSourceSelection::Auto:
        return RootPackageSearchScope::All;
    case PackageSourceSelection::AurOnly:
        return RootPackageSearchScope::Aur;
    case PackageSourceSelection::RepoOnly:
        return RootPackageSearchScope::Repository;
    }
    throw std::logic_error(localization::translate_message(
            "Unknown package source selection."));
}

void report_root_package_search_failure(
        const RootPackageSearchResult& result) {
    if(const auto* repository =
               std::get_if<RepositoryRootPackageSearchFailure>(&result);
       repository != nullptr) {
        // TRANSLATORS: The placeholder is a repository metadata diagnostic.
        Logger::error(localization::format_translated_message(
                "Repository package search failed: {}",
                repository->failure.diagnostic));
        return;
    }
    if(const auto* aur = std::get_if<AurRootPackageSearchFailure>(&result);
       aur != nullptr) {
        // TRANSLATORS: The placeholders are the AUR project identity and a query diagnostic.
        Logger::error(localization::format_translated_message(
                "{} package search failed: {}", "AUR", aur->diagnostic));
        return;
    }
    if(std::holds_alternative<InvalidRootPackageSearchSnapshot>(result)) {
        // Raw invalid candidate metadata is intentionally not reflected to the terminal.
        Logger::error(localization::translate_message(
                "Package search returned an invalid candidate snapshot."));
        return;
    }
    throw std::logic_error(localization::translate_message(
            "Package search failure reporting received a successful snapshot."));
}

bool has_source_build_cli_override(const ParsedCliArguments& parsed) noexcept {
    return parsed.cli_overrides.review_pkgbuild.has_value() ||
           parsed.cli_overrides.review_diff.has_value() ||
           parsed.cli_overrides.build_mode.has_value();
}

std::string join_root_package_names(
        const std::vector<AurRootPackageRouteTarget>& targets) {
    std::stringstream joined;
    for(std::size_t index = 0; index < targets.size(); ++index) {
        if(index > 0) joined << ", ";
        joined << targets[index].identity().package_name;
    }
    return joined.str();
}

std::string join_package_names(
        const std::vector<std::string>& package_names) {
    std::stringstream joined;
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        if(index > 0) joined << ", ";
        joined << package_names[index];
    }
    return joined.str();
}

void require_selected_aur_root_plan_correlation(
        const std::vector<AurRootPackageRouteTarget>& selected_targets,
        const BuildPlan& plan) {
    if(plan.root_targets.size() != selected_targets.size()) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the AUR project identity.
                "The {} build plan does not cover every selected root package.",
                "AUR"));
    }

    for(std::size_t index = 0; index < selected_targets.size(); ++index) {
        const AurRootPackageIdentity& selected =
                selected_targets[index].identity();
        const RootTargetIdentity& planned_root = plan.root_targets[index];
        if(planned_root.invocation_index != index ||
           planned_root.requested_name != selected.package_name) {
            throw std::runtime_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "The {} build plan changed the selected root package order or identity.",
                    "AUR"));
        }

        const PlannedPackageTarget* planned_target = nullptr;
        for(const auto& candidate : plan.package_targets) {
            if(candidate.package_name != selected.package_name) continue;
            if(planned_target != nullptr) {
                throw std::runtime_error(localization::format_translated_message(
                        // TRANSLATORS: The placeholders are the AUR project identity and a validated package name.
                        "The {} build plan contains duplicate identities for selected package {}.",
                        "AUR", selected.package_name));
            }
            planned_target = &candidate;
        }
        if(planned_target == nullptr) {
            throw std::runtime_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the AUR project identity and a validated package name.
                    "The {} build plan omitted selected package {}.",
                    "AUR", selected.package_name));
        }
        if(planned_target->package_base != selected.package_base) {
            // Both PackageBase values are validated identities. Refuse to route a stale
            // selection to metadata returned by the later build-plan query.
            // TRANSLATORS: The placeholders are the PackageBase and AUR identities, a validated package name, and two PackageBase names.
            throw std::runtime_error(localization::format_translated_message(
                    "The {} for selected {} package {} changed from {} to {}; rerun package selection.",
                    "PackageBase", "AUR", selected.package_name,
                    selected.package_base, planned_target->package_base));
        }
        if(std::find(
                   planned_target->roles.begin(),
                   planned_target->roles.end(),
                   PackageRole::Root) == planned_target->roles.end() ||
           std::find(
                   planned_target->roots.begin(),
                   planned_target->roots.end(),
                   planned_root) == planned_target->roots.end()) {
            throw std::runtime_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the AUR project identity and a validated package name.
                    "The {} build plan lost root attribution for selected package {}.",
                    "AUR", selected.package_name));
        }
    }
}

} // namespace

std::optional<PreparedRootPackageInstall> prepare_root_package_install(
        const ParsedCliArguments& parsed,
        RootPackageSelectionInvocation invocation,
        const AppConfig& config) {
    RootPackageSelectionSession selection_session =
            make_root_package_selection_session(
                    root_package_selection_interaction(),
                    config.no_confirm);
    if(invocation.query.empty()) {
        Logger::error(localization::translate_message(
                "Package selection query must not be empty."));
        return std::nullopt;
    }
    if(config.rm_deps || parsed.cli_overrides.rm_deps) {
        // TRANSLATORS: The placeholder is the literal CLI option --rmdeps.
        Logger::error(localization::format_translated_message(
                "Interactive package selection does not support {}.",
                "--rmdeps"));
        return std::nullopt;
    }

    // POLICY(#217): gate must be observable before official/AUR candidate query.
    if(!selection_session.is_interactive()) {
        report_root_package_selection_input_gate(
                selection_session.input_gate());
        return std::nullopt;
    }

    RootPackageSearchResult search_result = search_root_package_candidates(
            invocation.query,
            root_package_search_scope(parsed.source_selection));
    const auto* snapshot =
            std::get_if<RootPackageSearchSnapshot>(&search_result);
    if(snapshot == nullptr) {
        report_root_package_search_failure(search_result);
        return std::nullopt;
    }

    RootPackageSelectionSessionResult selection_result =
            selection_session.select(*snapshot);
    if(const auto* unavailable =
               std::get_if<UnavailableRootPackageSelection>(
                       &selection_result);
       unavailable != nullptr) {
        switch(unavailable->reason) {
        case RootPackageSelectionUnavailableReason::NoCandidates:
            Logger::error(localization::translate_message(
                    "No package candidates were found."));
            return std::nullopt;
        case RootPackageSelectionUnavailableReason::NonInteractiveInput:
            report_root_package_selection_input_gate(
                    RootPackageSelectionInputGate::NonTty);
            return std::nullopt;
        case RootPackageSelectionUnavailableReason::NoConfirm:
            report_root_package_selection_input_gate(
                    RootPackageSelectionInputGate::NoConfirm);
            return std::nullopt;
        }
        throw std::logic_error(localization::translate_message(
                "Package selection returned an unknown unavailable reason."));
    }
    if(std::holds_alternative<CancelledRootPackageSelection>(
               selection_result)) {
        Logger::error(localization::translate_message(
                "Package selection was cancelled."));
        return std::nullopt;
    }

    const RootPackageSelection& selection =
            std::get<RootPackageSelection>(selection_result);
    RootPackageRoutingProjectionResult routing =
            project_root_package_routing(selection);
    if(!routing.is_valid()) {
        // Unsafe repository identity is deliberately not echoed.
        Logger::error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal pacman program identity.
                "A selected repository package cannot be represented as an exact {} target.",
                "pacman"));
        return std::nullopt;
    }
    const RootPackageRoutingProjection& projection = *routing.projection();

    if(projection.aur_targets().empty() &&
       has_source_build_cli_override(parsed)) {
        Logger::error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the AUR project identity.
                "Source-build review and build-mode options require at least one selected {} package.",
                "AUR"));
        return std::nullopt;
    }

    PreparedRootPackageInstall prepared;
    prepared.needed = invocation.needed;
    prepared.exact_repository_targets.reserve(
            projection.repository_targets().size());
    for(const auto& target : projection.repository_targets()) {
        prepared.exact_repository_targets.push_back(
                target.exact_package_target());
    }

    if(projection.aur_targets().empty()) return prepared;

    // All AUR roots share one plan so same-PackageBase selected children become
    // one ordered work item. This preserves source-local selection order while
    // keeping dependency units before their consumers.
    require_supported_production_source_build_options(config);
    std::vector<std::string> aur_package_names;
    aur_package_names.reserve(projection.aur_targets().size());
    for(const auto& target : projection.aur_targets()) {
        aur_package_names.push_back(target.identity().package_name);
    }

    BuildPlan plan = resolve_build_plan(
            aur_package_names,
            provider_selection_callback(config));
    require_selected_aur_root_plan_correlation(
            projection.aur_targets(), plan);
    require_executable_build_plan(
            join_root_package_names(projection.aur_targets()), plan);

    std::vector<ProductionSourceBuildWorkItem> work_items =
            prepare_aur_source_build_work_items(
                    plan, false, prepared.needed);
    // Do not prepare/seed a cache here. execute_prepared_source_build_invocation
    // activates it only after the selected repository transaction succeeds.
    prepared.source_invocation =
            prepare_production_source_build_invocation(
                    std::move(work_items), config);
    return prepared;
}

int execute_prepared_root_package_install(
        PreparedRootPackageInstall prepared,
        const AppConfig& config) {
    const bool has_repository_transaction =
            !prepared.exact_repository_targets.empty();
    if(!has_repository_transaction && !prepared.source_invocation.has_value()) {
        throw std::invalid_argument(localization::translate_message(
                "Prepared package selection contains no install targets."));
    }

    if(has_repository_transaction) {
        std::vector<std::string> pacman_args{"-S"};
        if(prepared.needed) pacman_args.push_back("--needed");
        pacman_args.push_back("--");
        pacman_args.insert(
                pacman_args.end(),
                prepared.exact_repository_targets.begin(),
                prepared.exact_repository_targets.end());

        const int repository_status = run_command(
                "sudo pacman " + shell_words::join(pacman_args));
        if(repository_status != 0) {
            if(prepared.source_invocation.has_value()) {
                Logger::error(localization::format_translated_message(
                        // TRANSLATORS: The placeholder is the AUR project identity.
                        "The selected repository package transaction failed; selected {} packages were not executed.",
                        "AUR"));
            } else {
                Logger::error(localization::translate_message(
                        "The selected repository package transaction failed."));
            }
            return repository_status;
        }
    }

    if(!prepared.source_invocation.has_value()) return 0;

    try {
        const int source_status = execute_sync_source_build_invocation(
                prepared.source_invocation.value(), config);
        if(source_status != 0 && has_repository_transaction) {
            Logger::warn(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "The repository package transaction completed before the {} route failed; it was not rolled back.",
                    "AUR"));
        }
        return source_status;
    } catch(...) {
        if(has_repository_transaction) {
            Logger::warn(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the AUR project identity.
                    "The repository package transaction completed before the {} route failed; it was not rolled back.",
                    "AUR"));
        }
        throw;
    }
}

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

        BuildPlan plan = resolve_build_plan(
                parsed.targets, select_provider);
        require_executable_build_plan(
                join_package_names(parsed.targets), plan);

        std::vector<ProductionSourceBuildWorkItem> work_items =
                prepare_aur_source_build_work_items(
                        plan, false, source_sync_options.needed);
        ValidatedCacheRoot cache_root =
                prepare_sync_source_build_cache_root();
        assign_source_build_cache_root(work_items, cache_root);
        // POLICY(#168,#242/#351): 全rootを一つのplanへaggregateし、
        // static preflightとdatabase-path resolutionを最初の
        // checkout/workspace/build/install mutationより前に完了する。
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
    std::vector<std::string> repository_source_targets;
    std::vector<std::string> aur_plan_targets;
    for(const auto& package : aur_targets) {
        if(is_repo_package(package)) {
            repository_source_targets.push_back(package);
        } else {
            aur_plan_targets.push_back(package);
        }
    }

    std::optional<BuildPlan> aur_invocation_plan;
    if(!aur_plan_targets.empty()) {
        aur_invocation_plan = resolve_build_plan(
                aur_plan_targets, select_provider);
        require_executable_build_plan(
                join_package_names(aur_plan_targets),
                aur_invocation_plan.value());
    }

    std::vector<std::vector<ProductionSourceBuildWorkItem>>
            aur_work_items_by_root(aur_plan_targets.size());
    if(aur_invocation_plan.has_value()) {
        std::vector<ProductionSourceBuildWorkItem> aur_work_items =
                prepare_aur_source_build_work_items(
                        aur_invocation_plan.value(), true,
                        source_sync_options.needed);
        for(auto& work_item : aur_work_items) {
            const std::size_t root_index =
                    earliest_root_index_for_source_build_work_item(
                            aur_invocation_plan.value(), work_item);
            aur_work_items_by_root[root_index].push_back(
                    std::move(work_item));
        }
    }

    std::vector<ProductionSourceBuildWorkItem> source_work_items;
    std::size_t aur_root_index = 0;
    for(const auto& package : aur_targets) {
        if(std::find(
                   repository_source_targets.begin(),
                   repository_source_targets.end(), package) !=
           repository_source_targets.end()) {
            source_work_items.push_back(
                    prepare_smart_source_build_work_item(
                            package, false, source_sync_options.needed,
                            select_provider));
            continue;
        }
        if(aur_root_index >= aur_work_items_by_root.size()) {
            throw std::logic_error(localization::translate_message(
                    "Build plan root ownership is inconsistent with source targets."));
        }
        append_source_build_work_items(
                source_work_items,
                std::move(aur_work_items_by_root[aur_root_index]));
        ++aur_root_index;
    }
    if(aur_root_index != aur_work_items_by_root.size()) {
        throw std::logic_error(localization::translate_message(
                "Build plan source targets were not fully projected."));
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
