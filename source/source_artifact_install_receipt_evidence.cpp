#include "source_artifact_install_receipt_evidence.hpp"

#include "package_identifier.hpp"
#include "source_package_identity_projection.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using EvidenceIssue = SourceArtifactInstallReceiptEvidenceIssueKind;

void add_issue(
    std::vector<EvidenceIssue>& issues,
    EvidenceIssue issue) {
    if(std::find(issues.begin(), issues.end(), issue) == issues.end()) {
        issues.push_back(issue);
    }
}

void canonicalize_issues(std::vector<EvidenceIssue>& issues) {
    std::sort(issues.begin(), issues.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
}

bool roots_are_valid_and_unique(
    const std::vector<RootTargetIdentity>& roots) {
    if(roots.empty()) return false;
    std::set<std::pair<std::size_t, std::string>> seen;
    for(const RootTargetIdentity& root : roots) {
        if(!is_valid_package_name(root.requested_name) ||
           !seen.emplace(root.invocation_index, root.requested_name).second) {
            return false;
        }
    }
    return true;
}

bool same_root_set(
    const std::vector<RootTargetIdentity>& lhs,
    const std::vector<RootTargetIdentity>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(lhs.begin(), lhs.end(), [&rhs](const auto& root) {
        return std::find(rhs.begin(), rhs.end(), root) != rhs.end();
    });
}

bool root_set_is_subset(
    const std::vector<RootTargetIdentity>& subset,
    const std::vector<RootTargetIdentity>& superset) {
    return std::all_of(
        subset.begin(), subset.end(), [&superset](const auto& root) {
            return std::find(superset.begin(), superset.end(), root) !=
                   superset.end();
        });
}

bool is_build_or_check_role(PackageRole role) noexcept {
    switch(role) {
        case PackageRole::BuildDependency:
        case PackageRole::CheckDependency:
            return true;
        case PackageRole::Root:
        case PackageRole::RuntimeDependency:
            return false;
    }
    return false;
}

bool roles_are_unique(const std::vector<PackageRole>& roles) {
    std::set<int> seen;
    return std::all_of(roles.begin(), roles.end(), [&seen](PackageRole role) {
        return seen.insert(static_cast<int>(role)).second;
    });
}

bool edge_indices_are_unique(
    const std::vector<std::size_t>& edge_indices) {
    std::set<std::size_t> seen;
    return std::all_of(
        edge_indices.begin(), edge_indices.end(),
        [&seen](std::size_t edge_index) {
            return seen.insert(edge_index).second;
        });
}

bool same_edge_index_set(
    const std::vector<std::size_t>& lhs,
    const std::vector<std::size_t>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(
        lhs.begin(), lhs.end(), [&rhs](std::size_t edge_index) {
            return std::find(rhs.begin(), rhs.end(), edge_index) !=
                   rhs.end();
        });
}

bool same_role_set(
    const std::vector<PackageRole>& lhs,
    const std::vector<PackageRole>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(lhs.begin(), lhs.end(), [&rhs](PackageRole role) {
        return std::find(rhs.begin(), rhs.end(), role) != rhs.end();
    });
}

const SourceArtifactInstallObservedSelectedArtifact* find_observed_artifact(
    const std::vector<SourceArtifactInstallObservedSelectedArtifact>&
        artifacts,
    std::size_t artifact_index) {
    const auto found = std::find_if(
        artifacts.begin(), artifacts.end(),
        [artifact_index](const auto& artifact) {
            return artifact.artifact_index == artifact_index;
        });
    return found == artifacts.end() ? nullptr : &*found;
}

bool expected_identity_has_exact_version_and_architecture(
    const SourceAwarePackageIdentity& identity,
    std::vector<EvidenceIssue>& issues) {
    bool is_complete = true;
    if(identity.package_version().state() != PackageVersionState::Known ||
       identity.package_version().full_version() == nullptr) {
        add_issue(issues, EvidenceIssue::ExpectedVersionAuthorityIncomplete);
        is_complete = false;
    }
    if(identity.architecture().state() != PackageArchitectureState::Known ||
       identity.architecture().architectures().size() != 1) {
        add_issue(
            issues,
            EvidenceIssue::ExpectedArchitectureAuthorityIncomplete);
        is_complete = false;
    }
    return is_complete;
}

std::vector<std::string> expected_selected_package_names(
    const SourceArtifactInstallReceiptExpectation& expectation) {
    std::vector<std::string> package_names;
    package_names.reserve(expectation.selected_artifacts.size());
    for(const auto& artifact : expectation.selected_artifacts) {
        package_names.push_back(
            artifact.expected_identity.package().package_name());
    }
    return package_names;
}

bool requested_package_names_are_valid_and_unique(
    const std::vector<std::string>& package_names) {
    if(package_names.empty()) return false;
    std::set<std::string> seen;
    for(const std::string& package_name : package_names) {
        if(!is_valid_package_name(package_name) ||
           !seen.insert(package_name).second) {
            return false;
        }
    }
    return true;
}

bool is_invalid_issue(EvidenceIssue issue) noexcept {
    switch(issue) {
        case EvidenceIssue::TransactionMissing:
        case EvidenceIssue::CommandOutcomeNotSucceeded:
        case EvidenceIssue::ReceiptMissing:
        case EvidenceIssue::ReceiptIncomplete:
        case EvidenceIssue::SelectedArtifactNotInstalled:
            return false;
        default:
            return true;
    }
}

SourceArtifactInstallReceiptEvidenceCompleteness project_completeness(
    const std::vector<EvidenceIssue>& issues) noexcept {
    if(std::any_of(issues.begin(), issues.end(), is_invalid_issue)) {
        return SourceArtifactInstallReceiptEvidenceCompleteness::Invalid;
    }
    if(std::find(
           issues.begin(), issues.end(), EvidenceIssue::TransactionMissing) !=
           issues.end() ||
       std::find(issues.begin(), issues.end(), EvidenceIssue::ReceiptMissing) !=
           issues.end()) {
        return SourceArtifactInstallReceiptEvidenceCompleteness::Missing;
    }
    if(!issues.empty()) {
        return SourceArtifactInstallReceiptEvidenceCompleteness::Incomplete;
    }
    return SourceArtifactInstallReceiptEvidenceCompleteness::Complete;
}

} // namespace

