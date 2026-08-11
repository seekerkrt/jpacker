#include "package_constraint_metadata.hpp"
#include "alpm_stub.hpp"

#include <alpm.h>

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace stub = package_metadata_test_stub;

namespace {

static_assert(!std::is_pointer_v<decltype(InstalledExactPackage::package_name)>);
static_assert(!std::is_pointer_v<decltype(InstalledExactPackage::observed_version)>);
static_assert(!std::is_pointer_v<decltype(RepositoryExactPackage::repository)>);
static_assert(!std::is_pointer_v<decltype(RepositoryExactPackage::package_name)>);
static_assert(!std::is_pointer_v<decltype(RepositoryExactPackage::package_base)>);
static_assert(!std::is_pointer_v<decltype(RepositoryExactPackage::package_version)>);
static_assert(!std::is_pointer_v<decltype(RepositoryExactPackage::provides)>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Expected, typename... Alternatives>
Expected require_alternative(
        const std::variant<Alternatives...>& result,
        const std::string& context) {
    const Expected* value = std::get_if<Expected>(&result);
    if(value == nullptr) {
        throw std::runtime_error(context + ": unexpected result alternative");
    }
    return *value;
}

PackageMetadataSession open_installed_session() {
    return PackageMetadataSession::open(
            PacmanDatabasePaths{"/", "/var/lib/pacman/"});
}

PacmanRepositoryConfiguration repository_configuration(
        std::vector<std::string> repository_names = {"core", "extra"}) {
    return PacmanRepositoryConfiguration{
            PacmanDatabasePaths{"/", "/var/lib/pacman/"},
            std::move(repository_names)};
}

RepositoryExactPackageObservation require_repository_observation(
        const RepositoryExactPackageObservationResult& result,
        const std::string& context) {
    return require_alternative<RepositoryExactPackageObservation>(
            result,
            context);
}

void test_installed_exact_package_observed_version() {
    stub::reset_alpm_stub();
    stub::set_package_metadata(
            "installed-package", "2:1.2-3", ALPM_PKG_REASON_EXPLICIT);
    PackageMetadataSession session = open_installed_session();

    InstalledExactPackageObservationResult result =
            observe_installed_exact_package(session, "installed-package");
    const InstalledExactPackage& installed =
            require_alternative<InstalledExactPackage>(
                    result,
                    "installed exact package");
    expect(
            installed.package_name == "installed-package",
            "Installed package identity was not retained");
    expect(
            installed.observed_version.source() ==
                            ObservedVersionSource::InstalledExactPackage &&
                    installed.observed_version.version() != nullptr &&
                    *installed.observed_version.version() == "2:1.2-3",
            "Installed exact version was not projected as authoritative input");
}

void test_installed_absence_and_query_failure_are_distinct() {
    stub::reset_alpm_stub();
    stub::set_package_absent();
    PackageMetadataSession absent_session = open_installed_session();
    InstalledExactPackageObservationResult absent_result =
            observe_installed_exact_package(absent_session, "missing-package");
    const InstalledExactPackageAbsent& absent =
            require_alternative<InstalledExactPackageAbsent>(
                    absent_result,
                    "installed confirmed absence");
    expect(
            absent.package_name == "missing-package",
            "Installed absence lost package identity");

    stub::reset_alpm_stub();
    stub::set_package_query_failure(ALPM_ERR_DB_OPEN);
    PackageMetadataSession failure_session = open_installed_session();
    InstalledExactPackageObservationResult failure_result =
            observe_installed_exact_package(failure_session, "failed-package");
    const InstalledExactPackageQueryFailure& failure =
            require_alternative<InstalledExactPackageQueryFailure>(
                    failure_result,
                    "installed query failure");
    expect(
            failure.package_name == "failed-package" &&
                    failure.failure.code == PackageMetadataErrorCode::QueryFailed,
            "Installed query failure was flattened or lost provenance");
}

void test_installed_missing_and_invalid_versions_are_typed() {
    stub::reset_alpm_stub();
    stub::set_package_metadata(
            "missing-version", "1.0-1", ALPM_PKG_REASON_DEPEND);
    stub::set_null_package_version();
    PackageMetadataSession missing_session = open_installed_session();
    const InstalledExactPackage& missing =
            require_alternative<InstalledExactPackage>(
                    observe_installed_exact_package(
                            missing_session,
                            "missing-version"),
                    "installed missing version");
    expect(
            missing.observed_version.unknown_reason() != nullptr &&
                    *missing.observed_version.unknown_reason() ==
                            ObservedVersionUnknownReason::MissingVersionMetadata,
            "Installed missing version was not retained as Unknown");

    stub::reset_alpm_stub();
    stub::set_package_metadata(
            "invalid-version", "1.0 bad", ALPM_PKG_REASON_DEPEND);
    PackageMetadataSession invalid_session = open_installed_session();
    const InstalledExactPackage& invalid =
            require_alternative<InstalledExactPackage>(
                    observe_installed_exact_package(
                            invalid_session,
                            "invalid-version"),
                    "installed invalid version");
    expect(
            invalid.observed_version.invalid_reason() != nullptr &&
                    *invalid.observed_version.invalid_reason() ==
                            ConstraintInvalidReason::InvalidVersionIdentity,
            "Installed invalid version was not retained as Invalid");
}

void test_repository_exact_packages_retain_order_and_provenance() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "shared-package", 10, 20);
    stub::set_repository_package_version(
            "core", "shared-package", "3.0-1");
    stub::set_repository_package_metadata(
            "extra", "shared-package", 30, 40);
    stub::set_repository_package_version(
            "extra", "shared-package", "2.0-2");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "shared-package"),
                    "repository order and provenance");
    expect(
            observation.configured_repository_order ==
                    std::vector<std::string>({"core", "extra"}),
            "Configured repository order was not retained");
    expect(
            observation.source_results.size() == 2,
            "Repository observation did not retain every configured source");

    const RepositoryExactPackage& core =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[0],
                    "core exact package");
    const RepositoryExactPackage& extra =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[1],
                    "extra exact package");
    expect(
            core.repository == ConfiguredRepositoryIdentity{"core", 0} &&
                    core.package_name == "shared-package" &&
                    core.package_base == "shared-package" &&
                    core.package_version.version() != nullptr &&
                    *core.package_version.version() == "3.0-1",
            "First repository exact package identity/version differs");
    expect(
            extra.repository == ConfiguredRepositoryIdentity{"extra", 1} &&
                    extra.package_name == "shared-package" &&
                    extra.package_base == "shared-package" &&
                    extra.package_version.version() != nullptr &&
                    *extra.package_version.version() == "2.0-2",
            "Second repository exact package identity/version differs");
}

