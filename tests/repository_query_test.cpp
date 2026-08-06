#include "repository_query.hpp"

#include "dependency_provider.hpp"
#include "shell_words.hpp"
#include "stubs/repository-query/process_stub.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace stub = repository_query_test_stub;

constexpr const char* DATABASE_PATH_COMMAND =
        "pacman-conf --verbose RootDir DBPath 2>/dev/null";
constexpr const char* REPOSITORY_LIST_COMMAND =
        "pacman-conf --repo-list 2>/dev/null";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Alternative, typename Result>
Alternative require_alternative(
        const Result& result, const std::string& context) {
    const Alternative* alternative = std::get_if<Alternative>(&result);
    if(alternative == nullptr)
        throw std::runtime_error(context + ": unexpected result alternative");
    return *alternative;
}

class TestDatabase final {
public:
    explicit TestDatabase(bool create_sync_directory = true) {
        std::string template_text =
                (fs::temp_directory_path() / "moguet-repository-query-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr)
            throw std::runtime_error(
                    "Failed to create repository query test directory.");

        root_ = created_path;
        database_path_ = root_ / "pacman";
        sync_path_ = database_path_ / "sync";
        if(create_sync_directory)
            fs::create_directories(sync_path_);
        else
            fs::create_directories(database_path_);
    }

    TestDatabase(const TestDatabase&) = delete;
    TestDatabase& operator=(const TestDatabase&) = delete;

    ~TestDatabase() noexcept {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    const fs::path& database_path() const noexcept {
        return database_path_;
    }

    fs::path repository_path(const std::string& repository_name) const {
        return sync_path_ / (repository_name + ".db");
    }

    void add_repository_file(const std::string& repository_name) const {
        fs::path path = repository_path(repository_name);
        std::ofstream file(path);
        if(!file) {
            throw std::runtime_error(
                    "Failed to create repository query fixture: " + path.string());
        }
        file << "fixture";
    }

    void add_repository_directory(const std::string& repository_name) const {
        fs::create_directory(repository_path(repository_name));
    }

private:
    fs::path root_;
    fs::path database_path_;
    fs::path sync_path_;
};

std::string repository_list_output(
        const std::vector<std::string>& repository_names) {
    std::string output;
    for(const auto& repository_name : repository_names)
        output += repository_name + "\n";
    return output;
}

void enqueue_configuration(
        const TestDatabase& database,
        const std::vector<std::string>& repository_names) {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{
                    "RootDir = /\nDBPath = " + database.database_path().string() +
                            "\n",
                    0});
    stub::enqueue_captured_command_result(
            REPOSITORY_LIST_COMMAND,
            CapturedCommandResult{
                    repository_list_output(repository_names), 0});
}

std::string database_read_command(
        const TestDatabase& database, const std::string& repository_name) {
    return "bsdtar -xOf " +
           shell_words::quote(database.repository_path(repository_name).string()) +
           " '*/desc' 2>/dev/null";
}

void enqueue_database_read(
        const TestDatabase& database, const std::string& repository_name,
        std::string output, int exit_code = 0) {
    stub::enqueue_captured_command_result(
            database_read_command(database, repository_name),
            CapturedCommandResult{std::move(output), exit_code});
}

std::string package_description(
        const std::string& package_name,
        const std::vector<std::string>& provides = {},
        const std::string& package_version = "1.0-1") {
    std::string description =
            "%FILENAME%\n" + package_name + "-1-1-x86_64.pkg.tar.zst\n\n" +
            "%NAME%\n" + package_name + "\n\n" +
            "%VERSION%\n" + package_version + "\n\n";
    if(!provides.empty()) {
        description += "%PROVIDES%\n";
        for(const auto& provided : provides) description += provided + "\n";
        description += "\n";
    }
    return description;
}

void expect_commands(
        const std::vector<std::string>& expected,
        const std::string& context) {
    std::vector<std::string> actual = stub::captured_commands();
    expect(actual.size() == expected.size(), context + ": command count differs");
    for(std::size_t index = 0; index < expected.size(); ++index) {
        expect(
                actual[index] == expected[index],
                context + ": command differs at index " +
                        std::to_string(index));
    }
}

template <typename Result>
RepositoryMetadataFailure require_failure(
        const Result& result, RepositoryMetadataFailureKind expected_kind,
        const std::string& context) {
    RepositoryMetadataFailure failure =
            require_alternative<RepositoryMetadataFailure>(result, context);
    expect(failure.kind == expected_kind, context + ": failure kind differs");
    expect(!failure.diagnostic.empty(), context + ": empty diagnostic");
    return failure;
}

const RepositoryProviderOrigin& require_repository_provider_origin(
        const ProvidedDependency& provider, const std::string& context) {
    const auto* origin =
            std::get_if<RepositoryProviderOrigin>(&provider.origin);
    if(origin == nullptr)
        throw std::runtime_error(context + ": provider is not repository-origin");
    return *origin;
}

void test_candidate_value_contract() {
    const ProvidedDependency legacy_repository =
            ProvidedDependency::from_repository("aur", "same-package");
    const ProvidedDependency legacy_aur =
            ProvidedDependency::from_aur("same-package");
    expect(
            legacy_repository.package_base.empty() &&
                    legacy_repository.provided_dependency_name.empty() &&
                    legacy_repository.provided_dependency_specification.empty() &&
                    !legacy_repository.package_version.has_value(),
            "legacy repository factory metadata defaults differ");
    expect(
            legacy_aur.package_base == "same-package" &&
                    legacy_aur.provided_dependency_name.empty() &&
                    legacy_aur.provided_dependency_specification.empty() &&
                    !legacy_aur.package_version.has_value(),
            "legacy AUR factory metadata defaults differ");

    const ProvidedDependency repository =
            ProvidedDependency::from_repository(
                    "aur", "same-package", "virtual-api",
                    "virtual-api=2", std::string("2.3-1"));
    const ProvidedDependency same_repository_identity =
            ProvidedDependency::from_repository(
                    "aur", "same-package", "other-api",
                    "other-api>=1", std::string("2.4-1"));
    const ProvidedDependency aur = ProvidedDependency::from_aur(
            "same-package", "split-base", "virtual-api",
            "virtual-api=2", std::string("2.3-1"));
    const ProvidedDependency other_aur_base = ProvidedDependency::from_aur(
            "same-package", "other-base", "virtual-api",
            "virtual-api=2", std::string("2.3-1"));

    expect(
            repository.package_base.empty() &&
                    repository.provided_dependency_name == "virtual-api" &&
                    repository.provided_dependency_specification ==
                            "virtual-api=2" &&
                    repository.package_version ==
                            std::optional<std::string>("2.3-1"),
            "full repository factory lost owned metadata");
    expect(
            aur.package_base == "split-base" &&
                    aur.provided_dependency_name == "virtual-api" &&
                    aur.provided_dependency_specification == "virtual-api=2" &&
                    aur.package_version ==
                            std::optional<std::string>("2.3-1"),
            "full AUR factory lost owned metadata");
    expect(
            same_provider_identity(repository, same_repository_identity) &&
                    repository != same_repository_identity,
            "repository semantic identity depends on presentation metadata");
    expect(
            !same_provider_identity(aur, other_aur_base),
            "AUR semantic identity lost PackageBase");
    expect(
            !same_provider_identity(repository, aur) &&
                    provided_dependency_display(repository) ==
                            provided_dependency_display(legacy_aur),
            "typed provider identity was inferred from presentation text");
}

void test_success_snapshot_and_queries() {
    TestDatabase database;
    database.add_repository_file("core");
    database.add_repository_file("extra");
    database.add_repository_file("stale");
    enqueue_configuration(database, {"core", "extra"});
    enqueue_database_read(
            database, "core",
            package_description(
                    "core-provider", {"virtual-api=2", "virtual-api=2"},
                    "2.4-1") +
                    package_description("shared-package"));
    enqueue_database_read(
            database, "extra",
            package_description(
                    "extra-provider", {"virtual-api"}, "3.1-2") +
                    package_description("shared-package"));

    StrictRepositoryPackageQueryResult shared_result =
            query_repository_package_strict("shared-package");
    RepositoryPackagePresent shared =
            require_alternative<RepositoryPackagePresent>(
                    shared_result, "configured-order exact query");
    expect(
            shared.repository_name == "core",
            "exact query did not preserve configured repository precedence");

    StrictRepositoryPackageQueryResult extra_result =
            query_repository_package_strict("extra-provider");
    expect(
            require_alternative<RepositoryPackagePresent>(
                    extra_result, "extra exact query")
                            .repository_name == "extra",
            "extra package provenance differs");

    StrictRepositoryPackageQueryResult missing_result =
            query_repository_package_strict("missing-package");
    static_cast<void>(require_alternative<RepositoryPackageNotFound>(
            missing_result, "confirmed exact absence"));

    StrictRepositoryProvidersQueryResult provider_result =
            query_repository_providers_strict("virtual-api");
    std::vector<ProvidedDependency> providers =
            require_alternative<std::vector<ProvidedDependency>>(
                    provider_result, "provider query");
    expect(providers.size() == 2, "provider query did not deduplicate candidates");
    expect(
            require_repository_provider_origin(
                    providers[0], "first provider")
                            .repository_name == "core" &&
                    providers[0] == ProvidedDependency::from_repository(
                                            "core", "core-provider",
                                            "virtual-api", "virtual-api=2",
                                            std::string("2.4-1")),
            "first provider does not preserve configured order");
    expect(
            require_repository_provider_origin(
                    providers[1], "second provider")
                            .repository_name == "extra" &&
                    providers[1] == ProvidedDependency::from_repository(
                                            "extra", "extra-provider",
                                            "virtual-api", "virtual-api",
                                            std::string("3.1-2")),
            "second provider does not preserve configured order");

    StrictRepositoryProvidersQueryResult missing_provider_result =
            query_repository_providers_strict("missing-virtual");
    expect(
            require_alternative<std::vector<ProvidedDependency>>(
                    missing_provider_result, "confirmed provider absence")
                    .empty(),
            "missing provider query is not empty");

    expect_commands(
            {
                    DATABASE_PATH_COMMAND,
                    REPOSITORY_LIST_COMMAND,
                    database_read_command(database, "core"),
                    database_read_command(database, "extra"),
            },
            "shared success snapshot");
}

void test_repository_named_aur() {
    TestDatabase database;
    database.add_repository_file("aur");
    enqueue_configuration(database, {"aur"});
    enqueue_database_read(
            database, "aur",
            package_description(
                    "repository-provider",
                    {"virtual-api", "virtual-api"}));

    StrictRepositoryProvidersQueryResult provider_result =
            query_repository_providers_strict("virtual-api");
    std::vector<ProvidedDependency> providers =
            require_alternative<std::vector<ProvidedDependency>>(
                    provider_result, "repository named aur provider query");
    expect(
            providers.size() == 1,
            "repository named aur provider was not first-win deduplicated");
    expect(
            require_repository_provider_origin(
                    providers.front(), "repository named aur provider")
                            .repository_name == "aur",
            "repository named aur lost its exact origin name");
    expect(
            providers.front().package_name == "repository-provider",
            "repository named aur provider package differs");
    expect(
            providers.front() == ProvidedDependency::from_repository(
                                         "aur", "repository-provider",
                                         "virtual-api", "virtual-api",
                                         std::string("1.0-1")),
            "repository named aur provider metadata differs");
    expect(
            !same_provider_identity(
                    providers.front(),
                    ProvidedDependency::from_aur("repository-provider")),
            "repository named aur was conflated with AUR origin");

    expect_commands(
            {
                    DATABASE_PATH_COMMAND,
                    REPOSITORY_LIST_COMMAND,
                    database_read_command(database, "aur"),
            },
            "repository named aur snapshot");
}

void test_legacy_malformed_candidates_are_skipped() {
    const std::string description =
            package_description("", {"leaked-alias"}) +
            package_description("invalid/provider", {"virtual-api"}) +
            package_description(
                    "valid-provider", {"virtual-api", "virtual-api"}) +
            package_description(
                    "malformed-alias-provider", {"virtual-api="}) +
            package_description(
                    "invalid-alias-provider", {"virtual/api"}) +
            package_description(
                    "invalid-version-provider", {"virtual-api"},
                    "1.0\tinvalid");

    std::vector<ProvidedDependency> providers =
            parse_legacy_repository_provider_candidates_for_test(
                    description, "core", "virtual-api");
    expect(
            providers == std::vector<ProvidedDependency>{
                    ProvidedDependency::from_repository(
                            "core", "valid-provider", "virtual-api",
                            "virtual-api", std::string("1.0-1"))},
            "legacy parser did not skip malformed candidates locally");
    expect(
            parse_legacy_repository_provider_candidates_for_test(
                    description, "core", "leaked-alias")
                    .empty(),
            "legacy parser carried malformed record aliases into the next package");
    expect(
            parse_legacy_repository_provider_candidates_for_test(
                    description, "aur", "virtual-api") ==
                    std::vector<ProvidedDependency>{
                            ProvidedDependency::from_repository(
                                    "aur", "valid-provider", "virtual-api",
                                    "virtual-api", std::string("1.0-1"))},
            "legacy parser conflated repository name aur with AUR origin");
    expect(
            parse_legacy_repository_provider_candidates_for_test(
                    description, "", "virtual-api")
                    .empty(),
            "legacy parser accepted an empty repository name");
    expect(
            parse_legacy_repository_provider_candidates_for_test(
                    description, "co\nre", "virtual-api")
                    .empty(),
            "legacy parser accepted a control-character repository name");
}

void test_configuration_command_failure() {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{"ignored", 127});

