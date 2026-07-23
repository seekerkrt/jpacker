#pragma once

#include <optional>
#include <string>
#include <vector>

enum class DesiredInstallReason;

enum class ArtifactWorkspaceOwnership {
    InvocationOwnedFresh,
    ExternalOrShared
};

enum class SourcePkgdestState {
    Unchecked,
    NotDefined,
    Defined
};

struct ProducedPackageArtifact {
    // Archive identityのpure projection。filesystem path proofは別capabilityが所有する。
    std::string package_name;
};

struct ArtifactSelectionRequest {
    std::string                          requested_name;
    std::string                          package_base;
    ArtifactWorkspaceOwnership           workspace_ownership =
            ArtifactWorkspaceOwnership::ExternalOrShared;
    SourcePkgdestState                   source_pkgdest_state =
            SourcePkgdestState::Unchecked;
    std::vector<ProducedPackageArtifact> artifacts;
};

struct ValidatedArtifactInstallTarget {
    // Pure policy result。filesystem proofはValidatedPackageArtifactPathが別に所有する。
    std::string package_name;
};

enum class ExistingInstallReason {
    Explicit,
    Dependency
};

enum class InstalledVersionState {
    NotInstalled,
    SameVersion,
    DifferentVersion
};

enum class InstallReasonDirective {
    Default,
    AsExplicit,
    AsDependency
};

ValidatedArtifactInstallTarget validate_single_output_artifact(
        const ArtifactSelectionRequest& request);

InstallReasonDirective resolve_install_reason_directive(
        DesiredInstallReason desired_reason,
        InstalledVersionState version_state,
        std::optional<ExistingInstallReason> existing_reason,
        bool needed);

void require_supported_separated_install_options(bool rm_deps);
