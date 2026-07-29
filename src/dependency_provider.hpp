#pragma once

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

// 依存名を満たすprovider packageと、そのtyped provenance。
// POLICY(#267): origin shapeはfactoryで固定し、trust boundaryの文字列検証とは分離する。
struct ProvidedDependency {
    ProviderOrigin origin;
    std::string    package_name;

    static ProvidedDependency from_repository(
            std::string repository_name, std::string package_name) {
        return ProvidedDependency{
                RepositoryProviderOrigin{std::move(repository_name)},
                std::move(package_name)};
    }

    static ProvidedDependency from_aur(std::string package_name) {
        return ProvidedDependency{
                AurProviderOrigin{}, std::move(package_name)};
    }

    bool operator==(const ProvidedDependency&) const = default;

private:
    ProvidedDependency(ProviderOrigin origin, std::string package_name)
        : origin(std::move(origin)), package_name(std::move(package_name)) {}
};

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
