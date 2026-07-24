#include "artifact_install_plan.hpp"
#include "dependency_plan.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename ExpectedException, typename Callable>
void expect_exception(Callable callable, const std::string& expected_message) {
    try {
        callable();
    } catch(const ExpectedException& error) {
        expect(
                std::string(error.what()) == expected_message,
                "Unexpected exception message: expected [" + expected_message +
                        "], actual [" + error.what() + "]");
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Unexpected exception category: " + std::string(error.what()));
    }

    throw std::runtime_error("Expected exception: " + expected_message);
}

ArtifactSelectionRequest single_artifact_request() {
    return ArtifactSelectionRequest{
            "sample-package",
            "sample-package",
            ArtifactWorkspaceOwnership::InvocationOwnedFresh,
            SourcePkgdestState::NotDefined,
            {{"sample-package"}}};
}

void test_invocation_owned_single_exact_artifact() {
    ArtifactSelectionRequest request = single_artifact_request();
    ValidatedArtifactInstallTarget target = validate_single_output_artifact(request);

    expect(
            target.package_name == request.artifacts.front().package_name,
            "Validated artifact package name differs");
}

void test_external_or_shared_workspace_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.workspace_ownership = ArtifactWorkspaceOwnership::ExternalOrShared;

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Artifact workspace must be invocation-owned and fresh.");
}

void test_unknown_workspace_ownership_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.workspace_ownership = static_cast<ArtifactWorkspaceOwnership>(2);

    expect_exception<std::logic_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Unknown artifact workspace ownership.");
}

void test_unspecified_workspace_ownership_rejected() {
    ArtifactSelectionRequest request;
    request.requested_name = "sample-package";
    request.package_base = "sample-package";
    request.source_pkgdest_state = SourcePkgdestState::NotDefined;
    request.artifacts = {
            {"sample-package"}};

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Artifact workspace must be invocation-owned and fresh.");
}

void test_source_pkgdest_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.source_pkgdest_state = SourcePkgdestState::Defined;

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Source environment PKGDEST conflicts with invocation-owned artifact workspace.");
}

void test_unchecked_source_pkgdest_rejected() {
    ArtifactSelectionRequest request;

    expect_exception<std::logic_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Source PKGDEST state has not been checked.");
}

void test_unknown_source_pkgdest_state_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.source_pkgdest_state = static_cast<SourcePkgdestState>(3);

    expect_exception<std::logic_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Unknown source PKGDEST state.");
}

void test_package_base_mismatch_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.package_base = "sample-package-base";

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "PackageBase does not match the requested package: sample-package-base != "
            "sample-package.");
}

void test_zero_artifacts_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.artifacts.clear();

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Expected exactly one produced package artifact, got 0.");
}

void test_multiple_artifacts_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.artifacts.push_back(
            {"sample-package"});

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Expected exactly one produced package artifact, got 2.");
}

void test_debug_or_sibling_output_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.artifacts.push_back(
            {"sample-package-debug"});

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Expected exactly one produced package artifact, got 2.");
}

void test_artifact_package_name_mismatch_rejected() {
    ArtifactSelectionRequest request = single_artifact_request();
    request.artifacts.front().package_name = "different-package";

    expect_exception<std::runtime_error>(
            [&request]() { static_cast<void>(validate_single_output_artifact(request)); },
            "Produced artifact package name does not match the requested package: "
            "different-package != sample-package.");
}

void test_new_root_uses_default_reason() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled, std::nullopt, false) ==
                    InstallReasonDirective::Default,
            "New root reason directive differs");
}

void test_existing_explicit_root_uses_default_reason() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Explicit,
                    InstalledVersionState::DifferentVersion,
                    ExistingInstallReason::Explicit, false) ==
                    InstallReasonDirective::Default,
            "Existing explicit root reason directive differs");
}

void test_existing_dependency_root_becomes_explicit() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Explicit,
                    InstalledVersionState::DifferentVersion,
                    ExistingInstallReason::Dependency, false) ==
                    InstallReasonDirective::AsExplicit,
            "Existing dependency root reason directive differs");
}

void test_new_dependency_installs_as_dependency() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Dependency,
                    InstalledVersionState::NotInstalled, std::nullopt, false) ==
                    InstallReasonDirective::AsDependency,
            "New dependency reason directive differs");
}

void test_existing_dependency_stays_dependency() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Dependency,
                    InstalledVersionState::DifferentVersion,
                    ExistingInstallReason::Dependency, false) ==
                    InstallReasonDirective::Default,
            "Existing dependency reason directive differs");
}

void test_existing_explicit_dependency_stays_explicit() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Dependency,
                    InstalledVersionState::DifferentVersion,
                    ExistingInstallReason::Explicit, false) ==
                    InstallReasonDirective::Default,
            "Existing explicit dependency reason directive differs");
}

void test_same_version_needed_default_allowed() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Explicit,
                    InstalledVersionState::SameVersion,
                    ExistingInstallReason::Explicit, true) ==
                    InstallReasonDirective::Default,
            "Same-version --needed default directive differs");
}

void test_same_version_needed_reason_change_rejected() {
    expect_exception<std::runtime_error>(
            []() {
                static_cast<void>(resolve_install_reason_directive(
                        DesiredInstallReason::Explicit,
                        InstalledVersionState::SameVersion,
                        ExistingInstallReason::Dependency, true));
            },
            "Cannot change install reason because --needed may skip the same-version install.");
}

