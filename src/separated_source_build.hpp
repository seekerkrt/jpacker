#pragma once

#include "artifact_install_executor.hpp"
#include "artifact_workspace.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

// 1 PackageBaseのseparated build/installへ渡すvalidated inputだけを束ねる。
// Artifact path/identity/directiveはlifecycle内部で生成し、入力として公開しない。
struct SeparatedSourceBuildUnitRequest {
    ProductionArtifactSourceTree       source_tree;
    ValidatedPrivateCacheRoot          artifact_root;
    std::string                        requested_name;
    std::string                        package_base;
    DesiredInstallReason               desired_reason;
    SourceBuildEnvironment             source_environment;
    SourceEnvironmentEmptyValuePolicy empty_value_policy =
            SourceEnvironmentEmptyValuePolicy::Omit;
    PacmanDatabasePaths database_paths;
};

// --noconfirmはdependency installとartifact installの両境界へ同じ値を渡す。
// --neededはPreparedArtifactInstallだけへ渡し、build-only makepkgには渡さない。
struct SeparatedSourceBuildUnitOptions {
    bool no_confirm = false;
    bool needed = false;
    bool rm_deps = false;
    bool rebuild = false;
    bool clean_build = false;
};

// pacman transaction成功後のworkspace cleanup failureをtransaction failureと分け、
// package install/--needed skipのoutcomeも失わず保持する。
class SeparatedSourceBuildCleanupError : public std::runtime_error {
    ArtifactInstallExecutionOutcome install_outcome_;

public:
    SeparatedSourceBuildCleanupError(
            ArtifactInstallExecutionOutcome install_outcome,
            const std::string& diagnostic);

    ArtifactInstallExecutionOutcome install_outcome() const noexcept {
        return install_outcome_;
    }
};

// POLICY(#242): production callerはこのshared lifecycleだけを呼び、artifact pathや
// lower-level install primitiveを組み替えない。戻り値はcleanup完了後のinstall outcome。
ArtifactInstallExecutionOutcome execute_separated_source_build_unit(
        SeparatedSourceBuildUnitRequest request,
        const SeparatedSourceBuildUnitOptions& options);

#ifdef MOGUET_ENABLE_SEPARATED_SOURCE_BUILD_TEST_HOOKS
using SeparatedSourceBuildWorkspaceObserverForTest =
        void (*)(const std::filesystem::path& workspace_path);

// mkdtemp由来pathでstrict process expectationを組み立てるためのtest-only observer。
// workspaceやartifact ownershipを差し替える能力は公開しない。
void set_separated_source_build_workspace_observer_for_test(
        SeparatedSourceBuildWorkspaceObserverForTest observer);
#endif
