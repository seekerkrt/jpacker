#include "dependency_plan.hpp"

#include <algorithm>
#include <stdexcept>

DesiredInstallReason desired_install_reason(
        const PlannedPackageTarget& target) {
    if(std::find(
               target.roles.begin(), target.roles.end(), PackageRole::Root) !=
       target.roles.end()) {
        return DesiredInstallReason::Explicit;
    }

    for(const auto role : target.roles) {
        if(role == PackageRole::RuntimeDependency ||
           role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency) {
            return DesiredInstallReason::Dependency;
        }
    }

    // NO_TRANSLATE(Issue #308): production projection validates roles before
    // this reducer and converts an unavailable reason to a typed issue.
    throw std::logic_error(
            "Planned package target has no package role: " +
            target.package_name);
}

bool has_incomplete_constraint_evaluations(const BuildPlan& plan) noexcept {
    return std::any_of(
            plan.dependency_edges.begin(), plan.dependency_edges.end(),
            [](const BuildPlanDependencyEdge& edge) {
                if(!edge.constraint_evaluation.has_value()) return false;
                const ConstraintSatisfaction satisfaction =
                        edge.constraint_evaluation->satisfaction();
                return satisfaction == ConstraintSatisfaction::Unsatisfied ||
                       satisfaction == ConstraintSatisfaction::Unknown;
            });
}
