#include "package_base_artifact_install_executor.hpp"

#include "localization.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

[[noreturn]] void throw_incoherent_package_base_install() {
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal Arch field name
            // "PackageBase".
            "Prepared {} artifact install is incoherent.", "PackageBase"));
}

bool same_identity(
        const ArtifactPackageIdentity& left,
        const ArtifactPackageIdentity& right) noexcept {
    return left.package_name == right.package_name &&
           left.full_version == right.full_version;
}

bool same_planned_policy(
        const PreparedPackageBaseArtifactInstallSelectedArtifact& prepared,
        const PlannedPackageBaseArtifactInstallReason& planned) noexcept {
    return same_identity(prepared.identity, planned.identity) &&
           prepared.desired_reason == planned.desired_reason &&
           prepared.installed_version_state ==
                   planned.installed_version_state &&
           prepared.existing_reason == planned.existing_reason &&
           prepared.directive == planned.directive &&
           prepared.expected_outcome == planned.expected_outcome;
}

void require_install_draft_coherence(
        const std::string& package_base,
        const ValidatedPackageArtifactSet& artifacts,
        const std::vector<ArtifactPackageIdentity>& aggregate_identities,
        const std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>&
                selected_artifacts,
        const std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>&
                unselected_artifacts,
        InstallReasonDirective transaction_directive,
        bool needed) {
    artifacts.require_validity();
    const std::size_t artifact_count = artifacts.size();
    if(package_base.empty() || artifact_count == 0 ||
       selected_artifacts.empty() ||
       aggregate_identities.size() != artifact_count ||
       selected_artifacts.size() + unselected_artifacts.size() !=
               artifact_count) {
        throw_incoherent_package_base_install();
    }

    std::vector<bool> covered_indices(artifact_count, false);
    PackageBaseArtifactInstallReasonPolicyInput policy_input;
    policy_input.package_base = package_base;
    policy_input.needed = needed;
    policy_input.selected_artifacts.reserve(selected_artifacts.size());

    for(std::size_t index = 0; index < artifact_count; ++index) {
        const ArtifactPackageIdentity& identity = aggregate_identities[index];
        if(identity.package_name.empty() || identity.full_version.empty()) {
            throw_incoherent_package_base_install();
        }
        // path_at()でprivate identity snapshotのpositionがfilesystem aggregateにも
        // 存在することを確認する。path自体は公開recordへ保存しない。
        static_cast<void>(artifacts.path_at(index));
    }

    for(const PreparedPackageBaseArtifactInstallSelectedArtifact& selected :
        selected_artifacts) {
        if(selected.artifact_index >= artifact_count ||
           covered_indices[selected.artifact_index] ||
           !same_identity(
                   selected.identity,
                   aggregate_identities[selected.artifact_index])) {
            throw_incoherent_package_base_install();
        }
        covered_indices[selected.artifact_index] = true;
        static_cast<void>(artifacts.path_at(selected.artifact_index));
        policy_input.selected_artifacts.push_back(
                SelectedPackageBaseArtifactInstallReasonPolicyInput{
                        selected.identity,
                        selected.desired_reason,
                        selected.installed_version_state,
                        selected.existing_reason});
    }

    std::size_t previous_unselected_index = 0;
    bool has_previous_unselected_index = false;
    for(const PreparedPackageBaseArtifactInstallUnselectedArtifact& unselected :
        unselected_artifacts) {
        if(unselected.artifact_index >= artifact_count ||
           covered_indices[unselected.artifact_index] ||
           !same_identity(
                   unselected.identity,
                   aggregate_identities[unselected.artifact_index]) ||
           (has_previous_unselected_index &&
            unselected.artifact_index <= previous_unselected_index)) {
            throw_incoherent_package_base_install();
        }
        covered_indices[unselected.artifact_index] = true;
        previous_unselected_index = unselected.artifact_index;
        has_previous_unselected_index = true;
        static_cast<void>(artifacts.path_at(unselected.artifact_index));
    }
    for(bool is_covered : covered_indices) {
        if(!is_covered) throw_incoherent_package_base_install();
    }

    PackageBaseArtifactInstallReasonPlanResult planned =
            resolve_package_base_artifact_install_reason_plan(policy_input);
    const PackageBaseArtifactInstallReasonPlan* success = planned.success();
    if(!planned.is_success() || success == nullptr ||
       planned.failure() != nullptr ||
       success->package_base != package_base || success->needed != needed ||
       success->transaction_directive != transaction_directive ||
       success->selected_artifacts.size() != selected_artifacts.size()) {
        throw_incoherent_package_base_install();
    }
    for(std::size_t index = 0; index < selected_artifacts.size(); ++index) {
        if(!same_planned_policy(
                   selected_artifacts[index],
                   success->selected_artifacts[index])) {
            throw_incoherent_package_base_install();
        }
    }
    artifacts.require_validity();
}

