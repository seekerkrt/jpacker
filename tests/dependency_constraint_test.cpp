#include "dependency_constraint.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

static_assert(!std::is_default_constructible_v<DependencyVersionConstraint>);
static_assert(!std::is_default_constructible_v<ConsumerDependencyRequirement>);
static_assert(!std::is_default_constructible_v<ProviderCapability>);
static_assert(!std::is_default_constructible_v<ObservedVersion>);
static_assert(!std::is_default_constructible_v<ConstraintEvaluation>);

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ConsumerDependencyRequirement require_consumer_requirement(
        const DependencyRequirementParseResult& result,
        const std::string& context) {
    expect(result.failure() == nullptr, context + ": unexpected parse failure");
    const DependencyRequirement* requirement = result.requirement();
    expect(requirement != nullptr, context + ": missing typed requirement");
    const auto* consumer = std::get_if<ConsumerDependencyRequirement>(requirement);
    expect(consumer != nullptr, context + ": expected a consumer requirement");
    return *consumer;
}

void expect_dependency_parse_failure(
        const std::string& specification,
        DependencyConstraintParseFailureKind expected_kind) {
    const DependencyRequirementParseResult result =
            parse_dependency_requirement(specification);
    expect(result.requirement() == nullptr, "Invalid consumer requirement parsed");
    expect(result.failure() != nullptr, "Invalid consumer requirement lost failure");
    expect(
            result.failure()->kind == expected_kind &&
                    result.failure()->raw_specification == specification,
            "Consumer parse failure differs");
}

void expect_provider_parse_failure(
        const std::string& specification,
        DependencyConstraintParseFailureKind expected_kind) {
    const ProviderCapabilityParseResult result =
            parse_provider_capability(specification);
    expect(result.capability() == nullptr, "Invalid provider capability parsed");
    expect(result.failure() != nullptr, "Invalid provider capability lost failure");
    expect(
            result.failure()->kind == expected_kind &&
                    result.failure()->raw_specification == specification,
            "Provider parse failure differs");
}

void test_consumer_requirement_grammar() {
    const ConsumerDependencyRequirement unconstrained =
            require_consumer_requirement(
                    parse_dependency_requirement("consumer"),
                    "unconstrained consumer");
    expect(
            unconstrained.raw_specification() == "consumer" &&
                    unconstrained.package_name() == "consumer" &&
                    !unconstrained.constraint().has_value(),
            "Unconstrained consumer requirement differs");

    const std::vector<std::pair<std::string, DependencyVersionRelation>>
            specifications = {
                    {"consumer<1", DependencyVersionRelation::LessThan},
                    {"consumer<=1", DependencyVersionRelation::LessThanOrEqual},
                    {"consumer=1", DependencyVersionRelation::Equal},
                    {"consumer>=1", DependencyVersionRelation::GreaterThanOrEqual},
                    {"consumer>1", DependencyVersionRelation::GreaterThan},
            };
    for(const auto& [specification, relation] : specifications) {
        const ConsumerDependencyRequirement requirement =
                require_consumer_requirement(
                        parse_dependency_requirement(specification),
                        specification);
        expect(
                requirement.constraint().has_value() &&
                        requirement.constraint()->relation() == relation &&
                        requirement.constraint()->version() == "1",
                "Consumer relation grammar differs: " + specification);
    }

    const DependencyRequirementParseResult soname =
            parse_dependency_requirement("libsample:libsample.so.1");
    expect(
            soname.failure() == nullptr && soname.requirement() != nullptr &&
                    std::holds_alternative<SonameDependencyRequirement>(
                            *soname.requirement()) &&
                    std::get<SonameDependencyRequirement>(*soname.requirement())
                                    .soname() == "libsample:libsample.so.1",
            "SONAME requirement was converted to a package constraint");

    expect_dependency_parse_failure(
            "consumer>=", DependencyConstraintParseFailureKind::MissingVersion);
    expect_dependency_parse_failure(
            "consumer=1=2", DependencyConstraintParseFailureKind::InvalidVersion);
    expect_dependency_parse_failure(
            "bad/name>=1",
            DependencyConstraintParseFailureKind::InvalidPackageIdentity);
}

void test_provider_capability_grammar() {
    const ProviderCapabilityParseResult unversioned =
            parse_provider_capability("virtual-api");
    expect(
            unversioned.failure() == nullptr && unversioned.capability() != nullptr &&
                    unversioned.capability()->package_name() == "virtual-api" &&
                    !unversioned.capability()->version().has_value(),
            "Unversioned provider capability differs");

    const ProviderCapabilityParseResult equality =
            parse_provider_capability("virtual-api=2:1.0-3");
    expect(
            equality.failure() == nullptr && equality.capability() != nullptr &&
                    equality.capability()->package_name() == "virtual-api" &&
                    equality.capability()->version() == "2:1.0-3",
            "Equality-only provider capability differs");

    expect_provider_parse_failure(
            "virtual-api>=1",
            DependencyConstraintParseFailureKind::UnsupportedProviderOperator);
    expect_provider_parse_failure(
            "virtual-api<1",
            DependencyConstraintParseFailureKind::UnsupportedProviderOperator);
    expect_provider_parse_failure(
            "virtual-api==1", DependencyConstraintParseFailureKind::InvalidVersion);
}

