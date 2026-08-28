#include "invocation_owned_cleanup_adapter.hpp"
#include "makepkg_syncdeps_pacman_contract.hpp"
#include "makepkg_syncdeps_receipt_model.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t INVOKING_UID = 1000;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

std::string token(char digit) {
    return std::string(64, digit);
}

MakepkgSyncDependencySessionIdentity session_identity() {
    return MakepkgSyncDependencySessionIdentity{token('a'), INVOKING_UID};
}

PacmanTransactionReceipt transaction_receipt(
    const std::string& transaction_token,
    InvocationDependencyTransactionOwner owner,
    PacmanTransactionReceiptObservationState state,
    std::vector<PacmanTransactionPackageObservation> operations = {}) {
    if(state == PacmanTransactionReceiptObservationState::Missing) {
        return validate_pacman_transaction_receipt(
            transaction_token, owner,
            PacmanTransactionReceiptObservation{
                PacmanTransactionReceiptObservationState::Missing,
                std::nullopt,
                std::nullopt,
                {}});
    }
    return validate_pacman_transaction_receipt(
        transaction_token, owner,
        PacmanTransactionReceiptObservation{
            state, transaction_token, owner, std::move(operations)});
}

MakepkgSyncDependencyTransactionObservation transaction(
    std::size_t ordinal, const std::string& transaction_token,
    InvocationDependencyTransactionCommandOutcome command_outcome,
    PacmanTransactionReceiptObservationState receipt_state,
    std::vector<PacmanTransactionPackageObservation> operations,
    InvocationDependencyTransactionOwner owner =
        InvocationDependencyTransactionOwner::MakepkgSyncDependencies,
    std::vector<std::string> dependency_specifications = {"dependency>=1"}) {
    return MakepkgSyncDependencyTransactionObservation{
        static_cast<MakepkgSyncDependencyTransactionOrdinal>(ordinal),
        session_identity().session_token,
        std::move(dependency_specifications),
        InvocationDependencyTransaction{
            transaction_token,
            owner,
            {},
            command_outcome,
            transaction_receipt(
                transaction_token, owner, receipt_state,
                std::move(operations))}};
}

MakepkgSyncDependencySessionObservation complete_session(
    std::vector<MakepkgSyncDependencyTransactionObservation> transactions = {},
    MakepkgSyncDependencyMakepkgOutcome makepkg_outcome = {
        MakepkgSyncDependencyMakepkgOutcomeKind::Succeeded, 0}) {
    return MakepkgSyncDependencySessionObservation{
        MakepkgSyncDependencyTerminalState::Complete,
        session_identity(),
        InvocationDependencyTransactionOwner::MakepkgSyncDependencies,
        MakepkgSyncDependencyAdapterCoverage::Complete,
        MakepkgSyncDependencyProcessBinding::
            InstalledLauncherAndExactLauncherMakepkgLifetime,
        makepkg_outcome,
        transactions.size(),
        std::move(transactions)};
}

MakepkgSyncDependencySessionObservation missing_session() {
    return MakepkgSyncDependencySessionObservation{
        MakepkgSyncDependencyTerminalState::Missing,
        std::nullopt,
        std::nullopt,
        MakepkgSyncDependencyAdapterCoverage::Missing,
        MakepkgSyncDependencyProcessBinding::Missing,
        {MakepkgSyncDependencyMakepkgOutcomeKind::NotAttempted, std::nullopt},
        std::nullopt,
        {}};
}

bool has_session_issue(
    const MakepkgSyncDependencySessionReceipt& receipt,
    MakepkgSyncDependencySessionIssueKind issue) {
    return std::find(
               receipt.issues().begin(), receipt.issues().end(), issue) !=
           receipt.issues().end();
}

CleanupCurrentPackageEvidence current_package_evidence(
    const std::string& package_name) {
    return CleanupCurrentPackageEvidence{
        CleanupInstalledState::Present,
        InstalledPackageMetadata{
            package_name, "1.0-1", InstalledPackageReason::Dependency},
        CleanupEvidenceVerification::Verified};
}

CleanupCausalOwnership project_makepkg_causal_ownership(
    const MakepkgSyncDependencySessionReceipt& receipt,
    const std::string& package_name) {
    return project_makepkg_sync_dependency_causal_ownership(
        package_name, CleanupBaselineObservation::NewlyObserved,
        current_package_evidence(package_name), receipt);
}

