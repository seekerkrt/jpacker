#pragma once

#include <string>
#include <vector>

int cmd_deps(
        const std::vector<std::string>& targets,
        const std::vector<std::string>& flags);

int cmd_plan(
        const std::vector<std::string>& targets,
        const std::vector<std::string>& flags);

int cmd_fetch(
        const std::vector<std::string>& targets,
        const std::vector<std::string>& flags);

int cmd_export_pkgbuild_tree(const std::string& target);
int cmd_print_pkgbuild(const std::string& target);
int cmd_query_foreign_updates();
