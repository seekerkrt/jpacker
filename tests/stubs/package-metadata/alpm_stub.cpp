#include "alpm_stub.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct _alpm_handle_t {
    std::size_t  creation_index;
    alpm_errno_t error;
};

struct _alpm_db_t {
    alpm_handle_t* handle;
};

struct _alpm_pkg_t {
    alpm_handle_t* handle;
};

namespace {

enum class PackageLookupMode {
    Present,
    Absent,
    Failure,
    NullWithoutError
};

struct HandleRecord {
    explicit HandleRecord(std::size_t creation_index)
        : handle{creation_index, ALPM_ERR_OK},
          database{&handle},
          package{&handle},
          cache_node{&package, nullptr, nullptr} {}

    alpm_handle_t handle;
    alpm_db_t     database;
    alpm_pkg_t    package;
    alpm_list_t   cache_node;
    std::size_t   release_count = 0;
};

struct AlpmStubState {
    bool         initialize_fails = false;
    alpm_errno_t initialize_error = ALPM_ERR_SYSTEM;

    bool         local_database_available = true;
    alpm_errno_t local_database_error = ALPM_ERR_DB_NULL;
    bool         local_database_valid = true;
    alpm_errno_t local_database_valid_error = ALPM_ERR_DB_INVALID;

    bool         package_cache_fails = false;
    bool         package_cache_empty = false;
    alpm_errno_t package_cache_error = ALPM_ERR_DB_OPEN;

    PackageLookupMode package_lookup_mode = PackageLookupMode::Present;
    alpm_errno_t      package_query_error = ALPM_ERR_SYSTEM;
    bool              should_preserve_package_query_error = false;
    std::string       package_name = "test-package";
    std::string       package_version = "1.0-1";
    alpm_pkgreason_t  package_reason = ALPM_PKG_REASON_EXPLICIT;
    bool              package_name_is_null = false;
    bool              package_version_is_null = false;

    std::size_t initialize_calls = 0;
    std::size_t local_database_calls = 0;
    std::size_t database_valid_calls = 0;
    std::size_t package_cache_calls = 0;
    std::size_t package_query_calls = 0;

    std::string initialize_root;
    std::string initialize_database_path;
    std::string queried_package_name;

