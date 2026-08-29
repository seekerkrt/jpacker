#pragma once

#include "artifact_identity.hpp"
#include "invocation_owned_cleanup_model.hpp"
#include "source_package_identity.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// This identity separates invocation-local evidence sets. It is correlation
// authority, not runtime authentication, a PID/timestamp, or a replacement for
// Slice 2's root-backed transaction token.
class SourceArtifactInstallInvocationIdentity final {
public:
    SourceArtifactInstallInvocationIdentity() = delete;
    SourceArtifactInstallInvocationIdentity(
        const SourceArtifactInstallInvocationIdentity&) = default;
    SourceArtifactInstallInvocationIdentity(
        SourceArtifactInstallInvocationIdentity&&) noexcept = default;
    SourceArtifactInstallInvocationIdentity& operator=(
        const SourceArtifactInstallInvocationIdentity&) = default;
    SourceArtifactInstallInvocationIdentity& operator=(
        SourceArtifactInstallInvocationIdentity&&) noexcept = default;
    ~SourceArtifactInstallInvocationIdentity() = default;

    [[nodiscard]] static SourceArtifactInstallInvocationIdentity
    from_local_value(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

    bool operator==(
        const SourceArtifactInstallInvocationIdentity&) const = default;

private:
    explicit SourceArtifactInstallInvocationIdentity(
        std::string value) noexcept;

    std::string value_;
};

struct SourceArtifactInstallWorkItemBinding {
    SourceArtifactInstallInvocationIdentity invocation;
    std::size_t work_item_index;
    std::string package_base;
    std::vector<RootTargetIdentity> requested_roots;

    bool operator==(const SourceArtifactInstallWorkItemBinding&) const =
        default;
};

struct SourceArtifactInstallExpectedSelectedArtifact {
    std::size_t artifact_index;
    SourceAwarePackageIdentity expected_identity;
    DesiredInstallReason desired_reason;
    std::vector<PackageRole> dependency_roles;
    std::vector<RootTargetIdentity> requested_roots;

    bool operator==(
        const SourceArtifactInstallExpectedSelectedArtifact&) const =
        default;
};

struct SourceArtifactInstallObservedSelectedArtifact {
    std::size_t artifact_index;
    ArtifactPackageIdentity archive_identity;
    DesiredInstallReason desired_reason;
    std::vector<PackageRole> dependency_roles;
    std::vector<RootTargetIdentity> requested_roots;

    bool operator==(
        const SourceArtifactInstallObservedSelectedArtifact&) const =
        default;
};

// Pre-transaction identity. Owner is intentionally absent: this request can
// only be evaluated as SourceArtifactInstall.
struct SourceArtifactInstallReceiptExpectation {
    SourceArtifactInstallWorkItemBinding work_item;
    std::vector<SourceArtifactInstallExpectedSelectedArtifact>
        selected_artifacts;
    std::string transaction_token;
};

// Factual post-transaction shape reserved for the future Slice 2 transport.
// Raw ledgers cannot construct it in production; the test factory freezes the
// pure matrix without adding a runtime producer or privileged transport.
class SourceArtifactInstallReceiptObservation final {
public:
    SourceArtifactInstallReceiptObservation() = delete;
    SourceArtifactInstallReceiptObservation(
        const SourceArtifactInstallReceiptObservation&) = default;
    SourceArtifactInstallReceiptObservation(
        SourceArtifactInstallReceiptObservation&&) noexcept = default;
    SourceArtifactInstallReceiptObservation& operator=(
        const SourceArtifactInstallReceiptObservation&) = default;
    SourceArtifactInstallReceiptObservation& operator=(
        SourceArtifactInstallReceiptObservation&&) noexcept = default;
    ~SourceArtifactInstallReceiptObservation() = default;

    [[nodiscard]] const SourceArtifactInstallWorkItemBinding& work_item()
        const noexcept;
    [[nodiscard]] const std::vector<
        SourceArtifactInstallObservedSelectedArtifact>&
    selected_artifacts() const noexcept;
    [[nodiscard]] const InvocationDependencyTransactionLedger&
    transaction_ledger() const noexcept;

private:
    SourceArtifactInstallReceiptObservation(
        SourceArtifactInstallWorkItemBinding work_item,
        std::vector<SourceArtifactInstallObservedSelectedArtifact>
            selected_artifacts,
        InvocationDependencyTransactionLedger transaction_ledger) noexcept;

