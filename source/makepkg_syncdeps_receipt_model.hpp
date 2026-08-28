#pragma once

#include "invocation_owned_cleanup_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MakepkgSyncDependencySessionIdentity {
    std::string session_token;
    std::uint32_t invoking_uid;

    bool operator==(
        const MakepkgSyncDependencySessionIdentity&) const = default;
};

enum class MakepkgSyncDependencyTerminalState {
    Missing,
    Incomplete,
    Complete,
    Unsupported,
};

// Coverage is intentionally lossless. A bypass, a known unsupported route,
// conflicting PACMAN settings, and an unobserved route are not interchangeable.
enum class MakepkgSyncDependencyAdapterCoverage {
    Missing,
    Incomplete,
    Complete,
    Bypassed,
    Unsupported,
    Conflict,
    Unknown,
};

// Slice 1 freezes the positive process contract without implementing pidfd or
// privileged state. PID-only and timestamp-only evidence remain typed, but
// can never satisfy the positive session requirement.
enum class MakepkgSyncDependencyProcessBinding {
    Missing,
    Incomplete,
    InstalledLauncherAndExactLauncherMakepkgLifetime,
    PidOnly,
    TimestampOnly,
    Unsupported,
    Unknown,
};

enum class MakepkgSyncDependencyMakepkgOutcomeKind {
    NotAttempted,
    Succeeded,
    Failed,
    Unknown,
};

struct MakepkgSyncDependencyMakepkgOutcome {
    MakepkgSyncDependencyMakepkgOutcomeKind kind;
    std::optional<int> exit_code;
};

enum class MakepkgSyncDependencyTransactionOrdinal : std::uint8_t {
    First = 1,
    Second = 2,
};

struct MakepkgSyncDependencyTransactionObservation {
    std::optional<MakepkgSyncDependencyTransactionOrdinal> ordinal;
    std::optional<std::string> session_token;
    // These are exact argv elements accepted by the strict PACMAN grammar.
    // They are retained opaquely and are not package-solver input for Moguet.
    std::vector<std::string> dependency_specifications;
    InvocationDependencyTransaction transaction;
};

struct MakepkgSyncDependencySessionObservation {
    MakepkgSyncDependencyTerminalState terminal_state;
    std::optional<MakepkgSyncDependencySessionIdentity> session_identity;
    std::optional<InvocationDependencyTransactionOwner> owner;
    MakepkgSyncDependencyAdapterCoverage adapter_coverage;
    MakepkgSyncDependencyProcessBinding process_binding;
    MakepkgSyncDependencyMakepkgOutcome makepkg_outcome;
    std::optional<std::size_t> transaction_count;
    std::vector<MakepkgSyncDependencyTransactionObservation> transactions;
};

enum class MakepkgSyncDependencySessionState {
    Missing,
    Incomplete,
    Bypassed,
    Unsupported,
    Conflict,
    CompleteZero,
    CompleteOne,
    CompleteTwo,
    Invalid,
};

// Declaration order is the canonical issue order.
enum class MakepkgSyncDependencySessionIssueKind {
    InvalidExpectedSessionToken,
    InvalidTerminalState,
    SessionMissing,
    SessionIncomplete,
    UnexpectedMissingSessionData,
    SessionIdentityMissing,
    InvalidObservedSessionToken,
    SessionTokenMismatch,
    InvokingUidMismatch,
    SessionOwnerMissing,
    InvalidSessionOwner,
    SessionOwnerMismatch,
    AdapterCoverageMissing,
    AdapterCoverageIncomplete,
    AdapterBypassed,
    AdapterUnsupported,
    AdapterConflict,
    AdapterCoverageUnknown,
    ProcessBindingMissing,
    ProcessBindingIncomplete,
    PidOnlyProcessBinding,
    TimestampOnlyProcessBinding,
    ProcessBindingUnsupported,
    ProcessBindingUnknown,
    MakepkgOutcomeNotExact,
    InvalidMakepkgOutcome,
    TransactionCountMissing,
    TransactionCountTooLarge,
    TransactionCountMismatch,
    TransactionOrdinalMissing,
    TransactionOrdinalOutOfRange,
    DuplicateTransactionOrdinal,
    TransactionOrdinalMismatch,
    TransactionSessionTokenMissing,
    InvalidTransactionSessionToken,
    TransactionSessionTokenMismatch,
    InvalidTransactionToken,
    DuplicateTransactionToken,
    TransactionOwnerMismatch,
    MixedTransactionOwners,
    UnexpectedRequestedPackageNames,
    DependencySpecificationsMissing,
    InvalidDependencySpecification,
    TransactionCommandOutcomeNotExact,
    InvalidTransactionCommandOutcome,
    InvalidTransactionReceipt,
};