SourceArtifactInstallReceiptObservation::
    SourceArtifactInstallReceiptObservation(
        SourceArtifactInstallWorkItemBinding work_item,
        std::vector<SourceArtifactInstallObservedSelectedArtifact>
            selected_artifacts,
        InvocationDependencyTransactionLedger transaction_ledger) noexcept
    : work_item_(std::move(work_item)),
      selected_artifacts_(std::move(selected_artifacts)),
      transaction_ledger_(std::move(transaction_ledger)) {
}

const SourceArtifactInstallWorkItemBinding&
SourceArtifactInstallReceiptObservation::work_item() const noexcept {
    return work_item_;
}

const std::vector<SourceArtifactInstallObservedSelectedArtifact>&
SourceArtifactInstallReceiptObservation::selected_artifacts()
    const noexcept {
    return selected_artifacts_;
}

const InvocationDependencyTransactionLedger&
SourceArtifactInstallReceiptObservation::transaction_ledger()
    const noexcept {
    return transaction_ledger_;
}

#ifdef MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_RECEIPT_TEST_HOOKS
SourceArtifactInstallReceiptObservation
make_source_artifact_install_receipt_observation_for_test(
    SourceArtifactInstallWorkItemBinding work_item,
    std::vector<SourceArtifactInstallObservedSelectedArtifact>
        selected_artifacts,
    InvocationDependencyTransactionLedger transaction_ledger) {
    return SourceArtifactInstallReceiptObservation(
        std::move(work_item), std::move(selected_artifacts),
        std::move(transaction_ledger));
}
#endif

SourceArtifactInstallReceiptEvidence::
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
            issues) noexcept
    : completeness_(completeness), expectation_(std::move(expectation)),
      observation_(std::move(observation)),
      selected_artifacts_(std::move(selected_artifacts)),
      command_outcome_(command_outcome), receipt_state_(receipt_state),
      actual_install_set_(std::move(actual_install_set)),
      issues_(std::move(issues)) {
}

SourceArtifactInstallReceiptEvidenceCompleteness
SourceArtifactInstallReceiptEvidence::completeness() const noexcept {
    return completeness_;
}

InvocationDependencyTransactionOwner
SourceArtifactInstallReceiptEvidence::owner() const noexcept {
    return InvocationDependencyTransactionOwner::SourceArtifactInstall;
}

const SourceArtifactInstallReceiptExpectation&
SourceArtifactInstallReceiptEvidence::expectation() const noexcept {
    return expectation_;
}

const SourceArtifactInstallReceiptObservation&
SourceArtifactInstallReceiptEvidence::observation() const noexcept {
    return observation_;
}

const SourceArtifactInstallWorkItemBinding&
SourceArtifactInstallReceiptEvidence::work_item() const noexcept {
    return expectation_.work_item;
}

const std::string& SourceArtifactInstallReceiptEvidence::transaction_token()
    const noexcept {
    return expectation_.transaction_token;
}

