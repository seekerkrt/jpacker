#include "artifact_install_plan.hpp"

#include "dependency_plan.hpp"
#include "localization.hpp"
#include "package_identifier.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require_consistent_install_reason_input(
    InstalledVersionState version_state,
    const std::optional<ExistingInstallReason>& existing_reason) {
    switch(version_state) {
        case InstalledVersionState::NotInstalled:
            if(existing_reason.has_value()) {
                throw std::logic_error(localization::translate_message(
                    "Not-installed package must not have an existing install reason."));
            }
            break;
        case InstalledVersionState::SameVersion:
        case InstalledVersionState::DifferentVersion:
            if(!existing_reason.has_value()) {
                throw std::logic_error(localization::translate_message(
                    "Installed package must have an existing install reason."));
            }
            break;
        default:
            throw std::logic_error(localization::translate_message(
                "Unknown installed version state."));
    }

    if(!existing_reason.has_value()) return;
    switch(existing_reason.value()) {
        case ExistingInstallReason::Explicit:
        case ExistingInstallReason::Dependency:
            return;
    }

    throw std::logic_error(localization::translate_message(
        "Unknown existing install reason."));
}

bool is_known_desired_install_reason(DesiredInstallReason reason) noexcept {
    switch(reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            return true;
    }
    return false;
}

bool has_artifact_selection_failures(
    const PackageBaseArtifactSelectionFailure& failure) noexcept {
    return !failure.missing_required_artifacts.empty() ||
           !failure.duplicate_produced_identities.empty() ||
           !failure.duplicate_required_targets.empty() ||
           !failure.required_reason_conflicts.empty() ||
           !failure.attribution_mismatches.empty() ||
           !failure.identity_inconsistencies.empty() ||
           !failure.unknown_install_reasons.empty();
}

} // namespace

PackageBaseArtifactSelectionResult::PackageBaseArtifactSelectionResult(
    PackageBaseArtifactSelectionSuccess success)
    : outcome_(std::in_place_type<PackageBaseArtifactSelectionSuccess>,
               std::move(success)) {
}

PackageBaseArtifactSelectionResult::PackageBaseArtifactSelectionResult(
    PackageBaseArtifactSelectionFailure failure)
    : outcome_(std::in_place_type<PackageBaseArtifactSelectionFailure>,
               std::move(failure)) {
}

bool PackageBaseArtifactSelectionResult::is_success() const noexcept {
    return std::holds_alternative<PackageBaseArtifactSelectionSuccess>(
        outcome_);
}

const PackageBaseArtifactSelectionSuccess*
PackageBaseArtifactSelectionResult::success() const noexcept {
    return std::get_if<PackageBaseArtifactSelectionSuccess>(&outcome_);
}

const PackageBaseArtifactSelectionFailure*
PackageBaseArtifactSelectionResult::failure() const noexcept {
    return std::get_if<PackageBaseArtifactSelectionFailure>(&outcome_);
}

