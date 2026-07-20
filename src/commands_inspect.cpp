#include "commands_inspect.hpp"

#include "aur_rpc.hpp"
#include "checkout_fetch.hpp"
#include "dependency_plan.hpp"
#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "pkgbuild_export.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// inspection command固有のpresentationとtarget orchestrationを所有する。
// POLICY: CLI parsing、Curl/Logger lifetime、-G/-Gp special routeはrunner側に残す。
namespace {

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";

// inspection固有のtrimは、domain contractを汎用string utilityへ持ち上げず局所保持する。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string join_comma_display_values(const std::vector<std::string>& values) {
    std::stringstream ss;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) ss << ", ";
        ss << values[i];
    }
    return ss.str();
}

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}

bool aur_version_is_newer(const std::string& aur_version, const std::string& installed_version) {
    std::string cmp_cmd = "vercmp " + shell_words::quote(aur_version) + " " + shell_words::quote(installed_version);
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    try {
        return std::stoi(cmp_res) > 0;
    } catch(...) {
        Logger::warn("Failed to compare versions: " + installed_version + " -> " + aur_version);
        return false;
    }
}

std::string provider_display(const ProvidedDependency& provider) {
    return provider.repository + "/" + provider.package_name;
}

std::string dependency_display_name(const std::string& dependency, const std::string& package_name) {
    std::string display;
    if(package_name.empty() || dependency == package_name)
        display = dependency;
    else
        display = dependency + " (" + package_name + ")";
    return dependency_display_with_constraint_note(display, dependency);
}

