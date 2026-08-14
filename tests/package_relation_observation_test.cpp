#include "package_relation_observation.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

DeclaredPackageRelation relation(
        PackageRelationKind kind, const std::string& specification) {
    const DeclaredPackageRelationParseResult result =
            parse_declared_package_relation(
                    "declaring-child", "declaring-base", kind,
                    specification);
    expect(result.relation() != nullptr, "Relation fixture did not parse");
    return *result.relation();
}

PackageRelationInstalledDatabaseIdentity installed_source() {
    return PackageRelationInstalledDatabaseIdentity{
            "/", "/var/lib/pacman"};
}

PackageRelationRootAttribution root() {
    return PackageRelationRootAttribution{0, "planned-child"};
}

PackageRelationObservedCapability capability(
        std::string specification,
        ObservedVersionSource source) {
    const ProviderCapabilityParseResult parsed =
            parse_provider_capability(specification);
    expect(parsed.capability() != nullptr, "Capability fixture did not parse");
    const ProviderCapability value = *parsed.capability();
    return PackageRelationObservedCapability{
            value,
            value.version().has_value()
                    ? ObservedVersion::available(
                              source, value.version().value())
                    : ObservedVersion::unknown(
                              source,
                              ObservedVersionUnknownReason::
                                      UnversionedProviderCapability)};
}

PackageRelationObservedPackage installed_package(
        std::string package_name = "real-package",
        std::string package_version = "2.0",
        std::vector<PackageRelationObservedCapability> provides = {}) {
    return PackageRelationObservedPackage{
            std::move(package_name),
            std::nullopt,
            ObservedVersion::available(
                    ObservedVersionSource::InstalledExactPackage,
                    std::move(package_version)),
            std::move(provides),
            installed_source(),
            PackageRelationObservationRole::Installed,
            {}};
}

PackageRelationObservedPackage planned_package(
        std::string package_name = "planned-child",
        std::string package_version = "4.0",
        std::vector<PackageRelationObservedCapability> provides = {}) {
    const std::string package_base = "planned-base";
    return PackageRelationObservedPackage{
            package_name,
            package_base,
            ObservedVersion::available(
                    ObservedVersionSource::AurExactPackage,
                    std::move(package_version)),
            std::move(provides),
            PackageRelationAurSourceIdentity{
                    package_name, package_base},
            PackageRelationObservationRole::PlannedTarget,
            {root()}};
}

void expect_match(
        const PackageRelationMatchEvidence& evidence,
        PackageRelationIdentityMatchKind identity,
        PackageRelationVersionMatchKind version,
        const std::string& context) {
    expect(
            evidence.identity_match == identity &&
                    evidence.version_match == version &&
                    !evidence.invalid_reason.has_value(),
            context + ": matching evidence differs");
}

void test_exact_and_provided_identity_matching() {
    const PackageRelationObservedPackage installed = installed_package(
            "real-package", "2.0",
            {capability(
                     "virtual-api=3",
                     ObservedVersionSource::
                             InstalledProviderCapability),
             capability(
                     "other-api",
                     ObservedVersionSource::
                             InstalledProviderCapability)});
    expect_match(
            match_declared_package_relation(
                    relation(PackageRelationKind::Conflict, "real-package"),
                    installed),
            PackageRelationIdentityMatchKind::ExactPackage,
            PackageRelationVersionMatchKind::Unconstrained,
            "unversioned exact installed conflict");
    expect_match(
            match_declared_package_relation(
                    relation(PackageRelationKind::Conflict, "virtual-api"),
                    installed),
            PackageRelationIdentityMatchKind::ProvidedComponent,
            PackageRelationVersionMatchKind::Unconstrained,
            "unversioned provided installed conflict");
    expect_match(
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Replacement,
                            "real-package>=2"),
                    installed),
            PackageRelationIdentityMatchKind::ExactPackage,
            PackageRelationVersionMatchKind::Matched,
            "versioned exact installed replacement");
    const PackageRelationMatchEvidence provided_replacement =
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Replacement,
                            "virtual-api>=3"),
                    installed);
    expect_match(
            provided_replacement,
            PackageRelationIdentityMatchKind::ProvidedComponent,
            PackageRelationVersionMatchKind::Matched,
            "versioned provided installed replacement");
    expect(
            provided_replacement.provided_capability_evidence ==
                    std::vector<
                            PackageRelationProvidedCapabilityMatchEvidence>{
                            {0, PackageRelationVersionMatchKind::Matched}},
            "Provided component evidence was not retained");
}

