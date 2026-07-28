#include "artifact_identity_selection.hpp"
#include "dependency_plan.hpp"

#include <concepts>
#include <cstddef>
#include <exception>
#include <initializer_list>
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
concept HasArtifactPath = requires(const T& value) {
    value.artifact_path();
};

template <typename T>
concept HasWorkspacePath = requires(const T& value) {
    value.workspace_path();
};

template <typename T>
concept CanCleanupWorkspace = requires(T& value) {
    value.cleanup_workspace();
};

template <typename T>
concept CanRetainWorkspace = requires(T& value) {
    value.retain_workspace_for_diagnostics();
};

template <typename T>
concept HasDirectSelectedArtifacts = requires(T& value) {
    value.selected_artifacts;
};

template <typename T>
concept CanClearFailureDiagnostics = requires(T& value) {
    value.failure()->missing_required_artifacts.clear();
};

template <typename T>
concept CanMutateSelectedIdentity = requires(T& value) {
    value.success()->selected_artifacts[0].identity.package_name.clear();
};

template <typename T>
concept CanMutateSelectedIndex = requires(T& value) {
    value.success()->selected_artifacts[0].artifact_index = 0;
};

template <typename T>
concept HasArtifactIndex = requires(const T& value) {
    value.artifact_index;
};

template <typename T>
concept HasProducedArtifactIndex = requires(const T& value) {
    value.produced_artifact_index;
};

template <typename T>
concept HasDuplicateArtifactIndices = requires(const T& value) {
    value.first_artifact_index;
    value.duplicate_artifact_index;
};

static_assert(
        !std::is_default_constructible_v<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !std::is_aggregate_v<PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactIdentitySelectionSuccess>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactSelectionFailure>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactIdentitySelectionFailure>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactIdentitySelectionFailure&>);
static_assert(
        !std::is_constructible_v<
                PackageBaseArtifactIdentitySelectionResult,
                const PackageBaseArtifactIdentitySelectionFailure&>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactIdentitySelectionSuccess&>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 const PackageBaseArtifactIdentitySelectionSuccess&>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 PackageBaseArtifactSelectionFailure&>);
static_assert(
        !std::is_constructible_v<PackageBaseArtifactIdentitySelectionResult,
                                 const PackageBaseArtifactSelectionFailure&>);
