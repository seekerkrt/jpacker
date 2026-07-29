#include "separated_package_base_source_build.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

#ifdef MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
SeparatedPackageBaseSourceBuildWorkspaceObserverForTest
        g_workspace_observer = nullptr;

void notify_workspace_created_for_test(
        const std::filesystem::path& workspace_path) {
    if(g_workspace_observer != nullptr) g_workspace_observer(workspace_path);
}
#else
void notify_workspace_created_for_test(const std::filesystem::path&) {
}
#endif

void require_valid_set_request(
        const SeparatedPackageBaseSourceBuildRequest& request) {
    require_valid_package_name(request.package_base);
    if(request.required_targets.empty()) {
        throw std::logic_error(
                "Separated PackageBase source-build requires at least one required package target.");
    }

    for(std::size_t index = 0; index < request.required_targets.size();
        ++index) {
        const RequiredPackageArtifactTarget& target =
                request.required_targets[index];
        require_valid_package_name(target.package_base);
        require_valid_package_name(target.package_name);
        if(target.package_base != request.package_base) {
            throw std::logic_error(
                    "Separated PackageBase source-build required target has a mismatched PackageBase.");
        }
        if(std::any_of(
                   request.required_targets.begin(),
                   request.required_targets.begin() + index,
                   [&target](
                           const RequiredPackageArtifactTarget& existing) {
                       return existing.package_name == target.package_name;
                   })) {
            throw std::logic_error(
                    "Separated PackageBase source-build contains a duplicate required package target.");
        }
        switch(target.desired_reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            break;
        default:
            throw std::logic_error(
                    "Separated PackageBase source-build has an unknown install reason.");
        }
    }
}

ArtifactInstallExecutionOutcome execution_outcome(
        PackageBaseArtifactInstallExpectedOutcome outcome) noexcept {
    switch(outcome) {
    case PackageBaseArtifactInstallExpectedOutcome::Installed:
        return ArtifactInstallExecutionOutcome::Installed;
    case PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded:
        return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
    }
    // prepared/executor coherenceはunknown valueをtransaction前に拒否する。
    // 成功後promotionで到達するなら内部bugである。
    std::terminate();
}

struct PackageBaseSourceBuildResultDraft {
    std::vector<ArtifactPackageIdentity> unselected;
    // Transaction成功後のpromotionでallocationを起こさないための空capacity。
    std::vector<PackageBaseSourceBuildSelectedResult> promoted_selected;
    std::vector<ArtifactPackageIdentity> promoted_unselected;
};

PackageBaseSourceBuildResultDraft snapshot_result_draft(
        const PreparedPackageBaseArtifactInstall& prepared) {
    PackageBaseSourceBuildResultDraft draft;
    draft.promoted_selected.reserve(prepared.selected_artifacts().size());
    draft.unselected.reserve(prepared.unselected_artifacts().size());
    draft.promoted_unselected.reserve(
            prepared.unselected_artifacts().size());
    for(const auto& unselected : prepared.unselected_artifacts()) {
        draft.unselected.push_back(unselected.identity);
    }
    return draft;
}

void promote_successful_result(
        PackageBaseSourceBuildResultDraft& draft,
        std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
                executed_selected) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<
                  ArtifactPackageIdentity>);
    static_assert(std::is_nothrow_move_constructible_v<
                  PackageBaseSourceBuildSelectedResult>);

    for(auto& selected : executed_selected) {
        draft.promoted_selected.push_back(
                PackageBaseSourceBuildSelectedResult{
                        std::move(selected.identity),
                        selected.desired_reason,
                        execution_outcome(selected.outcome)});
    }
    for(auto& unselected : draft.unselected) {
        draft.promoted_unselected.push_back(std::move(unselected));
    }
}

std::string preparation_failure_diagnostic(
        const PackageBaseArtifactInstallPreparationFailure& failure) {
    if(failure.selection_failure() != nullptr) {
        return "PackageBase artifact selection failed before package transaction.";
    }
    if(failure.mixed_reason_failure() != nullptr) {
        return "PackageBase artifact install reasons cannot be represented by one package transaction.";
    }
    throw std::logic_error(
            "Unknown PackageBase artifact install preparation failure.");
}

} // namespace

PackageBaseSourceBuildExecutionResult::
        PackageBaseSourceBuildExecutionResult(
                std::string package_base,
                std::vector<PackageBaseSourceBuildSelectedResult>
                        selected_children,
                std::vector<ArtifactPackageIdentity> unselected_artifacts)
        noexcept
    : package_base_(std::move(package_base)),
      selected_children_(std::move(selected_children)),
      unselected_artifacts_(std::move(unselected_artifacts)) {
}

const std::string&
PackageBaseSourceBuildExecutionResult::package_base() const noexcept {
    return package_base_;
}

