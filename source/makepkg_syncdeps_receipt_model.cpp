#include "makepkg_syncdeps_receipt_model.hpp"

#include "makepkg_syncdeps_pacman_contract.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace {

void add_issue(
    std::vector<MakepkgSyncDependencySessionIssueKind>& issues,
    MakepkgSyncDependencySessionIssueKind issue) {
    if(std::find(issues.begin(), issues.end(), issue) == issues.end()) {
        issues.push_back(issue);
    }
}

void canonicalize_issues(
    std::vector<MakepkgSyncDependencySessionIssueKind>& issues) {
    std::sort(issues.begin(), issues.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
}

bool is_valid_terminal_state(
    MakepkgSyncDependencyTerminalState state) noexcept {
    switch(state) {
        case MakepkgSyncDependencyTerminalState::Missing:
        case MakepkgSyncDependencyTerminalState::Incomplete:
        case MakepkgSyncDependencyTerminalState::Complete:
        case MakepkgSyncDependencyTerminalState::Unsupported:
            return true;
    }
    return false;
}

bool is_valid_adapter_coverage(
    MakepkgSyncDependencyAdapterCoverage coverage) noexcept {
    switch(coverage) {
        case MakepkgSyncDependencyAdapterCoverage::Missing:
        case MakepkgSyncDependencyAdapterCoverage::Incomplete:
        case MakepkgSyncDependencyAdapterCoverage::Complete:
        case MakepkgSyncDependencyAdapterCoverage::Bypassed:
        case MakepkgSyncDependencyAdapterCoverage::Unsupported:
        case MakepkgSyncDependencyAdapterCoverage::Conflict:
        case MakepkgSyncDependencyAdapterCoverage::Unknown:
            return true;
    }
    return false;
}

bool is_valid_process_binding(
    MakepkgSyncDependencyProcessBinding binding) noexcept {
    switch(binding) {
        case MakepkgSyncDependencyProcessBinding::Missing:
        case MakepkgSyncDependencyProcessBinding::Incomplete:
        case MakepkgSyncDependencyProcessBinding::
            InstalledLauncherAndExactLauncherMakepkgLifetime:
        case MakepkgSyncDependencyProcessBinding::PidOnly:
        case MakepkgSyncDependencyProcessBinding::TimestampOnly:
        case MakepkgSyncDependencyProcessBinding::Unsupported:
        case MakepkgSyncDependencyProcessBinding::Unknown:
            return true;
    }
    return false;
}

bool is_valid_makepkg_outcome_kind(
    MakepkgSyncDependencyMakepkgOutcomeKind kind) noexcept {
    switch(kind) {
        case MakepkgSyncDependencyMakepkgOutcomeKind::NotAttempted:
        case MakepkgSyncDependencyMakepkgOutcomeKind::Succeeded:
        case MakepkgSyncDependencyMakepkgOutcomeKind::Failed:
        case MakepkgSyncDependencyMakepkgOutcomeKind::Unknown:
            return true;
    }
    return false;
}

bool is_valid_transaction_command_outcome(
    InvocationDependencyTransactionCommandOutcome outcome) noexcept {
    switch(outcome) {
        case InvocationDependencyTransactionCommandOutcome::NotAttempted:
        case InvocationDependencyTransactionCommandOutcome::Succeeded:
        case InvocationDependencyTransactionCommandOutcome::Failed:
        case InvocationDependencyTransactionCommandOutcome::Unknown:
            return true;
    }
    return false;
}

bool is_valid_transaction_ordinal(
    MakepkgSyncDependencyTransactionOrdinal ordinal) noexcept {
    switch(ordinal) {
        case MakepkgSyncDependencyTransactionOrdinal::First:
        case MakepkgSyncDependencyTransactionOrdinal::Second:
            return true;
    }
    return false;
}

bool is_valid_session_owner(
    InvocationDependencyTransactionOwner owner) noexcept {
    switch(owner) {
        case InvocationDependencyTransactionOwner::SelectedRepositoryProvider:
        case InvocationDependencyTransactionOwner::SourceArtifactInstall:
        case InvocationDependencyTransactionOwner::MakepkgSyncDependencies:
            return true;
        case InvocationDependencyTransactionOwner::Unknown:
            return false;
    }
    return false;
}

bool makepkg_outcome_is_exact(
    const MakepkgSyncDependencyMakepkgOutcome& outcome,
    bool& is_invalid) noexcept {
    if(!is_valid_makepkg_outcome_kind(outcome.kind)) {
        is_invalid = true;
        return false;
    }
    if(outcome.exit_code.has_value() &&
       (outcome.exit_code.value() < 0 || outcome.exit_code.value() > 255)) {
        is_invalid = true;
        return false;
    }
    switch(outcome.kind) {
        case MakepkgSyncDependencyMakepkgOutcomeKind::Succeeded:
            if(!outcome.exit_code.has_value() ||
               outcome.exit_code.value() != 0) {
                is_invalid = outcome.exit_code.has_value();
                return false;
            }
            return true;
        case MakepkgSyncDependencyMakepkgOutcomeKind::Failed:
            if(!outcome.exit_code.has_value() ||
               outcome.exit_code.value() == 0) {
                is_invalid = outcome.exit_code.has_value();
                return false;
            }
            return true;
        case MakepkgSyncDependencyMakepkgOutcomeKind::NotAttempted:
        case MakepkgSyncDependencyMakepkgOutcomeKind::Unknown:
            if(outcome.exit_code.has_value()) is_invalid = true;
            return false;
    }
    is_invalid = true;
    return false;
}

bool missing_observation_has_unexpected_data(
    const MakepkgSyncDependencySessionObservation& observation) noexcept {
    return observation.session_identity.has_value() ||
           observation.owner.has_value() ||
           observation.adapter_coverage !=
               MakepkgSyncDependencyAdapterCoverage::Missing ||
           observation.process_binding !=
               MakepkgSyncDependencyProcessBinding::Missing ||
           observation.makepkg_outcome.kind !=
               MakepkgSyncDependencyMakepkgOutcomeKind::NotAttempted ||
           observation.makepkg_outcome.exit_code.has_value() ||
           observation.transaction_count.has_value() ||
           !observation.transactions.empty();
}

MakepkgSyncDependencySessionState complete_state_for_count(
    std::size_t transaction_count) noexcept {
    switch(transaction_count) {
        case 0:
            return MakepkgSyncDependencySessionState::CompleteZero;
        case 1:
            return MakepkgSyncDependencySessionState::CompleteOne;
        case 2:
            return MakepkgSyncDependencySessionState::CompleteTwo;
        default:
            return MakepkgSyncDependencySessionState::Invalid;
    }
}

} // namespace