static_assert(
        std::is_copy_constructible_v<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        std::is_move_constructible_v<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !std::is_copy_assignable_v<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !std::is_move_assignable_v<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(std::same_as<
              decltype(std::declval<
                               PackageBaseArtifactIdentitySelectionResult&>()
                               .success()),
              const PackageBaseArtifactIdentitySelectionSuccess*>);
static_assert(std::same_as<
              decltype(std::declval<
                               PackageBaseArtifactIdentitySelectionResult&>()
                               .failure()),
              const PackageBaseArtifactIdentitySelectionFailure*>);
static_assert(std::same_as<
              decltype(std::declval<
                               const PackageBaseArtifactIdentitySelectionResult&>()
                               .success()),
              const PackageBaseArtifactIdentitySelectionSuccess*>);
static_assert(std::same_as<
              decltype(std::declval<
                               const PackageBaseArtifactIdentitySelectionResult&>()
                               .failure()),
              const PackageBaseArtifactIdentitySelectionFailure*>);
static_assert(
        !HasDirectSelectedArtifacts<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !HasDirectSelectedArtifacts<PackageBaseArtifactSelectionFailure>);
static_assert(
        !HasDirectSelectedArtifacts<
                PackageBaseArtifactIdentitySelectionFailure>);
static_assert(
        !CanClearFailureDiagnostics<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !CanMutateSelectedIdentity<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(
        !CanMutateSelectedIndex<
                PackageBaseArtifactIdentitySelectionResult>);
static_assert(!HasArtifactIndex<DiagnosticPackageArtifactIdentityMatch>);
static_assert(
        !HasProducedArtifactIndex<DiagnosticPackageArtifactIdentityMatch>);
static_assert(
        !HasDuplicateArtifactIndices<
                DuplicateProducedArtifactIdentityDiagnostic>);
static_assert(!HasArtifactPath<CorrelatedSelectedPackageArtifact>);
static_assert(!HasArtifactPath<CorrelatedUnselectedPackageArtifact>);
static_assert(!HasArtifactPath<PackageBaseArtifactIdentitySelectionResult>);
static_assert(!HasWorkspacePath<CorrelatedSelectedPackageArtifact>);
static_assert(!HasWorkspacePath<CorrelatedUnselectedPackageArtifact>);
static_assert(!HasWorkspacePath<PackageBaseArtifactIdentitySelectionResult>);
static_assert(!CanCleanupWorkspace<CorrelatedSelectedPackageArtifact>);
static_assert(!CanCleanupWorkspace<CorrelatedUnselectedPackageArtifact>);
static_assert(!CanCleanupWorkspace<PackageBaseArtifactIdentitySelectionResult>);
static_assert(!CanRetainWorkspace<CorrelatedSelectedPackageArtifact>);
static_assert(!CanRetainWorkspace<CorrelatedUnselectedPackageArtifact>);
static_assert(!CanRetainWorkspace<PackageBaseArtifactIdentitySelectionResult>);

struct IdentityInput {
    std::string package_name;
    std::string full_version;
};

struct ExpectedSelectedArtifact {
    std::size_t          artifact_index;
    std::string          package_name;
    std::string          full_version;
    DesiredInstallReason desired_reason;
};

struct ExpectedUnselectedArtifact {
    std::size_t artifact_index;
    std::string package_name;
    std::string full_version;
};

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ArtifactPackageIdentitySet identity_set(
        std::initializer_list<IdentityInput> inputs) {
    std::vector<ArtifactPackageIdentity> identities;
    identities.reserve(inputs.size());
    for(const IdentityInput& input : inputs) {
        identities.push_back(
                ArtifactPackageIdentity{input.package_name, input.full_version});
    }
    return make_artifact_package_identity_set_for_test(std::move(identities));
}

RequiredPackageArtifactTarget required_target(
        const std::string& package_base, const std::string& package_name,
        DesiredInstallReason desired_reason) {
    return RequiredPackageArtifactTarget{
            package_base, package_name, desired_reason};
}

const PackageBaseArtifactIdentitySelectionSuccess& expect_success(
        const PackageBaseArtifactIdentitySelectionResult& result,
        std::string_view context) {
    expect(
            result.is_success(),
            std::string(context) + " did not return success");
    expect(
            result.success() != nullptr,
            std::string(context) + " has no success payload");
    expect(
            result.failure() == nullptr,
            std::string(context) + " also has a failure payload");
    return *result.success();
}

const PackageBaseArtifactIdentitySelectionFailure& expect_failure(
        const PackageBaseArtifactIdentitySelectionResult& result,
        std::string_view context) {
    expect(
            !result.is_success(),
            std::string(context) + " unexpectedly returned success");
    expect(
            result.success() == nullptr,
            std::string(context) + " exposes a success payload");
    expect(
            result.failure() != nullptr,
            std::string(context) + " has no failure payload");
    return *result.failure();
}

void expect_selected_artifacts(
        const std::vector<CorrelatedSelectedPackageArtifact>& actual,
        const std::vector<ExpectedSelectedArtifact>& expected) {
    expect(actual.size() == expected.size(), "Selected artifact count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect(
                actual[index].artifact_index == expected[index].artifact_index,
                "Selected stable index differs at index " +
                        std::to_string(index));
        expect(
                actual[index].identity.package_name ==
                        expected[index].package_name,
                "Selected package name differs at index " +
                        std::to_string(index));
        expect(
                actual[index].identity.full_version ==
                        expected[index].full_version,
                "Selected full version differs at index " +
                        std::to_string(index));
        expect(
                actual[index].desired_reason ==
                        expected[index].desired_reason,
                "Selected install reason differs at index " +
                        std::to_string(index));
    }
}

void expect_unselected_artifacts(
        const std::vector<CorrelatedUnselectedPackageArtifact>& actual,
        const std::vector<ExpectedUnselectedArtifact>& expected) {
    expect(
            actual.size() == expected.size(),
            "Unselected artifact count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect(
                actual[index].artifact_index == expected[index].artifact_index,
                "Unselected stable index differs at index " +
                        std::to_string(index));
        expect(
                actual[index].identity.package_name ==
                        expected[index].package_name,
                "Unselected package name differs at index " +
                        std::to_string(index));
        expect(
                actual[index].identity.full_version ==
                        expected[index].full_version,
                "Unselected full version differs at index " +
                        std::to_string(index));
    }
}

