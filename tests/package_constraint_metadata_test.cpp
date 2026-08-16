#include "package_constraint_metadata.hpp"
#include "installed_package_relation_inventory.hpp"
#include "package_relation_observation_adapter.hpp"
#include "alpm_stub.hpp"

#include <alpm.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
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

DeclaredPackageRelation require_relation(
        PackageRelationKind kind, const std::string& specification) {
    const DeclaredPackageRelationParseResult parsed =
            parse_declared_package_relation(
                    "declaring-child", "declaring-base", kind,
                    specification);
    expect(parsed.relation() != nullptr, "Relation fixture did not parse");
    return *parsed.relation();
}

PackageRelationInstalledDatabaseIdentity installed_relation_source() {
    return PackageRelationInstalledDatabaseIdentity{
            "/", "/var/lib/pacman"};
}

RepositoryExactPackage repository_relation_package(
        ConfiguredRepositoryIdentity repository,
        std::string package_name,
        std::vector<RepositoryProviderCapability> provides = {}) {
    return RepositoryExactPackage{
            std::move(repository),
            package_name,
            package_name + "-base",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage, "1"),
            std::move(provides)};
}

RepositoryProviderCapability repository_relation_capability(
        const std::string& specification) {
    const ProviderCapabilityParseResult parsed =
            parse_provider_capability(specification);
    expect(
            parsed.capability() != nullptr,
            "Repository relation capability fixture did not parse");
    const ProviderCapability capability = *parsed.capability();
    return RepositoryProviderCapability{
            capability,
            capability.version().has_value()
                    ? ObservedVersion::available(
                              ObservedVersionSource::
                                      RepositoryProviderCapability,
                              capability.version().value())
                    : ObservedVersion::unknown(
                              ObservedVersionSource::
                                      RepositoryProviderCapability,
                              ObservedVersionUnknownReason::
                                      UnversionedProviderCapability)};
}

PackageMetadataFailure repository_relation_query_failure(
        const std::string& diagnostic) {
    return PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed, diagnostic};
}