void test_trusted_zero_requires_complete_terminal_authority() {
    const MakepkgSyncDependencySessionReceipt zero =
        validate_makepkg_sync_dependency_session(
            session_identity(), complete_session());
    expect(
        zero.state() == MakepkgSyncDependencySessionState::CompleteZero &&
            zero.is_trusted_terminal() &&
            zero.transaction_count() == std::optional<std::size_t>{0} &&
            zero.transaction_observations().empty(),
        "terminal complete zero was not represented as trusted zero");

    MakepkgSyncDependencySessionObservation empty_ledger_alone =
        complete_session();
    empty_ledger_alone.transaction_count.reset();
    empty_ledger_alone.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Unknown;
    empty_ledger_alone.makepkg_outcome = {
        MakepkgSyncDependencyMakepkgOutcomeKind::Unknown, std::nullopt};
    empty_ledger_alone.process_binding =
        MakepkgSyncDependencyProcessBinding::Missing;
    const MakepkgSyncDependencySessionReceipt rejected =
        validate_makepkg_sync_dependency_session(
            session_identity(), empty_ledger_alone);
    expect(
        rejected.state() == MakepkgSyncDependencySessionState::Incomplete &&
            !rejected.is_trusted_terminal() &&
            has_session_issue(
                rejected,
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountMissing),
        "empty ledger alone was promoted to trusted zero");
}

void test_missing_incomplete_unsupported_and_binding_states() {
    const MakepkgSyncDependencySessionReceipt missing =
        validate_makepkg_sync_dependency_session(
            session_identity(), missing_session());
    expect(
        missing.state() == MakepkgSyncDependencySessionState::Missing,
        "missing session was not preserved");

    MakepkgSyncDependencySessionObservation incomplete = complete_session();
    incomplete.terminal_state =
        MakepkgSyncDependencyTerminalState::Incomplete;
    incomplete.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Incomplete;
    const MakepkgSyncDependencySessionReceipt incomplete_receipt =
        validate_makepkg_sync_dependency_session(
            session_identity(), incomplete);
    expect(
        incomplete_receipt.state() ==
            MakepkgSyncDependencySessionState::Incomplete,
        "incomplete terminal session was not preserved");

    MakepkgSyncDependencySessionObservation unsupported = complete_session();
    unsupported.terminal_state =
        MakepkgSyncDependencyTerminalState::Unsupported;
    unsupported.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Unsupported;
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), unsupported)
                .state() ==
            MakepkgSyncDependencySessionState::Unsupported,
        "unsupported adapter session was not preserved");

    MakepkgSyncDependencySessionObservation pid_only = complete_session();
    pid_only.process_binding =
        MakepkgSyncDependencyProcessBinding::PidOnly;
    const MakepkgSyncDependencySessionReceipt pid_only_receipt =
        validate_makepkg_sync_dependency_session(
            session_identity(), pid_only);
    expect(
        pid_only_receipt.state() ==
                MakepkgSyncDependencySessionState::Unsupported &&
            !pid_only_receipt.is_trusted_terminal(),
        "PID-only process binding became positive session authority");

    MakepkgSyncDependencySessionObservation timestamp_only =
        complete_session();
    timestamp_only.process_binding =
        MakepkgSyncDependencyProcessBinding::TimestampOnly;
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), timestamp_only)
                .state() ==
            MakepkgSyncDependencySessionState::Unsupported,
        "timestamp-only process binding became positive session authority");

    MakepkgSyncDependencySessionObservation token_only = complete_session();
    token_only.owner.reset();
    token_only.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Missing;
    token_only.process_binding =
        MakepkgSyncDependencyProcessBinding::Missing;
    token_only.makepkg_outcome = {
        MakepkgSyncDependencyMakepkgOutcomeKind::Unknown, std::nullopt};
    token_only.transaction_count.reset();
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), token_only)
                .state() ==
            MakepkgSyncDependencySessionState::Incomplete,
        "session token alone became positive session authority");
}

