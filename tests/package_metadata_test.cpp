#include "package_metadata.hpp"

#ifdef ALPM_H
#error "package_metadata.hpp must not expose or include raw libalpm types"
#endif

#include "stubs/package-metadata/alpm_stub.hpp"
#include "stubs/package-metadata/process_stub.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

static_assert(!std::is_default_constructible_v<PackageMetadataSession>);
static_assert(!std::is_copy_constructible_v<PackageMetadataSession>);
static_assert(!std::is_copy_assignable_v<PackageMetadataSession>);
static_assert(std::is_nothrow_move_constructible_v<PackageMetadataSession>);
static_assert(std::is_nothrow_move_assignable_v<PackageMetadataSession>);
static_assert(std::is_nothrow_destructible_v<PackageMetadataSession>);
static_assert(!std::is_default_constructible_v<RepositoryPackageMetadataSession>);
static_assert(!std::is_copy_constructible_v<RepositoryPackageMetadataSession>);
static_assert(!std::is_copy_assignable_v<RepositoryPackageMetadataSession>);
static_assert(std::is_nothrow_move_constructible_v<RepositoryPackageMetadataSession>);
static_assert(std::is_nothrow_move_assignable_v<RepositoryPackageMetadataSession>);
static_assert(std::is_nothrow_destructible_v<RepositoryPackageMetadataSession>);

namespace {

namespace fs = std::filesystem;
namespace stub = package_metadata_test_stub;

constexpr const char* DATABASE_PATH_COMMAND =
        "pacman-conf --verbose RootDir DBPath 2>/dev/null";
constexpr const char* REPOSITORY_LIST_COMMAND =
        "pacman-conf --repo-list 2>/dev/null";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

PacmanDatabasePaths valid_database_paths() {
    return PacmanDatabasePaths{"/", "/var/lib/pacman"};
}

template <typename Callable>
void expect_metadata_error(
        Callable callable, PackageMetadataErrorCode expected_code,
        const std::string& context, const std::string& forbidden_diagnostic_text = "") {
    try {
        callable();
    } catch(const PackageMetadataError& error) {
        const PackageMetadataFailure& failure = error.failure();
        expect(failure.code == expected_code, context + ": unexpected error code");
        expect(!failure.diagnostic.empty(), context + ": empty diagnostic");
        expect(
                std::string(error.what()) == failure.diagnostic,
                context + ": exception and failure diagnostics differ");
        if(!forbidden_diagnostic_text.empty()) {
            expect(
                    failure.diagnostic.find(forbidden_diagnostic_text) == std::string::npos,
                    context + ": diagnostic contains raw command output");
        }
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected PackageMetadataError");
}

template <typename Expected, typename Result>
Expected require_result_alternative(
        const Result& result, const std::string& context) {
    const Expected* expected = std::get_if<Expected>(&result);
    if(expected == nullptr) {
        throw std::runtime_error(context + ": unexpected query result alternative");
    }
    return *expected;
}

PackageMetadataFailure require_query_failure(
        const InstalledPackageQueryResult& result, PackageMetadataErrorCode expected_code,
        const std::string& context) {
    PackageMetadataFailure failure =
            require_result_alternative<PackageMetadataFailure>(result, context);
    expect(failure.code == expected_code, context + ": unexpected failure code");
    expect(!failure.diagnostic.empty(), context + ": empty failure diagnostic");
    return failure;
}

PackageMetadataFailure require_repository_query_failure(
        const RepositoryPackageQueryResult& result,
        PackageMetadataErrorCode expected_code,
        const std::string& context) {
    PackageMetadataFailure failure =
            require_result_alternative<PackageMetadataFailure>(result, context);
    expect(failure.code == expected_code, context + ": unexpected failure code");
    expect(!failure.diagnostic.empty(), context + ": empty failure diagnostic");
    return failure;
}

void set_pacman_conf_output(const std::string& output, int exit_code = 0) {
    stub::reset_process_stub();
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{output, exit_code});
}

void set_repository_configuration_outputs(
        const std::string& repository_output,
        int repository_exit_code = 0,
        const std::string& path_output =
                "RootDir = /\nDBPath = /var/lib/pacman/\n",
        int path_exit_code = 0) {
    stub::reset_process_stub();
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{path_output, path_exit_code});
    stub::enqueue_captured_command_result(
            REPOSITORY_LIST_COMMAND,
            CapturedCommandResult{repository_output, repository_exit_code});
}

PacmanRepositoryConfiguration valid_repository_configuration(
        std::vector<std::string> repository_names = {"core", "extra"}) {
    return PacmanRepositoryConfiguration{
            valid_database_paths(),
            std::move(repository_names)};
}

void expect_repository_query_history(
        const std::vector<std::pair<std::string, std::string>>& expected_queries,
        const std::string& context) {
    std::vector<stub::RepositoryPackageQuery> actual_queries =
            stub::repository_package_query_history();
    expect(actual_queries.size() == expected_queries.size(), context + ": query count differs");
    for(std::size_t index = 0; index < expected_queries.size(); ++index) {
        expect(
                actual_queries[index].repository_name == expected_queries[index].first &&
                        actual_queries[index].package_name == expected_queries[index].second,
                context + ": query identity differs at index " + std::to_string(index));
    }
}

