#pragma once

#include "aur_rpc.hpp"
#include "repository_query.hpp"

#include <optional>
#include <string>
#include <vector>

// 複数 provider がある依存。#97 ではここで止め、暗黙選択しない。
struct AmbiguousProvidedDependency {
    std::string dependency;
    std::vector<ProvidedDependency> candidates;
};

// 依存を official repo / AUR / provider / unknown に分けた結果。
struct DependencyClassification {
    std::vector<std::string>             repo;
    std::vector<std::string>             aur;
    std::vector<std::string>             provided;
    std::vector<AmbiguousProvidedDependency> ambiguous_providers;
    std::vector<std::string>             unknown;
};

enum class DependencyKind {
    Repo,
    Aur,
    Provided,
    AmbiguousProvider,
    Unknown
};

// recursive dependency tree の 1 node。表示と循環検出結果を同じ単位で持つ。
struct RecursiveDependencyNode {
    std::string                          dependency;
    std::string                          package_name;
    std::string                          package_base;
    std::optional<ProvidedDependency>    provided_by;
    std::vector<ProvidedDependency>      provider_candidates;
    DependencyKind                       kind = DependencyKind::Unknown;
    bool                                 already_visited = false;
    bool                                 max_depth_reached = false;
    std::vector<RecursiveDependencyNode> children;
};

// build plan 内で、同じ PackageBase から生成される package 群を束ねる。
struct BuildPlanEntry {
    std::string              package_base;
    std::vector<std::string> package_names;
};

// install 実行時に、PackageBase とは別の package name を明示選択する必要がある target。
struct BuildPlanSplitPackageTarget {
    std::string package_base;
    std::string package_name;
};

// build plan 上で provider により解決された依存を記録する。
struct BuildPlanProvidedDependency {
    std::string        dependency;
    ProvidedDependency provider;
};

// AUR package が宣言する conflicts / replaces を、解決済みと誤認せず plan に残す。
// POLICY(#150): v1.x では installed/repo DB との照合や置換先選択を行わず、raw metadata を保持する。
struct BuildPlanMetadataRisk {
    std::string              package_name;
    std::string              package_base;
    std::vector<std::string> conflicts;
    std::vector<std::string> replaces;
};

// AUR build / fetch の順序、未解決依存、循環検出結果をまとめる計画。
struct BuildPlan {
    std::vector<BuildPlanEntry>              order;
    std::vector<BuildPlanSplitPackageTarget> split_package_targets;
    std::vector<BuildPlanProvidedDependency> provided;
    std::vector<BuildPlanMetadataRisk>       metadata_risks;
    std::vector<AmbiguousProvidedDependency> ambiguous_providers;
    std::vector<std::string>                 unresolved;
    std::vector<std::string>                 cycles;
};

std::vector<std::string> collect_build_dependencies(const AurPackageInfo& pkg);
DependencyClassification classify_dependencies(const std::vector<std::string>& dependencies);
std::vector<RecursiveDependencyNode> resolve_recursive_dependencies(const AurPackageInfo& pkg);
std::vector<BuildPlanMetadataRisk> collect_build_plan_metadata_risks(const AurPackageInfo& pkg);
BuildPlan resolve_build_plan(const std::string& target);
BuildPlan resolve_fetch_plan(const std::string& target);
void require_fetchable_build_plan(const std::string& target, const BuildPlan& plan);
void require_executable_build_plan(const std::string& target, const BuildPlan& plan);
void require_executable_install_plan(const std::string& target, const BuildPlan& plan);
