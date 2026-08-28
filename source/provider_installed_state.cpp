#include "provider_installed_state.hpp"

#include "package_identifier.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace {

void require_valid_lookup_package_name(const std::string& package_name) {
    try {
        require_valid_package_name(package_name);
    } catch(const std::runtime_error& error) {
        // POLICY: invalid candidate identityはmetadata availabilityではないため、
        // session open前にtyped strict failureとして止める。
        throw PackageMetadataError(PackageMetadataFailure{
            PackageMetadataErrorCode::InvalidPackageName,
            error.what()});
    }
}

ProviderInstalledStateObservation observe_metadata_failure(
    PackageMetadataFailure failure) {
    if(failure.code == PackageMetadataErrorCode::InvalidPackageName) {
        throw PackageMetadataError(std::move(failure));
    }
    return ProviderInstalledStateObservation::unknown(std::move(failure));
}

} // namespace

ProviderInstalledStateObservation::ProviderInstalledStateObservation(
    ProviderInstalledState state,
    std::optional<PackageMetadataFailure> failure) noexcept
    : state_(state), failure_(std::move(failure)) {
}

ProviderInstalledStateObservation ProviderInstalledStateObservation::installed() {
    return ProviderInstalledStateObservation(ProviderInstalledState::Installed, std::nullopt);
}

ProviderInstalledStateObservation ProviderInstalledStateObservation::not_installed() {
    return ProviderInstalledStateObservation(ProviderInstalledState::NotInstalled, std::nullopt);
}

ProviderInstalledStateObservation ProviderInstalledStateObservation::unknown(
    PackageMetadataFailure failure) {
    if(failure.code == PackageMetadataErrorCode::InvalidPackageName) {
        throw std::invalid_argument(
            "Invalid package name failures must not become an installed-state observation.");
    }
    return ProviderInstalledStateObservation(
        ProviderInstalledState::Unknown, std::move(failure));
}

ProviderInstalledState ProviderInstalledStateObservation::state() const noexcept {
    return state_;
}

bool ProviderInstalledStateObservation::has_failure() const noexcept {
    return failure_.has_value();
}

const PackageMetadataFailure& ProviderInstalledStateObservation::failure() const {
    if(!failure_.has_value()) {
        throw std::logic_error("Installed-state observation has no metadata failure.");
    }
    return *failure_;
}

ProviderInstalledStateObservation ProviderInstalledStateLookup::query(
    const std::string& package_name) {
    require_valid_lookup_package_name(package_name);

    const auto cached = observations_by_package_name_.find(package_name);
    if(cached != observations_by_package_name_.end()) return cached->second;

    open_session_if_needed();
    if(session_failure_.has_value()) {
        ProviderInstalledStateObservation observation =
            ProviderInstalledStateObservation::unknown(*session_failure_);
        observations_by_package_name_.emplace(package_name, observation);
        return observation;
    }

    if(!session_.has_value()) {
        throw std::logic_error("Installed-state lookup opened without a metadata session.");
    }

    ProviderInstalledStateObservation observation = project_query_result(
        session_->query_installed_package(package_name));
    observations_by_package_name_.emplace(package_name, observation);
    return observation;
}

void ProviderInstalledStateLookup::open_session_if_needed() {
    if(session_failure_.has_value() || session_.has_value()) return;

    if(session_open_attempted_) {
        throw std::logic_error("Installed-state lookup exhausted its session open attempt.");
    }
    session_open_attempted_ = true;

    try {
        PacmanDatabasePaths paths = resolve_pacman_database_paths();
        session_.emplace(PackageMetadataSession::open(paths));
    } catch(const PackageMetadataError& error) {
        if(error.failure().code == PackageMetadataErrorCode::InvalidPackageName) throw;
        session_failure_ = error.failure();
    }
}

ProviderInstalledStateObservation ProviderInstalledStateLookup::project_query_result(
    InstalledPackageQueryResult result) const {
    if(std::get_if<InstalledPackageMetadata>(&result) != nullptr) {
        return ProviderInstalledStateObservation::installed();
    }
    if(std::get_if<PackageNotFound>(&result) != nullptr) {
        return ProviderInstalledStateObservation::not_installed();
    }
    if(PackageMetadataFailure* failure = std::get_if<PackageMetadataFailure>(&result);
       failure != nullptr) {
        return observe_metadata_failure(std::move(*failure));
    }

    throw std::logic_error("Installed package query returned an unknown result alternative.");
}
