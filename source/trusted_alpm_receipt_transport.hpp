#pragma once

#include "invocation_owned_cleanup_model.hpp"

#include <optional>
#include <string>
#include <vector>

enum class TrustedAlpmReceiptRepositoryInstallDirective {
    PreserveExistingReason,
    AsDependency,
};

struct TrustedAlpmReceiptRepositoryTarget {
    std::string repository_name;
    std::string package_name;

    bool operator==(const TrustedAlpmReceiptRepositoryTarget&) const =
        default;
};

struct TrustedAlpmReceiptSelectedProviderRequest {
    std::vector<TrustedAlpmReceiptRepositoryTarget> targets;
    TrustedAlpmReceiptRepositoryInstallDirective install_directive;
    bool no_confirm = false;
};

enum class TrustedAlpmReceiptCaptureStatus {
    InvalidRequest,
    TrustedExecutableUnavailable,
    TokenGenerationFailed,
    PrepareFailed,
    PacmanFailed,
    OutcomeUnknown,
    AbortFailed,
    ConsumeFailed,
    MalformedReceipt,
    Missing,
    Complete,
};

struct TrustedAlpmReceiptCaptureResult {
    TrustedAlpmReceiptCaptureStatus status;
    std::optional<int> pacman_exit_status;
    InvocationDependencyTransactionLedger transaction_ledger;
    std::optional<std::string> diagnostic;
};

// Uses Linux getrandom(2) directly for 256 bits and returns lowercase hex.
// Failure is represented by nullopt; no weaker entropy source is used.
[[nodiscard]] std::optional<std::string>
generate_trusted_alpm_receipt_transaction_token() noexcept;

// This is the only production transport operation. The request cannot select
// a helper, executable, owner, state root, hook path, or output destination.
[[nodiscard]] TrustedAlpmReceiptCaptureResult
execute_trusted_alpm_receipt_selected_provider_transaction(
    const TrustedAlpmReceiptSelectedProviderRequest& request);

#ifdef MOGUET_ENABLE_TRUSTED_ALPM_RECEIPT_TEST_HOOKS
// Test-only seam: retains all fixed executable argv and protocol parsing but
// bypasses installed-file provenance and CSPRNG so a process stub can inspect
// deterministic calls without executing a development-tree helper as root.
[[nodiscard]] TrustedAlpmReceiptCaptureResult
execute_trusted_alpm_receipt_selected_provider_transaction_for_test(
    const TrustedAlpmReceiptSelectedProviderRequest& request,
    const std::string& transaction_token);
#endif
