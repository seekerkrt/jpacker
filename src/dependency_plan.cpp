#include "dependency_plan.hpp"

#include "dependency_plan_projection_support.hpp"
#include "dependency_provider.hpp"
#include "dependency_spec.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "repository_query.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

AurPackageInfo require_typed_aur_package_info(AurPackageInfo package) {
    if(package.constraint_metadata.has_value()) return package;
    AurConstraintMetadataProjectionResult projection =
            project_aur_constraint_metadata(package);
    if(const auto* metadata =
               std::get_if<AurPackageConstraintMetadata>(&projection);
       metadata != nullptr) {
        package.constraint_metadata = *metadata;
        return package;
    }
    throw std::runtime_error(localization::format_translated_message(
            "{} package metadata constraint projection failed: {}",
            "AUR", package.Name));
}

std::optional<AurPackageInfo> query_aur_package_info(
        const std::string& package_name, BuildPlanResolutionMode mode,
        bool require_authoritative_metadata = false) {
    std::optional<AurPackageInfo> package =
            mode == BuildPlanResolutionMode::CaptureOrdinaryFailures ||
                    require_authoritative_metadata
            ? AurClient::info_strict(package_name)
            : AurClient::info(package_name);
    if(package.has_value()) {
        package = require_typed_aur_package_info(std::move(package.value()));
    }
    return package;
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

bool matches_selected_aur_provider_contract(
        const AurPackageInfo& info,
        const ProvidedDependency& selected_provider) {
    if(!std::holds_alternative<AurProviderOrigin>(
               selected_provider.origin) ||
       info.Name != selected_provider.package_name ||
       info.PackageBase != selected_provider.package_base) {
        return false;
    }
    if(!info.constraint_metadata.has_value()) return false;
    return std::any_of(
            info.constraint_metadata->provides.begin(),
            info.constraint_metadata->provides.end(),
            [&selected_provider](
                    const AurProviderCapabilityMetadata& provided) {
                return provided.capability.package_name() ==
                        selected_provider.provided_dependency_name;
            });
}

std::string selected_aur_provider_revalidation_failure_diagnostic(
        const ProvidedDependency& selected_provider) {
    return localization::format_translated_message(
            "{} provider candidate changed during dependency resolution: {}",
            "AUR", selected_provider.package_name);
}

std::string selected_provider_package_identity_conflict_diagnostic(
        const ProvidedDependency& existing,
        const ProvidedDependency& selected) {
    return localization::format_translated_message(
            "Selected providers use incompatible identities for package {}: {} and {}.",
            selected.package_name,
            provider_package_identity_display(existing),
            provider_package_identity_display(selected));
}

void add_provider_candidate(std::vector<ProvidedDependency>& candidates, const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider_identity(existing, provider);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) != candidates.end()) return;
    candidates.push_back(provider);
}

std::optional<ProvidedDependency> aur_provider_from_metadata(
        const AurPackageInfo& info, const std::string& dependency_name) {
    if(!info.constraint_metadata.has_value()) return std::nullopt;
    const AurPackageConstraintMetadata& metadata =
            info.constraint_metadata.value();
    const auto provided = std::find_if(
            metadata.provides.begin(), metadata.provides.end(),
            [&dependency_name](
                    const AurProviderCapabilityMetadata& candidate) {
                return candidate.capability.package_name() == dependency_name;
            });
    if(provided == metadata.provides.end()) return std::nullopt;
    return ProvidedDependency::from_aur_constraint_metadata(
            metadata.package_name, metadata.package_base,
            ProviderConstraintMetadata{
                    provided->capability, metadata.package_version,
                    provided->provided_version});
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
        BuildPlanResolutionFailureContext* failure_context,
        bool require_authoritative_repository_metadata = false,
        std::optional<RepositoryPackagePresent>* present_package = nullptr) {
    if(mode == BuildPlanResolutionMode::Legacy &&
       !require_authoritative_repository_metadata) {
        if(is_repo_package(package_name)) {
            if(present_package != nullptr) {
                *present_package = RepositoryPackagePresent{
                        {}, 0, package_name, std::nullopt};
            }
            return RepositoryPackageQueryStatus::Present;
        }
        return RepositoryPackageQueryStatus::NotFound;
    }

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict(package_name);
    if(const auto* present =
               std::get_if<RepositoryPackagePresent>(&result);
       present != nullptr) {
        if(present_package != nullptr) *present_package = *present;
        return RepositoryPackageQueryStatus::Present;
    }
    if(std::holds_alternative<RepositoryPackageNotFound>(result))
        return RepositoryPackageQueryStatus::NotFound;

    const RepositoryMetadataFailure& failure =
            std::get<RepositoryMetadataFailure>(result);
    if(failure_context == nullptr) {
        throw std::runtime_error(failure.diagnostic);
    }
    add_resolution_failure(
            failure_context,
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            package_name, failure.diagnostic);
    return RepositoryPackageQueryStatus::Unavailable;
}

ConstraintEvaluation evaluate_dependency_requirement(
        const DependencyRequirement& requirement,
        const ObservedVersion& observed_version) {
    if(const auto* consumer =
               std::get_if<ConsumerDependencyRequirement>(&requirement);
       consumer != nullptr) {
        return evaluate_consumer_dependency_requirement(
                *consumer, observed_version);
    }
    if(std::holds_alternative<SonameDependencyRequirement>(requirement)) {
        return ConstraintEvaluation::unknown(
                ObservedVersionUnknownReason::RelationKindNotComparable);
    }
    return ConstraintEvaluation::invalid(
            ConstraintInvalidReason::InternalInvariantViolation);
}

RepositoryExactPackage repository_candidate(
        const RepositoryPackagePresent& present,
        const std::string& package_name) {
    const ObservedVersion observed_version =
            present.package_version.value_or(ObservedVersion::unknown(
                    ObservedVersionSource::RepositoryExactPackage,
                    ObservedVersionUnknownReason::MetadataQueryFailure));
    return RepositoryExactPackage{
            ConfiguredRepositoryIdentity{
                    present.repository_name, present.configured_order},
            present.package_name.empty() ? package_name
                                         : present.package_name,
            observed_version,
            {}};
}

AurResolvedDependencyCandidate aur_candidate(
        const AurPackageInfo& package) {
    if(!package.constraint_metadata.has_value()) {
        throw std::logic_error(localization::format_translated_message(
                "{} package metadata is missing its typed constraint projection: {}",
                "AUR", package.Name));
    }
    const AurPackageConstraintMetadata& metadata =
            package.constraint_metadata.value();
    if(metadata.package_name != package.Name ||
       metadata.package_base != package.PackageBase) {
        throw std::logic_error(localization::format_translated_message(
                "{} package metadata constraint identity is inconsistent: {}",
                "AUR", package.Name));
    }
    return AurResolvedDependencyCandidate{
            metadata.package_name, metadata.package_base,
            metadata.package_version};
}

ConstraintEvaluation evaluate_provider_requirement(
        const DependencyRequirement& requirement,
        const ProvidedDependency& provider) {
    const auto* consumer =
            std::get_if<ConsumerDependencyRequirement>(&requirement);
    if(consumer == nullptr) {
        return ConstraintEvaluation::unknown(
                ObservedVersionUnknownReason::RelationKindNotComparable);
    }
    if(!provider.constraint_metadata.has_value()) {
        return ConstraintEvaluation::unknown(
                ObservedVersionUnknownReason::CandidateVersionUnavailable);
    }
    const ProviderConstraintMetadata& metadata =
            provider.constraint_metadata.value();
    if(metadata.provided_capability.package_name() !=
       consumer->package_name()) {
        return ConstraintEvaluation::invalid(
                ConstraintInvalidReason::InternalInvariantViolation);
    }
    return evaluate_consumer_dependency_requirement(
            *consumer, metadata.provided_version);
}

ObservedVersion provider_observed_version(
        const ProvidedDependency& provider) {
    if(provider.constraint_metadata.has_value()) {
        return provider.constraint_metadata->provided_version;
    }
    const ObservedVersionSource source =
            std::holds_alternative<RepositoryProviderOrigin>(provider.origin)
            ? ObservedVersionSource::RepositoryProviderCapability
            : ObservedVersionSource::AurProviderCapability;
    return ObservedVersion::unknown(
            source,
            ObservedVersionUnknownReason::CandidateVersionUnavailable);
}

