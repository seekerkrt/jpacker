#pragma once

#include "dependency_constraint.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// repository由来providerはprovenanceとしてexact repository名を所有する。
struct RepositoryProviderOrigin {
    std::string repository_name;

    bool operator==(const RepositoryProviderOrigin&) const = default;
};

// AUR由来providerにはrepository名という概念を持たせない。
struct AurProviderOrigin {
    bool operator==(const AurProviderOrigin&) const = default;
};

using ProviderOrigin = std::variant<RepositoryProviderOrigin, AurProviderOrigin>;

// Constraint metadata is resolution metadata, not provider identity. Keeping
// package and provided versions in separate typed fields prevents an
// unversioned capability from inheriting its provider package's version.
struct ProviderConstraintMetadata {
    ProviderCapability provided_capability;
    ObservedVersion    package_version;
    ObservedVersion    provided_version;

    bool operator==(const ProviderConstraintMetadata&) const = default;
};

// 依存名を満たすprovider packageと、そのtyped provenance。
// POLICY(#267): origin shapeはfactoryで固定し、trust boundaryの文字列検証とは分離する。
struct ProvidedDependency {
    ProviderOrigin            origin;
    std::string               package_name;
    std::string               package_base;
    std::string               provided_dependency_name;
    std::string               provided_dependency_specification;
    std::optional<std::string> package_version;
    std::optional<ProviderConstraintMetadata> constraint_metadata;

    static ProvidedDependency from_repository(
            std::string repository_name, std::string package_name) {
        return from_repository(
                std::move(repository_name), std::move(package_name), {}, {},
                std::nullopt);
    }

    static ProvidedDependency from_repository(
            std::string repository_name, std::string package_name,
            std::string provided_dependency_name,
            std::string provided_dependency_specification,
            std::optional<std::string> package_version) {
        return ProvidedDependency{
                RepositoryProviderOrigin{std::move(repository_name)},
                std::move(package_name), {},
                std::move(provided_dependency_name),
                std::move(provided_dependency_specification),
                std::move(package_version), std::nullopt};
    }

    static ProvidedDependency from_aur(std::string package_name) {
        std::string package_base = package_name;
        return from_aur(
                std::move(package_name), std::move(package_base), {}, {},
                std::nullopt);
    }

    static ProvidedDependency from_aur(
            std::string package_name, std::string package_base,
            std::string provided_dependency_name,
            std::string provided_dependency_specification,
            std::optional<std::string> package_version) {
        return ProvidedDependency{
                AurProviderOrigin{}, std::move(package_name),
                std::move(package_base),
                std::move(provided_dependency_name),
                std::move(provided_dependency_specification),
                std::move(package_version), std::nullopt};
    }

    static ProvidedDependency from_aur_constraint_metadata(
            std::string package_name, std::string package_base,
            ProviderConstraintMetadata constraint_metadata) {
        const ProviderCapability& capability =
                constraint_metadata.provided_capability;
        if(constraint_metadata.package_version.source() !=
                   ObservedVersionSource::AurExactPackage ||
           constraint_metadata.provided_version.source() !=
                   ObservedVersionSource::AurProviderCapability) {
            throw std::invalid_argument(
                    "AUR provider constraint metadata has incompatible version sources.");
        }
        if(capability.version().has_value()) {
            const std::string* provided_version =
                    constraint_metadata.provided_version.version();
            if(provided_version == nullptr ||
               *provided_version != capability.version().value()) {
                throw std::invalid_argument(
                        "AUR provided capability version metadata is inconsistent.");
            }
        } else {
            const ObservedVersionUnknownReason* unknown_reason =
                    constraint_metadata.provided_version.unknown_reason();
            if(unknown_reason == nullptr ||
               *unknown_reason != ObservedVersionUnknownReason::
                                          UnversionedProviderCapability) {
                throw std::invalid_argument(
                        "Unversioned AUR capability has version metadata.");
            }
        }
        const std::string* package_version =
                constraint_metadata.package_version.version();
        return ProvidedDependency{
                AurProviderOrigin{},
                std::move(package_name),
                std::move(package_base),
                capability.package_name(),
                capability.raw_specification(),
                package_version == nullptr
                        ? std::nullopt
                        : std::optional<std::string>(*package_version),
                std::move(constraint_metadata)};
    }

    bool operator==(const ProvidedDependency&) const = default;

private:
    ProvidedDependency(
            ProviderOrigin origin, std::string package_name,
            std::string package_base,
            std::string provided_dependency_name,
            std::string provided_dependency_specification,
            std::optional<std::string> package_version,
            std::optional<ProviderConstraintMetadata> constraint_metadata)
        : origin(std::move(origin)), package_name(std::move(package_name)),
          package_base(std::move(package_base)),
          provided_dependency_name(std::move(provided_dependency_name)),
          provided_dependency_specification(
                  std::move(provided_dependency_specification)),
          package_version(std::move(package_version)),
          constraint_metadata(std::move(constraint_metadata)) {}
};

// provider choiceのidentityはpresentationや取得時点の補助metadataに依存させない。
inline bool same_provider_identity(
        const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.origin == rhs.origin && lhs.package_name == rhs.package_name &&
           lhs.package_base == rhs.package_base;
}

// 同じpackage nameを異なるsource identityへ同時に束ねるchoiceは、1 transaction
// 内でどちらを導入したか証明できないため互換とみなさない。
inline bool has_incompatible_provider_package_identity(
        const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.package_name == rhs.package_name &&
           !same_provider_identity(lhs, rhs);
}

// conflict diagnostic向けのsource-aware identity。constraint/version等の
// 可変metadataを含めず、choiceを構成するidentity fieldだけを表示する。
inline std::string provider_package_identity_display(
        const ProvidedDependency& provider) {
    if(const auto* repository =
               std::get_if<RepositoryProviderOrigin>(&provider.origin);
       repository != nullptr) {
        return repository->repository_name + "/" + provider.package_name;
    }
    return "aur/" + provider.package_name + " (PackageBase: " +
            provider.package_base + ")";
}

// presentation専用のone-way変換。結果をorigin判定へ使わない。
inline std::string provided_dependency_display(
        const ProvidedDependency& provider) {
    if(const auto* repository =
               std::get_if<RepositoryProviderOrigin>(&provider.origin);
       repository != nullptr) {
        return repository->repository_name + "/" + provider.package_name;
    }
    return "aur/" + provider.package_name;
}
