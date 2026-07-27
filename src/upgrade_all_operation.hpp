#pragma once

#include "filtered_aur_update_operation.hpp"
#include "package_metadata.hpp"
#include "system_source_upgrade.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct AppConfig;

enum class UpgradeAllOperationPhase {
    None,
    Preparation,
    System,
    RegisteredSource,
    ForeignInventory,
    AurQuery,
    AurPreparation,
    AurExecution,
    Reduction,
};

enum class UpgradeAllOperationStatus {
    Completed,
    NoUpdates,
    BlockedBeforeMutation,
    StoppedOnSystemFailure,
    StoppedOnSourceFailure,
    StoppedAfterSourceCleanupFailure,
    StoppedBeforeAurExecution,
    StoppedOnAurFailure,
    StoppedAfterAurCleanupFailure,
    InconsistentResult,
};

enum class UpgradeAllForeignInventoryPhaseStatus {
    NotAttempted,
    Completed,
    Failed,
};

enum class UpgradeAllAurPhaseStatus {
    NotAttempted,
    NoUpdates,
    Completed,
    BlockedBeforeExecution,
    StoppedOnWorkItemFailure,
    StoppedAfterCleanupFailure,
    InconsistentResult,
};

enum class UpgradeAllNotAttemptedReason {
    PreparationBlocked,
    SystemFailure,
    SourceFailure,
    SourceCleanupFailure,
    SystemSourceIncomplete,
    ForeignInventoryFailure,
    PriorAggregateInconsistency,
};

enum class UpgradeAllExplicitSourceAdapterIssueKind {
    PreferencePackageNameMissing,
    PackageBaseUnavailable,
    CanonicalSourceIdentityUnavailable,
    DuplicateOriginalPreferenceIndex,
    AdapterCorrelationInconsistent,
};

struct UpgradeAllExplicitSourceAdapterIssue {
    UpgradeAllExplicitSourceAdapterIssueKind kind =
            UpgradeAllExplicitSourceAdapterIssueKind::
                    AdapterCorrelationInconsistent;
    std::optional<std::size_t> adapter_index;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> preference_package_name;
    std::string diagnostic;
};

// PR2のprepared source intentとPR1 plannerのdense indexを同じrecordへ固定する。
// original_preference_indexはdirectory snapshot上の位置であり、adapter_indexとは
// 一致するとは限らない。
struct UpgradeAllExplicitSourceAdapterEntry {
    std::size_t adapter_index = 0;
    std::size_t original_preference_index = 0;
    std::string preference_package_name;
    std::optional<std::string> canonical_source_identity_key;
    std::optional<std::string> resolved_package_base;
    std::vector<std::string> affected_package_names;
    UpgradeAllExplicitSourceIdentity planner_identity;
};

struct UpgradeAllExplicitSourceIdentityAdapter {
    std::vector<UpgradeAllExplicitSourceAdapterEntry> entries;
    std::vector<UpgradeAllExplicitSourceAdapterIssue> issues;

    bool is_valid() const noexcept;
    std::vector<UpgradeAllExplicitSourceIdentity> planner_identities() const;
};

UpgradeAllExplicitSourceIdentityAdapter
adapt_prepared_source_identities_for_upgrade_all(
        const SystemSourceUpgradePreparedSnapshot& source_snapshot);

enum class UpgradeAllOperationWarningKind {
    RegisteredSourcePreference,
    AurPreparation,
};

struct UpgradeAllOperationWarning {
    UpgradeAllOperationWarningKind kind =
            UpgradeAllOperationWarningKind::RegisteredSourcePreference;
    UpgradeAllOperationPhase phase = UpgradeAllOperationPhase::Preparation;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> package_name;
    std::string diagnostic;
};

enum class UpgradeAllOperationIssueKind {
    ExplicitSourceAdapterInvalid,
    OptionSnapshotMismatch,
    SourceSnapshotMismatch,
    ExplicitSourceCorrelationInconsistent,
    PreparedCapabilityConsumed,
    SystemSourceExecutionFailedUnexpectedly,
    SystemSourcePhaseIncomplete,
    ForeignInventoryConfigurationFailed,
    ForeignInventoryReadFailed,
    AurQueryFailed,
    FilteredAurPreparationFailed,
    FilteredAurExecutionFailed,
    DuplicateExclusionCorrelationInconsistent,
    ExternalSatisfactionCorrelationInconsistent,
    UnknownFailure,
};

struct UpgradeAllOperationIssue {
    UpgradeAllOperationIssueKind kind =
            UpgradeAllOperationIssueKind::UnknownFailure;
    UpgradeAllOperationPhase phase = UpgradeAllOperationPhase::Preparation;
    std::optional<std::size_t> adapter_index;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::size_t> original_query_plan_index;
    std::optional<std::size_t> build_plan_order_index;
    std::optional<std::string> package_name;
    std::optional<PackageMetadataFailure> package_metadata_failure;
    std::string diagnostic;
};

struct UpgradeAllOperationDiagnostic {
    UpgradeAllOperationPhase phase = UpgradeAllOperationPhase::Preparation;
    bool stops_execution = false;
    std::string diagnostic;
};

