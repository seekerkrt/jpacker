#include "local_source_build.hpp"

#include "build_plan_artifact_target_projection.hpp"
#include "local_source_workspace.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

// LocalSourceWorkspaceのretained directoryを、既存makepkg contextへ渡すだけの
// narrow bridge。raw descriptorやmutable path authorityは公開しない。
struct LocalSourceBuildAccess final {
    static const ValidatedCachePath& validated_cache_path(
            const LocalSourceWorkspace& workspace) noexcept {
        return workspace.directory_.path();
    }
};

namespace {

struct PreparedLocalBuildUnit {
    std::string                                package_base;
    std::vector<RequiredPackageArtifactTarget> required_targets;
};

struct CompletedLocalSourceBuild {
    PackageBaseArtifactIdentitySelectionSuccess selection;
    ValidatedPackageArtifactSet                  artifacts;
};

std::string diagnostic_with_cause(
        const char* token, const std::exception& error) {
    return std::string(token) + ": " + error.what();
}

std::string retained_artifact_diagnostic(
        const char* token, const std::filesystem::path& workspace_path) {
    return std::string(token) + ": retained artifact workspace " +
           workspace_path.string();
}

std::string retained_artifact_diagnostic(
        const char* token, const std::exception& error,
        const std::filesystem::path& workspace_path) {
    return diagnostic_with_cause(token, error) +
           "; retained artifact workspace " + workspace_path.string();
}

PreparedLocalBuildUnit require_local_build_unit(
        const LocalSourceBuildRequest& request) {
    require_unclaimed_artifact_pkgdest(request.source_environment);
    if(!request.source_environment.ordered_assignments.empty()) {
        throw std::runtime_error(
                "local-source-build-environment-is-not-bound-to-metadata");
    }

    const LocalSourceMetadataSnapshot& source_metadata =
            request.source_root.metadata();
    const LocalPackageMetadataParseResult* source_parse_result =
            source_metadata.parse_result();
    if(source_metadata.state() !=
               LocalSourceMetadataState::UsableUnverified ||
       source_parse_result == nullptr ||
       !source_parse_result->is_success() ||
       source_parse_result->metadata() == nullptr ||
       source_parse_result->failure() != nullptr) {
        throw std::runtime_error("local-source-metadata-is-not-buildable");
    }

    const LocalPackageMetadata& metadata =
            request.build_plan.local_metadata();
    if(*source_parse_result->metadata() != metadata) {
        throw std::runtime_error("local-source-build-plan-metadata-mismatch");
    }
    if(!request.build_plan.failures().empty()) {
        throw std::runtime_error("local-build-plan-has-local-failures");
    }

    const BuildPlan& plan = request.build_plan.build_plan();
    require_executable_build_plan(metadata.package_base, plan);

    BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success() || projection.success() == nullptr ||
       projection.failure() != nullptr) {
        throw std::runtime_error("local-build-artifact-target-projection-failed");
    }

    const ProjectedBuildPlanArtifactTargets* local_unit = nullptr;
    for(const auto& unit : projection.success()->build_units) {
        if(unit.package_base != metadata.package_base) continue;
        if(local_unit != nullptr) {
            throw std::runtime_error("local-build-unit-is-not-unique");
        }
        local_unit = &unit;
    }
    if(local_unit == nullptr) {
        throw std::runtime_error("local-build-unit-is-missing");
    }
    if(local_unit->required_targets.size() != metadata.children.size()) {
        throw std::runtime_error("local-build-required-child-count-mismatch");
    }

    for(std::size_t index = 0; index < metadata.children.size(); ++index) {
        const LocalPackageMetadataChild& child = metadata.children[index];
        const RequiredPackageArtifactTarget& target =
                local_unit->required_targets[index];
        if(target.package_base != metadata.package_base ||
           target.package_name != child.name ||
           target.desired_reason != DesiredInstallReason::Explicit) {
            throw std::runtime_error("local-build-required-child-mismatch");
        }
    }

    return PreparedLocalBuildUnit{
            metadata.package_base, local_unit->required_targets};
}

void require_source_command_precondition(
        const LocalSourceBuildRequest& request,
        const LocalSourceWorkspace& source_workspace) {
    request.source_root.require_unchanged_identity();
    source_workspace.require_unchanged_identity();
}

