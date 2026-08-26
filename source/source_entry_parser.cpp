#include "source_entry_parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct ExplicitVcsPrefix {
    std::string_view identifier;
    std::string_view remainder;
};

constexpr bool is_ascii_lower(char character) noexcept {
    return character >= 'a' && character <= 'z';
}

constexpr bool is_ascii_upper(char character) noexcept {
    return character >= 'A' && character <= 'Z';
}

constexpr bool is_ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

constexpr bool is_uri_scheme_character(char character) noexcept {
    return is_ascii_lower(character) || is_ascii_upper(character) ||
            is_ascii_digit(character) || character == '+' ||
            character == '-' || character == '.';
}

bool is_utf8_continuation_byte(unsigned char byte) noexcept {
    return byte >= 0x80U && byte <= 0xbfU;
}

bool decode_utf8_code_point(
        std::string_view value, std::size_t offset,
        std::uint32_t& code_point, std::size_t& length) noexcept {
    const auto byte_at = [&value](std::size_t index) {
        return static_cast<unsigned char>(value[index]);
    };

    const unsigned char first = byte_at(offset);
    if(first <= 0x7fU) {
        code_point = first;
        length = 1;
        return true;
    }
    if(first >= 0xc2U && first <= 0xdfU) {
        if(offset + 1 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        if(!is_utf8_continuation_byte(second)) return false;
        code_point =
                (static_cast<std::uint32_t>(first & 0x1fU) << 6) |
                static_cast<std::uint32_t>(second & 0x3fU);
        length = 2;
        return true;
    }
    if(first >= 0xe0U && first <= 0xefU) {
        if(offset + 2 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const bool valid_second = first == 0xe0U
                ? second >= 0xa0U && second <= 0xbfU
                : first == 0xedU
                ? second >= 0x80U && second <= 0x9fU
                : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third)) return false;
        code_point =
                (static_cast<std::uint32_t>(first & 0x0fU) << 12) |
                (static_cast<std::uint32_t>(second & 0x3fU) << 6) |
                static_cast<std::uint32_t>(third & 0x3fU);
        length = 3;
        return true;
    }
    if(first >= 0xf0U && first <= 0xf4U) {
        if(offset + 3 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const unsigned char fourth = byte_at(offset + 3);
        const bool valid_second = first == 0xf0U
                ? second >= 0x90U && second <= 0xbfU
                : first == 0xf4U
                ? second >= 0x80U && second <= 0x8fU
                : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third) ||
           !is_utf8_continuation_byte(fourth)) {
            return false;
        }
        code_point =
                (static_cast<std::uint32_t>(first & 0x07U) << 18) |
                (static_cast<std::uint32_t>(second & 0x3fU) << 12) |
                (static_cast<std::uint32_t>(third & 0x3fU) << 6) |
                static_cast<std::uint32_t>(fourth & 0x3fU);
        length = 4;
        return true;
    }
    return false;
}