void test_one_and_two_ordered_transactions() {
    const MakepkgSyncDependencySessionReceipt one =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Complete,
                {{PacmanTransactionPackageOperation::Install, "dep-one"}})}));
    expect(
        one.state() == MakepkgSyncDependencySessionState::CompleteOne &&
            one.transaction_observations().size() == 1 &&
            one.contains_authoritative_install("dep-one"),
        "one ordered transaction did not retain factual Install evidence");

    const MakepkgSyncDependencySessionReceipt two =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({
                transaction(
                    1, token('b'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
                transaction(
                    2, token('c'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-two"}}),
            }));
    expect(
        two.state() == MakepkgSyncDependencySessionState::CompleteTwo &&
            two.transaction_observations()[0].ordinal ==
                std::optional<MakepkgSyncDependencyTransactionOrdinal>{
                    MakepkgSyncDependencyTransactionOrdinal::First} &&
            two.transaction_observations()[1].ordinal ==
                std::optional<MakepkgSyncDependencyTransactionOrdinal>{
                    MakepkgSyncDependencyTransactionOrdinal::Second} &&
            two.contains_authoritative_install("dep-one") &&
            two.contains_authoritative_install("dep-two"),
        "two transactions did not preserve evidence order");

    MakepkgSyncDependencySessionObservation three = complete_session({
        transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Missing, {}),
        transaction(
            2, token('c'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Missing, {}),
        transaction(
            3, token('d'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Missing, {}),
    });
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), three)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "third transaction was not rejected before becoming trusted state");
}

void test_transaction_count_mismatches_are_rejected() {
    MakepkgSyncDependencySessionObservation declared_one =
        complete_session();
    declared_one.transaction_count = 1;
    const MakepkgSyncDependencySessionReceipt empty_ledger_mismatch =
        validate_makepkg_sync_dependency_session(
            session_identity(), declared_one);
    expect(
        empty_ledger_mismatch.state() ==
                MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                empty_ledger_mismatch,
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountMismatch),
        "declared count one with zero transactions was accepted");

    MakepkgSyncDependencySessionObservation declared_zero =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}})});
    declared_zero.transaction_count = 0;
    const MakepkgSyncDependencySessionReceipt one_transaction_mismatch =
        validate_makepkg_sync_dependency_session(
            session_identity(), declared_zero);
    expect(
        one_transaction_mismatch.state() ==
                MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                one_transaction_mismatch,
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountMismatch),
        "declared count zero with one transaction was accepted");
}

void test_session_identity_mismatches_are_rejected() {
    MakepkgSyncDependencySessionObservation token_mismatch =
        complete_session();
    token_mismatch.session_identity->session_token = token('f');
    const MakepkgSyncDependencySessionReceipt wrong_token =
        validate_makepkg_sync_dependency_session(
            session_identity(), token_mismatch);
    expect(
        wrong_token.state() == MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                wrong_token,
                MakepkgSyncDependencySessionIssueKind::SessionTokenMismatch),
        "observed session token mismatch was accepted");

    MakepkgSyncDependencySessionObservation uid_mismatch =
        complete_session();
    uid_mismatch.session_identity->invoking_uid = INVOKING_UID + 1;
    const MakepkgSyncDependencySessionReceipt wrong_uid =
        validate_makepkg_sync_dependency_session(
            session_identity(), uid_mismatch);
    expect(
        wrong_uid.state() == MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                wrong_uid,
                MakepkgSyncDependencySessionIssueKind::InvokingUidMismatch),
        "observed invoking uid mismatch was accepted");
}

