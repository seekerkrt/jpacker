#include "search_stub.hpp"

#include <deque>
#include <stdexcept>
#include <utility>
#include <variant>

namespace {

using AurSearchOutcome =
    std::variant<std::vector<AurPackageInfo>, std::string>;

std::deque<RepositoryPackageSearchResult> g_repository_results;
std::deque<AurSearchOutcome> g_aur_results;
std::vector<std::string> g_repository_queries;
std::vector<std::string> g_aur_queries;

} // namespace

namespace root_package_search_test_stub {

void reset() {
    g_repository_results.clear();
    g_aur_results.clear();
    g_repository_queries.clear();
    g_aur_queries.clear();
}

void enqueue_repository_result(RepositoryPackageSearchResult result) {
    g_repository_results.push_back(std::move(result));
}

void enqueue_aur_result(std::vector<AurPackageInfo> result) {
    g_aur_results.push_back(std::move(result));
}

void enqueue_aur_failure(std::string diagnostic) {
    g_aur_results.push_back(std::move(diagnostic));
}

std::size_t repository_query_count() {
    return g_repository_queries.size();
}

std::size_t aur_query_count() {
    return g_aur_queries.size();
}

std::vector<std::string> repository_queries() {
    return g_repository_queries;
}

std::vector<std::string> aur_queries() {
    return g_aur_queries;
}

} // namespace root_package_search_test_stub

RepositoryPackageSearchResult query_repository_root_package_search(
    const std::string& query) {
    g_repository_queries.push_back(query);
    if(g_repository_results.empty()) {
        throw std::logic_error(
            "Unexpected repository root package search query.");
    }
    RepositoryPackageSearchResult result =
        std::move(g_repository_results.front());
    g_repository_results.pop_front();
    return result;
}

std::vector<AurPackageInfo> AurClient::search_strict(
    const std::string& query) {
    g_aur_queries.push_back(query);
    if(g_aur_results.empty()) {
        throw std::logic_error("Unexpected strict AUR root package search.");
    }
    AurSearchOutcome outcome = std::move(g_aur_results.front());
    g_aur_results.pop_front();
    if(const auto* diagnostic = std::get_if<std::string>(&outcome);
       diagnostic != nullptr) {
        throw std::runtime_error(*diagnostic);
    }
    return std::get<std::vector<AurPackageInfo>>(std::move(outcome));
}
