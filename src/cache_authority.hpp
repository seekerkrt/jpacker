#pragma once

#include "trusted_cache.hpp"

// Process environmentを読むproduction adapter。cache consumerはこの境界で
// cache-only resolve -> directory prepare -> trusted adoptionを一度だけ行う。
ValidatedCacheRoot prepare_process_cache_root();
