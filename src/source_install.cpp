#include "source_install.hpp"

#include "app_config.hpp"
#include "aur_rpc.hpp"
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
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";

// requested package から、実際に取得する PackageBase と git URL を結びつける型。
struct PackageBuildSource {
    std::string requested_name;
    std::string clone_name;
    std::string git_url;
    bool        is_aur = false;
    bool        has_distinct_package_base = false;
};

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

PackageBuildSource resolve_build_source(const std::string& package_name) {
    require_valid_package_name(package_name);

    if(is_repo_package(package_name)) {
        return PackageBuildSource{
                package_name, package_name, ARCH_GIT_BASE + package_name + ".git", false, false};
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(package_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& e) {
        throw std::runtime_error("Failed to fetch AUR info for " + package_name + ": " + e.what());
    }

    if(!info.has_value()) {
        throw std::runtime_error("Package not found in repos or AUR: " + package_name);
    }
    if(info->PackageBase.empty()) {
        throw std::runtime_error("AUR info for " + package_name + " does not include PackageBase.");
    }
    require_valid_package_name(info->PackageBase);

    return PackageBuildSource{
            package_name, info->PackageBase, AUR_BASE_URL + info->PackageBase + ".git", true,
            has_distinct_package_base(info.value())};
}

void require_supported_build_source_install_target(const PackageBuildSource& source) {
    // POLICY(#98,#242): productionのsingle-artifact selectionではsplit packageの
    // install対象を個別選択できないため、requested nameとPackageBaseが異なる
    // AUR targetは安全側で停止する。
    if(source.is_aur && source.has_distinct_package_base) {
        throw std::runtime_error(
                "Cannot build/install split AUR package " + source.requested_name + " from PackageBase " +
                source.clone_name + "; explicit split package install target selection is not implemented.");
    }
}

const PlannedPackageTarget& bind_planned_package_target(
        const BuildPlan& plan, const std::string& package_name,
        const std::string& package_base) {
    const PlannedPackageTarget* matched_target = nullptr;
    for(const auto& target : plan.package_targets) {
        if(target.package_name != package_name ||
           target.package_base != package_base) {
            continue;
        }
        if(matched_target != nullptr) {
            throw std::logic_error(
                    "BuildPlan contains duplicate package target binding for " +
                    package_name + " from PackageBase " + package_base + ".");
        }
        matched_target = &target;
    }
    if(matched_target == nullptr) {
        throw std::logic_error(
                "BuildPlan is missing package target binding for " + package_name +
                " from PackageBase " + package_base + ".");
    }
    if(matched_target->roots.empty()) {
        throw std::logic_error(
                "BuildPlan package target has no root identity: " + package_name + ".");
    }
    for(const auto& root : matched_target->roots) {
        if(std::find(plan.root_targets.begin(), plan.root_targets.end(), root) ==
           plan.root_targets.end()) {
            throw std::logic_error(
                    "BuildPlan package target refers to an unknown root identity: " +
                    package_name + ".");
        }
    }
    return *matched_target;
}

DesiredInstallReason resolve_source_target_reason(
        const PackageBuildSource& source) {
    if(!source.is_aur) return DesiredInstallReason::Explicit;

    // POLICY(#174,#242): dependency graph全体のRPC schemaを解決してから
    // split/executable guardへ進む。upgradeのsystem transaction前preflightでも
    // malformed dependencyをtarget-local split diagnosticで隠さない。
    BuildPlan plan = resolve_build_plan(source.requested_name);
    require_supported_build_source_install_target(source);
    require_executable_install_plan(source.requested_name, plan);
    const PlannedPackageTarget& target = bind_planned_package_target(
            plan, source.requested_name, source.clone_name);
    return desired_install_reason(target);
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

void require_static_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item) {
    require_valid_package_name(work_item.request.package_name);
    require_valid_package_name(work_item.request.checkout_name);
    if(work_item.request.package_name != work_item.request.checkout_name) {
        throw std::runtime_error(
                "Production separated source-build requires requested package and "
                "PackageBase to match: " + work_item.request.package_name + " / " +
                work_item.request.checkout_name + ".");
    }
    if(work_item.request.git_url.empty()) {
        throw std::logic_error(
                "Production source-build work item has an empty Git URL for " +
                work_item.request.package_name + ".");
    }
    switch(work_item.desired_reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            break;
        default:
            throw std::logic_error(
                    "Production source-build work item has an unknown install reason.");
    }
}

ProductionSourceBuildWorkItem make_direct_source_build_work_item(
        const PackageBuildSource& source,
        SourceBuildEnvironment environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy,
        bool only_if_updated,
        bool needed) {
    ProductionSourceBuildWorkItem work_item;
    work_item.request.package_name = source.requested_name;
    work_item.request.checkout_name = source.clone_name;
    work_item.request.git_url = source.git_url;
    work_item.request.custom_environment = std::move(environment);
    work_item.request.empty_value_policy = empty_value_policy;
    work_item.request.only_if_updated = only_if_updated;
    work_item.request.needed = needed;
    work_item.desired_reason = resolve_source_target_reason(source);
    work_item.uses_system_update_baseline = !source.is_aur;
    require_static_source_build_work_item(work_item);
    return work_item;
}

} // namespace

