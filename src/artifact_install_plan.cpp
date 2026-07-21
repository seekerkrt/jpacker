#include "artifact_install_plan.hpp"

#include "dependency_plan.hpp"

#include <stdexcept>
#include <string>

namespace {

void require_consistent_install_reason_input(
        InstalledVersionState version_state,
        const std::optional<ExistingInstallReason>& existing_reason) {
    switch(version_state) {
    case InstalledVersionState::NotInstalled:
        if(existing_reason.has_value()) {
            throw std::logic_error(
                    "Not-installed package must not have an existing install reason.");
        }
        break;
    case InstalledVersionState::SameVersion:
    case InstalledVersionState::DifferentVersion:
        if(!existing_reason.has_value()) {
            throw std::logic_error(
                    "Installed package must have an existing install reason.");
        }
        break;
    default:
        throw std::logic_error("Unknown installed version state.");
    }

    if(!existing_reason.has_value()) return;
    switch(existing_reason.value()) {
    case ExistingInstallReason::Explicit:
    case ExistingInstallReason::Dependency:
        return;
    }

    throw std::logic_error("Unknown existing install reason.");
}

} // namespace

ValidatedArtifactInstallTarget validate_single_output_artifact(
        const ArtifactSelectionRequest& request) {
    switch(request.source_pkgdest_state) {
    case SourcePkgdestState::Unchecked:
        throw std::logic_error("Source PKGDEST state has not been checked.");
    case SourcePkgdestState::NotDefined:
        break;
    case SourcePkgdestState::Defined:
        throw std::runtime_error(
                "Source environment PKGDEST conflicts with invocation-owned artifact workspace.");
    default:
        throw std::logic_error("Unknown source PKGDEST state.");
    }

    // POLICY(#218): shared directoryには以前の生成物が混在し得るため、今回の出力として採用しない。
    switch(request.workspace_ownership) {
    case ArtifactWorkspaceOwnership::InvocationOwnedFresh:
        break;
    case ArtifactWorkspaceOwnership::ExternalOrShared:
        throw std::runtime_error(
                "Artifact workspace must be invocation-owned and fresh.");
    default:
        throw std::logic_error("Unknown artifact workspace ownership.");
    }

    if(request.package_base != request.requested_name) {
        throw std::runtime_error(
                "PackageBase does not match the requested package: " +
                request.package_base + " != " + request.requested_name + ".");
    }

    // POLICY(#218): split/sibling/debug outputを暗黙選択せず、single-output migrationだけを許可する。
    if(request.artifacts.size() != 1) {
        throw std::runtime_error(
                "Expected exactly one produced package artifact, got " +
                std::to_string(request.artifacts.size()) + ".");
    }

    const ProducedPackageArtifact& artifact = request.artifacts.front();
    if(artifact.path.empty()) {
        throw std::runtime_error("Produced package artifact path is empty.");
    }
    if(artifact.package_name != request.requested_name) {
        throw std::runtime_error(
                "Produced artifact package name does not match the requested package: " +
                artifact.package_name + " != " + request.requested_name + ".");
    }

    return ValidatedArtifactInstallTarget{artifact.path, artifact.package_name};
}

InstallReasonDirective resolve_install_reason_directive(
        DesiredInstallReason desired_reason,
        InstalledVersionState version_state,
        std::optional<ExistingInstallReason> existing_reason,
        bool needed) {
    require_consistent_install_reason_input(version_state, existing_reason);

    InstallReasonDirective directive = InstallReasonDirective::Default;
    switch(desired_reason) {
    case DesiredInstallReason::Explicit:
        if(version_state != InstalledVersionState::NotInstalled &&
           existing_reason.value() == ExistingInstallReason::Dependency) {
            directive = InstallReasonDirective::AsExplicit;
        }
        break;
    case DesiredInstallReason::Dependency:
        if(version_state == InstalledVersionState::NotInstalled) {
            directive = InstallReasonDirective::AsDependency;
        }
        // POLICY(#218): 既存explicit packageは、dependencyとして要求されても暗黙降格しない。
        break;
    default:
        throw std::logic_error("Unknown desired install reason.");
    }

    // LANDMINE(#218): same-version installがskipされる場合、reason変更を成功扱いできない。
    if(version_state == InstalledVersionState::SameVersion && needed &&
       directive != InstallReasonDirective::Default) {
        throw std::runtime_error(
                "Cannot change install reason because --needed may skip the same-version install.");
    }

    return directive;
}

void require_supported_separated_install_options(bool rm_deps) {
    if(!rm_deps) return;

    // LANDMINE(#218): build-onlyのmakepkg -rは、artifact install前にruntime dependencyも削除し得る。
    throw std::runtime_error("Separated build/install does not support --rmdeps.");
}
