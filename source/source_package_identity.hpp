#pragma once

#include <optional>
#include <string>
#include <vector>

enum class IdentityUnavailableReason {
    AuthorityUnavailable,
    ObservationFailed,
    InvalidObservation,
};

enum class SourceLocationKind {
    GitRemote,
    LocalPath,
};

enum class SourceLocationState {
    Known,
    Unknown,
    Unavailable,
};

class SourceLocationIdentity final {
public:
    SourceLocationIdentity() = delete;
    SourceLocationIdentity(const SourceLocationIdentity&) = default;
    SourceLocationIdentity(SourceLocationIdentity&&) noexcept = default;
    SourceLocationIdentity& operator=(const SourceLocationIdentity&) = default;
    SourceLocationIdentity& operator=(SourceLocationIdentity&&) noexcept =
            default;
    ~SourceLocationIdentity() = default;

    [[nodiscard]] static SourceLocationIdentity known_git_remote(
            std::string location);
    [[nodiscard]] static SourceLocationIdentity known_local_path(
            std::string location);
    [[nodiscard]] static SourceLocationIdentity unknown(
            SourceLocationKind kind);
    [[nodiscard]] static SourceLocationIdentity unavailable(
            SourceLocationKind kind, IdentityUnavailableReason reason);

    [[nodiscard]] SourceLocationKind kind() const noexcept;
    [[nodiscard]] SourceLocationState state() const noexcept;
    [[nodiscard]] const std::string* value() const noexcept;
    [[nodiscard]] const IdentityUnavailableReason* unavailable_reason()
            const noexcept;

    bool operator==(const SourceLocationIdentity&) const = default;

private:
    SourceLocationIdentity(
            SourceLocationKind kind, SourceLocationState state,
            std::optional<std::string> value,
            std::optional<IdentityUnavailableReason> unavailable_reason)
        noexcept;

    SourceLocationKind                       kind_;
    SourceLocationState                      state_;
    std::optional<std::string>                value_;
    std::optional<IdentityUnavailableReason> unavailable_reason_;
};

enum class PackageSourceKind {
    Repository,
    Aur,
    Local,
};

class PackageSourceIdentity final {
public:
    PackageSourceIdentity() = delete;
    PackageSourceIdentity(const PackageSourceIdentity&) = default;
    PackageSourceIdentity(PackageSourceIdentity&&) noexcept = default;
    PackageSourceIdentity& operator=(const PackageSourceIdentity&) = default;
    PackageSourceIdentity& operator=(PackageSourceIdentity&&) noexcept =
            default;
    ~PackageSourceIdentity() = default;

    [[nodiscard]] static PackageSourceIdentity repository(
            std::string repository_name, SourceLocationIdentity location);
    [[nodiscard]] static PackageSourceIdentity aur(
            SourceLocationIdentity location);
    [[nodiscard]] static PackageSourceIdentity local(
            SourceLocationIdentity location);

    [[nodiscard]] PackageSourceKind kind() const noexcept;
    [[nodiscard]] const std::string* repository_name() const noexcept;
    [[nodiscard]] const SourceLocationIdentity& location() const noexcept;

    bool operator==(const PackageSourceIdentity&) const = default;

private:
    PackageSourceIdentity(
            PackageSourceKind kind,
            std::optional<std::string> repository_name,
            SourceLocationIdentity location) noexcept;

    PackageSourceKind                 kind_;
    std::optional<std::string>        repository_name_;
    SourceLocationIdentity            location_;
};

class PackageBaseIdentity final {
public:
    PackageBaseIdentity() = delete;
    PackageBaseIdentity(const PackageBaseIdentity&) = default;
    PackageBaseIdentity(PackageBaseIdentity&&) noexcept = default;
    PackageBaseIdentity& operator=(const PackageBaseIdentity&) = default;
    PackageBaseIdentity& operator=(PackageBaseIdentity&&) noexcept = default;
    ~PackageBaseIdentity() = default;

    [[nodiscard]] static PackageBaseIdentity make(
            PackageSourceIdentity source, std::string package_base);

    [[nodiscard]] const PackageSourceIdentity& source() const noexcept;
    [[nodiscard]] const std::string& package_base() const noexcept;

    bool operator==(const PackageBaseIdentity&) const = default;

private:
    PackageBaseIdentity(
            PackageSourceIdentity source,
            std::string package_base) noexcept;

