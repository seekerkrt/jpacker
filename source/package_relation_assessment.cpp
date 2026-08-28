#include "package_relation_assessment.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace {

void retain_source(
    PackageRelationObservationSet& observations,
    const PackageRelationSourceIdentity& source) {
    if(std::find(
           observations.required_sources.begin(),
           observations.required_sources.end(), source) ==
       observations.required_sources.end()) {
        observations.required_sources.push_back(source);
    }

    const auto coverage = std::find_if(
        observations.source_identity_coverage.begin(),
        observations.source_identity_coverage.end(),
        [&source](const auto& item) { return item.source == source; });
    if(coverage == observations.source_identity_coverage.end()) {
        observations.source_identity_coverage.push_back(
            PackageRelationSourceIdentityCoverage{
                source, true, true});
        return;
    }
    coverage->exact_package_identity = true;
    coverage->provided_component_identity = true;
}

void retain_uncovered_source(
    PackageRelationObservationSet& observations,
    const PackageRelationSourceIdentity& source) {
    if(std::find(
           observations.required_sources.begin(),
           observations.required_sources.end(), source) ==
       observations.required_sources.end()) {
        observations.required_sources.push_back(source);
    }
    if(std::none_of(
           observations.source_identity_coverage.begin(),
           observations.source_identity_coverage.end(),
           [&source](const auto& coverage) {
               return coverage.source == source;
           })) {
        observations.source_identity_coverage.push_back(
            PackageRelationSourceIdentityCoverage{
                source, false, false});
    }
}

PackageRelationObservationSet installed_authority_observations(
    const PackageRelationObservationSet& installed) {
    PackageRelationObservationSet normalized = installed;
    std::erase_if(
        normalized.packages,
        [&normalized](const PackageRelationObservedPackage& package) {
            if(package.role ==
               PackageRelationObservationRole::Installed) {
                return false;
            }
            normalized.completeness =
                PackageRelationObservationCompleteness::Invalid;
            retain_uncovered_source(normalized, package.source);
            normalized.failures.push_back(
                PackageRelationObservationFailure{
                    PackageRelationObservationFailureKind::
                        InvalidIdentity,
                    package.role,
                    package.source,
                    package.package_name,
                    "Installed relation authority contains a "
                    "non-installed observation."});
            return true;
        });
    return normalized;
}

PackageRelationObservationSet planned_target_observations(
    const std::vector<PlannedPackageRelationObservation>& planned) {
    PackageRelationObservationSet observations{
        planned.empty()
            ? PackageRelationObservationCompleteness::Unavailable
            : PackageRelationObservationCompleteness::Complete,
        {},
        {},
        {},
        {}};
    observations.packages.reserve(planned.size());
    for(const auto& item : planned) {
        if(item.package.role !=
           PackageRelationObservationRole::PlannedTarget) {
            observations.completeness =
                PackageRelationObservationCompleteness::Invalid;
            retain_uncovered_source(observations, item.package.source);
            observations.failures.push_back(
                PackageRelationObservationFailure{
                    PackageRelationObservationFailureKind::
                        InvalidIdentity,
                    item.package.role,
                    item.package.source,
                    item.package.package_name,
                    "Planned relation authority contains a "
                    "non-planned observation."});
            continue;
        }
        retain_source(observations, item.package.source);
        observations.packages.push_back(item.package);
    }
    return observations;
}

PackageRelationObservationCompleteness merge_completeness(
    PackageRelationObservationCompleteness installed,
    PackageRelationObservationCompleteness planned) noexcept {
    if(installed == PackageRelationObservationCompleteness::Invalid ||
       planned == PackageRelationObservationCompleteness::Invalid) {
        return PackageRelationObservationCompleteness::Invalid;
    }
    if(installed == PackageRelationObservationCompleteness::Complete &&
       planned == PackageRelationObservationCompleteness::Complete) {
        return PackageRelationObservationCompleteness::Complete;
    }
    if(installed == PackageRelationObservationCompleteness::Unavailable &&
       planned == PackageRelationObservationCompleteness::Unavailable) {
        return PackageRelationObservationCompleteness::Unavailable;
    }
    return PackageRelationObservationCompleteness::Partial;
}