void test_repository_split_package_base_is_lossless() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "suite-child", 10, 20);
    stub::set_repository_package_version(
            "core", "suite-child", "4.0-1");
    stub::set_repository_package_base(
            "core", "suite-child", "suite");
    PacmanRepositoryConfiguration configuration =
            repository_configuration({"core"});

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration, "suite-child"),
                    "repository split PackageBase projection");
    const RepositoryExactPackage& package =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results.front(),
                    "repository split exact package");
    expect(
            package.package_name == "suite-child" &&
                    package.package_base == "suite" &&
                    package.package_version.version() != nullptr &&
                    *package.package_version.version() == "4.0-1",
            "Repository constraint projection flattened split PackageBase");
}

void test_repository_confirmed_absence() {
    stub::reset_alpm_stub();
    stub::set_repository_package_absent("core", "missing-package");
    stub::set_repository_package_absent("extra", "missing-package");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "missing-package"),
                    "repository confirmed absence");
    expect(
            observation.source_results.size() == 2,
            "Repository absence did not retain all configured sources");
    const RepositoryExactPackageAbsent& core =
            require_alternative<RepositoryExactPackageAbsent>(
                    observation.source_results[0],
                    "core confirmed absence");
    const RepositoryExactPackageAbsent& extra =
            require_alternative<RepositoryExactPackageAbsent>(
                    observation.source_results[1],
                    "extra confirmed absence");
    expect(
            core.repository == ConfiguredRepositoryIdentity{"core", 0} &&
                    extra.repository ==
                            ConfiguredRepositoryIdentity{"extra", 1},
            "Repository absence lost configured provenance/order");
}