    StrictRepositoryPackageQueryResult package_result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure package_failure = require_failure(
            package_result,
            RepositoryMetadataFailureKind::ConfigurationUnavailable,
            "configuration command failure");

    StrictRepositoryProvidersQueryResult provider_result =
            query_repository_providers_strict("virtual-name");
    RepositoryMetadataFailure provider_failure = require_failure(
            provider_result,
            RepositoryMetadataFailureKind::ConfigurationUnavailable,
            "cached configuration command failure");
    expect(
            package_failure == provider_failure,
            "strict queries did not share the owned failure snapshot");
    expect_commands({DATABASE_PATH_COMMAND}, "configuration command failure");
}

void test_configuration_parse_failure() {
    stub::enqueue_captured_command_result(
            DATABASE_PATH_COMMAND,
            CapturedCommandResult{"RootDir = /\n", 0});

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    static_cast<void>(require_failure(
            result,
            RepositoryMetadataFailureKind::ConfigurationMalformed,
            "configuration parse failure"));
    expect_commands({DATABASE_PATH_COMMAND}, "configuration parse failure");
}

void test_unsafe_repository_name() {
    TestDatabase database;
    enqueue_configuration(database, {"../escape"});

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::ConfigurationMalformed,
            "unsafe repository name");
    expect(
            failure.repository_name == std::optional<std::string>("../escape"),
            "unsafe repository failure lost repository identity");
    expect_commands(
            {DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND},
            "unsafe repository name");
}

