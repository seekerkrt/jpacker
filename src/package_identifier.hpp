#pragma once

#include <string>

bool is_valid_package_name(const std::string& name);
void require_valid_package_name(const std::string& name);
