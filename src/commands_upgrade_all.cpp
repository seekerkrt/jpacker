#include "commands_upgrade_all.hpp"

#include "app_config.hpp"
#include "aur_update_cli_presentation.hpp"
#include "cli_authority.hpp"
#include "cli_parser.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "upgrade_all_operation.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view PACKAGE_BASE_FIELD = "PackageBase";
constexpr std::string_view PKGDEST_KEY = "PKGDEST";
constexpr std::string_view AUR_SERVICE = "AUR";
constexpr std::string_view COMMAND_NAME = "upgrade-all";
constexpr std::string_view PACMAN_COMMAND = "pacman";

std::string package_state_label(PackageStateChange state) {
    switch(state) {
    case PackageStateChange::NoChange:
        return localization::translate_message("no change");
    case PackageStateChange::Changed:
        return localization::translate_message("changed");
    case PackageStateChange::Unknown:
        return localization::translate_message("unknown");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown package-state change value."));
}

std::string aggregate_package_state_label(PackageStateChange state) {
    switch(state) {
    case PackageStateChange::NoChange:
        return localization::translate_message("no package state change");
    case PackageStateChange::Changed:
        return localization::translate_message("package state changed");
    case PackageStateChange::Unknown:
        return localization::translate_message("package state change unknown");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown aggregate package-state change value."));
}

std::string aggregate_status_label(UpgradeAllOperationStatus status) {
    switch(status) {
    case UpgradeAllOperationStatus::Completed:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message("{} completed",
                                                        COMMAND_NAME);
    case UpgradeAllOperationStatus::NoUpdates:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} completed: no updates", COMMAND_NAME);
    case UpgradeAllOperationStatus::BlockedBeforeMutation:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} blocked before mutation", COMMAND_NAME);
    case UpgradeAllOperationStatus::StoppedOnSystemFailure:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} stopped on system failure", COMMAND_NAME);
    case UpgradeAllOperationStatus::StoppedOnSourceFailure:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} stopped on source failure", COMMAND_NAME);
    case UpgradeAllOperationStatus::StoppedAfterSourceCleanupFailure:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} stopped after source cleanup failure", COMMAND_NAME);
    case UpgradeAllOperationStatus::StoppedBeforeAurExecution:
        // TRANSLATORS: The placeholders are the literal command name
        // "upgrade-all" and service name "AUR".
        return localization::format_translated_message(
                "{} stopped before {} execution", COMMAND_NAME, AUR_SERVICE);
    case UpgradeAllOperationStatus::StoppedOnAurFailure:
        // TRANSLATORS: The placeholders are the literal command name
        // "upgrade-all" and service name "AUR".
        return localization::format_translated_message(
                "{} stopped on {} failure", COMMAND_NAME, AUR_SERVICE);
    case UpgradeAllOperationStatus::StoppedAfterAurCleanupFailure:
        // TRANSLATORS: The placeholders are the literal command name
        // "upgrade-all" and service name "AUR".
        return localization::format_translated_message(
                "{} stopped after {} cleanup failure", COMMAND_NAME,
                AUR_SERVICE);
    case UpgradeAllOperationStatus::InconsistentResult:
        // TRANSLATORS: The placeholder is the literal command name
        // "upgrade-all".
        return localization::format_translated_message(
                "{} result inconsistent", COMMAND_NAME);
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} operation status.", COMMAND_NAME));
}

std::string aggregate_phase_label(UpgradeAllOperationPhase phase) {
    switch(phase) {
    case UpgradeAllOperationPhase::None:
        return localization::translate_message("none");
    case UpgradeAllOperationPhase::Preparation:
        return localization::translate_message("preparation");
    case UpgradeAllOperationPhase::System:
        return localization::translate_message("system");
    case UpgradeAllOperationPhase::RegisteredSource:
        return localization::translate_message("registered source");
    case UpgradeAllOperationPhase::ForeignInventory:
        return localization::translate_message("foreign inventory");
    case UpgradeAllOperationPhase::AurQuery:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("{} query", AUR_SERVICE);
    case UpgradeAllOperationPhase::AurPreparation:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("{} preparation",
                                                        AUR_SERVICE);
    case UpgradeAllOperationPhase::AurExecution:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("{} execution",
                                                        AUR_SERVICE);
    case UpgradeAllOperationPhase::Reduction:
        return localization::translate_message("reduction");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} operation phase.", COMMAND_NAME));
}

std::string not_attempted_reason_label(
        UpgradeAllNotAttemptedReason reason) {
    switch(reason) {
    case UpgradeAllNotAttemptedReason::PreparationBlocked:
        return localization::translate_message("preparation blocked");
    case UpgradeAllNotAttemptedReason::SystemFailure:
        return localization::translate_message("system failure");
    case UpgradeAllNotAttemptedReason::SourceFailure:
        return localization::translate_message("source failure");
    case UpgradeAllNotAttemptedReason::SourceCleanupFailure:
        return localization::translate_message("source cleanup failure");
    case UpgradeAllNotAttemptedReason::SystemSourceIncomplete:
        return localization::translate_message("system/source phase incomplete");
    case UpgradeAllNotAttemptedReason::ForeignInventoryFailure:
        return localization::translate_message("foreign inventory failure");
    case UpgradeAllNotAttemptedReason::CacheAuthorityFailure:
        return localization::translate_message("cache authority failure");
    case UpgradeAllNotAttemptedReason::PriorAggregateInconsistency:
        return localization::translate_message("prior aggregate inconsistency");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholders are the literal command name
            // "upgrade-all" and enum state "NotAttempted".
            "Unknown {} {} reason.", COMMAND_NAME, "NotAttempted"));
}

std::string aur_phase_status_label(UpgradeAllAurPhaseStatus status) {
    switch(status) {
    case UpgradeAllAurPhaseStatus::NotAttempted:
        return localization::translate_message("not attempted");
    case UpgradeAllAurPhaseStatus::NoUpdates:
        return localization::translate_message("no updates");
    case UpgradeAllAurPhaseStatus::Completed:
        return localization::translate_message("completed");
    case UpgradeAllAurPhaseStatus::BlockedBeforeExecution:
        return localization::translate_message("blocked before execution");
    case UpgradeAllAurPhaseStatus::StoppedOnProviderTransactionFailure:
        return localization::translate_message(
                "stopped after repository provider transaction failure");
    case UpgradeAllAurPhaseStatus::StoppedOnWorkItemFailure:
        return localization::translate_message("stopped on work-item failure");
    case UpgradeAllAurPhaseStatus::StoppedAfterCleanupFailure:
        return localization::translate_message("stopped after cleanup failure");
    case UpgradeAllAurPhaseStatus::InconsistentResult:
        return localization::translate_message("inconsistent result");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholders are the literal command name
            // "upgrade-all" and service name "AUR".
            "Unknown {} {} phase status.", COMMAND_NAME, AUR_SERVICE));
}

