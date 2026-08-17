#pragma once

#include "commands_sync.hpp"
#include "diagnostic_model.hpp"
#include "interactive_confirmation.hpp"
#include "local_source_root.hpp"
#include "package_metadata.hpp"
#include "root_package_selection.hpp"
#include "upgrade_all_operation.hpp"

#include <optional>
#include <string>
#include <vector>

DiagnosticSourceKind upgrade_all_source_kind(
        UpgradeAllOperationPhase phase) noexcept;

NormalizedDiagnostic<ConfirmationResult>
project_confirmation_diagnostic(
        const ConfirmationResult& result,
        DiagnosticOperation operation,
        DiagnosticPhase phase,
        DiagnosticIdentity identity = {});

std::vector<NormalizedDiagnostic<RootPackageSelectionIssue>>
project_root_selection_diagnostics(
        const InvalidRootPackageSelection& invalid,
        std::optional<std::string> requested_package = std::nullopt);

NormalizedDiagnostic<RootPackageSelectionCancellationReason>
project_root_selection_diagnostic(
        const CancelledRootPackageSelection& cancellation,
        std::optional<std::string> requested_package = std::nullopt);

NormalizedDiagnostic<RootPackageSelectionUnavailableReason>
project_root_selection_diagnostic(
        const UnavailableRootPackageSelection& unavailable,
        std::optional<std::string> requested_package = std::nullopt);

NormalizedDiagnostic<LocalSourceRootFailure>
project_local_source_root_diagnostic(const LocalSourceRootFailure& failure);

NormalizedDiagnostic<PackageMetadataErrorCode>
project_package_metadata_diagnostic(
        const PackageMetadataFailure& failure,
        DiagnosticOperation operation,
        DiagnosticPhase phase,
        DiagnosticIdentity identity = {});

NormalizedDiagnostic<SyncInstallPreparationIssue>
project_sync_install_diagnostic(const SyncInstallPreparationIssue& issue);

NormalizedDiagnostic<RepositoryMetadataFailureKind>
project_sync_install_diagnostic(
        const SyncRepositoryMetadataReadFailure& failure);

NormalizedDiagnostic<UpgradeAllOperationIssue>
project_upgrade_all_diagnostic(const UpgradeAllOperationIssue& issue);