void test_pacman_conf_path_parse_success() {
    set_pacman_conf_output("RootDir = /\nDBPath = /var/lib/pacman/\n");

    PacmanDatabasePaths paths = resolve_pacman_database_paths();

    expect(paths.root_dir == fs::path("/"), "RootDir parse result differs");
    expect(paths.db_path == fs::path("/var/lib/pacman"), "DBPath parse result differs");
    expect(stub::capture_command_call_count() == 1, "pacman-conf was not called exactly once");
    expect(
            stub::last_captured_command() ==
                    "pacman-conf --verbose RootDir DBPath 2>/dev/null",
            "Unexpected pacman-conf command");
}

void test_pacman_conf_command_failure() {
    set_pacman_conf_output("RootDir = /\nDBPath = /var/lib/pacman/", 127);
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationUnavailable,
            "pacman-conf command failure");
}

void test_root_dir_missing() {
    set_pacman_conf_output("DBPath = /var/lib/pacman/");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "missing RootDir");
}

void test_database_path_missing() {
    set_pacman_conf_output("RootDir = /");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "missing DBPath");
}

void test_duplicate_root_dir() {
    set_pacman_conf_output("RootDir = /\nRootDir = /srv/root\nDBPath = /var/lib/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "duplicate RootDir");
}

void test_duplicate_database_path() {
    set_pacman_conf_output(
            "RootDir = /\nDBPath = /var/lib/pacman\nDBPath = /srv/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "duplicate DBPath");
}

void test_empty_root_dir() {
    set_pacman_conf_output("RootDir =    \nDBPath = /var/lib/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "empty RootDir");
}

void test_empty_database_path() {
    set_pacman_conf_output("RootDir = /\nDBPath =    ");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "empty DBPath");
}

void test_relative_root_dir() {
    set_pacman_conf_output("RootDir = relative/root\nDBPath = /var/lib/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "relative RootDir");
}

void test_relative_database_path() {
    set_pacman_conf_output("RootDir = /\nDBPath = relative/database");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "relative DBPath");
}

void test_trailing_slash_normalization() {
    set_pacman_conf_output("RootDir = /srv/root///\nDBPath = /var/lib/pacman///");

    PacmanDatabasePaths paths = resolve_pacman_database_paths();

    expect(paths.root_dir == fs::path("/srv/root"), "RootDir trailing slash remains");
    expect(paths.db_path == fs::path("/var/lib/pacman"), "DBPath trailing slash remains");
}

void test_malformed_pacman_conf_line() {
    set_pacman_conf_output("RootDir = /\nmalformed line\nDBPath = /var/lib/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "malformed pacman-conf line");
}

void test_leading_blank_pacman_conf_line() {
    set_pacman_conf_output("\nRootDir = /\nDBPath = /var/lib/pacman\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "leading blank pacman-conf line");
}

void test_trailing_blank_pacman_conf_line() {
    set_pacman_conf_output("RootDir = /\nDBPath = /var/lib/pacman\n\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "trailing blank pacman-conf line");
}

void test_unexpected_pacman_conf_key_does_not_leak_output() {
    const std::string sensitive_marker = "sensitive-marker";
    set_pacman_conf_output(
            "RootDir = /\nUnexpected = " + sensitive_marker +
            "\nDBPath = /var/lib/pacman");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_database_paths()); },
            PackageMetadataErrorCode::ConfigurationMalformed, "unexpected pacman-conf key",
            sensitive_marker);
}

void test_repository_configuration_preserves_configured_order() {
    set_repository_configuration_outputs("testing\ncore\nextra\n");

    PacmanRepositoryConfiguration configuration =
            resolve_pacman_repository_configuration();

    expect(
            configuration.database_paths.root_dir == fs::path("/") &&
                    configuration.database_paths.db_path == fs::path("/var/lib/pacman"),
            "repository configuration paths differ");
    expect(
            configuration.repository_names ==
                    std::vector<std::string>({"testing", "core", "extra"}),
            "configured repository order changed");
    expect(
            stub::captured_commands() ==
                    std::vector<std::string>({DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND}),
            "repository resolver command order differs");
}

void test_empty_repository_configuration_is_valid() {
    set_repository_configuration_outputs("");

    PacmanRepositoryConfiguration configuration =
            resolve_pacman_repository_configuration();

    expect(configuration.repository_names.empty(), "empty repository list was not preserved");
}

void test_repository_configuration_rejects_duplicate_and_blank_lines() {
    set_repository_configuration_outputs("core\ncore\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "duplicate repository");

    set_repository_configuration_outputs("core\n\nextra\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "blank repository line");
}

void test_repository_configuration_rejects_control_characters() {
    set_repository_configuration_outputs("core\r\nextra\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "repository carriage return");

    std::string nul_output = "core\n";
    nul_output.append("sec\0ret", 7);
    nul_output.push_back('\n');
    set_repository_configuration_outputs(nul_output);
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "repository null byte");

    set_repository_configuration_outputs("core\nex\ttra\n");
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "repository tab");

    std::string delete_output = "core\nextra";
    delete_output.push_back(static_cast<char>(0x7f));
    delete_output.push_back('\n');
    set_repository_configuration_outputs(delete_output);
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "repository delete control");
}

void test_repository_configuration_command_failures_are_distinct() {
    set_repository_configuration_outputs(
            "core\n", 0,
            "raw-path-marker", 41);
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationUnavailable,
            "repository configuration path command failure",
            "raw-path-marker");
    expect(
            stub::captured_commands() == std::vector<std::string>({DATABASE_PATH_COMMAND}),
            "repo-list ran after path command failure");

    set_repository_configuration_outputs("raw-repository-marker", 42);
    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationUnavailable,
            "repository list command failure",
            "raw-repository-marker");
    expect(
            stub::captured_commands() ==
                    std::vector<std::string>({DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND}),
            "repository list command failure history differs");
}