PackageBaseArtifactSelectionResult select_package_base_artifacts(
    const PackageBaseArtifactSelectionRequest& request) {
    PackageBaseArtifactSelectionFailure failure;
    failure.package_base = request.package_base;

    const bool has_valid_package_base =
        is_valid_package_name(request.package_base);
    if(!has_valid_package_base) {
        failure.identity_inconsistencies.push_back(
            ArtifactSelectionIdentityInconsistency{
                ArtifactSelectionIdentityInput::PackageBase,
                std::nullopt, request.package_base});
    }

    std::vector<bool> consistent_required_targets(
        request.required_targets.size(), true);
    for(std::size_t i = 0; i < request.required_targets.size(); ++i) {
        const RequiredPackageArtifactTarget& target = request.required_targets[i];

        if(!is_valid_package_name(target.package_base)) {
            consistent_required_targets[i] = false;
            failure.identity_inconsistencies.push_back(
                ArtifactSelectionIdentityInconsistency{
                    ArtifactSelectionIdentityInput::RequiredPackageBase,
                    i, target.package_base});
        }
        if(target.package_base != request.package_base) {
            consistent_required_targets[i] = false;
            failure.attribution_mismatches.push_back(
                RequiredArtifactAttributionMismatch{
                    i, request.package_base, target.package_base,
                    target.package_name});
        }
        const bool has_valid_package_name =
            is_valid_package_name(target.package_name);
        if(!has_valid_package_name) {
            consistent_required_targets[i] = false;
            failure.identity_inconsistencies.push_back(
                ArtifactSelectionIdentityInconsistency{
                    ArtifactSelectionIdentityInput::RequiredPackage,
                    i, target.package_name});
        }
        if(!is_known_desired_install_reason(target.desired_reason)) {
            consistent_required_targets[i] = false;
            failure.unknown_install_reasons.push_back(
                UnknownArtifactInstallReason{i, target});
        }
    }

    std::vector<bool> selectable_required_targets =
        consistent_required_targets;
    std::vector<bool> classified_required_targets(
        request.required_targets.size(), false);
    for(std::size_t i = 0; i < request.required_targets.size(); ++i) {
        if(!consistent_required_targets[i] || classified_required_targets[i]) {
            continue;
        }

        const RequiredPackageArtifactTarget& target = request.required_targets[i];
        std::vector<std::size_t> required_target_indices;
        std::vector<RequiredArtifactReasonConflict::Occurrence> occurrences;
        for(std::size_t group_index = i;
            group_index < request.required_targets.size();
            ++group_index) {
            if(!consistent_required_targets[group_index] ||
               request.required_targets[group_index].package_name !=
                   target.package_name) {
                continue;
            }

            classified_required_targets[group_index] = true;
            required_target_indices.push_back(group_index);
            occurrences.push_back(
                RequiredArtifactReasonConflict::Occurrence{
                    group_index,
                    request.required_targets[group_index]
                        .desired_reason});
        }

        if(required_target_indices.size() == 1) continue;

        for(const std::size_t target_index : required_target_indices) {
            selectable_required_targets[target_index] = false;
        }

        bool has_reason_conflict = false;
        for(const RequiredArtifactReasonConflict::Occurrence& occurrence :
            occurrences) {
            if(occurrence.desired_reason ==
               occurrences.front().desired_reason) {
                continue;
            }
            has_reason_conflict = true;
            break;
        }

        if(has_reason_conflict) {
            failure.required_reason_conflicts.push_back(
                RequiredArtifactReasonConflict{
                    target.package_name,
                    std::move(occurrences)});
            continue;
        }

        failure.duplicate_required_targets.push_back(
            DuplicateRequiredArtifactTarget{
                target.package_name,
                occurrences.front().desired_reason,
                std::move(required_target_indices)});
    }

    std::vector<bool> unique_produced_artifacts(
        request.produced_artifacts.size(), true);
    for(std::size_t i = 0; i < request.produced_artifacts.size(); ++i) {
        const ProducedPackageArtifact& artifact = request.produced_artifacts[i];
        if(!is_valid_package_name(artifact.package_name)) {
            unique_produced_artifacts[i] = false;
            failure.identity_inconsistencies.push_back(
                ArtifactSelectionIdentityInconsistency{
                    ArtifactSelectionIdentityInput::ProducedPackage,
                    i, artifact.package_name});
            continue;
        }

        for(std::size_t prior = 0; prior < i; ++prior) {
            if(request.produced_artifacts[prior].package_name !=
               artifact.package_name) {
                continue;
            }

            unique_produced_artifacts[prior] = false;
            unique_produced_artifacts[i] = false;
            failure.duplicate_produced_identities.push_back(
                DuplicateProducedPackageIdentity{
                    prior, i, artifact.package_name});
            break;
        }
    }

    std::vector<bool> selected_produced_artifacts(
        request.produced_artifacts.size(), false);
    for(std::size_t i = 0; i < request.required_targets.size(); ++i) {
        if(!has_valid_package_base || !selectable_required_targets[i]) {
            continue;
        }

        const RequiredPackageArtifactTarget& target = request.required_targets[i];
        std::size_t matching_artifact_count = 0;
        std::size_t matching_artifact_index = 0;
        for(std::size_t artifact_index = 0;
            artifact_index < request.produced_artifacts.size();
            ++artifact_index) {
            if(request.produced_artifacts[artifact_index].package_name !=
               target.package_name) {
                continue;
            }
            ++matching_artifact_count;
            matching_artifact_index = artifact_index;
        }

        if(matching_artifact_count == 0) {
            failure.missing_required_artifacts.push_back(
                MissingRequiredArtifact{i, target});
            continue;
        }
        if(matching_artifact_count != 1 ||
           !unique_produced_artifacts[matching_artifact_index]) {
            continue;
        }

        failure.diagnostic_partial_matches.push_back(
            DiagnosticPackageArtifactMatch{
                i, matching_artifact_index,
                request.produced_artifacts[matching_artifact_index],
                target.desired_reason});
        selected_produced_artifacts[matching_artifact_index] = true;
    }

    for(std::size_t i = 0; i < request.produced_artifacts.size(); ++i) {
        if(selected_produced_artifacts[i]) continue;
        failure.diagnostic_unselected_artifacts.push_back(
            request.produced_artifacts[i]);
    }

    if(has_artifact_selection_failures(failure)) {
        return PackageBaseArtifactSelectionResult{std::move(failure)};
    }

    std::vector<SelectedPackageArtifact> selected_artifacts;
    selected_artifacts.reserve(failure.diagnostic_partial_matches.size());
    for(DiagnosticPackageArtifactMatch& match :
        failure.diagnostic_partial_matches) {
        selected_artifacts.push_back(
            SelectedPackageArtifact{
                std::move(match.artifact), match.desired_reason});
    }

    return PackageBaseArtifactSelectionResult{
        PackageBaseArtifactSelectionSuccess{
            std::move(failure.package_base),
            std::move(selected_artifacts),
            std::move(failure.diagnostic_unselected_artifacts)}};
}

