#include "cli_authority.hpp"
#include "diagnostic_projection.hpp"
#include "operation_state_model.hpp"
#include "presentation_projection.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void test_rich_cli_operation_contract() {
    using namespace cli_authority;

    expect(rich_cli_metadata_is_index_aligned(),
           "Rich CLI metadata is not index-aligned");

    const OperationMetadata& build =
            operation_metadata(OperationId::Build);
    expect(
            operation_spec(OperationId::Build).token == "build" &&
                    build.canonical_token == "build" &&
                    build.aliases.count == 0 &&
                    build.owner == GrammarOwnership::MoguetOwned &&
                    build.dry_run_support == DryRunSupport::Supported &&
                    build.form_count == 2,
            "Build operation metadata differs");

    const OperationFormSpec& remote = operation_form(build, 0);
    const OperationFormSpec& local = operation_form(build, 1);
    expect(
            remote.operands.term_count == 2 &&
                    remote.operands.terms[0].kind == OperandKind::Package &&
                    remote.operands.terms[0].min_count == 1 &&
                    remote.operands.terms[0].max_count == 1 &&
                    remote.operands.terms[1].kind ==
                            OperandKind::EnvironmentAssignment &&
                    remote.operands.terms[1].max_count ==
                            UNBOUNDED_OPERAND_COUNT &&
                    remote.operands.ordering == OperandOrderingRule::
                                                        PrimaryThenEnvironmentAssignments,
            "Remote build operand/cardinality contract differs");
    expect(
            local.operands.terms[0].kind == OperandKind::Directory &&
                    local.target_policy == TargetPolicy::ExactlyOne &&
                    local.option_relations.contains(OptionId::LocalSource) &&
                    local.option_relations.contains(OptionId::NoConfirm) &&
                    !local.option_relations.contains(OptionId::Diff) &&
                    !local.option_relations.contains(OptionId::RmDeps) &&
                    remote.option_relations.contains(OptionId::Diff) &&
                    !remote.option_relations.contains(OptionId::RmDeps),
            "Local build structured relation is missing");

    const OperationMetadata& deps = operation_metadata(OperationId::Deps);
    const OperationFormSpec& deps_form = operation_form(deps, 0);
    expect(
                    deps.semantic_scope ==
                            OperationSemanticScope::DependencyInspection &&
                    deps.dry_run_support == DryRunSupport::Unsupported &&
                    deps_form.operands.terms[0].min_count == 1 &&
                    deps_form.operands.terms[0].max_count ==
                            UNBOUNDED_OPERAND_COUNT &&
                    deps_form.target_policy == TargetPolicy::OneOrMore &&
                    deps_form.option_relations.contains(OptionId::Recursive),
            "deps canonical repeatable operand contract differs");

    const OperationFormSpec& add_source = operation_form(
            operation_metadata(OperationId::AddSource), 0);
    expect(
            add_source.operands.terms[0].kind ==
                            OperandKind::SourcePreferenceItem &&
                    add_source.operands.ordering == OperandOrderingRule::
                            PackageIntroducesFollowingAssignmentScope,
            "add-src ordered grammar contract differs");

    for(OperationId id : {OperationId::Upgrade, OperationId::UpgradeAur,
                          OperationId::UpgradeAll, OperationId::Clean,
                          OperationId::ListSources}) {
        const OperationFormSpec& form = operation_form(
                operation_metadata(id), 0);
        expect(
                form.operands.term_count == 0 &&
                        form.target_policy == TargetPolicy::None,
                "Targetless operation has nonzero cardinality");
    }
}

