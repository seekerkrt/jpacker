#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class LocalPackageMetadataRelationKind {
    Depends,
    Makedepends,
    Checkdepends,
    Optdepends,
    Provides,
    Conflicts,
    Replaces
};

enum class LocalPackageMetadataScopeKind {
    PackageBase,
    ChildPackage
};

enum class LocalPackageMetadataRelationTargetKind {
    Package,
    Soname
};

enum class LocalPackageMetadataComparison {
    LessThan,
    LessThanOrEqual,
    Equal,
    GreaterThanOrEqual,
    GreaterThan
};

struct LocalPackageMetadataScope {
    LocalPackageMetadataScopeKind kind;
    std::optional<std::string>     package_name;

    bool operator==(const LocalPackageMetadataScope&) const = default;
};

struct LocalPackageMetadataRelationTarget {
    LocalPackageMetadataRelationTargetKind       kind;
    std::string                                  name;
    std::optional<LocalPackageMetadataComparison> comparison;
    std::optional<std::string>                   version;

    bool operator==(const LocalPackageMetadataRelationTarget&) const = default;
};

struct LocalPackageMetadataRelation {
    LocalPackageMetadataRelationKind kind;
    std::string                      raw_value;
    std::optional<LocalPackageMetadataRelationTarget> target;
    LocalPackageMetadataScope        scope;
    std::optional<std::string>       architecture_qualifier;
    std::optional<std::string>       optdepends_description;
    bool                             is_explicit_unset;

    bool operator==(const LocalPackageMetadataRelation&) const = default;
};

struct LocalPackageMetadataChild {
    std::string              name;
    bool                     has_architecture_override;
    bool                     clears_inherited_architectures;
    std::vector<std::string> architectures;

    bool operator==(const LocalPackageMetadataChild&) const = default;
};

struct LocalPackageMetadata {
    std::string                               package_base;
    std::optional<std::string>                epoch;
    std::string                               pkgver;
    std::string                               pkgrel;
    std::vector<std::string>                  architectures;
    std::vector<LocalPackageMetadataChild>    children;
    std::vector<LocalPackageMetadataRelation> relations;

    bool operator==(const LocalPackageMetadata&) const = default;
};

enum class LocalPackageMetadataParseErrorCode {
    MalformedLine,
    ControlCharacter,
    InvalidPackageIdentity,
    InvalidFieldScope,
    InvalidEpoch,
    InvalidPkgver,
    InvalidPkgrel,
    InvalidArchitecture,
    DuplicateArchitecture,
    ConflictingArchitecture,
    InvalidArchitectureQualifier,
    InvalidRelation,
    EmptyRequiredValue,
    DuplicatePackageBase,
    ConflictingPackageBase,
    DuplicatePackageName,
    DuplicateEpoch,
    DuplicatePkgver,
    DuplicatePkgrel,
    MissingPackageBase,
    MissingPkgver,
    MissingPkgrel,
    MissingArchitecture,
    MissingPackageName
};

struct LocalPackageMetadataParseFailure {
    LocalPackageMetadataParseErrorCode code;
    std::size_t                        line;

    bool operator==(const LocalPackageMetadataParseFailure&) const = default;
};

class LocalPackageMetadataParseResult final {
public:
    LocalPackageMetadataParseResult() = delete;
    LocalPackageMetadataParseResult(
            const LocalPackageMetadataParseResult&) = default;
    LocalPackageMetadataParseResult(
            LocalPackageMetadataParseResult&&) noexcept = default;
    LocalPackageMetadataParseResult& operator=(
            const LocalPackageMetadataParseResult&) = delete;
    LocalPackageMetadataParseResult& operator=(
            LocalPackageMetadataParseResult&&) noexcept = delete;
    ~LocalPackageMetadataParseResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const LocalPackageMetadata* metadata() const noexcept;
    [[nodiscard]] const LocalPackageMetadataParseFailure* failure()
            const noexcept;

private:
    explicit LocalPackageMetadataParseResult(
            LocalPackageMetadata metadata) noexcept;
    explicit LocalPackageMetadataParseResult(
            LocalPackageMetadataParseFailure failure) noexcept;

    std::variant<LocalPackageMetadata, LocalPackageMetadataParseFailure>
            outcome_;

    friend LocalPackageMetadataParseResult parse_local_package_metadata(
            std::string_view source);
};

// `.SRCINFO` textだけを入力とし、filesystem、AUR、CLI stateを参照しない。
LocalPackageMetadataParseResult parse_local_package_metadata(
        std::string_view source);
