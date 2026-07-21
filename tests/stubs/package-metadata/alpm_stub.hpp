#pragma once

#include <alpm.h>

#include <cstddef>
#include <string>

namespace package_metadata_test_stub {

void reset_alpm_stub();

void set_initialize_failure(alpm_errno_t error);
void set_local_database_unavailable(alpm_errno_t error = ALPM_ERR_DB_NULL);
void set_local_database_invalid(alpm_errno_t error = ALPM_ERR_DB_INVALID);
void set_package_cache_failure(alpm_errno_t error = ALPM_ERR_DB_OPEN);
void set_empty_package_cache();

void set_package_absent();
void set_package_query_failure(alpm_errno_t error);
void set_package_query_null_without_error();
void preserve_error_on_next_package_query();
void set_package_metadata(
        const std::string& name, const std::string& version,
        alpm_pkgreason_t reason);
void set_null_package_name();
void set_null_package_version();

std::size_t initialize_call_count();
std::size_t local_database_call_count();
std::size_t database_valid_call_count();
std::size_t package_cache_call_count();
std::size_t package_query_call_count();
std::size_t created_handle_count();
std::size_t release_call_count();
std::size_t release_count_for_handle(std::size_t creation_index);

std::string last_initialize_root();
std::string last_initialize_database_path();
std::string last_queried_package_name();

} // namespace package_metadata_test_stub
