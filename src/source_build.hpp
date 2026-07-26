#pragma once

#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "source_environment.hpp"

#include <optional>
#include <string>

struct AppConfig;
enum class ArtifactInstallExecutionOutcome;

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

SourceBuildExecutionResult execute_source_build_typed(
        const SourceBuildRequest& request,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);

// generic only-if-updated経路がinstall前に正常skipした場合だけnulloptを返す。
// artifact transaction成功後はpackage stateのtyped outcomeを返すlegacy wrapper。
std::optional<ArtifactInstallExecutionOutcome> execute_source_build(
        const SourceBuildRequest& request,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);
