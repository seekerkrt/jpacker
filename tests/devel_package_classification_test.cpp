#include "devel_package_classification.hpp"

#include "devel_update_model.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

VcsSourceIdentity source(
        VcsKind kind,
        VcsSelector selector,
        const std::string& location = "https://example.invalid/source") {
    return VcsSourceIdentity::make(
            kind, location, std::move(selector));
}

void test_all_candidate_suffixes_and_false_positives() {
    const std::array<std::pair<std::string, VcsKind>, 6> candidates = {{
            {"foo-git", VcsKind::Git},
            {"foo-svn", VcsKind::Svn},
            {"foo-hg", VcsKind::Hg},
            {"foo-bzr", VcsKind::Bzr},
            {"foo-cvs", VcsKind::Cvs},
            {"foo-darcs", VcsKind::Darcs},
    }};
    for(const auto& [package_name, expected_kind] : candidates) {
        const std::optional<VcsKind> actual =
                devel_suffix_candidate_kind(package_name);
        require(actual.has_value() && *actual == expected_kind,
                "Candidate suffix kind differs for " + package_name + ".");
    }

    for(const std::string normal : {
                "normal-package",
                "foo-git-cli",
                "foogit",
                "foo-git2",
                "git"}) {
        require(!devel_suffix_candidate_kind(normal).has_value(),
                "Suffix-like package was a false positive: " + normal +
                        ".");
    }
}

void test_package_base_and_installed_child_evidence_stay_separate() {
    const DevelPackageSuffixEvidence base_candidate =
            DevelPackageSuffixEvidence::classify(
                    "foo-git", {"foo-git-doc"});
    require(base_candidate.package_base_candidate_kind() != nullptr &&
                    *base_candidate.package_base_candidate_kind() ==
                            VcsKind::Git &&
                    base_candidate.installed_children().size() == 1 &&
                    base_candidate.installed_children()[0].candidate_kind() ==
                            nullptr,
            "PackageBase evidence was projected from its child.");

    const DevelPackageSuffixEvidence ordinary_child =
            DevelPackageSuffixEvidence::classify(
                    "foo-git", {"foo-cli"});
    require(ordinary_child.package_base_candidate_kind() != nullptr &&
                    ordinary_child.installed_children()[0].candidate_kind() ==
                            nullptr,
            "Ordinary child changed PackageBase evidence.");

    const DevelPackageSuffixEvidence child_candidate =
            DevelPackageSuffixEvidence::classify("foo", {"foo-git"});
    require(child_candidate.package_base_candidate_kind() == nullptr &&
                    child_candidate.installed_children()[0].candidate_kind() !=
                            nullptr &&
                    *child_candidate.installed_children()[0]
                             .candidate_kind() == VcsKind::Git,
            "Installed child evidence promoted the PackageBase.");

    const DevelPackageSuffixEvidence siblings =
            DevelPackageSuffixEvidence::classify(
                    "foo-git",
                    {"foo-git", "foo-git-cli", "foo-git-doc"});
    require(siblings.installed_children().size() == 3 &&
                    siblings.installed_children()[0].package_name() ==
                            "foo-git" &&
                    siblings.installed_children()[0].candidate_kind() !=
                            nullptr &&
                    siblings.installed_children()[1].candidate_kind() ==
                            nullptr &&
                    siblings.installed_children()[2].candidate_kind() ==
                            nullptr,
            "Split sibling evidence was reordered or flattened.");
}

void test_suffix_only_is_candidate_not_confirmation() {
    const DevelPackageClassification candidate =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo-git", {"foo-git"}));
    const DevelPackageClassification normal =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo", {"foo-cli"}));

    require(candidate.evidence_level() ==
                            DevelEvidenceLevel::SuffixCandidate &&
                    candidate.source_form_disposition() ==
                            DevelSourceFormDisposition::RequiresCheck &&
                    candidate.trusted_metadata().empty() &&
                    candidate.successful_build_confirmations().empty(),
            "Suffix candidate became a confirmed source.");
    require(normal.evidence_level() == DevelEvidenceLevel::Normal &&
                    normal.source_form_disposition() ==
                            DevelSourceFormDisposition::NotApplicable,
            "Normal package became a devel candidate.");
}