class MakepkgSyncDependencySessionReceipt final {
public:
    MakepkgSyncDependencySessionReceipt() = delete;
    MakepkgSyncDependencySessionReceipt(
        const MakepkgSyncDependencySessionReceipt&) = default;
    MakepkgSyncDependencySessionReceipt(
        MakepkgSyncDependencySessionReceipt&&) noexcept = default;
    MakepkgSyncDependencySessionReceipt& operator=(
        const MakepkgSyncDependencySessionReceipt&) = default;
    MakepkgSyncDependencySessionReceipt& operator=(
        MakepkgSyncDependencySessionReceipt&&) noexcept = default;
    ~MakepkgSyncDependencySessionReceipt() = default;

    [[nodiscard]] MakepkgSyncDependencySessionState state() const noexcept;
    [[nodiscard]] bool is_trusted_terminal() const noexcept;
    [[nodiscard]] const std::optional<MakepkgSyncDependencySessionIdentity>&
    session_identity() const noexcept;
    [[nodiscard]] const std::optional<InvocationDependencyTransactionOwner>&
    owner() const noexcept;
    [[nodiscard]] MakepkgSyncDependencyAdapterCoverage adapter_coverage()
        const noexcept;
    [[nodiscard]] MakepkgSyncDependencyProcessBinding process_binding()
        const noexcept;
    [[nodiscard]] const MakepkgSyncDependencyMakepkgOutcome& makepkg_outcome()
        const noexcept;
    [[nodiscard]] const std::optional<std::size_t>& transaction_count()
        const noexcept;
    [[nodiscard]] const std::vector<
        MakepkgSyncDependencyTransactionObservation>&
    transaction_observations() const noexcept;
    [[nodiscard]] const std::vector<MakepkgSyncDependencySessionIssueKind>&
    issues() const noexcept;

    // A parent makepkg failure does not erase an earlier factual Install.
    // Conversely, Upgrade, Missing/incomplete receipt, failed pacman command,
    // or incomplete/bypassed session coverage never becomes positive proof.
    [[nodiscard]] bool contains_authoritative_install(
        const std::string& package_name) const noexcept;

private:
    MakepkgSyncDependencySessionReceipt(
        MakepkgSyncDependencySessionState state,
        MakepkgSyncDependencySessionObservation observation,
        std::vector<MakepkgSyncDependencySessionIssueKind> issues) noexcept;

    MakepkgSyncDependencySessionState state_;
    MakepkgSyncDependencySessionObservation observation_;
    std::vector<MakepkgSyncDependencySessionIssueKind> issues_;

    friend MakepkgSyncDependencySessionReceipt
    validate_makepkg_sync_dependency_session(
        const MakepkgSyncDependencySessionIdentity& expected_identity,
        const MakepkgSyncDependencySessionObservation& observation);
};

[[nodiscard]] bool is_valid_makepkg_sync_dependency_session_token(
    const std::string& session_token) noexcept;

// The owner is fixed to MakepkgSyncDependencies. The factory rejects owner
// mismatch and mixed-owner ledgers instead of weakening the selected-provider
// protocol or accepting a caller-selected owner.
[[nodiscard]] MakepkgSyncDependencySessionReceipt
validate_makepkg_sync_dependency_session(
    const MakepkgSyncDependencySessionIdentity& expected_identity,
    const MakepkgSyncDependencySessionObservation& observation);