std::string system_phase_status_label(SystemUpgradePhaseStatus status) {
    switch(status) {
    case SystemUpgradePhaseStatus::NotAttempted:
        return localization::translate_message("not attempted");
    case SystemUpgradePhaseStatus::Completed:
        return localization::translate_message("completed");
    case SystemUpgradePhaseStatus::Failed:
        return localization::translate_message("failed");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system upgrade phase status."));
}

std::string source_failure_label(
        RegisteredSourceUpgradeFailureKind kind) {
    switch(kind) {
    case RegisteredSourceUpgradeFailureKind::None:
        return localization::translate_message("reason unavailable");
    case RegisteredSourceUpgradeFailureKind::InvalidPreferenceName:
        return localization::translate_message("invalid source preference name");
    case RegisteredSourceUpgradeFailureKind::PreferenceUnavailable:
        return localization::translate_message("source preference unavailable");
    case RegisteredSourceUpgradeFailureKind::PackageMetadataUnavailable:
        return localization::translate_message("package metadata unavailable");
    case RegisteredSourceUpgradeFailureKind::CacheAuthorityFailure:
        return localization::translate_message("cache authority failure");
    case RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed:
        return localization::translate_message("build or install failed");
    case RegisteredSourceUpgradeFailureKind::
            CleanupFailedAfterPackageTransaction:
        return localization::translate_message("cleanup failed after package transaction");
    case RegisteredSourceUpgradeFailureKind::UpdateStatusUnknownSkipped:
        return localization::translate_message("package update status unknown");
    case RegisteredSourceUpgradeFailureKind::PriorPhaseStopped:
        return localization::translate_message("prior phase stopped");
    case RegisteredSourceUpgradeFailureKind::UnknownException:
        return localization::translate_message("unknown exception");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown registered source failure kind."));
}

std::string source_diagnostic_or_reason(
        const RegisteredSourceUpgradeResult& source) {
    if(source.diagnostic.has_value() && !source.diagnostic->empty()) {
        return *source.diagnostic;
    }
    return std::string(source_failure_label(source.failure_kind));
}

std::string source_status_label(
        const RegisteredSourceUpgradeResult& source) {
    switch(source.status) {
    case RegisteredSourceUpgradeStatus::Updated:
        return localization::translate_message("updated");
    case RegisteredSourceUpgradeStatus::NoChange:
        return localization::translate_message("no change");
    case RegisteredSourceUpgradeStatus::Failed:
        return localization::format_translated_message(
                "failed: {}", source_diagnostic_or_reason(source));
    case RegisteredSourceUpgradeStatus::UpdatedCleanupFailed:
        return localization::translate_message("updated, but cleanup failed");
    case RegisteredSourceUpgradeStatus::NoChangeCleanupFailed:
        return localization::translate_message("no package change, but cleanup failed");
    case RegisteredSourceUpgradeStatus::NotAttempted:
        return localization::format_translated_message(
                "not attempted: {}", source_diagnostic_or_reason(source));
    case RegisteredSourceUpgradeStatus::Unsupported:
        return localization::format_translated_message(
                "unsupported: {}", source_diagnostic_or_reason(source));
    case RegisteredSourceUpgradeStatus::Incomplete:
        return localization::format_translated_message(
                "incomplete: {}", source_diagnostic_or_reason(source));
    }
    throw std::logic_error(localization::translate_message(
            "Unknown registered source status."));
}

std::string system_source_phase_label(SystemSourceUpgradePhase phase) {
    switch(phase) {
    case SystemSourceUpgradePhase::None:
        return localization::translate_message("none");
    case SystemSourceUpgradePhase::Preparation:
        return localization::translate_message("preparation");
    case SystemSourceUpgradePhase::System:
        return localization::translate_message("system");
    case SystemSourceUpgradePhase::RegisteredSource:
        return localization::translate_message("registered source");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system/source phase."));
}

std::string system_issue_kind_label(SystemSourceUpgradeIssueKind kind) {
    switch(kind) {
    case SystemSourceUpgradeIssueKind::PreferenceEnumerationUnavailable:
        return localization::translate_message("source preference enumeration unavailable");
    case SystemSourceUpgradeIssueKind::PreferenceUnavailable:
        return localization::translate_message("source preference unavailable");
    case SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed:
        return localization::translate_message("source identity resolution failed");
    case SystemSourceUpgradeIssueKind::SourceWorkItemPreparationFailed:
        return localization::translate_message("source work-item preparation failed");
    case SystemSourceUpgradeIssueKind::SourceInvocationPreparationFailed:
        return localization::translate_message("source invocation preparation failed");
    case SystemSourceUpgradeIssueKind::SourceBaselineSnapshotUnavailable:
        return localization::translate_message("source baseline snapshot unavailable");
    case SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable:
        return localization::translate_message("system package snapshot unavailable");
    case SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable:
        return localization::translate_message("post-system/source snapshot unavailable");
    case SystemSourceUpgradeIssueKind::CacheAuthorityInvalid:
        return localization::translate_message("cache authority invalid");
    case SystemSourceUpgradeIssueKind::InvalidPreferenceName:
        return localization::translate_message("invalid source preference name");
    case SystemSourceUpgradeIssueKind::OptionSnapshotMismatch:
        return localization::translate_message("option snapshot mismatch");
    case SystemSourceUpgradeIssueKind::PreparedCorrelationInconsistent:
        return localization::translate_message("prepared source correlation inconsistent");
    case SystemSourceUpgradeIssueKind::PreparedCapabilityConsumed:
        return localization::translate_message("prepared source capability consumed");
    case SystemSourceUpgradeIssueKind::UnknownPreparationFailure:
        return localization::translate_message("unknown source preparation failure");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system/source issue kind."));
}

std::string system_issue_impact_label(
        SystemSourceUpgradeIssueImpact impact) {
    switch(impact) {
    case SystemSourceUpgradeIssueImpact::ObservabilityOnly:
        return localization::translate_message("observability only");
    case SystemSourceUpgradeIssueImpact::AffectsSuccess:
        return localization::translate_message("affects success");
    case SystemSourceUpgradeIssueImpact::BlocksExecution:
        return localization::translate_message("blocks execution");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system/source issue impact."));
}

std::string system_warning_kind_label(
        SystemSourceUpgradeWarningKind kind) {
    switch(kind) {
    case SystemSourceUpgradeWarningKind::SourcePreference:
        return localization::translate_message("source preference");
    case SystemSourceUpgradeWarningKind::InvalidPreferenceName:
        return localization::translate_message("invalid source preference name");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown system/source warning kind."));
}

std::string package_metadata_error_label(PackageMetadataErrorCode code) {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationUnavailable:
        return localization::translate_message("configuration unavailable");
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return localization::translate_message("configuration malformed");
    case PackageMetadataErrorCode::InitializationFailed:
        return localization::translate_message("database initialization failed");
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        return localization::translate_message("local database unavailable");
    case PackageMetadataErrorCode::InvalidPackageName:
        return localization::translate_message("invalid package name");
    case PackageMetadataErrorCode::QueryFailed:
        return localization::translate_message("package query failed");
    case PackageMetadataErrorCode::MalformedMetadata:
        return localization::translate_message("malformed package metadata");
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return localization::translate_message("sync database unavailable");
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return localization::translate_message("repository not configured");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown package metadata error code."));
}

std::string aggregate_warning_kind_label(
        UpgradeAllOperationWarningKind kind) {
    switch(kind) {
    case UpgradeAllOperationWarningKind::RegisteredSourcePreference:
        return localization::translate_message("registered source preference");
    case UpgradeAllOperationWarningKind::AurPreparation:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("{} preparation",
                                                        AUR_SERVICE);
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} warning kind.", COMMAND_NAME));
}

std::string aggregate_issue_kind_label(
        UpgradeAllOperationIssueKind kind) {
    switch(kind) {
    case UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid:
        return localization::translate_message("explicit source adapter invalid");
    case UpgradeAllOperationIssueKind::OptionSnapshotMismatch:
        return localization::translate_message("option snapshot mismatch");
    case UpgradeAllOperationIssueKind::SourceSnapshotMismatch:
        return localization::translate_message("source snapshot mismatch");
    case UpgradeAllOperationIssueKind::ExplicitSourceCorrelationInconsistent:
        return localization::translate_message("explicit source correlation inconsistent");
    case UpgradeAllOperationIssueKind::PreparedCapabilityConsumed:
        return localization::translate_message("prepared capability consumed");
    case UpgradeAllOperationIssueKind::SystemSourceExecutionFailedUnexpectedly:
        return localization::translate_message("system/source execution failed unexpectedly");
    case UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete:
        return localization::translate_message("system/source phase incomplete");
    case UpgradeAllOperationIssueKind::ForeignInventoryConfigurationFailed:
        return localization::translate_message("foreign inventory configuration failed");
    case UpgradeAllOperationIssueKind::ForeignInventoryReadFailed:
        return localization::translate_message("foreign inventory read failed");
    case UpgradeAllOperationIssueKind::CacheAuthorityInvalid:
        return localization::translate_message("cache authority invalid");
    case UpgradeAllOperationIssueKind::AurQueryFailed:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("{} query failed",
                                                        AUR_SERVICE);
    case UpgradeAllOperationIssueKind::FilteredAurPreparationFailed:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "filtered {} preparation failed", AUR_SERVICE);
    case UpgradeAllOperationIssueKind::FilteredAurExecutionFailed:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "filtered {} execution failed", AUR_SERVICE);
    case UpgradeAllOperationIssueKind::
            DuplicateExclusionCorrelationInconsistent:
        return localization::translate_message("duplicate exclusion correlation inconsistent");
    case UpgradeAllOperationIssueKind::
            ExternalSatisfactionCorrelationInconsistent:
        return localization::translate_message("external satisfaction correlation inconsistent");
    case UpgradeAllOperationIssueKind::UnknownFailure:
        return localization::translate_message("unknown aggregate failure");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} issue kind.", COMMAND_NAME));
}

std::string adapter_issue_kind_label(
        UpgradeAllExplicitSourceAdapterIssueKind kind) {
    switch(kind) {
    case UpgradeAllExplicitSourceAdapterIssueKind::PreferencePackageNameMissing:
        return localization::translate_message("preference package name missing");
    case UpgradeAllExplicitSourceAdapterIssueKind::PackageBaseUnavailable:
        return localization::format_translated_message(
                "{} unavailable", PACKAGE_BASE_FIELD);
    case UpgradeAllExplicitSourceAdapterIssueKind::
            CanonicalSourceIdentityUnavailable:
        return localization::translate_message("canonical source identity unavailable");
    case UpgradeAllExplicitSourceAdapterIssueKind::
            DuplicateOriginalPreferenceIndex:
        return localization::translate_message("duplicate original preference index");
    case UpgradeAllExplicitSourceAdapterIssueKind::
            AdapterCorrelationInconsistent:
        return localization::translate_message("adapter correlation inconsistent");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown explicit source adapter issue kind."));
}

std::string preflight_reason_label(AurUpdateExecutionReason reason) {
    switch(reason) {
    case AurUpdateExecutionReason::None:
        return localization::translate_message("none");
    case AurUpdateExecutionReason::UpToDate:
        return localization::translate_message("up to date");
    case AurUpdateExecutionReason::NonAurForeign:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message("non-{} foreign",
                                                        AUR_SERVICE);
    case AurUpdateExecutionReason::AurMetadataUnavailable:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "{} metadata unavailable", AUR_SERVICE);
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
        return localization::translate_message("version comparison unavailable");
    case AurUpdateExecutionReason::InstalledReasonUnknown:
        return localization::translate_message("installed reason unknown");
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
        return localization::translate_message("update plan inconsistent");
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
        return localization::translate_message("duplicate update target");
    case AurUpdateExecutionReason::InstalledPackageMetadataUnavailable:
        return localization::translate_message(
                "installed package metadata unavailable");
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
        return localization::translate_message("repository metadata unavailable");
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "{} dependency metadata unavailable", AUR_SERVICE);
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
        return localization::translate_message("provider metadata unavailable");
    case AurUpdateExecutionReason::UnresolvedDependency:
        return localization::translate_message("unresolved dependency");
    case AurUpdateExecutionReason::VersionConstraintUnverified:
        return localization::translate_message("version constraint unverified");
    case AurUpdateExecutionReason::DependencyCycle:
        return localization::translate_message("dependency cycle");
    case AurUpdateExecutionReason::BuildPlanInconsistent:
        return localization::translate_message("build plan inconsistent");
    case AurUpdateExecutionReason::PackageBaseMismatch:
        return localization::translate_message("package base mismatch");
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
        return localization::translate_message("split package selection required");
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
        return localization::translate_message("multiple package targets for package base");
    case AurUpdateExecutionReason::AmbiguousProvider:
        return localization::translate_message("ambiguous provider");
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return localization::translate_message("conflicts/replaces unresolved");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} update preflight reason.", AUR_SERVICE));
}

std::string preparation_reason_label(AurUpdatePreparationReason reason) {
    switch(reason) {
    case AurUpdatePreparationReason::None:
        return localization::translate_message("none");
    case AurUpdatePreparationReason::BlockingPreflight:
        return localization::translate_message("blocking preflight");
    case AurUpdatePreparationReason::PreflightInconsistent:
        return localization::translate_message("preflight inconsistent");
    case AurUpdatePreparationReason::BuildPlanMissing:
        return localization::translate_message("build plan missing");
    case AurUpdatePreparationReason::BuildPlanOrderEmpty:
        return localization::translate_message("build plan order empty");
    case AurUpdatePreparationReason::RootAttributionInconsistent:
        return localization::translate_message("root attribution inconsistent");
    case AurUpdatePreparationReason::PackageTargetAttributionInconsistent:
        return localization::translate_message("package target attribution inconsistent");
    case AurUpdatePreparationReason::DesiredInstallReasonMissing:
        return localization::translate_message("desired install reason missing");
    case AurUpdatePreparationReason::SourcePreferenceUnavailable:
        return localization::translate_message("source preference unavailable");
    case AurUpdatePreparationReason::SourcePreferencePkgdestConflict:
        // TRANSLATORS: The placeholder is the literal environment key
        // "PKGDEST".
        return localization::format_translated_message(
                "source preference {} conflict", PKGDEST_KEY);
    case AurUpdatePreparationReason::StaticWorkItemInvalid:
        return localization::translate_message("static work item invalid");
    case AurUpdatePreparationReason::PacmanDatabaseUnavailable:
        // TRANSLATORS: The placeholder is the literal command name "pacman".
        return localization::format_translated_message(
                "{} database unavailable", PACMAN_COMMAND);
    case AurUpdatePreparationReason::GenericPreparationInconsistent:
        return localization::translate_message("generic preparation inconsistent");
    case AurUpdatePreparationReason::BuildUnitSelectionInconsistent:
        return localization::translate_message("build unit selection inconsistent");
    case AurUpdatePreparationReason::ExternalSatisfactionInconsistent:
        return localization::translate_message("external satisfaction inconsistent");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} update preparation reason.", AUR_SERVICE));
}

std::string reduction_stage_label(AurUpdateOperationReductionStage stage) {
    switch(stage) {
    case AurUpdateOperationReductionStage::Preflight:
        return localization::translate_message("preflight");
    case AurUpdateOperationReductionStage::Preparation:
        return localization::translate_message("preparation");
    case AurUpdateOperationReductionStage::Execution:
        return localization::translate_message("execution");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} reduction stage.", AUR_SERVICE));
}

std::string reduction_reason_label(
        AurUpdateOperationReductionReason reason) {
    switch(reason) {
    case AurUpdateOperationReductionReason::
            DuplicatePreflightUpdatePlanIndex:
        return localization::translate_message("duplicate preflight update plan index");
    case AurUpdateOperationReductionReason::
            OutOfRangePreflightUpdatePlanIndex:
        return localization::translate_message("out-of-range preflight update plan index");
    case AurUpdateOperationReductionReason::
            PreflightTargetOrderInconsistent:
        return localization::translate_message("preflight target order inconsistent");
    case AurUpdateOperationReductionReason::DuplicatePreparationAttribution:
        return localization::translate_message("duplicate preparation attribution");
    case AurUpdateOperationReductionReason::UnknownPreparationUpdatePlanIndex:
        return localization::translate_message("unknown preparation update plan index");
    case AurUpdateOperationReductionReason::
            PreparationAttributionInconsistent:
        return localization::translate_message("preparation attribution inconsistent");
    case AurUpdateOperationReductionReason::
            PreparationTargetSnapshotInconsistent:
        return localization::translate_message("preparation target snapshot inconsistent");
    case AurUpdateOperationReductionReason::DuplicateExecutionWorkItemIndex:
        return localization::translate_message("duplicate execution work item index");
    case AurUpdateOperationReductionReason::
            ExecutionWorkItemOrderInconsistent:
        return localization::translate_message("execution work item order inconsistent");
    case AurUpdateOperationReductionReason::DuplicateExecutionAttribution:
        return localization::translate_message("duplicate execution attribution");
    case AurUpdateOperationReductionReason::UnknownExecutionUpdatePlanIndex:
        return localization::translate_message("unknown execution update plan index");
    case AurUpdateOperationReductionReason::MissingExecutionAttribution:
        return localization::translate_message("missing execution attribution");
    case AurUpdateOperationReductionReason::
            DuplicateExecutionChildAttribution:
        return localization::translate_message("duplicate execution child attribution");
    case AurUpdateOperationReductionReason::
            MissingExecutionChildAttribution:
        return localization::translate_message("missing execution child attribution");
    case AurUpdateOperationReductionReason::
            UnexpectedExecutionChildAttribution:
        return localization::translate_message("unexpected execution child attribution");
    case AurUpdateOperationReductionReason::
            UnknownExecutionChildUpdatePlanIndex:
        return localization::translate_message("unknown execution child update plan index");
    case AurUpdateOperationReductionReason::
            ExecutionChildSnapshotInconsistent:
        return localization::translate_message("execution child snapshot inconsistent");
    case AurUpdateOperationReductionReason::UnexpectedSelectedArtifact:
        return localization::translate_message("unexpected selected artifact");
    case AurUpdateOperationReductionReason::
            UnexpectedUnselectedArtifactIdentity:
        return localization::translate_message("unexpected unselected artifact identity");
    case AurUpdateOperationReductionReason::
            ExecutionResultWithPreparationIssues:
        return localization::translate_message("execution result with preparation issues");
    case AurUpdateOperationReductionReason::MissingExecutionResult:
        return localization::translate_message("missing execution result");
    case AurUpdateOperationReductionReason::UnknownEnumValue:
        return localization::translate_message("unknown enum value");
    case AurUpdateOperationReductionReason::WorkItemResultInconsistent:
        return localization::translate_message("work item result inconsistent");
    case AurUpdateOperationReductionReason::InvocationResultInconsistent:
        return localization::translate_message("invocation result inconsistent");
    case AurUpdateOperationReductionReason::OtherCorrelationInconsistent:
        return localization::translate_message("other correlation inconsistency");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} reduction reason.", AUR_SERVICE));
}

std::string filtered_issue_kind_label(
        FilteredAurUpdateOperationIssueKind kind) {
    switch(kind) {
    case FilteredAurUpdateOperationIssueKind::UnknownUpdateClassification:
        return localization::translate_message("unknown update classification");
    case FilteredAurUpdateOperationIssueKind::TargetPlannerMappingInconsistent:
        return localization::translate_message("target/planner mapping inconsistent");
    case FilteredAurUpdateOperationIssueKind::FilteredTargetMappingInconsistent:
        return localization::translate_message("filtered target mapping inconsistent");
    case FilteredAurUpdateOperationIssueKind::
            PreflightTargetMappingInconsistent:
        return localization::translate_message("preflight target mapping inconsistent");
    case FilteredAurUpdateOperationIssueKind::
            PreflightInvocationIndexOutOfRange:
        return localization::translate_message("preflight invocation index out of range");
    case FilteredAurUpdateOperationIssueKind::
            PreflightInvocationIdentityMismatch:
        return localization::translate_message("preflight invocation identity mismatch");
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIndexMissing:
        return localization::translate_message("build-plan root index missing");
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIndexOutOfRange:
        return localization::translate_message("build-plan root index out of range");
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIdentityMismatch:
        return localization::translate_message("build-plan root identity mismatch");
    case FilteredAurUpdateOperationIssueKind::
            BuildPlanRootPackageIdentityMismatch:
        return localization::translate_message("build-plan root package identity mismatch");
    case FilteredAurUpdateOperationIssueKind::BuildUnitOrderIdentityMismatch:
        return localization::translate_message("build-unit order identity mismatch");
    case FilteredAurUpdateOperationIssueKind::
            BuildUnitRootAttributionInconsistent:
        return localization::translate_message("build-unit root attribution inconsistent");
    case FilteredAurUpdateOperationIssueKind::
            BuildUnitSelectionMappingInconsistent:
        return localization::translate_message("build-unit selection mapping inconsistent");
    case FilteredAurUpdateOperationIssueKind::
            ExecutionBuildUnitMappingInconsistent:
        return localization::translate_message("execution build-unit mapping inconsistent");
    case FilteredAurUpdateOperationIssueKind::ReducedTargetMappingInconsistent:
        return localization::translate_message("reduced target mapping inconsistent");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown filtered {} operation issue kind.", AUR_SERVICE));
}

std::string planning_issue_kind_label(UpgradeAllPlanningIssueKind kind) {
    switch(kind) {
    case UpgradeAllPlanningIssueKind::ExplicitPreferencePackageNameMissing:
        return localization::translate_message("explicit preference package name missing");
    case UpgradeAllPlanningIssueKind::ExplicitProducedPackageNameMissing:
        return localization::translate_message("explicit produced package name missing");
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseAbsent:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "explicit {} absent", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseResolutionFailed:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "explicit {} resolution failed", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseEmpty:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "explicit {} empty", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityAbsent:
        return localization::translate_message("explicit source identity absent");
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityResolutionFailed:
        return localization::translate_message("explicit source identity resolution failed");
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityEmpty:
        return localization::translate_message("explicit source identity empty");
    case UpgradeAllPlanningIssueKind::
            ConflictingExplicitSourceIdentityDefinition:
        return localization::translate_message("conflicting explicit source identity definition");
    case UpgradeAllPlanningIssueKind::ConflictingExplicitPackageName:
        return localization::translate_message("conflicting explicit package name");
    case UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "conflicting explicit {}", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::AurTargetPackageNameMissing:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "{} target package name missing", AUR_SERVICE);
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent:
        // TRANSLATORS: The placeholders are the literal service name "AUR"
        // and field name "PackageBase".
        return localization::format_translated_message(
                "{} target {} absent", AUR_SERVICE, PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseResolutionFailed:
        // TRANSLATORS: The placeholders are the literal service name "AUR"
        // and field name "PackageBase".
        return localization::format_translated_message(
                "{} target {} resolution failed", AUR_SERVICE,
                PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseEmpty:
        // TRANSLATORS: The placeholders are the literal service name "AUR"
        // and field name "PackageBase".
        return localization::format_translated_message(
                "{} target {} empty", AUR_SERVICE, PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::UnsupportedAurTarget:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "unsupported {} target", AUR_SERVICE);
    case UpgradeAllPlanningIssueKind::IncompleteAurTarget:
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        return localization::format_translated_message(
                "incomplete {} target", AUR_SERVICE);
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseAbsent:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "build-unit {} absent", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseResolutionFailed:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "build-unit {} resolution failed", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseEmpty:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "build-unit {} empty", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::BuildUnitHasNoRootAttribution:
        return localization::translate_message("build unit has no root attribution");
    case UpgradeAllPlanningIssueKind::BuildUnitTargetIndexOutOfRange:
        return localization::translate_message("build-unit target index out of range");
    case UpgradeAllPlanningIssueKind::DuplicateSelectedTargetPackageBase:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "duplicate selected target {}", PACKAGE_BASE_FIELD);
    case UpgradeAllPlanningIssueKind::DuplicateSelectedBuildUnitPackageBase:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "duplicate selected build-unit {}", PACKAGE_BASE_FIELD);
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} planning issue kind.", COMMAND_NAME));
}

std::string duplicate_reason_label(UpgradeAllTargetDisposition disposition) {
    switch(disposition) {
    case UpgradeAllTargetDisposition::ExcludedByExplicitPackageName:
        return localization::translate_message("package name handled by explicit source preference");
    case UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase:
        // TRANSLATORS: The placeholder is the literal field name
        // "PackageBase".
        return localization::format_translated_message(
                "{} handled by explicit source preference",
                PACKAGE_BASE_FIELD);
    case UpgradeAllTargetDisposition::Selected:
    case UpgradeAllTargetDisposition::Unsupported:
    case UpgradeAllTargetDisposition::IdentityIncomplete:
    case UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity:
    case UpgradeAllTargetDisposition::ConflictingSelectedPackageBase:
        throw std::logic_error(localization::translate_message(
                "Non-exclusion target disposition reached duplicate presentation."));
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} target disposition.", COMMAND_NAME));
}

std::string build_unit_role_label(UpgradeAllBuildUnitRole role) {
    switch(role) {
    case UpgradeAllBuildUnitRole::Root:
        return localization::translate_message("root");
    case UpgradeAllBuildUnitRole::RuntimeDependency:
        return localization::translate_message("runtime dependency");
    case UpgradeAllBuildUnitRole::BuildDependency:
        return localization::translate_message("build dependency");
    case UpgradeAllBuildUnitRole::CheckDependency:
        return localization::translate_message("check dependency");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} build-unit role.", COMMAND_NAME));
}

bool is_normal_skip_reason(AurUpdateExecutionReason reason) {
    switch(reason) {
    case AurUpdateExecutionReason::UpToDate:
    case AurUpdateExecutionReason::NonAurForeign:
        return true;
    case AurUpdateExecutionReason::None:
    case AurUpdateExecutionReason::AurMetadataUnavailable:
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
    case AurUpdateExecutionReason::InstalledReasonUnknown:
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
    case AurUpdateExecutionReason::InstalledPackageMetadataUnavailable:
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
    case AurUpdateExecutionReason::UnresolvedDependency:
    case AurUpdateExecutionReason::VersionConstraintUnverified:
    case AurUpdateExecutionReason::DependencyCycle:
    case AurUpdateExecutionReason::BuildPlanInconsistent:
    case AurUpdateExecutionReason::PackageBaseMismatch:
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
    case AurUpdateExecutionReason::AmbiguousProvider:
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return false;
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} update preflight reason.", AUR_SERVICE));
}

std::string target_reason_label(const AurUpdateOperationTargetResult& target) {
    if(!target.preflight_issues.empty()) {
        return std::string(
                preflight_reason_label(target.preflight_issues.front().reason));
    }
    if(!target.preparation_issues.empty()) {
        return std::string(preparation_reason_label(
                target.preparation_issues.front().reason));
    }
    return localization::translate_message("reason unavailable");
}

std::string aur_target_status_label(
        const AurUpdateOperationTargetResult& target,
        AurUpdateOperationStatus operation_status) {
    switch(target.status) {
    case AurUpdateOperationTargetStatus::Updated:
        return localization::translate_message("updated");
    case AurUpdateOperationTargetStatus::NoChange:
        return localization::translate_message("no change");
    case AurUpdateOperationTargetStatus::Skipped:
        return localization::format_translated_message(
                "skipped: {}", target_reason_label(target));
    case AurUpdateOperationTargetStatus::Unsupported:
        return localization::format_translated_message(
                "unsupported: {}", target_reason_label(target));
    case AurUpdateOperationTargetStatus::Incomplete:
        return localization::format_translated_message(
                "incomplete: {}", target_reason_label(target));
    case AurUpdateOperationTargetStatus::Failed:
        return localization::format_translated_message(
                "failed: {}", aur_update_cli_target_failure_summary(target));
    case AurUpdateOperationTargetStatus::UpdatedCleanupFailed:
        return localization::translate_message("updated, but cleanup failed");
    case AurUpdateOperationTargetStatus::NoChangeCleanupFailed:
        return localization::translate_message("no package change, but cleanup failed");
    case AurUpdateOperationTargetStatus::NotAttempted:
        if(operation_status == AurUpdateOperationStatus::
                                       StoppedOnProviderTransactionFailure) {
            return localization::translate_message(
                    "not attempted: repository provider transaction failed");
        }
        if(target.execution_failure_kind ==
           AurUpdateWorkItemFailureKind::PriorWorkItemStopped) {
            return localization::translate_message(
                    "not attempted: prior work item stopped");
        }
        switch(operation_status) {
        case AurUpdateOperationStatus::BlockedBeforeExecution:
            return localization::translate_message(
                    "not attempted: operation blocked before execution");
        case AurUpdateOperationStatus::InconsistentResult:
            return localization::translate_message(
                    "not attempted: result inconsistent");
        case AurUpdateOperationStatus::
                StoppedOnProviderTransactionFailure:
            return localization::translate_message(
                    "not attempted: repository provider transaction failed");
        case AurUpdateOperationStatus::StoppedOnWorkItemFailure:
        case AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure:
            return localization::translate_message(
                    "not attempted: prior work item stopped");
        case AurUpdateOperationStatus::NoUpdates:
        case AurUpdateOperationStatus::Completed:
            return localization::translate_message(
                    "not attempted: result inconsistent");
        }
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal service name
                // "AUR".
                "Unknown {} update operation status.", AUR_SERVICE));
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal service name "AUR".
            "Unknown {} update target status.", AUR_SERVICE));
}

