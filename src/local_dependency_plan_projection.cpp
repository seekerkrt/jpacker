#include "local_dependency_plan_projection.hpp"

#include "dependency_constraint.hpp"
#include "dependency_plan_projection_support.hpp"
#include "dependency_spec.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using Relation = LocalPackageMetadataRelation;
using RelationKind = LocalPackageMetadataRelationKind;
using Target = LocalPackageMetadataRelationTarget;
using TargetKind = LocalPackageMetadataRelationTargetKind;

struct EffectiveChild {
    std::string                   package_name;
    bool                          architecture_supported;
    std::vector<const Relation*> relations;
};

struct RequiredDependency {
    TargetKind                                  kind;
    std::string                                 name;
    std::optional<ConsumerDependencyRequirement> requirement;
    bool                                        malformed_constraint;
};

struct Candidate {
    std::string                    package_name;
    LocalDependencyResolutionKind resolution_kind;
    std::optional<std::string>     provided_specification;
    std::optional<ProviderCapability> provider_capability;
    ObservedVersion                observed_version;
    bool                           architecture_supported;
};

bool architecture_matches(
        const std::vector<std::string>& architectures,
        const std::string& effective_architecture) {
    return std::find(
                   architectures.begin(), architectures.end(), "any") !=
                   architectures.end() ||
           std::find(
                   architectures.begin(), architectures.end(),
                   effective_architecture) != architectures.end();
}

bool child_supports_architecture(
        const LocalPackageMetadata& metadata,
        const LocalPackageMetadataChild& child,
        const std::string& effective_architecture) {
    if(child.has_architecture_override) {
        return architecture_matches(
                child.architectures, effective_architecture);
    }
    return architecture_matches(
            metadata.architectures, effective_architecture);
}

bool same_relation_key(const Relation& lhs, const Relation& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.architecture_qualifier == rhs.architecture_qualifier;
}

bool is_child_relation_for(
        const Relation& relation, const std::string& package_name) {
    return relation.scope.kind ==
                   LocalPackageMetadataScopeKind::ChildPackage &&
           relation.scope.package_name == package_name;
}

bool child_overrides_relation_key(
        const LocalPackageMetadata& metadata,
        const std::string& package_name, const Relation& base_relation) {
    return std::any_of(
            metadata.relations.begin(), metadata.relations.end(),
            [&](const Relation& relation) {
                return is_child_relation_for(relation, package_name) &&
                       same_relation_key(relation, base_relation);
            });
}

std::vector<const Relation*> effective_relations(
        const LocalPackageMetadata& metadata,
        const std::string& package_name,
        const std::string& effective_architecture) {
    std::vector<const Relation*> result;
    for(const auto& relation : metadata.relations) {
        if(relation.architecture_qualifier.has_value() &&
           relation.architecture_qualifier.value() !=
                   effective_architecture) {
            continue;
        }

        if(relation.scope.kind ==
           LocalPackageMetadataScopeKind::PackageBase) {
            if(child_overrides_relation_key(
                       metadata, package_name, relation)) {
                continue;
            }
        } else if(!is_child_relation_for(relation, package_name)) {
            continue;
        }

        if(!relation.is_explicit_unset) result.push_back(&relation);
    }
    return result;
}

std::vector<EffectiveChild> collect_effective_children(
        const LocalPackageMetadata& metadata,
        const std::string& effective_architecture) {
    std::vector<EffectiveChild> children;
    children.reserve(metadata.children.size());
    for(const auto& child : metadata.children) {
        children.push_back(EffectiveChild{
                child.name,
                child_supports_architecture(
                        metadata, child, effective_architecture),
                effective_relations(
                        metadata, child.name, effective_architecture)});
    }
    return children;
}

std::string local_package_version(const LocalPackageMetadata& metadata) {
    std::string version;
    if(metadata.epoch.has_value()) {
        version = metadata.epoch.value() + ":";
    }
    version += metadata.pkgver + "-" + metadata.pkgrel;
    return version;
}

std::optional<PackageRole> package_role(RelationKind kind) {
    switch(kind) {
    case RelationKind::Depends:
        return PackageRole::RuntimeDependency;
    case RelationKind::Makedepends:
        return PackageRole::BuildDependency;
    case RelationKind::Checkdepends:
        return PackageRole::CheckDependency;
    case RelationKind::Optdepends:
    case RelationKind::Provides:
    case RelationKind::Conflicts:
    case RelationKind::Replaces:
        return std::nullopt;
    }
    throw std::logic_error("Unknown local metadata relation kind.");
}

