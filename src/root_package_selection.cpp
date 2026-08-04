#include "root_package_selection.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <istream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

bool is_ascii_whitespace(char character) noexcept {
    switch(character) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v':
        return true;
    default:
        return false;
    }
}

std::string_view trim_ascii_whitespace(std::string_view value) noexcept {
    while(!value.empty() && is_ascii_whitespace(value.front())) {
        value.remove_prefix(1);
    }
    while(!value.empty() && is_ascii_whitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::vector<std::string_view> split_ascii_whitespace(
        std::string_view value) {
    std::vector<std::string_view> tokens;
    std::size_t                   offset = 0;
    while(offset < value.size()) {
        while(offset < value.size() && is_ascii_whitespace(value[offset])) {
            ++offset;
        }
        if(offset == value.size()) break;

        const std::size_t begin = offset;
        while(offset < value.size() && !is_ascii_whitespace(value[offset])) {
            ++offset;
        }
        tokens.push_back(value.substr(begin, offset - begin));
    }
    return tokens;
}

std::string ascii_lower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for(unsigned char character : value) {
        if(character >= 'A' && character <= 'Z') {
            lowered.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            lowered.push_back(static_cast<char>(character));
        }
    }
    return lowered;
}

bool is_cancel_token(std::string_view value) {
    const std::string lowered = ascii_lower(value);
    return lowered == "q" || lowered == "quit" || lowered == "cancel";
}

enum class CandidateIndexParseStatus {
    Valid,
    Malformed,
    OutOfRange
};

struct CandidateIndexParseResult {
    CandidateIndexParseStatus status;
    std::size_t               value = 0;
};

CandidateIndexParseResult parse_candidate_index(
        std::string_view value,
        std::size_t candidate_count) {
    if(value.empty()) {
        return CandidateIndexParseResult{
                CandidateIndexParseStatus::Malformed};
    }

    std::size_t selected = 0;
    const char* first = value.data();
    const char* last = first + value.size();
    const auto [end, error] = std::from_chars(first, last, selected);
    if(error == std::errc::result_out_of_range) {
        return CandidateIndexParseResult{
                CandidateIndexParseStatus::OutOfRange};
    }
    if(error != std::errc{} || end != last) {
        return CandidateIndexParseResult{
                CandidateIndexParseStatus::Malformed};
    }
    if(selected == 0 || selected > candidate_count) {
        return CandidateIndexParseResult{
                CandidateIndexParseStatus::OutOfRange};
    }
    return CandidateIndexParseResult{
            CandidateIndexParseStatus::Valid, selected};
}

void append_index_issue(
        const CandidateIndexParseResult& result,
        std::string_view token,
        std::size_t candidate_count,
        std::vector<RootPackageSelectionIssue>& issues) {
    switch(result.status) {
    case CandidateIndexParseStatus::Malformed:
        issues.push_back(MalformedRootPackageSelectionToken{
                std::string(token)});
        break;
    case CandidateIndexParseStatus::OutOfRange:
        issues.push_back(RootPackageSelectionIndexOutOfRange{
                std::string(token), candidate_count});
        break;
    case CandidateIndexParseStatus::Valid:
        break;
    }
}

void select_group_members(
        std::string_view token,
        const RootPackageSearchSnapshot& snapshot,
        std::vector<bool>& selected,
        std::vector<RootPackageSelectionIssue>& issues) {
    const std::string group_name(token.substr(1));
    if(!is_valid_package_name(group_name)) {
        issues.push_back(MalformedRootPackageSelectionToken{
                std::string(token)});
        return;
    }

    bool found = false;
    for(std::size_t index = 0; index < snapshot.candidates.size(); ++index) {
        if(snapshot.candidates[index].candidate.source_kind() !=
           RootPackageSourceKind::Repository) {
            continue;
        }
        const auto& group_names =
                snapshot.candidates[index].selectable_group_names;
        if(std::find(group_names.begin(), group_names.end(), group_name) !=
           group_names.end()) {
            selected[index] = true;
            found = true;
        }
    }
    if(!found) {
        issues.push_back(UnknownRootPackageSelectionGroup{
                group_name});
    }
}

void select_range(
        std::string_view token,
        std::size_t separator,
        std::size_t candidate_count,
        std::vector<bool>& selected,
        std::vector<RootPackageSelectionIssue>& issues) {
    const CandidateIndexParseResult first = parse_candidate_index(
            token.substr(0, separator), candidate_count);
    const CandidateIndexParseResult last = parse_candidate_index(
            token.substr(separator + 1), candidate_count);

    if(first.status == CandidateIndexParseStatus::Malformed ||
       last.status == CandidateIndexParseStatus::Malformed) {
        issues.push_back(MalformedRootPackageSelectionToken{
                std::string(token)});
        return;
    }
    if(first.status == CandidateIndexParseStatus::OutOfRange ||
       last.status == CandidateIndexParseStatus::OutOfRange) {
        issues.push_back(RootPackageSelectionIndexOutOfRange{
                std::string(token), candidate_count});
        return;
    }
    if(first.value > last.value) {
        issues.push_back(DescendingRootPackageSelectionRange{
                std::string(token)});
        return;
    }

    for(std::size_t index = first.value; index < last.value; ++index) {
        selected[index - 1] = true;
    }
    selected[last.value - 1] = true;
}

struct SelectedPackageIdentitySet {
    std::string                      package_name;
    std::vector<RootPackageIdentity> identities;
};

} // namespace