bool is_single_line_code_point(std::uint32_t code_point) noexcept {
    return (code_point == 0x09U || code_point > 0x1fU) &&
            !(code_point >= 0x7fU && code_point <= 0x9fU) &&
            code_point != 0x2028U && code_point != 0x2029U;
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

bool has_ascii_edge_whitespace(std::string_view value) noexcept {
    if(value.empty()) return false;
    const auto is_ascii_whitespace = [](char character) {
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
    };
    return is_ascii_whitespace(value.front()) ||
            is_ascii_whitespace(value.back());
}

std::optional<std::string_view> parse_uri_scheme(
        std::string_view value) noexcept {
    const std::size_t separator = value.find("://");
    if(separator == std::string_view::npos || separator == 0 ||
       separator + 3 >= value.size()) {
        return std::nullopt;
    }
    if(!is_ascii_lower(value.front()) && !is_ascii_upper(value.front())) {
        return std::nullopt;
    }
    if(!std::all_of(
               value.begin() + 1, value.begin() + separator,
               is_uri_scheme_character)) {
        return std::nullopt;
    }
    return value.substr(0, separator);
}

std::optional<ParsedSourceVcsKind> parse_known_vcs_kind(
        std::string_view identifier) noexcept {
    if(identifier == "bzr") return ParsedSourceVcsKind::Bzr;
    if(identifier == "fossil") return ParsedSourceVcsKind::Fossil;
    if(identifier == "git") return ParsedSourceVcsKind::Git;
    if(identifier == "hg") return ParsedSourceVcsKind::Hg;
    if(identifier == "svn") return ParsedSourceVcsKind::Svn;
    return std::nullopt;
}

bool is_curated_unrecognized_vcs_identifier(
        std::string_view identifier) noexcept {
    // Issue #270 names CVS and Darcs as explicit VCS candidates even though
    // current makepkg has no native source owner for either syntax. A generic
    // '+' in an arbitrary URI scheme is not equivalent VCS evidence.
    return identifier == "cvs" || identifier == "darcs";
}

std::optional<ExplicitVcsPrefix> parse_explicit_vcs_prefix(
        std::string_view value) noexcept {
    const std::size_t separator = value.find('+');
    if(separator == std::string_view::npos || separator == 0) {
        return std::nullopt;
    }
    const std::string_view identifier = value.substr(0, separator);
    if((!is_ascii_lower(identifier.front()) &&
        !is_ascii_upper(identifier.front())) ||
       !std::all_of(
               identifier.begin() + 1, identifier.end(),
               is_uri_scheme_character)) {
        return std::nullopt;
    }
    if(!parse_known_vcs_kind(identifier).has_value() &&
       !is_curated_unrecognized_vcs_identifier(identifier)) {
        return std::nullopt;
    }

    const std::string_view remainder = value.substr(separator + 1);
    if(parse_uri_scheme(remainder).has_value() ||
       (identifier == "bzr" && remainder.starts_with("lp:") &&
        remainder.size() > 3)) {
        return ExplicitVcsPrefix{identifier, remainder};
    }
    return std::nullopt;
}

constexpr bool uses_makepkg_query_components(
        ParsedSourceVcsKind kind) noexcept {
    // Git and Fossil use makepkg's shared fragment/query helpers. Hg, SVN and
    // Bzr retain '?' in the remote location or selector value instead.
    return kind == ParsedSourceVcsKind::Git ||
            kind == ParsedSourceVcsKind::Fossil;
}

constexpr bool uses_last_selector_value_separator(
        ParsedSourceVcsKind kind) noexcept {
    // makepkg's Bzr owner expands ${fragment#*=}; the other current VCS
    // owners expand ${fragment##*=}. All owners take the key before the first
    // '=' while their effective value boundary is intentionally different.
    return kind != ParsedSourceVcsKind::Bzr;
}

std::optional<ParsedSourceSelectorRole> recognized_selector_role(
        ParsedSourceVcsKind kind, std::string_view key) noexcept {
    switch(kind) {
    case ParsedSourceVcsKind::Git:
    case ParsedSourceVcsKind::Fossil:
        if(key == "branch") return ParsedSourceSelectorRole::Branch;
        if(key == "commit") return ParsedSourceSelectorRole::Commit;
        if(key == "tag") return ParsedSourceSelectorRole::Tag;
        return std::nullopt;
    case ParsedSourceVcsKind::Hg:
        if(key == "branch") return ParsedSourceSelectorRole::Branch;
        if(key == "revision") return ParsedSourceSelectorRole::Revision;
        if(key == "tag") return ParsedSourceSelectorRole::Tag;
        return std::nullopt;
    case ParsedSourceVcsKind::Bzr:
    case ParsedSourceVcsKind::Svn:
        return key == "revision"
                ? std::optional<ParsedSourceSelectorRole>(
                          ParsedSourceSelectorRole::Revision)
                : std::nullopt;
    }
    return std::nullopt;
}

SourceEntryParseFailure make_failure(SourceEntryParseErrorCode code) noexcept {
    return SourceEntryParseFailure{code};
}

std::optional<SourceEntryParseFailure> parse_vcs_components(
        std::string_view source, ParsedSourceVcsKind kind,
        std::string& source_location,
        std::optional<std::string>& transport_scheme,
        std::optional<ParsedSourceSelector>& selector,
        std::optional<ParsedSourceQuery>& query,
        ParsedSourceVcsComponentOrder& component_order) {
    if(static_cast<std::size_t>(std::count(source.begin(), source.end(), '#')) >
       1) {
        return make_failure(SourceEntryParseErrorCode::DuplicateSelector);
    }
    const bool has_query_components = uses_makepkg_query_components(kind);
    if(has_query_components &&
       static_cast<std::size_t>(std::count(source.begin(), source.end(), '?')) >
               1) {
        return make_failure(SourceEntryParseErrorCode::DuplicateQuery);
    }

    const std::size_t fragment_position = source.find('#');
    const std::size_t query_position = has_query_components
            ? source.find('?')
            : std::string_view::npos;
    std::size_t location_end = source.size();
    if(fragment_position != std::string_view::npos) {
        location_end = std::min(location_end, fragment_position);
    }
    if(query_position != std::string_view::npos) {
        location_end = std::min(location_end, query_position);
    }

    const std::string_view location = source.substr(0, location_end);
    if(location.empty()) {
        return make_failure(
                SourceEntryParseErrorCode::InvalidSourceLocation);
    }

    if(location.starts_with("lp:")) {
        if(kind != ParsedSourceVcsKind::Bzr || location.size() == 3) {
            return make_failure(
                    SourceEntryParseErrorCode::InvalidSourceLocation);
        }
        transport_scheme = "lp";
    } else {
        const std::optional<std::string_view> scheme =
                parse_uri_scheme(location);
        if(!scheme.has_value()) {
            return make_failure(
                    SourceEntryParseErrorCode::InvalidSourceLocation);
        }
        transport_scheme = std::string(*scheme);
    }
    source_location = std::string(location);

    if(fragment_position != std::string_view::npos) {
        const std::size_t fragment_end =
                query_position != std::string_view::npos &&
                        query_position > fragment_position
                ? query_position
                : source.size();
        const std::string_view raw_fragment = source.substr(
                fragment_position + 1,
                fragment_end - fragment_position - 1);
        const std::size_t key_separator = raw_fragment.find('=');
        const std::size_t value_separator =
                uses_last_selector_value_separator(kind)
                ? raw_fragment.rfind('=')
                : key_separator;
        if(key_separator == std::string_view::npos || key_separator == 0 ||
           value_separator + 1 == raw_fragment.size()) {
            return make_failure(SourceEntryParseErrorCode::MalformedSelector);
        }
        const std::string_view key = raw_fragment.substr(0, key_separator);
        const std::string_view value = raw_fragment.substr(value_separator + 1);
        selector = ParsedSourceSelector{
                std::string(raw_fragment),
                std::string(key),
                std::string(value),
                recognized_selector_role(kind, key)};
    }

    if(query_position != std::string_view::npos) {
        const std::size_t query_end =
                fragment_position != std::string_view::npos &&
                        fragment_position > query_position
                ? fragment_position
                : source.size();
        const std::string_view raw_query = source.substr(
                query_position + 1, query_end - query_position - 1);
        if(raw_query.empty()) {
            return make_failure(SourceEntryParseErrorCode::MalformedQuery);
        }
        std::optional<ParsedSourceQueryFlag> recognized_flag;
        if(kind == ParsedSourceVcsKind::Git && raw_query == "signed") {
            recognized_flag = ParsedSourceQueryFlag::Signed;
        }
        query = ParsedSourceQuery{std::string(raw_query), recognized_flag};
    }

    if(fragment_position == std::string_view::npos &&
       query_position == std::string_view::npos) {
        component_order = ParsedSourceVcsComponentOrder::None;
    } else if(fragment_position == std::string_view::npos) {
        component_order = ParsedSourceVcsComponentOrder::QueryOnly;
    } else if(query_position == std::string_view::npos) {
        component_order = ParsedSourceVcsComponentOrder::FragmentOnly;
    } else if(query_position < fragment_position) {
        component_order = ParsedSourceVcsComponentOrder::QueryThenFragment;
    } else {
        component_order = ParsedSourceVcsComponentOrder::FragmentThenQuery;
    }
    return std::nullopt;
}

} // namespace

