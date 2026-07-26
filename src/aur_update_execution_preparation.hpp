#pragma once

#include "aur_update_execution_preflight.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct AppConfig;
struct AurUpdateSourceBuildPreparation;
struct AurUpdateSourceBuildExecutionResult;

enum class AurUpdatePreparationReason {
    None,
    BlockingPreflight,
    PreflightInconsistent,
    BuildPlanMissing,
    BuildPlanOrderEmpty,
    RootAttributionInconsistent,
    PackageTargetAttributionInconsistent,
    DesiredInstallReasonMissing,
    SourcePreferenceUnavailable,
    SourcePreferencePkgdestConflict,
    StaticWorkItemInvalid,
    PacmanDatabaseUnavailable,
    GenericPreparationInconsistent,
};

// preparation issueは表示文字列ではなくreasonと既存typed failureを正本にする。
struct AurUpdatePreparationIssue {
    AurUpdatePreparationReason             reason = AurUpdatePreparationReason::None;
    std::vector<std::size_t>               affected_update_plan_indices;
    std::vector<RootTargetIdentity>        affected_roots;
    std::optional<std::string>             package_name;
    std::optional<std::string>             package_base;
    std::optional<AurUpdateExecutionIssue> preflight_issue;
    std::optional<SourcePreferenceFailure> source_preference_failure;
    std::optional<PackageMetadataFailure>  package_metadata_failure;
    std::string                            diagnostic;
};

// strict readerが返したwarningを、read順と対象attributionごとowned valueへ写す。
struct AurUpdatePreparationWarning {
    std::string                     preference_name;
    std::filesystem::path           entry_path;
    std::vector<std::size_t>        affected_update_plan_indices;
    std::vector<RootTargetIdentity> affected_roots;
    std::string                     diagnostic;
};

// BuildPlan::order上の1 work itemと、影響するupdate target/rootを固定する。
struct AurUpdatePreparedWorkItemAttribution {
    std::size_t                     invocation_work_item_index = 0;
    std::string                     package_name;
    std::string                     package_base;
    std::vector<std::size_t>        affected_update_plan_indices;
    std::vector<RootTargetIdentity> affected_roots;
};

// generic production invocationとupdate固有attributionを相関済みで所有する。
// POLICY(#267): execution-bearing generic invocationを外部へ公開せず、parallel
// vectorのcount/order/identityをpreparation後に書き換えられないようにする。
// moveはexecution capabilityを移し、move元を明示的にinvalid化する。
class PreparedAurUpdateSourceBuildInvocation final {
    PreparedProductionSourceBuildInvocation production_invocation_;
    std::vector<AurUpdatePreparedWorkItemAttribution>
            work_item_attributions_;
    bool valid_ = true;

    PreparedAurUpdateSourceBuildInvocation(
            PreparedProductionSourceBuildInvocation&& production_invocation,
            std::vector<AurUpdatePreparedWorkItemAttribution>&&
                    work_item_attributions) noexcept;

    friend AurUpdateSourceBuildPreparation
    prepare_aur_update_source_build_invocation(
            const AurUpdateExecutionPreflight& preflight,
            bool needed,
            const AppConfig& config);
    friend struct AurUpdateSourceBuildPreparation;
    friend AurUpdateSourceBuildExecutionResult
    execute_prepared_aur_update_source_build_invocation(
            PreparedAurUpdateSourceBuildInvocation invocation,
            const AppConfig& config);

public:
    PreparedAurUpdateSourceBuildInvocation(
            const PreparedAurUpdateSourceBuildInvocation&) = delete;
    PreparedAurUpdateSourceBuildInvocation& operator=(
            const PreparedAurUpdateSourceBuildInvocation&) = delete;
    PreparedAurUpdateSourceBuildInvocation(
            PreparedAurUpdateSourceBuildInvocation&& other) noexcept;
    PreparedAurUpdateSourceBuildInvocation& operator=(
            PreparedAurUpdateSourceBuildInvocation&&) = delete;
    ~PreparedAurUpdateSourceBuildInvocation() noexcept = default;

#ifdef JPACKER_ENABLE_AUR_UPDATE_EXECUTION_PREPARATION_TEST_HOOKS
    // preparationのexact field assertion専用。production buildではgeneric
    // executorへ渡せる内部snapshotを公開しない。
    const PreparedProductionSourceBuildInvocation&
    production_invocation_for_test() const noexcept {
        return production_invocation_;
    }
#endif

    const std::vector<AurUpdatePreparedWorkItemAttribution>&
    work_item_attributions() const noexcept {
        return work_item_attributions_;
    }

    bool is_valid() const noexcept {
        return valid_;
    }
};

struct AurUpdateSourceBuildPreparation {
    std::vector<AurUpdatePreparationIssue>   issues;
    std::vector<AurUpdatePreparationWarning> warnings;
    std::vector<AurUpdateExecutionTarget>    affected_update_targets;
    std::vector<RootTargetIdentity>          affected_roots;
    std::optional<PreparedAurUpdateSourceBuildInvocation> invocation;

    bool is_prepared() const noexcept;
    bool is_noop() const noexcept;
    bool is_blocked() const noexcept;
};

// Update preflightを、executionへ接続しないowned invocation snapshotへ射影する。
AurUpdateSourceBuildPreparation prepare_aur_update_source_build_invocation(
        const AurUpdateExecutionPreflight& preflight,
        bool needed,
        const AppConfig& config);
