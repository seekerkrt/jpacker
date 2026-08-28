#pragma once

#include "trusted_cache.hpp"
#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"

#include <utility>

// Test-owned temporary XDG_CACHE_HOME must already exist. This helper follows
// the production cache-only resolve -> prepare -> adopt sequence without using
// the real user cache or exposing descriptors.
inline ValidatedCacheRoot prepare_test_trusted_cache_root() {
    xdg_paths::CachePaths paths =
        xdg_paths::resolve_cache_process_environment();
    xdg_directory_safety::PreparedDirectory directory =
        xdg_directory_safety::prepare_directory(paths);
    return adopt_trusted_cache_root(paths, std::move(directory));
}
