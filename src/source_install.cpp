#include "source_install.hpp"

#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "build_plan_artifact_target_projection.hpp"
#include "cache_authority.hpp"
#include "dependency_plan.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "repository_query.hpp"
#include "separated_source_build.hpp"
#include "source_build.hpp"
#include "source_preference.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";

SourceBuildEnvironment load_source_preference_environment(
        const std::string& package_name) {
    return get_package_env(
            package_name,
            [](const fs::path& entry_path) {
                Logger::info("Loading custom build flags from " + entry_path.string());
            },
            [](const std::string& warning) {
                Logger::warn(warning);
            });
}

bool has_distinct_package_base(const AurPackageInfo& info) {
    return info.PackageBase != info.Name;
}

std::string canonical_source_key(
        SourceBuildSourceKind source_kind,
        const std::string& package_base) {
    switch(source_kind) {
        case SourceBuildSourceKind::Repository:
            return "repository:" + package_base;
        case SourceBuildSourceKind::Aur:
            return "aur:" + package_base;
    }
    throw std::logic_error("Unknown source-build source kind.");
}

void require_supported_registered_source_install_target(
        const ResolvedSourceBuildIdentity& source) {
    // POLICY(#98,#268): registered source upgradeのlegacy singular lifecycleは
    // requested split childを個別選択できないため安全側で停止する。
    if(source.source_kind == SourceBuildSourceKind::Aur &&
       source.has_distinct_package_base) {
        throw std::runtime_error(
                "Registered source upgrade does not support split AUR preference " +
                source.requested_name + " from PackageBase " +
                source.package_base +
                "; this route requires a singular package identity.");
    }
}

DesiredInstallReason resolve_source_target_reason(
        const ResolvedSourceBuildIdentity& source,
        bool use_package_base_lifecycle) {
    if(source.source_kind != SourceBuildSourceKind::Aur) {
        return DesiredInstallReason::Explicit;
    }

    // POLICY(#174,#268): dependency graph全体のRPC schemaを解決してから
    // route固有のexecutable guardへ進む。registered source upgradeのlegacy
    // singular ownerだけはsplit selection guardを維持する。
    BuildPlan plan = resolve_build_plan(source.requested_name);
    if(use_package_base_lifecycle) {
        require_executable_build_plan(source.requested_name, plan);
    } else {
        require_supported_registered_source_install_target(source);
        require_executable_install_plan(source.requested_name, plan);
    }
    BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success()) {
        throw std::logic_error(
                "BuildPlan required artifact target projection failed for " +
                source.requested_name + ".");
    }
    for(const auto& unit : projection.success()->build_units) {
        if(unit.package_base != source.package_base) continue;
        for(const auto& target : unit.required_targets) {
            if(target.package_name == source.requested_name) {
                return target.desired_reason;
            }
        }
    }
    throw std::logic_error(
            "BuildPlan required artifact target projection omitted " +
            source.requested_name + ".");
}

std::string join_required_package_names(
        const std::vector<RequiredPackageArtifactTarget>& targets) {
    std::stringstream ss;
    for(size_t i = 0; i < targets.size(); ++i) {
        if(i > 0) ss << ", ";
        ss << targets[i].package_name;
    }
    return ss.str();
}

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}

ProductionSourceBuildWorkItem make_direct_source_build_work_item(
        const ResolvedSourceBuildIdentity& source,
        SourceBuildEnvironment environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy,
        bool only_if_updated,
        bool needed,
        bool use_package_base_lifecycle) {
    ProductionSourceBuildWorkItem work_item;
    work_item.request.package_name = source.requested_name;
    work_item.request.checkout_name = source.package_base;
    work_item.request.git_url = source.git_url;
    work_item.request.custom_environment = std::move(environment);
    work_item.request.empty_value_policy = empty_value_policy;
    work_item.request.only_if_updated = only_if_updated;
    work_item.request.needed = needed;
    work_item.required_targets.push_back(RequiredPackageArtifactTarget{
            source.package_base,
            source.requested_name,
            resolve_source_target_reason(
                    source, use_package_base_lifecycle)});
    work_item.is_build_plan_entry = use_package_base_lifecycle;
    work_item.uses_system_update_baseline =
            source.source_kind == SourceBuildSourceKind::Repository;
    require_static_production_source_build_work_item(work_item);
    return work_item;
}

