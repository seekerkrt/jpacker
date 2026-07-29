#pragma once

#include "package_metadata.hpp"

#include <cstddef>

namespace aur_update_execution_preparation_integration_stub {

void reset();
void set_database_paths(PacmanDatabasePaths database_paths);
void set_database_failure(PackageMetadataFailure failure);

std::size_t database_resolution_call_count();
std::size_t separated_option_check_call_count();
std::size_t artifact_pkgdest_check_call_count();

} // namespace aur_update_execution_preparation_integration_stub
