#include "aur_update_plan.hpp"

#include <stdexcept>

AurUpdatePlanEntry classify_aur_update(const AurUpdatePlanInput& input) {
    if(const auto* aur_package = std::get_if<AurUpdateRemotePackage>(&input.aur_metadata)) {
        AurUpdateClassification classification;
        switch(aur_package->version_relation) {
        case AurVersionRelation::OlderThanInstalled:
        case AurVersionRelation::SameAsInstalled:
            classification = AurUpdateClassification::UpToDate;
            break;
        case AurVersionRelation::NewerThanInstalled:
            classification = AurUpdateClassification::UpdateAvailable;
            break;
        case AurVersionRelation::Unavailable:
            classification = AurUpdateClassification::VersionComparisonUnavailable;
            break;
        default:
            throw std::logic_error("Unknown AUR version relation.");
        }

        return AurUpdatePlanEntry{
                input.installed_name,
                input.installed_version,
                *aur_package,
                classification};
    }

    if(std::holds_alternative<AurUpdateMetadataNotFound>(input.aur_metadata)) {
        return AurUpdatePlanEntry{
                input.installed_name,
                input.installed_version,
                std::nullopt,
                AurUpdateClassification::NonAurForeign};
    }

    if(std::holds_alternative<AurUpdateMetadataUnavailable>(input.aur_metadata)) {
        return AurUpdatePlanEntry{
                input.installed_name,
                input.installed_version,
                std::nullopt,
                AurUpdateClassification::MetadataUnavailable};
    }

    throw std::logic_error("Unknown AUR update metadata result.");
}

AurUpdatePlan make_aur_update_plan(const std::vector<AurUpdatePlanInput>& inputs) {
    AurUpdatePlan plan;
    plan.entries.reserve(inputs.size());

    // POLICY(#266): pacman -Qm由来のinstalled package順をplanでも維持する。
    for(const auto& input : inputs) {
        plan.entries.push_back(classify_aur_update(input));
    }

    return plan;
}
