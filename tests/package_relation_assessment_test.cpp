#include "package_relation_assessment.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

PackageRelationInstalledDatabaseIdentity installed_source() {
    return PackageRelationInstalledDatabaseIdentity{
        "/", "/var/lib/pacman"};
}

PackageRelationObservedCapability capability(
    const std::string& specification, ObservedVersionSource source) {
    const ProviderCapabilityParseResult parsed =
        parse_provider_capability(specification);
    expect(parsed.capability() != nullptr, "Capability fixture did not parse");
    const ProviderCapability value = *parsed.capability();
    return PackageRelationObservedCapability{
        value,
        value.version().has_value()
            ? ObservedVersion::available(source, *value.version())
            : ObservedVersion::unknown(
                  source,
                  ObservedVersionUnknownReason::
                      UnversionedProviderCapability)};
}

PackageRelationObservedPackage installed_package(
    std::string package_name, std::string version = "1",
    std::vector<PackageRelationObservedCapability> provides = {}) {
    return PackageRelationObservedPackage{
        std::move(package_name),
        std::nullopt,
        ObservedVersion::available(
            ObservedVersionSource::InstalledExactPackage,
            std::move(version)),
        std::move(provides),
        installed_source(),
        PackageRelationObservationRole::Installed,
        {}};
}

PackageRelationObservationSet complete_installed(
    std::vector<PackageRelationObservedPackage> packages = {}) {
    return PackageRelationObservationSet{
        PackageRelationObservationCompleteness::Complete,
        {installed_source()},
        {PackageRelationSourceIdentityCoverage{
            PackageRelationSourceIdentity{installed_source()},
            true,
            true}},
        std::move(packages),
        {}};
}

DeclaredPackageRelation declaration(
    const std::string& declaring_name,
    const std::string& declaring_base,
    PackageRelationKind kind,
    const std::string& specification) {
    const DeclaredPackageRelationParseResult parsed =
        parse_declared_package_relation(
            declaring_name, declaring_base, kind, specification);
    expect(parsed.relation() != nullptr, "Relation fixture did not parse");
    return *parsed.relation();
}

PlannedPackageRelationObservation planned_package(
    std::string package_name, std::string package_base,
    std::vector<PackageRelationObservedCapability> provides = {},
    std::vector<DeclaredPackageRelation> declarations = {},
    std::vector<PackageRelationRootAttribution> roots = {{0, "root"}}) {
    const std::string source_name = package_name;
    const std::string source_base = package_base;
    return PlannedPackageRelationObservation{
        PackageRelationObservedPackage{
            std::move(package_name),
            package_base,
            ObservedVersion::available(
                ObservedVersionSource::AurExactPackage, "1"),
            std::move(provides),
            PackageRelationAurSourceIdentity{
                source_name, source_base},
            PackageRelationObservationRole::PlannedTarget,
            std::move(roots)},
        std::move(declarations)};
}

PlannedPackageRelationObservation declaring_package(
    const std::string& package_name,
    const std::string& package_base,
    PackageRelationKind kind,
    const std::string& specification,
    std::vector<PackageRelationObservedCapability> provides = {},
    std::vector<PackageRelationRootAttribution> roots = {{0, "root"}}) {
    return planned_package(
        package_name, package_base, std::move(provides),
        {declaration(
            package_name, package_base, kind, specification)},
        std::move(roots));
}

std::size_t count_kind(
    const std::vector<PackageRelationAssessment>& assessments,
    PackageRelationAssessmentKind kind) {
    return static_cast<std::size_t>(std::count_if(
        assessments.begin(), assessments.end(),
        [kind](const auto& assessment) {
            return assessment.kind == kind;
        }));
}

const PackageRelationAssessment& require_kind(
    const std::vector<PackageRelationAssessment>& assessments,
    PackageRelationAssessmentKind kind,
    std::string_view context) {
    const auto found = std::find_if(
        assessments.begin(), assessments.end(),
        [kind](const auto& assessment) {
            return assessment.kind == kind;
        });
    expect(
        found != assessments.end(),
        std::string(context) + ": assessment kind missing");
    return *found;
}

