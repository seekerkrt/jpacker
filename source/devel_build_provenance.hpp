#pragma once

#include "artifact_identity.hpp"
#include "installed_artifact_binding.hpp"
#include "vcs_source_identity.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

inline constexpr std::uint32_t DEVEL_BUILD_PROVENANCE_SCHEMA_VERSION = 1;

class PackageArchiveSha256Digest final {
public:
    PackageArchiveSha256Digest() = delete;

    [[nodiscard]] static PackageArchiveSha256Digest make(
        std::string digest);

    [[nodiscard]] const std::string& value() const noexcept;

    bool operator==(const PackageArchiveSha256Digest&) const = default;

private:
    explicit PackageArchiveSha256Digest(std::string digest) noexcept;

    std::string digest_;
};

// Observation of the same makepkg-managed Git workspace at the defined
// post-preparation and post-build boundaries. It does not claim binary
// reproducibility or detect arbitrary temporary PKGBUILD mutations that were
// perfectly restored before the second observation.
class MakepkgManagedGitWorkspaceRevisionObservation final {
public:
    MakepkgManagedGitWorkspaceRevisionObservation() = delete;
    MakepkgManagedGitWorkspaceRevisionObservation(
        const MakepkgManagedGitWorkspaceRevisionObservation&) = default;
    MakepkgManagedGitWorkspaceRevisionObservation(
        MakepkgManagedGitWorkspaceRevisionObservation&&) noexcept = default;
    MakepkgManagedGitWorkspaceRevisionObservation& operator=(
        const MakepkgManagedGitWorkspaceRevisionObservation&) = default;
    MakepkgManagedGitWorkspaceRevisionObservation& operator=(
        MakepkgManagedGitWorkspaceRevisionObservation&&) noexcept = default;
    ~MakepkgManagedGitWorkspaceRevisionObservation() = default;

    [[nodiscard]] const UpstreamGitRevision& prepared_revision()
        const noexcept;
    [[nodiscard]] const UpstreamGitRevision& post_build_revision()
        const noexcept;

    bool operator==(
        const MakepkgManagedGitWorkspaceRevisionObservation&) const =
        default;

private:
#ifdef MOGUET_ENABLE_DEVEL_BUILD_PROVENANCE_TEST_HOOKS
    friend MakepkgManagedGitWorkspaceRevisionObservation
    make_makepkg_git_workspace_revision_observation_fixture_for_test(
        UpstreamGitRevision prepared_revision,
        UpstreamGitRevision post_build_revision);
#endif

    MakepkgManagedGitWorkspaceRevisionObservation(
        UpstreamGitRevision prepared_revision,
        UpstreamGitRevision post_build_revision) noexcept;

    UpstreamGitRevision prepared_revision_;
    UpstreamGitRevision post_build_revision_;
};

enum class ActualBuiltGitRevisionProofFailureReason {
    SourceIdentityMismatch,
    RevisionMismatch,
};

struct ActualBuiltGitRevisionProofFailure {
    ActualBuiltGitRevisionProofFailureReason reason;

    bool operator==(
        const ActualBuiltGitRevisionProofFailure&) const = default;
};

// Capability for the exact Git revision observed at both bounded workspace
// boundaries. Raw OIDs, reviewed recipe revisions, cache HEADs, and remote
// observations have no constructor path into this type.
class ActualBuiltGitRevision final {
public:
    ActualBuiltGitRevision() = delete;
    ActualBuiltGitRevision(const ActualBuiltGitRevision&) = default;
    ActualBuiltGitRevision(ActualBuiltGitRevision&&) noexcept = default;
    ActualBuiltGitRevision& operator=(const ActualBuiltGitRevision&) = default;
    ActualBuiltGitRevision& operator=(
        ActualBuiltGitRevision&&) noexcept = default;
    ~ActualBuiltGitRevision() = default;

    [[nodiscard]] const UpstreamGitRevision& revision() const noexcept;

    bool operator==(const ActualBuiltGitRevision&) const = default;

private:
    friend std::variant<
        ActualBuiltGitRevision,
        ActualBuiltGitRevisionProofFailure>
    prove_actual_built_git_revision(
        MakepkgManagedGitWorkspaceRevisionObservation observation);

    explicit ActualBuiltGitRevision(
        UpstreamGitRevision revision) noexcept;

    UpstreamGitRevision revision_;
};

using ActualBuiltGitRevisionProofResult = std::variant<
    ActualBuiltGitRevision,
    ActualBuiltGitRevisionProofFailure>;

[[nodiscard]] ActualBuiltGitRevisionProofResult
prove_actual_built_git_revision(
    MakepkgManagedGitWorkspaceRevisionObservation observation);

class DevelBuildProvenanceRecordGeneration final {
public:
    DevelBuildProvenanceRecordGeneration() = delete;

    // Slice 1 models the first CAS generation only. Loading or advancing a
    // persisted generation belongs to the later store Slice.
    [[nodiscard]] static DevelBuildProvenanceRecordGeneration
    initial() noexcept;

