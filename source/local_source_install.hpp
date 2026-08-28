#pragma once

#include "local_source_build.hpp"
#include "separated_package_base_source_build.hpp"

// Validated local artifact aggregateをone-shotで既存PackageBase install
// planner/executorへ渡す。raw path、artifact index、install directiveはcallerへ
// 公開しない。
PackageBaseSourceBuildExecutionResult execute_local_source_install(
    LocalSourceBuildResult result,
    const PacmanDatabasePaths& database_paths,
    const SeparatedSourceBuildUnitOptions& options);
