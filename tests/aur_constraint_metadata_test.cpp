#include "aur_constraint_metadata.hpp"
#include "aur_rpc.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

static_assert(
    !std::is_pointer_v<decltype(AurPackageConstraintMetadata::package_name)>);
static_assert(
    !std::is_pointer_v<decltype(AurPackageConstraintMetadata::package_version)>);
static_assert(
    !std::is_pointer_v<decltype(AurPackageConstraintMetadata::relations)>);
static_assert(
    !std::is_pointer_v<decltype(ProviderConstraintMetadata::provided_capability)>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Expected, typename... Alternatives>
Expected require_alternative(
    const std::variant<Alternatives...>& result,
    const std::string& context) {
    const Expected* value = std::get_if<Expected>(&result);
    if(value == nullptr) {
        throw std::runtime_error(context + ": unexpected result alternative");
    }
    return *value;
}

ConsumerDependencyRequirement consumer_requirement(
    const std::string& specification) {
    DependencyRequirementParseResult parsed =
        parse_dependency_requirement(specification);
    const DependencyRequirement* requirement = parsed.requirement();
    if(requirement == nullptr) {
        throw std::runtime_error(
            "test requirement did not parse: " + specification);
    }
    const auto* consumer =
        std::get_if<ConsumerDependencyRequirement>(requirement);
    if(consumer == nullptr) {
        throw std::runtime_error(
            "test requirement was not a package requirement: " +
            specification);
    }
    return *consumer;
}

AurPackageInfo package_info(
    std::string name = "provider-package",
    std::string package_base = "provider-base",
    std::string version = "9.0-1") {
    AurPackageInfo package;
    package.Name = std::move(name);
    package.PackageBase = std::move(package_base);
    package.Version = std::move(version);
    return package;
}

AurPackageConstraintMetadata project_package(
    const AurPackageInfo& package,
    const std::string& context) {
    AurConstraintMetadataProjectionResult result =
        project_aur_constraint_metadata(package);
    return require_alternative<AurPackageConstraintMetadata>(
        result, context);
}

AurProviderDependencyProjection selected_aur_provider() {
    AurPackageInfo package = package_info(
        "provider", "provider-base", "8.0-1");
    package.Provides = {"virtual-api=3"};
    return require_alternative<AurProviderDependencyProjection>(
        project_aur_provider_dependency(
            consumer_requirement("virtual-api>=2"),
            project_package(package, "selected AUR provider")),
        "selected AUR provider projection");
}

AurConstraintMetadataProjectionFailure malformed_provider_metadata(
    std::string package_name, std::string package_base) {
    return AurConstraintMetadataProjectionFailure{
        std::move(package_name),
        std::move(package_base),
        AurConstraintMetadataField::Provides,
        0,
        DependencyConstraintParseFailure{
            DependencyConstraintParseFailureKind::
                UnsupportedProviderOperator,
            "virtual-api>=3"}};
}

void expect_refresh_identity_changed(
    const AurProviderDependencyProjectionResult& result,
    const AurProviderDependencyProjection& selected,
    const std::string& context) {
    const AurProviderDependencyProjectionFailure& failure =
        require_alternative<AurProviderDependencyProjectionFailure>(
            result, context);
    expect(
        failure.kind == AurProviderProjectionFailureKind::
                            ProviderIdentityChanged &&
            failure.package_name == selected.provider.package_name &&
            failure.package_base == std::optional<std::string>(
                                        selected.provider
                                            .package_base) &&
            !failure.metadata_failure.has_value(),
        context + ": selected provider identity was not retained");
}

ConsumerDependencyRequirement require_consumer(
    const DependencyRequirement& requirement,
    const std::string& context) {
    const auto* consumer =
        std::get_if<ConsumerDependencyRequirement>(&requirement);
    if(consumer == nullptr) {
        throw std::runtime_error(context + ": not a consumer requirement");
    }
    return *consumer;
}

void test_dependency_arrays_are_typed_by_category() {
    AurPackageInfo package = package_info();
    package.Depends = {"runtime>=2:1.0-1"};
    package.MakeDepends = {"builder<3"};
    package.CheckDepends = {"checker"};

    const AurPackageConstraintMetadata metadata =
        project_package(package, "typed dependency arrays");
    expect(
        metadata.depends.size() == 1 &&
            metadata.make_depends.size() == 1 &&
            metadata.check_depends.size() == 1,
        "AUR dependency categories were merged or dropped");

    const ConsumerDependencyRequirement runtime = require_consumer(
        metadata.depends.front(), "runtime dependency");
    const ConsumerDependencyRequirement builder = require_consumer(
        metadata.make_depends.front(), "make dependency");
    const ConsumerDependencyRequirement checker = require_consumer(
        metadata.check_depends.front(), "check dependency");
    expect(
        runtime.raw_specification() == "runtime>=2:1.0-1" &&
            runtime.package_name() == "runtime" &&
            runtime.constraint().has_value() &&
            runtime.constraint()->relation() ==
                DependencyVersionRelation::GreaterThanOrEqual &&
            runtime.constraint()->version() == "2:1.0-1",
        "Depends did not retain its typed requirement");
    expect(
        builder.raw_specification() == "builder<3" &&
            builder.constraint().has_value() &&
            builder.constraint()->relation() ==
                DependencyVersionRelation::LessThan,
        "MakeDepends did not retain its typed requirement");
    expect(
        checker.raw_specification() == "checker" &&
            !checker.constraint().has_value(),
        "CheckDepends did not retain its unconstrained requirement");
}

void test_exact_identity_and_version_states() {
    const AurPackageConstraintMetadata exact = project_package(
        package_info("split-child", "split-base", "2:1.2-3"),
        "AUR exact package");
    expect(
        exact.package_name == "split-child" &&
            exact.package_base == "split-base" &&
            exact.package_version.source() ==
                ObservedVersionSource::AurExactPackage &&
            exact.package_version.version() != nullptr &&
            *exact.package_version.version() == "2:1.2-3",
        "AUR exact Name/PackageBase/Version identity was flattened");

    const AurPackageConstraintMetadata missing = project_package(
        package_info("missing-version", "missing-version", ""),
        "missing AUR version");
    expect(
        missing.package_version.unknown_reason() != nullptr &&
            *missing.package_version.unknown_reason() ==
                ObservedVersionUnknownReason::
                    MissingVersionMetadata,
        "Missing AUR Version was not retained as Unknown");

    const AurPackageConstraintMetadata invalid = project_package(
        package_info("invalid-version", "invalid-version", "bad version"),
        "invalid AUR version");
    expect(
        invalid.package_version.invalid_reason() != nullptr &&
            *invalid.package_version.invalid_reason() ==
                ConstraintInvalidReason::InvalidVersionIdentity,
        "Invalid AUR Version was not retained as Invalid");
}

void test_provides_equality_only_and_version_separation() {
    AurPackageInfo package = package_info(
        "provider-package", "provider-base", "9.0-1");
    package.Provides = {"foo", "bar=1.2-3"};
    const AurPackageConstraintMetadata metadata =
        project_package(package, "AUR Provides");
    expect(
        metadata.provides.size() == 2,
        "AUR Provides capability count differs");

    const AurProviderCapabilityMetadata& unversioned = metadata.provides[0];
    expect(
        unversioned.capability.package_name() == "foo" &&
            !unversioned.capability.version().has_value() &&
            unversioned.provided_version.unknown_reason() != nullptr &&
            *unversioned.provided_version.unknown_reason() ==
                ObservedVersionUnknownReason::
                    UnversionedProviderCapability,
        "Unversioned AUR Provide inherited a package version");

    const AurProviderCapabilityMetadata& equality = metadata.provides[1];
    expect(
        equality.capability.raw_specification() == "bar=1.2-3" &&
            equality.capability.version() ==
                std::optional<std::string>("1.2-3") &&
            equality.provided_version.version() != nullptr &&
            *equality.provided_version.version() == "1.2-3" &&
            metadata.package_version.version() != nullptr &&
            *metadata.package_version.version() == "9.0-1" &&
            *equality.provided_version.version() !=
                *metadata.package_version.version(),
        "AUR package Version and provided capability version were mixed");

    const ConsumerDependencyRequirement versioned =
        consumer_requirement("foo>=1");
    const AurProviderDependencyProjection& projected =
        require_alternative<AurProviderDependencyProjection>(
            project_aur_provider_dependency(versioned, metadata),
            "unversioned provider projection");
    expect(
        projected.evaluation.satisfaction() ==
                ConstraintSatisfaction::Unknown &&
            projected.evaluation.unknown_reason() != nullptr &&
            *projected.evaluation.unknown_reason() ==
                ObservedVersionUnknownReason::
                    UnversionedProviderCapability &&
            projected.provider.constraint_metadata.has_value() &&
            projected.provider.constraint_metadata->package_version
                    .version() != nullptr &&
            *projected.provider.constraint_metadata->package_version
                    .version() == "9.0-1" &&
            projected.provider.constraint_metadata->provided_version
                    .version() == nullptr,
        "Provider projection substituted package Version for foo");
}

void expect_provides_failure(
    const std::string& specification,
    DependencyConstraintParseFailureKind expected_kind,
    const std::string& context) {
    AurPackageInfo package = package_info();
    package.Provides = {specification};
    const AurConstraintMetadataProjectionFailure& failure =
        require_alternative<AurConstraintMetadataProjectionFailure>(
            project_aur_constraint_metadata(package), context);
    expect(
        failure.field == AurConstraintMetadataField::Provides &&
            failure.item_index == 0 &&
            failure.reason.kind == expected_kind &&
            failure.reason.raw_specification == specification,
        context + ": malformed Provides was flattened");
}

void test_provides_reject_non_equality_and_present_empty() {
    for(const std::string specification :
        {"foo>=1", "foo<=1", "foo>1", "foo<1"}) {
        expect_provides_failure(
            specification,
            DependencyConstraintParseFailureKind::
                UnsupportedProviderOperator,
            "non-equality AUR Provide " + specification);
    }
    expect_provides_failure(
        "foo=",
        DependencyConstraintParseFailureKind::MissingVersion,
        "present-empty AUR Provide");
    expect_provides_failure(
        "foo==1",
        DependencyConstraintParseFailureKind::InvalidVersion,
        "malformed AUR Provide");
}

void test_conflicts_and_replaces_are_typed_once_in_source_order() {
    AurPackageInfo package = package_info(
        "declaring-child", "declaring-base", "4.0-1");
    package.Conflicts = {"old-component>=2:1.0-3", "second-conflict"};
    package.Replaces = {"legacy-component=1.0-1", "second-replacement"};

    const AurPackageConstraintMetadata metadata =
        project_package(package, "AUR declared relations");
    expect(
        metadata.relations.size() == 4,
        "AUR relation count or field ordering differs");

    const DeclaredPackageRelation& first_conflict = metadata.relations[0];
    const DeclaredPackageRelation& second_conflict = metadata.relations[1];
    const DeclaredPackageRelation& first_replacement = metadata.relations[2];
    const DeclaredPackageRelation& second_replacement = metadata.relations[3];
    expect(
        first_conflict.kind() == PackageRelationKind::Conflict &&
            first_conflict.declaring_package_name() ==
                "declaring-child" &&
            first_conflict.declaring_package_base() ==
                "declaring-base" &&
            first_conflict.raw_specification() ==
                "old-component>=2:1.0-3" &&
            first_conflict.target_component() == "old-component" &&
            first_conflict.constraint().has_value() &&
            first_conflict.constraint()->relation() ==
                DependencyVersionRelation::GreaterThanOrEqual &&
            first_conflict.constraint()->version() == "2:1.0-3",
        "First AUR conflict lost kind, identity, raw, or constraint");
    expect(
        second_conflict.kind() == PackageRelationKind::Conflict &&
            second_conflict.raw_specification() ==
                "second-conflict" &&
            second_conflict.target_component() ==
                "second-conflict" &&
            !second_conflict.constraint().has_value(),
        "Second AUR conflict lost field-local ordering");
    expect(
        first_replacement.kind() == PackageRelationKind::Replacement &&
            first_replacement.raw_specification() ==
                "legacy-component=1.0-1" &&
            first_replacement.target_component() ==
                "legacy-component" &&
            first_replacement.constraint().has_value() &&
            first_replacement.constraint()->relation() ==
                DependencyVersionRelation::Equal &&
            first_replacement.constraint()->version() == "1.0-1" &&
            second_replacement.kind() ==
                PackageRelationKind::Replacement &&
            second_replacement.target_component() ==
                "second-replacement",
        "AUR replacement kind or ordering was flattened");

    expect(
        package.Conflicts == std::vector<std::string>(
                                 {"old-component>=2:1.0-3",
                                  "second-conflict"}) &&
            package.Replaces == std::vector<std::string>(
                                    {"legacy-component=1.0-1",
                                     "second-replacement"}),
        "Typed projection changed raw AUR relation arrays");
}

void test_relation_failure_preserves_field_index_and_source_identity() {
    AurPackageInfo conflict_package = package_info(
        "conflict-child", "conflict-base", "1.0-1");
    conflict_package.Conflicts = {"valid-conflict", "broken-conflict>="};
    const AurConstraintMetadataProjectionFailure& conflict_failure =
        require_alternative<AurConstraintMetadataProjectionFailure>(
            project_aur_constraint_metadata(conflict_package),
            "malformed AUR conflict");
    expect(
        conflict_failure.package_name == "conflict-child" &&
            conflict_failure.package_base == "conflict-base" &&
            conflict_failure.field ==
                AurConstraintMetadataField::Conflicts &&
            conflict_failure.item_index == 1 &&
            conflict_failure.reason.kind ==
                DependencyConstraintParseFailureKind::
                    MissingVersion &&
            conflict_failure.reason.raw_specification ==
                "broken-conflict>=",
        "Malformed Conflict lost package/Base/field/index/raw attribution");

    AurPackageInfo replacement_package = package_info(
        "replacement-child", "replacement-base", "1.0-1");
    replacement_package.Replaces = {
        "valid-replacement", "another-valid", "broken-replacement==1"};
    const AurConstraintMetadataProjectionFailure& replacement_failure =
        require_alternative<AurConstraintMetadataProjectionFailure>(
            project_aur_constraint_metadata(replacement_package),
            "malformed AUR replacement");
    expect(
        replacement_failure.package_name == "replacement-child" &&
            replacement_failure.package_base ==
                "replacement-base" &&
            replacement_failure.field ==
                AurConstraintMetadataField::Replaces &&
            replacement_failure.item_index == 2 &&
            replacement_failure.reason.kind ==
                DependencyConstraintParseFailureKind::
                    UnsupportedConsumerOperator &&
            replacement_failure.reason.raw_specification ==
                "broken-replacement==1",
        "Malformed Replace lost package/Base/field/index/raw attribution");
}

void test_partial_candidate_failure_preserves_order_and_identity() {
    AurPackageInfo first_package = package_info(
        "provider-a", "provider-a-base", "3.0-1");
    first_package.Provides = {"virtual-api=3"};
    AurPackageInfo third_package = package_info(
        "provider-c", "provider-c-base", "4.0-1");
    third_package.Provides = {"virtual-api=4"};

    const ConsumerDependencyRequirement requirement =
        consumer_requirement("virtual-api>=2");
    const std::vector<AurProviderCandidateMetadata> candidates{
        project_package(first_package, "first provider"),
        AurProviderMetadataUnavailable{
            "provider-b",
            std::nullopt,
            ObservedVersionUnknownReason::PartialSourceFailure},
        project_package(third_package, "third provider")};
    const std::vector<AurProviderDependencyProjectionResult> projections =
        project_aur_provider_dependencies(requirement, candidates);

    expect(
        projections.size() == candidates.size(),
        "Partial AUR candidate failure filtered the candidate set");
    const AurProviderDependencyProjection& first =
        require_alternative<AurProviderDependencyProjection>(
            projections[0], "first projected provider");
    const AurProviderDependencyUnknown& partial =
        require_alternative<AurProviderDependencyUnknown>(
            projections[1], "partial provider failure");
    const AurProviderDependencyProjection& third =
        require_alternative<AurProviderDependencyProjection>(
            projections[2], "third projected provider");
    expect(
        first.provider.package_name == "provider-a" &&
            partial.package_name == "provider-b" &&
            !partial.package_base.has_value() &&
            partial.reason ==
                ObservedVersionUnknownReason::PartialSourceFailure &&
            third.provider.package_name == "provider-c",
        "Partial AUR candidate failure changed order or source identity");
}

void test_refresh_unavailable_metadata_preserves_selected_identity() {
    const AurProviderDependencyProjection selected = selected_aur_provider();

    const AurProviderDependencyUnknown& same_identity =
        require_alternative<AurProviderDependencyUnknown>(
            refresh_aur_provider_dependency(
                selected,
                AurProviderMetadataUnavailable{
                    "provider",
                    std::optional<std::string>(
                        "provider-base"),
                    ObservedVersionUnknownReason::
                        PartialSourceFailure}),
            "same-identity unavailable metadata");
    expect(
        same_identity.requirement == selected.requirement &&
            same_identity.package_name ==
                selected.provider.package_name &&
            same_identity.package_base ==
                std::optional<std::string>(
                    selected.provider.package_base) &&
            same_identity.reason == ObservedVersionUnknownReason::
                                        PartialSourceFailure,
        "Unavailable metadata lost selected identity or failure reason");

    const AurProviderDependencyUnknown& missing_package_base =
        require_alternative<AurProviderDependencyUnknown>(
            refresh_aur_provider_dependency(
                selected,
                AurProviderMetadataUnavailable{
                    "provider",
                    std::nullopt,
                    ObservedVersionUnknownReason::
                        MetadataQueryFailure}),
            "unavailable PackageBase metadata");
    expect(
        missing_package_base.requirement == selected.requirement &&
            missing_package_base.package_name ==
                selected.provider.package_name &&
            missing_package_base.package_base ==
                std::optional<std::string>(
                    selected.provider.package_base) &&
            missing_package_base.reason ==
                ObservedVersionUnknownReason::MetadataQueryFailure,
        "Unknown PackageBase discarded selected identity or query failure");
}

void test_refresh_rejects_known_unavailable_identity_changes() {
    const AurProviderDependencyProjection selected = selected_aur_provider();

    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            AurProviderMetadataUnavailable{
                "changed-provider",
                std::optional<std::string>("provider-base"),
                ObservedVersionUnknownReason::PartialSourceFailure}),
        selected,
        "unavailable provider Name change");
    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            AurProviderMetadataUnavailable{
                "provider",
                std::optional<std::string>("changed-base"),
                ObservedVersionUnknownReason::PartialSourceFailure}),
        selected,
        "unavailable provider PackageBase change");
}

