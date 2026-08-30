#include "source_artifact_install_receipt_evidence.hpp"

#include "invocation_owned_cleanup_adapter.hpp"
#include "trusted_alpm_receipt_transport.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(
    !std::is_constructible_v<
        SourceArtifactInstallReceiptObservation,
        InvocationDependencyTransactionLedger>);
static_assert(
    !std::is_constructible_v<
        SourceArtifactInstallReceiptEvidence,
        InvocationDependencyTransactionLedger>);
static_assert(
    !std::is_convertible_v<
        InvocationDependencyTransactionLedger,
        SourceArtifactInstallReceiptEvidence>);
static_assert(
    !std::is_constructible_v<
        SourceArtifactInstallCausalEvidence,
        InvocationDependencyTransactionLedger>);
static_assert(
    !std::is_convertible_v<
        TrustedAlpmReceiptCaptureResult,
        SourceArtifactInstallReceiptEvidence>);
static_assert(
    !std::is_convertible_v<
        TrustedAlpmReceiptCaptureResult,
        SourceArtifactInstallCausalEvidence>);
static_assert(
    !std::is_convertible_v<
        SourceArtifactInstallReceiptEvidence,
        TrustedAlpmReceiptCaptureResult>);
static_assert(
    !std::is_convertible_v<
        SourceArtifactInstallCausalEvidence,
        TrustedAlpmReceiptCaptureResult>);
static_assert(
    !std::is_constructible_v<
        SelectedRepositoryProviderTrustedExecutionEvidence,
        SourceArtifactInstallCausalEvidence>);
static_assert(
    !std::is_convertible_v<
        SourceArtifactInstallCausalEvidence,
        SelectedRepositoryProviderTrustedExecutionEvidence>);
static_assert(
    !std::is_constructible_v<
        SelectedRepositoryProviderTrustedExecutionEvidence,
        InvocationDependencyTransactionLedger>);
static_assert(
    !std::is_convertible_v<
        InvocationDependencyTransactionLedger,
        SelectedRepositoryProviderTrustedExecutionEvidence>);
static_assert(
    !std::is_constructible_v<
        SelectedRepositoryProviderTrustedExecutionEvidence,
        std::string>);
static_assert(
    !std::is_convertible_v<
        std::string,
        SelectedRepositoryProviderTrustedExecutionEvidence>);

using CleanupCandidateProjectionFunction = decltype(&project_invocation_owned_cleanup_candidate);
static_assert(
    !std::is_invocable_v<
        CleanupCandidateProjectionFunction,
        const InstalledPackageStateSnapshotResult&,
        const InstalledPackageStateSnapshotResult&,
        const BuildPlan&,
        const ResolvedDependencyCandidate&,
        const CleanupInvocationLifecycleEvidence&,
        const std::vector<SelectedRepositoryProviderTransactionResult>&,
        const InvocationDependencyTransactionLedger&>);

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

std::string token(char character = 'a') {
    return std::string(64, character);
}

RootTargetIdentity root(std::size_t index, std::string name) {
    return RootTargetIdentity{index, std::move(name)};
}

CleanupInvocationAuthority invocation_authority() {
    CleanupInvocationSession session = CleanupInvocationSession::begin(
        PreparedRemoteSourceBuild{
            ResolvedSourceBuildIdentity{ResolvedAurSourceBuildIdentity{
                "receipt-fixture", "receipt-fixture"}},
            BuildPlan{},
            PreparedProductionSourceBuildInvocation{}});
    return session.authority();
}

SourceAwarePackageIdentity expected_identity(
    const std::string& package_name,
    const std::string& full_version,
    const std::string& package_base = "foo",
    PackageVersionIdentity version =
        PackageVersionIdentity::composite("1.0-1"),
    PackageArchitectureIdentity architecture =
        PackageArchitectureIdentity::known({"x86_64"})) {
    if(version.state() == PackageVersionState::Known) {
        version = PackageVersionIdentity::composite(full_version);
    }
    return SourceAwarePackageIdentity::make(
        PackageChildIdentity::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(
                        "https://aur.archlinux.org/foo.git")),
                package_base),
            package_name),
        SourceRevisionIdentity::unknown(), std::move(version),
        std::move(architecture));
}