std::optional<ArtifactInstallExecutionOutcome> flatten_source_build_result(
        const SourceBuildExecutionResult& result) {
    switch(result.status) {
        case SourceBuildExecutionStatus::Installed:
            return ArtifactInstallExecutionOutcome::Installed;
        case SourceBuildExecutionStatus::SkippedAsNeeded:
            return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
        case SourceBuildExecutionStatus::UpToDate:
        case SourceBuildExecutionStatus::UpdateStatusUnknownSkipped:
            return std::nullopt;
    }
    throw std::logic_error("Unknown source-build execution status.");
}

std::string_view install_reason_label(DesiredInstallReason reason) {
    switch(reason) {
        case DesiredInstallReason::Explicit:
            return "explicit";
        case DesiredInstallReason::Dependency:
            return "dependency";
    }
    throw std::logic_error("Unknown desired install reason.");
}

std::string_view install_outcome_label(
        ArtifactInstallExecutionOutcome outcome) {
    switch(outcome) {
        case ArtifactInstallExecutionOutcome::Installed:
            return "installed";
        case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
            return "skipped as needed (--needed)";
    }
    throw std::logic_error("Unknown artifact install execution outcome.");
}

bool should_present_package_base_result(
        const ProductionSourceBuildWorkItem& work_item,
        const PackageBaseSourceBuildExecutionResult& result) noexcept {
    return work_item.required_targets.size() != 1 ||
           work_item.required_targets.front().package_name !=
                   work_item.request.checkout_name ||
           !result.unselected_artifacts().empty();
}

void present_package_base_result(
        const ProductionSourceBuildWorkItem& work_item,
        const PackageBaseSourceBuildExecutionResult& result) {
    if(!should_present_package_base_result(work_item, result)) return;
    if(result.package_base() != work_item.request.checkout_name ||
       result.selected_children().size() !=
               work_item.required_targets.size()) {
        throw std::logic_error(
                "PackageBase source-build result is incoherent for presentation.");
    }

    Logger::info("PackageBase result: " + result.package_base());
    for(std::size_t index = 0;
        index < result.selected_children().size(); ++index) {
        const RequiredPackageArtifactTarget& required =
                work_item.required_targets[index];
        const PackageBaseSourceBuildSelectedResult& child =
                result.selected_children()[index];
        if(child.identity.package_name != required.package_name ||
           child.identity.full_version.empty() ||
           child.desired_reason != required.desired_reason) {
            throw std::logic_error(
                    "PackageBase source-build child result is incoherent for presentation.");
        }
        Logger::info(
                "  required child: " + required.package_name + " -> " +
                child.identity.package_name + " " +
                child.identity.full_version + " (" +
                std::string(install_reason_label(child.desired_reason)) +
                "): " + std::string(install_outcome_label(child.outcome)));
    }
    for(const ArtifactPackageIdentity& unselected :
        result.unselected_artifacts()) {
        if(unselected.package_name.empty() ||
           unselected.full_version.empty()) {
            throw std::logic_error(
                    "PackageBase unselected artifact identity is incoherent for presentation.");
        }
        Logger::info(
                "  produced artifact: " + unselected.package_name + " " +
                unselected.full_version +
                " (not selected; not installed)");
    }
}

} // namespace

void seed_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation,
        const ValidatedCacheRoot& cache_root) {
    if(invocation.work_items.empty()) {
        throw std::logic_error(
                "Cannot seed cache for an empty source-build invocation.");
    }

    cache_root.require_unchanged_identity();
    std::optional<ValidatedCacheRoot> existing_root = invocation.cache_root;
    for(const auto& work_item : invocation.work_items) {
        if(!work_item.cache_root.has_value()) continue;
        work_item.cache_root->require_unchanged_identity();
        if(!existing_root.has_value()) {
            existing_root = work_item.cache_root.value();
            continue;
        }
        existing_root->require_unchanged_identity();
        if(existing_root->device() != work_item.cache_root->device() ||
           existing_root->inode() != work_item.cache_root->inode() ||
           existing_root->owner() != work_item.cache_root->owner()) {
            throw std::logic_error(
                    "Production source-build work items use different cache authorities.");
        }
    }

    if(existing_root.has_value() &&
       (existing_root->device() != cache_root.device() ||
        existing_root->inode() != cache_root.inode() ||
        existing_root->owner() != cache_root.owner())) {
        throw std::logic_error(
                "Production source-build invocation cache authority changed.");
    }

    invocation.cache_root = cache_root;
    for(auto& work_item : invocation.work_items) {
        work_item.cache_root = cache_root;
    }
}

