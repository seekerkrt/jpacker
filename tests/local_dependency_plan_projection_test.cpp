#include "local_dependency_plan_projection.hpp"

#include "dependency_provider.hpp"
#include "dependency_spec.hpp"
#include "stubs/local-dependency-plan/query_stub.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

static_assert(!std::is_default_constructible_v<LocalBuildPlan>);
static_assert(std::is_copy_constructible_v<LocalBuildPlan>);
static_assert(std::is_nothrow_move_constructible_v<LocalBuildPlan>);
static_assert(!std::is_copy_assignable_v<LocalBuildPlan>);
static_assert(!std::is_move_assignable_v<LocalBuildPlan>);
static_assert(!std::is_convertible_v<LocalBuildPlan, BuildPlan>);

namespace {

using Comparison = LocalPackageMetadataComparison;
using RelationKind = LocalPackageMetadataRelationKind;
using RelationTarget = LocalPackageMetadataRelationTarget;
using ScopeKind = LocalPackageMetadataScopeKind;
using TargetKind = LocalPackageMetadataRelationTargetKind;

namespace query_stub = local_dependency_plan_query_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

RelationTarget package_target(
        std::string name,
        std::optional<Comparison> comparison = std::nullopt,
        std::optional<std::string> version = std::nullopt) {
    return RelationTarget{
            TargetKind::Package, std::move(name), comparison,
            std::move(version)};
}

RelationTarget soname_target(std::string name) {
    return RelationTarget{
            TargetKind::Soname, std::move(name), std::nullopt, std::nullopt};
}

LocalPackageMetadataRelation base_relation(
        RelationKind kind, std::string raw_value, RelationTarget target,
        std::optional<std::string> architecture_qualifier = std::nullopt) {
    return LocalPackageMetadataRelation{
            kind,
            std::move(raw_value),
            std::move(target),
            LocalPackageMetadataScope{ScopeKind::PackageBase, std::nullopt},
            std::move(architecture_qualifier),
            std::nullopt,
            false};
}

LocalPackageMetadataRelation child_relation(
        RelationKind kind, std::string raw_value, RelationTarget target,
        std::string package_name,
        std::optional<std::string> architecture_qualifier = std::nullopt) {
    return LocalPackageMetadataRelation{
            kind,
            std::move(raw_value),
            std::move(target),
            LocalPackageMetadataScope{
                    ScopeKind::ChildPackage, std::move(package_name)},
            std::move(architecture_qualifier),
            std::nullopt,
            false};
}

LocalPackageMetadataRelation child_unset_relation(
        RelationKind kind, std::string package_name,
        std::optional<std::string> architecture_qualifier = std::nullopt) {
    return LocalPackageMetadataRelation{
            kind,
            "",
            std::nullopt,
            LocalPackageMetadataScope{
                    ScopeKind::ChildPackage, std::move(package_name)},
            std::move(architecture_qualifier),
            std::nullopt,
            true};
}

LocalPackageMetadata metadata_fixture(
        std::string package_base,
        std::vector<std::string> child_names,
        std::vector<LocalPackageMetadataRelation> relations = {},
        std::optional<std::string> epoch = std::nullopt,
        std::string pkgver = "1.0",
        std::string pkgrel = "1") {
    std::vector<LocalPackageMetadataChild> children;
    children.reserve(child_names.size());
    for(auto& child_name : child_names) {
        children.push_back(LocalPackageMetadataChild{
                std::move(child_name), false, false, {}});
    }
    return LocalPackageMetadata{
            std::move(package_base),
            std::move(epoch),
            std::move(pkgver),
            std::move(pkgrel),
            {"x86_64", "aarch64"},
            std::move(children),
            std::move(relations)};
}

AurPackageInfo aur_package(
        std::string name,
        std::string package_base = {},
        std::vector<std::string> depends = {},
        std::vector<std::string> provides = {}) {
    AurPackageInfo package;
    package.Name = std::move(name);
    package.PackageBase = package_base.empty() ? package.Name
                                               : std::move(package_base);
    package.Version = "1.0-1";
    package.Description = "local dependency plan fixture";
    package.Depends = std::move(depends);
    package.Provides = std::move(provides);
    package.Maintainer = "moguet-test";
    return package;
}

void reset_queries() {
    query_stub::reset_repository_stub();
    query_stub::reset_aur_stub();
}

ProviderSelectionCallback reject_provider_selection() {
    return [](
                   const std::string& dependency,
                   const std::vector<ProvidedDependency>&)
                   -> std::optional<ProvidedDependency> {
        throw std::runtime_error(
                "Unexpected provider selection callback: " + dependency);
    };
}

const PlannedPackageTarget& require_package_target(
        const BuildPlan& plan, std::string_view package_name) {
    const auto target = std::find_if(
            plan.package_targets.begin(), plan.package_targets.end(),
            [package_name](const PlannedPackageTarget& candidate) {
                return candidate.package_name == package_name;
            });
    if(target == plan.package_targets.end()) {
        throw std::runtime_error(
                "Missing package target: " + std::string(package_name));
    }
    return *target;
}

const BuildPlanDependencyEdge& require_remote_edge(
        const BuildPlan& plan, std::string_view parent_package_name,
        std::string_view dependency_specification, PackageRole role) {
    const auto edge = std::find_if(
            plan.dependency_edges.begin(), plan.dependency_edges.end(),
            [parent_package_name, dependency_specification, role](
                    const BuildPlanDependencyEdge& candidate) {
                return candidate.parent_package_name == parent_package_name &&
                        candidate.dependency_spec ==
                                dependency_specification &&
                        candidate.role == role;
            });
    if(edge == plan.dependency_edges.end()) {
        throw std::runtime_error(
                "Missing remote dependency edge: " +
                std::string(parent_package_name) + " -> " +
                std::string(dependency_specification));
    }
    return *edge;
}

const LocalDependencyPlanInternalEdge& require_internal_edge(
        const LocalBuildPlan& plan, std::string_view parent_package_name,
        std::string_view dependency_specification) {
    const auto edge = std::find_if(
            plan.internal_edges().begin(), plan.internal_edges().end(),
            [parent_package_name, dependency_specification](
                    const LocalDependencyPlanInternalEdge& candidate) {
                return candidate.parent_package_name == parent_package_name &&
                        candidate.dependency_specification ==
                                dependency_specification;
            });
    if(edge == plan.internal_edges().end()) {
        throw std::runtime_error(
                "Missing internal dependency edge: " +
                std::string(parent_package_name) + " -> " +
                std::string(dependency_specification));
    }
    return *edge;
}

const BuildPlanMetadataRisk& require_metadata_risk(
        const BuildPlan& plan, std::string_view package_name) {
    const auto risk = std::find_if(
            plan.metadata_risks.begin(), plan.metadata_risks.end(),
            [package_name](const BuildPlanMetadataRisk& candidate) {
                return candidate.package_name == package_name;
            });
    if(risk == plan.metadata_risks.end()) {
        throw std::runtime_error(
                "Missing metadata risk: " + std::string(package_name));
    }
    return *risk;
}

const BuildPlanResolutionFailure& require_resolution_failure(
        const BuildPlan& plan, BuildPlanResolutionFailureKind kind,
        std::string_view subject) {
    const auto failure = std::find_if(
            plan.resolution_failures.begin(),
            plan.resolution_failures.end(),
            [kind, subject](const BuildPlanResolutionFailure& candidate) {
                return candidate.kind == kind &&
                        candidate.subject == subject;
            });
    if(failure == plan.resolution_failures.end()) {
        throw std::runtime_error(
                "Missing resolution failure: " + std::string(subject));
    }
    return *failure;
}

std::size_t build_plan_order_index(
        const BuildPlan& plan, std::string_view package_base) {
    for(std::size_t index = 0; index < plan.order.size(); ++index) {
        if(plan.order[index].package_base == package_base) return index;
    }
    throw std::runtime_error(
            "Missing build plan order entry: " + std::string(package_base));
}

bool contains_value(
        const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool repository_subject_was_queried(std::string_view subject) {
    const auto& history = query_stub::repository_query_history();
    return std::any_of(
            history.begin(), history.end(), [subject](const auto& query) {
                return query.subject == subject;
            });
}

bool aur_subject_was_queried(std::string_view subject) {
    const auto& history = query_stub::aur_query_history();
    return std::any_of(
            history.begin(), history.end(), [subject](const auto& query) {
                return query.subject == subject;
            });
}

void expect_no_external_query_for(
        std::string_view subject, const std::string& context) {
    expect(
            !repository_subject_was_queried(subject),
            context + ": repository query observed for " +
                    std::string(subject));
    expect(
            !aur_subject_was_queried(subject),
            context + ": AUR query observed for " + std::string(subject));
}

void expect_local_root_shape(
        const LocalBuildPlan& plan, std::string_view package_base,
        const std::vector<std::string>& child_names) {
    std::vector<RootTargetIdentity> expected_roots;
    for(std::size_t index = 0; index < child_names.size(); ++index) {
        expected_roots.push_back(RootTargetIdentity{index, child_names[index]});
    }
    expect(
            plan.build_plan().root_targets == expected_roots,
            "Local root target order differs");
    expect(plan.build_plan().order.size() == 1, "Local build unit count differs");
    expect(
            plan.build_plan().order[0].package_base == package_base &&
                    plan.build_plan().order[0].package_names == child_names,
            "Local PackageBase aggregation differs");
    for(std::size_t index = 0; index < child_names.size(); ++index) {
        const PlannedPackageTarget& target = require_package_target(
                plan.build_plan(), child_names[index]);
        expect(target.package_base == package_base, "Local target base differs");
        expect(
                target.roles == std::vector<PackageRole>{PackageRole::Root},
                "Local target role differs");
        expect(
                target.roots == std::vector<RootTargetIdentity>{
                                        {index, child_names[index]}},
                "Local target root identity differs");
    }
}

void test_all_children_are_required_without_root_queries() {
    const std::vector<std::string> children = {
            "local-root-cli", "local-root-libs"};
    const LocalPackageMetadata metadata =
            metadata_fixture("local-root-suite", children);

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "x86_64", reject_provider_selection());

    expect(
            plan.local_metadata() == metadata,
            "Local metadata snapshot was not preserved");
    expect(
            plan.effective_architecture() == "x86_64",
            "Effective architecture was not preserved");
    expect_local_root_shape(plan, metadata.package_base, children);
    expect(plan.internal_edges().empty(), "Unexpected internal edge");
    expect(plan.failures().empty(), "Unexpected local failure");
    expect(
            plan.build_plan().dependency_edges.empty(),
            "Unexpected remote dependency edge");
    expect(
            plan.build_plan().split_package_targets.size() == children.size(),
            "Required split-package targets differ");
    expect(
            query_stub::repository_query_history().empty(),
            "Dependency-free local root queried a repository");
    expect(
            query_stub::aur_query_history().empty(),
            "Dependency-free local root queried AUR");
    expect_no_external_query_for(
            metadata.package_base, "Local PackageBase identity");
    for(const auto& child : children) {
        expect_no_external_query_for(child, "Local child identity");
    }
}

void test_unsupported_architecture_stops_before_queries() {
    const LocalPackageMetadata metadata = metadata_fixture(
            "local-architecture-suite",
            {"local-architecture-cli", "local-architecture-libs"},
            {base_relation(
                    RelationKind::Depends, "must-not-query",
                    package_target("must-not-query"))});

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "riscv64", reject_provider_selection());

