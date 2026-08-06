#include "provider_installed_state.hpp"

#ifdef ALPM_H
#error "provider_installed_state.hpp must not expose or include raw libalpm types"
#endif

#include "stubs/package-metadata/alpm_stub.hpp"
#include "stubs/package-metadata/process_stub.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<ProviderInstalledStateObservation>);
static_assert(!std::is_constructible_v<
        ProviderInstalledStateObservation,
        ProviderInstalledState,
        PackageMetadataFailure>);
static_assert(std::is_copy_constructible_v<ProviderInstalledStateObservation>);
static_assert(std::is_copy_assignable_v<ProviderInstalledStateObservation>);
static_assert(!std::is_copy_constructible_v<ProviderInstalledStateLookup>);
static_assert(!std::is_copy_assignable_v<ProviderInstalledStateLookup>);
static_assert(!std::is_move_constructible_v<ProviderInstalledStateLookup>);
static_assert(!std::is_move_assignable_v<ProviderInstalledStateLookup>);
static_assert(std::is_nothrow_destructible_v<ProviderInstalledStateLookup>);

namespace {

namespace stub = package_metadata_test_stub;

constexpr const char* DATABASE_PATH_COMMAND =
        "pacman-conf --verbose RootDir DBPath 2>/dev/null";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void reset_stubs() {
    stub::reset_alpm_stub();
    stub::reset_process_stub();
}

void enqueue_valid_database_paths() {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{
                    "RootDir = /\nDBPath = /var/lib/pacman/\n", 0});
}

void enqueue_database_path_failure(int exit_code = 127) {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{"", exit_code});
}

void enqueue_malformed_database_paths() {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{"RootDir = /\n", 0});
}

void expect_state(
        const ProviderInstalledStateObservation& observation,
        ProviderInstalledState expected_state, const std::string& context) {
    expect(observation.state() == expected_state, context + ": state differs");
    expect(
            observation.has_failure() == (expected_state == ProviderInstalledState::Unknown),
            context + ": failure presence differs");
}

void expect_unknown_failure(
        const ProviderInstalledStateObservation& observation,
        PackageMetadataErrorCode expected_code, const std::string& context) {
    expect_state(observation, ProviderInstalledState::Unknown, context);
    const PackageMetadataFailure& failure = observation.failure();
    expect(failure.code == expected_code, context + ": failure code differs");
    expect(!failure.diagnostic.empty(), context + ": failure diagnostic is empty");
}

template <typename Callable>
void expect_metadata_error(
        Callable callable, PackageMetadataErrorCode expected_code,
        const std::string& context) {
    try {
        callable();
    } catch(const PackageMetadataError& error) {
        expect(error.failure().code == expected_code, context + ": failure code differs");
        expect(!error.failure().diagnostic.empty(), context + ": failure diagnostic is empty");
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected PackageMetadataError");
}

void test_pure_model_states_and_invariants() {
    ProviderInstalledStateObservation installed =
            ProviderInstalledStateObservation::installed();
    expect_state(installed, ProviderInstalledState::Installed, "installed observation");

    ProviderInstalledStateObservation not_installed =
            ProviderInstalledStateObservation::not_installed();
    expect_state(not_installed, ProviderInstalledState::NotInstalled, "not-installed observation");

    const PackageMetadataFailure expected_failure{
            PackageMetadataErrorCode::QueryFailed, "typed query failure"};
    ProviderInstalledStateObservation unknown =
            ProviderInstalledStateObservation::unknown(expected_failure);
    expect_unknown_failure(
            unknown, PackageMetadataErrorCode::QueryFailed, "unknown observation");
    expect(
            unknown.failure().diagnostic == expected_failure.diagnostic,
            "unknown observation lost the diagnostic");

    try {
        static_cast<void>(ProviderInstalledStateObservation::unknown(
                PackageMetadataFailure{
                        PackageMetadataErrorCode::InvalidPackageName,
                        "invalid package name"}));
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("invalid package-name failure became an Unknown observation");
}

void test_lookup_construction_is_lazy() {
    reset_stubs();

    ProviderInstalledStateLookup lookup;
    static_cast<void>(lookup);

    expect(stub::capture_command_call_count() == 0, "construction ran pacman-conf");
    expect(stub::initialize_call_count() == 0, "construction initialized libalpm");
}

void test_first_valid_query_opens_authority_and_projects_present_package() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_metadata("present-package", "1.0-1", ALPM_PKG_REASON_DEPEND);

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation observation = lookup.query("present-package");

    expect_state(observation, ProviderInstalledState::Installed, "present package");
    expect(stub::capture_command_call_count() == 1, "valid query did not run pacman-conf once");
    expect(stub::initialize_call_count() == 1, "valid query did not initialize libalpm once");
    expect(stub::package_query_call_count() == 1, "valid query did not query local DB once");
}

void test_package_absence_projects_to_not_installed() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_absent();

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation observation = lookup.query("missing-package");

    expect_state(observation, ProviderInstalledState::NotInstalled, "absent package");
    expect(stub::package_query_call_count() == 1, "absence did not query local DB once");
}