std::string join_strings(const std::vector<std::string>& values) {
    std::string joined;
    for(std::size_t index = 0; index < values.size(); ++index) {
        if(index != 0) joined += ", ";
        joined += values[index];
    }
    return joined;
}

std::string join_query_package_names(
        const std::vector<std::string>& package_names) {
    return package_names.empty() ? localization::translate_message("unknown packages")
                                 : join_strings(package_names);
}

void print_system_phase(const SystemSourceUpgradeResult& result) {
    std::cout << localization::format_translated_message(
                         "system: {}",
                         system_phase_status_label(result.system.status))
              << std::endl;
    std::cout << localization::format_translated_message(
                         "package state: {}",
                         package_state_label(result.system.package_state_change))
              << std::endl;
}

void print_registered_sources(const SystemSourceUpgradeResult& result) {
    if(result.registered_source_results.empty()) {
        std::cout << localization::translate_message(
                             "registered source packages: none")
                  << std::endl;
        return;
    }

    // POLICY(#281): vector順はprepared preference snapshot順。表示側で
    // sort/deduplicateせず、各preferenceへexactly one outcomeを出す。
    for(const RegisteredSourceUpgradeResult& source :
        result.registered_source_results) {
        const std::string package_name = source.preference_package_name.empty()
                ? localization::translate_message("<unknown preference>")
                : source.preference_package_name;
        std::cout << localization::format_translated_message(
                             "registered source: {}: {}", package_name,
                             source_status_label(source))
                  << std::endl;
        if(source.resolved_package_base.has_value()) {
            // NO_TRANSLATE(Issue #308): PackageBase is a schema field name,
            // and the value is a package identity.
            std::cout << "  " << PACKAGE_BASE_FIELD << ": "
                      << *source.resolved_package_base << std::endl;
        }
        if(source.canonical_source_identity_key.has_value()) {
            std::cout << localization::format_translated_message(
                                 "  canonical source identity: {}",
                                 *source.canonical_source_identity_key)
                      << std::endl;
        }
    }
}