void expect_version_comparison(
        const std::string& lhs, const std::string& rhs,
        const std::string& result) {
    expect(
            ::setenv(
                    "MOGUET_TEST_ALPM_VERCMP_EXPECTED_LHS",
                    lhs.c_str(), 1) == 0 &&
                    ::setenv(
                            "MOGUET_TEST_ALPM_VERCMP_EXPECTED_RHS",
                            rhs.c_str(), 1) == 0 &&
                    ::setenv(
                            "MOGUET_TEST_ALPM_VERCMP_RESULT",
                            result.c_str(), 1) == 0,
            "Failed to configure version comparison stub");
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

void test_installed_relation_inventory_retains_versions_and_provides() {
    stub::reset_alpm_stub();
    stub::set_local_packages(
            {stub::LocalPackageMetadata{
                    "real-package",
                    "2.0",
                    ALPM_PKG_REASON_EXPLICIT,
                    {stub::RepositoryProvidedPackageMetadata{
                             std::string("virtual-unversioned"),
                             std::nullopt,
                             ALPM_DEP_MOD_ANY},
                     stub::RepositoryProvidedPackageMetadata{
                             std::string("virtual-api"),
                             std::string("3"),
                             ALPM_DEP_MOD_EQ},
                     stub::RepositoryProvidedPackageMetadata{
                             std::string("other-api"),
                             std::string("8"),
                             ALPM_DEP_MOD_EQ}}}});
    PackageMetadataSession session = open_installed_session();
    const InstalledPackageRelationInventory& inventory =
            require_alternative<InstalledPackageRelationInventory>(
                    observe_installed_package_relations(
                            session, installed_relation_source()),
                    "installed relation inventory");
    expect(
            inventory.source == installed_relation_source() &&
                    inventory.packages.size() == 1,
            "Installed inventory lost source identity or package");
    const PackageRelationObservedPackage& package = inventory.packages.front();
    expect(
            package.package_name == "real-package" &&
                    package.role ==
                            PackageRelationObservationRole::Installed &&
                    !package.package_base.has_value() &&
                    package.package_version.version() != nullptr &&
                    *package.package_version.version() == "2.0" &&
                    package.provides.size() == 3,
            "Installed exact package observation differs");
    expect(
            package.provides[0].capability.package_name() ==
                            "virtual-unversioned" &&
                    package.provides[0].observed_version.source() ==
                            ObservedVersionSource::
                                    InstalledProviderCapability &&
                    package.provides[0].observed_version.version() ==
                            nullptr &&
                    package.provides[0].observed_version.unknown_reason() !=
                            nullptr &&
                    *package.provides[0]
                             .observed_version.unknown_reason() ==
                            ObservedVersionUnknownReason::
                                    UnversionedProviderCapability,
            "Unversioned installed Provide inherited a package version");
    expect(
            package.provides[1].capability.package_name() ==
                            "virtual-api" &&
                    package.provides[1].observed_version.version() !=
                            nullptr &&
                    *package.provides[1].observed_version.version() == "3" &&
                    package.provides[2].capability.package_name() ==
                            "other-api",
            "Versioned or multiple installed Provides were not retained");

    const PackageRelationObservationSet observations =
            project_installed_relation_observations(
                    InstalledPackageRelationInventoryResult{inventory});
    expect_version_comparison("2.0", "2", "1");
    const PackageRelationMatchingEvidence exact =
            match_declared_package_relation(
                    require_relation(
                            PackageRelationKind::Conflict,
                            "real-package>=2"),
                    observations);
    expect_version_comparison("3", "3", "0");
    const PackageRelationMatchingEvidence provided =
            match_declared_package_relation(
                    require_relation(
                            PackageRelationKind::Replacement,
                            "virtual-api>=3"),
                    observations);
    const PackageRelationMatchingEvidence unavailable =
            match_declared_package_relation(
                    require_relation(
                            PackageRelationKind::Conflict,
                            "virtual-unversioned>=1"),
                    observations);
    expect(
            package_relation_has_confirmed_match(exact) &&
                    package_relation_has_confirmed_match(provided) &&
                    !package_relation_has_confirmed_match(unavailable) &&
                    !package_relation_confirms_no_match(unavailable) &&
                    unavailable.package_evidence.front().version_match ==
                            PackageRelationVersionMatchKind::Unavailable,
            "Installed exact/provided matching semantics differ");

    const PackageRelationMatchingEvidence exact_miss =
            match_declared_package_relation(
                    require_relation(
                            PackageRelationKind::Conflict,
                            "missing-package"),
                    observations);
    expect(
            package_relation_confirms_no_match(exact_miss),
            "Complete installed inventory did not prove exact miss");
}

void test_installed_inventory_empty_and_failures_are_distinct() {
    stub::reset_alpm_stub();
    stub::set_empty_package_cache();
    const InstalledPackageRelationInventory& empty =
            require_alternative<InstalledPackageRelationInventory>(
                    query_installed_package_relations(
                            PacmanDatabasePaths{"/", "/var/lib/pacman"}),
                    "successful empty installed inventory");
    expect(
            empty.packages.empty(),
            "Successful empty inventory was not retained");
    expect(
            project_installed_relation_observations(
                    InstalledPackageRelationInventoryResult{empty})
                            .completeness ==
                    PackageRelationObservationCompleteness::Complete,
            "Successful empty inventory was not Complete");

    stub::reset_alpm_stub();
    stub::set_initialize_failure(ALPM_ERR_SYSTEM);
    const InstalledPackageRelationInventoryFailure& session_failure =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    query_installed_package_relations(
                            PacmanDatabasePaths{"/", "/var/lib/pacman"}),
                    "installed session failure");
    expect(
            std::get<PackageMetadataFailure>(session_failure.reason).code ==
                    PackageMetadataErrorCode::InitializationFailed,
            "Installed session failure was flattened");
    expect(
            project_installed_relation_observations(
                    InstalledPackageRelationInventoryResult{
                            session_failure})
                            .completeness ==
                    PackageRelationObservationCompleteness::Unavailable,
            "Installed session failure did not remain Unavailable");

    stub::reset_alpm_stub();
    stub::set_local_database_unavailable();
    const InstalledPackageRelationInventoryFailure& database_failure =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    query_installed_package_relations(
                            PacmanDatabasePaths{"/", "/var/lib/pacman"}),
                    "installed local DB failure");
    expect(
            std::get<PackageMetadataFailure>(database_failure.reason).code ==
                    PackageMetadataErrorCode::LocalDatabaseUnavailable,
            "Local DB failure became an empty inventory");

    stub::reset_alpm_stub();
    stub::set_local_packages(
            {stub::LocalPackageMetadata{
                    "query-package", "1", ALPM_PKG_REASON_EXPLICIT, {}}});
    PackageMetadataSession query_session = open_installed_session();
    stub::set_local_package_cache_entry_null(0);
    const InstalledPackageRelationInventoryFailure& query_failure =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    observe_installed_package_relations(
                            query_session, installed_relation_source()),
                    "installed inventory query failure");
    expect(
            std::get<PackageMetadataFailure>(query_failure.reason).code ==
                    PackageMetadataErrorCode::QueryFailed,
            "Installed query failure became an empty inventory");
}