std::vector<ProvidedDependency> find_aur_providers(
        const std::string& dependency_name,
        BuildPlanResolutionFailureContext* failure_context = nullptr,
        bool require_complete_candidates = false) {
    std::vector<ProvidedDependency> providers;
    if(!is_valid_package_name(dependency_name)) return providers;

    const bool use_strict_query =
            failure_context != nullptr || require_complete_candidates;
    bool has_incomplete_candidate_metadata = false;
    std::vector<std::string> candidates;
    try {
        if(use_strict_query) {
            candidates = AurClient::search_names_by_provides_strict(dependency_name);
        } else {
            candidates = AurClient::search_names_by_provides(dependency_name);
        }
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        if(failure_context == nullptr && require_complete_candidates) throw;
        add_resolution_failure(
                failure_context,
                BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                dependency_name, e.what());
        Logger::warn(localization::format_translated_message(
                "Failed to search {} providers for {}: {}",
                "AUR", dependency_name, e.what()));
        return providers;
    }
    for(const auto& candidate : candidates) {
        try {
            std::optional<AurPackageInfo> info = use_strict_query
                    ? AurClient::info_strict(candidate)
                    : AurClient::info(candidate);
            if(info.has_value()) {
                info = require_typed_aur_package_info(
                        std::move(info.value()));
            }
            std::optional<ProvidedDependency> provider;
            if(info.has_value()) {
                provider = aur_provider_from_metadata(
                        info.value(), dependency_name);
            }
            if(info.has_value() && provider.has_value()) {
                add_provider_candidate(providers, provider.value());
            } else if(failure_context != nullptr ||
                      require_complete_candidates) {
                has_incomplete_candidate_metadata = true;
                std::string diagnostic =
                        localization::format_translated_message(
                                "{} provider candidate metadata was not returned.",
                                "AUR");
                if(failure_context == nullptr && require_complete_candidates) {
                    throw std::runtime_error(diagnostic);
                }
                add_resolution_failure(
                        failure_context,
                        BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                        candidate, diagnostic);
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            if(failure_context == nullptr && require_complete_candidates) throw;
            has_incomplete_candidate_metadata = true;
            add_resolution_failure(
                    failure_context,
                    BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                    candidate, e.what());
            Logger::warn(localization::format_translated_message(
                    "Failed to check {} provider {}: {}",
                    "AUR", candidate, e.what()));
        }
    }

    if(failure_context != nullptr &&
       has_incomplete_candidate_metadata) {
        providers.clear();
    }

    return providers;
}

std::vector<ProvidedDependency> find_dependency_providers(
        const std::string& dependency_name,
        BuildPlanResolutionFailureContext* failure_context = nullptr,
        bool require_authoritative_candidates = false) {
    std::vector<ProvidedDependency> repo_provider;
    if(failure_context == nullptr &&
       !require_authoritative_candidates) {
        repo_provider = find_repo_providers(dependency_name);
    } else {
        StrictRepositoryProvidersQueryResult result =
                query_repository_providers_strict(dependency_name);
        if(const auto* failure = std::get_if<RepositoryMetadataFailure>(&result);
           failure != nullptr) {
            if(failure_context == nullptr) {
                throw std::runtime_error(failure->diagnostic);
            }
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
    return find_aur_providers(
            dependency_name, failure_context,
            require_authoritative_candidates);
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
        const std::string& dependency, PackageRole role,
        const DependencyRequirement& requirement) {
    if(trim(dependency).empty()) return;

    auto same_dependency = [&dependency, role](const TypedPackageDependency& existing) {
        return existing.specification == dependency && existing.role == role;
    };
    if(std::find_if(dependencies.begin(), dependencies.end(), same_dependency) != dependencies.end()) return;
    dependencies.push_back(
            TypedPackageDependency{dependency, role, requirement});
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
    if(has_distinct_package_base(info)) {
        // NO_TRANSLATE(Issue #308): "base" is a stable BuildPlan relationship
        // token joining package identities, not human-readable prose.
        display += " (base: " + info.PackageBase + ")";
    }
    dependencies.push_back(dependency_display_with_constraint_note(display, dependency));
}

std::string provided_dependency_resolution_display(
        const std::string& dependency, const ProvidedDependency& provider) {
    return dependency_display_with_constraint_note(
            localization::format_translated_message(
                    // TRANSLATORS: The placeholders are dependency and provider identities.
                    "{} [provided by {}]", dependency,
                    provided_dependency_display(provider)),
            dependency);
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

std::optional<ProvidedDependency> select_provider_candidate(
        const std::string& dependency,
        const std::vector<ProvidedDependency>& candidates,
        const ProviderSelectionCallback& select_provider) {
    if(candidates.empty() || !select_provider) return std::nullopt;

    std::optional<ProvidedDependency> selected =
            select_provider(dependency, candidates);
    if(!selected.has_value()) return std::nullopt;

    auto matching_candidate = std::find_if(
            candidates.begin(), candidates.end(),
            [&selected](const ProvidedDependency& candidate) {
                return same_provider_identity(candidate, selected.value());
            });
    if(matching_candidate == candidates.end()) {
        throw std::logic_error(localization::format_translated_message(
                "Provider selection returned a candidate that was not offered for {}.",
                dependency));
    }
    // Resolver-owned metadata is authoritative even when an injected selector
    // returns an identity-only value.
    return *matching_candidate;
}

void warn_unverified_version_constraint(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(!parsed.has_parseable_constraint()) return;
    Logger::warn(localization::format_translated_message(
            // TRANSLATORS: The placeholder is a dependency specification.
            "version constraint for {} is not verified", parsed.raw));
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
    std::optional<AurPackageConstraintMetadata> projected_metadata;
    if(!pkg.constraint_metadata.has_value()) {
        AurConstraintMetadataProjectionResult projection =
                project_aur_constraint_metadata(pkg);
        if(const auto* metadata =
                   std::get_if<AurPackageConstraintMetadata>(&projection);
           metadata != nullptr) {
            projected_metadata = *metadata;
        } else {
            throw std::logic_error(localization::format_translated_message(
                    "{} package metadata constraint projection failed: {}",
                    "AUR", pkg.Name));
        }
    }

    std::vector<TypedPackageDependency> dependencies;
    const AurPackageConstraintMetadata& metadata =
            pkg.constraint_metadata.has_value()
            ? pkg.constraint_metadata.value()
            : projected_metadata.value();
    if(pkg.Depends.size() != metadata.depends.size() ||
       pkg.MakeDepends.size() != metadata.make_depends.size() ||
       pkg.CheckDepends.size() != metadata.check_depends.size()) {
        throw std::logic_error(localization::format_translated_message(
                "{} package metadata constraint projection is inconsistent: {}",
                "AUR", pkg.Name));
    }

    for(std::size_t index = 0; index < pkg.Depends.size(); ++index) {
        add_typed_dependency(
                dependencies, pkg.Depends[index],
                PackageRole::RuntimeDependency, metadata.depends[index]);
    }
    for(std::size_t index = 0; index < pkg.MakeDepends.size(); ++index) {
        add_typed_dependency(
                dependencies, pkg.MakeDepends[index],
                PackageRole::BuildDependency,
                metadata.make_depends[index]);
    }
    for(std::size_t index = 0; index < pkg.CheckDepends.size(); ++index) {
        add_typed_dependency(
                dependencies, pkg.CheckDepends[index],
                PackageRole::CheckDependency,
                metadata.check_depends[index]);
    }

    return dependencies;
}

DependencyClassification classify_dependencies(
        const std::vector<std::string>& dependencies,
        const ProviderSelectionCallback& select_provider) {
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

        if(query_repository_package(
                   package_name, BuildPlanResolutionMode::Legacy, nullptr,
                   static_cast<bool>(select_provider)) ==
           RepositoryPackageQueryStatus::Present) {
            add_classified_dependency(result.repo, dependency, package_name);
            continue;
        }

        try {
            std::optional<AurPackageInfo> info = select_provider
                    ? AurClient::info_strict(package_name)
                    : AurClient::info(package_name);
            if(info.has_value()) {
                add_classified_aur_dependency(result.aur, dependency, info.value());
            } else {
                std::vector<ProvidedDependency> providers =
                        find_dependency_providers(
                                package_name, nullptr,
                                static_cast<bool>(select_provider));
                std::optional<ProvidedDependency> selected =
                        select_provider_candidate(
                                dependency, providers, select_provider);
                if(selected.has_value()) {
                    result.selected_providers.push_back(
                            SelectedProvidedDependency{
                                    dependency, selected.value()});
                } else if(providers.size() == 1) {
                    result.provided.push_back(
                            provided_dependency_resolution_display(
                                    dependency, providers.front()));
                } else if(providers.size() > 1) {
                    add_ambiguous_provider_dependency(
                            result.ambiguous_providers, dependency,
                            providers);
                }
                else
                    result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception& e) {
            if(select_provider) throw;
            Logger::warn(localization::format_translated_message(
                    "Failed to check {} dependency {}: {}",
                    "AUR", package_name, e.what()));
            std::vector<ProvidedDependency> providers =
                    find_repo_providers(package_name);
            std::optional<ProvidedDependency> selected =
                    select_provider_candidate(
                            dependency, providers, select_provider);
            if(selected.has_value()) {
                result.selected_providers.push_back(
                        SelectedProvidedDependency{
                                dependency, selected.value()});
            } else if(providers.size() == 1) {
                result.provided.push_back(
                        provided_dependency_resolution_display(
                                dependency, providers.front()));
            } else if(providers.size() > 1) {
                add_ambiguous_provider_dependency(
                        result.ambiguous_providers, dependency,
                        providers);
            }
            else
                result.unknown.push_back(dependency_display_with_constraint_note(dependency, dependency));
        }
    }

    return result;
}

DependencyClassification classify_dependencies(
        const std::vector<std::string>& dependencies) {
    return classify_dependencies(dependencies, ProviderSelectionCallback{});
}

namespace {

RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited,
        int depth, int max_depth,
        const ProviderSelectionCallback& select_provider);

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg, std::set<std::string>& visited,
        int depth, int max_depth,
        const ProviderSelectionCallback& select_provider) {
    std::vector<RecursiveDependencyNode> nodes;
    for(const auto& dependency : collect_build_dependencies(pkg)) {
        nodes.push_back(resolve_recursive_dependency(
                dependency, visited, depth, max_depth, select_provider));
    }
    return nodes;
}

void populate_recursive_aur_provider_children(
        RecursiveDependencyNode& node, const AurPackageInfo& info,
        std::set<std::string>& visited, int depth, int max_depth,
        const ProviderSelectionCallback& select_provider) {
    node.package_base = package_base_name(info);
    if(!visited.insert(node.package_base).second) {
        node.already_visited = true;
        return;
    }
    if(depth >= max_depth) {
        node.max_depth_reached = true;
        return;
    }
    node.children = resolve_recursive_dependencies(
            info, visited, depth + 1, max_depth, select_provider);
}

RecursiveDependencyNode resolve_recursive_dependency(
        const std::string& dependency, std::set<std::string>& visited,
        int depth, int max_depth,
        const ProviderSelectionCallback& select_provider) {
    RecursiveDependencyNode node;
    node.dependency = dependency;
    ParsedDependency parsed = parse_dependency_string(dependency);
    node.package_name = parsed.name;

    if(!is_valid_package_name(node.package_name) || parsed.has_malformed_constraint()) {
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(query_repository_package(
               node.package_name, BuildPlanResolutionMode::Legacy, nullptr,
               static_cast<bool>(select_provider)) ==
       RepositoryPackageQueryStatus::Present) {
        node.kind = DependencyKind::Repo;
        return node;
    }

    std::optional<AurPackageInfo> info;
    try {
        info = select_provider
                ? AurClient::info_strict(node.package_name)
                : AurClient::info(node.package_name);
        if(info.has_value()) {
            info = require_typed_aur_package_info(std::move(info.value()));
        }
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        if(select_provider) throw;
        Logger::warn(localization::format_translated_message(
                "Failed to check {} dependency {}: {}",
                "AUR", node.package_name, e.what()));
        node.kind = DependencyKind::Unknown;
        return node;
    }

    if(!info.has_value()) {
        std::vector<ProvidedDependency> providers =
                find_dependency_providers(
                        node.package_name, nullptr,
                        static_cast<bool>(select_provider));
        std::optional<ProvidedDependency> resolved_provider =
                select_provider_candidate(
                        dependency, providers, select_provider);
        if(resolved_provider.has_value()) {
            node.provider_resolution =
                    ProviderResolutionKind::UserSelected;
        } else if(providers.size() == 1) {
            resolved_provider = providers.front();
        } else if(providers.size() > 1) {
            node.kind = DependencyKind::AmbiguousProvider;
            node.provider_candidates = providers;
        } else {
            node.kind = DependencyKind::Unknown;
        }

        if(!resolved_provider.has_value()) return node;
        node.kind = DependencyKind::Provided;
        node.provided_by = resolved_provider;
        if(std::holds_alternative<RepositoryProviderOrigin>(
                   resolved_provider->origin)) {
            return node;
        }
        if(node.provider_resolution !=
           ProviderResolutionKind::UserSelected) {
            return node;
        }

        std::optional<AurPackageInfo> provider_info =
                AurClient::info_strict(resolved_provider->package_name);
        if(provider_info.has_value()) {
            provider_info = require_typed_aur_package_info(
                    std::move(provider_info.value()));
        }
        if(!provider_info.has_value()) {
            node.kind = DependencyKind::Unknown;
            node.provided_by.reset();
            return node;
        }
        if(!matches_selected_aur_provider_contract(
                   provider_info.value(), resolved_provider.value())) {
            throw std::runtime_error(
                    selected_aur_provider_revalidation_failure_diagnostic(
                            resolved_provider.value()));
        }
        populate_recursive_aur_provider_children(
                node, provider_info.value(), visited, depth, max_depth,
                select_provider);
        return node;
    }

    node.kind = DependencyKind::Aur;
    populate_recursive_aur_provider_children(
            node, info.value(), visited, depth, max_depth,
            select_provider);
    return node;
}

} // namespace

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg,
        const ProviderSelectionCallback& select_provider) {
    std::set<std::string> visited;
    visited.insert(package_base_name(pkg));
    return resolve_recursive_dependencies(
            pkg, visited, 1, MAX_RECURSIVE_DEP_DEPTH, select_provider);
}

std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(
        const AurPackageInfo& pkg) {
    return resolve_recursive_dependencies(pkg, ProviderSelectionCallback{});
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
    throw std::logic_error(
            localization::translate_message("Unknown package role."));
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

const PlannedPackageTarget* find_package_target(
        const BuildPlan& plan, const std::string& package_name) {
    auto same_name = [&package_name](const PlannedPackageTarget& target) {
        return target.package_name == package_name;
    };
    auto it = std::find_if(plan.package_targets.begin(), plan.package_targets.end(), same_name);
    return it == plan.package_targets.end() ? nullptr : &(*it);
}

void add_planned_package_target(
        BuildPlan& plan, const std::string& package_name,
        const std::string& package_base,
        const std::vector<PackageRole>& roles, const RootTargetIdentity& root) {
    PlannedPackageTarget* target = find_package_target(plan, package_name);
    if(target == nullptr) {
        plan.package_targets.push_back(
                PlannedPackageTarget{package_name, package_base, {}, {}});
        target = &plan.package_targets.back();
    }

    for(const auto role : roles) add_package_role(target->roles, role);
    add_root_identity(target->roots, root);
}

void add_planned_package_target(
        BuildPlan& plan, const AurPackageInfo& info,
        const std::vector<PackageRole>& roles, const RootTargetIdentity& root) {
    add_planned_package_target(
            plan, info.Name, package_base_name(info), roles, root);
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
        edge.requirement = dependency.requirement;
        plan.dependency_edges.push_back(std::move(edge));
    }
}

std::optional<std::string> resolved_aur_dependency_name(
        const BuildPlanDependencyEdge& edge) {
    if(edge.kind == DependencyKind::Aur) return edge.resolved_package_name;
    if(edge.kind == DependencyKind::Provided && edge.resolved_provider.has_value() &&
       std::holds_alternative<AurProviderOrigin>(
               edge.resolved_provider->origin)) {
        return edge.resolved_provider->package_name;
    }
    return std::nullopt;
}

void propagate_root_identities(BuildPlan& plan) {
    // WHY(#218/#268): package visitedはmetadata traversalの重複を止めるためinvocation-wideで共有する。
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

void add_build_plan_split_package_target(
        BuildPlan& plan, const std::string& package_name,
        const std::string& package_base) {
    if(package_name == package_base) return;

    auto same_target = [&](const BuildPlanSplitPackageTarget& existing) {
        return existing.package_base == package_base &&
               existing.package_name == package_name;
    };
    if(std::find_if(plan.split_package_targets.begin(), plan.split_package_targets.end(), same_target) !=
       plan.split_package_targets.end())
        return;

    plan.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{package_base, package_name});
}

void add_build_plan_split_package_target(
        BuildPlan& plan, const AurPackageInfo& info) {
    add_build_plan_split_package_target(
            plan, info.Name, package_base_name(info));
}

void add_build_plan_metadata_risk(
        BuildPlan& plan, const std::string& package_name,
        const std::string& package_base,
        const std::vector<std::string>& conflicts,
        const std::vector<std::string>& replaces) {
    if(conflicts.empty() && replaces.empty()) return;

    auto same_package = [&](const BuildPlanMetadataRisk& existing) {
        return existing.package_name == package_name &&
               existing.package_base == package_base;
    };
    if(std::find_if(plan.metadata_risks.begin(), plan.metadata_risks.end(), same_package) !=
       plan.metadata_risks.end())
        return;

    plan.metadata_risks.push_back(
            BuildPlanMetadataRisk{
                    package_name, package_base, conflicts, replaces});
}

void add_build_plan_metadata_risk(BuildPlan& plan, const AurPackageInfo& info) {
    add_build_plan_metadata_risk(
            plan, info.Name, package_base_name(info), info.Conflicts,
            info.Replaces);
}

void add_build_plan_entry(
        BuildPlan& plan, const std::string& package_name,
        const std::string& package_base) {
    auto        same_base = [&package_base](const BuildPlanEntry& existing) { return existing.package_base == package_base; };
    auto        it = std::find_if(plan.order.begin(), plan.order.end(), same_base);
    if(it == plan.order.end()) {
        plan.order.push_back(BuildPlanEntry{package_base, {package_name}});
        return;
    }
    add_unique_value(it->package_names, package_name);
}

void add_build_plan_entry(BuildPlan& plan, const AurPackageInfo& info) {
    add_build_plan_entry(
            plan, info.Name, package_base_name(info));
}

const BuildPlanEntry* find_build_plan_entry(
        const std::vector<BuildPlanEntry>& entries,
        const std::string& package_base) {
    auto same_base = [&package_base](const BuildPlanEntry& entry) {
        return entry.package_base == package_base;
    };
    auto it = std::find_if(entries.begin(), entries.end(), same_base);
    return it == entries.end() ? nullptr : &(*it);
}

std::optional<std::string> resolved_aur_dependency_package_base(
        const BuildPlan& plan, const BuildPlanDependencyEdge& edge) {
    if(edge.kind == DependencyKind::Aur &&
       edge.resolved_package_base.has_value() &&
       !edge.resolved_package_base->empty()) {
        return edge.resolved_package_base;
    }

    std::optional<std::string> package_name =
            resolved_aur_dependency_name(edge);
    if(!package_name.has_value()) return std::nullopt;

    const PlannedPackageTarget* target =
            find_package_target(plan, package_name.value());
    if(target == nullptr) return std::nullopt;
    return target->package_base;
}

void append_build_plan_entry_postorder(
        const std::string& package_base, BuildPlan& plan,
        const std::vector<BuildPlanEntry>& aggregated_entries,
        std::set<std::string>& ordered_package_bases,
        std::set<std::string>& visiting_package_bases,
        std::vector<BuildPlanEntry>& ordered_entries) {
    if(ordered_package_bases.count(package_base) > 0) return;
    if(!visiting_package_bases.insert(package_base).second) {
        add_unique_value(plan.cycles, package_base);
        return;
    }

    for(const auto& edge : plan.dependency_edges) {
        if(edge.parent_package_base != package_base) continue;
        std::optional<std::string> dependency_package_base =
                resolved_aur_dependency_package_base(plan, edge);
        if(!dependency_package_base.has_value() ||
           dependency_package_base.value() == package_base ||
           find_build_plan_entry(
                   aggregated_entries, dependency_package_base.value()) == nullptr) {
            continue;
        }
        append_build_plan_entry_postorder(
                dependency_package_base.value(), plan, aggregated_entries,
                ordered_package_bases, visiting_package_bases, ordered_entries);
    }

    visiting_package_bases.erase(package_base);
    if(!ordered_package_bases.insert(package_base).second) return;

    const BuildPlanEntry* entry =
            find_build_plan_entry(aggregated_entries, package_base);
    if(entry == nullptr) {
        throw std::logic_error(localization::format_translated_message(
                "{} aggregation is missing during build plan ordering: {}",
                "PackageBase", package_base));
    }
    ordered_entries.push_back(*entry);
}

void order_build_plan_entries(BuildPlan& plan) {
    std::vector<BuildPlanEntry> aggregated_entries = std::move(plan.order);
    std::vector<BuildPlanEntry> ordered_entries;
    ordered_entries.reserve(aggregated_entries.size());
    std::set<std::string> ordered_package_bases;
    std::set<std::string> visiting_package_bases;

    // POLICY(#268): first-seen PackageBase/edge orderを保ったpost-orderで、
    // 全required childのdependencyより後に各execution unitを一度だけ置く。
    for(const auto& entry : aggregated_entries) {
        append_build_plan_entry_postorder(
                entry.package_base, plan, aggregated_entries,
                ordered_package_bases, visiting_package_bases, ordered_entries);
    }

    plan.order = std::move(ordered_entries);
}

std::string resolved_candidate_constraint_key(
        const ResolvedDependencyCandidate& candidate) {
    return std::visit(
            [](const auto& value) -> std::string {
                using Candidate = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Candidate,
                                            InstalledExactPackage>) {
                    return "installed/" + value.package_name;
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            RepositoryExactPackage>) {
                    return "repository/" + value.repository.repository_name +
                           "/" + value.package_name;
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            AurResolvedDependencyCandidate>) {
                    return "aur/" + value.package_base + "/" +
                           value.package_name;
                } else if constexpr(std::is_same_v<
                                            Candidate,
                                            LocalResolvedDependencyCandidate>) {
                    return "local/" + value.package_base + "/" +
                           value.package_name;
                } else {
                    return "provider/" +
                           provider_package_identity_display(value.provider) +
                           "/" +
                           value.provider.provided_dependency_name;
                }
            },
            candidate);
}

void project_build_plan_constraint_conflicts(BuildPlan& plan) {
    std::map<std::string, std::vector<std::size_t>> edge_indices;
    for(std::size_t index = 0; index < plan.dependency_edges.size(); ++index) {
        const BuildPlanDependencyEdge& edge = plan.dependency_edges[index];
        if(!edge.requirement.has_value() ||
           !edge.resolved_candidate.has_value() ||
           std::get_if<ConsumerDependencyRequirement>(
                   &edge.requirement.value()) == nullptr) {
            continue;
        }
        const auto& consumer = std::get<ConsumerDependencyRequirement>(
                edge.requirement.value());
        edge_indices[resolved_candidate_constraint_key(
                             edge.resolved_candidate.value()) +
                     "/requirement/" + consumer.package_name()]
                .push_back(index);
    }

    for(const auto& [identity, indices] : edge_indices) {
        static_cast<void>(identity);
        std::vector<ConsumerDependencyRequirement> requirements;
        requirements.reserve(indices.size());
        for(const std::size_t index : indices) {
            requirements.push_back(
                    std::get<ConsumerDependencyRequirement>(
                            plan.dependency_edges[index]
                                    .requirement.value()));
        }
        const std::optional<ConstraintEvaluation> conflict =
                project_conflicting_constraint_invocation(requirements);
        if(!conflict.has_value()) continue;
        for(const std::size_t index : indices) {
            plan.dependency_edges[index].constraint_evaluation =
                    conflict.value();
        }
    }
}

void add_build_plan_provided_dependency(
        BuildPlan& plan, const std::string& dependency,
        const ProvidedDependency& provider,
        ProviderResolutionKind resolution) {
    std::string trimmed = trim(dependency);
    if(trimmed.empty()) return;

    auto same_dependency = [&](const BuildPlanProvidedDependency& existing) {
        return existing.dependency == trimmed &&
               same_provider_identity(existing.provider, provider);
    };
    auto existing = std::find_if(
            plan.provided.begin(), plan.provided.end(), same_dependency);
    if(existing != plan.provided.end()) {
        if(existing->resolution != resolution) {
            existing->resolution = ProviderResolutionKind::UserSelected;
        }
        return;
    }
    plan.provided.push_back(
            BuildPlanProvidedDependency{trimmed, provider, resolution});
}

void add_build_plan_ambiguous_provider(
        BuildPlan& plan, const std::string& dependency,
        const std::vector<ProvidedDependency>& candidates) {
    add_ambiguous_provider_dependency(plan.ambiguous_providers, dependency, candidates);
}

void collect_aur_build_plan(
        const std::string& package_name, BuildPlan& plan,
        std::set<std::string>& visited_package_names,
        std::set<std::string>& visiting_package_names,
        const std::vector<PackageRole>& roles,
        const RootTargetIdentity& root, int depth, int max_depth,
        bool traverse_aur_providers, BuildPlanResolutionMode resolution_mode,
        const ProviderSelectionCallback& select_provider,
        const std::set<std::string>& local_package_bases,
        const std::set<std::string>& local_package_names,
        std::vector<dependency_plan_projection_support::
                            RemoteProviderIdentityConflict>*
                identity_conflicts,
        const dependency_plan_projection_support::LocalDependencyResolver&
                resolve_local_dependency,
        const std::optional<std::string>& parent_package_name,
        const std::optional<std::string>& parent_package_base,
        const std::optional<std::string>& dependency_specification,
        const std::optional<ProvidedDependency>& expected_selected_provider,
        std::optional<AurPackageInfo> preloaded_package_info);

void resolve_build_plan_dependency(
        const std::string& parent_package_name,
        const std::string& parent_package_base,
        const std::string& dependency,
        const std::vector<TypedPackageDependency>& matching_dependencies,
        BuildPlan& plan,
        std::set<std::string>& visited_package_names,
        std::set<std::string>& visiting_package_names,
        const RootTargetIdentity& root, int depth, int max_depth,
        bool traverse_aur_providers, BuildPlanResolutionMode resolution_mode,
        const ProviderSelectionCallback& select_provider,
        const std::set<std::string>& local_package_bases,
        const std::set<std::string>& local_package_names,
        std::vector<dependency_plan_projection_support::
                            RemoteProviderIdentityConflict>*
                identity_conflicts,
        const dependency_plan_projection_support::LocalDependencyResolver&
                resolve_local_dependency) {
    if(matching_dependencies.empty()) {
        throw std::logic_error(localization::format_translated_message(
                "Build dependency is missing its package role: {}",
                dependency));
    }

    if(resolve_local_dependency) {
        const dependency_plan_projection_support::DependencyDecision decision =
                resolve_local_dependency(
                        dependency_plan_projection_support::DependencyContext{
                                parent_package_name,
                                parent_package_base,
                                matching_dependencies,
                                root});
        if(decision.handled_locally) {
            if(decision.cycle_package_base.has_value()) {
                add_unique_value(
                        plan.cycles, decision.cycle_package_base.value());
            }
            if(decision.unresolved_reason.has_value()) {
                add_unique_value(
                        plan.unresolved, decision.unresolved_reason.value());
            }
            return;
        }
    }

    const std::vector<PackageRole> dependency_roles =
            package_roles_for_dependencies(matching_dependencies);
    BuildPlanDependencyEdge edge{
            parent_package_name,
            parent_package_base,
            matching_dependencies.front().specification,
            matching_dependencies.front().role,
            DependencyKind::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt};

    if(!matching_dependencies.front().requirement.has_value()) {
        edge.constraint_evaluation = ConstraintEvaluation::invalid(
                ConstraintInvalidReason::MalformedRequirement);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        require_constructible_build_plan_constraints(plan);
        return;
    }
    const DependencyRequirement& requirement =
            matching_dependencies.front().requirement.value();
    const auto* consumer =
            std::get_if<ConsumerDependencyRequirement>(&requirement);
    if(consumer == nullptr) {
        edge.constraint_evaluation = ConstraintEvaluation::unknown(
                ObservedVersionUnknownReason::RelationKindNotComparable);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        return;
    }
    const std::string& dep_name = consumer->package_name();

    BuildPlanResolutionFailureContext dependency_failure_context{
            plan, root, parent_package_name, parent_package_base, dependency};
    BuildPlanResolutionFailureContext* dependency_failure_sink =
            resolution_mode == BuildPlanResolutionMode::CaptureOrdinaryFailures
            ? &dependency_failure_context
            : nullptr;
    std::optional<RepositoryPackagePresent> present_repository_package;
    const RepositoryPackageQueryStatus repository_status =
            query_repository_package(
                    dep_name, resolution_mode, dependency_failure_sink,
                    true,
                    &present_repository_package);
    if(repository_status == RepositoryPackageQueryStatus::Present) {
        if(!present_repository_package.has_value()) {
            edge.constraint_evaluation = ConstraintEvaluation::invalid(
                    ConstraintInvalidReason::InternalInvariantViolation);
            add_build_plan_dependency_edges(plan, edge, matching_dependencies);
            require_constructible_build_plan_constraints(plan);
            return;
        }
        RepositoryExactPackage candidate = repository_candidate(
                present_repository_package.value(), dep_name);
        edge.kind = DependencyKind::Repo;
        edge.resolved_package_name = candidate.package_name;
        edge.resolved_candidate = candidate;
        edge.constraint_evaluation = evaluate_dependency_requirement(
                requirement, candidate.package_version);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        return;
    }
    if(repository_status == RepositoryPackageQueryStatus::Unavailable) {
        add_unique_value(plan.unresolved, dependency);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        return;
    }

    std::optional<AurPackageInfo> dependency_info;
    bool dependency_metadata_unavailable = false;
    try {
        dependency_info = query_aur_package_info(
                dep_name, resolution_mode,
                static_cast<bool>(select_provider));
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& error) {
        if(resolution_mode == BuildPlanResolutionMode::Legacy &&
           select_provider) {
            throw;
        }
        add_resolution_failure(
                dependency_failure_sink,
                BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
                dep_name, error.what());
        dependency_metadata_unavailable = dependency_failure_sink != nullptr;
        Logger::warn(localization::format_translated_message(
                "Failed to check {} dependency {}: {}",
                "AUR", dep_name, error.what()));
    }

    if(dependency_metadata_unavailable) {
        add_unique_value(plan.unresolved, dependency);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        return;
    }

    if(dependency_info.has_value()) {
        AurResolvedDependencyCandidate candidate =
                aur_candidate(dependency_info.value());
        edge.kind = DependencyKind::Aur;
        edge.resolved_package_name = candidate.package_name;
        edge.resolved_package_base = candidate.package_base;
        edge.resolved_candidate = candidate;
        edge.constraint_evaluation = evaluate_dependency_requirement(
                requirement, candidate.package_version);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        if(local_package_bases.count(edge.resolved_package_base.value()) > 0) {
            add_unique_value(plan.cycles, edge.resolved_package_base.value());
            return;
        }
        collect_aur_build_plan(
                dep_name, plan, visited_package_names,
                visiting_package_names, dependency_roles, root,
                depth + 1, max_depth, traverse_aur_providers,
                resolution_mode, select_provider, local_package_bases,
                local_package_names, identity_conflicts,
                resolve_local_dependency, parent_package_name,
                parent_package_base, dependency, std::nullopt,
                std::move(dependency_info));
        return;
    }

    std::vector<ProvidedDependency> providers =
            find_dependency_providers(
                    dep_name, dependency_failure_sink,
                    true);
    for(const auto& provider : providers) {
        const ConstraintEvaluation evaluation =
                evaluate_provider_requirement(requirement, provider);
        const ConstraintSatisfaction satisfaction =
                evaluation.satisfaction();
        if(satisfaction == ConstraintSatisfaction::Invalid ||
           satisfaction == ConstraintSatisfaction::Conflicting) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Cannot select a provider for {}; candidate {} is {}: {}.",
                            dependency,
                            provider_package_identity_display(provider),
                            constraint_satisfaction_display(satisfaction),
                            constraint_evaluation_reason_display(evaluation)));
        }
        if(satisfaction == ConstraintSatisfaction::Unsatisfied ||
           satisfaction == ConstraintSatisfaction::Unknown) {
            Logger::warn(localization::format_translated_message(
                    "Provider candidate {} for {} is {}: {}. Candidate order and explicit selection are unchanged.",
                    provider_package_identity_display(provider), dependency,
                    constraint_satisfaction_display(satisfaction),
                    constraint_evaluation_reason_display(evaluation)));
        }
    }
    std::optional<ProvidedDependency> resolved_provider =
            select_provider_candidate(
                    dependency, providers, select_provider);
    const ProviderResolutionKind provider_resolution =
            resolved_provider.has_value()
            ? ProviderResolutionKind::UserSelected
            : ProviderResolutionKind::Unique;
    if(!resolved_provider.has_value() && providers.size() == 1) {
        resolved_provider = providers.front();
    }

    if(resolved_provider.has_value()) {
        ProvidedDependency provider = resolved_provider.value();
        ConstraintEvaluation provider_evaluation =
                evaluate_provider_requirement(requirement, provider);
        std::optional<AurPackageInfo> refreshed_provider_info;
        if(std::holds_alternative<AurProviderOrigin>(provider.origin)) {
            const auto fail_provider_refresh = [&]() {
                if(dependency_failure_sink != nullptr) return;
                throw std::runtime_error(
                        selected_aur_provider_revalidation_failure_diagnostic(
                                provider));
            };
            std::optional<AurPackageInfo> current_info =
                    AurClient::info_strict(provider.package_name);
            if(current_info.has_value()) {
                current_info = require_typed_aur_package_info(
                        std::move(current_info.value()));
            }
            if(!current_info.has_value() ||
               !current_info->constraint_metadata.has_value()) {
                fail_provider_refresh();
            } else {
                AurProviderDependencyProjection selected_projection{
                        *consumer, provider, provider_evaluation};
                AurProviderDependencyProjectionResult refreshed =
                        refresh_aur_provider_dependency(
                                selected_projection,
                                current_info->constraint_metadata.value());
                if(const auto* projection =
                           std::get_if<AurProviderDependencyProjection>(
                                   &refreshed);
                   projection != nullptr) {
                    provider = projection->provider;
                    provider_evaluation = projection->evaluation;
                    refreshed_provider_info = std::move(current_info);
                } else if(const auto* unknown =
                                  std::get_if<AurProviderDependencyUnknown>(
                                          &refreshed);
                          unknown != nullptr) {
                    provider_evaluation =
                            ConstraintEvaluation::unknown(unknown->reason);
                    refreshed_provider_info = std::move(current_info);
                } else {
                    fail_provider_refresh();
                }
            }
        }
        const bool returns_local_package_base =
                std::holds_alternative<AurProviderOrigin>(provider.origin) &&
                local_package_bases.count(provider.package_base) > 0;
        if(local_package_names.count(provider.package_name) > 0 &&
           !returns_local_package_base) {
            if(identity_conflicts != nullptr) {
                identity_conflicts->push_back(
                        dependency_plan_projection_support::
                                RemoteProviderIdentityConflict{
                                        parent_package_name,
                                        dependency,
                                        provider});
            }
            add_unique_value(
                    plan.unresolved,
                    dependency +
                            " (remote provider conflicts with local package identity)");
            add_build_plan_dependency_edges(
                    plan, edge, matching_dependencies);
            return;
        }
        edge.kind = DependencyKind::Provided;
        edge.resolved_provider = provider;
        edge.provider_resolution = provider_resolution;
        edge.resolved_candidate = ProviderResolvedDependencyCandidate{
                provider, provider_observed_version(provider)};
        edge.constraint_evaluation = provider_evaluation;
        add_build_plan_provided_dependency(
                plan, dependency, provider, provider_resolution);
        add_build_plan_dependency_edges(plan, edge, matching_dependencies);
        if((traverse_aur_providers ||
           provider_resolution == ProviderResolutionKind::UserSelected) &&
           std::holds_alternative<AurProviderOrigin>(provider.origin)) {
            if(returns_local_package_base) {
                add_unique_value(plan.cycles, provider.package_base);
                return;
            }
            std::optional<ProvidedDependency> provider_revalidation_contract;
            if(provider_resolution == ProviderResolutionKind::UserSelected) {
                provider_revalidation_contract = provider;
            }
            collect_aur_build_plan(
                    provider.package_name, plan, visited_package_names,
                    visiting_package_names, dependency_roles, root,
                    depth + 1, max_depth, traverse_aur_providers,
                    resolution_mode, select_provider, local_package_bases,
                    local_package_names, identity_conflicts,
                    resolve_local_dependency, parent_package_name,
                    parent_package_base, dependency,
                    provider_revalidation_contract,
                    std::move(refreshed_provider_info));
        }
        return;
    }

    if(providers.size() > 1) {
        edge.kind = DependencyKind::AmbiguousProvider;
        add_build_plan_ambiguous_provider(plan, dependency, providers);
    } else {
        InstalledExactPackageObservationResult installed_result =
                query_installed_exact_package_strict(dep_name);
        if(const auto* installed =
                   std::get_if<InstalledExactPackage>(&installed_result);
           installed != nullptr) {
            edge.kind = DependencyKind::Installed;
            edge.resolved_package_name = installed->package_name;
            edge.resolved_candidate = *installed;
            edge.constraint_evaluation = evaluate_dependency_requirement(
                    requirement, installed->observed_version);
        } else {
            add_unique_value(plan.unresolved, dependency);
        }
    }
    add_build_plan_dependency_edges(plan, edge, matching_dependencies);
}

