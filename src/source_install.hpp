#pragma once

#include "artifact_install_plan.hpp"
#include "dependency_provider.hpp"
#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "repository_query.hpp"
#include "separated_package_base_source_build.hpp"
#include "source_build.hpp"
#include "trusted_cache.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
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

// PackageBaseだけを入力にcanonical key / official checkout URLを構成し、
// URL leafからPackageBaseを逆算させないclosed source identity。
class SourceCheckoutIdentity final {
public:
    [[nodiscard]] const std::string& package_base() const noexcept {
        return package_base_;
    }
    [[nodiscard]] const std::string& canonical_source_key() const noexcept {
        return canonical_source_key_;
    }
    [[nodiscard]] const std::string& git_url() const noexcept {
        return git_url_;
    }

    bool operator==(const SourceCheckoutIdentity&) const = default;

private:
    SourceCheckoutIdentity(
            SourceBuildSourceKind source_kind,
            std::string package_base)
        : package_base_(std::move(package_base)) {
        switch(source_kind) {
        case SourceBuildSourceKind::Repository:
            canonical_source_key_ = "repository:" + package_base_;
            git_url_ =
                    "https://gitlab.archlinux.org/archlinux/packaging/packages/" +
                    package_base_ + ".git";
            break;
        case SourceBuildSourceKind::Aur:
            canonical_source_key_ = "aur:" + package_base_;
            git_url_ = "https://aur.archlinux.org/" + package_base_ + ".git";
            break;
        }
    }

    std::string package_base_;
    std::string canonical_source_key_;
    std::string git_url_;

    friend class ResolvedRepositorySourceBuildIdentity;
    friend class ResolvedAurSourceBuildIdentity;
};

// strict repository exact observationと、それだけから導いたcheckout identityを
// 同じowned valueへ閉じ込める。requested childとPackageBaseは別fieldで保持する。
class ResolvedRepositorySourceBuildIdentity final {
public:
    explicit ResolvedRepositorySourceBuildIdentity(
            RepositoryPackagePresent exact_package)
        : exact_package_(std::move(exact_package)),
          checkout_(
                  SourceBuildSourceKind::Repository,
                  exact_package_.package_base) {
        if(exact_package_.repository_name.empty()) {
            throw std::invalid_argument(
                    "Repository source-build identity has no repository name.");
        }
        if(exact_package_.configured_repository_order.has_value()) {
            const auto& order =
                    exact_package_.configured_repository_order.value();
            if(exact_package_.configured_order >= order.size() ||
               order[exact_package_.configured_order] !=
                       exact_package_.repository_name) {
                throw std::invalid_argument(
                        "Repository source-build identity has inconsistent repository provenance.");
            }
        }
    }

    [[nodiscard]] const RepositoryPackagePresent& exact_package()
            const noexcept {
        return exact_package_;
    }
    [[nodiscard]] const std::string& requested_child() const noexcept {
        return exact_package_.package_name;
    }
    [[nodiscard]] const std::string& package_base() const noexcept {
        return exact_package_.package_base;
    }
    [[nodiscard]] const SourceCheckoutIdentity& checkout() const noexcept {
        return checkout_;
    }

    bool operator==(const ResolvedRepositorySourceBuildIdentity&) const =
            default;

private:
    RepositoryPackagePresent exact_package_;
    SourceCheckoutIdentity   checkout_;
};

class ResolvedAurSourceBuildIdentity final {
public:
    ResolvedAurSourceBuildIdentity(
            std::string requested_name,
            std::string package_base)
        : requested_name_(std::move(requested_name)),
          checkout_(SourceBuildSourceKind::Aur, std::move(package_base)) {}

    [[nodiscard]] const std::string& requested_name() const noexcept {
        return requested_name_;
    }
    [[nodiscard]] const SourceCheckoutIdentity& checkout() const noexcept {
        return checkout_;
    }
    [[nodiscard]] bool has_distinct_package_base() const noexcept {
        return requested_name_ != checkout_.package_base();
    }

    bool operator==(const ResolvedAurSourceBuildIdentity&) const = default;

private:
    std::string            requested_name_;
    SourceCheckoutIdentity checkout_;
};

