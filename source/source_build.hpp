#pragma once

#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "reviewed_source_production_failure.hpp"
#include "separated_package_base_source_build.hpp"
#include "source_environment.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct AppConfig;
class RemoteAurCleanupCandidateCollector;
class ReviewedSourceFatalStatePreflightSlot;

enum class SourceBuildExecutionStatus {
    Installed,
    SkippedAsNeeded,
    UpToDate,
    UpdateStatusUnknownSkipped,
};

enum class SourceBuildUpdateStatusUnknownSkipReason {
    NoConfirm,
    NonInteractiveStdin,
    UserDeclined,
};

enum class SourceBuildUpdatePolicy {
    AlwaysBuild,
    OnlyIfUpdated,
};

struct SourceBuildUpToDate {
    std::string diagnostic;
    std::optional<ProductionSourceBuildProvenance> source_provenance;
};

struct SourceBuildUpdateStatusUnknownSkipped {
    SourceBuildUpdateStatusUnknownSkipReason reason =
        SourceBuildUpdateStatusUnknownSkipReason::NoConfirm;
    std::string diagnostic;
    std::optional<ProductionSourceBuildProvenance> source_provenance;
};

// generic source-buildの正常終了を、package transactionの有無まで潰さず返す。
// diagnosticはCLI出力の解析用ではなく、上位phaseがowned detailを保持するための値。
struct SourceBuildExecutionResult {
    SourceBuildExecutionStatus status =
        SourceBuildExecutionStatus::UpdateStatusUnknownSkipped;
    std::optional<SourceBuildUpdateStatusUnknownSkipReason>
        update_status_unknown_skip_reason;
    std::string diagnostic;
    std::optional<ProductionSourceBuildStagedOutcome>
        production_outcome;
};

// upgrade baselineの有無と、snapshot時点の未installを別状態として保持する。
struct SourceUpdateBaseline {
    std::optional<std::string> installed_version;
};

// authoritative snapshotの有無はrequest側のoptionalで表し、観測済みの未installと分ける。
struct SourceInstalledSnapshot {
    std::optional<std::string> installed_version;
};

struct SourceBuildRequest {
    std::string package_name;
    std::string checkout_name;
    std::string git_url;
    SourceBuildEnvironment custom_environment;
    SourceEnvironmentEmptyValuePolicy empty_value_policy =
        SourceEnvironmentEmptyValuePolicy::Omit;
    std::optional<SourceUpdateBaseline> update_baseline;
    std::optional<SourceInstalledSnapshot> installed_snapshot;
    bool only_if_updated = false;
    bool needed = false;
    std::optional<PackageBaseIdentity> aur_review_identity;
    // Invocation preparation owns this read. Copies of a prepared work item
    // share one consumable snapshot rather than minting another observation.
    std::shared_ptr<ReviewedSourceFatalStatePreflightSlot>
        reviewed_state_preflight;
};

// AUR requests return one single-consumption slot; repository requests return
// nullptr. Fatal observations throw before the invocation can reach mutation.
[[nodiscard]] std::shared_ptr<ReviewedSourceFatalStatePreflightSlot>
preflight_reviewed_source_fatal_state_for_production(
    const SourceBuildRequest& request);

// checkout/update-check/reviewとprivate artifact rootを一度だけ通過した
// execution capability。raw pathはpreparation/executor ownerへ閉じる。
class PreparedSourceBuildNeedsBuild final {
    std::optional<ProductionArtifactSourceTree> source_tree_;
    std::optional<ValidatedPrivateCacheRoot> artifact_root_;
    bool rebuild_ = false;
    bool clean_build_ = false;

    PreparedSourceBuildNeedsBuild(
        ProductionArtifactSourceTree source_tree,
        ValidatedPrivateCacheRoot artifact_root,
        bool rebuild,
        bool clean_build) noexcept
        : source_tree_(std::move(source_tree)),
          artifact_root_(std::move(artifact_root)), rebuild_(rebuild),
          clean_build_(clean_build) {
    }