void test_planned_exact_and_provided_matching() {
    const PackageRelationObservedPackage planned = planned_package(
            "planned-child", "4.0",
            {capability(
                    "planned-virtual=7",
                    ObservedVersionSource::AurProviderCapability)});
    expect_match(
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Conflict,
                            "planned-child>=4"),
                    planned),
            PackageRelationIdentityMatchKind::ExactPackage,
            PackageRelationVersionMatchKind::Matched,
            "planned exact conflict");
    expect_match(
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Conflict,
                            "planned-virtual=7"),
                    planned),
            PackageRelationIdentityMatchKind::ProvidedComponent,
            PackageRelationVersionMatchKind::Matched,
            "planned provided conflict");
}

void test_version_outcomes_do_not_flatten_unknown() {
    const PackageRelationObservedPackage installed = installed_package(
            "real-package", "2.0",
            {capability(
                    "virtual-api",
                    ObservedVersionSource::
                            InstalledProviderCapability)});
    expect_match(
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Conflict,
                            "real-package>3"),
                    installed),
            PackageRelationIdentityMatchKind::ExactPackage,
            PackageRelationVersionMatchKind::NotMatched,
            "unsatisfied exact version");
    expect_match(
            match_declared_package_relation(
                    relation(
                            PackageRelationKind::Conflict,
                            "virtual-api>=1"),
                    installed),
            PackageRelationIdentityMatchKind::ProvidedComponent,
            PackageRelationVersionMatchKind::Unavailable,
            "versioned relation against unversioned capability");
    expect_match(
            match_declared_package_relation(
                    relation(PackageRelationKind::Conflict, "unrelated"),
                    installed),
            PackageRelationIdentityMatchKind::NoIdentityMatch,
            PackageRelationVersionMatchKind::NotApplicable,
            "unrelated package and provides");
}