void test_installed_inventory_rejects_invalid_identity_and_provides() {
    stub::reset_alpm_stub();
    stub::set_local_packages(
            {stub::LocalPackageMetadata{
                    "bad/name", "1", ALPM_PKG_REASON_EXPLICIT, {}}});
    PackageMetadataSession identity_session = open_installed_session();
    const InstalledPackageRelationInventoryFailure& invalid_identity =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    observe_installed_package_relations(
                            identity_session,
                            installed_relation_source()),
                    "installed invalid identity");
    expect(
            std::get<PackageMetadataFailure>(invalid_identity.reason).code ==
                    PackageMetadataErrorCode::MalformedMetadata,
            "Invalid installed identity was not rejected");

    stub::reset_alpm_stub();
    stub::set_local_packages(
            {stub::LocalPackageMetadata{
                    "provider-package",
                    "5",
                    ALPM_PKG_REASON_EXPLICIT,
                    {stub::RepositoryProvidedPackageMetadata{
                            std::nullopt,
                            std::string("3"),
                            ALPM_DEP_MOD_EQ}}}});
    PackageMetadataSession provide_session = open_installed_session();
    const InstalledPackageRelationInventoryFailure& malformed_provide =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    observe_installed_package_relations(
                            provide_session,
                            installed_relation_source()),
                    "installed malformed provide");
    expect(
            malformed_provide.package_name ==
                            std::optional<std::string>("provider-package") &&
                    std::holds_alternative<
                            DependencyConstraintParseFailure>(
                            malformed_provide.reason),
            "Malformed installed Provide was not typed per package");
    expect(
            project_installed_relation_observations(
                    InstalledPackageRelationInventoryResult{
                            malformed_provide})
                            .completeness ==
                    PackageRelationObservationCompleteness::Invalid,
            "Malformed installed Provide was not Invalid observation");

    stub::reset_alpm_stub();
    stub::set_local_packages(
            {stub::LocalPackageMetadata{
                     "retained-package",
                     "2",
                     ALPM_PKG_REASON_EXPLICIT,
                     {}},
             stub::LocalPackageMetadata{
                     "broken-provider",
                     "1",
                     ALPM_PKG_REASON_EXPLICIT,
                     {stub::RepositoryProvidedPackageMetadata{
                             std::nullopt,
                             std::string("1"),
                             ALPM_DEP_MOD_EQ}}}});
    PackageMetadataSession partial_session = open_installed_session();
    const InstalledPackageRelationInventoryFailure& partial_failure =
            require_alternative<InstalledPackageRelationInventoryFailure>(
                    observe_installed_package_relations(
                            partial_session,
                            installed_relation_source()),
                    "installed partial malformed inventory");
    const PackageRelationObservationSet partial_observations =
            project_installed_relation_observations(
                    InstalledPackageRelationInventoryResult{
                            partial_failure});
    const PackageRelationMatchingEvidence retained_match =
            match_declared_package_relation(
                    require_relation(
                            PackageRelationKind::Conflict,
                            "retained-package"),
                    partial_observations);
    expect(
            partial_failure.package_index == 1 &&
                    partial_failure.observed_packages.size() == 1 &&
                    partial_observations.completeness ==
                            PackageRelationObservationCompleteness::Invalid &&
                    partial_observations.packages.size() == 1 &&
                    partial_observations.failures.size() == 1 &&
                    package_relation_has_confirmed_match(retained_match) &&
                    !package_relation_confirms_no_match(retained_match),
            "Installed invalid entry discarded prior confirmed evidence");
}