void test_partial_repository_failure_is_not_absence() {
    stub::reset_alpm_stub();
    stub::set_repository_package_query_failure(
            "core", "partial-package", ALPM_ERR_DB_OPEN);
    stub::set_repository_package_metadata(
            "extra", "partial-package", 30, 40);
    stub::set_repository_package_version(
            "extra", "partial-package", "1.5-2");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "partial-package"),
                    "partial repository failure");
    const RepositoryExactPackageSourceFailure& core_failure =
            require_alternative<RepositoryExactPackageSourceFailure>(
                    observation.source_results[0],
                    "failed repository source");
    const PackageMetadataFailure& query_failure =
            require_alternative<PackageMetadataFailure>(
                    core_failure.reason,
                    "repository query failure reason");
    const RepositoryExactPackage& extra_package =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[1],
                    "successful repository after failure");
    expect(
            core_failure.repository ==
                            ConfiguredRepositoryIdentity{"core", 0} &&
                    query_failure.code == PackageMetadataErrorCode::QueryFailed,
            "Repository query failure lost source provenance");
    expect(
            extra_package.repository ==
                            ConfiguredRepositoryIdentity{"extra", 1} &&
                    extra_package.package_version.version() != nullptr &&
                    *extra_package.package_version.version() == "1.5-2",
            "Later repository result was lost after a partial failure");
}

enum class RepositoryOpenFailurePhase {
    Registration,
    Validation,
    CacheLoad,
};

void expect_partial_repository_open_failure(
        RepositoryOpenFailurePhase phase,
        const std::vector<std::string>& expected_operations,
        const std::string& context) {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "open-partial-package", 10, 20);
    stub::set_repository_package_version(
            "core", "open-partial-package", "3.0-1");
    stub::set_repository_package_metadata(
            "testing", "open-partial-package", 30, 40);
    stub::set_repository_package_version(
            "testing", "open-partial-package", "4.0-2");
    switch(phase) {
        case RepositoryOpenFailurePhase::Registration:
            stub::set_sync_database_register_failure("extra");
            break;
        case RepositoryOpenFailurePhase::Validation:
            stub::set_sync_database_validation_failure("extra");
            break;
        case RepositoryOpenFailurePhase::CacheLoad:
            stub::set_sync_database_cache_failure("extra");
            break;
    }

    PacmanRepositoryConfiguration configuration =
            repository_configuration({"core", "extra", "testing"});
    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "open-partial-package"),
                    context);
    expect(
            observation.configured_repository_order ==
                    std::vector<std::string>({"core", "extra", "testing"}),
            context + ": configured order differs");
    expect(
            observation.source_results.size() == 3,
            context + ": source result count differs");

    const RepositoryExactPackage& core =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[0],
                    context + ": core source");
    const RepositoryExactPackageSourceFailure& extra_failure =
            require_alternative<RepositoryExactPackageSourceFailure>(
                    observation.source_results[1],
                    context + ": extra source failure");
    const PackageMetadataFailure& open_failure =
            require_alternative<PackageMetadataFailure>(
                    extra_failure.reason,
                    context + ": extra failure reason");
    const RepositoryExactPackage& testing =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[2],
                    context + ": testing source");
    expect(
            core.repository == ConfiguredRepositoryIdentity{"core", 0} &&
                    extra_failure.repository ==
                            ConfiguredRepositoryIdentity{"extra", 1} &&
                    testing.repository ==
                            ConfiguredRepositoryIdentity{"testing", 2},
            context + ": source provenance/order differs");
    expect(
            open_failure.code ==
                    PackageMetadataErrorCode::SyncDatabaseUnavailable,
            context + ": open failure was flattened");

    const std::vector<stub::RepositoryPackageQuery> query_history =
            stub::repository_package_query_history();
    expect(
            query_history.size() == 2 &&
                    query_history[0].repository_name == "core" &&
                    query_history[1].repository_name == "testing",
            context + ": successful sources were not queried independently");
    expect(
            stub::sync_database_operation_history() == expected_operations,
            context + ": repository open did not continue in configured order");
    expect(
            stub::created_handle_count() == 3 &&
                    stub::release_call_count() == 3,
            context + ": per-source read phases were not released exactly once");
}

