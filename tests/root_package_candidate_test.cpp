#include "root_package_candidate.hpp"

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

namespace {

static_assert(!std::is_default_constructible_v<RootPackageCandidate>);
static_assert(!std::is_aggregate_v<RootPackageCandidate>);
static_assert(std::is_copy_constructible_v<RootPackageCandidate>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidate, RootPackageIdentity,
        RootPackageCandidatePresentation>);
static_assert(!std::is_default_constructible_v<SelectedRootPackageTarget>);
static_assert(!std::is_aggregate_v<SelectedRootPackageTarget>);
static_assert(std::is_copy_constructible_v<SelectedRootPackageTarget>);
static_assert(
    !std::is_constructible_v<
        SelectedRootPackageTarget, RootPackageIdentity>);
static_assert(
    !std::is_default_constructible_v<
        RootPackageCandidateValidationResult>);
static_assert(!std::is_aggregate_v<RootPackageCandidateValidationResult>);
static_assert(
    std::is_copy_constructible_v<
        RootPackageCandidateValidationResult>);
static_assert(
    std::is_move_constructible_v<
        RootPackageCandidateValidationResult>);
static_assert(
    !std::is_copy_assignable_v<RootPackageCandidateValidationResult>);
static_assert(
    !std::is_move_assignable_v<RootPackageCandidateValidationResult>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidateValidationResult, RootPackageCandidate>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidateValidationResult,
        RootPackageCandidateValidationFailure>);
static_assert(
    std::is_same_v<
        decltype(std::declval<
                     const RootPackageCandidateValidationResult&>()
                     .candidate()),
        const RootPackageCandidate*>);
static_assert(
    std::is_same_v<
        decltype(std::declval<
                     const RootPackageCandidateValidationResult&>()
                     .failure()),
        const RootPackageCandidateValidationFailure*>);
static_assert(!std::is_default_constructible_v<RootPackageCandidatePairResult>);
static_assert(!std::is_aggregate_v<RootPackageCandidatePairResult>);
static_assert(std::is_copy_constructible_v<RootPackageCandidatePairResult>);
static_assert(std::is_move_constructible_v<RootPackageCandidatePairResult>);
static_assert(!std::is_copy_assignable_v<RootPackageCandidatePairResult>);
static_assert(!std::is_move_assignable_v<RootPackageCandidatePairResult>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidatePairResult,
        DistinctRootPackageCandidates>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidatePairResult,
        DuplicateRootPackageCandidate>);
static_assert(
    !std::is_constructible_v<
        RootPackageCandidatePairResult,
        InvalidRootPackageCandidatePair>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const RootPackageCandidatePairResult&>()
                     .distinct()),
        const DistinctRootPackageCandidates*>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const RootPackageCandidatePairResult&>()
                     .duplicate()),
        const DuplicateRootPackageCandidate*>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const RootPackageCandidatePairResult&>()
                     .invalid()),
        const InvalidRootPackageCandidatePair*>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const RootPackageCandidate&>().identity()),
        const RootPackageIdentity&>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const SelectedRootPackageTarget&>()
                     .identity()),
        const RootPackageIdentity&>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

const RootPackageCandidate& require_candidate(
    const RootPackageCandidateValidationResult& result,
    std::string_view context) {
    const std::string context_text(context);
    expect(result.is_valid(), context_text + " was not valid");
    expect(
        result.candidate() != nullptr,
        context_text + " lost its candidate");
    expect(
        result.failure() == nullptr,
        context_text + " also exposed a failure");
    return *result.candidate();
}

const RootPackageCandidateValidationFailure& require_failure(
    const RootPackageCandidateValidationResult& result,
    std::string_view context) {
    const std::string context_text(context);
    expect(!result.is_valid(), context_text + " was unexpectedly valid");
    expect(
        result.candidate() == nullptr,
        context_text + " exposed partial success");
    expect(
        result.failure() != nullptr,
        context_text + " lost its failure");
    return *result.failure();
}

RootPackageCandidate repository_candidate(
    std::string repository_name = "extra",
    std::string package_name = "sample-package",
    std::optional<std::string> version = std::nullopt,
    std::optional<std::string> description = std::nullopt) {
    RootPackageCandidateValidationResult result =
        make_repository_root_package_candidate(
            std::move(repository_name), std::move(package_name),
            std::move(version), std::move(description));
    return require_candidate(result, "repository candidate");
}

RootPackageCandidate aur_candidate(
    std::string package_name = "sample-package",
    std::string package_base = "sample-package",
    std::optional<std::string> version = std::nullopt,
    std::optional<std::string> description = std::nullopt) {
    RootPackageCandidateValidationResult result =
        make_aur_root_package_candidate(
            std::move(package_name), std::move(package_base),
            std::move(version), std::move(description));
    return require_candidate(result, "AUR candidate");
}

