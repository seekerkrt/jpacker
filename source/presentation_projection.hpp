#pragma once

#include "dependency_plan.hpp"
#include "diagnostic_model.hpp"
#include "operation_state_model.hpp"
#include "upgrade_all_operation.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct PresentationArtifactIdentity {
    std::string package_name;
    std::string full_version;

    bool operator==(const PresentationArtifactIdentity&) const = default;
};

enum class PlanPresentationReasonKind {
    ConstraintAuthority,
    SelectedProviderIdentityConflict,
    ConstraintReadiness,
    ResolutionFailure,
    UnresolvedDependency,
    AmbiguousProvider,
    DependencyCycle,
    DeclaredRelation,
    SplitPackage,
    IncompleteProviderCandidate,
};

using PlanPresentationReasonDetail = std::variant<
        std::monostate,
        PlanConstraintAuthorityIssueKind,
        ConstraintSatisfaction,
        BuildPlanResolutionFailureKind,
        PackageRelationAssessmentKind,
        ObservedVersionUnknownReason>;

struct PlanPresentationReason {
    PlanPresentationReasonKind   kind =
            PlanPresentationReasonKind::ConstraintAuthority;
    PlanPresentationReasonDetail detail;
    ExecutionCapability          capability = ExecutionCapability::Install;
    ExecutionReadinessState      readiness =
            ExecutionReadinessState::NotAssessed;
    bool                         blocks_production_guard = false;
    PlanRequiredAction           required_action = PlanRequiredAction::None;
    std::optional<std::size_t>   edge_index;
    std::optional<std::string>   subject;
    std::optional<std::string>   package_base;

    bool operator==(const PlanPresentationReason&) const = default;
};

enum class UpgradeAllPresentationBoundaryReason {
    AggregateDiagnostic,
    AurPhaseDiagnostic,
    AurQueryFailure,
};

using UpgradeAllPresentationReasonValue = std::variant<
        UpgradeAllPresentationBoundaryReason,
        OperationOutcome,
        UpgradeAllOperationStatus,
        UpgradeAllOperationIssueKind,
        SystemUpgradePhaseStatus,
        UpgradeAllForeignInventoryPhaseStatus,
        PackageMetadataErrorCode,
        RegisteredSourceUpgradeStatus,
        RegisteredSourceUpgradeFailureKind,
        AurUpdatePreparationReason,
        UpgradeAllPlanningIssueKind,
        AurUpdateOperationReductionReason,
        FilteredAurUpdateOperationIssueKind,
        AurUpdateOperationTargetStatus,
        AurUpdateWorkItemFailureKind>;

struct PresentationCorrelationIdentity {
    std::optional<std::size_t> adapter_index;
    std::optional<std::size_t> original_preference_index;
    std::optional<std::size_t> original_query_plan_index;
    std::optional<std::size_t> planner_target_index;
    std::optional<std::size_t> selected_target_index;
    std::optional<std::size_t> filtered_update_plan_index;
    std::optional<std::size_t> preflight_invocation_index;
    std::optional<std::size_t> build_plan_root_index;
    std::optional<std::size_t> build_plan_order_index;
    std::optional<std::size_t> invocation_work_item_index;
    std::vector<std::size_t>   affected_update_plan_indices;
    std::vector<std::size_t>   original_target_indices;
    std::vector<std::size_t>   build_unit_indices;
    std::optional<std::string> package_name;
    std::optional<std::string> package_base;
    std::optional<std::string> canonical_source_identity;

    bool operator==(const PresentationCorrelationIdentity&) const = default;
};

struct UpgradeAllPresentationReason {
    UpgradeAllPresentationReasonValue reason;
    UpgradeAllOperationPhase          phase = UpgradeAllOperationPhase::None;
    DiagnosticSourceKind              source_kind =
            DiagnosticSourceKind::Unspecified;
    DiagnosticRequiredAction          required_action =
            DiagnosticRequiredAction::None;
    PresentationCorrelationIdentity   correlation;
    std::optional<std::string>         supplemental_detail;

    bool operator==(const UpgradeAllPresentationReason&) const = default;
};

struct PresentationItem {
    DiagnosticSourceKind                       source_kind =
            DiagnosticSourceKind::Unspecified;
    std::optional<std::string>                 repository;
    std::optional<std::string>                 requested_package;
    std::optional<std::string>                 package_base;
    std::optional<std::string>                 canonical_source_identity;
    std::optional<std::filesystem::path>       local_root;
    std::vector<PresentationArtifactIdentity> selected_artifacts;
    std::vector<PresentationArtifactIdentity> unselected_artifacts;
    std::optional<PackageStateObservationValue> package_state;
    std::optional<AurUpdateExecutionReason>     aur_normal_skip_reason;
    std::optional<DiagnosticClass>             diagnostic_class;
    std::vector<PlanPresentationReason>        plan_reasons;
    std::vector<UpgradeAllPresentationReason>  upgrade_all_reasons;
    bool                                       is_update_candidate = false;
    bool                                       is_blocking = false;
    bool                                       requires_check = false;
    bool                                       requires_manual_action = false;

    bool operator==(const PresentationItem&) const = default;
};

struct PresentationSummaryCounts {
    std::size_t total = 0;
    std::size_t normal = 0;
    std::size_t attention_required = 0;
    std::size_t update_candidates = 0;
    std::size_t blockers = 0;
    std::size_t requires_check = 0;
    std::size_t failures = 0;
    std::size_t unverified = 0;
    std::size_t not_observed = 0;
    std::size_t split_identities = 0;

    bool operator==(const PresentationSummaryCounts&) const = default;
};

struct PresentationProjection {
    PresentationSummaryCounts     summary_counts;
    std::vector<PresentationItem> attention_items;
    std::vector<PresentationItem> full_items;

    bool operator==(const PresentationProjection&) const = default;
};

bool has_distinct_package_base_identity(
        const PresentationItem& item) noexcept;
bool has_distinct_artifact_identity(
        const PresentationItem& item) noexcept;
bool is_attention_required(const PresentationItem& item) noexcept;
bool should_suppress_repeated_package_base_identity(
        const PresentationItem& item) noexcept;

PresentationProjection partition_presentation_items(
        std::vector<PresentationItem> items);

PresentationItem project_registered_source_presentation_item(
        const RegisteredSourcePreferenceSnapshot* source,
        const RegisteredSourceUpgradeResult& result);

PresentationItem project_aur_update_presentation_item(
        const AurUpdateOperationTargetResult& result);

PresentationProjection project_upgrade_all_presentation(
        const UpgradeAllOperationResult& result);

PresentationProjection project_upgrade_all_presentation_with_operation_state(
        const UpgradeAllOperationResult& result,
        const OperationStateProjection& operation_state);

PresentationProjection project_build_plan_presentation(
        const BuildPlan& plan);
