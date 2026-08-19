#include "source_package_compatibility.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(
        !std::is_default_constructible_v<
                SourcePackageCompatibilityEvaluation>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

bool has_reason(
        const SourcePackageCompatibilityEvaluation& evaluation,
        SourcePackageMismatchReason reason) {
    for(const SourcePackageMismatchReason candidate : evaluation.reasons()) {
        if(candidate == reason) return true;
    }
    return false;
}

PackageSourceIdentity aur_source(
        SourceLocationIdentity location =
                SourceLocationIdentity::known_git_remote(
                        "https://aur.archlinux.org/suite-base.git")) {
    return PackageSourceIdentity::aur(std::move(location));
}

PackageSourceIdentity repository_source(
        std::string repository_name = "extra",
        std::string location =
                "https://gitlab.archlinux.org/archlinux/packaging/packages/suite-base.git") {
    return PackageSourceIdentity::repository(
            std::move(repository_name),
            SourceLocationIdentity::known_git_remote(std::move(location)));
}

SourceAwarePackageIdentity identity(
        PackageSourceIdentity source = aur_source(),
        std::string package_base = "suite-base",
        std::string package_name = "suite-child",
        SourceRevisionIdentity revision =
                SourceRevisionIdentity::git_commit(std::string(40, 'a')),
        PackageVersionIdentity version =
                PackageVersionIdentity::composite("1.0-1"),
        PackageArchitectureIdentity architecture =
                PackageArchitectureIdentity::known({"x86_64"})) {
    return SourceAwarePackageIdentity::make(
            PackageChildIdentity::make(
                    PackageBaseIdentity::make(
                            std::move(source), std::move(package_base)),
                    std::move(package_name)),
            std::move(revision), std::move(version),
            std::move(architecture));
}

void test_exact_match_and_version_representation() {
    const SourceAwarePackageIdentity expected = identity();
    const SourceAwarePackageIdentity actual = identity(
            aur_source(), "suite-base", "suite-child",
            SourceRevisionIdentity::git_commit(std::string(40, 'a')),
            PackageVersionIdentity::pkgver_pkgrel(
                    std::nullopt, "1.0", "1"),
            PackageArchitectureIdentity::known({"x86_64"}));
    expect(expected != actual,
            "Fixture must keep structural version representation distinct.");

    const SourcePackageCompatibilityEvaluation evaluation =
            evaluate_source_package_compatibility(expected, actual);
    expect(evaluation.kind() == SourcePackageCompatibilityKind::ExactMatch &&
                   evaluation.is_exact_match() &&
                   evaluation.source_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.package_base_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.package_child_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.revision_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.version_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.architecture_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   evaluation.reasons().empty(),
           "Known exact values did not produce ExactMatch.");
}

void test_same_package_base_and_child() {
    const SourceAwarePackageIdentity expected = identity();
    const SourcePackageCompatibilityEvaluation sibling =
            evaluate_source_package_compatibility(
                    expected,
                    identity(
                            aur_source(), "suite-base", "suite-sibling"));
    expect(sibling.kind() ==
                           SourcePackageCompatibilityKind::SamePackageBase &&
                   sibling.package_base_state() ==
                           SourcePackageCompatibilityDimensionState::Matched &&
                   sibling.package_child_state() ==
                           SourcePackageCompatibilityDimensionState::
                                   Mismatched &&
                   has_reason(
                           sibling,
                           SourcePackageMismatchReason::PackageChildMismatch),
           "Different split child was not classified as SamePackageBase.");

    const SourcePackageCompatibilityEvaluation commit_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::git_commit(
                                    std::string(40, 'b'))));
    expect(commit_drift.kind() ==
                           SourcePackageCompatibilityKind::SamePackageChild &&
                   commit_drift.revision_state() ==
                           SourcePackageCompatibilityDimensionState::
                                   Mismatched &&
                   has_reason(
                           commit_drift,
                           SourcePackageMismatchReason::SourceCommitMismatch),
           "Known commit drift was not retained as SamePackageChild evidence.");

    const SourcePackageCompatibilityEvaluation release_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::git_commit(
                                    std::string(40, 'a')),
                            PackageVersionIdentity::composite("1.0-2")));
    expect(release_drift.kind() ==
                           SourcePackageCompatibilityKind::SamePackageChild &&
                   has_reason(
                           release_drift,
                           SourcePackageMismatchReason::
                                   PackageVersionMismatch),
           "Package release drift was not typed.");

    const SourcePackageCompatibilityEvaluation architecture_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::git_commit(
                                    std::string(40, 'a')),
                            PackageVersionIdentity::composite("1.0-1"),
                            PackageArchitectureIdentity::known({"aarch64"})));
    expect(architecture_drift.kind() ==
                           SourcePackageCompatibilityKind::SamePackageChild &&
                   has_reason(
                           architecture_drift,
                           SourcePackageMismatchReason::ArchitectureMismatch),
           "Architecture drift was not typed.");
}