void require_correlated_selection_success(
        const std::string& package_base,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ValidatedPackageArtifactSet& artifacts,
        const ArtifactPackageIdentitySet& identity_set,
        const PackageBaseArtifactIdentitySelectionSuccess& selection,
        std::vector<ArtifactPackageIdentity>& aggregate_identities) {
    artifacts.require_validity();
    const std::size_t artifact_count = artifacts.size();
    if(required_targets.empty() || selection.selected_artifacts.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "{} artifact install requires at least one selected artifact.",
                "PackageBase"));
    }
    if(selection.package_base != package_base ||
       selection.selected_artifacts.size() != required_targets.size() ||
       selection.selected_artifacts.size() +
                       selection.unselected_artifacts.size() !=
               artifact_count ||
       identity_set.size() != artifact_count) {
        throw_incoherent_package_base_install();
    }

    aggregate_identities.clear();
    aggregate_identities.reserve(artifact_count);
    for(std::size_t position = 0; position < artifact_count; ++position) {
        const IndexedArtifactPackageIdentity& entry =
                identity_set.entry_at(position);
        if(entry.artifact_index() != position ||
           entry.identity().package_name.empty() ||
           entry.identity().full_version.empty()) {
            throw_incoherent_package_base_install();
        }
        aggregate_identities.push_back(entry.identity());
    }

    std::vector<bool> selected_indices(artifact_count, false);
    for(std::size_t position = 0;
        position < selection.selected_artifacts.size();
        ++position) {
        const CorrelatedSelectedPackageArtifact& selected =
                selection.selected_artifacts[position];
        const RequiredPackageArtifactTarget& required =
                required_targets[position];
        if(required.package_base != package_base ||
           selected.artifact_index >= artifact_count ||
           selected_indices[selected.artifact_index] ||
           selected.identity.package_name != required.package_name ||
           selected.desired_reason != required.desired_reason ||
           !same_identity(
                   selected.identity,
                   aggregate_identities[selected.artifact_index])) {
            throw_incoherent_package_base_install();
        }
        selected_indices[selected.artifact_index] = true;
    }

    std::size_t unselected_position = 0;
    for(std::size_t artifact_index = 0;
        artifact_index < artifact_count;
        ++artifact_index) {
        if(selected_indices[artifact_index]) continue;
        if(unselected_position >= selection.unselected_artifacts.size()) {
            throw_incoherent_package_base_install();
        }
        const CorrelatedUnselectedPackageArtifact& unselected =
                selection.unselected_artifacts[unselected_position];
        if(unselected.artifact_index != artifact_index ||
           !same_identity(
                   unselected.identity,
                   aggregate_identities[artifact_index])) {
            throw_incoherent_package_base_install();
        }
        ++unselected_position;
    }
    if(unselected_position != selection.unselected_artifacts.size()) {
        throw_incoherent_package_base_install();
    }
    artifacts.require_validity();
}

} // namespace

