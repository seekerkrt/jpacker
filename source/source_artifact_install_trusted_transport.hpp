#pragma once

#include "package_base_artifact_install_executor.hpp"
#include "source_artifact_install_receipt_evidence.hpp"

#include <optional>
#include <string>
#include <vector>

// Full invocation/work-item attribution remains unprivileged correlation
// evidence. The root helper receives only its minimal exact transaction
// projection; the transport result preserves this binding without permitting
// later attachment to a different work item.
struct SourceArtifactInstallTrustedBinding {
    SourceArtifactInstallWorkItemBinding work_item;
    std::vector<SourceArtifactInstallExpectedSelectedArtifact>
        selected_artifacts;
};

enum class SourceArtifactInstallTrustedExecutionStatus {
    InvalidRequest,
    TrustedExecutableUnavailable,
    TokenGenerationFailed,
    ArtifactSnapshotFailed,
    PrepareFailed,
    PacmanFailed,
    AbortFailed,
    ConsumeFailed,
    MalformedReceipt,
    Missing,
    Complete,
    OutcomeUnknown,
};

class SourceArtifactInstallTrustedExecutionResult final {
public:
    SourceArtifactInstallTrustedExecutionResult() = delete;
    SourceArtifactInstallTrustedExecutionResult(
        const SourceArtifactInstallTrustedExecutionResult&) = default;
    SourceArtifactInstallTrustedExecutionResult(
        SourceArtifactInstallTrustedExecutionResult&&) noexcept = default;
    SourceArtifactInstallTrustedExecutionResult& operator=(
        const SourceArtifactInstallTrustedExecutionResult&) = default;
    SourceArtifactInstallTrustedExecutionResult& operator=(
        SourceArtifactInstallTrustedExecutionResult&&) noexcept = default;
    ~SourceArtifactInstallTrustedExecutionResult() = default;

    [[nodiscard]] SourceArtifactInstallTrustedExecutionStatus status()
        const noexcept;
    [[nodiscard]] const std::optional<int>& pacman_exit_status()
        const noexcept;
    [[nodiscard]] const std::optional<
        SourceArtifactInstallReceiptExpectation>&
    expectation() const noexcept;
    [[nodiscard]] const std::optional<
        SourceArtifactInstallReceiptObservation>&
    observation() const noexcept;
    [[nodiscard]] const std::optional<std::string>& diagnostic()
        const noexcept;

private:
    SourceArtifactInstallTrustedExecutionResult(
        SourceArtifactInstallTrustedExecutionStatus status,
        std::optional<int> pacman_exit_status,
        std::optional<SourceArtifactInstallReceiptExpectation> expectation,
        std::optional<SourceArtifactInstallReceiptObservation> observation,
        std::optional<std::string> diagnostic) noexcept;

    SourceArtifactInstallTrustedExecutionStatus status_;
    std::optional<int> pacman_exit_status_;
    std::optional<SourceArtifactInstallReceiptExpectation> expectation_;
    std::optional<SourceArtifactInstallReceiptObservation> observation_;
    std::optional<std::string> diagnostic_;

    friend class SourceArtifactInstallTrustedTransport;
};

// This separate owner-specific operation is production-capable but has no
// public cleanup-candidate caller in Slice 2. It consumes only a prepared
// PackageBase install capability and never accepts paths, executables,
// hookdirs, state roots, pacman configuration, or an owner string.
[[nodiscard]] SourceArtifactInstallTrustedExecutionResult
execute_source_artifact_install_trusted_transaction(
    PreparedPackageBaseArtifactInstall& install,
    const SourceArtifactInstallTrustedBinding& binding,
    const ArtifactInstallExecutionOptions& options);

#ifdef MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_TRUSTED_TRANSPORT_TEST_HOOKS
// Deterministic token/provenance seam for process-stub tests only.
[[nodiscard]] SourceArtifactInstallTrustedExecutionResult
execute_source_artifact_install_trusted_transaction_for_test(
    PreparedPackageBaseArtifactInstall& install,
    const SourceArtifactInstallTrustedBinding& binding,
    const ArtifactInstallExecutionOptions& options,
    const std::string& transaction_token);
#endif