bool has_identity_inconsistency(
        const PackageBaseArtifactIdentitySelectionFailure& failure,
        ArtifactSelectionIdentityInput input,
        std::optional<std::size_t> required_target_index,
        std::string_view identity) {
    for(const ArtifactIdentitySelectionIdentityInconsistency& inconsistency :
        failure.identity_inconsistencies) {
        if(inconsistency.input == input &&
           inconsistency.required_target_index == required_target_index &&
           inconsistency.identity == identity) {
            return true;
        }
    }
    return false;
}

void test_ordinary_single_output() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"sample-package", "2:1.4.0-3"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "sample-package",
                    {required_target(
                            "sample-package", "sample-package",
                            DesiredInstallReason::Explicit)},
                    identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Ordinary single-output correlation");
    expect(
            success.package_base == "sample-package",
            "Ordinary result PackageBase differs");
    expect_selected_artifacts(
            success.selected_artifacts,
            {{0, "sample-package", "2:1.4.0-3",
              DesiredInstallReason::Explicit}});
    expect_unselected_artifacts(success.unselected_artifacts, {});
}

void test_requested_split_child() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-cli", "1.0-2"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {required_target(
                            "shared-suite", "shared-suite-cli",
                            DesiredInstallReason::Explicit)},
                    identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Requested split child correlation");
    expect_selected_artifacts(
            success.selected_artifacts,
            {{0, "shared-suite-cli", "1.0-2",
              DesiredInstallReason::Explicit}});
}

void test_multiple_selected_children() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite-runtime", "3.2-1"},
            {"shared-suite-tools", "3.2-1"},
    });
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "shared-suite-tools",
                                    DesiredInstallReason::Dependency),
                            required_target(
                                    "shared-suite", "shared-suite-runtime",
                                    DesiredInstallReason::Dependency),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Multiple child correlation");
    expect_selected_artifacts(
            success.selected_artifacts,
            {
                    {1, "shared-suite-tools", "3.2-1",
                     DesiredInstallReason::Dependency},
                    {0, "shared-suite-runtime", "3.2-1",
                     DesiredInstallReason::Dependency},
            });
    expect_unselected_artifacts(success.unselected_artifacts, {});
}

void test_root_and_dependency_children() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite-library", "4.0-7"},
            {"shared-suite-app", "4.0-7"},
    });
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Explicit),
                            required_target(
                                    "shared-suite", "shared-suite-library",
                                    DesiredInstallReason::Dependency),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Root/dependency child correlation");
    expect_selected_artifacts(
            success.selected_artifacts,
            {
                    {1, "shared-suite-app", "4.0-7",
                     DesiredInstallReason::Explicit},
                    {0, "shared-suite-library", "4.0-7",
                     DesiredInstallReason::Dependency},
            });
}

void test_stable_selected_and_unselected_order() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"sibling", "1.0-1"},
            {"child-a", "5:2.0-4"},
            {"child-b", "2.0-4"},
            {"child-a-debug", "5:2.0-4"},
    });
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "child-b",
                                    DesiredInstallReason::Dependency),
                            required_target(
                                    "shared-suite", "child-a",
                                    DesiredInstallReason::Explicit),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Stable-order correlation");
    expect_selected_artifacts(
            success.selected_artifacts,
            {
                    {2, "child-b", "2.0-4",
                     DesiredInstallReason::Dependency},
                    {1, "child-a", "5:2.0-4",
                     DesiredInstallReason::Explicit},
            });
    expect_unselected_artifacts(
            success.unselected_artifacts,
            {
                    {0, "sibling", "1.0-1"},
                    {3, "child-a-debug", "5:2.0-4"},
            });
}