void print_aur_phase(
        const UpgradeAllAurPhaseResult& aur,
        const AurUpdateCliPresentation* presentation) {
    if(aur.status == UpgradeAllAurPhaseStatus::NotAttempted) {
        if(!aur.not_attempted_reason.has_value()) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the literal service
                    // name "AUR" and enum state "NotAttempted".
                    "The {} phase is {} without a typed reason.", AUR_SERVICE,
                    "NotAttempted"));
        }
        const std::string reason =
                not_attempted_reason_label(*aur.not_attempted_reason);
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the second is a localized reason.
        std::cout << localization::format_translated_message(
                             "{} phase not attempted: {}", AUR_SERVICE, reason)
                  << std::endl;
        return;
    }

    const std::string phase_status = aur_phase_status_label(aur.status);
    // TRANSLATORS: The first placeholder is the literal service name "AUR";
    // the second is a localized phase status.
    std::cout << localization::format_translated_message(
                         "{} phase: {}", AUR_SERVICE, phase_status)
              << std::endl;
    if(!aur.operation_result.has_value()) {
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        std::cout << localization::format_translated_message(
                             "{} targets: unavailable", AUR_SERVICE)
                  << std::endl;
        return;
    }

    const AurUpdateOperationResult& operation =
            aur.operation_result->reduced_operation_result;
    if(operation.targets.empty()) {
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        std::cout << localization::format_translated_message(
                             "{} targets: none", AUR_SERVICE)
                  << std::endl;
    } else {
        for(const AurUpdateOperationTargetResult& target : operation.targets) {
            const std::string status =
                    aur_target_status_label(target, operation.status);
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a package name and localized status.
            std::cout << localization::format_translated_message(
                                 "{} target: {}: {}", AUR_SERVICE,
                                 target.update.installed_name, status)
                      << std::endl;
            if(target.package_base.has_value()) {
                // NO_TRANSLATE(Issue #308): PackageBase is a schema field
                // name, and the value is a package identity.
                std::cout << "  " << PACKAGE_BASE_FIELD << ": "
                          << *target.package_base << std::endl;
            }
        }
    }
    if(presentation == nullptr) return;
    for(const std::string& line : presentation->summary_lines) {
        std::cout << line << std::endl;
    }
}

