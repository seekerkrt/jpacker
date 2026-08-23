#include "artifact_identity_selection.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// NO_TRANSLATE(Issue #308): this diagnoses a closed-result coherence bug;
// ordinary artifact selection failures leave this adapter as typed data.
[[noreturn]] void throw_incoherent_selection_result() {
    throw std::logic_error(
            "Artifact identity selection returned an incoherent result.");
}

void require_identity_set_index_coherence(
        const ArtifactPackageIdentitySet& identities) {
    const std::size_t identity_count = identities.size();
    if(identity_count == 0) throw_incoherent_selection_result();

    std::vector<bool> seen_indices(identity_count, false);
    for(std::size_t position = 0; position < identity_count; ++position) {
        const std::size_t artifact_index =
                identities.entry_at(position).artifact_index();
        if(artifact_index >= identity_count || seen_indices[artifact_index] ||
           artifact_index != position) {
            throw_incoherent_selection_result();
        }
        seen_indices[artifact_index] = true;
    }
}

std::size_t find_unique_identity_position(
        const ArtifactPackageIdentitySet& identities,
        const std::string& package_name) {
    std::size_t matching_position = identities.size();
    std::size_t matching_count = 0;
    for(std::size_t position = 0; position < identities.size(); ++position) {
        if(identities.entry_at(position).identity().package_name != package_name) {
            continue;
        }
        matching_position = position;
        ++matching_count;
    }

    if(matching_count != 1) throw_incoherent_selection_result();
    return matching_position;
}

PackageBaseArtifactIdentitySelectionFailure project_selection_failure(
        const PackageBaseArtifactSelectionFailure& failure) {
    PackageBaseArtifactIdentitySelectionFailure projected;
    projected.package_base = failure.package_base;

    projected.diagnostic_partial_matches.reserve(
            failure.diagnostic_partial_matches.size());
    for(const DiagnosticPackageArtifactMatch& match :
        failure.diagnostic_partial_matches) {
        projected.diagnostic_partial_matches.push_back(
                DiagnosticPackageArtifactIdentityMatch{
                        match.required_target_index, match.artifact,
                        match.desired_reason});
    }
    projected.diagnostic_unselected_artifacts =
            failure.diagnostic_unselected_artifacts;
    projected.missing_required_artifacts = failure.missing_required_artifacts;

    projected.duplicate_produced_identities.reserve(
            failure.duplicate_produced_identities.size());
    for(const DuplicateProducedPackageIdentity& duplicate :
        failure.duplicate_produced_identities) {
        projected.duplicate_produced_identities.push_back(
                DuplicateProducedArtifactIdentityDiagnostic{
                        duplicate.package_name});
    }

    projected.duplicate_required_targets = failure.duplicate_required_targets;
    projected.required_reason_conflicts = failure.required_reason_conflicts;
    projected.attribution_mismatches = failure.attribution_mismatches;

    projected.identity_inconsistencies.reserve(
            failure.identity_inconsistencies.size());
    for(const ArtifactSelectionIdentityInconsistency& inconsistency :
        failure.identity_inconsistencies) {
        std::optional<std::size_t> required_target_index;
        switch(inconsistency.input) {
        case ArtifactSelectionIdentityInput::PackageBase:
            if(inconsistency.input_index.has_value()) {
                throw_incoherent_selection_result();
            }
            break;
        case ArtifactSelectionIdentityInput::RequiredPackageBase:
        case ArtifactSelectionIdentityInput::RequiredPackage:
            if(!inconsistency.input_index.has_value()) {
                throw_incoherent_selection_result();
            }
            required_target_index = inconsistency.input_index;
            break;
        case ArtifactSelectionIdentityInput::ProducedPackage:
            if(!inconsistency.input_index.has_value()) {
                throw_incoherent_selection_result();
            }
            break;
        default:
            throw_incoherent_selection_result();
        }

        projected.identity_inconsistencies.push_back(
                ArtifactIdentitySelectionIdentityInconsistency{
                        inconsistency.input, required_target_index,
                        inconsistency.identity});
    }
    projected.unknown_install_reasons = failure.unknown_install_reasons;
    return projected;
}

} // namespace

PackageBaseArtifactIdentitySelectionResult::
        PackageBaseArtifactIdentitySelectionResult(
                PackageBaseArtifactIdentitySelectionSuccess success)
    : outcome_(
              std::in_place_type<PackageBaseArtifactIdentitySelectionSuccess>,
              std::move(success)) {
}

PackageBaseArtifactIdentitySelectionResult::
        PackageBaseArtifactIdentitySelectionResult(
                PackageBaseArtifactIdentitySelectionFailure failure)
    : outcome_(
              std::in_place_type<PackageBaseArtifactIdentitySelectionFailure>,
              std::move(failure)) {
}