std::string dependency_kind_display(DependencyKind kind) {
    switch(kind) {
    case DependencyKind::Repo:
        return "repo";
    case DependencyKind::Aur:
        return "aur";
    case DependencyKind::Provided:
        return "provided";
    case DependencyKind::AmbiguousProvider:
        return "ambiguous-provider";
    case DependencyKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

void print_recursive_dependency_node(const RecursiveDependencyNode& node, size_t indent) {
    std::cout << std::string(indent, ' ') << "- "
              << dependency_display_name(node.dependency, node.package_name) << " ["
              << dependency_kind_display(node.kind) << "]";
    if(node.kind == DependencyKind::Aur && !node.package_base.empty() && node.package_base != node.package_name) {
        std::cout << " base: " << node.package_base;
    }
    if(node.provided_by.has_value()) {
        std::cout << " by " << node.provided_by->repository << "/" << node.provided_by->package_name;
    }
    if(!node.provider_candidates.empty()) {
        std::cout << " candidates: ";
        for(size_t i = 0; i < node.provider_candidates.size(); ++i) {
            if(i > 0) std::cout << ", ";
            std::cout << provider_display(node.provider_candidates[i]);
        }
    }
    if(node.already_visited) std::cout << " (already visited)";
    if(node.max_depth_reached) std::cout << " (max depth reached)";
    std::cout << std::endl;

    for(const auto& child : node.children) {
        print_recursive_dependency_node(child, indent + 2);
    }
}

void print_recursive_dependency_tree(const std::vector<RecursiveDependencyNode>& nodes) {
    std::cout << "Recursive dependency tree:" << std::endl;
    if(nodes.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }
    for(const auto& node : nodes) {
        print_recursive_dependency_node(node, 2);
    }
}

void add_unique_value(std::vector<std::string>& values, const std::string& value) {
    std::string trimmed = trim(value);
    if(trimmed.empty()) return;
    if(std::find(values.begin(), values.end(), trimmed) == values.end()) values.push_back(trimmed);
}

void print_dependency_group(const std::string& label, const std::vector<std::string>& dependencies) {
    std::cout << label << std::endl;
    if(dependencies.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }
    for(const auto& dep : dependencies) {
        std::cout << "  " << dep << std::endl;
    }
}

void print_ambiguous_provider_group(
        const std::string& label, const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::cout << label << std::endl;
    if(dependencies.empty()) {
        std::cout << "  None" << std::endl;
        return;
    }

    for(const auto& dependency : dependencies) {
        std::cout << "  " << dependency_display_with_constraint_note(dependency.dependency, dependency.dependency)
                  << std::endl;
        std::cout << "    candidates:" << std::endl;
        for(size_t i = 0; i < dependency.candidates.size(); ++i) {
            std::cout << "      " << (i + 1) << ". " << provider_display(dependency.candidates[i]) << std::endl;
        }
    }
}

void print_metadata_risk_group(const std::vector<BuildPlanMetadataRisk>& risks) {
    std::cout << "Metadata conflicts/replaces:" << std::endl;
    for(const auto& risk : risks) {
        std::cout << "  " << risk.package_name;
        if(risk.package_base != risk.package_name) std::cout << " (base: " << risk.package_base << ")";
        std::cout << std::endl;
        if(!risk.conflicts.empty())
            std::cout << "    conflicts: " << join_comma_display_values(risk.conflicts) << std::endl;
        if(!risk.replaces.empty())
            std::cout << "    replaces: " << join_comma_display_values(risk.replaces) << std::endl;
    }
}

void print_build_plan(const BuildPlan& plan) {
    std::cout << "Build plan:" << std::endl;
    if(plan.order.empty()) {
        std::cout << "  None" << std::endl;
    } else {
        for(size_t i = 0; i < plan.order.size(); ++i) {
            const BuildPlanEntry& entry = plan.order[i];
            std::cout << "  " << (i + 1) << ". " << entry.package_base;
            std::cout << std::endl;
            std::vector<std::string> distinct_targets;
            for(const auto& package_name : entry.package_names) {
                if(package_name != entry.package_base) add_unique_value(distinct_targets, package_name);
            }
            if(!distinct_targets.empty()) {
                std::cout << "     target package";
                if(distinct_targets.size() > 1) std::cout << "s";
                std::cout << ": " << join_comma_display_values(distinct_targets) << std::endl;
            }
        }
    }

    if(!plan.provided.empty()) {
        std::cout << std::endl;
        std::cout << "Provided dependencies:" << std::endl;
        for(const auto& dependency : plan.provided) {
            std::cout << "  - "
                      << dependency_display_with_constraint_note(dependency.dependency, dependency.dependency)
                      << " -> " << dependency.provider.repository << "/" << dependency.provider.package_name
                      << std::endl;
        }
    }

    if(!plan.ambiguous_providers.empty()) {
        std::cout << std::endl;
        print_ambiguous_provider_group("Ambiguous provided dependencies:", plan.ambiguous_providers);
    }

    if(!plan.split_package_targets.empty()) {
        std::cout << std::endl;
        std::cout << "Split package install targets:" << std::endl;
        for(const auto& target : plan.split_package_targets) {
            std::cout << "  - " << target.package_name << " (base: " << target.package_base << ")" << std::endl;
        }
    }

    if(!plan.metadata_risks.empty()) {
        std::cout << std::endl;
        print_metadata_risk_group(plan.metadata_risks);
    }

    if(!plan.unresolved.empty()) {
        std::cout << std::endl;
        std::cout << "Unresolved dependencies:" << std::endl;
        for(const auto& dependency : plan.unresolved) {
            std::cout << "  - " << dependency << std::endl;
        }
    }

    if(!plan.cycles.empty()) {
        std::cout << std::endl;
        std::cout << "Cyclic dependencies:" << std::endl;
        for(const auto& dependency : plan.cycles) {
            std::cout << "  - " << dependency << std::endl;
        }
    }

    if(!plan.unresolved.empty() || !plan.ambiguous_providers.empty() || !plan.cycles.empty() ||
       !plan.split_package_targets.empty() || !plan.metadata_risks.empty()) {
        std::cout << std::endl;
        std::cout << "Plan status: incomplete" << std::endl;
        if(!plan.unresolved.empty()) std::cout << "  unresolved dependencies remain" << std::endl;
        if(!plan.ambiguous_providers.empty()) std::cout << "  ambiguous providers are not selected" << std::endl;
        if(!plan.cycles.empty()) std::cout << "  cyclic dependencies detected" << std::endl;
        if(!plan.split_package_targets.empty())
            std::cout << "  split package install target selection is not implemented" << std::endl;
        if(!plan.metadata_risks.empty())
            std::cout << "  conflicts/replaces metadata is not resolved automatically" << std::endl;
    }
}

void print_fetch_plan(const BuildPlan& plan) {
    std::cout << "Fetch targets:" << std::endl;
    if(plan.order.empty()) {
        std::cout << "  None" << std::endl;
    } else {
        for(size_t i = 0; i < plan.order.size(); ++i) {
            const BuildPlanEntry& entry = plan.order[i];
            std::cout << "  " << (i + 1) << ". " << entry.package_base << " -> "
                      << aur_git_url_for_package_base(entry.package_base) << std::endl;
        }
    }

    if(!plan.unresolved.empty()) {
        std::cout << std::endl;
        std::cout << "Unresolved dependencies:" << std::endl;
        for(const auto& dependency : plan.unresolved) {
            Logger::warn(dependency);
        }
    }

    if(!plan.ambiguous_providers.empty()) {
        std::cout << std::endl;
        print_ambiguous_provider_group("Ambiguous provided dependencies:", plan.ambiguous_providers);
    }

    if(!plan.cycles.empty()) {
        std::cout << std::endl;
        std::cout << "Cyclic dependencies:" << std::endl;
        for(const auto& dependency : plan.cycles) {
            Logger::warn(dependency);
        }
    }

    if(!plan.metadata_risks.empty()) {
        std::cout << std::endl;
        print_metadata_risk_group(plan.metadata_risks);
        Logger::warn(
                "Conflicts/replaces metadata requires manual review before build/install; fetch is allowed.");
    }
}

} // namespace

int cmd_deps(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    bool recursive = false;
    for(const auto& flag : flags) {
        if(flag == "deps") continue;
        if(flag == "--recursive") {
            recursive = true;
            continue;
        }
        Logger::error("Unsupported deps option: " + flag);
        Logger::error("Usage: jpacker deps [--recursive] <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker deps [--recursive] <pkg>");
        return 1;
    }

    bool failed = false;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            std::optional<AurPackageInfo> info = AurClient::info(target);
            if(!info.has_value()) {
                Logger::error("AUR package not found: " + target);
                failed = true;
                continue;
            }

            std::vector<std::string> dependencies = collect_build_dependencies(info.value());
            DependencyClassification classified = classify_dependencies(dependencies);

            if(i > 0) std::cout << std::endl;
            std::cout << "Package         : " << info->Name << std::endl;
            std::cout << "Package Base    : " << info->PackageBase << std::endl;
            std::cout << "Dependencies    : " << dependencies.size() << std::endl;
            std::cout << std::endl;
            print_dependency_group("Official repo dependencies:", classified.repo);
            std::cout << std::endl;
            print_dependency_group("AUR dependencies:", classified.aur);
            std::cout << std::endl;
            print_dependency_group("Provided dependencies:", classified.provided);
            std::cout << std::endl;
            print_ambiguous_provider_group("Ambiguous provided dependencies:", classified.ambiguous_providers);
            std::cout << std::endl;
            print_dependency_group("Unknown dependencies:", classified.unknown);
            std::vector<BuildPlanMetadataRisk> metadata_risks =
                    collect_build_plan_metadata_risks(info.value());
            if(!metadata_risks.empty()) {
                std::cout << std::endl;
                print_metadata_risk_group(metadata_risks);
                Logger::warn("Conflicts/replaces metadata is separate from dependency resolution and requires manual review.");
            }
            if(recursive) {
                std::vector<RecursiveDependencyNode> recursive_nodes =
                        resolve_recursive_dependencies(info.value());
                std::cout << std::endl;
                print_recursive_dependency_tree(recursive_nodes);
            }
        } catch(const std::exception& e) {
            Logger::error("Failed to inspect dependencies for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_plan(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == "plan") continue;
        Logger::error("Unsupported plan option: " + flag);
        Logger::error("Usage: jpacker plan <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker plan <pkg>");
        return 1;
    }

    bool failed = false;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_build_plan(target);

            if(i > 0) std::cout << std::endl;
            print_build_plan(plan);
        } catch(const std::exception& e) {
            Logger::error("Failed to plan build order for " + target + ": " + e.what());
            failed = true;
        }
    }

    return failed ? 1 : 0;
}

