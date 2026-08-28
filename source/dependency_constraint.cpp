#include "dependency_constraint.hpp"

#include <alpm.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr bool is_ascii_lower(char ch) noexcept {
    return ch >= 'a' && ch <= 'z';
}

constexpr bool is_ascii_upper(char ch) noexcept {
    return ch >= 'A' && ch <= 'Z';
}

constexpr bool is_ascii_digit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

constexpr bool is_ascii_alphanumeric(char ch) noexcept {
    return is_ascii_lower(ch) || is_ascii_upper(ch) || is_ascii_digit(ch);
}

std::string trim(std::string_view value) {
    const std::size_t first = value.find_first_not_of(" \t\n\r");
    if(first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\n\r");
    return std::string(value.substr(first, last - first + 1));
}

bool is_valid_package_identity(std::string_view identity) noexcept {
    if(identity.empty() || identity.front() == '-' || identity.front() == '.') {
        return false;
    }
    return std::all_of(identity.begin(), identity.end(), [](char ch) {
        return is_ascii_alphanumeric(ch) || ch == '+' || ch == '_' ||
               ch == '.' || ch == '@' || ch == '-';
    });
}

bool is_valid_pkgver(std::string_view value) noexcept {
    if(value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](char ch) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if(byte < 0x21U || byte > 0x7eU) return false;
        return ch != ':' && ch != '/' && ch != '-' && ch != '<' &&
               ch != '>' && ch != '=';
    });
}

bool contains_only_ascii_digits(std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), is_ascii_digit);
}

bool is_valid_pkgrel(std::string_view value) noexcept {
    const std::size_t dot = value.find('.');
    if(dot == std::string_view::npos) {
        return contains_only_ascii_digits(value);
    }
    return contains_only_ascii_digits(value.substr(0, dot)) &&
           contains_only_ascii_digits(value.substr(dot + 1)) &&
           value.find('.', dot + 1) == std::string_view::npos;
}

bool is_valid_package_version(std::string_view value) noexcept {
    if(value.empty()) return false;

    std::string_view version = value;
    const std::size_t epoch_separator = version.find(':');
    if(epoch_separator != std::string_view::npos) {
        if(!contains_only_ascii_digits(version.substr(0, epoch_separator))) {
            return false;
        }
        version.remove_prefix(epoch_separator + 1);
        if(version.find(':') != std::string_view::npos) return false;
    }

    const std::size_t release_separator = version.find('-');
    if(release_separator == std::string_view::npos) {
        return is_valid_pkgver(version);
    }
    return is_valid_pkgver(version.substr(0, release_separator)) &&
           is_valid_pkgrel(version.substr(release_separator + 1));
}

bool is_valid_legacy_soname_v1_unversioned_provide(
    std::string_view capability_name,
    std::string_view version) noexcept {
    if(!capability_name.ends_with(".so") ||
       version.size() != capability_name.size() + 3 ||
       !version.starts_with(capability_name) ||
       version[capability_name.size()] != '-') {
        return false;
    }

    const std::string_view architecture =
        version.substr(capability_name.size() + 1);
    return architecture == "32" || architecture == "64";
}

bool is_valid_provider_capability_version(
    std::string_view capability_name,
    std::string_view version) noexcept {
    // ALPM provisions are a structurally ambiguous union. A value that is not
    // a confirmed SONAME v1 form may still be an ordinary component version.
    if(is_valid_legacy_soname_v1_unversioned_provide(
           capability_name, version)) {
        return true;
    }
    return is_valid_package_version(version);
}

std::string provider_capability_specification(
    const std::string& package_name,
    const std::optional<std::string>& version) {
    return version.has_value()
               ? package_name + "=" + version.value()
               : package_name;
}

bool is_valid_soname_payload(std::string_view payload) noexcept {
    if(payload.empty()) return false;
    return std::all_of(payload.begin(), payload.end(), [](char ch) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        return byte >= 0x21U && byte <= 0x7eU && ch != '/' && ch != ':' &&
               ch != '<' && ch != '>' && ch != '=';
    });
}