    SourceArtifactInstallWorkItemBinding work_item_;
    std::vector<SourceArtifactInstallObservedSelectedArtifact>
        selected_artifacts_;
    InvocationDependencyTransactionLedger transaction_ledger_;

#ifdef MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_RECEIPT_TEST_HOOKS
    friend SourceArtifactInstallReceiptObservation
    make_source_artifact_install_receipt_observation_for_test(
        SourceArtifactInstallWorkItemBinding work_item,
        std::vector<SourceArtifactInstallObservedSelectedArtifact>
            selected_artifacts,
        InvocationDependencyTransactionLedger transaction_ledger);
#endif
};

#ifdef MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_RECEIPT_TEST_HOOKS
SourceArtifactInstallReceiptObservation
make_source_artifact_install_receipt_observation_for_test(
    SourceArtifactInstallWorkItemBinding work_item,
    std::vector<SourceArtifactInstallObservedSelectedArtifact>
        selected_artifacts,
    InvocationDependencyTransactionLedger transaction_ledger);
#endif

enum class SourceArtifactInstallReceiptEvidenceCompleteness {
    Complete,
    Incomplete,
    Missing,
    Invalid,
};

// Declaration order is the canonical issue order.
enum class SourceArtifactInstallReceiptEvidenceIssueKind {
    InvalidWorkItemPackageBase,
    InvalidRequestedRootAttribution,
    InvocationMismatch,
    WorkItemIndexMismatch,
    PackageBaseMismatch,
    RequestedRootAttributionMismatch,
    InvalidExpectedTransactionToken,
    SelectedArtifactSetMissing,
    DuplicateSelectedArtifactIndex,
    DuplicateSelectedPackageName,
    ExpectedPackageBaseMismatch,
    ExpectedVersionAuthorityIncomplete,
    ExpectedArchitectureAuthorityIncomplete,
    DesiredInstallReasonNotDependency,
    DependencyRoleMissing,
    DependencyRoleNotBuildOrCheck,
    SelectedArtifactRootAttributionInvalid,
    SelectedArtifactSetMismatch,
    DesiredInstallReasonMismatch,
    DependencyRoleMismatch,
    SelectedArtifactRootAttributionMismatch,
    ArchiveIdentityMismatch,
    TransactionMissing,
    UnexpectedTransactionCount,
    DuplicateTransactionToken,
    TransactionTokenMismatch,
    TransactionOwnerMismatch,
    RequestedPackageSetMismatch,
    CommandOutcomeNotSucceeded,
    ReceiptMissing,
    ReceiptIncomplete,
    ReceiptInvalid,
    SelectedArtifactNotInstalled,
};

struct SourceArtifactInstallCorrelatedSelectedArtifact {
    std::size_t artifact_index;
    SourceAwarePackageIdentity expected_identity;
    ArtifactPackageIdentity archive_identity;
    DesiredInstallReason desired_reason;
    std::vector<PackageRole> dependency_roles;
    std::vector<RootTargetIdentity> requested_roots;

    bool operator==(
        const SourceArtifactInstallCorrelatedSelectedArtifact&) const =
        default;
};

// Closed factual evidence retains requested selected artifacts and the actual
// transaction ledger separately. Complete still does not grant cleanup or
// classify policy/shared lifetime.
class SourceArtifactInstallReceiptEvidence final {
public:
    SourceArtifactInstallReceiptEvidence() = delete;
    SourceArtifactInstallReceiptEvidence(
        const SourceArtifactInstallReceiptEvidence&) = default;
    SourceArtifactInstallReceiptEvidence(
        SourceArtifactInstallReceiptEvidence&&) noexcept = default;
    SourceArtifactInstallReceiptEvidence& operator=(
        const SourceArtifactInstallReceiptEvidence&) = default;
    SourceArtifactInstallReceiptEvidence& operator=(
        SourceArtifactInstallReceiptEvidence&&) noexcept = default;
    ~SourceArtifactInstallReceiptEvidence() = default;