void test_empty_required_targets() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite", "1.0-1"},
            {"shared-suite-debug", "1.0-1"},
    });
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite", {}, identities);

    const PackageBaseArtifactIdentitySelectionSuccess& success =
            expect_success(result, "Empty required target correlation");
    expect_selected_artifacts(success.selected_artifacts, {});
    expect_unselected_artifacts(
            success.unselected_artifacts,
            {
                    {0, "shared-suite", "1.0-1"},
                    {1, "shared-suite-debug", "1.0-1"},
            });
}

void test_missing_required_child_and_partial_match_isolation() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite-app", "1.0-1"},
            {"shared-suite-docs", "1.0-1"},
    });
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Explicit),
                            required_target(
                                    "shared-suite", "shared-suite-library",
                                    DesiredInstallReason::Dependency),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure = expect_failure(
            result, "Missing required child correlation");
    expect(
            failure.package_base == "shared-suite",
            "Failure PackageBase differs");
    expect(
            failure.missing_required_artifacts.size() == 1,
            "Missing required artifact diagnostic count differs");
    expect(
            failure.missing_required_artifacts.front().required_target_index ==
                            1 &&
                    failure.missing_required_artifacts.front()
                                    .target.package_name ==
                            "shared-suite-library",
            "Missing required artifact attribution differs");
    expect(
            failure.diagnostic_partial_matches.size() == 1 &&
                    failure.diagnostic_partial_matches.front()
                                    .required_target_index == 0 &&
                    failure.diagnostic_partial_matches.front()
                                    .artifact.package_name ==
                            "shared-suite-app" &&
                    failure.diagnostic_partial_matches.front().desired_reason ==
                            DesiredInstallReason::Explicit,
            "Failure partial-match diagnostic differs");
    expect(
            result.success() == nullptr,
            "Failure partial match became an installable success payload");
}

void expect_duplicate_produced_failure(
        const ArtifactPackageIdentitySet& identities,
        const std::string& context) {
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {required_target(
                            "shared-suite", "shared-suite-app",
                            DesiredInstallReason::Explicit)},
                    identities);
    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, context);
    expect(
            failure.duplicate_produced_identities.size() == 1,
            context + " duplicate diagnostic count differs");
    const DuplicateProducedArtifactIdentityDiagnostic& duplicate =
            failure.duplicate_produced_identities.front();
    expect(
            duplicate.package_name == "shared-suite-app",
            context + " duplicate attribution differs");
}

void test_duplicate_produced_package_name() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite-app", "1.0-1"},
            {"shared-suite-app", "1.0-1"},
    });
    expect_duplicate_produced_failure(
            identities, "Duplicate produced package name");
}

void test_duplicate_produced_name_with_different_versions() {
    ArtifactPackageIdentitySet identities = identity_set({
            {"shared-suite-app", "1.0-1"},
            {"shared-suite-app", "2.0-1"},
    });
    expect_duplicate_produced_failure(
            identities, "Version-distinct duplicate package name");
}

void test_duplicate_required_target() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-app", "1.0-1"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Explicit),
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Explicit),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, "Duplicate required target correlation");
    expect(
            failure.duplicate_required_targets.size() == 1 &&
                    failure.duplicate_required_targets.front()
                                    .required_target_indices ==
                            std::vector<std::size_t>{0, 1},
            "Duplicate required target diagnostic differs");
    expect(
            failure.required_reason_conflicts.empty(),
            "Duplicate required target was also a reason conflict");
}

void test_install_reason_conflict() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-app", "1.0-1"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Explicit),
                            required_target(
                                    "shared-suite", "shared-suite-app",
                                    DesiredInstallReason::Dependency),
                    },
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, "Install reason conflict correlation");
    expect(
            failure.required_reason_conflicts.size() == 1 &&
                    failure.required_reason_conflicts.front()
                                    .occurrences.size() == 2,
            "Install reason conflict diagnostic differs");
    expect(
            failure.duplicate_required_targets.empty(),
            "Install reason conflict was also a duplicate target");
}