    expect(
            plan.effective_architecture() == "riscv64",
            "Rejected effective architecture was not preserved");
    expect(
            plan.failures().size() == 2,
            "Unsupported architecture failure count differs");
    const std::vector<std::string> expected_children = {
            "local-architecture-cli", "local-architecture-libs"};
    for(std::size_t index = 0; index < expected_children.size(); ++index) {
        expect(
                plan.failures()[index].kind ==
                                LocalDependencyPlanFailureKind::
                                        UnsupportedArchitecture &&
                        plan.failures()[index].parent_package_name ==
                                expected_children[index] &&
                        !plan.failures()[index]
                                 .dependency_specification.has_value() &&
                        plan.failures()[index].effective_architecture ==
                                "riscv64",
                "Unsupported architecture lost child failure context");
    }
    expect(plan.internal_edges().empty(), "Rejected plan has internal edges");
    expect(
            plan.build_plan().dependency_edges.empty(),
            "Rejected plan has remote edges");
    expect(
            query_stub::repository_query_history().empty(),
            "Unsupported architecture queried a repository");
    expect(
            query_stub::aur_query_history().empty(),
            "Unsupported architecture queried AUR");
}

void test_effective_relation_override_and_metadata_risks() {
    const std::string package_base = "local-effective-suite";
    const std::string cli = "local-effective-cli";
    const std::string libs = "local-effective-libs";
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {cli, libs},
            {
                    base_relation(
                            RelationKind::Depends, "base-runtime",
                            package_target("base-runtime")),
                    base_relation(
                            RelationKind::Depends, "base-x86",
                            package_target("base-x86"), "x86_64"),
                    base_relation(
                            RelationKind::Depends, "base-arm",
                            package_target("base-arm"), "aarch64"),
                    base_relation(
                            RelationKind::Makedepends, "base-make",
                            package_target("base-make")),
                    base_relation(
                            RelationKind::Checkdepends, "base-check",
                            package_target("base-check")),
                    base_relation(
                            RelationKind::Optdepends, "base-opt",
                            package_target("base-opt")),
                    base_relation(
                            RelationKind::Conflicts, "base-conflict",
                            package_target("base-conflict")),
                    base_relation(
                            RelationKind::Replaces, "base-replace",
                            package_target("base-replace")),
                    child_relation(
                            RelationKind::Depends, "cli-runtime-a",
                            package_target("cli-runtime-a"), cli),
                    child_relation(
                            RelationKind::Depends, "cli-runtime-b",
                            package_target("cli-runtime-b"), cli),
                    child_unset_relation(
                            RelationKind::Checkdepends, cli),
                    child_relation(
                            RelationKind::Conflicts, "cli-conflict",
                            package_target("cli-conflict"), cli),
                    child_unset_relation(
                            RelationKind::Depends, libs, "x86_64"),
            });

    for(const std::string dependency : {
                "base-runtime", "base-x86", "base-make", "base-check",
                "cli-runtime-a", "cli-runtime-b"}) {
        query_stub::set_repository_package_response(dependency, "core");
    }

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "x86_64", reject_provider_selection());
    const BuildPlan& build_plan = plan.build_plan();

    expect(plan.failures().empty(), "Effective relation plan has failures");
    expect(plan.internal_edges().empty(), "Unexpected local relation match");
    expect(
            build_plan.dependency_edges.size() == 7,
            "Effective dependency edge count differs");

    for(const std::string dependency : {"cli-runtime-a", "cli-runtime-b"}) {
        const auto& edge = require_remote_edge(
                build_plan, cli, dependency, PackageRole::RuntimeDependency);
        expect(edge.kind == DependencyKind::Repo, "CLI dependency is not repo");
    }
    expect(
            require_remote_edge(
                    build_plan, cli, "base-x86",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo,
            "CLI architecture-qualified dependency is not repo");
    expect(
            require_remote_edge(
                    build_plan, cli, "base-make",
                    PackageRole::BuildDependency)
                            .kind == DependencyKind::Repo,
            "CLI inherited makedepends role differs");

    expect(
            require_remote_edge(
                    build_plan, libs, "base-runtime",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo,
            "Library inherited depends differs");
    expect(
            require_remote_edge(
                    build_plan, libs, "base-make",
                    PackageRole::BuildDependency)
                            .kind == DependencyKind::Repo,
            "Library inherited makedepends role differs");
    expect(
            require_remote_edge(
                    build_plan, libs, "base-check",
                    PackageRole::CheckDependency)
                            .kind == DependencyKind::Repo,
            "Library inherited checkdepends role differs");

    for(const std::string filtered : {
                "base-arm", "base-opt", "base-conflict", "base-replace",
                "cli-conflict"}) {
        expect_no_external_query_for(filtered, "Non-dependency relation");
    }
    expect(
            !std::any_of(
                    build_plan.dependency_edges.begin(),
                    build_plan.dependency_edges.end(),
                    [&cli](const BuildPlanDependencyEdge& edge) {
                        return edge.parent_package_name == cli &&
                                (edge.dependency_spec == "base-runtime" ||
                                 edge.dependency_spec == "base-check");
                    }),
            "CLI child override retained an overridden base relation");
    expect(
            !std::any_of(
                    build_plan.dependency_edges.begin(),
                    build_plan.dependency_edges.end(),
                    [&libs](const BuildPlanDependencyEdge& edge) {
                        return edge.parent_package_name == libs &&
                                edge.dependency_spec == "base-x86";
                    }),
            "Explicit architecture unset retained the base relation");

    expect(
            build_plan.metadata_risks.size() == 2,
            "Local metadata risk count differs");
    const BuildPlanMetadataRisk& cli_risk =
            require_metadata_risk(build_plan, cli);
    expect(
            cli_risk.package_base == package_base &&
                    cli_risk.conflicts ==
                            std::vector<std::string>{"cli-conflict"} &&
                    cli_risk.replaces ==
                            std::vector<std::string>{"base-replace"},
            "CLI conflicts/replaces projection differs");
    const BuildPlanMetadataRisk& libs_risk =
            require_metadata_risk(build_plan, libs);
    expect(
            libs_risk.package_base == package_base &&
                    libs_risk.conflicts ==
                            std::vector<std::string>{"base-conflict"} &&
                    libs_risk.replaces ==
                            std::vector<std::string>{"base-replace"},
            "Library conflicts/replaces projection differs");
    expect_no_external_query_for(package_base, "Local PackageBase identity");
    expect_no_external_query_for(cli, "Local child identity");
    expect_no_external_query_for(libs, "Local child identity");
}

void test_internal_constraints_and_provides_use_libalpm() {
    const std::string package_base = "local-version-suite";
    const std::string core = "local-version-core";
    const std::string consumer = "local-version-consumer";
    const std::vector<std::string> direct_constraints = {
            core + "<2:1.0-11", core + "<=2:1.0-10",
            core + "=2:1.0-10", core + ">=2:1.0-10",
            core + ">2:1.0-3", core + ">1:99.0-99"};
    std::vector<LocalPackageMetadataRelation> relations = {
            child_relation(
                    RelationKind::Provides, "virtual-api=1.0rc1",
                    package_target(
                            "virtual-api", Comparison::Equal, "1.0rc1"),
                    core),
            child_relation(
                    RelationKind::Provides, "lib:liblocal.so.1",
                    soname_target("lib:liblocal.so.1"), core),
            child_relation(
                    RelationKind::Depends, direct_constraints[0],
                    package_target(
                            core, Comparison::LessThan, "2:1.0-11"),
                    consumer),
            child_relation(
                    RelationKind::Depends, direct_constraints[1],
                    package_target(
                            core, Comparison::LessThanOrEqual,
                            "2:1.0-10"),
                    consumer),
            child_relation(
                    RelationKind::Depends, direct_constraints[2],
                    package_target(core, Comparison::Equal, "2:1.0-10"),
                    consumer),
            child_relation(
                    RelationKind::Depends, direct_constraints[3],
                    package_target(
                            core, Comparison::GreaterThanOrEqual,
                            "2:1.0-10"),
                    consumer),
            child_relation(
                    RelationKind::Depends, direct_constraints[4],
                    package_target(
                            core, Comparison::GreaterThan, "2:1.0-3"),
                    consumer),
            child_relation(
                    RelationKind::Depends, direct_constraints[5],
                    package_target(
                            core, Comparison::GreaterThan, "1:99.0-99"),
                    consumer),
    };
    relations.push_back(child_relation(
            RelationKind::Depends, "virtual-api<1.0",
            package_target(
                    "virtual-api", Comparison::LessThan, "1.0"),
            consumer));
    relations.push_back(child_relation(
            RelationKind::Depends, "lib:liblocal.so.1",
            soname_target("lib:liblocal.so.1"), consumer));
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {core, consumer}, std::move(relations), "2",
            "1.0", "10");

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "x86_64", reject_provider_selection());

    expect(plan.failures().empty(), "Satisfied constraints have failures");
    expect(
            plan.internal_edges().size() == direct_constraints.size() + 2,
            "Satisfied internal edge count differs");
    expect(
            plan.build_plan().dependency_edges.empty(),
            "Satisfied local dependencies reached remote resolution");
    expect(plan.build_plan().unresolved.empty(), "Satisfied local edge unresolved");
    expect(plan.build_plan().cycles.empty(), "One-way local edges are cyclic");

    for(const auto& dependency : direct_constraints) {
        const auto& edge = require_internal_edge(plan, consumer, dependency);
        expect(
                edge.role == PackageRole::RuntimeDependency &&
                        edge.resolved_package_name == core &&
                        edge.resolution_kind ==
                                LocalDependencyResolutionKind::Package &&
                        !edge.provided_specification.has_value() &&
                        !edge.is_cycle,
                "Direct local version edge differs");
    }
    const auto& provided =
            require_internal_edge(plan, consumer, "virtual-api<1.0");
    expect(
            provided.resolved_package_name == core &&
                    provided.resolution_kind ==
                            LocalDependencyResolutionKind::Provided &&
                    provided.provided_specification == "virtual-api=1.0rc1" &&
                    !provided.is_cycle,
            "Versioned local provides edge differs");
    const auto& soname =
            require_internal_edge(plan, consumer, "lib:liblocal.so.1");
    expect(
            soname.resolved_package_name == core &&
                    soname.resolution_kind ==
                            LocalDependencyResolutionKind::Provided &&
                    soname.provided_specification == "lib:liblocal.so.1" &&
                    !soname.is_cycle,
            "SONAME local provides edge differs");
    expect(
            query_stub::repository_query_history().empty(),
            "Satisfied local constraints queried a repository");
    expect(
            query_stub::aur_query_history().empty(),
            "Satisfied local constraints queried AUR");

    reset_queries();
    const std::string exact = "local-version-fallback-api";
    const std::string provider = "local-version-fallback-provider";
    const std::string fallback_consumer =
            "local-version-fallback-consumer";
    const LocalPackageMetadata fallback_metadata = metadata_fixture(
            "local-version-fallback-suite",
            {exact, provider, fallback_consumer},
            {
                    child_relation(
                            RelationKind::Provides,
                            exact + "=2",
                            package_target(
                                    exact, Comparison::Equal, "2"),
                            provider),
                    child_relation(
                            RelationKind::Depends,
                            exact + ">=2",
                            package_target(
                                    exact,
                                    Comparison::GreaterThanOrEqual,
                                    "2"),
                            fallback_consumer),
            },
            std::nullopt, "1", "1");
    const LocalBuildPlan fallback_plan = resolve_local_build_plan(
            fallback_metadata, "x86_64", reject_provider_selection());
    const auto& fallback_edge = require_internal_edge(
            fallback_plan, fallback_consumer, exact + ">=2");
    expect(
            fallback_plan.failures().empty() &&
                    fallback_edge.resolution_kind ==
                            LocalDependencyResolutionKind::Provided &&
                    fallback_edge.resolved_package_name == provider &&
                    fallback_edge.provided_specification == exact + "=2",
            "Compatible local provide did not satisfy an exact version mismatch");
    expect(
            query_stub::repository_query_history().empty() &&
                    query_stub::aur_query_history().empty(),
            "Local exact/provider constraint classification queried remotely");
}