void test_transaction_ordinal_order_mismatch_is_rejected() {
    const MakepkgSyncDependencySessionReceipt out_of_order =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({
                transaction(
                    2, token('b'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-two"}}),
                transaction(
                    1, token('c'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
            }));
    expect(
        out_of_order.state() == MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                out_of_order,
                MakepkgSyncDependencySessionIssueKind::
                    TransactionOrdinalMismatch) &&
            !has_session_issue(
                out_of_order,
                MakepkgSyncDependencySessionIssueKind::
                    DuplicateTransactionOrdinal),
        "Second-to-First transaction order was accepted");
}

void test_transaction_identity_and_owner_mismatches_are_rejected() {
    MakepkgSyncDependencySessionObservation missing_ordinal =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Missing, {})});
    missing_ordinal.transactions[0].ordinal.reset();
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), missing_ordinal)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "missing transaction ordinal was accepted");

    MakepkgSyncDependencySessionObservation duplicate_ordinal =
        complete_session({
            transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {}),
            transaction(
                1, token('c'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {}),
        });
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), duplicate_ordinal)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "duplicate transaction ordinal was accepted");

    MakepkgSyncDependencySessionObservation duplicate_token =
        complete_session({
            transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {}),
            transaction(
                2, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {}),
        });
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), duplicate_token)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "duplicate transaction token was accepted");

    MakepkgSyncDependencySessionObservation session_mismatch =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Missing, {})});
    session_mismatch.transactions[0].session_token = token('f');
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), session_mismatch)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "transaction/session token mismatch was accepted");

    MakepkgSyncDependencySessionObservation receipt_mismatch =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}})});
    receipt_mismatch.transactions[0].transaction.transaction_token =
        token('c');
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), receipt_mismatch)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "transaction/receipt token mismatch was accepted");

    MakepkgSyncDependencySessionObservation wrong_owner =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}},
            InvocationDependencyTransactionOwner::
                SelectedRepositoryProvider)});
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), wrong_owner)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "selected-provider owner was accepted by makepkg session factory");

    MakepkgSyncDependencySessionObservation wrong_session_owner =
        complete_session();
    wrong_session_owner.owner =
        InvocationDependencyTransactionOwner::SelectedRepositoryProvider;
    expect(
        validate_makepkg_sync_dependency_session(
            session_identity(), wrong_session_owner)
                .state() == MakepkgSyncDependencySessionState::Invalid,
        "selected-provider session owner was accepted");

    MakepkgSyncDependencySessionObservation mixed_owner =
        complete_session({
            transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {}),
            transaction(
                2, token('c'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {},
                InvocationDependencyTransactionOwner::
                    SelectedRepositoryProvider),
        });
    const MakepkgSyncDependencySessionReceipt mixed =
        validate_makepkg_sync_dependency_session(
            session_identity(), mixed_owner);
    expect(
        mixed.state() == MakepkgSyncDependencySessionState::Invalid &&
            has_session_issue(
                mixed,
                MakepkgSyncDependencySessionIssueKind::
                    MixedTransactionOwners),
        "mixed-owner ledger was accepted");
}

void test_positive_receipt_and_parent_failure_dimensions() {
    const MakepkgSyncDependencySessionReceipt upgrade =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Complete,
                {{PacmanTransactionPackageOperation::Upgrade, "dep-one"}})}));
    expect(
        !upgrade.contains_authoritative_install("dep-one"),
        "Upgrade was promoted to Install proof");

    const MakepkgSyncDependencySessionReceipt missing =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Missing, {})}));
    expect(
        missing.state() == MakepkgSyncDependencySessionState::CompleteOne &&
            !missing.contains_authoritative_install("dep-one"),
        "missing transaction receipt became positive proof");

    const MakepkgSyncDependencySessionReceipt failed_transaction =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Failed,
                PacmanTransactionReceiptObservationState::Complete,
                {{PacmanTransactionPackageOperation::Install, "dep-one"}})}));
    expect(
        !failed_transaction.contains_authoritative_install("dep-one"),
        "failed transaction became positive proof");

    const MakepkgSyncDependencySessionReceipt unknown_transaction =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Unknown,
                PacmanTransactionReceiptObservationState::Complete,
                {{PacmanTransactionPackageOperation::Install, "dep-one"}})}));
    expect(
        unknown_transaction.state() ==
                MakepkgSyncDependencySessionState::Incomplete &&
            !unknown_transaction.contains_authoritative_install("dep-one"),
        "unknown transaction outcome became complete positive authority");

    const MakepkgSyncDependencySessionReceipt parent_failed =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session(
                {
                    transaction(
                        1, token('b'),
                        InvocationDependencyTransactionCommandOutcome::
                            Succeeded,
                        PacmanTransactionReceiptObservationState::Complete,
                        {{PacmanTransactionPackageOperation::Install,
                          "dep-one"}}),
                    transaction(
                        2, token('c'),
                        InvocationDependencyTransactionCommandOutcome::Failed,
                        PacmanTransactionReceiptObservationState::Missing,
                        {}),
                },
                {MakepkgSyncDependencyMakepkgOutcomeKind::Failed, 13}));
    expect(
        parent_failed.state() ==
                MakepkgSyncDependencySessionState::CompleteTwo &&
            parent_failed.makepkg_outcome().kind ==
                MakepkgSyncDependencyMakepkgOutcomeKind::Failed &&
            parent_failed.makepkg_outcome().exit_code ==
                std::optional<int>{13} &&
            parent_failed.contains_authoritative_install("dep-one") &&
            !parent_failed.contains_authoritative_install("dep-two"),
        "parent makepkg failure did not retain the earlier factual receipt");
}

