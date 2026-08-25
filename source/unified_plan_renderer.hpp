#pragma once

#include "unified_plan_observation.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

enum class UnifiedPlanRenderingIssueKind {
    UnsupportedValue,
    MissingReferencedValue
};

enum class UnifiedPlanRenderingSection {
    Status,
    Roots,
    Phases,
    Dependencies,
    BuildUnits,
    RequiredArtifacts,
    TransactionIntents,
    Blockers
};

// Presentation-local failures never change the observation status or become
// an execution/exit-status decision. The diagnostic is owned so no borrowed
// production authority escapes the render call.
struct UnifiedPlanRenderingIssue {
    UnifiedPlanRenderingIssueKind kind;
    UnifiedPlanRenderingSection   section;
    std::size_t                   item_index;
    std::optional<std::size_t>    detail_index;
    std::string                   diagnostic;

    bool operator==(const UnifiedPlanRenderingIssue&) const = default;
};

struct UnifiedPlanRenderingResult {
    std::string                            text;
    std::vector<UnifiedPlanRenderingIssue> issues;

    [[nodiscard]] bool is_complete() const noexcept {
        return issues.empty();
    }
};

// The renderer observes the existing vector order and typed identities. It
// does not resolve, sort, rebuild transaction intent, or retain references.
UnifiedPlanRenderingResult render_unified_plan_observation(
        const UnifiedPlanObservation& observation);
