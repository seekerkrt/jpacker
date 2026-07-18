#pragma once

#include <string>
#include <vector>

struct AppConfig;

int cmd_build(
        const std::vector<std::string>& args,
        const AppConfig& config);

int cmd_add_src(const std::vector<std::string>& args);

int cmd_edit_src(
        const std::vector<std::string>& targets,
        const AppConfig& config);

void cmd_list_src();

int cmd_del_src(const std::vector<std::string>& targets);

void cmd_revert(
        const std::vector<std::string>& targets,
        const AppConfig& config);

int cmd_clean(const AppConfig& config);

int cmd_upgrade(const AppConfig& config);
