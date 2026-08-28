#pragma once

#include "build_plan_artifact_target_projection.hpp"
#include "source_install.hpp"

// source_installのgeneric BuildPlan projectionとlocal専用compositionが共有する
// 1 PackageBase unit builder。LocalBuildPlanやlocal root identityは受けない。
ProductionSourceBuildWorkItem prepare_aur_source_build_work_item_internal(
    const ProjectedBuildPlanArtifactTargets& unit,
    const BuildPlan& plan,
    bool use_source_build_preferences,
    bool needed);
