#include "package_base_artifact_install_plan.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

template <typename T>
concept HasArtifactIndex = requires(const T& value) {
    value.artifact_index;
};

template <typename T>
concept HasArtifactPath = requires(const T& value) {
    value.artifact_path;
};

template <typename T>
concept HasWorkspacePath = requires(const T& value) {
    value.workspace_path;
};

template <typename T>
concept CanMutateSuccess = requires(T& value) {
    value.success()->selected_artifacts.clear();
};

static_assert(
        !std::is_default_constructible_v<
                PackageBaseArtifactInstallReasonPlanResult>);
static_assert(
        !std::is_aggregate_v<PackageBaseArtifactInstallReasonPlanResult>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactInstallReasonPlanResult,
                                 PackageBaseArtifactInstallReasonPlan>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactInstallReasonPlanResult,
                                 MixedPackageBaseInstallReasonUnsupported>);
static_assert(
        std::is_copy_constructible_v<
                PackageBaseArtifactInstallReasonPlanResult>);
static_assert(
        std::is_move_constructible_v<
                PackageBaseArtifactInstallReasonPlanResult>);
static_assert(
        !std::is_copy_assignable_v<
                PackageBaseArtifactInstallReasonPlanResult>);
static_assert(
        !std::is_move_assignable_v<
                PackageBaseArtifactInstallReasonPlanResult>);
static_assert(std::same_as<
              decltype(std::declval<
                               PackageBaseArtifactInstallReasonPlanResult&>()
                               .success()),
              const PackageBaseArtifactInstallReasonPlan*>);
static_assert(std::same_as<
              decltype(std::declval<
                               PackageBaseArtifactInstallReasonPlanResult&>()
                               .failure()),
              const MixedPackageBaseInstallReasonUnsupported*>);
static_assert(
        !CanMutateSuccess<PackageBaseArtifactInstallReasonPlanResult>);
static_assert(!HasArtifactIndex<MixedPackageBaseInstallReasonArtifact>);
static_assert(!HasArtifactPath<MixedPackageBaseInstallReasonArtifact>);
static_assert(!HasWorkspacePath<MixedPackageBaseInstallReasonArtifact>);
static_assert(!HasArtifactIndex<MixedPackageBaseInstallReasonUnsupported>);
static_assert(!HasArtifactPath<MixedPackageBaseInstallReasonUnsupported>);
static_assert(!HasWorkspacePath<MixedPackageBaseInstallReasonUnsupported>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename ExpectedException, typename Callable>
void expect_exception(Callable callable, std::string_view context) {
    try {
        callable();
    } catch(const ExpectedException&) {
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                std::string(context) +
                " threw an unexpected exception category: " + error.what());
    }
    throw std::runtime_error(
            std::string(context) + " did not throw an exception");
}

SelectedPackageBaseArtifactInstallReasonPolicyInput selected(
        std::string package_name,
        DesiredInstallReason desired_reason,
        InstalledVersionState installed_version_state,
        std::optional<ExistingInstallReason> existing_reason,
        std::string full_version = "1.0-1") {
    return SelectedPackageBaseArtifactInstallReasonPolicyInput{
            ArtifactPackageIdentity{
                    std::move(package_name), std::move(full_version)},
            desired_reason,
            installed_version_state,
            existing_reason};
}

PackageBaseArtifactInstallReasonPolicyInput policy(
        std::vector<SelectedPackageBaseArtifactInstallReasonPolicyInput>
                selected_artifacts,
        bool needed = false) {
    return PackageBaseArtifactInstallReasonPolicyInput{
            "sample-base", std::move(selected_artifacts), needed};
}

const PackageBaseArtifactInstallReasonPlan& expect_success(
        const PackageBaseArtifactInstallReasonPlanResult& result,
        std::string_view context) {
    expect(
            result.is_success(),
            std::string(context) + " unexpectedly failed");
    expect(
            result.success() != nullptr,
            std::string(context) + " has no success payload");
    expect(
            result.failure() == nullptr,
            std::string(context) + " also has a failure payload");
    return *result.success();
}

