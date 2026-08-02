#include "cache_authority.hpp"

#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"

#include <utility>

ValidatedCacheRoot prepare_process_cache_root() {
    xdg_paths::CachePaths paths =
            xdg_paths::resolve_cache_process_environment();
    xdg_directory_safety::PreparedDirectory directory =
            xdg_directory_safety::prepare_directory(paths);
    return adopt_trusted_cache_root(paths, std::move(directory));
}
