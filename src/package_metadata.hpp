#pragma once

#include "installed_package.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

struct PacmanDatabasePaths {
    std::filesystem::path root_dir;
    std::filesystem::path db_path;
};

struct PacmanRepositoryConfiguration {
    PacmanDatabasePaths      database_paths;
    std::vector<std::string> repository_names;
};

struct RepositoryPackageLookup {
    std::string                package_name;
    std::optional<std::string> exact_repository_name;
};

struct RepositoryPackageMetadata {
    std::string   repository_name;
    std::string   package_name;
    std::uint64_t package_size_bytes;
    std::uint64_t installed_size_bytes;
};

struct PackageNotFound {};

enum class PackageMetadataErrorCode {
    ConfigurationUnavailable,
    ConfigurationMalformed,
    InitializationFailed,
    LocalDatabaseUnavailable,
    InvalidPackageName,
    QueryFailed,
    MalformedMetadata,
    SyncDatabaseUnavailable,
    RepositoryNotConfigured,
};

struct PackageMetadataFailure {
    PackageMetadataErrorCode code;
    std::string              diagnostic;
};

using InstalledPackageQueryResult = std::variant<
        InstalledPackageMetadata,
        PackageNotFound,
        PackageMetadataFailure>;

using RepositoryPackageQueryResult = std::variant<
        RepositoryPackageMetadata,
        PackageNotFound,
        PackageMetadataFailure>;

using ForeignPackageInventory = std::vector<InstalledPackageMetadata>;

using ForeignPackageInventoryResult = std::variant<
        ForeignPackageInventory,
        PackageMetadataFailure>;

// package state比較用に、libalpmのborrowを残さないname/version snapshotを保持する。
using LocalPackageVersionSnapshot = std::map<std::string, std::string>;

using LocalPackageVersionSnapshotResult = std::variant<
        LocalPackageVersionSnapshot,
        PackageMetadataFailure>;

// resolver/session openの失敗を、CLI境界でstd::exceptionとして扱える形で伝播する。
class PackageMetadataError : public std::runtime_error {
public:
    explicit PackageMetadataError(PackageMetadataFailure failure);

    const PackageMetadataFailure& failure() const noexcept;

private:
    PackageMetadataFailure failure_;
};

PacmanDatabasePaths resolve_pacman_database_paths();
PacmanRepositoryConfiguration resolve_pacman_repository_configuration();

// local DBと全sync DBを1 handleで照合し、borrowを残さないowned inventoryを返す。
ForeignPackageInventoryResult query_foreign_package_inventory(
        const PacmanRepositoryConfiguration& configuration);

// 1 read phaseのlibalpm handleを単独所有し、raw libalpm型をdomain側へ公開しない。
class PackageMetadataSession {
public:
    static PackageMetadataSession open(const PacmanDatabasePaths& paths);

    PackageMetadataSession(const PackageMetadataSession&) = delete;
    PackageMetadataSession& operator=(const PackageMetadataSession&) = delete;

    PackageMetadataSession(PackageMetadataSession&&) noexcept;
    PackageMetadataSession& operator=(PackageMetadataSession&&) noexcept;

    ~PackageMetadataSession() noexcept;

    InstalledPackageQueryResult query_installed_package(
            const std::string& package_name) const;

    LocalPackageVersionSnapshotResult snapshot_local_package_versions() const;

private:
    struct Impl;

    explicit PackageMetadataSession(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

// repository sync DB専用のread phaseを所有し、installed/local sessionとは分離する。
class RepositoryPackageMetadataSession {
public:
    static RepositoryPackageMetadataSession open(
            const PacmanRepositoryConfiguration& configuration);

    RepositoryPackageMetadataSession(const RepositoryPackageMetadataSession&) = delete;
    RepositoryPackageMetadataSession& operator=(
            const RepositoryPackageMetadataSession&) = delete;

    RepositoryPackageMetadataSession(RepositoryPackageMetadataSession&&) noexcept;
    RepositoryPackageMetadataSession& operator=(
            RepositoryPackageMetadataSession&&) noexcept;

    ~RepositoryPackageMetadataSession() noexcept;

    RepositoryPackageQueryResult query_repository_package(
            const RepositoryPackageLookup& lookup) const;

private:
    struct Impl;

    explicit RepositoryPackageMetadataSession(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
