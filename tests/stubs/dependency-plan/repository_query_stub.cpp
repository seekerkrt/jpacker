#include "repository_query.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::size_t g_legacy_repo_package_queries = 0;
std::size_t g_sync_database_package_queries = 0;
std::size_t g_strict_repo_provider_queries = 0;

std::vector<ProvidedDependency> repository_providers(
        const std::string& dependency_name) {
    if(dependency_name == "case8-virtual") {
        return {
                ProvidedDependency{"extra", "case8-provider-a"},
                ProvidedDependency{"community", "case8-provider-b"},
        };
    }
    if(dependency_name == "case11-ambiguous") {
        return {
                ProvidedDependency{"extra", "case11-provider-a"},
                ProvidedDependency{"community", "case11-provider-b"},
        };
    }
    if(dependency_name == "case14-virtual") {
        return {ProvidedDependency{"extra", "case14-provider"}};
    }
    if(dependency_name == "case7-virtual-api" || dependency_name == "case9-missing" ||
       dependency_name == "case11-virtual" || dependency_name == "case11-missing" ||
       dependency_name == "preflight-dependency-failure-child" ||
       dependency_name == "preflight-provider-search-virtual" ||
       dependency_name == "preflight-provider-candidate-virtual" ||
       dependency_name == "preflight-shared-failure" ||
       dependency_name == "preflight-provider-response-virtual" ||
       dependency_name == "preflight-dependency-response-child" ||
       dependency_name == "preflight-provider-candidate-response-virtual" ||
       dependency_name == "preflight-repository-provider-failure-virtual") {
        return {};
    }
    throw std::runtime_error(
            "Unexpected dependency-plan repository provider query: " + dependency_name);
}

} // namespace

namespace dependency_plan_repository_query_stub {

void reset_query_counts() {
    g_legacy_repo_package_queries = 0;
    g_sync_database_package_queries = 0;
    g_strict_repo_provider_queries = 0;
}

std::size_t legacy_repo_package_query_count() {
    return g_legacy_repo_package_queries;
}

std::size_t sync_database_package_query_count() {
    return g_sync_database_package_queries;
}

std::size_t strict_repo_provider_query_count() {
    return g_strict_repo_provider_queries;
}

} // namespace dependency_plan_repository_query_stub

bool is_repo_package(const std::string& package_name) {
    ++g_legacy_repo_package_queries;
    return package_name == "case6-repo-lib";
}

StrictRepositoryPackageQueryResult query_repository_package_strict(
        const std::string& package_name) {
    ++g_sync_database_package_queries;
    if(package_name == "preflight-repository-exact-failure-child") {
        return RepositoryMetadataFailure{
                RepositoryMetadataFailureKind::ConfigurationUnavailable,
                std::nullopt,
                "strict repository exact metadata failure"};
    }
    if(package_name == "case6-repo-lib") {
        return RepositoryPackagePresent{"core"};
    }
    return RepositoryPackageNotFound{};
}

std::vector<ProvidedDependency> find_repo_providers(
        const std::string& dependency_name) {
    return repository_providers(dependency_name);
}

StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name) {
    ++g_strict_repo_provider_queries;
    if(dependency_name == "preflight-repository-provider-failure-virtual") {
        return RepositoryMetadataFailure{
                RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
                std::optional<std::string>{"core"},
                "strict repository provider metadata failure"};
    }
    return repository_providers(dependency_name);
}
