#pragma once

#include "package_metadata.hpp"

#include <map>
#include <optional>
#include <string>

// provider候補と同名のpackageがlocal DBにあるかの、presentation専用のread-only観測値。
enum class ProviderInstalledState {
    Installed,
    NotInstalled,
    Unknown,
};

// Unknownだけがmetadata failureを保持する。候補identity、version、install reasonは保持しない。
class ProviderInstalledStateObservation {
public:
    static ProviderInstalledStateObservation installed();
    static ProviderInstalledStateObservation not_installed();
    static ProviderInstalledStateObservation unknown(PackageMetadataFailure failure);

    ProviderInstalledState state() const noexcept;
    bool has_failure() const noexcept;
    const PackageMetadataFailure& failure() const;

private:
    ProviderInstalledStateObservation(
            ProviderInstalledState state,
            std::optional<PackageMetadataFailure> failure) noexcept;

    ProviderInstalledState               state_;
    std::optional<PackageMetadataFailure> failure_;
};

// provider selection phaseでlocal package metadataをlazyに照会するowned context。
class ProviderInstalledStateLookup {
public:
    ProviderInstalledStateLookup() = default;

    ProviderInstalledStateLookup(const ProviderInstalledStateLookup&) = delete;
    ProviderInstalledStateLookup& operator=(const ProviderInstalledStateLookup&) = delete;
    ProviderInstalledStateLookup(ProviderInstalledStateLookup&&) = delete;
    ProviderInstalledStateLookup& operator=(ProviderInstalledStateLookup&&) = delete;

    ~ProviderInstalledStateLookup() noexcept = default;

    ProviderInstalledStateObservation query(const std::string& package_name);

private:
    void open_session_if_needed();
    ProviderInstalledStateObservation project_query_result(
            InstalledPackageQueryResult result) const;

    bool                                      session_open_attempted_ = false;
    std::optional<PackageMetadataSession>      session_;
    std::optional<PackageMetadataFailure>      session_failure_;
    std::map<std::string, ProviderInstalledStateObservation> observations_by_package_name_;
};
