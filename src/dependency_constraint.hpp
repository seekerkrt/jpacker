#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Consumer dependency requirements and provider capabilities intentionally use
// different value types. A provider can only publish an unversioned capability
// or an equality-qualified capability; it cannot inherit the consumer grammar.
enum class DependencyVersionRelation {
    LessThan,
    LessThanOrEqual,
    Equal,
    GreaterThanOrEqual,
    GreaterThan
};

class DependencyVersionConstraint final {
public:
    DependencyVersionConstraint() = delete;
    DependencyVersionConstraint(
            DependencyVersionRelation relation, std::string version);
    DependencyVersionConstraint(const DependencyVersionConstraint&) = default;
    DependencyVersionConstraint(DependencyVersionConstraint&&) noexcept =
            default;
    DependencyVersionConstraint& operator=(
            const DependencyVersionConstraint&) = default;
    DependencyVersionConstraint& operator=(
            DependencyVersionConstraint&&) noexcept = default;
    ~DependencyVersionConstraint() = default;

    [[nodiscard]] DependencyVersionRelation relation() const noexcept;
    [[nodiscard]] const std::string& version() const noexcept;

    bool operator==(const DependencyVersionConstraint&) const = default;

private:
    DependencyVersionRelation relation_;
    std::string               version_;
};

class ConsumerDependencyRequirement final {
public:
    ConsumerDependencyRequirement() = delete;
    ConsumerDependencyRequirement(
            std::string raw_specification, std::string package_name,
            std::optional<DependencyVersionConstraint> constraint);
    ConsumerDependencyRequirement(const ConsumerDependencyRequirement&) =
            default;
    ConsumerDependencyRequirement(ConsumerDependencyRequirement&&) noexcept =
            default;
    ConsumerDependencyRequirement& operator=(
            const ConsumerDependencyRequirement&) = default;
    ConsumerDependencyRequirement& operator=(
            ConsumerDependencyRequirement&&) noexcept = default;
    ~ConsumerDependencyRequirement() = default;

    [[nodiscard]] const std::string& raw_specification() const noexcept;
    [[nodiscard]] const std::string& package_name() const noexcept;
    [[nodiscard]] const std::optional<DependencyVersionConstraint>&
    constraint() const noexcept;

    bool operator==(const ConsumerDependencyRequirement&) const = default;

private:
    std::string                                raw_specification_;
    std::string                                package_name_;
    std::optional<DependencyVersionConstraint> constraint_;
};

class ProviderCapability final {
public:
    ProviderCapability() = delete;
    ProviderCapability(
            std::string raw_specification, std::string package_name,
            std::optional<std::string> version);
    ProviderCapability(const ProviderCapability&) = default;
    ProviderCapability(ProviderCapability&&) noexcept = default;
    ProviderCapability& operator=(const ProviderCapability&) = default;
    ProviderCapability& operator=(ProviderCapability&&) noexcept = default;
    ~ProviderCapability() = default;

    [[nodiscard]] const std::string& raw_specification() const noexcept;
    [[nodiscard]] const std::string& package_name() const noexcept;
    [[nodiscard]] const std::optional<std::string>& version() const noexcept;

    bool operator==(const ProviderCapability&) const = default;

private:
    std::string                    raw_specification_;
    std::string                    package_name_;
    std::optional<std::string>     version_;
};

class SonameDependencyRequirement final {
public:
    SonameDependencyRequirement() = delete;
    SonameDependencyRequirement(
            std::string raw_specification, std::string soname);
    SonameDependencyRequirement(const SonameDependencyRequirement&) = default;
    SonameDependencyRequirement(SonameDependencyRequirement&&) noexcept =
            default;
    SonameDependencyRequirement& operator=(
            const SonameDependencyRequirement&) = default;
    SonameDependencyRequirement& operator=(
            SonameDependencyRequirement&&) noexcept = default;
    ~SonameDependencyRequirement() = default;

    [[nodiscard]] const std::string& raw_specification() const noexcept;
    [[nodiscard]] const std::string& soname() const noexcept;

    bool operator==(const SonameDependencyRequirement&) const = default;

private:
    std::string raw_specification_;
    std::string soname_;
};

using DependencyRequirement =
        std::variant<ConsumerDependencyRequirement, SonameDependencyRequirement>;