void test_typed_confirmation_precedence_and_tracking_policy() {
    const VcsSourceIdentity floating_git = source(
            VcsKind::Git, VcsSelector::default_head());
    const DevelPackageClassification trusted_no_suffix =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "unusual-package", {"unusual-package"}),
                    {TrustedDevelSourceMetadata::make(floating_git)});
    require(trusted_no_suffix.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    trusted_no_suffix.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate &&
                    !trusted_no_suffix.suffix_evidence().has_candidate() &&
                    trusted_no_suffix.trusted_metadata().size() == 1,
            "Trusted no-suffix Git metadata was not confirmed.");

    const DevelPackageClassification trusted_over_suffix =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "misleading-cvs", {"misleading-cvs"}),
                    {TrustedDevelSourceMetadata::make(floating_git)});
    require(trusted_over_suffix.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    trusted_over_suffix.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate &&
                    trusted_over_suffix.suffix_evidence()
                                    .package_base_candidate_kind() != nullptr &&
                    *trusted_over_suffix.suffix_evidence()
                             .package_base_candidate_kind() == VcsKind::Cvs &&
                    trusted_over_suffix.trusted_metadata()[0]
                                    .source()
                                    .kind() == VcsKind::Git,
            "Suffix evidence overrode trusted source metadata.");

    const DevelPackageClassification fixed_git =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo-git", {"foo-git"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Git,
                            VcsSelector::fixed_revision(
                                    std::string(40, 'b'))))});
    require(fixed_git.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    fixed_git.source_form_disposition() ==
                            DevelSourceFormDisposition::Fixed &&
                    fixed_git.suffix_evidence().has_candidate(),
            "Fixed Git source became a floating update candidate.");

    const DevelPackageClassification unsupported =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo-cvs", {"foo-cvs"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Cvs,
                            VcsSelector::default_head()))});
    require(unsupported.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    unsupported.source_form_disposition() ==
                            DevelSourceFormDisposition::Unsupported,
            "Confirmed unsupported VCS was treated as supported.");

    const DevelPackageClassification check_required =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo-svn", {"foo-svn"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Svn,
                            VcsSelector::default_head()))});
    require(check_required.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    check_required.source_form_disposition() ==
                            DevelSourceFormDisposition::RequiresCheck,
            "Initial SVN policy was promoted to supported tracking.");

    const DevelPackageClassification unsupported_selector =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "foo-git", {"foo-git"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Git,
                            VcsSelector::unrecognized(
                                    "future-selector=value")))});
    require(unsupported_selector.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    unsupported_selector.source_form_disposition() ==
                            DevelSourceFormDisposition::RequiresCheck,
            "Unrecognized selector was promoted to supported tracking.");

    const DevelPackageClassification build_confirmation_only =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "renamed-package", {"renamed-package"}),
                    {},
                    {SuccessfulBuildSourceConfirmation::make(floating_git)});
    require(build_confirmation_only.evidence_level() ==
                            DevelEvidenceLevel::BuildSourceConfirmed &&
                    build_confirmation_only.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate &&
                    build_confirmation_only.trusted_metadata().empty() &&
                    build_confirmation_only.successful_build_confirmations()
                                    .size() == 1,
            "Successful-build source confirmation was not kept separate.");
}

void test_confirmation_authorities_and_multiple_sources_are_lossless() {
    const VcsSourceIdentity first = source(
            VcsKind::Git,
            VcsSelector::branch("main"),
            "https://example.invalid/first.git");
    const VcsSourceIdentity second = source(
            VcsKind::Git,
            VcsSelector::branch("stable"),
            "https://example.invalid/second.git");
    const DevelPackageClassification multiple =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "suite-git", {"suite-git-cli"}),
                    {TrustedDevelSourceMetadata::make(first)},
                    {SuccessfulBuildSourceConfirmation::make(second)});

    require(multiple.evidence_level() ==
                            DevelEvidenceLevel::BuildSourceConfirmed &&
                    multiple.source_form_disposition() ==
                            DevelSourceFormDisposition::RequiresCheck &&
                    multiple.trusted_metadata().size() == 1 &&
                    multiple.successful_build_confirmations().size() == 1 &&
                    multiple.trusted_metadata()[0].source() == first &&
                    multiple.successful_build_confirmations()[0].source() ==
                            second,
            "Confirmation authorities or multiple floating sources were flattened.");

    const DevelPackageClassification same_source_two_authorities =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "suite", {"suite-cli"}),
                    {TrustedDevelSourceMetadata::make(first)},
                    {SuccessfulBuildSourceConfirmation::make(first)});
    require(same_source_two_authorities.evidence_level() ==
                            DevelEvidenceLevel::BuildSourceConfirmed &&
                    same_source_two_authorities.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate &&
                    same_source_two_authorities.trusted_metadata().size() ==
                            1 &&
                    same_source_two_authorities
                                    .successful_build_confirmations()
                                    .size() == 1,
            "Same source from distinct authorities was discarded or double-counted.");
}