std::optional<DependencyVersionRelation> parse_consumer_relation(
    std::string_view specification, std::size_t operator_position,
    std::size_t& operator_size) noexcept {
    operator_size = 1;
    switch(specification[operator_position]) {
        case '<':
            if(operator_position + 1 < specification.size() &&
               specification[operator_position + 1] == '=') {
                operator_size = 2;
                return DependencyVersionRelation::LessThanOrEqual;
            }
            return DependencyVersionRelation::LessThan;
        case '=':
            return DependencyVersionRelation::Equal;
        case '>':
            if(operator_position + 1 < specification.size() &&
               specification[operator_position + 1] == '=') {
                operator_size = 2;
                return DependencyVersionRelation::GreaterThanOrEqual;
            }
            return DependencyVersionRelation::GreaterThan;
    }
    return std::nullopt;
}

DependencyConstraintParseFailure failure(
    DependencyConstraintParseFailureKind kind, std::string raw) {
    return DependencyConstraintParseFailure{kind, std::move(raw)};
}

bool relation_is_satisfied(
    DependencyVersionRelation relation, ArchVersionOrdering ordering) {
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
    throw std::logic_error("Unknown consumer dependency version relation.");
}

bool is_strict_lower_bound(DependencyVersionRelation relation) noexcept {
    return relation == DependencyVersionRelation::GreaterThan;
}

bool is_strict_upper_bound(DependencyVersionRelation relation) noexcept {
    return relation == DependencyVersionRelation::LessThan;
}

bool is_stricter_lower_bound(
    const DependencyVersionConstraint& candidate,
    const DependencyVersionConstraint& current) noexcept {
    const ArchVersionOrdering ordering = compare_arch_package_versions(
        candidate.version(), current.version());
    return ordering == ArchVersionOrdering::Greater ||
           (ordering == ArchVersionOrdering::Equal &&
            is_strict_lower_bound(candidate.relation()) &&
            !is_strict_lower_bound(current.relation()));
}

bool is_stricter_upper_bound(
    const DependencyVersionConstraint& candidate,
    const DependencyVersionConstraint& current) noexcept {
    const ArchVersionOrdering ordering = compare_arch_package_versions(
        candidate.version(), current.version());
    return ordering == ArchVersionOrdering::Less ||
           (ordering == ArchVersionOrdering::Equal &&
            is_strict_upper_bound(candidate.relation()) &&
            !is_strict_upper_bound(current.relation()));
}

} // namespace

DependencyVersionConstraint::DependencyVersionConstraint(
    DependencyVersionRelation relation, std::string version)
    : relation_(relation), version_(std::move(version)) {
}

DependencyVersionRelation DependencyVersionConstraint::relation() const noexcept {
    return relation_;
}

const std::string& DependencyVersionConstraint::version() const noexcept {
    return version_;
}

ConsumerDependencyRequirement::ConsumerDependencyRequirement(
    std::string raw_specification, std::string package_name,
    std::optional<DependencyVersionConstraint> constraint)
    : raw_specification_(std::move(raw_specification)),
      package_name_(std::move(package_name)),
      constraint_(std::move(constraint)) {
}

const std::string& ConsumerDependencyRequirement::raw_specification()
    const noexcept {
    return raw_specification_;
}

const std::string& ConsumerDependencyRequirement::package_name() const noexcept {
    return package_name_;
}

const std::optional<DependencyVersionConstraint>&
ConsumerDependencyRequirement::constraint() const noexcept {
    return constraint_;
}

ProviderCapability::ProviderCapability(
    std::string raw_specification, std::string package_name,
    std::optional<std::string> version)
    : raw_specification_(std::move(raw_specification)),
      package_name_(std::move(package_name)), version_(std::move(version)) {
}

const std::string& ProviderCapability::raw_specification() const noexcept {
    return raw_specification_;
}