bool has_foreign_inventory_failure(
        const UpgradeAllForeignInventoryPhaseResult& inventory) noexcept {
    return inventory.status == UpgradeAllForeignInventoryPhaseStatus::Failed ||
           inventory.failure.has_value() || inventory.diagnostic.has_value();
}

void print_aur_phase(
        const UpgradeAllOperationResult& result,
        const AurUpdateCliPresentation* presentation) {
    // POLICY(#281): inventory failureを含むsynthetic resultでも、AUR mutationが
    // 完了したような表示へ丸めない。直接fieldをtyped source of truthにする。
    if(has_foreign_inventory_failure(result.foreign_inventory)) {
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        std::cout << localization::format_translated_message(
                             "{} phase not attempted: foreign inventory failure",
                             AUR_SERVICE)
                  << std::endl;
        return;
    }
    print_aur_phase(result.aur, presentation);
}

void print_duplicate_exclusions(const UpgradeAllOperationResult& result) {
    for(const UpgradeAllDuplicateExcludedAurTarget& exclusion :
        result.duplicate_excluded_aur_targets) {
        const std::string reason = duplicate_reason_label(
                exclusion.planner_entry.disposition);
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the second is a package name.
        std::cout << localization::format_translated_message(
                             "excluded from {} update: {}", AUR_SERVICE,
                             exclusion.query_entry.installed_name)
                  << std::endl;
        std::cout << localization::format_translated_message(
                             "reason: {}", reason)
                  << std::endl;

        if(!exclusion.planner_entry.explicit_source.has_value()) {
            throw std::logic_error(localization::translate_message(
                    "The duplicate exclusion has no explicit source attribution."));
        }
        const UpgradeAllExplicitSourceAttribution& attribution =
                *exclusion.planner_entry.explicit_source;
        if(attribution.matched_package_name.has_value()) {
            std::cout << localization::format_translated_message(
                                 "matched explicit source package: {}",
                                 *attribution.matched_package_name)
                      << std::endl;
        }
        if(attribution.matched_package_base.has_value()) {
            // TRANSLATORS: The first placeholder is the literal field name
            // "PackageBase"; the second is its runtime value.
            std::cout << localization::format_translated_message(
                                 "matched {}: {}", PACKAGE_BASE_FIELD,
                                 *attribution.matched_package_base)
                      << std::endl;
        }
        for(const std::string& identity : attribution.source_identity_keys) {
            std::cout << localization::format_translated_message(
                                 "canonical source identity: {}", identity)
                      << std::endl;
        }
    }
}

