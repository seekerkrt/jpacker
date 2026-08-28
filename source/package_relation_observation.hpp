#pragma once

#include "package_constraint_metadata.hpp"
#include "package_relation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class PackageRelationObservationRole {
    Installed,
    PlannedTarget,
    RepositoryCandidate
};

struct PackageRelationInstalledDatabaseIdentity {
    std::filesystem::path root_dir;
    std::filesystem::path database_path;

    bool operator==(const PackageRelationInstalledDatabaseIdentity&) const =
        default;
};

struct PackageRelationAurSourceIdentity {
    std::string package_name;
    std::string package_base;

    bool operator==(const PackageRelationAurSourceIdentity&) const = default;
};

struct PackageRelationLocalSourceIdentity {
    std::filesystem::path canonical_root;
    std::uintmax_t device;
    std::uintmax_t inode;

    bool operator==(const PackageRelationLocalSourceIdentity&) const =
        default;
};

using PackageRelationSourceIdentity = std::variant<
    PackageRelationInstalledDatabaseIdentity,
    ConfiguredRepositoryIdentity,
    PackageRelationAurSourceIdentity,
    PackageRelationLocalSourceIdentity>;

struct PackageRelationRootAttribution {
    std::size_t invocation_index;
    std::string requested_name;

    bool operator==(const PackageRelationRootAttribution&) const = default;
};

struct PackageRelationObservedCapability {
    ProviderCapability capability;
    ObservedVersion observed_version;

    bool operator==(const PackageRelationObservedCapability&) const = default;
};

// Cross-source matching consumes only owned values. PackageBase remains
// separate from the child package identity, and a local source identity keeps
// the invocation-owned filesystem object rather than a renderer label.
struct PackageRelationObservedPackage {
    std::string package_name;
    std::optional<std::string> package_base;
    ObservedVersion package_version;
    std::vector<PackageRelationObservedCapability> provides;
    PackageRelationSourceIdentity source;
    PackageRelationObservationRole role;
    std::vector<PackageRelationRootAttribution> roots;

    bool operator==(const PackageRelationObservedPackage&) const = default;
};

struct PlannedPackageRelationObservation {
    PackageRelationObservedPackage package;
    std::vector<DeclaredPackageRelation> declarations;

    bool operator==(const PlannedPackageRelationObservation&) const = default;
};

enum class PackageRelationObservationCompleteness {
    Complete,
    Partial,
    Unavailable,
    Invalid
};

enum class PackageRelationObservationFailureKind {
    SourceUnavailable,
    PartialSourceFailure,
    InvalidIdentity,
    MalformedMetadata
};

struct PackageRelationObservationFailure {
    PackageRelationObservationFailureKind kind;
    PackageRelationObservationRole role;
    std::optional<PackageRelationSourceIdentity> source;
    std::optional<std::string> package_name;
    std::string diagnostic;

    bool operator==(const PackageRelationObservationFailure&) const = default;
};

// Coverage is source-local because exact-package and provided-component
// observations may come from separate authoritative queries. Query-local
// completion of either identity channel alone is not a complete NoMatch proof.
struct PackageRelationSourceIdentityCoverage {
    PackageRelationSourceIdentity source;
    bool exact_package_identity = false;
    bool provided_component_identity = false;

    bool operator==(const PackageRelationSourceIdentityCoverage&) const =
        default;
};

// required_sources records successful absence as well as presence. A complete
// empty collection can therefore prove absence only when every required source
// has complete exact-package and provided-component identity coverage.
struct PackageRelationObservationSet {
    PackageRelationObservationCompleteness completeness =
        PackageRelationObservationCompleteness::Unavailable;
    std::vector<PackageRelationSourceIdentity> required_sources;
    std::vector<PackageRelationSourceIdentityCoverage>
        source_identity_coverage;
    std::vector<PackageRelationObservedPackage> packages;
    std::vector<PackageRelationObservationFailure> failures;

    bool operator==(const PackageRelationObservationSet&) const = default;
};

enum class PackageRelationIdentityMatchKind {
    ExactPackage,
    ProvidedComponent,
    NoIdentityMatch,
    InvalidInput
};

enum class PackageRelationVersionMatchKind {
    NotApplicable,
    Unconstrained,
    Matched,
    NotMatched,
    Unavailable,
    Invalid
};

enum class PackageRelationMatchInvalidReason {
    InvalidDeclarationTarget,
    InvalidPackageIdentity,
    SourceRoleMismatch,
    SourceIdentityMismatch,
    MissingPackageBase,
    InvalidPackageBase,
    VersionSourceMismatch,
    MalformedCapability,
    InvalidVersionMetadata,
    InvalidRootAttribution
};

struct PackageRelationProvidedCapabilityMatchEvidence {
    std::size_t provided_capability_index;
    PackageRelationVersionMatchKind version_match;

    bool operator==(
        const PackageRelationProvidedCapabilityMatchEvidence&) const =
        default;
};

struct PackageRelationMatchEvidence {
    PackageRelationObservedPackage observed_package;
    PackageRelationIdentityMatchKind identity_match;
    PackageRelationVersionMatchKind version_match;
    std::vector<PackageRelationProvidedCapabilityMatchEvidence>
        provided_capability_evidence;
    std::optional<PackageRelationMatchInvalidReason> invalid_reason;

    bool operator==(const PackageRelationMatchEvidence&) const = default;
};

struct PackageRelationMatchingEvidence {
    PackageRelationObservationCompleteness observation_completeness;
    std::vector<PackageRelationSourceIdentity> required_sources;
    std::vector<PackageRelationSourceIdentityCoverage>
        source_identity_coverage;
    std::vector<PackageRelationMatchEvidence> package_evidence;
    std::vector<PackageRelationObservationFailure> observation_failures;

    bool operator==(const PackageRelationMatchingEvidence&) const = default;
};

// Validates the owned observation structure without comparing it to a
// declaration target or version constraint.
[[nodiscard]] std::optional<PackageRelationMatchInvalidReason>
validate_package_relation_observation(
    const PackageRelationObservedPackage& observed_package) noexcept;

PackageRelationMatchEvidence match_declared_package_relation(
    const DeclaredPackageRelation& relation,
    const PackageRelationObservedPackage& observed_package);

PackageRelationMatchingEvidence match_declared_package_relation(
    const DeclaredPackageRelation& relation,
    const PackageRelationObservationSet& observations);

// Assessment needs the authoritative per-package result so multiple matching
// targets are retained independently. The identity/version decision remains
// owned by this observation module rather than being reconstructed downstream.
[[nodiscard]] bool package_relation_match_is_confirmed(
    const PackageRelationMatchEvidence& evidence) noexcept;

// This answers identity/version matching only. In particular, a matching
// RepositoryCandidate is diagnostic input and is not an active conflict.
[[nodiscard]] bool package_relation_has_confirmed_match(
    const PackageRelationMatchingEvidence& evidence) noexcept;

// Every required source must cover both identity channels. This is stricter
// than query-local observation completeness.
[[nodiscard]] bool package_relation_has_complete_identity_coverage(
    const PackageRelationMatchingEvidence& evidence) noexcept;

// A positive no-match proof requires complete source observation and only
// authoritative identity misses or completed unsatisfied comparisons.
[[nodiscard]] bool package_relation_confirms_no_match(
    const PackageRelationMatchingEvidence& evidence) noexcept;

void add_package_relation_root_attribution(
    PackageRelationObservedPackage& package,
    PackageRelationRootAttribution root);