    std::vector<std::unique_ptr<HandleRecord>> handles;
};

AlpmStubState g_state;

void append_alpm_event(
        const char* event,
        const char* detail = nullptr) noexcept {
    const char* event_log_path =
            std::getenv("JPACKER_TEST_PACKAGE_METADATA_EVENT_LOG");
    if(event_log_path == nullptr || event_log_path[0] == '\0') return;

    // POLICY: alpm_release()はnoexcept destructorから呼ばれるため、test観測もC stdioで閉じる。
    std::FILE* event_log = std::fopen(event_log_path, "a");
    if(event_log == nullptr) return;
    if(detail == nullptr) {
        static_cast<void>(std::fprintf(event_log, "%s\n", event));
    } else {
        static_cast<void>(std::fprintf(event_log, "%s %s\n", event, detail));
    }
    static_cast<void>(std::fclose(event_log));
}

bool environment_flag_is_enabled(const char* name) noexcept {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool environment_requests_query_failure(const char* package_name) noexcept {
    const char* failure_package =
            std::getenv("JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE");
    if(failure_package != nullptr && package_name != nullptr &&
       std::strcmp(failure_package, package_name) == 0) {
        return true;
    }

    const char* failure_ordinal_text =
            std::getenv("JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_AT");
    if(failure_ordinal_text == nullptr || failure_ordinal_text[0] == '\0') return false;

    char* ordinal_end = nullptr;
    errno = 0;
    unsigned long long failure_ordinal =
            std::strtoull(failure_ordinal_text, &ordinal_end, 10);
    if(errno != 0 || ordinal_end == failure_ordinal_text || ordinal_end[0] != '\0') {
        return false;
    }
    return failure_ordinal ==
           static_cast<unsigned long long>(g_state.package_query_calls);
}

void configure_package_lookup_from_environment(
        const char* queried_package_name,
        PackageLookupMode& lookup_mode,
        alpm_errno_t& query_error) {
    if(environment_requests_query_failure(queried_package_name)) {
        lookup_mode = PackageLookupMode::Failure;
        query_error = ALPM_ERR_DB_OPEN;
        return;
    }

    const char* state_file_path =
            std::getenv("JPACKER_TEST_PACKAGE_METADATA_STATE_FILE");
    if(state_file_path == nullptr) return;

    std::ifstream state_file(state_file_path);
    if(!state_file) {
        lookup_mode = PackageLookupMode::Failure;
        query_error = ALPM_ERR_DB_OPEN;
        return;
    }

    std::string package_name;
    std::string package_version;
    while(state_file >> package_name >> package_version) {
        if(package_name != queried_package_name) continue;

        lookup_mode = PackageLookupMode::Present;
        g_state.package_name = std::move(package_name);
        g_state.package_version = std::move(package_version);
        g_state.package_reason = ALPM_PKG_REASON_EXPLICIT;
        g_state.package_name_is_null = false;
        g_state.package_version_is_null = false;
        return;
    }

    if(state_file.bad()) {
        lookup_mode = PackageLookupMode::Failure;
        query_error = ALPM_ERR_DB_OPEN;
        return;
    }
    lookup_mode = PackageLookupMode::Absent;
}

HandleRecord* record_for_handle(alpm_handle_t* handle) {
    if(handle == nullptr || handle->creation_index >= g_state.handles.size()) return nullptr;
    HandleRecord* record = g_state.handles[handle->creation_index].get();
    if(&record->handle != handle) return nullptr;
    return record;
}

void set_handle_error(alpm_handle_t* handle, alpm_errno_t error) {
    if(handle != nullptr) handle->error = error;
}

} // namespace

namespace package_metadata_test_stub {

void reset_alpm_stub() {
    g_state = AlpmStubState{};
}

void set_initialize_failure(alpm_errno_t error) {
    g_state.initialize_fails = true;
    g_state.initialize_error = error;
}

void set_local_database_unavailable(alpm_errno_t error) {
    g_state.local_database_available = false;
    g_state.local_database_error = error;
}

void set_local_database_invalid(alpm_errno_t error) {
    g_state.local_database_valid = false;
    g_state.local_database_valid_error = error;
}

void set_package_cache_failure(alpm_errno_t error) {
    g_state.package_cache_fails = true;
    g_state.package_cache_error = error;
}

void set_empty_package_cache() {
    g_state.package_cache_empty = true;
}

void set_package_absent() {
    g_state.package_lookup_mode = PackageLookupMode::Absent;
}

void set_package_query_failure(alpm_errno_t error) {
    g_state.package_lookup_mode = PackageLookupMode::Failure;
    g_state.package_query_error = error;
}

void set_package_query_null_without_error() {
    g_state.package_lookup_mode = PackageLookupMode::NullWithoutError;
}

void preserve_error_on_next_package_query() {
    g_state.should_preserve_package_query_error = true;
}

void set_package_metadata(
        const std::string& name, const std::string& version,
        alpm_pkgreason_t reason) {
    g_state.package_lookup_mode = PackageLookupMode::Present;
    g_state.package_name = name;
    g_state.package_version = version;
    g_state.package_reason = reason;
    g_state.package_name_is_null = false;
    g_state.package_version_is_null = false;
}

void set_null_package_name() {
    g_state.package_lookup_mode = PackageLookupMode::Present;
    g_state.package_name_is_null = true;
}

void set_null_package_version() {
    g_state.package_lookup_mode = PackageLookupMode::Present;
    g_state.package_version_is_null = true;
}

std::size_t initialize_call_count() {
    return g_state.initialize_calls;
}

std::size_t local_database_call_count() {
    return g_state.local_database_calls;
}

std::size_t database_valid_call_count() {
    return g_state.database_valid_calls;
}

std::size_t package_cache_call_count() {
    return g_state.package_cache_calls;
}

std::size_t package_query_call_count() {
    return g_state.package_query_calls;
}

std::size_t created_handle_count() {
    return g_state.handles.size();
}

std::size_t release_call_count() {
    std::size_t count = 0;
    for(const auto& record : g_state.handles) count += record->release_count;
    return count;
}

std::size_t release_count_for_handle(std::size_t creation_index) {
    if(creation_index >= g_state.handles.size()) return 0;
    return g_state.handles[creation_index]->release_count;
}

std::string last_initialize_root() {
    return g_state.initialize_root;
}

std::string last_initialize_database_path() {
    return g_state.initialize_database_path;
}

std::string last_queried_package_name() {
    return g_state.queried_package_name;
}

} // namespace package_metadata_test_stub

extern "C" {

alpm_handle_t* alpm_initialize(
        const char* root, const char* database_path, alpm_errno_t* error) {
    append_alpm_event("alpm initialize");
    ++g_state.initialize_calls;
    g_state.initialize_root = root == nullptr ? "" : root;
    g_state.initialize_database_path = database_path == nullptr ? "" : database_path;

    bool environment_failure = environment_flag_is_enabled(
            "JPACKER_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE");
    if(environment_failure || g_state.initialize_fails) {
        if(error != nullptr) {
            *error = environment_failure ? ALPM_ERR_SYSTEM : g_state.initialize_error;
        }
        return nullptr;
    }

    auto record = std::make_unique<HandleRecord>(g_state.handles.size());
    alpm_handle_t* handle = &record->handle;
    g_state.handles.push_back(std::move(record));
    if(error != nullptr) *error = ALPM_ERR_OK;
    return handle;
}

int alpm_release(alpm_handle_t* handle) {
    append_alpm_event("alpm release");
    HandleRecord* record = record_for_handle(handle);
    if(record == nullptr) return -1;
    ++record->release_count;
    return 0;
}

alpm_errno_t alpm_errno(alpm_handle_t* handle) {
    if(handle == nullptr) return ALPM_ERR_HANDLE_NULL;
    return handle->error;
}

const char* alpm_strerror(alpm_errno_t error) {
    switch(error) {
        case ALPM_ERR_OK:
            return "no error";
        case ALPM_ERR_SYSTEM:
            return "system error";
        case ALPM_ERR_WRONG_ARGS:
            return "wrong arguments";
        case ALPM_ERR_DB_NULL:
            return "database unavailable";
        case ALPM_ERR_DB_OPEN:
            return "database open failed";
        case ALPM_ERR_DB_INVALID:
            return "invalid database";
        case ALPM_ERR_PKG_NOT_FOUND:
            return "package not found";
        default:
            return "stub libalpm error";
    }
}

alpm_db_t* alpm_get_localdb(alpm_handle_t* handle) {
    ++g_state.local_database_calls;
    HandleRecord* record = record_for_handle(handle);
    if(record == nullptr) return nullptr;
    set_handle_error(handle, ALPM_ERR_OK);
    if(!g_state.local_database_available) {
        set_handle_error(handle, g_state.local_database_error);
        return nullptr;
    }
    return &record->database;
}

int alpm_db_get_valid(alpm_db_t* database) {
    ++g_state.database_valid_calls;
    if(database == nullptr) return -1;
    set_handle_error(database->handle, ALPM_ERR_OK);
    if(!g_state.local_database_valid) {
        set_handle_error(database->handle, g_state.local_database_valid_error);
        return -1;
    }
    return 0;
}

alpm_list_t* alpm_db_get_pkgcache(alpm_db_t* database) {
    ++g_state.package_cache_calls;
    if(database == nullptr) return nullptr;
    set_handle_error(database->handle, ALPM_ERR_OK);
    if(g_state.package_cache_fails) {
        set_handle_error(database->handle, g_state.package_cache_error);
        return nullptr;
    }
    if(g_state.package_cache_empty) return nullptr;

    HandleRecord* record = record_for_handle(database->handle);
    return record == nullptr ? nullptr : &record->cache_node;
}

alpm_pkg_t* alpm_db_get_pkg(alpm_db_t* database, const char* name) {
    append_alpm_event("alpm query", name == nullptr ? "" : name);
    ++g_state.package_query_calls;
    if(database == nullptr) return nullptr;
    if(g_state.should_preserve_package_query_error) {
        g_state.should_preserve_package_query_error = false;
    } else {
        set_handle_error(database->handle, ALPM_ERR_OK);
    }
    g_state.queried_package_name = name == nullptr ? "" : name;

    if(name == nullptr || name[0] == '\0') {
        set_handle_error(database->handle, ALPM_ERR_WRONG_ARGS);
        return nullptr;
    }

    PackageLookupMode lookup_mode = g_state.package_lookup_mode;
    alpm_errno_t query_error = g_state.package_query_error;
    try {
        configure_package_lookup_from_environment(name, lookup_mode, query_error);
    } catch(...) {
        // Test fixtureの読込自体が失敗しても、package absenceへflattenしない。
        lookup_mode = PackageLookupMode::Failure;
        query_error = ALPM_ERR_DB_OPEN;
    }

    switch(lookup_mode) {
        case PackageLookupMode::Present: {
            HandleRecord* record = record_for_handle(database->handle);
            return record == nullptr ? nullptr : &record->package;
        }
        case PackageLookupMode::Absent:
            set_handle_error(database->handle, ALPM_ERR_PKG_NOT_FOUND);
            return nullptr;
        case PackageLookupMode::Failure:
            set_handle_error(database->handle, query_error);
            return nullptr;
        case PackageLookupMode::NullWithoutError:
            return nullptr;
    }
    return nullptr;
}

const char* alpm_pkg_get_name(alpm_pkg_t* package) {
    if(package == nullptr) return nullptr;
    set_handle_error(package->handle, ALPM_ERR_OK);
    if(g_state.package_name_is_null) return nullptr;
    return g_state.package_name.c_str();
}

const char* alpm_pkg_get_version(alpm_pkg_t* package) {
    if(package == nullptr) return nullptr;
    set_handle_error(package->handle, ALPM_ERR_OK);
    if(g_state.package_version_is_null) return nullptr;
    return g_state.package_version.c_str();
}

alpm_pkgreason_t alpm_pkg_get_reason(alpm_pkg_t* package) {
    if(package == nullptr) return static_cast<alpm_pkgreason_t>(-1);
    set_handle_error(package->handle, ALPM_ERR_OK);
    return g_state.package_reason;
}

} // extern "C"