const std::string& ProviderCapability::package_name() const noexcept {
    return package_name_;
}

const std::optional<std::string>& ProviderCapability::version() const noexcept {
    return version_;
}

SonameDependencyRequirement::SonameDependencyRequirement(
    std::string raw_specification, std::string soname)
    : raw_specification_(std::move(raw_specification)),
      soname_(std::move(soname)) {
}

const std::string& SonameDependencyRequirement::raw_specification()
    const noexcept {
    return raw_specification_;
}

const std::string& SonameDependencyRequirement::soname() const noexcept {
    return soname_;
}

DependencyRequirementParseResult::DependencyRequirementParseResult(
    DependencyRequirement requirement) noexcept
    : outcome_(std::move(requirement)) {
}

DependencyRequirementParseResult::DependencyRequirementParseResult(
    DependencyConstraintParseFailure failure) noexcept
    : outcome_(std::move(failure)) {
}

const DependencyRequirement* DependencyRequirementParseResult::requirement()
    const noexcept {
    return std::get_if<DependencyRequirement>(&outcome_);
}

const DependencyConstraintParseFailure*
DependencyRequirementParseResult::failure() const noexcept {
    return std::get_if<DependencyConstraintParseFailure>(&outcome_);
}

ProviderCapabilityParseResult::ProviderCapabilityParseResult(
    ProviderCapability capability) noexcept
    : outcome_(std::move(capability)) {
}

ProviderCapabilityParseResult::ProviderCapabilityParseResult(
    DependencyConstraintParseFailure failure) noexcept
    : outcome_(std::move(failure)) {
}

const ProviderCapability* ProviderCapabilityParseResult::capability()
    const noexcept {
    return std::get_if<ProviderCapability>(&outcome_);
}

const DependencyConstraintParseFailure* ProviderCapabilityParseResult::failure()
    const noexcept {
    return std::get_if<DependencyConstraintParseFailure>(&outcome_);
}

DependencyRequirementParseResult parse_dependency_requirement(
    std::string_view specification) {
    const std::string raw = trim(specification);
    if(raw.empty()) {
        return DependencyRequirementParseResult(failure(
            DependencyConstraintParseFailureKind::EmptySpecification,
            raw));
    }

    const std::size_t operator_position = raw.find_first_of("<>=");
    if(operator_position == std::string::npos) {
        const std::size_t soname_separator = raw.find(':');
        if(soname_separator != std::string::npos) {
            if(raw.find(':', soname_separator + 1) != std::string::npos ||
               !is_valid_package_identity(
                   std::string_view(raw).substr(0, soname_separator)) ||
               !is_valid_soname_payload(
                   std::string_view(raw).substr(soname_separator + 1))) {
                return DependencyRequirementParseResult(failure(
                    DependencyConstraintParseFailureKind::
                        InvalidSonameIdentity,
                    raw));
            }
            return DependencyRequirementParseResult(DependencyRequirement{
                SonameDependencyRequirement(raw, raw)});
        }
        if(!is_valid_package_identity(raw)) {
            return DependencyRequirementParseResult(failure(
                DependencyConstraintParseFailureKind::
                    InvalidPackageIdentity,
                raw));
        }
        return DependencyRequirementParseResult(DependencyRequirement{
            ConsumerDependencyRequirement(raw, raw, std::nullopt)});
    }

    const std::string package_name = trim(
        std::string_view(raw).substr(0, operator_position));
    if(!is_valid_package_identity(package_name)) {
        return DependencyRequirementParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidPackageIdentity,
            raw));
    }

    std::size_t operator_size = 0;
    const std::optional<DependencyVersionRelation> relation =
        parse_consumer_relation(raw, operator_position, operator_size);
    if(!relation.has_value()) {
        return DependencyRequirementParseResult(failure(
            DependencyConstraintParseFailureKind::
                UnsupportedConsumerOperator,
            raw));
    }
    const std::string version = trim(std::string_view(raw).substr(
        operator_position + operator_size));
    if(version.empty()) {
        return DependencyRequirementParseResult(failure(
            DependencyConstraintParseFailureKind::MissingVersion, raw));
    }
    if(version.find_first_of("<>=") != std::string::npos ||
       !is_valid_package_version(version)) {
        return DependencyRequirementParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidVersion, raw));
    }
    return DependencyRequirementParseResult(DependencyRequirement{
        ConsumerDependencyRequirement(
            raw, package_name,
            DependencyVersionConstraint(*relation, version))});
}

