#pragma once

#include "artifact_identity_selection.hpp"
#include "artifact_install_executor.hpp"
#include "package_base_artifact_install_plan.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class PackageBaseArtifactInstallPreparationResult;
class PackageBaseArtifactInstallExecutionResult;

struct PreparedPackageBaseArtifactInstallSelectedArtifact {
    std::size_t                            artifact_index;
    ArtifactPackageIdentity                identity;
    DesiredInstallReason                   desired_reason;
    InstalledVersionState                  installed_version_state;
    std::optional<ExistingInstallReason>   existing_reason;
    InstallReasonDirective                 directive;
    PackageBaseArtifactInstallExpectedOutcome expected_outcome;
};

struct PreparedPackageBaseArtifactInstallUnselectedArtifact {
    std::size_t             artifact_index;
    ArtifactPackageIdentity identity;
};

class PreparedPackageBaseArtifactInstall final {
    enum class State {
        Active,
        Consumed,
        CleanupPending,
        Cleaned,
        MovedFrom
    };

    std::string package_base_;
    // Exactly one filesystem/cleanup owner。selected recordへ分配しない。
    ValidatedPackageArtifactSet artifacts_;
    // Aggregate順のprivate identity snapshot。executor直前のindex相関再証明に使う。
    std::vector<ArtifactPackageIdentity> aggregate_identities_;
    std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>
            selected_artifacts_;
    std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>
            unselected_artifacts_;
    InstallReasonDirective transaction_directive_ =
            InstallReasonDirective::Default;
    bool  needed_ = false;
    State state_ = State::Active;

    PreparedPackageBaseArtifactInstall(
            std::string&& package_base,
            ValidatedPackageArtifactSet&& artifacts,
            std::vector<ArtifactPackageIdentity>&& aggregate_identities,
            std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>&&
                    selected_artifacts,
            std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>&&
                    unselected_artifacts,
            InstallReasonDirective transaction_directive,
            bool needed) noexcept;

    void require_not_moved_from() const;
    void require_active_for_execution() const;
    void require_execution_coherence() const;

    friend class PackageBaseArtifactInstallPreparationResult;
    friend PackageBaseArtifactInstallPreparationResult
    prepare_package_base_artifact_install(
            ValidatedPackageArtifactSet& artifacts,
            const std::string& package_base,
            const std::vector<RequiredPackageArtifactTarget>& required_targets,
            const ArtifactInstallPreparationOptions& options,
            const PacmanDatabasePaths& database_paths);
    friend class PackageBaseArtifactInstallExecutionResult;
    friend PackageBaseArtifactInstallExecutionResult
    execute_prepared_package_base_artifact_install(
            PreparedPackageBaseArtifactInstall& install,
            const ArtifactInstallExecutionOptions& options);

public:
    PreparedPackageBaseArtifactInstall(
            const PreparedPackageBaseArtifactInstall&) = delete;
    PreparedPackageBaseArtifactInstall& operator=(
            const PreparedPackageBaseArtifactInstall&) = delete;
    PreparedPackageBaseArtifactInstall(
            PreparedPackageBaseArtifactInstall&& other) noexcept;
    PreparedPackageBaseArtifactInstall& operator=(
            PreparedPackageBaseArtifactInstall&&) = delete;
    ~PreparedPackageBaseArtifactInstall() noexcept = default;

    const std::string& package_base() const;
    const std::vector<PreparedPackageBaseArtifactInstallSelectedArtifact>&
    selected_artifacts() const;
    const std::vector<PreparedPackageBaseArtifactInstallUnselectedArtifact>&
    unselected_artifacts() const;
    InstallReasonDirective transaction_directive() const;
    bool needed() const;

    const std::filesystem::path& workspace_path() const;
    void retain_workspace_for_diagnostics();
    void cleanup_workspace();
};

class PackageBaseArtifactInstallPreparationFailure final {
public:
    PackageBaseArtifactInstallPreparationFailure() = delete;
    PackageBaseArtifactInstallPreparationFailure(
            const PackageBaseArtifactInstallPreparationFailure&) = default;
    PackageBaseArtifactInstallPreparationFailure(
            PackageBaseArtifactInstallPreparationFailure&&) noexcept = default;
    PackageBaseArtifactInstallPreparationFailure& operator=(
            const PackageBaseArtifactInstallPreparationFailure&) = delete;
    PackageBaseArtifactInstallPreparationFailure& operator=(
            PackageBaseArtifactInstallPreparationFailure&&) noexcept = delete;
    ~PackageBaseArtifactInstallPreparationFailure() = default;

    [[nodiscard]] const PackageBaseArtifactIdentitySelectionFailure*
    selection_failure() const noexcept;
    [[nodiscard]] const MixedPackageBaseInstallReasonUnsupported*
    mixed_reason_failure() const noexcept;

private:
    explicit PackageBaseArtifactInstallPreparationFailure(
            PackageBaseArtifactIdentitySelectionFailure failure);
    explicit PackageBaseArtifactInstallPreparationFailure(
            MixedPackageBaseInstallReasonUnsupported failure);