void test_incomplete_or_bypassed_adapter_never_proves_install() {
    MakepkgSyncDependencySessionObservation incomplete =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}})});
    incomplete.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Incomplete;
    const MakepkgSyncDependencySessionReceipt incomplete_receipt =
        validate_makepkg_sync_dependency_session(
            session_identity(), incomplete);
    expect(
        incomplete_receipt.state() ==
                MakepkgSyncDependencySessionState::Incomplete &&
            !incomplete_receipt.contains_authoritative_install("dep-one"),
        "incomplete adapter coverage became positive proof");

    MakepkgSyncDependencySessionObservation bypassed = incomplete;
    bypassed.terminal_state =
        MakepkgSyncDependencyTerminalState::Unsupported;
    bypassed.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Bypassed;
    const MakepkgSyncDependencySessionReceipt bypassed_receipt =
        validate_makepkg_sync_dependency_session(
            session_identity(), bypassed);
    expect(
        bypassed_receipt.state() ==
                MakepkgSyncDependencySessionState::Bypassed &&
            bypassed_receipt.adapter_coverage() ==
                MakepkgSyncDependencyAdapterCoverage::Bypassed &&
            !bypassed_receipt.contains_authoritative_install("dep-one"),
        "adapter bypass was flattened or became positive proof");
}

void test_owner_specific_projection_preserves_session_authority() {
    const MakepkgSyncDependencySessionReceipt trusted_one =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({transaction(
                1, token('b'),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservationState::Complete,
                {{PacmanTransactionPackageOperation::Install, "dep-one"}})}));
    expect(
        project_makepkg_causal_ownership(trusted_one, "dep-one") ==
            CleanupCausalOwnership::InvocationOwned,
        "trusted CompleteOne did not project makepkg-owned causality");

    const MakepkgSyncDependencySessionReceipt trusted_two =
        validate_makepkg_sync_dependency_session(
            session_identity(),
            complete_session({
                transaction(
                    1, token('b'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
                transaction(
                    2, token('c'),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    PacmanTransactionReceiptObservationState::Complete,
                    {{PacmanTransactionPackageOperation::Install, "dep-two"}}),
            }));
    expect(
        project_makepkg_causal_ownership(trusted_two, "dep-one") ==
                CleanupCausalOwnership::InvocationOwned &&
            project_makepkg_causal_ownership(trusted_two, "dep-two") ==
                CleanupCausalOwnership::InvocationOwned,
        "trusted CompleteTwo lost makepkg-owned causal receipts");

    MakepkgSyncDependencySessionObservation bypassed = complete_session({
        transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
    });
    bypassed.terminal_state =
        MakepkgSyncDependencyTerminalState::Unsupported;
    bypassed.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Bypassed;
    expect(
        project_makepkg_causal_ownership(
            validate_makepkg_sync_dependency_session(
                session_identity(), bypassed),
            "dep-one") == CleanupCausalOwnership::Unknown,
        "bypassed session escaped through makepkg causal projection");

    MakepkgSyncDependencySessionObservation incomplete_binding =
        complete_session({transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}})});
    incomplete_binding.process_binding =
        MakepkgSyncDependencyProcessBinding::Incomplete;
    expect(
        project_makepkg_causal_ownership(
            validate_makepkg_sync_dependency_session(
                session_identity(), incomplete_binding),
            "dep-one") == CleanupCausalOwnership::Unknown,
        "incomplete process binding escaped through causal projection");

    MakepkgSyncDependencySessionObservation mixed_owner = complete_session({
        transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
        transaction(
            2, token('c'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-two"}},
            InvocationDependencyTransactionOwner::
                SelectedRepositoryProvider),
    });
    expect(
        project_makepkg_causal_ownership(
            validate_makepkg_sync_dependency_session(
                session_identity(), mixed_owner),
            "dep-one") == CleanupCausalOwnership::Unknown,
        "invalid mixed-owner session projected positive makepkg causality");

    MakepkgSyncDependencySessionObservation unsupported = complete_session({
        transaction(
            1, token('b'),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservationState::Complete,
            {{PacmanTransactionPackageOperation::Install, "dep-one"}}),
    });
    unsupported.terminal_state =
        MakepkgSyncDependencyTerminalState::Unsupported;
    unsupported.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Unsupported;
    expect(
        project_makepkg_causal_ownership(
            validate_makepkg_sync_dependency_session(
                session_identity(), unsupported),
            "dep-one") == CleanupCausalOwnership::Unknown,
        "unsupported session projected positive makepkg causality");

    MakepkgSyncDependencySessionObservation conflict = unsupported;
    conflict.adapter_coverage =
        MakepkgSyncDependencyAdapterCoverage::Conflict;
    expect(
        project_makepkg_causal_ownership(
            validate_makepkg_sync_dependency_session(
                session_identity(), conflict),
            "dep-one") == CleanupCausalOwnership::Unknown,
        "conflicting session projected positive makepkg causality");
}

