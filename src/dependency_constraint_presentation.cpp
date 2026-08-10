#include "dependency_plan.hpp"

#include "localization.hpp"

#include <stdexcept>

std::string constraint_satisfaction_display(
        ConstraintSatisfaction satisfaction) {
    switch(satisfaction) {
    case ConstraintSatisfaction::Unconstrained:
        return localization::translate_message("Unconstrained");
    case ConstraintSatisfaction::Satisfied:
        return localization::translate_message("Satisfied");
    case ConstraintSatisfaction::Unsatisfied:
        return localization::translate_message("Unsatisfied");
    case ConstraintSatisfaction::Unknown:
        return localization::translate_message("Unknown");
    case ConstraintSatisfaction::Invalid:
        return localization::translate_message("Invalid");
    case ConstraintSatisfaction::Conflicting:
        return localization::translate_message("Conflicting");
    }
    throw std::logic_error(localization::translate_message(
            "Unknown constraint satisfaction result."));
}

std::string constraint_evaluation_reason_display(
        const ConstraintEvaluation& evaluation) {
    if(const auto* reason = evaluation.unknown_reason(); reason != nullptr) {
        switch(*reason) {
        case ObservedVersionUnknownReason::MissingVersionMetadata:
            return localization::translate_message("version metadata is missing");
        case ObservedVersionUnknownReason::UnversionedProviderCapability:
            return localization::translate_message(
                    "the matching provider capability has no version");
        case ObservedVersionUnknownReason::MetadataQueryFailure:
            return localization::translate_message("metadata query failed");
        case ObservedVersionUnknownReason::PartialSourceFailure:
            return localization::translate_message("source metadata is incomplete");
        case ObservedVersionUnknownReason::ComparisonAuthorityUnavailable:
            return localization::translate_message(
                    "version comparison authority is unavailable");
        case ObservedVersionUnknownReason::CandidateVersionUnavailable:
            return localization::translate_message("candidate version cannot be proven");
        case ObservedVersionUnknownReason::RelationKindNotComparable:
            return localization::translate_message(
                    "the relation is not a package-version constraint");
        }
    }
    if(const auto* reason = evaluation.invalid_reason(); reason != nullptr) {
        switch(*reason) {
        case ConstraintInvalidReason::MalformedRequirement:
            return localization::translate_message(
                    "the dependency requirement is malformed");
        case ConstraintInvalidReason::UnsupportedConsumerOperator:
            return localization::translate_message(
                    "the dependency operator is unsupported");
        case ConstraintInvalidReason::UnsupportedProviderOperator:
            return localization::translate_message(
                    "the provider operator is unsupported");
        case ConstraintInvalidReason::InvalidPackageIdentity:
            return localization::translate_message("the package identity is invalid");
        case ConstraintInvalidReason::InvalidVersionIdentity:
            return localization::translate_message("the package version is invalid");
        case ConstraintInvalidReason::InternalInvariantViolation:
            return localization::translate_message(
                    "an internal constraint invariant failed");
        }
    }
    if(const auto* reason = evaluation.conflict_reason(); reason != nullptr) {
        switch(*reason) {
        case ConstraintConflictReason::IncompatibleRequirements:
            return localization::translate_message(
                    "requirements for the same candidate conflict");
        case ConstraintConflictReason::IncompatibleProviderIdentity:
            return localization::translate_message(
                    "provider source identities conflict");
        }
    }
    switch(evaluation.satisfaction()) {
    case ConstraintSatisfaction::Unconstrained:
        return localization::translate_message("no version comparison is required");
    case ConstraintSatisfaction::Satisfied:
        return localization::translate_message(
                "the observed version satisfies the requirement");
    case ConstraintSatisfaction::Unsatisfied:
        return localization::translate_message(
                "the observed version does not satisfy the requirement");
    case ConstraintSatisfaction::Unknown:
    case ConstraintSatisfaction::Invalid:
    case ConstraintSatisfaction::Conflicting:
        break;
    }
    throw std::logic_error(localization::translate_message(
            "Constraint evaluation reason is missing."));
}