void test_malformed_repository_output_does_not_leak_raw_text() {
    const std::string sensitive_marker = "repository-sensitive-marker";
    set_repository_configuration_outputs("core\n" + sensitive_marker + "\tvalue\n");

    expect_metadata_error(
            []() { static_cast<void>(resolve_pacman_repository_configuration()); },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "malformed repository output diagnostic",
            sensitive_marker);
}

void test_session_open_success() {
    stub::reset_alpm_stub();
    {
        PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());
        static_cast<void>(session);
        expect(stub::initialize_call_count() == 1, "alpm_initialize call count differs");
        expect(stub::local_database_call_count() == 1, "alpm_get_localdb call count differs");
        expect(stub::database_valid_call_count() == 1, "alpm_db_get_valid call count differs");
        expect(stub::package_cache_call_count() == 1, "package cache was not loaded");
        expect(stub::last_initialize_root() == "/", "alpm RootDir differs");
        expect(
                stub::last_initialize_database_path() == "/var/lib/pacman",
                "alpm DBPath differs");
        expect(stub::release_call_count() == 0, "session released handle before destruction");
    }
    expect(stub::release_call_count() == 1, "session did not release handle exactly once");
}

void test_session_rejects_relative_paths_before_initialize() {
    stub::reset_alpm_stub();
    expect_metadata_error(
            []() {
                static_cast<void>(PackageMetadataSession::open(
                        PacmanDatabasePaths{"relative/root", "/var/lib/pacman"}));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "session relative RootDir validation");
    expect(stub::initialize_call_count() == 0, "relative RootDir reached libalpm");

    expect_metadata_error(
            []() {
                static_cast<void>(PackageMetadataSession::open(
                        PacmanDatabasePaths{"/", "relative/database"}));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "session relative DBPath validation");
    expect(stub::initialize_call_count() == 0, "relative DBPath reached libalpm");
}

void test_session_rejects_embedded_null_path_before_initialize() {
    stub::reset_alpm_stub();
    const std::string root_with_null("/srv/root\0suffix", 16);

    expect_metadata_error(
            [&root_with_null]() {
                static_cast<void>(PackageMetadataSession::open(
                        PacmanDatabasePaths{fs::path(root_with_null), "/var/lib/pacman"}));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "session embedded null path validation");
    expect(stub::initialize_call_count() == 0, "embedded null path reached libalpm");
}

void test_alpm_initialization_failure() {
    stub::reset_alpm_stub();
    stub::set_initialize_failure(ALPM_ERR_SYSTEM);

    expect_metadata_error(
            []() { static_cast<void>(PackageMetadataSession::open(valid_database_paths())); },
            PackageMetadataErrorCode::InitializationFailed, "alpm initialization failure");
    expect(stub::created_handle_count() == 0, "failed initialization published a handle");
    expect(stub::release_call_count() == 0, "failed initialization released an unowned handle");
}

void test_local_database_unavailable_releases_handle() {
    stub::reset_alpm_stub();
    stub::set_local_database_unavailable();

    expect_metadata_error(
            []() { static_cast<void>(PackageMetadataSession::open(valid_database_paths())); },
            PackageMetadataErrorCode::LocalDatabaseUnavailable,
            "local database unavailable");
    expect(stub::created_handle_count() == 1, "local DB failure did not create a handle");
    expect(stub::release_count_for_handle(0) == 1, "local DB failure leaked its handle");
}

void test_invalid_local_database_releases_handle() {
    stub::reset_alpm_stub();
    stub::set_local_database_invalid();

    expect_metadata_error(
            []() { static_cast<void>(PackageMetadataSession::open(valid_database_paths())); },
            PackageMetadataErrorCode::LocalDatabaseUnavailable, "invalid local database");
    expect(stub::release_count_for_handle(0) == 1, "invalid local DB leaked its handle");
}

void test_package_cache_failure_releases_handle() {
    stub::reset_alpm_stub();
    stub::set_package_cache_failure();

    expect_metadata_error(
            []() { static_cast<void>(PackageMetadataSession::open(valid_database_paths())); },
            PackageMetadataErrorCode::LocalDatabaseUnavailable, "package cache failure");
    expect(stub::release_count_for_handle(0) == 1, "package cache failure leaked its handle");
}

void test_empty_package_cache_is_queryable_state() {
    stub::reset_alpm_stub();
    stub::set_empty_package_cache();
    stub::set_package_absent();
    {
        PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());
        InstalledPackageQueryResult result =
                session.query_installed_package("test-package");
        static_cast<void>(require_result_alternative<PackageNotFound>(
                result, "empty package cache query"));
    }
    expect(stub::release_count_for_handle(0) == 1, "empty package cache leaked its handle");
}

void test_package_absent() {
    stub::reset_alpm_stub();
    stub::set_package_absent();
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_result_alternative<PackageNotFound>(result, "package absence"));
}

void test_package_present() {
    stub::reset_alpm_stub();
    stub::set_package_metadata("test-package", "2.4.1-3", ALPM_PKG_REASON_EXPLICIT);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");
    InstalledPackageMetadata metadata =
            require_result_alternative<InstalledPackageMetadata>(result, "package present");

    expect(metadata.name == "test-package", "installed package name differs");
    expect(metadata.version == "2.4.1-3", "installed package version differs");
    expect(metadata.reason == InstalledPackageReason::Explicit, "installed package reason differs");
}

void test_returned_package_name_mismatch() {
    stub::reset_alpm_stub();
    stub::set_package_metadata("different-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::MalformedMetadata,
            "returned package name mismatch"));
}