PackageRelationObservationSet complete_installed_set(
        std::vector<PackageRelationObservedPackage> packages) {
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

bool has_capability_version_result(
        const PackageRelationMatchEvidence& evidence,
        PackageRelationVersionMatchKind result) {
    return std::any_of(
            evidence.provided_capability_evidence.begin(),
            evidence.provided_capability_evidence.end(),
            [result](const auto& capability_evidence) {
                return capability_evidence.version_match == result;
            });
}

PackageRelationMatchingEvidence match_complete_installed_package(
        const DeclaredPackageRelation& declaration,
        PackageRelationObservedPackage package) {
    return match_declared_package_relation(
            declaration,
            complete_installed_set({std::move(package)}));
}

void expect_duplicate_provide_outcome(
        const DeclaredPackageRelation& declaration,
        std::vector<PackageRelationObservedCapability> provides,
        PackageRelationVersionMatchKind expected_version,
        bool expected_match,
        bool expected_no_match,
        const std::string& context) {
    const PackageRelationMatchingEvidence evidence =
            match_complete_installed_package(
                    declaration,
                    installed_package(
                            "provider-package", "9", std::move(provides)));
    expect(
            evidence.package_evidence.size() == 1 &&
                    evidence.package_evidence.front().identity_match ==
                            PackageRelationIdentityMatchKind::
                                    ProvidedComponent &&
                    evidence.package_evidence.front().version_match ==
                            expected_version &&
                    package_relation_has_confirmed_match(evidence) ==
                            expected_match &&
                    package_relation_confirms_no_match(evidence) ==
                            expected_no_match,
            context + ": duplicate Provide aggregate differs");
}

void test_duplicate_same_name_provides_are_order_independent() {
    const DeclaredPackageRelation declaration = relation(
            PackageRelationKind::Conflict, "virtual-api>=2");
    const PackageRelationObservedCapability version_one = capability(
            "virtual-api=1",
            ObservedVersionSource::InstalledProviderCapability);
    const PackageRelationObservedCapability version_three = capability(
            "virtual-api=3",
            ObservedVersionSource::InstalledProviderCapability);
    const PackageRelationObservedCapability unversioned = capability(
            "virtual-api",
            ObservedVersionSource::InstalledProviderCapability);

    expect_duplicate_provide_outcome(
            declaration, {version_one, version_three},
            PackageRelationVersionMatchKind::Matched, true, false,
            "NotMatched then Matched");
    expect_duplicate_provide_outcome(
            declaration, {version_three, version_one},
            PackageRelationVersionMatchKind::Matched, true, false,
            "Matched then NotMatched");

    for(const auto& provides :
        std::vector<std::vector<PackageRelationObservedCapability>>{
                {version_one, unversioned},
                {unversioned, version_one}}) {
        const PackageRelationMatchingEvidence evidence =
                match_complete_installed_package(
                        declaration,
                        installed_package(
                                "provider-package", "9", provides));
        expect(
                !package_relation_has_confirmed_match(evidence) &&
                        !package_relation_confirms_no_match(evidence) &&
                        evidence.package_evidence.front().version_match ==
                                PackageRelationVersionMatchKind::Unavailable &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::NotMatched) &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::Unavailable),
                "NotMatched and Unavailable Provide evidence was flattened");
    }

    for(const auto& provides :
        std::vector<std::vector<PackageRelationObservedCapability>>{
                {unversioned, version_three},
                {version_three, unversioned}}) {
        const PackageRelationMatchingEvidence evidence =
                match_complete_installed_package(
                        declaration,
                        installed_package(
                                "provider-package", "9", provides));
        expect(
                package_relation_has_confirmed_match(evidence) &&
                        !package_relation_confirms_no_match(evidence) &&
                        evidence.package_evidence.front().version_match ==
                                PackageRelationVersionMatchKind::Matched &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::Unavailable),
                "Matched Provide discarded coexisting Unavailable evidence");
    }

    expect_duplicate_provide_outcome(
            declaration, {version_one, version_one},
            PackageRelationVersionMatchKind::NotMatched, false, true,
            "All duplicate Provides NotMatched");

    const PackageRelationObservedCapability invalid{
            ProviderCapability(
                    "virtual-api=invalid", "virtual-api", "invalid"),
            ObservedVersion::invalid(
                    ObservedVersionSource::InstalledProviderCapability,
                    ConstraintInvalidReason::InvalidVersionIdentity)};
    for(const auto& provides :
        std::vector<std::vector<PackageRelationObservedCapability>>{
                {version_one, invalid},
                {invalid, version_one}}) {
        const PackageRelationMatchingEvidence evidence =
                match_complete_installed_package(
                        declaration,
                        installed_package(
                                "provider-package", "9", provides));
        expect(
                !package_relation_has_confirmed_match(evidence) &&
                        !package_relation_confirms_no_match(evidence) &&
                        evidence.package_evidence.front().identity_match ==
                                PackageRelationIdentityMatchKind::
                                        ProvidedComponent &&
                        evidence.package_evidence.front().version_match ==
                                PackageRelationVersionMatchKind::Invalid &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::NotMatched) &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::Invalid) &&
                        evidence.package_evidence.front().invalid_reason ==
                                PackageRelationMatchInvalidReason::
                                        InvalidVersionMetadata,
                "Invalid duplicate Provide evidence depended on order");
    }

    for(const auto& provides :
        std::vector<std::vector<PackageRelationObservedCapability>>{
                {version_three, invalid},
                {invalid, version_three}}) {
        const PackageRelationMatchingEvidence evidence =
                match_complete_installed_package(
                        declaration,
                        installed_package(
                                "provider-package", "9", provides));
        expect(
                package_relation_has_confirmed_match(evidence) &&
                        !package_relation_confirms_no_match(evidence) &&
                        evidence.package_evidence.front().identity_match ==
                                PackageRelationIdentityMatchKind::
                                        ProvidedComponent &&
                        evidence.package_evidence.front().version_match ==
                                PackageRelationVersionMatchKind::Matched &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::Matched) &&
                        has_capability_version_result(
                                evidence.package_evidence.front(),
                                PackageRelationVersionMatchKind::Invalid) &&
                        evidence.package_evidence.front().invalid_reason ==
                                PackageRelationMatchInvalidReason::
                                        InvalidVersionMetadata,
                "Matched and Invalid Provide evidence depended on order");
    }

    const PackageRelationObservedCapability unrelated_invalid{
            ProviderCapability(
                    "other-api=invalid", "other-api", "invalid"),
            ObservedVersion::invalid(
                    ObservedVersionSource::InstalledProviderCapability,
                    ConstraintInvalidReason::InvalidVersionIdentity)};
    for(const auto& provides :
        std::vector<std::vector<PackageRelationObservedCapability>>{
                {version_one, unrelated_invalid},
                {unrelated_invalid, version_one}}) {
        const PackageRelationMatchingEvidence evidence =
                match_complete_installed_package(
                        declaration,
                        installed_package(
                                "provider-package", "9", provides));
        expect(
                !package_relation_has_confirmed_match(evidence) &&
                        !package_relation_confirms_no_match(evidence) &&
                        evidence.package_evidence.front().identity_match ==
                                PackageRelationIdentityMatchKind::
                                        ProvidedComponent &&
                        evidence.package_evidence.front().version_match ==
                                PackageRelationVersionMatchKind::NotMatched &&
                        evidence.package_evidence.front().invalid_reason ==
                                PackageRelationMatchInvalidReason::
                                        InvalidVersionMetadata,
                "Unrelated Invalid Provide enabled a NoMatch proof");
    }
}