    PreparedSourceBuildNeedsBuild() noexcept = default;

    friend struct SourceBuildPreparationAccess;
    friend struct SourceBuildPreparedExecutionAccess;
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRODUCTION_TEST_HOOKS
    friend const ProductionSourceBuildProvenance&
    prepared_source_build_provenance_for_test(
        const PreparedSourceBuildNeedsBuild& prepared);
#endif

public:
    PreparedSourceBuildNeedsBuild(
        const PreparedSourceBuildNeedsBuild&) = delete;
    PreparedSourceBuildNeedsBuild& operator=(
        const PreparedSourceBuildNeedsBuild&) = delete;
    PreparedSourceBuildNeedsBuild(
        PreparedSourceBuildNeedsBuild&&) noexcept = default;
    PreparedSourceBuildNeedsBuild& operator=(
        PreparedSourceBuildNeedsBuild&&) = delete;
    ~PreparedSourceBuildNeedsBuild() = default;

#if defined(MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS) || \
    defined(MOGUET_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS)
    static PreparedSourceBuildNeedsBuild
    make_for_registered_source_build_test() noexcept {
        return PreparedSourceBuildNeedsBuild{};
    }
#endif
};

using SourceBuildPreparationOutcome = std::variant<
    SourceBuildUpToDate,
    SourceBuildUpdateStatusUnknownSkipped,
    PreparedSourceBuildNeedsBuild>;

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRODUCTION_TEST_HOOKS
const ProductionSourceBuildProvenance&
prepared_source_build_provenance_for_test(
    const PreparedSourceBuildNeedsBuild& prepared);

using ReviewedSourceBeforePublicationHookForTest = void (*)();
void set_reviewed_source_before_publication_hook_for_test(
    ReviewedSourceBeforePublicationHookForTest hook);
#endif

SourceBuildPreparationOutcome prepare_source_build_for_execution(
    const SourceBuildRequest& request,
    const std::string& display_name,
    SourceBuildUpdatePolicy update_policy,
    const ValidatedCacheRoot& cache_root,
    const AppConfig& config);

PackageBaseSourceBuildExecutionResult
execute_prepared_source_build_package_base_typed(
    const SourceBuildRequest& request,
    const std::vector<RequiredPackageArtifactTarget>& required_targets,
    PreparedSourceBuildNeedsBuild prepared,
    const PacmanDatabasePaths& database_paths,
    const AppConfig& config);

PackageBaseSourceBuildExecutionResult
execute_prepared_source_build_package_base_with_cleanup_authority(
    const SourceBuildRequest& request,
    const std::vector<RequiredPackageArtifactTarget>& required_targets,
    PreparedSourceBuildNeedsBuild prepared,
    const PacmanDatabasePaths& database_paths,
    const AppConfig& config,
    RemoteAurCleanupCandidateCollector& collector,
    std::size_t work_item_index);

SourceBuildExecutionResult execute_source_build_typed(
    const SourceBuildRequest& request,
    const ValidatedCacheRoot& cache_root,
    DesiredInstallReason desired_reason,
    const PacmanDatabasePaths& database_paths,
    const AppConfig& config);

// source-neutralなPackageBase execution。ordered required_targetsを一つのfresh
// workspace/transactionへ渡し、multipleではrequest.package_nameを使わない。
PackageBaseSourceBuildExecutionResult
execute_source_build_package_base_typed(
    const SourceBuildRequest& request,
    const std::vector<RequiredPackageArtifactTarget>& required_targets,
    const ValidatedCacheRoot& cache_root,
    const PacmanDatabasePaths& database_paths,
    const AppConfig& config);

PackageBaseSourceBuildExecutionResult
execute_source_build_package_base_with_cleanup_authority(
    const SourceBuildRequest& request,
    const std::vector<RequiredPackageArtifactTarget>& required_targets,
    const ValidatedCacheRoot& cache_root,
    const PacmanDatabasePaths& database_paths,
    const AppConfig& config,
    RemoteAurCleanupCandidateCollector& collector,
    std::size_t work_item_index);