void test_local_mismatch_and_ambiguity_fail_closed() {
    const std::string package_base = "local-failure-suite";
    const std::string core = "local-failure-core";
    const std::string consumer = "local-failure-consumer";
    const LocalPackageMetadata mismatch_metadata = metadata_fixture(
            package_base, {core, consumer},
            {
                    child_relation(
                            RelationKind::Provides, "virtual-api=1.0rc1",
                            package_target(
                                    "virtual-api", Comparison::Equal,
                                    "1.0rc1"),
                            core),
                    child_relation(
                            RelationKind::Provides, "unversioned-api",
                            package_target("unversioned-api"), core),
                    child_relation(
                            RelationKind::Depends,
                            core + ">2:1.0-10",
                            package_target(
                                    core, Comparison::GreaterThan,
                                    "2:1.0-10"),
                            consumer),
                    child_relation(
                            RelationKind::Depends, "virtual-api>=1.0",
                            package_target(
                                    "virtual-api",
                                    Comparison::GreaterThanOrEqual, "1.0"),
                            consumer),
                    child_relation(
                            RelationKind::Depends, "unversioned-api>=1",
                            package_target(
                                    "unversioned-api",
                                    Comparison::GreaterThanOrEqual, "1"),
                            consumer),
            },
            "2", "1.0", "10");

    query_stub::set_repository_package_response("virtual-api", "core");
    query_stub::set_repository_package_response("unversioned-api", "extra");
    const LocalBuildPlan mismatch_plan = resolve_local_build_plan(
            mismatch_metadata, "x86_64", reject_provider_selection());
    expect(
            mismatch_plan.failures().size() == 1,
            "Constraint mismatch failure count differs");
    const std::vector<LocalDependencyPlanCandidate> mismatch_candidates = {
            {core, std::nullopt, "2:1.0-10", std::nullopt}};
    expect(
            mismatch_plan.failures()[0].kind ==
                            LocalDependencyPlanFailureKind::ConstraintMismatch &&
                    mismatch_plan.failures()[0].parent_package_name ==
                            consumer &&
                    mismatch_plan.failures()[0].dependency_specification ==
                            core + ">2:1.0-10" &&
                    mismatch_plan.failures()[0].candidates ==
                            mismatch_candidates,
            "Constraint mismatch lost its typed failure");
    expect(
            mismatch_plan.internal_edges().empty(),
            "Mismatched local candidates became internal edges");
    expect(
            require_remote_edge(
                    mismatch_plan.build_plan(), consumer,
                    "virtual-api>=1.0",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo &&
                    require_remote_edge(
                            mismatch_plan.build_plan(), consumer,
                            "unversioned-api>=1",
                            PackageRole::RuntimeDependency)
                                    .kind == DependencyKind::Repo,
            "Incompatible local provides did not reach remote resolution");
    expect(
            repository_subject_was_queried("virtual-api") &&
                    repository_subject_was_queried("unversioned-api") &&
                    query_stub::aur_query_history().empty(),
            "Local provide mismatch external query boundary differs");

    reset_queries();
    const std::string provider_a = "local-provider-a";
    const std::string provider_b = "local-provider-b";
    const LocalPackageMetadata ambiguity_metadata = metadata_fixture(
            "local-ambiguity-suite",
            {provider_a, provider_b, "local-provider-consumer"},
            {
                    child_relation(
                            RelationKind::Provides, "shared-api=1",
                            package_target(
                                    "shared-api", Comparison::Equal, "1"),
                            provider_a),
                    child_relation(
                            RelationKind::Provides, "shared-api=1",
                            package_target(
                                    "shared-api", Comparison::Equal, "1"),
                            provider_b),
                    child_relation(
                            RelationKind::Depends, "shared-api=1",
                            package_target(
                                    "shared-api", Comparison::Equal, "1"),
                            "local-provider-consumer"),
            });
    const LocalBuildPlan ambiguity_plan = resolve_local_build_plan(
            ambiguity_metadata, "x86_64", reject_provider_selection());
    expect(
            ambiguity_plan.failures().size() == 1 &&
                    ambiguity_plan.failures()[0].kind ==
                            LocalDependencyPlanFailureKind::AmbiguousLocalProvider,
            "Local provider ambiguity lost its typed failure");
    expect(
            ambiguity_plan.failures()[0].candidates ==
                    std::vector<LocalDependencyPlanCandidate>{
                            {provider_a, "shared-api=1", "1", std::nullopt},
                            {provider_b, "shared-api=1", "1", std::nullopt}},
            "Local provider ambiguity candidate order differs");
    expect(
            ambiguity_plan.internal_edges().empty() &&
                    ambiguity_plan.build_plan().dependency_edges.empty(),
            "Ambiguous local provider escaped to another resolution path");
    expect(
            query_stub::repository_query_history().empty() &&
                    query_stub::aur_query_history().empty(),
            "Ambiguous local provider triggered external queries");
}

void test_internal_cycle_classification() {
    const std::string package_base = "local-cycle-suite";
    const std::string package_a = "local-cycle-a";
    const std::string package_b = "local-cycle-b";

    const LocalPackageMetadata one_way_metadata = metadata_fixture(
            package_base, {package_a, package_b},
            {child_relation(
                    RelationKind::Depends, package_b,
                    package_target(package_b), package_a)});
    const LocalBuildPlan one_way_plan = resolve_local_build_plan(
            one_way_metadata, "x86_64", reject_provider_selection());
    expect(
            one_way_plan.failures().empty() &&
                    one_way_plan.internal_edges().size() == 1,
            "One-way local edge classification differs");
    expect(
            !one_way_plan.internal_edges()[0].is_cycle &&
                    one_way_plan.build_plan().cycles.empty(),
            "One-way local edge was classified as a cycle");

    reset_queries();
    const LocalPackageMetadata mutual_metadata = metadata_fixture(
            package_base, {package_a, package_b},
            {
                    child_relation(
                            RelationKind::Depends, package_b,
                            package_target(package_b), package_a),
                    child_relation(
                            RelationKind::Depends, package_a,
                            package_target(package_a), package_b),
            });
    const LocalBuildPlan mutual_plan = resolve_local_build_plan(
            mutual_metadata, "x86_64", reject_provider_selection());
    expect(
            mutual_plan.internal_edges().size() == 2 &&
                    std::all_of(
                            mutual_plan.internal_edges().begin(),
                            mutual_plan.internal_edges().end(),
                            [](const auto& edge) { return edge.is_cycle; }),
            "Mutual local cycle edge classification differs");
    expect(
            contains_value(mutual_plan.build_plan().cycles, package_base),
            "Mutual local cycle was not retained in BuildPlan");

    reset_queries();
    const LocalPackageMetadata self_metadata = metadata_fixture(
            package_base, {package_a},
            {
                    child_relation(
                            RelationKind::Provides, "virtual-self=1",
                            package_target(
                                    "virtual-self", Comparison::Equal, "1"),
                            package_a),
                    child_relation(
                            RelationKind::Depends, "virtual-self=1",
                            package_target(
                                    "virtual-self", Comparison::Equal, "1"),
                            package_a),
            });
    const LocalBuildPlan self_plan = resolve_local_build_plan(
            self_metadata, "x86_64", reject_provider_selection());
    const auto& self_edge =
            require_internal_edge(self_plan, package_a, "virtual-self=1");
    expect(
            self_edge.resolution_kind ==
                            LocalDependencyResolutionKind::Provided &&
                    self_edge.resolved_package_name == package_a &&
                    self_edge.is_cycle,
            "Provides self-return was not classified as a cycle");
    expect(
            contains_value(self_plan.build_plan().cycles, package_base),
            "Provides self-return cycle was not retained in BuildPlan");
    expect(
            query_stub::repository_query_history().empty() &&
                    query_stub::aur_query_history().empty(),
            "Internal cycle classification triggered external queries");
}

void test_internal_edges_propagate_roots_to_remote_subtree() {
    const std::string package_base = "local-propagation-suite";
    const std::string package_a = "local-propagation-a";
    const std::string package_b = "local-propagation-b";
    const std::string remote_x = "remote-propagation-x";
    const std::string remote_y = "remote-propagation-y";
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {package_a, package_b},
            {
                    child_relation(
                            RelationKind::Depends, package_b,
                            package_target(package_b), package_a),
                    child_relation(
                            RelationKind::Depends, remote_x,
                            package_target(remote_x), package_b),
            });

    query_stub::set_repository_package_response(remote_x, std::nullopt);
    query_stub::set_repository_package_response(remote_y, std::nullopt);
    query_stub::set_aur_package_response(
            remote_x,
            aur_package(
                    remote_x, "remote-propagation-x-base",
                    {remote_y, package_a}));
    query_stub::set_aur_package_response(
            remote_y,
            aur_package(remote_y, "remote-propagation-y-base"));

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "x86_64", reject_provider_selection());
    const BuildPlan& build_plan = plan.build_plan();
    const std::vector<RootTargetIdentity> expected_roots = {
            {0, package_a}, {1, package_b}};

    const PlannedPackageTarget& target_b =
            require_package_target(build_plan, package_b);
    expect(
            target_b.roles == std::vector<PackageRole>{
                                      PackageRole::Root,
                                      PackageRole::RuntimeDependency} &&
                    target_b.roots == expected_roots,
            "Internal dependency root/role attribution differs");
    for(const std::string& remote : {remote_x, remote_y}) {
        const PlannedPackageTarget& target =
                require_package_target(build_plan, remote);
        expect(
                target.roles == std::vector<PackageRole>{
                                        PackageRole::RuntimeDependency} &&
                        target.roots == expected_roots,
                "Internal root did not propagate through the remote subtree");
    }
    expect(
            require_remote_edge(
                    build_plan, package_b, remote_x,
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Aur &&
                    require_remote_edge(
                            build_plan, remote_x, remote_y,
                            PackageRole::RuntimeDependency)
                                    .kind == DependencyKind::Aur,
            "Remote subtree dependency edges differ");
    const auto& return_edge =
            require_internal_edge(plan, remote_x, package_a);
    expect(
            return_edge.resolution_kind ==
                            LocalDependencyResolutionKind::Package &&
                    return_edge.resolved_package_name == package_a &&
                    return_edge.is_cycle,
            "Transitive AUR return to a local child was not a cycle");
    expect(
            contains_value(build_plan.cycles, package_base),
            "Transitive AUR/local cycle was not retained in BuildPlan");
    expect_no_external_query_for(package_a, "Transitive local return");
    expect_no_external_query_for(package_b, "Local dependency child");
    expect_no_external_query_for(package_base, "Local PackageBase identity");

    reset_queries();
    const std::string one_way_a = "local-one-way-a";
    const std::string one_way_b = "local-one-way-b";
    const std::string one_way_remote = "remote-one-way-x";
    const LocalPackageMetadata one_way_metadata = metadata_fixture(
            "local-one-way-suite", {one_way_a, one_way_b},
            {child_relation(
                    RelationKind::Depends, one_way_remote,
                    package_target(one_way_remote), one_way_a)});
    query_stub::set_repository_package_response(
            one_way_remote, std::nullopt);
    query_stub::set_aur_package_response(
            one_way_remote,
            aur_package(
                    one_way_remote, "remote-one-way-base",
                    {one_way_b, one_way_b + ">="}));

    const LocalBuildPlan one_way_plan = resolve_local_build_plan(
            one_way_metadata, "x86_64", reject_provider_selection());
    const auto& one_way_return = require_internal_edge(
            one_way_plan, one_way_remote, one_way_b);
    expect(
            one_way_return.is_cycle &&
                    contains_value(
                            one_way_plan.build_plan().cycles,
                            "local-one-way-suite"),
            "Remote return to the local PackageBase was not a cycle");
    expect(
            one_way_plan.failures().empty() &&
                    contains_value(
                            one_way_plan.build_plan().unresolved,
                            dependency_constraint_unresolved_reason(
                                    one_way_b + ">=")),
            "Malformed remote constraint was reclassified as a local failure");
    expect_no_external_query_for(
            one_way_b, "One-way transitive local dependency");
}