void test_refresh_malformed_metadata_checks_and_preserves_identity() {
    const AurProviderDependencyProjection selected = selected_aur_provider();
    const AurConstraintMetadataProjectionFailure same_identity_metadata =
        malformed_provider_metadata("provider", "provider-base");
    const AurProviderDependencyProjectionFailure& same_identity =
        require_alternative<AurProviderDependencyProjectionFailure>(
            refresh_aur_provider_dependency(
                selected, same_identity_metadata),
            "same-identity malformed metadata");
    expect(
        same_identity.requirement == selected.requirement &&
            same_identity.package_name ==
                selected.provider.package_name &&
            same_identity.package_base ==
                std::optional<std::string>(
                    selected.provider.package_base) &&
            same_identity.kind == AurProviderProjectionFailureKind::
                                      InvalidCandidateMetadata &&
            same_identity.metadata_failure ==
                std::optional<DependencyConstraintParseFailure>(
                    same_identity_metadata.reason),
        "Malformed metadata lost selected identity or parse failure");

    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            malformed_provider_metadata(
                "changed-provider", "provider-base")),
        selected,
        "malformed provider Name change");
    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            malformed_provider_metadata("provider", "changed-base")),
        selected,
        "malformed provider PackageBase change");
}

void test_refresh_rejects_non_aur_selected_origin() {
    AurProviderDependencyProjection selected = selected_aur_provider();
    selected.provider =
        ProvidedDependency::from_repository("core", "provider");

    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            AurProviderMetadataUnavailable{
                "provider",
                std::optional<std::string>("provider-base"),
                ObservedVersionUnknownReason::PartialSourceFailure}),
        selected,
        "non-AUR selected provider origin");
}