const std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>&
SourceArtifactInstallReceiptEvidence::selected_artifacts() const noexcept {
    return selected_artifacts_;
}

const InvocationDependencyTransactionLedger&
SourceArtifactInstallReceiptEvidence::transaction_ledger() const noexcept {
    return observation_.transaction_ledger();
}

const std::optional<InvocationDependencyTransactionCommandOutcome>&
SourceArtifactInstallReceiptEvidence::command_outcome() const noexcept {
    return command_outcome_;
}

const std::optional<PacmanTransactionReceiptState>&
SourceArtifactInstallReceiptEvidence::receipt_state() const noexcept {
    return receipt_state_;
}

const std::vector<std::string>&
SourceArtifactInstallReceiptEvidence::actual_install_set() const noexcept {
    return actual_install_set_;
}

const std::vector<SourceArtifactInstallReceiptEvidenceIssueKind>&
SourceArtifactInstallReceiptEvidence::issues() const noexcept {
    return issues_;
}

SourceArtifactInstallReceiptEvidence
establish_source_artifact_install_receipt_evidence(
    const SourceArtifactInstallReceiptExpectation& expectation,
    const SourceArtifactInstallReceiptObservation& observation) {
    std::vector<EvidenceIssue> issues;
    std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
        correlated_artifacts;

    const bool expected_work_item_valid =
        is_valid_package_name(expectation.work_item.package_base);
    const bool observed_work_item_valid =
        is_valid_package_name(observation.work_item().package_base);
    if(!expected_work_item_valid || !observed_work_item_valid) {
        add_issue(issues, EvidenceIssue::InvalidWorkItemPackageBase);
    }
    if(!roots_are_valid_and_unique(expectation.work_item.requested_roots) ||
       !roots_are_valid_and_unique(
           observation.work_item().requested_roots)) {
        add_issue(issues, EvidenceIssue::InvalidRequestedRootAttribution);
    }
    if(expectation.work_item.invocation !=
       observation.work_item().invocation) {
        add_issue(issues, EvidenceIssue::InvocationMismatch);
    }
    if(expectation.work_item.work_item_index !=
       observation.work_item().work_item_index) {
        add_issue(issues, EvidenceIssue::WorkItemIndexMismatch);
    }
    if(expectation.work_item.package_base !=
       observation.work_item().package_base) {
        add_issue(issues, EvidenceIssue::PackageBaseMismatch);
    }
    if(!same_root_set(
           expectation.work_item.requested_roots,
           observation.work_item().requested_roots)) {
        add_issue(issues, EvidenceIssue::RequestedRootAttributionMismatch);
    }
    if(!is_valid_pacman_transaction_token(expectation.transaction_token)) {
        add_issue(issues, EvidenceIssue::InvalidExpectedTransactionToken);
    }

    if(expectation.selected_artifacts.empty() ||
       observation.selected_artifacts().empty()) {
        add_issue(issues, EvidenceIssue::SelectedArtifactSetMissing);
    }
    if(expectation.selected_artifacts.size() !=
       observation.selected_artifacts().size()) {
        add_issue(issues, EvidenceIssue::SelectedArtifactSetMismatch);
    }

    std::set<std::size_t> expected_indices;
    std::set<std::string> expected_names;
    std::vector<RootTargetIdentity> attributed_roots;
    for(const auto& expected : expectation.selected_artifacts) {
        if(!expected_indices.insert(expected.artifact_index).second) {
            add_issue(issues, EvidenceIssue::DuplicateSelectedArtifactIndex);
        }
        const std::string& expected_name =
            expected.expected_identity.package().package_name();
        if(!expected_names.insert(expected_name).second) {
            add_issue(issues, EvidenceIssue::DuplicateSelectedPackageName);
        }
        if(expected.expected_identity.package()
               .package_base()
               .package_base() != expectation.work_item.package_base) {
            add_issue(issues, EvidenceIssue::ExpectedPackageBaseMismatch);
        }
        static_cast<void>(expected_identity_has_exact_version_and_architecture(
            expected.expected_identity, issues));
        if(expected.desired_reason != DesiredInstallReason::Dependency) {
            add_issue(issues, EvidenceIssue::DesiredInstallReasonNotDependency);
        }
        if(expected.dependency_roles.empty()) {
            add_issue(issues, EvidenceIssue::DependencyRoleMissing);
        }
        if(!roles_are_unique(expected.dependency_roles) ||
           !std::all_of(
               expected.dependency_roles.begin(),
               expected.dependency_roles.end(), is_build_or_check_role)) {
            add_issue(issues, EvidenceIssue::DependencyRoleNotBuildOrCheck);
        }
        if(!roots_are_valid_and_unique(expected.requested_roots) ||
           !root_set_is_subset(
               expected.requested_roots,
               expectation.work_item.requested_roots)) {
            add_issue(
                issues,
                EvidenceIssue::SelectedArtifactRootAttributionInvalid);
        }
        if(!edge_indices_are_unique(
               expected.build_plan_dependency_edge_indices)) {
            add_issue(
                issues,
                EvidenceIssue::InvalidBuildPlanDependencyEdgeAttribution);
        }
        for(const RootTargetIdentity& root : expected.requested_roots) {
            if(std::find(attributed_roots.begin(), attributed_roots.end(), root) ==
               attributed_roots.end()) {
                attributed_roots.push_back(root);
            }
        }

        const auto* observed = find_observed_artifact(
            observation.selected_artifacts(), expected.artifact_index);
        if(observed == nullptr) {
            add_issue(issues, EvidenceIssue::SelectedArtifactSetMismatch);
            continue;
        }
        if(observed->desired_reason != expected.desired_reason) {
            add_issue(issues, EvidenceIssue::DesiredInstallReasonMismatch);
        }
        if(!roles_are_unique(observed->dependency_roles) ||
           !same_role_set(
               expected.dependency_roles, observed->dependency_roles)) {
            add_issue(issues, EvidenceIssue::DependencyRoleMismatch);
        }
        if(!roots_are_valid_and_unique(observed->requested_roots) ||
           !same_root_set(
               expected.requested_roots, observed->requested_roots)) {
            add_issue(
                issues,
                EvidenceIssue::SelectedArtifactRootAttributionMismatch);
        }
        if(!edge_indices_are_unique(
               observed->build_plan_dependency_edge_indices) ||
           !same_edge_index_set(
               expected.build_plan_dependency_edge_indices,
               observed->build_plan_dependency_edge_indices)) {
            add_issue(
                issues,
                EvidenceIssue::BuildPlanDependencyEdgeAttributionMismatch);
        }

        const SourcePackageIdentityProjectionResult identity_projection =
            project_artifact_source_package_identity(
                expected.expected_identity, observed->archive_identity);
        if(!identity_projection.is_success()) {
            add_issue(issues, EvidenceIssue::ArchiveIdentityMismatch);
        }
        correlated_artifacts.push_back(
            SourceArtifactInstallCorrelatedSelectedArtifact{
                expected.artifact_index,
                expected.expected_identity,
                observed->archive_identity,
                expected.desired_reason,
                expected.dependency_roles,
                expected.requested_roots,
                expected.build_plan_dependency_edge_indices});
    }
    if(!same_root_set(
           attributed_roots, expectation.work_item.requested_roots)) {
        add_issue(
            issues,
            EvidenceIssue::SelectedArtifactRootAttributionInvalid);
    }

    std::set<std::size_t> observed_indices;
    std::set<std::string> observed_names;
    for(const auto& observed : observation.selected_artifacts()) {
        if(!observed_indices.insert(observed.artifact_index).second) {
            add_issue(issues, EvidenceIssue::DuplicateSelectedArtifactIndex);
        }
        if(!observed_names.insert(observed.archive_identity.package_name)
                .second) {
            add_issue(issues, EvidenceIssue::DuplicateSelectedPackageName);
        }
        if(expected_indices.find(observed.artifact_index) ==
           expected_indices.end()) {
            add_issue(issues, EvidenceIssue::SelectedArtifactSetMismatch);
        }
    }

    const InvocationDependencyTransactionLedger& observed_ledger =
        observation.transaction_ledger();
    const InvocationDependencyTransaction* transaction = nullptr;
    if(observed_ledger.transactions.empty()) {
        add_issue(issues, EvidenceIssue::TransactionMissing);
    } else if(observed_ledger.transactions.size() != 1) {
        add_issue(issues, EvidenceIssue::UnexpectedTransactionCount);
        std::set<std::string> observed_tokens;
        for(const auto& candidate : observed_ledger.transactions) {
            if(!observed_tokens.insert(candidate.transaction_token).second) {
                add_issue(issues, EvidenceIssue::DuplicateTransactionToken);
            }
        }
    } else {
        transaction = &observed_ledger.transactions.front();
    }

    std::optional<InvocationDependencyTransactionCommandOutcome>
        command_outcome;
    std::optional<PacmanTransactionReceiptState> receipt_state;
    std::vector<std::string> actual_install_set;
    if(transaction != nullptr) {
        command_outcome = transaction->command_outcome;
        receipt_state = transaction->receipt.state();
        for(const PacmanInstalledPackageReceipt& installed :
            transaction->receipt.newly_installed_packages()) {
            actual_install_set.push_back(installed.package_name);
        }

        if(transaction->transaction_token != expectation.transaction_token ||
           !is_valid_pacman_transaction_token(
               transaction->transaction_token)) {
            add_issue(issues, EvidenceIssue::TransactionTokenMismatch);
        }
        if(transaction->owner !=
           InvocationDependencyTransactionOwner::SourceArtifactInstall) {
            add_issue(issues, EvidenceIssue::TransactionOwnerMismatch);
        }

        const std::vector<std::string> expected_package_names =
            expected_selected_package_names(expectation);
        if(!requested_package_names_are_valid_and_unique(
               transaction->requested_package_names) ||
           transaction->requested_package_names != expected_package_names) {
            add_issue(issues, EvidenceIssue::RequestedPackageSetMismatch);
        }
        if(transaction->command_outcome !=
           InvocationDependencyTransactionCommandOutcome::Succeeded) {
            add_issue(issues, EvidenceIssue::CommandOutcomeNotSucceeded);
        }

        switch(transaction->receipt.state()) {
            case PacmanTransactionReceiptState::Unavailable:
                add_issue(issues, EvidenceIssue::ReceiptMissing);
                break;
            case PacmanTransactionReceiptState::Incomplete:
                add_issue(issues, EvidenceIssue::ReceiptIncomplete);
                break;
            case PacmanTransactionReceiptState::Invalid:
                add_issue(issues, EvidenceIssue::ReceiptInvalid);
                break;
            case PacmanTransactionReceiptState::Complete:
                if(!transaction->receipt.is_complete_for(
                       expectation.transaction_token,
                       InvocationDependencyTransactionOwner::
                           SourceArtifactInstall)) {
                    add_issue(issues, EvidenceIssue::ReceiptInvalid);
                }
                break;
        }

        for(const auto& expected : expectation.selected_artifacts) {
            if(!transaction->receipt.contains_newly_installed_package(
                   expected.expected_identity.package().package_name())) {
                add_issue(
                    issues,
                    EvidenceIssue::SelectedArtifactNotInstalled);
            }
        }
    }

    canonicalize_issues(issues);
    const auto completeness = project_completeness(issues);
    return SourceArtifactInstallReceiptEvidence(
        completeness, expectation, observation,
        std::move(correlated_artifacts), command_outcome, receipt_state,
        std::move(actual_install_set), std::move(issues));
}

