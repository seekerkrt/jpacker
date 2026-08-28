#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

inline constexpr std::size_t TRUSTED_ALPM_RECEIPT_TOKEN_HEX_LENGTH = 64;
inline constexpr std::size_t TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES =
    16U * 1024U * 1024U;
inline constexpr std::size_t TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGES = 100000;
inline constexpr std::size_t TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGE_NAME_BYTES =
    4096;

enum class TrustedAlpmReceiptHelperCommand {
    Prepare,
    Record,
    Consume,
    Abort,
};

enum class TrustedAlpmReceiptProtocolIssueKind {
    InvalidCommand,
    InvalidArgumentCount,
    InvalidTransactionToken,
    InvalidTransactionOwner,
    MissingArgumentSeparator,
    EmptyRequestedPackageSet,
    InvalidPackageName,
    DuplicatePackageName,
    InputTooLarge,
    TooManyPackages,
    MissingFinalNewline,
    InvalidHeader,
    TruncatedProtocol,
    UnexpectedRecord,
    InvalidHookDirectory,
    InvalidReceiptState,
};

struct TrustedAlpmReceiptProtocolFailure {
    TrustedAlpmReceiptProtocolIssueKind issue;
};

struct TrustedAlpmReceiptHelperInvocation {
    TrustedAlpmReceiptHelperCommand command;
    std::string transaction_token;
    std::vector<std::string> requested_package_names;
};

using TrustedAlpmReceiptHelperInvocationResult = std::variant<
    TrustedAlpmReceiptHelperInvocation,
    TrustedAlpmReceiptProtocolFailure>;

struct TrustedAlpmReceiptPreparedState {
    std::string transaction_token;
    std::vector<std::string> requested_package_names;
};

using TrustedAlpmReceiptPreparedStateResult = std::variant<
    TrustedAlpmReceiptPreparedState,
    TrustedAlpmReceiptProtocolFailure>;

struct TrustedAlpmReceiptPrepareResponse {
    std::string transaction_token;
    std::string hook_directory;
};

using TrustedAlpmReceiptPrepareResponseResult = std::variant<
    TrustedAlpmReceiptPrepareResponse,
    TrustedAlpmReceiptProtocolFailure>;

enum class TrustedAlpmReceiptMachineState {
    Missing,
    Complete,
};

struct TrustedAlpmReceiptMachineReceipt {
    TrustedAlpmReceiptMachineState state;
    std::string transaction_token;
    std::vector<std::string> installed_package_names;
};

using TrustedAlpmReceiptMachineReceiptResult = std::variant<
    TrustedAlpmReceiptMachineReceipt,
    TrustedAlpmReceiptProtocolFailure>;

using TrustedAlpmReceiptNeedsTargetsResult = std::variant<
    std::vector<std::string>,
    TrustedAlpmReceiptProtocolFailure>;

[[nodiscard]] std::string_view
trusted_alpm_receipt_selected_repository_provider_owner() noexcept;

[[nodiscard]] bool is_valid_trusted_alpm_receipt_token(
    std::string_view transaction_token) noexcept;

// The root helper deliberately uses a locale-neutral subset no wider than the
// package-name authority used by the unprivileged Slice 3.5 validator.
[[nodiscard]] bool is_valid_trusted_alpm_receipt_package_name(
    std::string_view package_name) noexcept;

[[nodiscard]] TrustedAlpmReceiptHelperInvocationResult
parse_trusted_alpm_receipt_helper_arguments(
    const std::vector<std::string>& arguments);

[[nodiscard]] TrustedAlpmReceiptNeedsTargetsResult
parse_trusted_alpm_receipt_needs_targets(std::string_view input);

[[nodiscard]] std::string serialize_trusted_alpm_receipt_prepared_state(
    const TrustedAlpmReceiptPreparedState& state);

[[nodiscard]] TrustedAlpmReceiptPreparedStateResult
parse_trusted_alpm_receipt_prepared_state(std::string_view protocol);

[[nodiscard]] std::string trusted_alpm_receipt_hook_directory(
    std::string_view transaction_token);

[[nodiscard]] std::string trusted_alpm_receipt_hook_filename(
    std::string_view transaction_token);

[[nodiscard]] std::string serialize_trusted_alpm_receipt_prepare_response(
    const TrustedAlpmReceiptPrepareResponse& response);

[[nodiscard]] TrustedAlpmReceiptPrepareResponseResult
parse_trusted_alpm_receipt_prepare_response(
    std::string_view protocol,
    std::string_view expected_transaction_token);

[[nodiscard]] std::string serialize_trusted_alpm_receipt_machine_receipt(
    const TrustedAlpmReceiptMachineReceipt& receipt);

[[nodiscard]] TrustedAlpmReceiptMachineReceiptResult
parse_trusted_alpm_receipt_machine_receipt(std::string_view protocol);
