#include "build_plan_relation_assessment.hpp"

#include "dependency_plan.hpp"
#include "installed_package_relation_inventory.hpp"
#include "package_relation_assessment.hpp"

#include <algorithm>

void finalize_build_plan_relation_assessments(BuildPlan& plan) {
    plan.relation_assessments.clear();
    const bool has_declarations = std::any_of(
            plan.planned_relation_observations.begin(),
            plan.planned_relation_observations.end(),
            [](const PlannedPackageRelationObservation& observation) {
                return !observation.declarations.empty();
            });
    if(!has_declarations) return;

    const PackageRelationObservationSet installed =
            query_installed_package_relation_observations();
    plan.relation_assessments = assess_package_relations(
            installed, plan.planned_relation_observations);
}