void test_null_package_name() {
    stub::reset_alpm_stub();
    stub::set_package_metadata("test-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    stub::set_null_package_name();
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::MalformedMetadata, "null package name"));
}

void test_null_package_version() {
    stub::reset_alpm_stub();
    stub::set_package_metadata("test-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    stub::set_null_package_version();
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::MalformedMetadata, "null package version"));
}

void test_empty_package_version() {
    stub::reset_alpm_stub();
    stub::set_package_metadata("test-package", "", ALPM_PKG_REASON_EXPLICIT);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::MalformedMetadata, "empty package version"));
}

void expect_reason_mapping(
        alpm_pkgreason_t alpm_reason, InstalledPackageReason expected_reason,
        const std::string& context) {
    stub::reset_alpm_stub();
    stub::set_package_metadata("test-package", "1.0-1", alpm_reason);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");
    InstalledPackageMetadata metadata =
            require_result_alternative<InstalledPackageMetadata>(result, context);
    expect(metadata.reason == expected_reason, context + ": mapped reason differs");
}

void test_install_reason_mapping() {
    expect_reason_mapping(
            ALPM_PKG_REASON_EXPLICIT, InstalledPackageReason::Explicit,
            "explicit install reason");
    expect_reason_mapping(
            ALPM_PKG_REASON_DEPEND, InstalledPackageReason::Dependency,
            "dependency install reason");
    expect_reason_mapping(
            ALPM_PKG_REASON_UNKNOWN, InstalledPackageReason::Unknown,
            "unknown install reason");
    expect_reason_mapping(
            static_cast<alpm_pkgreason_t>(99), InstalledPackageReason::Unknown,
            "future install reason");
}

void test_invalid_package_name() {
    stub::reset_alpm_stub();
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("invalid/package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::InvalidPackageName, "invalid package name"));
    expect(stub::package_query_call_count() == 0, "invalid package name reached libalpm");
}

void test_query_failure_is_not_absence() {
    stub::reset_alpm_stub();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::QueryFailed,
            "libalpm package query failure"));
}

void test_null_query_without_error_is_not_absence() {
    stub::reset_alpm_stub();
    stub::set_package_query_null_without_error();
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult result = session.query_installed_package("test-package");

    static_cast<void>(require_query_failure(
            result, PackageMetadataErrorCode::QueryFailed,
            "package query null without libalpm error"));
}

void test_stale_query_error_does_not_override_found_package() {
    stub::reset_alpm_stub();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult failed_result =
            session.query_installed_package("test-package");
    static_cast<void>(require_query_failure(
            failed_result, PackageMetadataErrorCode::QueryFailed,
            "initial package query failure"));

    stub::set_package_metadata("test-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    stub::preserve_error_on_next_package_query();
    InstalledPackageQueryResult found_result =
            session.query_installed_package("test-package");

    static_cast<void>(require_result_alternative<InstalledPackageMetadata>(
            found_result, "package found with stale libalpm error"));
}

void test_stale_query_error_does_not_override_package_absence() {
    stub::reset_alpm_stub();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);
    PackageMetadataSession session = PackageMetadataSession::open(valid_database_paths());

    InstalledPackageQueryResult failed_result =
            session.query_installed_package("test-package");
    static_cast<void>(require_query_failure(
            failed_result, PackageMetadataErrorCode::QueryFailed,
            "initial package query failure"));

    stub::set_package_absent();
    InstalledPackageQueryResult absent_result =
            session.query_installed_package("test-package");

    static_cast<void>(require_result_alternative<PackageNotFound>(
            absent_result, "package absent after stale libalpm error"));
}

