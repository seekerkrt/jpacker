#pragma once

#include "artifact_identity_selection.hpp"
#include "artifact_workspace.hpp"
#include "local_dependency_plan_projection.hpp"
#include "local_source_root.hpp"
#include "local_source_workspace.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Local source、dependency plan、cache authorityを一つのbuild-only requestに
// 固定する。install policyやtransaction optionはこの境界へ持ち込まない。
struct LocalSourceBuildRequest {
    LocalSourceRoot                     source_root;
    LocalBuildPlan                      build_plan;
    ValidatedCacheRoot                  cache_root;
    SourceBuildEnvironment              source_environment;
    SourceEnvironmentEmptyValuePolicy  empty_value_policy =
            SourceEnvironmentEmptyValuePolicy::Omit;
    ArtifactMakepkgBuildOptions         makepkg_options;
};

enum class LocalSourceBuildFailurePhase {
    Preflight,
    SourceWorkspace,
    ArtifactWorkspace,
    BuildContext,
    Packagelist,
    Build,
    ArtifactValidation,
    ArtifactIdentity,
    ArtifactSelection,
    SourceCleanup,
};

// Stable phaseと、利用可能な場合だけbuild status / closed selection failure /
// local source failure / primary failure中のsecondary cleanup failureを保持する。
// diagnostic textはretained workspaceのdisplay pathを含み得るが、artifact path
// accessorやcleanup capabilityはerrorへ公開しない。
class LocalSourceBuildPhaseError final : public std::runtime_error {
    LocalSourceBuildFailurePhase phase_;
    std::optional<int>           build_exit_code_;
    std::optional<PackageBaseArtifactIdentitySelectionFailure>
            selection_failure_;
    std::optional<LocalSourceRootFailure> source_root_failure_;
    std::optional<LocalSourceWorkspaceFailure> source_workspace_failure_;
    std::optional<LocalSourceWorkspaceFailure> source_cleanup_failure_;

public:
    LocalSourceBuildPhaseError(
            LocalSourceBuildFailurePhase phase,
            const std::string& diagnostic,
            std::optional<int> build_exit_code = std::nullopt,
            std::optional<PackageBaseArtifactIdentitySelectionFailure>
                    selection_failure = std::nullopt,
            std::optional<LocalSourceRootFailure> source_root_failure =
                    std::nullopt,
            std::optional<LocalSourceWorkspaceFailure>
                    source_workspace_failure = std::nullopt,
            std::optional<LocalSourceWorkspaceFailure>
                    source_cleanup_failure = std::nullopt);
    LocalSourceBuildPhaseError(
            const LocalSourceBuildPhaseError& primary,
            LocalSourceWorkspaceFailure source_cleanup_failure);

    LocalSourceBuildFailurePhase phase() const noexcept;
    std::optional<int> build_exit_code() const noexcept;
    const PackageBaseArtifactIdentitySelectionFailure* selection_failure()
            const noexcept;
    const LocalSourceRootFailure* source_root_failure() const noexcept;
    const LocalSourceWorkspaceFailure* source_workspace_failure()
            const noexcept;
    const LocalSourceWorkspaceFailure* source_cleanup_failure()
            const noexcept;
};

// Validated aggregateをprivateに所有し、publicにはstable artifact index付きの
// identity snapshotと明示cleanupだけを公開する。path/cleanup authorityやinstall
// directiveは外へ出さない。
class LocalSourceBuildResult final {
    PackageBaseArtifactIdentitySelectionSuccess selection_;
    ValidatedPackageArtifactSet                 artifacts_;

    LocalSourceBuildResult(
            PackageBaseArtifactIdentitySelectionSuccess selection,
            ValidatedPackageArtifactSet artifacts) noexcept;

    friend LocalSourceBuildResult execute_local_source_build(
            LocalSourceBuildRequest request);

public:
    LocalSourceBuildResult(const LocalSourceBuildResult&) = delete;
    LocalSourceBuildResult& operator=(const LocalSourceBuildResult&) = delete;
    LocalSourceBuildResult(LocalSourceBuildResult&& other) noexcept = default;
    LocalSourceBuildResult& operator=(LocalSourceBuildResult&&) = delete;
    ~LocalSourceBuildResult() noexcept = default;

    const std::string& package_base() const noexcept;
    const std::vector<CorrelatedSelectedPackageArtifact>& selected_artifacts()
            const noexcept;
    const std::vector<CorrelatedUnselectedPackageArtifact>&
    unselected_artifacts() const noexcept;

    void cleanup_artifacts();
};

// local root unitだけをbuildし、remote dependency unitやinstall transactionは
// 実行しない。success resultがvalidated artifact aggregateを所有する。
LocalSourceBuildResult execute_local_source_build(
        LocalSourceBuildRequest request);
