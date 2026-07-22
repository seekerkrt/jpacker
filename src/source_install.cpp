#include "source_install.hpp"

#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "dependency_plan.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "repository_query.hpp"
#include "source_build.hpp"
#include "source_preference.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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

std::string load_source_preference_environment(const std::string& package_name) {
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
    // POLICY(#98): makepkg -i 経路では split package の install 対象を jpacker が個別選択できない。
    // v1.9.0 では、PackageBase と requested package name が異なる AUR target は安全側で停止する。
    if(source.is_aur && source.has_distinct_package_base) {
        throw std::runtime_error(
                "Cannot build/install split AUR package " + source.requested_name + " from PackageBase " +
                source.clone_name + "; explicit split package install target selection is not implemented.");
    }
}

void require_executable_build_source_plan(const PackageBuildSource& source) {
    require_supported_build_source_install_target(source);
    if(!source.is_aur) return;

    // POLICY(#99): build/source-build は makepkg -i まで進む実行系なので、
    // clone/fetch/build/install 前に unresolved / ambiguous / cyclic / split target を拒否する。
    BuildPlan plan = resolve_build_plan(source.requested_name);
    require_executable_install_plan(source.requested_name, plan);
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

} // namespace

void build_source_target(
        const std::string& package_name,
        const std::string& custom_environment,
        const AppConfig& config) {
    PackageBuildSource source = resolve_build_source(package_name);
    require_executable_build_source_plan(source);
    // build コマンドは常にビルドする (only_if_updated = false)
    SourceBuildRequest request;
    request.package_name = source.requested_name;
    request.checkout_name = source.clone_name;
    request.git_url = source.git_url;
    request.custom_environment = custom_environment;
    execute_source_build(request, config);
}

void require_executable_source_install_target(
        const std::string& package_name) {
    if(is_repo_package(package_name)) {
        PackageBuildSource source = resolve_build_source(package_name);
        require_executable_build_source_plan(source);
        return;
    }

    BuildPlan plan = resolve_build_plan(package_name);
    require_executable_install_plan(package_name, plan);
}

void execute_aur_build_plan(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed,
        const AppConfig& config) {
    for(const auto& entry : plan.order) {
        std::string package_names = join_comma_display_values(entry.package_names);
        Logger::info("Building AUR PackageBase: " + entry.package_base);
        Logger::info("Target package(s): " + package_names);

        std::string package_name =
                entry.package_names.empty() ? entry.package_base : entry.package_names.front();
        std::string environment;
        if(use_source_build_preferences) {
            environment = load_source_preference_environment(package_name);
            if(environment.empty() && package_name != entry.package_base) {
                environment = load_source_preference_environment(entry.package_base);
            }
        }

        try {
            SourceBuildRequest request;
            request.package_name = package_name;
            request.checkout_name = entry.package_base;
            request.git_url = aur_git_url_for_package_base(entry.package_base);
            request.custom_environment = environment;
            request.needed = needed;
            execute_source_build(request, config);
        } catch(const std::exception& e) {
            throw std::runtime_error(
                    "Failed while building/installing PackageBase " + entry.package_base + " (" + package_names +
                    "): " + e.what());
        }
    }
}

void install_smart_source(
        const std::string& package_name,
        bool only_if_updated,
        bool needed,
        const AppConfig& config,
        const std::optional<SourceUpdateBaseline>& update_baseline,
        const std::optional<SourceInstalledSnapshot>& installed_snapshot) {
    if(only_if_updated && !installed_snapshot.has_value()) {
        throw std::runtime_error(
                "Authoritative installed package snapshot was not supplied for " +
                package_name + ".");
    }

    std::string environment = load_source_preference_environment(package_name);
    PackageBuildSource source = resolve_build_source(package_name);
    require_executable_build_source_plan(source);

    SourceBuildRequest request;
    request.package_name = source.requested_name;
    request.checkout_name = source.clone_name;
    request.git_url = source.git_url;
    request.custom_environment = environment;
    request.only_if_updated = only_if_updated;
    request.needed = needed;
    // POLICY(#215): system transactionによるbinary置換baselineはofficial sourceだけに適用する。
    if(!source.is_aur) request.update_baseline = update_baseline;
    // POLICY(#152): post-Syu installed stateはsource種別に関係なく同じ更新判定へ渡す。
    request.installed_snapshot = installed_snapshot;
    execute_source_build(request, config);
}

void preflight_upgrade_source_metadata() {
    if(!fs::exists(source_preference_root())) return;

    // POLICY(#174): upgradeの既存pacman-first実行は維持するが、schema violationだけは
    // system transactionより前に全source packageのplanを横断して拒否する。
    for(const auto& entry : source_preference_entries()) {
        if(!entry.is_regular_file()) continue;

        std::string package_name = entry.path().filename().string();
        if(!is_valid_package_name(package_name)) continue;
        try {
            PackageBuildSource source = resolve_build_source(package_name);
            if(source.is_aur) {
                // LANDMINE(#174): split/install guardより先にplan全体のschemaを検証する。
                // preflightでは実行可能性を判定せず、ordinary plan errorは実行phaseへ委ねる。
                static_cast<void>(resolve_build_plan(source.requested_name));
            }
        } catch(const AurRpcResponseError&) {
            throw;
        } catch(const std::exception&) {
            // not-found/transport/通常plan errorは、従来どおりsystem upgrade後の実行phaseで報告する。
        }
    }
}
