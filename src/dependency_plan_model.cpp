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