ValidatedPackageArtifactSet build_and_validate_local_artifacts(
        const LocalSourceBuildRequest& request,
        const LocalSourceWorkspace& source_workspace) {
    ArtifactWorkspace workspace = [&]() {
        try {
            ValidatedPrivateCacheRoot artifact_root =
                    prepare_private_trusted_cache_root(request.cache_root);
            return create_artifact_workspace(std::move(artifact_root));
        } catch(const std::exception& error) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactWorkspace,
                    diagnostic_with_cause(
                            "local-source-artifact-workspace-failed", error));
        } catch(...) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactWorkspace,
                    "local-source-artifact-workspace-failed");
        }
    }();

    ArtifactMakepkgContext makepkg_context = [&]() {
        try {
            return prepare_artifact_makepkg_context(
                    LocalSourceBuildAccess::validated_cache_path(
                            source_workspace),
                    workspace, request.source_environment,
                    request.empty_value_policy);
        } catch(const std::exception& error) {
            workspace.retain_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::BuildContext,
                    retained_artifact_diagnostic(
                            "local-source-build-context-failed", error,
                            workspace.path()));
        } catch(...) {
            workspace.retain_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::BuildContext,
                    retained_artifact_diagnostic(
                            "local-source-build-context-failed",
                            workspace.path()));
        }
    }();

    try {
        require_source_command_precondition(request, source_workspace);
    } catch(const LocalSourceRootError& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Packagelist,
                retained_artifact_diagnostic(
                        "local-source-packagelist-precondition-failed",
                        error, workspace.path()),
                std::nullopt, std::nullopt, error.failure());
    } catch(const LocalSourceWorkspaceError& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Packagelist,
                retained_artifact_diagnostic(
                        "local-source-packagelist-precondition-failed",
                        error, workspace.path()),
                std::nullopt, std::nullopt, std::nullopt,
                error.failure());
    } catch(const std::exception& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Packagelist,
                retained_artifact_diagnostic(
                        "local-source-packagelist-precondition-failed",
                        error, workspace.path()));
    } catch(...) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Packagelist,
                retained_artifact_diagnostic(
                        "local-source-packagelist-precondition-failed",
                        workspace.path()));
    }

    ExpectedPackageArtifactSet expected = [&]() {
        try {
            return query_makepkg_packagelist_set(workspace, makepkg_context);
        } catch(const std::exception& error) {
            workspace.retain_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::Packagelist,
                    retained_artifact_diagnostic(
                            "local-source-packagelist-failed", error,
                            workspace.path()));
        } catch(...) {
            workspace.retain_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::Packagelist,
                    retained_artifact_diagnostic(
                            "local-source-packagelist-failed",
                            workspace.path()));
        }
    }();

    try {
        require_source_command_precondition(request, source_workspace);
    } catch(const LocalSourceRootError& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-precondition-failed", error,
                        workspace.path()),
                std::nullopt, std::nullopt, error.failure());
    } catch(const LocalSourceWorkspaceError& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-precondition-failed", error,
                        workspace.path()),
                std::nullopt, std::nullopt, std::nullopt,
                error.failure());
    } catch(const std::exception& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-precondition-failed", error,
                        workspace.path()));
    } catch(...) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-precondition-failed",
                        workspace.path()));
    }

    int build_exit_code = 0;
    try {
        build_exit_code = makepkg_context.run_makepkg_build_only(
                workspace, expected, request.makepkg_options);
    } catch(const std::exception& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-command-failed", error,
                        workspace.path()));
    } catch(...) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-command-failed", workspace.path()));
    }
    if(build_exit_code != 0) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::Build,
                retained_artifact_diagnostic(
                        "local-source-build-command-returned-nonzero",
                        workspace.path()),
                build_exit_code);
    }

    try {
        return validate_post_build_package_artifacts(
                std::move(workspace), expected);
    } catch(const std::exception& error) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactValidation,
                retained_artifact_diagnostic(
                        "local-source-artifact-validation-failed", error,
                        workspace.path()));
    } catch(...) {
        workspace.retain_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactValidation,
                retained_artifact_diagnostic(
                        "local-source-artifact-validation-failed",
                        workspace.path()));
    }
}

