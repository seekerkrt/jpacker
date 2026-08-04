#pragma once

#include "aur_rpc.hpp"
#include "package_metadata.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace root_package_search_test_stub {

void reset();

void enqueue_repository_result(RepositoryPackageSearchResult result);
void enqueue_aur_result(std::vector<AurPackageInfo> result);
void enqueue_aur_failure(std::string diagnostic);

std::size_t repository_query_count();
std::size_t aur_query_count();
std::vector<std::string> repository_queries();
std::vector<std::string> aur_queries();

} // namespace root_package_search_test_stub
