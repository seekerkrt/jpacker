#pragma once

#include "trusted_cache.hpp"
#include "xdg_directory_safety.hpp"

// Process environmentを読むproduction adapter。cache consumerはこの境界で
// cache-only resolve -> directory prepare -> trusted adoptionを一度だけ行う。
ValidatedCacheRoot prepare_process_cache_root();
ValidatedCacheRoot prepare_process_cache_root(
    const xdg_directory_safety::DirectoryCreationPrecondition&
        creation_precondition);