ArtifactPackageIdentity actual_identity(
    std::string package_name,
    std::string full_version,
    ArtifactPackageBaseIdentity package_base =
        ArtifactPackageBaseIdentity::known("foo"),
    ArtifactPackageArchitectureIdentity architecture =
        ArtifactPackageArchitectureIdentity::known("x86_64")) {
    return ArtifactPackageIdentity{
        std::move(package_name), std::move(full_version),
        std::move(package_base), std::move(architecture)};
}

PacmanTransactionReceipt receipt(
    const std::string& transaction_token,
    InvocationDependencyTransactionOwner owner,
    std::vector<PacmanTransactionPackageObservation> operations,
    PacmanTransactionReceiptObservationState state =
        PacmanTransactionReceiptObservationState::Complete) {
    return validate_pacman_transaction_receipt(
        transaction_token, owner,
        PacmanTransactionReceiptObservation{
            state,
            state == PacmanTransactionReceiptObservationState::Missing
                ? std::nullopt
                : std::optional<std::string>{transaction_token},
            state == PacmanTransactionReceiptObservationState::Missing
                ? std::nullopt
                : std::optional<InvocationDependencyTransactionOwner>{owner},
            std::move(operations)});
}

InvocationDependencyTransaction transaction(
    std::string transaction_token,
    InvocationDependencyTransactionOwner owner,
    std::vector<std::string> requested_names,
    InvocationDependencyTransactionCommandOutcome outcome,
    PacmanTransactionReceipt transaction_receipt) {
    return InvocationDependencyTransaction{
        std::move(transaction_token), owner, std::move(requested_names),
        outcome, std::move(transaction_receipt)};
}

struct ScenarioInputs {
    SourceArtifactInstallReceiptExpectation expectation;
    SourceArtifactInstallWorkItemBinding observed_work_item;
    std::vector<SourceArtifactInstallObservedSelectedArtifact>
        observed_selected_artifacts;
    InvocationDependencyTransactionLedger ledger;

    SourceArtifactInstallReceiptEvidence evaluate() const {
        SourceArtifactInstallReceiptObservation observation =
            make_source_artifact_install_receipt_observation_for_test(
                observed_work_item, observed_selected_artifacts, ledger);
        return establish_source_artifact_install_receipt_evidence(
            expectation, observation);
    }
};

ScenarioInputs positive_inputs() {
    const std::vector<RootTargetIdentity> roots = {
        root(0, "root-a"), root(1, "root-b")};
    SourceArtifactInstallWorkItemBinding binding{
        std::nullopt, 3, "foo", roots};

    std::vector<SourceArtifactInstallExpectedSelectedArtifact> expected = {
        SourceArtifactInstallExpectedSelectedArtifact{
            1, expected_identity("foo-libs", "1.0-1"), DesiredInstallReason::Dependency, {PackageRole::BuildDependency}, {roots[0]}},
        SourceArtifactInstallExpectedSelectedArtifact{
            4, expected_identity("foo-docs", "1.0-1"), DesiredInstallReason::Dependency, {PackageRole::CheckDependency}, {roots[1]}},
    };
    std::vector<SourceArtifactInstallObservedSelectedArtifact> observed = {
        SourceArtifactInstallObservedSelectedArtifact{
            1, actual_identity("foo-libs", "1.0-1"), DesiredInstallReason::Dependency, {PackageRole::BuildDependency}, {roots[0]}},
        SourceArtifactInstallObservedSelectedArtifact{
            4, actual_identity("foo-docs", "1.0-1"), DesiredInstallReason::Dependency, {PackageRole::CheckDependency}, {roots[1]}},
    };

    const std::string transaction_token = token();
    InvocationDependencyTransactionLedger ledger{{transaction(
        transaction_token,
        InvocationDependencyTransactionOwner::SourceArtifactInstall,
        {"foo-libs", "foo-docs"},
        InvocationDependencyTransactionCommandOutcome::Succeeded,
        receipt(
            transaction_token,
            InvocationDependencyTransactionOwner::SourceArtifactInstall,
            {{PacmanTransactionPackageOperation::Install, "foo-libs"},
             {PacmanTransactionPackageOperation::Install, "foo-docs"},
             {PacmanTransactionPackageOperation::Install,
              "solver-introduced-tool"}}))}};

    return ScenarioInputs{
        SourceArtifactInstallReceiptExpectation{
            binding, std::move(expected), transaction_token},
        binding, std::move(observed), std::move(ledger)};
}

