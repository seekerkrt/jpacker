#pragma once

#include "artifact_install_plan.hpp"
#include "dependency_provider.hpp"
#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "separated_package_base_source_build.hpp"
#include "source_build.hpp"
#include "trusted_cache.hpp"

#include <optional>
#include <string>
#include <vector>

struct AppConfig;
class LocalBuildPlan;
class LocalSourceBuildDependencyPreparation;
struct PreparedProductionSourceBuildInvocation;

PreparedProductionSourceBuildInvocation
prepare_local_source_build_dependency_invocation(
        LocalSourceBuildDependencyPreparation preparation,
        const ValidatedCacheRoot& cache_root,
        const AppConfig& config);

// Empty remote invocationをgeneric callerが捏造して通さないためのauthority。
// local rootを別ownerが保持するpreparationだけが生成できる。
class LocalSourceBuildInvocationAuthority final {
    LocalSourceBuildInvocationAuthority() noexcept = default;

    friend PreparedProductionSourceBuildInvocation
    prepare_local_source_build_dependency_invocation(
            LocalSourceBuildDependencyPreparation preparation,
            const ValidatedCacheRoot& cache_root,
            const AppConfig& config);

public:
    LocalSourceBuildInvocationAuthority(
            const LocalSourceBuildInvocationAuthority&) = default;
    LocalSourceBuildInvocationAuthority(
            LocalSourceBuildInvocationAuthority&&) noexcept = default;
    LocalSourceBuildInvocationAuthority& operator=(
            const LocalSourceBuildInvocationAuthority&) = default;
    LocalSourceBuildInvocationAuthority& operator=(
            LocalSourceBuildInvocationAuthority&&) noexcept = default;
    ~LocalSourceBuildInvocationAuthority() = default;
};

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
    // 利用者が選択したofficial providerを、対応するAUR build unitの
    // execution前dependency transactionまでtyped identityのまま保持する。
    std::vector<ProvidedDependency> selected_repository_providers;
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
    std::vector<ProvidedDependency>             selected_repository_providers;
    PacmanDatabasePaths                        database_paths;
    std::optional<ValidatedCacheRoot>          cache_root;
    std::optional<LocalSourceBuildInvocationAuthority>
            local_source_authority = std::nullopt;
};

// LocalBuildPlanのlocal root unitをexecution consumerへ渡さず、remote AUR
// dependency unitと全edge由来のselected repository providerだけを保持する。
// Production callerはLocalBuildPlan projectionを経ずに生成できない。
class LocalSourceBuildDependencyPreparation final {
    std::vector<ProductionSourceBuildWorkItem> remote_work_items_;
    std::vector<ProvidedDependency> selected_repository_providers_;

    LocalSourceBuildDependencyPreparation(
            std::vector<ProductionSourceBuildWorkItem> remote_work_items,
            std::vector<ProvidedDependency> selected_repository_providers)
            noexcept;

    friend LocalSourceBuildDependencyPreparation
    prepare_local_source_build_dependencies(
            const LocalBuildPlan& plan,
            bool use_source_build_preferences,
            bool needed);
    friend PreparedProductionSourceBuildInvocation
    prepare_local_source_build_dependency_invocation(
            LocalSourceBuildDependencyPreparation preparation,
            const ValidatedCacheRoot& cache_root,
            const AppConfig& config);

public:
    LocalSourceBuildDependencyPreparation(
            const LocalSourceBuildDependencyPreparation&) = delete;
    LocalSourceBuildDependencyPreparation(
            LocalSourceBuildDependencyPreparation&&) noexcept = default;
    LocalSourceBuildDependencyPreparation& operator=(
            const LocalSourceBuildDependencyPreparation&) = delete;
    LocalSourceBuildDependencyPreparation& operator=(
            LocalSourceBuildDependencyPreparation&&) noexcept = default;
    ~LocalSourceBuildDependencyPreparation() = default;

    const std::vector<ProductionSourceBuildWorkItem>& remote_work_items()
            const noexcept;
    const std::vector<ProvidedDependency>& selected_repository_providers()
            const noexcept;

#ifdef MOGUET_ENABLE_TEST_OVERRIDES
    static LocalSourceBuildDependencyPreparation
    make_for_production_source_build_test(
            std::vector<ProductionSourceBuildWorkItem> remote_work_items,
            std::vector<ProvidedDependency> selected_repository_providers);
#endif
};

enum class SelectedRepositoryProviderTransactionStatus {
    NotRequired,
    BlockedBeforeExecution,
    Succeeded,
    Failed,
};

// provider transactionをsource work-itemへ誤帰属させず、selected identityと
// package-stateの断言可能範囲をinvocation消費後もowned snapshotとして残す。
struct SelectedRepositoryProviderTransactionResult {
    SelectedRepositoryProviderTransactionStatus status =
            SelectedRepositoryProviderTransactionStatus::NotRequired;
    std::vector<ProvidedDependency> selected_providers;
    PackageStateChange package_state_change = PackageStateChange::NoChange;
    std::optional<int> command_exit_status;
    std::optional<std::string> diagnostic;

    bool is_success() const noexcept {
        switch(status) {
        case SelectedRepositoryProviderTransactionStatus::NotRequired:
        case SelectedRepositoryProviderTransactionStatus::Succeeded:
            return true;
        case SelectedRepositoryProviderTransactionStatus::
                BlockedBeforeExecution:
        case SelectedRepositoryProviderTransactionStatus::Failed:
            return false;
        }
        return false;
    }
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
ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed,
        const ProviderSelectionCallback& select_provider);

ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed);
ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed,
        const ProviderSelectionCallback& select_provider);

std::vector<ProductionSourceBuildWorkItem> prepare_aur_source_build_work_items(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed);

LocalSourceBuildDependencyPreparation
prepare_local_source_build_dependencies(
        const LocalBuildPlan& plan,
        bool use_source_build_preferences,
        bool needed);

PreparedProductionSourceBuildInvocation prepare_production_source_build_invocation(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        const AppConfig& config);

// Generic production preparationのnonempty契約は維持し、local root ownerが
// 存在するこの境界だけremote AUR dependency 0件を許可する。
PreparedProductionSourceBuildInvocation
prepare_local_source_build_dependency_invocation(
        LocalSourceBuildDependencyPreparation preparation,
        const ValidatedCacheRoot& cache_root,
        const AppConfig& config);
PreparedProductionSourceBuildInvocation
prepare_local_source_build_dependency_invocation(
        const LocalBuildPlan& plan,
        bool use_source_build_preferences,
        bool needed,
        const ValidatedCacheRoot& cache_root,
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

// invocation全体で選択済みのofficial providerを1回のexact pacman
// dependency transactionへ渡す。各source executorより前にphase ownerが呼ぶ。
SelectedRepositoryProviderTransactionResult
execute_selected_repository_provider_transaction(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config);

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
