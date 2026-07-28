#include "aur_rpc.hpp"
#include "dependency_plan.hpp"
#include "dependency_provider.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dependency_plan_repository_query_stub {

void reset_query_counts();
std::size_t legacy_repo_package_query_count();
std::size_t sync_database_package_query_count();
std::size_t strict_repo_provider_query_count();

} // namespace dependency_plan_repository_query_stub

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

const PlannedPackageTarget& require_package_target(
        const BuildPlan& plan, std::string_view package_name) {
    const PlannedPackageTarget* found = nullptr;
    for(const auto& target : plan.package_targets) {
        if(target.package_name != package_name) continue;
        if(found != nullptr) {
            throw std::runtime_error(
                    "Duplicate planned package target: " + std::string(package_name));
        }
        found = &target;
    }
    if(found == nullptr) {
        throw std::runtime_error(
                "Missing planned package target: " + std::string(package_name));
    }
    return *found;
}

std::size_t package_target_count(
        const BuildPlan& plan, std::string_view package_name) {
    return static_cast<std::size_t>(std::count_if(
            plan.package_targets.begin(), plan.package_targets.end(),
            [&package_name](const PlannedPackageTarget& target) {
                return target.package_name == package_name;
            }));
}

const BuildPlanDependencyEdge& require_edge(
        const BuildPlan& plan, std::string_view parent_package_name,
        std::string_view dependency_spec, PackageRole role) {
    const BuildPlanDependencyEdge* found = nullptr;
    for(const auto& edge : plan.dependency_edges) {
        if(edge.parent_package_name != parent_package_name ||
           edge.dependency_spec != dependency_spec || edge.role != role) {
            continue;
        }
        if(found != nullptr) {
            throw std::runtime_error(
                    "Duplicate dependency edge: " + std::string(parent_package_name) +
                    " -> " + std::string(dependency_spec));
        }
        found = &edge;
    }
    if(found == nullptr) {
        throw std::runtime_error(
                "Missing dependency edge: " + std::string(parent_package_name) + " -> " +
                std::string(dependency_spec));
    }
    return *found;
}

const BuildPlanResolutionFailure& require_resolution_failure(
        const BuildPlan& plan, BuildPlanResolutionFailureKind kind,
        std::string_view subject) {
    const BuildPlanResolutionFailure* found = nullptr;
    for(const auto& failure : plan.resolution_failures) {
        if(failure.kind != kind || failure.subject != subject) continue;
        if(found != nullptr) {
            throw std::runtime_error(
                    "Duplicate resolution failure: " + std::string(subject));
        }
        found = &failure;
    }
    if(found == nullptr) {
        throw std::runtime_error(
                "Missing resolution failure: " + std::string(subject));
    }
    return *found;
}

void expect_roles(
        const PlannedPackageTarget& target, const std::vector<PackageRole>& expected) {
    expect(target.roles == expected, "Unexpected roles for " + target.package_name);
}

void expect_roots(
        const PlannedPackageTarget& target,
        const std::vector<RootTargetIdentity>& expected) {
    expect(target.roots == expected, "Unexpected roots for " + target.package_name);
}

void expect_resolution_failure_context(
        const BuildPlanResolutionFailure& failure,
        const std::optional<std::string>& parent_package_name,
        const std::optional<std::string>& parent_package_base,
        const std::optional<std::string>& dependency_specification,
        const std::vector<RootTargetIdentity>& roots,
        const std::string& diagnostic) {
    expect(
            failure.parent_package_name == parent_package_name,
            "Resolution failure parent name differs for " + failure.subject);
    expect(
            failure.parent_package_base == parent_package_base,
            "Resolution failure parent base differs for " + failure.subject);
    expect(
            failure.dependency_specification == dependency_specification,
            "Resolution failure dependency differs for " + failure.subject);
    expect(
            failure.roots == roots,
            "Resolution failure roots differ for " + failure.subject);
    expect(
            failure.diagnostic == diagnostic,
            "Resolution failure diagnostic differs for " + failure.subject);
}

void expect_legacy_order(
        const BuildPlan& plan, const std::vector<std::string>& expected_package_bases) {
    expect(plan.order.size() == expected_package_bases.size(), "Unexpected legacy order size");
    for(std::size_t i = 0; i < expected_package_bases.size(); ++i) {
        expect(
                plan.order[i].package_base == expected_package_bases[i],
                "Unexpected legacy order at index " + std::to_string(i));
        expect(
                plan.order[i].package_names ==
                        std::vector<std::string>{expected_package_bases[i]},
                "Unexpected legacy package names at index " + std::to_string(i));
    }
}

const BuildPlanEntry& require_build_plan_entry(
        const BuildPlan& plan, std::string_view package_base) {
    const BuildPlanEntry* found = nullptr;
    for(const auto& entry : plan.order) {
        if(entry.package_base != package_base) continue;
        if(found != nullptr) {
            throw std::runtime_error(
                    "Duplicate build plan entry: " + std::string(package_base));
        }
        found = &entry;
    }
    if(found == nullptr) {
        throw std::runtime_error(
                "Missing build plan entry: " + std::string(package_base));
    }
    return *found;
}

std::size_t require_build_plan_order_index(
        const BuildPlan& plan, std::string_view package_base) {
    static_cast<void>(require_build_plan_entry(plan, package_base));
    for(std::size_t i = 0; i < plan.order.size(); ++i) {
        if(plan.order[i].package_base == package_base) return i;
    }
    throw std::logic_error(
            "Validated build plan entry has no order index: " +
            std::string(package_base));
}

void expect_build_unit_before(
        const BuildPlan& plan, std::string_view dependency_package_base,
        std::string_view dependent_package_base) {
    expect(
            require_build_plan_order_index(plan, dependency_package_base) <
                    require_build_plan_order_index(plan, dependent_package_base),
            "Build unit order differs: " + std::string(dependency_package_base) +
                    " must precede " + std::string(dependent_package_base));
}

void expect_direct_aur_resolution(
        const BuildPlanDependencyEdge& edge, const std::string& expected_name,
        const std::string& expected_base) {
    expect(edge.kind == DependencyKind::Aur, "Direct AUR edge kind differs");
    expect(
            edge.resolved_package_name == std::optional<std::string>{expected_name},
            "Direct AUR resolved name differs");
    expect(
            edge.resolved_package_base == std::optional<std::string>{expected_base},
            "Direct AUR resolved base differs");
    expect(!edge.resolved_provider.has_value(), "Direct AUR edge has a provider");
}