void test_source_and_package_base_drift() {
    const SourceAwarePackageIdentity expected = identity();
    const SourcePackageCompatibilityEvaluation kind_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(repository_source()));
    expect(kind_drift.kind() ==
                           SourcePackageCompatibilityKind::Incompatible &&
                   has_reason(
                           kind_drift,
                           SourcePackageMismatchReason::SourceKindMismatch),
           "Source kind drift was not incompatible.");

    const SourcePackageCompatibilityEvaluation repository_drift =
            evaluate_source_package_compatibility(
                    identity(repository_source()),
                    identity(repository_source("core")));
    expect(repository_drift.kind() ==
                           SourcePackageCompatibilityKind::Incompatible &&
                   has_reason(
                           repository_drift,
                           SourcePackageMismatchReason::RepositoryMismatch),
           "Repository drift was not typed.");

    const SourcePackageCompatibilityEvaluation location_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(aur_source(
                            SourceLocationIdentity::known_git_remote(
                                    "https://example.invalid/suite-base.git"))));
    expect(location_drift.kind() ==
                           SourcePackageCompatibilityKind::Incompatible &&
                   has_reason(
                           location_drift,
                           SourcePackageMismatchReason::
                                   SourceLocationMismatch),
           "Known source location drift was not typed.");

    const SourcePackageCompatibilityEvaluation package_base_drift =
            evaluate_source_package_compatibility(
                    expected,
                    identity(aur_source(), "other-base"));
    expect(package_base_drift.kind() ==
                           SourcePackageCompatibilityKind::Incompatible &&
                   has_reason(
                           package_base_drift,
                           SourcePackageMismatchReason::PackageBaseMismatch),
           "PackageBase drift was not incompatible.");
}

void test_unknown_unavailable_absent_and_inapplicable() {
    const SourceAwarePackageIdentity unknown = identity(
            aur_source(SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote)),
            "suite-base", "suite-child",
            SourceRevisionIdentity::unknown(),
            PackageVersionIdentity::unknown(),
            PackageArchitectureIdentity::unknown());
    const SourcePackageCompatibilityEvaluation unknown_evaluation =
            evaluate_source_package_compatibility(unknown, unknown);
    expect(unknown == unknown &&
                   unknown_evaluation.kind() ==
                           SourcePackageCompatibilityKind::Indeterminate &&
                   !unknown_evaluation.is_exact_match() &&
                   has_reason(
                           unknown_evaluation,
                           SourcePackageMismatchReason::
                                   SourceLocationUnknown) &&
                   has_reason(
                           unknown_evaluation,
                           SourcePackageMismatchReason::
                                   SourceRevisionUnknown) &&
                   has_reason(
                           unknown_evaluation,
                           SourcePackageMismatchReason::
                                   PackageVersionUnknown) &&
                   has_reason(
                           unknown_evaluation,
                           SourcePackageMismatchReason::ArchitectureUnknown),
           "Structurally equal Unknown evidence became ExactMatch.");

    const SourceAwarePackageIdentity unavailable = identity(
            aur_source(SourceLocationIdentity::unavailable(
                    SourceLocationKind::GitRemote,
                    IdentityUnavailableReason::ObservationFailed)),
            "suite-base", "suite-child",
            SourceRevisionIdentity::unavailable(
                    IdentityUnavailableReason::ObservationFailed),
            PackageVersionIdentity::unavailable(
                    IdentityUnavailableReason::ObservationFailed),
            PackageArchitectureIdentity::unavailable(
                    IdentityUnavailableReason::ObservationFailed));
    const SourcePackageCompatibilityEvaluation unavailable_evaluation =
            evaluate_source_package_compatibility(
                    unavailable, unavailable);
    expect(unavailable_evaluation.kind() ==
                           SourcePackageCompatibilityKind::Indeterminate &&
                   has_reason(
                           unavailable_evaluation,
                           SourcePackageMismatchReason::
                                   SourceLocationUnavailable) &&
                   has_reason(
                           unavailable_evaluation,
                           SourcePackageMismatchReason::
                                   SourceRevisionUnavailable) &&
                   has_reason(
                           unavailable_evaluation,
                           SourcePackageMismatchReason::
                                   PackageVersionUnavailable) &&
                   has_reason(
                           unavailable_evaluation,
                           SourcePackageMismatchReason::
                                   ArchitectureUnavailable),
           "Unavailable evidence became compatible.");

    const SourcePackageCompatibilityEvaluation absent =
            evaluate_source_package_compatibility(
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::absent()),
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::absent()));
    expect(absent.kind() == SourcePackageCompatibilityKind::Indeterminate &&
                   absent.revision_state() ==
                           SourcePackageCompatibilityDimensionState::
                                   Indeterminate &&
                   has_reason(
                           absent,
                           SourcePackageMismatchReason::
                                   SourceRevisionAbsent),
           "Absent revisions became a known match.");

    const SourcePackageCompatibilityEvaluation inapplicable =
            evaluate_source_package_compatibility(
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::inapplicable()),
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::inapplicable()));
    expect(inapplicable.kind() ==
                           SourcePackageCompatibilityKind::Indeterminate &&
                   inapplicable.revision_state() ==
                           SourcePackageCompatibilityDimensionState::
                                   Inapplicable &&
                   has_reason(
                           inapplicable,
                           SourcePackageMismatchReason::
                                   SourceRevisionInapplicable),
           "Inapplicable revisions became a generic exact match.");

    const SourcePackageCompatibilityEvaluation state_mismatch =
            evaluate_source_package_compatibility(
                    identity(),
                    identity(
                            aur_source(), "suite-base", "suite-child",
                            SourceRevisionIdentity::absent()));
    expect(state_mismatch.kind() ==
                           SourcePackageCompatibilityKind::SamePackageChild &&
                   has_reason(
                           state_mismatch,
                           SourcePackageMismatchReason::
                                   SourceRevisionStateMismatch) &&
                   has_reason(
                           state_mismatch,
                           SourcePackageMismatchReason::
                                   SourceRevisionAbsent),
           "Known/Absent revision mismatch was not typed.");
}

} // namespace

int main() {
    try {
        test_exact_match_and_version_representation();
        test_same_package_base_and_child();
        test_source_and_package_base_drift();
        test_unknown_unavailable_absent_and_inapplicable();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "source package compatibility tests: all checks passed\n";
    return 0;
}
