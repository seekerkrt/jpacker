#pragma once

#include <string>

void fetch_persistent_checkout(
        const std::string& package_base,
        const std::string& expected_remote_url);