PreparedPackageBaseArtifactInstall::PreparedPackageBaseArtifactInstall(
        std::string&& package_base,
        ValidatedPackageArtifactSet&& artifacts,
        std::vector<ArtifactPackageIdentity>&& aggregate_identities,
        std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>&&
                selected_artifacts,
        std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>&&
                unselected_artifacts,
        InstallReasonDirective transaction_directive,
        bool needed) noexcept
    : package_base_(std::move(package_base)), artifacts_(std::move(artifacts)),
      aggregate_identities_(std::move(aggregate_identities)),
      selected_artifacts_(std::move(selected_artifacts)),
      unselected_artifacts_(std::move(unselected_artifacts)),
      transaction_directive_(transaction_directive), needed_(needed) {
}

PreparedPackageBaseArtifactInstall::PreparedPackageBaseArtifactInstall(
        PreparedPackageBaseArtifactInstall&& other) noexcept
    : package_base_(std::move(other.package_base_)),
      artifacts_(std::move(other.artifacts_)),
      aggregate_identities_(std::move(other.aggregate_identities_)),
      selected_artifacts_(std::move(other.selected_artifacts_)),
      unselected_artifacts_(std::move(other.unselected_artifacts_)),
      transaction_directive_(other.transaction_directive_),
      needed_(other.needed_),
      state_(std::exchange(other.state_, State::MovedFrom)) {
}

void PreparedPackageBaseArtifactInstall::require_not_moved_from() const {
    if(state_ == State::MovedFrom) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install was moved from.",
                "PackageBase"));
    }
}

void PreparedPackageBaseArtifactInstall::require_active_for_execution() const {
    require_not_moved_from();
    if(state_ != State::Active) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install is no longer executable.",
                "PackageBase"));
    }
}

void PreparedPackageBaseArtifactInstall::require_execution_coherence() const {
    require_active_for_execution();
    require_install_draft_coherence(
            package_base_, artifacts_, aggregate_identities_,
            selected_artifacts_, unselected_artifacts_,
            transaction_directive_, needed_);
}

const std::string& PreparedPackageBaseArtifactInstall::package_base() const {
    require_not_moved_from();
    return package_base_;
}

const std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>&
PreparedPackageBaseArtifactInstall::selected_artifacts() const {
    require_not_moved_from();
    return selected_artifacts_;
}

const std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>&
PreparedPackageBaseArtifactInstall::unselected_artifacts() const {
    require_not_moved_from();
    return unselected_artifacts_;
}

InstallReasonDirective
PreparedPackageBaseArtifactInstall::transaction_directive() const {
    require_not_moved_from();
    return transaction_directive_;
}

bool PreparedPackageBaseArtifactInstall::needed() const {
    require_not_moved_from();
    return needed_;
}

const std::filesystem::path&
PreparedPackageBaseArtifactInstall::workspace_path() const {
    require_not_moved_from();
    if(state_ == State::Cleaned) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install workspace was cleaned.",
                "PackageBase"));
    }
    return artifacts_.workspace_path();
}

void PreparedPackageBaseArtifactInstall::retain_workspace_for_diagnostics() {
    require_not_moved_from();
    if(state_ == State::Cleaned) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install workspace was cleaned.",
                "PackageBase"));
    }
    artifacts_.retain_workspace_for_diagnostics();
}

void PreparedPackageBaseArtifactInstall::cleanup_workspace() {
    require_not_moved_from();
    if(state_ == State::Cleaned) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install workspace was already cleaned.",
                "PackageBase"));
    }

    // LANDMINE(#268): cleanupがthrowしてもtransaction capabilityを復活させない。
    // Underlying aggregateはworkspace ownershipを保持し、retry cleanupだけを許す。
    state_ = State::CleanupPending;
    artifacts_.cleanup_workspace();
    state_ = State::Cleaned;
}