    std::variant<PackageBaseArtifactIdentitySelectionFailure,
                 MixedPackageBaseInstallReasonUnsupported>
            failure_;

    friend PackageBaseArtifactInstallPreparationResult
    prepare_package_base_artifact_install(
            ValidatedPackageArtifactSet& artifacts,
            const std::string& package_base,
            const std::vector<RequiredPackageArtifactTarget>& required_targets,
            const ArtifactInstallPreparationOptions& options,
            const PacmanDatabasePaths& database_paths);
};

class PackageBaseArtifactInstallPreparationResult final {
public:
    PackageBaseArtifactInstallPreparationResult() = delete;
    PackageBaseArtifactInstallPreparationResult(
            const PackageBaseArtifactInstallPreparationResult&) = delete;
    PackageBaseArtifactInstallPreparationResult(
            PackageBaseArtifactInstallPreparationResult&&) noexcept = default;
    PackageBaseArtifactInstallPreparationResult& operator=(
            const PackageBaseArtifactInstallPreparationResult&) = delete;
    PackageBaseArtifactInstallPreparationResult& operator=(
            PackageBaseArtifactInstallPreparationResult&&) noexcept = delete;
    ~PackageBaseArtifactInstallPreparationResult() noexcept = default;

    [[nodiscard]] bool is_prepared() const noexcept;
    [[nodiscard]] PreparedPackageBaseArtifactInstall* prepared() noexcept;
    [[nodiscard]] const PreparedPackageBaseArtifactInstall* prepared()
            const noexcept;
    [[nodiscard]] const PackageBaseArtifactInstallPreparationFailure* failure()
            const noexcept;

private:
    explicit PackageBaseArtifactInstallPreparationResult(
            PreparedPackageBaseArtifactInstall&& prepared) noexcept;
    explicit PackageBaseArtifactInstallPreparationResult(
            PackageBaseArtifactInstallPreparationFailure&& failure) noexcept;

    std::variant<PreparedPackageBaseArtifactInstall,
                 PackageBaseArtifactInstallPreparationFailure>
            outcome_;

    friend PackageBaseArtifactInstallPreparationResult
    prepare_package_base_artifact_install(
            ValidatedPackageArtifactSet& artifacts,
            const std::string& package_base,
            const std::vector<RequiredPackageArtifactTarget>& required_targets,
            const ArtifactInstallPreparationOptions& options,
            const PacmanDatabasePaths& database_paths);
};

// identity setやselection index/directiveをcaller入力にせず、一つのoperationで
// query、selection、metadata、policy、ownership commitまで閉じる。
PackageBaseArtifactInstallPreparationResult
prepare_package_base_artifact_install(
        ValidatedPackageArtifactSet& artifacts,
        const std::string& package_base,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ArtifactInstallPreparationOptions& options,
        const PacmanDatabasePaths& database_paths);

struct PackageBaseArtifactInstallExecutionArtifactResult {
    std::size_t             artifact_index;
    ArtifactPackageIdentity identity;
    DesiredInstallReason    desired_reason;
    PackageBaseArtifactInstallExpectedOutcome outcome;
};

class PackageBaseArtifactInstallExecutionResult final {
public:
    PackageBaseArtifactInstallExecutionResult() = delete;
    PackageBaseArtifactInstallExecutionResult(
            const PackageBaseArtifactInstallExecutionResult&) = default;
    PackageBaseArtifactInstallExecutionResult(
            PackageBaseArtifactInstallExecutionResult&&) noexcept = default;
    PackageBaseArtifactInstallExecutionResult& operator=(
            const PackageBaseArtifactInstallExecutionResult&) = delete;
    PackageBaseArtifactInstallExecutionResult& operator=(
            PackageBaseArtifactInstallExecutionResult&&) noexcept = delete;
    ~PackageBaseArtifactInstallExecutionResult() = default;

    const std::string& package_base() const noexcept;
    const std::vector<PackageBaseArtifactInstallExecutionArtifactResult>&
    selected_artifacts() const noexcept;
    bool is_success() const noexcept;

private:
    PackageBaseArtifactInstallExecutionResult(
            std::string package_base,
            std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
                    selected_artifacts) noexcept;

    std::string package_base_;
    std::vector<PackageBaseArtifactInstallExecutionArtifactResult>
            selected_artifacts_;
    bool is_success_ = true;

    friend PackageBaseArtifactInstallExecutionResult
    execute_prepared_package_base_artifact_install(
            PreparedPackageBaseArtifactInstall& install,
            const ArtifactInstallExecutionOptions& options);
};

// POLICY(#268): raw path executorは公開せず、correlated one-shot capabilityだけを受ける。
PackageBaseArtifactInstallExecutionResult
execute_prepared_package_base_artifact_install(
        PreparedPackageBaseArtifactInstall& install,
        const ArtifactInstallExecutionOptions& options);
