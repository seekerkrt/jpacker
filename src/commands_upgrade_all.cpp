#include "commands_upgrade_all.hpp"

#include "app_config.hpp"
#include "aur_update_cli_presentation.hpp"
#include "cli_parser.hpp"
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

std::string_view package_state_label(PackageStateChange state) {
    switch(state) {
    case PackageStateChange::NoChange:
        return "no change";
    case PackageStateChange::Changed:
        return "changed";
    case PackageStateChange::Unknown:
        return "unknown";
    }
    throw std::logic_error("Unknown package-state change value.");
}

std::string_view aggregate_package_state_label(PackageStateChange state) {
    switch(state) {
    case PackageStateChange::NoChange:
        return "no package state change";
    case PackageStateChange::Changed:
        return "package state changed";
    case PackageStateChange::Unknown:
        return "package state change unknown";
    }
    throw std::logic_error("Unknown aggregate package-state change value.");
}

std::string_view aggregate_status_label(UpgradeAllOperationStatus status) {
    switch(status) {
    case UpgradeAllOperationStatus::Completed:
        return "upgrade-all completed";
    case UpgradeAllOperationStatus::NoUpdates:
        return "upgrade-all completed: no updates";
    case UpgradeAllOperationStatus::BlockedBeforeMutation:
        return "upgrade-all blocked before mutation";
    case UpgradeAllOperationStatus::StoppedOnSystemFailure:
        return "upgrade-all stopped on system failure";
    case UpgradeAllOperationStatus::StoppedOnSourceFailure:
        return "upgrade-all stopped on source failure";
    case UpgradeAllOperationStatus::StoppedAfterSourceCleanupFailure:
        return "upgrade-all stopped after source cleanup failure";
    case UpgradeAllOperationStatus::StoppedBeforeAurExecution:
        return "upgrade-all stopped before AUR execution";
    case UpgradeAllOperationStatus::StoppedOnAurFailure:
        return "upgrade-all stopped on AUR failure";
    case UpgradeAllOperationStatus::StoppedAfterAurCleanupFailure:
        return "upgrade-all stopped after AUR cleanup failure";
    case UpgradeAllOperationStatus::InconsistentResult:
        return "upgrade-all result inconsistent";
    }
    throw std::logic_error("Unknown upgrade-all operation status.");
}

std::string_view aggregate_phase_label(UpgradeAllOperationPhase phase) {
    switch(phase) {
    case UpgradeAllOperationPhase::None:
        return "none";
    case UpgradeAllOperationPhase::Preparation:
        return "preparation";
    case UpgradeAllOperationPhase::System:
        return "system";
    case UpgradeAllOperationPhase::RegisteredSource:
        return "registered source";
    case UpgradeAllOperationPhase::ForeignInventory:
        return "foreign inventory";
    case UpgradeAllOperationPhase::AurQuery:
        return "AUR query";
    case UpgradeAllOperationPhase::AurPreparation:
        return "AUR preparation";
    case UpgradeAllOperationPhase::AurExecution:
        return "AUR execution";
    case UpgradeAllOperationPhase::Reduction:
        return "reduction";
    }
    throw std::logic_error("Unknown upgrade-all operation phase.");
}

std::string_view not_attempted_reason_label(
        UpgradeAllNotAttemptedReason reason) {
    switch(reason) {
    case UpgradeAllNotAttemptedReason::PreparationBlocked:
        return "preparation blocked";
    case UpgradeAllNotAttemptedReason::SystemFailure:
        return "system failure";
    case UpgradeAllNotAttemptedReason::SourceFailure:
        return "source failure";
    case UpgradeAllNotAttemptedReason::SourceCleanupFailure:
        return "source cleanup failure";
    case UpgradeAllNotAttemptedReason::SystemSourceIncomplete:
        return "system/source phase incomplete";
    case UpgradeAllNotAttemptedReason::ForeignInventoryFailure:
        return "foreign inventory failure";
    case UpgradeAllNotAttemptedReason::PriorAggregateInconsistency:
        return "prior aggregate inconsistency";
    }
    throw std::logic_error("Unknown upgrade-all NotAttempted reason.");
}

std::string_view aur_phase_status_label(UpgradeAllAurPhaseStatus status) {
    switch(status) {
    case UpgradeAllAurPhaseStatus::NotAttempted:
        return "not attempted";
    case UpgradeAllAurPhaseStatus::NoUpdates:
        return "no updates";
    case UpgradeAllAurPhaseStatus::Completed:
        return "completed";
    case UpgradeAllAurPhaseStatus::BlockedBeforeExecution:
        return "blocked before execution";
    case UpgradeAllAurPhaseStatus::StoppedOnWorkItemFailure:
        return "stopped on work-item failure";
    case UpgradeAllAurPhaseStatus::StoppedAfterCleanupFailure:
        return "stopped after cleanup failure";
    case UpgradeAllAurPhaseStatus::InconsistentResult:
        return "inconsistent result";
    }
    throw std::logic_error("Unknown upgrade-all AUR phase status.");
}

std::string_view system_phase_status_label(SystemUpgradePhaseStatus status) {
    switch(status) {
    case SystemUpgradePhaseStatus::NotAttempted:
        return "not attempted";
    case SystemUpgradePhaseStatus::Completed:
        return "completed";
    case SystemUpgradePhaseStatus::Failed:
        return "failed";
    }
    throw std::logic_error("Unknown system upgrade phase status.");
}