void activate_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation) {
    if(invocation.work_items.empty()) {
        throw std::logic_error(
                "Cannot activate cache for an empty source-build invocation.");
    }
    std::optional<ValidatedCacheRoot> shared_root = invocation.cache_root;
    for(const auto& work_item : invocation.work_items) {
        if(!work_item.cache_root.has_value()) continue;
        if(!shared_root.has_value()) shared_root = work_item.cache_root;
    }
    if(!shared_root.has_value()) shared_root = prepare_process_cache_root();
    seed_production_source_build_cache(invocation, shared_root.value());
}

namespace {

const ValidatedCacheRoot& require_prepared_cache_root(
        const ProductionSourceBuildWorkItem& work_item) {
    if(!work_item.cache_root.has_value()) {
        throw std::logic_error(
                "Production source-build work item has no prepared cache authority.");
    }
    work_item.cache_root->require_unchanged_identity();
    return work_item.cache_root.value();
}

} // namespace

ResolvedSourceBuildIdentity resolve_source_build_identity(
        const std::string& package_name) {
    require_valid_package_name(package_name);

    if(is_repo_package(package_name)) {
        const SourceBuildSourceKind source_kind =
                SourceBuildSourceKind::Repository;
        return ResolvedSourceBuildIdentity{
                package_name,
                package_name,
                canonical_source_key(source_kind, package_name),
                ARCH_GIT_BASE + package_name + ".git",
                source_kind,
                false};
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(package_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Failed to fetch AUR info for " + package_name + ": " +
                error.what());
    }

    if(!info.has_value()) {
        throw std::runtime_error(
                "Package not found in repos or AUR: " + package_name);
    }
    if(info->PackageBase.empty()) {
        throw std::runtime_error(
                "AUR info for " + package_name +
                " does not include PackageBase.");
    }
    require_valid_package_name(info->PackageBase);

    const SourceBuildSourceKind source_kind = SourceBuildSourceKind::Aur;
    return ResolvedSourceBuildIdentity{
            package_name,
            info->PackageBase,
            canonical_source_key(source_kind, info->PackageBase),
            AUR_BASE_URL + info->PackageBase + ".git",
            source_kind,
            has_distinct_package_base(info.value())};
}

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config) {
    // --rmdepsはAUR/repository probeより前に、invocation optionとして拒否する。
    require_supported_production_source_build_options(config);
    require_valid_package_name(package_name);
    ValidatedCacheRoot cache_root = prepare_process_cache_root();
    ResolvedSourceBuildIdentity source =
            resolve_source_build_identity(package_name);
    ProductionSourceBuildWorkItem work_item = make_direct_source_build_work_item(
            source, custom_environment,
            SourceEnvironmentEmptyValuePolicy::Forward, false, false,
            source.source_kind == SourceBuildSourceKind::Aur);
    work_item.cache_root = cache_root;
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(std::move(work_item));
    PreparedProductionSourceBuildInvocation invocation =
            prepare_production_source_build_invocation(
                    std::move(work_items), config);
    execute_prepared_source_build_invocation(invocation, config);
}

ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed) {
    return make_direct_source_build_work_item(
            identity, std::move(environment),
            SourceEnvironmentEmptyValuePolicy::Omit, only_if_updated, needed,
            false);
}