PackageBaseArtifactInstallPreparationFailure::
        PackageBaseArtifactInstallPreparationFailure(
                PackageBaseArtifactIdentitySelectionFailure failure)
    : failure_(
              std::in_place_type<
                      PackageBaseArtifactIdentitySelectionFailure>,
              std::move(failure)) {
}

PackageBaseArtifactInstallPreparationFailure::
        PackageBaseArtifactInstallPreparationFailure(
                MixedPackageBaseInstallReasonUnsupported failure)
    : failure_(
              std::in_place_type<MixedPackageBaseInstallReasonUnsupported>,
              std::move(failure)) {
}

const PackageBaseArtifactIdentitySelectionFailure*
PackageBaseArtifactInstallPreparationFailure::selection_failure()
        const noexcept {
    return std::get_if<PackageBaseArtifactIdentitySelectionFailure>(&failure_);
}

const MixedPackageBaseInstallReasonUnsupported*
PackageBaseArtifactInstallPreparationFailure::mixed_reason_failure()
        const noexcept {
    return std::get_if<MixedPackageBaseInstallReasonUnsupported>(&failure_);
}

PackageBaseArtifactInstallPreparationResult::
        PackageBaseArtifactInstallPreparationResult(
                PreparedPackageBaseArtifactInstall&& prepared) noexcept
    : outcome_(
              std::in_place_type<PreparedPackageBaseArtifactInstall>,
              std::move(prepared)) {
}

PackageBaseArtifactInstallPreparationResult::
        PackageBaseArtifactInstallPreparationResult(
                PackageBaseArtifactInstallPreparationFailure&& failure) noexcept
    : outcome_(
              std::in_place_type<
                      PackageBaseArtifactInstallPreparationFailure>,
              std::move(failure)) {
}

bool PackageBaseArtifactInstallPreparationResult::is_prepared() const noexcept {
    return std::holds_alternative<PreparedPackageBaseArtifactInstall>(outcome_);
}

PreparedPackageBaseArtifactInstall*
PackageBaseArtifactInstallPreparationResult::prepared() noexcept {
    return std::get_if<PreparedPackageBaseArtifactInstall>(&outcome_);
}

const PreparedPackageBaseArtifactInstall*
PackageBaseArtifactInstallPreparationResult::prepared() const noexcept {
    return std::get_if<PreparedPackageBaseArtifactInstall>(&outcome_);
}

const PackageBaseArtifactInstallPreparationFailure*
PackageBaseArtifactInstallPreparationResult::failure() const noexcept {
    return std::get_if<PackageBaseArtifactInstallPreparationFailure>(&outcome_);
}

