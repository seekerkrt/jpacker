#include "package_metadata.hpp"

#include "package_identifier.hpp"
#include "process.hpp"

#include <alpm.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char* PACMAN_DATABASE_PATH_COMMAND =
        "pacman-conf --verbose RootDir DBPath 2>/dev/null";
constexpr const char* PACMAN_REPOSITORY_LIST_COMMAND =
        "pacman-conf --repo-list 2>/dev/null";
constexpr std::size_t MAX_ALPM_DIAGNOSTIC_LENGTH = 160;

// POLICY: pacman-managed on-disk sync DBをread-onlyで構造/cache parseする。
// pacman.confのsignature policyを再実行するものではない。
constexpr alpm_siglevel_t READ_ONLY_SYNC_DATABASE_SIGLEVEL =
        static_cast<alpm_siglevel_t>(0);

[[noreturn]] void throw_package_metadata_error(
        PackageMetadataErrorCode code,
        const std::string& diagnostic) {
    throw PackageMetadataError(PackageMetadataFailure{code, diagnostic});
}

fs::path normalize_absolute_path(
        const fs::path& path,
        const std::string& field_name) {
    if(path.empty() || !path.is_absolute()) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::ConfigurationMalformed,
                field_name + " must be an absolute path.");
    }
    if(path.string().find('\0') != std::string::npos) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::ConfigurationMalformed,
                field_name + " must not contain a null byte.");
    }

    fs::path normalized = path.lexically_normal();
    // POLICY: pacman-confの末尾slashには意味を持たせないが、root path自体は保持する。
    while(normalized != normalized.root_path() && normalized.filename().empty()) {
        normalized = normalized.parent_path();
    }
    return normalized;
}

PacmanDatabasePaths normalize_pacman_database_paths(
        const PacmanDatabasePaths& paths) {
    return PacmanDatabasePaths{
            normalize_absolute_path(paths.root_dir, "RootDir"),
            normalize_absolute_path(paths.db_path, "DBPath")};
}

PacmanDatabasePaths parse_pacman_database_paths(const std::string& output) {
    std::string root_dir;
    std::string db_path;
    bool        has_root_dir = false;
    bool        has_db_path = false;

    std::stringstream output_stream(output);
    std::string       line;
    while(std::getline(output_stream, line)) {
        if(line.empty() || line.find('\r') != std::string::npos) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "pacman-conf returned a malformed database path line.");
        }

        const std::string separator = " = ";
        std::size_t       separator_position = line.find(separator);
        if(separator_position == std::string::npos) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "pacman-conf returned a malformed database path line.");
        }

        std::string key = line.substr(0, separator_position);
        std::string value = line.substr(separator_position + separator.size());
        if(value.empty()) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "pacman-conf returned an empty database path.");
        }

        if(key == "RootDir") {
            if(has_root_dir) {
                throw_package_metadata_error(
                        PackageMetadataErrorCode::ConfigurationMalformed,
                        "pacman-conf returned RootDir more than once.");
            }
            has_root_dir = true;
            root_dir = std::move(value);
        } else if(key == "DBPath") {
            if(has_db_path) {
                throw_package_metadata_error(
                        PackageMetadataErrorCode::ConfigurationMalformed,
                        "pacman-conf returned DBPath more than once.");
            }
            has_db_path = true;
            db_path = std::move(value);
        } else {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "pacman-conf returned an unexpected database path key.");
        }
    }

    if(!has_root_dir || !has_db_path) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::ConfigurationMalformed,
                "pacman-conf did not return both RootDir and DBPath.");
    }

    return normalize_pacman_database_paths(
            PacmanDatabasePaths{fs::path(root_dir), fs::path(db_path)});
}

bool contains_control_character(const std::string& value) {
    for(unsigned char character : value) {
        if(std::iscntrl(character)) return true;
    }
    return false;
}

void validate_repository_names(const std::vector<std::string>& repository_names) {
    std::set<std::string> seen_repository_names;
    for(const auto& repository_name : repository_names) {
        if(repository_name.empty() || contains_control_character(repository_name)) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "Repository configuration contains an invalid repository name.");
        }
        if(!seen_repository_names.insert(repository_name).second) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "Repository configuration contains a duplicate repository name.");
        }
    }
}