// Resolver result自体もsource-specific alternativeへ閉じ、source kindと
// unrelated stringsをcallerが別々に組み立てる余地を持たせない。
class ResolvedSourceBuildIdentity final {
public:
    explicit ResolvedSourceBuildIdentity(
            ResolvedRepositorySourceBuildIdentity repository)
        : source_(std::move(repository)) {}
    explicit ResolvedSourceBuildIdentity(
            ResolvedAurSourceBuildIdentity aur)
        : source_(std::move(aur)) {}

    [[nodiscard]] SourceBuildSourceKind source_kind() const noexcept {
        return std::holds_alternative<ResolvedRepositorySourceBuildIdentity>(
                       source_)
                ? SourceBuildSourceKind::Repository
                : SourceBuildSourceKind::Aur;
    }
    [[nodiscard]] const std::string& requested_name() const noexcept {
        if(const auto* repository = repository_identity();
           repository != nullptr) {
            return repository->requested_child();
        }
        return std::get<ResolvedAurSourceBuildIdentity>(source_)
                .requested_name();
    }
    [[nodiscard]] const SourceCheckoutIdentity& checkout() const noexcept {
        if(const auto* repository = repository_identity();
           repository != nullptr) {
            return repository->checkout();
        }
        return std::get<ResolvedAurSourceBuildIdentity>(source_).checkout();
    }
    [[nodiscard]] const std::string& package_base() const noexcept {
        return checkout().package_base();
    }
    [[nodiscard]] const std::string& canonical_source_key() const noexcept {
        return checkout().canonical_source_key();
    }
    [[nodiscard]] const std::string& git_url() const noexcept {
        return checkout().git_url();
    }
    [[nodiscard]] bool has_distinct_package_base() const noexcept {
        if(const auto* aur =
                   std::get_if<ResolvedAurSourceBuildIdentity>(&source_);
           aur != nullptr) {
            return aur->has_distinct_package_base();
        }
        return requested_name() != package_base();
    }
    [[nodiscard]] const ResolvedRepositorySourceBuildIdentity*
    repository_identity() const noexcept {
        return std::get_if<ResolvedRepositorySourceBuildIdentity>(&source_);
    }

    bool operator==(const ResolvedSourceBuildIdentity&) const = default;

private:
    std::variant<
            ResolvedRepositorySourceBuildIdentity,
            ResolvedAurSourceBuildIdentity>
            source_;
};

enum class RequiredTargetProvenance {
    Unspecified,
    RepositoryExactPackageProjection,
    AurBuildPlanProjection,
};

enum class ArtifactLifecycleIntent {
    Unspecified,
    SingularCompatibility,
    PackageBaseSet,
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
    // required targetのauthorityとartifact execution selectorを別domainで保持する。
    RequiredTargetProvenance required_target_provenance =
            RequiredTargetProvenance::Unspecified;
    ArtifactLifecycleIntent artifact_lifecycle_intent =
            ArtifactLifecycleIntent::Unspecified;
    // repository projectionだけがstrict exact observationを保持する。
    std::optional<ResolvedRepositorySourceBuildIdentity>
            repository_identity = std::nullopt;
    bool                          uses_system_update_baseline = false;
    // dependency/provider resolutionが問い合わせたconfiguration snapshot。
    // nulloptはrepository authority未問い合わせを表す。
    std::optional<std::vector<std::string>> configured_repository_order =
            std::nullopt;
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

// Remote buildのroute identity、AUR plan（該当時）、cache未activateの
// production invocationを同じowned snapshotへ束ねる。dry-run projectionは
// このauthorityをborrowし、actual executionだけがcacheをactivateする。
struct PreparedRemoteSourceBuild {
    ResolvedSourceBuildIdentity source;
    std::optional<BuildPlan> aur_build_plan;
    PreparedProductionSourceBuildInvocation invocation;
};

struct RemoteSourceBuildPlanFailure {
    ResolvedSourceBuildIdentity source;
    BuildPlan plan;
};

using RemoteSourceBuildPreparation = std::variant<
        PreparedRemoteSourceBuild,
        RemoteSourceBuildPlanFailure>;

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

#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
// phase/upgrade-allのfake-symbol binaryは#268 lower lifecycleをlinkしない。
// production factoryを公開せず、registered orchestration seamのtyped ABIだけを
// layout非依存のdoubleで保持する。
class RegisteredSourcePackageBaseExecutionResultTestDouble final {
    std::string package_base_;
    std::vector<PackageBaseSourceBuildSelectedResult> selected_children_;
    std::vector<ArtifactPackageIdentity> unselected_artifacts_;

public:
    RegisteredSourcePackageBaseExecutionResultTestDouble(
            std::string package_base,
            std::vector<PackageBaseSourceBuildSelectedResult>
                    selected_children,
            std::vector<ArtifactPackageIdentity> unselected_artifacts)
        : package_base_(std::move(package_base)),
          selected_children_(std::move(selected_children)),
          unselected_artifacts_(std::move(unselected_artifacts)) {
    }

