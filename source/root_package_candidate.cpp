#include "root_package_candidate.hpp"

#include "package_identifier.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace {

bool is_continuation_byte(unsigned char byte) noexcept {
    return byte >= 0x80 && byte <= 0xbf;
}

bool decode_utf8_code_point(
    std::string_view value, std::size_t offset,
    std::uint32_t& code_point, std::size_t& length) noexcept {
    const auto byte_at = [&value](std::size_t index) {
        return static_cast<unsigned char>(value[index]);
    };

    const unsigned char first = byte_at(offset);
    if(first <= 0x7f) {
        code_point = first;
        length = 1;
        return true;
    }

    if(first >= 0xc2 && first <= 0xdf) {
        if(offset + 1 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        if(!is_continuation_byte(second)) return false;
        code_point =
            (static_cast<std::uint32_t>(first & 0x1f) << 6) |
            static_cast<std::uint32_t>(second & 0x3f);
        length = 2;
        return true;
    }

    if(first >= 0xe0 && first <= 0xef) {
        if(offset + 2 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const bool valid_second =
            first == 0xe0 ? second >= 0xa0 && second <= 0xbf
            : first == 0xed
                ? second >= 0x80 && second <= 0x9f
                : is_continuation_byte(second);
        if(!valid_second || !is_continuation_byte(third)) return false;
        code_point =
            (static_cast<std::uint32_t>(first & 0x0f) << 12) |
            (static_cast<std::uint32_t>(second & 0x3f) << 6) |
            static_cast<std::uint32_t>(third & 0x3f);
        length = 3;
        return true;
    }

    if(first >= 0xf0 && first <= 0xf4) {
        if(offset + 3 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const unsigned char fourth = byte_at(offset + 3);
        const bool valid_second =
            first == 0xf0 ? second >= 0x90 && second <= 0xbf
            : first == 0xf4
                ? second >= 0x80 && second <= 0x8f
                : is_continuation_byte(second);
        if(!valid_second || !is_continuation_byte(third) ||
           !is_continuation_byte(fourth)) {
            return false;
        }
        code_point =
            (static_cast<std::uint32_t>(first & 0x07) << 18) |
            (static_cast<std::uint32_t>(second & 0x3f) << 12) |
            (static_cast<std::uint32_t>(third & 0x3f) << 6) |
            static_cast<std::uint32_t>(fourth & 0x3f);
        length = 4;
        return true;
    }

    return false;
}

bool is_single_line_code_point(std::uint32_t code_point) noexcept {
    return code_point > 0x1f &&
           !(code_point >= 0x7f && code_point <= 0x9f) &&
           code_point != 0x2028 && code_point != 0x2029;
}

bool is_valid_single_line_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while(offset < value.size()) {
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        if(!decode_utf8_code_point(value, offset, code_point, length) ||
           !is_single_line_code_point(code_point)) {
            return false;
        }
        offset += length;
    }
    return true;
}

bool is_valid_root_package_name(const std::string& value) {
    return is_valid_single_line_utf8(value) && is_valid_package_name(value);
}

bool contains_control_character(std::string_view value) noexcept {
    for(unsigned char character : value) {
        if(std::iscntrl(character)) return true;
    }
    return false;
}

bool is_valid_root_repository_name(const std::string& value) noexcept {
    // read-only libalpm sessionへ渡すconfigured repository nameと同じ契約を使う。
    return !value.empty() && !contains_control_character(value);
}

std::optional<std::string> normalize_presentation_value(
    std::optional<std::string> value) {
    if(value.has_value() && value->empty()) return std::nullopt;
    return value;
}

void append_presentation_validation_issues(
    const RootPackageCandidatePresentation& presentation,
    std::vector<RootPackageCandidateValidationIssue>& issues) {
    if(presentation.version.has_value() &&
       !is_valid_single_line_utf8(presentation.version.value())) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidVersion,
            presentation.version.value()});
    }
    if(presentation.description.has_value() &&
       !is_valid_single_line_utf8(presentation.description.value())) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidDescription,
            presentation.description.value()});
    }
}

RootPackageCandidatePresentation normalize_presentation(
    std::optional<std::string> version,
    std::optional<std::string> description) {
    return RootPackageCandidatePresentation{
        normalize_presentation_value(std::move(version)),
        normalize_presentation_value(std::move(description))};
}

std::optional<std::string> merge_presentation_value(
    const std::optional<std::string>& lhs,
    const std::optional<std::string>& rhs) {
    if(lhs.has_value()) return lhs;
    return rhs;
}