void collect_aur_build_plan(
        const std::string& package_name, BuildPlan& plan,
        std::set<std::string>& visited_package_names,
        std::set<std::string>& visiting_package_names,
        const std::vector<PackageRole>& roles,
        const RootTargetIdentity& root, int depth, int max_depth,
        bool traverse_aur_providers, BuildPlanResolutionMode resolution_mode,
        const ProviderSelectionCallback& select_provider,
        const std::set<std::string>& local_package_bases,
        const std::set<std::string>& local_package_names,
        std::vector<dependency_plan_projection_support::
                            RemoteProviderIdentityConflict>*
                identity_conflicts,
        const dependency_plan_projection_support::LocalDependencyResolver&
                resolve_local_dependency,
        const std::optional<std::string>& parent_package_name,
        const std::optional<std::string>& parent_package_base,
        const std::optional<std::string>& dependency_specification,
        const std::optional<ProvidedDependency>& expected_selected_provider,
        std::optional<AurPackageInfo> preloaded_package_info) {
    if(depth > max_depth) {
        add_unique_value(
                plan.unresolved,
                localization::format_translated_message(
                        "{} (max depth reached)", package_name));
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
        if(preloaded_package_info.has_value()) {
            info = std::move(preloaded_package_info);
        } else {
            info = query_aur_package_info(
                    package_name, resolution_mode,
                    static_cast<bool>(select_provider));
        }
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        if(resolution_mode == BuildPlanResolutionMode::Legacy &&
           select_provider) {
            throw;
        }
        add_resolution_failure(
                failure_context,
                BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
                package_name, e.what());
        Logger::warn(localization::format_translated_message(
                "Failed to fetch {} info for {}: {}",
                "AUR", package_name, e.what()));
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    if(!info.has_value()) {
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    if(expected_selected_provider.has_value() &&
       !matches_selected_aur_provider_contract(
               info.value(), expected_selected_provider.value())) {
        std::string diagnostic =
                selected_aur_provider_revalidation_failure_diagnostic(
                        expected_selected_provider.value());
        if(failure_context == nullptr) {
            throw std::runtime_error(diagnostic);
        }
        add_resolution_failure(
                failure_context,
                BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                package_name, diagnostic);
        Logger::warn(diagnostic);
        add_unique_value(plan.unresolved, package_name);
        return;
    }

    // POLICY(#150): visited packageで再帰を打ち切る場合も、package単位のraw metadataは先に保持する。
    add_build_plan_metadata_risk(plan, info.value());
    // POLICY(#218/#268): role/rootとPackageBase child identityはpackage visitedより先にmergeする。
    add_planned_package_target(plan, info.value(), roles, root);
    add_build_plan_entry(plan, info.value());

    std::string build_unit = package_base_name(info.value());
    std::string package_identity = info->Name;
    if(visited_package_names.count(package_identity) > 0) return;
    if(visiting_package_names.count(package_identity) > 0) {
        add_unique_value(plan.cycles, build_unit);
        return;
    }

    visiting_package_names.insert(package_identity);

    const std::vector<TypedPackageDependency> typed_dependencies =
            collect_typed_build_dependencies(info.value());
    for(const auto& dependency : collect_build_dependencies(info.value())) {
        resolve_build_plan_dependency(
                info->Name, build_unit, dependency,
                typed_dependencies_for_specification(
                        typed_dependencies, dependency),
                plan, visited_package_names, visiting_package_names, root,
                depth, max_depth, traverse_aur_providers, resolution_mode,
                select_provider, local_package_bases, local_package_names,
                identity_conflicts,
                resolve_local_dependency);
    }

    visiting_package_names.erase(package_identity);
    visited_package_names.insert(package_identity);
    add_build_plan_split_package_target(plan, info.value());
}

BuildPlan resolve_build_plan_internal(
        const std::vector<std::string>& targets,
        BuildPlanResolutionMode resolution_mode,
        const ProviderSelectionCallback& select_provider) {
    if(targets.empty()) {
        throw std::invalid_argument(localization::translate_message(
                "Build plan targets must not be empty."));
    }

    for(const auto& target : targets) require_valid_package_name(target);
    if(resolution_mode == BuildPlanResolutionMode::Legacy) {
        // POLICY: legacy resolverの事前存在確認と例外境界は通常-S consumerの契約。
        for(const auto& target : targets) {
            if(!query_aur_package_info(
                        target, resolution_mode,
                        static_cast<bool>(select_provider))
                        .has_value()) {
                throw std::runtime_error(
                        localization::format_translated_message(
                                "{} package not found: {}",
                                "AUR", target));
            }
        }
    }

    BuildPlan             plan;
    std::set<std::string> visited_package_names;
    std::set<std::string> visiting_package_names;
    const std::vector<PackageRole> root_roles = {PackageRole::Root};
    for(std::size_t i = 0; i < targets.size(); ++i) {
        RootTargetIdentity root{i, targets[i]};
        plan.root_targets.push_back(root);
        collect_aur_build_plan(
                targets[i], plan, visited_package_names,
                visiting_package_names, root_roles, root, 0,
                MAX_RECURSIVE_DEP_DEPTH, true, resolution_mode,
                select_provider, {}, {}, nullptr, {},
                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt);
    }
    order_build_plan_entries(plan);
    propagate_root_identities(plan);
    propagate_resolution_failure_root_identities(plan);
    project_build_plan_constraint_conflicts(plan);
    require_compatible_selected_provider_package_identities(plan);
    require_constructible_build_plan_constraints(plan);
    return plan;
}

} // namespace

dependency_plan_projection_support::ResolutionResult
dependency_plan_projection_support::resolve(
        const std::vector<RootPackage>& roots,
        const std::set<std::string>& local_package_bases,
        const LocalDependencyResolver& resolve_local_dependency,
        const ProviderSelectionCallback& select_provider) {
    if(roots.empty()) {
        throw std::invalid_argument(localization::translate_message(
                "Build plan targets must not be empty."));
    }

    for(const auto& root : roots) {
        require_valid_package_name(root.package_name);
        require_valid_package_name(root.package_base);
    }

    BuildPlan plan;
    std::set<std::string> visited_package_names;
    std::set<std::string> visiting_package_names;
    std::set<std::string> local_package_names;
    std::vector<RemoteProviderIdentityConflict> identity_conflicts;
    const std::vector<PackageRole> root_roles = {PackageRole::Root};
    for(const auto& root : roots) {
        local_package_names.insert(root.package_name);
    }

    for(std::size_t index = 0; index < roots.size(); ++index) {
        const RootPackage& root_package = roots[index];
        const RootTargetIdentity root{index, root_package.package_name};
        plan.root_targets.push_back(root);
        add_planned_package_target(
                plan, root_package.package_name, root_package.package_base,
                root_roles, root);
        add_build_plan_entry(
                plan, root_package.package_name, root_package.package_base);
        add_build_plan_split_package_target(
                plan, root_package.package_name, root_package.package_base);
        add_build_plan_metadata_risk(
                plan, root_package.package_name, root_package.package_base,
                root_package.conflicts, root_package.replaces);

        std::vector<std::string> specifications;
        for(const auto& dependency : root_package.dependencies) {
            add_unique_value(specifications, dependency.specification);
        }
        for(const auto& specification : specifications) {
            resolve_build_plan_dependency(
                    root_package.package_name, root_package.package_base,
                    specification,
                    typed_dependencies_for_specification(
                            root_package.dependencies, specification),
                    plan, visited_package_names, visiting_package_names, root,
                    0, MAX_RECURSIVE_DEP_DEPTH, true,
                    BuildPlanResolutionMode::CaptureOrdinaryFailures,
                    select_provider,
                    local_package_bases, local_package_names,
                    &identity_conflicts, resolve_local_dependency);
        }
    }

    order_build_plan_entries(plan);
    propagate_root_identities(plan);
    propagate_resolution_failure_root_identities(plan);
    project_build_plan_constraint_conflicts(plan);
    require_compatible_selected_provider_package_identities(plan);
    require_constructible_build_plan_constraints(plan);
    return ResolutionResult{
            std::move(plan), std::move(identity_conflicts)};
}

std::vector<BuildPlanMetadataRisk> collect_build_plan_metadata_risks(const AurPackageInfo& pkg) {
    BuildPlan plan;
    add_build_plan_metadata_risk(plan, pkg);
    return std::move(plan.metadata_risks);
}

BuildPlan resolve_build_plan(const std::string& target) {
    return resolve_build_plan(target, ProviderSelectionCallback{});
}

BuildPlan resolve_build_plan(
        const std::string& target,
        const ProviderSelectionCallback& select_provider) {
    return resolve_build_plan(
            std::vector<std::string>{target}, select_provider);
}

BuildPlan resolve_build_plan(const std::vector<std::string>& targets) {
    return resolve_build_plan(targets, ProviderSelectionCallback{});
}

BuildPlan resolve_build_plan(
        const std::vector<std::string>& targets,
        const ProviderSelectionCallback& select_provider) {
    return resolve_build_plan_internal(
            targets, BuildPlanResolutionMode::Legacy, select_provider);
}

BuildPlan resolve_build_plan_for_preflight(
        const std::vector<std::string>& targets) {
    return resolve_build_plan_for_preflight(
            targets, ProviderSelectionCallback{});
}

BuildPlan resolve_build_plan_for_preflight(
        const std::vector<std::string>& targets,
        const ProviderSelectionCallback& select_provider) {
    return resolve_build_plan_internal(
            targets, BuildPlanResolutionMode::CaptureOrdinaryFailures,
            select_provider);
}

BuildPlan resolve_fetch_plan(const std::string& target) {
    return resolve_fetch_plan(target, ProviderSelectionCallback{});
}

BuildPlan resolve_fetch_plan(
        const std::string& target,
        const ProviderSelectionCallback& select_provider) {
    require_valid_package_name(target);
    if(!query_aur_package_info(
                target, BuildPlanResolutionMode::Legacy,
                static_cast<bool>(select_provider))
                .has_value()) {
        throw std::runtime_error(localization::format_translated_message(
                "{} package not found: {}", "AUR", target));
    }

    BuildPlan             plan;
    std::set<std::string> visited_package_names;
    std::set<std::string> visiting_package_names;
    RootTargetIdentity    root{0, target};
    plan.root_targets.push_back(root);
    const std::vector<PackageRole> root_roles = {PackageRole::Root};
    // POLICY: fetch は取得対象の列挙まで。AUR provider を辿って暗黙に追加取得しない。
    collect_aur_build_plan(
            target, plan, visited_package_names, visiting_package_names,
            root_roles, root, 0,
            MAX_RECURSIVE_DEP_DEPTH, false, BuildPlanResolutionMode::Legacy,
            select_provider, {}, {}, nullptr, {},
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt);
    order_build_plan_entries(plan);
    propagate_root_identities(plan);
    project_build_plan_constraint_conflicts(plan);
    require_compatible_selected_provider_package_identities(plan);
    require_constructible_build_plan_constraints(plan);
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
    return provided_dependency_display(provider);
}

std::string ambiguous_provider_dependency_summary(const AmbiguousProvidedDependency& dependency) {
    std::vector<std::string> candidates;
    for(const auto& candidate : dependency.candidates) {
        candidates.push_back(provider_summary(candidate));
    }
    return dependency.dependency + " (" +
           join_guard_summary_values(candidates) + ")";
}

std::string join_ambiguous_provider_summaries(const std::vector<AmbiguousProvidedDependency>& dependencies) {
    std::vector<std::string> values;
    for(const auto& dependency : dependencies) {
        values.push_back(ambiguous_provider_dependency_summary(dependency));
    }
    return join_guard_summary_values(values);
}

std::string split_package_target_summary(const BuildPlanSplitPackageTarget& target) {
    // NO_TRANSLATE(Issue #308): This guard summary is a stable structured
    // package identity; "base" names its BuildPlan relationship.
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
    // NO_TRANSLATE(Issue #308): These are stable BuildPlan metadata field
    // tokens surrounding package identities, not human-readable prose.
    std::vector<std::string> metadata;
    if(!risk.conflicts.empty()) {
        metadata.push_back(
                "conflicts: " + join_guard_summary_values(risk.conflicts));
    }
    if(!risk.replaces.empty()) {
        metadata.push_back(
                "replaces: " + join_guard_summary_values(risk.replaces));
    }

    std::string package_display = risk.package_name;
    if(risk.package_base != risk.package_name) {
        package_display += " (base: " + risk.package_base + ")";
    }

    std::stringstream metadata_summary;
    for(size_t i = 0; i < metadata.size(); ++i) {
        if(i > 0) metadata_summary << "; ";
        metadata_summary << metadata[i];
    }
    return package_display + " [" + metadata_summary.str() + "]";
}

std::string join_metadata_risk_summaries(const std::vector<BuildPlanMetadataRisk>& risks) {
    std::vector<std::string> values;
    for(const auto& risk : risks) {
        values.push_back(metadata_risk_summary(risk));
    }
    return join_guard_summary_values(values);
}

} // namespace