    PackageSourceIdentity source_;
    std::string           package_base_;
};

class PackageChildIdentity final {
public:
    PackageChildIdentity() = delete;
    PackageChildIdentity(const PackageChildIdentity&) = default;
    PackageChildIdentity(PackageChildIdentity&&) noexcept = default;
    PackageChildIdentity& operator=(const PackageChildIdentity&) = default;
    PackageChildIdentity& operator=(PackageChildIdentity&&) noexcept = default;
    ~PackageChildIdentity() = default;

    [[nodiscard]] static PackageChildIdentity make(
            PackageBaseIdentity package_base, std::string package_name);

    [[nodiscard]] const PackageBaseIdentity& package_base() const noexcept;
    [[nodiscard]] const std::string& package_name() const noexcept;

    bool operator==(const PackageChildIdentity&) const = default;

private:
    PackageChildIdentity(
            PackageBaseIdentity package_base,
            std::string package_name) noexcept;

    PackageBaseIdentity package_base_;
    std::string         package_name_;
};

enum class GitObjectFormat {
    Sha1,
    Sha256,
};

enum class SourceRevisionState {
    Known,
    Unknown,
    Absent,
    Unavailable,
    Inapplicable,
};

class SourceRevisionIdentity final {
public:
    SourceRevisionIdentity() = delete;
    SourceRevisionIdentity(const SourceRevisionIdentity&) = default;
    SourceRevisionIdentity(SourceRevisionIdentity&&) noexcept = default;
    SourceRevisionIdentity& operator=(const SourceRevisionIdentity&) = default;
    SourceRevisionIdentity& operator=(SourceRevisionIdentity&&) noexcept =
            default;
    ~SourceRevisionIdentity() = default;

    [[nodiscard]] static SourceRevisionIdentity git_commit(
            std::string object_id);
    [[nodiscard]] static SourceRevisionIdentity unknown() noexcept;
    [[nodiscard]] static SourceRevisionIdentity absent() noexcept;
    [[nodiscard]] static SourceRevisionIdentity unavailable(
            IdentityUnavailableReason reason);
    [[nodiscard]] static SourceRevisionIdentity inapplicable() noexcept;

    [[nodiscard]] SourceRevisionState state() const noexcept;
    [[nodiscard]] const GitObjectFormat* git_object_format() const noexcept;
    [[nodiscard]] const std::string* git_commit() const noexcept;
    [[nodiscard]] const IdentityUnavailableReason* unavailable_reason()
            const noexcept;

    bool operator==(const SourceRevisionIdentity&) const = default;

private:
    SourceRevisionIdentity(
            SourceRevisionState state,
            std::optional<GitObjectFormat> git_object_format,
            std::optional<std::string> git_commit,
            std::optional<IdentityUnavailableReason> unavailable_reason)
        noexcept;

    SourceRevisionState                      state_;
    std::optional<GitObjectFormat>            git_object_format_;
    std::optional<std::string>                git_commit_;
    std::optional<IdentityUnavailableReason> unavailable_reason_;
};

enum class PackageVersionState {
    Known,
    Unknown,
    Unavailable,
};

enum class PackageVersionRepresentation {
    Composite,
    PkgverPkgrel,
};

class PackageVersionIdentity final {
public:
    PackageVersionIdentity() = delete;
    PackageVersionIdentity(const PackageVersionIdentity&) = default;
    PackageVersionIdentity(PackageVersionIdentity&&) noexcept = default;
    PackageVersionIdentity& operator=(const PackageVersionIdentity&) = default;
    PackageVersionIdentity& operator=(PackageVersionIdentity&&) noexcept =
            default;
    ~PackageVersionIdentity() = default;

    [[nodiscard]] static PackageVersionIdentity composite(
            std::string full_version);
    [[nodiscard]] static PackageVersionIdentity pkgver_pkgrel(
            std::optional<std::string> epoch,
            std::string pkgver,
            std::string pkgrel);
    [[nodiscard]] static PackageVersionIdentity unknown() noexcept;
    [[nodiscard]] static PackageVersionIdentity unavailable(
            IdentityUnavailableReason reason);

