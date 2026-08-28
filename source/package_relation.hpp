#pragma once

#include "dependency_constraint.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

enum class PackageRelationKind {
    Conflict,
    Replacement
};

class DeclaredPackageRelation final {
public:
    DeclaredPackageRelation() = delete;
    DeclaredPackageRelation(
        std::string declaring_package_name,
        std::string declaring_package_base, PackageRelationKind kind,
        std::string raw_specification, std::string target_component,
        std::optional<DependencyVersionConstraint> constraint);
    DeclaredPackageRelation(const DeclaredPackageRelation&) = default;
    DeclaredPackageRelation(DeclaredPackageRelation&&) noexcept = default;
    DeclaredPackageRelation& operator=(const DeclaredPackageRelation&) =
        default;
    DeclaredPackageRelation& operator=(DeclaredPackageRelation&&) noexcept =
        default;
    ~DeclaredPackageRelation() = default;

    [[nodiscard]] const std::string& declaring_package_name() const noexcept;
    [[nodiscard]] const std::string& declaring_package_base() const noexcept;
    [[nodiscard]] PackageRelationKind kind() const noexcept;
    [[nodiscard]] const std::string& raw_specification() const noexcept;
    [[nodiscard]] const std::string& target_component() const noexcept;
    [[nodiscard]] const std::optional<DependencyVersionConstraint>&
    constraint() const noexcept;

    bool operator==(const DeclaredPackageRelation&) const = default;

private:
    std::string declaring_package_name_;
    std::string declaring_package_base_;
    PackageRelationKind kind_;
    std::string raw_specification_;
    std::string target_component_;
    std::optional<DependencyVersionConstraint> constraint_;
};

class DeclaredPackageRelationParseResult final {
public:
    DeclaredPackageRelationParseResult() = delete;
    DeclaredPackageRelationParseResult(
        const DeclaredPackageRelationParseResult&) = default;
    DeclaredPackageRelationParseResult(
        DeclaredPackageRelationParseResult&&) noexcept = default;
    DeclaredPackageRelationParseResult& operator=(
        const DeclaredPackageRelationParseResult&) = delete;
    DeclaredPackageRelationParseResult& operator=(
        DeclaredPackageRelationParseResult&&) noexcept = delete;
    ~DeclaredPackageRelationParseResult() = default;

    [[nodiscard]] const DeclaredPackageRelation* relation() const noexcept;
    [[nodiscard]] const DependencyConstraintParseFailure* failure()
        const noexcept;

private:
    explicit DeclaredPackageRelationParseResult(
        DeclaredPackageRelation relation) noexcept;
    explicit DeclaredPackageRelationParseResult(
        DependencyConstraintParseFailure failure) noexcept;

    std::variant<DeclaredPackageRelation, DependencyConstraintParseFailure>
        outcome_;

    friend DeclaredPackageRelationParseResult parse_declared_package_relation(
        std::string declaring_package_name,
        std::string declaring_package_base, PackageRelationKind kind,
        std::string_view specification);
};

// Relation declarations have their own domain owner. The shared dependency
// grammar contributes only version constraint values and parse failures.
DeclaredPackageRelationParseResult parse_declared_package_relation(
    std::string declaring_package_name, std::string declaring_package_base,
    PackageRelationKind kind, std::string_view specification);

// Identity matching is a later observation concern. This pure predicate only
// answers whether an already matching component satisfies the declaration's
// version condition. A missing version cannot satisfy a versioned declaration.
bool declared_package_relation_version_matches(
    const DeclaredPackageRelation& relation,
    const std::optional<std::string>& observed_component_version) noexcept;