SourceEntryParseResult::SourceEntryParseResult(
        ParsedSourceEntry entry) noexcept
    : outcome_(std::move(entry)) {}

SourceEntryParseResult::SourceEntryParseResult(
        SourceEntryParseFailure failure) noexcept
    : outcome_(failure) {}

bool SourceEntryParseResult::is_success() const noexcept {
    return std::holds_alternative<ParsedSourceEntry>(outcome_);
}

const ParsedSourceEntry* SourceEntryParseResult::entry() const noexcept {
    return std::get_if<ParsedSourceEntry>(&outcome_);
}

const SourceEntryParseFailure* SourceEntryParseResult::failure()
        const noexcept {
    return std::get_if<SourceEntryParseFailure>(&outcome_);
}

SourceSyntaxTextStatus validate_source_syntax_text(
        std::string_view value) noexcept {
    std::size_t offset = 0;
    while(offset < value.size()) {
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        if(!decode_utf8_code_point(value, offset, code_point, length)) {
            return SourceSyntaxTextStatus::InvalidUtf8;
        }
        if(!is_single_line_code_point(code_point)) {
            return SourceSyntaxTextStatus::ControlCharacter;
        }
        offset += length;
    }
    return SourceSyntaxTextStatus::Valid;
}

SourceEntryParseResult parse_source_entry(std::string_view value) {
    if(value.empty()) {
        return SourceEntryParseResult(
                make_failure(SourceEntryParseErrorCode::EmptyValue));
    }
    switch(validate_source_syntax_text(value)) {
    case SourceSyntaxTextStatus::InvalidUtf8:
        return SourceEntryParseResult(
                make_failure(SourceEntryParseErrorCode::InvalidUtf8));
    case SourceSyntaxTextStatus::ControlCharacter:
        return SourceEntryParseResult(
                make_failure(SourceEntryParseErrorCode::ControlCharacter));
    case SourceSyntaxTextStatus::Valid:
        break;
    }
    if(has_ascii_edge_whitespace(value)) {
        return SourceEntryParseResult(
                make_failure(SourceEntryParseErrorCode::Whitespace));
    }

    std::optional<std::string> destination_name;
    std::string_view source = value;
    const std::size_t destination_separator = value.find("::");
    if(destination_separator != std::string_view::npos) {
        const std::string_view destination =
                value.substr(0, destination_separator);
        if(destination.empty()) {
            return SourceEntryParseResult(make_failure(
                    SourceEntryParseErrorCode::EmptyDestination));
        }
        if(has_ascii_edge_whitespace(destination) || destination == "." ||
           destination == ".." ||
           destination.find('/') != std::string_view::npos) {
            return SourceEntryParseResult(make_failure(
                    SourceEntryParseErrorCode::InvalidDestination));
        }
        destination_name = std::string(destination);
        source.remove_prefix(destination_separator + 2);
        if(source.empty()) {
            return SourceEntryParseResult(
                    make_failure(SourceEntryParseErrorCode::EmptySource));
        }
        if(has_ascii_edge_whitespace(source)) {
            return SourceEntryParseResult(
                    make_failure(SourceEntryParseErrorCode::Whitespace));
        }
    }

    ParsedSourceEntry entry{
            std::string(value),
            std::move(destination_name),
            std::string(source),
            ParsedSourceEntryKind::Local,
            std::string(source),
            std::nullopt,
            std::nullopt};

    const std::optional<ExplicitVcsPrefix> explicit_prefix =
            parse_explicit_vcs_prefix(source);
    if(explicit_prefix.has_value()) {
        if(contains_ascii_whitespace(source)) {
            return SourceEntryParseResult(
                    make_failure(SourceEntryParseErrorCode::Whitespace));
        }
        const std::optional<ParsedSourceVcsKind> known_kind =
                parse_known_vcs_kind(explicit_prefix->identifier);
        if(!known_kind.has_value()) {
            const std::size_t component_position =
                    explicit_prefix->remainder.find_first_of("?#");
            const std::string_view location =
                    component_position == std::string_view::npos
                    ? explicit_prefix->remainder
                    : explicit_prefix->remainder.substr(
                              0, component_position);
            const std::optional<std::string_view> scheme =
                    parse_uri_scheme(location);
            if(!scheme.has_value()) {
                return SourceEntryParseResult(make_failure(
                        SourceEntryParseErrorCode::InvalidSourceLocation));
            }
            entry.kind = ParsedSourceEntryKind::UnrecognizedVcs;
            entry.source_location =
                    std::string(explicit_prefix->remainder);
            entry.transport_scheme = std::string(*scheme);
            entry.vcs = ParsedSourceVcsSyntax{
                    std::string(explicit_prefix->identifier),
                    std::nullopt,
                    ParsedSourceVcsDeclarationKind::ExplicitPrefix,
                    std::nullopt,
                    std::nullopt,
                    ParsedSourceVcsComponentOrder::None};
            return SourceEntryParseResult(std::move(entry));
        }

        std::optional<ParsedSourceSelector> selector;
        std::optional<ParsedSourceQuery> query;
        ParsedSourceVcsComponentOrder component_order =
                ParsedSourceVcsComponentOrder::None;
        const std::optional<SourceEntryParseFailure> failure =
                parse_vcs_components(
                        explicit_prefix->remainder, *known_kind,
                        entry.source_location, entry.transport_scheme,
                        selector, query, component_order);
        if(failure.has_value()) return SourceEntryParseResult(*failure);

        entry.kind = ParsedSourceEntryKind::Vcs;
        entry.vcs = ParsedSourceVcsSyntax{
                std::string(explicit_prefix->identifier),
                known_kind,
                ParsedSourceVcsDeclarationKind::ExplicitPrefix,
                std::move(selector),
                std::move(query),
                component_order};
        return SourceEntryParseResult(std::move(entry));
    }

    const std::optional<std::string_view> scheme = parse_uri_scheme(source);
    if(scheme.has_value()) {
        if(contains_ascii_whitespace(source)) {
            return SourceEntryParseResult(
                    make_failure(SourceEntryParseErrorCode::Whitespace));
        }
        const std::optional<ParsedSourceVcsKind> native_vcs_kind =
                parse_known_vcs_kind(*scheme);
        if(native_vcs_kind.has_value()) {
            std::optional<ParsedSourceSelector> selector;
            std::optional<ParsedSourceQuery> query;
            ParsedSourceVcsComponentOrder component_order =
                    ParsedSourceVcsComponentOrder::None;
            const std::optional<SourceEntryParseFailure> failure =
                    parse_vcs_components(
                            source, *native_vcs_kind,
                            entry.source_location, entry.transport_scheme,
                            selector, query, component_order);
            if(failure.has_value()) return SourceEntryParseResult(*failure);

            entry.kind = ParsedSourceEntryKind::Vcs;
            entry.vcs = ParsedSourceVcsSyntax{
                    std::string(*scheme),
                    native_vcs_kind,
                    ParsedSourceVcsDeclarationKind::NativeScheme,
                    std::move(selector),
                    std::move(query),
                    component_order};
        } else {
            // Ordinary URL query and fragment data belong to the URL itself;
            // only recognized VCS syntax receives makepkg interpretation.
            entry.kind = ParsedSourceEntryKind::Remote;
            entry.transport_scheme = std::string(*scheme);
        }
        return SourceEntryParseResult(std::move(entry));
    }

    if(source.find("://") != std::string_view::npos) {
        return SourceEntryParseResult(make_failure(
                SourceEntryParseErrorCode::InvalidSourceLocation));
    }
    return SourceEntryParseResult(std::move(entry));
}
