#include "dependency_plan.hpp"

#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

const int MAX_RECURSIVE_DEP_DEPTH = 16;

enum class BuildPlanResolutionMode {
    Legacy,
    CaptureOrdinaryFailures
};

struct BuildPlanResolutionFailureContext {
    BuildPlan&                     plan;
    RootTargetIdentity             root;
    std::optional<std::string>     parent_package_name;
    std::optional<std::string>     parent_package_base;
    std::optional<std::string>     dependency_specification;
};

std::optional<AurPackageInfo> query_aur_package_info(
        const std::string& package_name, BuildPlanResolutionMode mode) {
    if(mode == BuildPlanResolutionMode::CaptureOrdinaryFailures) {
        return AurClient::info_strict(package_name);
    }
    return AurClient::info(package_name);
}

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

bool same_resolution_failure(
        const BuildPlanResolutionFailure& lhs,
        const BuildPlanResolutionFailure& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.parent_package_name == rhs.parent_package_name &&
           lhs.parent_package_base == rhs.parent_package_base &&
           lhs.subject == rhs.subject &&
           lhs.dependency_specification == rhs.dependency_specification;
}

void add_resolution_failure(
        BuildPlanResolutionFailureContext* context,
        BuildPlanResolutionFailureKind kind, const std::string& subject,
        const std::string& diagnostic) {
    if(context == nullptr) return;

    BuildPlanResolutionFailure failure{
            kind,
            context->parent_package_name,
            context->parent_package_base,
            subject,
            context->dependency_specification,
            {context->root},
            diagnostic};
    auto same_failure = [&failure](const BuildPlanResolutionFailure& existing) {
        return same_resolution_failure(existing, failure);
    };
    auto it = std::find_if(
            context->plan.resolution_failures.begin(),
            context->plan.resolution_failures.end(), same_failure);
    if(it == context->plan.resolution_failures.end()) {
        context->plan.resolution_failures.push_back(std::move(failure));
        return;
    }
    if(std::find(it->roots.begin(), it->roots.end(), context->root) == it->roots.end()) {
        it->roots.push_back(context->root);
    }
}

enum class RepositoryPackageQueryStatus {
    Present,
    NotFound,
    Unavailable
};

RepositoryPackageQueryStatus query_repository_package(
        const std::string& package_name, BuildPlanResolutionMode mode,
        BuildPlanResolutionFailureContext* failure_context) {
    if(mode == BuildPlanResolutionMode::Legacy) {
        return is_repo_package(package_name)
                ? RepositoryPackageQueryStatus::Present
                : RepositoryPackageQueryStatus::NotFound;
    }

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict(package_name);
    if(std::holds_alternative<RepositoryPackagePresent>(result))
        return RepositoryPackageQueryStatus::Present;
    if(std::holds_alternative<RepositoryPackageNotFound>(result))
        return RepositoryPackageQueryStatus::NotFound;

    const RepositoryMetadataFailure& failure =
            std::get<RepositoryMetadataFailure>(result);
    add_resolution_failure(
            failure_context,
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            package_name, failure.diagnostic);
    return RepositoryPackageQueryStatus::Unavailable;
}