std::string_view source_failure_label(
        RegisteredSourceUpgradeFailureKind kind) {
    switch(kind) {
    case RegisteredSourceUpgradeFailureKind::None:
        return "reason unavailable";
    case RegisteredSourceUpgradeFailureKind::InvalidPreferenceName:
        return "invalid source preference name";
    case RegisteredSourceUpgradeFailureKind::PreferenceUnavailable:
        return "source preference unavailable";
    case RegisteredSourceUpgradeFailureKind::PackageMetadataUnavailable:
        return "package metadata unavailable";
    case RegisteredSourceUpgradeFailureKind::BuildOrInstallFailed:
        return "build or install failed";
    case RegisteredSourceUpgradeFailureKind::
            CleanupFailedAfterPackageTransaction:
        return "cleanup failed after package transaction";
    case RegisteredSourceUpgradeFailureKind::UpdateStatusUnknownSkipped:
        return "package update status unknown";
    case RegisteredSourceUpgradeFailureKind::PriorPhaseStopped:
        return "prior phase stopped";
    case RegisteredSourceUpgradeFailureKind::UnknownException:
        return "unknown exception";
    }
    throw std::logic_error("Unknown registered source failure kind.");
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
        return "updated";
    case RegisteredSourceUpgradeStatus::NoChange:
        return "no change";
    case RegisteredSourceUpgradeStatus::Failed:
        return "failed: " + source_diagnostic_or_reason(source);
    case RegisteredSourceUpgradeStatus::UpdatedCleanupFailed:
        return "updated, but cleanup failed";
    case RegisteredSourceUpgradeStatus::NoChangeCleanupFailed:
        return "no package change, but cleanup failed";
    case RegisteredSourceUpgradeStatus::NotAttempted:
        return "not attempted: " + source_diagnostic_or_reason(source);
    case RegisteredSourceUpgradeStatus::Unsupported:
        return "unsupported: " + source_diagnostic_or_reason(source);
    case RegisteredSourceUpgradeStatus::Incomplete:
        return "incomplete: " + source_diagnostic_or_reason(source);
    }
    throw std::logic_error("Unknown registered source status.");
}

std::string_view system_source_phase_label(SystemSourceUpgradePhase phase) {
    switch(phase) {
    case SystemSourceUpgradePhase::None:
        return "none";
    case SystemSourceUpgradePhase::Preparation:
        return "preparation";
    case SystemSourceUpgradePhase::System:
        return "system";
    case SystemSourceUpgradePhase::RegisteredSource:
        return "registered source";
    }
    throw std::logic_error("Unknown system/source phase.");
}

std::string_view system_issue_kind_label(SystemSourceUpgradeIssueKind kind) {
    switch(kind) {
    case SystemSourceUpgradeIssueKind::PreferenceEnumerationUnavailable:
        return "source preference enumeration unavailable";
    case SystemSourceUpgradeIssueKind::PreferenceUnavailable:
        return "source preference unavailable";
    case SystemSourceUpgradeIssueKind::SourceIdentityResolutionFailed:
        return "source identity resolution failed";
    case SystemSourceUpgradeIssueKind::SourceWorkItemPreparationFailed:
        return "source work-item preparation failed";
    case SystemSourceUpgradeIssueKind::SourceInvocationPreparationFailed:
        return "source invocation preparation failed";
    case SystemSourceUpgradeIssueKind::SourceBaselineSnapshotUnavailable:
        return "source baseline snapshot unavailable";
    case SystemSourceUpgradeIssueKind::SystemPackageSnapshotUnavailable:
        return "system package snapshot unavailable";
    case SystemSourceUpgradeIssueKind::PostSystemSourceSnapshotUnavailable:
        return "post-system/source snapshot unavailable";
    case SystemSourceUpgradeIssueKind::InvalidPreferenceName:
        return "invalid source preference name";
    case SystemSourceUpgradeIssueKind::OptionSnapshotMismatch:
        return "option snapshot mismatch";
    case SystemSourceUpgradeIssueKind::PreparedCorrelationInconsistent:
        return "prepared source correlation inconsistent";
    case SystemSourceUpgradeIssueKind::PreparedCapabilityConsumed:
        return "prepared source capability consumed";
    case SystemSourceUpgradeIssueKind::UnknownPreparationFailure:
        return "unknown source preparation failure";
    }
    throw std::logic_error("Unknown system/source issue kind.");
}

std::string_view system_issue_impact_label(
        SystemSourceUpgradeIssueImpact impact) {
    switch(impact) {
    case SystemSourceUpgradeIssueImpact::ObservabilityOnly:
        return "observability only";
    case SystemSourceUpgradeIssueImpact::AffectsSuccess:
        return "affects success";
    case SystemSourceUpgradeIssueImpact::BlocksExecution:
        return "blocks execution";
    }
    throw std::logic_error("Unknown system/source issue impact.");
}

std::string_view system_warning_kind_label(
        SystemSourceUpgradeWarningKind kind) {
    switch(kind) {
    case SystemSourceUpgradeWarningKind::SourcePreference:
        return "source preference";
    case SystemSourceUpgradeWarningKind::InvalidPreferenceName:
        return "invalid source preference name";
    }
    throw std::logic_error("Unknown system/source warning kind.");
}

std::string_view package_metadata_error_label(PackageMetadataErrorCode code) {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationUnavailable:
        return "configuration unavailable";
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return "configuration malformed";
    case PackageMetadataErrorCode::InitializationFailed:
        return "database initialization failed";
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        return "local database unavailable";
    case PackageMetadataErrorCode::InvalidPackageName:
        return "invalid package name";
    case PackageMetadataErrorCode::QueryFailed:
        return "package query failed";
    case PackageMetadataErrorCode::MalformedMetadata:
        return "malformed package metadata";
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return "sync database unavailable";
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return "repository not configured";
    }
    throw std::logic_error("Unknown package metadata error code.");
}

std::string_view aggregate_warning_kind_label(
        UpgradeAllOperationWarningKind kind) {
    switch(kind) {
    case UpgradeAllOperationWarningKind::RegisteredSourcePreference:
        return "registered source preference";
    case UpgradeAllOperationWarningKind::AurPreparation:
        return "AUR preparation";
    }
    throw std::logic_error("Unknown upgrade-all warning kind.");
}

