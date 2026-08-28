#pragma once

#include "dependency_plan.hpp"

#include <optional>
#include <string>
#include <utility>

namespace package_relation_assessment_fixture {

inline PlanDeclaredRelationReason confirmed_installed_conflict_reason(
    std::string declaring_package, std::string declaring_package_base,
    std::string target_component,
    std::string requested_root = "fixture-root") {
    const std::string declaring_source_name = declaring_package;
    const PackageRelationInstalledDatabaseIdentity installed_source{
        "/", "/var/lib/pacman"};
    const PackageRelationSourceIdentity installed_identity{installed_source};
    const PackageRelationObservedPackage matched_package{
        target_component,
        std::nullopt,
        ObservedVersion::available(
            ObservedVersionSource::InstalledExactPackage, "1"),
        {},
        installed_source,
        PackageRelationObservationRole::Installed,
        {}};
    const PackageRelationMatchEvidence matched_evidence{
        matched_package,
        PackageRelationIdentityMatchKind::ExactPackage,
        PackageRelationVersionMatchKind::Unconstrained,
        {},
        std::nullopt};
    const PackageRelationMatchingEvidence active_evidence{
        PackageRelationObservationCompleteness::Complete,
        {installed_identity},
        {PackageRelationSourceIdentityCoverage{
            installed_identity, true, true}},
        {matched_evidence},
        {}};

    return PlanDeclaredRelationReason{PackageRelationAssessment{
        DeclaredPackageRelation{
            declaring_package,
            declaring_package_base,
            PackageRelationKind::Conflict,
            target_component,
            target_component,
            std::nullopt},
        PackageRelationAssessmentKind::ConfirmedInstalledConflict,
        PackageRelationObservedPackage{
            std::move(declaring_package),
            declaring_package_base,
            ObservedVersion::available(
                ObservedVersionSource::AurExactPackage, "1"),
            {},
            PackageRelationAurSourceIdentity{
                declaring_source_name, declaring_package_base},
            PackageRelationObservationRole::PlannedTarget,
            {{0, std::move(requested_root)}}},
        active_evidence,
        std::nullopt,
        matched_evidence,
        std::nullopt}};
}

} // namespace package_relation_assessment_fixture
