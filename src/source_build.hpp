#pragma once

#include <string>

struct AppConfig;

struct SourceBuildRequest {
    std::string package_name;
    std::string checkout_name;
    std::string git_url;
    std::string custom_environment;
    bool        only_if_updated = false;
    bool        needed = false;
};

void execute_source_build(
        const SourceBuildRequest& request,
        const AppConfig& config);