void test_repository_open_failures_are_source_local() {
    expect_partial_repository_open_failure(
            RepositoryOpenFailurePhase::Registration,
            {
                    "register core", "valid core", "cache core",
                    "query core/open-partial-package",
                    "register extra",
                    "register testing", "valid testing", "cache testing",
                    "query testing/open-partial-package",
            },
            "repository registration partial failure");
    expect_partial_repository_open_failure(
            RepositoryOpenFailurePhase::Validation,
            {
                    "register core", "valid core", "cache core",
                    "query core/open-partial-package",
                    "register extra", "valid extra",
                    "register testing", "valid testing", "cache testing",
                    "query testing/open-partial-package",
            },
            "repository validation partial failure");
    expect_partial_repository_open_failure(
            RepositoryOpenFailurePhase::CacheLoad,
            {
                    "register core", "valid core", "cache core",
                    "query core/open-partial-package",
                    "register extra", "valid extra", "cache extra",
                    "register testing", "valid testing", "cache testing",
                    "query testing/open-partial-package",
            },
            "repository cache-load partial failure");
}

void test_global_repository_open_failure_remains_top_level() {
    stub::reset_alpm_stub();
    stub::set_initialize_failure(ALPM_ERR_SYSTEM);

    RepositoryExactPackageObservationResult result =
            observe_repository_exact_package(
                    repository_configuration({"core", "extra"}),
                    "initialization-package");
    const RepositoryExactPackageObservationFailure& failure =
            require_alternative<RepositoryExactPackageObservationFailure>(
                    result,
                    "global repository initialization failure");
    expect(
            failure.package_name == "initialization-package" &&
                    failure.failure.code ==
                            PackageMetadataErrorCode::InitializationFailed,
            "Global initialization failure became a source-local result");
    expect(
            stub::initialize_call_count() == 1 &&
                    stub::sync_database_operation_history().empty(),
            "Global initialization failure continued into repository sources");
}

void test_repository_missing_and_invalid_versions_are_typed() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "version-package", 10, 20);
    stub::set_repository_package_version_null(
            "core", "version-package");
    stub::set_repository_package_metadata(
            "extra", "version-package", 30, 40);
    stub::set_repository_package_version(
            "extra", "version-package", "bad version");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "version-package"),
                    "repository version states");
    const RepositoryExactPackage& missing =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[0],
                    "repository missing version");
    const RepositoryExactPackage& invalid =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[1],
                    "repository invalid version");
    expect(
            missing.package_version.unknown_reason() != nullptr &&
                    *missing.package_version.unknown_reason() ==
                            ObservedVersionUnknownReason::MissingVersionMetadata,
            "Repository missing version was not retained as Unknown");
    expect(
            invalid.package_version.invalid_reason() != nullptr &&
                    *invalid.package_version.invalid_reason() ==
                            ConstraintInvalidReason::InvalidVersionIdentity,
            "Repository invalid version was not retained as Invalid");
}

void test_repository_provides_use_equality_only_capabilities() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "provider-package", 10, 20);
    stub::set_repository_package_version(
            "core", "provider-package", "9.0-1");
    stub::set_repository_package_provides(
            "core",
            "provider-package",
            {
                    stub::RepositoryProvidedPackageMetadata{
                            std::string("foo"),
                            std::nullopt,
                            ALPM_DEP_MOD_ANY},
                    stub::RepositoryProvidedPackageMetadata{
                            std::string("bar"),
                            std::string("1.2-3"),
                            ALPM_DEP_MOD_EQ},
            });
    stub::set_repository_package_absent("extra", "provider-package");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "provider-package"),
                    "repository Provides");
    const RepositoryExactPackage& package =
            require_alternative<RepositoryExactPackage>(
                    observation.source_results[0],
                    "repository provider package");
    expect(
            package.provides.size() == 2,
            "Repository Provides capability count differs");

    const RepositoryProviderCapability& unversioned = package.provides[0];
    expect(
            unversioned.capability.raw_specification() == "foo" &&
                    unversioned.capability.package_name() == "foo" &&
                    !unversioned.capability.version().has_value() &&
                    unversioned.provided_version.source() ==
                            ObservedVersionSource::RepositoryProviderCapability &&
                    unversioned.provided_version.unknown_reason() != nullptr &&
                    *unversioned.provided_version.unknown_reason() ==
                            ObservedVersionUnknownReason::
                                    UnversionedProviderCapability,
            "Unversioned repository Provide was not projected strictly");

    const RepositoryProviderCapability& equality = package.provides[1];
    expect(
            equality.capability.raw_specification() == "bar=1.2-3" &&
                    equality.capability.package_name() == "bar" &&
                    equality.capability.version() ==
                            std::optional<std::string>("1.2-3") &&
                    equality.provided_version.version() != nullptr &&
                    *equality.provided_version.version() == "1.2-3",
            "Equality repository Provide was not projected strictly");
    expect(
            package.package_version.version() != nullptr &&
                    *package.package_version.version() == "9.0-1" &&
                    *equality.provided_version.version() !=
                            *package.package_version.version(),
            "Provider package version was substituted for capability version");
}

