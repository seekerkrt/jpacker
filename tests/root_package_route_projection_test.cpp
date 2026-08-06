#include "root_package_route_projection.hpp"

#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(!std::is_default_constructible_v<RootPackageSelection>);
static_assert(!std::is_aggregate_v<RootPackageSelection>);
static_assert(
        !std::is_constructible_v<
                RootPackageSelection,
                std::vector<SelectedRootPackageTarget>>);
static_assert(
        !std::is_default_constructible_v<
                RepositoryRootPackageRouteTarget>);
static_assert(!std::is_aggregate_v<RepositoryRootPackageRouteTarget>);
static_assert(
        !std::is_constructible_v<
                RepositoryRootPackageRouteTarget, std::size_t,
                SelectedRootPackageTarget>);
static_assert(!std::is_default_constructible_v<AurRootPackageRouteTarget>);
static_assert(!std::is_aggregate_v<AurRootPackageRouteTarget>);
static_assert(
        !std::is_constructible_v<
                AurRootPackageRouteTarget, std::size_t,
                SelectedRootPackageTarget>);
static_assert(!std::is_default_constructible_v<RootPackageRoutingProjection>);
static_assert(!std::is_aggregate_v<RootPackageRoutingProjection>);
static_assert(
        !std::is_default_constructible_v<
                RootPackageRoutingProjectionResult>);
static_assert(!std::is_aggregate_v<RootPackageRoutingProjectionResult>);
static_assert(
        !std::is_constructible_v<
                RootPackageRoutingProjectionResult,
                RootPackageRoutingProjection>);
static_assert(
        !std::is_constructible_v<
                RootPackageRoutingProjectionResult,
                InvalidRootPackageRoutingProjection>);
static_assert(
        std::is_copy_constructible_v<
                RootPackageRoutingProjectionResult>);
static_assert(
        std::is_move_constructible_v<
                RootPackageRoutingProjectionResult>);
static_assert(
        !std::is_copy_assignable_v<
                RootPackageRoutingProjectionResult>);
static_assert(
        !std::is_move_assignable_v<
                RootPackageRoutingProjectionResult>);
static_assert(std::same_as<
              decltype(std::declval<
                               const RootPackageRoutingProjectionResult&>()
                               .projection()),
              const RootPackageRoutingProjection*>);
static_assert(std::same_as<
              decltype(std::declval<
                               const RootPackageRoutingProjectionResult&>()
                               .failure()),
              const InvalidRootPackageRoutingProjection*>);