void test_repository_and_aur_observation_adapters_retain_source() {
    const RepositoryExactPackage repository_package{
            ConfiguredRepositoryIdentity{"extra", 1},
            "repo-child",
            "repo-base",
            ObservedVersion::available(
                    ObservedVersionSource::RepositoryExactPackage,
                    "6"),
            {RepositoryProviderCapability{
                    ProviderCapability(
                            "repo-virtual=2", "repo-virtual", "2"),
                    ObservedVersion::available(
                            ObservedVersionSource::
                                    RepositoryProviderCapability,
                            "2")}}};
    const PackageRelationObservedPackage repository =
            project_repository_relation_observation(
                    repository_package,
                    PackageRelationObservationRole::RepositoryCandidate);
    expect(
            repository.package_name == "repo-child" &&
                    repository.package_base ==
                            std::optional<std::string>("repo-base") &&
                    std::get<ConfiguredRepositoryIdentity>(
                            repository.source) ==
                            ConfiguredRepositoryIdentity{"extra", 1} &&
                    repository.provides.size() == 1,
            "Repository observation adapter flattened source or provides");

    const RepositoryExactPackageObservationResult partial_input =
            RepositoryExactPackageObservation{
                    {"core", "extra", "testing"},
                    {RepositoryExactPackageAbsent{
                             ConfiguredRepositoryIdentity{"core", 0},
                             "repo-child"},
                     repository_package,
                     RepositoryExactPackageSourceFailure{
                             ConfiguredRepositoryIdentity{"testing", 2},
                             "repo-child",
                             PackageMetadataFailure{
                                     PackageMetadataErrorCode::QueryFailed,
                                     "testing source unavailable"}}}};
    const PackageRelationObservationSet partial =
            project_repository_exact_relation_observations(partial_input);
    expect(
            partial.completeness ==
                            PackageRelationObservationCompleteness::Partial &&
                    partial.required_sources ==
                            std::vector<PackageRelationSourceIdentity>{
                                    ConfiguredRepositoryIdentity{"core", 0},
                                    ConfiguredRepositoryIdentity{"extra", 1},
                                    ConfiguredRepositoryIdentity{"testing", 2}} &&
                    partial.packages.size() == 1 &&
                    partial.failures.size() == 1,
            "Repository partial source evidence was flattened");

    const DeclaredPackageRelation aur_relation = require_relation(
            PackageRelationKind::Conflict, "old-component<2");
    const AurPackageConstraintMetadata aur_metadata{
            "aur-child",
            "aur-base",
            ObservedVersion::available(
                    ObservedVersionSource::AurExactPackage, "3"),
            {},
            {},
            {},
            {AurProviderCapabilityMetadata{
                    ProviderCapability(
                            "aur-virtual=4", "aur-virtual", "4"),
                    ObservedVersion::available(
                            ObservedVersionSource::AurProviderCapability,
                            "4")}},
            {aur_relation}};
    const PlannedPackageRelationObservation aur =
            project_aur_relation_observation(
                    aur_metadata,
                    {PackageRelationRootAttribution{2, "aur-child"}});
    expect(
            aur.package.package_name == "aur-child" &&
                    aur.package.package_base ==
                            std::optional<std::string>("aur-base") &&
                    std::get<PackageRelationAurSourceIdentity>(
                            aur.package.source) ==
                            PackageRelationAurSourceIdentity{
                                    "aur-child", "aur-base"} &&
                    aur.package.package_version.version() != nullptr &&
                    *aur.package.package_version.version() == "3" &&
                    aur.package.provides.size() == 1 &&
                    aur.package.roots ==
                            std::vector<PackageRelationRootAttribution>{
                                    {2, "aur-child"}} &&
                    aur.declarations ==
                            std::vector<DeclaredPackageRelation>{
                                    aur_relation},
            "AUR observation adapter lost direct typed metadata");
}

