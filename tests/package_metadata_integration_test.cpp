#include "package_metadata.hpp"

#ifdef ALPM_H
#error "package_metadata.hpp must not expose or include raw libalpm types"
#endif

#include "process.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

    InstalledPackageStateSnapshotResult snapshot_result =
            session.snapshot_installed_package_states();
    if(const auto* failure =
               std::get_if<PackageMetadataFailure>(&snapshot_result)) {
        throw std::runtime_error(
                "installed package state snapshot failed: " +
                failure->diagnostic);
    }
    const InstalledPackageStateSnapshot& snapshot =
            std::get<InstalledPackageStateSnapshot>(snapshot_result);
    const auto pacman = snapshot.find("pacman");
    expect(
            pacman != snapshot.end(),
            "installed package state snapshot omitted pacman");
    expect(
            pacman->second.name == metadata.name &&
                    pacman->second.version == metadata.version &&
                    pacman->second.reason == metadata.reason,
            "installed package state snapshot differs from exact metadata");
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
                    "moguet-issue-125-package-that-does-not-exist",
                    std::nullopt});
    if(const auto* failure = std::get_if<PackageMetadataFailure>(&missing_result)) {
        throw std::runtime_error(
                "missing repository package query failed: " + failure->diagnostic);
    }
    expect(
            std::holds_alternative<PackageNotFound>(missing_result),
            "missing repository package was not reported as not found");
}

struct PacmanForeignPackage {
    std::string name;
    std::string version;
};

std::vector<PacmanForeignPackage> query_pacman_foreign_packages() {
    CapturedCommandResult result =
            capture_command_output_raw("pacman -Qm 2>/dev/null");
    // pacman reports a valid query with no foreign package matches as exit 1
    // and empty output. The corresponding libalpm inventory is an empty vector.
    const bool is_empty_inventory = result.exit_code == 1 && result.output.empty();
    if(result.exit_code != 0 && !is_empty_inventory) {
        throw std::runtime_error(
                "pacman -Qm failed with exit code " +
                std::to_string(result.exit_code));
    }

    std::vector<PacmanForeignPackage> packages;
    std::stringstream                 output_stream(result.output);
    std::string                       line;
    while(std::getline(output_stream, line)) {
        if(line.empty()) {
            throw std::runtime_error("pacman -Qm returned an empty output line");
        }

        std::stringstream line_stream(line);
        PacmanForeignPackage package;
        std::string          unexpected_field;
        if(!(line_stream >> package.name >> package.version) ||
           (line_stream >> unexpected_field)) {
            throw std::runtime_error("pacman -Qm returned a malformed output line");
        }
        packages.push_back(std::move(package));
    }
    return packages;
}

void run_foreign_package_inventory_compatibility_test() {
    std::vector<PacmanForeignPackage> pacman_packages =
            query_pacman_foreign_packages();
    PacmanRepositoryConfiguration configuration =
            resolve_pacman_repository_configuration();
    ForeignPackageInventoryResult result =
            query_foreign_package_inventory(configuration);
    if(const auto* failure = std::get_if<PackageMetadataFailure>(&result)) {
        throw std::runtime_error(
                "foreign package inventory failed: " + failure->diagnostic);
    }

    const ForeignPackageInventory& inventory =
            std::get<ForeignPackageInventory>(result);
    expect(
            inventory.size() == pacman_packages.size(),
            "foreign package inventory count differs from pacman -Qm");
    for(std::size_t index = 0; index < inventory.size(); ++index) {
        expect(
                inventory[index].name == pacman_packages[index].name,
                "foreign package inventory name/order differs from pacman -Qm");
        expect(
                inventory[index].version == pacman_packages[index].version,
                "foreign package inventory version differs from pacman -Qm");
    }
}

} // namespace

int main() {
    try {
        test_raw_capture_preserves_boundary_whitespace();
        run_pacman_metadata_smoke_test();
        run_repository_metadata_smoke_test();
        run_foreign_package_inventory_compatibility_test();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "package metadata integration test: all checks passed\n";
    return 0;
}
