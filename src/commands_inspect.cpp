#include "commands_inspect.hpp"

#include "aur_rpc.hpp"
#include "aur_update_plan.hpp"
#include "checkout_fetch.hpp"
#include "dependency_plan.hpp"
#include "dependency_spec.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "package_metadata.hpp"
#include "pkgbuild_export.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// inspection command固有のpresentationとtarget orchestrationを所有する。
// POLICY: CLI parsing、Curl/Logger lifetime、-G/-Gp special routeはrunner側に残す。
namespace {

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";

using RepositoryPackageLookupIdentity =
        std::pair<std::optional<std::string>, std::string>;
using RepositoryPackageDisplayIdentity = std::pair<std::string, std::string>;

// repository metadataはplan graphの正本ではなく、1回のplan invocationだけで有効な
// read-only presentation enrichmentとして所有する。
struct RepositoryMetadataPresentationContext {
    bool configuration_attempted = false;
    std::optional<PacmanRepositoryConfiguration> configuration;
    bool session_open_attempted = false;
    std::optional<RepositoryPackageMetadataSession> session;
    std::optional<PackageMetadataFailure> unavailable_failure;
    std::map<RepositoryPackageLookupIdentity, RepositoryPackageQueryResult>
            query_results;
};

RepositoryPackageLookupIdentity repository_package_lookup_identity(
        const RepositoryPackageLookup& lookup) {
    return {lookup.exact_repository_name, lookup.package_name};
}

void add_repository_package_lookup(
        std::vector<RepositoryPackageLookup>& lookups,
        std::set<RepositoryPackageLookupIdentity>& seen_lookups,
        RepositoryPackageLookup lookup) {
    RepositoryPackageLookupIdentity identity =
            repository_package_lookup_identity(lookup);
    if(!seen_lookups.insert(identity).second) return;
    lookups.push_back(std::move(lookup));
}

std::vector<RepositoryPackageLookup> collect_repository_package_lookups(
        const BuildPlan& plan) {
    std::vector<RepositoryPackageLookup> lookups;
    std::set<RepositoryPackageLookupIdentity> seen_lookups;

    // POLICY(#125): BuildPlan::orderはAUR build unit。official package sizeの正本は
    // dependency edgeの解決結果だけとし、edge first-seen orderを保持する。
    for(const auto& edge : plan.dependency_edges) {
        if(edge.kind == DependencyKind::Repo &&
           edge.resolved_package_name.has_value()) {
            add_repository_package_lookup(
                    lookups, seen_lookups,
                    RepositoryPackageLookup{
                            edge.resolved_package_name.value(), std::nullopt});
            continue;
        }

        if(edge.kind != DependencyKind::Provided ||
           !edge.resolved_provider.has_value() ||
           edge.resolved_provider->repository == "aur") {
            continue;
        }

        // Configured membershipはpacman-confの正本をresolveした後で確認する。
        // 現行provider resolverが返し得るunconfigured/stale repositoryはここではまだ保持する。
        add_repository_package_lookup(
                lookups, seen_lookups,
                RepositoryPackageLookup{
                        edge.resolved_provider->package_name,
                        edge.resolved_provider->repository});
    }
    return lookups;
}

bool ensure_repository_configuration(
        RepositoryMetadataPresentationContext& context) {
    if(context.configuration.has_value()) return true;
    if(context.configuration_attempted) return false;

    context.configuration_attempted = true;
    try {
        context.configuration.emplace(
                resolve_pacman_repository_configuration());
        return true;
    } catch(const PackageMetadataError& error) {
        context.unavailable_failure = error.failure();
        return false;
    }
}

bool is_configured_repository(
        const PacmanRepositoryConfiguration& configuration,
        const std::string& repository_name) {
    return std::find(
                   configuration.repository_names.begin(),
                   configuration.repository_names.end(), repository_name) !=
           configuration.repository_names.end();
}

std::vector<RepositoryPackageLookup> filter_configured_repository_lookups(
        const std::vector<RepositoryPackageLookup>& lookups,
        const PacmanRepositoryConfiguration& configuration) {
    std::vector<RepositoryPackageLookup> configured_lookups;
    configured_lookups.reserve(lookups.size());
    for(const auto& lookup : lookups) {
        if(lookup.exact_repository_name.has_value() &&
           !is_configured_repository(
                   configuration, lookup.exact_repository_name.value())) {
            continue;
        }
        configured_lookups.push_back(lookup);
    }
    return configured_lookups;
}

bool ensure_repository_metadata_session(
        RepositoryMetadataPresentationContext& context) {
    if(context.session.has_value()) return true;
    if(context.session_open_attempted || context.unavailable_failure.has_value() ||
       !context.configuration.has_value()) {
        return false;
    }

    context.session_open_attempted = true;
    try {
        context.session.emplace(RepositoryPackageMetadataSession::open(
                context.configuration.value()));
        return true;
    } catch(const PackageMetadataError& error) {
        context.unavailable_failure = error.failure();
        return false;
    }
}

const RepositoryPackageQueryResult& query_repository_package_cached(
        RepositoryMetadataPresentationContext& context,
        const RepositoryPackageLookup& lookup) {
    RepositoryPackageLookupIdentity identity =
            repository_package_lookup_identity(lookup);
    auto cached_result = context.query_results.find(identity);
    if(cached_result != context.query_results.end()) return cached_result->second;

    RepositoryPackageQueryResult result =
            context.session->query_repository_package(lookup);
    auto inserted = context.query_results.emplace(
            std::move(identity), std::move(result));
    return inserted.first->second;
}

std::string repository_metadata_failure_reason(PackageMetadataErrorCode code) {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationUnavailable:
        return "configuration unavailable";
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return "configuration malformed";
    case PackageMetadataErrorCode::InitializationFailed:
        return "initialization failed";
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        return "local database unavailable";
    case PackageMetadataErrorCode::InvalidPackageName:
        return "invalid package name";
    case PackageMetadataErrorCode::QueryFailed:
        return "query failed";
    case PackageMetadataErrorCode::MalformedMetadata:
        return "invalid metadata";
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return "sync database unavailable";
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return "repository not configured";
    }
    return "metadata failure";
}