void require_compatible_selected_provider_package_identities(
        const BuildPlan& plan) {
    for(std::size_t index = 0; index < plan.provided.size(); ++index) {
        const BuildPlanProvidedDependency& selected = plan.provided[index];
        if(selected.resolution != ProviderResolutionKind::UserSelected) {
            continue;
        }
        auto conflict = std::find_if(
                plan.provided.begin(), plan.provided.begin() + index,
                [&selected](const BuildPlanProvidedDependency& existing) {
                    return existing.resolution ==
                                   ProviderResolutionKind::UserSelected &&
                           has_incompatible_provider_package_identity(
                                   existing.provider, selected.provider);
                });
        if(conflict != plan.provided.begin() + index) {
            throw std::runtime_error(
                    selected_provider_package_identity_conflict_diagnostic(
                            conflict->provider, selected.provider));
        }
    }
}

void require_constructible_build_plan_constraints(const BuildPlan& plan) {
    for(const auto& edge : plan.dependency_edges) {
        const bool has_typed_state = edge.requirement.has_value() ||
                edge.resolved_candidate.has_value() ||
                edge.constraint_evaluation.has_value();
        // Older pure fixtures may describe graph shape only. Production
        // resolver edges always enter the typed branch.
        if(!has_typed_state) continue;

        if(!edge.requirement.has_value()) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Build plan constraint requirement is invalid for {}.",
                            edge.dependency_spec));
        }

        const std::string& raw_specification = std::visit(
                [](const auto& requirement) -> const std::string& {
                    return requirement.raw_specification();
                },
                edge.requirement.value());
        if(raw_specification != edge.dependency_spec) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Build plan constraint requirement identity changed for {}.",
                            edge.dependency_spec));
        }

        if(edge.resolved_candidate.has_value()) {
            const bool identity_matches = std::visit(
                    [&edge](const auto& candidate) {
                        using Candidate =
                                std::decay_t<decltype(candidate)>;
                        if constexpr(std::is_same_v<
                                             Candidate,
                                             InstalledExactPackage>) {
                            return edge.kind == DependencyKind::Installed &&
                                   edge.resolved_package_name ==
                                           candidate.package_name;
                        } else if constexpr(std::is_same_v<
                                             Candidate,
                                             RepositoryExactPackage>) {
                            return edge.kind == DependencyKind::Repo &&
                                   !candidate.repository.repository_name.empty() &&
                                   edge.resolved_package_name ==
                                           candidate.package_name;
                        } else if constexpr(std::is_same_v<
                                             Candidate,
                                             AurResolvedDependencyCandidate>) {
                            return edge.kind == DependencyKind::Aur &&
                                   edge.resolved_package_name ==
                                           candidate.package_name &&
                                   edge.resolved_package_base ==
                                           candidate.package_base;
                        } else if constexpr(std::is_same_v<
                                             Candidate,
                                             LocalResolvedDependencyCandidate>) {
                            return edge.kind == DependencyKind::Local &&
                                   edge.resolved_package_name ==
                                           candidate.package_name &&
                                   edge.resolved_package_base ==
                                           candidate.package_base;
                        } else {
                            return edge.kind == DependencyKind::Provided &&
                                   edge.resolved_provider.has_value() &&
                                   same_provider_identity(
                                           edge.resolved_provider.value(),
                                           candidate.provider) &&
                                   edge.resolved_provider
                                                   ->provided_dependency_name ==
                                           candidate.provider
                                                   .provided_dependency_name;
                        }
                    },
                    edge.resolved_candidate.value());
            if(!identity_matches) {
                throw std::runtime_error(
                        localization::format_translated_message(
                                "Build plan constraint source identity is inconsistent for {}.",
                                edge.dependency_spec));
            }
            if(!edge.constraint_evaluation.has_value()) {
                throw std::runtime_error(
                        localization::format_translated_message(
                                "Build plan constraint evaluation is missing for {}.",
                                edge.dependency_spec));
            }
        }

        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
                edge.constraint_evaluation->satisfaction();
        if(satisfaction == ConstraintSatisfaction::Invalid ||
           satisfaction == ConstraintSatisfaction::Conflicting) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Cannot construct build plan; dependency {} is {}: {}.",
                            edge.dependency_spec,
                            constraint_satisfaction_display(satisfaction),
                            constraint_evaluation_reason_display(
                                    edge.constraint_evaluation.value())));
        }
    }
}