void test_different_version_needed_reason_change_allowed() {
    expect(
            resolve_install_reason_directive(
                    DesiredInstallReason::Explicit,
                    InstalledVersionState::DifferentVersion,
                    ExistingInstallReason::Dependency, true) ==
                    InstallReasonDirective::AsExplicit,
            "Different-version --needed reason directive differs");
}

void test_not_installed_with_existing_reason_rejected() {
    expect_exception<std::logic_error>(
            []() {
                static_cast<void>(resolve_install_reason_directive(
                        DesiredInstallReason::Explicit,
                        InstalledVersionState::NotInstalled,
                        ExistingInstallReason::Explicit, false));
            },
            "Not-installed package must not have an existing install reason.");
}

void test_installed_without_existing_reason_rejected() {
    auto expect_missing_reason = [](InstalledVersionState version_state) {
        expect_exception<std::logic_error>(
                [version_state]() {
                    static_cast<void>(resolve_install_reason_directive(
                            DesiredInstallReason::Explicit, version_state,
                            std::nullopt, false));
                },
                "Installed package must have an existing install reason.");
    };

    expect_missing_reason(InstalledVersionState::SameVersion);
    expect_missing_reason(InstalledVersionState::DifferentVersion);
}

void test_unknown_existing_reason_rejected() {
    expect_exception<std::logic_error>(
            []() {
                static_cast<void>(resolve_install_reason_directive(
                        DesiredInstallReason::Explicit,
                        InstalledVersionState::DifferentVersion,
                        static_cast<ExistingInstallReason>(2), false));
            },
            "Unknown existing install reason.");
}

void test_unknown_installed_version_state_rejected() {
    expect_exception<std::logic_error>(
            []() {
                static_cast<void>(resolve_install_reason_directive(
                        DesiredInstallReason::Explicit,
                        static_cast<InstalledVersionState>(3), std::nullopt,
                        false));
            },
            "Unknown installed version state.");
}

void test_unknown_desired_install_reason_rejected() {
    expect_exception<std::logic_error>(
            []() {
                static_cast<void>(resolve_install_reason_directive(
                        static_cast<DesiredInstallReason>(2),
                        InstalledVersionState::NotInstalled, std::nullopt,
                        false));
            },
            "Unknown desired install reason.");
}

void test_separated_install_without_rmdeps_allowed() {
    require_supported_separated_install_options(false);
}

void test_separated_install_with_rmdeps_rejected() {
    expect_exception<std::runtime_error>(
            []() { require_supported_separated_install_options(true); },
            "Separated build/install does not support --rmdeps.");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "invocation-owned single exact artifact",
                test_invocation_owned_single_exact_artifact);
        run_case(
                "external or shared workspace rejected",
                test_external_or_shared_workspace_rejected);
        run_case(
                "unknown workspace ownership rejected",
                test_unknown_workspace_ownership_rejected);
        run_case(
                "unspecified workspace ownership rejected",
                test_unspecified_workspace_ownership_rejected);
        run_case("source PKGDEST rejected", test_source_pkgdest_rejected);
        run_case(
                "unchecked source PKGDEST rejected",
                test_unchecked_source_pkgdest_rejected);
        run_case(
                "unknown source PKGDEST state rejected",
                test_unknown_source_pkgdest_state_rejected);
        run_case("PackageBase mismatch rejected", test_package_base_mismatch_rejected);
        run_case("zero artifacts rejected", test_zero_artifacts_rejected);
        run_case("multiple artifacts rejected", test_multiple_artifacts_rejected);
        run_case("debug or sibling output rejected", test_debug_or_sibling_output_rejected);
        run_case(
                "artifact package name mismatch rejected",
                test_artifact_package_name_mismatch_rejected);
        run_case("new root uses default reason", test_new_root_uses_default_reason);
        run_case(
                "existing explicit root uses default reason",
                test_existing_explicit_root_uses_default_reason);
        run_case(
                "existing dependency root becomes explicit",
                test_existing_dependency_root_becomes_explicit);
        run_case(
                "new dependency installs as dependency",
                test_new_dependency_installs_as_dependency);
        run_case(
                "existing dependency stays dependency",
                test_existing_dependency_stays_dependency);
        run_case(
                "existing explicit dependency stays explicit",
                test_existing_explicit_dependency_stays_explicit);
        run_case(
                "same-version --needed default allowed",
                test_same_version_needed_default_allowed);
        run_case(
                "same-version --needed reason change rejected",
                test_same_version_needed_reason_change_rejected);
        run_case(
                "different-version --needed reason change allowed",
                test_different_version_needed_reason_change_allowed);
        run_case(
                "not-installed with existing reason rejected",
                test_not_installed_with_existing_reason_rejected);
        run_case(
                "installed without existing reason rejected",
                test_installed_without_existing_reason_rejected);
        run_case(
                "unknown existing reason rejected",
                test_unknown_existing_reason_rejected);
        run_case(
                "unknown installed version state rejected",
                test_unknown_installed_version_state_rejected);
        run_case(
                "unknown desired install reason rejected",
                test_unknown_desired_install_reason_rejected);
        run_case(
                "separated install without --rmdeps allowed",
                test_separated_install_without_rmdeps_allowed);
        run_case(
                "separated install with --rmdeps rejected",
                test_separated_install_with_rmdeps_rejected);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact install plan tests: all 28 checks passed\n";
    return 0;
}