std::string_view aggregate_issue_kind_label(
        UpgradeAllOperationIssueKind kind) {
    switch(kind) {
    case UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid:
        return "explicit source adapter invalid";
    case UpgradeAllOperationIssueKind::OptionSnapshotMismatch:
        return "option snapshot mismatch";
    case UpgradeAllOperationIssueKind::SourceSnapshotMismatch:
        return "source snapshot mismatch";
    case UpgradeAllOperationIssueKind::ExplicitSourceCorrelationInconsistent:
        return "explicit source correlation inconsistent";
    case UpgradeAllOperationIssueKind::PreparedCapabilityConsumed:
        return "prepared capability consumed";
    case UpgradeAllOperationIssueKind::SystemSourceExecutionFailedUnexpectedly:
        return "system/source execution failed unexpectedly";
    case UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete:
        return "system/source phase incomplete";
    case UpgradeAllOperationIssueKind::ForeignInventoryConfigurationFailed:
        return "foreign inventory configuration failed";
    case UpgradeAllOperationIssueKind::ForeignInventoryReadFailed:
        return "foreign inventory read failed";
    case UpgradeAllOperationIssueKind::AurQueryFailed:
        return "AUR query failed";
    case UpgradeAllOperationIssueKind::FilteredAurPreparationFailed:
        return "filtered AUR preparation failed";
    case UpgradeAllOperationIssueKind::FilteredAurExecutionFailed:
        return "filtered AUR execution failed";
    case UpgradeAllOperationIssueKind::
            DuplicateExclusionCorrelationInconsistent:
        return "duplicate exclusion correlation inconsistent";
    case UpgradeAllOperationIssueKind::
            ExternalSatisfactionCorrelationInconsistent:
        return "external satisfaction correlation inconsistent";
    case UpgradeAllOperationIssueKind::UnknownFailure:
        return "unknown aggregate failure";
    }
    throw std::logic_error("Unknown upgrade-all issue kind.");
}

std::string_view adapter_issue_kind_label(
        UpgradeAllExplicitSourceAdapterIssueKind kind) {
    switch(kind) {
    case UpgradeAllExplicitSourceAdapterIssueKind::PreferencePackageNameMissing:
        return "preference package name missing";
    case UpgradeAllExplicitSourceAdapterIssueKind::PackageBaseUnavailable:
        return "PackageBase unavailable";
    case UpgradeAllExplicitSourceAdapterIssueKind::
            CanonicalSourceIdentityUnavailable:
        return "canonical source identity unavailable";
    case UpgradeAllExplicitSourceAdapterIssueKind::
            DuplicateOriginalPreferenceIndex:
        return "duplicate original preference index";
    case UpgradeAllExplicitSourceAdapterIssueKind::
            AdapterCorrelationInconsistent:
        return "adapter correlation inconsistent";
    }
    throw std::logic_error("Unknown explicit source adapter issue kind.");
}

std::string_view preflight_reason_label(AurUpdateExecutionReason reason) {
    switch(reason) {
    case AurUpdateExecutionReason::None:
        return "none";
    case AurUpdateExecutionReason::UpToDate:
        return "up to date";
    case AurUpdateExecutionReason::NonAurForeign:
        return "non-AUR foreign";
    case AurUpdateExecutionReason::AurMetadataUnavailable:
        return "AUR metadata unavailable";
    case AurUpdateExecutionReason::VersionComparisonUnavailable:
        return "version comparison unavailable";
    case AurUpdateExecutionReason::InstalledReasonUnknown:
        return "installed reason unknown";
    case AurUpdateExecutionReason::UpdatePlanInconsistent:
        return "update plan inconsistent";
    case AurUpdateExecutionReason::DuplicateUpdateTarget:
        return "duplicate update target";
    case AurUpdateExecutionReason::RepositoryMetadataUnavailable:
        return "repository metadata unavailable";
    case AurUpdateExecutionReason::AurDependencyMetadataUnavailable:
        return "AUR dependency metadata unavailable";
    case AurUpdateExecutionReason::ProviderMetadataUnavailable:
        return "provider metadata unavailable";
    case AurUpdateExecutionReason::UnresolvedDependency:
        return "unresolved dependency";
    case AurUpdateExecutionReason::VersionConstraintUnverified:
        return "version constraint unverified";
    case AurUpdateExecutionReason::DependencyCycle:
        return "dependency cycle";
    case AurUpdateExecutionReason::BuildPlanInconsistent:
        return "build plan inconsistent";
    case AurUpdateExecutionReason::PackageBaseMismatch:
        return "package base mismatch";
    case AurUpdateExecutionReason::SplitPackageSelectionRequired:
        return "split package selection required";
    case AurUpdateExecutionReason::MultiplePackageTargetsForPackageBase:
        return "multiple package targets for package base";
    case AurUpdateExecutionReason::AmbiguousProvider:
        return "ambiguous provider";
    case AurUpdateExecutionReason::ConflictsOrReplacesUnresolved:
        return "conflicts/replaces unresolved";
    }
    throw std::logic_error("Unknown AUR update preflight reason.");
}

