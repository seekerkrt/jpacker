#include "package_relation.hpp"

#include <utility>

namespace {

bool has_malformed_operator(std::string_view specification) noexcept {
    const std::size_t operator_position =
            specification.find_first_of("<>=");
    if(operator_position == std::string_view::npos) return false;

    if(operator_position > 0 &&
       (specification[operator_position - 1] == '!' ||
        specification[operator_position - 1] == '~')) {
        return true;
    }

    std::size_t operator_size = 1;
    if((specification[operator_position] == '<' ||
        specification[operator_position] == '>') &&
       operator_position + 1 < specification.size() &&
       specification[operator_position + 1] == '=') {
        operator_size = 2;
    }
    const std::size_t version_position = operator_position + operator_size;
    return version_position < specification.size() &&
            specification.find_first_of("<>=", version_position) !=
                    std::string_view::npos;
}

DependencyConstraintParseFailure relation_failure(
        DependencyConstraintParseFailure failure,
        std::string_view raw_specification) {
    if(has_malformed_operator(raw_specification)) {
        failure.kind = DependencyConstraintParseFailureKind::
                UnsupportedConsumerOperator;
    }
    failure.raw_specification = std::string(raw_specification);
    return failure;
}

bool version_relation_matches(
        DependencyVersionRelation relation,
        ArchVersionOrdering ordering) noexcept {
    switch(relation) {
    case DependencyVersionRelation::LessThan:
        return ordering == ArchVersionOrdering::Less;
    case DependencyVersionRelation::LessThanOrEqual:
        return ordering != ArchVersionOrdering::Greater;
    case DependencyVersionRelation::Equal:
        return ordering == ArchVersionOrdering::Equal;
    case DependencyVersionRelation::GreaterThanOrEqual:
        return ordering != ArchVersionOrdering::Less;
    case DependencyVersionRelation::GreaterThan:
        return ordering == ArchVersionOrdering::Greater;
    }
    return false;
}

} // namespace

DeclaredPackageRelation::DeclaredPackageRelation(
        std::string declaring_package_name,
        std::string declaring_package_base, PackageRelationKind kind,
        std::string raw_specification, std::string target_component,
        std::optional<DependencyVersionConstraint> constraint)
        : declaring_package_name_(std::move(declaring_package_name)),
          declaring_package_base_(std::move(declaring_package_base)),
          kind_(kind), raw_specification_(std::move(raw_specification)),
          target_component_(std::move(target_component)),
          constraint_(std::move(constraint)) {}

const std::string& DeclaredPackageRelation::declaring_package_name()
        const noexcept {
    return declaring_package_name_;
}

const std::string& DeclaredPackageRelation::declaring_package_base()
        const noexcept {
    return declaring_package_base_;
}

PackageRelationKind DeclaredPackageRelation::kind() const noexcept {
    return kind_;
}

const std::string& DeclaredPackageRelation::raw_specification()
        const noexcept {
    return raw_specification_;
}

const std::string& DeclaredPackageRelation::target_component() const noexcept {
    return target_component_;
}

const std::optional<DependencyVersionConstraint>&
DeclaredPackageRelation::constraint() const noexcept {
    return constraint_;
}

DeclaredPackageRelationParseResult::DeclaredPackageRelationParseResult(
        DeclaredPackageRelation relation) noexcept
        : outcome_(std::move(relation)) {}

DeclaredPackageRelationParseResult::DeclaredPackageRelationParseResult(
        DependencyConstraintParseFailure failure) noexcept
        : outcome_(std::move(failure)) {}

const DeclaredPackageRelation*
DeclaredPackageRelationParseResult::relation() const noexcept {
    return std::get_if<DeclaredPackageRelation>(&outcome_);
}

const DependencyConstraintParseFailure*
DeclaredPackageRelationParseResult::failure() const noexcept {
    return std::get_if<DependencyConstraintParseFailure>(&outcome_);
}

DeclaredPackageRelationParseResult parse_declared_package_relation(
        std::string declaring_package_name, std::string declaring_package_base,
        PackageRelationKind kind, std::string_view specification) {
    const DependencyRequirementParseResult parsed =
            parse_dependency_requirement(specification);
    if(const auto* failure = parsed.failure(); failure != nullptr) {
        return DeclaredPackageRelationParseResult(
                relation_failure(*failure, specification));
    }

    const DependencyRequirement* requirement = parsed.requirement();
    const auto* package_requirement = requirement == nullptr
            ? nullptr
            : std::get_if<ConsumerDependencyRequirement>(requirement);
    if(package_requirement == nullptr) {
        return DeclaredPackageRelationParseResult(
                DependencyConstraintParseFailure{
                        DependencyConstraintParseFailureKind::
                                InvalidPackageIdentity,
                        std::string(specification)});
    }

    return DeclaredPackageRelationParseResult(DeclaredPackageRelation(
            std::move(declaring_package_name),
            std::move(declaring_package_base), kind,
            std::string(specification), package_requirement->package_name(),
            package_requirement->constraint()));
}

bool declared_package_relation_version_matches(
        const DeclaredPackageRelation& relation,
        const std::optional<std::string>& observed_component_version) noexcept {
    if(!relation.constraint().has_value()) return true;
    if(!observed_component_version.has_value()) return false;

    const DependencyVersionConstraint& constraint =
            relation.constraint().value();
    return version_relation_matches(
            constraint.relation(),
            compare_arch_package_versions(
                    observed_component_version.value(),
                    constraint.version()));
}
