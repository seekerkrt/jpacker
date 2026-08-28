#include "artifact_install_plan.hpp"
#include "dependency_plan.hpp"

#include <concepts>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ExpectedSelectedArtifact =
    std::pair<std::string, DesiredInstallReason>;

template <typename T>
concept HasDirectSelectedArtifacts = requires(T& value) {
    value.selected_artifacts;
};

template <typename T>
concept HasDirectPartialMatches = requires(T& value) {
    value.diagnostic_partial_matches;
};

template <typename T>
concept CanClearFailureDiagnostics = requires(T& value) {
    value.failure()->missing_required_artifacts.clear();
};

template <typename T>
concept CanClearDiagnosticPartialMatches = requires(T& value) {
    value.failure()->diagnostic_partial_matches.clear();
};

static_assert(
    !std::is_default_constructible_v<
        PackageBaseArtifactSelectionResult>);
static_assert(!std::is_aggregate_v<PackageBaseArtifactSelectionResult>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             PackageBaseArtifactSelectionSuccess>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             PackageBaseArtifactSelectionSuccess&>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             const PackageBaseArtifactSelectionSuccess&>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             PackageBaseArtifactSelectionFailure>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             PackageBaseArtifactSelectionFailure&>);
static_assert(
    !std::is_constructible_v<PackageBaseArtifactSelectionResult,
                             const PackageBaseArtifactSelectionFailure&>);
static_assert(
    std::is_copy_constructible_v<PackageBaseArtifactSelectionResult>);
static_assert(
    std::is_move_constructible_v<PackageBaseArtifactSelectionResult>);
static_assert(
    !std::is_copy_assignable_v<PackageBaseArtifactSelectionResult>);
static_assert(
    !std::is_move_assignable_v<PackageBaseArtifactSelectionResult>);
static_assert(std::same_as<
              decltype(std::declval<PackageBaseArtifactSelectionResult&>()
                           .success()),
              const PackageBaseArtifactSelectionSuccess*>);
static_assert(std::same_as<
              decltype(std::declval<PackageBaseArtifactSelectionResult&>()
                           .failure()),
              const PackageBaseArtifactSelectionFailure*>);
static_assert(std::same_as<
              decltype(std::declval<
                           const PackageBaseArtifactSelectionResult&>()
                           .failure()),
              const PackageBaseArtifactSelectionFailure*>);
static_assert(
    !HasDirectSelectedArtifacts<PackageBaseArtifactSelectionResult>);
static_assert(!HasDirectPartialMatches<PackageBaseArtifactSelectionResult>);
static_assert(
    !CanClearFailureDiagnostics<PackageBaseArtifactSelectionResult>);
