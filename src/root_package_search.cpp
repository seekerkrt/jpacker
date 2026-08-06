#include "root_package_search.hpp"

#include "aur_rpc.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct PendingRootPackageSearchCandidate {
    RootPackageCandidate     candidate;
    std::vector<std::string> selectable_group_names;
};

int compare_bytewise(std::string_view lhs, std::string_view rhs) noexcept {
    const std::size_t shared_size = std::min(lhs.size(), rhs.size());
    for(std::size_t index = 0; index < shared_size; ++index) {
        const unsigned char lhs_byte =
                static_cast<unsigned char>(lhs[index]);
        const unsigned char rhs_byte =
                static_cast<unsigned char>(rhs[index]);
        if(lhs_byte < rhs_byte) return -1;
        if(lhs_byte > rhs_byte) return 1;
    }
    if(lhs.size() < rhs.size()) return -1;
    if(lhs.size() > rhs.size()) return 1;
    return 0;
}

bool bytewise_less(const std::string& lhs, const std::string& rhs) noexcept {
    return compare_bytewise(lhs, rhs) < 0;
}

bool is_safe_group_selector_name(const std::string& group_name) {
    return is_valid_package_name(group_name);
}

void add_group_name(
        std::vector<std::string>& group_names,
        const std::optional<std::string>& group_name) {
    if(!group_name.has_value() ||
       !is_safe_group_selector_name(group_name.value())) {
        return;
    }
    if(std::find(group_names.begin(), group_names.end(), group_name.value()) ==
       group_names.end()) {
        group_names.push_back(group_name.value());
    }
}

void merge_group_names(
        std::vector<std::string>& destination,
        const std::vector<std::string>& source) {
    for(const auto& group_name : source) {
        if(std::find(destination.begin(), destination.end(), group_name) ==
           destination.end()) {
            destination.push_back(group_name);
        }
    }
}

std::map<std::string, std::size_t> repository_rank_by_name(
        const std::vector<std::string>& repository_order,
        std::vector<std::string>& duplicate_entries) {
    std::map<std::string, std::size_t> ranks;
    for(std::size_t rank = 0; rank < repository_order.size(); ++rank) {
        if(!ranks.emplace(repository_order[rank], rank).second) {
            duplicate_entries.push_back(repository_order[rank]);
        }
    }
    return ranks;
}