void test_collection_match_and_no_match_boundaries() {
    const DeclaredPackageRelation declaration = relation(
            PackageRelationKind::Conflict, "target>=2");
    PackageRelationMatchingEvidence exact_match =
            match_declared_package_relation(
                    declaration,
                    complete_installed_set(
                            {installed_package("target", "2")}));
    expect(
            package_relation_has_confirmed_match(exact_match) &&
                    !package_relation_confirms_no_match(exact_match),
            "Confirmed exact match was flattened");

    PackageRelationMatchingEvidence confirmed_miss =
            match_declared_package_relation(
                    declaration,
                    complete_installed_set(
                            {installed_package("other", "9")}));
    expect(
            !package_relation_has_confirmed_match(confirmed_miss) &&
                    package_relation_confirms_no_match(confirmed_miss),
            "Complete unrelated inventory did not prove NoMatch");

    PackageRelationMatchingEvidence successful_empty =
            match_declared_package_relation(
                    declaration, complete_installed_set({}));
    expect(
            package_relation_confirms_no_match(successful_empty),
            "Successful empty installed inventory did not prove NoMatch");

    PackageRelationObservationSet unscoped_complete{
            PackageRelationObservationCompleteness::Complete,
            {},
            {},
            {},
            {}};
    expect(
            !package_relation_confirms_no_match(
                    match_declared_package_relation(
                            declaration, unscoped_complete)),
            "Complete-without-observation-scope proved NoMatch");

    PackageRelationObservationSet partial = complete_installed_set(
            {installed_package("other", "9")});
    partial.completeness = PackageRelationObservationCompleteness::Partial;
    partial.failures.push_back(PackageRelationObservationFailure{
            PackageRelationObservationFailureKind::PartialSourceFailure,
            PackageRelationObservationRole::Installed,
            PackageRelationSourceIdentity{installed_source()},
            std::nullopt,
            "partial local database observation"});
    expect(
            !package_relation_confirms_no_match(
                    match_declared_package_relation(
                            declaration, partial)),
            "Partial observation proved NoMatch");

    partial.packages.push_back(installed_package("target", "2"));
    const PackageRelationMatchingEvidence partial_with_match =
            match_declared_package_relation(declaration, partial);
    expect(
            package_relation_has_confirmed_match(partial_with_match) &&
                    !partial_with_match.observation_failures.empty() &&
                    !package_relation_confirms_no_match(partial_with_match),
            "Partial failure and confirmed match did not coexist");
}

void test_unavailable_and_invalid_observations() {
    const DeclaredPackageRelation declaration = relation(
            PackageRelationKind::Replacement, "virtual-api>=1");
    PackageRelationObservationSet unavailable{
            PackageRelationObservationCompleteness::Unavailable,
            {installed_source()},
            {PackageRelationSourceIdentityCoverage{
                    PackageRelationSourceIdentity{installed_source()},
                    false,
                    false}},
            {},
            {PackageRelationObservationFailure{
                    PackageRelationObservationFailureKind::
                            SourceUnavailable,
                    PackageRelationObservationRole::Installed,
                    PackageRelationSourceIdentity{installed_source()},
                    std::nullopt,
                    "local database unavailable"}}};
    const PackageRelationMatchingEvidence unavailable_evidence =
            match_declared_package_relation(declaration, unavailable);
    expect(
            !package_relation_has_confirmed_match(unavailable_evidence) &&
                    !package_relation_confirms_no_match(
                            unavailable_evidence),
            "Unavailable observation was flattened");

    PackageRelationObservedPackage invalid = installed_package();
    invalid.role = PackageRelationObservationRole::PlannedTarget;
    const PackageRelationMatchEvidence invalid_evidence =
            match_declared_package_relation(declaration, invalid);
    expect(
            invalid_evidence.identity_match ==
                            PackageRelationIdentityMatchKind::InvalidInput &&
                    invalid_evidence.version_match ==
                            PackageRelationVersionMatchKind::Invalid &&
                    invalid_evidence.invalid_reason ==
                            PackageRelationMatchInvalidReason::
                                    SourceRoleMismatch,
            "Invalid source/role combination was not typed");

    PackageRelationObservedPackage malformed = installed_package(
            "provider", "5",
            {PackageRelationObservedCapability{
                    ProviderCapability(
                            "virtual-api=3", "virtual-api", "3"),
                    ObservedVersion::available(
                            ObservedVersionSource::
                                    InstalledProviderCapability,
                            "4")}});
    const PackageRelationMatchEvidence malformed_evidence =
            match_declared_package_relation(declaration, malformed);
    expect(
            malformed_evidence.invalid_reason ==
                    PackageRelationMatchInvalidReason::MalformedCapability,
            "Malformed capability version was not Invalid evidence");
}

} // namespace

int main() {
    try {
        test_exact_and_provided_identity_matching();
        test_planned_exact_and_provided_matching();
        test_version_outcomes_do_not_flatten_unknown();
        test_duplicate_same_name_provides_are_order_independent();
        test_collection_match_and_no_match_boundaries();
        test_unavailable_and_invalid_observations();
        std::cout << "package relation observation tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "package relation observation tests: "
                  << error.what() << '\n';
        return 1;
    }
}