bool PackageBaseArtifactIdentitySelectionResult::is_success() const noexcept {
    return std::holds_alternative<
            PackageBaseArtifactIdentitySelectionSuccess>(outcome_);
}

const PackageBaseArtifactIdentitySelectionSuccess*
PackageBaseArtifactIdentitySelectionResult::success() const noexcept {
    return std::get_if<PackageBaseArtifactIdentitySelectionSuccess>(&outcome_);
}

const PackageBaseArtifactIdentitySelectionFailure*
PackageBaseArtifactIdentitySelectionResult::failure() const noexcept {
    return std::get_if<PackageBaseArtifactIdentitySelectionFailure>(&outcome_);
}

PackageBaseArtifactIdentitySelectionResult
correlate_package_base_artifact_identities(
        const std::string& package_base,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ArtifactPackageIdentitySet& identities) {
    require_identity_set_index_coherence(identities);

    PackageBaseArtifactSelectionRequest request;
    request.package_base = package_base;
    request.required_targets = required_targets;
    request.produced_artifacts.reserve(identities.size());
    for(std::size_t position = 0; position < identities.size(); ++position) {
        request.produced_artifacts.push_back(ProducedPackageArtifact{
                identities.entry_at(position).identity().package_name});
    }

    PackageBaseArtifactSelectionResult selection =
            select_package_base_artifacts(request);
    if(!selection.is_success()) {
        if(selection.success() != nullptr || selection.failure() == nullptr) {
            throw_incoherent_selection_result();
        }
        // PR1のtyped diagnosticを保ちつつ、produced artifact indexだけを公開境界から除く。
        return PackageBaseArtifactIdentitySelectionResult{
                project_selection_failure(*selection.failure())};
    }

    if(selection.success() == nullptr || selection.failure() != nullptr) {
        throw_incoherent_selection_result();
    }
    const PackageBaseArtifactSelectionSuccess& selected =
            *selection.success();
    if(selected.package_base != package_base ||
       selected.selected_artifacts.size() != required_targets.size()) {
        throw_incoherent_selection_result();
    }

    std::vector<bool> selected_positions(identities.size(), false);
    std::vector<CorrelatedSelectedPackageArtifact> correlated_selected;
    correlated_selected.reserve(selected.selected_artifacts.size());
    for(std::size_t selected_index = 0;
        selected_index < selected.selected_artifacts.size();
        ++selected_index) {
        const SelectedPackageArtifact& selected_artifact =
                selected.selected_artifacts[selected_index];
        const RequiredPackageArtifactTarget& required_target =
                required_targets[selected_index];
        if(selected_artifact.artifact.package_name !=
                   required_target.package_name ||
           selected_artifact.desired_reason !=
                   required_target.desired_reason) {
            throw_incoherent_selection_result();
        }

        const std::size_t identity_position = find_unique_identity_position(
                identities, selected_artifact.artifact.package_name);
        if(selected_positions[identity_position]) {
            throw_incoherent_selection_result();
        }
        selected_positions[identity_position] = true;

        const auto& identity_entry = identities.entry_at(identity_position);
        correlated_selected.push_back(CorrelatedSelectedPackageArtifact{
                identity_entry.artifact_index(), identity_entry.identity(),
                selected_artifact.desired_reason});
    }

    if(selected.unselected_artifacts.size() !=
       identities.size() - correlated_selected.size()) {
        throw_incoherent_selection_result();
    }

    std::vector<CorrelatedUnselectedPackageArtifact> correlated_unselected;
    correlated_unselected.reserve(selected.unselected_artifacts.size());
    std::size_t selector_unselected_index = 0;
    for(std::size_t position = 0; position < identities.size(); ++position) {
        if(selected_positions[position]) continue;
        if(selector_unselected_index >= selected.unselected_artifacts.size()) {
            throw_incoherent_selection_result();
        }

        const auto& identity_entry = identities.entry_at(position);
        if(selected.unselected_artifacts[selector_unselected_index]
                   .package_name != identity_entry.identity().package_name) {
            throw_incoherent_selection_result();
        }
        correlated_unselected.push_back(
                CorrelatedUnselectedPackageArtifact{
                        identity_entry.artifact_index(),
                        identity_entry.identity()});
        ++selector_unselected_index;
    }
    if(selector_unselected_index != selected.unselected_artifacts.size() ||
       correlated_selected.size() + correlated_unselected.size() !=
               identities.size()) {
        throw_incoherent_selection_result();
    }

    return PackageBaseArtifactIdentitySelectionResult{
            PackageBaseArtifactIdentitySelectionSuccess{
                    package_base, std::move(correlated_selected),
                    std::move(correlated_unselected)}};
}
