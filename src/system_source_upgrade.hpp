#pragma once

#include "package_metadata.hpp"
#include "source_environment.hpp"
#include "source_preference.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct AppConfig;

// Installed package stateについて断言できる範囲を保持する。
// UnknownをNoChangeへ丸めず、strict boolean helperとは別に扱う。
enum class PackageStateChange {
    NoChange,
    Changed,
    Unknown,
};

enum class SystemSourceUpgradePhase {
    None,
    Preparation,
    System,
    RegisteredSource,
};

enum class SystemUpgradePhaseStatus {
    NotAttempted,
    Completed,
    Failed,
};

enum class RegisteredSourceUpgradeStatus {
    Updated,
    NoChange,
    Failed,
    UpdatedCleanupFailed,
    NoChangeCleanupFailed,
    NotAttempted,
    Unsupported,
    Incomplete,
};

enum class RegisteredSourceUpgradeFailureKind {
    None,
    InvalidPreferenceName,
    PreferenceUnavailable,
    PackageMetadataUnavailable,
    BuildOrInstallFailed,
    CleanupFailedAfterPackageTransaction,
    UpdateStatusUnknownSkipped,
    PriorPhaseStopped,
    UnknownException,
};

enum class SystemSourceUpgradeStatus {
    Completed,
    BlockedBeforeMutation,
    StoppedOnSystemFailure,
    StoppedOnSourceFailure,
    StoppedAfterSourceCleanupFailure,
    InconsistentResult,
};

enum class SystemSourceUpgradeIssueKind {
    PreferenceEnumerationUnavailable,
    PreferenceUnavailable,
    SourceIdentityResolutionFailed,
    SourceWorkItemPreparationFailed,
    SourceInvocationPreparationFailed,
    SourceBaselineSnapshotUnavailable,
    SystemPackageSnapshotUnavailable,
    PostSystemSourceSnapshotUnavailable,
    InvalidPreferenceName,
    OptionSnapshotMismatch,
    PreparedCorrelationInconsistent,
    PreparedCapabilityConsumed,
    UnknownPreparationFailure,
};

enum class SystemSourceUpgradeIssueImpact {
    ObservabilityOnly,
    AffectsSuccess,
    BlocksExecution,
};

enum class SystemSourceUpgradeWarningKind {
    SourcePreference,
    InvalidPreferenceName,
};

enum class SystemSourceUpgradeEventKind {
    LoadingSourcePreference,
    SourcePreferenceWarning,
    SystemUpgradeStarting,
    CheckingSourcePackages,
    InvalidPreferenceWarning,
};

#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
enum class SystemSourceUpgradeUnexpectedExceptionPoint {
    SystemPhaseStarted,
    SystemPhaseCompleted,
    SourceWorkItemStarted,
    SourceResultRecorded,
};
#endif

struct SystemSourceUpgradeOptionSnapshot {
    bool        no_edit = false;
    bool        no_diff = false;
    bool        no_confirm = false;
    bool        rebuild = false;
    bool        clean_build = false;
    bool        rm_deps = false;
    std::string editor;
};

// system mutation前に確定した利用者intentとsource identity。
// environmentはstrict readerが全byteを読めた場合だけ保持する。
struct RegisteredSourcePreferenceSnapshot {
    std::size_t           original_preference_index = 0;
    std::string           preference_package_name;
    std::filesystem::path entry_path;
    std::optional<SourceBuildEnvironment> environment;
    std::optional<std::string> canonical_source_identity_key;
    std::optional<std::string> resolved_package_base;
    std::vector<std::string> preference_load_warnings;
};

struct SystemSourceUpgradePreparedSnapshot {
    bool preference_root_exists = false;
    SystemSourceUpgradeOptionSnapshot options;
    std::vector<RegisteredSourcePreferenceSnapshot> registered_sources;
};

struct SystemSourceUpgradeIssue {
    SystemSourceUpgradeIssueKind kind =
            SystemSourceUpgradeIssueKind::UnknownPreparationFailure;
    SystemSourceUpgradeIssueImpact impact =
            SystemSourceUpgradeIssueImpact::BlocksExecution;
    SystemSourceUpgradePhase phase = SystemSourceUpgradePhase::Preparation;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> preference_package_name;
    std::optional<std::string> resolved_package_base;
    std::optional<SourcePreferenceFailure> source_preference_failure;
    std::optional<PackageMetadataFailure> package_metadata_failure;
    std::string diagnostic;
};

struct SystemSourceUpgradeWarning {
    SystemSourceUpgradeWarningKind kind =
            SystemSourceUpgradeWarningKind::SourcePreference;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> preference_package_name;
    std::optional<std::filesystem::path> entry_path;
    std::string diagnostic;
};