MakepkgSyncDependencySessionReceipt::
    MakepkgSyncDependencySessionReceipt(
        MakepkgSyncDependencySessionState state,
        MakepkgSyncDependencySessionObservation observation,
        std::vector<MakepkgSyncDependencySessionIssueKind> issues) noexcept
    : state_(state), observation_(std::move(observation)),
      issues_(std::move(issues)) {
}

MakepkgSyncDependencySessionState
MakepkgSyncDependencySessionReceipt::state() const noexcept {
    return state_;
}

bool MakepkgSyncDependencySessionReceipt::is_trusted_terminal()
    const noexcept {
    return state_ == MakepkgSyncDependencySessionState::CompleteZero ||
           state_ == MakepkgSyncDependencySessionState::CompleteOne ||
           state_ == MakepkgSyncDependencySessionState::CompleteTwo;
}

const std::optional<MakepkgSyncDependencySessionIdentity>&
MakepkgSyncDependencySessionReceipt::session_identity() const noexcept {
    return observation_.session_identity;
}

const std::optional<InvocationDependencyTransactionOwner>&
MakepkgSyncDependencySessionReceipt::owner() const noexcept {
    return observation_.owner;
}

MakepkgSyncDependencyAdapterCoverage
MakepkgSyncDependencySessionReceipt::adapter_coverage() const noexcept {
    return observation_.adapter_coverage;
}

MakepkgSyncDependencyProcessBinding
MakepkgSyncDependencySessionReceipt::process_binding() const noexcept {
    return observation_.process_binding;
}

const MakepkgSyncDependencyMakepkgOutcome&
MakepkgSyncDependencySessionReceipt::makepkg_outcome() const noexcept {
    return observation_.makepkg_outcome;
}

const std::optional<std::size_t>&
MakepkgSyncDependencySessionReceipt::transaction_count() const noexcept {
    return observation_.transaction_count;
}