std::vector<std::string> parse_pacman_repository_names(
        const std::string& output) {
    std::vector<std::string> repository_names;
    std::stringstream        output_stream(output);
    std::string              line;
    while(std::getline(output_stream, line)) {
        if(line.empty() || contains_control_character(line)) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::ConfigurationMalformed,
                    "pacman-conf returned a malformed repository list.");
        }
        repository_names.push_back(std::move(line));
    }
    validate_repository_names(repository_names);
    return repository_names;
}

PacmanRepositoryConfiguration normalize_pacman_repository_configuration(
        const PacmanRepositoryConfiguration& configuration) {
    validate_repository_names(configuration.repository_names);
    return PacmanRepositoryConfiguration{
            normalize_pacman_database_paths(configuration.database_paths),
            configuration.repository_names};
}

std::string bounded_alpm_error_text(alpm_errno_t error_code) {
    const char* raw_text = alpm_strerror(error_code);
    if(raw_text == nullptr || raw_text[0] == '\0') return "unknown libalpm error";

    std::string bounded_text;
    for(std::size_t index = 0;
        raw_text[index] != '\0' && index < MAX_ALPM_DIAGNOSTIC_LENGTH;
        ++index) {
        unsigned char character = static_cast<unsigned char>(raw_text[index]);
        bounded_text.push_back(std::iscntrl(character) ? ' ' : raw_text[index]);
    }
    if(raw_text[bounded_text.size()] != '\0') bounded_text += "...";
    return bounded_text;
}

std::string alpm_failure_diagnostic(
        const std::string& context,
        alpm_errno_t error_code) {
    if(error_code == ALPM_ERR_OK) {
        return context + ": libalpm reported no error detail.";
    }
    return context + ": " + bounded_alpm_error_text(error_code) + ".";
}

struct AlpmHandleReleaser {
    void operator()(alpm_handle_t* handle) const noexcept {
        if(handle != nullptr) static_cast<void>(alpm_release(handle));
    }
};

using UniqueAlpmHandle = std::unique_ptr<alpm_handle_t, AlpmHandleReleaser>;

InstalledPackageReason map_install_reason(alpm_pkgreason_t reason) {
    switch(reason) {
        case ALPM_PKG_REASON_EXPLICIT:
            return InstalledPackageReason::Explicit;
        case ALPM_PKG_REASON_DEPEND:
            return InstalledPackageReason::Dependency;
        case ALPM_PKG_REASON_UNKNOWN:
        default:
            // POLICY: malformed/将来値をExplicitやDependencyへ推測変換しない。
            return InstalledPackageReason::Unknown;
    }
}

PackageMetadataFailure query_failure(
        PackageMetadataErrorCode code,
        const std::string& diagnostic) {
    return PackageMetadataFailure{code, diagnostic};
}

RepositoryPackageQueryResult query_repository_database(
        alpm_handle_t* handle,
        alpm_db_t* database,
        const std::string& repository_name,
        const std::string& package_name) {
    alpm_pkg_t* package = alpm_db_get_pkg(database, package_name.c_str());
    if(package == nullptr) {
        alpm_errno_t query_error = alpm_errno(handle);
        if(query_error == ALPM_ERR_PKG_NOT_FOUND) return PackageNotFound{};
        return query_failure(
                PackageMetadataErrorCode::QueryFailed,
                alpm_failure_diagnostic(
                        "Repository package query failed",
                        query_error));
    }

    // POLICY: libalpmのnonnull returnがquery成功であり、以前の失敗errnoは参照しない。
    const char* returned_name = alpm_pkg_get_name(package);
    if(returned_name == nullptr || package_name != returned_name) {
        return query_failure(
                PackageMetadataErrorCode::MalformedMetadata,
                "Repository package metadata contains an invalid package name.");
    }

    off_t package_size = alpm_pkg_get_size(package);
    off_t installed_size = alpm_pkg_get_isize(package);
    if(package_size < static_cast<off_t>(0) ||
       installed_size < static_cast<off_t>(0)) {
        return query_failure(
                PackageMetadataErrorCode::MalformedMetadata,
                "Repository package metadata contains an invalid package size.");
    }

    return RepositoryPackageMetadata{
            repository_name,
            returned_name,
            static_cast<std::uint64_t>(package_size),
            static_cast<std::uint64_t>(installed_size)};
}