PackageBaseArtifactIdentitySelectionSuccess select_local_artifacts(
        const PreparedLocalBuildUnit& unit,
        ValidatedPackageArtifactSet& artifacts) {
    ArtifactPackageIdentitySet identities = [&]() {
        try {
            return query_artifact_package_identities(artifacts);
        } catch(const std::exception& error) {
            artifacts.retain_workspace_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactIdentity,
                    retained_artifact_diagnostic(
                            "local-source-artifact-identity-failed", error,
                            artifacts.workspace_path()));
        } catch(...) {
            artifacts.retain_workspace_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactIdentity,
                    retained_artifact_diagnostic(
                            "local-source-artifact-identity-failed",
                            artifacts.workspace_path()));
        }
    }();

    PackageBaseArtifactIdentitySelectionResult selection = [&]() {
        try {
            return correlate_package_base_artifact_identities(
                    unit.package_base, unit.required_targets, identities);
        } catch(const std::exception& error) {
            artifacts.retain_workspace_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactSelection,
                    retained_artifact_diagnostic(
                            "local-source-artifact-selection-failed", error,
                            artifacts.workspace_path()));
        } catch(...) {
            artifacts.retain_workspace_for_diagnostics();
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactSelection,
                    retained_artifact_diagnostic(
                            "local-source-artifact-selection-failed",
                            artifacts.workspace_path()));
        }
    }();

    if(!selection.is_success() || selection.success() == nullptr) {
        artifacts.retain_workspace_for_diagnostics();
        if(selection.failure() == nullptr) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::ArtifactSelection,
                    retained_artifact_diagnostic(
                            "local-source-artifact-selection-incoherent",
                            artifacts.workspace_path()));
        }
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactSelection,
                retained_artifact_diagnostic(
                        "local-source-artifact-selection-rejected",
                        artifacts.workspace_path()),
                std::nullopt, *selection.failure());
    }
    if(selection.failure() != nullptr) {
        artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactSelection,
                retained_artifact_diagnostic(
                        "local-source-artifact-selection-incoherent",
                        artifacts.workspace_path()));
    }

    try {
        return *selection.success();
    } catch(const std::exception& error) {
        artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactSelection,
                retained_artifact_diagnostic(
                        "local-source-artifact-selection-snapshot-failed",
                        error, artifacts.workspace_path()));
    } catch(...) {
        artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::ArtifactSelection,
                retained_artifact_diagnostic(
                        "local-source-artifact-selection-snapshot-failed",
                        artifacts.workspace_path()));
    }
}

} // namespace

LocalSourceBuildPhaseError::LocalSourceBuildPhaseError(
        LocalSourceBuildFailurePhase phase, const std::string& diagnostic,
        std::optional<int> build_exit_code,
        std::optional<PackageBaseArtifactIdentitySelectionFailure>
                selection_failure,
        std::optional<LocalSourceRootFailure> source_root_failure,
        std::optional<LocalSourceWorkspaceFailure>
                source_workspace_failure,
        std::optional<LocalSourceWorkspaceFailure>
                source_cleanup_failure)
    : std::runtime_error(diagnostic), phase_(phase),
      build_exit_code_(build_exit_code),
      selection_failure_(std::move(selection_failure)),
      source_root_failure_(std::move(source_root_failure)),
      source_workspace_failure_(std::move(source_workspace_failure)),
      source_cleanup_failure_(std::move(source_cleanup_failure)) {}

LocalSourceBuildPhaseError::LocalSourceBuildPhaseError(
        const LocalSourceBuildPhaseError& primary,
        LocalSourceWorkspaceFailure source_cleanup_failure)
    : std::runtime_error(primary.what()), phase_(primary.phase_),
      build_exit_code_(primary.build_exit_code_),
      selection_failure_(primary.selection_failure_),
      source_root_failure_(primary.source_root_failure_),
      source_workspace_failure_(primary.source_workspace_failure_),
      source_cleanup_failure_(std::move(source_cleanup_failure)) {}

LocalSourceBuildFailurePhase LocalSourceBuildPhaseError::phase()
        const noexcept {
    return phase_;
}

std::optional<int> LocalSourceBuildPhaseError::build_exit_code()
        const noexcept {
    return build_exit_code_;
}

const PackageBaseArtifactIdentitySelectionFailure*
LocalSourceBuildPhaseError::selection_failure() const noexcept {
    return selection_failure_.has_value() ? &selection_failure_.value()
                                          : nullptr;
}

const LocalSourceRootFailure*
LocalSourceBuildPhaseError::source_root_failure() const noexcept {
    return source_root_failure_.has_value() ? &source_root_failure_.value()
                                            : nullptr;
}