static_assert(
    !CanClearDiagnosticPartialMatches<
        PackageBaseArtifactSelectionResult>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

RequiredPackageArtifactTarget required_target(
    const std::string& package_base, const std::string& package_name,
    DesiredInstallReason desired_reason) {
    return RequiredPackageArtifactTarget{
        package_base, package_name, desired_reason};
}

const PackageBaseArtifactSelectionSuccess& expect_success(
    const PackageBaseArtifactSelectionResult& result,
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

const PackageBaseArtifactSelectionFailure& expect_failure(
    const PackageBaseArtifactSelectionResult& result,
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
    const std::vector<SelectedPackageArtifact>& actual,
    const std::vector<ExpectedSelectedArtifact>& expected) {
    expect(
        actual.size() == expected.size(),
        "Selected artifact count differs");
    for(std::size_t i = 0; i < expected.size(); ++i) {
        expect(
            actual[i].artifact.package_name == expected[i].first,
            "Selected artifact identity differs at index " +
                std::to_string(i));
        expect(
            actual[i].desired_reason == expected[i].second,
            "Selected artifact install reason differs at index " +
                std::to_string(i));
    }
}

void expect_diagnostic_partial_matches(
    const std::vector<DiagnosticPackageArtifactMatch>& actual,
    const std::vector<std::size_t>& expected_required_indices,
    const std::vector<std::size_t>& expected_produced_indices,
    const std::vector<ExpectedSelectedArtifact>& expected_artifacts) {
    expect(
        actual.size() == expected_artifacts.size() &&
            actual.size() == expected_required_indices.size() &&
            actual.size() == expected_produced_indices.size(),
        "Diagnostic partial match count differs");
    for(std::size_t i = 0; i < actual.size(); ++i) {
        expect(
            actual[i].required_target_index == expected_required_indices[i],
            "Diagnostic required target index differs at index " +
                std::to_string(i));
        expect(
            actual[i].produced_artifact_index == expected_produced_indices[i],
            "Diagnostic produced artifact index differs at index " +
                std::to_string(i));
        expect(
            actual[i].artifact.package_name == expected_artifacts[i].first,
            "Diagnostic artifact identity differs at index " +
                std::to_string(i));
        expect(
            actual[i].desired_reason == expected_artifacts[i].second,
            "Diagnostic artifact reason differs at index " +
                std::to_string(i));
    }
}

void expect_artifact_names(
    const std::vector<ProducedPackageArtifact>& actual,
    const std::vector<std::string>& expected,
    const std::string& collection_name) {
    expect(
        actual.size() == expected.size(),
        collection_name + " artifact count differs");
    for(std::size_t i = 0; i < expected.size(); ++i) {
        expect(
            actual[i].package_name == expected[i],
            collection_name + " artifact identity differs at index " +
                std::to_string(i));
    }
}

void expect_non_group_failures_empty(
    const PackageBaseArtifactSelectionFailure& failure) {
    expect(
        failure.missing_required_artifacts.empty(),
        "Unexpected missing required artifact issue");
    expect(
        failure.duplicate_produced_identities.empty(),
        "Unexpected duplicate produced identity issue");
    expect(
        failure.attribution_mismatches.empty(),
        "Unexpected attribution mismatch issue");
    expect(
        failure.identity_inconsistencies.empty(),
        "Unexpected identity inconsistency issue");
    expect(
        failure.unknown_install_reasons.empty(),
        "Unexpected unknown install reason issue");
}

void test_empty_required_targets() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "suite", {}, {{"suite"}, {"suite-debug"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Empty required target selection");
    expect(success.package_base == "suite", "Result PackageBase differs");
    expect_selected_artifacts(success.selected_artifacts, {});
    expect_artifact_names(
        success.unselected_artifacts,
        {"suite", "suite-debug"}, "Unselected");
}

void test_ordinary_single_output() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "sample-package",
            {required_target(
                "sample-package", "sample-package",
                DesiredInstallReason::Explicit)},
            {{"sample-package"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Ordinary single-output selection");
    expect(
        success.package_base == "sample-package",
        "Result PackageBase differs");
    expect_selected_artifacts(
        success.selected_artifacts,
        {{"sample-package", DesiredInstallReason::Explicit}});
    expect_artifact_names(success.unselected_artifacts, {}, "Unselected");
}

void test_split_package_child() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {required_target(
                "shared-suite", "shared-suite-cli",
                DesiredInstallReason::Explicit)},
            {{"shared-suite-cli"}}});

    const PackageBaseArtifactSelectionSuccess& success = expect_success(
        result, "PackageBase-distinct requested child selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {{"shared-suite-cli", DesiredInstallReason::Explicit}});
}

void test_multiple_required_children() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-runtime",
                    DesiredInstallReason::Dependency),
                required_target(
                    "shared-suite", "shared-suite-tools",
                    DesiredInstallReason::Dependency),
            },
            {{"shared-suite-runtime"}, {"shared-suite-tools"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Multiple required child selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {
            {"shared-suite-runtime", DesiredInstallReason::Dependency},
            {"shared-suite-tools", DesiredInstallReason::Dependency},
        });
}

void test_root_and_dependency_reasons() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit),
                required_target(
                    "shared-suite", "shared-suite-library",
                    DesiredInstallReason::Dependency),
            },
            {{"shared-suite-library"}, {"shared-suite-app"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Root/dependency child selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {
            {"shared-suite-app", DesiredInstallReason::Explicit},
            {"shared-suite-library", DesiredInstallReason::Dependency},
        });
}

void test_sibling_output_is_unselected() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {required_target(
                "shared-suite", "shared-suite-app",
                DesiredInstallReason::Explicit)},
            {{"shared-suite-app"}, {"shared-suite-docs"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Sibling output selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {{"shared-suite-app", DesiredInstallReason::Explicit}});
    expect_artifact_names(
        success.unselected_artifacts,
        {"shared-suite-docs"}, "Unselected");
}

