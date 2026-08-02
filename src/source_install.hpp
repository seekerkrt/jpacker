#pragma once

#include "artifact_install_plan.hpp"
#include "package_metadata.hpp"
#include "separated_package_base_source_build.hpp"
#include "source_build.hpp"
#include "trusted_cache.hpp"

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
    // AUR BuildPlanが確定したchild setをPackageBase ownerへ渡す。
    // falseはofficial/generic/registered-source singular compatibility境界。
    bool                          is_build_plan_entry = false;
    bool                          uses_system_update_baseline = false;
    // 1 invocationでadoptした同一cache-root capabilityを全build unitが共有する。
    // static model生成中はemptyで、production invocation preparationだけが設定する。
    std::optional<ValidatedCacheRoot> cache_root;
};

// PacmanDatabasePathsはinvocationで1回だけ解決し、全build unitへvalueとして共有する。
struct PreparedProductionSourceBuildInvocation {
    std::vector<ProductionSourceBuildWorkItem> work_items;
    PacmanDatabasePaths                        database_paths;
    std::optional<ValidatedCacheRoot>          cache_root;
};

// checkoutやmetadata queryより前に確認できるwork item単体のstatic契約。
void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item);

// official/generic/registered-source singular lifecycleのcompatibility境界。
// multipleを先頭要素へ潰さない。
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

// Higher-level operationが先に確定したcache capabilityを、generic invocation
// と全work itemへ同一snapshotとして配る。environmentは再読込しない。
void seed_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation,
        const ValidatedCacheRoot& cache_root);

// Execution ownerが最初のexternal mutationより前に1回だけ呼び、invocation内の
// 全work itemへ同じretained cache-root capabilityを配る。
void activate_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation);

// AUR PackageBase execution専用のset owner。required_targetsをauthorityにし、
// child別outcomeとunselected artifact identityをflattenせず返す。
PackageBaseSourceBuildExecutionResult
execute_prepared_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
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

// AUR BuildPlan work itemはPackageBase set owner、それ以外はlegacy
// singular ownerへroutingする。invocationのDB snapshotを再queryしない。
void execute_prepared_source_build_invocation(
        PreparedProductionSourceBuildInvocation invocation,
        const AppConfig& config);