DependencyVersionRelation relation_from_local_comparison(
        LocalPackageMetadataComparison comparison) {
    switch(comparison) {
    case LocalPackageMetadataComparison::LessThan:
        return DependencyVersionRelation::LessThan;
    case LocalPackageMetadataComparison::LessThanOrEqual:
        return DependencyVersionRelation::LessThanOrEqual;
    case LocalPackageMetadataComparison::Equal:
        return DependencyVersionRelation::Equal;
    case LocalPackageMetadataComparison::GreaterThanOrEqual:
        return DependencyVersionRelation::GreaterThanOrEqual;
    case LocalPackageMetadataComparison::GreaterThan:
        return DependencyVersionRelation::GreaterThan;
    }
    throw std::logic_error("Unknown local metadata comparison.");
}

std::optional<DependencyRequirement> typed_requirement_from_relation(
        const Relation& relation) {
    if(!relation.target.has_value()) return std::nullopt;
    const Target& target = relation.target.value();
    if(target.kind == TargetKind::Soname) {
        return DependencyRequirement{SonameDependencyRequirement(
                relation.raw_value, target.name)};
    }
    if(target.kind != TargetKind::Package) {
        throw std::logic_error("Unknown local metadata relation target kind.");
    }

    std::optional<DependencyVersionConstraint> constraint;
    if(target.comparison.has_value()) {
        if(!target.version.has_value()) return std::nullopt;
        constraint.emplace(
                relation_from_local_comparison(target.comparison.value()),
                target.version.value());
    } else if(target.version.has_value()) {
        return std::nullopt;
    }
    return DependencyRequirement{ConsumerDependencyRequirement(
            relation.raw_value, target.name, std::move(constraint))};
}

std::optional<ProviderCapability> provider_capability_from_relation(
        const Relation& relation) {
    if(!relation.target.has_value() ||
       relation.target->kind != TargetKind::Package) {
        return std::nullopt;
    }
    return ProviderCapability(
            relation.raw_value, relation.target->name,
            relation.target->version);
}

void add_typed_dependency(
        std::vector<TypedPackageDependency>& dependencies,
        const Relation& relation, PackageRole role) {
    const TypedPackageDependency dependency{
            relation.raw_value, role, typed_requirement_from_relation(relation)};
    const auto same = [&](const TypedPackageDependency& existing) {
        return existing.specification == dependency.specification &&
               existing.role == dependency.role;
    };
    if(std::find_if(dependencies.begin(), dependencies.end(), same) ==
       dependencies.end()) {
        dependencies.push_back(dependency);
    }
}

std::vector<dependency_plan_projection_support::RootPackage>
collect_root_packages(const std::vector<EffectiveChild>& children,
                      const std::string& package_base) {
    std::vector<dependency_plan_projection_support::RootPackage> roots;
    roots.reserve(children.size());
    for(const auto& child : children) {
        dependency_plan_projection_support::RootPackage root{
                child.package_name, package_base, {}, {}, {}};
        if(child.architecture_supported) {
            for(const Relation* relation : child.relations) {
                const std::optional<PackageRole> role =
                        package_role(relation->kind);
                if(role.has_value()) {
                    add_typed_dependency(
                            root.dependencies, *relation, role.value());
                } else if(relation->kind == RelationKind::Conflicts) {
                    root.conflicts.push_back(relation->raw_value);
                } else if(relation->kind == RelationKind::Replaces) {
                    root.replaces.push_back(relation->raw_value);
                }
            }
        }
        roots.push_back(std::move(root));
    }
    return roots;
}

std::optional<DependencyVersionRelation> relation_from_operator(
        const std::optional<std::string>& value) {
    if(!value.has_value()) return std::nullopt;
    if(value.value() == "<") return DependencyVersionRelation::LessThan;
    if(value.value() == "<=") return DependencyVersionRelation::LessThanOrEqual;
    if(value.value() == "=") return DependencyVersionRelation::Equal;
    if(value.value() == ">=") return DependencyVersionRelation::GreaterThanOrEqual;
    if(value.value() == ">") return DependencyVersionRelation::GreaterThan;
    return std::nullopt;
}