PackageBaseArtifactInstallPreparationResult
prepare_package_base_artifact_install(
        ValidatedPackageArtifactSet& artifacts,
        const std::string& package_base,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ArtifactInstallPreparationOptions& options,
        const PacmanDatabasePaths& database_paths) {
    artifacts.require_validity();
    require_supported_separated_install_options(options.rm_deps);

    ArtifactPackageIdentitySet identity_set =
            query_artifact_package_identities(artifacts);
    PackageBaseArtifactIdentitySelectionResult selection =
            correlate_package_base_artifact_identities(
                    package_base, required_targets, identity_set);
    if(!selection.is_success()) {
        if(selection.success() != nullptr || selection.failure() == nullptr) {
            throw_incoherent_package_base_install();
        }
        PackageBaseArtifactInstallPreparationFailure failure(
                *selection.failure());
        return PackageBaseArtifactInstallPreparationResult(
                std::move(failure));
    }
    if(selection.success() == nullptr || selection.failure() != nullptr) {
        throw_incoherent_package_base_install();
    }

    const PackageBaseArtifactIdentitySelectionSuccess& selected =
            *selection.success();
    std::vector<ArtifactPackageIdentity> aggregate_identities;
    require_correlated_selection_success(
            package_base, required_targets, artifacts, identity_set,
            selected, aggregate_identities);

    std::vector<InstalledArtifactPolicyState> installed_states;
    installed_states.reserve(selected.selected_artifacts.size());
    {
        // POLICY(#268): preparation全体でfresh local DB sessionは一つだけ。
        // owned stateへ写した後、全reason reductionより前にscopeを閉じる。
        PackageMetadataSession session =
                PackageMetadataSession::open(database_paths);
        for(const CorrelatedSelectedPackageArtifact& artifact :
            selected.selected_artifacts) {
            InstalledPackageQueryResult query_result =
                    session.query_installed_package(
                            artifact.identity.package_name);
            installed_states.push_back(
                    map_installed_artifact_policy_state(
                            artifact.identity, query_result));
        }
    }

    PackageBaseArtifactInstallReasonPolicyInput policy_input;
    policy_input.package_base = package_base;
    policy_input.needed = options.needed;
    policy_input.selected_artifacts.reserve(
            selected.selected_artifacts.size());
    for(std::size_t index = 0;
        index < selected.selected_artifacts.size();
        ++index) {
        const CorrelatedSelectedPackageArtifact& artifact =
                selected.selected_artifacts[index];
        const InstalledArtifactPolicyState& state = installed_states[index];
        policy_input.selected_artifacts.push_back(
                SelectedPackageBaseArtifactInstallReasonPolicyInput{
                        artifact.identity, artifact.desired_reason,
                        state.version_state, state.existing_reason});
    }

    PackageBaseArtifactInstallReasonPlanResult reason_plan =
            resolve_package_base_artifact_install_reason_plan(policy_input);
    if(!reason_plan.is_success()) {
        if(reason_plan.success() != nullptr || reason_plan.failure() == nullptr) {
            throw_incoherent_package_base_install();
        }
        artifacts.require_validity();
        PackageBaseArtifactInstallPreparationFailure failure(
                *reason_plan.failure());
        return PackageBaseArtifactInstallPreparationResult(
                std::move(failure));
    }
    if(reason_plan.success() == nullptr || reason_plan.failure() != nullptr) {
        throw_incoherent_package_base_install();
    }
    const PackageBaseArtifactInstallReasonPlan& planned =
            *reason_plan.success();
    if(planned.package_base != package_base ||
       planned.needed != options.needed ||
       planned.selected_artifacts.size() !=
               selected.selected_artifacts.size()) {
        throw_incoherent_package_base_install();
    }

    std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>
            prepared_selected;
    prepared_selected.reserve(selected.selected_artifacts.size());
    for(std::size_t index = 0;
        index < selected.selected_artifacts.size();
        ++index) {
        const CorrelatedSelectedPackageArtifact& artifact =
                selected.selected_artifacts[index];
        const PlannedPackageBaseArtifactInstallReason& artifact_plan =
                planned.selected_artifacts[index];
        if(!same_identity(artifact.identity, artifact_plan.identity) ||
           artifact.desired_reason != artifact_plan.desired_reason) {
            throw_incoherent_package_base_install();
        }
        prepared_selected.push_back(
                PreparedPackageBaseArtifactInstallSelectedArtifact{
                        artifact.artifact_index,
                        artifact.identity,
                        artifact.desired_reason,
                        artifact_plan.installed_version_state,
                        artifact_plan.existing_reason,
                        artifact_plan.directive,
                        artifact_plan.expected_outcome});
    }

    std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>
            prepared_unselected;
    prepared_unselected.reserve(selected.unselected_artifacts.size());
    for(const CorrelatedUnselectedPackageArtifact& artifact :
        selected.unselected_artifacts) {
        prepared_unselected.push_back(
                PreparedPackageBaseArtifactInstallUnselectedArtifact{
                        artifact.artifact_index, artifact.identity});
    }

    std::string owned_package_base = package_base;
    require_install_draft_coherence(
            owned_package_base, artifacts, aggregate_identities,
            prepared_selected, prepared_unselected,
            planned.transaction_directive, options.needed);

    // 全throw可能処理とaggregate-wide再証明を終えた後のnoexcept commitだけで
    // caller-owned artifact setをprepared capabilityへ移す。
    PreparedPackageBaseArtifactInstall prepared(
            std::move(owned_package_base), std::move(artifacts),
            std::move(aggregate_identities), std::move(prepared_selected),
            std::move(prepared_unselected),
            planned.transaction_directive, options.needed);
    return PackageBaseArtifactInstallPreparationResult(std::move(prepared));
}