const MixedPackageBaseInstallReasonUnsupported& expect_failure(
        const PackageBaseArtifactInstallReasonPlanResult& result,
        std::string_view context) {
    expect(
            !result.is_success(),
            std::string(context) + " unexpectedly succeeded");
    expect(
            result.success() == nullptr,
            std::string(context) + " exposes a success payload");
    expect(
            result.failure() != nullptr,
            std::string(context) + " has no failure payload");
    return *result.failure();
}

void expect_entry(
        const PlannedPackageBaseArtifactInstallReason& entry,
        std::string_view package_name,
        DesiredInstallReason desired_reason,
        InstalledVersionState version_state,
        std::optional<ExistingInstallReason> existing_reason,
        InstallReasonDirective directive,
        PackageBaseArtifactInstallExpectedOutcome expected_outcome) {
    expect(entry.identity.package_name == package_name, "Package name differs");
    expect(entry.identity.full_version == "1.0-1", "Full version differs");
    expect(entry.desired_reason == desired_reason, "Desired reason differs");
    expect(
            entry.installed_version_state == version_state,
            "Installed version state differs");
    expect(
            entry.existing_reason == existing_reason,
            "Existing reason differs");
    expect(entry.directive == directive, "Per-artifact directive differs");
    expect(
            entry.expected_outcome == expected_outcome,
            "Expected outcome differs");
}

void test_all_default_uses_default_even_with_mixed_final_reasons() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "explicit-child", DesiredInstallReason::Dependency,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Explicit),
                    selected(
                            "dependency-child",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
                    selected(
                            "new-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "all Default");
    expect(
            plan.transaction_directive == InstallReasonDirective::Default,
            "All-Default policy did not keep the default transaction");
    expect(plan.selected_artifacts.size() == 3, "All-Default count differs");
    for(const PlannedPackageBaseArtifactInstallReason& entry :
        plan.selected_artifacts) {
        expect(
                entry.directive == InstallReasonDirective::Default,
                "All-Default item has a non-default directive");
    }
}

void test_all_as_explicit_uses_as_explicit() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "root-one", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
                    selected(
                            "root-two", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "all AsExplicit");
    expect(
            plan.transaction_directive == InstallReasonDirective::AsExplicit,
            "All-AsExplicit transaction directive differs");
    for(const PlannedPackageBaseArtifactInstallReason& entry :
        plan.selected_artifacts) {
        expect(
                entry.directive == InstallReasonDirective::AsExplicit,
                "All-AsExplicit item directive differs");
    }
}

void test_all_as_dependency_uses_as_dependency() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "dependency-one",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt),
                    selected(
                            "dependency-two",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "all AsDependency");
    expect(
            plan.transaction_directive ==
                    InstallReasonDirective::AsDependency,
            "All-AsDependency transaction directive differs");
    for(const PlannedPackageBaseArtifactInstallReason& entry :
        plan.selected_artifacts) {
        expect(
                entry.directive == InstallReasonDirective::AsDependency,
                "All-AsDependency item directive differs");
    }
}

void test_default_and_as_explicit_reduce_when_all_final_explicit() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "new-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::NotInstalled, std::nullopt),
                    selected(
                            "promoted-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "Default + AsExplicit");
    expect(
            plan.transaction_directive == InstallReasonDirective::AsExplicit,
            "Mixed per-item explicit policy did not reduce to AsExplicit");
    expect(
            plan.selected_artifacts[0].directive ==
                    InstallReasonDirective::Default,
            "New root did not retain its per-item Default directive");
    expect(
            plan.selected_artifacts[1].directive ==
                    InstallReasonDirective::AsExplicit,
            "Promoted root directive differs");
}