enum class DependencyConstraintParseFailureKind {
    EmptySpecification,
    InvalidPackageIdentity,
    InvalidSonameIdentity,
    UnsupportedConsumerOperator,
    UnsupportedProviderOperator,
    MissingVersion,
    InvalidVersion
};

struct DependencyConstraintParseFailure {
    DependencyConstraintParseFailureKind kind;
    std::string                          raw_specification;

    bool operator==(const DependencyConstraintParseFailure&) const = default;
};

class DependencyRequirementParseResult final {
public:
    DependencyRequirementParseResult() = delete;
    DependencyRequirementParseResult(const DependencyRequirementParseResult&) =
            default;
    DependencyRequirementParseResult(
            DependencyRequirementParseResult&&) noexcept = default;
    DependencyRequirementParseResult& operator=(
            const DependencyRequirementParseResult&) = delete;
    DependencyRequirementParseResult& operator=(
            DependencyRequirementParseResult&&) noexcept = delete;
    ~DependencyRequirementParseResult() = default;

    [[nodiscard]] const DependencyRequirement* requirement() const noexcept;
    [[nodiscard]] const DependencyConstraintParseFailure* failure()
            const noexcept;

private:
    explicit DependencyRequirementParseResult(
            DependencyRequirement requirement) noexcept;
    explicit DependencyRequirementParseResult(
            DependencyConstraintParseFailure failure) noexcept;

    std::variant<DependencyRequirement, DependencyConstraintParseFailure>
            outcome_;

    friend DependencyRequirementParseResult parse_dependency_requirement(
            std::string_view specification);
};

class ProviderCapabilityParseResult final {
public:
    ProviderCapabilityParseResult() = delete;
    ProviderCapabilityParseResult(const ProviderCapabilityParseResult&) =
            default;
    ProviderCapabilityParseResult(ProviderCapabilityParseResult&&) noexcept =
            default;
    ProviderCapabilityParseResult& operator=(
            const ProviderCapabilityParseResult&) = delete;
    ProviderCapabilityParseResult& operator=(
            ProviderCapabilityParseResult&&) noexcept = delete;
    ~ProviderCapabilityParseResult() = default;

    [[nodiscard]] const ProviderCapability* capability() const noexcept;
    [[nodiscard]] const DependencyConstraintParseFailure* failure()
            const noexcept;

private:
    explicit ProviderCapabilityParseResult(ProviderCapability capability)
            noexcept;
    explicit ProviderCapabilityParseResult(
            DependencyConstraintParseFailure failure) noexcept;

    std::variant<ProviderCapability, DependencyConstraintParseFailure>
            outcome_;

    friend ProviderCapabilityParseResult parse_provider_capability(
            std::string_view specification);
};

// The parser is a metadata trust-boundary operation. Consumers retain this
// typed value rather than reparsing raw specification text downstream.
DependencyRequirementParseResult parse_dependency_requirement(
        std::string_view specification);
ProviderCapabilityParseResult parse_provider_capability(
        std::string_view specification);

enum class ObservedVersionSource {
    InstalledExactPackage,
    RepositoryExactPackage,
    AurExactPackage,
    LocalExactPackage,
    RepositoryProviderCapability,
    AurProviderCapability,
    LocalProviderCapability
};

enum class ObservedVersionUnknownReason {
    MissingVersionMetadata,
    UnversionedProviderCapability,
    MetadataQueryFailure,
    PartialSourceFailure,
    ComparisonAuthorityUnavailable,
    CandidateVersionUnavailable,
    RelationKindNotComparable
};

enum class ConstraintInvalidReason {
    MalformedRequirement,
    UnsupportedConsumerOperator,
    UnsupportedProviderOperator,
    InvalidPackageIdentity,
    InvalidVersionIdentity,
    InternalInvariantViolation
};

class ObservedVersion final {
public:
    ObservedVersion() = delete;
    ObservedVersion(const ObservedVersion&) = default;
    ObservedVersion(ObservedVersion&&) noexcept = default;
    ObservedVersion& operator=(const ObservedVersion&) = default;
    ObservedVersion& operator=(ObservedVersion&&) noexcept = default;
    ~ObservedVersion() = default;