void test_move_construction_does_not_double_release() {
    stub::reset_alpm_stub();
    {
        PackageMetadataSession source = PackageMetadataSession::open(valid_database_paths());
        PackageMetadataSession destination(std::move(source));
        static_cast<void>(destination);
        expect(stub::release_call_count() == 0, "move construction released live handle");
    }
    expect(stub::created_handle_count() == 1, "move construction created extra handle");
    expect(stub::release_count_for_handle(0) == 1, "moved handle was not released exactly once");
}

void test_move_assignment_does_not_double_release() {
    stub::reset_alpm_stub();
    {
        PackageMetadataSession source = PackageMetadataSession::open(valid_database_paths());
        PackageMetadataSession destination = PackageMetadataSession::open(valid_database_paths());

        destination = std::move(source);

        expect(
                stub::release_count_for_handle(0) == 0,
                "move assignment released transferred handle early");
        expect(
                stub::release_count_for_handle(1) == 1,
                "move assignment did not release destination's old handle");
    }
    expect(stub::created_handle_count() == 2, "move assignment handle count differs");
    expect(stub::release_count_for_handle(0) == 1, "transferred handle release count differs");
    expect(stub::release_count_for_handle(1) == 1, "old destination handle was double released");
}

void test_repository_session_open_registers_in_configured_order() {
    stub::reset_alpm_stub();
    {
        RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
                valid_repository_configuration({"testing", "core", "extra"}));
        static_cast<void>(session);

        std::vector<stub::SyncDatabaseRegistration> registrations =
                stub::sync_database_registration_history();
        expect(registrations.size() == 3, "sync registration count differs");
        expect(
                registrations[0].repository_name == "testing" &&
                        registrations[1].repository_name == "core" &&
                        registrations[2].repository_name == "extra",
                "sync registration order differs");
        for(const auto& registration : registrations) {
            expect(registration.signature_level == 0, "sync registration siglevel differs");
        }
        expect(
                stub::sync_database_operation_history() ==
                        std::vector<std::string>({
                                "register testing", "valid testing", "cache testing",
                                "register core", "valid core", "cache core",
                                "register extra", "valid extra", "cache extra"}),
                "sync register/valid/cache order differs");
        expect(stub::local_database_call_count() == 0, "sync session accessed local DB");
        expect(stub::release_call_count() == 0, "sync session released handle early");
    }
    expect(stub::release_count_for_handle(0) == 1, "sync session leaked its handle");
}

void test_repository_session_accepts_empty_repository_list() {
    stub::reset_alpm_stub();
    {
        RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
                valid_repository_configuration({}));
        RepositoryPackageQueryResult result = session.query_repository_package(
                RepositoryPackageLookup{"test-package", std::nullopt});
        static_cast<void>(require_result_alternative<PackageNotFound>(
                result, "empty repository session query"));
    }
    expect(
            stub::sync_database_registration_history().empty(),
            "empty repository session registered a database");
    expect(stub::release_count_for_handle(0) == 1, "empty repository session leaked handle");
}

void test_repository_session_revalidates_manual_configuration() {
    stub::reset_alpm_stub();
    PacmanRepositoryConfiguration duplicate_configuration =
            valid_repository_configuration({"core", "core"});
    expect_metadata_error(
            [&duplicate_configuration]() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        duplicate_configuration));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "manual duplicate repository configuration");
    expect(stub::initialize_call_count() == 0, "duplicate repository reached libalpm");

    PacmanRepositoryConfiguration control_configuration =
            valid_repository_configuration({"core", "ex\ttra"});
    expect_metadata_error(
            [&control_configuration]() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        control_configuration));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "manual control repository configuration");
    expect(stub::initialize_call_count() == 0, "control repository reached libalpm");

    PacmanRepositoryConfiguration invalid_path_configuration{
            PacmanDatabasePaths{"relative/root", "/var/lib/pacman"},
            {"core"}};
    expect_metadata_error(
            [&invalid_path_configuration]() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        invalid_path_configuration));
            },
            PackageMetadataErrorCode::ConfigurationMalformed,
            "manual repository path configuration");
    expect(stub::initialize_call_count() == 0, "invalid repository path reached libalpm");
}

void test_repository_register_failure_releases_all_state() {
    stub::reset_alpm_stub();
    stub::set_sync_database_register_failure("extra");

    expect_metadata_error(
            []() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        valid_repository_configuration({"core", "extra", "testing"})));
            },
            PackageMetadataErrorCode::SyncDatabaseUnavailable,
            "sync database registration failure");

    expect(
            stub::sync_database_operation_history() ==
                    std::vector<std::string>({
                            "register core", "valid core", "cache core", "register extra"}),
            "registration failure did not stop in configured order");
    expect(stub::release_count_for_handle(0) == 1, "registration failure leaked handle");
}