void expect_single_structural_invalid(
    const std::vector<PackageRelationAssessment>& assessments,
    PackageRelationMatchInvalidReason reason,
    std::string_view context) {
    const PackageRelationAssessment& invalid = require_kind(
        assessments, PackageRelationAssessmentKind::Invalid, context);
    expect(
        assessments.size() == 1 &&
            invalid.attributed_package_evidence.has_value() &&
            invalid.attributed_package_evidence->identity_match ==
                PackageRelationIdentityMatchKind::InvalidInput &&
            invalid.attributed_package_evidence->version_match ==
                PackageRelationVersionMatchKind::Invalid &&
            invalid.attributed_package_evidence->invalid_reason ==
                std::optional<PackageRelationMatchInvalidReason>{
                    reason},
        std::string(context) +
            ": structural Invalid evidence was not preserved alone");
}

void test_conflict_exact_provided_and_multiple_sources() {
    PlannedPackageRelationObservation exact_declaring = declaring_package(
        "declaring", "declaring-base",
        PackageRelationKind::Conflict, "target>=2", {},
        {{0, "root-a"}, {1, "root-b"}});
    PlannedPackageRelationObservation planned_exact = planned_package(
        "target", "planned-target-base");
    planned_exact.package.package_version = ObservedVersion::available(
        ObservedVersionSource::AurExactPackage, "3");
    const auto exact = assess_package_relations(
        complete_installed({installed_package("target", "2")}),
        {exact_declaring, planned_exact});
    expect(
        count_kind(
            exact,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            count_kind(
                exact,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 1,
        "Installed and planned exact conflicts were flattened");
    const PackageRelationAssessment& planned_match = require_kind(
        exact,
        PackageRelationAssessmentKind::
            ConfirmedPlannedTargetConflict,
        "planned exact conflict");
    expect(
        planned_match.attributed_package_evidence.has_value() &&
            planned_match.attributed_package_evidence
                    ->observed_package.package_name ==
                "target" &&
            planned_match.declaring_package.roots ==
                std::vector<PackageRelationRootAttribution>{
                    {0, "root-a"}, {1, "root-b"}},
        "Planned conflict lost matched child or merged roots");

    const PlannedPackageRelationObservation provided_declaring =
        declaring_package(
            "provided-declaring", "provided-base",
            PackageRelationKind::Conflict, "virtual-api>=2");
    const PlannedPackageRelationObservation planned_provider =
        planned_package(
            "planned-provider", "planned-provider-base",
            {capability(
                "virtual-api=4",
                ObservedVersionSource::AurProviderCapability)});
    const auto provided = assess_package_relations(
        complete_installed({installed_package(
            "installed-provider", "9",
            {capability(
                "virtual-api=3",
                ObservedVersionSource::
                    InstalledProviderCapability)})}),
        {provided_declaring, planned_provider});
    expect(
        count_kind(
            provided,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            count_kind(
                provided,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 1,
        "Installed and planned provided conflicts were not retained");
    for(const auto& assessment : provided) {
        if(assessment.kind != PackageRelationAssessmentKind::
                                  ConfirmedInstalledConflict &&
           assessment.kind != PackageRelationAssessmentKind::
                                  ConfirmedPlannedTargetConflict) {
            continue;
        }
        expect(
            assessment.attributed_package_evidence.has_value() &&
                assessment.attributed_package_evidence
                        ->identity_match ==
                    PackageRelationIdentityMatchKind::
                        ProvidedComponent,
            "Provided conflict lost matched component evidence");
    }
}

void test_versioned_no_match_and_unavailable_provider() {
    const auto declaring = declaring_package(
        "versioned-root", "versioned-root",
        PackageRelationKind::Conflict, "target>=2");
    const auto no_match = assess_package_relations(
        complete_installed({installed_package("target", "1")}),
        {declaring});
    expect(
        no_match.size() == 1 &&
            no_match.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget,
        "Authoritative version nonmatch did not prove NoMatch");

    const auto provided_declaring = declaring_package(
        "provided-root", "provided-root",
        PackageRelationKind::Conflict, "virtual-api>=2");
    const auto unknown = assess_package_relations(
        complete_installed({installed_package(
            "provider", "4",
            {capability(
                "virtual-api",
                ObservedVersionSource::
                    InstalledProviderCapability)})}),
        {provided_declaring});
    const PackageRelationAssessment& unknown_assessment = require_kind(
        unknown, PackageRelationAssessmentKind::Unknown,
        "unversioned provide");
    expect(
        unknown_assessment.attributed_package_evidence.has_value() &&
            unknown_assessment.attributed_package_evidence
                    ->version_match ==
                PackageRelationVersionMatchKind::Unavailable,
        "Versioned relation against unversioned Provide lost Unknown evidence");
}

void test_self_filter_and_split_sibling_boundary() {
    const auto self = declaring_package(
        "foo-git", "foo-git", PackageRelationKind::Conflict, "foo",
        {capability(
            "foo", ObservedVersionSource::AurProviderCapability)});
    const auto self_only =
        assess_package_relations(complete_installed(), {self});
    expect(
        self_only.size() == 1 &&
            self_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget,
        "Declaring package's own Provides became a planned conflict");

    const auto installed = assess_package_relations(
        complete_installed({installed_package("foo")}), {self});
    expect(
        count_kind(
            installed,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1,
        "Self filtering removed a real installed conflict");

    const auto other_planned = assess_package_relations(
        complete_installed(), {self, planned_package("foo", "foo")});
    expect(
        count_kind(
            other_planned,
            PackageRelationAssessmentKind::
                ConfirmedPlannedTargetConflict) == 1,
        "Self filtering removed a distinct planned package");

    const auto split_declaring = declaring_package(
        "foo-git", "foo-suite",
        PackageRelationKind::Conflict, "foo");
    const auto split_sibling = assess_package_relations(
        complete_installed(),
        {split_declaring, planned_package("foo", "foo-suite")});
    const PackageRelationAssessment& split_match = require_kind(
        split_sibling,
        PackageRelationAssessmentKind::
            ConfirmedPlannedTargetConflict,
        "split sibling conflict");
    expect(
        split_match.attributed_package_evidence->observed_package
                .package_base ==
            std::optional<std::string>{"foo-suite"},
        "PackageBase-wide self filtering erased a split sibling");
}

void test_installed_old_self_filter_boundary() {
    const auto self = declaring_package(
        "foo-git", "foo-suite", PackageRelationKind::Conflict, "foo",
        {capability(
            "foo=2",
            ObservedVersionSource::AurProviderCapability)});
    const auto old_self = installed_package(
        "foo-git", "1",
        {capability(
            "foo=1",
            ObservedVersionSource::InstalledProviderCapability)});

    const auto old_self_only = assess_package_relations(
        complete_installed({old_self}), {self});
    expect(
        old_self_only.size() == 1 &&
            old_self_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget,
        "Installed old self became an active conflict target");

    const auto real_installed = assess_package_relations(
        complete_installed(
            {old_self, installed_package("foo")}),
        {self});
    const PackageRelationAssessment& installed_match = require_kind(
        real_installed,
        PackageRelationAssessmentKind::ConfirmedInstalledConflict,
        "installed conflict beside old self");
    expect(
        count_kind(
            real_installed,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            installed_match.attributed_package_evidence.has_value() &&
            installed_match.attributed_package_evidence
                    ->observed_package.package_name ==
                "foo",
        "Old-self filtering removed or misattributed a real installed conflict");

    const auto real_installed_provider = assess_package_relations(
        complete_installed(
            {old_self,
             installed_package(
                 "foo-provider", "1",
                 {capability(
                     "foo=1",
                     ObservedVersionSource::
                         InstalledProviderCapability)})}),
        {self});
    const PackageRelationAssessment& installed_provider_match = require_kind(
        real_installed_provider,
        PackageRelationAssessmentKind::ConfirmedInstalledConflict,
        "installed provider beside old self");
    expect(
        count_kind(
            real_installed_provider,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            installed_provider_match.attributed_package_evidence
                .has_value() &&
            installed_provider_match.attributed_package_evidence
                    ->observed_package.package_name ==
                "foo-provider",
        "Old-self filtering removed a distinct installed provider");

    const auto real_planned = assess_package_relations(
        complete_installed({old_self}),
        {self, planned_package("foo", "foo")});
    const PackageRelationAssessment& planned_match = require_kind(
        real_planned,
        PackageRelationAssessmentKind::ConfirmedPlannedTargetConflict,
        "planned conflict beside old self");
    expect(
        count_kind(
            real_planned,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 0 &&
            count_kind(
                real_planned,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 1 &&
            planned_match.attributed_package_evidence.has_value() &&
            planned_match.attributed_package_evidence
                    ->observed_package.package_name ==
                "foo",
        "Old-self filtering removed a distinct planned conflict");

    const auto split_sibling = assess_package_relations(
        complete_installed({old_self}),
        {self,
         planned_package(
             "foo-git-tools", "foo-suite",
             {capability(
                 "foo=3",
                 ObservedVersionSource::
                     AurProviderCapability)})});
    const PackageRelationAssessment& sibling_match = require_kind(
        split_sibling,
        PackageRelationAssessmentKind::ConfirmedPlannedTargetConflict,
        "split sibling beside old self");
    expect(
        sibling_match.attributed_package_evidence.has_value() &&
            sibling_match.attributed_package_evidence
                    ->observed_package.package_name ==
                "foo-git-tools" &&
            sibling_match.attributed_package_evidence
                    ->observed_package.package_base ==
                std::optional<std::string>{"foo-suite"},
        "Old-self filtering treated the PackageBase as self");

    const auto versioned_self = declaring_package(
        "foo-git", "foo-suite", PackageRelationKind::Conflict,
        "foo>=1",
        {capability(
            "foo=2",
            ObservedVersionSource::AurProviderCapability)});
    const auto versioned_old_self_only = assess_package_relations(
        complete_installed({old_self}), {versioned_self});
    expect(
        versioned_old_self_only.size() == 1 &&
            versioned_old_self_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget,
        "Installed old self reached versioned provider matching");

    const auto unversioned_old_self = installed_package(
        "foo-git", "1",
        {capability(
            "foo",
            ObservedVersionSource::InstalledProviderCapability)});
    const auto unversioned_old_self_only = assess_package_relations(
        complete_installed({unversioned_old_self}), {versioned_self});
    expect(
        unversioned_old_self_only.size() == 1 &&
            unversioned_old_self_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget &&
            count_kind(
                unversioned_old_self_only,
                PackageRelationAssessmentKind::Unknown) == 0 &&
            count_kind(
                unversioned_old_self_only,
                PackageRelationAssessmentKind::Invalid) == 0,
        "Installed old self restored unavailable comparison evidence");

    const auto replacement_self = declaring_package(
        "foo-git", "foo-suite", PackageRelationKind::Replacement,
        "foo",
        {capability(
            "foo=2",
            ObservedVersionSource::AurProviderCapability)});
    const auto replacement_old_self_only = assess_package_relations(
        complete_installed({old_self}), {replacement_self});
    expect(
        replacement_old_self_only.size() == 1 &&
            replacement_old_self_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget &&
            count_kind(
                replacement_old_self_only,
                PackageRelationAssessmentKind::
                    PotentialReplacement) == 0,
        "Installed old self became a potential replacement target");

    PackageRelationObservedPackage invalid_source_old_self = old_self;
    invalid_source_old_self.package_base = "foo-suite";
    invalid_source_old_self.package_version = ObservedVersion::available(
        ObservedVersionSource::AurExactPackage, "1");
    invalid_source_old_self.source = PackageRelationAurSourceIdentity{
        "foo-git", "foo-suite"};
    const auto invalid_source_result = assess_package_relations(
        complete_installed({invalid_source_old_self}), {self});
    expect_single_structural_invalid(
        invalid_source_result,
        PackageRelationMatchInvalidReason::SourceRoleMismatch,
        "invalid installed old-self source role");

    PackageRelationObservedPackage invalid_capability_old_self = old_self;
    invalid_capability_old_self.provides.front().observed_version =
        ObservedVersion::available(
            ObservedVersionSource::AurProviderCapability, "1");
    const auto invalid_capability_result = assess_package_relations(
        complete_installed({invalid_capability_old_self}), {self});
    expect_single_structural_invalid(
        invalid_capability_result,
        PackageRelationMatchInvalidReason::VersionSourceMismatch,
        "invalid installed old-self capability authority");

    const auto invalid_with_installed_conflict = assess_package_relations(
        complete_installed(
            {invalid_source_old_self, installed_package("foo")}),
        {self});
    expect(
        invalid_with_installed_conflict.size() == 2 &&
            count_kind(
                invalid_with_installed_conflict,
                PackageRelationAssessmentKind::Invalid) == 1 &&
            count_kind(
                invalid_with_installed_conflict,
                PackageRelationAssessmentKind::
                    ConfirmedInstalledConflict) == 1,
        "Installed conflict and filtered old-self Invalid were flattened");

    const auto invalid_with_planned_conflict = assess_package_relations(
        complete_installed({invalid_source_old_self}),
        {self, planned_package("foo", "foo")});
    expect(
        invalid_with_planned_conflict.size() == 2 &&
            count_kind(
                invalid_with_planned_conflict,
                PackageRelationAssessmentKind::Invalid) == 1 &&
            count_kind(
                invalid_with_planned_conflict,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 1,
        "Planned conflict and filtered old-self Invalid were flattened");

    const auto invalid_replacement_result = assess_package_relations(
        complete_installed({invalid_source_old_self}),
        {replacement_self});
    expect_single_structural_invalid(
        invalid_replacement_result,
        PackageRelationMatchInvalidReason::SourceRoleMismatch,
        "invalid installed old-self replacement source role");

    const auto invalid_with_replacement = assess_package_relations(
        complete_installed(
            {invalid_source_old_self, installed_package("foo")}),
        {replacement_self});
    expect(
        invalid_with_replacement.size() == 2 &&
            count_kind(
                invalid_with_replacement,
                PackageRelationAssessmentKind::Invalid) == 1 &&
            count_kind(
                invalid_with_replacement,
                PackageRelationAssessmentKind::
                    PotentialReplacement) == 1,
        "Potential replacement and filtered old-self Invalid were flattened");

    PackageRelationObservationSet partial =
        complete_installed({old_self});
    partial.completeness = PackageRelationObservationCompleteness::Partial;
    partial.failures.push_back(PackageRelationObservationFailure{
        PackageRelationObservationFailureKind::PartialSourceFailure,
        PackageRelationObservationRole::Installed,
        PackageRelationSourceIdentity{installed_source()},
        std::nullopt,
        "partial installed database"});
    const auto partial_result = assess_package_relations(partial, {self});
    expect(
        count_kind(
            partial_result,
            PackageRelationAssessmentKind::Unknown) == 1 &&
            count_kind(
                partial_result,
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget) ==
                0,
        "Old-self filtering erased incomplete observation evidence");

    PackageRelationObservationSet invalid =
        complete_installed({old_self});
    invalid.completeness = PackageRelationObservationCompleteness::Invalid;
    invalid.failures.push_back(PackageRelationObservationFailure{
        PackageRelationObservationFailureKind::MalformedMetadata,
        PackageRelationObservationRole::Installed,
        PackageRelationSourceIdentity{installed_source()},
        std::optional<std::string>{"broken-package"},
        "malformed installed database"});
    const auto invalid_result = assess_package_relations(invalid, {self});
    expect(
        count_kind(
            invalid_result,
            PackageRelationAssessmentKind::Invalid) == 1 &&
            count_kind(
                invalid_result,
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget) ==
                0,
        "Old-self filtering erased invalid observation evidence");
}

PackageRelationObservationSet repository_context(
    std::vector<PackageRelationObservedPackage> packages) {
    PackageRelationSourceIdentity source =
        ConfiguredRepositoryIdentity{"core", 0};
    return PackageRelationObservationSet{
        PackageRelationObservationCompleteness::Complete,
        {source},
        {PackageRelationSourceIdentityCoverage{source, true, true}},
        std::move(packages),
        {}};
}

PackageRelationObservedPackage repository_candidate(
    std::string package_name,
    std::vector<PackageRelationObservedCapability> provides = {}) {
    const std::string package_base = package_name;
    return PackageRelationObservedPackage{
        package_name,
        package_base,
        ObservedVersion::available(
            ObservedVersionSource::RepositoryExactPackage, "3"),
        std::move(provides),
        ConfiguredRepositoryIdentity{"core", 0},
        PackageRelationObservationRole::RepositoryCandidate,
        {}};
}

void test_replacement_is_potential_and_repository_only_is_context() {
    const auto declaring = declaring_package(
        "replacement-root", "replacement-root",
        PackageRelationKind::Replacement, "legacy-api>=2");
    const auto planned = planned_package(
        "planned-legacy-provider", "planned-legacy-provider",
        {capability(
            "legacy-api=4",
            ObservedVersionSource::AurProviderCapability)});
    const auto active = assess_package_relations(
        complete_installed(
            {installed_package("legacy-api", "2"),
             installed_package(
                 "installed-legacy-provider", "9",
                 {capability(
                     "legacy-api=3",
                     ObservedVersionSource::
                         InstalledProviderCapability)})}),
        {declaring, planned});
    expect(
        count_kind(
            active,
            PackageRelationAssessmentKind::PotentialReplacement) ==
                3 &&
            count_kind(
                active,
                PackageRelationAssessmentKind::
                    ConfirmedInstalledConflict) == 0 &&
            count_kind(
                active,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 0,
        "Exact/provided replacement matches were flattened or misclassified");

    PlannedPackageRelationObservation planned_exact_target =
        planned_package("legacy-api", "legacy-api-base");
    planned_exact_target.package.package_version = ObservedVersion::available(
        ObservedVersionSource::AurExactPackage, "3");
    const auto planned_exact = assess_package_relations(
        complete_installed(), {declaring, planned_exact_target});
    const PackageRelationAssessment& planned_exact_assessment = require_kind(
        planned_exact,
        PackageRelationAssessmentKind::PotentialReplacement,
        "planned exact replacement");
    expect(
        count_kind(
            planned_exact,
            PackageRelationAssessmentKind::PotentialReplacement) ==
                1 &&
            planned_exact_assessment.attributed_package_evidence
                .has_value() &&
            planned_exact_assessment.attributed_package_evidence
                    ->observed_package.role ==
                PackageRelationObservationRole::PlannedTarget,
        "Planned exact replacement was not retained as potential impact");

    const PackageRelationObservationSet context = repository_context(
        {repository_candidate(
            "repository-provider",
            {capability(
                "legacy-api=5",
                ObservedVersionSource::
                    RepositoryProviderCapability)})});
    const auto repository_only = assess_package_relations(
        complete_installed(), {declaring}, context);
    expect(
        repository_only.size() == 1 &&
            repository_only.front().kind ==
                PackageRelationAssessmentKind::
                    ConfirmedNoMatchingCurrentOrPlannedTarget &&
            repository_only.front().repository_context_evidence.has_value() &&
            package_relation_has_confirmed_match(
                *repository_only.front()
                     .repository_context_evidence),
        "Repository candidate alone became an active replacement");
}

PackageRelationObservationSet failed_installed(
    PackageRelationObservationFailureKind failure_kind,
    PackageRelationObservationCompleteness completeness) {
    return PackageRelationObservationSet{
        completeness,
        {installed_source()},
        {PackageRelationSourceIdentityCoverage{
            PackageRelationSourceIdentity{installed_source()},
            false,
            false}},
        {},
        {PackageRelationObservationFailure{
            failure_kind,
            PackageRelationObservationRole::Installed,
            PackageRelationSourceIdentity{installed_source()},
            std::nullopt,
            "installed observation failure"}}};
}

void test_unknown_declared_and_invalid_boundaries() {
    const auto declaring = declaring_package(
        "unknown-root", "unknown-root",
        PackageRelationKind::Conflict, "target");
    const auto query_failure = assess_package_relations(
        failed_installed(
            PackageRelationObservationFailureKind::SourceUnavailable,
            PackageRelationObservationCompleteness::Unavailable),
        {declaring});
    expect(
        count_kind(
            query_failure,
            PackageRelationAssessmentKind::Unknown) == 1 &&
            query_failure.front().attributed_observation_failure.has_value(),
        "Installed inventory failure was treated as empty inventory");

    PackageRelationObservationSet incomplete = complete_installed();
    incomplete.source_identity_coverage.front()
        .provided_component_identity = false;
    const auto incomplete_result =
        assess_package_relations(incomplete, {declaring});
    expect(
        count_kind(
            incomplete_result,
            PackageRelationAssessmentKind::Unknown) == 1,
        "Incomplete identity coverage proved NoMatch");

    const PackageRelationObservationSet no_authority;
    const auto declared =
        assess_package_relations(no_authority, {declaring});
    expect(
        declared.size() == 1 &&
            declared.front().kind ==
                PackageRelationAssessmentKind::DeclaredRelation,
        "Missing observation authority did not retain DeclaredRelation fallback");

    PackageRelationObservedPackage invalid_candidate =
        installed_package("invalid-candidate");
    invalid_candidate.role = PackageRelationObservationRole::PlannedTarget;
    const auto invalid = assess_package_relations(
        complete_installed({invalid_candidate}), {declaring});
    expect(
        count_kind(
            invalid,
            PackageRelationAssessmentKind::Invalid) == 1,
        "Invalid observation was flattened into Unknown or absence");

    const auto malformed = assess_package_relations(
        failed_installed(
            PackageRelationObservationFailureKind::MalformedMetadata,
            PackageRelationObservationCompleteness::Invalid),
        {declaring});
    expect(
        count_kind(
            malformed,
            PackageRelationAssessmentKind::Invalid) >= 1 &&
            count_kind(
                malformed,
                PackageRelationAssessmentKind::Unknown) == 0,
        "Malformed metadata was not classified Invalid");

    PlannedPackageRelationObservation invalid_planned = declaring;
    invalid_planned.package.role =
        PackageRelationObservationRole::RepositoryCandidate;
    invalid_planned.package.source =
        ConfiguredRepositoryIdentity{"core", 0};
    const auto invalid_planned_result = assess_package_relations(
        complete_installed(), {invalid_planned});
    expect(
        count_kind(
            invalid_planned_result,
            PackageRelationAssessmentKind::Invalid) >= 1 &&
            count_kind(
                invalid_planned_result,
                PackageRelationAssessmentKind::
                    ConfirmedPlannedTargetConflict) == 0,
        "RepositoryCandidate role became an active planned conflict");
}

void test_confirmed_matches_coexist_with_failure_evidence() {
    const auto conflict = declaring_package(
        "conflict-root", "conflict-root",
        PackageRelationKind::Conflict, "target");
    PackageRelationObservationSet partial = complete_installed(
        {installed_package("target")});
    partial.completeness = PackageRelationObservationCompleteness::Partial;
    partial.failures.push_back(PackageRelationObservationFailure{
        PackageRelationObservationFailureKind::PartialSourceFailure,
        PackageRelationObservationRole::Installed,
        PackageRelationSourceIdentity{installed_source()},
        std::nullopt,
        "partial installed database"});
    const auto conflict_result =
        assess_package_relations(partial, {conflict});
    expect(
        count_kind(
            conflict_result,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            count_kind(
                conflict_result,
                PackageRelationAssessmentKind::Unknown) == 1,
        "Confirmed conflict and partial failure were flattened");

    const auto replacement = declaring_package(
        "replacement-root", "replacement-root",
        PackageRelationKind::Replacement, "target");
    const auto replacement_result =
        assess_package_relations(partial, {replacement});
    expect(
        count_kind(
            replacement_result,
            PackageRelationAssessmentKind::PotentialReplacement) ==
                1 &&
            count_kind(
                replacement_result,
                PackageRelationAssessmentKind::Unknown) == 1,
        "Potential replacement and source failure were flattened");

    const PackageRelationObservationSet no_installed_authority;
    const auto planned_conflict_result = assess_package_relations(
        no_installed_authority,
        {conflict, planned_package("target", "target-base")});
    expect(
        count_kind(
            planned_conflict_result,
            PackageRelationAssessmentKind::
                ConfirmedPlannedTargetConflict) == 1 &&
            count_kind(
                planned_conflict_result,
                PackageRelationAssessmentKind::Unknown) == 1,
        "Planned conflict erased missing installed authority");

    const PackageRelationObservedCapability matched = capability(
        "virtual=3",
        ObservedVersionSource::InstalledProviderCapability);
    const PackageRelationObservedCapability invalid{
        ProviderCapability("virtual=invalid", "virtual", "invalid"),
        ObservedVersion::invalid(
            ObservedVersionSource::InstalledProviderCapability,
            ConstraintInvalidReason::InvalidVersionIdentity)};
    const auto duplicate_declaring = declaring_package(
        "duplicate-root", "duplicate-root",
        PackageRelationKind::Conflict, "virtual>=2");
    const auto mixed = assess_package_relations(
        complete_installed({installed_package(
            "provider", "9", {matched, invalid})}),
        {duplicate_declaring});
    expect(
        count_kind(
            mixed,
            PackageRelationAssessmentKind::
                ConfirmedInstalledConflict) == 1 &&
            count_kind(
                mixed,
                PackageRelationAssessmentKind::Invalid) == 1,
        "Confirmed Provide and Invalid duplicate evidence were flattened");
}

} // namespace

int main() {
    try {
        test_conflict_exact_provided_and_multiple_sources();
        test_versioned_no_match_and_unavailable_provider();
        test_self_filter_and_split_sibling_boundary();
        test_installed_old_self_filter_boundary();
        test_replacement_is_potential_and_repository_only_is_context();
        test_unknown_declared_and_invalid_boundaries();
        test_confirmed_matches_coexist_with_failure_evidence();
        std::cout << "package relation assessment tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "package relation assessment tests: "
                  << error.what() << '\n';
        return 1;
    }
}