int cmd_fetch(const std::vector<std::string>& targets, const std::vector<std::string>& flags) {
    for(const auto& flag : flags) {
        if(flag == "fetch") continue;
        Logger::error("Unsupported fetch option: " + flag);
        Logger::error("Usage: jpacker fetch <pkg>");
        return 1;
    }

    if(targets.empty()) {
        Logger::error("Usage: jpacker fetch <pkg>");
        return 1;
    }

    bool                                           failed = false;
    std::vector<std::pair<std::string, BuildPlan>> plans;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_fetch_plan(target);

            if(i > 0) std::cout << std::endl;
            print_fetch_plan(plan);
            // POLICY(#150): fetch は read-only retrieval stage。metadata risk は表示するが取得を妨げない。
            require_fetchable_build_plan(target, plan);
            plans.emplace_back(target, std::move(plan));
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch repositories for " + target + ": " + e.what());
            failed = true;
        }
    }

    // POLICY(#174): 全targetのschema/semantic preflightが成功するまでclone/fetchへ進まない。
    if(failed) return 1;

    for(const auto& [target, plan] : plans) {
        for(const auto& entry : plan.order) {
            try {
                fetch_persistent_checkout(
                        entry.package_base,
                        aur_git_url_for_package_base(entry.package_base));
            } catch(const std::exception& e) {
                Logger::error("Failed to fetch repositories for " + target + ": " + e.what());
                failed = true;
            }
        }
    }

    return failed ? 1 : 0;
}

