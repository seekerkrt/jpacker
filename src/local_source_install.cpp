#include "local_source_install.hpp"

#include "localization.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

ArtifactInstallExecutionOutcome execution_outcome(
        PackageBaseArtifactInstallExpectedOutcome outcome) noexcept {
    switch(outcome) {
    case PackageBaseArtifactInstallExpectedOutcome::Installed:
        return ArtifactInstallExecutionOutcome::Installed;
    case PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded:
        return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
    }
    std::terminate();
}

struct LocalSourceInstallInput {
    std::string package_base;
    std::vector<RequiredPackageArtifactTarget> required_targets;
    ValidatedPackageArtifactSet artifacts;
};

struct LocalSourceInstallResultDraft {
    std::vector<ArtifactPackageIdentity> unselected;
    std::vector<PackageBaseSourceBuildSelectedResult> promoted_selected;
    std::vector<ArtifactPackageIdentity> promoted_unselected;
};

LocalSourceInstallResultDraft snapshot_result_draft(
        const PreparedPackageBaseArtifactInstall& prepared) {
    LocalSourceInstallResultDraft draft;
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
        LocalSourceInstallResultDraft& draft,
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
        return localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "{} artifact selection failed before package transaction.",
                "PackageBase");
    }
    if(failure.mixed_reason_failure() != nullptr) {
        return localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "{} artifact install reasons cannot be represented by one package transaction.",
                "PackageBase");
    }
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal Arch field name
            // "PackageBase".
            "Unknown {} artifact install preparation failure.",
            "PackageBase"));
}

} // namespace

// Narrow shared friend for the two existing closed owners. The public API
// remains the free one-shot composition above.
class LocalSourceInstallAccess final {
public:
    static LocalSourceInstallInput consume(LocalSourceBuildResult result) {
        std::vector<RequiredPackageArtifactTarget> required_targets;
        required_targets.reserve(result.selection_.selected_artifacts.size());
        for(const CorrelatedSelectedPackageArtifact& selected :
            result.selection_.selected_artifacts) {
            required_targets.push_back(RequiredPackageArtifactTarget{
                    result.selection_.package_base,
                    selected.identity.package_name,
                    selected.desired_reason});
        }
        return LocalSourceInstallInput{
                result.selection_.package_base,
                std::move(required_targets),
                std::move(result.artifacts_)};
    }

    static PackageBaseSourceBuildExecutionResult make_result(
            std::string package_base,
            std::vector<PackageBaseSourceBuildSelectedResult>
                    selected_children,
            std::vector<ArtifactPackageIdentity> unselected_artifacts) {
        return PackageBaseSourceBuildExecutionResult(
                std::move(package_base), std::move(selected_children),
                std::move(unselected_artifacts));
    }
};

PackageBaseSourceBuildExecutionResult execute_local_source_install(
        LocalSourceBuildResult result,
        const PacmanDatabasePaths& database_paths,
        const SeparatedSourceBuildUnitOptions& options) {
    require_supported_separated_install_options(options.rm_deps);
    LocalSourceInstallInput input =
            LocalSourceInstallAccess::consume(std::move(result));
    input.artifacts.retain_workspace_for_diagnostics();

    PackageBaseArtifactInstallPreparationResult preparation = [&]() {
        try {
            return prepare_package_base_artifact_install(
                    input.artifacts, input.package_base,
                    input.required_targets,
                    ArtifactInstallPreparationOptions{
                            options.needed, options.rm_deps},
                    database_paths);
        } catch(const PackageMetadataError&) {
            throw;
        } catch(...) {
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::
                            ArtifactIdentity,
                    localization::format_translated_message(
                            // TRANSLATORS: The placeholder is the literal Arch
                            // field name "PackageBase".
                            "{} artifact identity or validation failed before install preparation.",
                            "PackageBase"));
        }
    }();
    if(!preparation.is_prepared()) {
        if(preparation.prepared() != nullptr ||
           preparation.failure() == nullptr) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal Arch field
                    // name "PackageBase".
                    "{} artifact install preparation result is incoherent.",
                    "PackageBase"));
        }
        PackageBaseArtifactInstallPreparationFailure failure =
                *preparation.failure();
        std::string diagnostic = preparation_failure_diagnostic(failure);
        throw SeparatedPackageBaseSourceBuildPreparationError(
                std::move(failure), diagnostic);
    }
    if(preparation.prepared() == nullptr ||
       preparation.failure() != nullptr) {
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal Arch field name
                // "PackageBase".
                "{} artifact install preparation result is incoherent.",
                "PackageBase"));
    }

    PreparedPackageBaseArtifactInstall& prepared = *preparation.prepared();
    LocalSourceInstallResultDraft result_draft =
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
                    localization::format_translated_message(
                            // TRANSLATORS: The placeholder is the literal Arch
                            // field name "PackageBase".
                            "Prepared {} artifact install failed validation before transaction.",
                            "PackageBase"));
        }
    }();

    std::string executed_package_base =
            std::move(execution_result).release_package_base();
    std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
            executed_selected =
                    std::move(execution_result).release_selected_artifacts();
    promote_successful_result(
            result_draft, std::move(executed_selected));
    PackageBaseSourceBuildExecutionResult installed =
            LocalSourceInstallAccess::make_result(
                    std::move(executed_package_base),
                    std::move(result_draft.promoted_selected),
                    std::move(result_draft.promoted_unselected));

    try {
        prepared.cleanup_workspace();
    } catch(const std::exception& error) {
        throw SeparatedPackageBaseSourceBuildCleanupError(
                std::move(installed),
                localization::format_translated_message(
                        "Package installation succeeded, but artifact workspace cleanup failed: {}",
                        error.what()));
    } catch(...) {
        throw SeparatedPackageBaseSourceBuildCleanupError(
                std::move(installed),
                localization::translate_message(
                        "Package installation succeeded, but artifact workspace cleanup failed with an unknown error."));
    }
    return installed;
}
