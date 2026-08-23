#pragma once

struct BuildPlan;

// Production orchestration seam. Resolver-focused tests replace this symbol
// without linking the installed metadata/query stack.
void finalize_build_plan_relation_assessments(BuildPlan& plan);