void test_default_and_as_dependency_reduce_when_all_final_dependency() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "existing-dependency",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
                    selected(
                            "new-dependency",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "Default + AsDependency");
    expect(
            plan.transaction_directive ==
                    InstallReasonDirective::AsDependency,
            "Mixed per-item dependency policy did not reduce to AsDependency");
    expect(
            plan.selected_artifacts[0].directive ==
                    InstallReasonDirective::Default,
            "Existing dependency did not retain Default");
    expect(
            plan.selected_artifacts[1].directive ==
                    InstallReasonDirective::AsDependency,
            "New dependency directive differs");
}

void test_existing_explicit_is_not_downgraded() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "explicit-child", DesiredInstallReason::Dependency,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Explicit),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "existing Explicit dependency intent");
    expect_entry(
            plan.selected_artifacts.front(), "explicit-child",
            DesiredInstallReason::Dependency,
            InstalledVersionState::DifferentVersion,
            ExistingInstallReason::Explicit,
            InstallReasonDirective::Default,
            PackageBaseArtifactInstallExpectedOutcome::Installed);
    expect(
            plan.transaction_directive == InstallReasonDirective::Default,
            "Existing Explicit package was implicitly downgraded");
}

void test_existing_dependency_root_is_promoted() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "promoted-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
            }));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "existing Dependency root");
    expect(
            plan.selected_artifacts.front().directive ==
                    InstallReasonDirective::AsExplicit,
            "Existing Dependency root was not promoted");
    expect(
            plan.transaction_directive == InstallReasonDirective::AsExplicit,
            "Root promotion transaction directive differs");
}

void test_new_dependency_is_as_dependency_and_new_root_is_default() {
    PackageBaseArtifactInstallReasonPlanResult dependency_result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "new-dependency",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    const PackageBaseArtifactInstallReasonPlan& dependency_plan =
            expect_success(dependency_result, "new dependency");
    expect(
            dependency_plan.transaction_directive ==
                    InstallReasonDirective::AsDependency,
            "New dependency transaction directive differs");

    PackageBaseArtifactInstallReasonPlanResult root_result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "new-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    const PackageBaseArtifactInstallReasonPlan& root_plan =
            expect_success(root_result, "new root");
    expect(
            root_plan.transaction_directive ==
                    InstallReasonDirective::Default,
            "New root did not keep pacman default reason semantics");
}

void test_new_explicit_and_dependency_is_typed_mixed_failure() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "new-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::NotInstalled, std::nullopt,
                            "2.0-1"),
                    selected(
                            "new-child", DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt,
                            "3.0-2"),
            }));
    const MixedPackageBaseInstallReasonUnsupported& failure =
            expect_failure(result, "new Explicit + new Dependency");
    expect(failure.package_base == "sample-base", "Failure PackageBase differs");
    expect(failure.selected_artifacts.size() == 2, "Failure item count differs");

    const MixedPackageBaseInstallReasonArtifact& root =
            failure.selected_artifacts[0];
    expect(root.identity.package_name == "new-root", "Failure root name differs");
    expect(root.identity.full_version == "2.0-1", "Failure root version differs");
    expect(
            root.desired_reason == DesiredInstallReason::Explicit,
            "Failure root desired reason differs");
    expect(
            root.installed_version_state == InstalledVersionState::NotInstalled,
            "Failure root installed state differs");
    expect(!root.existing_reason.has_value(), "Failure root has existing reason");
    expect(
            root.directive == InstallReasonDirective::Default,
            "Failure root directive differs");

    const MixedPackageBaseInstallReasonArtifact& child =
            failure.selected_artifacts[1];
    expect(child.identity.package_name == "new-child", "Failure child name differs");
    expect(child.identity.full_version == "3.0-2", "Failure child version differs");
    expect(
            child.desired_reason == DesiredInstallReason::Dependency,
            "Failure child desired reason differs");
    expect(
            child.installed_version_state ==
                    InstalledVersionState::NotInstalled,
            "Failure child installed state differs");
    expect(!child.existing_reason.has_value(), "Failure child has existing reason");
    expect(
            child.directive == InstallReasonDirective::AsDependency,
            "Failure child directive differs");
}