void test_missing_sync_directory_with_no_repositories() {
    TestDatabase database(false);
    enqueue_configuration(database, {});

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "missing sync directory");
    expect(
            !failure.repository_name.has_value(),
            "sync directory failure unexpectedly has repository identity");
    expect_commands(
            {DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND},
            "missing sync directory");
}

void test_empty_repository_configuration() {
    TestDatabase database;
    enqueue_configuration(database, {});

    StrictRepositoryPackageQueryResult package_result =
            query_repository_package_strict("package-name");
    static_cast<void>(require_alternative<RepositoryPackageNotFound>(
            package_result, "empty repository exact query"));
    StrictRepositoryProvidersQueryResult provider_result =
            query_repository_providers_strict("virtual-name");
    expect(
            require_alternative<std::vector<ProvidedDependency>>(
                    provider_result, "empty repository provider query")
                    .empty(),
            "empty repository provider query is not empty");
    expect_commands(
            {DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND},
            "empty repository configuration");
}

void test_missing_configured_database() {
    TestDatabase database;
    enqueue_configuration(database, {"core"});

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "missing configured database");
    expect(
            failure.repository_name == std::optional<std::string>("core"),
            "missing database failure lost repository identity");
    expect_commands(
            {DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND},
            "missing configured database");
}