std::string repository_package_lookup_display(
        const RepositoryPackageLookup& lookup) {
    if(!lookup.exact_repository_name.has_value()) return lookup.package_name;
    return lookup.exact_repository_name.value() + "/" + lookup.package_name;
}

std::string format_iec_bytes(std::uint64_t bytes) {
    constexpr std::uint64_t IEC_UNIT_BASE = 1024;
    constexpr std::array<const char*, 7> IEC_UNITS = {
            "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};

    if(bytes < IEC_UNIT_BASE) return std::to_string(bytes) + " B";

    std::size_t unit_index = 0;
    std::uint64_t unit_divisor = 1;
    while(unit_index + 1 < IEC_UNITS.size() &&
          bytes / unit_divisor >= IEC_UNIT_BASE) {
        unit_divisor *= IEC_UNIT_BASE;
        ++unit_index;
    }

    std::uint64_t whole = bytes / unit_divisor;
    std::uint64_t remainder = bytes % unit_divisor;
    std::array<std::uint64_t, 3> decimal_digits{};
    for(auto& digit : decimal_digits) {
        // unit_divisorは最大2^60なので、remainder * 10はuint64_t内に収まる。
        remainder *= 10;
        digit = remainder / unit_divisor;
        remainder %= unit_divisor;
    }

    std::uint64_t hundredths = decimal_digits[0] * 10 + decimal_digits[1];
    if(decimal_digits[2] >= 5) ++hundredths;
    if(hundredths == 100) {
        hundredths = 0;
        ++whole;
    }

    // LANDMINE: 1023.995...は1024.00ではなく、次unitの1.00として表示する。
    if(whole == IEC_UNIT_BASE && unit_index + 1 < IEC_UNITS.size()) {
        whole = 1;
        hundredths = 0;
        ++unit_index;
    }

    std::string formatted = std::to_string(whole) + ".";
    if(hundredths < 10) formatted += "0";
    formatted += std::to_string(hundredths);
    formatted += " ";
    formatted += IEC_UNITS[unit_index];
    return formatted;
}

void print_repository_metadata_unavailable(
        const PackageMetadataFailure& failure) {
    std::cout << std::endl;
    std::cout << "Repository package sizes:" << std::endl;
    std::cout << "  Metadata       : unavailable ("
              << repository_metadata_failure_reason(failure.code) << ")"
              << std::endl;
}

void print_repository_package_sizes(
        const BuildPlan& plan,
        RepositoryMetadataPresentationContext& context) {
    std::vector<RepositoryPackageLookup> provisional_lookups =
            collect_repository_package_lookups(plan);
    if(provisional_lookups.empty()) return;

    if(!ensure_repository_configuration(context)) {
        print_repository_metadata_unavailable(context.unavailable_failure.value());
        return;
    }

    std::vector<RepositoryPackageLookup> lookups =
            filter_configured_repository_lookups(
                    provisional_lookups, context.configuration.value());
    if(lookups.empty()) return;

    if(!ensure_repository_metadata_session(context)) {
        print_repository_metadata_unavailable(context.unavailable_failure.value());
        return;
    }

    std::cout << std::endl;
    std::cout << "Repository package sizes:" << std::endl;
    std::set<RepositoryPackageDisplayIdentity> displayed_packages;
    for(const auto& lookup : lookups) {
        const RepositoryPackageQueryResult& result =
                query_repository_package_cached(context, lookup);
        if(const auto* metadata =
                   std::get_if<RepositoryPackageMetadata>(&result)) {
            RepositoryPackageDisplayIdentity display_identity{
                    metadata->repository_name, metadata->package_name};
            if(!displayed_packages.insert(display_identity).second) continue;

            std::cout << "  " << metadata->repository_name << "/"
                      << metadata->package_name << std::endl;
            std::cout << "    Package size   : "
                      << format_iec_bytes(metadata->package_size_bytes) << std::endl;
            std::cout << "    Installed size : "
                      << format_iec_bytes(metadata->installed_size_bytes) << std::endl;
            continue;
        }

        std::cout << "  " << repository_package_lookup_display(lookup) << std::endl;
        if(std::holds_alternative<PackageNotFound>(result)) {
            std::cout << "    Metadata       : not found" << std::endl;
            continue;
        }

        const PackageMetadataFailure& failure =
                std::get<PackageMetadataFailure>(result);
        std::cout << "    Metadata       : unavailable ("
                  << repository_metadata_failure_reason(failure.code) << ")"
                  << std::endl;
    }
}

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

AurVersionRelation compare_aur_versions(
        const std::string& aur_version, const std::string& installed_version) {
    std::string cmp_cmd = "vercmp " + shell_words::quote(aur_version) + " " + shell_words::quote(installed_version);
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    try {
        int comparison = std::stoi(cmp_res);
        if(comparison > 0) return AurVersionRelation::NewerThanInstalled;
        if(comparison < 0) return AurVersionRelation::OlderThanInstalled;
        return AurVersionRelation::SameAsInstalled;
    } catch(...) {
        Logger::warn("Failed to compare versions: " + installed_version + " -> " + aur_version);
        return AurVersionRelation::Unavailable;
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

    bool                                  failed = false;
    RepositoryMetadataPresentationContext metadata_context;
    for(size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        require_valid_package_name(target);

        try {
            BuildPlan plan = resolve_build_plan(target);

            if(i > 0) std::cout << std::endl;
            print_build_plan(plan);
            print_repository_package_sizes(plan, metadata_context);
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
    std::set<std::string>                 metadata_unavailable_packages;
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
            metadata_unavailable_packages.insert(batch.begin(), batch.end());
            failed = true;
        }
    }

    AurUpdatePlan update_plan;
    update_plan.entries.reserve(packages.size());
    for(size_t i = 0; i < packages.size(); ++i) {
        const auto& local_pkg = packages[i];
        Logger::info("Checking package " + std::to_string(i + 1) + "/" + std::to_string(packages.size()) + ": " + local_pkg.name);

        auto aur_pkg = aur_packages.find(local_pkg.name);
        AurUpdateMetadataResult aur_metadata = AurUpdateMetadataNotFound{};
        if(metadata_unavailable_packages.contains(local_pkg.name)) {
            aur_metadata = AurUpdateMetadataUnavailable{};
        } else if(aur_pkg != aur_packages.end()) {
            aur_metadata = AurUpdateRemotePackage{
                    aur_pkg->second.Name,
                    aur_pkg->second.PackageBase,
                    aur_pkg->second.Version,
                    compare_aur_versions(aur_pkg->second.Version, local_pkg.version)};
        }

        update_plan.entries.push_back(classify_aur_update(AurUpdatePlanInput{
                local_pkg.name, local_pkg.version, std::move(aur_metadata)}));
        const AurUpdatePlanEntry& entry = update_plan.entries.back();

        switch(entry.classification) {
        case AurUpdateClassification::NonAurForeign:
        case AurUpdateClassification::MetadataUnavailable:
            // POLICY(#266): failed batchも従来のnot-found warningを維持するが、
            // pure modelではconfirmed absenceとquery failureを同一視しない。
            Logger::warn("Foreign package not found in AUR: " + local_pkg.name);
            break;
        case AurUpdateClassification::UpdateAvailable:
            std::cout << entry.installed_name << " " << entry.installed_version
                      << " -> " << entry.aur_package->version << std::endl;
            break;
        case AurUpdateClassification::UpToDate:
        case AurUpdateClassification::VersionComparisonUnavailable:
            break;
        default:
            throw std::logic_error("Unknown AUR update classification.");
        }
    }

    return failed ? 1 : 0;
}