void expect_single_validation_issue(
    const RootPackageCandidateValidationResult& result,
    RootPackageSourceKind expected_source,
    RootPackageCandidateValidationIssueKind expected_kind,
    const std::string& expected_value,
    std::string_view context) {
    const std::string context_text(context);
    const RootPackageCandidateValidationFailure& failure =
        require_failure(result, context);
    expect(
        failure.source_kind == expected_source,
        context_text + " source differs");
    expect(
        failure.issues.size() == 1,
        context_text + " issue count differs");
    expect(
        failure.issues[0] == RootPackageCandidateValidationIssue{
                                 expected_kind, expected_value},
        context_text + " issue detail differs");
}

void test_repository_candidate_and_selected_target_preserve_identity() {
    RootPackageCandidateValidationResult result =
        make_repository_root_package_candidate(
            "aur", "same-name", "1.2.3-1", "Repository package");
    const RootPackageCandidate& candidate =
        require_candidate(result, "repository identity");

    expect(
        candidate.source_kind() == RootPackageSourceKind::Repository,
        "repository source kind differs");
    expect(
        candidate.target_role() == RootPackageTargetRole::Root,
        "repository candidate role differs");
    expect(candidate.package_name() == "same-name", "repository package differs");
    expect(
        candidate.presentation() == RootPackageCandidatePresentation{
                                        "1.2.3-1", "Repository package"},
        "repository presentation differs");

    const auto* identity =
        std::get_if<RepositoryRootPackageIdentity>(&candidate.identity());
    expect(identity != nullptr, "repository identity alternative differs");
    expect(identity->repository_name == "aur", "exact repository was lost");
    expect(identity->package_name == "same-name", "identity package differs");

    const SelectedRootPackageTarget selected =
        select_root_package_target(candidate);
    expect(
        selected.source_kind() == RootPackageSourceKind::Repository,
        "selected repository source differs");
    expect(
        selected.target_role() == RootPackageTargetRole::Root,
        "selected repository role differs");
    expect(
        selected.identity() == candidate.identity(),
        "selected repository identity was flattened");
    expect(selected.package_name() == "same-name", "selected package differs");
}

void test_aur_candidate_and_selected_target_preserve_package_base() {
    RootPackageCandidateValidationResult result =
        make_aur_root_package_candidate(
            "suite-child", "suite-base", "2.0-1", "AUR child");
    const RootPackageCandidate& candidate =
        require_candidate(result, "AUR identity");

    expect(
        candidate.source_kind() == RootPackageSourceKind::Aur,
        "AUR source kind differs");
    expect(
        candidate.target_role() == RootPackageTargetRole::Root,
        "AUR candidate role differs");
    const auto* identity =
        std::get_if<AurRootPackageIdentity>(&candidate.identity());
    expect(identity != nullptr, "AUR identity alternative differs");
    expect(identity->package_name == "suite-child", "AUR package differs");
    expect(identity->package_base == "suite-base", "PackageBase was lost");

    const SelectedRootPackageTarget selected =
        select_root_package_target(candidate);
    expect(
        selected.source_kind() == RootPackageSourceKind::Aur,
        "selected AUR source differs");
    expect(
        selected.identity() == candidate.identity(),
        "selected AUR identity was flattened");
}

void test_repository_named_aur_is_not_aur_source() {
    const RootPackageCandidate repository =
        repository_candidate("aur", "same-name");
    const RootPackageCandidate aur =
        aur_candidate("same-name", "same-name");

    expect(
        repository.source_kind() == RootPackageSourceKind::Repository,
        "repository named aur changed source kind");
    expect(
        aur.source_kind() == RootPackageSourceKind::Aur,
        "AUR candidate changed source kind");
    expect(
        !same_root_package_identity(
            repository.identity(), aur.identity()),
        "repository named aur was conflated with AUR");

    RootPackageCandidatePairResult pair =
        assess_root_package_candidate_pair(repository, aur);
    expect(pair.is_distinct(), "repository/AUR pair was not distinct");
    expect(pair.distinct() != nullptr, "distinct pair arm is unavailable");
    expect(pair.duplicate() == nullptr, "distinct pair exposed duplicate data");
    expect(pair.invalid() == nullptr, "distinct pair exposed invalid data");
}