RootPackageSearchResult aggregate_root_package_search(
        const std::string& query,
        RepositoryPackageSearchSnapshot repository_snapshot,
        std::vector<AurPackageInfo> aur_packages) {
    InvalidRootPackageSearchSnapshot invalid;
    const std::map<std::string, std::size_t> repository_ranks =
            repository_rank_by_name(
                    repository_snapshot.repository_order,
                    invalid.duplicate_repository_order_entries);

    std::vector<PendingRootPackageSearchCandidate> pending;
    pending.reserve(repository_snapshot.matches.size() + aur_packages.size());

    for(auto& match : repository_snapshot.matches) {
        const RepositoryRootPackageIdentity raw_identity{
                match.repository_name, match.package_name};
        auto rank = repository_ranks.find(match.repository_name);
        if(rank == repository_ranks.end()) {
            invalid.unranked_repository_candidates.push_back(
                    raw_identity);
        }

        std::vector<std::string> group_names;
        switch(match.kind) {
        case RepositoryPackageSearchMatchKind::Search:
            if(match.group_name.has_value()) {
                invalid.invalid_group_matches.push_back(
                        InvalidRepositoryRootPackageGroupMatch{
                                raw_identity, match.group_name});
            }
            break;
        case RepositoryPackageSearchMatchKind::ExactGroup:
            if(!match.group_name.has_value() ||
               match.group_name.value() != query) {
                invalid.invalid_group_matches.push_back(
                        InvalidRepositoryRootPackageGroupMatch{
                                raw_identity, match.group_name});
            } else {
                add_group_name(group_names, match.group_name);
            }
            break;
        default:
            invalid.invalid_group_matches.push_back(
                    InvalidRepositoryRootPackageGroupMatch{
                            raw_identity, match.group_name});
            break;
        }

        RootPackageCandidateValidationResult candidate_result =
                make_repository_root_package_candidate(
                        std::move(match.repository_name),
                        std::move(match.package_name),
                        std::move(match.version),
                        std::move(match.description));
        if(const auto* failure = candidate_result.failure(); failure != nullptr) {
            invalid.validation_failures.push_back(*failure);
            continue;
        }

        pending.push_back(PendingRootPackageSearchCandidate{
                *candidate_result.candidate(), std::move(group_names)});
    }

    for(auto& package : aur_packages) {
        RootPackageCandidateValidationResult candidate_result =
                make_aur_root_package_candidate(
                        std::move(package.Name),
                        std::move(package.PackageBase),
                        std::move(package.Version),
                        std::move(package.Description));
        if(const auto* failure = candidate_result.failure(); failure != nullptr) {
            invalid.validation_failures.push_back(*failure);
            continue;
        }
        pending.push_back(PendingRootPackageSearchCandidate{
                *candidate_result.candidate(), {}});
    }

    if(!invalid.validation_failures.empty() ||
       !invalid.invalid_group_matches.empty() ||
       !invalid.duplicate_repository_order_entries.empty() ||
       !invalid.unranked_repository_candidates.empty()) {
        return invalid;
    }

    std::vector<RootPackageSearchCandidate> aggregated;
    aggregated.reserve(pending.size());
    for(auto& incoming : pending) {
        std::optional<std::size_t> duplicate_index;
        for(std::size_t index = 0; index < aggregated.size(); ++index) {
            RootPackageCandidatePairResult pair =
                    assess_root_package_candidate_pair(
                            aggregated[index].candidate,
                            incoming.candidate);
            if(const auto* pair_invalid = pair.invalid();
               pair_invalid != nullptr) {
                invalid.candidate_pair_issues.insert(
                        invalid.candidate_pair_issues.end(),
                        pair_invalid->issues.begin(),
                        pair_invalid->issues.end());
                return invalid;
            }
            if(pair.duplicate() != nullptr) {
                duplicate_index = index;
                aggregated[index].candidate = pair.duplicate()->candidate;
            }
        }

        if(duplicate_index.has_value()) {
            merge_group_names(
                    aggregated[duplicate_index.value()].selectable_group_names,
                    incoming.selectable_group_names);
        } else {
            aggregated.push_back(RootPackageSearchCandidate{
                    std::move(incoming.candidate),
                    std::move(incoming.selectable_group_names)});
        }
    }

    for(auto& entry : aggregated) {
        std::sort(
                entry.selectable_group_names.begin(),
                entry.selectable_group_names.end(), bytewise_less);
    }

    std::sort(
            aggregated.begin(), aggregated.end(),
            [&repository_ranks](
                    const RootPackageSearchCandidate& lhs,
                    const RootPackageSearchCandidate& rhs) {
                int package_comparison = compare_bytewise(
                        lhs.candidate.package_name(),
                        rhs.candidate.package_name());
                if(package_comparison != 0) return package_comparison < 0;

                if(lhs.candidate.source_kind() != rhs.candidate.source_kind()) {
                    return lhs.candidate.source_kind() ==
                           RootPackageSourceKind::Repository;
                }

                if(lhs.candidate.source_kind() ==
                   RootPackageSourceKind::Repository) {
                    const auto& lhs_identity =
                            std::get<RepositoryRootPackageIdentity>(
                                    lhs.candidate.identity());
                    const auto& rhs_identity =
                            std::get<RepositoryRootPackageIdentity>(
                                    rhs.candidate.identity());
                    const std::size_t lhs_rank =
                            repository_ranks.at(lhs_identity.repository_name);
                    const std::size_t rhs_rank =
                            repository_ranks.at(rhs_identity.repository_name);
                    if(lhs_rank != rhs_rank) return lhs_rank < rhs_rank;
                    return bytewise_less(
                            lhs_identity.repository_name,
                            rhs_identity.repository_name);
                }

                const auto& lhs_identity =
                        std::get<AurRootPackageIdentity>(
                                lhs.candidate.identity());
                const auto& rhs_identity =
                        std::get<AurRootPackageIdentity>(
                                rhs.candidate.identity());
                return bytewise_less(
                        lhs_identity.package_base,
                        rhs_identity.package_base);
            });

    return RootPackageSearchSnapshot{std::move(aggregated)};
}

} // namespace

RootPackageSearchResult search_root_package_candidates(
        const std::string& query,
        RootPackageSearchScope scope) {
    bool query_repository = false;
    bool query_aur = false;
    switch(scope) {
    case RootPackageSearchScope::All:
        query_repository = true;
        query_aur = true;
        break;
    case RootPackageSearchScope::Repository:
        query_repository = true;
        break;
    case RootPackageSearchScope::Aur:
        query_aur = true;
        break;
    default:
        throw std::invalid_argument("Unknown root package search scope.");
    }

    RepositoryPackageSearchSnapshot repository_snapshot;
    if(query_repository) {
        RepositoryPackageSearchResult repository_result =
                query_repository_root_package_search(query);
        if(const auto* failure =
                   std::get_if<PackageMetadataFailure>(&repository_result);
           failure != nullptr) {
            return RepositoryRootPackageSearchFailure{*failure};
        }
        repository_snapshot = std::get<RepositoryPackageSearchSnapshot>(
                std::move(repository_result));
    }

    std::vector<AurPackageInfo> aur_packages;
    if(query_aur) {
        try {
            aur_packages = AurClient::search_strict(query);
        } catch(const std::runtime_error& error) {
            return AurRootPackageSearchFailure{error.what()};
        }
    }

    return aggregate_root_package_search(
            query, std::move(repository_snapshot), std::move(aur_packages));
}