void merge_observation_set(
    PackageRelationObservationSet& destination,
    const PackageRelationObservationSet& source) {
    for(const auto& required_source : source.required_sources) {
        if(std::find(
               destination.required_sources.begin(),
               destination.required_sources.end(), required_source) ==
           destination.required_sources.end()) {
            destination.required_sources.push_back(required_source);
        }
    }
    for(const auto& source_coverage : source.source_identity_coverage) {
        const auto existing = std::find_if(
            destination.source_identity_coverage.begin(),
            destination.source_identity_coverage.end(),
            [&source_coverage](const auto& coverage) {
                return coverage.source == source_coverage.source;
            });
        if(existing == destination.source_identity_coverage.end()) {
            destination.source_identity_coverage.push_back(source_coverage);
            continue;
        }
        existing->exact_package_identity =
            existing->exact_package_identity ||
            source_coverage.exact_package_identity;
        existing->provided_component_identity =
            existing->provided_component_identity ||
            source_coverage.provided_component_identity;
    }
    destination.packages.insert(
        destination.packages.end(), source.packages.begin(),
        source.packages.end());
    destination.failures.insert(
        destination.failures.end(), source.failures.begin(),
        source.failures.end());
}

bool is_same_planned_child(
    const PackageRelationObservedPackage& declaring,
    const PackageRelationObservedPackage& candidate) noexcept {
    return candidate.role == PackageRelationObservationRole::PlannedTarget &&
           declaring.package_name == candidate.package_name &&
           declaring.package_base == candidate.package_base &&
           declaring.source == candidate.source;
}

bool is_installed_old_self(
    const PackageRelationObservedPackage& declaring,
    const PackageRelationObservedPackage& candidate) noexcept {
    // The local database package name is the installed identity authority;
    // matching Provides or PackageBase would also erase distinct packages.
    return declaring.role ==
               PackageRelationObservationRole::PlannedTarget &&
           candidate.role == PackageRelationObservationRole::Installed &&
           declaring.package_name == candidate.package_name;
}

struct ActivePackageRelationObservations {
    PackageRelationObservationSet observations;
    std::vector<PackageRelationMatchEvidence>
        filtered_old_self_invalid_evidence;
};

ActivePackageRelationObservations active_observations_for(
    const PackageRelationObservationSet& installed,
    const PackageRelationObservationSet& planned,
    const PackageRelationObservedPackage& declaring) {
    ActivePackageRelationObservations active{
        PackageRelationObservationSet{
            merge_completeness(
                installed.completeness, planned.completeness),
            {},
            {},
            {},
            {}},
        {}};
    merge_observation_set(active.observations, installed);
    merge_observation_set(active.observations, planned);

    // Installed old self is never a relation target. Preserve only its
    // structural Invalid evidence; target and version comparison remain
    // intentionally disconnected from this validation.
    for(const auto& candidate : active.observations.packages) {
        if(!is_installed_old_self(declaring, candidate)) continue;
        const auto invalid = validate_package_relation_observation(candidate);
        if(!invalid.has_value()) continue;
        active.filtered_old_self_invalid_evidence.push_back(
            PackageRelationMatchEvidence{
                candidate,
                PackageRelationIdentityMatchKind::InvalidInput,
                PackageRelationVersionMatchKind::Invalid,
                {},
                invalid});
    }
    std::erase_if(
        active.observations.packages,
        [&declaring](const PackageRelationObservedPackage& candidate) {
            return is_same_planned_child(declaring, candidate) ||
                   is_installed_old_self(declaring, candidate);
        });
    return active;
}

