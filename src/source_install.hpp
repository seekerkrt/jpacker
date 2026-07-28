#pragma once

#include "artifact_install_plan.hpp"
#include "package_metadata.hpp"
#include "source_build.hpp"

#include <optional>
#include <string>
#include <vector>

struct AppConfig;
struct BuildPlan;

enum class SourceBuildSourceKind {
    Repository,
    Aur,
};

// requested packageと実際にcheckoutするPackageBase/sourceを結ぶowned identity。
// canonical_source_keyはrequested aliasに寄せず、source kind + PackageBaseで安定化する。
struct ResolvedSourceBuildIdentity {
    std::string           requested_name;
    std::string           package_base;
    std::string           canonical_source_key;
    std::string           git_url;
    SourceBuildSourceKind source_kind = SourceBuildSourceKind::Repository;
    bool                  has_distinct_package_base = false;
};

// production all-target preflightで確定し、mutation phaseまで保持する1 build unit。
// Artifact path/identity/install directiveはPR4 lifecycle内部でだけ生成する。
struct ProductionSourceBuildWorkItem {
    SourceBuildRequest             request;
    // POLICY(#268): PackageBase execution unitのinstall対象とreasonはこのvectorが正本。
    // singular executor用request.package_nameはsize 1の場合だけ設定する。
    std::vector<RequiredPackageArtifactTarget> required_targets;
    bool                          is_build_plan_entry = false;
    bool                          uses_system_update_baseline = false;
};

// PacmanDatabasePathsはinvocationで1回だけ解決し、全build unitへvalueとして共有する。
struct PreparedProductionSourceBuildInvocation {
    std::vector<ProductionSourceBuildWorkItem> work_items;
    PacmanDatabasePaths                        database_paths;
};

// checkoutやmetadata queryより前に確認できるwork item単体のstatic契約。
void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item);

// 現行single-artifact lifecycleのcompatibility境界。multipleを先頭要素へ潰さない。
const RequiredPackageArtifactTarget& require_singular_required_package_target(
        const ProductionSourceBuildWorkItem& work_item);

void require_supported_production_source_build_options(
        const AppConfig& config);

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config);

ResolvedSourceBuildIdentity resolve_source_build_identity(
        const std::string& package_name);

// strict reader等で既にowned化したenvironmentを再readせずwork itemに射影する。
ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed);

ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed);

std::vector<ProductionSourceBuildWorkItem> prepare_aur_source_build_work_items(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed);

PreparedProductionSourceBuildInvocation prepare_production_source_build_invocation(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        const AppConfig& config);

SourceBuildExecutionResult execute_prepared_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);

// nulloptはgeneric only-if-updatedの正常skip。update runner用work itemは
// only_if_updated=falseなので、artifact install outcomeを必ず要求できる。
std::optional<ArtifactInstallExecutionOutcome>
execute_prepared_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);

void execute_prepared_source_build_invocation(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config);