void append_metadata_conflict(
    const RootPackageCandidate& lhs,
    RootPackageCandidateMetadataField field,
    const std::optional<std::string>& lhs_value,
    const std::optional<std::string>& rhs_value,
    std::vector<RootPackageCandidatePairIssue>& issues) {
    if(!lhs_value.has_value() || !rhs_value.has_value() ||
       lhs_value == rhs_value) {
        return;
    }
    issues.push_back(ConflictingRootPackageCandidateMetadata{
        lhs.identity(), field, lhs_value.value(), rhs_value.value()});
}

} // namespace

RootPackageSourceKind root_package_source_kind(
    const RootPackageIdentity& identity) noexcept {
    return std::holds_alternative<RepositoryRootPackageIdentity>(identity)
               ? RootPackageSourceKind::Repository
               : RootPackageSourceKind::Aur;
}

const std::string& root_package_name(
    const RootPackageIdentity& identity) noexcept {
    return std::visit(
        [](const auto& source_identity) -> const std::string& {
            return source_identity.package_name;
        },
        identity);
}

bool same_root_package_identity(
    const RootPackageIdentity& lhs,
    const RootPackageIdentity& rhs) noexcept {
    return lhs == rhs;
}

RootPackageCandidate::RootPackageCandidate(
    RootPackageIdentity identity,
    RootPackageCandidatePresentation presentation) noexcept
    : identity_(std::move(identity)), presentation_(std::move(presentation)) {
}

RootPackageSourceKind RootPackageCandidate::source_kind() const noexcept {
    return root_package_source_kind(identity_);
}

RootPackageTargetRole RootPackageCandidate::target_role() const noexcept {
    return target_role_;
}

const RootPackageIdentity& RootPackageCandidate::identity() const noexcept {
    return identity_;
}

const std::string& RootPackageCandidate::package_name() const noexcept {
    return root_package_name(identity_);
}

const RootPackageCandidatePresentation&
RootPackageCandidate::presentation() const noexcept {
    return presentation_;
}

RootPackageCandidateValidationResult::RootPackageCandidateValidationResult(
    RootPackageCandidate candidate) noexcept
    : outcome_(std::move(candidate)) {
}

RootPackageCandidateValidationResult::RootPackageCandidateValidationResult(
    RootPackageCandidateValidationFailure failure) noexcept
    : outcome_(std::move(failure)) {
}

bool RootPackageCandidateValidationResult::is_valid() const noexcept {
    return std::holds_alternative<RootPackageCandidate>(outcome_);
}

const RootPackageCandidate*
RootPackageCandidateValidationResult::candidate() const noexcept {
    return std::get_if<RootPackageCandidate>(&outcome_);
}

const RootPackageCandidateValidationFailure*
RootPackageCandidateValidationResult::failure() const noexcept {
    return std::get_if<RootPackageCandidateValidationFailure>(&outcome_);
}

RootPackageCandidateValidationResult make_repository_root_package_candidate(
    std::string repository_name, std::string package_name,
    std::optional<std::string> version,
    std::optional<std::string> description) {
    RootPackageCandidatePresentation presentation =
        normalize_presentation(
            std::move(version), std::move(description));
    std::vector<RootPackageCandidateValidationIssue> issues;
    if(!is_valid_root_repository_name(repository_name)) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidRepositoryName,
            repository_name});
    }
    if(!is_valid_root_package_name(package_name)) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidPackageName,
            package_name});
    }
    append_presentation_validation_issues(presentation, issues);

    if(!issues.empty()) {
        return RootPackageCandidateValidationResult(
            RootPackageCandidateValidationFailure{
                RootPackageSourceKind::Repository,
                std::move(issues)});
    }
    return RootPackageCandidateValidationResult(RootPackageCandidate(
        RepositoryRootPackageIdentity{
            std::move(repository_name), std::move(package_name)},
        std::move(presentation)));
}

RootPackageCandidateValidationResult make_aur_root_package_candidate(
    std::string package_name, std::string package_base,
    std::optional<std::string> version,
    std::optional<std::string> description) {
    RootPackageCandidatePresentation presentation =
        normalize_presentation(
            std::move(version), std::move(description));
    std::vector<RootPackageCandidateValidationIssue> issues;
    if(!is_valid_root_package_name(package_name)) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidPackageName,
            package_name});
    }
    if(!is_valid_root_package_name(package_base)) {
        issues.push_back(RootPackageCandidateValidationIssue{
            RootPackageCandidateValidationIssueKind::InvalidPackageBase,
            package_base});
    }
    append_presentation_validation_issues(presentation, issues);

    if(!issues.empty()) {
        return RootPackageCandidateValidationResult(
            RootPackageCandidateValidationFailure{
                RootPackageSourceKind::Aur, std::move(issues)});
    }
    return RootPackageCandidateValidationResult(RootPackageCandidate(
        AurRootPackageIdentity{
            std::move(package_name), std::move(package_base)},
        std::move(presentation)));
}