    [[nodiscard]] std::uint64_t value() const noexcept;

    bool operator==(
        const DevelBuildProvenanceRecordGeneration&) const = default;

private:
    explicit DevelBuildProvenanceRecordGeneration(
        std::uint64_t value) noexcept;

    std::uint64_t value_;
};

// Archive SHA-256 is historical evidence for the selected bytes. The MTREE
// digest is kept separately because it can be compared with the installed
// local-database MTREE after the archive itself has been cleaned up.
struct BuiltPackageArtifactEvidence {
    ArtifactPackageIdentity identity;
    PackageArchiveSha256Digest archive_digest;
    AlpmMtreeSha256Digest mtree_digest;

    bool operator==(
        const BuiltPackageArtifactEvidence&) const = default;
};

enum class DevelBuildProvenanceFailureReason {
    PackageSourceMismatch,
    EvaluatedSourceMismatch,
    PackageIdentityMismatch,
    PackageBaseMismatch,
    MalformedArtifactIdentity,
    VersionMismatch,
    ArchitectureMismatch,
    MtreeMismatch,
};

struct DevelBuildProvenanceFailure {
    DevelBuildProvenanceFailureReason reason;

    bool operator==(const DevelBuildProvenanceFailure&) const = default;
};

class DevelBuildProvenance final {
public:
    DevelBuildProvenance() = delete;
    DevelBuildProvenance(const DevelBuildProvenance&) = default;
    DevelBuildProvenance(DevelBuildProvenance&&) noexcept = default;
    DevelBuildProvenance& operator=(const DevelBuildProvenance&) = default;
    DevelBuildProvenance& operator=(
        DevelBuildProvenance&&) noexcept = default;
    ~DevelBuildProvenance() = default;

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const DevelBuildProvenanceRecordGeneration&
    record_generation() const noexcept;
    [[nodiscard]] const PackageBaseIdentity& package_base() const noexcept;
    [[nodiscard]] const AurRecipeRevision& reviewed_recipe_revision()
        const noexcept;
    [[nodiscard]] const VcsSourceIdentity& evaluated_source() const noexcept;
    [[nodiscard]] const ActualBuiltGitRevision& actual_built_revision()
        const noexcept;
    [[nodiscard]] const BuiltPackageArtifactEvidence& artifact()
        const noexcept;
    [[nodiscard]] const InstalledArtifactBinding& installed_binding()
        const noexcept;

    bool operator==(const DevelBuildProvenance&) const = default;

private:
    friend std::variant<
        DevelBuildProvenance,
        DevelBuildProvenanceFailure>
    make_devel_build_provenance(
        DevelBuildProvenanceRecordGeneration record_generation,
        PackageBaseIdentity package_base,
        AurRecipeRevision reviewed_recipe_revision,
        VcsSourceIdentity evaluated_source,
        ActualBuiltGitRevision actual_built_revision,
        BuiltPackageArtifactEvidence artifact,
        InstalledArtifactBinding installed_binding);

    DevelBuildProvenance(
        DevelBuildProvenanceRecordGeneration record_generation,
        PackageBaseIdentity package_base,
        AurRecipeRevision reviewed_recipe_revision,
        VcsSourceIdentity evaluated_source,
        ActualBuiltGitRevision actual_built_revision,
        BuiltPackageArtifactEvidence artifact,
        InstalledArtifactBinding installed_binding) noexcept;

    DevelBuildProvenanceRecordGeneration record_generation_;
    PackageBaseIdentity package_base_;
    AurRecipeRevision reviewed_recipe_revision_;
    VcsSourceIdentity evaluated_source_;
    ActualBuiltGitRevision actual_built_revision_;
    BuiltPackageArtifactEvidence artifact_;
    InstalledArtifactBinding installed_binding_;
};

using DevelBuildProvenanceResult = std::variant<
    DevelBuildProvenance,
    DevelBuildProvenanceFailure>;

[[nodiscard]] DevelBuildProvenanceResult make_devel_build_provenance(
    DevelBuildProvenanceRecordGeneration record_generation,
    PackageBaseIdentity package_base,
    AurRecipeRevision reviewed_recipe_revision,
    VcsSourceIdentity evaluated_source,
    ActualBuiltGitRevision actual_built_revision,
    BuiltPackageArtifactEvidence artifact,
    InstalledArtifactBinding installed_binding);

#ifdef MOGUET_ENABLE_DEVEL_BUILD_PROVENANCE_TEST_HOOKS
// Test-only workspace observation mint. The production observation owner is
// intentionally absent from Slice 1.
[[nodiscard]] MakepkgManagedGitWorkspaceRevisionObservation
make_makepkg_git_workspace_revision_observation_fixture_for_test(
    UpstreamGitRevision prepared_revision,
    UpstreamGitRevision post_build_revision);
#endif