RootPackageSelection::RootPackageSelection(
        std::vector<SelectedRootPackageTarget> targets) noexcept
    : targets_(std::move(targets)) {}

const std::vector<SelectedRootPackageTarget>&
RootPackageSelection::targets() const noexcept {
    return targets_;
}

RootPackageSelectionExpressionResult parse_root_package_selection(
        std::string input,
        const RootPackageSearchSnapshot& snapshot) {
    const std::string_view trimmed = trim_ascii_whitespace(input);
    if(trimmed.empty()) {
        return CancelledRootPackageSelection{
                RootPackageSelectionCancellationReason::EmptyInput};
    }
    if(is_cancel_token(trimmed)) {
        return CancelledRootPackageSelection{
                RootPackageSelectionCancellationReason::CancelToken};
    }

    const std::vector<std::string_view> tokens =
            split_ascii_whitespace(trimmed);
    std::vector<bool> selected(snapshot.candidates.size(), false);
    std::vector<RootPackageSelectionIssue> issues;

    for(const std::string_view token : tokens) {
        if(is_cancel_token(token)) {
            issues.push_back(MixedRootPackageSelectionCancellationToken{
                    std::string(token)});
            continue;
        }
        if(!token.empty() && token.front() == '@') {
            select_group_members(token, snapshot, selected, issues);
            continue;
        }

        const std::size_t separator = token.find('-');
        if(separator != std::string_view::npos) {
            if(separator == 0 || separator + 1 == token.size() ||
               token.find('-', separator + 1) != std::string_view::npos) {
                issues.push_back(MalformedRootPackageSelectionToken{
                        std::string(token)});
                continue;
            }
            select_range(
                    token, separator, snapshot.candidates.size(),
                    selected, issues);
            continue;
        }

        const CandidateIndexParseResult index = parse_candidate_index(
                token, snapshot.candidates.size());
        append_index_issue(
                index, token, snapshot.candidates.size(), issues);
        if(index.status == CandidateIndexParseStatus::Valid) {
            selected[index.value - 1] = true;
        }
    }

    if(!issues.empty()) {
        return InvalidRootPackageSelection{std::move(issues)};
    }

    std::vector<SelectedRootPackageTarget> selected_targets;
    std::vector<SelectedPackageIdentitySet> identities_by_package;
    for(std::size_t index = 0; index < snapshot.candidates.size(); ++index) {
        if(!selected[index]) continue;
        const RootPackageCandidate& candidate =
                snapshot.candidates[index].candidate;

        auto duplicate = std::find_if(
                selected_targets.begin(), selected_targets.end(),
                [&candidate](const SelectedRootPackageTarget& target) {
                    return same_root_package_identity(
                            candidate.identity(), target.identity());
                });
        if(duplicate != selected_targets.end()) continue;

        auto package = std::find_if(
                identities_by_package.begin(), identities_by_package.end(),
                [&candidate](const SelectedPackageIdentitySet& entry) {
                    return entry.package_name == candidate.package_name();
                });
        if(package == identities_by_package.end()) {
            identities_by_package.push_back(SelectedPackageIdentitySet{
                    candidate.package_name(), {candidate.identity()}});
        } else {
            package->identities.push_back(candidate.identity());
        }
        selected_targets.push_back(select_root_package_target(candidate));
    }

    for(auto& package : identities_by_package) {
        if(package.identities.size() > 1) {
            issues.push_back(ConflictingRootPackageSelectionAlternatives{
                    std::move(package.package_name),
                    std::move(package.identities)});
        }
    }
    if(!issues.empty()) {
        return InvalidRootPackageSelection{std::move(issues)};
    }

    // non-cancel expressionは少なくとも1 selectorを持ち、valid selectorは
    // 1件以上のdisplayed candidateへ解決される。
    return RootPackageSelection(std::move(selected_targets));
}