struct UpgradeAllOperationPreparedSnapshot {
    SystemSourceUpgradePreparedSnapshot system_source;
    UpgradeAllExplicitSourceIdentityAdapter explicit_source_adapter;
    std::vector<UpgradeAllOperationWarning> warnings;
};

struct UpgradeAllForeignInventoryPhaseResult {
    UpgradeAllForeignInventoryPhaseStatus status =
            UpgradeAllForeignInventoryPhaseStatus::NotAttempted;
    std::optional<UpgradeAllNotAttemptedReason> not_attempted_reason;
    std::optional<PacmanRepositoryConfiguration> repository_configuration;
    ForeignPackageInventory inventory;
    std::optional<PackageMetadataFailure> failure;
    std::optional<std::string> diagnostic;
};

struct UpgradeAllAurPhaseResult {
    UpgradeAllAurPhaseStatus status =
            UpgradeAllAurPhaseStatus::NotAttempted;
    std::optional<UpgradeAllNotAttemptedReason> not_attempted_reason;
    std::optional<FilteredAurUpdateExecutionResult> operation_result;
    std::optional<std::string> diagnostic;
};

// duplicate targetはplanner-local indexとoriginal query indexの両方を保持する。
struct UpgradeAllDuplicateExcludedAurTarget {
    std::size_t planner_target_index = 0;
    std::size_t original_query_plan_index = 0;
    UpgradeAllTargetPlanEntry planner_entry;
    AurUpdatePlanEntry query_entry;
};

// root-roleの正本はPR3 correlationであり、affected_rootsとrolesをzipしない。
struct UpgradeAllExternallySatisfiedAurBuildUnit {
    AurUpdateExternallySatisfiedBuildUnit operation_unit;
    std::vector<FilteredAurUpdateBuildUnitRootCorrelation> root_correlations;
};

struct UpgradeAllOperationResult {
    UpgradeAllOperationStatus status =
            UpgradeAllOperationStatus::InconsistentResult;
    UpgradeAllOperationPhase stopped_phase =
            UpgradeAllOperationPhase::Preparation;
    UpgradeAllOperationPreparedSnapshot prepared_snapshot;
    SystemSourceUpgradeResult system_source;
    UpgradeAllForeignInventoryPhaseResult foreign_inventory;
    UpgradeAllAurPhaseResult aur;
    std::vector<UpgradeAllDuplicateExcludedAurTarget>
            duplicate_excluded_aur_targets;
    std::vector<UpgradeAllExternallySatisfiedAurBuildUnit>
            externally_satisfied_aur_build_units;
    std::vector<UpgradeAllOperationWarning> warnings;
    std::vector<UpgradeAllOperationIssue> issues;
    std::vector<UpgradeAllOperationDiagnostic> diagnostics;

    bool is_success() const noexcept;
    PackageStateChange package_state_change() const noexcept;
    bool has_partial_completion() const noexcept;
    bool has_not_attempted_phase() const noexcept;
    bool has_cleanup_failure() const noexcept;
    bool has_query_failure() const noexcept;
    bool has_planning_issue() const noexcept;
    bool has_duplicate_exclusions() const noexcept;
    bool has_external_satisfaction() const noexcept;
    bool has_inconsistency() const noexcept;
};

class PreparedUpgradeAllOperation final {
    struct Impl;

    explicit PreparedUpgradeAllOperation(std::unique_ptr<Impl> impl) noexcept;

    friend struct UpgradeAllOperationPreparationAccess;
    friend UpgradeAllOperationResult execute_prepared_upgrade_all_operation(
            PreparedUpgradeAllOperation prepared,
            const AppConfig& config);

    std::unique_ptr<Impl> impl_;

public:
    PreparedUpgradeAllOperation(const PreparedUpgradeAllOperation&) = delete;
    PreparedUpgradeAllOperation& operator=(
            const PreparedUpgradeAllOperation&) = delete;
    PreparedUpgradeAllOperation(PreparedUpgradeAllOperation&&) noexcept;
    PreparedUpgradeAllOperation& operator=(PreparedUpgradeAllOperation&&) =
            delete;
    ~PreparedUpgradeAllOperation() noexcept;

    bool is_valid() const noexcept;
    const UpgradeAllOperationPreparedSnapshot* snapshot() const noexcept;

#ifdef JPACKER_ENABLE_UPGRADE_ALL_OPERATION_TEST_HOOKS
    void make_source_snapshot_inconsistent_for_test();
    void make_explicit_source_correlation_inconsistent_for_test();
    void make_nested_system_source_correlation_inconsistent_for_test();
#endif
};

// blocked resultとexecutable capabilityを同時に返さないsum type。
using UpgradeAllOperationPreparation = std::variant<
        PreparedUpgradeAllOperation,
        UpgradeAllOperationResult>;

UpgradeAllOperationPreparation prepare_upgrade_all_operation(
        const AppConfig& config);

// by-value consumeによりouterとnested capabilityを最初のsystem mutation前に
// invalid化し、replayをtyped resultへ変換する。
UpgradeAllOperationResult execute_prepared_upgrade_all_operation(
        PreparedUpgradeAllOperation prepared,
        const AppConfig& config);