void test_mixed_final_reason_with_nondefault_is_rejected() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy({
                    selected(
                            "existing-explicit",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Explicit),
                    selected(
                            "new-dependency",
                            DesiredInstallReason::Dependency,
                            InstalledVersionState::NotInstalled, std::nullopt),
            }));
    static_cast<void>(expect_failure(result, "mixed final reason set"));
}

void test_input_permutation_does_not_change_reducer_judgment() {
    std::vector<SelectedPackageBaseArtifactInstallReasonPolicyInput>
            reducible = {
                    selected(
                            "a-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::NotInstalled, std::nullopt),
                    selected(
                            "b-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Dependency),
                    selected(
                            "c-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::DifferentVersion,
                            ExistingInstallReason::Explicit),
            };
    std::sort(
            reducible.begin(), reducible.end(),
            [](const auto& left, const auto& right) {
                return left.identity.package_name < right.identity.package_name;
            });
    do {
        PackageBaseArtifactInstallReasonPlanResult result =
                resolve_package_base_artifact_install_reason_plan(
                        policy(reducible));
        const PackageBaseArtifactInstallReasonPlan& plan =
                expect_success(result, "reducible permutation");
        expect(
                plan.transaction_directive ==
                        InstallReasonDirective::AsExplicit,
                "Reducible permutation changed the transaction directive");
        for(std::size_t index = 0; index < reducible.size(); ++index) {
            expect(
                    plan.selected_artifacts[index].identity.package_name ==
                            reducible[index].identity.package_name,
                    "Reducer changed selected ordering");
        }
    } while(std::next_permutation(
            reducible.begin(), reducible.end(),
            [](const auto& left, const auto& right) {
                return left.identity.package_name < right.identity.package_name;
            }));

    std::vector<SelectedPackageBaseArtifactInstallReasonPolicyInput> mixed = {
            selected(
                    "a-root", DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled, std::nullopt),
            selected(
                    "b-child", DesiredInstallReason::Dependency,
                    InstalledVersionState::NotInstalled, std::nullopt),
    };
    do {
        PackageBaseArtifactInstallReasonPlanResult result =
                resolve_package_base_artifact_install_reason_plan(policy(mixed));
        static_cast<void>(expect_failure(result, "mixed permutation"));
    } while(std::next_permutation(
            mixed.begin(), mixed.end(),
            [](const auto& left, const auto& right) {
                return left.identity.package_name < right.identity.package_name;
            }));
}

void test_needed_all_install() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "new-root", DesiredInstallReason::Explicit,
                                    InstalledVersionState::NotInstalled,
                                    std::nullopt),
                            selected(
                                    "upgraded-root",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::DifferentVersion,
                                    ExistingInstallReason::Explicit),
                    },
                    true));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "needed all install");
    expect(plan.needed, "Needed policy was not retained");
    for(const PlannedPackageBaseArtifactInstallReason& entry :
        plan.selected_artifacts) {
        expect(
                entry.expected_outcome ==
                        PackageBaseArtifactInstallExpectedOutcome::Installed,
                "Non-same-version item was marked skipped");
    }
}

void test_needed_all_same_version_skip() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "explicit-package",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Explicit),
                            selected(
                                    "dependency-package",
                                    DesiredInstallReason::Dependency,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Dependency),
                    },
                    true));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "needed all skip");
    expect(
            plan.transaction_directive == InstallReasonDirective::Default,
            "All-skip transaction directive differs");
    for(const PlannedPackageBaseArtifactInstallReason& entry :
        plan.selected_artifacts) {
        expect(
                entry.expected_outcome ==
                        PackageBaseArtifactInstallExpectedOutcome::
                                SkippedAsNeeded,
                "Same-version Default item was not marked skipped");
    }
}