bool has_issue(
    const SourceArtifactInstallReceiptEvidence& evidence,
    SourceArtifactInstallReceiptEvidenceIssueKind issue) {
    return std::find(evidence.issues().begin(), evidence.issues().end(), issue) !=
           evidence.issues().end();
}

void expect_no_causal_evidence(
    const ScenarioInputs& inputs,
    const std::string& context,
    std::optional<SourceArtifactInstallReceiptEvidenceIssueKind>
        expected_issue = std::nullopt) {
    const SourceArtifactInstallReceiptEvidence evidence = inputs.evaluate();
    expect(
        !project_source_artifact_install_causal_evidence(evidence).has_value(),
        context + " produced causal evidence");
    if(expected_issue.has_value()) {
        expect(
            has_issue(evidence, expected_issue.value()),
            context + " did not retain the expected issue");
    }
}

void test_complete_source_artifact_install_evidence() {
    const SourceArtifactInstallReceiptEvidence evidence =
        positive_inputs().evaluate();
    const auto causal =
        project_source_artifact_install_causal_evidence(evidence);
    expect(
        evidence.completeness() ==
                SourceArtifactInstallReceiptEvidenceCompleteness::Complete &&
            evidence.owner() ==
                InvocationDependencyTransactionOwner::SourceArtifactInstall &&
            evidence.command_outcome() ==
                InvocationDependencyTransactionCommandOutcome::Succeeded &&
            evidence.receipt_state() ==
                PacmanTransactionReceiptState::Complete &&
            evidence.selected_artifacts().size() == 2 &&
            evidence.actual_install_set().size() == 3 &&
            causal.has_value(),
        "complete source-artifact evidence was not closed");
    expect(
        causal->owner() ==
                InvocationDependencyTransactionOwner::SourceArtifactInstall &&
            causal->work_item().work_item_index == 3 &&
            causal->work_item().package_base == "foo" &&
            causal->selected_artifacts().size() == 2 &&
            causal->actual_install_set().back() ==
                "solver-introduced-tool",
        "causal evidence lost its exact binding or actual Install set");
    expect(
        std::none_of(
            causal->selected_artifacts().begin(),
            causal->selected_artifacts().end(), [](const auto& selected) {
                return selected.archive_identity.package_name ==
                       "solver-introduced-tool";
            }),
        "an unselected actual Install was promoted into the selected set");
}