std::vector<ProvidedDependency> find_aur_providers(
        const std::string& dependency_name,
        BuildPlanResolutionFailureContext* failure_context = nullptr) {
    std::vector<ProvidedDependency> providers;
    if(!is_valid_package_name(dependency_name)) return providers;

    std::vector<std::string> candidates;
    try {
        if(failure_context == nullptr) {
            candidates = AurClient::search_names_by_provides(dependency_name);
        } else {
            candidates = AurClient::search_names_by_provides_strict(dependency_name);
        }
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        add_resolution_failure(
                failure_context,
                BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                dependency_name, e.what());
        Logger::warn("Failed to search AUR providers for " + dependency_name + ": " + e.what());
        return providers;
    }
    for(const auto& candidate : candidates) {
        try {
            std::optional<AurPackageInfo> info = failure_context == nullptr
                    ? AurClient::info(candidate)
                    : AurClient::info_strict(candidate);
            if(info.has_value() && aur_package_provides(info.value(), dependency_name)) {
                add_provider_candidate(providers, ProvidedDependency{"aur", info->Name});
            } else if(failure_context != nullptr && !info.has_value()) {
                add_resolution_failure(
                        failure_context,
                        BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                        candidate,
                        "AUR provider candidate metadata was not returned.");
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            add_resolution_failure(
                    failure_context,
                    BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                    candidate, e.what());
            Logger::warn("Failed to check AUR provider " + candidate + ": " + e.what());
        }
    }

    return providers;
}

std::vector<ProvidedDependency> find_dependency_providers(
        const std::string& dependency_name,
        BuildPlanResolutionFailureContext* failure_context = nullptr) {
    std::vector<ProvidedDependency> repo_provider;
    if(failure_context == nullptr) {
        repo_provider = find_repo_providers(dependency_name);
    } else {
        StrictRepositoryProvidersQueryResult result =
                query_repository_providers_strict(dependency_name);
        if(const auto* failure = std::get_if<RepositoryMetadataFailure>(&result);
           failure != nullptr) {
            add_resolution_failure(
                    failure_context,
                    BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
                    dependency_name, failure->diagnostic);
            // POLICY(#267): unavailable repository metadata is not confirmed absence.
            return {};
        }
        repo_provider =
                std::get<std::vector<ProvidedDependency>>(std::move(result));
    }
    // POLICY: pacman-first。official repo provider が見つかる場合は AUR provider を混ぜない。
    if(!repo_provider.empty()) return repo_provider;
    return find_aur_providers(dependency_name, failure_context);
}

void add_dependency(
        std::vector<std::string>& dependencies, std::set<std::string>& seen,
        const std::string& dependency) {
    std::string dep = trim(dependency);
    if(dep.empty()) return;
    if(seen.insert(dep).second) dependencies.push_back(dep);
}

void add_typed_dependency(
        std::vector<TypedPackageDependency>& dependencies,
        const std::string& dependency, PackageRole role) {
    if(trim(dependency).empty()) return;

    auto same_dependency = [&dependency, role](const TypedPackageDependency& existing) {
        return existing.specification == dependency && existing.role == role;
    };
    if(std::find_if(dependencies.begin(), dependencies.end(), same_dependency) != dependencies.end()) return;
    dependencies.push_back(TypedPackageDependency{dependency, role});
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

std::vector<TypedPackageDependency> collect_typed_build_dependencies(const AurPackageInfo& pkg) {
    std::vector<TypedPackageDependency> dependencies;

    for(const auto& dep : pkg.Depends)
        add_typed_dependency(dependencies, dep, PackageRole::RuntimeDependency);
    for(const auto& dep : pkg.MakeDepends)
        add_typed_dependency(dependencies, dep, PackageRole::BuildDependency);
    for(const auto& dep : pkg.CheckDepends)
        add_typed_dependency(dependencies, dep, PackageRole::CheckDependency);

    return dependencies;
}

DesiredInstallReason desired_install_reason(const PlannedPackageTarget& target) {
    if(std::find(target.roles.begin(), target.roles.end(), PackageRole::Root) != target.roles.end()) {
        return DesiredInstallReason::Explicit;
    }

    for(const auto role : target.roles) {
        if(role == PackageRole::RuntimeDependency || role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency) {
            return DesiredInstallReason::Dependency;
        }
    }

    throw std::logic_error("Planned package target has no package role: " + target.package_name);
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

int package_role_rank(PackageRole role) {
    switch(role) {
    case PackageRole::Root:
        return 0;
    case PackageRole::RuntimeDependency:
        return 1;
    case PackageRole::BuildDependency:
        return 2;
    case PackageRole::CheckDependency:
        return 3;
    }
    throw std::logic_error("Unknown package role.");
}

void add_package_role(std::vector<PackageRole>& roles, PackageRole role) {
    if(std::find(roles.begin(), roles.end(), role) != roles.end()) return;
    roles.push_back(role);
    std::sort(roles.begin(), roles.end(), [](PackageRole lhs, PackageRole rhs) {
        return package_role_rank(lhs) < package_role_rank(rhs);
    });
}

bool root_identity_less(const RootTargetIdentity& lhs, const RootTargetIdentity& rhs) {
    if(lhs.invocation_index != rhs.invocation_index) {
        return lhs.invocation_index < rhs.invocation_index;
    }
    return lhs.requested_name < rhs.requested_name;
}

void add_root_identity(
        std::vector<RootTargetIdentity>& roots, const RootTargetIdentity& root) {
    if(std::find(roots.begin(), roots.end(), root) != roots.end()) return;
    roots.push_back(root);
    std::sort(roots.begin(), roots.end(), root_identity_less);
}

PlannedPackageTarget* find_package_target(
        BuildPlan& plan, const std::string& package_name) {
    auto same_name = [&package_name](const PlannedPackageTarget& target) {
        return target.package_name == package_name;
    };
    auto it = std::find_if(plan.package_targets.begin(), plan.package_targets.end(), same_name);
    return it == plan.package_targets.end() ? nullptr : &(*it);
}

void add_planned_package_target(
        BuildPlan& plan, const AurPackageInfo& info,
        const std::vector<PackageRole>& roles, const RootTargetIdentity& root) {
    PlannedPackageTarget* target = find_package_target(plan, info.Name);
    if(target == nullptr) {
        plan.package_targets.push_back(
                PlannedPackageTarget{info.Name, package_base_name(info), {}, {}});
        target = &plan.package_targets.back();
    }

    for(const auto role : roles) add_package_role(target->roles, role);
    add_root_identity(target->roots, root);
}

std::vector<TypedPackageDependency> typed_dependencies_for_specification(
        const std::vector<TypedPackageDependency>& dependencies,
        const std::string& specification) {
    std::vector<TypedPackageDependency> matches;
    for(const auto& dependency : dependencies) {
        if(trim(dependency.specification) == specification) matches.push_back(dependency);
    }
    return matches;
}

std::vector<PackageRole> package_roles_for_dependencies(
        const std::vector<TypedPackageDependency>& dependencies) {
    std::vector<PackageRole> roles;
    for(const auto& dependency : dependencies) add_package_role(roles, dependency.role);
    return roles;
}

void add_build_plan_dependency_edges(
        BuildPlan& plan, const BuildPlanDependencyEdge& classified_edge,
        const std::vector<TypedPackageDependency>& dependencies) {
    for(const auto& dependency : dependencies) {
        BuildPlanDependencyEdge edge = classified_edge;
        edge.dependency_spec = dependency.specification;
        edge.role = dependency.role;
        plan.dependency_edges.push_back(std::move(edge));
    }
}

std::optional<std::string> resolved_aur_dependency_name(
        const BuildPlanDependencyEdge& edge) {
    if(edge.kind == DependencyKind::Aur) return edge.resolved_package_name;
    if(edge.kind == DependencyKind::Provided && edge.resolved_provider.has_value() &&
       edge.resolved_provider->repository == "aur") {
        return edge.resolved_provider->package_name;
    }
    return std::nullopt;
}

void propagate_root_identities(BuildPlan& plan) {
    // WHY(#218): PackageBase visitedはlegacy orderを守るためinvocation-wideで共有する。
    // 後からrootになったvisited nodeの既存subtreeへは、queryを増やさずedge上でidentityを伝播する。
    for(const auto& root : plan.root_targets) {
        std::vector<std::string> pending = {root.requested_name};
        std::set<std::string>    visited_package_names;

        while(!pending.empty()) {
            std::string package_name = std::move(pending.back());
            pending.pop_back();
            if(!visited_package_names.insert(package_name).second) continue;

            PlannedPackageTarget* target = find_package_target(plan, package_name);
            if(target == nullptr) continue;
            add_root_identity(target->roots, root);

            for(const auto& edge : plan.dependency_edges) {
                if(edge.parent_package_name != package_name) continue;
                std::optional<std::string> dependency_name = resolved_aur_dependency_name(edge);
                if(!dependency_name.has_value() ||
                   find_package_target(plan, dependency_name.value()) == nullptr) {
                    continue;
                }
                pending.push_back(dependency_name.value());
            }
        }
    }
}

void propagate_resolution_failure_root_identities(BuildPlan& plan) {
    for(auto& failure : plan.resolution_failures) {
        if(!failure.parent_package_name.has_value()) continue;

        for(const auto& target : plan.package_targets) {
            if(target.package_name != failure.parent_package_name.value()) continue;
            if(failure.parent_package_base.has_value() &&
               target.package_base != failure.parent_package_base.value()) {
                continue;
            }
            for(const auto& root : target.roots) {
                add_root_identity(failure.roots, root);
            }
        }
    }
}

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
        std::set<std::string>& visiting, const std::vector<PackageRole>& roles,
        const RootTargetIdentity& root, int depth, int max_depth,
        bool traverse_aur_providers, BuildPlanResolutionMode resolution_mode,
        const std::optional<std::string>& parent_package_name,
        const std::optional<std::string>& parent_package_base,
        const std::optional<std::string>& dependency_specification) {
    if(depth > max_depth) {
        add_unique_value(plan.unresolved, package_name + " (max depth reached)");
        return;
    }

    std::optional<AurPackageInfo> info;
    BuildPlanResolutionFailureContext package_failure_context{
            plan, root, parent_package_name, parent_package_base,
            dependency_specification};
    BuildPlanResolutionFailureContext* failure_context =
            resolution_mode == BuildPlanResolutionMode::CaptureOrdinaryFailures
            ? &package_failure_context
            : nullptr;
    try {
        info = query_aur_package_info(package_name, resolution_mode);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        add_resolution_failure(
                failure_context,
                BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
                package_name, e.what());
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
    // POLICY(#218): role/root metadataはPackageBase visitedより先にmergeする。
    // legacy orderのpost-order追加はこの位置へ動かさない。
    add_planned_package_target(plan, info.value(), roles, root);

    std::string build_unit = package_base_name(info.value());
    if(visited.count(build_unit) > 0) return;
    if(visiting.count(build_unit) > 0) {
        add_unique_value(plan.cycles, build_unit);
        return;
    }

    visiting.insert(build_unit);

    const std::vector<TypedPackageDependency> typed_dependencies =
            collect_typed_build_dependencies(info.value());
    for(const auto& dependency : collect_build_dependencies(info.value())) {
        std::vector<TypedPackageDependency> matching_dependencies =
                typed_dependencies_for_specification(typed_dependencies, dependency);
        if(matching_dependencies.empty()) {
            throw std::logic_error("Build dependency is missing its package role: " + dependency);
        }
        std::vector<PackageRole> dependency_roles =
                package_roles_for_dependencies(matching_dependencies);
        BuildPlanDependencyEdge edge{
                info->Name, build_unit, matching_dependencies.front().specification,
                matching_dependencies.front().role, DependencyKind::Unknown,
                std::nullopt, std::nullopt, std::nullopt};

        ParsedDependency parsed = parse_dependency_string(dependency);
        std::string      dep_name = parsed.name;
        if(!is_valid_package_name(dep_name)) {
            add_unique_value(plan.unresolved, dependency);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            continue;
        }
        if(parsed.has_malformed_constraint()) {
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            continue;
        }
        if(parsed.has_constraint()) {
            // POLICY(#96): plan は未検証 constraint を解決済み扱いにしない。
            add_unique_value(plan.unresolved, dependency_constraint_unresolved_reason(dependency));
        }
        BuildPlanResolutionFailureContext dependency_failure_context{
                plan, root, info->Name, build_unit, dependency};
        BuildPlanResolutionFailureContext* dependency_failure_sink =
                resolution_mode == BuildPlanResolutionMode::CaptureOrdinaryFailures
                ? &dependency_failure_context
                : nullptr;
        RepositoryPackageQueryStatus repository_status =
                query_repository_package(
                        dep_name, resolution_mode, dependency_failure_sink);
        if(repository_status == RepositoryPackageQueryStatus::Present) {
            edge.kind = DependencyKind::Repo;
            edge.resolved_package_name = dep_name;
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            continue;
        }
        if(repository_status == RepositoryPackageQueryStatus::Unavailable) {
            add_unique_value(plan.unresolved, dependency);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            continue;
        }

        std::optional<AurPackageInfo> dependency_info;
        try {
            dependency_info = query_aur_package_info(dep_name, resolution_mode);
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            add_resolution_failure(
                    dependency_failure_sink,
                    BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
                    dep_name, e.what());
            Logger::warn("Failed to check AUR dependency " + dep_name + ": " + e.what());
        }

        if(dependency_info.has_value()) {
            edge.kind = DependencyKind::Aur;
            edge.resolved_package_name = dependency_info->Name;
            edge.resolved_package_base = dependency_info->PackageBase;
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            collect_aur_build_plan(
                    dep_name, plan, visited, visiting, dependency_roles, root,
                    depth + 1, max_depth, traverse_aur_providers,
                    resolution_mode, info->Name, build_unit, dependency);
            continue;
        }

        std::vector<ProvidedDependency> providers =
                find_dependency_providers(dep_name, dependency_failure_sink);
        if(providers.size() == 1) {
            const ProvidedDependency& provider = providers.front();
            edge.kind = DependencyKind::Provided;
            edge.resolved_provider = provider;
            add_build_plan_provided_dependency(plan, dependency, provider);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            if(traverse_aur_providers && provider.repository == "aur") {
                collect_aur_build_plan(
                        provider.package_name, plan, visited, visiting, dependency_roles, root,
                        depth + 1, max_depth, traverse_aur_providers,
                        resolution_mode, info->Name, build_unit, dependency);
            }
        } else if(providers.size() > 1) {
            edge.kind = DependencyKind::AmbiguousProvider;
            add_build_plan_ambiguous_provider(plan, dependency, providers);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        } else {
            add_unique_value(plan.unresolved, dependency);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        }
    }

    visiting.erase(build_unit);
    visited.insert(build_unit);
    add_build_plan_entry(plan, info.value());
}

BuildPlan resolve_build_plan_internal(
        const std::vector<std::string>& targets,
        BuildPlanResolutionMode resolution_mode) {
    if(targets.empty()) throw std::invalid_argument("Build plan targets must not be empty.");

    for(const auto& target : targets) require_valid_package_name(target);
    if(resolution_mode == BuildPlanResolutionMode::Legacy) {
        // POLICY: legacy resolverの事前存在確認と例外境界は通常-S consumerの契約。
        for(const auto& target : targets) {
            if(!AurClient::info(target).has_value()) {
                throw std::runtime_error("AUR package not found: " + target);
            }
        }
    }

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    const std::vector<PackageRole> root_roles = {PackageRole::Root};
    for(std::size_t i = 0; i < targets.size(); ++i) {
        RootTargetIdentity root{i, targets[i]};
        plan.root_targets.push_back(root);
        collect_aur_build_plan(
                targets[i], plan, visited, visiting, root_roles, root, 0,
                MAX_RECURSIVE_DEP_DEPTH, true, resolution_mode,
                std::nullopt, std::nullopt, std::nullopt);
    }
    propagate_root_identities(plan);
    propagate_resolution_failure_root_identities(plan);
    return plan;
}

} // namespace

std::vector<BuildPlanMetadataRisk> collect_build_plan_metadata_risks(const AurPackageInfo& pkg) {
    BuildPlan plan;
    add_build_plan_metadata_risk(plan, pkg);
    return std::move(plan.metadata_risks);
}

BuildPlan resolve_build_plan(const std::string& target) {
    return resolve_build_plan(std::vector<std::string>{target});
}

BuildPlan resolve_build_plan(const std::vector<std::string>& targets) {
    return resolve_build_plan_internal(targets, BuildPlanResolutionMode::Legacy);
}

BuildPlan resolve_build_plan_for_preflight(
        const std::vector<std::string>& targets) {
    return resolve_build_plan_internal(
            targets, BuildPlanResolutionMode::CaptureOrdinaryFailures);
}

BuildPlan resolve_fetch_plan(const std::string& target) {
    require_valid_package_name(target);
    if(!AurClient::info(target).has_value()) throw std::runtime_error("AUR package not found: " + target);

    BuildPlan             plan;
    std::set<std::string> visited;
    std::set<std::string> visiting;
    RootTargetIdentity    root{0, target};
    plan.root_targets.push_back(root);
    const std::vector<PackageRole> root_roles = {PackageRole::Root};
    // POLICY: fetch は取得対象の列挙まで。AUR provider を辿って暗黙に追加取得しない。
    collect_aur_build_plan(
            target, plan, visited, visiting, root_roles, root, 0,
            MAX_RECURSIVE_DEP_DEPTH, false, BuildPlanResolutionMode::Legacy,
            std::nullopt, std::nullopt, std::nullopt);
    propagate_root_identities(plan);
    return plan;
}

namespace {

std::string join_guard_summary_values(const std::vector<std::string>& values) {
    std::stringstream summary;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) summary << ", ";
        summary << values[i];
    }
    return summary.str();
}

std::string provider_summary(const ProvidedDependency& provider) {
    return provider.repository + "/" + provider.package_name;
}

std::string ambiguous_provider_dependency_summary(const AmbiguousProvidedDependency& dependency) {
    std::vector<std::string> candidates;
    for(const auto& candidate : dependency.candidates) {
        candidates.push_back(provider_summary(candidate));
    }
    return dependency.dependency + " (" + join_guard_summary_values(candidates) + ")";
}

std::string join_ambiguous_provider_summaries(const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::vector<std::string> values;
    for(const auto& dependency : dependencies) {
        values.push_back(ambiguous_provider_dependency_summary(dependency));
    }
    return join_guard_summary_values(values);
}

std::string split_package_target_summary(const BuildPlanSplitPackageTarget& target) {
    return target.package_name + " (base: " + target.package_base + ")";
}

std::string join_split_package_target_summaries(const std::vector<BuildPlanSplitPackageTarget>& targets) {
    std::vector<std::string> values;
    for(const auto& target : targets) {
        values.push_back(split_package_target_summary(target));
    }
    return join_guard_summary_values(values);
}

std::string metadata_risk_summary(const BuildPlanMetadataRisk& risk) {
    std::vector<std::string> metadata;
    if(!risk.conflicts.empty()) metadata.push_back("conflicts: " + join_guard_summary_values(risk.conflicts));
    if(!risk.replaces.empty()) metadata.push_back("replaces: " + join_guard_summary_values(risk.replaces));

    std::stringstream summary;
    summary << risk.package_name;
    if(risk.package_base != risk.package_name) summary << " (base: " << risk.package_base << ")";
    summary << " [";
    for(size_t i = 0; i < metadata.size(); ++i) {
        if(i > 0) summary << "; ";
        summary << metadata[i];
    }
    summary << "]";
    return summary.str();
}

std::string join_metadata_risk_summaries(const std::vector<BuildPlanMetadataRisk>& risks) {
    std::vector<std::string> values;
    for(const auto& risk : risks) {
        values.push_back(metadata_risk_summary(risk));
    }
    return join_guard_summary_values(values);
}

} // namespace