ForeignPackageInventory query_foreign_package_inventory_read_phase(
        const PacmanRepositoryConfiguration& configuration) {
    PacmanRepositoryConfiguration normalized_configuration =
            normalize_pacman_repository_configuration(configuration);
    if(normalized_configuration.repository_names.empty()) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::RepositoryNotConfigured,
                "No package repositories are configured for foreign package inventory.");
    }

    std::string root_dir = normalized_configuration.database_paths.root_dir.string();
    std::string db_path = normalized_configuration.database_paths.db_path.string();

    alpm_errno_t initialization_error = ALPM_ERR_OK;
    alpm_handle_t* raw_handle =
            alpm_initialize(root_dir.c_str(), db_path.c_str(), &initialization_error);
    if(raw_handle == nullptr) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::InitializationFailed,
                alpm_failure_diagnostic(
                        "Failed to initialize foreign package inventory",
                        initialization_error));
    }
    UniqueAlpmHandle handle(raw_handle);

    // POLICY: nonnull pointerがAPI上の成功条件。handle-global errnoは成功時に
    // 以前の値を保持し得るため、失敗時のdiagnosticにだけ使用する。
    alpm_db_t* local_db = alpm_get_localdb(handle.get());
    if(local_db == nullptr) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                alpm_failure_diagnostic(
                        "Failed to access the local package database",
                        alpm_errno(handle.get())));
    }

    if(alpm_db_get_valid(local_db) != 0) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                alpm_failure_diagnostic(
                        "Local package database validation failed",
                        alpm_errno(handle.get())));
    }

    std::vector<alpm_db_t*> sync_databases;
    sync_databases.reserve(normalized_configuration.repository_names.size());
    for(const auto& repository_name : normalized_configuration.repository_names) {
        alpm_db_t* database = alpm_register_syncdb(
                handle.get(), repository_name.c_str(),
                READ_ONLY_SYNC_DATABASE_SIGLEVEL);
        if(database == nullptr) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Failed to register repository package database '" +
                                    repository_name + "'",
                            alpm_errno(handle.get())));
        }
        sync_databases.push_back(database);
    }

    for(alpm_db_t* database : sync_databases) {
        if(alpm_db_get_valid(database) != 0) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Repository package database validation failed",
                            alpm_errno(handle.get())));
        }
    }

    // POLICY(#266): cacheをlookup前に全件loadし、正常emptyとload failureを
    // nullptr直後のerrnoで区別する。local listはこのreturn値を一度だけ走査する。
    alpm_list_t* local_package_cache = alpm_db_get_pkgcache(local_db);
    if(local_package_cache == nullptr) {
        alpm_errno_t package_cache_error = alpm_errno(handle.get());
        if(package_cache_error != ALPM_ERR_OK) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::LocalDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Failed to load the local package database",
                            package_cache_error));
        }
    }

    for(alpm_db_t* database : sync_databases) {
        alpm_list_t* package_cache = alpm_db_get_pkgcache(database);
        if(package_cache == nullptr) {
            alpm_errno_t package_cache_error = alpm_errno(handle.get());
            if(package_cache_error != ALPM_ERR_OK) {
                throw_package_metadata_error(
                        PackageMetadataErrorCode::SyncDatabaseUnavailable,
                        alpm_failure_diagnostic(
                                "Failed to load a repository package database",
                                package_cache_error));
            }
        }
    }

    ForeignPackageInventory inventory;
    // POLICY(#266): pacman -Qmと同じくlocal cache順を正本にし、独自sortしない。
    for(alpm_list_t* node = local_package_cache; node != nullptr; node = node->next) {
        if(node->data == nullptr) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::QueryFailed,
                    "Local package cache contains an invalid package entry.");
        }

        auto* local_package = static_cast<alpm_pkg_t*>(node->data);
        const char* raw_package_name = alpm_pkg_get_name(local_package);
        if(raw_package_name == nullptr || raw_package_name[0] == '\0') {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::MalformedMetadata,
                    "Local package metadata contains an invalid package name.");
        }

        std::string package_name(raw_package_name);
        if(!is_valid_package_name(package_name)) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::MalformedMetadata,
                    "Local package metadata contains an invalid package name.");
        }

        bool is_native_package = false;
        for(alpm_db_t* database : sync_databases) {
            alpm_pkg_t* sync_package =
                    alpm_db_get_pkg(database, package_name.c_str());
            if(sync_package == nullptr) {
                alpm_errno_t query_error = alpm_errno(handle.get());
                if(query_error == ALPM_ERR_PKG_NOT_FOUND) continue;
                throw_package_metadata_error(
                        PackageMetadataErrorCode::QueryFailed,
                        alpm_failure_diagnostic(
                                "Repository package membership query failed",
                                query_error));
            }

            const char* returned_name = alpm_pkg_get_name(sync_package);
            if(returned_name == nullptr || returned_name[0] == '\0') {
                throw_package_metadata_error(
                        PackageMetadataErrorCode::MalformedMetadata,
                        "Repository package metadata contains an invalid package name.");
            }
            std::string returned_package_name(returned_name);
            if(!is_valid_package_name(returned_package_name) ||
               package_name != returned_package_name) {
                throw_package_metadata_error(
                        PackageMetadataErrorCode::MalformedMetadata,
                        "Repository package metadata contains an invalid package name.");
            }
            is_native_package = true;
            break;
        }
        if(is_native_package) continue;

        const char* installed_version = alpm_pkg_get_version(local_package);
        if(installed_version == nullptr || installed_version[0] == '\0') {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::MalformedMetadata,
                    "Foreign package metadata contains an invalid version.");
        }

        inventory.push_back(InstalledPackageMetadata{
                std::move(package_name),
                installed_version,
                map_install_reason(alpm_pkg_get_reason(local_package))});
    }
    return inventory;
}

} // namespace