void test_repository_provides_reject_empty_unversioned_metadata() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "empty-unversioned-provider", 10, 20);
    stub::set_repository_package_version(
            "core", "empty-unversioned-provider", "7.0-1");
    stub::set_repository_package_provides(
            "core",
            "empty-unversioned-provider",
            {stub::RepositoryProvidedPackageMetadata{
                    std::string("foo"),
                    std::string(""),
                    ALPM_DEP_MOD_ANY}});
    stub::set_repository_package_absent(
            "extra", "empty-unversioned-provider");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "empty-unversioned-provider"),
                    "empty unversioned repository Provide");
    const RepositoryExactPackageSourceFailure& source_failure =
            require_alternative<RepositoryExactPackageSourceFailure>(
                    observation.source_results[0],
                    "empty unversioned provider source failure");
    const DependencyConstraintParseFailure& parse_failure =
            require_alternative<DependencyConstraintParseFailure>(
                    source_failure.reason,
                    "empty unversioned provider parse failure");
    expect(
            source_failure.repository ==
                            ConfiguredRepositoryIdentity{"core", 0} &&
                    parse_failure.kind ==
                            DependencyConstraintParseFailureKind::
                                    UnsupportedProviderOperator &&
                    parse_failure.raw_specification == "foo>",
            "Empty unversioned repository Provide became a valid capability");
}

void test_repository_provides_reject_non_equality_operator() {
    stub::reset_alpm_stub();
    stub::set_repository_package_metadata(
            "core", "invalid-provider", 10, 20);
    stub::set_repository_package_version(
            "core", "invalid-provider", "7.0-1");
    stub::set_repository_package_provides(
            "core",
            "invalid-provider",
            {stub::RepositoryProvidedPackageMetadata{
                    std::string("foo"),
                    std::string("1"),
                    ALPM_DEP_MOD_GE}});
    stub::set_repository_package_absent("extra", "invalid-provider");
    PacmanRepositoryConfiguration configuration = repository_configuration();

    const RepositoryExactPackageObservation& observation =
            require_repository_observation(
                    observe_repository_exact_package(
                            configuration,
                            "invalid-provider"),
                    "invalid repository Provide");
    const RepositoryExactPackageSourceFailure& source_failure =
            require_alternative<RepositoryExactPackageSourceFailure>(
                    observation.source_results[0],
                    "non-equality provider source failure");
    const DependencyConstraintParseFailure& parse_failure =
            require_alternative<DependencyConstraintParseFailure>(
                    source_failure.reason,
                    "non-equality provider parse failure");
    expect(
            source_failure.repository ==
                            ConfiguredRepositoryIdentity{"core", 0} &&
                    parse_failure.kind ==
                            DependencyConstraintParseFailureKind::
                                    UnsupportedProviderOperator &&
                    parse_failure.raw_specification == "foo>=1",
            "Non-equality repository Provide was not rejected strictly");
}

} // namespace

int main() {
    try {
        test_installed_exact_package_observed_version();
        test_installed_absence_and_query_failure_are_distinct();
        test_installed_missing_and_invalid_versions_are_typed();
        test_repository_exact_packages_retain_order_and_provenance();
        test_repository_split_package_base_is_lossless();
        test_repository_confirmed_absence();
        test_partial_repository_failure_is_not_absence();
        test_repository_open_failures_are_source_local();
        test_global_repository_open_failure_remains_top_level();
        test_repository_missing_and_invalid_versions_are_typed();
        test_repository_provides_use_equality_only_capabilities();
        test_repository_provides_reject_empty_unversioned_metadata();
        test_repository_provides_reject_non_equality_operator();
    } catch(const std::exception& error) {
        std::cerr << "package constraint metadata test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