const std::vector<MakepkgSyncDependencyTransactionObservation>&
MakepkgSyncDependencySessionReceipt::transaction_observations()
    const noexcept {
    return observation_.transactions;
}

const std::vector<MakepkgSyncDependencySessionIssueKind>&
MakepkgSyncDependencySessionReceipt::issues() const noexcept {
    return issues_;
}

bool MakepkgSyncDependencySessionReceipt::contains_authoritative_install(
    const std::string& package_name) const noexcept {
    if(!is_trusted_terminal() || !is_valid_package_name(package_name)) {
        return false;
    }
    return std::any_of(
        observation_.transactions.begin(), observation_.transactions.end(),
        [&package_name](
            const MakepkgSyncDependencyTransactionObservation& observed) {
            const InvocationDependencyTransaction& transaction =
                observed.transaction;
            return transaction.owner ==
                       InvocationDependencyTransactionOwner::
                           MakepkgSyncDependencies &&
                   transaction.command_outcome ==
                       InvocationDependencyTransactionCommandOutcome::
                           Succeeded &&
                   is_valid_pacman_transaction_token(
                       transaction.transaction_token) &&
                   transaction.receipt.is_complete_for(
                       transaction.transaction_token,
                       InvocationDependencyTransactionOwner::
                           MakepkgSyncDependencies) &&
                   transaction.receipt.contains_newly_installed_package(
                       package_name);
        });
}

bool is_valid_makepkg_sync_dependency_session_token(
    const std::string& session_token) noexcept {
    return is_valid_pacman_transaction_token(session_token);
}