PackageMetadataError::PackageMetadataError(PackageMetadataFailure failure)
    : std::runtime_error(failure.diagnostic), failure_(std::move(failure)) {}

const PackageMetadataFailure& PackageMetadataError::failure() const noexcept {
    return failure_;
}

PacmanDatabasePaths resolve_pacman_database_paths() {
    CapturedCommandResult command_result =
            capture_command_output_raw(PACMAN_DATABASE_PATH_COMMAND);
    if(command_result.exit_code != 0) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::ConfigurationUnavailable,
                "pacman-conf failed with exit code " +
                        std::to_string(command_result.exit_code) + ".");
    }
    return parse_pacman_database_paths(command_result.output);
}

PacmanRepositoryConfiguration resolve_pacman_repository_configuration() {
    PacmanDatabasePaths database_paths = resolve_pacman_database_paths();

    CapturedCommandResult command_result =
            capture_command_output_raw(PACMAN_REPOSITORY_LIST_COMMAND);
    if(command_result.exit_code != 0) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::ConfigurationUnavailable,
                "pacman-conf repository list failed with exit code " +
                        std::to_string(command_result.exit_code) + ".");
    }

    return PacmanRepositoryConfiguration{
            std::move(database_paths),
            parse_pacman_repository_names(command_result.output)};
}

ForeignPackageInventoryResult query_foreign_package_inventory(
        const PacmanRepositoryConfiguration& configuration) {
    try {
        // read phaseのRAII資源はprivate helperからreturnする前に必ず解放される。
        return query_foreign_package_inventory_read_phase(configuration);
    } catch(const PackageMetadataError& error) {
        // Expected metadata/libalpm failuresだけをvalueへ戻す。
        // std::bad_alloc等のruntime failureはflattenしない。
        return error.failure();
    }
}

struct PackageMetadataSession::Impl {
    Impl(UniqueAlpmHandle owned_handle, alpm_db_t* borrowed_local_db) noexcept
        : handle(std::move(owned_handle)), local_db(borrowed_local_db) {}

    UniqueAlpmHandle handle;
    alpm_db_t*       local_db;
};

