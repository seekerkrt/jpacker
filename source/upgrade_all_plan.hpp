#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// PackageBaseが存在しない状態と、解決処理自体が失敗した状態を混同しない。
struct UpgradeAllPackageBaseAbsent {};

struct UpgradeAllResolvedPackageBase {
    std::string package_base;
};

struct UpgradeAllPackageBaseResolutionFailed {
    std::string diagnostic;
};

using UpgradeAllPackageBaseIdentity = std::variant<
        UpgradeAllPackageBaseAbsent,
        UpgradeAllResolvedPackageBase,
        UpgradeAllPackageBaseResolutionFailed>;

// 同じsourceを指す複数preferenceだけを安全に正規化するためのidentity key。
struct UpgradeAllSourceIdentityAbsent {};

struct UpgradeAllResolvedSourceIdentity {
    std::string key;
};

struct UpgradeAllSourceIdentityResolutionFailed {
    std::string diagnostic;
};

using UpgradeAllSourceIdentity = std::variant<
        UpgradeAllSourceIdentityAbsent,
        UpgradeAllResolvedSourceIdentity,
        UpgradeAllSourceIdentityResolutionFailed>;

// PR1のplannerは、source discoveryが所有するresolved valueだけを受け取る。
struct UpgradeAllExplicitSourceIdentity {
    std::string                       preference_package_name;
    UpgradeAllPackageBaseIdentity    package_base;
    std::vector<std::string>         produced_package_names;
    UpgradeAllSourceIdentity         source_identity;
};

enum class UpgradeAllAurTargetStatus {
    Candidate,
    Unsupported,
    Incomplete
};

struct UpgradeAllAurTarget {
    std::string                       package_name;
    UpgradeAllPackageBaseIdentity    package_base;
    UpgradeAllAurTargetStatus        status = UpgradeAllAurTargetStatus::Candidate;
    std::string                       status_detail;
};

enum class UpgradeAllBuildUnitRole {
    Root,
    RuntimeDependency,
    BuildDependency,
    CheckDependency
};

struct UpgradeAllBuildUnitRootAttribution {
    std::size_t                  original_target_index = 0;
    UpgradeAllBuildUnitRole      role = UpgradeAllBuildUnitRole::Root;
};

struct UpgradeAllAurBuildUnit {
    UpgradeAllPackageBaseIdentity                   package_base;
    std::vector<std::string>                        package_names;
    std::vector<UpgradeAllBuildUnitRootAttribution> root_attributions;
};

struct UpgradeAllPlanInput {
    std::vector<UpgradeAllExplicitSourceIdentity> explicit_sources;
    std::vector<UpgradeAllAurTarget>              aur_targets;
    std::vector<UpgradeAllAurBuildUnit>           build_units;
};

enum class UpgradeAllTargetDisposition {
    Selected,
    ExcludedByExplicitPackageName,
    ExcludedByExplicitPackageBase,
    Unsupported,
    IdentityIncomplete,
    ConflictingExplicitSourceIdentity,
    ConflictingSelectedPackageBase
};

enum class UpgradeAllBuildUnitDisposition {
    SelectedForAurExecution,
    ExternallySatisfiedByExplicitSourcePackageBase,
    NotRequiredBySelectedTarget,
    IdentityIncomplete,
    ConflictingExplicitSourceIdentity,
    ConflictingSelectedPackageBase
};

