#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <sys/types.h>

inline constexpr std::size_t MAKEPKG_SYNCDEPS_ADAPTER_TOKEN_HEX_LENGTH = 64;
inline constexpr std::size_t MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES =
    128U * 1024U;
inline constexpr std::size_t
    MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATIONS = 4096;
inline constexpr std::size_t
    MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATION_BYTES = 4096;

enum class MakepkgSyncdepsAdapterCommand {
    SessionPrepare,
    SessionBind,
    TransactionPrepare,
    TransactionRecord,
    TransactionFinalize,
    TransactionConsume,
    TransactionAbort,
    SessionFinalize,
    SessionConsume,
    SessionAbort,
    SyntheticSession,
    RootSyntheticSession,
    SyntheticSecurity,
    RootSyntheticSecurity,
};

enum class MakepkgSyncdepsAdapterProtocolIssueKind {
    InvalidCommand,
    InvalidArgumentCount,
    InvalidSessionToken,
    InvalidTransactionToken,
    InvalidProcessIdentifier,
    InvalidOrdinal,
    InvalidOutcome,
    InvalidExitCode,
    InvalidSyntheticObservation,
    MissingArgumentSeparator,
    EmptyDependencySpecificationSet,
    InvalidDependencySpecification,
    InputTooLarge,
    TooManyRecords,
    MissingFinalNewline,
    InvalidHeader,
    TruncatedProtocol,
    UnexpectedRecord,
    InvalidOwner,
    InvalidNumericRecord,
    InvalidProcessIdentity,
    InvalidTerminalState,
    InvalidCoverage,
    InvalidEvidenceKind,
    DuplicateOrdinal,
    DuplicateTransactionToken,
};

struct MakepkgSyncdepsAdapterProtocolFailure {
    MakepkgSyncdepsAdapterProtocolIssueKind issue;
};

enum class MakepkgSyncdepsSyntheticObservation {
    Missing,
    Observed,
};

enum class MakepkgSyncdepsCommandOutcome {
    NotAttempted,
    Succeeded,
    Failed,
};

enum class MakepkgSyncdepsTerminalState {
    Complete,
    Unsupported,
};

enum class MakepkgSyncdepsAdapterCoverage {
    Complete,
    Unsupported,
};

// Slice 2 deliberately marks its transaction evidence as synthetic. It proves
// the installed/root state machine, not an ALPM Install receipt.
enum class MakepkgSyncdepsEvidenceKind {
    Synthetic,
};

struct MakepkgSyncdepsAdapterInvocation {
    MakepkgSyncdepsAdapterCommand command;
    std::string session_token;
    std::string transaction_token;
    pid_t launcher_pid = -1;
    pid_t child_pid = -1;
    pid_t transaction_adapter_pid = -1;
    std::size_t ordinal = 0;
    MakepkgSyncdepsSyntheticObservation synthetic_observation =
        MakepkgSyncdepsSyntheticObservation::Missing;
    MakepkgSyncdepsCommandOutcome command_outcome =
        MakepkgSyncdepsCommandOutcome::NotAttempted;
    std::optional<int> exit_code;
    std::vector<std::string> dependency_specifications;
    std::size_t synthetic_transaction_count = 0;
    bool synthetic_hold = false;
    std::string synthetic_security_scenario;
};

using MakepkgSyncdepsAdapterInvocationResult = std::variant<
    MakepkgSyncdepsAdapterInvocation,
    MakepkgSyncdepsAdapterProtocolFailure>;

struct MakepkgSyncdepsPidfdIdentity {
    pid_t pid = -1;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint32_t uid = 0;
};

struct MakepkgSyncdepsInstalledExecutableIdentity {
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
};

struct MakepkgSyncdepsPreparedSessionState {
    std::string session_token;
    std::uint32_t invoking_uid = 0;
    MakepkgSyncdepsInstalledExecutableIdentity installed_executable;
    MakepkgSyncdepsPidfdIdentity launcher;
    MakepkgSyncdepsPidfdIdentity supervisor;
};

struct MakepkgSyncdepsBoundChildState {
    std::string session_token;
    MakepkgSyncdepsPidfdIdentity child;
    MakepkgSyncdepsPidfdIdentity transaction_adapter;
};

struct MakepkgSyncdepsPreparedTransactionState {
    std::string session_token;
    std::size_t ordinal = 0;
    std::string transaction_token;
    std::vector<std::string> dependency_specifications;
};

struct MakepkgSyncdepsTransactionObservationState {
    std::string session_token;
    std::size_t ordinal = 0;
    std::string transaction_token;
    MakepkgSyncdepsSyntheticObservation observation =
        MakepkgSyncdepsSyntheticObservation::Missing;
};

struct MakepkgSyncdepsTransactionOutcomeState {
    std::string session_token;
    std::size_t ordinal = 0;
    std::string transaction_token;
    MakepkgSyncdepsCommandOutcome outcome =
        MakepkgSyncdepsCommandOutcome::NotAttempted;
    std::optional<int> exit_code;
};

struct MakepkgSyncdepsTerminalSessionState {
    std::string session_token;
    MakepkgSyncdepsTerminalState terminal_state =
        MakepkgSyncdepsTerminalState::Complete;
    MakepkgSyncdepsAdapterCoverage coverage =
        MakepkgSyncdepsAdapterCoverage::Complete;
    MakepkgSyncdepsCommandOutcome makepkg_outcome =
        MakepkgSyncdepsCommandOutcome::NotAttempted;
    std::optional<int> makepkg_exit_code;
    std::size_t transaction_count = 0;
};