PackageMetadataSession::PackageMetadataSession(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

PackageMetadataSession::PackageMetadataSession(PackageMetadataSession&&) noexcept = default;

PackageMetadataSession& PackageMetadataSession::operator=(PackageMetadataSession&&) noexcept =
        default;

PackageMetadataSession::~PackageMetadataSession() noexcept = default;

PackageMetadataSession PackageMetadataSession::open(
        const PacmanDatabasePaths& paths) {
    PacmanDatabasePaths normalized_paths = normalize_pacman_database_paths(paths);
    std::string root_dir = normalized_paths.root_dir.string();
    std::string db_path = normalized_paths.db_path.string();

    alpm_errno_t initialization_error = ALPM_ERR_OK;
    alpm_handle_t* raw_handle =
            alpm_initialize(root_dir.c_str(), db_path.c_str(), &initialization_error);
    if(raw_handle == nullptr) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::InitializationFailed,
                alpm_failure_diagnostic(
                        "Failed to initialize package metadata session",
                        initialization_error));
    }

    UniqueAlpmHandle handle(raw_handle);
    if(initialization_error != ALPM_ERR_OK) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::InitializationFailed,
                alpm_failure_diagnostic(
                        "Failed to initialize package metadata session",
                        initialization_error));
    }

    alpm_db_t* local_db = alpm_get_localdb(handle.get());
    alpm_errno_t local_db_error = alpm_errno(handle.get());
    if(local_db == nullptr || local_db_error != ALPM_ERR_OK) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                alpm_failure_diagnostic(
                        "Failed to access the local package database",
                        local_db_error));
    }

    int validation_result = alpm_db_get_valid(local_db);
    alpm_errno_t validation_error = alpm_errno(handle.get());
    if(validation_result != 0 || validation_error != ALPM_ERR_OK) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                alpm_failure_diagnostic(
                        "Local package database validation failed",
                        validation_error));
    }

    // LANDMINE: libalpm 16 maps a cache-load failure inside alpm_db_get_pkg() to
    // ALPM_ERR_PKG_NOT_FOUND. Preload here so a later not-found means a lookup miss.
    // nullptr+OK remains a valid empty DB.
    static_cast<void>(alpm_db_get_pkgcache(local_db));
    alpm_errno_t package_cache_error = alpm_errno(handle.get());
    if(package_cache_error != ALPM_ERR_OK) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                alpm_failure_diagnostic(
                        "Failed to load the local package database",
                        package_cache_error));
    }

    auto impl = std::make_unique<Impl>(std::move(handle), local_db);
    return PackageMetadataSession(std::move(impl));
}

InstalledPackageQueryResult PackageMetadataSession::query_installed_package(
        const std::string& package_name) const {
    if(impl_ == nullptr) {
        return query_failure(
                PackageMetadataErrorCode::QueryFailed,
                "Package metadata session is not open.");
    }
    if(!is_valid_package_name(package_name)) {
        return query_failure(
                PackageMetadataErrorCode::InvalidPackageName,
                "Package name is invalid.");
    }

    alpm_pkg_t* package = alpm_db_get_pkg(impl_->local_db, package_name.c_str());
    if(package == nullptr) {
        alpm_errno_t query_error = alpm_errno(impl_->handle.get());
        if(query_error == ALPM_ERR_PKG_NOT_FOUND) return PackageNotFound{};
        return query_failure(
                PackageMetadataErrorCode::QueryFailed,
                alpm_failure_diagnostic(
                        "Installed package query failed",
                        query_error));
    }

    // POLICY: libalpmのpublic contractではnonnull returnが成功を表す。
    // 成功時のhandle errnoは以前の失敗を保持し得るため、判定には使わない。

    const char* returned_name = alpm_pkg_get_name(package);
    if(returned_name == nullptr || package_name != returned_name) {
        return query_failure(
                PackageMetadataErrorCode::MalformedMetadata,
                "Installed package metadata contains an invalid package name.");
    }

    const char* installed_version = alpm_pkg_get_version(package);
    if(installed_version == nullptr || installed_version[0] == '\0') {
        return query_failure(
                PackageMetadataErrorCode::MalformedMetadata,
                "Installed package metadata contains an invalid version.");
    }

    return InstalledPackageMetadata{
            returned_name,
            installed_version,
            map_install_reason(alpm_pkg_get_reason(package))};
}

struct RepositoryPackageMetadataSession::Impl {
    struct RegisteredRepository {
        std::string repository_name;
        alpm_db_t*  database;
    };

    Impl(
            UniqueAlpmHandle owned_handle,
            std::vector<RegisteredRepository> registered_repositories) noexcept
        : handle(std::move(owned_handle)),
          repositories(std::move(registered_repositories)) {}

    UniqueAlpmHandle                  handle;
    std::vector<RegisteredRepository> repositories;
};