void test_query_failure_projects_to_unknown() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation observation = lookup.query("failed-package");

    expect_unknown_failure(
            observation, PackageMetadataErrorCode::QueryFailed, "query failure");
    expect(
            observation.state() != ProviderInstalledState::NotInstalled,
            "query failure was flattened to not installed");
}

void test_malformed_query_metadata_projects_to_unknown() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::enqueue_local_package_query_present(
            "mismatched-package", "different-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    stub::enqueue_local_package_query_present(
            "empty-version-package", "empty-version-package", "", ALPM_PKG_REASON_EXPLICIT);

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation mismatched = lookup.query("mismatched-package");
    ProviderInstalledStateObservation empty_version = lookup.query("empty-version-package");

    expect_unknown_failure(
            mismatched, PackageMetadataErrorCode::MalformedMetadata,
            "mismatched returned package name");
    expect_unknown_failure(
            empty_version, PackageMetadataErrorCode::MalformedMetadata,
            "empty returned package version");
    stub::require_local_package_query_expectations_consumed();
}

void test_invalid_package_name_is_strict_before_authority_access() {
    reset_stubs();
    ProviderInstalledStateLookup lookup;

    expect_metadata_error(
            [&lookup]() { static_cast<void>(lookup.query("invalid/package")); },
            PackageMetadataErrorCode::InvalidPackageName, "invalid package name");
    expect(stub::capture_command_call_count() == 0, "invalid name ran pacman-conf");
    expect(stub::initialize_call_count() == 0, "invalid name initialized libalpm");
    expect(stub::package_query_call_count() == 0, "invalid name reached the package query");
}

void test_configuration_command_failure_becomes_cached_unknown() {
    reset_stubs();
    enqueue_database_path_failure();

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation first = lookup.query("first-package");
    ProviderInstalledStateObservation second = lookup.query("second-package");

    expect_unknown_failure(
            first, PackageMetadataErrorCode::ConfigurationUnavailable,
            "configuration command failure");
    expect_unknown_failure(
            second, PackageMetadataErrorCode::ConfigurationUnavailable,
            "cached configuration command failure");
    expect(
            first.failure().diagnostic == second.failure().diagnostic,
            "cached session failure diagnostic differs");
    expect(stub::capture_command_call_count() == 1, "session failure retried pacman-conf");
    expect(stub::initialize_call_count() == 0, "configuration failure initialized libalpm");
    expect(stub::package_query_call_count() == 0, "configuration failure queried local DB");
}

void test_malformed_configuration_becomes_unknown() {
    reset_stubs();
    enqueue_malformed_database_paths();

    ProviderInstalledStateLookup lookup;
    expect_unknown_failure(
            lookup.query("configured-package"),
            PackageMetadataErrorCode::ConfigurationMalformed,
            "malformed configuration");
    expect(stub::initialize_call_count() == 0, "malformed configuration initialized libalpm");
}

void test_initialization_failure_becomes_unknown() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_initialize_failure(ALPM_ERR_SYSTEM);

    ProviderInstalledStateLookup lookup;
    expect_unknown_failure(
            lookup.query("initialize-package"),
            PackageMetadataErrorCode::InitializationFailed,
            "initialization failure");
    expect(stub::initialize_call_count() == 1, "initialization failure call count differs");
}

void test_local_database_failures_become_unknown() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_local_database_unavailable();
    {
        ProviderInstalledStateLookup lookup;
        expect_unknown_failure(
                lookup.query("unavailable-package"),
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                "local database unavailable");
    }

    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_local_database_invalid();
    {
        ProviderInstalledStateLookup lookup;
        expect_unknown_failure(
                lookup.query("invalid-database-package"),
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                "local database invalid");
    }

    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_cache_failure();
    {
        ProviderInstalledStateLookup lookup;
        expect_unknown_failure(
                lookup.query("cache-failure-package"),
                PackageMetadataErrorCode::LocalDatabaseUnavailable,
                "local database cache preload failure");
    }
}