void finalize_build_plan_constraints(BuildPlan& plan) {
    project_build_plan_constraint_conflicts(plan);
    require_constructible_build_plan_constraints(plan);
}

void require_mutation_constraint_preflight(
        const std::string& target, const BuildPlan& plan) {
    require_constructible_build_plan_constraints(plan);
    for(const auto& edge : plan.dependency_edges) {
        if(!edge.constraint_evaluation.has_value()) continue;
        const ConstraintSatisfaction satisfaction =
                edge.constraint_evaluation->satisfaction();
        if(satisfaction != ConstraintSatisfaction::Unsatisfied &&
           satisfaction != ConstraintSatisfaction::Unknown) {
            continue;
        }
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; dependency {} is {}: {}.",
                target, edge.dependency_spec,
                constraint_satisfaction_display(satisfaction),
                constraint_evaluation_reason_display(
                        edge.constraint_evaluation.value())));
    }
}

void require_fetchable_build_plan(const std::string& target, const BuildPlan& plan) {
    require_compatible_selected_provider_package_identities(plan);
    require_mutation_constraint_preflight(target, plan);
    if(!plan.unresolved.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; unresolved dependencies: {}",
                target, join_guard_summary_values(plan.unresolved)));
    }
    if(!plan.ambiguous_providers.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; ambiguous providers: {}",
                target,
                join_ambiguous_provider_summaries(
                        plan.ambiguous_providers)));
    }
    if(!plan.cycles.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; cyclic dependencies: {}",
                target, join_guard_summary_values(plan.cycles)));
    }
}

void require_executable_build_plan(const std::string& target, const BuildPlan& plan) {
    require_fetchable_build_plan(target, plan);
    if(!plan.metadata_risks.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute build plan for {}; conflicts/replaces metadata requires manual review: {}",
                target,
                join_metadata_risk_summaries(plan.metadata_risks)));
    }
}

void require_executable_install_plan(const std::string& target, const BuildPlan& plan) {
    // POLICY: 段階的なguard呼び出し順は、複数の問題があるplanで最初に報告するcategoryの契約。
    require_executable_build_plan(target, plan);
    if(!plan.split_package_targets.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                "Cannot execute singular install plan for {}; split package targets require the {} set lifecycle: {}",
                target, "PackageBase",
                join_split_package_target_summaries(
                        plan.split_package_targets)));
    }
}
