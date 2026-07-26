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

struct AurUpdateSourceBuildPreparation {
    std::vector<AurUpdatePreparationIssue>   issues;
    std::vector<AurUpdatePreparationWarning> warnings;
    std::vector<AurUpdateExecutionTarget>    affected_update_targets;
    std::vector<RootTargetIdentity>          affected_roots;
    std::optional<PreparedProductionSourceBuildInvocation> invocation;

    bool is_prepared() const noexcept;
    bool is_noop() const noexcept;
    bool is_blocked() const noexcept;
};

// Update preflightを、executionへ接続しないowned invocation snapshotへ射影する。
AurUpdateSourceBuildPreparation prepare_aur_update_source_build_invocation(
        const AurUpdateExecutionPreflight& preflight,
        bool needed,
        const AppConfig& config);
