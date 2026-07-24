#include "package_metadata.hpp"

#ifdef ALPM_H
#error "package_metadata.hpp must not expose or include raw libalpm types"
#endif

#include "process.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void test_raw_capture_preserves_boundary_whitespace() {
    const std::string expected =
            "\nRootDir = /\nDBPath = /var/lib/pacman/\n\n";
    CapturedCommandResult result = capture_command_output_raw(
            "printf '\\nRootDir = /\\nDBPath = /var/lib/pacman/\\n\\n'");

    expect(result.exit_code == 0, "raw capture command failed");
    expect(result.output == expected, "raw capture changed boundary whitespace");
}

void run_pacman_metadata_smoke_test() {
    PacmanDatabasePaths paths = resolve_pacman_database_paths();
    PackageMetadataSession session = PackageMetadataSession::open(paths);

    InstalledPackageQueryResult result = session.query_installed_package("pacman");
    if(const auto* failure = std::get_if<PackageMetadataFailure>(&result)) {
        throw std::runtime_error("pacman metadata query failed: " + failure->diagnostic);
    }
    if(std::holds_alternative<PackageNotFound>(result)) {
        throw std::runtime_error("installed pacman package was not found");
    }

    const InstalledPackageMetadata& metadata = std::get<InstalledPackageMetadata>(result);
    expect(metadata.name == "pacman", "pacman metadata returned a different package name");
    expect(!metadata.version.empty(), "pacman metadata returned an empty version");
    expect(
            metadata.reason == InstalledPackageReason::Explicit ||
                    metadata.reason == InstalledPackageReason::Dependency ||
                    metadata.reason == InstalledPackageReason::Unknown,
            "pacman metadata returned an unknown public install reason");
}

void run_repository_metadata_smoke_test() {
    PacmanRepositoryConfiguration configuration =
            resolve_pacman_repository_configuration();
    expect(
            !configuration.repository_names.empty(),
            "pacman configuration did not return any repositories");

    RepositoryPackageMetadataSession session =
            RepositoryPackageMetadataSession::open(configuration);
    RepositoryPackageQueryResult pacman_result = session.query_repository_package(
            RepositoryPackageLookup{"pacman", std::nullopt});
    if(const auto* failure = std::get_if<PackageMetadataFailure>(&pacman_result)) {
        throw std::runtime_error(
                "repository pacman metadata query failed: " + failure->diagnostic);
    }
    if(std::holds_alternative<PackageNotFound>(pacman_result)) {
        throw std::runtime_error("repository pacman package was not found");
    }

    const RepositoryPackageMetadata& metadata =
            std::get<RepositoryPackageMetadata>(pacman_result);
    expect(metadata.package_name == "pacman", "repository query returned a different package");
    expect(
            std::find(
                    configuration.repository_names.begin(),
                    configuration.repository_names.end(),
                    metadata.repository_name) != configuration.repository_names.end(),
            "repository query returned an unconfigured repository");

    RepositoryPackageQueryResult missing_result = session.query_repository_package(
            RepositoryPackageLookup{
                    "jpacker-issue-125-package-that-does-not-exist",
                    std::nullopt});
    if(const auto* failure = std::get_if<PackageMetadataFailure>(&missing_result)) {
        throw std::runtime_error(
                "missing repository package query failed: " + failure->diagnostic);
    }
    expect(
            std::holds_alternative<PackageNotFound>(missing_result),
            "missing repository package was not reported as not found");
}

} // namespace

int main() {
    try {
        test_raw_capture_preserves_boundary_whitespace();
        run_pacman_metadata_smoke_test();
        run_repository_metadata_smoke_test();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "package metadata integration test: all checks passed\n";
    return 0;
}