struct MakepkgSyncdepsTransactionManifestEntry {
    MakepkgSyncdepsPreparedTransactionState prepared;
    MakepkgSyncdepsTransactionObservationState observation;
    MakepkgSyncdepsTransactionOutcomeState outcome;
};

struct MakepkgSyncdepsSessionManifest {
    MakepkgSyncdepsPreparedSessionState prepared;
    MakepkgSyncdepsBoundChildState binding;
    MakepkgSyncdepsTerminalSessionState terminal;
    MakepkgSyncdepsEvidenceKind evidence_kind =
        MakepkgSyncdepsEvidenceKind::Synthetic;
    std::vector<MakepkgSyncdepsTransactionManifestEntry> transactions;
};

using MakepkgSyncdepsPreparedSessionStateResult = std::variant<
    MakepkgSyncdepsPreparedSessionState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsBoundChildStateResult = std::variant<
    MakepkgSyncdepsBoundChildState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsPreparedTransactionStateResult = std::variant<
    MakepkgSyncdepsPreparedTransactionState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsTransactionObservationStateResult = std::variant<
    MakepkgSyncdepsTransactionObservationState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsTransactionOutcomeStateResult = std::variant<
    MakepkgSyncdepsTransactionOutcomeState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsTerminalSessionStateResult = std::variant<
    MakepkgSyncdepsTerminalSessionState,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsSessionManifestResult = std::variant<
    MakepkgSyncdepsSessionManifest,
    MakepkgSyncdepsAdapterProtocolFailure>;

struct MakepkgSyncdepsSessionPrepareResponse {
    std::string session_token;
    std::uint32_t invoking_uid = 0;
};

struct MakepkgSyncdepsTransactionPrepareResponse {
    std::string session_token;
    std::size_t ordinal = 0;
    std::string transaction_token;
};

using MakepkgSyncdepsSessionPrepareResponseResult = std::variant<
    MakepkgSyncdepsSessionPrepareResponse,
    MakepkgSyncdepsAdapterProtocolFailure>;
using MakepkgSyncdepsTransactionPrepareResponseResult = std::variant<
    MakepkgSyncdepsTransactionPrepareResponse,
    MakepkgSyncdepsAdapterProtocolFailure>;

[[nodiscard]] std::string_view makepkg_syncdeps_adapter_owner() noexcept;

[[nodiscard]] bool is_valid_makepkg_syncdeps_adapter_token(
    std::string_view token) noexcept;

[[nodiscard]] bool is_valid_makepkg_syncdeps_dependency_specification(
    std::string_view specification) noexcept;

[[nodiscard]] bool is_valid_makepkg_syncdeps_security_scenario(
    std::string_view scenario) noexcept;

// Linux getrandom(2) is the only token source. Failure is returned without a
// PRNG, timestamp, counter, or library fallback.
[[nodiscard]] std::optional<std::string>
generate_makepkg_syncdeps_adapter_token() noexcept;

[[nodiscard]] MakepkgSyncdepsAdapterInvocationResult
parse_makepkg_syncdeps_adapter_arguments(
    const std::vector<std::string>& arguments);

[[nodiscard]] std::string serialize_makepkg_syncdeps_adapter_request(
    const MakepkgSyncdepsAdapterInvocation& invocation);

[[nodiscard]] MakepkgSyncdepsAdapterInvocationResult
parse_makepkg_syncdeps_adapter_request(std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_prepared_session_state(
    const MakepkgSyncdepsPreparedSessionState& state);
[[nodiscard]] MakepkgSyncdepsPreparedSessionStateResult
parse_makepkg_syncdeps_prepared_session_state(std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_bound_child_state(
    const MakepkgSyncdepsBoundChildState& state);
[[nodiscard]] MakepkgSyncdepsBoundChildStateResult
parse_makepkg_syncdeps_bound_child_state(std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_prepared_transaction_state(
    const MakepkgSyncdepsPreparedTransactionState& state);
[[nodiscard]] MakepkgSyncdepsPreparedTransactionStateResult
parse_makepkg_syncdeps_prepared_transaction_state(std::string_view protocol);

[[nodiscard]] std::string
serialize_makepkg_syncdeps_transaction_observation_state(
    const MakepkgSyncdepsTransactionObservationState& state);
[[nodiscard]] MakepkgSyncdepsTransactionObservationStateResult
parse_makepkg_syncdeps_transaction_observation_state(
    std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_transaction_outcome_state(
    const MakepkgSyncdepsTransactionOutcomeState& state);
[[nodiscard]] MakepkgSyncdepsTransactionOutcomeStateResult
parse_makepkg_syncdeps_transaction_outcome_state(std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_terminal_session_state(
    const MakepkgSyncdepsTerminalSessionState& state);
[[nodiscard]] MakepkgSyncdepsTerminalSessionStateResult
parse_makepkg_syncdeps_terminal_session_state(std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_session_prepare_response(
    const MakepkgSyncdepsSessionPrepareResponse& response);
[[nodiscard]] MakepkgSyncdepsSessionPrepareResponseResult
parse_makepkg_syncdeps_session_prepare_response(
    std::string_view protocol);

[[nodiscard]] std::string
serialize_makepkg_syncdeps_transaction_prepare_response(
    const MakepkgSyncdepsTransactionPrepareResponse& response);
[[nodiscard]] MakepkgSyncdepsTransactionPrepareResponseResult
parse_makepkg_syncdeps_transaction_prepare_response(
    std::string_view protocol);

[[nodiscard]] std::string serialize_makepkg_syncdeps_session_manifest(
    const MakepkgSyncdepsSessionManifest& manifest);
[[nodiscard]] MakepkgSyncdepsSessionManifestResult
parse_makepkg_syncdeps_session_manifest(std::string_view protocol);