std::string_view preparation_reason_label(AurUpdatePreparationReason reason) {
    switch(reason) {
    case AurUpdatePreparationReason::None:
        return "none";
    case AurUpdatePreparationReason::BlockingPreflight:
        return "blocking preflight";
    case AurUpdatePreparationReason::PreflightInconsistent:
        return "preflight inconsistent";
    case AurUpdatePreparationReason::BuildPlanMissing:
        return "build plan missing";
    case AurUpdatePreparationReason::BuildPlanOrderEmpty:
        return "build plan order empty";
    case AurUpdatePreparationReason::RootAttributionInconsistent:
        return "root attribution inconsistent";
    case AurUpdatePreparationReason::PackageTargetAttributionInconsistent:
        return "package target attribution inconsistent";
    case AurUpdatePreparationReason::DesiredInstallReasonMissing:
        return "desired install reason missing";
    case AurUpdatePreparationReason::SourcePreferenceUnavailable:
        return "source preference unavailable";
    case AurUpdatePreparationReason::SourcePreferencePkgdestConflict:
        return "source preference PKGDEST conflict";
    case AurUpdatePreparationReason::StaticWorkItemInvalid:
        return "static work item invalid";
    case AurUpdatePreparationReason::PacmanDatabaseUnavailable:
        return "pacman database unavailable";
    case AurUpdatePreparationReason::GenericPreparationInconsistent:
        return "generic preparation inconsistent";
    case AurUpdatePreparationReason::BuildUnitSelectionInconsistent:
        return "build unit selection inconsistent";
    case AurUpdatePreparationReason::ExternalSatisfactionInconsistent:
        return "external satisfaction inconsistent";
    }
    throw std::logic_error("Unknown AUR update preparation reason.");
}

std::string_view reduction_stage_label(AurUpdateOperationReductionStage stage) {
    switch(stage) {
    case AurUpdateOperationReductionStage::Preflight:
        return "preflight";
    case AurUpdateOperationReductionStage::Preparation:
        return "preparation";
    case AurUpdateOperationReductionStage::Execution:
        return "execution";
    }
    throw std::logic_error("Unknown AUR reduction stage.");
}

std::string_view reduction_reason_label(
        AurUpdateOperationReductionReason reason) {
    switch(reason) {
    case AurUpdateOperationReductionReason::
            DuplicatePreflightUpdatePlanIndex:
        return "duplicate preflight update plan index";
    case AurUpdateOperationReductionReason::
            OutOfRangePreflightUpdatePlanIndex:
        return "out-of-range preflight update plan index";
    case AurUpdateOperationReductionReason::
            PreflightTargetOrderInconsistent:
        return "preflight target order inconsistent";
    case AurUpdateOperationReductionReason::DuplicatePreparationAttribution:
        return "duplicate preparation attribution";
    case AurUpdateOperationReductionReason::UnknownPreparationUpdatePlanIndex:
        return "unknown preparation update plan index";
    case AurUpdateOperationReductionReason::
            PreparationAttributionInconsistent:
        return "preparation attribution inconsistent";
    case AurUpdateOperationReductionReason::
            PreparationTargetSnapshotInconsistent:
        return "preparation target snapshot inconsistent";
    case AurUpdateOperationReductionReason::DuplicateExecutionWorkItemIndex:
        return "duplicate execution work item index";
    case AurUpdateOperationReductionReason::
            ExecutionWorkItemOrderInconsistent:
        return "execution work item order inconsistent";
    case AurUpdateOperationReductionReason::DuplicateExecutionAttribution:
        return "duplicate execution attribution";
    case AurUpdateOperationReductionReason::UnknownExecutionUpdatePlanIndex:
        return "unknown execution update plan index";
    case AurUpdateOperationReductionReason::MissingExecutionAttribution:
        return "missing execution attribution";
    case AurUpdateOperationReductionReason::
            DuplicateExecutionChildAttribution:
        return "duplicate execution child attribution";
    case AurUpdateOperationReductionReason::
            MissingExecutionChildAttribution:
        return "missing execution child attribution";
    case AurUpdateOperationReductionReason::
            UnexpectedExecutionChildAttribution:
        return "unexpected execution child attribution";
    case AurUpdateOperationReductionReason::
            UnknownExecutionChildUpdatePlanIndex:
        return "unknown execution child update plan index";
    case AurUpdateOperationReductionReason::
            ExecutionChildSnapshotInconsistent:
        return "execution child snapshot inconsistent";
    case AurUpdateOperationReductionReason::UnexpectedSelectedArtifact:
        return "unexpected selected artifact";
    case AurUpdateOperationReductionReason::
            UnexpectedUnselectedArtifactIdentity:
        return "unexpected unselected artifact identity";
    case AurUpdateOperationReductionReason::
            ExecutionResultWithPreparationIssues:
        return "execution result with preparation issues";
    case AurUpdateOperationReductionReason::MissingExecutionResult:
        return "missing execution result";
    case AurUpdateOperationReductionReason::UnknownEnumValue:
        return "unknown enum value";
    case AurUpdateOperationReductionReason::WorkItemResultInconsistent:
        return "work item result inconsistent";
    case AurUpdateOperationReductionReason::InvocationResultInconsistent:
        return "invocation result inconsistent";
    case AurUpdateOperationReductionReason::OtherCorrelationInconsistent:
        return "other correlation inconsistency";
    }
    throw std::logic_error("Unknown AUR reduction reason.");
}

std::string_view filtered_issue_kind_label(
        FilteredAurUpdateOperationIssueKind kind) {
    switch(kind) {
    case FilteredAurUpdateOperationIssueKind::UnknownUpdateClassification:
        return "unknown update classification";
    case FilteredAurUpdateOperationIssueKind::TargetPlannerMappingInconsistent:
        return "target/planner mapping inconsistent";
    case FilteredAurUpdateOperationIssueKind::FilteredTargetMappingInconsistent:
        return "filtered target mapping inconsistent";
    case FilteredAurUpdateOperationIssueKind::
            PreflightTargetMappingInconsistent:
        return "preflight target mapping inconsistent";
    case FilteredAurUpdateOperationIssueKind::
            PreflightInvocationIndexOutOfRange:
        return "preflight invocation index out of range";
    case FilteredAurUpdateOperationIssueKind::
            PreflightInvocationIdentityMismatch:
        return "preflight invocation identity mismatch";
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIndexMissing:
        return "build-plan root index missing";
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIndexOutOfRange:
        return "build-plan root index out of range";
    case FilteredAurUpdateOperationIssueKind::BuildPlanRootIdentityMismatch:
        return "build-plan root identity mismatch";
    case FilteredAurUpdateOperationIssueKind::
            BuildPlanRootPackageIdentityMismatch:
        return "build-plan root package identity mismatch";
    case FilteredAurUpdateOperationIssueKind::BuildUnitOrderIdentityMismatch:
        return "build-unit order identity mismatch";
    case FilteredAurUpdateOperationIssueKind::
            BuildUnitRootAttributionInconsistent:
        return "build-unit root attribution inconsistent";
    case FilteredAurUpdateOperationIssueKind::
            BuildUnitSelectionMappingInconsistent:
        return "build-unit selection mapping inconsistent";
    case FilteredAurUpdateOperationIssueKind::
            ExecutionBuildUnitMappingInconsistent:
        return "execution build-unit mapping inconsistent";
    case FilteredAurUpdateOperationIssueKind::ReducedTargetMappingInconsistent:
        return "reduced target mapping inconsistent";
    }
    throw std::logic_error("Unknown filtered AUR operation issue kind.");
}

