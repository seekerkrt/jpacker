#pragma once

#include "artifact_workspace.hpp"

#include <string>
#include <vector>

// Aggregate/CLI projection only. These localized lines describe review/state
// provenance without claiming that makepkg or pacman succeeded.
struct ReviewedSourceProductionOutcomePresentation {
    std::vector<std::string> info_lines;
};

ReviewedSourceProductionOutcomePresentation
format_reviewed_source_production_outcome(
        const std::string& package_base,
        const ProductionSourceBuildProvenance& provenance);

ReviewedSourceProductionOutcomePresentation
format_production_source_build_staged_outcome(
        const std::string& package_base,
        const ProductionSourceBuildStagedOutcome& outcome);