    [[nodiscard]] PackageVersionState state() const noexcept;
    [[nodiscard]] const PackageVersionRepresentation* representation()
            const noexcept;
    [[nodiscard]] const std::string* full_version() const noexcept;
    [[nodiscard]] const std::string* epoch() const noexcept;
    [[nodiscard]] const std::string* pkgver() const noexcept;
    [[nodiscard]] const std::string* pkgrel() const noexcept;
    [[nodiscard]] const IdentityUnavailableReason* unavailable_reason()
            const noexcept;

    bool operator==(const PackageVersionIdentity&) const = default;

private:
    PackageVersionIdentity(
            PackageVersionState state,
            std::optional<PackageVersionRepresentation> representation,
            std::optional<std::string> full_version,
            std::optional<std::string> epoch,
            std::optional<std::string> pkgver,
            std::optional<std::string> pkgrel,
            std::optional<IdentityUnavailableReason> unavailable_reason)
        noexcept;

    PackageVersionState                             state_;
    std::optional<PackageVersionRepresentation>     representation_;
    std::optional<std::string>                      full_version_;
    std::optional<std::string>                      epoch_;
    std::optional<std::string>                      pkgver_;
    std::optional<std::string>                      pkgrel_;
    std::optional<IdentityUnavailableReason>        unavailable_reason_;
};

enum class PackageArchitectureState {
    Known,
    Unknown,
    Unavailable,
};

class PackageArchitectureIdentity final {
public:
    PackageArchitectureIdentity() = delete;
    PackageArchitectureIdentity(const PackageArchitectureIdentity&) = default;
    PackageArchitectureIdentity(PackageArchitectureIdentity&&) noexcept =
            default;
    PackageArchitectureIdentity& operator=(
            const PackageArchitectureIdentity&) = default;
    PackageArchitectureIdentity& operator=(
            PackageArchitectureIdentity&&) noexcept = default;
    ~PackageArchitectureIdentity() = default;

    [[nodiscard]] static PackageArchitectureIdentity known(
            std::vector<std::string> architectures);
    [[nodiscard]] static PackageArchitectureIdentity unknown() noexcept;
    [[nodiscard]] static PackageArchitectureIdentity unavailable(
            IdentityUnavailableReason reason);

    [[nodiscard]] PackageArchitectureState state() const noexcept;
    [[nodiscard]] const std::vector<std::string>& architectures()
            const noexcept;
    [[nodiscard]] const IdentityUnavailableReason* unavailable_reason()
            const noexcept;

    bool operator==(const PackageArchitectureIdentity&) const = default;

private:
    PackageArchitectureIdentity(
            PackageArchitectureState state,
            std::vector<std::string> architectures,
            std::optional<IdentityUnavailableReason> unavailable_reason)
        noexcept;

    PackageArchitectureState                 state_;
    std::vector<std::string>                  architectures_;
    std::optional<IdentityUnavailableReason> unavailable_reason_;
};

class SourceAwarePackageIdentity final {
public:
    SourceAwarePackageIdentity() = delete;
    SourceAwarePackageIdentity(const SourceAwarePackageIdentity&) = default;
    SourceAwarePackageIdentity(SourceAwarePackageIdentity&&) noexcept = default;
    SourceAwarePackageIdentity& operator=(
            const SourceAwarePackageIdentity&) = default;
    SourceAwarePackageIdentity& operator=(
            SourceAwarePackageIdentity&&) noexcept = default;
    ~SourceAwarePackageIdentity() = default;

    [[nodiscard]] static SourceAwarePackageIdentity make(
            PackageChildIdentity package,
            SourceRevisionIdentity source_revision,
            PackageVersionIdentity package_version,
            PackageArchitectureIdentity architecture);

    [[nodiscard]] const PackageChildIdentity& package() const noexcept;
    [[nodiscard]] const SourceRevisionIdentity& source_revision()
            const noexcept;
    [[nodiscard]] const PackageVersionIdentity& package_version()
            const noexcept;
    [[nodiscard]] const PackageArchitectureIdentity& architecture()
            const noexcept;

    bool operator==(const SourceAwarePackageIdentity&) const = default;

private:
    SourceAwarePackageIdentity(
            PackageChildIdentity package,
            SourceRevisionIdentity source_revision,
            PackageVersionIdentity package_version,
            PackageArchitectureIdentity architecture) noexcept;

    PackageChildIdentity        package_;
    SourceRevisionIdentity      source_revision_;
    PackageVersionIdentity      package_version_;
    PackageArchitectureIdentity architecture_;
};