void test_metadata_failures_remain_typed_and_keep_local_roots() {
    const std::string package_base = "local-query-failure-suite";
    const std::string package_a = "local-query-failure-a";
    const std::string package_b = "local-query-failure-b";
    const std::string repository_failure = "remote-repository-failure";
    const std::string aur_failure = "remote-aur-failure";
    const std::string provider_failure = "remote-provider-failure";
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {package_a, package_b},
            {
                    child_relation(
                            RelationKind::Depends, package_b,
                            package_target(package_b), package_a),
                    child_relation(
                            RelationKind::Depends, repository_failure,
                            package_target(repository_failure), package_b),
                    child_relation(
                            RelationKind::Depends, aur_failure,
                            package_target(aur_failure), package_b),
                    child_relation(
                            RelationKind::Depends, provider_failure,
                            package_target(provider_failure), package_b),
            });

    query_stub::set_repository_package_failure(
            repository_failure, "repository metadata unavailable");
    query_stub::set_repository_package_response(aur_failure, std::nullopt);
    query_stub::set_aur_package_failure(
            aur_failure, "AUR metadata unavailable");
    query_stub::set_repository_package_response(
            provider_failure, std::nullopt);
    query_stub::set_aur_package_response(provider_failure, std::nullopt);
    query_stub::set_repository_provider_response(provider_failure, {});
    query_stub::set_aur_provider_failure(
            provider_failure, "provider search unavailable");

    const LocalBuildPlan plan = resolve_local_build_plan(
            metadata, "x86_64", reject_provider_selection());
    const BuildPlan& build_plan = plan.build_plan();
    const std::vector<RootTargetIdentity> expected_roots = {
            {0, package_a}, {1, package_b}};

    expect(
            build_plan.resolution_failures.size() == 3,
            "Remote metadata failure count differs");
    const BuildPlanResolutionFailure& repository =
            require_resolution_failure(
                    build_plan,
                    BuildPlanResolutionFailureKind::
                            RepositoryMetadataUnavailable,
                    repository_failure);
    const BuildPlanResolutionFailure& aur = require_resolution_failure(
            build_plan,
            BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            aur_failure);
    const BuildPlanResolutionFailure& provider =
            require_resolution_failure(
                    build_plan,
                    BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                    provider_failure);
    for(const BuildPlanResolutionFailure* failure : {
                &repository, &aur, &provider}) {
        expect(
                failure->parent_package_name == package_b &&
                        failure->parent_package_base == package_base &&
                        failure->dependency_specification ==
                                failure->subject &&
                        failure->roots == expected_roots &&
                        !failure->diagnostic.empty(),
                "Typed metadata failure context/root attribution differs");
        expect(
                contains_value(build_plan.unresolved, failure->subject),
                "Metadata failure was not retained as unresolved");
        expect(
                require_remote_edge(
                        build_plan, package_b, failure->subject,
                        PackageRole::RuntimeDependency)
                                .kind == DependencyKind::Unknown,
                "Metadata failure edge was classified as confirmed absence");
    }
    expect_no_external_query_for(package_a, "Local root identity");
    expect_no_external_query_for(package_b, "Local dependency child");
    expect_no_external_query_for(package_base, "Local PackageBase identity");
}