void test_empty_presentation_values_are_canonical_absence() {
    RootPackageCandidateValidationResult result =
        make_repository_root_package_candidate(
            "extra", "sample-package", std::string{},
            std::string{});
    const RootPackageCandidate& candidate =
        require_candidate(result, "empty presentation normalization");
    expect(
        !candidate.presentation().version.has_value(),
        "empty version was not normalized to absence");
    expect(
        !candidate.presentation().description.has_value(),
        "empty description was not normalized to absence");
}

void test_valid_utf8_presentation_is_preserved() {
    RootPackageCandidateValidationResult result =
        make_aur_root_package_candidate(
            "sample-package", "sample-package", "1.0-1",
            "安全な説明");
    const RootPackageCandidate& candidate =
        require_candidate(result, "UTF-8 presentation");
    expect(
        candidate.presentation().description ==
            std::optional<std::string>{"安全な説明"},
        "valid UTF-8 description was changed");
}

void test_repository_validation_matches_configured_name_contract() {
    const std::vector<std::string> invalid_repository_names{
        "", "bad\nrepository", std::string("bad\x7f", 4)};
    for(const std::string& repository_name : invalid_repository_names) {
        expect_single_validation_issue(
            make_repository_root_package_candidate(
                repository_name, "sample-package"),
            RootPackageSourceKind::Repository,
            RootPackageCandidateValidationIssueKind::InvalidRepositoryName,
            repository_name, "invalid repository name");
    }

    const std::vector<std::string> opaque_repository_names{
        ".", "..", "nested/repository",
        std::string("configured\xc3\x28", 12)};
    for(const std::string& repository_name : opaque_repository_names) {
        RootPackageCandidateValidationResult result =
            make_repository_root_package_candidate(
                repository_name, "sample-package");
        const RootPackageCandidate& candidate =
            require_candidate(result, "configured repository name");
        const auto* identity = std::get_if<RepositoryRootPackageIdentity>(
            &candidate.identity());
        expect(identity != nullptr, "configured repository source differs");
        expect(
            identity->repository_name == repository_name,
            "configured repository name was changed");
    }
}

void test_package_and_package_base_use_package_identifier_contract() {
    const std::vector<std::string> invalid_package_names{
        "", ".", "..", "-leading", "bad/name", "bad name"};
    for(const std::string& package_name : invalid_package_names) {
        expect_single_validation_issue(
            make_repository_root_package_candidate(
                "extra", package_name),
            RootPackageSourceKind::Repository,
            RootPackageCandidateValidationIssueKind::InvalidPackageName,
            package_name, "invalid repository package name");
        expect_single_validation_issue(
            make_aur_root_package_candidate(
                package_name, "valid-base"),
            RootPackageSourceKind::Aur,
            RootPackageCandidateValidationIssueKind::InvalidPackageName,
            package_name, "invalid AUR package name");
        expect_single_validation_issue(
            make_aur_root_package_candidate(
                "valid-package", package_name),
            RootPackageSourceKind::Aur,
            RootPackageCandidateValidationIssueKind::InvalidPackageBase,
            package_name, "invalid AUR PackageBase");
    }
}

void test_validation_retains_all_invalid_fields_without_partial_candidate() {
    const std::string invalid_utf8("bad\xf4\x90\x80\x80", 7);
    RootPackageCandidateValidationResult result =
        make_aur_root_package_candidate(
            "bad package", "bad/base", "1\n2", invalid_utf8);
    const RootPackageCandidateValidationFailure& failure =
        require_failure(result, "multi-field AUR validation");

    expect(
        failure.source_kind == RootPackageSourceKind::Aur,
        "multi-field failure source differs");
    expect(
        failure.issues ==
            std::vector<RootPackageCandidateValidationIssue>{
                {RootPackageCandidateValidationIssueKind::
                     InvalidPackageName,
                 "bad package"},
                {RootPackageCandidateValidationIssueKind::
                     InvalidPackageBase,
                 "bad/base"},
                {RootPackageCandidateValidationIssueKind::
                     InvalidVersion,
                 "1\n2"},
                {RootPackageCandidateValidationIssueKind::
                     InvalidDescription,
                 invalid_utf8}},
        "multi-field validation did not retain every typed issue");
}