void print_external_satisfaction(const UpgradeAllOperationResult& result) {
    for(const UpgradeAllExternallySatisfiedAurBuildUnit& external :
        result.externally_satisfied_aur_build_units) {
        const AurUpdateExternallySatisfiedBuildUnit& unit =
                external.operation_unit;
        if(unit.external_satisfaction.source_identity_keys.empty()) {
            throw std::logic_error(localization::translate_message(
                    "External satisfaction has no explicit source identity."));
        }
        std::vector<std::string> role_labels;
        role_labels.reserve(external.root_correlations.size());
        for(const FilteredAurUpdateBuildUnitRootCorrelation& root :
            external.root_correlations) {
            role_labels.push_back(build_unit_role_label(root.role));
        }

        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the second is a package-base identity.
        std::cout << localization::format_translated_message(
                             "{} build unit externally satisfied: {}",
                             AUR_SERVICE, unit.package_base)
                  << std::endl;
        std::cout << localization::format_translated_message(
                             "provided by explicit source preference: {}",
                             join_strings(
                                     unit.external_satisfaction.source_identity_keys))
                  << std::endl;
        if(unit.external_satisfaction.matched_package_name.has_value()) {
            std::cout << localization::format_translated_message(
                                 "matched explicit source package: {}",
                                 *unit.external_satisfaction.matched_package_name)
                      << std::endl;
        }
        if(unit.external_satisfaction.matched_package_base.has_value()) {
            // TRANSLATORS: The first placeholder is the literal field name
            // "PackageBase"; the second is its runtime value.
            std::cout << localization::format_translated_message(
                                 "matched {}: {}", PACKAGE_BASE_FIELD,
                                 *unit.external_satisfaction.matched_package_base)
                      << std::endl;
        }
        for(std::size_t index = 0;
            index < external.root_correlations.size(); ++index) {
            const FilteredAurUpdateBuildUnitRootCorrelation& root =
                    external.root_correlations[index];
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a package name and localized role.
            std::cout << localization::format_translated_message(
                                 "affected {} root: {} ({})", AUR_SERVICE,
                                 root.preflight_root.requested_name,
                                 role_labels[index])
                      << std::endl;
        }
    }
}

std::string diagnostic_key(
        std::string_view phase, const std::string& diagnostic) {
    return std::string(phase) + "\n" + diagnostic;
}

void print_system_warnings(
        const SystemSourceUpgradeResult& result,
        std::set<std::string>& printed_warning_keys) {
    for(const SystemSourceUpgradeWarning& warning : result.warnings) {
        const std::string package_name =
                warning.preference_package_name.value_or(
                        localization::translate_message("unknown preference"));
        Logger::warn(localization::format_translated_message(
                "system/source warning: {}: {}: {}",
                system_warning_kind_label(warning.kind), package_name,
                warning.diagnostic));
        printed_warning_keys.insert(package_name + "\n" + warning.diagnostic);
    }
}

void print_system_failures(const SystemSourceUpgradeResult& result) {
    if(result.system.diagnostic.has_value() &&
       !result.system.diagnostic->empty()) {
        Logger::error(localization::format_translated_message(
                "system failure: {}", *result.system.diagnostic));
    }
    for(const RegisteredSourceUpgradeResult& source :
        result.registered_source_results) {
        if((source.status == RegisteredSourceUpgradeStatus::Failed ||
            source.status == RegisteredSourceUpgradeStatus::Unsupported ||
            source.status == RegisteredSourceUpgradeStatus::Incomplete) &&
           source.diagnostic.has_value() && !source.diagnostic->empty()) {
            Logger::error(localization::format_translated_message(
                    "registered source failure: {}: {}",
                    source.preference_package_name, *source.diagnostic));
        }
        if((source.status ==
                    RegisteredSourceUpgradeStatus::UpdatedCleanupFailed ||
            source.status ==
                    RegisteredSourceUpgradeStatus::NoChangeCleanupFailed) &&
           source.cleanup_diagnostic.has_value() &&
           !source.cleanup_diagnostic->empty()) {
            Logger::error(localization::format_translated_message(
                    "registered source cleanup failure: {}: {}",
                    source.preference_package_name,
                    *source.cleanup_diagnostic));
        }
    }
}

void print_system_issues_and_diagnostics(
        const SystemSourceUpgradeResult& result) {
    std::set<std::string> issue_diagnostics;
    for(const SystemSourceUpgradeIssue& issue : result.issues) {
        const std::string kind = system_issue_kind_label(issue.kind);
        const std::string impact = system_issue_impact_label(issue.impact);
        const std::string phase = system_source_phase_label(issue.phase);
        const bool has_package = issue.preference_package_name.has_value();
        const bool has_metadata = issue.package_metadata_failure.has_value();
        const bool has_diagnostic = !issue.diagnostic.empty();
        std::string message;
        if(has_package && has_metadata && has_diagnostic) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}): {} [{}]: {}", kind,
                    impact, phase, *issue.preference_package_name,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code),
                    issue.diagnostic);
        } else if(has_package && has_metadata) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}): {} [{}]", kind,
                    impact, phase, *issue.preference_package_name,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code));
        } else if(has_package && has_diagnostic) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}): {}: {}", kind,
                    impact, phase, *issue.preference_package_name,
                    issue.diagnostic);
        } else if(has_metadata && has_diagnostic) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}) [{}]: {}", kind,
                    impact, phase,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code),
                    issue.diagnostic);
        } else if(has_package) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}): {}", kind, impact,
                    phase, *issue.preference_package_name);
        } else if(has_metadata) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}) [{}]", kind, impact,
                    phase,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code));
        } else if(has_diagnostic) {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {}): {}", kind, impact,
                    phase, issue.diagnostic);
        } else {
            message = localization::format_translated_message(
                    "system/source issue: {} ({}, {})", kind, impact, phase);
        }
        Logger::error(message);
        issue_diagnostics.insert(diagnostic_key(
                system_source_phase_label(issue.phase), issue.diagnostic));
    }

    for(const SystemSourceUpgradeDiagnostic& diagnostic :
        result.diagnostics) {
        if(diagnostic.diagnostic.empty() ||
           issue_diagnostics.contains(diagnostic_key(
                   system_source_phase_label(diagnostic.phase),
                   diagnostic.diagnostic))) {
            continue;
        }
        Logger::error(localization::format_translated_message(
                "system/source diagnostic: {}: {}",
                system_source_phase_label(diagnostic.phase),
                diagnostic.diagnostic));
    }
}

void print_aur_query_failures(const FilteredAurUpdateExecutionResult& result) {
    for(const AurUpdateQueryFailure& failure :
        result.query_result.recoverable_failures) {
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the others are package names and a runtime diagnostic.
        Logger::error(localization::format_translated_message(
                "{} query failure for {}: {}", AUR_SERVICE,
                join_query_package_names(failure.package_names),
                failure.diagnostic));
    }
}

void print_planning_issues(const UpgradeAllPlan& plan) {
    for(const UpgradeAllPlanningIssue& issue : plan.issues) {
        const std::string kind = planning_issue_kind_label(issue.kind);
        std::string message;
        if(issue.package_name.has_value() && issue.package_base.has_value()) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the fourth is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} planner issue: {}: package {}: {} {}", AUR_SERVICE,
                    kind, *issue.package_name, PACKAGE_BASE_FIELD,
                    *issue.package_base);
        } else if(issue.package_name.has_value()) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a localized kind and package name.
            message = localization::format_translated_message(
                    "{} planner issue: {}: package {}", AUR_SERVICE, kind,
                    *issue.package_name);
        } else if(issue.package_base.has_value()) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the third is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} planner issue: {}: {} {}", AUR_SERVICE, kind,
                    PACKAGE_BASE_FIELD, *issue.package_base);
        } else {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the second is a localized issue kind.
            message = localization::format_translated_message(
                    "{} planner issue: {}", AUR_SERVICE, kind);
        }
        Logger::error(message);
    }
}

void print_filtered_mapping_issues(
        const FilteredAurUpdateExecutionResult& result) {
    for(const FilteredAurUpdateOperationIssue& issue : result.issues) {
        const std::string kind = filtered_issue_kind_label(issue.kind);
        const bool has_package = issue.package_name.has_value();
        const bool has_base = issue.package_base.has_value();
        const bool has_diagnostic = !issue.diagnostic.empty();
        std::string message;
        if(has_package && has_base && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the fourth is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} mapping issue: {}: package {}: {} {}: {}", AUR_SERVICE,
                    kind, *issue.package_name, PACKAGE_BASE_FIELD,
                    *issue.package_base, issue.diagnostic);
        } else if(has_package && has_base) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the fourth is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} mapping issue: {}: package {}: {} {}", AUR_SERVICE,
                    kind, *issue.package_name, PACKAGE_BASE_FIELD,
                    *issue.package_base);
        } else if(has_package && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a localized kind and runtime data.
            message = localization::format_translated_message(
                    "{} mapping issue: {}: package {}: {}", AUR_SERVICE, kind,
                    *issue.package_name, issue.diagnostic);
        } else if(has_base && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the third is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} mapping issue: {}: {} {}: {}", AUR_SERVICE, kind,
                    PACKAGE_BASE_FIELD, *issue.package_base,
                    issue.diagnostic);
        } else if(has_package) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a localized kind and package name.
            message = localization::format_translated_message(
                    "{} mapping issue: {}: package {}", AUR_SERVICE, kind,
                    *issue.package_name);
        } else if(has_base) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the third is the literal field name "PackageBase".
            message = localization::format_translated_message(
                    "{} mapping issue: {}: {} {}", AUR_SERVICE, kind,
                    PACKAGE_BASE_FIELD, *issue.package_base);
        } else if(has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a localized kind and runtime diagnostic.
            message = localization::format_translated_message(
                    "{} mapping issue: {}: {}", AUR_SERVICE, kind,
                    issue.diagnostic);
        } else {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the second is a localized issue kind.
            message = localization::format_translated_message(
                    "{} mapping issue: {}", AUR_SERVICE, kind);
        }
        Logger::error(message);
    }
}