static_assert(std::same_as<
              decltype(project_root_package_routing(
                      std::declval<const RootPackageSelection&>())),
              RootPackageRoutingProjectionResult>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

RootPackageSearchCandidate repository_entry(
        std::string repository_name, std::string package_name) {
    RootPackageCandidateValidationResult result =
            make_repository_root_package_candidate(
                    std::move(repository_name), std::move(package_name));
    expect(result.is_valid(), "repository test candidate was invalid");
    expect(
            result.candidate() != nullptr,
            "repository test candidate is unavailable");
    expect(
            result.failure() == nullptr,
            "repository test candidate also exposed a failure");
    return RootPackageSearchCandidate{*result.candidate(), {}};
}

RootPackageSearchCandidate aur_entry(
        std::string package_name, std::string package_base) {
    RootPackageCandidateValidationResult result =
            make_aur_root_package_candidate(
                    std::move(package_name), std::move(package_base));
    expect(result.is_valid(), "AUR test candidate was invalid");
    expect(
            result.candidate() != nullptr,
            "AUR test candidate is unavailable");
    expect(
            result.failure() == nullptr,
            "AUR test candidate also exposed a failure");
    return RootPackageSearchCandidate{*result.candidate(), {}};
}

RootPackageSelection require_selection(
        RootPackageSelectionExpressionResult result,
        std::string_view context) {
    const std::string context_text(context);
    auto* selection = std::get_if<RootPackageSelection>(&result);
    expect(
            selection != nullptr,
            context_text + ": parser did not return a valid selection");
    return std::move(*selection);
}

const RootPackageRoutingProjection& require_projection(
        const RootPackageRoutingProjectionResult& result,
        std::string_view context) {
    const std::string context_text(context);
    expect(result.is_valid(), context_text + ": projection was invalid");
    expect(
            result.projection() != nullptr,
            context_text + ": projection payload is unavailable");
    expect(
            result.failure() == nullptr,
            context_text + ": valid result also exposed a failure");
    return *result.projection();
}

const InvalidRootPackageRoutingProjection& require_failure(
        const RootPackageRoutingProjectionResult& result,
        std::string_view context) {
    const std::string context_text(context);
    expect(!result.is_valid(), context_text + ": projection was accepted");
    expect(
            result.projection() == nullptr,
            context_text + ": invalid result exposed a partial projection");
    expect(
            result.failure() != nullptr,
            context_text + ": failure payload is unavailable");
    return *result.failure();
}

void test_repository_route_preserves_exact_target() {
    const RootPackageSearchSnapshot snapshot{{
            repository_entry("core", "exact-package"),
    }};
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection("1", snapshot),
            "repository selection");

    const RootPackageRoutingProjectionResult result =
            project_root_package_routing(selection);
    const RootPackageRoutingProjection& projection =
            require_projection(result, "repository route");
    expect(
            projection.repository_targets().size() == 1,
            "repository route count differs");
    expect(
            projection.aur_targets().empty(),
            "repository selection leaked into the AUR route");

    const RepositoryRootPackageRouteTarget& target =
            projection.repository_targets().front();
    expect(target.selection_index() == 0, "repository index differs");
    expect(
            target.identity() == RepositoryRootPackageIdentity{
                                         "core", "exact-package"},
            "repository identity was flattened");
    expect(
            target.exact_package_target() == "core/exact-package",
            "exact repository/package target differs");
    expect(
            target.selected_target() == selection.targets().front(),
            "repository route changed the selected root target");
}

void test_aur_route_preserves_package_base() {
    const RootPackageSearchSnapshot snapshot{{
            aur_entry("suite-child", "suite-base"),
    }};
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection("1", snapshot), "AUR selection");

    const RootPackageRoutingProjectionResult result =
            project_root_package_routing(selection);
    const RootPackageRoutingProjection& projection =
            require_projection(result, "AUR route");
    expect(
            projection.repository_targets().empty(),
            "AUR selection leaked into the repository route");
    expect(projection.aur_targets().size() == 1, "AUR route count differs");

    const AurRootPackageRouteTarget& target =
            projection.aur_targets().front();
    expect(target.selection_index() == 0, "AUR index differs");
    expect(
            target.identity() ==
                    AurRootPackageIdentity{"suite-child", "suite-base"},
            "AUR PackageBase identity was flattened");
    expect(
            target.selected_target() == selection.targets().front(),
            "AUR route changed the selected root target");
}

void test_mixed_partition_preserves_relative_order_and_selection_index() {
    const RootPackageSearchSnapshot snapshot{{
            repository_entry("aur", "repository-first"),
            aur_entry("aur-first", "aur-first-base"),
            repository_entry("extra", "repository-second"),
            aur_entry("aur-second", "aur-second-base"),
    }};
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection("4 1-3", snapshot),
            "mixed selection");

    const RootPackageRoutingProjectionResult result =
            project_root_package_routing(selection);
    const RootPackageRoutingProjection& projection =
            require_projection(result, "mixed route");
    expect(
            projection.repository_targets().size() == 2,
            "mixed repository route count differs");
    expect(
            projection.aur_targets().size() == 2,
            "mixed AUR route count differs");

    const auto& repositories = projection.repository_targets();
    expect(
            repositories[0].selection_index() == 0 &&
                    repositories[0].identity() ==
                            RepositoryRootPackageIdentity{
                                    "aur", "repository-first"} &&
                    repositories[0].exact_package_target() ==
                            "aur/repository-first",
            "repository named aur lost its repository route identity");
    expect(
            repositories[1].selection_index() == 2 &&
                    repositories[1].identity() ==
                            RepositoryRootPackageIdentity{
                                    "extra", "repository-second"},
            "repository partition order or index differs");

    const auto& aur_targets = projection.aur_targets();
    expect(
            aur_targets[0].selection_index() == 1 &&
                    aur_targets[0].identity() ==
                            AurRootPackageIdentity{
                                    "aur-first", "aur-first-base"},
            "first AUR partition target differs");
    expect(
            aur_targets[1].selection_index() == 3 &&
                    aur_targets[1].identity() ==
                            AurRootPackageIdentity{
                                    "aur-second", "aur-second-base"},
            "second AUR partition target differs");
}