void test_repository_validation_failure_releases_all_state() {
    stub::reset_alpm_stub();
    stub::set_sync_database_validation_failure("extra");

    expect_metadata_error(
            []() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        valid_repository_configuration({"core", "extra", "testing"})));
            },
            PackageMetadataErrorCode::SyncDatabaseUnavailable,
            "sync database validation failure");

    expect(
            stub::sync_database_operation_history() ==
                    std::vector<std::string>({
                            "register core", "valid core", "cache core",
                            "register extra", "valid extra"}),
            "validation failure did not stop before cache/later repository");
    expect(stub::release_count_for_handle(0) == 1, "validation failure leaked handle");
}

void test_repository_cache_failure_releases_all_state() {
    stub::reset_alpm_stub();
    stub::set_sync_database_cache_failure("extra");

    expect_metadata_error(
            []() {
                static_cast<void>(RepositoryPackageMetadataSession::open(
                        valid_repository_configuration({"core", "extra", "testing"})));
            },
            PackageMetadataErrorCode::SyncDatabaseUnavailable,
            "sync database cache failure");

    expect(
            stub::sync_database_operation_history() ==
                    std::vector<std::string>({
                            "register core", "valid core", "cache core",
                            "register extra", "valid extra", "cache extra"}),
            "cache failure continued to later repository");
    expect(stub::release_count_for_handle(0) == 1, "cache failure leaked handle");
}

void test_empty_repository_cache_is_valid() {
    stub::reset_alpm_stub();
    stub::set_sync_database_empty_cache("core");

    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));
    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"test-package", std::nullopt});

    static_cast<void>(require_result_alternative<PackageNotFound>(
            result, "empty sync cache query"));
}

void test_repository_precedence_uses_first_configured_hit() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "same-package", 10, 20);
    stub::set_repository_package_metadata("extra", "same-package", 30, 40);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"same-package", std::nullopt});
    RepositoryPackageMetadata metadata =
            require_result_alternative<RepositoryPackageMetadata>(result, "first repo hit");

    expect(metadata.repository_name == "core", "first configured repository did not win");
    expect(metadata.package_name == "same-package", "repository package name differs");
    expect(metadata.package_size_bytes == 10, "repository package size differs");
    expect(metadata.installed_size_bytes == 20, "repository installed size differs");
    expect_repository_query_history({{"core", "same-package"}}, "first repo hit");
}

void test_repository_precedence_continues_after_explicit_miss() {
    stub::reset_alpm_stub();
    stub::set_repository_package_absent("core", "later-package");
    stub::set_repository_package_metadata("extra", "later-package", 50, 60);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"later-package", std::nullopt});
    RepositoryPackageMetadata metadata = require_result_alternative<RepositoryPackageMetadata>(
            result, "earlier miss later hit");

    expect(metadata.repository_name == "extra", "later repository hit differs");
    expect_repository_query_history(
            {{"core", "later-package"}, {"extra", "later-package"}},
            "earlier miss later hit");
}

void test_repository_precedence_stops_after_query_failure() {
    stub::reset_alpm_stub();
    stub::set_repository_package_query_failure("core", "broken-package");
    stub::set_repository_package_metadata("extra", "broken-package", 50, 60);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"broken-package", std::nullopt});

    static_cast<void>(require_repository_query_failure(
            result, PackageMetadataErrorCode::QueryFailed,
            "earlier repository query failure"));
    expect_repository_query_history(
            {{"core", "broken-package"}},
            "earlier repository query failure");
}

void test_all_repository_misses_return_not_found() {
    stub::reset_alpm_stub();
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"missing-package", std::nullopt});

    static_cast<void>(require_result_alternative<PackageNotFound>(
            result, "all repository misses"));
    expect_repository_query_history(
            {{"core", "missing-package"}, {"extra", "missing-package"}},
            "all repository misses");
}

void test_exact_repository_lookup_does_not_use_precedence_fallback() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "provider-package", 10, 20);
    stub::set_repository_package_metadata("extra", "provider-package", 30, 40);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"provider-package", std::string("extra")});
    RepositoryPackageMetadata metadata = require_result_alternative<RepositoryPackageMetadata>(
            result, "exact repository lookup");

    expect(metadata.repository_name == "extra", "exact repository lookup selected wrong repo");
    expect(metadata.package_size_bytes == 30, "exact repository package size differs");
    expect_repository_query_history(
            {{"extra", "provider-package"}},
            "exact repository lookup");
}

void test_exact_repository_miss_is_not_found_without_fallback() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "provider-package", 10, 20);
    stub::set_repository_package_absent("extra", "provider-package");
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"provider-package", std::string("extra")});

    static_cast<void>(require_result_alternative<PackageNotFound>(
            result, "exact repository miss"));
    expect_repository_query_history(
            {{"extra", "provider-package"}},
            "exact repository miss");
}

