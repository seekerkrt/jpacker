#include "diagnostic_projection.hpp"

#include "localization.hpp"

#include <stdexcept>
#include <utility>

namespace {

DiagnosticClass local_source_diagnostic_class(
    LocalSourceRootErrorCode code) noexcept {
    switch(code) {
        case LocalSourceRootErrorCode::InvalidInputPath:
        case LocalSourceRootErrorCode::Missing:
        case LocalSourceRootErrorCode::Symlink:
        case LocalSourceRootErrorCode::NotDirectory:
        case LocalSourceRootErrorCode::NotRegularFile:
        case LocalSourceRootErrorCode::OwnershipMismatch:
        case LocalSourceRootErrorCode::UnsafePermissions:
            return DiagnosticClass::Invalid;
        case LocalSourceRootErrorCode::MetadataFailure:
        case LocalSourceRootErrorCode::UnsafeMetadata:
            return DiagnosticClass::MetadataFailure;
        case LocalSourceRootErrorCode::PermissionDenied:
        case LocalSourceRootErrorCode::ReadFailure:
        case LocalSourceRootErrorCode::ConcurrentReplacement:
        case LocalSourceRootErrorCode::ContentChanged:
            return DiagnosticClass::Unavailable;
    }
    return DiagnosticClass::InternalInconsistency;
}

bool is_ambiguous_root_selection_issue(
    const RootPackageSelectionIssue& issue) noexcept {
    return std::holds_alternative<
        ConflictingRootPackageSelectionAlternatives>(issue);
}

DiagnosticRequiredAction root_selection_unavailable_action(
    RootPackageSelectionUnavailableReason reason) noexcept {
    switch(reason) {
        case RootPackageSelectionUnavailableReason::NonInteractiveInput:
        case RootPackageSelectionUnavailableReason::NoConfirm:
            return DiagnosticRequiredAction::EnableInteraction;
        case RootPackageSelectionUnavailableReason::NoCandidates:
            return DiagnosticRequiredAction::CorrectInput;
    }
    return DiagnosticRequiredAction::ResolveBlocker;
}

DiagnosticPhase local_source_diagnostic_phase(
    LocalSourceRootStage stage) noexcept {
    switch(stage) {
        case LocalSourceRootStage::SrcinfoInspection:
        case LocalSourceRootStage::SrcinfoOpen:
        case LocalSourceRootStage::SrcinfoRead:
        case LocalSourceRootStage::SrcinfoRevalidation:
        case LocalSourceRootStage::MetadataRevalidation:
            return DiagnosticPhase::Metadata;
        case LocalSourceRootStage::InvocationAnchorOpen:
        case LocalSourceRootStage::RootInspection:
        case LocalSourceRootStage::RootOpen:
        case LocalSourceRootStage::CanonicalPathResolution:
        case LocalSourceRootStage::RootRevalidation:
        case LocalSourceRootStage::PkgbuildInspection:
        case LocalSourceRootStage::PkgbuildOpen:
        case LocalSourceRootStage::PkgbuildRead:
        case LocalSourceRootStage::PkgbuildRevalidation:
            return DiagnosticPhase::Preflight;
    }
    return DiagnosticPhase::Preflight;
}

DiagnosticClass package_metadata_diagnostic_class(
    PackageMetadataErrorCode code) noexcept {
    switch(code) {
        case PackageMetadataErrorCode::InvalidPackageName:
            return DiagnosticClass::Invalid;
        case PackageMetadataErrorCode::MalformedMetadata:
            return DiagnosticClass::MetadataFailure;
        case PackageMetadataErrorCode::ConfigurationUnavailable:
        case PackageMetadataErrorCode::ConfigurationMalformed:
        case PackageMetadataErrorCode::InitializationFailed:
        case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        case PackageMetadataErrorCode::QueryFailed:
        case PackageMetadataErrorCode::SyncDatabaseUnavailable:
            return DiagnosticClass::QueryFailure;
        case PackageMetadataErrorCode::RepositoryNotConfigured:
            return DiagnosticClass::Unavailable;
    }
    return DiagnosticClass::InternalInconsistency;
}

DiagnosticClass sync_issue_diagnostic_class(
    SyncInstallPreparationIssueKind kind) noexcept {
    switch(kind) {
        case SyncInstallPreparationIssueKind::UnsupportedSourceSelection:
        case SyncInstallPreparationIssueKind::UnsupportedSourceOption:
        case SyncInstallPreparationIssueKind::SourceBuildOptionsUnsupported:
            return DiagnosticClass::Unsupported;
        case SyncInstallPreparationIssueKind::MissingAurTarget:
        case SyncInstallPreparationIssueKind::InvalidTarget:
            return DiagnosticClass::Invalid;
        case SyncInstallPreparationIssueKind::BuildPlanResolutionFailed:
            return DiagnosticClass::QueryFailure;
        case SyncInstallPreparationIssueKind::BuildPlanBlocked:
            return DiagnosticClass::Blocked;
        case SyncInstallPreparationIssueKind::SourceWorkPreparationFailed:
            return DiagnosticClass::MetadataFailure;
        case SyncInstallPreparationIssueKind::TargetCorrelationFailed:
        case SyncInstallPreparationIssueKind::RepositoryAuthorityChanged:
        case SyncInstallPreparationIssueKind::BuildPlanCorrelationFailed:
        case SyncInstallPreparationIssueKind::EmptyPreparedRoute:
            return DiagnosticClass::InternalInconsistency;
    }
    return DiagnosticClass::InternalInconsistency;
}

DiagnosticClass upgrade_all_issue_diagnostic_class(
    UpgradeAllOperationIssueKind kind) noexcept {
    switch(kind) {
        case UpgradeAllOperationIssueKind::ForeignInventoryConfigurationFailed:
        case UpgradeAllOperationIssueKind::ForeignInventoryReadFailed:
        case UpgradeAllOperationIssueKind::AurQueryFailed:
            return DiagnosticClass::QueryFailure;
        case UpgradeAllOperationIssueKind::ExplicitSourceAdapterInvalid:
        case UpgradeAllOperationIssueKind::FilteredAurPreparationFailed:
            return DiagnosticClass::MetadataFailure;
        case UpgradeAllOperationIssueKind::CacheAuthorityInvalid:
        case UpgradeAllOperationIssueKind::SystemSourcePhaseIncomplete:
            return DiagnosticClass::Blocked;
        case UpgradeAllOperationIssueKind::SystemSourceExecutionFailedUnexpectedly:
        case UpgradeAllOperationIssueKind::FilteredAurExecutionFailed:
            return DiagnosticClass::ExecutionFailure;
        case UpgradeAllOperationIssueKind::OptionSnapshotMismatch:
        case UpgradeAllOperationIssueKind::SourceSnapshotMismatch:
        case UpgradeAllOperationIssueKind::ExplicitSourceCorrelationInconsistent:
        case UpgradeAllOperationIssueKind::PreparedCapabilityConsumed:
        case UpgradeAllOperationIssueKind::DuplicateExclusionCorrelationInconsistent:
        case UpgradeAllOperationIssueKind::ExternalSatisfactionCorrelationInconsistent:
        case UpgradeAllOperationIssueKind::UnknownFailure:
            return DiagnosticClass::InternalInconsistency;
    }
    return DiagnosticClass::InternalInconsistency;
}

DiagnosticRequiredAction required_action_for(
    DiagnosticClass classification) noexcept {
    switch(classification) {
        case DiagnosticClass::Invalid:
            return DiagnosticRequiredAction::CorrectInput;
        case DiagnosticClass::Unsupported:
        case DiagnosticClass::Blocked:
            return DiagnosticRequiredAction::ResolveBlocker;
        case DiagnosticClass::Ambiguous:
            return DiagnosticRequiredAction::SelectCandidate;
        case DiagnosticClass::Declined:
        case DiagnosticClass::Cancelled:
            return DiagnosticRequiredAction::None;
        case DiagnosticClass::Unavailable:
            return DiagnosticRequiredAction::EnableInteraction;
        case DiagnosticClass::InputFailure:
            return DiagnosticRequiredAction::ResolveBlocker;
        case DiagnosticClass::QueryFailure:
            return DiagnosticRequiredAction::RetryQuery;
        case DiagnosticClass::MetadataFailure:
        case DiagnosticClass::RequiresCheck:
            return DiagnosticRequiredAction::InspectMetadata;
        case DiagnosticClass::PartialFailure:
            return DiagnosticRequiredAction::InspectPartialResult;
        case DiagnosticClass::ExecutionFailure:
            return DiagnosticRequiredAction::ResolveBlocker;
        case DiagnosticClass::InternalInconsistency:
            return DiagnosticRequiredAction::ReportInconsistency;
    }
    return DiagnosticRequiredAction::ReportInconsistency;
}

DiagnosticPhase upgrade_all_phase(UpgradeAllOperationPhase phase) noexcept {
    switch(phase) {
        case UpgradeAllOperationPhase::None:
        case UpgradeAllOperationPhase::Preparation:
            return DiagnosticPhase::Preflight;
        case UpgradeAllOperationPhase::System:
            return DiagnosticPhase::Install;
        case UpgradeAllOperationPhase::RegisteredSource:
            return DiagnosticPhase::Build;
        case UpgradeAllOperationPhase::ForeignInventory:
        case UpgradeAllOperationPhase::AurQuery:
            return DiagnosticPhase::Query;
        case UpgradeAllOperationPhase::AurPreparation:
            return DiagnosticPhase::Planning;
        case UpgradeAllOperationPhase::AurExecution:
            return DiagnosticPhase::Install;
        case UpgradeAllOperationPhase::Reduction:
            return DiagnosticPhase::Reduction;
    }
    return DiagnosticPhase::Reduction;
}

} // namespace