void test_repository_relation_composite_requires_both_identity_channels() {
    const std::vector<std::string> order{"core", "extra"};
    const ConfiguredRepositoryIdentity core{"core", 0};
    const ConfiguredRepositoryIdentity extra{"extra", 1};
    const RepositoryProviderObservationResult empty_providers =
            RepositoryProviderObservation{
                    order,
                    {RepositoryProviderSourceObservation{core, {}},
                     RepositoryProviderSourceObservation{extra, {}}}};

    const RepositoryExactPackageObservationResult virtual_exact_absent =
            RepositoryExactPackageObservation{
                    order,
                    {RepositoryExactPackageAbsent{core, "virtual-api"},
                     RepositoryExactPackageAbsent{extra, "virtual-api"}}};
    const RepositoryExactPackage provider_package =
            repository_relation_package(
                    core, "provider-package",
                    {repository_relation_capability("virtual-api")});
    const RepositoryProviderObservationResult virtual_provider_present =
            RepositoryProviderObservation{
                    order,
                    {RepositoryProviderSourceObservation{
                             core, {provider_package}},
                     RepositoryProviderSourceObservation{extra, {}}}};
    const DeclaredPackageRelation virtual_relation = require_relation(
            PackageRelationKind::Conflict, "virtual-api");

    const PackageRelationMatchingEvidence exact_only =
            match_declared_package_relation(
                    virtual_relation,
                    project_repository_exact_relation_observations(
                            virtual_exact_absent));
    expect(
            exact_only.observation_completeness ==
                            PackageRelationObservationCompleteness::Complete &&
                    !package_relation_has_complete_identity_coverage(
                            exact_only) &&
                    !package_relation_confirms_no_match(exact_only),
            "Exact-only repository coverage proved NoMatch");

    const PackageRelationObservationSet provider_composite =
            project_repository_relation_observations(
                    virtual_exact_absent, virtual_provider_present);
    const PackageRelationMatchingEvidence provider_match =
            match_declared_package_relation(
                    virtual_relation, provider_composite);
    expect(
            provider_composite.completeness ==
                            PackageRelationObservationCompleteness::Complete &&
                    package_relation_has_complete_identity_coverage(
                            provider_match) &&
                    package_relation_has_confirmed_match(provider_match) &&
                    !package_relation_confirms_no_match(provider_match) &&
                    provider_composite.required_sources ==
                            std::vector<PackageRelationSourceIdentity>{
                                    core, extra},
            "Exact absence hid a matching repository provider");

    const RepositoryExactPackage exact_package =
            repository_relation_package(core, "exact-target");
    const RepositoryExactPackageObservationResult exact_present =
            RepositoryExactPackageObservation{
                    order,
                    {exact_package,
                     RepositoryExactPackageAbsent{extra, "exact-target"}}};
    const DeclaredPackageRelation exact_relation = require_relation(
            PackageRelationKind::Replacement, "exact-target");
    const PackageRelationMatchingEvidence provider_only =
            match_declared_package_relation(
                    exact_relation,
                    project_repository_provider_relation_observations(
                            empty_providers));
    expect(
            provider_only.observation_completeness ==
                            PackageRelationObservationCompleteness::Complete &&
                    !package_relation_has_complete_identity_coverage(
                            provider_only) &&
                    !package_relation_confirms_no_match(provider_only),
            "Provider-only repository coverage proved NoMatch");

    const PackageRelationMatchingEvidence exact_match =
            match_declared_package_relation(
                    exact_relation,
                    project_repository_relation_observations(
                            exact_present, empty_providers));
    expect(
            package_relation_has_confirmed_match(exact_match) &&
                    !package_relation_confirms_no_match(exact_match),
            "Empty provider search hid an exact repository package");

    const RepositoryExactPackageObservationResult missing_exact =
            RepositoryExactPackageObservation{
                    order,
                    {RepositoryExactPackageAbsent{core, "missing-target"},
                     RepositoryExactPackageAbsent{
                             extra, "missing-target"}}};
    const DeclaredPackageRelation missing_relation = require_relation(
            PackageRelationKind::Conflict, "missing-target");
    const PackageRelationMatchingEvidence confirmed_absence =
            match_declared_package_relation(
                    missing_relation,
                    project_repository_relation_observations(
                            missing_exact, empty_providers));
    expect(
            package_relation_has_complete_identity_coverage(
                    confirmed_absence) &&
                    package_relation_confirms_no_match(confirmed_absence),
            "Complete exact/provider repository absence did not prove NoMatch");

    const RepositoryExactPackageObservationResult exact_query_failure =
            RepositoryExactPackageObservationFailure{
                    "missing-target",
                    repository_relation_query_failure(
                            "exact repository query failed")};
    const PackageRelationMatchingEvidence failed_exact =
            match_declared_package_relation(
                    missing_relation,
                    project_repository_relation_observations(
                            exact_query_failure, empty_providers));
    expect(
            !failed_exact.observation_failures.empty() &&
                    !package_relation_has_complete_identity_coverage(
                            failed_exact) &&
                    !package_relation_confirms_no_match(failed_exact),
            "Exact query failure was flattened by an empty provider search");

    const RepositoryProviderObservationResult provider_query_failure =
            RepositoryProviderObservationFailure{
                    "missing-target",
                    repository_relation_query_failure(
                            "provider repository query failed")};
    const PackageRelationMatchingEvidence failed_provider =
            match_declared_package_relation(
                    missing_relation,
                    project_repository_relation_observations(
                            missing_exact, provider_query_failure));
    expect(
            !failed_provider.observation_failures.empty() &&
                    !package_relation_has_complete_identity_coverage(
                            failed_provider) &&
                    !package_relation_confirms_no_match(failed_provider),
            "Provider query failure was flattened by exact absence");

    const RepositoryProviderObservationResult partial_providers =
            RepositoryProviderObservation{
                    order,
                    {RepositoryProviderSourceObservation{core, {}},
                     RepositoryProviderSourceFailure{
                             extra,
                             repository_relation_query_failure(
                                     "extra provider query failed")}}};
    const PackageRelationMatchingEvidence exact_match_with_failure =
            match_declared_package_relation(
                    exact_relation,
                    project_repository_relation_observations(
                            exact_present, partial_providers));
    expect(
            package_relation_has_confirmed_match(
                    exact_match_with_failure) &&
                    exact_match_with_failure.observation_failures.size() == 1 &&
                    exact_match_with_failure.observation_failures.front()
                                    .source ==
                            std::optional<PackageRelationSourceIdentity>(
                                    extra) &&
                    !package_relation_has_complete_identity_coverage(
                            exact_match_with_failure) &&
                    !package_relation_confirms_no_match(
                            exact_match_with_failure),
            "Exact match did not coexist with provider source failure");

    const RepositoryExactPackageObservationResult partial_exact =
            RepositoryExactPackageObservation{
                    order,
                    {RepositoryExactPackageAbsent{core, "virtual-api"},
                     RepositoryExactPackageSourceFailure{
                             extra,
                             "virtual-api",
                             repository_relation_query_failure(
                                     "extra exact query failed")}}};
    const PackageRelationMatchingEvidence provider_match_with_failure =
            match_declared_package_relation(
                    virtual_relation,
                    project_repository_relation_observations(
                            partial_exact, virtual_provider_present));
    expect(
            package_relation_has_confirmed_match(
                    provider_match_with_failure) &&
                    provider_match_with_failure.observation_failures.size() ==
                            1 &&
                    provider_match_with_failure.observation_failures.front()
                                    .source ==
                            std::optional<PackageRelationSourceIdentity>(
                                    extra) &&
                    !package_relation_has_complete_identity_coverage(
                            provider_match_with_failure) &&
                    !package_relation_confirms_no_match(
                            provider_match_with_failure),
            "Provider match did not coexist with exact source failure");
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
        test_installed_relation_inventory_retains_versions_and_provides();
        test_installed_inventory_empty_and_failures_are_distinct();
        test_installed_inventory_rejects_invalid_identity_and_provides();
        test_repository_and_aur_observation_adapters_retain_source();
        test_repository_relation_composite_requires_both_identity_channels();
    } catch(const std::exception& error) {
        std::cerr << "package constraint metadata test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