int cmd_export_pkgbuild_tree(const std::string& target) {
    export_pkgbuild_tree(target);
    return 0;
}

int cmd_print_pkgbuild(const std::string& target) {
    std::string pkgbuild = load_pkgbuild_for_stdout(target);
    if(pkgbuild.size() >
       static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("PKGBUILD is too large to write to stdout.");
    }

    // POLICY(#167/#196): moduleがtemporary checkoutをcleanupしてから返したbytesだけを出力する。
    std::cout.write(pkgbuild.data(), static_cast<std::streamsize>(pkgbuild.size()));
    std::cout.flush();
    if(!std::cout) {
        throw std::runtime_error("Failed to write PKGBUILD to stdout.");
    }
    return 0;
}

int cmd_query_foreign_updates() {
    bool failed = false;

    std::vector<InstalledPackage> packages = get_foreign_packages();
    if(packages.empty()) {
        Logger::info("No foreign packages found.");
        return 0;
    }

    std::vector<std::string> package_names;
    for(const auto& local_pkg : packages) {
        package_names.push_back(local_pkg.name);
    }

    Logger::info("Checking AUR updates for " + std::to_string(packages.size()) + " foreign packages...");

    std::map<std::string, AurPackageInfo> aur_packages;
    const size_t                          batch_size = 100;
    for(size_t offset = 0; offset < package_names.size(); offset += batch_size) {
        size_t end = std::min(offset + batch_size, package_names.size());
        Logger::info("Fetching AUR info for packages " + std::to_string(offset + 1) + "-" + std::to_string(end) + " of " +
                     std::to_string(package_names.size()) + "...");

        std::vector<std::string> batch(package_names.begin() + offset, package_names.begin() + end);
        try {
            std::map<std::string, AurPackageInfo> batch_results = AurClient::info_many(batch);
            if(batch_results.empty()) {
                Logger::warn("Bulk AUR info returned no results. Falling back to per-package checks for this batch.");
                for(const auto& package_name : batch) {
                    std::optional<AurPackageInfo> aur_pkg = AurClient::info(package_name);
                    if(aur_pkg.has_value()) {
                        batch_results[aur_pkg->Name] = aur_pkg.value();
                    }
                }
            }
            aur_packages.insert(batch_results.begin(), batch_results.end());
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            Logger::error("Failed to fetch AUR info: " + std::string(e.what()));
            failed = true;
        }
    }

    for(size_t i = 0; i < packages.size(); ++i) {
        const auto& local_pkg = packages[i];
        Logger::info("Checking package " + std::to_string(i + 1) + "/" + std::to_string(packages.size()) + ": " + local_pkg.name);

        auto aur_pkg = aur_packages.find(local_pkg.name);
        if(aur_pkg == aur_packages.end()) {
            Logger::warn("Foreign package not found in AUR: " + local_pkg.name);
            continue;
        }

        if(aur_version_is_newer(aur_pkg->second.Version, local_pkg.version)) {
            std::cout << local_pkg.name << " " << local_pkg.version << " -> " << aur_pkg->second.Version << std::endl;
        }
    }

    return failed ? 1 : 0;
}