RootPackageSelectionSession::RootPackageSelectionSession(
        std::istream& input,
        RootPackageSelectionInteractionCallback interaction,
        RootPackageSelectionInputGate input_gate)
    : input_(&input),
      interaction_(std::move(interaction)),
      input_gate_(input_gate) {
    switch(input_gate_) {
    case RootPackageSelectionInputGate::Interactive:
    case RootPackageSelectionInputGate::NonTty:
    case RootPackageSelectionInputGate::NoConfirm:
        break;
    default:
        throw std::invalid_argument(
                "Unknown root package selection input gate.");
    }
}

RootPackageSelectionSessionResult RootPackageSelectionSession::select(
        const RootPackageSearchSnapshot& snapshot) {
    if(input_gate_ == RootPackageSelectionInputGate::NoConfirm) {
        return UnavailableRootPackageSelection{
                RootPackageSelectionUnavailableReason::NoConfirm};
    }
    if(input_gate_ == RootPackageSelectionInputGate::NonTty) {
        return UnavailableRootPackageSelection{
                RootPackageSelectionUnavailableReason::NonInteractiveInput};
    }
    if(snapshot.candidates.empty()) {
        return UnavailableRootPackageSelection{
                RootPackageSelectionUnavailableReason::NoCandidates};
    }

    if(interaction_) {
        interaction_(
                PresentRootPackageSelectionCandidates{}, snapshot);
    }
    for(;;) {
        if(interaction_) {
            interaction_(PromptForRootPackageSelection{}, snapshot);
        }

        std::string input;
        if(!std::getline(*input_, input)) {
            return CancelledRootPackageSelection{
                    RootPackageSelectionCancellationReason::EndOfInput};
        }

        RootPackageSelectionExpressionResult result =
                parse_root_package_selection(
                        std::move(input), snapshot);
        if(auto* selection = std::get_if<RootPackageSelection>(&result);
           selection != nullptr) {
            return std::move(*selection);
        }
        if(auto* cancelled =
                   std::get_if<CancelledRootPackageSelection>(&result);
           cancelled != nullptr) {
            return *cancelled;
        }

        if(interaction_) {
            interaction_(
                    InvalidRootPackageSelectionAttempt{
                            std::move(std::get<InvalidRootPackageSelection>(
                                    result))},
                    snapshot);
        }
    }
}

bool RootPackageSelectionSession::is_interactive() const noexcept {
    return input_gate_ == RootPackageSelectionInputGate::Interactive;
}

RootPackageSelectionInputGate
RootPackageSelectionSession::input_gate() const noexcept {
    return input_gate_;
}

RootPackageSelectionSession make_root_package_selection_session(
        RootPackageSelectionInteractionCallback interaction,
        bool no_confirm) {
    const RootPackageSelectionInputGate input_gate =
            no_confirm
                    ? RootPackageSelectionInputGate::NoConfirm
                    : isatty(STDIN_FILENO) != 0
                            ? RootPackageSelectionInputGate::Interactive
                            : RootPackageSelectionInputGate::NonTty;
    return RootPackageSelectionSession(
            std::cin, std::move(interaction), input_gate);
}