RequiredDependency parse_required_dependency(
        const TypedPackageDependency& declaration) {
    if(declaration.requirement.has_value()) {
        const DependencyRequirement& requirement = declaration.requirement.value();
        if(const auto* consumer =
                   std::get_if<ConsumerDependencyRequirement>(&requirement)) {
            return RequiredDependency{
                    TargetKind::Package, consumer->package_name(), *consumer,
                    false};
        }
        if(const auto* soname =
                   std::get_if<SonameDependencyRequirement>(&requirement)) {
            return RequiredDependency{
                    TargetKind::Soname, soname->soname(), std::nullopt, false};
        }
        throw std::logic_error("Unknown typed dependency requirement.");
    }

    // AUR declaration parsing remains on its existing path until Slice 4
    // introduces its own metadata trust boundary. Local metadata always takes
    // the typed branch above and is never reparsed here.
    const std::string& specification = declaration.specification;
    const ParsedDependency parsed = parse_dependency_string(specification);
    const bool is_soname = !parsed.has_constraint() &&
                           parsed.name.find(':') != std::string::npos;
    const std::optional<DependencyVersionRelation> relation =
            relation_from_operator(parsed.op);
    std::optional<ConsumerDependencyRequirement> requirement;
    if(!is_soname && relation.has_value() && parsed.version.has_value()) {
        requirement.emplace(
                parsed.raw, parsed.name,
                DependencyVersionConstraint(
                        relation.value(), parsed.version.value()));
    } else if(!is_soname && !parsed.has_constraint()) {
        requirement.emplace(parsed.raw, parsed.name, std::nullopt);
    }
    return RequiredDependency{
            is_soname ? TargetKind::Soname : TargetKind::Package,
            parsed.name,
            std::move(requirement),
            parsed.has_malformed_constraint()};
}

bool same_candidate_identity(
        const Candidate& lhs, const Candidate& rhs) {
    return lhs.package_name == rhs.package_name &&
           lhs.resolution_kind == rhs.resolution_kind &&
           lhs.provided_specification == rhs.provided_specification;
}