void test_debug_output_is_unselected() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {required_target(
                "shared-suite", "shared-suite-app",
                DesiredInstallReason::Explicit)},
            {{"shared-suite-app"}, {"shared-suite-app-debug"}}});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Debug output selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {{"shared-suite-app", DesiredInstallReason::Explicit}});
    expect_artifact_names(
        success.unselected_artifacts,
        {"shared-suite-app-debug"}, "Unselected");
}

void test_missing_required_child() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit),
                required_target(
                    "shared-suite", "shared-suite-library",
                    DesiredInstallReason::Dependency),
            },
            {{"shared-suite-app"}, {"shared-suite-docs"}}});

    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, "Missing required child selection");
    expect(
        failure.missing_required_artifacts.size() == 1,
        "Missing issue count differs");
    const MissingRequiredArtifact& missing =
        failure.missing_required_artifacts.front();
    expect(
        missing.required_target_index == 1 &&
            missing.target.package_name == "shared-suite-library" &&
            missing.target.desired_reason ==
                DesiredInstallReason::Dependency,
        "Missing required child attribution differs");
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches,
        {0}, {0},
        {{"shared-suite-app", DesiredInstallReason::Explicit}});
    expect_artifact_names(
        failure.diagnostic_unselected_artifacts,
        {"shared-suite-docs"}, "Diagnostic unmatched");
    expect(
        failure.duplicate_produced_identities.empty() &&
            failure.duplicate_required_targets.empty() &&
            failure.required_reason_conflicts.empty() &&
            failure.attribution_mismatches.empty() &&
            failure.identity_inconsistencies.empty() &&
            failure.unknown_install_reasons.empty(),
        "Missing selection has unrelated failure diagnostics");
}

void expect_duplicate_produced_issue(
    const PackageBaseArtifactSelectionFailure& failure,
    const std::string& package_name) {
    expect(
        failure.duplicate_produced_identities.size() == 1,
        "Duplicate produced identity issue count differs");
    const DuplicateProducedPackageIdentity& duplicate =
        failure.duplicate_produced_identities.front();
    expect(
        duplicate.first_artifact_index == 0 &&
            duplicate.duplicate_artifact_index == 1 &&
            duplicate.package_name == package_name,
        "Duplicate produced identity attribution differs");
}

void test_duplicate_produced_identity() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {required_target(
                "shared-suite", "shared-suite-app",
                DesiredInstallReason::Explicit)},
            {{"shared-suite-app"}, {"shared-suite-app"}}});

    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, "Required duplicate produced identity");
    expect_duplicate_produced_issue(failure, "shared-suite-app");
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        failure.diagnostic_unselected_artifacts,
        {"shared-suite-app", "shared-suite-app"},
        "Diagnostic unmatched");
    expect(
        failure.missing_required_artifacts.empty() &&
            failure.duplicate_required_targets.empty() &&
            failure.required_reason_conflicts.empty() &&
            failure.attribution_mismatches.empty() &&
            failure.identity_inconsistencies.empty() &&
            failure.unknown_install_reasons.empty(),
        "Duplicate produced identity has unrelated diagnostics");

    PackageBaseArtifactSelectionResult unrequired_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "shared-suite", {}, {{"shared-suite-docs"}, {"shared-suite-docs"}}});
    const PackageBaseArtifactSelectionFailure& unrequired_failure =
        expect_failure(
            unrequired_result,
            "Unrequired duplicate produced identity");
    expect_duplicate_produced_issue(unrequired_failure, "shared-suite-docs");
    expect_diagnostic_partial_matches(
        unrequired_failure.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        unrequired_failure.diagnostic_unselected_artifacts,
        {"shared-suite-docs", "shared-suite-docs"},
        "Diagnostic unmatched");
    expect(
        unrequired_failure.missing_required_artifacts.empty() &&
            unrequired_failure.duplicate_required_targets.empty() &&
            unrequired_failure.required_reason_conflicts.empty() &&
            unrequired_failure.attribution_mismatches.empty() &&
            unrequired_failure.identity_inconsistencies.empty() &&
            unrequired_failure.unknown_install_reasons.empty(),
        "Unrequired duplicate has unrelated diagnostics");
}