    const std::string& package_base() const noexcept {
        return package_base_;
    }
    const std::vector<PackageBaseSourceBuildSelectedResult>&
    selected_children() const noexcept {
        return selected_children_;
    }
    const std::vector<ArtifactPackageIdentity>&
    unselected_artifacts() const noexcept {
        return unselected_artifacts_;
    }
};

class RegisteredSourcePackageBaseCleanupErrorTestDouble final
    : public std::runtime_error {
    RegisteredSourcePackageBaseExecutionResultTestDouble result_;

public:
    RegisteredSourcePackageBaseCleanupErrorTestDouble(
            RegisteredSourcePackageBaseExecutionResultTestDouble result,
            const std::string& diagnostic)
        : std::runtime_error(diagnostic), result_(std::move(result)) {
    }

    const RegisteredSourcePackageBaseExecutionResultTestDouble& result()
            const noexcept {
        return result_;
    }
};

class RegisteredSourcePackageBasePreparationErrorTestDouble final
    : public std::runtime_error {
    std::variant<
            PackageBaseArtifactIdentitySelectionFailure,
            MixedPackageBaseInstallReasonUnsupported>
            failure_;

public:
    RegisteredSourcePackageBasePreparationErrorTestDouble(
            PackageBaseArtifactIdentitySelectionFailure failure,
            const std::string& diagnostic)
        : std::runtime_error(diagnostic), failure_(std::move(failure)) {
    }
    RegisteredSourcePackageBasePreparationErrorTestDouble(
            MixedPackageBaseInstallReasonUnsupported failure,
            const std::string& diagnostic)
        : std::runtime_error(diagnostic), failure_(std::move(failure)) {
    }

    const PackageBaseArtifactIdentitySelectionFailure* selection_failure()
            const noexcept {
        return std::get_if<
                PackageBaseArtifactIdentitySelectionFailure>(&failure_);
    }
    const MixedPackageBaseInstallReasonUnsupported* mixed_reason_failure()
            const noexcept {
        return std::get_if<
                MixedPackageBaseInstallReasonUnsupported>(&failure_);
    }
};

class RegisteredSourcePackageBasePhaseErrorTestDouble final
    : public std::runtime_error {
    SeparatedPackageBaseSourceBuildFailurePhase phase_;

public:
    RegisteredSourcePackageBasePhaseErrorTestDouble(
            SeparatedPackageBaseSourceBuildFailurePhase phase,
            const std::string& diagnostic)
        : std::runtime_error(diagnostic), phase_(phase) {
    }

    SeparatedPackageBaseSourceBuildFailurePhase phase() const noexcept {
        return phase_;
    }
};

class RegisteredSourcePackageTransactionErrorTestDouble final
    : public std::runtime_error {
    PackageBaseArtifactInstallTransactionFailureKind failure_kind_;
    std::string package_base_;
    std::vector<PackageBaseArtifactInstallTransactionAttempt> attempts_;
    std::optional<int> exit_code_;

public:
    RegisteredSourcePackageTransactionErrorTestDouble(
            PackageBaseArtifactInstallTransactionFailureKind failure_kind,
            std::string package_base,
            std::vector<PackageBaseArtifactInstallTransactionAttempt>
                    attempts,
            std::optional<int> exit_code,
            const std::string& diagnostic)
        : std::runtime_error(diagnostic), failure_kind_(failure_kind),
          package_base_(std::move(package_base)),
          attempts_(std::move(attempts)), exit_code_(exit_code) {
    }

    PackageBaseArtifactInstallTransactionFailureKind failure_kind()
            const noexcept {
        return failure_kind_;
    }
    const std::string& package_base() const noexcept {
        return package_base_;
    }
    const std::vector<PackageBaseArtifactInstallTransactionAttempt>&
    attempts() const noexcept {
        return attempts_;
    }
    const std::optional<int>& exit_code() const noexcept {
        return exit_code_;
    }
};

using RegisteredSourcePackageBaseExecutionResult =
        RegisteredSourcePackageBaseExecutionResultTestDouble;
using RegisteredSourcePackageBaseCleanupError =
        RegisteredSourcePackageBaseCleanupErrorTestDouble;
using RegisteredSourcePackageBasePreparationError =
        RegisteredSourcePackageBasePreparationErrorTestDouble;
using RegisteredSourcePackageBasePhaseError =
        RegisteredSourcePackageBasePhaseErrorTestDouble;
using RegisteredSourcePackageTransactionError =
        RegisteredSourcePackageTransactionErrorTestDouble;
#else
using RegisteredSourcePackageBaseExecutionResult =
        PackageBaseSourceBuildExecutionResult;
using RegisteredSourcePackageBaseCleanupError =
        SeparatedPackageBaseSourceBuildCleanupError;
using RegisteredSourcePackageBasePreparationError =
        SeparatedPackageBaseSourceBuildPreparationError;
using RegisteredSourcePackageBasePhaseError =
        SeparatedPackageBaseSourceBuildPhaseError;
using RegisteredSourcePackageTransactionError =
        PackageBaseArtifactInstallTransactionError;
#endif

// checkoutやmetadata queryより前に確認できるwork item単体のstatic契約。
void require_static_production_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item);

