#include "presentation_projection.hpp"

PresentationProjection project_upgrade_all_presentation(
        const UpgradeAllOperationResult& result) {
    return project_upgrade_all_presentation_with_operation_state(
            result, project_upgrade_all_operation_state(result));
}
