#pragma once

#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "separated_package_base_source_build.hpp"
#include "source_environment.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct AppConfig;

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
};

struct SourceBuildUpdateStatusUnknownSkipped {
    SourceBuildUpdateStatusUnknownSkipReason reason =
            SourceBuildUpdateStatusUnknownSkipReason::NoConfirm;
    std::string diagnostic;
};

// generic source-buildの正常終了を、package transactionの有無まで潰さず返す。
// diagnosticはCLI出力の解析用ではなく、上位phaseがowned detailを保持するための値。
struct SourceBuildExecutionResult {
    SourceBuildExecutionStatus status =
            SourceBuildExecutionStatus::UpdateStatusUnknownSkipped;
    std::optional<SourceBuildUpdateStatusUnknownSkipReason>
            update_status_unknown_skip_reason;
    std::string diagnostic;
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
    bool        only_if_updated = false;
    bool        needed = false;
};

// checkout/update-check/reviewとprivate artifact rootを一度だけ通過した
// execution capability。raw pathはpreparation/executor ownerへ閉じる。
class PreparedSourceBuildNeedsBuild final {
    std::optional<ValidatedCachePath> checkout_;
    std::optional<ValidatedPrivateCacheRoot> artifact_root_;
    bool rebuild_ = false;
    bool clean_build_ = false;

    PreparedSourceBuildNeedsBuild(
            ValidatedCachePath checkout,
            ValidatedPrivateCacheRoot artifact_root,
            bool rebuild,
            bool clean_build) noexcept
        : checkout_(std::move(checkout)),
          artifact_root_(std::move(artifact_root)), rebuild_(rebuild),
          clean_build_(clean_build) {
    }

    PreparedSourceBuildNeedsBuild() noexcept = default;

    friend struct SourceBuildPreparationAccess;
    friend struct SourceBuildPreparedExecutionAccess;

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

// generic only-if-updated経路がinstall前に正常skipした場合だけnulloptを返す。
// artifact transaction成功後はpackage stateのtyped outcomeを返すlegacy wrapper。
std::optional<ArtifactInstallExecutionOutcome> execute_source_build(
        const SourceBuildRequest& request,
        const ValidatedCacheRoot& cache_root,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);
