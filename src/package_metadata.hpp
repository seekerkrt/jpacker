#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

struct PacmanDatabasePaths {
    std::filesystem::path root_dir;
    std::filesystem::path db_path;
};

enum class InstalledPackageReason {
    Explicit,
    Dependency,
    Unknown,
};

struct InstalledPackageMetadata {
    std::string            name;
    std::string            version;
    InstalledPackageReason reason = InstalledPackageReason::Unknown;
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
};

struct PackageMetadataFailure {
    PackageMetadataErrorCode code;
    std::string              diagnostic;
};

using InstalledPackageQueryResult = std::variant<
        InstalledPackageMetadata,
        PackageNotFound,
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

private:
    struct Impl;

    explicit PackageMetadataSession(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
