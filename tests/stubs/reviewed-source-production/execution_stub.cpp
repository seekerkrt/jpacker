#include "artifact_install_plan.hpp"
#include "separated_package_base_source_build.hpp"
#include "separated_source_build.hpp"

#include <stdexcept>

void require_supported_separated_install_options(bool) {
}

ArtifactInstallExecutionOutcome execute_separated_source_build_unit(
        SeparatedSourceBuildUnitRequest,
        const SeparatedSourceBuildUnitOptions&) {
    throw std::logic_error(
            "Reviewed source production preparation test reached execution.");
}

PackageBaseSourceBuildExecutionResult
execute_separated_package_base_source_build(
        SeparatedPackageBaseSourceBuildRequest,
        const SeparatedSourceBuildUnitOptions&) {
    throw std::logic_error(
            "Reviewed source production preparation test reached PackageBase execution.");
}

SeparatedPackageBaseSourceBuildPhaseError::
        SeparatedPackageBaseSourceBuildPhaseError(
                SeparatedPackageBaseSourceBuildFailurePhase phase,
                const std::string& diagnostic,
                std::optional<ReviewedSourceProductionFailure>
                        reviewed_source_failure)
    : std::runtime_error(diagnostic), phase_(phase),
      reviewed_source_failure_(std::move(reviewed_source_failure)) {
}

SeparatedPackageBaseSourceBuildFailurePhase
SeparatedPackageBaseSourceBuildPhaseError::phase() const noexcept {
    return phase_;
}

const std::optional<ReviewedSourceProductionFailure>&
SeparatedPackageBaseSourceBuildPhaseError::reviewed_source_failure()
        const noexcept {
    return reviewed_source_failure_;
}