NormalizedDiagnostic<ConfirmationResult>
project_confirmation_diagnostic(
    const ConfirmationResult& result,
    DiagnosticOperation operation,
    DiagnosticPhase phase,
    DiagnosticIdentity identity) {
    DiagnosticClass classification;
    DiagnosticSeverity severity;
    if(std::holds_alternative<ConfirmationDeclined>(result)) {
        classification = DiagnosticClass::Declined;
        severity = DiagnosticSeverity::Warning;
    } else if(std::holds_alternative<ConfirmationCancelled>(result)) {
        classification = DiagnosticClass::Cancelled;
        severity = DiagnosticSeverity::Warning;
    } else if(std::holds_alternative<ConfirmationUnavailable>(result)) {
        classification = DiagnosticClass::Unavailable;
        severity = DiagnosticSeverity::Error;
    } else if(std::holds_alternative<ConfirmationInputFailure>(result)) {
        classification = DiagnosticClass::InputFailure;
        severity = DiagnosticSeverity::Error;
    } else {
        throw std::logic_error(localization::translate_message(
            "An accepted confirmation has no stopping diagnostic."));
    }

    return NormalizedDiagnostic<ConfirmationResult>{
        classification,
        severity,
        operation,
        phase,
        std::move(identity),
        result,
        required_action_for(classification),
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        std::nullopt};
}

