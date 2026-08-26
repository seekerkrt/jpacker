#include "vcs_source_identity.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
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

bool is_valid_nonempty_text(std::string_view value) noexcept {
    if(value.empty()) return false;

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

bool contains_ascii_whitespace(std::string_view value) noexcept {
    return std::any_of(value.begin(), value.end(), [](char character) {
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
    });
}

bool is_valid_token(std::string_view value) noexcept {
    return is_valid_nonempty_text(value) &&
           !contains_ascii_whitespace(value);
}

void require_vcs_kind(VcsKind kind) {
    switch(kind) {
    case VcsKind::Git:
    case VcsKind::Svn:
    case VcsKind::Hg:
    case VcsKind::Bzr:
    case VcsKind::Cvs:
    case VcsKind::Darcs:
        return;
    }
    throw std::invalid_argument("VCS kind is invalid.");
}

void require_selector_value(
        const std::string& value, std::string_view selector_name) {
    if(!is_valid_nonempty_text(value)) {
        throw std::invalid_argument(
                std::string(selector_name) +
                " must be nonempty single-line UTF-8.");
    }
}

} // namespace

VcsSelector::VcsSelector(
        VcsSelectorKind kind,
        VcsSelectorTrackingBehavior tracking_behavior,
        std::optional<std::string> value) noexcept
    : kind_(kind), tracking_behavior_(tracking_behavior),
      value_(std::move(value)) {}

VcsSelector VcsSelector::default_head() noexcept {
    return VcsSelector(
            VcsSelectorKind::DefaultHead,
            VcsSelectorTrackingBehavior::Floating,
            std::nullopt);
}

VcsSelector VcsSelector::branch(std::string branch_name) {
    require_selector_value(branch_name, "VCS branch selector");
    return VcsSelector(
            VcsSelectorKind::Branch,
            VcsSelectorTrackingBehavior::Floating,
            std::move(branch_name));
}

VcsSelector VcsSelector::fixed_revision(std::string revision) {
    require_selector_value(revision, "Fixed VCS revision selector");
    return VcsSelector(
            VcsSelectorKind::FixedRevision,
            VcsSelectorTrackingBehavior::Fixed,
            std::move(revision));
}

VcsSelector VcsSelector::tag(std::string tag_name) {
    require_selector_value(tag_name, "VCS tag selector");
    return VcsSelector(
            VcsSelectorKind::Tag,
            VcsSelectorTrackingBehavior::Fixed,
            std::move(tag_name));
}

VcsSelector VcsSelector::unsupported(std::string selector) {
    require_selector_value(selector, "Unsupported VCS selector");
    return VcsSelector(
            VcsSelectorKind::Unsupported,
            VcsSelectorTrackingBehavior::Indeterminate,
            std::move(selector));
}

VcsSelector VcsSelector::unrecognized(std::string selector) {
    require_selector_value(selector, "Unrecognized VCS selector");
    return VcsSelector(
            VcsSelectorKind::Unrecognized,
            VcsSelectorTrackingBehavior::Indeterminate,
            std::move(selector));
}

VcsSelectorKind VcsSelector::kind() const noexcept {
    return kind_;
}

VcsSelectorTrackingBehavior VcsSelector::tracking_behavior() const noexcept {
    return tracking_behavior_;
}

const std::string* VcsSelector::value() const noexcept {
    return value_.has_value() ? &value_.value() : nullptr;
}

VcsSourceIdentity::VcsSourceIdentity(
        VcsKind kind,
        std::string source_location,
        VcsSelector selector,
        std::optional<std::string> architecture) noexcept
    : kind_(kind), source_location_(std::move(source_location)),
      selector_(std::move(selector)),
      architecture_(std::move(architecture)) {}

VcsSourceIdentity VcsSourceIdentity::make(
        VcsKind kind,
        std::string source_location,
        VcsSelector selector,
        std::optional<std::string> architecture) {
    require_vcs_kind(kind);
    if(!is_valid_token(source_location)) {
        throw std::invalid_argument(
                "VCS source location must be nonempty single-line UTF-8 without whitespace.");
    }
    if(architecture.has_value() && !is_valid_token(*architecture)) {
        throw std::invalid_argument(
                "VCS source architecture must be nonempty single-line UTF-8 without whitespace.");
    }
    return VcsSourceIdentity(
            kind,
            std::move(source_location),
            std::move(selector),
            std::move(architecture));
}

VcsKind VcsSourceIdentity::kind() const noexcept {
    return kind_;
}

const std::string& VcsSourceIdentity::source_location() const noexcept {
    return source_location_;
}

const VcsSelector& VcsSourceIdentity::selector() const noexcept {
    return selector_;
}

const std::string* VcsSourceIdentity::architecture() const noexcept {
    return architecture_.has_value() ? &architecture_.value() : nullptr;
}

AurRecipeRevision::AurRecipeRevision(SourceRevisionIdentity value) noexcept
    : value_(std::move(value)) {}

AurRecipeRevision AurRecipeRevision::git_commit(std::string object_id) {
    return AurRecipeRevision(
            SourceRevisionIdentity::git_commit(std::move(object_id)));
}

const SourceRevisionIdentity& AurRecipeRevision::value() const noexcept {
    return value_;
}

UpstreamGitRevision::UpstreamGitRevision(
        VcsSourceIdentity source,
        SourceRevisionIdentity value) noexcept
    : source_(std::move(source)), value_(std::move(value)) {}

UpstreamGitRevision UpstreamGitRevision::git_commit(
        VcsSourceIdentity source, std::string object_id) {
    if(source.kind() != VcsKind::Git) {
        throw std::invalid_argument(
                "Upstream Git revision requires a Git VCS source identity.");
    }
    return UpstreamGitRevision(
            std::move(source),
            SourceRevisionIdentity::git_commit(std::move(object_id)));
}

const VcsSourceIdentity& UpstreamGitRevision::source() const noexcept {
    return source_;
}

const SourceRevisionIdentity& UpstreamGitRevision::value() const noexcept {
    return value_;
}
