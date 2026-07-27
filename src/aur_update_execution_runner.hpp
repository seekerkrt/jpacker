#pragma once

#include "aur_update_execution_preparation.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct AppConfig;

enum class AurUpdateWorkItemExecutionStatus {
    Updated,
    NoChange,
    Failed,
    UpdatedCleanupFailed,
    NoChangeCleanupFailed,
    NotAttempted,
};

enum class AurUpdateWorkItemFailureKind {
    None,
    BuildOrInstallFailed,
    CleanupFailedAfterPackageTransaction,
    UnknownException,
    PriorWorkItemStopped,
};

enum class AurUpdateInvocationExecutionStatus {
    Completed,
    StoppedOnWorkItemFailure,
    StoppedAfterPackageCleanupFailure,
};

// 1 PackageBaseのexecution outcomeと、次段のtarget-level reducerに必要な
// update target/root attributionをowned snapshotとして保持する。
struct AurUpdateWorkItemExecutionResult {
    std::size_t              work_item_index = 0;
    std::size_t              build_plan_order_index = 0;
    std::string              package_name;
    std::string              package_base;
    std::vector<std::string> plan_package_names;

    std::vector<std::size_t>        affected_update_plan_indices;
    std::vector<RootTargetIdentity> affected_roots;

    AurUpdateWorkItemExecutionStatus status =
            AurUpdateWorkItemExecutionStatus::NotAttempted;
    AurUpdateWorkItemFailureKind failure_kind =
            AurUpdateWorkItemFailureKind::PriorWorkItemStopped;
    std::optional<std::string> diagnostic;
};

struct AurUpdateSourceBuildExecutionResult {
    AurUpdateInvocationExecutionStatus status =
            AurUpdateInvocationExecutionStatus::Completed;
    std::vector<AurUpdateWorkItemExecutionResult> work_item_results;

    bool is_success() const noexcept;
    bool changed_package_state() const noexcept;
    bool has_not_attempted_items() const noexcept;
    bool has_cleanup_failure() const noexcept;
    std::optional<std::size_t> stopped_work_item_index() const noexcept;
};

// Correlated preparation snapshotをone-shot capabilityとしてconsumeし、逐次実行する。
// preflight、BuildPlan、source preference、Pacman DBは再queryせず、最初のfailureで
// 後続をNotAttemptedのまま返す。
AurUpdateSourceBuildExecutionResult
execute_prepared_aur_update_source_build_invocation(
        PreparedAurUpdateSourceBuildInvocation invocation,
        const AppConfig& config);