PackageBaseArtifactInstallExecutionResult::
        PackageBaseArtifactInstallExecutionResult(
                std::string package_base,
                std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
                        selected_artifacts) noexcept
    : package_base_(std::move(package_base)),
      selected_artifacts_(std::move(selected_artifacts)) {
}

const std::string&
PackageBaseArtifactInstallExecutionResult::package_base() const noexcept {
    return package_base_;
}

const std::vector<PackageBaseArtifactInstallExecutionArtifactResult>&
PackageBaseArtifactInstallExecutionResult::selected_artifacts()
        const noexcept {
    return selected_artifacts_;
}

bool PackageBaseArtifactInstallExecutionResult::is_success() const noexcept {
    return is_success_;
}

PackageBaseArtifactInstallTransactionError::
        PackageBaseArtifactInstallTransactionError(
                PackageBaseArtifactInstallTransactionFailureKind failure_kind,
                std::string package_base,
                std::vector<PackageBaseArtifactInstallTransactionAttempt>
                        attempts,
                std::optional<int> exit_code,
                const std::string& diagnostic)
    : std::runtime_error(diagnostic), failure_kind_(failure_kind),
      package_base_(std::move(package_base)), attempts_(std::move(attempts)),
      exit_code_(exit_code) {
}

PackageBaseArtifactInstallTransactionFailureKind
PackageBaseArtifactInstallTransactionError::failure_kind() const noexcept {
    return failure_kind_;
}

const std::string&
PackageBaseArtifactInstallTransactionError::package_base() const noexcept {
    return package_base_;
}

const std::vector<PackageBaseArtifactInstallTransactionAttempt>&
PackageBaseArtifactInstallTransactionError::attempts() const noexcept {
    return attempts_;
}

const std::optional<int>&
PackageBaseArtifactInstallTransactionError::exit_code() const noexcept {
    return exit_code_;
}

const std::optional<ProductionSourceBuildStagedOutcome>&
PackageBaseArtifactInstallTransactionError::production_outcome()
        const noexcept {
    return production_outcome_;
}

void PackageBaseArtifactInstallTransactionError::attach_production_outcome(
        ProductionSourceBuildStagedOutcome production_outcome) {
    if(production_outcome_.has_value()) {
        throw std::logic_error(
                "Package transaction failure already has a production outcome.");
    }
    production_outcome_.emplace(std::move(production_outcome));
}

std::vector<PackageBaseArtifactInstallTransactionAttempt>
PackageBaseArtifactInstallTransactionError::release_attempts() && noexcept {
    return std::move(attempts_);
}

std::string
PackageBaseArtifactInstallExecutionResult::release_package_base()
        && noexcept {
    return std::move(package_base_);
}

std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
PackageBaseArtifactInstallExecutionResult::release_selected_artifacts()
        && noexcept {
    return std::move(selected_artifacts_);
}