void add_candidate(
        std::vector<Candidate>& candidates, Candidate candidate) {
    const auto same = [&](const Candidate& existing) {
        return same_candidate_identity(existing, candidate);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) ==
       candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

std::optional<Candidate> exact_candidate(
        const std::vector<EffectiveChild>& children,
        const RequiredDependency& dependency,
        const std::string& package_version) {
    if(dependency.kind != TargetKind::Package) return std::nullopt;
    for(const auto& child : children) {
        if(child.package_name == dependency.name) {
            return Candidate{
                    child.package_name,
                    LocalDependencyResolutionKind::Package,
                    std::nullopt,
                    std::nullopt,
                    ObservedVersion::available(
                            ObservedVersionSource::LocalExactPackage,
                            package_version),
                    child.architecture_supported};
        }
    }
    return std::nullopt;
}

void add_provided_candidates(
        const std::vector<EffectiveChild>& children,
        const RequiredDependency& dependency,
        std::vector<Candidate>& candidates) {
    for(const auto& child : children) {
        for(const Relation* relation : child.relations) {
            if(relation->kind != RelationKind::Provides ||
               !relation->target.has_value() ||
               relation->target->kind != dependency.kind ||
               relation->target->name != dependency.name) {
                continue;
            }
            const std::optional<ProviderCapability> capability =
                    provider_capability_from_relation(*relation);
            add_candidate(
                    candidates,
                    Candidate{
                            child.package_name,
                            LocalDependencyResolutionKind::Provided,
                            relation->raw_value,
                            capability,
                            capability.has_value() &&
                                            capability->version().has_value()
                                    ? ObservedVersion::available(
                                              ObservedVersionSource::
                                                      LocalProviderCapability,
                                              capability->version().value())
                                    : ObservedVersion::unknown(
                                              ObservedVersionSource::
                                                      LocalProviderCapability,
                                              dependency.kind == TargetKind::Package
                                                      ? ObservedVersionUnknownReason::
                                                                UnversionedProviderCapability
                                                      : ObservedVersionUnknownReason::
                                                                RelationKindNotComparable),
                            child.architecture_supported});
        }
    }
}

ConstraintEvaluation candidate_constraint_evaluation(
        const Candidate& candidate, const RequiredDependency& dependency) {
    if(dependency.malformed_constraint) {
        return ConstraintEvaluation::invalid(
                ConstraintInvalidReason::MalformedRequirement);
    }
    if(dependency.kind == TargetKind::Soname) {
        return ConstraintEvaluation::unknown(
                ObservedVersionUnknownReason::RelationKindNotComparable);
    }
    if(!dependency.requirement.has_value()) {
        return ConstraintEvaluation::invalid(
                ConstraintInvalidReason::InternalInvariantViolation);
    }
    return evaluate_consumer_dependency_requirement(
            dependency.requirement.value(), candidate.observed_version);
}

bool candidate_is_compatible(
        const Candidate& candidate,
        const RequiredDependency& dependency) {
    if(!candidate.architecture_supported || dependency.malformed_constraint) {
        return false;
    }
    if(dependency.kind == TargetKind::Soname) return true;
    switch(candidate_constraint_evaluation(candidate, dependency).satisfaction()) {
    case ConstraintSatisfaction::Unconstrained:
    case ConstraintSatisfaction::Satisfied:
        return true;
    case ConstraintSatisfaction::Unsatisfied:
    case ConstraintSatisfaction::Unknown:
    case ConstraintSatisfaction::Invalid:
    case ConstraintSatisfaction::Conflicting:
        return false;
    }
    throw std::logic_error("Unknown constraint satisfiability result.");
}

LocalDependencyPlanCandidate public_candidate(
        const Candidate& candidate, const RequiredDependency& dependency) {
    std::optional<std::string> version;
    if(const std::string* observed_version = candidate.observed_version.version()) {
        version = *observed_version;
    }
    return LocalDependencyPlanCandidate{
            candidate.package_name,
            candidate.provided_specification,
            std::move(version),
            std::nullopt,
            candidate_constraint_evaluation(candidate, dependency)};
}

std::vector<LocalDependencyPlanCandidate> public_candidates(
        const std::vector<Candidate>& candidates,
        const RequiredDependency& dependency) {
    std::vector<LocalDependencyPlanCandidate> result;
    result.reserve(candidates.size());
    for(const auto& candidate : candidates) {
        result.push_back(public_candidate(candidate, dependency));
    }
    return result;
}

bool same_failure(
        const LocalDependencyPlanFailure& lhs,
        const LocalDependencyPlanFailure& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.parent_package_name == rhs.parent_package_name &&
           lhs.dependency_specification == rhs.dependency_specification;
}

void add_failure(
        std::vector<LocalDependencyPlanFailure>& failures,
        LocalDependencyPlanFailure failure) {
    const auto same = [&](const LocalDependencyPlanFailure& existing) {
        return same_failure(existing, failure);
    };
    if(std::find_if(failures.begin(), failures.end(), same) ==
       failures.end()) {
        failures.push_back(std::move(failure));
    }
}

bool same_internal_edge(
        const LocalDependencyPlanInternalEdge& lhs,
        const LocalDependencyPlanInternalEdge& rhs) {
    return lhs.parent_package_name == rhs.parent_package_name &&
           lhs.dependency_specification == rhs.dependency_specification &&
           lhs.role == rhs.role &&
           lhs.resolved_package_name == rhs.resolved_package_name &&
           lhs.resolution_kind == rhs.resolution_kind &&
           lhs.provided_specification == rhs.provided_specification;
}

void add_internal_edge(
        std::vector<LocalDependencyPlanInternalEdge>& edges,
        LocalDependencyPlanInternalEdge edge) {
    const auto same = [&](const LocalDependencyPlanInternalEdge& existing) {
        return same_internal_edge(existing, edge);
    };
    if(std::find_if(edges.begin(), edges.end(), same) == edges.end()) {
        edges.push_back(std::move(edge));
    }
}

bool is_local_child(
        const std::vector<EffectiveChild>& children,
        const std::string& package_name) {
    return std::any_of(
            children.begin(), children.end(),
            [&](const EffectiveChild& child) {
                return child.package_name == package_name;
            });
}

dependency_plan_projection_support::DependencyDecision resolve_local_candidate(
        const dependency_plan_projection_support::DependencyContext& context,
        const std::vector<EffectiveChild>& children,
        const std::string& package_base,
        const std::string& package_version,
        std::vector<LocalDependencyPlanInternalEdge>& internal_edges,
        std::vector<LocalDependencyPlanFailure>& failures) {
    const std::string& specification =
            context.declarations.front().specification;
    const RequiredDependency dependency =
            parse_required_dependency(context.declarations.front());
    if(dependency.malformed_constraint) {
        return {false, std::nullopt, std::nullopt};
    }
    const std::optional<Candidate> exact =
            exact_candidate(children, dependency, package_version);
    std::vector<Candidate> candidates;
    if(exact.has_value()) candidates.push_back(exact.value());
    const bool has_compatible_exact =
            exact.has_value() &&
            candidate_is_compatible(exact.value(), dependency);
    if(!has_compatible_exact) {
        add_provided_candidates(children, dependency, candidates);
    }
    if(candidates.empty()) return {false, std::nullopt, std::nullopt};

    std::vector<Candidate> compatible;
    if(has_compatible_exact) {
        compatible.push_back(exact.value());
    } else {
        for(const auto& candidate : candidates) {
            if(!candidate_is_compatible(candidate, dependency)) continue;
            const auto same_package = [&](const Candidate& existing) {
                return existing.package_name == candidate.package_name;
            };
            if(std::find_if(
                       compatible.begin(), compatible.end(), same_package) ==
               compatible.end()) {
                compatible.push_back(candidate);
            }
        }
    }

    if(compatible.empty()) {
        if(!exact.has_value()) {
            // Version不適合のvirtual provideはlocal identity collisionではない。
            // 未解決dependencyとして既存repo/AUR/provider policyへ渡す。
            return {false, std::nullopt, std::nullopt};
        }
        add_failure(
                failures,
                LocalDependencyPlanFailure{
                        LocalDependencyPlanFailureKind::ConstraintMismatch,
                        context.parent_package_name,
                        specification,
                        std::nullopt,
                        public_candidates(candidates, dependency)});
        return {
                true,
                std::nullopt,
                specification + " (local candidate is incompatible)"};
    }

    if(compatible.size() > 1) {
        add_failure(
                failures,
                LocalDependencyPlanFailure{
                        LocalDependencyPlanFailureKind::AmbiguousLocalProvider,
                        context.parent_package_name,
                        specification,
                        std::nullopt,
                        public_candidates(compatible, dependency)});
        return {
                true,
                std::nullopt,
                specification + " (ambiguous local providers)"};
    }

    const Candidate& candidate = compatible.front();
    const bool remote_back_edge =
            context.parent_package_base != package_base;
    const bool direct_self_edge =
            context.parent_package_name == candidate.package_name;
    for(const auto& declaration : context.declarations) {
        add_internal_edge(
                internal_edges,
                LocalDependencyPlanInternalEdge{
                        context.parent_package_name,
                        declaration.specification,
                        declaration.role,
                        candidate.package_name,
                        candidate.resolution_kind,
                        candidate.provided_specification,
                        remote_back_edge || direct_self_edge});
    }
    return {
            true,
            remote_back_edge || direct_self_edge
                    ? std::optional<std::string>{package_base}
                    : std::nullopt,
            std::nullopt};
}

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

void add_package_role(
        std::vector<PackageRole>& roles, PackageRole role) {
    if(std::find(roles.begin(), roles.end(), role) != roles.end()) return;
    roles.push_back(role);
    std::sort(
            roles.begin(), roles.end(),
            [](PackageRole lhs, PackageRole rhs) {
                return package_role_rank(lhs) < package_role_rank(rhs);
            });
}

PlannedPackageTarget* find_package_target(
        BuildPlan& plan, const std::string& package_name) {
    const auto same = [&](const PlannedPackageTarget& target) {
        return target.package_name == package_name;
    };
    const auto found = std::find_if(
            plan.package_targets.begin(), plan.package_targets.end(), same);
    return found == plan.package_targets.end() ? nullptr : &(*found);
}

void aggregate_local_roles(
        BuildPlan& plan,
        const std::vector<LocalDependencyPlanInternalEdge>& internal_edges,
        const std::vector<EffectiveChild>& children) {
    for(const auto& edge : internal_edges) {
        if(!is_local_child(children, edge.resolved_package_name)) continue;
        PlannedPackageTarget* target =
                find_package_target(plan, edge.resolved_package_name);
        if(target != nullptr) add_package_role(target->roles, edge.role);
    }
}

std::optional<std::string> resolved_aur_package_name(
        const BuildPlanDependencyEdge& edge);

bool has_dependency_path(
        const std::string& from, const std::string& target,
        BuildPlan& plan,
        const std::vector<LocalDependencyPlanInternalEdge>& internal_edges,
        std::set<std::string>& visited) {
    if(from == target) return true;
    if(!visited.insert(from).second) return false;
    for(const auto& edge : internal_edges) {
        if(edge.parent_package_name != from) continue;
        if(has_dependency_path(
                   edge.resolved_package_name, target, plan,
                   internal_edges, visited)) {
            return true;
        }
    }
    for(const auto& edge : plan.dependency_edges) {
        if(edge.parent_package_name != from) continue;
        const std::optional<std::string> resolved_name =
                resolved_aur_package_name(edge);
        if(!resolved_name.has_value() ||
           find_package_target(plan, resolved_name.value()) == nullptr) {
            continue;
        }
        if(has_dependency_path(
                   resolved_name.value(), target, plan, internal_edges,
                   visited)) {
            return true;
        }
    }
    return false;
}

void classify_local_cycles(
        BuildPlan& plan,
        std::vector<LocalDependencyPlanInternalEdge>& internal_edges,
        const std::string& package_base) {
    bool has_cycle = false;
    for(auto& edge : internal_edges) {
        if(edge.is_cycle) {
            has_cycle = has_cycle || edge.is_cycle;
            continue;
        }
        std::set<std::string> visited;
        edge.is_cycle = has_dependency_path(
                edge.resolved_package_name, edge.parent_package_name,
                plan, internal_edges, visited);
        has_cycle = has_cycle || edge.is_cycle;
    }
    if(has_cycle &&
       std::find(plan.cycles.begin(), plan.cycles.end(), package_base) ==
               plan.cycles.end()) {
        plan.cycles.push_back(package_base);
    }
}

bool root_identity_less(
        const RootTargetIdentity& lhs, const RootTargetIdentity& rhs) {
    if(lhs.invocation_index != rhs.invocation_index) {
        return lhs.invocation_index < rhs.invocation_index;
    }
    return lhs.requested_name < rhs.requested_name;
}

void add_root_identity(
        std::vector<RootTargetIdentity>& roots,
        const RootTargetIdentity& root) {
    if(std::find(roots.begin(), roots.end(), root) != roots.end()) return;
    roots.push_back(root);
    std::sort(roots.begin(), roots.end(), root_identity_less);
}

std::optional<std::string> resolved_aur_package_name(
        const BuildPlanDependencyEdge& edge) {
    if(edge.kind == DependencyKind::Aur) return edge.resolved_package_name;
    if(edge.kind == DependencyKind::Provided &&
       edge.resolved_provider.has_value() &&
       std::holds_alternative<AurProviderOrigin>(
               edge.resolved_provider->origin)) {
        return edge.resolved_provider->package_name;
    }
    return std::nullopt;
}

void propagate_local_root_identities(
        BuildPlan& plan,
        const std::vector<LocalDependencyPlanInternalEdge>& internal_edges) {
    for(const auto& root : plan.root_targets) {
        std::vector<std::string> pending = {root.requested_name};
        std::set<std::string> visited;
        while(!pending.empty()) {
            std::string package_name = std::move(pending.back());
            pending.pop_back();
            if(!visited.insert(package_name).second) continue;

            PlannedPackageTarget* target =
                    find_package_target(plan, package_name);
            if(target != nullptr) add_root_identity(target->roots, root);

            for(const auto& edge : internal_edges) {
                if(edge.parent_package_name == package_name) {
                    pending.push_back(edge.resolved_package_name);
                }
            }
            for(const auto& edge : plan.dependency_edges) {
                if(edge.parent_package_name != package_name) continue;
                const std::optional<std::string> resolved_name =
                        resolved_aur_package_name(edge);
                if(resolved_name.has_value()) {
                    pending.push_back(resolved_name.value());
                }
            }
        }
    }

    for(auto& failure : plan.resolution_failures) {
        if(!failure.parent_package_name.has_value()) continue;
        PlannedPackageTarget* parent = find_package_target(
                plan, failure.parent_package_name.value());
        if(parent == nullptr) continue;
        for(const auto& root : parent->roots) {
            add_root_identity(failure.roots, root);
        }
    }
}

void add_unsupported_architecture_failures(
        BuildPlan& plan, const std::vector<EffectiveChild>& children,
        const std::string& effective_architecture,
        std::vector<LocalDependencyPlanFailure>& failures) {
    for(const auto& child : children) {
        if(child.architecture_supported) continue;
        add_failure(
                failures,
                LocalDependencyPlanFailure{
                        LocalDependencyPlanFailureKind::UnsupportedArchitecture,
                        child.package_name,
                        std::nullopt,
                        effective_architecture,
                        {}});
        const std::string unresolved =
                child.package_name + " (unsupported architecture: " +
                effective_architecture + ")";
        if(std::find(
                   plan.unresolved.begin(), plan.unresolved.end(),
                   unresolved) == plan.unresolved.end()) {
            plan.unresolved.push_back(unresolved);
        }
    }
}

struct CachedDependencyDecision {
    std::string parent_package_name;
    std::string dependency_specification;
    dependency_plan_projection_support::DependencyDecision decision;
};

std::vector<std::string> unique_dependency_specifications(
        const dependency_plan_projection_support::RootPackage& root) {
    std::vector<std::string> specifications;
    for(const auto& dependency : root.dependencies) {
        if(std::find(
                   specifications.begin(), specifications.end(),
                   dependency.specification) == specifications.end()) {
            specifications.push_back(dependency.specification);
        }
    }
    return specifications;
}

std::vector<TypedPackageDependency> declarations_for_specification(
        const dependency_plan_projection_support::RootPackage& root,
        const std::string& specification) {
    std::vector<TypedPackageDependency> declarations;
    for(const auto& dependency : root.dependencies) {
        if(dependency.specification == specification) {
            declarations.push_back(dependency);
        }
    }
    return declarations;
}

std::vector<CachedDependencyDecision> preclassify_local_dependencies(
        const std::vector<dependency_plan_projection_support::RootPackage>&
                roots,
        const dependency_plan_projection_support::LocalDependencyResolver&
                resolve_local_dependency) {
    std::vector<CachedDependencyDecision> decisions;
    for(std::size_t index = 0; index < roots.size(); ++index) {
        const auto& root = roots[index];
        for(const auto& specification :
            unique_dependency_specifications(root)) {
            const auto decision = resolve_local_dependency(
                    dependency_plan_projection_support::DependencyContext{
                            root.package_name,
                            root.package_base,
                            declarations_for_specification(
                                    root, specification),
                            RootTargetIdentity{index, root.package_name}});
            decisions.push_back(CachedDependencyDecision{
                    root.package_name, specification, decision});
        }
    }
    return decisions;
}

std::optional<dependency_plan_projection_support::DependencyDecision>
find_cached_decision(
        const std::vector<CachedDependencyDecision>& decisions,
        const dependency_plan_projection_support::DependencyContext& context) {
    const std::string& specification =
            context.declarations.front().specification;
    const auto found = std::find_if(
            decisions.begin(), decisions.end(),
            [&](const CachedDependencyDecision& decision) {
                return decision.parent_package_name ==
                               context.parent_package_name &&
                       decision.dependency_specification == specification;
            });
    if(found == decisions.end()) return std::nullopt;
    return found->decision;
}

void append_unique_values(
        std::vector<std::string>& destination,
        const std::vector<std::string>& source) {
    for(const auto& value : source) {
        if(std::find(destination.begin(), destination.end(), value) ==
           destination.end()) {
            destination.push_back(value);
        }
    }
}

void add_remote_identity_conflict_failures(
        const std::vector<dependency_plan_projection_support::
                                  RemoteProviderIdentityConflict>& conflicts,
        std::vector<LocalDependencyPlanFailure>& failures) {
    for(const auto& conflict : conflicts) {
        add_failure(
                failures,
                LocalDependencyPlanFailure{
                        LocalDependencyPlanFailureKind::
                                RemoteProviderIdentityConflict,
                        conflict.parent_package_name,
                        conflict.dependency_specification,
                        std::nullopt,
                        {LocalDependencyPlanCandidate{
                                conflict.provider.package_name,
                                conflict.provider
                                        .provided_dependency_specification,
                                conflict.provider.package_version,
                                conflict.provider}}});
    }
}

} // namespace