const std::vector<PackageBaseSourceBuildSelectedResult>&
PackageBaseSourceBuildExecutionResult::selected_children() const noexcept {
    return selected_children_;
}

const std::vector<ArtifactPackageIdentity>&
PackageBaseSourceBuildExecutionResult::unselected_artifacts() const noexcept {
    return unselected_artifacts_;
}

bool PackageBaseSourceBuildExecutionResult::installed_any() const noexcept {
    return std::any_of(
            selected_children_.begin(), selected_children_.end(),
            [](const PackageBaseSourceBuildSelectedResult& child) {
                return child.outcome ==
                        ArtifactInstallExecutionOutcome::Installed;
            });
}

bool PackageBaseSourceBuildExecutionResult::all_skipped_as_needed()
        const noexcept {
    return !selected_children_.empty() && std::all_of(
            selected_children_.begin(), selected_children_.end(),
            [](const PackageBaseSourceBuildSelectedResult& child) {
                return child.outcome ==
                        ArtifactInstallExecutionOutcome::SkippedAsNeeded;
            });
}

std::string
PackageBaseSourceBuildExecutionResult::release_package_base() && noexcept {
    return std::move(package_base_);
}

std::vector<PackageBaseSourceBuildSelectedResult>
PackageBaseSourceBuildExecutionResult::release_selected_children() && noexcept {
    return std::move(selected_children_);
}

std::vector<ArtifactPackageIdentity>
PackageBaseSourceBuildExecutionResult::release_unselected_artifacts()
        && noexcept {
    return std::move(unselected_artifacts_);
}

SeparatedPackageBaseSourceBuildPhaseError::
        SeparatedPackageBaseSourceBuildPhaseError(
                SeparatedPackageBaseSourceBuildFailurePhase phase,
                const std::string& diagnostic)
    : std::runtime_error(diagnostic), phase_(phase) {
}

SeparatedPackageBaseSourceBuildFailurePhase
SeparatedPackageBaseSourceBuildPhaseError::phase() const noexcept {
    return phase_;
}

SeparatedPackageBaseSourceBuildPreparationError::
        SeparatedPackageBaseSourceBuildPreparationError(
                PackageBaseArtifactInstallPreparationFailure failure,
                const std::string& diagnostic)
    : std::runtime_error(diagnostic), failure_(std::move(failure)) {
}

const PackageBaseArtifactInstallPreparationFailure&
SeparatedPackageBaseSourceBuildPreparationError::failure() const noexcept {
    return failure_;
}

const PackageBaseArtifactIdentitySelectionFailure*
SeparatedPackageBaseSourceBuildPreparationError::selection_failure()
        const noexcept {
    return failure_.selection_failure();
}

const MixedPackageBaseInstallReasonUnsupported*
SeparatedPackageBaseSourceBuildPreparationError::mixed_reason_failure()
        const noexcept {
    return failure_.mixed_reason_failure();
}

SeparatedPackageBaseSourceBuildCleanupError::
        SeparatedPackageBaseSourceBuildCleanupError(
                PackageBaseSourceBuildExecutionResult result,
                const std::string& diagnostic)
    : std::runtime_error(diagnostic), result_(std::move(result)) {
}

const PackageBaseSourceBuildExecutionResult&
SeparatedPackageBaseSourceBuildCleanupError::result() const noexcept {
    return result_;
}

PackageBaseSourceBuildExecutionResult
SeparatedPackageBaseSourceBuildCleanupError::release_result() && noexcept {
    return std::move(result_);
}