void test_unconfigured_exact_repository_is_failure() {
    stub::reset_alpm_stub();
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core", "extra"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"provider-package", std::string("testing")});

    static_cast<void>(require_repository_query_failure(
            result, PackageMetadataErrorCode::RepositoryNotConfigured,
            "unconfigured exact repository"));
    expect_repository_query_history({}, "unconfigured exact repository");
}

void test_repository_query_rejects_invalid_package_name() {
    stub::reset_alpm_stub();
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"invalid/package", std::nullopt});

    static_cast<void>(require_repository_query_failure(
            result, PackageMetadataErrorCode::InvalidPackageName,
            "invalid repository package name"));
    expect_repository_query_history({}, "invalid repository package name");
}

void test_repository_query_success_ignores_stale_errno() {
    stub::reset_alpm_stub();
    stub::set_repository_package_query_failure("core", "test-package");
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));
    RepositoryPackageQueryResult failed_result = session.query_repository_package(
            RepositoryPackageLookup{"test-package", std::nullopt});
    static_cast<void>(require_repository_query_failure(
            failed_result, PackageMetadataErrorCode::QueryFailed,
            "initial repository query failure"));

    stub::set_repository_package_metadata("core", "test-package", 10, 20);
    stub::preserve_error_on_next_repository_package_query("core", "test-package");
    RepositoryPackageQueryResult found_result = session.query_repository_package(
            RepositoryPackageLookup{"test-package", std::nullopt});

    static_cast<void>(require_result_alternative<RepositoryPackageMetadata>(
            found_result, "repository success with stale errno"));
}

void test_repository_query_null_without_error_is_failure() {
    stub::reset_alpm_stub();
    stub::set_repository_package_query_null_without_error("core", "test-package");
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));

    RepositoryPackageQueryResult result = session.query_repository_package(
            RepositoryPackageLookup{"test-package", std::nullopt});

    static_cast<void>(require_repository_query_failure(
            result, PackageMetadataErrorCode::QueryFailed,
            "repository nullptr with OK"));
}

void test_repository_metadata_validates_returned_name() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "test-package", 10, 20);
    stub::set_repository_package_name_null("core", "test-package");
    RepositoryPackageMetadataSession null_name_session =
            RepositoryPackageMetadataSession::open(
                    valid_repository_configuration({"core"}));
    RepositoryPackageQueryResult null_name_result =
            null_name_session.query_repository_package(
                    RepositoryPackageLookup{"test-package", std::nullopt});
    static_cast<void>(require_repository_query_failure(
            null_name_result, PackageMetadataErrorCode::MalformedMetadata,
            "null repository package name"));

    stub::set_repository_package_metadata("core", "test-package", 10, 20);
    stub::set_repository_package_returned_name(
            "core", "test-package", "different-package");
    RepositoryPackageQueryResult mismatch_result =
            null_name_session.query_repository_package(
                    RepositoryPackageLookup{"test-package", std::nullopt});
    static_cast<void>(require_repository_query_failure(
            mismatch_result, PackageMetadataErrorCode::MalformedMetadata,
            "mismatched repository package name"));
}

void test_repository_metadata_accepts_positive_and_zero_sizes() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "positive-package", 991730, 5283285);
    stub::set_repository_package_metadata("core", "zero-package", 0, 0);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));

    RepositoryPackageMetadata positive = require_result_alternative<RepositoryPackageMetadata>(
            session.query_repository_package(
                    RepositoryPackageLookup{"positive-package", std::nullopt}),
            "positive repository package sizes");
    expect(
            positive.package_size_bytes == 991730 &&
                    positive.installed_size_bytes == 5283285,
            "positive repository package sizes differ");

    RepositoryPackageMetadata zero = require_result_alternative<RepositoryPackageMetadata>(
            session.query_repository_package(
                    RepositoryPackageLookup{"zero-package", std::nullopt}),
            "zero repository package sizes");
    expect(
            zero.package_size_bytes == 0 && zero.installed_size_bytes == 0,
            "known zero repository package sizes differ");
}

void test_repository_metadata_rejects_negative_sizes() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata("core", "negative-package", -1, 20);
    stub::set_repository_package_metadata("core", "negative-installed", 10, -1);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));

    static_cast<void>(require_repository_query_failure(
            session.query_repository_package(
                    RepositoryPackageLookup{"negative-package", std::nullopt}),
            PackageMetadataErrorCode::MalformedMetadata,
            "negative repository package size"));
    static_cast<void>(require_repository_query_failure(
            session.query_repository_package(
                    RepositoryPackageLookup{"negative-installed", std::nullopt}),
            PackageMetadataErrorCode::MalformedMetadata,
            "negative repository installed size"));
}

void test_repository_metadata_accepts_maximum_off_t() {
    stub::reset_alpm_stub();
    constexpr off_t MAXIMUM_SIZE = std::numeric_limits<off_t>::max();
    stub::set_repository_package_metadata(
            "core", "maximum-package", MAXIMUM_SIZE, MAXIMUM_SIZE);
    RepositoryPackageMetadataSession session = RepositoryPackageMetadataSession::open(
            valid_repository_configuration({"core"}));

    RepositoryPackageMetadata metadata = require_result_alternative<RepositoryPackageMetadata>(
            session.query_repository_package(
                    RepositoryPackageLookup{"maximum-package", std::nullopt}),
            "maximum repository package sizes");

    expect(
            metadata.package_size_bytes == static_cast<std::uint64_t>(MAXIMUM_SIZE) &&
                    metadata.installed_size_bytes == static_cast<std::uint64_t>(MAXIMUM_SIZE),
            "maximum off_t size conversion differs");
}