ProviderCapabilityParseResult parse_provider_capability(
    std::string_view specification) {
    const std::string raw = trim(specification);
    if(raw.empty()) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::EmptySpecification,
            raw));
    }

    const std::size_t operator_position = raw.find_first_of("<>=");
    if(operator_position == std::string::npos) {
        if(!is_valid_package_identity(raw)) {
            return ProviderCapabilityParseResult(failure(
                DependencyConstraintParseFailureKind::
                    InvalidPackageIdentity,
                raw));
        }
        return ProviderCapabilityParseResult(
            ProviderCapability(raw, raw, std::nullopt));
    }
    if(raw[operator_position] != '=') {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::
                UnsupportedProviderOperator,
            raw));
    }

    const std::string package_name = trim(
        std::string_view(raw).substr(0, operator_position));
    if(!is_valid_package_identity(package_name)) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidPackageIdentity,
            raw));
    }
    const std::string version = trim(
        std::string_view(raw).substr(operator_position + 1));
    if(version.empty()) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::MissingVersion, raw));
    }
    if(version.find_first_of("<>=") != std::string::npos ||
       !is_valid_package_version(version)) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidVersion, raw));
    }
    return ProviderCapabilityParseResult(
        ProviderCapability(raw, package_name, version));
}

ProviderCapabilityParseResult make_provider_capability_from_metadata(
    std::string package_name, std::optional<std::string> version) {
    const std::string raw =
        provider_capability_specification(package_name, version);
    if(!is_valid_package_identity(package_name)) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidPackageIdentity,
            raw));
    }
    if(version.has_value() && version->empty()) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::MissingVersion, raw));
    }
    if(version.has_value() &&
       (version->find_first_of("<>=") != std::string::npos ||
        !is_valid_provider_capability_version(package_name, *version))) {
        return ProviderCapabilityParseResult(failure(
            DependencyConstraintParseFailureKind::InvalidVersion, raw));
    }
    return ProviderCapabilityParseResult(ProviderCapability(
        raw, std::move(package_name), std::move(version)));
}

ObservedVersion::ObservedVersion(
    ObservedVersionSource source,
    std::variant<std::string, ObservedVersionUnknownReason,
                 ConstraintInvalidReason>
        outcome) noexcept
    : source_(source), outcome_(std::move(outcome)) {
}

ObservedVersion ObservedVersion::available(
    ObservedVersionSource source, std::string version) {
    if(!is_valid_package_version(version)) {
        return ObservedVersion::invalid(
            source, ConstraintInvalidReason::InvalidVersionIdentity);
    }
    return ObservedVersion(source, std::move(version));
}

ObservedVersion ObservedVersion::from_provider_capability(
    ObservedVersionSource source,
    const ProviderCapability& capability) {
    if(!is_valid_package_identity(capability.package_name())) {
        return ObservedVersion::invalid(
            source, ConstraintInvalidReason::InvalidPackageIdentity);
    }
    if(!capability.version().has_value()) {
        return ObservedVersion::unknown(
            source,
            ObservedVersionUnknownReason::UnversionedProviderCapability);
    }
    if(!is_valid_provider_capability_version(
           capability.package_name(), capability.version().value())) {
        return ObservedVersion::invalid(
            source, ConstraintInvalidReason::InvalidVersionIdentity);
    }
    return ObservedVersion(source, capability.version().value());
}

ObservedVersion ObservedVersion::unknown(
    ObservedVersionSource source,
    ObservedVersionUnknownReason unknown_reason) {
    return ObservedVersion(source, unknown_reason);
}