    [[nodiscard]] SourceArtifactInstallReceiptEvidenceCompleteness
    completeness() const noexcept;
    [[nodiscard]] InvocationDependencyTransactionOwner owner()
        const noexcept;
    [[nodiscard]] const SourceArtifactInstallReceiptExpectation& expectation()
        const noexcept;
    [[nodiscard]] const SourceArtifactInstallReceiptObservation& observation()
        const noexcept;
    [[nodiscard]] const SourceArtifactInstallWorkItemBinding& work_item()
        const noexcept;
    [[nodiscard]] const std::string& transaction_token() const noexcept;
    [[nodiscard]] const std::vector<
        SourceArtifactInstallCorrelatedSelectedArtifact>&
    selected_artifacts() const noexcept;
    [[nodiscard]] const InvocationDependencyTransactionLedger&
    transaction_ledger() const noexcept;
    [[nodiscard]] const std::optional<
        InvocationDependencyTransactionCommandOutcome>&
    command_outcome() const noexcept;
    [[nodiscard]] const std::optional<PacmanTransactionReceiptState>&
    receipt_state() const noexcept;
    [[nodiscard]] const std::vector<std::string>& actual_install_set()
        const noexcept;
    [[nodiscard]] const std::vector<
        SourceArtifactInstallReceiptEvidenceIssueKind>&
    issues() const noexcept;

private:
    SourceArtifactInstallReceiptEvidence(
        SourceArtifactInstallReceiptEvidenceCompleteness completeness,
        SourceArtifactInstallReceiptExpectation expectation,
        SourceArtifactInstallReceiptObservation observation,
        std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
            selected_artifacts,
        std::optional<InvocationDependencyTransactionCommandOutcome>
            command_outcome,
        std::optional<PacmanTransactionReceiptState> receipt_state,
        std::vector<std::string> actual_install_set,
        std::vector<SourceArtifactInstallReceiptEvidenceIssueKind>
            issues) noexcept;

    SourceArtifactInstallReceiptEvidenceCompleteness completeness_;
    SourceArtifactInstallReceiptExpectation expectation_;
    SourceArtifactInstallReceiptObservation observation_;
    std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
        selected_artifacts_;
    std::optional<InvocationDependencyTransactionCommandOutcome>
        command_outcome_;
    std::optional<PacmanTransactionReceiptState> receipt_state_;
    std::vector<std::string> actual_install_set_;
    std::vector<SourceArtifactInstallReceiptEvidenceIssueKind> issues_;

    friend SourceArtifactInstallReceiptEvidence
    establish_source_artifact_install_receipt_evidence(
        const SourceArtifactInstallReceiptExpectation& expectation,
        const SourceArtifactInstallReceiptObservation& observation);
};

SourceArtifactInstallReceiptEvidence
establish_source_artifact_install_receipt_evidence(
    const SourceArtifactInstallReceiptExpectation& expectation,
    const SourceArtifactInstallReceiptObservation& observation);

// Positive causal capability. It is not constructible from a raw ledger,
// selected-provider execution result, path capability, or command success.
class SourceArtifactInstallCausalEvidence final {
public:
    SourceArtifactInstallCausalEvidence() = delete;
    SourceArtifactInstallCausalEvidence(
        const SourceArtifactInstallCausalEvidence&) = default;
    SourceArtifactInstallCausalEvidence(
        SourceArtifactInstallCausalEvidence&&) noexcept = default;
    SourceArtifactInstallCausalEvidence& operator=(
        const SourceArtifactInstallCausalEvidence&) = default;
    SourceArtifactInstallCausalEvidence& operator=(
        SourceArtifactInstallCausalEvidence&&) noexcept = default;
    ~SourceArtifactInstallCausalEvidence() = default;

    [[nodiscard]] InvocationDependencyTransactionOwner owner()
        const noexcept;
    [[nodiscard]] const SourceArtifactInstallWorkItemBinding& work_item()
        const noexcept;
    [[nodiscard]] const std::string& transaction_token() const noexcept;
    [[nodiscard]] const std::vector<
        SourceArtifactInstallCorrelatedSelectedArtifact>&
    selected_artifacts() const noexcept;
    [[nodiscard]] const std::vector<std::string>& actual_install_set()
        const noexcept;

private:
    SourceArtifactInstallCausalEvidence(
        SourceArtifactInstallWorkItemBinding work_item,
        std::string transaction_token,
        std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
            selected_artifacts,
        std::vector<std::string> actual_install_set) noexcept;

    SourceArtifactInstallWorkItemBinding work_item_;
    std::string transaction_token_;
    std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
        selected_artifacts_;
    std::vector<std::string> actual_install_set_;

    friend std::optional<SourceArtifactInstallCausalEvidence>
    project_source_artifact_install_causal_evidence(
        const SourceArtifactInstallReceiptEvidence& evidence);
};

[[nodiscard]] std::optional<SourceArtifactInstallCausalEvidence>
project_source_artifact_install_causal_evidence(
    const SourceArtifactInstallReceiptEvidence& evidence);
