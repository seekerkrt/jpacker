#include "stubs/local-dependency-plan/query_stub.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using local_dependency_plan_query_stub::AurQuery;
using local_dependency_plan_query_stub::AurQueryKind;

std::map<std::string, std::optional<AurPackageInfo>> g_package_responses;
std::map<std::string, std::vector<std::string>> g_provider_responses;
std::map<std::string, std::string> g_package_failures;
std::map<std::string, std::string> g_provider_failures;
std::vector<AurQuery> g_query_history;

const std::optional<AurPackageInfo>& require_package_response(
        const std::string& package_name) {
    const auto failure = g_package_failures.find(package_name);
    if(failure != g_package_failures.end()) {
        throw std::runtime_error(failure->second);
    }
    const auto response = g_package_responses.find(package_name);
    if(response == g_package_responses.end()) {
        throw std::runtime_error(
                "Unexpected local dependency plan AUR package query: " +
                package_name);
    }
    return response->second;
}

const std::vector<std::string>& require_provider_response(
        const std::string& provided_name) {
    const auto failure = g_provider_failures.find(provided_name);
    if(failure != g_provider_failures.end()) {
        throw std::runtime_error(failure->second);
    }
    const auto response = g_provider_responses.find(provided_name);
    if(response == g_provider_responses.end()) {
        throw std::runtime_error(
                "Unexpected local dependency plan AUR provider query: " +
                provided_name);
    }
    return response->second;
}

} // namespace

namespace local_dependency_plan_query_stub {

void reset_aur_stub() {
    g_package_responses.clear();
    g_provider_responses.clear();
    g_package_failures.clear();
    g_provider_failures.clear();
    g_query_history.clear();
}

void set_aur_package_response(
        std::string package_name, std::optional<AurPackageInfo> package) {
    g_package_failures.erase(package_name);
    g_package_responses.insert_or_assign(
            std::move(package_name), std::move(package));
}

void set_aur_package_failure(
        std::string package_name, std::string diagnostic) {
    g_package_responses.erase(package_name);
    g_package_failures.insert_or_assign(
            std::move(package_name), std::move(diagnostic));
}

void set_aur_provider_response(
        std::string provided_name, std::vector<std::string> package_names) {
    g_provider_failures.erase(provided_name);
    g_provider_responses.insert_or_assign(
            std::move(provided_name), std::move(package_names));
}

void set_aur_provider_failure(
        std::string provided_name, std::string diagnostic) {
    g_provider_responses.erase(provided_name);
    g_provider_failures.insert_or_assign(
            std::move(provided_name), std::move(diagnostic));
}

const std::vector<AurQuery>& aur_query_history() {
    return g_query_history;
}

std::size_t aur_query_count(AurQueryKind kind, const std::string& subject) {
    return static_cast<std::size_t>(std::count(
            g_query_history.begin(), g_query_history.end(),
            AurQuery{kind, subject}));
}

} // namespace local_dependency_plan_query_stub

std::vector<AurPackageInfo> AurClient::search(const std::string& query) {
    throw std::runtime_error(
            "Unexpected local dependency plan AUR search query: " + query);
}

std::vector<AurPackageInfo> AurClient::search_strict(
        const std::string& query) {
    throw std::runtime_error(
            "Unexpected local dependency plan strict AUR search query: " + query);
}

std::vector<std::string> AurClient::search_names_by_provides(
        const std::string& provided_name) {
    g_query_history.push_back(
            AurQuery{AurQueryKind::LegacyProviderSearch, provided_name});
    return require_provider_response(provided_name);
}

std::vector<std::string> AurClient::search_names_by_provides_strict(
        const std::string& provided_name) {
    g_query_history.push_back(
            AurQuery{AurQueryKind::StrictProviderSearch, provided_name});
    return require_provider_response(provided_name);
}

std::optional<AurPackageInfo> AurClient::info(
        const std::string& package_name) {
    g_query_history.push_back(
            AurQuery{AurQueryKind::LegacyInfo, package_name});
    return require_package_response(package_name);
}

std::optional<AurPackageInfo> AurClient::info_strict(
        const std::string& package_name) {
    g_query_history.push_back(
            AurQuery{AurQueryKind::StrictInfo, package_name});
    return require_package_response(package_name);
}

std::map<std::string, AurPackageInfo> AurClient::info_many(
        const std::vector<std::string>& package_names) {
    throw std::runtime_error(
            "Unexpected local dependency plan AUR batch info query: " +
            std::to_string(package_names.size()));
}