ObservedVersion ObservedVersion::invalid(
    ObservedVersionSource source, ConstraintInvalidReason invalid_reason) {
    return ObservedVersion(source, invalid_reason);
}

ObservedVersionSource ObservedVersion::source() const noexcept {
    return source_;
}

const std::string* ObservedVersion::version() const noexcept {
    return std::get_if<std::string>(&outcome_);
}

const ObservedVersionUnknownReason* ObservedVersion::unknown_reason()
    const noexcept {
    return std::get_if<ObservedVersionUnknownReason>(&outcome_);
}

const ConstraintInvalidReason* ObservedVersion::invalid_reason() const noexcept {
    return std::get_if<ConstraintInvalidReason>(&outcome_);
}

ArchVersionOrdering compare_arch_package_versions(
    const std::string& observed_version,
    const std::string& required_version) noexcept {
    const int comparison = alpm_pkg_vercmp(
        observed_version.c_str(), required_version.c_str());
    if(comparison < 0) return ArchVersionOrdering::Less;
    if(comparison > 0) return ArchVersionOrdering::Greater;
    return ArchVersionOrdering::Equal;
}

ConstraintEvaluation::ConstraintEvaluation(
    ConstraintSatisfaction satisfaction,
    std::optional<ObservedVersionUnknownReason> unknown_reason,
    std::optional<ConstraintInvalidReason> invalid_reason,
    std::optional<ConstraintConflictReason> conflict_reason) noexcept
    : satisfaction_(satisfaction),
      unknown_reason_(unknown_reason),
      invalid_reason_(invalid_reason),
      conflict_reason_(conflict_reason) {
}

ConstraintEvaluation ConstraintEvaluation::unconstrained() noexcept {
    return ConstraintEvaluation(
        ConstraintSatisfaction::Unconstrained, std::nullopt,
        std::nullopt, std::nullopt);
}

ConstraintEvaluation ConstraintEvaluation::satisfied() noexcept {
    return ConstraintEvaluation(
        ConstraintSatisfaction::Satisfied, std::nullopt, std::nullopt,
        std::nullopt);
}

ConstraintEvaluation ConstraintEvaluation::unsatisfied() noexcept {
    return ConstraintEvaluation(
        ConstraintSatisfaction::Unsatisfied, std::nullopt,
        std::nullopt, std::nullopt);
}

ConstraintEvaluation ConstraintEvaluation::unknown(
    ObservedVersionUnknownReason reason) noexcept {
    return ConstraintEvaluation(
        ConstraintSatisfaction::Unknown, reason, std::nullopt,
        std::nullopt);
}

ConstraintEvaluation ConstraintEvaluation::invalid(
    ConstraintInvalidReason reason) noexcept {
    return ConstraintEvaluation(
        ConstraintSatisfaction::Invalid, std::nullopt, reason,
        std::nullopt);
}

ConstraintSatisfaction ConstraintEvaluation::satisfaction() const noexcept {
    return satisfaction_;
}

const ObservedVersionUnknownReason* ConstraintEvaluation::unknown_reason()
    const noexcept {
    return unknown_reason_ ? &(*unknown_reason_) : nullptr;
}

const ConstraintInvalidReason* ConstraintEvaluation::invalid_reason()
    const noexcept {
    return invalid_reason_ ? &(*invalid_reason_) : nullptr;
}

const ConstraintConflictReason* ConstraintEvaluation::conflict_reason()
    const noexcept {
    return conflict_reason_ ? &(*conflict_reason_) : nullptr;
}

