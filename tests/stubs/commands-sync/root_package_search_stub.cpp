#include "root_package_search.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// WHY: final sync CLI testはproduction selection/executionを通しつつ、
// repository/AUR discoveryの外部状態だけをdeterministic snapshotへ置換する。
namespace {

const char* COMMAND_LOG_ENV = "MOGUET_TEST_COMMAND_LOG";

std::string required_environment_value(const char* name) {
    const char* value = std::getenv(name);
    if(value == nullptr || value[0] == '\0') {
        throw std::runtime_error(
            std::string("Missing test environment variable: ") + name);
    }
    return value;
}

void append_command_log(const std::string& line) {
    std::ofstream log(
        required_environment_value(COMMAND_LOG_ENV), std::ios::app);
    if(!log) {
        throw std::runtime_error(
            "Failed to open commands-sync command log.");
    }

    log << line << '\n';
    if(!log) {
        throw std::runtime_error(
            "Failed to write commands-sync command log.");
    }
}

std::string scope_name(RootPackageSearchScope scope) {
    switch(scope) {
        case RootPackageSearchScope::All:
            return "all";
        case RootPackageSearchScope::Repository:
            return "repository";
        case RootPackageSearchScope::Aur:
            return "aur";
    }
    throw std::runtime_error("Unknown commands-sync search scope.");
}

bool includes_repository(RootPackageSearchScope scope) noexcept {
    return scope == RootPackageSearchScope::All ||
           scope == RootPackageSearchScope::Repository;
}

bool includes_aur(RootPackageSearchScope scope) noexcept {
    return scope == RootPackageSearchScope::All ||
           scope == RootPackageSearchScope::Aur;
}

RootPackageSearchCandidate repository_candidate(
    std::string repository_name,
    std::string package_name,
    std::string version,
    std::string description,
    std::vector<std::string> groups = {}) {
    RootPackageCandidateValidationResult result =
        make_repository_root_package_candidate(
            std::move(repository_name),
            std::move(package_name),
            std::move(version),
            std::move(description));
    if(!result.is_valid()) {
        throw std::runtime_error(
            "Invalid repository commands-sync search fixture.");
    }
    return RootPackageSearchCandidate{
        *result.candidate(), std::move(groups)};
}

RootPackageSearchCandidate aur_candidate(
    std::string package_name,
    std::string package_base,
    std::string version,
    std::string description) {
    RootPackageCandidateValidationResult result =
        make_aur_root_package_candidate(
            std::move(package_name),
            std::move(package_base),
            std::move(version),
            std::move(description));
    if(!result.is_valid()) {
        throw std::runtime_error(
            "Invalid AUR commands-sync search fixture.");
    }
    return RootPackageSearchCandidate{*result.candidate(), {}};
}

RootPackageSearchSnapshot presentation_snapshot(
    RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(includes_repository(scope)) {
        snapshot.candidates.push_back(repository_candidate(
            "aur", "repo-presented", "3.0-1",
            "repository presentation fixture", {"desktop"}));
    }
    if(includes_aur(scope)) {
        snapshot.candidates.push_back(aur_candidate(
            "aur-presented", "aur-presented", "4.0-1",
            "AUR presentation fixture"));
    }
    return snapshot;
}

RootPackageSearchSnapshot alternative_conflict_snapshot(
    RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(includes_repository(scope)) {
        snapshot.candidates.push_back(repository_candidate(
            "core", "shared-alternative", "3.0-1",
            "repository alternative fixture"));
    }
    if(includes_aur(scope)) {
        snapshot.candidates.push_back(aur_candidate(
            "shared-alternative", "shared-alternative-base", "4.0-1",
            "AUR alternative fixture"));
    }
    return snapshot;
}

RootPackageSearchSnapshot scope_snapshot(RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(includes_repository(scope)) {
        snapshot.candidates.push_back(repository_candidate(
            "core", "scope-repo", "1.0-1",
            "repository scope fixture"));
    }
    if(includes_aur(scope)) {
        snapshot.candidates.push_back(aur_candidate(
            "scope-aur", "scope-aur", "1.0-1",
            "AUR scope fixture"));
    }
    return snapshot;
}

RootPackageSearchSnapshot repository_snapshot(
    RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(!includes_repository(scope)) return snapshot;

    snapshot.candidates.push_back(repository_candidate(
        "core", "repo-one", "1.0-1",
        "first repository transaction fixture", {"repo-group"}));
    snapshot.candidates.push_back(repository_candidate(
        "extra", "repo-two", "2.0-1",
        "second repository transaction fixture", {"repo-group"}));
    return snapshot;
}

RootPackageSearchSnapshot mixed_snapshot(RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(includes_repository(scope)) {
        snapshot.candidates.push_back(repository_candidate(
            "extra", "mixed-repo", "1.0-1",
            "mixed repository fixture"));
    }
    if(includes_aur(scope)) {
        snapshot.candidates.push_back(aur_candidate(
            "mixed-aur", "mixed-aur", "1.0-1",
            "mixed AUR fixture"));
    }
    return snapshot;
}

RootPackageSearchSnapshot package_base_mismatch_snapshot(
    RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(includes_aur(scope)) {
        snapshot.candidates.push_back(aur_candidate(
            "mismatch-child", "selected-base", "1.0-1",
            "stale PackageBase fixture"));
    }
    return snapshot;
}

RootPackageSearchSnapshot same_base_snapshot(
    RootPackageSearchScope scope) {
    RootPackageSearchSnapshot snapshot;
    if(!includes_aur(scope)) return snapshot;

    snapshot.candidates.push_back(aur_candidate(
        "split-one", "split-suite", "1.0-1",
        "first split package fixture"));
    snapshot.candidates.push_back(aur_candidate(
        "split-two", "split-suite", "1.0-1",
        "second split package fixture"));
    return snapshot;
}

} // namespace

RootPackageSearchResult search_root_package_candidates(
    const std::string& query,
    RootPackageSearchScope scope) {
    append_command_log(
        "root search " + scope_name(scope) + " " + query);

    if(query == "select-presentation") {
        return presentation_snapshot(scope);
    }
    if(query == "select-alternative-conflict") {
        return alternative_conflict_snapshot(scope);
    }
    if(query == "select-scope") return scope_snapshot(scope);
    if(query == "select-repository") {
        return repository_snapshot(scope);
    }
    if(query == "select-mixed") return mixed_snapshot(scope);
    if(query == "select-package-base-mismatch") {
        return package_base_mismatch_snapshot(scope);
    }
    if(query == "select-same-base") {
        return same_base_snapshot(scope);
    }
    if(query == "select-search-failure") {
        return AurRootPackageSearchFailure{
            "fixture selected root search failure"};
    }
    if(query == "select-repository-search-failure") {
        return RepositoryRootPackageSearchFailure{PackageMetadataFailure{
            PackageMetadataErrorCode::QueryFailed,
            "fixture selected repository search failure"}};
    }
    return RootPackageSearchSnapshot{};
}