void test_duplicate_required_target() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit),
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit),
            },
            {{"shared-suite-app"}}});

    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, "Duplicate required target");
    expect(
        failure.duplicate_required_targets.size() == 1,
        "Duplicate required target issue count differs");
    expect(
        failure.required_reason_conflicts.empty(),
        "Same-reason duplicate was also classified as a conflict");
    const DuplicateRequiredArtifactTarget& duplicate =
        failure.duplicate_required_targets.front();
    expect(
        duplicate.package_name == "shared-suite-app" &&
            duplicate.desired_reason ==
                DesiredInstallReason::Explicit &&
            duplicate.required_target_indices ==
                std::vector<std::size_t>{0, 1},
        "Duplicate required target attribution differs");
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        failure.diagnostic_unselected_artifacts,
        {"shared-suite-app"}, "Diagnostic unmatched");
    expect_non_group_failures_empty(failure);
}

void test_required_target_reason_conflict() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit),
                required_target(
                    "shared-suite", "shared-suite-app",
                    DesiredInstallReason::Dependency),
            },
            {{"shared-suite-app"}}});

    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, "Required target reason conflict");
    expect(
        failure.required_reason_conflicts.size() == 1,
        "Required target reason conflict count differs");
    expect(
        failure.duplicate_required_targets.empty(),
        "Reason conflict was also classified as a duplicate");
    const RequiredArtifactReasonConflict& conflict =
        failure.required_reason_conflicts.front();
    expect(
        conflict.package_name == "shared-suite-app" &&
            conflict.occurrences.size() == 2 &&
            conflict.occurrences[0].required_target_index == 0 &&
            conflict.occurrences[0].desired_reason ==
                DesiredInstallReason::Explicit &&
            conflict.occurrences[1].required_target_index == 1 &&
            conflict.occurrences[1].desired_reason ==
                DesiredInstallReason::Dependency,
        "Required target reason conflict attribution differs");
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches, {}, {}, {});
    expect_non_group_failures_empty(failure);
}

void expect_mixed_reason_group_conflict(
    const std::vector<DesiredInstallReason>& reasons,
    const std::string& context) {
    std::vector<RequiredPackageArtifactTarget> required_targets;
    for(const DesiredInstallReason reason : reasons) {
        required_targets.push_back(required_target(
            "shared-suite", "shared-suite-app", reason));
    }

    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite", std::move(required_targets), {{"shared-suite-app"}}});
    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, context);
    expect(
        failure.required_reason_conflicts.size() == 1,
        context + " conflict count differs");
    expect(
        failure.duplicate_required_targets.empty(),
        context + " was also classified as duplicate");
    const RequiredArtifactReasonConflict& conflict =
        failure.required_reason_conflicts.front();
    expect(
        conflict.package_name == "shared-suite-app" &&
            conflict.occurrences.size() == 3,
        context + " group attribution differs");
    for(std::size_t i = 0; i < reasons.size(); ++i) {
        expect(
            conflict.occurrences[i].required_target_index == i &&
                conflict.occurrences[i].desired_reason == reasons[i],
            context + " occurrence order differs at index " +
                std::to_string(i));
    }
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        failure.diagnostic_unselected_artifacts,
        {"shared-suite-app"}, "Diagnostic unmatched");
    expect_non_group_failures_empty(failure);
}

void test_mixed_reason_group_permutations() {
    expect_mixed_reason_group_conflict(
        {
            DesiredInstallReason::Explicit,
            DesiredInstallReason::Dependency,
            DesiredInstallReason::Dependency,
        },
        "Explicit/Dependency/Dependency group");
    expect_mixed_reason_group_conflict(
        {
            DesiredInstallReason::Dependency,
            DesiredInstallReason::Explicit,
            DesiredInstallReason::Dependency,
        },
        "Dependency/Explicit/Dependency group");
    expect_mixed_reason_group_conflict(
        {
            DesiredInstallReason::Dependency,
            DesiredInstallReason::Dependency,
            DesiredInstallReason::Explicit,
        },
        "Dependency/Dependency/Explicit group");
}