void test_presentation_rejects_controls_and_non_single_line_unicode() {
    const std::vector<std::string> invalid_values{
        "line\tvalue",
        std::string("\xc2\x85", 2),
        std::string("\xe2\x80\xa8", 3),
        std::string("\xe2\x80\xa9", 3),
        std::string("\xc0\xaf", 2),
        std::string("\xed\xa0\x80", 3),
        std::string("\x80", 1),
        std::string("\xc2", 1),
        std::string("\xf0\x90\x80", 3),
        std::string("\xf5\x80\x80\x80", 4)};
    for(const std::string& invalid : invalid_values) {
        expect_single_validation_issue(
            make_repository_root_package_candidate(
                "extra", "sample-package", invalid),
            RootPackageSourceKind::Repository,
            RootPackageCandidateValidationIssueKind::InvalidVersion,
            invalid, "invalid version text");
        expect_single_validation_issue(
            make_aur_root_package_candidate(
                "sample-package", "sample-package", std::nullopt,
                invalid),
            RootPackageSourceKind::Aur,
            RootPackageCandidateValidationIssueKind::InvalidDescription,
            invalid, "invalid description text");
    }
}

void test_presentation_accepts_utf8_boundary_values() {
    const std::vector<std::string> valid_values{
        std::string("\xc2\xa0", 2),
        std::string("\xf4\x8f\xbf\xbf", 4)};
    for(const std::string& value : valid_values) {
        RootPackageCandidateValidationResult result =
            make_aur_root_package_candidate(
                "sample-package", "sample-package", value, value);
        const RootPackageCandidate& candidate =
            require_candidate(result, "UTF-8 boundary presentation");
        expect(
            candidate.presentation() ==
                RootPackageCandidatePresentation{value, value},
            "valid UTF-8 boundary value was changed");
    }
}

void test_duplicate_identity_merges_compatible_metadata_symmetrically() {
    const RootPackageCandidate without_version = repository_candidate(
        "extra", "sample-package", std::nullopt, "Description");
    const RootPackageCandidate without_description = repository_candidate(
        "extra", "sample-package", "1.0-1", std::nullopt);

    expect(
        same_root_package_identity(
            without_version.identity(),
            without_description.identity()),
        "presentation metadata changed semantic identity");
    expect(
        without_version != without_description,
        "full candidate equality ignored presentation metadata");

    RootPackageCandidatePairResult forward =
        assess_root_package_candidate_pair(
            without_version, without_description);
    RootPackageCandidatePairResult reverse =
        assess_root_package_candidate_pair(
            without_description, without_version);
    for(const RootPackageCandidatePairResult* result : {&forward, &reverse}) {
        expect(result->is_duplicate(), "compatible pair was not duplicate");
        expect(result->distinct() == nullptr, "duplicate exposed distinct arm");
        expect(result->invalid() == nullptr, "duplicate exposed invalid arm");
        expect(result->duplicate() != nullptr, "duplicate data is unavailable");
        expect(
            result->duplicate()->candidate.presentation() ==
                RootPackageCandidatePresentation{
                    "1.0-1", "Description"},
            "compatible duplicate metadata was not completed");
    }
    expect(
        forward.duplicate()->candidate == reverse.duplicate()->candidate,
        "compatible duplicate merge depends on pair order");
}

void test_same_name_distinct_source_identities_remain_distinct() {
    const RootPackageCandidate extra =
        repository_candidate("extra", "same-name");
    const RootPackageCandidate core =
        repository_candidate("core", "same-name");
    const RootPackageCandidate aur =
        aur_candidate("same-name", "same-base");
    const RootPackageCandidate split_sibling =
        aur_candidate("sibling-name", "same-base");

    const std::vector<std::pair<RootPackageCandidate, RootPackageCandidate>>
        distinct_pairs{
            {extra, core},
            {extra, aur},
            {aur, split_sibling}};
    for(const auto& [lhs, rhs] : distinct_pairs) {
        RootPackageCandidatePairResult result =
            assess_root_package_candidate_pair(lhs, rhs);
        expect(result.is_distinct(), "valid identity pair was not distinct");
        expect(result.distinct() != nullptr, "distinct data is unavailable");
        expect(result.duplicate() == nullptr, "distinct pair was deduplicated");
        expect(result.invalid() == nullptr, "distinct pair was invalidated");
    }
}

void test_same_aur_name_with_different_package_base_is_invalid() {
    const RootPackageCandidate first =
        aur_candidate("same-name", "first-base");
    const RootPackageCandidate second =
        aur_candidate("same-name", "second-base");

    RootPackageCandidatePairResult result =
        assess_root_package_candidate_pair(first, second);
    expect(result.is_invalid(), "AUR PackageBase inconsistency was accepted");
    expect(result.distinct() == nullptr, "invalid pair exposed distinct data");
    expect(result.duplicate() == nullptr, "invalid pair exposed duplicate data");
    expect(result.invalid() != nullptr, "invalid pair detail is unavailable");
    expect(result.invalid()->issues.size() == 1, "PackageBase issue count differs");
    const auto* issue = std::get_if<InconsistentAurRootPackageBase>(
        &result.invalid()->issues[0]);
    expect(issue != nullptr, "PackageBase issue kind differs");
    expect(
        *issue == InconsistentAurRootPackageBase{
                      "same-name", "first-base", "second-base"},
        "PackageBase inconsistency detail differs");
}