void test_direct_libalpm_evaluation_and_unknown() {
    const ConsumerDependencyRequirement requirement =
            require_consumer_requirement(
                    parse_dependency_requirement("consumer>=2:1.0-1"),
                    "epoch requirement");
    const ConstraintEvaluation satisfied =
            evaluate_consumer_dependency_requirement(
                    requirement,
                    ObservedVersion::available(
                            ObservedVersionSource::LocalExactPackage,
                            "2:1.0-1"));
    expect(
            satisfied.satisfaction() == ConstraintSatisfaction::Satisfied &&
                    satisfied.unknown_reason() == nullptr,
            "Direct libalpm equality evaluation differs");

    const ConstraintEvaluation unsatisfied =
            evaluate_consumer_dependency_requirement(
                    requirement,
                    ObservedVersion::available(
                            ObservedVersionSource::LocalExactPackage,
                            "1:9.9-1"));
    expect(
            unsatisfied.satisfaction() == ConstraintSatisfaction::Unsatisfied,
            "Direct libalpm epoch ordering differs");

    const ConstraintEvaluation invalid_observed =
            evaluate_consumer_dependency_requirement(
                    requirement,
                    ObservedVersion::available(
                            ObservedVersionSource::LocalExactPackage,
                            "invalid/version"));
    expect(
            invalid_observed.satisfaction() == ConstraintSatisfaction::Invalid &&
                    invalid_observed.invalid_reason() != nullptr &&
                    *invalid_observed.invalid_reason() ==
                            ConstraintInvalidReason::InvalidVersionIdentity,
            "Invalid observed version was compared instead of rejected");

    const ObservedVersion missing_provider = ObservedVersion::unknown(
            ObservedVersionSource::LocalProviderCapability,
            ObservedVersionUnknownReason::UnversionedProviderCapability);
    const ConstraintEvaluation unknown =
            evaluate_consumer_dependency_requirement(requirement, missing_provider);
    expect(
            missing_provider.source() ==
                            ObservedVersionSource::LocalProviderCapability &&
                    missing_provider.version() == nullptr &&
                    missing_provider.unknown_reason() != nullptr &&
                    unknown.satisfaction() == ConstraintSatisfaction::Unknown &&
                    unknown.unknown_reason() != nullptr &&
                    *unknown.unknown_reason() ==
                            ObservedVersionUnknownReason::
                                    UnversionedProviderCapability,
            "Missing observed provider version was not preserved as Unknown");

    const ConsumerDependencyRequirement unconstrained =
            require_consumer_requirement(
                    parse_dependency_requirement("consumer"),
                    "unconstrained result");
    const ConstraintEvaluation unconstrained_result =
            evaluate_consumer_dependency_requirement(
                    unconstrained, missing_provider);
    expect(
            unconstrained_result.satisfaction() ==
                            ConstraintSatisfaction::Unconstrained,
            "Unconstrained requirement was rounded to another result");
}

void test_invalid_and_aggregate_conflicting_results() {
    const ConstraintEvaluation invalid = ConstraintEvaluation::invalid(
            ConstraintInvalidReason::InvalidVersionIdentity);
    expect(
            invalid.satisfaction() == ConstraintSatisfaction::Invalid &&
                    invalid.invalid_reason() != nullptr &&
                    *invalid.invalid_reason() ==
                            ConstraintInvalidReason::InvalidVersionIdentity,
            "Invalid result lost its typed reason");

    const ConsumerDependencyRequirement first =
            require_consumer_requirement(
                    parse_dependency_requirement("consumer>=2"),
                    "first aggregate requirement");
    const ConsumerDependencyRequirement second =
            require_consumer_requirement(
                    parse_dependency_requirement("consumer<2"),
                    "second aggregate requirement");
    const ConstraintEvaluation conflicting =
            project_conflicting_constraint_invocation(
                    {first, second},
                    ConstraintConflictReason::IncompatibleRequirements);
    expect(
            conflicting.satisfaction() == ConstraintSatisfaction::Conflicting &&
                    conflicting.conflict_reason() != nullptr &&
                    *conflicting.conflict_reason() ==
                            ConstraintConflictReason::IncompatibleRequirements,
            "Aggregate conflicting result differs");
}

} // namespace

int main() {
    try {
        test_consumer_requirement_grammar();
        test_provider_capability_grammar();
        test_direct_libalpm_evaluation_and_unknown();
        test_invalid_and_aggregate_conflicting_results();
        std::cout << "dependency constraint tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "dependency constraint tests: " << error.what() << "\n";
        return 1;
    }
}
