#pragma once

#include "source_build.hpp"

#include <optional>
#include <string>

struct AppConfig;
struct BuildPlan;

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config);

void require_executable_source_install_target(
        const std::string& package_name);

void execute_aur_build_plan(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed,
        const AppConfig& config);

void install_smart_source(
        const std::string& package_name,
        bool only_if_updated,
        bool needed,
        const AppConfig& config,
        const std::optional<SourceUpdateBaseline>& update_baseline = std::nullopt,
        const std::optional<SourceInstalledSnapshot>& installed_snapshot = std::nullopt);

void preflight_upgrade_source_metadata();