struct SystemSourceUpgradeDiagnostic {
    SystemSourceUpgradePhase phase = SystemSourceUpgradePhase::Preparation;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> preference_package_name;
    std::optional<std::string> resolved_package_base;
    bool stops_execution = false;
    std::string diagnostic;
};

struct SystemSourceUpgradeEvent {
    SystemSourceUpgradeEventKind kind =
            SystemSourceUpgradeEventKind::SystemUpgradeStarting;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::string> preference_package_name;
    std::optional<std::filesystem::path> entry_path;
    std::string diagnostic;
};

using SystemSourceUpgradeEventObserver =
        std::function<void(const SystemSourceUpgradeEvent& event)>;

struct SystemUpgradePhaseResult {
    SystemUpgradePhaseStatus status = SystemUpgradePhaseStatus::NotAttempted;
    PackageStateChange package_state_change = PackageStateChange::NoChange;
    std::optional<int> command_exit_status;
    std::optional<PackageMetadataFailure> before_snapshot_failure;
    std::optional<PackageMetadataFailure> after_snapshot_failure;
    std::optional<std::string> diagnostic;
};

struct RegisteredSourceUpgradeResult {
    std::size_t original_preference_index = 0;
    std::string preference_package_name;
    std::optional<std::string> canonical_source_identity_key;
    std::optional<std::string> resolved_package_base;
    RegisteredSourceUpgradeStatus status =
            RegisteredSourceUpgradeStatus::NotAttempted;
    RegisteredSourceUpgradeFailureKind failure_kind =
            RegisteredSourceUpgradeFailureKind::PriorPhaseStopped;
    PackageStateChange package_state_change = PackageStateChange::NoChange;
    std::optional<std::string> diagnostic;
    std::optional<std::string> cleanup_diagnostic;
};

struct SystemSourceUpgradeResult {
    SystemSourceUpgradeStatus status =
            SystemSourceUpgradeStatus::InconsistentResult;
    SystemSourceUpgradePhase stopped_phase = SystemSourceUpgradePhase::None;
    SystemSourceUpgradePreparedSnapshot prepared_snapshot;
    SystemUpgradePhaseResult system;
    std::vector<RegisteredSourceUpgradeResult> registered_source_results;
    std::vector<SystemSourceUpgradeWarning> warnings;
    std::vector<SystemSourceUpgradeIssue> issues;
    std::vector<SystemSourceUpgradeDiagnostic> diagnostics;

    bool is_success() const noexcept;
    PackageStateChange package_state_change() const noexcept;
    bool definitely_changed_package_state() const noexcept;
    bool has_partial_completion() const noexcept;
    bool has_not_attempted_sources() const noexcept;
    bool has_cleanup_failure() const noexcept;
    bool has_blocking_issue() const noexcept;
    std::optional<std::string> failure_diagnostic() const;
};

class PreparedSystemSourceUpgrade final {
    struct Impl;

    explicit PreparedSystemSourceUpgrade(std::unique_ptr<Impl> impl) noexcept;

    friend struct SystemSourceUpgradePreparationAccess;
    friend SystemSourceUpgradeResult execute_prepared_system_source_upgrade(
            PreparedSystemSourceUpgrade prepared,
            const AppConfig& config,
            const SystemSourceUpgradeEventObserver& observer);

    std::unique_ptr<Impl> impl_;

public:
    PreparedSystemSourceUpgrade(const PreparedSystemSourceUpgrade&) = delete;
    PreparedSystemSourceUpgrade& operator=(const PreparedSystemSourceUpgrade&) = delete;
    PreparedSystemSourceUpgrade(PreparedSystemSourceUpgrade&&) noexcept;
    PreparedSystemSourceUpgrade& operator=(PreparedSystemSourceUpgrade&&) = delete;
    ~PreparedSystemSourceUpgrade() noexcept;

    bool is_valid() const noexcept;
    const SystemSourceUpgradePreparedSnapshot* snapshot() const noexcept;

#ifdef MOGUET_ENABLE_SYSTEM_SOURCE_UPGRADE_TEST_HOOKS
    void make_first_source_correlation_inconsistent_for_test();
    void set_unexpected_exception_for_test(
            SystemSourceUpgradeUnexpectedExceptionPoint point,
            bool unknown_exception = false);
#endif
};

// blocked resultとexecutable capabilityを同時に返さないsum type。
using SystemSourceUpgradePreparation = std::variant<
        PreparedSystemSourceUpgrade,
        SystemSourceUpgradeResult>;

SystemSourceUpgradePreparation prepare_system_source_upgrade(
        const AppConfig& config,
        const SystemSourceUpgradeEventObserver& observer = {});

// by-value consumeにより、呼び出し元capabilityをmutation前にinvalid化する。
SystemSourceUpgradeResult execute_prepared_system_source_upgrade(
        PreparedSystemSourceUpgrade prepared,
        const AppConfig& config,
        const SystemSourceUpgradeEventObserver& observer = {});
