#include "aur_update_plan.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void run_devel_package_classification_tests();
void run_devel_update_model_tests();

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename ExpectedException, typename Callable>
void expect_exception(Callable callable, const std::string& expected_message) {
    try {
        callable();
    } catch(const ExpectedException& error) {
        expect(
                std::string(error.what()) == expected_message,
                "Unexpected exception message: expected [" + expected_message +
                        "], actual [" + error.what() + "]");
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Unexpected exception category: " + std::string(error.what()));
    }

    throw std::runtime_error("Expected exception: " + expected_message);
}

AurUpdatePlanInput remote_input(
        const std::string& installed_name,
        const std::string& installed_version,
        InstalledPackageReason install_reason,
        const std::string& aur_name,
        const std::string& package_base,
        const std::string& aur_version,
        AurVersionRelation version_relation) {
    return AurUpdatePlanInput{
            installed_name,
            installed_version,
            install_reason,
            AurUpdateRemotePackage{
                    aur_name, package_base, aur_version, version_relation}};
}

void test_newer_remote_version_is_update_available() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "sample-package", "1.0-1", InstalledPackageReason::Explicit,
            "sample-package", "sample-package", "2.0-1",
            AurVersionRelation::NewerThanInstalled));

    expect(
            entry.classification == AurUpdateClassification::UpdateAvailable,
            "Newer remote version was not classified as update available");
    expect(
            entry.install_reason == InstalledPackageReason::Explicit,
            "Update-available entry lost the explicit install reason");
    expect(entry.aur_package.has_value(), "Update entry lost AUR metadata");
    expect(entry.aur_package->version == "2.0-1", "Update entry AUR version differs");
}

void test_same_remote_version_is_up_to_date() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "sample-package", "1.0-1", InstalledPackageReason::Dependency,
            "sample-package", "sample-package", "1.0-1",
            AurVersionRelation::SameAsInstalled));

    expect(
            entry.classification == AurUpdateClassification::UpToDate,
            "Same remote version was not classified as up to date");
    expect(
            entry.install_reason == InstalledPackageReason::Dependency,
            "Up-to-date entry lost the dependency install reason");
}

void test_older_remote_version_is_up_to_date() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "sample-package", "2.0-1", InstalledPackageReason::Unknown,
            "sample-package", "sample-package", "1.0-1",
            AurVersionRelation::OlderThanInstalled));

    expect(
            entry.classification == AurUpdateClassification::UpToDate,
            "Older remote version was not classified as up to date");
}

void test_missing_aur_metadata_is_non_aur_foreign() {
    AurUpdatePlanEntry entry = classify_aur_update(AurUpdatePlanInput{
            "repository-only-package",
            "1.0-1",
            InstalledPackageReason::Unknown,
            AurUpdateMetadataNotFound{}});

    expect(
            entry.classification == AurUpdateClassification::NonAurForeign,
            "Missing AUR metadata was not classified as non-AUR foreign");
    expect(
            entry.install_reason == InstalledPackageReason::Unknown,
            "Non-AUR entry changed the unknown install reason");
    expect(!entry.aur_package.has_value(), "Non-AUR entry unexpectedly has AUR metadata");
}

void test_unavailable_metadata_remains_distinct() {
    AurUpdatePlanEntry entry = classify_aur_update(AurUpdatePlanInput{
            "unknown-package",
            "1.0-1",
            InstalledPackageReason::Explicit,
            AurUpdateMetadataUnavailable{}});

    expect(
            entry.classification == AurUpdateClassification::MetadataUnavailable,
            "Unavailable metadata was not preserved as a distinct classification");
    expect(
            entry.install_reason == InstalledPackageReason::Explicit,
            "Metadata-unavailable entry lost the explicit install reason");
    expect(
            !entry.aur_package.has_value(),
            "Metadata-unavailable entry unexpectedly has AUR metadata");
}

void test_unavailable_version_comparison_remains_distinct() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "sample-package", "opaque-installed", InstalledPackageReason::Dependency,
            "sample-package", "sample-package", "opaque-remote",
            AurVersionRelation::Unavailable));

    expect(
            entry.classification ==
                    AurUpdateClassification::VersionComparisonUnavailable,
            "Unavailable version comparison was not preserved");
    expect(
            entry.install_reason == InstalledPackageReason::Dependency,
            "Version-comparison failure lost the dependency install reason");
    expect(
            entry.aur_package.has_value(),
            "Version-comparison failure lost available AUR metadata");
}