RepositoryPackageMetadataSession::RepositoryPackageMetadataSession(
        std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

RepositoryPackageMetadataSession::RepositoryPackageMetadataSession(
        RepositoryPackageMetadataSession&&) noexcept = default;

RepositoryPackageMetadataSession& RepositoryPackageMetadataSession::operator=(
        RepositoryPackageMetadataSession&&) noexcept = default;

RepositoryPackageMetadataSession::~RepositoryPackageMetadataSession() noexcept = default;

RepositoryPackageMetadataSession RepositoryPackageMetadataSession::open(
        const PacmanRepositoryConfiguration& configuration) {
    PacmanRepositoryConfiguration normalized_configuration =
            normalize_pacman_repository_configuration(configuration);
    std::string root_dir = normalized_configuration.database_paths.root_dir.string();
    std::string db_path = normalized_configuration.database_paths.db_path.string();

    alpm_errno_t initialization_error = ALPM_ERR_OK;
    alpm_handle_t* raw_handle =
            alpm_initialize(root_dir.c_str(), db_path.c_str(), &initialization_error);
    if(raw_handle == nullptr) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::InitializationFailed,
                alpm_failure_diagnostic(
                        "Failed to initialize repository package metadata session",
                        initialization_error));
    }

    UniqueAlpmHandle handle(raw_handle);
    if(initialization_error != ALPM_ERR_OK) {
        throw_package_metadata_error(
                PackageMetadataErrorCode::InitializationFailed,
                alpm_failure_diagnostic(
                        "Failed to initialize repository package metadata session",
                        initialization_error));
    }

    std::vector<Impl::RegisteredRepository> repositories;
    repositories.reserve(normalized_configuration.repository_names.size());
    for(const auto& repository_name : normalized_configuration.repository_names) {
        alpm_db_t* database = alpm_register_syncdb(
                handle.get(), repository_name.c_str(),
                READ_ONLY_SYNC_DATABASE_SIGLEVEL);
        alpm_errno_t registration_error = alpm_errno(handle.get());
        if(database == nullptr || registration_error != ALPM_ERR_OK) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Failed to register a repository package database",
                            registration_error));
        }

        int validation_result = alpm_db_get_valid(database);
        alpm_errno_t validation_error = alpm_errno(handle.get());
        if(validation_result != 0 || validation_error != ALPM_ERR_OK) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Repository package database validation failed",
                            validation_error));
        }

        // LANDMINE: missing/corrupt sync DB can surface only while loading the cache.
        // nullptr+OK is retained as a valid empty repository database.
        static_cast<void>(alpm_db_get_pkgcache(database));
        alpm_errno_t package_cache_error = alpm_errno(handle.get());
        if(package_cache_error != ALPM_ERR_OK) {
            throw_package_metadata_error(
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
                    alpm_failure_diagnostic(
                            "Failed to load a repository package database",
                            package_cache_error));
        }

        repositories.push_back(Impl::RegisteredRepository{repository_name, database});
    }

    auto impl = std::make_unique<Impl>(std::move(handle), std::move(repositories));
    return RepositoryPackageMetadataSession(std::move(impl));
}

RepositoryPackageQueryResult RepositoryPackageMetadataSession::query_repository_package(
        const RepositoryPackageLookup& lookup) const {
    if(impl_ == nullptr) {
        return query_failure(
                PackageMetadataErrorCode::QueryFailed,
                "Repository package metadata session is not open.");
    }
    if(!is_valid_package_name(lookup.package_name)) {
        return query_failure(
                PackageMetadataErrorCode::InvalidPackageName,
                "Package name is invalid.");
    }

    if(lookup.exact_repository_name.has_value()) {
        for(const auto& repository : impl_->repositories) {
            if(repository.repository_name != lookup.exact_repository_name.value()) continue;
            return query_repository_database(
                    impl_->handle.get(), repository.database,
                    repository.repository_name, lookup.package_name);
        }
        return query_failure(
                PackageMetadataErrorCode::RepositoryNotConfigured,
                "Requested repository is not configured.");
    }

    for(const auto& repository : impl_->repositories) {
        RepositoryPackageQueryResult result = query_repository_database(
                impl_->handle.get(), repository.database,
                repository.repository_name, lookup.package_name);
        if(std::holds_alternative<PackageNotFound>(result)) continue;
        return result;
    }
    return PackageNotFound{};
}