SourceArtifactInstallCausalEvidence::SourceArtifactInstallCausalEvidence(
    SourceArtifactInstallWorkItemBinding work_item,
    std::string transaction_token,
    std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>
        selected_artifacts,
    std::vector<std::string> actual_install_set) noexcept
    : work_item_(std::move(work_item)),
      transaction_token_(std::move(transaction_token)),
      selected_artifacts_(std::move(selected_artifacts)),
      actual_install_set_(std::move(actual_install_set)) {
}

InvocationDependencyTransactionOwner
SourceArtifactInstallCausalEvidence::owner() const noexcept {
    return InvocationDependencyTransactionOwner::SourceArtifactInstall;
}

const SourceArtifactInstallWorkItemBinding&
SourceArtifactInstallCausalEvidence::work_item() const noexcept {
    return work_item_;
}

const std::string& SourceArtifactInstallCausalEvidence::transaction_token()
    const noexcept {
    return transaction_token_;
}

const std::vector<SourceArtifactInstallCorrelatedSelectedArtifact>&
SourceArtifactInstallCausalEvidence::selected_artifacts() const noexcept {
    return selected_artifacts_;
}

const std::vector<std::string>&
SourceArtifactInstallCausalEvidence::actual_install_set() const noexcept {
    return actual_install_set_;
}

std::optional<SourceArtifactInstallCausalEvidence>
project_source_artifact_install_causal_evidence(
    const SourceArtifactInstallReceiptEvidence& evidence) {
    if(evidence.completeness() !=
           SourceArtifactInstallReceiptEvidenceCompleteness::Complete ||
       !evidence.issues().empty() || !evidence.command_outcome().has_value() ||
       evidence.command_outcome().value() !=
           InvocationDependencyTransactionCommandOutcome::Succeeded ||
       !evidence.receipt_state().has_value() ||
       evidence.receipt_state().value() !=
           PacmanTransactionReceiptState::Complete) {
        return std::nullopt;
    }

    return SourceArtifactInstallCausalEvidence(
        evidence.work_item(), evidence.transaction_token(),
        evidence.selected_artifacts(), evidence.actual_install_set());
}
