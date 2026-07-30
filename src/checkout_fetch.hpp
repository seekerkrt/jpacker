#pragma once

#include "trusted_cache.hpp"

#include <string>

void fetch_persistent_checkout(
        const ValidatedCacheRoot& cache_root,
        const std::string& package_base,
        const std::string& expected_remote_url);
