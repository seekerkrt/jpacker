#include "aur_rpc.hpp"
#include "dependency_plan.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

void expect_roles(
        const PlannedPackageTarget& target, const std::vector<PackageRole>& expected) {
    expect(target.roles == expected, "Unexpected roles for " + target.package_name);
}

void expect_roots(
        const PlannedPackageTarget& target,
        const std::vector<RootTargetIdentity>& expected) {
    expect(target.roots == expected, "Unexpected roots for " + target.package_name);
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

bool same_provider(const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.repository == rhs.repository && lhs.package_name == rhs.package_name;
}

bool same_optional_provider(
        const std::optional<ProvidedDependency>& lhs,
        const std::optional<ProvidedDependency>& rhs) {
    if(lhs.has_value() != rhs.has_value()) return false;
    return !lhs.has_value() || same_provider(lhs.value(), rhs.value());
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
                same_optional_provider(left.resolved_provider, right.resolved_provider),
                "Edge provider differs");
    }

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
        expect(same_provider(lhs.provided[i].provider, rhs.provided[i].provider), "Provider differs");
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
            expect(same_provider(left.candidates[j], right.candidates[j]), "Ambiguous provider differs");
        }
    }

    expect(lhs.unresolved == rhs.unresolved, "BuildPlan::unresolved differs");
    expect(lhs.cycles == rhs.cycles, "BuildPlan::cycles differs");
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
            same_provider(
                    edge.resolved_provider.value(),
                    ProvidedDependency{"aur", "case7-provider-pkg"}),
            "Case 7 provider differs");
    const PlannedPackageTarget& provider =
            require_package_target(plan, "case7-provider-pkg");
    expect_roles(provider, {PackageRole::RuntimeDependency});
    expect_roots(provider, {{0, "case7-app"}});
    expect(plan.provided.size() == 1, "Case 7 legacy provider count differs");
    expect(plan.provided[0].dependency == "case7-virtual-api", "Case 7 legacy dependency differs");
    expect(
            same_provider(
                    plan.provided[0].provider,
                    ProvidedDependency{"aur", "case7-provider-pkg"}),
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
            same_provider(
                    provider_edge.resolved_provider.value(),
                    ProvidedDependency{"extra", "case14-provider"}),
            "Repository provider result differs");
    expect(
            package_target_count(repository_provider, "case14-provider") == 0,
            "Repository provider leaked into source package targets");
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

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
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
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "dependency plan model tests: all checks passed\n";
    return 0;
}