void test_same_package_base_split_siblings_remain_distinct() {
    const RootPackageSearchSnapshot snapshot{{
            aur_entry("suite-runtime", "shared-suite"),
            aur_entry("suite-tools", "shared-suite"),
    }};
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection("1-2", snapshot),
            "split sibling selection");

    const RootPackageRoutingProjectionResult result =
            project_root_package_routing(selection);
    const RootPackageRoutingProjection& projection =
            require_projection(result, "split sibling route");
    expect(
            projection.repository_targets().empty(),
            "split AUR siblings leaked into the repository route");
    expect(
            projection.aur_targets().size() == 2,
            "same-PackageBase siblings were collapsed");
    expect(
            projection.aur_targets()[0].identity() ==
                            AurRootPackageIdentity{
                                    "suite-runtime", "shared-suite"} &&
                    projection.aur_targets()[1].identity() ==
                            AurRootPackageIdentity{
                                    "suite-tools", "shared-suite"},
            "split sibling package identities differ");
    expect(
            projection.aur_targets()[0].selection_index() == 0 &&
                    projection.aur_targets()[1].selection_index() == 1,
            "split sibling selection indices differ");
}

void test_unsafe_repository_names_fail_atomically_with_typed_detail() {
    const RootPackageSearchSnapshot snapshot{{
            repository_entry(".", "dot-package"),
            repository_entry("core", "safe-package"),
            repository_entry("..", "parent-package"),
            aur_entry("safe-aur-package", "safe-aur-base"),
            repository_entry("nested/repository", "nested-package"),
    }};
    const RootPackageSelection selection = require_selection(
            parse_root_package_selection("1-5", snapshot),
            "unsafe repository selection");

    const RootPackageRoutingProjectionResult result =
            project_root_package_routing(selection);
    const InvalidRootPackageRoutingProjection& failure = require_failure(
            result, "unsafe repository route");
    const std::vector<UnrepresentableRepositoryRootPackageRouteTarget>
            expected{
                    {0, {".", "dot-package"}},
                    {2, {"..", "parent-package"}},
                    {4, {"nested/repository", "nested-package"}},
            };
    expect(
            failure.unrepresentable_repository_targets == expected,
            "unsafe repository failure detail or order differs");
}

void test_projection_owns_selected_targets_after_inputs_expire() {
    const RootPackageRoutingProjectionResult result = [] {
        const RootPackageSearchSnapshot snapshot{{
                repository_entry("testing", "owned-repository"),
                aur_entry("owned-child", "owned-base"),
        }};
        const RootPackageSelection selection = require_selection(
                parse_root_package_selection("1-2", snapshot),
                "owned projection selection");
        return project_root_package_routing(selection);
    }();

    const RootPackageRoutingProjection& projection =
            require_projection(result, "owned projection");
    expect(
            projection.repository_targets().front().identity() ==
                            RepositoryRootPackageIdentity{
                                    "testing", "owned-repository"} &&
                    projection.repository_targets()
                                    .front()
                                    .exact_package_target() ==
                            "testing/owned-repository",
            "repository route borrowed expired selection storage");
    expect(
            projection.aur_targets().front().identity() ==
                    AurRootPackageIdentity{"owned-child", "owned-base"},
            "AUR route borrowed expired selection storage");
}

} // namespace

int main() {
    try {
        test_repository_route_preserves_exact_target();
        test_aur_route_preserves_package_base();
        test_mixed_partition_preserves_relative_order_and_selection_index();
        test_same_package_base_split_siblings_remain_distinct();
        test_unsafe_repository_names_fail_atomically_with_typed_detail();
        test_projection_owns_selected_targets_after_inputs_expire();
        std::cout << "root package route projection tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