SelectedRootPackageTarget::SelectedRootPackageTarget(
    RootPackageIdentity identity) noexcept
    : identity_(std::move(identity)) {
}

RootPackageSourceKind SelectedRootPackageTarget::source_kind() const noexcept {
    return root_package_source_kind(identity_);
}

RootPackageTargetRole SelectedRootPackageTarget::target_role() const noexcept {
    return target_role_;
}

const RootPackageIdentity&
SelectedRootPackageTarget::identity() const noexcept {
    return identity_;
}

const std::string& SelectedRootPackageTarget::package_name() const noexcept {
    return root_package_name(identity_);
}

SelectedRootPackageTarget select_root_package_target(
    const RootPackageCandidate& candidate) {
    return SelectedRootPackageTarget(candidate.identity());
}

RootPackageCandidatePairResult::RootPackageCandidatePairResult(
    DistinctRootPackageCandidates distinct) noexcept
    : outcome_(std::move(distinct)) {
}

RootPackageCandidatePairResult::RootPackageCandidatePairResult(
    DuplicateRootPackageCandidate duplicate) noexcept
    : outcome_(std::move(duplicate)) {
}

RootPackageCandidatePairResult::RootPackageCandidatePairResult(
    InvalidRootPackageCandidatePair invalid) noexcept
    : outcome_(std::move(invalid)) {
}

bool RootPackageCandidatePairResult::is_distinct() const noexcept {
    return std::holds_alternative<DistinctRootPackageCandidates>(outcome_);
}

bool RootPackageCandidatePairResult::is_duplicate() const noexcept {
    return std::holds_alternative<DuplicateRootPackageCandidate>(outcome_);
}

bool RootPackageCandidatePairResult::is_invalid() const noexcept {
    return std::holds_alternative<InvalidRootPackageCandidatePair>(outcome_);
}

const DistinctRootPackageCandidates*
RootPackageCandidatePairResult::distinct() const noexcept {
    return std::get_if<DistinctRootPackageCandidates>(&outcome_);
}

const DuplicateRootPackageCandidate*
RootPackageCandidatePairResult::duplicate() const noexcept {
    return std::get_if<DuplicateRootPackageCandidate>(&outcome_);
}

const InvalidRootPackageCandidatePair*
RootPackageCandidatePairResult::invalid() const noexcept {
    return std::get_if<InvalidRootPackageCandidatePair>(&outcome_);
}

RootPackageCandidatePairResult assess_root_package_candidate_pair(
    const RootPackageCandidate& lhs,
    const RootPackageCandidate& rhs) {
    const auto* lhs_aur = std::get_if<AurRootPackageIdentity>(&lhs.identity());
    const auto* rhs_aur = std::get_if<AurRootPackageIdentity>(&rhs.identity());
    if(lhs_aur != nullptr && rhs_aur != nullptr &&
       lhs_aur->package_name == rhs_aur->package_name &&
       lhs_aur->package_base != rhs_aur->package_base) {
        return RootPackageCandidatePairResult(
            InvalidRootPackageCandidatePair{{InconsistentAurRootPackageBase{
                lhs_aur->package_name,
                lhs_aur->package_base,
                rhs_aur->package_base}}});
    }

    if(!same_root_package_identity(lhs.identity(), rhs.identity())) {
        return RootPackageCandidatePairResult(
            DistinctRootPackageCandidates{});
    }

    std::vector<RootPackageCandidatePairIssue> issues;
    append_metadata_conflict(
        lhs, RootPackageCandidateMetadataField::Version,
        lhs.presentation().version, rhs.presentation().version, issues);
    append_metadata_conflict(
        lhs, RootPackageCandidateMetadataField::Description,
        lhs.presentation().description,
        rhs.presentation().description, issues);
    if(!issues.empty()) {
        return RootPackageCandidatePairResult(
            InvalidRootPackageCandidatePair{std::move(issues)});
    }

    return RootPackageCandidatePairResult(DuplicateRootPackageCandidate{
        RootPackageCandidate(
            lhs.identity(),
            RootPackageCandidatePresentation{
                merge_presentation_value(
                    lhs.presentation().version,
                    rhs.presentation().version),
                merge_presentation_value(
                    lhs.presentation().description,
                    rhs.presentation().description)})});
}
