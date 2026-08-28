#pragma once

#include "package_relation_observation.hpp"

#include <optional>
#include <string_view>
#include <vector>

enum class PackageRelationAssessmentKind {
    DeclaredRelation,
    ConfirmedInstalledConflict,
    ConfirmedPlannedTargetConflict,
    PotentialReplacement,
    ConfirmedNoMatchingCurrentOrPlannedTarget,
    Unknown,
    Invalid
};

// One declaration may produce several assessments. In particular, confirmed
// targets and partial/invalid observation evidence coexist instead of being
// reduced to a priority enum. All values are owned snapshots; no libalpm
// pointer crosses this boundary.
struct PackageRelationAssessment {
    DeclaredPackageRelation declaration;
    PackageRelationAssessmentKind kind;
    PackageRelationObservedPackage declaring_package;
    PackageRelationMatchingEvidence active_evidence;
    std::optional<PackageRelationMatchingEvidence>
        repository_context_evidence;
    std::optional<PackageRelationMatchEvidence> attributed_package_evidence;
    std::optional<PackageRelationObservationFailure>
        attributed_observation_failure;

    bool operator==(const PackageRelationAssessment&) const = default;
};

// Installed and PlannedTarget observations are the only active/current
// authority. RepositoryCandidate observations are retained as context but can
// neither create an active relation nor prevent a complete active NoMatch.
std::vector<PackageRelationAssessment> assess_package_relations(
    const PackageRelationObservationSet& installed_observations,
    const std::vector<PlannedPackageRelationObservation>&
        planned_observations,
    const std::optional<PackageRelationObservationSet>&
        repository_context = std::nullopt);

[[nodiscard]] constexpr std::string_view
package_relation_assessment_kind_token(
    PackageRelationAssessmentKind kind) noexcept {
    switch(kind) {
        case PackageRelationAssessmentKind::DeclaredRelation:
            return "DeclaredRelation";
        case PackageRelationAssessmentKind::ConfirmedInstalledConflict:
            return "ConfirmedInstalledConflict";
        case PackageRelationAssessmentKind::ConfirmedPlannedTargetConflict:
            return "ConfirmedPlannedTargetConflict";
        case PackageRelationAssessmentKind::PotentialReplacement:
            return "PotentialReplacement";
        case PackageRelationAssessmentKind::
            ConfirmedNoMatchingCurrentOrPlannedTarget:
            return "ConfirmedNoMatchingCurrentOrPlannedTarget";
        case PackageRelationAssessmentKind::Unknown:
            return "Unknown";
        case PackageRelationAssessmentKind::Invalid:
            return "Invalid";
    }
    return "Invalid";
}
