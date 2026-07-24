#include "separated_source_build.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

#ifdef JPACKER_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
SeparatedSourceBuildWorkspaceObserverForTest g_workspace_observer = nullptr;

void notify_workspace_created_for_test(
        const std::filesystem::path& workspace_path) {
    if(g_workspace_observer != nullptr) g_workspace_observer(workspace_path);
}
#else
void notify_workspace_created_for_test(const std::filesystem::path&) {
}
#endif

} // namespace

SeparatedSourceBuildCleanupError::SeparatedSourceBuildCleanupError(
        const std::string& diagnostic)
    : std::runtime_error(diagnostic) {
}

void execute_separated_source_build_unit(
        SeparatedSourceBuildUnitRequest request,
        const SeparatedSourceBuildUnitOptions& options) {
    // POLICY(#242): build unit最初のmutationより前に、caller-controlled optionと
    // PKGDEST ownership conflictをempty valueも含めて確定する。
    require_supported_separated_install_options(options.rm_deps);
    require_unclaimed_artifact_pkgdest(request.source_environment);

    ArtifactWorkspace workspace = create_artifact_workspace(
            std::move(request.artifact_root));
    notify_workspace_created_for_test(workspace.path());

    ArtifactMakepkgContext makepkg_context =
            prepare_artifact_makepkg_context(
                    request.checkout, workspace, request.source_environment,
                    request.empty_value_policy);
    ExpectedPackageArtifactPath expected = query_makepkg_packagelist(
            workspace, makepkg_context);

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
        // run_makepkg_build_only()はcommand前後の再検証でもthrowし得る。
        // build phaseへ入った後のfailureはdiagnostic workspaceを残す。
        workspace.retain_for_diagnostics();
        throw;
    }

    // LANDMINE(#242): status判定やfilesystem validationより先に保持へ遷移する。
    // ここから先のfailureは、生成済みartifactを自動cleanupしてはならない。
    workspace.retain_for_diagnostics();
    if(build_exit_code != 0) {
        throw std::runtime_error(
                "Build-only makepkg failed with exit code " +
                std::to_string(build_exit_code) + ".");
    }

    ValidatedPackageArtifactPath artifact =
            validate_post_build_package_artifact(
                    std::move(workspace), expected);
    PreparedArtifactInstall prepared = prepare_artifact_install(
            artifact, request.requested_name, request.package_base,
            request.desired_reason,
            ArtifactInstallPreparationOptions{
                    options.needed, options.rm_deps},
            request.database_paths);
    execute_prepared_artifact_install(
            prepared,
            ArtifactInstallExecutionOptions{options.no_confirm});

    try {
        // POLICY(#242): transaction成功後だけcleanup ownershipを明示的に行使する。
        prepared.cleanup_workspace();
    } catch(const std::exception& error) {
        throw SeparatedSourceBuildCleanupError(
                "Package installation succeeded, but artifact workspace cleanup failed: " +
                std::string(error.what()));
    } catch(...) {
        throw SeparatedSourceBuildCleanupError(
                "Package installation succeeded, but artifact workspace cleanup failed with an unknown error.");
    }
}

#ifdef JPACKER_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
void set_separated_source_build_workspace_observer_for_test(
        SeparatedSourceBuildWorkspaceObserverForTest observer) {
    g_workspace_observer = observer;
}
#endif
