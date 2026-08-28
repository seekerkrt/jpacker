#pragma once

#include "dependency_plan.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dependency_plan_projection_support {

struct RootPackage {
    std::string package_name;
    std::string package_base;
    std::vector<TypedPackageDependency> dependencies;
    std::vector<std::string> conflicts;
    std::vector<std::string> replaces;
};

struct DependencyContext {
    std::string parent_package_name;
    std::string parent_package_base;
    std::vector<TypedPackageDependency> declarations;
    RootTargetIdentity root;
};

struct DependencyDecision {
    bool handled_locally;
    std::optional<std::string> cycle_package_base;
    std::optional<std::string> unresolved_reason;
};

using LocalDependencyResolver = std::function<DependencyDecision(
    const DependencyContext& context)>;

struct RemoteProviderIdentityConflict {
    std::string parent_package_name;
    std::string dependency_specification;
    ProvidedDependency provider;
};

struct ResolutionResult {
    BuildPlan plan;
    std::vector<RemoteProviderIdentityConflict> identity_conflicts;
};

ResolutionResult resolve(
    const std::vector<RootPackage>& roots,
    const std::set<std::string>& local_package_bases,
    const LocalDependencyResolver& resolve_local_dependency,
    const ProviderSelectionCallback& select_provider);

} // namespace dependency_plan_projection_support