void require_supported_production_source_build_options(
        const AppConfig& config) {
    require_supported_separated_install_options(config.rm_deps);
}

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config) {
    // --rmdepsはAUR/repository probeより前に、invocation optionとして拒否する。
    require_supported_production_source_build_options(config);
    PackageBuildSource source = resolve_build_source(package_name);
    ProductionSourceBuildWorkItem work_item = make_direct_source_build_work_item(
            source, custom_environment,
            SourceEnvironmentEmptyValuePolicy::Forward, false, false);
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.push_back(std::move(work_item));
    PreparedProductionSourceBuildInvocation invocation =
            prepare_production_source_build_invocation(
                    std::move(work_items), config);
    execute_prepared_source_build_invocation(invocation, config);
}

std::vector<ProductionSourceBuildWorkItem> prepare_aur_source_build_work_items(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed) {
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.reserve(plan.order.size());
    for(const auto& entry : plan.order) {
        if(entry.package_names.size() != 1) {
            throw std::logic_error(
                    "Production separated source-build requires exactly one package "
                    "name for PackageBase " + entry.package_base + ".");
        }
        const std::string& package_name = entry.package_names.front();
        const PlannedPackageTarget& target = bind_planned_package_target(
                plan, package_name, entry.package_base);
        SourceBuildEnvironment environment;
        if(use_source_build_preferences) {
            SourceBuildEnvironment requested_environment =
                    load_source_preference_environment(package_name);
            // POLICY(#242): empty definitionを保持したまま、fallback判定だけは従来の
            // forward可能なnonempty assignment基準にする。PKGDEST definitionは
            // fallbackで捨てず、all-target preflightまで保持する。
            if(!requested_environment.has_forwarded_nonempty_assignment() &&
               !requested_environment.defines("PKGDEST") &&
               package_name != entry.package_base) {
                environment = load_source_preference_environment(entry.package_base);
            } else {
                environment = requested_environment;
            }
        }

        ProductionSourceBuildWorkItem work_item;
        work_item.request.package_name = package_name;
        work_item.request.checkout_name = entry.package_base;
        work_item.request.git_url = aur_git_url_for_package_base(entry.package_base);
        work_item.request.custom_environment = std::move(environment);
        work_item.request.needed = needed;
        work_item.desired_reason = desired_install_reason(target);
        work_item.is_build_plan_entry = true;
        work_item.plan_package_names = entry.package_names;
        require_static_source_build_work_item(work_item);
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
    PackageBuildSource source = resolve_build_source(package_name);
    return make_direct_source_build_work_item(
            source, std::move(environment),
            SourceEnvironmentEmptyValuePolicy::Omit, only_if_updated, needed);
}

PreparedProductionSourceBuildInvocation prepare_production_source_build_invocation(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        const AppConfig& config) {
    if(work_items.empty()) {
        throw std::invalid_argument(
                "Production source-build invocation must contain at least one work item.");
    }

    // POLICY(#242): exact orderはrmdeps → inherited PKGDEST → all source
    // environments → static identity/role → database paths。ここまではworkspace、
    // checkout、makepkg、metadata session、sudoを開始しない。
    require_supported_separated_install_options(config.rm_deps);
    require_unclaimed_artifact_pkgdest(SourceBuildEnvironment{});
    for(const auto& work_item : work_items) {
        require_unclaimed_artifact_pkgdest(
                work_item.request.custom_environment);
    }
    for(const auto& work_item : work_items) {
        require_static_source_build_work_item(work_item);
    }

    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items), std::move(database_paths)};
}

void execute_prepared_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    if(work_item.is_build_plan_entry) {
        Logger::info(
                "Building AUR PackageBase: " +
                work_item.request.checkout_name);
        Logger::info(
                "Target package(s): " +
                join_comma_display_values(work_item.plan_package_names));
    }

    try {
        execute_source_build(
                work_item.request, work_item.desired_reason,
                database_paths, config);
    } catch(const SeparatedSourceBuildCleanupError&) {
        // POLICY(#242): install成功後cleanup失敗の型とdiagnosticをgeneric
        // build/install failureへflattenしない。
        throw;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Failed while building/installing PackageBase " +
                work_item.request.checkout_name + " (" +
                work_item.request.package_name + "): " + error.what());
    }
}

void execute_prepared_source_build_invocation(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config) {
    for(const auto& work_item : invocation.work_items) {
        execute_prepared_source_build_work_item(
                work_item, invocation.database_paths, config);
    }
}