DiagnosticSourceKind upgrade_all_source_kind(
    UpgradeAllOperationPhase phase) noexcept {
    switch(phase) {
        case UpgradeAllOperationPhase::System:
        case UpgradeAllOperationPhase::ForeignInventory:
            return DiagnosticSourceKind::Pacman;
        case UpgradeAllOperationPhase::AurQuery:
        case UpgradeAllOperationPhase::AurPreparation:
        case UpgradeAllOperationPhase::AurExecution:
        case UpgradeAllOperationPhase::Reduction:
            return DiagnosticSourceKind::Aur;
        case UpgradeAllOperationPhase::None:
        case UpgradeAllOperationPhase::Preparation:
        case UpgradeAllOperationPhase::RegisteredSource:
            return DiagnosticSourceKind::Unspecified;
    }
    return DiagnosticSourceKind::Unspecified;
}

std::vector<NormalizedDiagnostic<RootPackageSelectionIssue>>
project_root_selection_diagnostics(
    const InvalidRootPackageSelection& invalid,
    std::optional<std::string> requested_package) {
    std::vector<NormalizedDiagnostic<RootPackageSelectionIssue>> diagnostics;
    diagnostics.reserve(invalid.issues.size());
    for(const RootPackageSelectionIssue& issue : invalid.issues) {
        DiagnosticIdentity identity;
        identity.requested_package = requested_package;
        const bool is_ambiguous = is_ambiguous_root_selection_issue(issue);
        if(const auto* conflict = std::get_if<
               ConflictingRootPackageSelectionAlternatives>(&issue)) {
            identity.requested_package = conflict->package_name;
        }
        diagnostics.push_back(
            NormalizedDiagnostic<RootPackageSelectionIssue>{
                is_ambiguous ? DiagnosticClass::Ambiguous
                             : DiagnosticClass::Invalid,
                DiagnosticSeverity::Error,
                DiagnosticOperation::RootPackageSelection,
                DiagnosticPhase::Selection,
                std::move(identity),
                issue,
                is_ambiguous
                    ? DiagnosticRequiredAction::SelectCandidate
                    : DiagnosticRequiredAction::CorrectInput,
                DiagnosticBlockingDecision::BlocksCurrentOperation,
                DiagnosticExitStatusEffect::Failure,
                std::nullopt});
    }
    return diagnostics;
}