bool contains_version_result(
    const PackageRelationMatchEvidence& evidence,
    PackageRelationVersionMatchKind result) noexcept {
    if(evidence.version_match == result) return true;
    return std::any_of(
        evidence.provided_capability_evidence.begin(),
        evidence.provided_capability_evidence.end(),
        [result](const auto& provided) {
            return provided.version_match == result;
        });
}

bool has_invalid_evidence(
    const PackageRelationMatchEvidence& evidence) noexcept {
    return evidence.invalid_reason.has_value() ||
           evidence.identity_match ==
               PackageRelationIdentityMatchKind::InvalidInput ||
           contains_version_result(
               evidence, PackageRelationVersionMatchKind::Invalid);
}

bool has_unknown_evidence(
    const PackageRelationMatchEvidence& evidence) noexcept {
    return contains_version_result(
        evidence, PackageRelationVersionMatchKind::Unavailable);
}

PackageRelationAssessmentKind confirmed_kind(
    const DeclaredPackageRelation& declaration,
    const PackageRelationMatchEvidence& evidence) {
    if(declaration.kind() == PackageRelationKind::Replacement) {
        return PackageRelationAssessmentKind::PotentialReplacement;
    }
    return evidence.observed_package.role ==
                   PackageRelationObservationRole::Installed
               ? PackageRelationAssessmentKind::ConfirmedInstalledConflict
               : PackageRelationAssessmentKind::
                     ConfirmedPlannedTargetConflict;
}

PackageRelationAssessment make_assessment(
    const PlannedPackageRelationObservation& declaring,
    const DeclaredPackageRelation& declaration,
    PackageRelationAssessmentKind kind,
    const PackageRelationMatchingEvidence& active_evidence,
    const std::optional<PackageRelationMatchingEvidence>&
        repository_context_evidence,
    std::optional<PackageRelationMatchEvidence> package_evidence =
        std::nullopt,
    std::optional<PackageRelationObservationFailure> failure =
        std::nullopt) {
    return PackageRelationAssessment{
        declaration,
        kind,
        declaring.package,
        active_evidence,
        repository_context_evidence,
        std::move(package_evidence),
        std::move(failure)};
}

bool failure_is_invalid(
    PackageRelationObservationFailureKind kind) noexcept {
    return kind == PackageRelationObservationFailureKind::InvalidIdentity ||
           kind == PackageRelationObservationFailureKind::MalformedMetadata;
}

bool installed_authority_is_undeclared(
    const PackageRelationObservationSet& installed) noexcept {
    return installed.completeness ==
               PackageRelationObservationCompleteness::Unavailable &&
           installed.required_sources.empty() &&
           installed.source_identity_coverage.empty() &&
           installed.packages.empty() && installed.failures.empty();
}