void print_aur_preflight_issues(const AurUpdateOperationResult& result) {
    for(const AurUpdateOperationTargetResult& target : result.targets) {
        for(const AurUpdateExecutionIssue& issue : target.preflight_issues) {
            if(is_normal_skip_reason(issue.reason)) continue;
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the others are a localized reason and diagnostic.
            Logger::error(localization::format_translated_message(
                    "{} preflight issue: {}: {}", AUR_SERVICE,
                    preflight_reason_label(issue.reason), issue.diagnostic));
        }
    }
}

void print_aur_preparation_details(
        const AurUpdateOperationResult& result,
        std::set<std::string>& printed_warning_keys) {
    for(const AurUpdatePreparationWarning& warning :
        result.preparation_warnings) {
        const std::string package_name = warning.preference_name.empty()
                ? localization::translate_message("unknown preference")
                : warning.preference_name;
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the others are a package name and runtime diagnostic.
        Logger::warn(localization::format_translated_message(
                "{} preparation warning: {}: {}", AUR_SERVICE, package_name,
                warning.diagnostic));
        printed_warning_keys.insert(package_name + "\n" + warning.diagnostic);
    }
    for(const AurUpdatePreparationIssue& issue : result.preparation_issues) {
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the others are a localized reason and diagnostic.
        Logger::error(localization::format_translated_message(
                "{} preparation issue: {}: {}", AUR_SERVICE,
                preparation_reason_label(issue.reason), issue.diagnostic));
    }
}

void print_aur_reduction_issues(const AurUpdateOperationResult& result) {
    for(const AurUpdateOperationReductionIssue& issue :
        result.reduction_issues) {
        // TRANSLATORS: The first placeholder is the literal service name
        // "AUR"; the others are localized details and a runtime diagnostic.
        Logger::error(localization::format_translated_message(
                "{} reduction issue: {}: {}: {}", AUR_SERVICE,
                reduction_stage_label(issue.stage),
                reduction_reason_label(issue.reason), issue.diagnostic));
    }
}

void print_adapter_issues(const UpgradeAllOperationResult& result) {
    for(const UpgradeAllExplicitSourceAdapterIssue& issue :
        result.prepared_snapshot.explicit_source_adapter.issues) {
        const std::string kind = adapter_issue_kind_label(issue.kind);
        std::string message;
        if(issue.preference_package_name.has_value() &&
           !issue.diagnostic.empty()) {
            message = localization::format_translated_message(
                    "explicit source adapter issue: {}: {}: {}", kind,
                    *issue.preference_package_name, issue.diagnostic);
        } else if(issue.preference_package_name.has_value()) {
            message = localization::format_translated_message(
                    "explicit source adapter issue: {}: {}", kind,
                    *issue.preference_package_name);
        } else if(!issue.diagnostic.empty()) {
            message = localization::format_translated_message(
                    "explicit source adapter issue: {}: {}", kind,
                    issue.diagnostic);
        } else {
            message = localization::format_translated_message(
                    "explicit source adapter issue: {}", kind);
        }
        Logger::error(message);
    }
}

std::set<std::string> print_foreign_inventory_failure(
        const UpgradeAllForeignInventoryPhaseResult& inventory) {
    std::set<std::string> printed_diagnostics;
    if(!has_foreign_inventory_failure(inventory)) {
        return printed_diagnostics;
    }

    if(inventory.failure.has_value()) {
        std::string message;
        if(!inventory.failure->diagnostic.empty()) {
            message = localization::format_translated_message(
                    "foreign inventory failure [{}]: {}",
                    package_metadata_error_label(inventory.failure->code),
                    inventory.failure->diagnostic);
            printed_diagnostics.insert(inventory.failure->diagnostic);
        } else {
            message = localization::format_translated_message(
                    "foreign inventory failure [{}]",
                    package_metadata_error_label(inventory.failure->code));
        }
        Logger::error(message);
    }

    if(inventory.diagnostic.has_value() &&
       !inventory.diagnostic->empty() &&
       !printed_diagnostics.contains(*inventory.diagnostic)) {
        Logger::error(localization::format_translated_message(
                "foreign inventory diagnostic: {}", *inventory.diagnostic));
        printed_diagnostics.insert(*inventory.diagnostic);
    }

    if(!inventory.failure.has_value() && printed_diagnostics.empty()) {
        Logger::error(localization::translate_message(
                "foreign inventory failure: diagnostic unavailable"));
    }
    return printed_diagnostics;
}

void print_aggregate_warnings(
        const UpgradeAllOperationResult& result,
        std::set<std::string>& printed_warning_keys) {
    for(const UpgradeAllOperationWarning& warning : result.warnings) {
        const std::string package_name =
                warning.package_name.value_or(
                        localization::translate_message("aggregate"));
        const std::string key = package_name + "\n" + warning.diagnostic;
        if(printed_warning_keys.contains(key)) continue;
        // TRANSLATORS: The first placeholder is the literal command name
        // "upgrade-all"; the others are localized or runtime details.
        Logger::warn(localization::format_translated_message(
                "{} warning: {} ({}): {}: {}", COMMAND_NAME,
                aggregate_warning_kind_label(warning.kind),
                aggregate_phase_label(warning.phase), package_name,
                warning.diagnostic));
        printed_warning_keys.insert(key);
    }
}

void print_aggregate_issues_and_diagnostics(
        const UpgradeAllOperationResult& result,
        const std::set<std::string>& already_reported_diagnostics) {
    const bool has_typed_aur_execution_snapshot =
            result.aur.operation_result.has_value();
    std::set<std::string> issue_diagnostics;
    for(const UpgradeAllOperationIssue& issue : result.issues) {
        const bool already_reported =
                issue.phase == UpgradeAllOperationPhase::ForeignInventory &&
                !issue.diagnostic.empty() &&
                already_reported_diagnostics.contains(issue.diagnostic);
        const bool suppress_raw_aur_execution_diagnostic =
                has_typed_aur_execution_snapshot &&
                issue.phase == UpgradeAllOperationPhase::AurExecution;
        const std::string kind = aggregate_issue_kind_label(issue.kind);
        const std::string phase = aggregate_phase_label(issue.phase);
        const bool has_package = issue.package_name.has_value();
        const bool has_metadata = issue.package_metadata_failure.has_value();
        const bool has_diagnostic = !issue.diagnostic.empty() &&
                !suppress_raw_aur_execution_diagnostic;
        std::string message;
        if(has_package && has_metadata && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}): {} [{}]: {}", COMMAND_NAME, kind,
                    phase, *issue.package_name,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code),
                    issue.diagnostic);
        } else if(has_package && has_metadata) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}): {} [{}]", COMMAND_NAME, kind, phase,
                    *issue.package_name,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code));
        } else if(has_package && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}): {}: {}", COMMAND_NAME, kind, phase,
                    *issue.package_name, issue.diagnostic);
        } else if(has_metadata && has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}) [{}]: {}", COMMAND_NAME, kind, phase,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code),
                    issue.diagnostic);
        } else if(has_package) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}): {}", COMMAND_NAME, kind, phase,
                    *issue.package_name);
        } else if(has_metadata) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}) [{}]", COMMAND_NAME, kind, phase,
                    package_metadata_error_label(
                            issue.package_metadata_failure->code));
        } else if(has_diagnostic) {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized or runtime details.
            message = localization::format_translated_message(
                    "{} issue: {} ({}): {}", COMMAND_NAME, kind, phase,
                    issue.diagnostic);
        } else {
            // TRANSLATORS: The first placeholder is the literal command name
            // "upgrade-all"; the others are localized details.
            message = localization::format_translated_message(
                    "{} issue: {} ({})", COMMAND_NAME, kind, phase);
        }
        if(!already_reported) Logger::error(message);
        issue_diagnostics.insert(diagnostic_key(
                aggregate_phase_label(issue.phase), issue.diagnostic));
    }

    if(!has_typed_aur_execution_snapshot &&
       result.aur.diagnostic.has_value() && !result.aur.diagnostic->empty()) {
        const bool already_reported = std::any_of(
                result.issues.begin(), result.issues.end(),
                [&](const UpgradeAllOperationIssue& issue) {
                    return issue.diagnostic == *result.aur.diagnostic;
        });
        if(!already_reported) {
            // TRANSLATORS: The first placeholder is the literal service name
            // "AUR"; the second is a runtime diagnostic.
            Logger::error(localization::format_translated_message(
                    "{} phase diagnostic: {}", AUR_SERVICE,
                    *result.aur.diagnostic));
        }
    }

    for(const UpgradeAllOperationDiagnostic& diagnostic :
        result.diagnostics) {
        if(diagnostic.diagnostic.empty() ||
           (has_typed_aur_execution_snapshot &&
            diagnostic.phase == UpgradeAllOperationPhase::AurExecution) ||
           (diagnostic.phase == UpgradeAllOperationPhase::ForeignInventory &&
            already_reported_diagnostics.contains(diagnostic.diagnostic)) ||
           issue_diagnostics.contains(diagnostic_key(
                   aggregate_phase_label(diagnostic.phase),
                   diagnostic.diagnostic))) {
            continue;
        }
        // TRANSLATORS: The first placeholder is the literal command name
        // "upgrade-all"; the others are a localized phase and diagnostic.
        Logger::error(localization::format_translated_message(
                "{} diagnostic: {}: {}", COMMAND_NAME,
                aggregate_phase_label(diagnostic.phase),
                diagnostic.diagnostic));
    }
}