LocalBuildPlan::LocalBuildPlan(
        BuildPlan build_plan, LocalPackageMetadata local_metadata,
        std::string effective_architecture,
        std::vector<LocalDependencyPlanInternalEdge> internal_edges,
        std::vector<LocalDependencyPlanFailure> failures) noexcept
    : build_plan_(std::move(build_plan)),
      local_metadata_(std::move(local_metadata)),
      effective_architecture_(std::move(effective_architecture)),
      internal_edges_(std::move(internal_edges)),
      failures_(std::move(failures)) {}

const BuildPlan& LocalBuildPlan::build_plan() const noexcept {
    return build_plan_;
}

const LocalPackageMetadata& LocalBuildPlan::local_metadata() const noexcept {
    return local_metadata_;
}

const std::string& LocalBuildPlan::effective_architecture() const noexcept {
    return effective_architecture_;
}

const std::vector<LocalDependencyPlanInternalEdge>&
LocalBuildPlan::internal_edges() const noexcept {
    return internal_edges_;
}

const std::vector<LocalDependencyPlanFailure>&
LocalBuildPlan::failures() const noexcept {
    return failures_;
}

LocalBuildPlan resolve_local_build_plan(
        const LocalPackageMetadata& metadata,
        const std::string& effective_architecture) {
    return resolve_local_build_plan(
            metadata, effective_architecture,
            ProviderSelectionCallback{});
}

