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

    throw std::logic_error(
            "Planned package target has no package role: " +
            target.package_name);
}