void test_refresh_reprojects_current_capability() {
    const ConsumerDependencyRequirement requirement =
        consumer_requirement("virtual-api>=2");
    AurPackageInfo initial_package = package_info(
        "provider", "provider-base", "8.0-1");
    initial_package.Provides = {"virtual-api=3"};
    const AurProviderDependencyProjection& selected =
        require_alternative<AurProviderDependencyProjection>(
            project_aur_provider_dependency(
                requirement,
                project_package(
                    initial_package,
                    "initial provider metadata")),
            "initial provider projection");
    expect(
        selected.evaluation.satisfaction() ==
            ConstraintSatisfaction::Satisfied,
        "Initial capability did not satisfy the requirement");

    AurPackageInfo current_package = package_info(
        "provider", "provider-base", "9.0-1");
    current_package.Provides = {"virtual-api=3"};
    const AurProviderDependencyProjection& refreshed =
        require_alternative<AurProviderDependencyProjection>(
            refresh_aur_provider_dependency(
                selected,
                project_package(
                    current_package,
                    "current provider metadata")),
            "refreshed provider projection");
    expect(
        refreshed.requirement == selected.requirement &&
            same_provider_identity(
                refreshed.provider, selected.provider) &&
            refreshed.provider.constraint_metadata.has_value() &&
            refreshed.provider.constraint_metadata->package_version
                    .version() != nullptr &&
            *refreshed.provider.constraint_metadata->package_version
                    .version() == "9.0-1" &&
            refreshed.provider.constraint_metadata
                    ->provided_capability.raw_specification() ==
                "virtual-api=3" &&
            refreshed.provider.constraint_metadata->provided_version
                    .version() != nullptr &&
            *refreshed.provider.constraint_metadata->provided_version
                    .version() == "3" &&
            refreshed.evaluation.satisfaction() ==
                ConstraintSatisfaction::Satisfied,
        "Refresh did not retain the current package metadata");

    AurPackageInfo changed_capability_package = package_info(
        "provider", "provider-base", "10.0-1");
    changed_capability_package.Provides = {"virtual-api=1"};
    expect_refresh_identity_changed(
        refresh_aur_provider_dependency(
            selected,
            project_package(
                changed_capability_package,
                "changed current capability version")),
        selected,
        "provided capability version change");

    AurPackageInfo missing_capability_package = package_info(
        "provider", "provider-base", "11.0-1");
    missing_capability_package.Provides = {"other-api=7"};
    const AurProviderDependencyProjectionFailure& missing_capability =
        require_alternative<AurProviderDependencyProjectionFailure>(
            refresh_aur_provider_dependency(
                selected,
                project_package(
                    missing_capability_package,
                    "missing current capability")),
            "missing current capability projection");
    expect(
        missing_capability.kind ==
                AurProviderProjectionFailureKind::
                    MatchingCapabilityMissing &&
            missing_capability.package_name == "provider" &&
            missing_capability.package_base ==
                std::optional<std::string>("provider-base"),
        "Refresh reused a capability that disappeared from current metadata");
}

} // namespace

int main() {
    try {
        test_dependency_arrays_are_typed_by_category();
        test_exact_identity_and_version_states();
        test_provides_equality_only_and_version_separation();
        test_provides_reject_non_equality_and_present_empty();
        test_conflicts_and_replaces_are_typed_once_in_source_order();
        test_relation_failure_preserves_field_index_and_source_identity();
        test_partial_candidate_failure_preserves_order_and_identity();
        test_refresh_unavailable_metadata_preserves_selected_identity();
        test_refresh_rejects_known_unavailable_identity_changes();
        test_refresh_malformed_metadata_checks_and_preserves_identity();
        test_refresh_rejects_non_aur_selected_origin();
        test_refresh_reprojects_current_capability();
        std::cout << "AUR constraint metadata tests passed\n";
    } catch(const std::exception& error) {
        std::cerr << "AUR constraint metadata test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