void test_invalid_package_base() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-app", "1.0-1"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "../shared-suite",
                    {required_target(
                            "../shared-suite", "shared-suite-app",
                            DesiredInstallReason::Explicit)},
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, "Invalid PackageBase correlation");
    expect(
            has_identity_inconsistency(
                    failure, ArtifactSelectionIdentityInput::PackageBase,
                    std::nullopt, "../shared-suite"),
            "Invalid PackageBase diagnostic was lost");
    expect(
            has_identity_inconsistency(
                    failure,
                    ArtifactSelectionIdentityInput::RequiredPackageBase, 0,
                    "../shared-suite"),
            "Invalid required PackageBase diagnostic was lost");
}

void test_invalid_package_name() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"core/filesystem", "1.0-1"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {required_target(
                            "shared-suite", "core/filesystem",
                            DesiredInstallReason::Explicit)},
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, "Invalid package name correlation");
    expect(
            has_identity_inconsistency(
                    failure, ArtifactSelectionIdentityInput::RequiredPackage,
                    0, "core/filesystem"),
            "Invalid required package diagnostic was lost");
    expect(
            has_identity_inconsistency(
                    failure, ArtifactSelectionIdentityInput::ProducedPackage,
                    std::nullopt, "core/filesystem"),
            "Invalid produced package diagnostic was lost");
}

void test_package_base_attribution_mismatch() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-app", "1.0-1"}});
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {required_target(
                            "other-suite", "shared-suite-app",
                            DesiredInstallReason::Explicit)},
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure = expect_failure(
            result, "PackageBase attribution mismatch correlation");
    expect(
            failure.attribution_mismatches.size() == 1,
            "Attribution mismatch diagnostic count differs");
    const RequiredArtifactAttributionMismatch& mismatch =
            failure.attribution_mismatches.front();
    expect(
            mismatch.required_target_index == 0 &&
                    mismatch.expected_package_base == "shared-suite" &&
                    mismatch.actual_package_base == "other-suite" &&
                    mismatch.package_name == "shared-suite-app",
            "Attribution mismatch diagnostic differs");
}

void test_unknown_install_reason() {
    ArtifactPackageIdentitySet identities =
            identity_set({{"shared-suite-app", "1.0-1"}});
    const DesiredInstallReason unknown_reason =
            static_cast<DesiredInstallReason>(99);
    PackageBaseArtifactIdentitySelectionResult result =
            correlate_package_base_artifact_identities(
                    "shared-suite",
                    {required_target(
                            "shared-suite", "shared-suite-app",
                            unknown_reason)},
                    identities);

    const PackageBaseArtifactIdentitySelectionFailure& failure =
            expect_failure(result, "Unknown install reason correlation");
    expect(
            failure.unknown_install_reasons.size() == 1 &&
                    failure.unknown_install_reasons.front()
                                    .required_target_index == 0 &&
                    failure.unknown_install_reasons.front()
                                    .target.desired_reason == unknown_reason,
            "Unknown install reason diagnostic differs");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("ordinary single output", test_ordinary_single_output);
        run_case("requested split child", test_requested_split_child);
        run_case("multiple selected children", test_multiple_selected_children);
        run_case(
                "root and dependency children",
                test_root_and_dependency_children);
        run_case(
                "stable selected and unselected order",
                test_stable_selected_and_unselected_order);
        run_case("empty required targets", test_empty_required_targets);
        run_case(
                "missing child and partial match isolation",
                test_missing_required_child_and_partial_match_isolation);
        run_case(
                "duplicate produced package name",
                test_duplicate_produced_package_name);
        run_case(
                "version-distinct duplicate package name",
                test_duplicate_produced_name_with_different_versions);
        run_case("duplicate required target", test_duplicate_required_target);
        run_case("install reason conflict", test_install_reason_conflict);
        run_case("invalid PackageBase", test_invalid_package_base);
        run_case("invalid package name", test_invalid_package_name);
        run_case(
                "PackageBase attribution mismatch",
                test_package_base_attribution_mismatch);
        run_case("unknown install reason", test_unknown_install_reason);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact identity selection tests: all 15 checks passed\n";
    return 0;
}