void test_invocation_work_item_and_selection_mismatch_matrix() {
    ScenarioInputs missing_authority = positive_inputs();
    const CleanupInvocationAuthority typed_authority =
        invocation_authority();
    missing_authority.observed_work_item.invocation_authority =
        typed_authority;
    const SourceArtifactInstallReceiptEvidence separated_evidence =
        missing_authority.evaluate();
    expect(
        !separated_evidence.expectation()
                .work_item.invocation_authority.has_value() &&
            separated_evidence.observation()
                    .work_item()
                    .invocation_authority == typed_authority,
        "missing and typed invocation authority were not kept separate");
    expect_no_causal_evidence(
        missing_authority, "missing invocation authority relabel",
        SourceArtifactInstallReceiptEvidenceIssueKind::InvocationMismatch);

    ScenarioInputs stale_authority = positive_inputs();
    stale_authority.expectation.work_item.invocation_authority =
        invocation_authority();
    stale_authority.observed_work_item.invocation_authority =
        invocation_authority();
    expect_no_causal_evidence(
        stale_authority, "same-value distinct invocation authority",
        SourceArtifactInstallReceiptEvidenceIssueKind::InvocationMismatch);

    ScenarioInputs wrong_work_item = positive_inputs();
    wrong_work_item.observed_work_item.work_item_index = 4;
    expect_no_causal_evidence(
        wrong_work_item, "wrong work item",
        SourceArtifactInstallReceiptEvidenceIssueKind::WorkItemIndexMismatch);

    ScenarioInputs wrong_base = positive_inputs();
    wrong_base.observed_work_item.package_base = "other-base";
    expect_no_causal_evidence(
        wrong_base, "wrong work-item PackageBase",
        SourceArtifactInstallReceiptEvidenceIssueKind::PackageBaseMismatch);

    ScenarioInputs wrong_root = positive_inputs();
    wrong_root.observed_work_item.requested_roots[0] = root(0, "other-root");
    expect_no_causal_evidence(
        wrong_root, "same child under a different root",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            RequestedRootAttributionMismatch);

    ScenarioInputs missing_selected = positive_inputs();
    missing_selected.observed_selected_artifacts.pop_back();
    expect_no_causal_evidence(
        missing_selected, "wrong selected artifact set",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            SelectedArtifactSetMismatch);

    ScenarioInputs sibling_collision = positive_inputs();
    sibling_collision.observed_selected_artifacts[0].archive_identity =
        actual_identity("foo", "1.0-1");
    expect_no_causal_evidence(
        sibling_collision, "split sibling identity collision",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            ArchiveIdentityMismatch);
}

void test_identity_mismatch_and_unknown_matrix() {
    const auto expect_archive_failure = [](ArtifactPackageIdentity identity,
                                           const std::string& context) {
        ScenarioInputs inputs = positive_inputs();
        inputs.observed_selected_artifacts[0].archive_identity =
            std::move(identity);
        expect_no_causal_evidence(
            inputs, context,
            SourceArtifactInstallReceiptEvidenceIssueKind::
                ArchiveIdentityMismatch);
    };

    expect_archive_failure(
        actual_identity("other-child", "1.0-1"), "package name mismatch");
    expect_archive_failure(
        actual_identity("foo-libs", "2.0-1"), "version mismatch");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::known("other-base")),
        "actual PackageBase mismatch");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::missing()),
        "actual PackageBase missing");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::malformed()),
        "actual PackageBase malformed");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::unavailable()),
        "actual PackageBase unavailable");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::known("foo"),
            ArtifactPackageArchitectureIdentity::known("armv7h")),
        "architecture mismatch");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::known("foo"),
            ArtifactPackageArchitectureIdentity::missing()),
        "actual architecture missing");
    expect_archive_failure(
        actual_identity(
            "foo-libs", "1.0-1",
            ArtifactPackageBaseIdentity::known("foo"),
            ArtifactPackageArchitectureIdentity::unavailable()),
        "actual architecture unavailable");

    ScenarioInputs unknown_expected_version = positive_inputs();
    unknown_expected_version.expectation.selected_artifacts[0]
        .expected_identity = expected_identity(
        "foo-libs", "1.0-1", "foo", PackageVersionIdentity::unknown());
    expect_no_causal_evidence(
        unknown_expected_version, "unknown expected version",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            ExpectedVersionAuthorityIncomplete);

    ScenarioInputs unknown_expected_arch = positive_inputs();
    unknown_expected_arch.expectation.selected_artifacts[0]
        .expected_identity = expected_identity(
        "foo-libs", "1.0-1", "foo",
        PackageVersionIdentity::composite("1.0-1"),
        PackageArchitectureIdentity::unknown());
    expect_no_causal_evidence(
        unknown_expected_arch, "unknown expected architecture",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            ExpectedArchitectureAuthorityIncomplete);
}