void test_non_regular_configured_database() {
    TestDatabase database;
    database.add_repository_directory("core");
    enqueue_configuration(database, {"core"});

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "non-regular configured database");
    expect(
            failure.repository_name == std::optional<std::string>("core"),
            "non-regular database failure lost repository identity");
    expect_commands(
            {DATABASE_PATH_COMMAND, REPOSITORY_LIST_COMMAND},
            "non-regular configured database");
}

void test_database_read_failure() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(database, "core", "ignored", 2);

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "database read failure");
    expect(
            failure.repository_name == std::optional<std::string>("core"),
            "database read failure lost repository identity");
    expect_commands(
            {
                    DATABASE_PATH_COMMAND,
                    REPOSITORY_LIST_COMMAND,
                    database_read_command(database, "core"),
            },
            "database read failure");
}

void test_empty_database_is_valid() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(database, "core", "\n  \n", 0);

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    static_cast<void>(require_alternative<RepositoryPackageNotFound>(
            result, "empty database exact query"));
    expect_commands(
            {
                    DATABASE_PATH_COMMAND,
                    REPOSITORY_LIST_COMMAND,
                    database_read_command(database, "core"),
            },
            "empty database");
}

void test_malformed_database_metadata() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(database, "core", "%NAME%\npackage-name\n", 0);

    StrictRepositoryPackageQueryResult result =
            query_repository_package_strict("package-name");
    RepositoryMetadataFailure failure = require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            "malformed database metadata");
    expect(
            failure.repository_name == std::optional<std::string>("core"),
            "malformed database failure lost repository identity");
}

void test_invalid_provided_dependency() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(
            database, "core",
            package_description("provider-package", {"virtual-api="}), 0);

    StrictRepositoryProvidersQueryResult result =
            query_repository_providers_strict("virtual-api");
    static_cast<void>(require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            "invalid provided dependency"));
}