std::vector<ProductionSourceBuildWorkItem> prepare_aur_source_build_work_items(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed) {
    BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success()) {
        throw std::logic_error(
                "BuildPlan required artifact target projection failed before source-build work-item preparation.");
    }

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.reserve(projection.success()->build_units.size());
    for(const auto& unit : projection.success()->build_units) {
        const bool is_singular = unit.required_targets.size() == 1;
        const std::string preference_name = is_singular
                ? unit.required_targets.front().package_name
                : unit.package_base;
        SourceBuildEnvironment environment;
        if(use_source_build_preferences) {
            SourceBuildEnvironment requested_environment =
                    load_source_preference_environment(preference_name);
            // POLICY(#242): empty definitionを保持したまま、fallback判定だけは従来の
            // forward可能なnonempty assignment基準にする。PKGDEST definitionは
            // fallbackで捨てず、all-target preflightまで保持する。
            if(!requested_environment.has_forwarded_nonempty_assignment() &&
               !requested_environment.defines("PKGDEST") &&
               is_singular && preference_name != unit.package_base) {
                environment = load_source_preference_environment(unit.package_base);
            } else {
                environment = requested_environment;
            }
        }

        ProductionSourceBuildWorkItem work_item;
        if(is_singular) {
            work_item.request.package_name =
                    unit.required_targets.front().package_name;
        }
        work_item.request.checkout_name = unit.package_base;
        work_item.request.git_url = aur_git_url_for_package_base(unit.package_base);
        work_item.request.custom_environment = std::move(environment);
        work_item.request.needed = needed;
        work_item.required_targets = unit.required_targets;
        work_item.is_build_plan_entry = true;
        require_static_production_source_build_work_item(work_item);
        work_items.push_back(std::move(work_item));
    }
    return work_items;
}

ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed) {
    SourceBuildEnvironment environment =
            load_source_preference_environment(package_name);
    ResolvedSourceBuildIdentity identity =
            resolve_source_build_identity(package_name);
    return prepare_resolved_source_build_work_item(
            identity, std::move(environment), only_if_updated, needed);
}

PackageBaseSourceBuildExecutionResult
execute_prepared_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    // set ownerはAUR BuildPlanから必要childを確定したwork itemに限定する。
    require_static_production_source_build_work_item(work_item);
    if(!work_item.is_build_plan_entry) {
        throw std::logic_error(
                "PackageBase set source-build execution requires an AUR BuildPlan work item.");
    }
    if(work_item.request.only_if_updated) {
        throw std::logic_error(
                "PackageBase set source-build execution does not support only-if-updated requests.");
    }

    Logger::info(
            "Building AUR PackageBase: " +
            work_item.request.checkout_name);
    Logger::info(
            "Target package(s): " +
            join_required_package_names(work_item.required_targets));
    return execute_source_build_package_base_typed(
            work_item.request, work_item.required_targets,
            require_prepared_cache_root(work_item),
            database_paths, config);
}

SourceBuildExecutionResult execute_prepared_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    const RequiredPackageArtifactTarget& target =
            require_singular_required_package_target(work_item);
    if(work_item.is_build_plan_entry) {
        Logger::info(
                "Building AUR PackageBase: " +
                work_item.request.checkout_name);
        Logger::info(
                "Target package(s): " +
                join_required_package_names(work_item.required_targets));
    }

    try {
        return execute_source_build_typed(
                work_item.request, require_prepared_cache_root(work_item),
                target.desired_reason,
                database_paths, config);
    } catch(const SeparatedSourceBuildCleanupError&) {
        // POLICY(#242): install成功後cleanup失敗の型とdiagnosticをgeneric
        // build/install failureへflattenしない。
        throw;
    } catch(const TrustedCacheError&) {
        // Cache authority failureはtyped callerがphase/codeを保持できるよう、
        // generic build/install diagnosticへwrapしない。
        throw;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Failed while building/installing PackageBase " +
                work_item.request.checkout_name + " (" +
                work_item.request.package_name + "): " + error.what());
    }
}

std::optional<ArtifactInstallExecutionOutcome>
execute_prepared_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    return flatten_source_build_result(
            execute_prepared_source_build_work_item_typed(
                    work_item, database_paths, config));
}

void execute_prepared_source_build_invocation(
        PreparedProductionSourceBuildInvocation invocation,
        const AppConfig& config) {
    activate_production_source_build_cache(invocation);
    for(const auto& work_item : invocation.work_items) {
        if(work_item.is_build_plan_entry) {
            try {
                PackageBaseSourceBuildExecutionResult result =
                        execute_prepared_package_base_source_build_work_item_typed(
                                work_item, invocation.database_paths, config);
                present_package_base_result(work_item, result);
            } catch(const SeparatedPackageBaseSourceBuildCleanupError& error) {
                // Transaction完了済みのchild outcomeを失わず表示し、
                // callerがcleanup failureを成功と扱わないようtypedで再throwする。
                present_package_base_result(work_item, error.result());
                throw;
            }
        } else {
            execute_prepared_source_build_work_item(
                    work_item, invocation.database_paths, config);
        }
    }
}