void test_exact_any_and_known_other_architectures() {
    for(const char* architecture : {"any", "armv7h"}) {
        ScenarioInputs inputs = positive_inputs();
        inputs.expectation.selected_artifacts[0].expected_identity =
            expected_identity(
                "foo-libs", "1.0-1", "foo",
                PackageVersionIdentity::composite("1.0-1"),
                PackageArchitectureIdentity::known({architecture}));
        inputs.observed_selected_artifacts[0].archive_identity =
            actual_identity(
                "foo-libs", "1.0-1",
                ArtifactPackageBaseIdentity::known("foo"),
                ArtifactPackageArchitectureIdentity::known(architecture));
        const auto evidence = inputs.evaluate();
        expect(
            project_source_artifact_install_causal_evidence(evidence)
                .has_value(),
            std::string("exact known architecture was not retained: ") +
                architecture);
    }
}

void test_role_and_reason_matrix() {
    ScenarioInputs explicit_reason = positive_inputs();
    explicit_reason.expectation.selected_artifacts[0].desired_reason =
        DesiredInstallReason::Explicit;
    explicit_reason.observed_selected_artifacts[0].desired_reason =
        DesiredInstallReason::Explicit;
    expect_no_causal_evidence(
        explicit_reason, "Explicit selected artifact",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            DesiredInstallReasonNotDependency);

    for(const PackageRole role :
        {PackageRole::Root, PackageRole::RuntimeDependency}) {
        ScenarioInputs wrong_role = positive_inputs();
        wrong_role.expectation.selected_artifacts[0].dependency_roles = {role};
        wrong_role.observed_selected_artifacts[0].dependency_roles = {role};
        expect_no_causal_evidence(
            wrong_role, "non-build/check dependency role",
            SourceArtifactInstallReceiptEvidenceIssueKind::
                DependencyRoleNotBuildOrCheck);
    }
}

void test_token_owner_and_receipt_matrix() {
    ScenarioInputs missing_transaction = positive_inputs();
    missing_transaction.ledger.transactions.clear();
    expect_no_causal_evidence(
        missing_transaction, "missing transaction",
        SourceArtifactInstallReceiptEvidenceIssueKind::TransactionMissing);

    ScenarioInputs wrong_token = positive_inputs();
    const std::string other_token = token('b');
    wrong_token.ledger.transactions.front() = transaction(
        other_token,
        InvocationDependencyTransactionOwner::SourceArtifactInstall,
        {"foo-libs", "foo-docs"},
        InvocationDependencyTransactionCommandOutcome::Succeeded,
        receipt(
            other_token,
            InvocationDependencyTransactionOwner::SourceArtifactInstall,
            {{PacmanTransactionPackageOperation::Install, "foo-libs"},
             {PacmanTransactionPackageOperation::Install, "foo-docs"}}));
    expect_no_causal_evidence(
        wrong_token, "wrong transaction token",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            TransactionTokenMismatch);

    ScenarioInputs missing_token = positive_inputs();
    missing_token.ledger.transactions.front().transaction_token.clear();
    expect_no_causal_evidence(
        missing_token, "missing transaction token",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            TransactionTokenMismatch);

    ScenarioInputs duplicate_token = positive_inputs();
    duplicate_token.ledger.transactions.push_back(
        duplicate_token.ledger.transactions.front());
    expect_no_causal_evidence(
        duplicate_token, "duplicate transaction token",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            DuplicateTransactionToken);

    ScenarioInputs wrong_owner = positive_inputs();
    wrong_owner.ledger.transactions.front() = transaction(
        token(),
        InvocationDependencyTransactionOwner::SelectedRepositoryProvider,
        {"foo-libs", "foo-docs"},
        InvocationDependencyTransactionCommandOutcome::Succeeded,
        receipt(
            token(),
            InvocationDependencyTransactionOwner::SelectedRepositoryProvider,
            {{PacmanTransactionPackageOperation::Install, "foo-libs"},
             {PacmanTransactionPackageOperation::Install, "foo-docs"}}));
    expect_no_causal_evidence(
        wrong_owner, "selected-provider owner isolation",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            TransactionOwnerMismatch);

    ScenarioInputs wrong_requested_set = positive_inputs();
    wrong_requested_set.ledger.transactions.front().requested_package_names =
        {"foo-libs"};
    expect_no_causal_evidence(
        wrong_requested_set, "wrong requested selected package set",
        SourceArtifactInstallReceiptEvidenceIssueKind::
            RequestedPackageSetMismatch);

    ScenarioInputs missing_receipt = positive_inputs();
    missing_receipt.ledger.transactions.front().receipt = receipt(
        token(), InvocationDependencyTransactionOwner::SourceArtifactInstall,
        {}, PacmanTransactionReceiptObservationState::Missing);
    expect_no_causal_evidence(
        missing_receipt, "missing receipt",
        SourceArtifactInstallReceiptEvidenceIssueKind::ReceiptMissing);

    ScenarioInputs incomplete_receipt = positive_inputs();
    incomplete_receipt.ledger.transactions.front().receipt = receipt(
        token(), InvocationDependencyTransactionOwner::SourceArtifactInstall,
        {{PacmanTransactionPackageOperation::Install, "foo-libs"},
         {PacmanTransactionPackageOperation::Install, "foo-docs"}},
        PacmanTransactionReceiptObservationState::Incomplete);
    expect_no_causal_evidence(
        incomplete_receipt, "incomplete receipt",
        SourceArtifactInstallReceiptEvidenceIssueKind::ReceiptIncomplete);
}