void test_strict_pacman_positive_grammar() {
    const MakepkgSyncDependencyPacmanCall dependency_check =
        parse_makepkg_sync_dependency_pacman_call(
            {"-T", "runtime>=2", "virtual:libexample.so=1-64"});
    expect(
        dependency_check.is_supported() &&
            dependency_check.kind() ==
                MakepkgSyncDependencyPacmanCallKind::DependencyCheck &&
            dependency_check.dependency_specifications() ==
                std::vector<std::string>{
                    "runtime>=2", "virtual:libexample.so=1-64"},
        "read-only dependency check was not parsed losslessly");

    const MakepkgSyncDependencyPacmanCall package_query =
        parse_makepkg_sync_dependency_pacman_call({"-Qi"});
    expect(
        package_query.is_supported() &&
            package_query.kind() ==
                MakepkgSyncDependencyPacmanCallKind::InstalledPackageQuery,
        "current makepkg installed-package query was rejected");

    const std::vector<std::string> install_arguments{
        "--noconfirm", "--noprogressbar", "--color", "never", "-S",
        "--asdeps", "runtime>=2", "provider-name"};
    const MakepkgSyncDependencyPacmanCall install =
        parse_makepkg_sync_dependency_pacman_call(install_arguments);
    expect(
        install.is_supported() &&
            install.kind() ==
                MakepkgSyncDependencyPacmanCallKind::DependencyInstall &&
            install.safe_options() ==
                MakepkgSyncDependencyPacmanSafeOptions{true, true, true} &&
            install.dependency_specifications() ==
                std::vector<std::string>{"runtime>=2", "provider-name"} &&
            install.arguments() == install_arguments,
        "strict dependency install grammar changed opaque argv");
}

void test_strict_pacman_reject_matrix() {
    const std::vector<std::vector<std::string>> rejected_calls{
        {"--config", "/tmp/pacman.conf", "-S", "--asdeps", "dep"},
        {"--hookdir=/tmp/hooks", "-S", "--asdeps", "dep"},
        {"--root", "/tmp/root", "-S", "--asdeps", "dep"},
        {"--sysroot=/tmp/sysroot", "-S", "--asdeps", "dep"},
        {"--dbpath", "/tmp/db", "-S", "--asdeps", "dep"},
        {"--cachedir=/tmp/cache", "-S", "--asdeps", "dep"},
        {"--gpgdir", "/tmp/gpg", "-S", "--asdeps", "dep"},
        {"--logfile=/tmp/log", "-S", "--asdeps", "dep"},
        {"-Rns", "dep"},
        {"-U", "package.pkg.tar.zst"},
        {"--nodeps", "-S", "--asdeps", "dep"},
        {"--assume-installed=dep=1", "-S", "--asdeps", "dep"},
        {"--dbonly", "-S", "--asdeps", "dep"},
        {"--noscriptlet", "-S", "--asdeps", "dep"},
        {"-Syu"},
        {"-D", "--asdeps", "dep"},
        {"--unknown", "-S", "--asdeps", "dep"},
        {"-S", "dep"},
        {"-S", "--asdeps", "--ambiguous-option"},
        {"-Qi", "dep"},
        {"-Qq"},
    };

    for(const std::vector<std::string>& arguments : rejected_calls) {
        const MakepkgSyncDependencyPacmanCall result =
            parse_makepkg_sync_dependency_pacman_call(arguments);
        expect(
            !result.is_supported() &&
                result.kind() ==
                    MakepkgSyncDependencyPacmanCallKind::Unsupported &&
                result.arguments() == arguments && !result.issues().empty(),
            "PACMAN reject matrix admitted or lost an unsupported call");
    }
}