void test_package_name_cache_is_per_name_and_preserves_query_order() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::enqueue_local_package_query_present(
            "alpha-package", "alpha-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);
    stub::enqueue_local_package_query_absent("beta-package");
    stub::enqueue_local_package_query_failure("gamma-package");

    ProviderInstalledStateLookup lookup;
    ProviderInstalledStateObservation alpha_first = lookup.query("alpha-package");
    ProviderInstalledStateObservation alpha_cached = lookup.query("alpha-package");
    ProviderInstalledStateObservation beta_first = lookup.query("beta-package");
    ProviderInstalledStateObservation beta_cached = lookup.query("beta-package");
    ProviderInstalledStateObservation gamma_first = lookup.query("gamma-package");
    ProviderInstalledStateObservation gamma_cached = lookup.query("gamma-package");

    expect_state(alpha_first, ProviderInstalledState::Installed, "first alpha query");
    expect_state(alpha_cached, ProviderInstalledState::Installed, "cached alpha query");
    expect_state(beta_first, ProviderInstalledState::NotInstalled, "first beta query");
    expect_state(beta_cached, ProviderInstalledState::NotInstalled, "cached beta query");
    expect_unknown_failure(
            gamma_first, PackageMetadataErrorCode::QueryFailed, "first gamma query");
    expect_unknown_failure(
            gamma_cached, PackageMetadataErrorCode::QueryFailed, "cached gamma query");
    expect(
            gamma_first.failure().diagnostic == gamma_cached.failure().diagnostic,
            "cached query failure diagnostic differs");

    expect(stub::package_query_call_count() == 3, "package-name cache query count differs");
    expect(
            stub::local_package_query_history() ==
                    std::vector<std::string>{
                            "alpha-package", "beta-package", "gamma-package"},
            "package-name cache query order differs");
    stub::require_local_package_query_expectations_consumed();
}

void test_lookup_releases_open_session_exactly_once() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_metadata("lifetime-package", "1.0-1", ALPM_PKG_REASON_EXPLICIT);

    {
        ProviderInstalledStateLookup lookup;
        expect_state(
                lookup.query("lifetime-package"), ProviderInstalledState::Installed,
                "lifetime package");
        expect(stub::release_call_count() == 0, "lookup released a live session early");
    }

    expect(stub::created_handle_count() == 1, "lookup created an unexpected handle count");
    expect(stub::release_count_for_handle(0) == 1, "lookup did not release the handle once");
}

void test_unknown_observation_owns_diagnostic_after_lookup_destruction() {
    reset_stubs();
    enqueue_valid_database_paths();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);

    ProviderInstalledStateObservation observation =
            ProviderInstalledStateObservation::not_installed();
    std::string expected_diagnostic;
    {
        ProviderInstalledStateLookup lookup;
        observation = lookup.query("owned-diagnostic-package");
        expected_diagnostic = observation.failure().diagnostic;
    }
    reset_stubs();

    expect_unknown_failure(
            observation, PackageMetadataErrorCode::QueryFailed, "owned diagnostic");
    expect(
            observation.failure().diagnostic == expected_diagnostic,
            "observation diagnostic did not outlive lookup and stub state");
}

} // namespace

int main() {
    try {
        test_pure_model_states_and_invariants();
        test_lookup_construction_is_lazy();
        test_first_valid_query_opens_authority_and_projects_present_package();
        test_package_absence_projects_to_not_installed();
        test_query_failure_projects_to_unknown();
        test_malformed_query_metadata_projects_to_unknown();
        test_invalid_package_name_is_strict_before_authority_access();
        test_configuration_command_failure_becomes_cached_unknown();
        test_malformed_configuration_becomes_unknown();
        test_initialization_failure_becomes_unknown();
        test_local_database_failures_become_unknown();
        test_package_name_cache_is_per_name_and_preserves_query_order();
        test_lookup_releases_open_session_exactly_once();
        test_unknown_observation_owns_diagnostic_after_lookup_destruction();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "provider installed-state tests: all checks passed\n";
    return 0;
}