NormalizedDiagnostic<RootPackageSelectionCancellationReason>
project_root_selection_diagnostic(
    const CancelledRootPackageSelection& cancellation,
    std::optional<std::string> requested_package) {
    DiagnosticIdentity identity;
    identity.requested_package = std::move(requested_package);
    return NormalizedDiagnostic<RootPackageSelectionCancellationReason>{
        DiagnosticClass::Cancelled,
        DiagnosticSeverity::Warning,
        DiagnosticOperation::RootPackageSelection,
        DiagnosticPhase::Selection,
        std::move(identity),
        cancellation.reason,
        DiagnosticRequiredAction::None,
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        std::nullopt};
}

NormalizedDiagnostic<RootPackageSelectionUnavailableReason>
project_root_selection_diagnostic(
    const UnavailableRootPackageSelection& unavailable,
    std::optional<std::string> requested_package) {
    DiagnosticIdentity identity;
    identity.requested_package = std::move(requested_package);
    return NormalizedDiagnostic<RootPackageSelectionUnavailableReason>{
        DiagnosticClass::Unavailable,
        DiagnosticSeverity::Error,
        DiagnosticOperation::RootPackageSelection,
        DiagnosticPhase::Selection,
        std::move(identity),
        unavailable.reason,
        root_selection_unavailable_action(unavailable.reason),
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        std::nullopt};
}

NormalizedDiagnostic<LocalSourceRootFailure>
project_local_source_root_diagnostic(const LocalSourceRootFailure& failure) {
    DiagnosticIdentity identity;
    identity.source_kind = DiagnosticSourceKind::Local;
    identity.local_root = failure.path;
    const DiagnosticClass classification =
        local_source_diagnostic_class(failure.code);
    return NormalizedDiagnostic<LocalSourceRootFailure>{
        classification,
        DiagnosticSeverity::Error,
        DiagnosticOperation::Build,
        local_source_diagnostic_phase(failure.stage),
        std::move(identity),
        failure,
        required_action_for(classification),
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        std::nullopt};
}

NormalizedDiagnostic<PackageMetadataErrorCode>
project_package_metadata_diagnostic(
    const PackageMetadataFailure& failure,
    DiagnosticOperation operation,
    DiagnosticPhase phase,
    DiagnosticIdentity identity) {
    const DiagnosticClass classification =
        package_metadata_diagnostic_class(failure.code);
    return NormalizedDiagnostic<PackageMetadataErrorCode>{
        classification,
        DiagnosticSeverity::Error,
        operation,
        phase,
        std::move(identity),
        failure.code,
        required_action_for(classification),
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        failure.diagnostic};
}

NormalizedDiagnostic<SyncInstallPreparationIssue>
project_sync_install_diagnostic(const SyncInstallPreparationIssue& issue) {
    DiagnosticIdentity identity;
    if(issue.root.has_value()) {
        identity.requested_package = issue.root->requested_name;
    }
    const DiagnosticClass classification =
        sync_issue_diagnostic_class(issue.kind);
    return NormalizedDiagnostic<SyncInstallPreparationIssue>{
        classification,
        DiagnosticSeverity::Error,
        DiagnosticOperation::SyncInstall,
        DiagnosticPhase::Preflight,
        std::move(identity),
        issue,
        required_action_for(classification),
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        issue.diagnostic};
}

NormalizedDiagnostic<RepositoryMetadataFailureKind>
project_sync_install_diagnostic(
    const SyncRepositoryMetadataReadFailure& failure) {
    DiagnosticIdentity identity;
    identity.source_kind = DiagnosticSourceKind::RepositoryBinary;
    identity.repository = failure.failure.repository_name;
    identity.requested_package = failure.root.requested_name;
    return NormalizedDiagnostic<RepositoryMetadataFailureKind>{
        DiagnosticClass::QueryFailure,
        DiagnosticSeverity::Error,
        DiagnosticOperation::SyncInstall,
        DiagnosticPhase::Query,
        std::move(identity),
        failure.failure.kind,
        DiagnosticRequiredAction::RetryQuery,
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        failure.failure.diagnostic};
}

NormalizedDiagnostic<UpgradeAllOperationIssue>
project_upgrade_all_diagnostic(const UpgradeAllOperationIssue& issue) {
    DiagnosticIdentity identity;
    identity.requested_package = issue.package_name;
    identity.source_kind = upgrade_all_source_kind(issue.phase);
    const DiagnosticClass classification =
        upgrade_all_issue_diagnostic_class(issue.kind);
    return NormalizedDiagnostic<UpgradeAllOperationIssue>{
        classification,
        DiagnosticSeverity::Error,
        DiagnosticOperation::UpgradeAll,
        upgrade_all_phase(issue.phase),
        std::move(identity),
        issue,
        required_action_for(classification),
        DiagnosticBlockingDecision::StopsFollowingPhases,
        DiagnosticExitStatusEffect::Failure,
        issue.diagnostic};
}