    [[nodiscard]] static ObservedVersion available(
            ObservedVersionSource source, std::string version);
    [[nodiscard]] static ObservedVersion unknown(
            ObservedVersionSource source,
            ObservedVersionUnknownReason unknown_reason);
    [[nodiscard]] static ObservedVersion invalid(
            ObservedVersionSource source,
            ConstraintInvalidReason invalid_reason);

    [[nodiscard]] ObservedVersionSource source() const noexcept;
    [[nodiscard]] const std::string* version() const noexcept;
    [[nodiscard]] const ObservedVersionUnknownReason* unknown_reason()
            const noexcept;
    [[nodiscard]] const ConstraintInvalidReason* invalid_reason()
            const noexcept;

    bool operator==(const ObservedVersion&) const = default;

private:
    ObservedVersion(
            ObservedVersionSource source,
            std::variant<std::string, ObservedVersionUnknownReason,
                         ConstraintInvalidReason>
                    outcome)
            noexcept;

    ObservedVersionSource
            source_;
    std::variant<std::string, ObservedVersionUnknownReason,
                 ConstraintInvalidReason>
            outcome_;
};

enum class ArchVersionOrdering {
    Less,
    Equal,
    Greater
};

// This is the only version-comparison adapter in Moguet. It deliberately
// delegates the complete Arch epoch/pkgver/pkgrel ordering to libalpm.
ArchVersionOrdering compare_arch_package_versions(
        const std::string& observed_version,
        const std::string& required_version) noexcept;

enum class ConstraintSatisfaction {
    Unconstrained,
    Satisfied,
    Unsatisfied,
    Unknown,
    Invalid,
    Conflicting
};

enum class ConstraintConflictReason {
    IncompatibleRequirements,
    IncompatibleProviderIdentity
};

class ConstraintEvaluation final {
public:
    ConstraintEvaluation() = delete;
    ConstraintEvaluation(const ConstraintEvaluation&) = default;
    ConstraintEvaluation(ConstraintEvaluation&&) noexcept = default;
    ConstraintEvaluation& operator=(const ConstraintEvaluation&) = default;
    ConstraintEvaluation& operator=(ConstraintEvaluation&&) noexcept =
            default;
    ~ConstraintEvaluation() = default;

    [[nodiscard]] static ConstraintEvaluation unconstrained() noexcept;
    [[nodiscard]] static ConstraintEvaluation satisfied() noexcept;
    [[nodiscard]] static ConstraintEvaluation unsatisfied() noexcept;
    [[nodiscard]] static ConstraintEvaluation unknown(
            ObservedVersionUnknownReason reason) noexcept;
    [[nodiscard]] static ConstraintEvaluation invalid(
            ConstraintInvalidReason reason) noexcept;

    [[nodiscard]] ConstraintSatisfaction satisfaction() const noexcept;
    [[nodiscard]] const ObservedVersionUnknownReason* unknown_reason()
            const noexcept;
    [[nodiscard]] const ConstraintInvalidReason* invalid_reason()
            const noexcept;
    [[nodiscard]] const ConstraintConflictReason* conflict_reason()
            const noexcept;

    bool operator==(const ConstraintEvaluation&) const = default;

private:
    ConstraintEvaluation(
            ConstraintSatisfaction satisfaction,
            std::optional<ObservedVersionUnknownReason> unknown_reason,
            std::optional<ConstraintInvalidReason> invalid_reason,
            std::optional<ConstraintConflictReason> conflict_reason) noexcept;

    ConstraintSatisfaction                       satisfaction_;
    std::optional<ObservedVersionUnknownReason> unknown_reason_;
    std::optional<ConstraintInvalidReason>      invalid_reason_;
    std::optional<ConstraintConflictReason>     conflict_reason_;

    friend ConstraintEvaluation
    project_conflicting_constraint_invocation(
            const std::vector<ConsumerDependencyRequirement>& requirements,
            ConstraintConflictReason reason);
};

ConstraintEvaluation evaluate_consumer_dependency_requirement(
        const ConsumerDependencyRequirement& requirement,
        const ObservedVersion& observed_version);

// Conflicting is intentionally created only by this invocation-level
// projection. An atomic observed-versus-required comparison cannot produce it.
ConstraintEvaluation project_conflicting_constraint_invocation(
        const std::vector<ConsumerDependencyRequirement>& requirements,
        ConstraintConflictReason reason);
