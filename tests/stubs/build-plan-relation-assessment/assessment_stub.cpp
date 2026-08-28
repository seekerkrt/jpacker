#include "build_plan_relation_assessment.hpp"

#include "dependency_plan.hpp"

#include <optional>

void finalize_build_plan_relation_assessments(BuildPlan& plan) {
    const PackageRelationInstalledDatabaseIdentity source{
        "/", "/var/lib/pacman"};
    const PackageRelationMatchingEvidence complete_no_match_evidence{
        PackageRelationObservationCompleteness::Complete,
        {source},
        {PackageRelationSourceIdentityCoverage{
            PackageRelationSourceIdentity{source}, true, true}},
        {},
        {}};
    plan.relation_assessments.clear();
    for(const auto& observation : plan.planned_relation_observations) {
        for(const auto& declaration : observation.declarations) {
            // Resolver/model tests consume an already-computed assessment.
            // Pure matching/classification is owned by its dedicated target.
            plan.relation_assessments.push_back(
                PackageRelationAssessment{
                    declaration,
                    PackageRelationAssessmentKind::
                        ConfirmedNoMatchingCurrentOrPlannedTarget,
                    observation.package,
                    complete_no_match_evidence,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt});
        }
    }
}