void test_pacman_and_pacman_auth_route_policy() {
    const MakepkgSyncDependencyPacmanRoutePolicy supported =
        evaluate_makepkg_sync_dependency_pacman_route({{MakepkgSyncDependencyPacmanSettingState::
                                                            InvocationOwnedInstalledAdapter,
                                                        {"/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter"}},
                                                       {MakepkgSyncDependencyPacmanAuthSettingState::MakepkgDefault,
                                                        {}}});
    expect(
        supported.state() ==
                MakepkgSyncDependencyPacmanRouteState::Supported &&
            !supported.establishes_receipt_authority(),
        "supported PACMAN route incorrectly made PACMAN_AUTH receipt authority");

    const MakepkgSyncDependencyPacmanRoutePolicy custom_pacman =
        evaluate_makepkg_sync_dependency_pacman_route({{MakepkgSyncDependencyPacmanSettingState::Custom,
                                                        {"/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter"}},
                                                       {MakepkgSyncDependencyPacmanAuthSettingState::MakepkgDefault,
                                                        {}}});
    expect(
        custom_pacman.state() ==
                MakepkgSyncDependencyPacmanRouteState::Unsupported &&
            custom_pacman.observation().pacman.observed_values ==
                std::vector<std::string>{
                    "/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter"},
        "custom PACMAN was silently replaced or lost");

    const MakepkgSyncDependencyPacmanRoutePolicy custom_auth =
        evaluate_makepkg_sync_dependency_pacman_route({{MakepkgSyncDependencyPacmanSettingState::
                                                            InvocationOwnedInstalledAdapter,
                                                        {"/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter"}},
                                                       {MakepkgSyncDependencyPacmanAuthSettingState::Custom,
                                                        {"/home/user/bin/auth-wrapper"}}});
    expect(
        custom_auth.state() ==
                MakepkgSyncDependencyPacmanRouteState::Unsupported &&
            custom_auth.observation().pacman_auth.observed_values ==
                std::vector<std::string>{
                    "/home/user/bin/auth-wrapper"},
        "custom PACMAN_AUTH was silently chained or lost");

    const MakepkgSyncDependencyPacmanRoutePolicy conflict =
        evaluate_makepkg_sync_dependency_pacman_route({{MakepkgSyncDependencyPacmanSettingState::Conflict,
                                                        {"/usr/bin/pacman", "/home/user/bin/pacman-wrapper"}},
                                                       {MakepkgSyncDependencyPacmanAuthSettingState::Conflict,
                                                        {"sudo", "doas"}}});
    expect(
        conflict.state() ==
            MakepkgSyncDependencyPacmanRouteState::Conflict,
        "PACMAN/PACMAN_AUTH conflict was flattened");

    const MakepkgSyncDependencyPacmanRoutePolicy unknown =
        evaluate_makepkg_sync_dependency_pacman_route({{MakepkgSyncDependencyPacmanSettingState::Unknown, {}},
                                                       {MakepkgSyncDependencyPacmanAuthSettingState::Unknown, {}}});
    expect(
        unknown.state() == MakepkgSyncDependencyPacmanRouteState::Unknown,
        "unknown PACMAN route coverage was flattened");
}

void test_selected_provider_receipt_regression() {
    const auto owner = InvocationDependencyTransactionOwner::
        SelectedRepositoryProvider;
    const PacmanTransactionReceipt receipt =
        validate_pacman_transaction_receipt(
            token('b'), owner,
            PacmanTransactionReceiptObservation{
                PacmanTransactionReceiptObservationState::Complete,
                token('b'),
                owner,
                {{PacmanTransactionPackageOperation::Install,
                  "selected-provider-install"}}});
    expect(
        receipt.is_complete_for(token('b'), owner) &&
            receipt.contains_newly_installed_package(
                "selected-provider-install"),
        "selected-provider receipt contract regressed");
}

} // namespace

void run_makepkg_syncdeps_receipt_model_tests() {
    test_trusted_zero_requires_complete_terminal_authority();
    test_missing_incomplete_unsupported_and_binding_states();
    test_one_and_two_ordered_transactions();
    test_transaction_count_mismatches_are_rejected();
    test_session_identity_mismatches_are_rejected();
    test_transaction_ordinal_order_mismatch_is_rejected();
    test_transaction_identity_and_owner_mismatches_are_rejected();
    test_positive_receipt_and_parent_failure_dimensions();
    test_incomplete_or_bypassed_adapter_never_proves_install();
    test_owner_specific_projection_preserves_session_authority();
    test_strict_pacman_positive_grammar();
    test_strict_pacman_reject_matrix();
    test_pacman_and_pacman_auth_route_policy();
    test_selected_provider_receipt_regression();
}
