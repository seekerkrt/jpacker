#pragma once

#include "package_metadata.hpp"
#include "root_package_candidate.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class RootPackageSearchScope {
    All,
    Repository,
    Aur
};

// selectable_group_namesは、安全なexact official groupだけを保持する。
// unsafeなgroup名のmemberもcandidate自体は番号選択できる形で残す。
struct RootPackageSearchCandidate {
    RootPackageCandidate     candidate;
    std::vector<std::string> selectable_group_names;

    bool operator==(const RootPackageSearchCandidate&) const = default;
};

struct RootPackageSearchSnapshot {
    std::vector<RootPackageSearchCandidate> candidates;

    bool operator==(const RootPackageSearchSnapshot&) const = default;
};

struct RepositoryRootPackageSearchFailure {
    PackageMetadataFailure failure;
};

struct AurRootPackageSearchFailure {
    std::string diagnostic;
};

struct InvalidRepositoryRootPackageGroupMatch {
    RepositoryRootPackageIdentity identity;
    std::optional<std::string>     group_name;

    bool operator==(
            const InvalidRepositoryRootPackageGroupMatch&) const = default;
};

// adapterのvalidated snapshotだけをpublishするため、collection-levelの
// invalid stateをcandidate snapshotとは別alternativeで保持する。
struct InvalidRootPackageSearchSnapshot {
    std::vector<RootPackageCandidateValidationFailure> validation_failures;
    std::vector<RootPackageCandidatePairIssue> candidate_pair_issues;
    std::vector<InvalidRepositoryRootPackageGroupMatch>
            invalid_group_matches;
    std::vector<std::string> duplicate_repository_order_entries;
    std::vector<RepositoryRootPackageIdentity> unranked_repository_candidates;
};

using RootPackageSearchResult = std::variant<
        RootPackageSearchSnapshot,
        RepositoryRootPackageSearchFailure,
        AurRootPackageSearchFailure,
        InvalidRootPackageSearchSnapshot>;

RootPackageSearchResult search_root_package_candidates(
        const std::string& query,
        RootPackageSearchScope scope);