std::string_view planning_issue_kind_label(UpgradeAllPlanningIssueKind kind) {
    switch(kind) {
    case UpgradeAllPlanningIssueKind::ExplicitPreferencePackageNameMissing:
        return "explicit preference package name missing";
    case UpgradeAllPlanningIssueKind::ExplicitProducedPackageNameMissing:
        return "explicit produced package name missing";
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseAbsent:
        return "explicit PackageBase absent";
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseResolutionFailed:
        return "explicit PackageBase resolution failed";
    case UpgradeAllPlanningIssueKind::ExplicitPackageBaseEmpty:
        return "explicit PackageBase empty";
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityAbsent:
        return "explicit source identity absent";
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityResolutionFailed:
        return "explicit source identity resolution failed";
    case UpgradeAllPlanningIssueKind::ExplicitSourceIdentityEmpty:
        return "explicit source identity empty";
    case UpgradeAllPlanningIssueKind::
            ConflictingExplicitSourceIdentityDefinition:
        return "conflicting explicit source identity definition";
    case UpgradeAllPlanningIssueKind::ConflictingExplicitPackageName:
        return "conflicting explicit package name";
    case UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase:
        return "conflicting explicit PackageBase";
    case UpgradeAllPlanningIssueKind::AurTargetPackageNameMissing:
        return "AUR target package name missing";
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent:
        return "AUR target PackageBase absent";
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseResolutionFailed:
        return "AUR target PackageBase resolution failed";
    case UpgradeAllPlanningIssueKind::AurTargetPackageBaseEmpty:
        return "AUR target PackageBase empty";
    case UpgradeAllPlanningIssueKind::UnsupportedAurTarget:
        return "unsupported AUR target";
    case UpgradeAllPlanningIssueKind::IncompleteAurTarget:
        return "incomplete AUR target";
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseAbsent:
        return "build-unit PackageBase absent";
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseResolutionFailed:
        return "build-unit PackageBase resolution failed";
    case UpgradeAllPlanningIssueKind::BuildUnitPackageBaseEmpty:
        return "build-unit PackageBase empty";
    case UpgradeAllPlanningIssueKind::BuildUnitHasNoRootAttribution:
        return "build unit has no root attribution";
    case UpgradeAllPlanningIssueKind::BuildUnitTargetIndexOutOfRange:
        return "build-unit target index out of range";
    case UpgradeAllPlanningIssueKind::DuplicateSelectedTargetPackageBase:
        return "duplicate selected target PackageBase";
    case UpgradeAllPlanningIssueKind::DuplicateSelectedBuildUnitPackageBase:
        return "duplicate selected build-unit PackageBase";
    }
    throw std::logic_error("Unknown upgrade-all planning issue kind.");
}

std::string_view duplicate_reason_label(UpgradeAllTargetDisposition disposition) {
    switch(disposition) {
    case UpgradeAllTargetDisposition::ExcludedByExplicitPackageName:
        return "package name handled by explicit source preference";
    case UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase:
        return "PackageBase handled by explicit source preference";
    case UpgradeAllTargetDisposition::Selected:
    case UpgradeAllTargetDisposition::Unsupported:
    case UpgradeAllTargetDisposition::IdentityIncomplete:
    case UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity:
    case UpgradeAllTargetDisposition::ConflictingSelectedPackageBase:
        throw std::logic_error(
                "Non-exclusion target disposition reached duplicate presentation.");
    }
    throw std::logic_error("Unknown upgrade-all target disposition.");
}

std::string_view build_unit_role_label(UpgradeAllBuildUnitRole role) {
    switch(role) {
    case UpgradeAllBuildUnitRole::Root:
        return "root";
    case UpgradeAllBuildUnitRole::RuntimeDependency:
        return "runtime dependency";
    case UpgradeAllBuildUnitRole::BuildDependency:
        return "build dependency";
    case UpgradeAllBuildUnitRole::CheckDependency:
        return "check dependency";
    }
    throw std::logic_error("Unknown upgrade-all build-unit role.");
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
    throw std::logic_error("Unknown AUR update preflight reason.");
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
    return "reason unavailable";
}