ValidatedArtifactInstallTarget validate_single_output_artifact(
    const ArtifactSelectionRequest& request) {
    switch(request.source_pkgdest_state) {
        case SourcePkgdestState::Unchecked:
            throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: {} is the literal environment key "PKGDEST".
                "Source {} state has not been checked.", "PKGDEST"));
        case SourcePkgdestState::NotDefined:
            break;
        case SourcePkgdestState::Defined:
            throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: {} is the literal environment key "PKGDEST".
                "Source environment {} conflicts with the invocation-owned artifact workspace.",
                "PKGDEST"));
        default:
            throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: {} is the literal environment key "PKGDEST".
                "Unknown source {} state.", "PKGDEST"));
    }

    // POLICY(#218): shared directoryには以前の生成物が混在し得るため、今回の出力として採用しない。
    switch(request.workspace_ownership) {
        case ArtifactWorkspaceOwnership::InvocationOwnedFresh:
            break;
        case ArtifactWorkspaceOwnership::ExternalOrShared:
            throw std::runtime_error(localization::translate_message(
                "Artifact workspace must be invocation-owned and fresh."));
        default:
            throw std::logic_error(localization::translate_message(
                "Unknown artifact workspace ownership."));
    }

    if(request.package_base != request.requested_name) {
        throw std::runtime_error(localization::format_translated_message(
            // TRANSLATORS: The first placeholder is the literal Arch
            // field name "PackageBase"; the others are package identities.
            "{} does not match the requested package: {} != {}.",
            "PackageBase", request.package_base, request.requested_name));
    }

    // POLICY(#218): split/sibling/debug outputを暗黙選択せず、single-output migrationだけを許可する。
    if(request.artifacts.size() != 1) {
        throw std::runtime_error(localization::format_translated_message(
            "Expected exactly one produced package artifact, got {}.",
            request.artifacts.size()));
    }

    const ProducedPackageArtifact& artifact = request.artifacts.front();
    if(artifact.package_name != request.requested_name) {
        throw std::runtime_error(localization::format_translated_message(
            // TRANSLATORS: The placeholders are produced and requested package identities.
            "Produced artifact package name does not match the requested package: {} != {}.",
            artifact.package_name, request.requested_name));
    }

    return ValidatedArtifactInstallTarget{artifact.package_name};
}

InstallReasonDirective resolve_install_reason_directive(
    DesiredInstallReason desired_reason,
    InstalledVersionState version_state,
    std::optional<ExistingInstallReason> existing_reason,
    bool needed) {
    require_consistent_install_reason_input(version_state, existing_reason);

    InstallReasonDirective directive = InstallReasonDirective::Default;
    switch(desired_reason) {
        case DesiredInstallReason::Explicit:
            if(version_state != InstalledVersionState::NotInstalled &&
               existing_reason.value() == ExistingInstallReason::Dependency) {
                directive = InstallReasonDirective::AsExplicit;
            }
            break;
        case DesiredInstallReason::Dependency:
            if(version_state == InstalledVersionState::NotInstalled) {
                directive = InstallReasonDirective::AsDependency;
            }
            // POLICY(#218): 既存explicit packageは、dependencyとして要求されても暗黙降格しない。
            break;
        default:
            throw std::logic_error(localization::translate_message(
                "Unknown desired install reason."));
    }

    // LANDMINE(#218): same-version installがskipされる場合、reason変更を成功扱いできない。
    if(version_state == InstalledVersionState::SameVersion && needed &&
       directive != InstallReasonDirective::Default) {
        throw std::runtime_error(localization::format_translated_message(
            // TRANSLATORS: {} is the literal CLI option "--needed".
            "Cannot change the install reason because {} may skip the same-version install.",
            "--needed"));
    }

    return directive;
}

void require_supported_separated_install_options(bool rm_deps) {
    if(!rm_deps) return;

    // POLICY(#269): separated lifecycleは今回導入したdependencyのexact setを
    // 所有しない。cleanupを推測したりmakepkg -rへ変換したりせずfail closedとする。
    throw std::runtime_error(localization::format_translated_message(
        // TRANSLATORS: {} is the literal CLI option "--rmdeps".
        "Separated build/install does not support {}.", "--rmdeps"));
}
