#pragma once

#include <alpm.h>

#include <cstddef>
#include <string>
#include <vector>

namespace package_metadata_test_stub {

struct SyncDatabaseRegistration {
    std::string repository_name;
    int         signature_level;
};

struct RepositoryPackageQuery {
    std::string repository_name;
    std::string package_name;
};

struct LocalPackageMetadata {
    std::string       name;
    std::string       version;
    alpm_pkgreason_t reason = ALPM_PKG_REASON_EXPLICIT;
};

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
void set_local_packages(const std::vector<LocalPackageMetadata>& packages);
void set_local_package_name_null(std::size_t package_index);
void set_local_package_version_null(std::size_t package_index);
void set_null_package_name();
void set_null_package_version();

void preserve_error_on_next_local_database(
        alpm_errno_t stale_error = ALPM_ERR_DB_OPEN);
void preserve_error_on_next_package_cache(
        alpm_errno_t stale_error = ALPM_ERR_DB_OPEN);

void set_sync_database_register_failure(
        const std::string& repository_name,
        alpm_errno_t error = ALPM_ERR_DB_OPEN);
void set_sync_database_validation_failure(
        const std::string& repository_name,
        alpm_errno_t error = ALPM_ERR_DB_INVALID);
void set_sync_database_cache_failure(
        const std::string& repository_name,
        alpm_errno_t error = ALPM_ERR_DB_OPEN);
void set_sync_database_empty_cache(const std::string& repository_name);
void preserve_error_on_next_sync_database_registration(
        const std::string& repository_name,
        alpm_errno_t stale_error = ALPM_ERR_DB_OPEN);
void preserve_error_on_next_sync_database_cache(
        const std::string& repository_name,
        alpm_errno_t stale_error = ALPM_ERR_DB_OPEN);

void set_repository_package_absent(
        const std::string& repository_name,
        const std::string& package_name);
void set_repository_package_query_failure(
        const std::string& repository_name,
        const std::string& package_name,
        alpm_errno_t error = ALPM_ERR_DB_OPEN);
void set_repository_package_query_null_without_error(
        const std::string& repository_name,
        const std::string& package_name);
void set_repository_package_metadata(
        const std::string& repository_name,
        const std::string& package_name,
        off_t package_size,
        off_t installed_size);
void set_repository_package_returned_name(
        const std::string& repository_name,
        const std::string& package_name,
        const std::string& returned_name);
void set_repository_package_name_null(
        const std::string& repository_name,
        const std::string& package_name);
void preserve_error_on_next_repository_package_query(
        const std::string& repository_name,
        const std::string& package_name,
        alpm_errno_t stale_error = ALPM_ERR_DB_OPEN);

std::size_t initialize_call_count();
std::size_t local_database_call_count();
std::size_t database_valid_call_count();
std::size_t package_cache_call_count();
std::size_t package_query_call_count();
std::size_t sync_package_cache_call_count(
        const std::string& repository_name);
std::size_t created_handle_count();
std::size_t release_call_count();
std::size_t release_count_for_handle(std::size_t creation_index);

std::string last_initialize_root();
std::string last_initialize_database_path();
std::string last_queried_package_name();

std::vector<SyncDatabaseRegistration> sync_database_registration_history();
std::vector<std::string> sync_database_operation_history();
std::vector<RepositoryPackageQuery> repository_package_query_history();

} // namespace package_metadata_test_stub