void assess_declaration(
    std::vector<PackageRelationAssessment>& assessments,
    const PlannedPackageRelationObservation& declaring,
    const DeclaredPackageRelation& declaration,
    const ActivePackageRelationObservations& active_observations,
    const std::optional<PackageRelationObservationSet>&
        repository_context,
    bool installed_authority_missing) {
    const PackageRelationMatchingEvidence active_evidence =
        match_declared_package_relation(
            declaration, active_observations.observations);
    const std::optional<PackageRelationMatchingEvidence> context_evidence =
        repository_context.has_value()
            ? std::optional<PackageRelationMatchingEvidence>{
                  match_declared_package_relation(
                      declaration, repository_context.value())}
            : std::nullopt;

    bool has_confirmed = false;
    bool has_unknown = false;
    bool has_invalid = false;

    // The declaring child is excluded as a target, but its own snapshot still
    // has to be structurally valid. This does not treat its Provides as a
    // conflict match.
    const PackageRelationMatchEvidence declaring_validation =
        match_declared_package_relation(
            declaration, declaring.package);
    if(has_invalid_evidence(declaring_validation)) {
        assessments.push_back(make_assessment(
            declaring, declaration,
            PackageRelationAssessmentKind::Invalid,
            active_evidence, context_evidence,
            declaring_validation));
        has_invalid = true;
    }

    for(const auto& evidence :
        active_observations.filtered_old_self_invalid_evidence) {
        assessments.push_back(make_assessment(
            declaring, declaration,
            PackageRelationAssessmentKind::Invalid,
            active_evidence, context_evidence, evidence));
        has_invalid = true;
    }

    for(const auto& evidence : active_evidence.package_evidence) {
        if(package_relation_match_is_confirmed(evidence)) {
            assessments.push_back(make_assessment(
                declaring, declaration,
                confirmed_kind(declaration, evidence), active_evidence,
                context_evidence, evidence));
            has_confirmed = true;
        }
        if(has_invalid_evidence(evidence)) {
            assessments.push_back(make_assessment(
                declaring, declaration,
                PackageRelationAssessmentKind::Invalid,
                active_evidence, context_evidence, evidence));
            has_invalid = true;
        }
        if(has_unknown_evidence(evidence)) {
            assessments.push_back(make_assessment(
                declaring, declaration,
                PackageRelationAssessmentKind::Unknown,
                active_evidence, context_evidence, evidence));
            has_unknown = true;
        }
    }

    for(const auto& failure : active_evidence.observation_failures) {
        const bool is_invalid = failure_is_invalid(failure.kind);
        assessments.push_back(make_assessment(
            declaring, declaration,
            is_invalid ? PackageRelationAssessmentKind::Invalid
                       : PackageRelationAssessmentKind::Unknown,
            active_evidence, context_evidence, std::nullopt, failure));
        has_invalid = has_invalid || is_invalid;
        has_unknown = has_unknown || !is_invalid;
    }

    if(active_evidence.observation_completeness ==
       PackageRelationObservationCompleteness::Invalid) {
        if(!has_invalid) {
            assessments.push_back(make_assessment(
                declaring, declaration,
                PackageRelationAssessmentKind::Invalid,
                active_evidence, context_evidence));
            has_invalid = true;
        }
    } else if(active_evidence.observation_completeness !=
                  PackageRelationObservationCompleteness::Complete &&
              !has_unknown &&
              (!installed_authority_missing || has_confirmed)) {
        assessments.push_back(make_assessment(
            declaring, declaration,
            PackageRelationAssessmentKind::Unknown,
            active_evidence, context_evidence));
        has_unknown = true;
    }

    if(!has_confirmed && !has_unknown && !has_invalid &&
       package_relation_confirms_no_match(active_evidence)) {
        assessments.push_back(make_assessment(
            declaring, declaration,
            PackageRelationAssessmentKind::
                ConfirmedNoMatchingCurrentOrPlannedTarget,
            active_evidence, context_evidence));
        return;
    }

    if(!has_confirmed && !has_unknown && !has_invalid) {
        assessments.push_back(make_assessment(
            declaring, declaration,
            installed_authority_missing
                ? PackageRelationAssessmentKind::DeclaredRelation
                : PackageRelationAssessmentKind::Unknown,
            active_evidence, context_evidence));
    }
}

} // namespace

std::vector<PackageRelationAssessment> assess_package_relations(
    const PackageRelationObservationSet& installed_observations,
    const std::vector<PlannedPackageRelationObservation>&
        planned_observations,
    const std::optional<PackageRelationObservationSet>&
        repository_context) {
    std::vector<PackageRelationAssessment> assessments;
    const PackageRelationObservationSet installed =
        installed_authority_observations(installed_observations);
    const PackageRelationObservationSet planned =
        planned_target_observations(planned_observations);
    const bool installed_authority_missing =
        installed_authority_is_undeclared(installed);

    for(const auto& declaring : planned_observations) {
        if(declaring.declarations.empty()) continue;
        const ActivePackageRelationObservations active =
            active_observations_for(
                installed, planned,
                declaring.package);
        for(const auto& declaration : declaring.declarations) {
            assess_declaration(
                assessments, declaring, declaration, active,
                repository_context, installed_authority_missing);
        }
    }
    return assessments;
}