void expect_same_reason_duplicate_group(
    DesiredInstallReason reason, const std::string& context) {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "shared-suite-app", reason),
                required_target(
                    "shared-suite", "shared-suite-app", reason),
                required_target(
                    "shared-suite", "shared-suite-app", reason),
            },
            {{"shared-suite-app"}}});

    const PackageBaseArtifactSelectionFailure& failure =
        expect_failure(result, context);
    expect(
        failure.duplicate_required_targets.size() == 1,
        context + " duplicate group count differs");
    expect(
        failure.required_reason_conflicts.empty(),
        context + " was also classified as conflict");
    const DuplicateRequiredArtifactTarget& duplicate =
        failure.duplicate_required_targets.front();
    expect(
        duplicate.package_name == "shared-suite-app" &&
            duplicate.desired_reason == reason &&
            duplicate.required_target_indices ==
                std::vector<std::size_t>{0, 1, 2},
        context + " group attribution differs");
    expect_diagnostic_partial_matches(
        failure.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        failure.diagnostic_unselected_artifacts,
        {"shared-suite-app"}, "Diagnostic unmatched");
    expect_non_group_failures_empty(failure);
}

void test_same_reason_duplicate_groups() {
    expect_same_reason_duplicate_group(
        DesiredInstallReason::Explicit, "Explicit duplicate group");
    expect_same_reason_duplicate_group(
        DesiredInstallReason::Dependency, "Dependency duplicate group");
}

void test_stable_ordering() {
    PackageBaseArtifactSelectionResult result = select_package_base_artifacts(
        PackageBaseArtifactSelectionRequest{
            "shared-suite",
            {
                required_target(
                    "shared-suite", "child-b",
                    DesiredInstallReason::Dependency),
                required_target(
                    "shared-suite", "child-a",
                    DesiredInstallReason::Explicit),
            },
            {
                {"sibling"},
                {"child-a"},
                {"child-b"},
                {"child-a-debug"},
            }});

    const PackageBaseArtifactSelectionSuccess& success =
        expect_success(result, "Stable-order selection");
    expect_selected_artifacts(
        success.selected_artifacts,
        {
            {"child-b", DesiredInstallReason::Dependency},
            {"child-a", DesiredInstallReason::Explicit},
        });
    expect_artifact_names(
        success.unselected_artifacts,
        {"sibling", "child-a-debug"}, "Unselected");
}