void test_missing_package_version() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(
            database, "core",
            "%FILENAME%\nprovider-package-1-1-x86_64.pkg.tar.zst\n\n"
            "%NAME%\nprovider-package\n\n"
            "%PROVIDES%\nvirtual-api\n\n",
            0);

    StrictRepositoryProvidersQueryResult result =
            query_repository_providers_strict("virtual-api");
    static_cast<void>(require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            "missing package version"));
}

void test_multiple_package_versions() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(
            database, "core",
            "%FILENAME%\nprovider-package-1-1-x86_64.pkg.tar.zst\n\n"
            "%NAME%\nprovider-package\n\n"
            "%VERSION%\n1.0-1\n2.0-1\n\n"
            "%PROVIDES%\nvirtual-api\n\n",
            0);

    StrictRepositoryProvidersQueryResult result =
            query_repository_providers_strict("virtual-api");
    static_cast<void>(require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            "multiple package versions"));
}

void test_invalid_package_version() {
    TestDatabase database;
    database.add_repository_file("core");
    enqueue_configuration(database, {"core"});
    enqueue_database_read(
            database, "core",
            package_description(
                    "provider-package", {"virtual-api"},
                    "1.0\tinvalid"),
            0);

    StrictRepositoryProvidersQueryResult result =
            query_repository_providers_strict("virtual-api");
    static_cast<void>(require_failure(
            result,
            RepositoryMetadataFailureKind::SyncDatabaseMalformed,
            "invalid package version"));
}

void test_partial_snapshot_is_not_published() {
    TestDatabase database;
    database.add_repository_file("core");
    database.add_repository_file("extra");
    enqueue_configuration(database, {"core", "extra"});
    enqueue_database_read(
            database, "core",
            package_description("core-package", {"virtual-api"}), 0);
    enqueue_database_read(database, "extra", "ignored", 2);

    StrictRepositoryPackageQueryResult package_result =
            query_repository_package_strict("core-package");
    RepositoryMetadataFailure package_failure = require_failure(
            package_result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "partial exact snapshot");
    StrictRepositoryProvidersQueryResult provider_result =
            query_repository_providers_strict("virtual-api");
    RepositoryMetadataFailure provider_failure = require_failure(
            provider_result,
            RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
            "partial provider snapshot");
    expect(
            package_failure == provider_failure,
            "partial snapshot failure differs between strict queries");
    expect_commands(
            {
                    DATABASE_PATH_COMMAND,
                    REPOSITORY_LIST_COMMAND,
                    database_read_command(database, "core"),
                    database_read_command(database, "extra"),
            },
            "partial snapshot failure");
}

void run_test_case(const std::string& test_case) {
    if(test_case == "candidate-value-contract")
        test_candidate_value_contract();
    else if(test_case == "success")
        test_success_snapshot_and_queries();
    else if(test_case == "repository-named-aur")
        test_repository_named_aur();
    else if(test_case == "legacy-malformed-candidates")
        test_legacy_malformed_candidates_are_skipped();
    else if(test_case == "configuration-command-failure")
        test_configuration_command_failure();
    else if(test_case == "configuration-parse-failure")
        test_configuration_parse_failure();
    else if(test_case == "unsafe-repository-name")
        test_unsafe_repository_name();
    else if(test_case == "missing-sync-directory")
        test_missing_sync_directory_with_no_repositories();
    else if(test_case == "empty-repository-configuration")
        test_empty_repository_configuration();
    else if(test_case == "missing-configured-database")
        test_missing_configured_database();
    else if(test_case == "non-regular-configured-database")
        test_non_regular_configured_database();
    else if(test_case == "database-read-failure")
        test_database_read_failure();
    else if(test_case == "empty-database")
        test_empty_database_is_valid();
    else if(test_case == "malformed-database")
        test_malformed_database_metadata();
    else if(test_case == "invalid-provided-dependency")
        test_invalid_provided_dependency();
    else if(test_case == "missing-package-version")
        test_missing_package_version();
    else if(test_case == "multiple-package-versions")
        test_multiple_package_versions();
    else if(test_case == "invalid-package-version")
        test_invalid_package_version();
    else if(test_case == "partial-snapshot")
        test_partial_snapshot_is_not_published();
    else
        throw std::runtime_error("Unknown repository query test case: " + test_case);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if(argc != 2)
            throw std::runtime_error(
                    "repository query test requires exactly one case name");
        run_test_case(argv[1]);
        std::cout << "repository query test passed: " << argv[1] << "\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "repository query test failed: " << error.what() << "\n";
        return 1;
    }
}