PackageBaseSourceBuildExecutionResult
execute_separated_package_base_source_build(
        SeparatedPackageBaseSourceBuildRequest request,
        const SeparatedSourceBuildUnitOptions& options) {
    // POLICY(#268): caller-controlled policy/identityを最初のworkspace mutation前に
    // 全件確定し、duplicate/mismatch/unknown reasonをselectionまで遅らせない。
    require_supported_separated_install_options(options.rm_deps);
    require_unclaimed_artifact_pkgdest(request.source_environment);
    require_valid_set_request(request);

    ArtifactWorkspace workspace = create_artifact_workspace(
            std::move(request.artifact_root));
    notify_workspace_created_for_test(workspace.path());

    ArtifactMakepkgContext makepkg_context = [&]() {
        try {
            return prepare_artifact_makepkg_context(
                    request.checkout, workspace,
                    request.source_environment,
                    request.empty_value_policy);
        } catch(...) {
            workspace.retain_for_diagnostics();
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::Build,
                    "PackageBase source-build context preparation failed.");
        }
    }();
    ExpectedPackageArtifactSet expected = [&]() {
        try {
            return query_makepkg_packagelist_set(workspace, makepkg_context);
        } catch(...) {
            workspace.retain_for_diagnostics();
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::Build,
                    "PackageBase makepkg artifact-list query failed.");
        }
    }();

    const ArtifactMakepkgBuildOptions makepkg_options{
            .no_confirm = options.no_confirm,
            .rebuild = options.rebuild,
            .clean_build = options.clean_build,
    };
    int build_exit_code = 0;
    try {
        build_exit_code = makepkg_context.run_makepkg_build_only(
                workspace, expected, makepkg_options);
    } catch(...) {
        workspace.retain_for_diagnostics();
        throw SeparatedPackageBaseSourceBuildPhaseError(
                SeparatedPackageBaseSourceBuildFailurePhase::Build,
                "PackageBase build-only makepkg execution failed.");
    }

    // LANDMINE(#268): command statusやaggregate validationより先にretainする。
    // build phaseへ入った後の全failureはfresh workspaceを診断用に残す。
    workspace.retain_for_diagnostics();
    if(build_exit_code != 0) {
        throw SeparatedPackageBaseSourceBuildPhaseError(
                SeparatedPackageBaseSourceBuildFailurePhase::Build,
                "Build-only makepkg failed with exit code " +
                        std::to_string(build_exit_code) + ".");
    }

    ValidatedPackageArtifactSet artifacts = [&]() {
        try {
            return validate_post_build_package_artifacts(
                    std::move(workspace), expected);
        } catch(...) {
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::
                            ArtifactValidation,
                    "PackageBase artifact aggregate validation failed.");
        }
    }();
    PackageBaseArtifactInstallPreparationResult preparation = [&]() {
        try {
            return prepare_package_base_artifact_install(
                    artifacts, request.package_base,
                    request.required_targets,
                    ArtifactInstallPreparationOptions{
                            options.needed, options.rm_deps},
                    request.database_paths);
        } catch(const PackageMetadataError&) {
            artifacts.retain_workspace_for_diagnostics();
            throw;
        } catch(...) {
            artifacts.retain_workspace_for_diagnostics();
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::
                            ArtifactIdentity,
                    "PackageBase artifact identity or validation failed before install preparation.");
        }
    }();
    if(!preparation.is_prepared()) {
        if(preparation.prepared() != nullptr ||
           preparation.failure() == nullptr) {
            throw std::logic_error(
                    "PackageBase artifact install preparation result is incoherent.");
        }
        artifacts.retain_workspace_for_diagnostics();
        PackageBaseArtifactInstallPreparationFailure failure =
                *preparation.failure();
        const std::string diagnostic =
                preparation_failure_diagnostic(failure);
        throw SeparatedPackageBaseSourceBuildPreparationError(
                std::move(failure), diagnostic);
    }
    if(preparation.prepared() == nullptr ||
       preparation.failure() != nullptr) {
        throw std::logic_error(
                "PackageBase artifact install preparation result is incoherent.");
    }

    PreparedPackageBaseArtifactInstall& prepared =
            *preparation.prepared();
    // Transaction前にはprivate draftとpromotion用capacityだけを確定する。
    // ordinary failureではpublic child success resultを一件も生成しない。
    PackageBaseSourceBuildResultDraft result_draft =
            snapshot_result_draft(prepared);
    PackageBaseArtifactInstallExecutionResult execution_result = [&]() {
        try {
            return execute_prepared_package_base_artifact_install(
                    prepared,
                    ArtifactInstallExecutionOptions{options.no_confirm});
        } catch(const PackageBaseArtifactInstallTransactionError&) {
            throw;
        } catch(...) {
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::
                            ArtifactValidation,
                    "Prepared PackageBase artifact install failed validation before transaction.");
        }
    }();

    // Transaction成功後はallocationなしのmoveだけでpublic resultへ昇格する。
    std::string executed_package_base =
            std::move(execution_result).release_package_base();
    std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
            executed_selected =
                    std::move(execution_result).release_selected_artifacts();
    promote_successful_result(
            result_draft, std::move(executed_selected));
    PackageBaseSourceBuildExecutionResult result(
            std::move(executed_package_base),
            std::move(result_draft.promoted_selected),
            std::move(result_draft.promoted_unselected));

    try {
        // POLICY(#268): transaction成功後だけaggregate cleanupを明示実行する。
        prepared.cleanup_workspace();
    } catch(const std::exception& error) {
        throw SeparatedPackageBaseSourceBuildCleanupError(
                std::move(result),
                "Package installation succeeded, but artifact workspace cleanup failed: " +
                        std::string(error.what()));
    } catch(...) {
        throw SeparatedPackageBaseSourceBuildCleanupError(
                std::move(result),
                "Package installation succeeded, but artifact workspace cleanup failed with an unknown error.");
    }
    return result;
}

#ifdef MOGUET_ENABLE_SEPARATED_PACKAGE_BASE_SOURCE_BUILD_TEST_HOOKS
void set_separated_package_base_source_build_workspace_observer_for_test(
        SeparatedPackageBaseSourceBuildWorkspaceObserverForTest observer) {
    g_workspace_observer = observer;
}
#endif
