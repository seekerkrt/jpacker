#pragma once

#include "dependency_plan.hpp"
#include "package_metadata.hpp"
#include "source_environment.hpp"

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
    SourceBuildEnvironment custom_environment;
    SourceEnvironmentEmptyValuePolicy empty_value_policy =
            SourceEnvironmentEmptyValuePolicy::Omit;
    std::optional<SourceUpdateBaseline> update_baseline;
    std::optional<SourceInstalledSnapshot> installed_snapshot;
    bool        only_if_updated = false;
    bool        needed = false;
};

void execute_source_build(
        const SourceBuildRequest& request,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config);