template <typename Callable>
void expect_exception(Callable callable, const std::string& expected_message) {
    try {
        callable();
    } catch(const std::exception& error) {
        expect(
                std::string(error.what()) == expected_message,
                "Unexpected exception message: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Expected exception: " + expected_message);
}

template <typename Callable>
void expect_failure(Callable callable, const std::string& context) {
    try {
        callable();
    } catch(const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected failure: " + context);
}

template <typename Callable>
void expect_aur_rpc_response_error(
        Callable callable, const std::string& expected_message) {
    try {
        callable();
    } catch(const AurRpcResponseError& error) {
        expect(
                std::string(error.what()) == expected_message,
                "Unexpected AUR RPC response error: " + std::string(error.what()));
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Expected AurRpcResponseError, got: " + std::string(error.what()));
    }
    throw std::runtime_error(
            "Expected AurRpcResponseError: " + expected_message);
}

void expect_equivalent_plans(const BuildPlan& lhs, const BuildPlan& rhs) {
    expect(lhs.order.size() == rhs.order.size(), "BuildPlan::order size differs");
    for(std::size_t i = 0; i < lhs.order.size(); ++i) {
        expect(lhs.order[i].package_base == rhs.order[i].package_base, "BuildPlan::order base differs");
        expect(lhs.order[i].package_names == rhs.order[i].package_names, "BuildPlan::order names differ");
    }

    expect(lhs.root_targets == rhs.root_targets, "BuildPlan::root_targets differs");
    expect(lhs.package_targets.size() == rhs.package_targets.size(), "BuildPlan::package_targets size differs");
    for(std::size_t i = 0; i < lhs.package_targets.size(); ++i) {
        const PlannedPackageTarget& left = lhs.package_targets[i];
        const PlannedPackageTarget& right = rhs.package_targets[i];
        expect(left.package_name == right.package_name, "Planned package name differs");
        expect(left.package_base == right.package_base, "Planned package base differs");
        expect(left.roles == right.roles, "Planned package roles differ");
        expect(left.roots == right.roots, "Planned package roots differ");
    }

    expect(
            lhs.dependency_edges.size() == rhs.dependency_edges.size(),
            "BuildPlan::dependency_edges size differs");
    for(std::size_t i = 0; i < lhs.dependency_edges.size(); ++i) {
        const BuildPlanDependencyEdge& left = lhs.dependency_edges[i];
        const BuildPlanDependencyEdge& right = rhs.dependency_edges[i];
        expect(left.parent_package_name == right.parent_package_name, "Edge parent name differs");
        expect(left.parent_package_base == right.parent_package_base, "Edge parent base differs");
        expect(left.dependency_spec == right.dependency_spec, "Edge specification differs");
        expect(left.role == right.role, "Edge role differs");
        expect(left.kind == right.kind, "Edge kind differs");
        expect(left.resolved_package_name == right.resolved_package_name, "Edge resolved name differs");
        expect(left.resolved_package_base == right.resolved_package_base, "Edge resolved base differs");
        expect(
                left.resolved_provider == right.resolved_provider,
                "Edge provider differs");
    }

    expect(
            lhs.resolution_failures == rhs.resolution_failures,
            "BuildPlan::resolution_failures differs");

    expect(
            lhs.split_package_targets.size() == rhs.split_package_targets.size(),
            "BuildPlan::split_package_targets size differs");
    for(std::size_t i = 0; i < lhs.split_package_targets.size(); ++i) {
        expect(
                lhs.split_package_targets[i].package_base == rhs.split_package_targets[i].package_base,
                "Split package base differs");
        expect(
                lhs.split_package_targets[i].package_name == rhs.split_package_targets[i].package_name,
                "Split package name differs");
    }

    expect(lhs.provided.size() == rhs.provided.size(), "BuildPlan::provided size differs");
    for(std::size_t i = 0; i < lhs.provided.size(); ++i) {
        expect(lhs.provided[i].dependency == rhs.provided[i].dependency, "Provided dependency differs");
        expect(lhs.provided[i].provider == rhs.provided[i].provider, "Provider differs");
    }

    expect(lhs.metadata_risks.size() == rhs.metadata_risks.size(), "BuildPlan::metadata_risks size differs");
    for(std::size_t i = 0; i < lhs.metadata_risks.size(); ++i) {
        const BuildPlanMetadataRisk& left = lhs.metadata_risks[i];
        const BuildPlanMetadataRisk& right = rhs.metadata_risks[i];
        expect(left.package_name == right.package_name, "Metadata risk package name differs");
        expect(left.package_base == right.package_base, "Metadata risk package base differs");
        expect(left.conflicts == right.conflicts, "Metadata risk conflicts differs");
        expect(left.replaces == right.replaces, "Metadata risk replaces differs");
    }

    expect(
            lhs.ambiguous_providers.size() == rhs.ambiguous_providers.size(),
            "BuildPlan::ambiguous_providers size differs");
    for(std::size_t i = 0; i < lhs.ambiguous_providers.size(); ++i) {
        const AmbiguousProvidedDependency& left = lhs.ambiguous_providers[i];
        const AmbiguousProvidedDependency& right = rhs.ambiguous_providers[i];
        expect(left.dependency == right.dependency, "Ambiguous dependency differs");
        expect(left.candidates.size() == right.candidates.size(), "Ambiguous candidates size differs");
        for(std::size_t j = 0; j < left.candidates.size(); ++j) {
            expect(left.candidates[j] == right.candidates[j], "Ambiguous provider differs");
        }
    }

    expect(lhs.unresolved == rhs.unresolved, "BuildPlan::unresolved differs");
    expect(lhs.cycles == rhs.cycles, "BuildPlan::cycles differs");
}

void test_provider_origin_value_contract() {
    const ProvidedDependency repository =
            ProvidedDependency::from_repository("aur", "same-package");
    const ProvidedDependency same_repository =
            ProvidedDependency::from_repository("aur", "same-package");
    const ProvidedDependency aur =
            ProvidedDependency::from_aur("same-package");
    const ProvidedDependency same_aur =
            ProvidedDependency::from_aur("same-package");

    expect(repository == same_repository, "Repository provider equality differs");
    expect(aur == same_aur, "AUR provider equality differs");
    expect(repository != aur, "Repository and AUR provider origins were conflated");

    const auto* repository_origin =
            std::get_if<RepositoryProviderOrigin>(&repository.origin);
    expect(repository_origin != nullptr, "Repository provider origin kind differs");
    expect(
            repository_origin->repository_name == "aur",
            "Repository provider origin name differs");
    expect(
            std::holds_alternative<AurProviderOrigin>(aur.origin),
            "AUR provider origin kind differs");
    expect(
            provided_dependency_display(repository) == "aur/same-package" &&
                    provided_dependency_display(aur) == "aur/same-package",
            "Typed provider display compatibility differs");
}

void test_case_1_root_only() {
    BuildPlan plan = resolve_build_plan("case1-app");
    expect(
            plan.root_targets == std::vector<RootTargetIdentity>{{0, "case1-app"}},
            "Case 1 root identity differs");

    const PlannedPackageTarget& app = require_package_target(plan, "case1-app");
    expect(plan.package_targets.size() == 1, "Case 1 package target count differs");
    expect(plan.dependency_edges.empty(), "Case 1 unexpectedly has dependency edges");
    expect(app.package_base == "case1-app", "Case 1 package base differs");
    expect_roles(app, {PackageRole::Root});
    expect_roots(app, {{0, "case1-app"}});
    expect(desired_install_reason(app) == DesiredInstallReason::Explicit, "Case 1 reason differs");
    expect_legacy_order(plan, {"case1-app"});
}

void test_case_2_dependency_roles() {
    std::optional<AurPackageInfo> app_info = AurClient::info("case2-app");
    expect(app_info.has_value(), "Case 2 fixture is missing");
    std::vector<TypedPackageDependency> typed =
            collect_typed_build_dependencies(app_info.value());
    expect(typed.size() == 3, "Case 2 typed dependency count differs");
    expect(
            typed[0].specification == "case2-runtime-dep>=2" &&
                    typed[0].role == PackageRole::RuntimeDependency,
            "Case 2 runtime typed dependency differs");
    expect(
            typed[1].specification == "case2-build-dep" &&
                    typed[1].role == PackageRole::BuildDependency,
            "Case 2 build typed dependency differs");
    expect(
            typed[2].specification == "case2-check-dep" &&
                    typed[2].role == PackageRole::CheckDependency,
            "Case 2 check typed dependency differs");

    BuildPlan plan = resolve_build_plan("case2-app");
    expect_roles(
            require_package_target(plan, "case2-runtime-dep"),
            {PackageRole::RuntimeDependency});
    expect_roles(
            require_package_target(plan, "case2-build-dep"),
            {PackageRole::BuildDependency});
    expect_roles(
            require_package_target(plan, "case2-check-dep"),
            {PackageRole::CheckDependency});
    expect(
            package_target_count(plan, "case2-optional-dep") == 0,
            "Case 2 OptDepends leaked into the build plan");
    expect(plan.package_targets.size() == 4, "Case 2 package target count differs");
    expect(plan.dependency_edges.size() == 3, "Case 2 edge count differs");

    const BuildPlanDependencyEdge& runtime = require_edge(
            plan, "case2-app", "case2-runtime-dep>=2",
            PackageRole::RuntimeDependency);
    const BuildPlanDependencyEdge& build = require_edge(
            plan, "case2-app", "case2-build-dep", PackageRole::BuildDependency);
    const BuildPlanDependencyEdge& check = require_edge(
            plan, "case2-app", "case2-check-dep", PackageRole::CheckDependency);
    for(const BuildPlanDependencyEdge* edge : {&runtime, &build, &check}) {
        expect(edge->parent_package_base == "case2-app", "Case 2 edge parent base differs");
    }
    expect_direct_aur_resolution(runtime, "case2-runtime-dep", "case2-runtime-dep");
    expect_direct_aur_resolution(build, "case2-build-dep", "case2-build-dep");
    expect_direct_aur_resolution(check, "case2-check-dep", "case2-check-dep");
    expect(
            desired_install_reason(require_package_target(plan, "case2-build-dep")) ==
                    DesiredInstallReason::Dependency,
            "Case 2 build dependency reason differs");
    expect(
            desired_install_reason(require_package_target(plan, "case2-check-dep")) ==
                    DesiredInstallReason::Dependency,
            "Case 2 check dependency reason differs");
    expect_legacy_order(
            plan,
            {"case2-runtime-dep", "case2-build-dep", "case2-check-dep", "case2-app"});
}

void test_case_3_multiple_roles() {
    std::optional<AurPackageInfo> app_info = AurClient::info("case3-app");
    expect(app_info.has_value(), "Case 3 fixture is missing");
    expect(
            collect_typed_build_dependencies(app_info.value()).size() == 2,
            "Case 3 typed roles were deduplicated");
    expect(
            collect_build_dependencies(app_info.value()) ==
                    std::vector<std::string>{"case3-shared"},
            "Case 3 legacy dependency dedup changed");

    BuildPlan plan = resolve_build_plan("case3-app");
    expect(package_target_count(plan, "case3-shared") == 1, "Case 3 target was not merged");
    const PlannedPackageTarget& shared = require_package_target(plan, "case3-shared");
    expect_roles(
            shared,
            {PackageRole::RuntimeDependency, PackageRole::BuildDependency});
    static_cast<void>(require_edge(
            plan, "case3-app", "case3-shared", PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case3-app", "case3-shared", PackageRole::BuildDependency));
    expect(plan.dependency_edges.size() == 2, "Case 3 edge count differs");
    expect_legacy_order(plan, {"case3-shared", "case3-app"});
    expect(
            desired_install_reason(shared) == DesiredInstallReason::Dependency,
            "Case 3 reason differs");
}

void test_case_4_root_dependency_overlap() {
    BuildPlan plan = resolve_build_plan(
            std::vector<std::string>{"case4-app", "case4-lib"});
    const PlannedPackageTarget& lib = require_package_target(plan, "case4-lib");
    expect_roles(lib, {PackageRole::Root, PackageRole::RuntimeDependency});
    expect_roots(lib, {{0, "case4-app"}, {1, "case4-lib"}});
    expect(
            desired_install_reason(lib) == DesiredInstallReason::Explicit,
            "Case 4 explicit-wins reducer differs");
    expect(package_target_count(plan, "case4-lib") == 1, "Case 4 lib target was duplicated");
    expect_legacy_order(plan, {"case4-lib", "case4-app"});
}

void test_case_5_shared_dependency() {
    BuildPlan plan = resolve_build_plan(
            std::vector<std::string>{"case5-app", "case5-tool"});
    const PlannedPackageTarget& common = require_package_target(plan, "case5-common");
    expect(package_target_count(plan, "case5-common") == 1, "Case 5 common target was duplicated");
    expect_roles(common, {PackageRole::RuntimeDependency});
    expect_roots(common, {{0, "case5-app"}, {1, "case5-tool"}});
    expect(
            desired_install_reason(common) == DesiredInstallReason::Dependency,
            "Case 5 reason differs");
    expect_legacy_order(plan, {"case5-common", "case5-app", "case5-tool"});
}

void test_case_6_repository_dependency() {
    BuildPlan plan = resolve_build_plan("case6-app");
    const BuildPlanDependencyEdge& edge = require_edge(
            plan, "case6-app", "case6-repo-lib", PackageRole::RuntimeDependency);
    expect(edge.kind == DependencyKind::Repo, "Case 6 edge kind differs");
    expect(
            edge.resolved_package_name == std::optional<std::string>{"case6-repo-lib"},
            "Case 6 resolved package differs");
    expect(!edge.resolved_package_base.has_value(), "Case 6 resolved base must be empty");
    expect(!edge.resolved_provider.has_value(), "Case 6 provider must be empty");
    expect(package_target_count(plan, "case6-repo-lib") == 0, "Case 6 repo target leaked into source plan");
    expect_legacy_order(plan, {"case6-app"});
}

void test_case_7_unique_aur_provider() {
    BuildPlan plan = resolve_build_plan("case7-app");
    const BuildPlanDependencyEdge& edge = require_edge(
            plan, "case7-app", "case7-virtual-api", PackageRole::RuntimeDependency);
    expect(edge.kind == DependencyKind::Provided, "Case 7 edge kind differs");
    expect(edge.resolved_provider.has_value(), "Case 7 provider is missing");
    expect(
            edge.resolved_provider.value() ==
                    ProvidedDependency::from_aur("case7-provider-pkg"),
            "Case 7 provider differs");
    const PlannedPackageTarget& provider =
            require_package_target(plan, "case7-provider-pkg");
    expect_roles(provider, {PackageRole::RuntimeDependency});
    expect_roots(provider, {{0, "case7-app"}});
    expect(plan.provided.size() == 1, "Case 7 legacy provider count differs");
    expect(plan.provided[0].dependency == "case7-virtual-api", "Case 7 legacy dependency differs");
    expect(
            plan.provided[0].provider ==
                    ProvidedDependency::from_aur("case7-provider-pkg"),
            "Case 7 legacy provider differs");
    expect_legacy_order(plan, {"case7-provider-pkg", "case7-app"});
}

void test_case_8_ambiguous_provider() {
    BuildPlan plan = resolve_build_plan("case8-app");
    const BuildPlanDependencyEdge& edge = require_edge(
            plan, "case8-app", "case8-virtual", PackageRole::RuntimeDependency);
    expect(edge.kind == DependencyKind::AmbiguousProvider, "Case 8 edge kind differs");
    expect(!edge.resolved_package_name.has_value(), "Case 8 resolved name must be empty");
    expect(!edge.resolved_package_base.has_value(), "Case 8 resolved base must be empty");
    expect(!edge.resolved_provider.has_value(), "Case 8 provider must be empty");
    expect(plan.ambiguous_providers.size() == 1, "Case 8 ambiguous dependency count differs");
    expect(plan.ambiguous_providers[0].candidates.size() == 2, "Case 8 candidate count differs");
    expect(package_target_count(plan, "case8-provider-a") == 0, "Case 8 selected a provider");
    expect(package_target_count(plan, "case8-provider-b") == 0, "Case 8 selected a provider");
    expect_exception(
            [&plan]() { require_fetchable_build_plan("case8-app", plan); },
            "Cannot execute build plan for case8-app; ambiguous providers: "
            "case8-virtual (extra/case8-provider-a, community/case8-provider-b)");
}

void test_case_9_unknown_dependency() {
    BuildPlan plan = resolve_build_plan("case9-app");
    const BuildPlanDependencyEdge& edge = require_edge(
            plan, "case9-app", "case9-missing", PackageRole::RuntimeDependency);
    expect(edge.kind == DependencyKind::Unknown, "Case 9 edge kind differs");
    expect(!edge.resolved_package_name.has_value(), "Case 9 resolved name must be empty");
    expect(!edge.resolved_package_base.has_value(), "Case 9 resolved base must be empty");
    expect(!edge.resolved_provider.has_value(), "Case 9 provider must be empty");
    expect(
            plan.unresolved == std::vector<std::string>{"case9-missing"},
            "Case 9 unresolved contract differs");
}

void test_case_10_dependency_chain() {
    BuildPlan plan = resolve_build_plan("case10-app");
    expect_roots(require_package_target(plan, "case10-lib"), {{0, "case10-app"}});
    expect_roots(require_package_target(plan, "case10-common"), {{0, "case10-app"}});
    static_cast<void>(require_edge(
            plan, "case10-app", "case10-lib", PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case10-lib", "case10-common", PackageRole::RuntimeDependency));
    expect_legacy_order(plan, {"case10-common", "case10-lib", "case10-app"});
}

void test_case_11_single_overload_compatibility() {
    BuildPlan single = resolve_build_plan("case11-root");
    BuildPlan multiple = resolve_build_plan(
            std::vector<std::string>{"case11-root"});
    expect(!single.split_package_targets.empty(), "Case 11 split fixture is empty");
    expect(!single.provided.empty(), "Case 11 provider fixture is empty");
    expect(!single.metadata_risks.empty(), "Case 11 risk fixture is empty");
    expect(!single.ambiguous_providers.empty(), "Case 11 ambiguous fixture is empty");
    expect(!single.unresolved.empty(), "Case 11 unresolved fixture is empty");
    expect(!single.cycles.empty(), "Case 11 cycle fixture is empty");
    expect(!single.dependency_edges.empty(), "Case 11 edge fixture is empty");
    const PlannedPackageTarget& root = require_package_target(single, "case11-root");
    expect_roles(root, {PackageRole::Root, PackageRole::RuntimeDependency});
    expect_roots(root, {{0, "case11-root"}});
    const PlannedPackageTarget& direct = require_package_target(single, "case11-direct");
    expect(
            direct.package_base == "case11-direct-base",
            "Case 11 planned package base differs");
    const BuildPlanDependencyEdge& direct_edge = require_edge(
            single, "case11-root", "case11-direct", PackageRole::RuntimeDependency);
    expect_direct_aur_resolution(direct_edge, "case11-direct", "case11-direct-base");
    expect(single.order.size() == 4, "Case 11 legacy order size differs");
    expect(
            single.order[0].package_base == "case11-direct-base" &&
                    single.order[0].package_names ==
                            std::vector<std::string>{"case11-direct"},
            "Case 11 split legacy entry differs");
    expect_equivalent_plans(single, multiple);
}

void test_case_12_invalid_reducer_state() {
    PlannedPackageTarget target{"case12-invalid", "case12-invalid", {}, {}};
    expect_failure(
            [&target]() { static_cast<void>(desired_install_reason(target)); },
            "role-less desired reason");
}

void test_supplemental_multi_root_contracts() {
    expect_failure(
            []() { static_cast<void>(resolve_build_plan(std::vector<std::string>{})); },
            "empty multi-root build plan");

    BuildPlan transitive = resolve_build_plan(
            std::vector<std::string>{"case13-app", "case13-lib"});
    expect_roots(
            require_package_target(transitive, "case13-common"),
            {{0, "case13-app"}, {1, "case13-lib"}});

    BuildPlan duplicate = resolve_build_plan(
            std::vector<std::string>{"case1-app", "case1-app"});
    expect(
            duplicate.root_targets ==
                    std::vector<RootTargetIdentity>{{0, "case1-app"}, {1, "case1-app"}},
            "Duplicate root identities were not preserved");
    expect_roots(
            require_package_target(duplicate, "case1-app"),
            {{0, "case1-app"}, {1, "case1-app"}});
    expect_legacy_order(duplicate, {"case1-app"});

    BuildPlan repository_provider = resolve_build_plan("case14-app");
    const BuildPlanDependencyEdge& provider_edge = require_edge(
            repository_provider, "case14-app", "case14-virtual",
            PackageRole::RuntimeDependency);
    expect(provider_edge.kind == DependencyKind::Provided, "Repository provider kind differs");
    expect(provider_edge.resolved_provider.has_value(), "Repository provider is missing");
    expect(
            provider_edge.resolved_provider.value() ==
                    ProvidedDependency::from_repository(
                            "aur", "case14-provider"),
            "Repository provider result differs");
    const auto* repository_origin = std::get_if<RepositoryProviderOrigin>(
            &provider_edge.resolved_provider->origin);
    expect(
            repository_origin != nullptr &&
                    repository_origin->repository_name == "aur",
            "Repository provider origin differs");
    expect(
            provided_dependency_display(
                    provider_edge.resolved_provider.value()) ==
                    "aur/case14-provider",
            "Repository provider display differs");
    expect(
            package_target_count(repository_provider, "case14-provider") == 0,
            "Repository provider leaked into source package targets");
    expect(
            repository_provider.package_targets.size() == 1,
            "Repository provider changed source target count");
    expect_roots(
            require_package_target(repository_provider, "case14-app"),
            {{0, "case14-app"}});
    expect_legacy_order(repository_provider, {"case14-app"});

    BuildPlan all_roles = resolve_build_plan(
            std::vector<std::string>{"case15-app", "case15-shared"});
    const PlannedPackageTarget& shared = require_package_target(all_roles, "case15-shared");
    expect_roles(
            shared,
            {PackageRole::Root, PackageRole::RuntimeDependency,
             PackageRole::BuildDependency, PackageRole::CheckDependency});
    expect(
            desired_install_reason(shared) == DesiredInstallReason::Explicit,
            "All-role explicit-wins reducer differs");
}

void test_same_package_base_root_coverage() {
    BuildPlan plan = resolve_build_plan(
            std::vector<std::string>{"case16-child-a", "case16-child-b"});
    expect(
            plan.root_targets == std::vector<RootTargetIdentity>{
                    {0, "case16-child-a"},
                    {1, "case16-child-b"},
            },
            "Same-base root identities differ");

    const BuildPlanEntry& suite =
            require_build_plan_entry(plan, "case16-suite");
    expect(
            suite.package_names == std::vector<std::string>{
                    "case16-child-a", "case16-child-b"},
            "Same-base required child order differs");
    expect(plan.order.size() == 4, "Same-base build unit count differs");

    const PlannedPackageTarget& child_a =
            require_package_target(plan, "case16-child-a");
    const PlannedPackageTarget& child_b =
            require_package_target(plan, "case16-child-b");
    const PlannedPackageTarget& shared =
            require_package_target(plan, "case16-shared");
    expect(plan.package_targets.size() == 5, "Same-base package target count differs");
    expect(package_target_count(plan, "case16-child-a") == 1, "First child target was duplicated");
    expect(package_target_count(plan, "case16-child-b") == 1, "Second child target was duplicated");
    expect(package_target_count(plan, "case16-shared") == 1, "Shared dependency target was duplicated");
    expect(child_a.package_base == "case16-suite", "First child PackageBase differs");
    expect(child_b.package_base == "case16-suite", "Second child PackageBase differs");
    expect_roles(child_a, {PackageRole::Root});
    expect_roles(child_b, {PackageRole::Root});
    expect_roles(shared, {PackageRole::RuntimeDependency});
    expect_roots(child_a, {{0, "case16-child-a"}});
    expect_roots(child_b, {{1, "case16-child-b"}});
    expect_roots(
            shared,
            {{0, "case16-child-a"}, {1, "case16-child-b"}});

    static_cast<void>(require_edge(
            plan, "case16-child-a", "case16-a-only",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case16-child-a", "case16-shared",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case16-child-b", "case16-b-only",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case16-child-b", "case16-shared",
            PackageRole::RuntimeDependency));
    expect(plan.dependency_edges.size() == 4, "Same-base dependency edge count differs");

    expect_build_unit_before(plan, "case16-a-only", "case16-suite");
    expect_build_unit_before(plan, "case16-b-only", "case16-suite");
    expect_build_unit_before(plan, "case16-shared", "case16-suite");
    expect(plan.cycles.empty(), "Independent same-base roots produced a cycle");
}

void test_same_package_base_sibling_attribution() {
    BuildPlan plan = resolve_build_plan(std::vector<std::string>{
            "case17-parent", "case17-parent", "case17-sibling"});
    expect(
            plan.root_targets == std::vector<RootTargetIdentity>{
                    {0, "case17-parent"},
                    {1, "case17-parent"},
                    {2, "case17-sibling"},
            },
            "Same-base duplicate root identities differ");

    const BuildPlanEntry& suite =
            require_build_plan_entry(plan, "case17-suite");
    expect(
            suite.package_names == std::vector<std::string>{
                    "case17-parent", "case17-sibling"},
            "Same-base sibling order or deduplication differs");
    expect(plan.order.size() == 2, "Same-base sibling build unit count differs");
    expect(plan.package_targets.size() == 3, "Same-base sibling target count differs");
    expect(package_target_count(plan, "case17-parent") == 1, "Parent target was duplicated");
    expect(package_target_count(plan, "case17-sibling") == 1, "Sibling target was duplicated");

    const PlannedPackageTarget& parent =
            require_package_target(plan, "case17-parent");
    const PlannedPackageTarget& sibling =
            require_package_target(plan, "case17-sibling");
    const PlannedPackageTarget& leaf =
            require_package_target(plan, "case17-leaf");
    expect_roles(parent, {PackageRole::Root});
    expect_roles(
            sibling,
            {PackageRole::Root, PackageRole::RuntimeDependency});
    expect_roles(leaf, {PackageRole::RuntimeDependency});
    expect_roots(
            sibling,
            {
                    {0, "case17-parent"},
                    {1, "case17-parent"},
                    {2, "case17-sibling"},
            });
    expect_roots(
            leaf,
            {
                    {0, "case17-parent"},
                    {1, "case17-parent"},
                    {2, "case17-sibling"},
            });
    expect(
            desired_install_reason(sibling) == DesiredInstallReason::Explicit,
            "Same-base root/dependency reason did not prefer Explicit");

    static_cast<void>(require_edge(
            plan, "case17-parent", "case17-sibling",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case17-sibling", "case17-leaf",
            PackageRole::RuntimeDependency));
    expect_build_unit_before(plan, "case17-leaf", "case17-suite");
    expect(plan.cycles.empty(), "Same-base sibling dependency produced a false cycle");
}

void test_real_dependency_cycle_is_preserved() {
    BuildPlan plan = resolve_build_plan("case18-cycle-a");
    static_cast<void>(require_edge(
            plan, "case18-cycle-a", "case18-cycle-b",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case18-cycle-b", "case18-cycle-a",
            PackageRole::RuntimeDependency));
    expect(!plan.cycles.empty(), "Real dependency cycle was not preserved");
}

void test_same_package_base_real_cycle_is_preserved() {
    BuildPlan plan = resolve_build_plan("case20-cycle-a");
    static_cast<void>(require_edge(
            plan, "case20-cycle-a", "case20-cycle-b",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case20-cycle-b", "case20-cycle-a",
            PackageRole::RuntimeDependency));
    expect(
            plan.order.size() == 1 &&
                    plan.order.front().package_base == "case20-suite" &&
                    plan.order.front().package_names ==
                            std::vector<std::string>{
                                    "case20-cycle-a", "case20-cycle-b"},
            "Same-base real cycle lost its exact execution unit");
    expect(
            plan.cycles == std::vector<std::string>{"case20-suite"},
            "Same-base real cycle was not preserved exactly once");
}

void test_same_package_base_late_dependency_ordering() {
    BuildPlan plan = resolve_build_plan(std::vector<std::string>{
            "case19-consumer", "case19-suite-b"});
    const BuildPlanEntry& suite =
            require_build_plan_entry(plan, "case19-suite");
    expect(
            suite.package_names == std::vector<std::string>{
                    "case19-suite-a", "case19-suite-b"},
            "Late same-base child aggregation differs");
    expect(plan.package_targets.size() == 5, "Late dependency target count differs");
    expect(package_target_count(plan, "case19-suite-a") == 1, "Early suite target was duplicated");
    expect(package_target_count(plan, "case19-suite-b") == 1, "Late suite target was duplicated");

    static_cast<void>(require_edge(
            plan, "case19-consumer", "case19-suite-a",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case19-suite-a", "case19-early-dep",
            PackageRole::RuntimeDependency));
    static_cast<void>(require_edge(
            plan, "case19-suite-b", "case19-late-dep",
            PackageRole::RuntimeDependency));

    // LANDMINE(#268): a late child may add dependencies after a consumer was first traversed.
    // Moving only the existing PackageBase entry would invert the consumer dependency.
    expect_build_unit_before(plan, "case19-early-dep", "case19-suite");
    expect_build_unit_before(plan, "case19-late-dep", "case19-suite");
    expect_build_unit_before(plan, "case19-suite", "case19-consumer");
    expect(plan.cycles.empty(), "Late same-base child produced a cycle");
}

void test_preflight_root_failure_and_continuation() {
    BuildPlan plan = resolve_build_plan_for_preflight(
            {"preflight-root-metadata-failure", "preflight-later-root"});

    expect(
            plan.root_targets == std::vector<RootTargetIdentity>{
                    {0, "preflight-root-metadata-failure"},
                    {1, "preflight-later-root"},
            },
            "Preflight root identities differ after root failure");
    expect(plan.resolution_failures.size() == 1, "Unexpected root failure count");
    const BuildPlanResolutionFailure& failure = require_resolution_failure(
            plan, BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            "preflight-root-metadata-failure");
    expect_resolution_failure_context(
            failure, std::nullopt, std::nullopt, std::nullopt,
            {{0, "preflight-root-metadata-failure"}},
            "strict root metadata failure");
    expect(
            plan.unresolved ==
                    std::vector<std::string>{"preflight-root-metadata-failure"},
            "Root metadata failure did not remain unresolved");
    expect_roots(
            require_package_target(plan, "preflight-later-root"),
            {{1, "preflight-later-root"}});
    expect_legacy_order(plan, {"preflight-later-root"});

    BuildPlan duplicate_failure = resolve_build_plan_for_preflight({
            "preflight-root-metadata-failure",
            "preflight-root-metadata-failure",
    });
    expect(
            duplicate_failure.resolution_failures.size() == 1,
            "Duplicate ordinary failure was not deduplicated");
    expect(
            duplicate_failure.resolution_failures.front().roots ==
                    std::vector<RootTargetIdentity>{
                            {0, "preflight-root-metadata-failure"},
                            {1, "preflight-root-metadata-failure"},
                    },
            "Deduplicated failure roots differ");
}

void test_preflight_dependency_and_provider_failures() {
    BuildPlan dependency = resolve_build_plan_for_preflight(
            {"preflight-dependency-failure-root"});
    expect(
            dependency.resolution_failures.size() == 1,
            "Unexpected dependency metadata failure count");
    const BuildPlanResolutionFailure& dependency_failure =
            require_resolution_failure(
                    dependency,
                    BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
                    "preflight-dependency-failure-child");
    expect_resolution_failure_context(
            dependency_failure,
            std::optional<std::string>{"preflight-dependency-failure-root"},
            std::optional<std::string>{"preflight-dependency-failure-root"},
            std::optional<std::string>{"preflight-dependency-failure-child"},
            {{0, "preflight-dependency-failure-root"}},
            "strict dependency metadata failure");
    expect(
            require_edge(
                    dependency, "preflight-dependency-failure-root",
                    "preflight-dependency-failure-child",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Unknown,
            "Dependency metadata failure was treated as resolved");

    BuildPlan provider_search = resolve_build_plan_for_preflight(
            {"preflight-provider-search-root"});
    expect(
            provider_search.resolution_failures.size() == 1,
            "Unexpected provider search failure count");
    const BuildPlanResolutionFailure& search_failure =
            require_resolution_failure(
                    provider_search,
                    BuildPlanResolutionFailureKind::ProviderSearchUnavailable,
                    "preflight-provider-search-virtual");
    expect_resolution_failure_context(
            search_failure,
            std::optional<std::string>{"preflight-provider-search-root"},
            std::optional<std::string>{"preflight-provider-search-root"},
            std::optional<std::string>{"preflight-provider-search-virtual"},
            {{0, "preflight-provider-search-root"}},
            "strict provider search failure");

    BuildPlan provider_candidate = resolve_build_plan_for_preflight(
            {"preflight-provider-candidate-root"});
    expect(
            provider_candidate.resolution_failures.size() == 1,
            "Unexpected provider candidate failure count");
    const BuildPlanResolutionFailure& candidate_failure =
            require_resolution_failure(
                    provider_candidate,
                    BuildPlanResolutionFailureKind::ProviderCandidateMetadataUnavailable,
                    "preflight-provider-broken");
    expect_resolution_failure_context(
            candidate_failure,
            std::optional<std::string>{"preflight-provider-candidate-root"},
            std::optional<std::string>{"preflight-provider-candidate-root"},
            std::optional<std::string>{"preflight-provider-candidate-virtual"},
            {{0, "preflight-provider-candidate-root"}},
            "strict provider candidate failure");
    const BuildPlanDependencyEdge& provider_edge = require_edge(
            provider_candidate, "preflight-provider-candidate-root",
            "preflight-provider-candidate-virtual",
            PackageRole::RuntimeDependency);
    expect(
            provider_edge.kind == DependencyKind::Provided &&
                    provider_edge.resolved_provider.has_value() &&
                    provider_edge.resolved_provider.value() ==
                            ProvidedDependency::from_aur(
                                    "preflight-provider-good"),
            "Known provider was lost after another candidate failed");
    expect(
            provider_candidate.unresolved.empty(),
            "Known provider was incorrectly left unresolved");
}

void test_preflight_shared_failure_root_attribution() {
    BuildPlan plan = resolve_build_plan_for_preflight(
            {"preflight-shared-root-a", "preflight-shared-root-b"});

    expect(plan.resolution_failures.size() == 1, "Unexpected shared failure count");
    const BuildPlanResolutionFailure& failure = require_resolution_failure(
            plan, BuildPlanResolutionFailureKind::AurPackageMetadataUnavailable,
            "preflight-shared-failure");
    expect_resolution_failure_context(
            failure,
            std::optional<std::string>{"preflight-shared-parent"},
            std::optional<std::string>{"preflight-shared-parent"},
            std::optional<std::string>{"preflight-shared-failure"},
            {
                    {0, "preflight-shared-root-a"},
                    {1, "preflight-shared-root-b"},
            },
            "strict shared dependency metadata failure");
    expect_roots(
            require_package_target(plan, "preflight-shared-parent"),
            {
                    {0, "preflight-shared-root-a"},
                    {1, "preflight-shared-root-b"},
            });
}

void test_preflight_response_error_propagation() {
    expect_aur_rpc_response_error(
            []() {
                static_cast<void>(resolve_build_plan_for_preflight(
                        {"preflight-response-error-root"}));
            },
            "root response failure");
    expect_aur_rpc_response_error(
            []() {
                static_cast<void>(resolve_build_plan_for_preflight(
                        {"preflight-provider-response-root"}));
            },
            "provider search response failure");
    expect_aur_rpc_response_error(
            []() {
                static_cast<void>(resolve_build_plan_for_preflight({
                        "preflight-dependency-response-root",
                        "preflight-response-must-stop-before-later-root",
                }));
            },
            "dependency info response failure");
    expect_aur_rpc_response_error(
            []() {
                static_cast<void>(resolve_build_plan_for_preflight({
                        "preflight-provider-candidate-response-root",
                        "preflight-response-must-stop-before-later-root",
                }));
            },
            "provider candidate info response failure");
}

void test_preflight_repository_query_boundary() {
    dependency_plan_repository_query_stub::reset_query_counts();
    BuildPlan repository_dependency =
            resolve_build_plan_for_preflight({"case6-app"});
    expect(
            require_edge(
                    repository_dependency, "case6-app", "case6-repo-lib",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Repo,
            "Strict resolver lost an exact repository dependency");
    expect(
            dependency_plan_repository_query_stub::legacy_repo_package_query_count() == 0,
            "Strict resolver called the legacy pacman repository query");
    expect(
            dependency_plan_repository_query_stub::sync_database_package_query_count() == 1,
            "Strict resolver did not use the sync database package query");

    BuildPlan repository_provider =
            resolve_build_plan_for_preflight({"case14-app"});
    expect(
            require_edge(
                    repository_provider, "case14-app", "case14-virtual",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Provided,
            "Strict resolver lost the pacman-first repository provider");
    expect(
            repository_provider.resolution_failures.empty(),
            "Repository provider lookup produced a strict AUR failure");
    expect(
            dependency_plan_repository_query_stub::strict_repo_provider_query_count() == 1,
            "Strict resolver did not use the typed repository provider query");
}

void test_preflight_repository_metadata_failures() {
    dependency_plan_repository_query_stub::reset_query_counts();
    BuildPlan exact_failure = resolve_build_plan_for_preflight({
            "preflight-repository-exact-failure-root",
            "preflight-later-root",
    });
    expect(
            exact_failure.resolution_failures.size() == 1,
            "Unexpected strict repository exact failure count");
    const BuildPlanResolutionFailure& exact = require_resolution_failure(
            exact_failure,
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            "preflight-repository-exact-failure-child");
    expect_resolution_failure_context(
            exact,
            std::optional<std::string>{"preflight-repository-exact-failure-root"},
            std::optional<std::string>{"preflight-repository-exact-failure-root"},
            std::optional<std::string>{"preflight-repository-exact-failure-child"},
            {{0, "preflight-repository-exact-failure-root"}},
            "strict repository exact metadata failure");
    expect(
            require_edge(
                    exact_failure,
                    "preflight-repository-exact-failure-root",
                    "preflight-repository-exact-failure-child",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Unknown,
            "Unavailable repository exact metadata was treated as confirmed absence");
    expect_roots(
            require_package_target(exact_failure, "preflight-later-root"),
            {{1, "preflight-later-root"}});
    expect(
            dependency_plan_repository_query_stub::strict_repo_provider_query_count() == 0,
            "Repository exact failure continued into provider lookup");

    dependency_plan_repository_query_stub::reset_query_counts();
    BuildPlan provider_failure = resolve_build_plan_for_preflight(
            {"preflight-repository-provider-failure-root"});
    expect(
            provider_failure.resolution_failures.size() == 1,
            "Unexpected strict repository provider failure count");
    const BuildPlanResolutionFailure& provider = require_resolution_failure(
            provider_failure,
            BuildPlanResolutionFailureKind::RepositoryMetadataUnavailable,
            "preflight-repository-provider-failure-virtual");
    expect_resolution_failure_context(
            provider,
            std::optional<std::string>{"preflight-repository-provider-failure-root"},
            std::optional<std::string>{"preflight-repository-provider-failure-root"},
            std::optional<std::string>{"preflight-repository-provider-failure-virtual"},
            {{0, "preflight-repository-provider-failure-root"}},
            "strict repository provider metadata failure");
    expect(
            require_edge(
                    provider_failure,
                    "preflight-repository-provider-failure-root",
                    "preflight-repository-provider-failure-virtual",
                    PackageRole::RuntimeDependency)
                            .kind == DependencyKind::Unknown,
            "Unavailable repository provider metadata was treated as an empty provider set");
    expect(
            dependency_plan_repository_query_stub::strict_repo_provider_query_count() == 1,
            "Strict repository provider query count differs after failure");
}

void test_legacy_resolution_failure_boundary() {
    BuildPlan normal = resolve_build_plan("case1-app");
    expect(
            normal.resolution_failures.empty(),
            "Legacy resolver added failures to a successful plan");

    BuildPlan dependency = resolve_build_plan(
            "preflight-dependency-failure-root");
    expect(
            dependency.resolution_failures.empty(),
            "Legacy resolver captured dependency metadata failure");
    expect(
            dependency.unresolved ==
                    std::vector<std::string>{"preflight-dependency-failure-child"},
            "Legacy dependency failure behavior changed");

    BuildPlan provider_search = resolve_build_plan(
            "preflight-provider-search-root");
    expect(
            provider_search.resolution_failures.empty(),
            "Legacy resolver captured provider search failure");
    expect(
            provider_search.unresolved ==
                    std::vector<std::string>{"preflight-provider-search-virtual"},
            "Legacy provider search failure behavior changed");

    BuildPlan provider_candidate = resolve_build_plan(
            "preflight-provider-candidate-root");
    expect(
            provider_candidate.resolution_failures.empty(),
            "Legacy resolver captured provider candidate failure");
    expect(
            provider_candidate.unresolved.empty(),
            "Legacy resolver lost the surviving provider candidate");

    expect_exception(
            []() {
                static_cast<void>(resolve_build_plan(
                        "preflight-root-metadata-failure"));
            },
            "legacy root metadata failure");
    expect_exception(
            []() {
                static_cast<void>(resolve_build_plan("preflight-root-not-found"));
            },
            "AUR package not found: preflight-root-not-found");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "provider origin value contract",
                test_provider_origin_value_contract);
        run_case("Case 1 root only", test_case_1_root_only);
        run_case("Case 2 dependency roles", test_case_2_dependency_roles);
        run_case("Case 3 multiple roles", test_case_3_multiple_roles);
        run_case("Case 4 root/dependency overlap", test_case_4_root_dependency_overlap);
        run_case("Case 5 shared dependency", test_case_5_shared_dependency);
        run_case("Case 6 repository dependency", test_case_6_repository_dependency);
        run_case("Case 7 unique AUR provider", test_case_7_unique_aur_provider);
        run_case("Case 8 ambiguous provider", test_case_8_ambiguous_provider);
        run_case("Case 9 unknown dependency", test_case_9_unknown_dependency);
        run_case("Case 10 dependency chain", test_case_10_dependency_chain);
        run_case("Case 11 single overload compatibility", test_case_11_single_overload_compatibility);
        run_case("Case 12 invalid reducer state", test_case_12_invalid_reducer_state);
        run_case("supplemental multi-root contracts", test_supplemental_multi_root_contracts);
        run_case(
                "same PackageBase root coverage",
                test_same_package_base_root_coverage);
        run_case(
                "same PackageBase sibling attribution",
                test_same_package_base_sibling_attribution);
        run_case(
                "real dependency cycle is preserved",
                test_real_dependency_cycle_is_preserved);
        run_case(
                "same PackageBase real cycle is preserved",
                test_same_package_base_real_cycle_is_preserved);
        run_case(
                "same PackageBase late dependency ordering",
                test_same_package_base_late_dependency_ordering);
        run_case(
                "preflight root failure and continuation",
                test_preflight_root_failure_and_continuation);
        run_case(
                "preflight dependency and provider failures",
                test_preflight_dependency_and_provider_failures);
        run_case(
                "preflight shared failure root attribution",
                test_preflight_shared_failure_root_attribution);
        run_case(
                "preflight response error propagation",
                test_preflight_response_error_propagation);
        run_case(
                "preflight repository query boundary",
                test_preflight_repository_query_boundary);
        run_case(
                "preflight repository metadata failures",
                test_preflight_repository_metadata_failures);
        run_case(
                "legacy resolution failure boundary",
                test_legacy_resolution_failure_boundary);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "dependency plan model tests: all checks passed\n";
    return 0;
}
