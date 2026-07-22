#pragma once

#include <optional>
#include <string>

struct AppConfig;

// upgrade baselineの有無と、snapshot時点の未installを別状態として保持する。
struct SourceUpdateBaseline {
    std::optional<std::string> installed_version;
};

// authoritative snapshotの有無はrequest側のoptionalで表し、観測済みの未installと分ける。
struct SourceInstalledSnapshot {
    std::optional<std::string> installed_version;
};

struct SourceBuildRequest {
    std::string package_name;
    std::string checkout_name;
    std::string git_url;
    std::string custom_environment;
    std::optional<SourceUpdateBaseline> update_baseline;
    std::optional<SourceInstalledSnapshot> installed_snapshot;
    bool        only_if_updated = false;
    bool        needed = false;
};

void execute_source_build(
        const SourceBuildRequest& request,
        const AppConfig& config);