MakepkgSyncDependencySessionReceipt
validate_makepkg_sync_dependency_session(
    const MakepkgSyncDependencySessionIdentity& expected_identity,
    const MakepkgSyncDependencySessionObservation& observation) {
    std::vector<MakepkgSyncDependencySessionIssueKind> issues;

    bool is_invalid = false;
    bool is_incomplete = false;
    bool is_unsupported = false;
    const auto invalidate = [&issues, &is_invalid](
                                MakepkgSyncDependencySessionIssueKind issue) {
        add_issue(issues, issue);
        is_invalid = true;
    };
    const auto incomplete = [&issues, &is_incomplete](
                                MakepkgSyncDependencySessionIssueKind issue) {
        add_issue(issues, issue);
        is_incomplete = true;
    };
    const auto unsupported = [&issues, &is_unsupported](
                                 MakepkgSyncDependencySessionIssueKind issue) {
        add_issue(issues, issue);
        is_unsupported = true;
    };

    if(!is_valid_makepkg_sync_dependency_session_token(
           expected_identity.session_token)) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::
                InvalidExpectedSessionToken);
    }
    if(!is_valid_terminal_state(observation.terminal_state)) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::InvalidTerminalState);
    }
    if(!is_valid_adapter_coverage(observation.adapter_coverage)) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::AdapterCoverageUnknown);
    }
    if(!is_valid_process_binding(observation.process_binding)) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::ProcessBindingUnknown);
    }

    if(observation.terminal_state ==
       MakepkgSyncDependencyTerminalState::Missing) {
        add_issue(
            issues, MakepkgSyncDependencySessionIssueKind::SessionMissing);
        if(missing_observation_has_unexpected_data(observation)) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    UnexpectedMissingSessionData);
        }
        canonicalize_issues(issues);
        return MakepkgSyncDependencySessionReceipt(
            is_invalid ? MakepkgSyncDependencySessionState::Invalid
                       : MakepkgSyncDependencySessionState::Missing,
            observation, std::move(issues));
    }

    if(observation.terminal_state ==
       MakepkgSyncDependencyTerminalState::Incomplete) {
        incomplete(
            MakepkgSyncDependencySessionIssueKind::SessionIncomplete);
    }

    if(!observation.session_identity.has_value()) {
        if(observation.terminal_state ==
           MakepkgSyncDependencyTerminalState::Complete) {
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    SessionIdentityMissing);
        }
    } else {
        if(!is_valid_makepkg_sync_dependency_session_token(
               observation.session_identity->session_token)) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidObservedSessionToken);
        } else if(observation.session_identity->session_token !=
                  expected_identity.session_token) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::SessionTokenMismatch);
        }
        if(observation.session_identity->invoking_uid !=
           expected_identity.invoking_uid) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::InvokingUidMismatch);
        }
    }

    if(!observation.owner.has_value()) {
        if(observation.terminal_state ==
           MakepkgSyncDependencyTerminalState::Complete) {
            incomplete(
                MakepkgSyncDependencySessionIssueKind::SessionOwnerMissing);
        }
    } else if(!is_valid_session_owner(observation.owner.value())) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::InvalidSessionOwner);
    } else if(observation.owner.value() !=
              InvocationDependencyTransactionOwner::
                  MakepkgSyncDependencies) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::SessionOwnerMismatch);
    }

    switch(observation.adapter_coverage) {
        case MakepkgSyncDependencyAdapterCoverage::Missing:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    AdapterCoverageMissing);
            break;
        case MakepkgSyncDependencyAdapterCoverage::Incomplete:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    AdapterCoverageIncomplete);
            break;
        case MakepkgSyncDependencyAdapterCoverage::Complete:
            break;
        case MakepkgSyncDependencyAdapterCoverage::Bypassed:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::AdapterBypassed);
            break;
        case MakepkgSyncDependencyAdapterCoverage::Unsupported:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::AdapterUnsupported);
            break;
        case MakepkgSyncDependencyAdapterCoverage::Conflict:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::AdapterConflict);
            break;
        case MakepkgSyncDependencyAdapterCoverage::Unknown:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    AdapterCoverageUnknown);
            break;
    }

    switch(observation.process_binding) {
        case MakepkgSyncDependencyProcessBinding::Missing:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    ProcessBindingMissing);
            break;
        case MakepkgSyncDependencyProcessBinding::Incomplete:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    ProcessBindingIncomplete);
            break;
        case MakepkgSyncDependencyProcessBinding::
            InstalledLauncherAndExactLauncherMakepkgLifetime:
            break;
        case MakepkgSyncDependencyProcessBinding::PidOnly:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::
                    PidOnlyProcessBinding);
            break;
        case MakepkgSyncDependencyProcessBinding::TimestampOnly:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::
                    TimestampOnlyProcessBinding);
            break;
        case MakepkgSyncDependencyProcessBinding::Unsupported:
            unsupported(
                MakepkgSyncDependencySessionIssueKind::
                    ProcessBindingUnsupported);
            break;
        case MakepkgSyncDependencyProcessBinding::Unknown:
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    ProcessBindingUnknown);
            break;
    }

    bool makepkg_outcome_invalid = false;
    if(!makepkg_outcome_is_exact(
           observation.makepkg_outcome, makepkg_outcome_invalid)) {
        if(makepkg_outcome_invalid) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidMakepkgOutcome);
        } else {
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    MakepkgOutcomeNotExact);
        }
    }

    if(!observation.transaction_count.has_value()) {
        if(observation.terminal_state ==
           MakepkgSyncDependencyTerminalState::Complete) {
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountMissing);
        }
    } else {
        if(observation.transaction_count.value() > 2) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountTooLarge);
        }
        if(observation.transaction_count.value() !=
           observation.transactions.size()) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCountMismatch);
        }
    }
    if(observation.transactions.size() > 2) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::
                TransactionCountTooLarge);
    }

    std::set<MakepkgSyncDependencyTransactionOrdinal> ordinals;
    std::set<std::string> transaction_tokens;
    std::set<InvocationDependencyTransactionOwner> transaction_owners;
    for(std::size_t index = 0; index < observation.transactions.size();
        ++index) {
        const MakepkgSyncDependencyTransactionObservation& observed =
            observation.transactions[index];
        const InvocationDependencyTransaction& transaction =
            observed.transaction;

        if(!observed.ordinal.has_value()) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionOrdinalMissing);
        } else {
            if(!is_valid_transaction_ordinal(observed.ordinal.value())) {
                invalidate(
                    MakepkgSyncDependencySessionIssueKind::
                        TransactionOrdinalOutOfRange);
            }
            if(!ordinals.insert(observed.ordinal.value()).second) {
                invalidate(
                    MakepkgSyncDependencySessionIssueKind::
                        DuplicateTransactionOrdinal);
            }
            if(static_cast<std::size_t>(observed.ordinal.value()) !=
               index + 1) {
                invalidate(
                    MakepkgSyncDependencySessionIssueKind::
                        TransactionOrdinalMismatch);
            }
        }

        if(!observed.session_token.has_value()) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionSessionTokenMissing);
        } else if(!is_valid_makepkg_sync_dependency_session_token(
                      observed.session_token.value())) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidTransactionSessionToken);
        } else if(observed.session_token.value() !=
                  expected_identity.session_token) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionSessionTokenMismatch);
        }

        if(!is_valid_pacman_transaction_token(
               transaction.transaction_token)) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidTransactionToken);
        } else if(!transaction_tokens
                       .insert(transaction.transaction_token)
                       .second) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    DuplicateTransactionToken);
        }

        transaction_owners.insert(transaction.owner);
        if(transaction.owner !=
           InvocationDependencyTransactionOwner::
               MakepkgSyncDependencies) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionOwnerMismatch);
        }
        if(!transaction.requested_package_names.empty()) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    UnexpectedRequestedPackageNames);
        }
        if(observed.dependency_specifications.empty()) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    DependencySpecificationsMissing);
        }
        if(std::any_of(
               observed.dependency_specifications.begin(),
               observed.dependency_specifications.end(),
               [](const std::string& specification) {
                   return !is_valid_makepkg_sync_dependency_specification(
                       specification);
               })) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidDependencySpecification);
        }
        if(!is_valid_transaction_command_outcome(
               transaction.command_outcome)) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidTransactionCommandOutcome);
        } else if(transaction.command_outcome ==
                      InvocationDependencyTransactionCommandOutcome::
                          NotAttempted ||
                  transaction.command_outcome ==
                      InvocationDependencyTransactionCommandOutcome::Unknown) {
            incomplete(
                MakepkgSyncDependencySessionIssueKind::
                    TransactionCommandOutcomeNotExact);
        }
        if(transaction.receipt.state() ==
               PacmanTransactionReceiptState::Invalid ||
           (transaction.receipt.transaction_token().has_value() &&
            transaction.receipt.transaction_token().value() !=
                transaction.transaction_token) ||
           (transaction.receipt.owner().has_value() &&
            transaction.receipt.owner().value() !=
                InvocationDependencyTransactionOwner::
                    MakepkgSyncDependencies)) {
            invalidate(
                MakepkgSyncDependencySessionIssueKind::
                    InvalidTransactionReceipt);
        }
    }
    if(transaction_owners.size() > 1) {
        invalidate(
            MakepkgSyncDependencySessionIssueKind::MixedTransactionOwners);
    }

    canonicalize_issues(issues);
    if(is_invalid) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Invalid, observation,
            std::move(issues));
    }
    if(observation.terminal_state ==
       MakepkgSyncDependencyTerminalState::Incomplete) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Incomplete, observation,
            std::move(issues));
    }
    if(observation.terminal_state ==
       MakepkgSyncDependencyTerminalState::Unsupported) {
        MakepkgSyncDependencySessionState state =
            MakepkgSyncDependencySessionState::Unsupported;
        if(observation.adapter_coverage ==
           MakepkgSyncDependencyAdapterCoverage::Bypassed) {
            state = MakepkgSyncDependencySessionState::Bypassed;
        } else if(observation.adapter_coverage ==
                  MakepkgSyncDependencyAdapterCoverage::Conflict) {
            state = MakepkgSyncDependencySessionState::Conflict;
        }
        return MakepkgSyncDependencySessionReceipt(
            state, observation, std::move(issues));
    }
    if(observation.adapter_coverage ==
       MakepkgSyncDependencyAdapterCoverage::Bypassed) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Bypassed, observation,
            std::move(issues));
    }
    if(observation.adapter_coverage ==
       MakepkgSyncDependencyAdapterCoverage::Conflict) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Conflict, observation,
            std::move(issues));
    }
    if(is_unsupported) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Unsupported, observation,
            std::move(issues));
    }
    if(is_incomplete ||
       observation.terminal_state !=
           MakepkgSyncDependencyTerminalState::Complete ||
       !observation.transaction_count.has_value()) {
        return MakepkgSyncDependencySessionReceipt(
            MakepkgSyncDependencySessionState::Incomplete, observation,
            std::move(issues));
    }

    return MakepkgSyncDependencySessionReceipt(
        complete_state_for_count(observation.transaction_count.value()),
        observation, std::move(issues));
}