PackageBaseArtifactInstallExecutionResult
execute_prepared_package_base_artifact_install(
        PreparedPackageBaseArtifactInstall& install,
        const ArtifactInstallExecutionOptions& options) {
    install.require_active_for_execution();
    try {
        install.require_execution_coherence();
    } catch(const std::exception&) {
        // package-controlled artifact/workspace pathをpublic diagnosticへ出さない。
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install failed validation before transaction.",
                "PackageBase"));
    }

    std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
            artifact_results;
    artifact_results.reserve(install.selected_artifacts_.size());
    std::vector<PackageBaseArtifactInstallTransactionAttempt>
            transaction_attempts;
    transaction_attempts.reserve(install.selected_artifacts_.size());
    for(const PreparedPackageBaseArtifactInstallSelectedArtifact& artifact :
        install.selected_artifacts_) {
        artifact_results.push_back(
                PackageBaseArtifactInstallExecutionArtifactResult{
                        artifact.artifact_index, artifact.identity,
                        artifact.desired_reason, artifact.expected_outcome});
        transaction_attempts.push_back(
                PackageBaseArtifactInstallTransactionAttempt{
                        artifact.identity, artifact.desired_reason});
    }
    PackageBaseArtifactInstallExecutionResult execution_result(
            install.package_base_, std::move(artifact_results));
    std::string transaction_package_base = install.package_base_;

    std::vector<std::string> arguments;
    arguments.reserve(7 + install.selected_artifacts_.size());
    arguments.emplace_back("sudo");
    arguments.emplace_back("pacman");
    arguments.emplace_back("-U");
    if(options.no_confirm) arguments.emplace_back("--noconfirm");
    if(install.needed_) arguments.emplace_back("--needed");
    switch(install.transaction_directive_) {
    case InstallReasonDirective::Default:
        break;
    case InstallReasonDirective::AsExplicit:
        arguments.emplace_back("--asexplicit");
        break;
    case InstallReasonDirective::AsDependency:
        arguments.emplace_back("--asdeps");
        break;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install has an unknown transaction directive.",
                "PackageBase"));
    }
    arguments.emplace_back("--");
    for(const PreparedPackageBaseArtifactInstallSelectedArtifact& artifact :
        install.selected_artifacts_) {
        arguments.push_back(
                install.artifacts_.path_at(artifact.artifact_index).string());
    }
    std::string command = shell_words::join(arguments);

    try {
        // command構築中のreplacementも、mutation capabilityをconsumeする直前に拒否する。
        install.require_execution_coherence();
    } catch(const std::exception&) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "Prepared {} artifact install failed validation before transaction.",
                "PackageBase"));
    }

    // LANDMINE(#268): process boundaryがthrow/nonzeroでも再実行を許さない。
    install.state_ = PreparedPackageBaseArtifactInstall::State::Consumed;
    int exit_code = 0;
    try {
        exit_code = run_command(command);
    } catch(const std::exception&) {
        throw PackageBaseArtifactInstallTransactionError(
                PackageBaseArtifactInstallTransactionFailureKind::
                        ProcessException,
                std::move(transaction_package_base),
                std::move(transaction_attempts), std::nullopt,
                localization::format_translated_message(
                        // TRANSLATORS: {} is the literal command "pacman -U".
                        "The {} transaction execution threw an exception.",
                        "pacman -U"));
    } catch(...) {
        throw PackageBaseArtifactInstallTransactionError(
                PackageBaseArtifactInstallTransactionFailureKind::
                        UnknownException,
                std::move(transaction_package_base),
                std::move(transaction_attempts), std::nullopt,
                localization::format_translated_message(
                        // TRANSLATORS: {} is the literal command "pacman -U".
                        "The {} transaction execution failed with an unknown exception.",
                        "pacman -U"));
    }
    if(exit_code != 0) {
        throw PackageBaseArtifactInstallTransactionError(
                PackageBaseArtifactInstallTransactionFailureKind::
                        NonzeroExit,
                std::move(transaction_package_base),
                std::move(transaction_attempts), exit_code,
                localization::format_translated_message(
                        // TRANSLATORS: The first placeholder is the literal
                        // command "pacman -U"; the second is its exit code.
                        "{} failed with exit code {}.", "pacman -U",
                        exit_code));
    }
    return execution_result;
}
