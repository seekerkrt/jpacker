#pragma once

#include "artifact_identity.hpp"
#include "aur_update_plan.hpp"
#include "dependency_plan.hpp"
#include "local_source_build.hpp"
#include "root_package_candidate.hpp"
#include "source_install.hpp"
#include "source_package_identity.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class SourcePackageIdentityProjectionInputKind {
    RootPackage,
    ResolvedSourceBuild,
    LocalSource,
    Dependency,
    Artifact,
    AurUpdate,
};

enum class SourcePackageIdentityProjectionIssueKind {
    MissingPackageBase,
    MissingSourceContext,
    UnsupportedSource,
    IncompleteAuthority,
    UnsupportedArchitecture,
    SourceNotFound,
    SourceMetadataUnavailable,
    PackageNameMismatch,
    InvalidIdentity,
};

struct SourcePackageIdentityProjectionIssue {
    SourcePackageIdentityProjectionInputKind input_kind;
    SourcePackageIdentityProjectionIssueKind  kind;
    std::optional<std::size_t>                input_index;
    std::optional<PackageSourceIdentity>      source;
    std::optional<std::string>                package_name;
    std::optional<std::string>                package_base;

    bool operator==(const SourcePackageIdentityProjectionIssue&) const =
            default;
};

struct SourcePackageIdentityProjectionSuccess {
    // Single-value adapters return size 1. Local source preserves accepted
    // metadata child order. An empty successful aggregate is never exposed.
    std::vector<SourceAwarePackageIdentity> identities;

    bool operator==(const SourcePackageIdentityProjectionSuccess&) const =
            default;
};

struct SourcePackageIdentityProjectionFailure {
    // Failure never exposes a partially projected identity aggregate.
    std::vector<SourcePackageIdentityProjectionIssue> issues;

    bool operator==(const SourcePackageIdentityProjectionFailure&) const =
            default;
};

struct SourcePackageIdentityProjectionAccess;

class SourcePackageIdentityProjectionResult final {
public:
    SourcePackageIdentityProjectionResult() = delete;
    SourcePackageIdentityProjectionResult(
            const SourcePackageIdentityProjectionResult&) = default;
    SourcePackageIdentityProjectionResult(
            SourcePackageIdentityProjectionResult&&) noexcept = default;
    SourcePackageIdentityProjectionResult& operator=(
            const SourcePackageIdentityProjectionResult&) = delete;
    SourcePackageIdentityProjectionResult& operator=(
            SourcePackageIdentityProjectionResult&&) noexcept = delete;
    ~SourcePackageIdentityProjectionResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const SourcePackageIdentityProjectionSuccess* success()
            const noexcept;
    [[nodiscard]] const SourcePackageIdentityProjectionFailure* failure()
            const noexcept;

private:
    explicit SourcePackageIdentityProjectionResult(
            SourcePackageIdentityProjectionSuccess success) noexcept;
    explicit SourcePackageIdentityProjectionResult(
            SourcePackageIdentityProjectionFailure failure) noexcept;

    std::variant<SourcePackageIdentityProjectionSuccess,
                 SourcePackageIdentityProjectionFailure>
            outcome_;

    friend struct SourcePackageIdentityProjectionAccess;
};

// Repository root identity intentionally returns MissingPackageBase. AUR root
// identity can project without re-querying or deriving a URL.
SourcePackageIdentityProjectionResult project_root_source_package_identity(
        const RootPackageIdentity& root);

// This is the complete remote source adapter: source kind, repository
// provenance where applicable, PackageBase, requested child, and owned Git URL
// all come from ResolvedSourceBuildIdentity. It does not query a commit.
SourcePackageIdentityProjectionResult
project_resolved_source_build_package_identity(
        const ResolvedSourceBuildIdentity& source);

// The closed local authority prevents metadata/root/architecture values from
// different invocations being combined. Every accepted child is projected in
// metadata order, or no identities are returned.
SourcePackageIdentityProjectionResult project_local_source_package_identities(
        const LocalSourceBuildProjectionAuthority& source);

// Only source-specific resolved candidates are consumed. Installed-only
// candidates have no PackageBase/source identity and return UnsupportedSource.
SourcePackageIdentityProjectionResult
project_dependency_source_package_identity(
        const ResolvedDependencyCandidate& candidate);

// Artifact metadata proves the actual child/release only. The optional common
// context supplies the already-projected source/PackageBase/revision/arch; a
// missing context is a typed failure rather than inferred identity.
SourcePackageIdentityProjectionResult project_artifact_source_package_identity(
        const std::optional<SourceAwarePackageIdentity>& source_context,
        const ArtifactPackageIdentity& artifact);

// An update entry without AUR metadata remains unavailable. The adapter does
// not query AUR or derive a remote URL/commit.
SourcePackageIdentityProjectionResult project_aur_update_package_identity(
        const AurUpdatePlanEntry& update);