void test_remaining_dependencies_reuse_build_plan_policy() {
    const std::string package_base = "local-remote-suite";
    const std::string child = "local-remote-app";
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {child},
            {
                    child_relation(
                            RelationKind::Depends, "remote-repo",
                            package_target("remote-repo"), child),
                    child_relation(
                            RelationKind::Depends, "remote-aur",
                            package_target("remote-aur"), child),
                    child_relation(
                            RelationKind::Depends, "remote-constrained>=2",
                            package_target(
                                    "remote-constrained",
                                    Comparison::GreaterThanOrEqual, "2"),
                            child),
                    child_relation(
                            RelationKind::Depends, "remote-virtual",
                            package_target("remote-virtual"), child),
                    child_relation(
                            RelationKind::Depends, "remote-missing",
                            package_target("remote-missing"), child),
            });

    query_stub::set_repository_package_response("remote-repo", "core");
    for(const std::string dependency : {
                "remote-aur", "remote-constrained", "remote-virtual",
                "remote-missing"}) {
        query_stub::set_repository_package_response(dependency, std::nullopt);
    }
    query_stub::set_aur_package_response(
            "remote-aur", aur_package("remote-aur", "remote-aur-base"));
    query_stub::set_aur_package_response(
            "remote-constrained",
            aur_package("remote-constrained", "remote-constrained-base"));
    query_stub::set_aur_package_response("remote-virtual", std::nullopt);
    query_stub::set_aur_package_response("remote-missing", std::nullopt);
    query_stub::set_repository_provider_response("remote-virtual", {});
    query_stub::set_repository_provider_response("remote-missing", {});
    query_stub::set_aur_provider_response(
            "remote-virtual", {"remote-provider-a", "remote-provider-b"});
    query_stub::set_aur_provider_response("remote-missing", {});
    query_stub::set_aur_package_response(
            "remote-provider-a",
            aur_package(
                    "remote-provider-a", "remote-provider-a-base", {},
                    {"remote-virtual=1"}));
    query_stub::set_aur_package_response(
            "remote-provider-b",
            aur_package(
                    "remote-provider-b", "remote-provider-b-base", {},
                    {"remote-virtual=1"}));

    std::size_t selection_count = 0;
    const ProviderSelectionCallback select_provider =
            [&selection_count](
                    const std::string& dependency,
                    const std::vector<ProvidedDependency>& candidates)
                    -> std::optional<ProvidedDependency> {
        ++selection_count;
        expect(
                dependency == "remote-virtual" && candidates.size() == 2 &&
                        candidates[0].package_name == "remote-provider-a" &&
                        candidates[1].package_name == "remote-provider-b",
                "Existing provider callback input differs");
        return candidates[1];
    };

    const LocalBuildPlan plan =
            resolve_local_build_plan(metadata, "x86_64", select_provider);
    const BuildPlan& build_plan = plan.build_plan();

    expect(selection_count == 1, "Provider callback invocation count differs");
    expect(plan.failures().empty(), "Remote delegation has local failures");
    expect(plan.internal_edges().empty(), "Remote dependency became local");
    expect(
            require_remote_edge(
                    build_plan, child, "remote-repo",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo,
            "Repository exact dependency classification differs");
    expect(
            require_remote_edge(
                    build_plan, child, "remote-aur",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Aur,
            "AUR exact dependency classification differs");
    expect(
            require_remote_edge(
                    build_plan, child, "remote-constrained>=2",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Aur,
            "Remote constrained dependency classification differs");
    const auto& provider_edge = require_remote_edge(
            build_plan, child, "remote-virtual",
            PackageRole::RuntimeDependency);
    expect(
            provider_edge.kind == DependencyKind::Provided &&
                    provider_edge.provider_resolution ==
                            ProviderResolutionKind::UserSelected &&
                    provider_edge.resolved_provider.has_value() &&
                    provider_edge.resolved_provider->package_name ==
                            "remote-provider-b" &&
                    provider_edge.resolved_provider->package_base ==
                            "remote-provider-b-base" &&
                    std::holds_alternative<AurProviderOrigin>(
                            provider_edge.resolved_provider->origin),
            "Selected remote provider identity differs");
    expect(
            require_remote_edge(
                    build_plan, child, "remote-missing",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Unknown,
            "Missing remote dependency classification differs");
    expect(
            build_plan.provided.size() == 1 &&
                    build_plan.provided[0].dependency == "remote-virtual" &&
                    build_plan.provided[0].resolution ==
                            ProviderResolutionKind::UserSelected,
            "BuildPlan selected provider record differs");
    expect(
            contains_value(
                    build_plan.unresolved,
                    dependency_constraint_unresolved_reason(
                            "remote-constrained>=2")) &&
                    contains_value(build_plan.unresolved, "remote-missing"),
            "Existing unresolved dependency policy was not reused");
    expect(
            build_plan.ambiguous_providers.empty(),
            "Selected provider remained ambiguous");
    expect(
            build_plan_order_index(build_plan, "remote-aur-base") <
                            build_plan_order_index(build_plan, package_base) &&
                    build_plan_order_index(
                            build_plan, "remote-constrained-base") <
                            build_plan_order_index(build_plan, package_base) &&
                    build_plan_order_index(
                            build_plan, "remote-provider-b-base") <
                            build_plan_order_index(build_plan, package_base),
            "Existing dependency-first BuildPlan ordering differs");
    expect_no_external_query_for(package_base, "Local PackageBase identity");
    expect_no_external_query_for(child, "Local child identity");
}

void test_package_base_back_edges_do_not_duplicate_local_unit() {
    const std::string package_base = "local-backedge-suite";
    const std::string child = "local-backedge-child";
    const LocalPackageMetadata exact_metadata = metadata_fixture(
            package_base, {child},
            {child_relation(
                    RelationKind::Depends, package_base,
                    package_target(package_base), child)});

    query_stub::set_repository_package_response(package_base, "extra");
    const LocalBuildPlan repository_plan = resolve_local_build_plan(
            exact_metadata, "x86_64", reject_provider_selection());
    expect(
            require_remote_edge(
                    repository_plan.build_plan(), child, package_base,
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo,
            "Repository PackageBase-name dependency was not kept remote");
    expect(
            repository_plan.build_plan().cycles.empty(),
            "Repository exact dependency became a local back-edge");
    expect(
            !aur_subject_was_queried(package_base),
            "Repository exact dependency queried AUR");

    reset_queries();
    query_stub::set_repository_package_response(package_base, std::nullopt);
    query_stub::set_aur_package_response(
            package_base, aur_package(package_base, package_base));
    const LocalBuildPlan aur_plan = resolve_local_build_plan(
            exact_metadata, "x86_64", reject_provider_selection());
    expect(
            require_remote_edge(
                    aur_plan.build_plan(), child, package_base,
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Aur,
            "AUR PackageBase-name dependency classification differs");
    expect(
            contains_value(aur_plan.build_plan().cycles, package_base),
            "AUR exact local PackageBase return was not a cycle");
    expect_local_root_shape(aur_plan, package_base, {child});

    reset_queries();
    const std::string provided_dependency = "virtual-backedge";
    const std::string provider_name = "remote-backedge-provider";
    const LocalPackageMetadata provider_metadata = metadata_fixture(
            package_base, {child},
            {child_relation(
                    RelationKind::Depends, provided_dependency,
                    package_target(provided_dependency), child)});
    query_stub::set_repository_package_response(
            provided_dependency, std::nullopt);
    query_stub::set_aur_package_response(provided_dependency, std::nullopt);
    query_stub::set_repository_provider_response(provided_dependency, {});
    query_stub::set_aur_provider_response(
            provided_dependency, {provider_name});
    query_stub::set_aur_package_response(
            provider_name,
            aur_package(
                    provider_name, package_base, {},
                    {provided_dependency + "=1"}));
    std::size_t selection_count = 0;
    const LocalBuildPlan provider_plan = resolve_local_build_plan(
            provider_metadata, "x86_64",
            [&selection_count](
                    const std::string& dependency,
                    const std::vector<ProvidedDependency>& candidates)
                    -> std::optional<ProvidedDependency> {
                ++selection_count;
                expect(
                        dependency == "virtual-backedge" &&
                                candidates.size() == 1,
                        "Back-edge provider callback input differs");
                return candidates.front();
            });
    const auto& provider_edge = require_remote_edge(
            provider_plan.build_plan(), child, provided_dependency,
            PackageRole::RuntimeDependency);
    expect(
            selection_count == 1 &&
                    provider_edge.kind == DependencyKind::Provided &&
                    provider_edge.provider_resolution ==
                            ProviderResolutionKind::UserSelected,
            "Selected AUR provider back-edge classification differs");
    expect(
            contains_value(provider_plan.build_plan().cycles, package_base),
            "Selected AUR provider PackageBase return was not a cycle");
    expect_local_root_shape(provider_plan, package_base, {child});
    expect_no_external_query_for(child, "Local child identity");
}

void test_remote_provider_local_name_collision_fails_closed() {
    const std::string package_base = "local-collision-suite";
    const std::string consumer = "local-collision-consumer";
    const std::string local_child = "local-collision-provider";
    const std::string dependency = "collision-virtual";
    const LocalPackageMetadata metadata = metadata_fixture(
            package_base, {consumer, local_child},
            {child_relation(
                    RelationKind::Depends, dependency,
                    package_target(dependency), consumer)});

    query_stub::set_repository_package_response(dependency, std::nullopt);
    query_stub::set_aur_package_response(dependency, std::nullopt);
    const ProvidedDependency repository_provider =
            ProvidedDependency::from_repository(
                    "extra", local_child, dependency, dependency + "=1",
                    std::optional<std::string>{"1-1"});
    query_stub::set_repository_provider_response(
            dependency, {repository_provider});

    const LocalBuildPlan repository_plan =
            resolve_local_build_plan(metadata, "x86_64");
    expect(
            repository_plan.failures().size() == 1 &&
                    repository_plan.failures()[0].kind ==
                            LocalDependencyPlanFailureKind::
                                    RemoteProviderIdentityConflict &&
                    repository_plan.failures()[0]
                            .dependency_specification == dependency &&
                    repository_plan.failures()[0].candidates.size() == 1 &&
                    repository_plan.failures()[0].candidates[0]
                            .remote_provider == repository_provider,
            "Repository provider/local identity collision lost typed context");
    expect(
            repository_plan.build_plan().provided.empty() &&
                    repository_plan.build_plan().order.size() == 1 &&
                    contains_value(
                            repository_plan.build_plan().unresolved,
                            dependency +
                                    " (remote provider conflicts with local package identity)"),
            "Repository provider/local collision escaped the fail-closed guard");
    expect_no_external_query_for(
            local_child, "Repository provider/local collision");

    reset_queries();
    query_stub::set_repository_package_response(dependency, std::nullopt);
    query_stub::set_aur_package_response(dependency, std::nullopt);
    query_stub::set_repository_provider_response(dependency, {});
    query_stub::set_aur_provider_response(dependency, {local_child});
    query_stub::set_aur_package_response(
            local_child,
            aur_package(
                    local_child, "remote-collision-base", {},
                    {dependency + "=1"}));
    const ProvidedDependency aur_provider = ProvidedDependency::from_aur(
            local_child, "remote-collision-base", dependency,
            dependency + "=1", std::optional<std::string>{"1.0-1"});

    const LocalBuildPlan aur_plan =
            resolve_local_build_plan(metadata, "x86_64");
    expect(
            aur_plan.failures().size() == 1 &&
                    aur_plan.failures()[0].kind ==
                            LocalDependencyPlanFailureKind::
                                    RemoteProviderIdentityConflict &&
                    aur_plan.failures()[0].candidates.size() == 1 &&
                    aur_plan.failures()[0].candidates[0].remote_provider ==
                            aur_provider,
            "AUR provider/local identity collision lost typed context");
    expect(
            aur_plan.build_plan().provided.empty() &&
                    aur_plan.build_plan().order.size() == 1 &&
                    build_plan_order_index(
                            aur_plan.build_plan(), package_base) == 0,
            "AUR provider/local collision added a remote build unit");
    expect(
            query_stub::aur_query_count(
                    query_stub::AurQueryKind::StrictInfo,
                    local_child) == 1,
            "AUR provider collision candidate metadata query count differs");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    reset_queries();
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "all children are required without root queries",
                test_all_children_are_required_without_root_queries);
        run_case(
                "unsupported architecture stops before queries",
                test_unsupported_architecture_stops_before_queries);
        run_case(
                "effective relation override and metadata risks",
                test_effective_relation_override_and_metadata_risks);
        run_case(
                "internal constraints and provides use libalpm",
                test_internal_constraints_and_provides_use_libalpm);
        run_case(
                "local mismatch and ambiguity fail closed",
                test_local_mismatch_and_ambiguity_fail_closed);
        run_case(
                "internal cycle classification",
                test_internal_cycle_classification);
        run_case(
                "internal edges propagate roots to remote subtree",
                test_internal_edges_propagate_roots_to_remote_subtree);
        run_case(
                "metadata failures remain typed and keep local roots",
                test_metadata_failures_remain_typed_and_keep_local_roots);
        run_case(
                "remaining dependencies reuse BuildPlan policy",
                test_remaining_dependencies_reuse_build_plan_policy);
        run_case(
                "PackageBase back-edges do not duplicate local unit",
                test_package_base_back_edges_do_not_duplicate_local_unit);
        run_case(
                "remote provider/local name collision fails closed",
                test_remote_provider_local_name_collision_fails_closed);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "local dependency plan projection tests: all checks passed\n";
    return 0;
}