std::string aur_target_status_label(
        const AurUpdateOperationTargetResult& target,
        AurUpdateOperationStatus operation_status) {
    switch(target.status) {
    case AurUpdateOperationTargetStatus::Updated:
        return "updated";
    case AurUpdateOperationTargetStatus::NoChange:
        return "no change";
    case AurUpdateOperationTargetStatus::Skipped:
        return "skipped: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Unsupported:
        return "unsupported: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Incomplete:
        return "incomplete: " + target_reason_label(target);
    case AurUpdateOperationTargetStatus::Failed:
        return "failed: " + aur_update_cli_target_failure_summary(target);
    case AurUpdateOperationTargetStatus::UpdatedCleanupFailed:
        return "updated, but cleanup failed";
    case AurUpdateOperationTargetStatus::NoChangeCleanupFailed:
        return "no package change, but cleanup failed";
    case AurUpdateOperationTargetStatus::NotAttempted:
        if(target.execution_failure_kind ==
           AurUpdateWorkItemFailureKind::PriorWorkItemStopped) {
            return "not attempted: prior work item stopped";
        }
        switch(operation_status) {
        case AurUpdateOperationStatus::BlockedBeforeExecution:
            return "not attempted: operation blocked before execution";
        case AurUpdateOperationStatus::InconsistentResult:
            return "not attempted: result inconsistent";
        case AurUpdateOperationStatus::StoppedOnWorkItemFailure:
        case AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure:
            return "not attempted: prior work item stopped";
        case AurUpdateOperationStatus::NoUpdates:
        case AurUpdateOperationStatus::Completed:
            return "not attempted: result inconsistent";
        }
        throw std::logic_error("Unknown AUR update operation status.");
    }
    throw std::logic_error("Unknown AUR update target status.");
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
    return package_names.empty() ? "unknown packages"
                                 : join_strings(package_names);
}

void print_system_phase(const SystemSourceUpgradeResult& result) {
    std::cout << "system: " << system_phase_status_label(result.system.status)
              << std::endl;
    std::cout << "package state: "
              << package_state_label(result.system.package_state_change)
              << std::endl;
}

void print_registered_sources(const SystemSourceUpgradeResult& result) {
    if(result.registered_source_results.empty()) {
        std::cout << "registered source packages: none" << std::endl;
        return;
    }

    // POLICY(#281): vector順はprepared preference snapshot順。表示側で
    // sort/deduplicateせず、各preferenceへexactly one outcomeを出す。
    for(const RegisteredSourceUpgradeResult& source :
        result.registered_source_results) {
        const std::string package_name = source.preference_package_name.empty()
                ? "<unknown preference>"
                : source.preference_package_name;
        std::cout << "registered source: " << package_name << ": "
                  << source_status_label(source) << std::endl;
        if(source.resolved_package_base.has_value()) {
            std::cout << "  PackageBase: " << *source.resolved_package_base
                      << std::endl;
        }
        if(source.canonical_source_identity_key.has_value()) {
            std::cout << "  canonical source identity: "
                      << *source.canonical_source_identity_key << std::endl;
        }
    }
}