void test_package_name_and_package_base_are_both_preserved() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "split-cli", "1.0-1", InstalledPackageReason::Explicit, "split-cli",
            "split-suite", "2.0-1", AurVersionRelation::NewerThanInstalled));

    expect(entry.installed_name == "split-cli", "Installed package name differs");
    expect(entry.aur_package.has_value(), "Split package entry lost AUR metadata");
    expect(entry.aur_package->aur_name == "split-cli", "AUR package name differs");
    expect(entry.aur_package->package_base == "split-suite", "PackageBase differs");
}

void test_plan_preserves_input_order() {
    const std::vector<AurUpdatePlanInput> inputs = {
            remote_input(
                    "zeta-package", "1", InstalledPackageReason::Explicit,
                    "zeta-package", "zeta-package", "2",
                    AurVersionRelation::NewerThanInstalled),
            {"alpha-package",
             "1",
             InstalledPackageReason::Dependency,
             AurUpdateMetadataNotFound{}},
            remote_input(
                    "middle-package", "1", InstalledPackageReason::Unknown,
                    "middle-package", "middle-package", "1",
                    AurVersionRelation::SameAsInstalled)};

    AurUpdatePlan plan = make_aur_update_plan(inputs);

    expect(plan.entries.size() == inputs.size(), "Plan entry count differs");
    expect(plan.entries[0].installed_name == "zeta-package", "First plan entry was reordered");
    expect(plan.entries[1].installed_name == "alpha-package", "Second plan entry was reordered");
    expect(plan.entries[2].installed_name == "middle-package", "Third plan entry was reordered");
    expect(
            plan.entries[0].install_reason == InstalledPackageReason::Explicit,
            "First plan entry lost its install reason");
    expect(
            plan.entries[1].install_reason == InstalledPackageReason::Dependency,
            "Second plan entry lost its install reason");
    expect(
            plan.entries[2].install_reason == InstalledPackageReason::Unknown,
            "Third plan entry lost its install reason");
}

void test_classifier_uses_relation_without_parsing_versions() {
    AurUpdatePlanEntry entry = classify_aur_update(remote_input(
            "sample-package", "not a pacman version", InstalledPackageReason::Unknown,
            "sample-package", "sample-package", "also:not/a/version",
            AurVersionRelation::NewerThanInstalled));

    expect(
            entry.classification == AurUpdateClassification::UpdateAvailable,
            "Classifier did not treat the supplied relation as authoritative");
    expect(
            entry.installed_version == "not a pacman version",
            "Classifier changed the opaque installed version");
    expect(entry.aur_package.has_value(), "Opaque version entry lost AUR metadata");
    expect(
            entry.aur_package->version == "also:not/a/version",
            "Classifier changed the opaque AUR version");
}

void test_unknown_version_relation_is_rejected() {
    AurUpdatePlanInput input = remote_input(
            "sample-package", "1.0-1", InstalledPackageReason::Unknown,
            "sample-package", "sample-package", "2.0-1",
            static_cast<AurVersionRelation>(4));

    expect_exception<std::logic_error>(
            [&input]() { static_cast<void>(classify_aur_update(input)); },
            "Unknown AUR version relation.");
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
                "newer remote version is update available",
                test_newer_remote_version_is_update_available);
        run_case("same remote version is up to date", test_same_remote_version_is_up_to_date);
        run_case("older remote version is up to date", test_older_remote_version_is_up_to_date);
        run_case(
                "missing AUR metadata is non-AUR foreign",
                test_missing_aur_metadata_is_non_aur_foreign);
        run_case(
                "unavailable metadata remains distinct",
                test_unavailable_metadata_remains_distinct);
        run_case(
                "unavailable version comparison remains distinct",
                test_unavailable_version_comparison_remains_distinct);
        run_case(
                "package name and PackageBase are both preserved",
                test_package_name_and_package_base_are_both_preserved);
        run_case("plan preserves input order", test_plan_preserves_input_order);
        run_case(
                "classifier uses relation without parsing versions",
                test_classifier_uses_relation_without_parsing_versions);
        run_case(
                "unknown version relation is rejected",
                test_unknown_version_relation_is_rejected);
        run_case(
                "devel package classification foundation",
                run_devel_package_classification_tests);
        run_case(
                "devel update assessment foundation",
                run_devel_update_model_tests);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update plan tests: all checks passed\n";
    return 0;
}