void test_trackable_source_form_is_not_an_update_assessment() {
    const DevelPackageClassification classification =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "unusual-package", {"unusual-package"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Git,
                            VcsSelector::default_head()))});
    const DevelUpdateAssessment no_baseline =
            DevelUpdateAssessment::requires_check(
                    DevelRequiresCheckReason::
                            NoAuthoritativeBuildProvenance);

    require(classification.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    classification.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate &&
                    no_baseline.state() ==
                            DevelUpdateAssessmentState::RequiresCheck &&
                    no_baseline.state() !=
                            DevelUpdateAssessmentState::UpToDate &&
                    no_baseline.requires_check_reason() != nullptr &&
                    *no_baseline.requires_check_reason() ==
                            DevelRequiresCheckReason::
                                    NoAuthoritativeBuildProvenance,
            "Trackable metadata implied an authoritative update assessment.");
}

void test_transport_and_architecture_do_not_gain_authority() {
    const DevelPackageClassification unresolved_transport =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "file-source", {"file-source"}),
                    {TrustedDevelSourceMetadata::make(source(
                            VcsKind::Git,
                            VcsSelector::default_head(),
                            "file:///tmp/unvalidated-upstream"))});
    require(unresolved_transport.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    unresolved_transport.source_form_disposition() ==
                            DevelSourceFormDisposition::TrackableCandidate,
            "Unvalidated transport gained production support authority.");

    const DevelPackageClassification unresolved_architecture =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "arch-source", {"arch-source"}),
                    {TrustedDevelSourceMetadata::make(
                            VcsSourceIdentity::make(
                                    VcsKind::Git,
                                    "git+https://example.invalid/source.git",
                                    VcsSelector::default_head(),
                                    "aarch64"))});
    require(unresolved_architecture.evidence_level() ==
                            DevelEvidenceLevel::SourceMetadataConfirmed &&
                    unresolved_architecture.source_form_disposition() ==
                            DevelSourceFormDisposition::RequiresCheck,
            "Architecture-qualified source bypassed resolution.");
}

void test_vcs_source_form_matrix_remains_conservative() {
    for(VcsKind kind : {VcsKind::Svn, VcsKind::Hg, VcsKind::Bzr}) {
        const DevelPackageClassification classification =
                DevelPackageClassification::classify(
                        DevelPackageSuffixEvidence::classify(
                                "check-source", {"check-source"}),
                        {TrustedDevelSourceMetadata::make(source(
                                kind, VcsSelector::default_head()))});
        require(classification.source_form_disposition() ==
                        DevelSourceFormDisposition::RequiresCheck,
                "Non-Git floating VCS became trackable automatically.");
    }

    for(VcsKind kind : {VcsKind::Cvs, VcsKind::Darcs}) {
        const DevelPackageClassification classification =
                DevelPackageClassification::classify(
                        DevelPackageSuffixEvidence::classify(
                                "unsupported-source",
                                {"unsupported-source"}),
                        {TrustedDevelSourceMetadata::make(source(
                                kind, VcsSelector::default_head()))});
        require(classification.source_form_disposition() ==
                        DevelSourceFormDisposition::Unsupported,
                "Unsupported VCS gained an automatic tracking contract.");
    }

    const DevelPackageClassification floating_with_fixed =
            DevelPackageClassification::classify(
                    DevelPackageSuffixEvidence::classify(
                            "mixed-source", {"mixed-source"}),
                    {TrustedDevelSourceMetadata::make(source(
                             VcsKind::Git,
                             VcsSelector::default_head(),
                             "https://example.invalid/floating.git")),
                     TrustedDevelSourceMetadata::make(source(
                             VcsKind::Git,
                             VcsSelector::tag("v1.0.0"),
                             "https://example.invalid/fixed.git"))});
    require(floating_with_fixed.source_form_disposition() ==
                    DevelSourceFormDisposition::TrackableCandidate,
            "One floating source plus fixed sources lost its disposition.");
}

} // namespace

void run_devel_package_classification_tests() {
    test_all_candidate_suffixes_and_false_positives();
    test_package_base_and_installed_child_evidence_stay_separate();
    test_suffix_only_is_candidate_not_confirmation();
    test_typed_confirmation_precedence_and_tracking_policy();
    test_confirmation_authorities_and_multiple_sources_are_lossless();
    test_trackable_source_form_is_not_an_update_assessment();
    test_transport_and_architecture_do_not_gain_authority();
    test_vcs_source_form_matrix_remains_conservative();
}