void require_fetchable_build_plan(const std::string& target, const BuildPlan& plan) {
    if(!plan.unresolved.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; unresolved dependencies: " +
                join_guard_summary_values(plan.unresolved));
    }
    if(!plan.ambiguous_providers.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; ambiguous providers: " +
                join_ambiguous_provider_summaries(plan.ambiguous_providers));
    }
    if(!plan.cycles.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target + "; cyclic dependencies: " +
                join_guard_summary_values(plan.cycles));
    }
}

void require_executable_build_plan(const std::string& target, const BuildPlan& plan) {
    require_fetchable_build_plan(target, plan);
    if(!plan.metadata_risks.empty()) {
        throw std::runtime_error(
                "Cannot execute build plan for " + target +
                "; conflicts/replaces metadata requires manual review: " +
                join_metadata_risk_summaries(plan.metadata_risks));
    }
}

void require_executable_install_plan(const std::string& target, const BuildPlan& plan) {
    // POLICY: 段階的なguard呼び出し順は、複数の問題があるplanで最初に報告するcategoryの契約。
    require_executable_build_plan(target, plan);
    if(!plan.split_package_targets.empty()) {
        throw std::runtime_error(
                "Cannot execute install plan for " + target +
                "; split package install target selection is not implemented: " +
                join_split_package_target_summaries(plan.split_package_targets));
    }
}