LocalBuildPlan resolve_local_build_plan(
        const LocalPackageMetadata& metadata,
        const std::string& effective_architecture,
        const ProviderSelectionCallback& select_provider) {
    if(effective_architecture.empty()) {
        throw std::invalid_argument(
                "Effective architecture must not be empty.");
    }

    const std::vector<EffectiveChild> children =
            collect_effective_children(metadata, effective_architecture);
    std::vector<LocalDependencyPlanInternalEdge> internal_edges;
    std::vector<LocalDependencyPlanFailure> failures;
    BuildPlan local_preflight_plan;
    add_unsupported_architecture_failures(
            local_preflight_plan, children, effective_architecture,
            failures);

    std::vector<dependency_plan_projection_support::RootPackage> roots =
            collect_root_packages(children, metadata.package_base);
    const std::string package_version = local_package_version(metadata);
    const auto classify_local_dependency = [&](const auto& context) {
        return resolve_local_candidate(
                context, children, metadata.package_base, package_version,
                internal_edges, failures);
    };

    std::vector<CachedDependencyDecision> cached_decisions;
    if(local_preflight_plan.unresolved.empty()) {
        cached_decisions = preclassify_local_dependencies(
                roots, classify_local_dependency);
        classify_local_cycles(
                local_preflight_plan, internal_edges,
                metadata.package_base);
    } else {
        // Required childのarchitectureが1件でも不適合なら、remote query前に
        // root shapeだけを確定してincomplete planとして停止する。
        for(auto& root : roots) root.dependencies.clear();
    }

    const auto local_resolver = [&](const auto& context) {
        const auto cached = find_cached_decision(cached_decisions, context);
        if(cached.has_value()) return cached.value();
        return classify_local_dependency(context);
    };

    dependency_plan_projection_support::ResolutionResult resolution =
            dependency_plan_projection_support::resolve(
                    roots, {metadata.package_base}, local_resolver,
                    select_provider);
    BuildPlan& plan = resolution.plan;
    append_unique_values(plan.unresolved, local_preflight_plan.unresolved);
    append_unique_values(plan.cycles, local_preflight_plan.cycles);
    add_remote_identity_conflict_failures(
            resolution.identity_conflicts, failures);
    aggregate_local_roles(plan, internal_edges, children);
    classify_local_cycles(
            plan, internal_edges, metadata.package_base);
    propagate_local_root_identities(plan, internal_edges);

    return LocalBuildPlan(
            std::move(plan), metadata, effective_architecture,
            std::move(internal_edges), std::move(failures));
}