void test_repository_session_move_construction_does_not_double_release() {
    stub::reset_alpm_stub();
    {
        RepositoryPackageMetadataSession source = RepositoryPackageMetadataSession::open(
                valid_repository_configuration({"core"}));
        RepositoryPackageMetadataSession destination(std::move(source));
        static_cast<void>(destination);
        expect(stub::release_call_count() == 0, "sync move construction released handle early");
    }
    expect(stub::created_handle_count() == 1, "sync move construction created extra handle");
    expect(stub::release_count_for_handle(0) == 1, "sync moved handle release count differs");
}

void test_repository_session_move_assignment_does_not_double_release() {
    stub::reset_alpm_stub();
    {
        RepositoryPackageMetadataSession source = RepositoryPackageMetadataSession::open(
                valid_repository_configuration({"core"}));
        RepositoryPackageMetadataSession destination = RepositoryPackageMetadataSession::open(
                valid_repository_configuration({"extra"}));

        destination = std::move(source);

        expect(stub::release_count_for_handle(0) == 0, "sync transferred handle released early");
        expect(stub::release_count_for_handle(1) == 1, "sync destination handle not released");
    }
    expect(stub::created_handle_count() == 2, "sync move assignment handle count differs");
    expect(stub::release_count_for_handle(0) == 1, "sync transferred handle release differs");
    expect(stub::release_count_for_handle(1) == 1, "sync old handle was double released");
}

} // namespace

int main() {
    try {
        test_pacman_conf_path_parse_success();
        test_pacman_conf_command_failure();
        test_root_dir_missing();
        test_database_path_missing();
        test_duplicate_root_dir();
        test_duplicate_database_path();
        test_empty_root_dir();
        test_empty_database_path();
        test_relative_root_dir();
        test_relative_database_path();
        test_trailing_slash_normalization();
        test_malformed_pacman_conf_line();
        test_leading_blank_pacman_conf_line();
        test_trailing_blank_pacman_conf_line();
        test_unexpected_pacman_conf_key_does_not_leak_output();
        test_repository_configuration_preserves_configured_order();
        test_empty_repository_configuration_is_valid();
        test_repository_configuration_rejects_duplicate_and_blank_lines();
        test_repository_configuration_rejects_control_characters();
        test_repository_configuration_command_failures_are_distinct();
        test_malformed_repository_output_does_not_leak_raw_text();

        test_session_open_success();
        test_session_rejects_relative_paths_before_initialize();
        test_session_rejects_embedded_null_path_before_initialize();
        test_alpm_initialization_failure();
        test_local_database_unavailable_releases_handle();
        test_invalid_local_database_releases_handle();
        test_package_cache_failure_releases_handle();
        test_empty_package_cache_is_queryable_state();

        test_package_absent();
        test_package_present();
        test_returned_package_name_mismatch();
        test_null_package_name();
        test_null_package_version();
        test_empty_package_version();
        test_install_reason_mapping();
        test_invalid_package_name();
        test_query_failure_is_not_absence();
        test_null_query_without_error_is_not_absence();
        test_stale_query_error_does_not_override_found_package();
        test_stale_query_error_does_not_override_package_absence();

        test_move_construction_does_not_double_release();
        test_move_assignment_does_not_double_release();

        test_repository_session_open_registers_in_configured_order();
        test_repository_session_accepts_empty_repository_list();
        test_repository_session_revalidates_manual_configuration();
        test_repository_register_failure_releases_all_state();
        test_repository_validation_failure_releases_all_state();
        test_repository_cache_failure_releases_all_state();
        test_empty_repository_cache_is_valid();
        test_repository_precedence_uses_first_configured_hit();
        test_repository_precedence_continues_after_explicit_miss();
        test_repository_precedence_stops_after_query_failure();
        test_all_repository_misses_return_not_found();
        test_exact_repository_lookup_does_not_use_precedence_fallback();
        test_exact_repository_miss_is_not_found_without_fallback();
        test_unconfigured_exact_repository_is_failure();
        test_repository_query_rejects_invalid_package_name();
        test_repository_query_success_ignores_stale_errno();
        test_repository_query_null_without_error_is_failure();
        test_repository_metadata_validates_returned_name();
        test_repository_metadata_accepts_positive_and_zero_sizes();
        test_repository_metadata_rejects_negative_sizes();
        test_repository_metadata_accepts_maximum_off_t();
        test_repository_session_move_construction_does_not_double_release();
        test_repository_session_move_assignment_does_not_double_release();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "package metadata tests: all checks passed\n";
    return 0;
}