// generic/sync/registered source routeが所有するsingular compatibility境界。
// standalone repository routeはPackageBaseSetを使い、multipleを先頭へ潰さない。
const RequiredPackageArtifactTarget& require_singular_required_package_target(
        const ProductionSourceBuildWorkItem& work_item);

void require_supported_production_source_build_options(
        const AppConfig& config);

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config);

RemoteSourceBuildPreparation prepare_remote_source_build(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config);

ResolvedSourceBuildIdentity resolve_source_build_identity(
        const std::string& package_name);

ResolvedSourceBuildIdentity make_repository_source_build_identity(
        const RepositoryPackagePresent& package);

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

// registered system/source routeだけがsource kindに応じたlifecycle policyを
// 選ぶ。shared standalone/sync factoryの契約は変更しない。
ProductionSourceBuildWorkItem prepare_registered_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
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

// invocation全体で選択済みのofficial providerをinstalled reason authorityで
// preflightし、1回のexact pacman transactionへ渡す。新規/Dependencyは
// Dependency、既存ExplicitはExplicitを維持し、混在時はmutation前に停止する。
SelectedRepositoryProviderTransactionResult
execute_selected_repository_provider_transaction(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config);

// source-neutralなPackageBase set execution owner。required_targetsをauthorityにし、
// child別outcomeとunselected artifact identityをflattenせず返す。
PackageBaseSourceBuildExecutionResult
execute_prepared_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);

SourceBuildPreparationOutcome
prepare_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        SourceBuildUpdatePolicy update_policy,
        const AppConfig& config);

RegisteredSourcePackageBaseExecutionResult
execute_prepared_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        PreparedSourceBuildNeedsBuild prepared,
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

// lifecycle intentがSetならsource-neutral set owner、それ以外はroute ownerが
// 検証済みのsingular compatibilityへroutingする。DB snapshotを再queryしない。
void execute_prepared_source_build_invocation(
        PreparedProductionSourceBuildInvocation invocation,
        const AppConfig& config);
