#include "dependency_plan.hpp"

#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

const int MAX_RECURSIVE_DEP_DEPTH = 16;

// dependency planをmonolithへ逆依存させず、汎用utilityを公開しないためのlocal helper。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

void add_unique_value(std::vector<std::string>& values, const std::string& value) {
    std::string trimmed = trim(value);
    if(trimmed.empty()) return;
    if(std::find(values.begin(), values.end(), trimmed) == values.end()) values.push_back(trimmed);
}

std::string package_base_name(const AurPackageInfo& info) {
    // POLICY(#174): PackageBase は strict AUR RPC parser の required identifier。
    return info.PackageBase;
}

bool has_distinct_package_base(const AurPackageInfo& info) {
    return info.PackageBase != info.Name;
}

bool same_provider(const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.repository == rhs.repository && lhs.package_name == rhs.package_name;
}

void add_provider_candidate(std::vector<ProvidedDependency>& candidates, const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider(existing, provider);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) != candidates.end()) return;
    candidates.push_back(provider);
}

bool aur_package_provides(const AurPackageInfo& info, const std::string& dependency_name) {
    for(const auto& provided : info.Provides) {
        if(provided_dependency_name(provided) == dependency_name) return true;
    }
    return false;
}

std::vector<ProvidedDependency> find_aur_providers(const std::string& dependency_name) {
    std::vector<ProvidedDependency> providers;
    if(!is_valid_package_name(dependency_name)) return providers;

    std::vector<std::string> candidates;
    try {
        candidates = AurClient::search_names_by_provides(dependency_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        Logger::warn("Failed to search AUR providers for " + dependency_name + ": " + e.what());
        return providers;
    }
    for(const auto& candidate : candidates) {
        try {
            std::optional<AurPackageInfo> info = AurClient::info(candidate);
            if(info.has_value() && aur_package_provides(info.value(), dependency_name)) {
                add_provider_candidate(providers, ProvidedDependency{"aur", info->Name});
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR provider " + candidate + ": " + e.what());
        }
    }

    return providers;
}

std::vector<ProvidedDependency> find_dependency_providers(const std::string& dependency_name) {
    std::vector<ProvidedDependency> repo_provider = find_repo_providers(dependency_name);
    // POLICY: pacman-first。official repo provider が見つかる場合は AUR provider を混ぜない。
    if(!repo_provider.empty()) return repo_provider;
    return find_aur_providers(dependency_name);
}

void add_dependency(
        std::vector<std::string>& dependencies, std::set<std::string>& seen,
        const std::string& dependency) {
    std::string dep = trim(dependency);
    if(dep.empty()) return;
    if(seen.insert(dep).second) dependencies.push_back(dep);
}

void add_classified_dependency(
        std::vector<std::string>& dependencies, const std::string& dependency,
        const std::string& package_name) {
    std::string display;
    if(dependency == package_name)
        display = dependency;
    else
        display = dependency + " (" + package_name + ")";
    dependencies.push_back(dependency_display_with_constraint_note(display, dependency));
}

void add_classified_aur_dependency(
        std::vector<std::string>& dependencies, const std::string& dependency,
        const AurPackageInfo& info) {
    std::string display;
    if(info.Name.empty() || dependency == info.Name)
        display = dependency;
    else
        display = dependency + " (" + info.Name + ")";
    if(has_distinct_package_base(info)) display += " (base: " + info.PackageBase + ")";
    dependencies.push_back(dependency_display_with_constraint_note(display, dependency));
}

std::string provided_dependency_display(
        const std::string& dependency, const ProvidedDependency& provider) {
    return dependency + " [provided by " + provider.repository + "/" + provider.package_name + "]" +
           dependency_constraint_note(dependency);
}

void add_ambiguous_provider_dependency(
        std::vector<AmbiguousProvidedDependency>& dependencies, const std::string& dependency,
        const std::vector<ProvidedDependency>& candidates) {
    std::string trimmed = trim(dependency);
    if(trimmed.empty() || candidates.empty()) return;

    // POLICY(#97/#143): 複数 provider はここで集約し、暗黙選択しない。
    auto same_dependency = [&trimmed](const AmbiguousProvidedDependency& existing) {
        return existing.dependency == trimmed;
    };
    auto it = std::find_if(dependencies.begin(), dependencies.end(), same_dependency);
    if(it == dependencies.end()) {
        dependencies.push_back(AmbiguousProvidedDependency{trimmed, {}});
        it = std::prev(dependencies.end());
    }
    for(const auto& candidate : candidates) {
        add_provider_candidate(it->candidates, candidate);
    }
}

void warn_unverified_version_constraint(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(!parsed.has_parseable_constraint()) return;
    Logger::warn("version constraint for " + parsed.raw + " is not verified");
}

} // namespace

std::vector<std::string> collect_build_dependencies(const AurPackageInfo& pkg) {
    std::vector<std::string> dependencies;
    std::set<std::string>    seen;

    for(const auto& dep : pkg.Depends)
        add_dependency(dependencies, seen, dep);
    for(const auto& dep : pkg.MakeDepends)
        add_dependency(dependencies, seen, dep);
    for(const auto& dep : pkg.CheckDepends)
        add_dependency(dependencies, seen, dep);

    return dependencies;
}

DependencyClassification classify_dependencies(const std::vector<std::string>& dependencies) {
    DependencyClassification result;

    for(const auto& dependency : dependencies) {
        ParsedDependency parsed = parse_dependency_string(dependency);
        std::string      package_name = parsed.name;
        if(!is_valid_package_name(package_name)) {
            result.unknown.push_back(dependency);
            continue;
        }
        if(parsed.has_malformed_constraint()) {
            result.unknown.push_back(dependency_constraint_unresolved_reason(dependency));
            continue;
        }
        warn_unverified_version_constraint(dependency);

        if(is_repo_package(package_name)) {
            add_classified_dependency(result.repo, dependency, package_name);
            continue;
        }

        try {
            std::optional<AurPackageInfo> info = AurClient::info(package_name);
            if(info.has_value()) {
                add_classified_aur_dependency(result.aur, dependency, info.value());
            } else {
                std::vector<ProvidedDependency> providers = find_dependency_providers(package_name);
                if(providers.size() == 1)
                    result.provided.push_back(provided_dependency_display(dependency, providers.front()));
                else if(providers.size() > 1)
                    add_ambiguous_provider_dependency(result.ambiguous_providers, dependency, providers);
                else
                    result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR dependency " + package_name + ": " + e.what());
            std::vector<ProvidedDependency> providers = find_repo_providers(package_name);
            if(providers.size() == 1)
                result.provided.push_back(provided_dependency_display(dependency, providers.front()));
            else if(providers.size() > 1)
                add_ambiguous_provider_dependency(result.ambiguous_providers, dependency, providers);
            else
                result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
        }
    }

    return result;
}

namespace {

RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited, int depth, int max_depth);

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg, std::set<std::string>& visited, int depth, int max_depth) {
    std::vector<RecursiveDependencyNode> nodes;
    for(const auto& dependency : collect_build_dependencies(pkg)) {
        nodes.push_back(resolve_recursive_dependency(dependency, visited, depth, max_depth));
    }
    return nodes;
}

RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited, int depth, int max_depth) {
    RecursiveDependencyNode node;
    node.dependency = dependency;
    ParsedDependency parsed = parse_dependency_string(dependency);
    node.package_name = parsed.name;

    if(!is_valid_package_name(node.package_name) || parsed.has_malformed_constraint()) {
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(is_repo_package(node.package_name)) {
        node.kind = DependencyKind::Repo;
        return node;
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(node.package_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        Logger::warn("Failed to check AUR dependency " + node.package_name + ": " + e.what());
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(!info.has_value()) {
        std::vector<ProvidedDependency> providers = find_dependency_providers(node.package_name);
        if(providers.size() == 1) {
            node.kind = DependencyKind::Provided;
            node.provided_by = providers.front();
        } else if(providers.size() > 1) {
            node.kind = DependencyKind::AmbiguousProvider;
            node.provider_candidates = providers;
        } else {
            node.kind = DependencyKind::Unknown;
        }
        return node;
    }

    node.kind = DependencyKind::Aur;
    node.package_base = package_base_name(info.value());
    if(!visited.insert(node.package_base).second) {
        node.already_visited = true;
        return node;
    }
    if(depth >= max_depth) {
        node.max_depth_reached = true;
        return node;
    }

    node.children = resolve_recursive_dependencies(info.value(), visited, depth + 1, max_depth);
    return node;
}

} // namespace

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(const AurPackageInfo& pkg) {
    std::set<std::string> visited;
    visited.insert(package_base_name(pkg));
    return resolve_recursive_dependencies(pkg, visited, 1, MAX_RECURSIVE_DEP_DEPTH);
}

namespace {

void add_build_plan_split_package_target(BuildPlan& plan, const AurPackageInfo& info) {
    if(!has_distinct_package_base(info)) return;

    auto same_target = [&info](const BuildPlanSplitPackageTarget& existing) {
        return existing.package_base == info.PackageBase && existing.package_name == info.Name;
    };
    if(std::find_if(plan.split_package_targets.begin(), plan.split_package_targets.end(), same_target) !=
       plan.split_package_targets.end())
        return;

    plan.split_package_targets.push_back(BuildPlanSplitPackageTarget{info.PackageBase, info.Name});
}

void add_build_plan_metadata_risk(BuildPlan& plan, const AurPackageInfo& info) {
    if(info.Conflicts.empty() && info.Replaces.empty()) return;

    std::string package_base = package_base_name(info);
    auto same_package = [&info, &package_base](const BuildPlanMetadataRisk& existing) {
        return existing.package_name == info.Name && existing.package_base == package_base;
    };
    if(std::find_if(plan.metadata_risks.begin(), plan.metadata_risks.end(), same_package) !=
       plan.metadata_risks.end())
        return;

    plan.metadata_risks.push_back(
            BuildPlanMetadataRisk{info.Name, package_base, info.Conflicts, info.Replaces});
}

void add_build_plan_entry(BuildPlan& plan, const AurPackageInfo& info) {
    std::string package_base = package_base_name(info);
    auto        same_base = [&package_base](const BuildPlanEntry& existing) { return existing.package_base == package_base; };
    auto        it = std::find_if(plan.order.begin(), plan.order.end(), same_base);
    add_build_plan_split_package_target(plan, info);
    if(it == plan.order.end()) {
        plan.order.push_back(BuildPlanEntry{package_base, {info.Name}});
        return;
    }
    add_unique_value(it->package_names, info.Name);
}

void add_build_plan_provided_dependency(
        BuildPlan& plan, const std::string& dependency, const ProvidedDependency& provider) {
    std::string trimmed = trim(dependency);
    if(trimmed.empty()) return;

    auto same_dependency = [&](const BuildPlanProvidedDependency& existing) {
        return existing.dependency == trimmed && existing.provider.repository == provider.repository &&
               existing.provider.package_name == provider.package_name;
    };
    if(std::find_if(plan.provided.begin(), plan.provided.end(), same_dependency) != plan.provided.end()) return;
    plan.provided.push_back(BuildPlanProvidedDependency{trimmed, provider});
}

void add_build_plan_ambiguous_provider(
        BuildPlan& plan, const std::string& dependency,
        const std::vector<ProvidedDependency>& candidates) {
    add_ambiguous_provider_dependency(plan.ambiguous_providers, dependency, candidates);
}

void collect_aur_build_plan(
        const std::string& package_name, BuildPlan& plan, std::set<std::string>& visited,
        std::set<std::string>& visiting, int depth, int max_depth, bool traverse_aur_providers) {
    if(depth > max_depth) {
        add_unique_value(plan.unresolved, package_name + " (max depth reached)");
        return;
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(package_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        Logger::warn("Failed to fetch AUR info for " + package_name + ": " + e.what());
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    if(!info.has_value()) {
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    // POLICY(#150): visited PackageBase で再帰を打ち切る場合も、package 単位の raw metadata は先に保持する。
    add_build_plan_metadata_risk(plan, info.value());

    std::string build_unit = package_base_name(info.value());
    if(visited.count(build_unit) > 0) return;
    if(visiting.count(build_unit) > 0) {
        add_unique_value(plan.cycles, build_unit);
        return;
    }

    visiting.insert(build_unit);

    for(const auto& dependency : collect_build_dependencies(info.value())) {
        ParsedDependency parsed = parse_dependency_string(dependency);
        std::string      dep_name = parsed.name;
        if(!is_valid_package_name(dep_name)) {
            add_unique_value(plan.unresolved, dependency);
            continue;
        }
        if(parsed.has_malformed_constraint()) {
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
            continue;
        }
        if(parsed.has_constraint()) {
            // POLICY(#96): plan は未検証 constraint を解決済み扱いにしない。
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
        }
        if(is_repo_package(dep_name)) continue;

        std::optional<AurPackageInfo> dependency_info;
        try {
            dependency_info = AurClient::info(dep_name);
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            Logger::warn("Failed to check AUR dependency " + dep_name + ": " + e.what());
        }

        if(dependency_info.has_value()) {
            collect_aur_build_plan(
                    dep_name, plan, visited, visiting, depth + 1, max_depth,
                    traverse_aur_providers);
            continue;
        }

        std::vector<ProvidedDependency> providers = find_dependency_providers(dep_name);
        if(providers.size() == 1) {
            const ProvidedDependency& provider = providers.front();
            add_build_plan_provided_dependency(plan, dependency, provider);
            if(traverse_aur_providers && provider.repository == "aur") {
                collect_aur_build_plan(
                        provider.package_name, plan, visited, visiting, depth + 1, max_depth,
                        traverse_aur_providers);
            }
        } else if(providers.size() > 1) {
            add_build_plan_ambiguous_provider(plan, dependency, providers);
        } else {
            add_unique_value(plan.unresolved, dependency);
        }
    }

    visiting.erase(build_unit);
    visited.insert(build_unit);
    add_build_plan_entry(plan, info.value());
}

} // namespace

std::vector<BuildPlanMetadataRisk> collect_build_plan_metadata_risks(const AurPackageInfo& pkg) {
    BuildPlan plan;
    add_build_plan_metadata_risk(plan, pkg);
    return std::move(plan.metadata_risks);
}

BuildPlan resolve_build_plan(const std::string& target) {
    require_valid_package_name(target);
    if(!AurClient::info(target).has_value()) throw std::runtime_error("AUR package not found: " + target);

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    collect_aur_build_plan(
            target, plan, visited, visiting, 0, MAX_RECURSIVE_DEP_DEPTH, true);
    return plan;
}

BuildPlan resolve_fetch_plan(const std::string& target) {
    require_valid_package_name(target);
    if(!AurClient::info(target).has_value()) throw std::runtime_error("AUR package not found: " + target);

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    // POLICY: fetch は取得対象の列挙まで。AUR provider を辿って暗黙に追加取得しない。
    collect_aur_build_plan(
            target, plan, visited, visiting, 0, MAX_RECURSIVE_DEP_DEPTH, false);
    return plan;
}
