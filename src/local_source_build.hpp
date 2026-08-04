#pragma once

#include "artifact_identity_selection.hpp"
#include "artifact_workspace.hpp"
#include "local_dependency_plan_projection.hpp"
#include "local_source_root.hpp"
#include "local_source_workspace.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct LocalSourceBuildRequest;
class PreparedLocalSourceBuild;
class LocalSourceBuildResult;

enum class LocalSourceBuildMetadataProvenance {
    ExistingSrcinfo,
    EvaluatedPkgbuild
};

// 採用metadataを、その評価に使ったone-off environmentとsource identityへ
// 束ねる。Slice 4のbuild-only ownerはこのproofだけを受け取り、callerが後から
// environmentを差し替える経路を持たない。
class LocalSourceBuildMetadata final {
    LocalPackageMetadata                 metadata_;
    SourceBuildEnvironment               source_environment_;
    std::string                          effective_architecture_;
    LocalSourceBuildMetadataProvenance   provenance_;
    LocalSourceDirectoryIdentity         source_directory_identity_;
    LocalSourceFileSnapshot              pkgbuild_snapshot_;

    LocalSourceBuildMetadata(
            LocalPackageMetadata metadata,
            SourceBuildEnvironment source_environment,
            std::string effective_architecture,
            LocalSourceBuildMetadataProvenance provenance,
            LocalSourceDirectoryIdentity source_directory_identity,
            LocalSourceFileSnapshot pkgbuild_snapshot) noexcept;

    friend LocalSourceBuildMetadata bind_existing_local_source_metadata(
            const LocalSourceRoot&, std::string);
    friend LocalSourceBuildMetadata bind_evaluated_local_source_metadata(
            const LocalSourceRoot&, SourceBuildEnvironment, std::string,
            std::string_view);
    friend class PreparedLocalSourceBuild;
    friend PreparedLocalSourceBuild prepare_local_source_build(
            struct LocalSourceBuildRequest request);

public:
    LocalSourceBuildMetadata(const LocalSourceBuildMetadata&) = default;
    LocalSourceBuildMetadata(LocalSourceBuildMetadata&&) noexcept = default;
    LocalSourceBuildMetadata& operator=(
            const LocalSourceBuildMetadata&) = delete;
    LocalSourceBuildMetadata& operator=(
            LocalSourceBuildMetadata&&) noexcept = delete;
    ~LocalSourceBuildMetadata() = default;

    const LocalPackageMetadata& metadata() const noexcept;
    const SourceBuildEnvironment& source_environment() const noexcept;
    const std::string& effective_architecture() const noexcept;
    LocalSourceBuildMetadataProvenance provenance() const noexcept;
    void require_matches(const LocalSourceRoot& source_root) const;
};

// Safeな既存.SRCINFOだけをenvironmentなしで採用する。
LocalSourceBuildMetadata bind_existing_local_source_metadata(
        const LocalSourceRoot& source_root,
        std::string effective_architecture);

// 明示許可済みmakepkg --printsrcinfo stdoutを、実行時environmentとsource
// identityへ束ねる。command実行自体はlocal_source_metadata_evaluationが所有する。
LocalSourceBuildMetadata bind_evaluated_local_source_metadata(
        const LocalSourceRoot& source_root,
        SourceBuildEnvironment source_environment,
        std::string effective_architecture,
        std::string_view evaluated_srcinfo);

// Local source、dependency plan、cache authorityを一つのbuild-only requestに
// 固定する。install policyやtransaction optionはこの境界へ持ち込まない。
struct LocalSourceBuildRequest {
    LocalSourceRoot                     source_root;
    LocalBuildPlan                      build_plan;
    ValidatedCacheRoot                  cache_root;
    LocalSourceBuildMetadata             metadata;
    ArtifactMakepkgBuildOptions         makepkg_options;
};

class PreparedLocalSourceBuild final {
    LocalSourceBuildRequest                    request_;
    std::string                                package_base_;
    std::vector<RequiredPackageArtifactTarget> required_targets_;

    PreparedLocalSourceBuild(
            LocalSourceBuildRequest request,
            std::string package_base,
            std::vector<RequiredPackageArtifactTarget>
                    required_targets) noexcept;

    friend PreparedLocalSourceBuild prepare_local_source_build(
            LocalSourceBuildRequest request);
    friend class LocalSourceBuildResult;
    friend LocalSourceBuildResult execute_prepared_local_source_build(
            PreparedLocalSourceBuild prepared);

public:
    PreparedLocalSourceBuild(const PreparedLocalSourceBuild&) = delete;
    PreparedLocalSourceBuild& operator=(
            const PreparedLocalSourceBuild&) = delete;
    PreparedLocalSourceBuild(PreparedLocalSourceBuild&&) noexcept = default;
    PreparedLocalSourceBuild& operator=(
            PreparedLocalSourceBuild&&) = delete;
    ~PreparedLocalSourceBuild() noexcept = default;
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
// retained workspace pathはdisplay専用であり、artifact pathやcleanup capabilityは
// errorへ公開しない。
class LocalSourceBuildPhaseError final : public std::runtime_error {
    LocalSourceBuildFailurePhase phase_;
    std::optional<int>           build_exit_code_;
    std::optional<PackageBaseArtifactIdentitySelectionFailure>
            selection_failure_;
    std::optional<LocalSourceRootFailure> source_root_failure_;
    std::optional<LocalSourceWorkspaceFailure> source_workspace_failure_;
    std::optional<LocalSourceWorkspaceFailure> source_cleanup_failure_;
    std::optional<std::filesystem::path> retained_artifact_workspace_;

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
            LocalSourceBuildFailurePhase phase,
            const std::string& diagnostic,
            std::filesystem::path retained_artifact_workspace,
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
    // Display-only path; callers must not use it as filesystem authority.
    const std::filesystem::path* retained_artifact_workspace()
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
    friend LocalSourceBuildResult execute_prepared_local_source_build(
            PreparedLocalSourceBuild prepared);
    friend class LocalSourceInstallAccess;

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

// 全static coherenceとsource identityをmutation-freeで固定する。remote
// dependency transactionより前にこのcapabilityを作る。
PreparedLocalSourceBuild prepare_local_source_build(
        LocalSourceBuildRequest request);

LocalSourceBuildResult execute_prepared_local_source_build(
        PreparedLocalSourceBuild prepared);

// local root unitだけをbuildし、remote dependency unitやinstall transactionは
// 実行しないcompatibility wrapper。success resultがvalidated aggregateを所有する。
LocalSourceBuildResult execute_local_source_build(
        LocalSourceBuildRequest request);
