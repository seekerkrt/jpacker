#include "artifact_identity.hpp"

#include "localization.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

IndexedArtifactPackageIdentity::IndexedArtifactPackageIdentity(
    std::size_t artifact_index,
    ArtifactPackageIdentity identity) noexcept
    : artifact_index_(artifact_index), identity_(std::move(identity)) {
}

ArtifactPackageIdentitySet::ArtifactPackageIdentitySet(
    std::vector<ArtifactPackageIdentity> identities) {
    if(identities.empty()) {
        throw std::logic_error(localization::translate_message(
            "Artifact package identity set must not be empty."));
    }

    entries_.reserve(identities.size());
    for(std::size_t index = 0; index < identities.size(); ++index) {
        entries_.push_back(IndexedArtifactPackageIdentity(
            index, std::move(identities[index])));
    }
}

void ArtifactPackageIdentitySet::require_active() const {
    if(!is_active_ || entries_.empty()) {
        throw std::runtime_error(localization::translate_message(
            "Artifact package identity set is no longer active."));
    }
}

ArtifactPackageIdentitySet::ArtifactPackageIdentitySet(
    ArtifactPackageIdentitySet&& other) noexcept
    : entries_(std::move(other.entries_)),
      is_active_(std::exchange(other.is_active_, false)) {
}

std::size_t ArtifactPackageIdentitySet::size() const {
    require_active();
    return entries_.size();
}

const IndexedArtifactPackageIdentity& ArtifactPackageIdentitySet::entry_at(
    std::size_t position) const {
    require_active();
    return entries_.at(position);
}