void test_unknown_and_inconsistent_input() {
    PackageBaseArtifactSelectionResult invalid_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "",
                {required_target(
                    "", "",
                    static_cast<DesiredInstallReason>(2))},
                {{""}}});
    const PackageBaseArtifactSelectionFailure& invalid =
        expect_failure(invalid_result, "Unknown or empty selection input");
    expect(
        invalid.identity_inconsistencies.size() == 4,
        "Identity inconsistency count differs");
    expect(
        invalid.identity_inconsistencies[0].input ==
                ArtifactSelectionIdentityInput::PackageBase &&
            !invalid.identity_inconsistencies[0]
                 .input_index.has_value(),
        "PackageBase identity inconsistency differs");
    expect(
        invalid.identity_inconsistencies[1].input ==
                ArtifactSelectionIdentityInput::RequiredPackageBase &&
            invalid.identity_inconsistencies[1].input_index ==
                std::optional<std::size_t>{0},
        "Required PackageBase identity inconsistency differs");
    expect(
        invalid.identity_inconsistencies[2].input ==
                ArtifactSelectionIdentityInput::RequiredPackage &&
            invalid.identity_inconsistencies[2].input_index ==
                std::optional<std::size_t>{0},
        "Required identity inconsistency differs");
    expect(
        invalid.identity_inconsistencies[3].input ==
                ArtifactSelectionIdentityInput::ProducedPackage &&
            invalid.identity_inconsistencies[3].input_index ==
                std::optional<std::size_t>{0},
        "Produced identity inconsistency differs");
    expect(
        invalid.unknown_install_reasons.size() == 1,
        "Unknown install reason issue count differs");
    expect_diagnostic_partial_matches(
        invalid.diagnostic_partial_matches, {}, {}, {});

    PackageBaseArtifactSelectionResult omitted_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "shared-suite",
                {RequiredPackageArtifactTarget{
                    "shared-suite", "shared-suite-app"}},
                {{"shared-suite-app"}}});
    const PackageBaseArtifactSelectionFailure& omitted =
        expect_failure(omitted_result, "Omitted install reason");
    expect(
        omitted.unknown_install_reasons.size() == 1,
        "Omitted install reason defaulted to a supported reason");
    expect_diagnostic_partial_matches(
        omitted.diagnostic_partial_matches, {}, {}, {});

    PackageBaseArtifactSelectionResult invalid_names_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "../base",
                {required_target(
                    "../base", "core/filesystem",
                    DesiredInstallReason::Explicit)},
                {{"core/filesystem"}}});
    const PackageBaseArtifactSelectionFailure& invalid_names =
        expect_failure(invalid_names_result, "Invalid package identifiers");
    expect(
        invalid_names.identity_inconsistencies.size() == 4,
        "Invalid package identifier issue count differs");
    expect_diagnostic_partial_matches(
        invalid_names.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        invalid_names.diagnostic_unselected_artifacts,
        {"core/filesystem"}, "Diagnostic unmatched");

    PackageBaseArtifactSelectionResult mismatch_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "shared-suite",
                {required_target(
                    "other-suite", "shared-suite-app",
                    DesiredInstallReason::Explicit)},
                {{"shared-suite-app"}}});
    const PackageBaseArtifactSelectionFailure& mismatched =
        expect_failure(mismatch_result, "Mismatched PackageBase attribution");
    expect(
        mismatched.attribution_mismatches.size() == 1,
        "Attribution mismatch count differs");
    const RequiredArtifactAttributionMismatch& mismatch =
        mismatched.attribution_mismatches.front();
    expect(
        mismatch.required_target_index == 0 &&
            mismatch.expected_package_base == "shared-suite" &&
            mismatch.actual_package_base == "other-suite" &&
            mismatch.package_name == "shared-suite-app",
        "Attribution mismatch detail differs");
    expect_diagnostic_partial_matches(
        mismatched.diagnostic_partial_matches, {}, {}, {});
    expect_artifact_names(
        mismatched.diagnostic_unselected_artifacts,
        {"shared-suite-app"}, "Diagnostic unmatched");

    PackageBaseArtifactSelectionResult partial_result =
        select_package_base_artifacts(
            PackageBaseArtifactSelectionRequest{
                "shared-suite",
                {
                    required_target(
                        "other-suite", "shared-suite-app",
                        DesiredInstallReason::Explicit),
                    required_target(
                        "shared-suite", "shared-suite-app",
                        DesiredInstallReason::Explicit),
                },
                {{"shared-suite-app"}}});
    const PackageBaseArtifactSelectionFailure& partial = expect_failure(
        partial_result, "Attribution mismatch with valid target");
    expect(
        partial.attribution_mismatches.size() == 1 &&
            partial.duplicate_required_targets.empty() &&
            partial.required_reason_conflicts.empty(),
        "Attribution mismatch was conflated with a required group issue");
    expect_diagnostic_partial_matches(
        partial.diagnostic_partial_matches,
        {1}, {0},
        {{"shared-suite-app", DesiredInstallReason::Explicit}});
    expect_artifact_names(
        partial.diagnostic_unselected_artifacts,
        {}, "Diagnostic unmatched");
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case("empty required targets", test_empty_required_targets);
        run_case("ordinary single-output", test_ordinary_single_output);
        run_case("split package child", test_split_package_child);
        run_case("multiple required children", test_multiple_required_children);
        run_case("root and dependency reasons", test_root_and_dependency_reasons);
        run_case(
            "sibling output is unselected",
            test_sibling_output_is_unselected);
        run_case(
            "debug output is unselected",
            test_debug_output_is_unselected);
        run_case("missing required child", test_missing_required_child);
        run_case(
            "duplicate produced identity",
            test_duplicate_produced_identity);
        run_case(
            "duplicate required target",
            test_duplicate_required_target);
        run_case(
            "required target reason conflict",
            test_required_target_reason_conflict);
        run_case(
            "mixed reason group permutations",
            test_mixed_reason_group_permutations);
        run_case(
            "same reason duplicate groups",
            test_same_reason_duplicate_groups);
        run_case("stable ordering", test_stable_ordering);
        run_case(
            "unknown and inconsistent input",
            test_unknown_and_inconsistent_input);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact selection model tests: all 15 checks passed\n";
    return 0;
}