void test_rich_cli_option_and_ownership_contract() {
    using namespace cli_authority;

    for(std::size_t index = 0; index < MOGUET_GLOBAL_OPTIONS.size(); ++index) {
        const GlobalOptionSpec& legacy = MOGUET_GLOBAL_OPTIONS[index];
        const OptionContract& rich = option_contract(option_id(legacy.id));
        expect(
                static_cast<std::size_t>(legacy.id) == index &&
                        static_cast<std::size_t>(rich.id) == index &&
                        rich.canonical_token == legacy.token,
                "Existing global option ID/token order drifted");
    }

    const OptionContract& recursive = option_contract(OptionId::Recursive);
    expect(
            recursive.canonical_token == "--recursive" &&
                    recursive.default_occurrence ==
                            OptionOccurrence::RepeatIdempotent &&
                    recursive.lexical_placement ==
                            OptionLexicalPlacement::OperationLocal &&
                    has_option_scope(
                            recursive.semantic_scopes,
                            OptionSemanticScope::DependencyInspection),
            "--recursive occurrence/scope contract differs");

    expect(
            option_contract(OptionId::Aur).conflicts.contains(OptionId::Repo) &&
                    option_contract(OptionId::Repo).conflicts.contains(
                            OptionId::Aur) &&
                    option_contract(OptionId::Aur).conflicts.rule ==
                            OptionConflictRule::FinalValueMustAgree,
            "Source-selector conflict contract is missing");
    const OptionConflictSet& build_mode_conflicts =
            option_contract(OptionId::BuildMode).conflicts;
    expect(
            option_contract(OptionId::BuildMode).default_occurrence ==
                            OptionOccurrence::RepeatSameValue &&
                    build_mode_conflicts.rule ==
                            OptionConflictRule::FinalValueMustAgree &&
                    build_mode_conflicts.value_identity == "build.mode" &&
                    build_mode_conflicts.contains(OptionId::Rebuild) &&
                    build_mode_conflicts.contains(OptionId::CleanBuild),
            "Build-mode final-value conflict contract differs");
    expect(
            option_contract(OptionId::Help).aliases.contains(
                    HELP_SHORT_OPTION) &&
                    option_contract(OptionId::Version).aliases.contains(
                            VERSION_SHORT_OPTION),
            "Help/version aliases are not structured");
    expect(
                    option_contract(OptionId::Needed).owner ==
                            GrammarOwnership::InterceptedPacman &&
                    option_contract(OptionId::EndOfOptions).owner ==
                            GrammarOwnership::InterceptedPacman &&
                    option_contract(OptionId::EndOfOptions).value.kind ==
                            OptionValueKind::Marker &&
                    find_option_contract("--") ==
                            &option_contract(OptionId::EndOfOptions),
            "Pacman ownership or -- marker contract differs");

    const SpecialOperationSpec& export_operation =
            special_operation_spec(SpecialOperationId::PkgbuildExport);
    const SpecialOperationSpec& print_operation =
            special_operation_spec(SpecialOperationId::PkgbuildPrint);
    const SpecialOperationSpec& select_operation =
            special_operation_spec(SpecialOperationId::SyncSelect);
    const SpecialOperationSpec& delegated =
            special_operation_spec(
                    SpecialOperationId::DelegatedPacmanGrammar);
    expect(
            export_operation.canonical_token == "-G" &&
                    print_operation.canonical_token == "-Gp" &&
                    find_special_operation("-G") == &export_operation &&
                    find_special_operation("-Gp") == &print_operation &&
                    export_operation.operands.terms[0].min_count == 1 &&
                    export_operation.operands.terms[0].max_count == 1 &&
                    export_operation.owner ==
                            GrammarOwnership::MoguetOwned &&
                    print_operation.owner ==
                            GrammarOwnership::MoguetOwned &&
                    export_operation.dry_run_support ==
                            DryRunSupport::Unsupported,
            "-G/-Gp structured authority differs");

    const std::array<OptionId, 1> select_context = {OptionId::Select};
    const std::array<OptionId, 2> select_needed_context = {
            OptionId::Select, OptionId::Needed};
    expect(
            find_special_operation("-S") == nullptr &&
                    find_special_operation("-S", select_context) ==
                            &select_operation &&
                    find_special_operation("-S", select_needed_context) ==
                            &select_operation,
            "Plain -S was collapsed into the closed -S --select form");

    const OptionRelationContract* select_relation =
            select_operation.option_relations.find(OptionId::Select);
    const OptionRelationContract* select_needed =
            select_operation.option_relations.find(OptionId::Needed);
    const OptionRelationContract* select_no_confirm =
            select_operation.option_relations.find(OptionId::NoConfirm);
    expect(
            select_relation != nullptr &&
                    select_relation->requirement ==
                            OptionRelationRequirement::Required &&
                    select_needed != nullptr &&
                    select_needed->requirement ==
                            OptionRelationRequirement::Optional &&
                    select_needed->occurrence ==
                            OptionOccurrence::RepeatIdempotent &&
                    select_needed->forwarding_occurrence ==
                            OptionForwardingOccurrence::ConsolidateSingle &&
                    has_option_effect(
                            select_needed->semantic_effects,
                            OptionSemanticEffect::UpstreamArgument) &&
                    has_option_effect(
                            select_needed->semantic_effects,
                            OptionSemanticEffect::FinalInstallSemantic) &&
                    has_option_forwarding_target(
                            select_needed->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    has_option_forwarding_target(
                            select_needed->forwarding_targets,
                            OptionForwardingTarget::FinalInstallPacman) &&
                    !has_option_forwarding_target(
                            select_needed->forwarding_targets,
                            OptionForwardingTarget::Makepkg) &&
                    select_no_confirm != nullptr &&
                    select_no_confirm->semantic_effects ==
                            option_effect(
                                    OptionSemanticEffect::MoguetControl) &&
                    select_no_confirm->forwarding_targets ==
                            option_forwarding_target(
                                    OptionForwardingTarget::None) &&
                    select_no_confirm->forwarding_occurrence ==
                            OptionForwardingOccurrence::None &&
                    select_operation.option_relations.contains(
                            OptionId::Aur) &&
                    select_operation.option_relations.contains(
                            OptionId::Repo) &&
                    select_operation.operands.terms[0].kind ==
                            OperandKind::Query,
            "-S --select structured relation differs");

    const OptionRelationContract generic_source_needed =
            source_needed_option_relation();
    const OptionRelationContract generic_source_no_confirm =
            source_no_confirm_option_relation();
    expect(
            generic_source_needed.forwarding_targets ==
                            option_forwarding_target(
                                    OptionForwardingTarget::FinalInstallPacman) &&
                    generic_source_needed.forwarding_targets !=
                            select_needed->forwarding_targets &&
                    has_option_forwarding_target(
                            generic_source_no_confirm.forwarding_targets,
                            OptionForwardingTarget::Makepkg) &&
                    generic_source_no_confirm.forwarding_targets !=
                            select_no_confirm->forwarding_targets,
            "SyncSelect reused generic source-build forwarding");

    const OptionRelationContract* delegated_needed =
            delegated.option_relations.find(OptionId::Needed);
    const OptionRelationContract* delegated_marker =
            delegated.option_relations.find(OptionId::EndOfOptions);
    const OptionRelationContract* delegated_no_confirm =
            delegated.option_relations.find(OptionId::NoConfirm);
    const OperationFormSpec& strict_build = operation_form(
            operation_metadata(OperationId::Build), 0);
    const OptionRelationContract* source_no_confirm =
            strict_build.option_relations.find(OptionId::NoConfirm);
    const OptionRelationContract* consumed_no_confirm =
            operation_form(operation_metadata(OperationId::Deps), 0)
                    .option_relations.find(OptionId::NoConfirm);
    const OptionRelationContract* clean_no_confirm =
            operation_form(operation_metadata(OperationId::Clean), 0)
                    .option_relations.find(OptionId::NoConfirm);
    const OptionRelationContract* revert_no_confirm =
            operation_form(operation_metadata(OperationId::Revert), 0)
                    .option_relations.find(OptionId::NoConfirm);
    expect(
            delegated.is_open_grammar &&
                    delegated.owner == GrammarOwnership::DelegatedPacman &&
                    delegated.target_policy == TargetPolicy::Delegated &&
                    delegated.dry_run_support ==
                            DryRunSupport::Unsupported,
            "Delegated pacman grammar became a closed allowlist");
    expect(
            delegated_needed != nullptr &&
                    delegated_needed->occurrence ==
                            OptionOccurrence::Delegated &&
                    delegated_needed->forwarding_occurrence ==
                            OptionForwardingOccurrence::PreserveAll &&
                    has_option_forwarding_target(
                            delegated_needed->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    !has_option_forwarding_target(
                            delegated_needed->forwarding_targets,
                            OptionForwardingTarget::FinalInstallPacman) &&
                    delegated_needed->forwarding_occurrence !=
                            select_needed->forwarding_occurrence &&
                    delegated_marker != nullptr &&
                    delegated_marker->forwarding_occurrence ==
                            OptionForwardingOccurrence::PreserveAll &&
                    has_option_effect(
                            delegated_marker->semantic_effects,
                            OptionSemanticEffect::ParserBoundary) &&
                    has_option_forwarding_target(
                            delegated_marker->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    !strict_build.option_relations.contains(
                            OptionId::EndOfOptions),
            "Delegated --/--needed semantics were flattened into a strict form");
    expect(
            delegated_no_confirm != nullptr &&
                    source_no_confirm != nullptr &&
                    consumed_no_confirm != nullptr &&
                    clean_no_confirm != nullptr &&
                    revert_no_confirm != nullptr &&
                    has_option_effect(
                            delegated_no_confirm->semantic_effects,
                            OptionSemanticEffect::MoguetControl) &&
                    has_option_forwarding_target(
                            delegated_no_confirm->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    !has_option_forwarding_target(
                            delegated_no_confirm->forwarding_targets,
                            OptionForwardingTarget::Makepkg) &&
                    has_option_forwarding_target(
                            source_no_confirm->forwarding_targets,
                            OptionForwardingTarget::Makepkg) &&
                    has_option_forwarding_target(
                            source_no_confirm->forwarding_targets,
                            OptionForwardingTarget::FinalInstallPacman) &&
                    has_option_forwarding_target(
                            clean_no_confirm->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    has_option_forwarding_target(
                            revert_no_confirm->forwarding_targets,
                            OptionForwardingTarget::Pacman) &&
                    consumed_no_confirm->forwarding_targets ==
                            option_forwarding_target(
                                    OptionForwardingTarget::None),
            "--noconfirm consumed/forwarded route semantics were flattened");
}

void test_diagnostic_projection_preserves_typed_reasons() {
    InvalidRootPackageSelection invalid;
    invalid.issues.push_back(
            ConflictingRootPackageSelectionAlternatives{
                    "ambiguous-root", {}});
    const auto invalid_diagnostics =
            project_root_selection_diagnostics(invalid);
    expect(
            invalid_diagnostics.size() == 1 &&
                    invalid_diagnostics.front().classification ==
                            DiagnosticClass::Ambiguous &&
                    invalid_diagnostics.front().required_action ==
                            DiagnosticRequiredAction::SelectCandidate &&
                    std::holds_alternative<
                            ConflictingRootPackageSelectionAlternatives>(
                            invalid_diagnostics.front().reason),
            "Ambiguous root-selection reason was flattened to invalid");

    const auto cancelled = project_root_selection_diagnostic(
            CancelledRootPackageSelection{
                    RootPackageSelectionCancellationReason::EndOfInput},
            "root-query");
    expect(
            cancelled.classification == DiagnosticClass::Cancelled &&
                    cancelled.reason ==
                            RootPackageSelectionCancellationReason::EndOfInput &&
                    cancelled.identity.requested_package == "root-query" &&
                    cancelled.blocking_decision ==
                            DiagnosticBlockingDecision::BlocksCurrentOperation &&
                    cancelled.exit_status_effect ==
                            DiagnosticExitStatusEffect::Failure,
            "Root cancellation diagnostic lost an independent dimension");

    const auto unavailable = project_root_selection_diagnostic(
            UnavailableRootPackageSelection{
                    RootPackageSelectionUnavailableReason::NoConfirm},
            "root-query");
    expect(
            unavailable.classification == DiagnosticClass::Unavailable &&
                    unavailable.reason ==
                            RootPackageSelectionUnavailableReason::NoConfirm &&
                    unavailable.required_action ==
                            DiagnosticRequiredAction::EnableInteraction,
            "Non-interactive root-selection reason lost its action");

    const auto local = project_local_source_root_diagnostic(
            LocalSourceRootFailure{
                    LocalSourceRootStage::SrcinfoRead,
                    LocalSourceRootErrorCode::UnsafeMetadata,
                    std::filesystem::path{"/fixture/local"},
                    std::nullopt});
    expect(
            local.classification == DiagnosticClass::MetadataFailure &&
                    local.reason.code ==
                            LocalSourceRootErrorCode::UnsafeMetadata &&
                    local.phase == DiagnosticPhase::Metadata &&
                    local.identity.source_kind ==
                            DiagnosticSourceKind::Local &&
                    local.identity.local_root ==
                            std::filesystem::path{"/fixture/local"},
            "Local root diagnostic lost typed source/reason identity");

    SyncRepositoryMetadataReadFailure repository_failure{
            RootTargetIdentity{4, "requested-child"},
            RepositoryMetadataFailure{
                    RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
                    std::optional<std::string>{"extra"},
                    "localized detail is presentation-only"}};
    const auto repository =
            project_sync_install_diagnostic(repository_failure);
    expect(
            repository.classification == DiagnosticClass::QueryFailure &&
                    repository.reason == RepositoryMetadataFailureKind::
                                                 SyncDatabaseUnavailable &&
                    repository.identity.repository == "extra" &&
                    repository.identity.requested_package ==
                            "requested-child",
            "Repository diagnostic flattened source identity");

    SyncInstallPreparationIssue sync_issue;
    sync_issue.kind =
            SyncInstallPreparationIssueKind::UnsupportedSourceOption;
    sync_issue.root = RootTargetIdentity{5, "sync-child"};
    sync_issue.option = "--repo";
    sync_issue.diagnostic = "localized detail is presentation-only";
    const auto sync = project_sync_install_diagnostic(sync_issue);
    expect(
            sync.classification == DiagnosticClass::Unsupported &&
                    sync.reason.kind ==
                            SyncInstallPreparationIssueKind::
                                    UnsupportedSourceOption &&
                    sync.reason.option == "--repo" &&
                    sync.identity.requested_package == "sync-child",
            "Sync preparation diagnostic lost its route-owned reason");

    UpgradeAllOperationIssue issue;
    issue.kind = UpgradeAllOperationIssueKind::AurQueryFailed;
    issue.phase = UpgradeAllOperationPhase::AurQuery;
    issue.package_name = "aur-child";
    issue.package_metadata_failure = PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "typed nested metadata failure"};
    issue.diagnostic = "localized detail is not classification authority";
    const auto upgrade = project_upgrade_all_diagnostic(issue);
    expect(
            upgrade.classification == DiagnosticClass::QueryFailure &&
                    upgrade.reason.kind ==
                            UpgradeAllOperationIssueKind::AurQueryFailed &&
                    upgrade.phase == DiagnosticPhase::Query &&
                    upgrade.identity.source_kind ==
                            DiagnosticSourceKind::Aur &&
                    upgrade.reason.package_metadata_failure->code ==
                            PackageMetadataErrorCode::QueryFailed &&
                    upgrade.identity.requested_package == "aur-child",
            "upgrade-all typed issue was flattened through its wording");

    UpgradeAllOperationIssue inventory_issue;
    inventory_issue.kind =
            UpgradeAllOperationIssueKind::ForeignInventoryReadFailed;
    inventory_issue.phase =
            UpgradeAllOperationPhase::ForeignInventory;
    const auto inventory =
            project_upgrade_all_diagnostic(inventory_issue);
    expect(
            inventory.identity.source_kind ==
                            DiagnosticSourceKind::Pacman &&
                    upgrade_all_source_kind(
                            UpgradeAllOperationPhase::ForeignInventory) ==
                            DiagnosticSourceKind::Pacman &&
                    upgrade_all_source_kind(
                            UpgradeAllOperationPhase::Reduction) ==
                            DiagnosticSourceKind::Aur,
            "upgrade-all phase/source authority diverged by projection");

    constexpr std::array<DiagnosticClass, 12> taxonomy = {
            DiagnosticClass::Invalid,
            DiagnosticClass::Unsupported,
            DiagnosticClass::Ambiguous,
            DiagnosticClass::Cancelled,
            DiagnosticClass::Unavailable,
            DiagnosticClass::QueryFailure,
            DiagnosticClass::MetadataFailure,
            DiagnosticClass::RequiresCheck,
            DiagnosticClass::Blocked,
            DiagnosticClass::PartialFailure,
            DiagnosticClass::ExecutionFailure,
            DiagnosticClass::InternalInconsistency};
    static_assert(taxonomy.size() == 12);
}

OperationStateProjection project_fixture(
        bool success, bool claims_no_op, bool partial, bool attempted,
        PackageStateChange state,
        ObservationReason reason = ObservationReason::ObservationNotPrepared) {
    return project_operation_state(OperationStateProjectionInput{
            success,
            claims_no_op
                    ? std::optional<NoOpBasis>{NoOpBasis::VerifiedUnchanged}
                    : std::nullopt,
            false,
            partial,
            attempted,
            false,
            state,
            reason});
}

void test_operation_outcome_and_observation_are_orthogonal() {
    const OperationStateProjection succeeded_changed = project_fixture(
            true, false, false, true, PackageStateChange::Changed);
    const OperationStateProjection succeeded_unchanged = project_fixture(
            true, false, false, true, PackageStateChange::NoChange);
    const OperationStateProjection verified_unchanged_noop = project_fixture(
            true, true, false, true, PackageStateChange::NoChange);
    const OperationStateProjection succeeded_unverified = project_fixture(
            true, true, false, true, PackageStateChange::Unknown,
            ObservationReason::ObservationNotPrepared);
    const OperationStateProjection partial_changed = project_fixture(
            false, false, true, true, PackageStateChange::Changed);
    const OperationStateProjection failed_unverified = project_fixture(
            false, false, false, true, PackageStateChange::Unknown,
            ObservationReason::OperationFailed);
    const OperationStateProjection no_relevant_work =
            project_operation_state(OperationStateProjectionInput{
                    true,
                    NoOpBasis::NoRelevantWork,
                    false,
                    false,
                    true,
                    false,
                    PackageStateChange::Unknown,
                    ObservationReason::ObservationNotPrepared});
    const OperationStateProjection not_attempted =
            project_operation_state(OperationStateProjectionInput{
                    false,
                    std::nullopt,
                    false,
                    false,
                    false,
                    false,
                    PackageStateChange::Unknown,
                    ObservationReason::PhaseNotAttempted});

    expect(
            succeeded_changed.outcome == OperationOutcome::Succeeded &&
                    !succeeded_changed.no_op_basis.has_value() &&
                    succeeded_changed.package_state.state ==
                            PackageStateObservation::Changed,
            "Succeeded + Changed differs");
    expect(
            succeeded_unchanged.outcome == OperationOutcome::Succeeded &&
                    succeeded_unchanged.package_state.state ==
                            PackageStateObservation::VerifiedUnchanged,
            "Succeeded + VerifiedUnchanged differs");
    expect(
            verified_unchanged_noop.outcome == OperationOutcome::NoOp &&
                    verified_unchanged_noop.no_op_basis ==
                            std::optional<NoOpBasis>{
                                    NoOpBasis::VerifiedUnchanged},
            "Verified-unchanged NoOp basis was not preserved in projection");
    expect(
            succeeded_unverified.outcome == OperationOutcome::Succeeded &&
                    succeeded_unverified.outcome != OperationOutcome::NoOp &&
                    !succeeded_unverified.no_op_basis.has_value() &&
                    succeeded_unverified.package_state.state ==
                            PackageStateObservation::Unverified &&
                    succeeded_unverified.package_state.reason ==
                            ObservationReason::ObservationNotPrepared,
            "Succeeded + Unverified was flattened to NoOp or failure");
    expect(
            partial_changed.outcome == OperationOutcome::PartialFailure &&
                    partial_changed.package_state.state ==
                            PackageStateObservation::Changed,
            "PartialFailure + Changed differs");
    expect(
            failed_unverified.outcome == OperationOutcome::Failed &&
                    failed_unverified.package_state.state ==
                            PackageStateObservation::Unverified &&
                    failed_unverified.package_state.reason ==
                            ObservationReason::OperationFailed,
            "Failed + Unverified differs");
    expect(
            no_relevant_work.outcome == OperationOutcome::NoOp &&
                    no_relevant_work.no_op_basis ==
                            std::optional<NoOpBasis>{
                                    NoOpBasis::NoRelevantWork} &&
                    no_relevant_work.package_state.state ==
                            PackageStateObservation::Unverified,
            "No-relevant-work NoOp was coupled to observation state");
    expect(
            not_attempted.outcome == OperationOutcome::NotAttempted &&
                    !not_attempted.no_op_basis.has_value() &&
                    not_attempted.package_state.state ==
                            PackageStateObservation::NotObserved &&
                    not_attempted.package_state.reason ==
                            ObservationReason::PhaseNotAttempted,
            "Not-attempted phase was flattened to unverified failure");
}

AurUpdateOperationTargetResult aur_target(
        std::string package_name, std::string package_base,
        AurUpdateClassification classification,
        AurUpdateOperationTargetStatus status) {
    AurUpdateOperationTargetResult target;
    target.update.installed_name = std::move(package_name);
    target.update.classification = classification;
    target.package_base = std::move(package_base);
    target.status = status;
    return target;
}

PresentationProjection project_upgrade_all_fixture(
        const UpgradeAllOperationResult& result) {
    return project_upgrade_all_presentation_with_operation_state(
            result, OperationStateProjection{});
}

void test_presentation_partition_preserves_identity() {
    const AurUpdateOperationTargetResult ordinary = aur_target(
            "ordinary", "ordinary", AurUpdateClassification::UpToDate,
            AurUpdateOperationTargetStatus::Skipped);
    AurUpdateOperationTargetResult split = aur_target(
            "requested-child", "shared-base",
            AurUpdateClassification::UpdateAvailable,
            AurUpdateOperationTargetStatus::Updated);
    AurUpdateOperationExecutionContribution selected_child;
    selected_child.selected_artifact =
            ArtifactPackageIdentity{"requested-child", "2.0-1"};
    split.execution_contributions.push_back(std::move(selected_child));

    const PresentationItem ordinary_item =
            project_aur_update_presentation_item(ordinary);
    const PresentationItem split_item =
            project_aur_update_presentation_item(split);
    expect(
            ordinary_item.package_base == "ordinary" &&
                    ordinary_item.package_state->state ==
                            PackageStateObservation::NotObserved &&
                    should_suppress_repeated_package_base_identity(
                            ordinary_item),
            "package == PackageBase normal suppression differs");
    expect(
            split_item.requested_package == "requested-child" &&
                    split_item.package_base == "shared-base" &&
                    split_item.canonical_source_identity ==
                            "aur:shared-base" &&
                    split_item.selected_artifacts.size() == 1 &&
                    split_item.selected_artifacts.front().package_name ==
                            "requested-child" &&
                    has_distinct_package_base_identity(split_item) &&
                    is_attention_required(split_item),
            "package != PackageBase identity was flattened");

    UpgradeAllOperationResult aggregate;
    aggregate.aur.operation_result.emplace();
    aggregate.aur.operation_result->reduced_operation_result.targets = {
            ordinary, split};
    const PresentationProjection projection =
            project_upgrade_all_fixture(aggregate);
    expect(
            projection.summary_counts.total == 2 &&
                    projection.summary_counts.normal == 1 &&
                    projection.summary_counts.attention_required == 1 &&
                    projection.summary_counts.update_candidates == 1 &&
                    projection.summary_counts.not_observed == 1 &&
                    projection.summary_counts.split_identities == 1 &&
                    projection.attention_items.size() == 1 &&
                    projection.full_items.size() == 2,
            "summary/attention/full presentation partition differs");
    expect(
            projection.attention_items.front().requested_package ==
                            "requested-child" &&
                    projection.attention_items.front().package_base ==
                            "shared-base",
            "Attention projection lost requested child or PackageBase");

    RegisteredSourcePreferenceSnapshot repository_source;
    repository_source.original_preference_index = 7;
    repository_source.preference_package_name = "repository-child";
    repository_source.canonical_source_identity_key =
            "repository:repository-base";
    repository_source.resolved_package_base = "repository-base";
    repository_source.source_kind = SourceBuildSourceKind::Repository;
    repository_source.repository_identity.emplace(
            RepositoryPackagePresent{
                    "extra", 0, "repository-child", "repository-base",
                    std::nullopt, std::nullopt});

    RegisteredSourceUpgradeResult repository_result;
    repository_result.original_preference_index = 7;
    repository_result.preference_package_name = "repository-child";
    repository_result.canonical_source_identity_key =
            "repository:repository-base";
    repository_result.resolved_package_base = "repository-base";
    repository_result.status = RegisteredSourceUpgradeStatus::Updated;
    repository_result.failure_kind = RegisteredSourceUpgradeFailureKind::None;
    repository_result.package_state_change = PackageStateChange::Changed;
    repository_result.package_base_execution =
            RegisteredSourcePackageBaseExecutionSnapshot{
                    "repository-base",
                    PackageBaseSourceBuildSelectedResult{
                            ArtifactPackageIdentity{
                                    "repository-child", "3.0-1"},
                            DesiredInstallReason::Explicit,
                            ArtifactInstallExecutionOutcome::Installed},
                    {ArtifactPackageIdentity{
                            "repository-extra", "3.0-1"}}};

    const PresentationItem repository_item =
            project_registered_source_presentation_item(
                    &repository_source, repository_result);
    expect(
            repository_item.source_kind ==
                            DiagnosticSourceKind::RepositorySource &&
                    repository_item.repository == "extra" &&
                    repository_item.requested_package ==
                            "repository-child" &&
                    repository_item.package_base == "repository-base" &&
                    repository_item.canonical_source_identity ==
                            "repository:repository-base" &&
                    repository_item.selected_artifacts.size() == 1 &&
                    repository_item.unselected_artifacts.size() == 1 &&
                    is_attention_required(repository_item),
            "Repository source projection flattened PackageBase-set identity");

    BuildPlan plan;
    plan.package_targets = {
            PlannedPackageTarget{"normal-plan", "normal-plan", {}, {}},
            PlannedPackageTarget{"split-plan-child", "split-plan-base", {}, {}}};
    plan.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{
                    "split-plan-base", "split-plan-child"});
    const PresentationProjection plan_projection =
            project_build_plan_presentation(plan);
    expect(
            plan_projection.summary_counts.total == 2 &&
                    plan_projection.summary_counts.normal == 1 &&
                    plan_projection.summary_counts.attention_required == 1 &&
                    plan_projection.attention_items.front()
                                    .requested_package ==
                            "split-plan-child" &&
                    plan_projection.attention_items.front().package_base ==
                            "split-plan-base" &&
                    plan_projection.attention_items.front()
                                    .source_kind ==
                            DiagnosticSourceKind::Unspecified &&
                    !plan_projection.attention_items.front()
                             .canonical_source_identity.has_value(),
            "BuildPlan presentation lost summary or split identity");
}

std::vector<PlanPresentationReason> collect_plan_presentation_reasons(
        const PresentationProjection& projection) {
    std::vector<PlanPresentationReason> reasons;
    for(const PresentationItem& item : projection.full_items) {
        reasons.insert(
                reasons.end(), item.plan_reasons.begin(),
                item.plan_reasons.end());
    }
    return reasons;
}

void expect_install_reason_projection_equivalence(
        const BuildPlan& plan, PlanPresentationReasonKind expected_kind,
        ExecutionReadinessState expected_state,
        const std::string& context) {
    const PlanStateProjection state = project_build_plan_state(plan);
    const ExecutionReadiness& install = execution_readiness(
            state, ExecutionCapability::Install);
    const PresentationProjection presentation =
            project_build_plan_presentation(plan);
    const std::vector<PlanPresentationReason> reasons =
            collect_plan_presentation_reasons(presentation);

    expect(
            install.reasons.size() == 1 && reasons.size() == 1,
            context + ": shared/presentation reason cardinality differs");
    expect(
            reasons.front().kind == expected_kind &&
                    reasons.front().capability ==
                            ExecutionCapability::Install &&
                    reasons.front().readiness ==
                            install.reasons.front().state &&
                    reasons.front().readiness == expected_state &&
                    reasons.front().blocks_production_guard ==
                            install.reasons.front()
                                    .blocks_production_guard &&
                    reasons.front().required_action ==
                            install.reasons.front().required_action,
            context + ": presentation reclassified shared readiness");
    expect(
            presentation.summary_counts.attention_required > 0 &&
                    (!install.reasons.front().blocks_production_guard ||
                     presentation.summary_counts.blockers > 0),
            context + ": shared blocker fell through to normal presentation");
}

BuildPlan missing_constraint_evaluation_plan() {
    BuildPlan plan;
    plan.package_targets.push_back(
            PlannedPackageTarget{"missing-eval-root", "missing-eval-root", {}, {}});
    ConsumerDependencyRequirement requirement(
            "missing-eval-dependency", "missing-eval-dependency",
            std::nullopt);
    RepositoryExactPackage candidate{
            ConfiguredRepositoryIdentity{"core", 0},
            "missing-eval-dependency",
            "missing-eval-dependency",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage,
                    "1.0-1"),
            {}};
    plan.dependency_edges.push_back(BuildPlanDependencyEdge{
            "missing-eval-root",
            "missing-eval-root",
            "missing-eval-dependency",
            PackageRole::RuntimeDependency,
            DependencyKind::Repo,
            std::optional<std::string>{"missing-eval-dependency"},
            std::nullopt,
            std::nullopt,
            ProviderResolutionKind::Unique,
            DependencyRequirement{requirement},
            ResolvedDependencyCandidate{candidate},
            std::nullopt});
    return plan;
}

void test_build_plan_presentation_uses_shared_install_readiness() {
    BuildPlan identity_conflict;
    identity_conflict.provided = {
            BuildPlanProvidedDependency{
                    "virtual-a",
                    ProvidedDependency::from_repository(
                            "extra", "shared-provider"),
                    ProviderResolutionKind::UserSelected},
            BuildPlanProvidedDependency{
                    "virtual-b",
                    ProvidedDependency::from_repository(
                            "community", "shared-provider"),
                    ProviderResolutionKind::UserSelected}};
    expect_install_reason_projection_equivalence(
            identity_conflict,
            PlanPresentationReasonKind::SelectedProviderIdentityConflict,
            ExecutionReadinessState::Blocked,
            "selected-provider package identity conflict");

    const BuildPlan missing_evaluation =
            missing_constraint_evaluation_plan();
    expect_install_reason_projection_equivalence(
            missing_evaluation,
            PlanPresentationReasonKind::ConstraintAuthority,
            ExecutionReadinessState::Blocked,
            "resolved candidate with MissingEvaluation");
    const PresentationProjection missing_evaluation_presentation =
            project_build_plan_presentation(missing_evaluation);
    expect(
            missing_evaluation_presentation.summary_counts.blockers == 1 &&
                    missing_evaluation_presentation.summary_counts.normal == 0,
            "MissingEvaluation production blocker fell through to normal item");

    BuildPlan unresolved;
    unresolved.unresolved.push_back("unresolved-dependency");
    expect_install_reason_projection_equivalence(
            unresolved,
            PlanPresentationReasonKind::UnresolvedDependency,
            ExecutionReadinessState::Blocked, "unresolved dependency");

    BuildPlan ambiguous;
    ambiguous.ambiguous_providers.push_back(AmbiguousProvidedDependency{
            "ambiguous-dependency",
            {ProvidedDependency::from_repository(
                     "extra", "provider-a"),
             ProvidedDependency::from_repository(
                     "community", "provider-b")}});
    expect_install_reason_projection_equivalence(
            ambiguous, PlanPresentationReasonKind::AmbiguousProvider,
            ExecutionReadinessState::Blocked, "ambiguous provider");

    BuildPlan cycle;
    cycle.cycles.push_back("cycle-base");
    expect_install_reason_projection_equivalence(
            cycle, PlanPresentationReasonKind::DependencyCycle,
            ExecutionReadinessState::Blocked, "dependency cycle");

    BuildPlan metadata;
    metadata.package_targets.push_back(
            PlannedPackageTarget{"risk-child", "risk-base", {}, {}});
    metadata.metadata_risks.push_back(BuildPlanMetadataRisk{
            "risk-child", "risk-base", {"declared-conflict"}, {}});
    expect_install_reason_projection_equivalence(
            metadata, PlanPresentationReasonKind::DeclaredRelation,
            ExecutionReadinessState::RequiresCheck, "metadata risk");

    BuildPlan split;
    split.package_targets.push_back(
            PlannedPackageTarget{"split-child", "split-base", {}, {}});
    split.split_package_targets.push_back(
            BuildPlanSplitPackageTarget{"split-base", "split-child"});
    expect_install_reason_projection_equivalence(
            split, PlanPresentationReasonKind::SplitPackage,
            ExecutionReadinessState::Blocked, "split PackageBase");
}

template<typename Reason>
const UpgradeAllPresentationReason* find_upgrade_all_presentation_reason(
        const PresentationProjection& projection, Reason expected) {
    for(const PresentationItem& item : projection.attention_items) {
        for(const UpgradeAllPresentationReason& reason :
            item.upgrade_all_reasons) {
            const Reason* value = std::get_if<Reason>(&reason.reason);
            if(value != nullptr && *value == expected) return &reason;
        }
    }
    return nullptr;
}

const PresentationItem& expect_single_logical_failure(
        const PresentationProjection& projection,
        UpgradeAllOperationPhase phase,
        DiagnosticSourceKind source_kind,
        const char* context) {
    expect(
            projection.summary_counts.total == 1 &&
                    projection.summary_counts.attention_required == 1 &&
                    projection.summary_counts.failures == 1 &&
                    projection.attention_items.size() == 1 &&
                    projection.full_items.size() == 1,
            std::string{context} +
                    ": logical failure was counted more than once");
    const PresentationItem& item = projection.attention_items.front();
    expect(
            item.source_kind == source_kind &&
                    std::all_of(
                            item.upgrade_all_reasons.begin(),
                            item.upgrade_all_reasons.end(),
                            [phase, source_kind](
                                    const UpgradeAllPresentationReason&
                                            reason) {
                                return reason.phase == phase &&
                                       reason.source_kind == source_kind;
                            }),
            std::string{context} +
                    ": phase/source attribution split across carriers");
    return item;
}

std::vector<std::pair<std::string, std::string>>
logical_package_identities(const PresentationProjection& projection) {
    std::vector<std::pair<std::string, std::string>> identities;
    for(const PresentationItem& item : projection.full_items) {
        if(item.requested_package.has_value() &&
           item.package_base.has_value()) {
            identities.emplace_back(
                    item.requested_package.value(),
                    item.package_base.value());
        }
    }
    std::sort(identities.begin(), identities.end());
    return identities;
}

std::vector<std::pair<std::string, std::string>>
reason_package_identities(const PresentationProjection& projection) {
    std::vector<std::pair<std::string, std::string>> identities;
    for(const PresentationItem& item : projection.full_items) {
        for(const UpgradeAllPresentationReason& reason :
            item.upgrade_all_reasons) {
            if(reason.correlation.package_name.has_value() &&
               reason.correlation.package_base.has_value()) {
                identities.emplace_back(
                        reason.correlation.package_name.value(),
                        reason.correlation.package_base.value());
            }
        }
    }
    std::sort(identities.begin(), identities.end());
    return identities;
}

PresentationProjection project_reduction_identity_collision(
        const std::array<std::pair<std::string, std::string>, 2>&
                insertion_order) {
    UpgradeAllOperationResult result;
    result.status = UpgradeAllOperationStatus::InconsistentResult;
    result.stopped_phase = UpgradeAllOperationPhase::Reduction;
    result.aur.status = UpgradeAllAurPhaseStatus::InconsistentResult;
    result.aur.operation_result.emplace();
    FilteredAurUpdateExecutionResult& filtered =
            result.aur.operation_result.value();
    filtered.reduced_operation_result.status =
            AurUpdateOperationStatus::InconsistentResult;

    AurUpdateOperationReductionIssue reduction_issue;
    reduction_issue.reason =
            AurUpdateOperationReductionReason::MissingExecutionResult;
    reduction_issue.stage = AurUpdateOperationReductionStage::Execution;
    reduction_issue.affected_update_plan_indices = {4};
    reduction_issue.execution_work_item_index = 5;
    reduction_issue.diagnostic = "identity-free reduction carrier";
    filtered.reduced_operation_result.reduction_issues.push_back(
            std::move(reduction_issue));

    for(const auto& [package_name, package_base] : insertion_order) {
        FilteredAurUpdateOperationIssue mapping_issue;
        mapping_issue.kind = FilteredAurUpdateOperationIssueKind::
                ExecutionBuildUnitMappingInconsistent;
        mapping_issue.build_plan_order_index = 6;
        mapping_issue.invocation_work_item_index = 5;
        mapping_issue.package_name = package_name;
        mapping_issue.package_base = package_base;
        mapping_issue.diagnostic = "conflicting package carrier";
        filtered.issues.push_back(std::move(mapping_issue));
    }

    return project_upgrade_all_fixture(result);
}

void test_upgrade_all_logical_failure_correlation() {
    UpgradeAllOperationResult inventory_failure;
    inventory_failure.status =
            UpgradeAllOperationStatus::StoppedBeforeAurExecution;
    inventory_failure.stopped_phase =
            UpgradeAllOperationPhase::ForeignInventory;
    inventory_failure.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Failed;
    inventory_failure.foreign_inventory.failure = PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "phase-local inventory detail"};
    inventory_failure.foreign_inventory.diagnostic =
            "phase-local inventory detail";
    UpgradeAllOperationIssue inventory_issue;
    inventory_issue.kind =
            UpgradeAllOperationIssueKind::ForeignInventoryReadFailed;
    inventory_issue.phase =
            UpgradeAllOperationPhase::ForeignInventory;
    inventory_issue.package_metadata_failure =
            inventory_failure.foreign_inventory.failure;
    inventory_issue.diagnostic = "outer inventory issue detail";
    inventory_failure.issues.push_back(std::move(inventory_issue));
    inventory_failure.diagnostics.push_back(
            UpgradeAllOperationDiagnostic{
                    UpgradeAllOperationPhase::ForeignInventory, true,
                    "aggregate stopping detail"});
    inventory_failure.aur.status =
            UpgradeAllAurPhaseStatus::NotAttempted;
    inventory_failure.aur.not_attempted_reason =
            UpgradeAllNotAttemptedReason::ForeignInventoryFailure;
    inventory_failure.aur.diagnostic = "AUR boundary detail";

    const PresentationProjection inventory_projection =
            project_upgrade_all_fixture(inventory_failure);
    const PresentationItem& inventory_item = expect_single_logical_failure(
            inventory_projection,
            UpgradeAllOperationPhase::ForeignInventory,
            DiagnosticSourceKind::Pacman,
            "foreign inventory preflight");
    expect(
            !inventory_item.requested_package.has_value() &&
                    !inventory_item.package_base.has_value() &&
                    find_upgrade_all_presentation_reason(
                            inventory_projection,
                            UpgradeAllForeignInventoryPhaseStatus::Failed) !=
                            nullptr &&
                    find_upgrade_all_presentation_reason(
                            inventory_projection,
                            UpgradeAllOperationIssueKind::
                                    ForeignInventoryReadFailed) != nullptr &&
                    find_upgrade_all_presentation_reason(
                            inventory_projection,
                            PackageMetadataErrorCode::QueryFailed) !=
                            nullptr &&
                    find_upgrade_all_presentation_reason(
                            inventory_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AggregateDiagnostic) != nullptr &&
                    find_upgrade_all_presentation_reason(
                            inventory_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AurPhaseDiagnostic) != nullptr,
            "foreign inventory carriers lost typed reasons or made a fake package");

    UpgradeAllOperationResult preparation_failure;
    preparation_failure.status =
            UpgradeAllOperationStatus::StoppedBeforeAurExecution;
    preparation_failure.stopped_phase =
            UpgradeAllOperationPhase::AurPreparation;
    preparation_failure.aur.status =
            UpgradeAllAurPhaseStatus::BlockedBeforeExecution;
    preparation_failure.aur.diagnostic = "AUR preparation boundary detail";
    UpgradeAllOperationIssue preparation_issue;
    preparation_issue.kind =
            UpgradeAllOperationIssueKind::FilteredAurPreparationFailed;
    preparation_issue.phase =
            UpgradeAllOperationPhase::AurPreparation;
    preparation_issue.diagnostic = "typed preparation issue detail";
    preparation_failure.issues.push_back(std::move(preparation_issue));
    preparation_failure.diagnostics.push_back(
            UpgradeAllOperationDiagnostic{
                    UpgradeAllOperationPhase::AurPreparation, true,
                    "aggregate preparation detail"});

    const PresentationProjection preparation_projection =
            project_upgrade_all_fixture(preparation_failure);
    const PresentationItem& preparation_item = expect_single_logical_failure(
            preparation_projection,
            UpgradeAllOperationPhase::AurPreparation,
            DiagnosticSourceKind::Aur,
            "AUR preparation failure");
    expect(
            !preparation_item.requested_package.has_value() &&
                    !preparation_item.package_base.has_value() &&
                    find_upgrade_all_presentation_reason(
                            preparation_projection,
                            UpgradeAllOperationIssueKind::
                                    FilteredAurPreparationFailed) != nullptr &&
                    find_upgrade_all_presentation_reason(
                            preparation_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AggregateDiagnostic) != nullptr &&
                    find_upgrade_all_presentation_reason(
                            preparation_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AurPhaseDiagnostic) != nullptr,
            "AUR preparation carriers were not joined at their typed phase");

    UpgradeAllOperationResult reduction_failure;
    reduction_failure.status =
            UpgradeAllOperationStatus::InconsistentResult;
    reduction_failure.stopped_phase =
            UpgradeAllOperationPhase::Reduction;
    reduction_failure.aur.status =
            UpgradeAllAurPhaseStatus::InconsistentResult;
    reduction_failure.aur.operation_result.emplace();
    FilteredAurUpdateExecutionResult& filtered =
            reduction_failure.aur.operation_result.value();
    filtered.reduced_operation_result.status =
            AurUpdateOperationStatus::InconsistentResult;
    AurUpdateOperationReductionIssue reduction_issue;
    reduction_issue.reason =
            AurUpdateOperationReductionReason::MissingExecutionResult;
    reduction_issue.stage = AurUpdateOperationReductionStage::Execution;
    reduction_issue.affected_update_plan_indices = {4};
    reduction_issue.execution_work_item_index = 5;
    reduction_issue.diagnostic = "typed reduction detail";
    filtered.reduced_operation_result.reduction_issues.push_back(
            std::move(reduction_issue));
    FilteredAurUpdateOperationIssue mapping_issue;
    mapping_issue.kind = FilteredAurUpdateOperationIssueKind::
            ExecutionBuildUnitMappingInconsistent;
    mapping_issue.build_plan_order_index = 6;
    mapping_issue.invocation_work_item_index = 5;
    mapping_issue.diagnostic = "typed mapping detail";
    filtered.issues.push_back(std::move(mapping_issue));

    const PresentationProjection reduction_projection =
            project_upgrade_all_fixture(reduction_failure);
    const PresentationItem& reduction_item = expect_single_logical_failure(
            reduction_projection, UpgradeAllOperationPhase::Reduction,
            DiagnosticSourceKind::Aur,
            "AUR planning/reduction failure");
    const UpgradeAllPresentationReason* reduction_reason =
            find_upgrade_all_presentation_reason(
                    reduction_projection,
                    AurUpdateOperationReductionReason::
                            MissingExecutionResult);
    const UpgradeAllPresentationReason* mapping_reason =
            find_upgrade_all_presentation_reason(
                    reduction_projection,
                    FilteredAurUpdateOperationIssueKind::
                            ExecutionBuildUnitMappingInconsistent);
    expect(
            !reduction_item.requested_package.has_value() &&
                    !reduction_item.package_base.has_value() &&
                    reduction_reason != nullptr &&
                    mapping_reason != nullptr &&
                    reduction_reason->correlation
                                    .affected_update_plan_indices ==
                            std::vector<std::size_t>{4} &&
                    reduction_reason->correlation
                                    .invocation_work_item_index ==
                            std::optional<std::size_t>{5} &&
                    mapping_reason->correlation
                                    .build_plan_order_index ==
                            std::optional<std::size_t>{6} &&
                    mapping_reason->correlation
                                    .invocation_work_item_index ==
                            std::optional<std::size_t>{5},
            "AUR reduction correlation was flattened during logical merge");

    UpgradeAllOperationResult cleanup_failure;
    cleanup_failure.status = UpgradeAllOperationStatus::
            StoppedAfterAurCleanupFailure;
    cleanup_failure.stopped_phase =
            UpgradeAllOperationPhase::AurExecution;
    cleanup_failure.aur.status =
            UpgradeAllAurPhaseStatus::StoppedAfterCleanupFailure;
    cleanup_failure.aur.diagnostic =
            "AUR cleanup boundary detail";
    cleanup_failure.diagnostics.push_back(
            UpgradeAllOperationDiagnostic{
                    UpgradeAllOperationPhase::AurExecution, true,
                    "aggregate cleanup boundary detail"});
    cleanup_failure.aur.operation_result.emplace();
    AurUpdateOperationTargetResult cleanup_target = aur_target(
            "cleanup-target", "cleanup-base",
            AurUpdateClassification::UpdateAvailable,
            AurUpdateOperationTargetStatus::NoChangeCleanupFailed);
    cleanup_target.update_plan_index = 9;
    cleanup_target.execution_work_item_index = 10;
    cleanup_target.execution_failure_kind =
            AurUpdateWorkItemFailureKind::
                    CleanupFailedAfterPackageTransaction;
    cleanup_failure.aur.operation_result->reduced_operation_result.status =
            AurUpdateOperationStatus::StoppedAfterPackageCleanupFailure;
    cleanup_failure.aur.operation_result->reduced_operation_result.targets.
            push_back(std::move(cleanup_target));

    const PresentationProjection cleanup_projection =
            project_upgrade_all_fixture(cleanup_failure);
    const PresentationItem& cleanup_item = expect_single_logical_failure(
            cleanup_projection, UpgradeAllOperationPhase::AurExecution,
            DiagnosticSourceKind::Aur,
            "aggregate cleanup partial failure");
    const UpgradeAllPresentationReason* cleanup_reason =
            find_upgrade_all_presentation_reason(
                    cleanup_projection,
                    AurUpdateWorkItemFailureKind::
                            CleanupFailedAfterPackageTransaction);
    expect(
            cleanup_item.requested_package == "cleanup-target" &&
                    cleanup_item.package_base == "cleanup-base" &&
                    find_upgrade_all_presentation_reason(
                            cleanup_projection,
                            AurUpdateOperationTargetStatus::
                                    NoChangeCleanupFailed) != nullptr &&
                    cleanup_reason != nullptr &&
                    find_upgrade_all_presentation_reason(
                            cleanup_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AggregateDiagnostic) != nullptr &&
                    find_upgrade_all_presentation_reason(
                            cleanup_projection,
                            UpgradeAllPresentationBoundaryReason::
                                    AurPhaseDiagnostic) != nullptr &&
                    cleanup_reason->correlation
                                    .filtered_update_plan_index ==
                            std::optional<std::size_t>{9} &&
                    cleanup_reason->correlation
                                    .invocation_work_item_index ==
                            std::optional<std::size_t>{10},
            "aggregate cleanup failure lost its actual target correlation");
}

void test_upgrade_all_failure_correlation_is_item_safe() {
    const PresentationProjection a_then_b =
            project_reduction_identity_collision(
                    {{{"package-a", "package-a-base"},
                      {"package-b", "package-b-base"}}});
    const PresentationProjection b_then_a =
            project_reduction_identity_collision(
                    {{{"package-b", "package-b-base"},
                      {"package-a", "package-a-base"}}});
    const std::vector<std::pair<std::string, std::string>>
            expected_identities = {
                    {"package-a", "package-a-base"},
                    {"package-b", "package-b-base"}};

    for(const PresentationProjection* projection : {&a_then_b, &b_then_a}) {
        std::size_t reduction_reason_count = 0;
        std::size_t mapping_reason_count = 0;
        bool has_reduction_carrier = false;
        for(const PresentationItem& item : projection->full_items) {
            bool item_has_reduction_reason = false;
            bool item_has_mapping_reason = false;
            for(const UpgradeAllPresentationReason& reason :
                item.upgrade_all_reasons) {
                if(std::get_if<AurUpdateOperationReductionReason>(
                           &reason.reason) != nullptr) {
                    ++reduction_reason_count;
                    item_has_reduction_reason = true;
                }
                if(std::get_if<FilteredAurUpdateOperationIssueKind>(
                           &reason.reason) != nullptr) {
                    ++mapping_reason_count;
                    item_has_mapping_reason = true;
                }
            }
            has_reduction_carrier |= item_has_reduction_reason &&
                    item_has_mapping_reason;
        }
        expect(
                projection->summary_counts.total == 2 &&
                        projection->summary_counts.attention_required == 2 &&
                        projection->summary_counts.failures == 2 &&
                        projection->attention_items.size() == 2 &&
                        projection->full_items.size() == 2 &&
                        logical_package_identities(*projection) ==
                                expected_identities &&
                        reason_package_identities(*projection) ==
                                expected_identities &&
                        reduction_reason_count == 1 &&
                        mapping_reason_count == 2 &&
                        has_reduction_carrier,
                "Conflicting package carriers were merged into one logical failure");
    }
    expect(
            logical_package_identities(a_then_b) ==
                    logical_package_identities(b_then_a),
            "Logical package identities depend on carrier insertion order");

    const PresentationProjection package_base_collision =
            project_reduction_identity_collision(
                    {{{"shared-package", "package-base-a"},
                      {"shared-package", "package-base-b"}}});
    expect(
            package_base_collision.summary_counts.total == 2 &&
                    package_base_collision.summary_counts.failures == 2 &&
                    reason_package_identities(package_base_collision) ==
                            std::vector<std::pair<std::string, std::string>>{
                                    {"shared-package", "package-base-a"},
                                    {"shared-package", "package-base-b"}},
            "PackageBase conflict was hidden by requested-package equality");

    UpgradeAllOperationResult nearby_distinct;
    nearby_distinct.aur.operation_result.emplace();
    FilteredAurUpdateOperationIssue plan_failure;
    plan_failure.kind = FilteredAurUpdateOperationIssueKind::
            BuildUnitOrderIdentityMismatch;
    plan_failure.build_plan_order_index = 40;
    plan_failure.invocation_work_item_index = 40;
    plan_failure.package_name = "nearby";
    plan_failure.package_base = "nearby-base";
    FilteredAurUpdateOperationIssue work_item_failure;
    work_item_failure.kind = FilteredAurUpdateOperationIssueKind::
            ExecutionBuildUnitMappingInconsistent;
    work_item_failure.build_plan_order_index = 41;
    work_item_failure.invocation_work_item_index = 41;
    work_item_failure.package_name = "nearby";
    work_item_failure.package_base = "nearby-base";
    nearby_distinct.aur.operation_result->issues.push_back(
            std::move(plan_failure));
    nearby_distinct.aur.operation_result->issues.push_back(
            std::move(work_item_failure));

    const PresentationProjection nearby_projection =
            project_upgrade_all_fixture(nearby_distinct);
    expect(
            nearby_projection.summary_counts.total == 2 &&
                    nearby_projection.summary_counts.failures == 2 &&
                    nearby_projection.attention_items.size() == 2,
            "Phase/package identity alone merged nearby distinct failures");

    UpgradeAllOperationResult ambiguous;
    ambiguous.aur.operation_result.emplace();
    FilteredAurUpdateOperationIssue work_item_match;
    work_item_match.kind = FilteredAurUpdateOperationIssueKind::
            ExecutionBuildUnitMappingInconsistent;
    work_item_match.invocation_work_item_index = 50;
    FilteredAurUpdateOperationIssue build_order_match;
    build_order_match.kind = FilteredAurUpdateOperationIssueKind::
            BuildUnitOrderIdentityMismatch;
    build_order_match.build_plan_order_index = 60;
    FilteredAurUpdateOperationIssue ambiguous_candidate;
    ambiguous_candidate.kind = FilteredAurUpdateOperationIssueKind::
            BuildUnitSelectionMappingInconsistent;
    ambiguous_candidate.invocation_work_item_index = 50;
    ambiguous_candidate.build_plan_order_index = 60;
    ambiguous.aur.operation_result->issues.push_back(
            std::move(work_item_match));
    ambiguous.aur.operation_result->issues.push_back(
            std::move(build_order_match));
    ambiguous.aur.operation_result->issues.push_back(
            std::move(ambiguous_candidate));

    const PresentationProjection ambiguous_projection =
            project_upgrade_all_fixture(ambiguous);
    const auto has_ambiguous_candidate = std::any_of(
            ambiguous_projection.full_items.begin(),
            ambiguous_projection.full_items.end(),
            [](const PresentationItem& item) {
                return item.upgrade_all_reasons.size() == 1 &&
                       item.upgrade_all_reasons.front()
                                       .correlation
                                       .invocation_work_item_index ==
                               std::optional<std::size_t>{50} &&
                       item.upgrade_all_reasons.front()
                                       .correlation
                                       .build_plan_order_index ==
                               std::optional<std::size_t>{60};
            });
    expect(
            ambiguous_projection.summary_counts.total == 3 &&
                    ambiguous_projection.summary_counts.failures == 3 &&
                    ambiguous_projection.attention_items.size() == 3 &&
                    has_ambiguous_candidate,
            "Candidate with two partial item matches was absorbed arbitrarily");
}

void test_upgrade_all_failure_only_presentation_keeps_attribution() {
    UpgradeAllOperationResult status_only_failure;
    status_only_failure.status =
            UpgradeAllOperationStatus::StoppedBeforeAurExecution;
    status_only_failure.stopped_phase =
            UpgradeAllOperationPhase::AurPreparation;
    const PresentationProjection status_only_projection =
            project_upgrade_all_fixture(status_only_failure);
    const UpgradeAllPresentationReason* status_only_reason =
            find_upgrade_all_presentation_reason(
                    status_only_projection,
                    UpgradeAllOperationStatus::StoppedBeforeAurExecution);
    expect(
            status_only_projection.summary_counts.total == 1 &&
                    status_only_projection.summary_counts.attention_required ==
                            1 &&
                    status_only_reason != nullptr &&
                    status_only_reason->phase ==
                            UpgradeAllOperationPhase::AurPreparation &&
                    status_only_reason->source_kind ==
                            DiagnosticSourceKind::Aur,
            "Failure-only aggregate status was projected as empty normal output");

    UpgradeAllOperationResult system_failure;
    system_failure.system_source.system.status =
            SystemUpgradePhaseStatus::Failed;
    const PresentationProjection system_projection =
            project_upgrade_all_fixture(system_failure);
    const UpgradeAllPresentationReason* system_reason =
            find_upgrade_all_presentation_reason(
                    system_projection, SystemUpgradePhaseStatus::Failed);
    expect(
            system_projection.summary_counts.total == 1 &&
                    system_projection.summary_counts.attention_required == 1 &&
                    system_reason != nullptr &&
                    system_reason->phase == UpgradeAllOperationPhase::System &&
                    system_reason->source_kind ==
                            DiagnosticSourceKind::Pacman,
            "No-target system failure was projected as an empty normal result");

    UpgradeAllOperationResult inventory_failure;
    inventory_failure.foreign_inventory.status =
            UpgradeAllForeignInventoryPhaseStatus::Failed;
    inventory_failure.foreign_inventory.failure = PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "wording is not classification authority"};
    const PresentationProjection inventory_projection =
            project_upgrade_all_fixture(inventory_failure);
    const UpgradeAllPresentationReason* inventory_status =
            find_upgrade_all_presentation_reason(
                    inventory_projection,
                    UpgradeAllForeignInventoryPhaseStatus::Failed);
    const UpgradeAllPresentationReason* inventory_reason =
            find_upgrade_all_presentation_reason(
                    inventory_projection,
                    PackageMetadataErrorCode::QueryFailed);
    expect(
            inventory_projection.summary_counts.total == 1 &&
                    inventory_projection.summary_counts.attention_required ==
                            1 &&
                    inventory_status != nullptr &&
                    inventory_reason != nullptr &&
                    inventory_reason->phase ==
                            UpgradeAllOperationPhase::ForeignInventory &&
                    inventory_reason->source_kind ==
                            DiagnosticSourceKind::Pacman,
            "Foreign inventory failure lost typed reason or source phase");

    UpgradeAllOperationResult aur_failures;
    aur_failures.aur.operation_result.emplace();
    FilteredAurUpdateExecutionResult& filtered =
            aur_failures.aur.operation_result.value();
    filtered.upgrade_all_plan.issues.push_back(UpgradeAllPlanningIssue{
            UpgradeAllPlanningIssueKind::UnsupportedAurTarget,
            {}, {2}, {},
            std::optional<std::string>{"planning-target"},
            std::optional<std::string>{"planning-base"}});
    AurUpdatePreparationIssue preparation;
    preparation.reason = AurUpdatePreparationReason::BuildPlanMissing;
    preparation.affected_update_plan_indices = {3};
    preparation.package_name = "preparation-target";
    preparation.package_base = "preparation-base";
    filtered.reduced_operation_result.preparation_issues.push_back(
            std::move(preparation));
    AurUpdateOperationReductionIssue reduction;
    reduction.reason =
            AurUpdateOperationReductionReason::MissingExecutionResult;
    reduction.stage = AurUpdateOperationReductionStage::Execution;
    reduction.affected_update_plan_indices = {4};
    reduction.execution_work_item_index = 5;
    filtered.reduced_operation_result.reduction_issues.push_back(
            std::move(reduction));

    const PresentationProjection aur_projection =
            project_upgrade_all_fixture(aur_failures);
    const UpgradeAllPresentationReason* planning_reason =
            find_upgrade_all_presentation_reason(
                    aur_projection,
                    UpgradeAllPlanningIssueKind::UnsupportedAurTarget);
    const UpgradeAllPresentationReason* preparation_reason =
            find_upgrade_all_presentation_reason(
                    aur_projection,
                    AurUpdatePreparationReason::BuildPlanMissing);
    const UpgradeAllPresentationReason* reduction_reason =
            find_upgrade_all_presentation_reason(
                    aur_projection,
                    AurUpdateOperationReductionReason::
                            MissingExecutionResult);
    expect(
            aur_projection.summary_counts.total == 3 &&
                    aur_projection.summary_counts.attention_required == 3 &&
                    planning_reason != nullptr &&
                    preparation_reason != nullptr &&
                    reduction_reason != nullptr &&
                    planning_reason->phase ==
                            UpgradeAllOperationPhase::AurPreparation &&
                    preparation_reason->correlation
                                    .affected_update_plan_indices ==
                            std::vector<std::size_t>{3} &&
                    reduction_reason->phase ==
                            UpgradeAllOperationPhase::Reduction &&
                    reduction_reason->correlation
                                    .invocation_work_item_index ==
                            std::optional<std::size_t>{5},
            "AUR preparation/planning/reduction attribution was flattened");

    UpgradeAllOperationResult aggregate_failure;
    aggregate_failure.issues.push_back(UpgradeAllOperationIssue{
            UpgradeAllOperationIssueKind::FilteredAurExecutionFailed,
            UpgradeAllOperationPhase::AurExecution,
            std::nullopt,
            std::nullopt,
            std::optional<std::size_t>{7},
            std::optional<std::size_t>{8},
            std::optional<std::string>{"aggregate-target"},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            "aggregate wording"});
    aggregate_failure.diagnostics.push_back(
            UpgradeAllOperationDiagnostic{
                    UpgradeAllOperationPhase::Reduction,
                    true,
                    "diagnostic wording"});
    const PresentationProjection aggregate_projection =
            project_upgrade_all_fixture(aggregate_failure);
    const UpgradeAllPresentationReason* aggregate_issue =
            find_upgrade_all_presentation_reason(
                    aggregate_projection,
                    UpgradeAllOperationIssueKind::FilteredAurExecutionFailed);
    const UpgradeAllPresentationReason* aggregate_diagnostic =
            find_upgrade_all_presentation_reason(
                    aggregate_projection,
                    UpgradeAllPresentationBoundaryReason::
                            AggregateDiagnostic);
    expect(
            aggregate_projection.summary_counts.attention_required == 2 &&
                    aggregate_issue != nullptr &&
                    aggregate_diagnostic != nullptr &&
                    aggregate_issue->correlation.original_query_plan_index ==
                            std::optional<std::size_t>{7} &&
                    aggregate_issue->correlation.build_plan_order_index ==
                            std::optional<std::size_t>{8} &&
                    aggregate_diagnostic->phase ==
                            UpgradeAllOperationPhase::Reduction,
            "Aggregate issue/diagnostic attribution was discarded");

    UpgradeAllOperationResult cleanup_failure;
    cleanup_failure.aur.operation_result.emplace();
    AurUpdateOperationTargetResult cleanup_target = aur_target(
            "cleanup-target", "cleanup-base",
            AurUpdateClassification::UpdateAvailable,
            AurUpdateOperationTargetStatus::NoChangeCleanupFailed);
    cleanup_target.update_plan_index = 9;
    cleanup_target.execution_work_item_index = 10;
    cleanup_target.execution_failure_kind =
            AurUpdateWorkItemFailureKind::
                    CleanupFailedAfterPackageTransaction;
    cleanup_failure.aur.operation_result->reduced_operation_result.targets.
            push_back(std::move(cleanup_target));
    const PresentationProjection cleanup_projection =
            project_upgrade_all_fixture(cleanup_failure);
    const UpgradeAllPresentationReason* cleanup_status =
            find_upgrade_all_presentation_reason(
                    cleanup_projection,
                    AurUpdateOperationTargetStatus::NoChangeCleanupFailed);
    const UpgradeAllPresentationReason* cleanup_reason =
            find_upgrade_all_presentation_reason(
                    cleanup_projection,
                    AurUpdateWorkItemFailureKind::
                            CleanupFailedAfterPackageTransaction);
    expect(
            cleanup_projection.summary_counts.total == 1 &&
                    cleanup_projection.summary_counts.attention_required == 1 &&
                    cleanup_projection.summary_counts.failures == 1 &&
                    cleanup_status != nullptr && cleanup_reason != nullptr &&
                    cleanup_reason->phase ==
                            UpgradeAllOperationPhase::AurExecution &&
                    cleanup_reason->source_kind ==
                            DiagnosticSourceKind::Aur &&
                    cleanup_reason->correlation
                                    .filtered_update_plan_index ==
                            std::optional<std::size_t>{9} &&
                    cleanup_reason->correlation
                                    .invocation_work_item_index ==
                            std::optional<std::size_t>{10},
            "Partial/cleanup failure lost typed target attribution");
}

template<typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("rich CLI operation contract",
                 test_rich_cli_operation_contract);
        run_case("rich CLI option/ownership contract",
                 test_rich_cli_option_and_ownership_contract);
        run_case("typed diagnostic projection",
                 test_diagnostic_projection_preserves_typed_reasons);
        run_case("operation outcome/state observation",
                 test_operation_outcome_and_observation_are_orthogonal);
        run_case("presentation partition/identity",
                 test_presentation_partition_preserves_identity);
        run_case(
                "BuildPlan presentation shared readiness equivalence",
                test_build_plan_presentation_uses_shared_install_readiness);
        run_case(
                "upgrade-all failure-only presentation attribution",
                test_upgrade_all_failure_only_presentation_keeps_attribution);
        run_case(
                "upgrade-all logical failure correlation",
                test_upgrade_all_logical_failure_correlation);
        run_case(
                "upgrade-all item-safe failure correlation",
                test_upgrade_all_failure_correlation_is_item_safe);
    } catch(const std::exception& error) {
        std::cerr << "cli_diagnostic_model_test: " << error.what() << '\n';
        return 1;
    }

    std::cout << "CLI/diagnostic model tests: all checks passed\n";
    return 0;
}