void test_needed_installed_and_skipped_outcomes_keep_selected_order() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "first-installed",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::DifferentVersion,
                                    ExistingInstallReason::Explicit),
                            selected(
                                    "middle-skipped",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Explicit),
                            selected(
                                    "last-installed",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::NotInstalled,
                                    std::nullopt),
                    },
                    true));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "mixed needed outcomes");
    expect_entry(
            plan.selected_artifacts[0], "first-installed",
            DesiredInstallReason::Explicit,
            InstalledVersionState::DifferentVersion,
            ExistingInstallReason::Explicit,
            InstallReasonDirective::Default,
            PackageBaseArtifactInstallExpectedOutcome::Installed);
    expect_entry(
            plan.selected_artifacts[1], "middle-skipped",
            DesiredInstallReason::Explicit,
            InstalledVersionState::SameVersion,
            ExistingInstallReason::Explicit,
            InstallReasonDirective::Default,
            PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded);
    expect_entry(
            plan.selected_artifacts[2], "last-installed",
            DesiredInstallReason::Explicit,
            InstalledVersionState::NotInstalled, std::nullopt,
            InstallReasonDirective::Default,
            PackageBaseArtifactInstallExpectedOutcome::Installed);
}

void test_needed_same_version_reason_change_is_rejected() {
    PackageBaseArtifactInstallReasonPolicyInput input = policy(
            {
                    selected(
                            "promoted-root", DesiredInstallReason::Explicit,
                            InstalledVersionState::SameVersion,
                            ExistingInstallReason::Dependency),
            },
            true);
    expect_exception<std::runtime_error>(
            [&input]() {
                static_cast<void>(
                        resolve_package_base_artifact_install_reason_plan(input));
            },
            "needed same-version reason change");
}

void test_global_as_explicit_allows_safe_default_skip() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "existing-explicit",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Explicit),
                            selected(
                                    "promoted-root",
                                    DesiredInstallReason::Explicit,
                                    InstalledVersionState::DifferentVersion,
                                    ExistingInstallReason::Dependency),
                    },
                    true));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "global AsExplicit safe skip");
    expect(
            plan.transaction_directive == InstallReasonDirective::AsExplicit,
            "Safe-skip transaction did not use AsExplicit");
    expect(
            plan.selected_artifacts[0].directive ==
                    InstallReasonDirective::Default,
            "Safe-skip item did not keep Default per-item policy");
    expect(
            plan.selected_artifacts[0].expected_outcome ==
                    PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded,
            "Existing Explicit Default item was not safely skipped");
    expect(
            plan.selected_artifacts[1].expected_outcome ==
                    PackageBaseArtifactInstallExpectedOutcome::Installed,
            "Promoted different-version item was not marked installed");
}

void test_global_as_dependency_allows_safe_default_skip() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "existing-dependency",
                                    DesiredInstallReason::Dependency,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Dependency),
                            selected(
                                    "new-dependency",
                                    DesiredInstallReason::Dependency,
                                    InstalledVersionState::NotInstalled,
                                    std::nullopt),
                    },
                    true));
    const PackageBaseArtifactInstallReasonPlan& plan =
            expect_success(result, "global AsDependency safe skip");
    expect(
            plan.transaction_directive ==
                    InstallReasonDirective::AsDependency,
            "Safe-skip transaction did not use AsDependency");
    expect(
            plan.selected_artifacts[0].expected_outcome ==
                    PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded,
            "Existing Dependency Default item was not safely skipped");
    expect(
            plan.selected_artifacts[1].expected_outcome ==
                    PackageBaseArtifactInstallExpectedOutcome::Installed,
            "New Dependency item was not marked installed");
}