ConstraintEvaluation evaluate_consumer_dependency_requirement(
    const ConsumerDependencyRequirement& requirement,
    const ObservedVersion& observed_version) {
    if(!requirement.constraint().has_value()) {
        return ConstraintEvaluation::unconstrained();
    }
    if(const auto* unknown_reason = observed_version.unknown_reason()) {
        return ConstraintEvaluation::unknown(*unknown_reason);
    }
    if(const auto* invalid_reason = observed_version.invalid_reason()) {
        return ConstraintEvaluation::invalid(*invalid_reason);
    }
    const std::string* observed = observed_version.version();
    if(observed == nullptr) {
        return ConstraintEvaluation::invalid(
            ConstraintInvalidReason::InternalInvariantViolation);
    }
    const DependencyVersionConstraint& constraint =
        requirement.constraint().value();
    const ArchVersionOrdering ordering = compare_arch_package_versions(
        *observed, constraint.version());
    return relation_is_satisfied(constraint.relation(), ordering)
               ? ConstraintEvaluation::satisfied()
               : ConstraintEvaluation::unsatisfied();
}

std::optional<ConstraintEvaluation> project_conflicting_constraint_invocation(
    const std::vector<ConsumerDependencyRequirement>& requirements) {
    if(requirements.size() < 2) return std::nullopt;

    const std::string& package_name = requirements.front().package_name();
    if(std::any_of(
           requirements.begin() + 1, requirements.end(),
           [&package_name](const ConsumerDependencyRequirement& candidate) {
               return candidate.package_name() != package_name;
           })) {
        throw std::logic_error(
            "Conflicting constraint projection requires one package identity.");
    }

    const DependencyVersionConstraint* equality = nullptr;
    const DependencyVersionConstraint* lower_bound = nullptr;
    const DependencyVersionConstraint* upper_bound = nullptr;
    for(const auto& requirement : requirements) {
        if(!requirement.constraint().has_value()) continue;
        const DependencyVersionConstraint& constraint =
            requirement.constraint().value();
        switch(constraint.relation()) {
            case DependencyVersionRelation::Equal:
                if(equality != nullptr &&
                   compare_arch_package_versions(
                       equality->version(), constraint.version()) !=
                       ArchVersionOrdering::Equal) {
                    return ConstraintEvaluation(
                        ConstraintSatisfaction::Conflicting, std::nullopt,
                        std::nullopt,
                        ConstraintConflictReason::IncompatibleRequirements);
                }
                equality = &constraint;
                break;
            case DependencyVersionRelation::LessThan:
            case DependencyVersionRelation::LessThanOrEqual:
                if(upper_bound == nullptr ||
                   is_stricter_upper_bound(constraint, *upper_bound)) {
                    upper_bound = &constraint;
                }
                break;
            case DependencyVersionRelation::GreaterThanOrEqual:
            case DependencyVersionRelation::GreaterThan:
                if(lower_bound == nullptr ||
                   is_stricter_lower_bound(constraint, *lower_bound)) {
                    lower_bound = &constraint;
                }
                break;
        }
    }

    if(equality != nullptr) {
        for(const auto& requirement : requirements) {
            if(!requirement.constraint().has_value()) continue;
            const DependencyVersionConstraint& constraint =
                requirement.constraint().value();
            const ArchVersionOrdering ordering = compare_arch_package_versions(
                equality->version(), constraint.version());
            if(!relation_is_satisfied(constraint.relation(), ordering)) {
                return ConstraintEvaluation(
                    ConstraintSatisfaction::Conflicting, std::nullopt,
                    std::nullopt,
                    ConstraintConflictReason::IncompatibleRequirements);
            }
        }
        return std::nullopt;
    }

    if(lower_bound == nullptr || upper_bound == nullptr) return std::nullopt;

    const ArchVersionOrdering ordering = compare_arch_package_versions(
        lower_bound->version(), upper_bound->version());
    if(ordering == ArchVersionOrdering::Greater ||
       (ordering == ArchVersionOrdering::Equal &&
        (is_strict_lower_bound(lower_bound->relation()) ||
         is_strict_upper_bound(upper_bound->relation())))) {
        return ConstraintEvaluation(
            ConstraintSatisfaction::Conflicting, std::nullopt,
            std::nullopt,
            ConstraintConflictReason::IncompatibleRequirements);
    }
    return std::nullopt;
}