void test_command_and_non_install_operation_matrix() {
    for(const auto outcome :
        {InvocationDependencyTransactionCommandOutcome::Failed,
         InvocationDependencyTransactionCommandOutcome::Unknown,
         InvocationDependencyTransactionCommandOutcome::NotAttempted}) {
        ScenarioInputs inputs = positive_inputs();
        inputs.ledger.transactions.front().command_outcome = outcome;
        expect_no_causal_evidence(
            inputs, "non-success command outcome",
            SourceArtifactInstallReceiptEvidenceIssueKind::
                CommandOutcomeNotSucceeded);
    }

    for(const char* context :
        {"Upgrade", "same-version reinstall", "downgrade"}) {
        ScenarioInputs inputs = positive_inputs();
        inputs.ledger.transactions.front().receipt = receipt(
            token(),
            InvocationDependencyTransactionOwner::SourceArtifactInstall,
            {{PacmanTransactionPackageOperation::Upgrade, "foo-libs"},
             {PacmanTransactionPackageOperation::Install, "foo-docs"}});
        expect_no_causal_evidence(
            inputs, context,
            SourceArtifactInstallReceiptEvidenceIssueKind::
                SelectedArtifactNotInstalled);
    }

    for(const char* context : {"SkippedAsNeeded", "reason-only transition"}) {
        ScenarioInputs inputs = positive_inputs();
        inputs.ledger.transactions.front().receipt = receipt(
            token(),
            InvocationDependencyTransactionOwner::SourceArtifactInstall,
            {{PacmanTransactionPackageOperation::Install,
              "solver-introduced-tool"}});
        expect_no_causal_evidence(
            inputs, context,
            SourceArtifactInstallReceiptEvidenceIssueKind::
                SelectedArtifactNotInstalled);
    }
}

} // namespace

void run_source_artifact_install_receipt_evidence_tests() {
    test_complete_source_artifact_install_evidence();
    test_invocation_work_item_and_selection_mismatch_matrix();
    test_identity_mismatch_and_unknown_matrix();
    test_exact_any_and_known_other_architectures();
    test_role_and_reason_matrix();
    test_token_owner_and_receipt_matrix();
    test_command_and_non_install_operation_matrix();
}