void test_needed_rejects_global_reason_that_cannot_preserve_default_skip() {
    PackageBaseArtifactInstallReasonPlanResult result =
            resolve_package_base_artifact_install_reason_plan(policy(
                    {
                            selected(
                                    "existing-explicit",
                                    DesiredInstallReason::Dependency,
                                    InstalledVersionState::SameVersion,
                                    ExistingInstallReason::Explicit),
                            selected(
                                    "new-dependency",
                                    DesiredInstallReason::Dependency,
                                    InstalledVersionState::NotInstalled,
                                    std::nullopt),
                    },
                    true));
    const MixedPackageBaseInstallReasonUnsupported& failure =
            expect_failure(result, "unsafe global reason for Default skip");
    expect(
            failure.selected_artifacts[0].directive ==
                    InstallReasonDirective::Default,
            "Unsafe-skip diagnostic lost the Default directive");
    expect(
            failure.selected_artifacts[1].directive ==
                    InstallReasonDirective::AsDependency,
            "Unsafe-skip diagnostic lost the AsDependency directive");
}

void test_unknown_and_incoherent_inputs_fail_closed() {
    auto expect_logic_failure = [](PackageBaseArtifactInstallReasonPolicyInput input,
                                   std::string_view context) {
        expect_exception<std::logic_error>(
                [&input]() {
                    static_cast<void>(
                            resolve_package_base_artifact_install_reason_plan(
                                    input));
                },
                context);
    };

    expect_logic_failure(policy({}), "empty selected set");

    PackageBaseArtifactInstallReasonPolicyInput invalid_base = policy({
            selected(
                    "package", DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled, std::nullopt),
    });
    invalid_base.package_base = "invalid base";
    expect_logic_failure(std::move(invalid_base), "invalid PackageBase");

    expect_logic_failure(
            policy({selected(
                    "invalid name", DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled, std::nullopt)}),
            "invalid package name");
    expect_logic_failure(
            policy({selected(
                    "package", DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled, std::nullopt, "")}),
            "empty full version");
    expect_logic_failure(
            policy({selected(
                    "package", static_cast<DesiredInstallReason>(99),
                    InstalledVersionState::NotInstalled, std::nullopt)}),
            "unknown desired reason");
    expect_logic_failure(
            policy({selected(
                    "package", DesiredInstallReason::Explicit,
                    static_cast<InstalledVersionState>(99), std::nullopt)}),
            "unknown installed state");
    expect_logic_failure(
            policy({selected(
                    "package", DesiredInstallReason::Explicit,
                    InstalledVersionState::DifferentVersion,
                    static_cast<ExistingInstallReason>(99))}),
            "unknown existing reason");
    expect_logic_failure(
            policy({selected(
                    "package", DesiredInstallReason::Explicit,
                    InstalledVersionState::NotInstalled,
                    ExistingInstallReason::Explicit)}),
            "not-installed with existing reason");
    expect_logic_failure(
            policy({selected(
                    "package", DesiredInstallReason::Explicit,
                    InstalledVersionState::DifferentVersion, std::nullopt)}),
            "installed without existing reason");
}

} // namespace

int main() {
    try {
        test_all_default_uses_default_even_with_mixed_final_reasons();
        test_all_as_explicit_uses_as_explicit();
        test_all_as_dependency_uses_as_dependency();
        test_default_and_as_explicit_reduce_when_all_final_explicit();
        test_default_and_as_dependency_reduce_when_all_final_dependency();
        test_existing_explicit_is_not_downgraded();
        test_existing_dependency_root_is_promoted();
        test_new_dependency_is_as_dependency_and_new_root_is_default();
        test_new_explicit_and_dependency_is_typed_mixed_failure();
        test_mixed_final_reason_with_nondefault_is_rejected();
        test_input_permutation_does_not_change_reducer_judgment();
        test_needed_all_install();
        test_needed_all_same_version_skip();
        test_needed_installed_and_skipped_outcomes_keep_selected_order();
        test_needed_same_version_reason_change_is_rejected();
        test_global_as_explicit_allows_safe_default_skip();
        test_global_as_dependency_allows_safe_default_skip();
        test_needed_rejects_global_reason_that_cannot_preserve_default_skip();
        test_unknown_and_incoherent_inputs_fail_closed();
    } catch(const std::exception& error) {
        std::cerr << "package base artifact install plan test failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "package base artifact install plan tests passed\n";
    return 0;
}