const LocalSourceWorkspaceFailure*
LocalSourceBuildPhaseError::source_workspace_failure() const noexcept {
    return source_workspace_failure_.has_value()
                   ? &source_workspace_failure_.value()
                   : nullptr;
}

const LocalSourceWorkspaceFailure*
LocalSourceBuildPhaseError::source_cleanup_failure() const noexcept {
    return source_cleanup_failure_.has_value()
                   ? &source_cleanup_failure_.value()
                   : nullptr;
}

LocalSourceBuildResult::LocalSourceBuildResult(
        PackageBaseArtifactIdentitySelectionSuccess selection,
        ValidatedPackageArtifactSet artifacts) noexcept
    : selection_(std::move(selection)), artifacts_(std::move(artifacts)) {}

const std::string& LocalSourceBuildResult::package_base() const noexcept {
    return selection_.package_base;
}

const std::vector<CorrelatedSelectedPackageArtifact>&
LocalSourceBuildResult::selected_artifacts() const noexcept {
    return selection_.selected_artifacts;
}

const std::vector<CorrelatedUnselectedPackageArtifact>&
LocalSourceBuildResult::unselected_artifacts() const noexcept {
    return selection_.unselected_artifacts;
}

void LocalSourceBuildResult::cleanup_artifacts() {
    artifacts_.cleanup_workspace();
}

LocalSourceBuildResult execute_local_source_build(
        LocalSourceBuildRequest request) {
    PreparedLocalBuildUnit unit = [&]() {
        try {
            return require_local_build_unit(request);
        } catch(const std::exception& error) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::Preflight,
                    diagnostic_with_cause(
                            "local-source-build-preflight-failed", error));
        } catch(...) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::Preflight,
                    "local-source-build-preflight-failed");
        }
    }();

    LocalSourceWorkspace source_workspace = [&]() {
        try {
            return materialize_local_source_workspace(
                    request.source_root, request.cache_root);
        } catch(const LocalSourceWorkspaceError& error) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::SourceWorkspace,
                    diagnostic_with_cause(
                            "local-source-workspace-failed", error),
                    std::nullopt, std::nullopt, std::nullopt,
                    error.failure());
        } catch(const std::exception& error) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::SourceWorkspace,
                    diagnostic_with_cause(
                            "local-source-workspace-failed", error));
        } catch(...) {
            throw LocalSourceBuildPhaseError(
                    LocalSourceBuildFailurePhase::SourceWorkspace,
                    "local-source-workspace-failed");
        }
    }();

    CompletedLocalSourceBuild completed = [&]() {
        try {
            ValidatedPackageArtifactSet artifacts =
                    build_and_validate_local_artifacts(
                            request, source_workspace);
            PackageBaseArtifactIdentitySelectionSuccess selection =
                    select_local_artifacts(unit, artifacts);
            return CompletedLocalSourceBuild{
                    std::move(selection), std::move(artifacts)};
        } catch(const LocalSourceBuildPhaseError& primary) {
            try {
                source_workspace.cleanup();
            } catch(const LocalSourceWorkspaceError& cleanup_error) {
                throw LocalSourceBuildPhaseError(
                        primary, cleanup_error.failure());
            } catch(...) {
                throw LocalSourceBuildPhaseError(
                        primary,
                        LocalSourceWorkspaceFailure{
                                LocalSourceWorkspaceStage::Cleanup,
                                LocalSourceWorkspaceErrorCode::CleanupFailure,
                                {}, std::nullopt});
            }
            throw;
        }
    }();

    try {
        source_workspace.cleanup();
    } catch(const LocalSourceWorkspaceError& error) {
        completed.artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::SourceCleanup,
                retained_artifact_diagnostic(
                        "local-source-workspace-cleanup-failed", error,
                        completed.artifacts.workspace_path()),
                std::nullopt, std::nullopt, std::nullopt,
                error.failure());
    } catch(const std::exception& error) {
        completed.artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::SourceCleanup,
                retained_artifact_diagnostic(
                        "local-source-workspace-cleanup-failed", error,
                        completed.artifacts.workspace_path()));
    } catch(...) {
        completed.artifacts.retain_workspace_for_diagnostics();
        throw LocalSourceBuildPhaseError(
                LocalSourceBuildFailurePhase::SourceCleanup,
                retained_artifact_diagnostic(
                        "local-source-workspace-cleanup-failed",
                        completed.artifacts.workspace_path()));
    }

    return LocalSourceBuildResult(
            std::move(completed.selection),
            std::move(completed.artifacts));
}
