#pragma once

#include "artifact_identity.hpp"
#include "artifact_install_plan.hpp"
#include "dependency_plan.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

struct SelectedPackageBaseArtifactInstallReasonPolicyInput {
    ArtifactPackageIdentity              identity;
    DesiredInstallReason                 desired_reason;
    InstalledVersionState                installed_version_state;
    std::optional<ExistingInstallReason> existing_reason;
};

struct PackageBaseArtifactInstallReasonPolicyInput {
    std::string package_base;
    // POLICY(#268): selectionで確定したrequired target順を維持する。
    std::vector<SelectedPackageBaseArtifactInstallReasonPolicyInput>
            selected_artifacts;
    bool needed = false;
};

enum class PackageBaseArtifactInstallExpectedOutcome {
    Installed,
    SkippedAsNeeded
};

struct PlannedPackageBaseArtifactInstallReason {
    ArtifactPackageIdentity                   identity;
    DesiredInstallReason                      desired_reason;
    InstalledVersionState                     installed_version_state;
    std::optional<ExistingInstallReason>      existing_reason;
    InstallReasonDirective                    directive;
    PackageBaseArtifactInstallExpectedOutcome expected_outcome;
};

struct PackageBaseArtifactInstallReasonPlan {
    std::string package_base;
    std::vector<PlannedPackageBaseArtifactInstallReason> selected_artifacts;
    InstallReasonDirective transaction_directive;
    bool                   needed;
};

struct MixedPackageBaseInstallReasonArtifact {
    ArtifactPackageIdentity              identity;
    DesiredInstallReason                 desired_reason;
    InstalledVersionState                installed_version_state;
    std::optional<ExistingInstallReason> existing_reason;
    InstallReasonDirective               directive;
};

struct MixedPackageBaseInstallReasonUnsupported {
    std::string package_base;
    // Diagnostic valueだけを保持し、artifact index/path/capabilityを公開しない。
    std::vector<MixedPackageBaseInstallReasonArtifact> selected_artifacts;
};

class PackageBaseArtifactInstallReasonPlanResult final {
  public:
    PackageBaseArtifactInstallReasonPlanResult() = delete;
    PackageBaseArtifactInstallReasonPlanResult(
            const PackageBaseArtifactInstallReasonPlanResult&) = default;
    PackageBaseArtifactInstallReasonPlanResult(
            PackageBaseArtifactInstallReasonPlanResult&&) noexcept = default;
    PackageBaseArtifactInstallReasonPlanResult& operator=(
            const PackageBaseArtifactInstallReasonPlanResult&) = delete;
    PackageBaseArtifactInstallReasonPlanResult& operator=(
            PackageBaseArtifactInstallReasonPlanResult&&) noexcept = delete;
    ~PackageBaseArtifactInstallReasonPlanResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const PackageBaseArtifactInstallReasonPlan* success()
            const noexcept;
    [[nodiscard]] const MixedPackageBaseInstallReasonUnsupported* failure()
            const noexcept;

  private:
    explicit PackageBaseArtifactInstallReasonPlanResult(
            PackageBaseArtifactInstallReasonPlan plan);
    explicit PackageBaseArtifactInstallReasonPlanResult(
            MixedPackageBaseInstallReasonUnsupported failure);

    std::variant<PackageBaseArtifactInstallReasonPlan,
                 MixedPackageBaseInstallReasonUnsupported>
            outcome_;

    friend PackageBaseArtifactInstallReasonPlanResult
    resolve_package_base_artifact_install_reason_plan(
            const PackageBaseArtifactInstallReasonPolicyInput& input);
};

// Per-artifact policyを既存singular reducerで確定し、一回のpacman -Uで
// 安全に表現できるtransaction-wide directiveへ畳む。
PackageBaseArtifactInstallReasonPlanResult
resolve_package_base_artifact_install_reason_plan(
        const PackageBaseArtifactInstallReasonPolicyInput& input);

#ifdef MOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS
using PackageBaseArtifactInstallReasonPlanObserverForTest = void (*)();

void set_package_base_artifact_install_reason_plan_observer_for_test(
        PackageBaseArtifactInstallReasonPlanObserverForTest observer);
#endif