void test_selected_and_exact_aur_duplicate_results_own_values() {
    const SelectedRootPackageTarget selected = [] {
        const RootPackageCandidate candidate =
            aur_candidate("owned-child", "owned-base");
        return select_root_package_target(candidate);
    }();
    const auto* selected_identity =
        std::get_if<AurRootPackageIdentity>(&selected.identity());
    expect(selected_identity != nullptr, "owned selected source differs");
    expect(
        *selected_identity ==
            AurRootPackageIdentity{"owned-child", "owned-base"},
        "selected target did not retain owned identity");

    RootPackageCandidatePairResult duplicate = [] {
        const RootPackageCandidate first = aur_candidate(
            "owned-child", "owned-base", "1.0-1", std::nullopt);
        const RootPackageCandidate second = aur_candidate(
            "owned-child", "owned-base", std::nullopt,
            "Owned description");
        return assess_root_package_candidate_pair(first, second);
    }();
    expect(duplicate.is_duplicate(), "exact AUR pair was not duplicate");
    expect(duplicate.duplicate() != nullptr, "owned duplicate is unavailable");
    expect(
        duplicate.duplicate()->candidate.identity() ==
            RootPackageIdentity{AurRootPackageIdentity{
                "owned-child", "owned-base"}},
        "duplicate result did not retain owned identity");
    expect(
        duplicate.duplicate()->candidate.presentation() ==
            RootPackageCandidatePresentation{
                "1.0-1", "Owned description"},
        "duplicate result did not retain owned presentation");
}

void test_duplicate_metadata_conflicts_retain_every_field() {
    const RootPackageCandidate first = repository_candidate(
        "extra", "same-name", "1.0-1", "First description");
    const RootPackageCandidate second = repository_candidate(
        "extra", "same-name", "2.0-1", "Second description");

    RootPackageCandidatePairResult result =
        assess_root_package_candidate_pair(first, second);
    expect(result.is_invalid(), "conflicting duplicate was accepted");
    expect(result.invalid() != nullptr, "metadata conflict detail is unavailable");
    expect(result.invalid()->issues.size() == 2, "metadata issue count differs");

    const auto* version =
        std::get_if<ConflictingRootPackageCandidateMetadata>(
            &result.invalid()->issues[0]);
    const auto* description =
        std::get_if<ConflictingRootPackageCandidateMetadata>(
            &result.invalid()->issues[1]);
    expect(version != nullptr, "version conflict kind differs");
    expect(description != nullptr, "description conflict kind differs");
    expect(
        version->identity == first.identity() &&
            version->field ==
                RootPackageCandidateMetadataField::Version &&
            version->first_value == "1.0-1" &&
            version->second_value == "2.0-1",
        "version conflict detail differs");
    expect(
        description->identity == first.identity() &&
            description->field ==
                RootPackageCandidateMetadataField::Description &&
            description->first_value == "First description" &&
            description->second_value == "Second description",
        "description conflict detail differs");
}

void test_selected_target_identity_ignores_presentation_metadata() {
    const RootPackageCandidate first =
        aur_candidate("same-name", "same-base", "1.0-1", "First");
    const RootPackageCandidate second =
        aur_candidate("same-name", "same-base", "2.0-1", "Second");

    expect(
        select_root_package_target(first) ==
            select_root_package_target(second),
        "selected target retained presentation metadata");
}

} // namespace

int main() {
    try {
        test_repository_candidate_and_selected_target_preserve_identity();
        test_aur_candidate_and_selected_target_preserve_package_base();
        test_repository_named_aur_is_not_aur_source();
        test_empty_presentation_values_are_canonical_absence();
        test_valid_utf8_presentation_is_preserved();
        test_repository_validation_matches_configured_name_contract();
        test_package_and_package_base_use_package_identifier_contract();
        test_validation_retains_all_invalid_fields_without_partial_candidate();
        test_presentation_rejects_controls_and_non_single_line_unicode();
        test_presentation_accepts_utf8_boundary_values();
        test_duplicate_identity_merges_compatible_metadata_symmetrically();
        test_same_name_distinct_source_identities_remain_distinct();
        test_same_aur_name_with_different_package_base_is_invalid();
        test_selected_and_exact_aur_duplicate_results_own_values();
        test_duplicate_metadata_conflicts_retain_every_field();
        test_selected_target_identity_ignores_presentation_metadata();
        std::cout << "root package candidate tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
