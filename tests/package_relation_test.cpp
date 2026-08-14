#include "package_relation.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<DeclaredPackageRelation>);
static_assert(
        !std::is_default_constructible_v<
                DeclaredPackageRelationParseResult>);

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

DeclaredPackageRelation require_relation(
        PackageRelationKind kind, const std::string& specification,
        const std::string& context,
        std::string package_name = "declaring-child",
        std::string package_base = "declaring-base") {
    const DeclaredPackageRelationParseResult result =
            parse_declared_package_relation(
                    std::move(package_name), std::move(package_base), kind,
                    specification);
    expect(result.failure() == nullptr, context + ": unexpected parse failure");
    expect(result.relation() != nullptr, context + ": missing typed relation");
    return *result.relation();
}

void expect_parse_failure(
        const std::string& specification,
        DependencyConstraintParseFailureKind expected_kind,
        const std::string& context) {
    const DeclaredPackageRelationParseResult result =
            parse_declared_package_relation(
                    "declaring-child", "declaring-base",
                    PackageRelationKind::Conflict, specification);
    expect(result.relation() == nullptr, context + ": invalid relation parsed");
    expect(result.failure() != nullptr, context + ": failure was lost");
    expect(
            result.failure()->kind == expected_kind &&
                    result.failure()->raw_specification == specification,
            context + ": typed failure attribution differs");
}

void test_unversioned_relation_identity_and_raw_preservation() {
    const DeclaredPackageRelation conflict = require_relation(
            PackageRelationKind::Conflict, "conflicting-component",
            "unversioned conflict", "split-child", "split-base");
    expect(
            conflict.kind() == PackageRelationKind::Conflict &&
                    conflict.declaring_package_name() == "split-child" &&
                    conflict.declaring_package_base() == "split-base" &&
                    conflict.raw_specification() ==
                            "conflicting-component" &&
                    conflict.target_component() ==
                            "conflicting-component" &&
                    !conflict.constraint().has_value(),
            "Unversioned conflict lost declaration identity");

    const DeclaredPackageRelation replacement = require_relation(
            PackageRelationKind::Replacement, " legacy-component ",
            "unversioned replacement", "replacement-child",
            "replacement-base");
    expect(
            replacement.kind() == PackageRelationKind::Replacement &&
                    replacement.declaring_package_name() ==
                            "replacement-child" &&
                    replacement.declaring_package_base() ==
                            "replacement-base" &&
                    replacement.raw_specification() ==
                            " legacy-component " &&
                    replacement.target_component() == "legacy-component" &&
                    !replacement.constraint().has_value(),
            "Unversioned replacement lost raw or typed identity");

    expect(
            declared_package_relation_version_matches(
                    conflict, std::nullopt),
            "Unversioned relation required an observed version");
}

void test_five_version_operators() {
    const std::vector<std::pair<std::string, DependencyVersionRelation>>
            specifications{
                    {"component<3", DependencyVersionRelation::LessThan},
                    {"component<=2",
                     DependencyVersionRelation::LessThanOrEqual},
                    {"component=2", DependencyVersionRelation::Equal},
                    {"component>=2",
                     DependencyVersionRelation::GreaterThanOrEqual},
                    {"component>1", DependencyVersionRelation::GreaterThan},
            };
    const std::optional<std::string> observed_version("2");
    for(const auto& [specification, expected_relation] : specifications) {
        const DeclaredPackageRelation relation = require_relation(
                PackageRelationKind::Conflict, specification,
                "version operator " + specification);
        expect(
                relation.constraint().has_value() &&
                        relation.constraint()->relation() ==
                                expected_relation &&
                        declared_package_relation_version_matches(
                                relation, observed_version),
                "Version operator was not retained or evaluated: " +
                        specification);
    }
}

void test_arch_version_ordering_and_false_matches() {
    const DeclaredPackageRelation epoch = require_relation(
            PackageRelationKind::Conflict, "component>=2:1.0-2",
            "epoch relation");
    expect(
            declared_package_relation_version_matches(
                    epoch, std::optional<std::string>("2:1.0-3")) &&
                    declared_package_relation_version_matches(
                            epoch,
                            std::optional<std::string>("2:1.1-1")) &&
                    !declared_package_relation_version_matches(
                            epoch,
                            std::optional<std::string>("1:99.0-99")),
            "Arch epoch/pkgver/pkgrel ordering was not delegated correctly");

    const std::vector<std::pair<std::string, std::string>> false_matches{
            {"component<2", "2"},
            {"component<=1", "2"},
            {"component=2", "3"},
            {"component>=3", "2"},
            {"component>2", "2"},
    };
    for(const auto& [specification, observed] : false_matches) {
        const DeclaredPackageRelation relation = require_relation(
                PackageRelationKind::Replacement, specification,
                "false version match " + specification);
        expect(
                !declared_package_relation_version_matches(
                        relation,
                        std::optional<std::string>(observed)),
                "False version condition matched: " + specification);
    }

    const DeclaredPackageRelation versioned = require_relation(
            PackageRelationKind::Conflict, "component>=1",
            "unavailable observed version");
    expect(
            !declared_package_relation_version_matches(
                    versioned, std::nullopt),
            "Unavailable version satisfied a versioned relation");
}

void test_strict_failures() {
    expect_parse_failure(
            "", DependencyConstraintParseFailureKind::EmptySpecification,
            "empty relation");
    expect_parse_failure(
            " \t ",
            DependencyConstraintParseFailureKind::EmptySpecification,
            "whitespace-only relation");
    expect_parse_failure(
            "bad/name>=1",
            DependencyConstraintParseFailureKind::InvalidPackageIdentity,
            "invalid target identity");
    expect_parse_failure(
            "libsample:libsample.so.1",
            DependencyConstraintParseFailureKind::InvalidPackageIdentity,
            "SONAME target");
    expect_parse_failure(
            "component>=",
            DependencyConstraintParseFailureKind::MissingVersion,
            "missing relation version");
    expect_parse_failure(
            "component=1/2",
            DependencyConstraintParseFailureKind::InvalidVersion,
            "invalid relation version");
    expect_parse_failure(
            "component==1",
            DependencyConstraintParseFailureKind::
                    UnsupportedConsumerOperator,
            "malformed equality operator");
    expect_parse_failure(
            "component!=1",
            DependencyConstraintParseFailureKind::
                    UnsupportedConsumerOperator,
            "unsupported relation operator");
    expect_parse_failure(
            "component<>1",
            DependencyConstraintParseFailureKind::
                    UnsupportedConsumerOperator,
            "otherwise malformed relation");
}

} // namespace

int main() {
    try {
        test_unversioned_relation_identity_and_raw_preservation();
        test_five_version_operators();
        test_arch_version_ordering_and_false_matches();
        test_strict_failures();
        std::cout << "package relation tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "package relation tests: " << error.what() << '\n';
        return 1;
    }
}