void print_aur_phase(
        const UpgradeAllAurPhaseResult& aur,
        const AurUpdateCliPresentation* presentation) {
    if(aur.status == UpgradeAllAurPhaseStatus::NotAttempted) {
        if(!aur.not_attempted_reason.has_value()) {
            throw std::logic_error(
                    "AUR phase is NotAttempted without a typed reason.");
        }
        const std::string_view reason =
                not_attempted_reason_label(*aur.not_attempted_reason);
        std::cout << "AUR phase not attempted: " << reason
                  << std::endl;
        return;
    }

    const std::string_view phase_status = aur_phase_status_label(aur.status);
    std::cout << "AUR phase: " << phase_status << std::endl;
    if(!aur.operation_result.has_value()) {
        std::cout << "AUR targets: unavailable" << std::endl;
        return;
    }

    const AurUpdateOperationResult& operation =
            aur.operation_result->reduced_operation_result;
    if(operation.targets.empty()) {
        std::cout << "AUR targets: none" << std::endl;
    } else {
        for(const AurUpdateOperationTargetResult& target : operation.targets) {
            const std::string status =
                    aur_target_status_label(target, operation.status);
            std::cout << "AUR target: " << target.update.installed_name << ": "
                      << status << std::endl;
            if(target.package_base.has_value()) {
                std::cout << "  PackageBase: " << *target.package_base
                          << std::endl;
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
        std::cout << "AUR phase not attempted: foreign inventory failure"
                  << std::endl;
        return;
    }
    print_aur_phase(result.aur, presentation);
}

void print_duplicate_exclusions(const UpgradeAllOperationResult& result) {
    for(const UpgradeAllDuplicateExcludedAurTarget& exclusion :
        result.duplicate_excluded_aur_targets) {
        const std::string_view reason = duplicate_reason_label(
                exclusion.planner_entry.disposition);
        std::cout << "excluded from AUR update: "
                  << exclusion.query_entry.installed_name << std::endl;
        std::cout << "reason: " << reason << std::endl;

        if(!exclusion.planner_entry.explicit_source.has_value()) {
            throw std::logic_error(
                    "Duplicate exclusion has no explicit source attribution.");
        }
        const UpgradeAllExplicitSourceAttribution& attribution =
                *exclusion.planner_entry.explicit_source;
        if(attribution.matched_package_name.has_value()) {
            std::cout << "matched explicit source package: "
                      << *attribution.matched_package_name << std::endl;
        }
        if(attribution.matched_package_base.has_value()) {
            std::cout << "matched PackageBase: "
                      << *attribution.matched_package_base << std::endl;
        }
        for(const std::string& identity : attribution.source_identity_keys) {
            std::cout << "canonical source identity: " << identity
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
            throw std::logic_error(
                    "External satisfaction has no explicit source identity.");
        }
        std::vector<std::string_view> role_labels;
        role_labels.reserve(external.root_correlations.size());
        for(const FilteredAurUpdateBuildUnitRootCorrelation& root :
            external.root_correlations) {
            role_labels.push_back(build_unit_role_label(root.role));
        }

        std::cout << "AUR build unit externally satisfied: "
                  << unit.package_base << std::endl;
        std::cout << "provided by explicit source preference: "
                  << join_strings(
                             unit.external_satisfaction.source_identity_keys)
                  << std::endl;
        if(unit.external_satisfaction.matched_package_name.has_value()) {
            std::cout << "matched explicit source package: "
                      << *unit.external_satisfaction.matched_package_name
                      << std::endl;
        }
        if(unit.external_satisfaction.matched_package_base.has_value()) {
            std::cout << "matched PackageBase: "
                      << *unit.external_satisfaction.matched_package_base
                      << std::endl;
        }
        for(std::size_t index = 0;
            index < external.root_correlations.size(); ++index) {
            const FilteredAurUpdateBuildUnitRootCorrelation& root =
                    external.root_correlations[index];
            std::cout << "affected AUR root: "
                      << root.preflight_root.requested_name << " ("
                      << role_labels[index] << ")" << std::endl;
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
                warning.preference_package_name.value_or("unknown preference");
        Logger::warn(
                "system/source warning: " +
                std::string(system_warning_kind_label(warning.kind)) + ": " +
                package_name + ": " + warning.diagnostic);
        printed_warning_keys.insert(package_name + "\n" + warning.diagnostic);
    }
}

void print_system_failures(const SystemSourceUpgradeResult& result) {
    if(result.system.diagnostic.has_value() &&
       !result.system.diagnostic->empty()) {
        Logger::error("system failure: " + *result.system.diagnostic);
    }
    for(const RegisteredSourceUpgradeResult& source :
        result.registered_source_results) {
        if((source.status == RegisteredSourceUpgradeStatus::Failed ||
            source.status == RegisteredSourceUpgradeStatus::Unsupported ||
            source.status == RegisteredSourceUpgradeStatus::Incomplete) &&
           source.diagnostic.has_value() && !source.diagnostic->empty()) {
            Logger::error(
                    "registered source failure: " +
                    source.preference_package_name + ": " +
                    *source.diagnostic);
        }
        if((source.status ==
                    RegisteredSourceUpgradeStatus::UpdatedCleanupFailed ||
            source.status ==
                    RegisteredSourceUpgradeStatus::NoChangeCleanupFailed) &&
           source.cleanup_diagnostic.has_value() &&
           !source.cleanup_diagnostic->empty()) {
            Logger::error(
                    "registered source cleanup failure: " +
                    source.preference_package_name + ": " +
                    *source.cleanup_diagnostic);
        }
    }
}

void print_system_issues_and_diagnostics(
        const SystemSourceUpgradeResult& result) {
    std::set<std::string> issue_diagnostics;
    for(const SystemSourceUpgradeIssue& issue : result.issues) {
        std::string message =
                "system/source issue: " +
                std::string(system_issue_kind_label(issue.kind)) + " (" +
                std::string(system_issue_impact_label(issue.impact)) + ", " +
                std::string(system_source_phase_label(issue.phase)) + ")";
        if(issue.preference_package_name.has_value()) {
            message += ": " + *issue.preference_package_name;
        }
        if(issue.package_metadata_failure.has_value()) {
            message += " [" + std::string(package_metadata_error_label(
                                      issue.package_metadata_failure->code)) +
                    "]";
        }
        if(!issue.diagnostic.empty()) message += ": " + issue.diagnostic;
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
        Logger::error(
                "system/source diagnostic: " +
                std::string(system_source_phase_label(diagnostic.phase)) +
                ": " + diagnostic.diagnostic);
    }
}

void print_aur_query_failures(const FilteredAurUpdateExecutionResult& result) {
    for(const AurUpdateQueryFailure& failure :
        result.query_result.recoverable_failures) {
        Logger::error(
                "AUR query failure for " +
                join_query_package_names(failure.package_names) + ": " +
                failure.diagnostic);
    }
}

void print_planning_issues(const UpgradeAllPlan& plan) {
    for(const UpgradeAllPlanningIssue& issue : plan.issues) {
        std::string message = "AUR planner issue: " +
                std::string(planning_issue_kind_label(issue.kind));
        if(issue.package_name.has_value()) {
            message += ": package " + *issue.package_name;
        }
        if(issue.package_base.has_value()) {
            message += ": PackageBase " + *issue.package_base;
        }
        Logger::error(message);
    }
}

void print_filtered_mapping_issues(
        const FilteredAurUpdateExecutionResult& result) {
    for(const FilteredAurUpdateOperationIssue& issue : result.issues) {
        std::string message = "AUR mapping issue: " +
                std::string(filtered_issue_kind_label(issue.kind));
        if(issue.package_name.has_value()) {
            message += ": package " + *issue.package_name;
        }
        if(issue.package_base.has_value()) {
            message += ": PackageBase " + *issue.package_base;
        }
        if(!issue.diagnostic.empty()) message += ": " + issue.diagnostic;
        Logger::error(message);
    }
}

void print_aur_preflight_issues(const AurUpdateOperationResult& result) {
    for(const AurUpdateOperationTargetResult& target : result.targets) {
        for(const AurUpdateExecutionIssue& issue : target.preflight_issues) {
            if(is_normal_skip_reason(issue.reason)) continue;
            Logger::error(
                    "AUR preflight issue: " +
                    std::string(preflight_reason_label(issue.reason)) +
                    ": " + issue.diagnostic);
        }
    }
}

void print_aur_preparation_details(
        const AurUpdateOperationResult& result,
        std::set<std::string>& printed_warning_keys) {
    for(const AurUpdatePreparationWarning& warning :
        result.preparation_warnings) {
        const std::string package_name = warning.preference_name.empty()
                ? "unknown preference"
                : warning.preference_name;
        Logger::warn(
                "AUR preparation warning: " + package_name + ": " +
                warning.diagnostic);
        printed_warning_keys.insert(package_name + "\n" + warning.diagnostic);
    }
    for(const AurUpdatePreparationIssue& issue : result.preparation_issues) {
        Logger::error(
                "AUR preparation issue: " +
                std::string(preparation_reason_label(issue.reason)) + ": " +
                issue.diagnostic);
    }
}

void print_aur_reduction_issues(const AurUpdateOperationResult& result) {
    for(const AurUpdateOperationReductionIssue& issue :
        result.reduction_issues) {
        Logger::error(
                "AUR reduction issue: " +
                std::string(reduction_stage_label(issue.stage)) + ": " +
                std::string(reduction_reason_label(issue.reason)) + ": " +
                issue.diagnostic);
    }
}

void print_adapter_issues(const UpgradeAllOperationResult& result) {
    for(const UpgradeAllExplicitSourceAdapterIssue& issue :
        result.prepared_snapshot.explicit_source_adapter.issues) {
        std::string message = "explicit source adapter issue: " +
                std::string(adapter_issue_kind_label(issue.kind));
        if(issue.preference_package_name.has_value()) {
            message += ": " + *issue.preference_package_name;
        }
        if(!issue.diagnostic.empty()) message += ": " + issue.diagnostic;
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
        std::string message = "foreign inventory failure [" +
                std::string(package_metadata_error_label(
                        inventory.failure->code)) + "]";
        if(!inventory.failure->diagnostic.empty()) {
            message += ": " + inventory.failure->diagnostic;
            printed_diagnostics.insert(inventory.failure->diagnostic);
        }
        Logger::error(message);
    }

    if(inventory.diagnostic.has_value() &&
       !inventory.diagnostic->empty() &&
       !printed_diagnostics.contains(*inventory.diagnostic)) {
        Logger::error(
                "foreign inventory diagnostic: " + *inventory.diagnostic);
        printed_diagnostics.insert(*inventory.diagnostic);
    }

    if(!inventory.failure.has_value() && printed_diagnostics.empty()) {
        Logger::error("foreign inventory failure: diagnostic unavailable");
    }
    return printed_diagnostics;
}

void print_aggregate_warnings(
        const UpgradeAllOperationResult& result,
        std::set<std::string>& printed_warning_keys) {
    for(const UpgradeAllOperationWarning& warning : result.warnings) {
        const std::string package_name =
                warning.package_name.value_or("aggregate");
        const std::string key = package_name + "\n" + warning.diagnostic;
        if(printed_warning_keys.contains(key)) continue;
        Logger::warn(
                "upgrade-all warning: " +
                std::string(aggregate_warning_kind_label(warning.kind)) +
                " (" + std::string(aggregate_phase_label(warning.phase)) +
                "): " +
                package_name + ": " + warning.diagnostic);
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
        std::string message =
                "upgrade-all issue: " +
                std::string(aggregate_issue_kind_label(issue.kind)) + " (" +
                std::string(aggregate_phase_label(issue.phase)) + ")";
        if(issue.package_name.has_value()) {
            message += ": " + *issue.package_name;
        }
        if(issue.package_metadata_failure.has_value()) {
            message += " [" + std::string(package_metadata_error_label(
                                      issue.package_metadata_failure->code)) +
                    "]";
        }
        const bool suppress_raw_aur_execution_diagnostic =
                has_typed_aur_execution_snapshot &&
                issue.phase == UpgradeAllOperationPhase::AurExecution;
        if(!issue.diagnostic.empty() &&
           !suppress_raw_aur_execution_diagnostic) {
            message += ": " + issue.diagnostic;
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
            Logger::error("AUR phase diagnostic: " + *result.aur.diagnostic);
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
        Logger::error(
                "upgrade-all diagnostic: " +
                std::string(aggregate_phase_label(diagnostic.phase)) +
                ": " + diagnostic.diagnostic);
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
        std::cout << "partial completion" << std::endl;
    }
    if(result.has_not_attempted_phase()) {
        std::cout << "some phases were not attempted" << std::endl;
    }
    if(result.has_cleanup_failure()) {
        std::cout << "cleanup failure occurred" << std::endl;
    }
    if(result.has_duplicate_exclusions()) {
        std::cout << "duplicate AUR targets excluded" << std::endl;
    }
    if(result.has_external_satisfaction()) {
        std::cout << "AUR build units externally satisfied" << std::endl;
    }

    if((result.status == UpgradeAllOperationStatus::Completed ||
        result.status == UpgradeAllOperationStatus::NoUpdates) &&
       !result.is_success()) {
        Logger::error(
                "upgrade-all result contains failure details despite a successful aggregate status.");
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
    return option == "--noedit" || option == "--nodiff" ||
           option == "--noconfirm" || option == "--rebuild" ||
           option == "--cleanbuild";
}

} // namespace

std::vector<std::string> validate_upgrade_all_invocation(
        const ParsedCliArguments& parsed) {
    if(parsed.operation != "upgrade-all") return {};

    std::vector<std::string> errors;
    for(const ParsedCliToken& token : parsed.tokens) {
        switch(token.role) {
        case CliTokenRole::Operation:
            break;
        case CliTokenRole::MoguetGlobalOption:
            if(!is_supported_upgrade_all_global_option(token.value)) {
                errors.push_back(
                        "Unsupported upgrade-all option: " + token.value);
            }
            break;
        case CliTokenRole::Target:
            errors.push_back(
                    "upgrade-all does not accept target operands: " +
                    token.value);
            break;
        case CliTokenRole::OpaqueOperand:
            errors.push_back(
                    "upgrade-all does not accept opaque operands: " +
                    token.value);
            break;
        case CliTokenRole::PacmanOption:
        case CliTokenRole::PacmanOptionValue:
            errors.push_back(
                    "Unsupported upgrade-all option or operand: " +
                    token.value);
            break;
        case CliTokenRole::EndOfOptions:
            // `--` itself carries no target; any following opaque operand gets
            // its own diagnostic. A bare marker is still unsupported.
            if(parsed.targets.empty()) {
                errors.push_back(
                        "upgrade-all does not accept the -- operand marker.");
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
        Logger::error(
                "Unexpected upgrade-all command failure: " +
                std::string(error.what()));
        return 1;
    } catch(...) {
        Logger::error(
                "Unexpected upgrade-all command failure: unknown exception.");
        return 1;
    }
}
