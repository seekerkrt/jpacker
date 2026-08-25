#pragma once

#include "artifact_identity.hpp"
#include "artifact_install_plan.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// stable indexは元のValidatedPackageArtifactSetと組み合わせた場合だけpathへ解決できる。
// このpure value自体はfilesystem ownershipやcleanup capabilityを持たない。
struct CorrelatedSelectedPackageArtifact {
    std::size_t             artifact_index;
    ArtifactPackageIdentity identity;
    DesiredInstallReason    desired_reason;
};

struct CorrelatedUnselectedPackageArtifact {
    std::size_t             artifact_index;
    ArtifactPackageIdentity identity;
};

struct PackageBaseArtifactIdentitySelectionSuccess {
    std::string                                      package_base;
    // POLICY(#268): selectedはrequired target順、unselectedはartifact aggregate順。
    std::vector<CorrelatedSelectedPackageArtifact>   selected_artifacts;
    std::vector<CorrelatedUnselectedPackageArtifact> unselected_artifacts;
};

// Failure時の部分照合は表示用diagnosticに限定し、aggregateのstable indexを持たせない。
struct DiagnosticPackageArtifactIdentityMatch {
    std::size_t             required_target_index;
    ProducedPackageArtifact artifact;
    DesiredInstallReason    desired_reason;
};

// Duplicateというtyped diagnosticとpackage nameを保ちつつ、該当artifact indexは公開しない。
struct DuplicateProducedArtifactIdentityDiagnostic {
    std::string package_name;
};

// Required target由来の位置だけを明示し、produced artifact位置はredactする。
struct ArtifactIdentitySelectionIdentityInconsistency {
    ArtifactSelectionIdentityInput  input;
    std::optional<std::size_t>       required_target_index;
    std::string                      identity;
};

struct PackageBaseArtifactIdentitySelectionFailure {
    std::string package_base;
    std::vector<DiagnosticPackageArtifactIdentityMatch>
            diagnostic_partial_matches;
    std::vector<ProducedPackageArtifact> diagnostic_unselected_artifacts;
    std::vector<MissingRequiredArtifact> missing_required_artifacts;
    std::vector<DuplicateProducedArtifactIdentityDiagnostic>
            duplicate_produced_identities;
    std::vector<DuplicateRequiredArtifactTarget> duplicate_required_targets;
    std::vector<RequiredArtifactReasonConflict> required_reason_conflicts;
    std::vector<RequiredArtifactAttributionMismatch> attribution_mismatches;
    std::vector<ArtifactIdentitySelectionIdentityInconsistency>
            identity_inconsistencies;
    std::vector<UnknownArtifactInstallReason> unknown_install_reasons;
};

class PackageBaseArtifactIdentitySelectionResult final {
  public:
    PackageBaseArtifactIdentitySelectionResult() = delete;
    PackageBaseArtifactIdentitySelectionResult(
            const PackageBaseArtifactIdentitySelectionResult&) = default;
    PackageBaseArtifactIdentitySelectionResult(
            PackageBaseArtifactIdentitySelectionResult&&) noexcept = default;
    PackageBaseArtifactIdentitySelectionResult& operator=(
            const PackageBaseArtifactIdentitySelectionResult&) = delete;
    PackageBaseArtifactIdentitySelectionResult& operator=(
            PackageBaseArtifactIdentitySelectionResult&&) noexcept = delete;
    ~PackageBaseArtifactIdentitySelectionResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const PackageBaseArtifactIdentitySelectionSuccess* success()
            const noexcept;
    [[nodiscard]] const PackageBaseArtifactIdentitySelectionFailure* failure()
            const noexcept;

  private:
    explicit PackageBaseArtifactIdentitySelectionResult(
            PackageBaseArtifactIdentitySelectionSuccess success);
    explicit PackageBaseArtifactIdentitySelectionResult(
            PackageBaseArtifactIdentitySelectionFailure failure);

    std::variant<PackageBaseArtifactIdentitySelectionSuccess,
                 PackageBaseArtifactIdentitySelectionFailure>
            outcome_;

    friend PackageBaseArtifactIdentitySelectionResult
    correlate_package_base_artifact_identities(
            const std::string& package_base,
            const std::vector<RequiredPackageArtifactTarget>& required_targets,
            const ArtifactPackageIdentitySet& identities);
};

// 同じidentity setからselector requestとcorrelated resultを一回で作り、
// selector requestと別のidentity collectionを差し替えられる境界を公開しない。
PackageBaseArtifactIdentitySelectionResult
correlate_package_base_artifact_identities(
        const std::string& package_base,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ArtifactPackageIdentitySet& identities);