void print_details(
        const UpgradeAllOperationResult& result,
        const AurUpdateCliPresentation* presentation) {
    std::set<std::string> printed_warning_keys;
    print_system_warnings(result.system_source, printed_warning_keys);
    print_system_failures(result.system_source);
    print_system_issues_and_diagnostics(result.system_source);
    print_adapter_issues(result);

    if(result.aur.operation_result.has_value()) {
        const FilteredAurUpdateExecutionResult& filtered =
                *result.aur.operation_result;
        print_aur_query_failures(filtered);
        print_planning_issues(filtered.upgrade_all_plan);
        print_filtered_mapping_issues(filtered);
        print_aur_preflight_issues(filtered.reduced_operation_result);
        print_aur_preparation_details(
                filtered.reduced_operation_result,
                printed_warning_keys);
        if(presentation != nullptr) {
            for(const std::string& line : presentation->error_lines) {
                Logger::error(line);
            }
        }
        print_aur_reduction_issues(filtered.reduced_operation_result);
    }

    const std::set<std::string> inventory_diagnostics =
            print_foreign_inventory_failure(result.foreign_inventory);
    print_aggregate_warnings(result, printed_warning_keys);
    print_aggregate_issues_and_diagnostics(result, inventory_diagnostics);
}

void print_aggregate_summary(const UpgradeAllOperationResult& result) {
    std::cout << aggregate_status_label(result.status) << std::endl;
    std::cout << aggregate_package_state_label(result.package_state_change())
              << std::endl;
    if(result.has_partial_completion()) {
        std::cout << localization::translate_message("partial completion")
                  << std::endl;
    }
    if(result.has_not_attempted_phase()) {
        std::cout << localization::translate_message(
                             "some phases were not attempted")
                  << std::endl;
    }
    if(result.has_cleanup_failure()) {
        std::cout << localization::translate_message("cleanup failure occurred")
                  << std::endl;
    }
    if(result.has_duplicate_exclusions()) {
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        std::cout << localization::format_translated_message(
                             "duplicate {} targets excluded", AUR_SERVICE)
                  << std::endl;
    }
    if(result.has_external_satisfaction()) {
        // TRANSLATORS: The placeholder is the literal service name "AUR".
        std::cout << localization::format_translated_message(
                             "{} build units externally satisfied", AUR_SERVICE)
                  << std::endl;
    }

    if((result.status == UpgradeAllOperationStatus::Completed ||
        result.status == UpgradeAllOperationStatus::NoUpdates) &&
       !result.is_success()) {
        Logger::error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal command name
                // "upgrade-all".
                "The {} result contains failure details despite a successful aggregate status.",
                COMMAND_NAME));
    }
}

void print_operation_result(const UpgradeAllOperationResult& result) {
    // AUR child snapshotを最初に検証し、unknown enumやincoherent identityを
    // 成功済みsummaryへ混ぜずfail-closedにする。
    std::optional<AurUpdateCliPresentation> aur_presentation;
    if(result.aur.operation_result.has_value()) {
        aur_presentation.emplace(format_aur_update_cli_presentation(
                result.aur.operation_result->reduced_operation_result));
    }
    const AurUpdateCliPresentation* presentation =
            aur_presentation.has_value() ? &*aur_presentation : nullptr;

    // POLICY(#281): phase/target order is part of the public CLI contract.
    print_system_phase(result.system_source);
    print_registered_sources(result.system_source);
    print_aur_phase(result, presentation);
    print_duplicate_exclusions(result);
    print_external_satisfaction(result);
    print_details(result, presentation);
    print_aggregate_summary(result);
}

bool is_supported_upgrade_all_global_option(const std::string& option) {
    const cli_authority::GlobalOptionSpec* spec =
            cli_authority::find_moguet_global_option(option);
    if(spec == nullptr) return false;

    switch(spec->id) {
    case cli_authority::GlobalOptionId::Edit:
    case cli_authority::GlobalOptionId::NoEdit:
    case cli_authority::GlobalOptionId::Diff:
    case cli_authority::GlobalOptionId::NoDiff:
    case cli_authority::GlobalOptionId::NoConfirm:
    case cli_authority::GlobalOptionId::DryRun:
    case cli_authority::GlobalOptionId::BuildMode:
    case cli_authority::GlobalOptionId::Rebuild:
    case cli_authority::GlobalOptionId::CleanBuild:
        return true;
    case cli_authority::GlobalOptionId::RmDeps:
    case cli_authority::GlobalOptionId::Select:
    case cli_authority::GlobalOptionId::Aur:
    case cli_authority::GlobalOptionId::Repo:
    case cli_authority::GlobalOptionId::Count:
        return false;
    }
    return false;
}

} // namespace

std::vector<std::string> validate_upgrade_all_invocation(
        const ParsedCliArguments& parsed) {
    if(parsed.operation !=
       cli_authority::operation_spec(
               cli_authority::OperationId::UpgradeAll)
               .token) {
        return {};
    }

    std::vector<std::string> errors;
    for(const ParsedCliToken& token : parsed.tokens) {
        switch(token.role) {
        case CliTokenRole::Operation:
            break;
        case CliTokenRole::MoguetGlobalOption:
            if(!is_supported_upgrade_all_global_option(token.value)) {
                errors.push_back(
                        localization::format_translated_message(
                                // TRANSLATORS: The first placeholder is the
                                // literal command name "upgrade-all"; the
                                // second is a runtime option token.
                                "Unsupported {} option: {}", COMMAND_NAME,
                                token.value));
            }
            break;
        case CliTokenRole::Target:
            errors.push_back(localization::format_translated_message(
                    // TRANSLATORS: The first placeholder is the literal
                    // command name "upgrade-all"; the second is an operand.
                    "{} does not accept target operands: {}", COMMAND_NAME,
                    token.value));
            break;
        case CliTokenRole::OpaqueOperand:
            errors.push_back(localization::format_translated_message(
                    // TRANSLATORS: The first placeholder is the literal
                    // command name "upgrade-all"; the second is an operand.
                    "{} does not accept opaque operands: {}", COMMAND_NAME,
                    token.value));
            break;
        case CliTokenRole::PacmanOption:
        case CliTokenRole::PacmanOptionValue:
            errors.push_back(localization::format_translated_message(
                    // TRANSLATORS: The first placeholder is the literal
                    // command name "upgrade-all"; the second is a runtime
                    // option or operand.
                    "Unsupported {} option or operand: {}", COMMAND_NAME,
                    token.value));
            break;
        case CliTokenRole::EndOfOptions:
            // `--` itself carries no target; any following opaque operand gets
            // its own diagnostic. A bare marker is still unsupported.
            if(parsed.targets.empty()) {
                errors.push_back(localization::format_translated_message(
                        // TRANSLATORS: The placeholders are the literal
                        // command name "upgrade-all" and CLI marker "--".
                        "{} does not accept the {} operand marker.",
                        COMMAND_NAME, "--"));
            }
            break;
        }
    }
    return errors;
}

int cmd_upgrade_all(const AppConfig& config) {
    try {
        UpgradeAllOperationPreparation preparation =
                prepare_upgrade_all_operation(config);
        UpgradeAllOperationResult result =
                std::holds_alternative<PreparedUpgradeAllOperation>(
                        preparation)
                ? execute_prepared_upgrade_all_operation(
                          std::move(std::get<PreparedUpgradeAllOperation>(
                                  preparation)),
                          config)
                : std::move(std::get<UpgradeAllOperationResult>(preparation));

        print_operation_result(result);
        return result.is_success() ? 0 : 1;
    } catch(const std::exception& error) {
        Logger::error(localization::format_translated_message(
                // TRANSLATORS: The first placeholder is the literal command
                // name "upgrade-all"; the second is a runtime diagnostic.
                "Unexpected {} command failure: {}", COMMAND_NAME,
                error.what()));
        return 1;
    } catch(...) {
        Logger::error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal command name
                // "upgrade-all".
                "Unexpected {} command failure: unknown exception.",
                COMMAND_NAME));
        return 1;
    }
}