enum class UpgradeAllPlanningIssueKind {
    ExplicitPreferencePackageNameMissing,
    ExplicitProducedPackageNameMissing,
    ExplicitPackageBaseAbsent,
    ExplicitPackageBaseResolutionFailed,
    ExplicitPackageBaseEmpty,
    ExplicitSourceIdentityAbsent,
    ExplicitSourceIdentityResolutionFailed,
    ExplicitSourceIdentityEmpty,
    ConflictingExplicitSourceIdentityDefinition,
    ConflictingExplicitPackageName,
    ConflictingExplicitPackageBase,
    AurTargetPackageNameMissing,
    AurTargetPackageBaseAbsent,
    AurTargetPackageBaseResolutionFailed,
    AurTargetPackageBaseEmpty,
    UnsupportedAurTarget,
    IncompleteAurTarget,
    BuildUnitPackageBaseAbsent,
    BuildUnitPackageBaseResolutionFailed,
    BuildUnitPackageBaseEmpty,
    BuildUnitHasNoRootAttribution,
    BuildUnitTargetIndexOutOfRange,
    DuplicateSelectedTargetPackageBase,
    DuplicateSelectedBuildUnitPackageBase
};

// 入力相関の問題はthrowせず、既知のdispositionと並べて保持する。
struct UpgradeAllPlanningIssue {
    UpgradeAllPlanningIssueKind  kind;
    std::vector<std::size_t>     explicit_source_indexes;
    std::vector<std::size_t>     original_target_indexes;
    std::vector<std::size_t>     build_unit_indexes;
    std::optional<std::string>   package_name;
    std::optional<std::string>   package_base;
};

struct UpgradeAllExplicitSourceAttribution {
    std::vector<std::size_t>     explicit_source_indexes;
    std::vector<std::string>     source_identity_keys;
    std::optional<std::string>   matched_package_name;
    std::optional<std::string>   matched_package_base;
};

struct UpgradeAllTargetPlanEntry {
    std::size_t                                  original_target_index = 0;
    UpgradeAllAurTarget                          target;
    UpgradeAllTargetDisposition                  disposition =
            UpgradeAllTargetDisposition::IdentityIncomplete;
    std::optional<std::size_t>                   selected_index;
    std::optional<UpgradeAllExplicitSourceAttribution> explicit_source;
};

struct UpgradeAllSelectedAurTarget {
    std::size_t selected_index = 0;
    std::size_t original_target_index = 0;
    std::string package_name;
    std::string package_base;
};

struct UpgradeAllBuildUnitPlanEntry {
    std::size_t                                  original_build_plan_index = 0;
    UpgradeAllAurBuildUnit                       build_unit;
    UpgradeAllBuildUnitDisposition               disposition =
            UpgradeAllBuildUnitDisposition::IdentityIncomplete;
    std::optional<std::size_t>                   selected_execution_index;
    std::optional<UpgradeAllExplicitSourceAttribution> explicit_source;
};

struct UpgradeAllSelectedBuildUnit {
    std::size_t selected_execution_index = 0;
    std::size_t original_build_plan_index = 0;
    std::string package_base;
};

struct UpgradeAllPlan {
    std::vector<UpgradeAllExplicitSourceIdentity> explicit_sources;
    std::vector<UpgradeAllTargetPlanEntry>         target_dispositions;
    std::vector<UpgradeAllSelectedAurTarget>       selected_targets;
    std::vector<std::optional<std::size_t>>        original_to_selected_index;
    std::vector<std::size_t>                       excluded_duplicate_target_indexes;

    std::vector<UpgradeAllBuildUnitPlanEntry>      build_unit_dispositions;
    std::vector<UpgradeAllSelectedBuildUnit>       selected_build_units;
    std::vector<std::size_t>                       externally_satisfied_build_unit_indexes;
    std::vector<std::string>                       externally_satisfied_package_bases;

    std::vector<UpgradeAllPlanningIssue>           issues;
};

UpgradeAllPlan make_upgrade_all_plan(const UpgradeAllPlanInput& input);
UpgradeAllPlan make_upgrade_all_target_plan(
        const std::vector<UpgradeAllExplicitSourceIdentity>& explicit_sources,
        const std::vector<UpgradeAllAurTarget>& aur_targets);
UpgradeAllPlan complete_upgrade_all_build_unit_plan(
        const UpgradeAllPlan& target_plan,
        const std::vector<UpgradeAllAurBuildUnit>& build_units);
bool has_upgrade_all_planning_issues(const UpgradeAllPlan& plan) noexcept;
