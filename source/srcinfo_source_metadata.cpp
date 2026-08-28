#include "srcinfo_source_metadata.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct SourceFieldMatch {
    std::optional<std::string_view> architecture_qualifier;
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

constexpr bool is_ascii_alphanumeric(char character) noexcept {
    return is_ascii_lower(character) || is_ascii_upper(character) ||
           is_ascii_digit(character);
}

std::string_view trim_assignment_whitespace(std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t");
    if(first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool is_valid_field_name(std::string_view field_name) noexcept {
    if(field_name.empty() || !is_ascii_lower(field_name.front())) return false;
    return std::all_of(
        field_name.begin() + 1, field_name.end(), [](char character) {
            return is_ascii_alphanumeric(character) ||
                   character == '_';
        });
}

bool is_valid_package_identity(std::string_view identity) noexcept {
    if(identity.empty() || identity.front() == '-' ||
       identity.front() == '.') {
        return false;
    }
    return std::all_of(identity.begin(), identity.end(), [](char character) {
        return is_ascii_alphanumeric(character) || character == '+' ||
               character == '_' || character == '.' || character == '@' ||
               character == '-';
    });
}

bool is_valid_architecture_qualifier(std::string_view qualifier) noexcept {
    return !qualifier.empty() && qualifier != "any" &&
           std::all_of(
               qualifier.begin(), qualifier.end(), [](char character) {
                   return is_ascii_alphanumeric(character) ||
                          character == '_';
               });
}

std::optional<SourceFieldMatch> match_source_field(
    std::string_view field_name) noexcept {
    if(field_name == "source") {
        return SourceFieldMatch{std::nullopt};
    }
    constexpr std::string_view PREFIX = "source_";
    if(field_name.starts_with(PREFIX)) {
        return SourceFieldMatch{field_name.substr(PREFIX.size())};
    }
    return std::nullopt;
}

SrcinfoSourceMetadataParseFailure make_failure(
    SrcinfoSourceMetadataParseErrorCode code, std::size_t line,
    std::optional<SourceEntryParseErrorCode> source_entry_error =
        std::nullopt) noexcept {
    return SrcinfoSourceMetadataParseFailure{
        code, line, source_entry_error};
}

std::size_t source_line_count(std::string_view source) noexcept {
    if(source.empty()) return 0;
    const std::size_t newline_count = static_cast<std::size_t>(
        std::count(source.begin(), source.end(), '\n'));
    return source.back() == '\n' ? newline_count : newline_count + 1;
}

} // namespace

SrcinfoSourceMetadataParseResult::SrcinfoSourceMetadataParseResult(
    ParsedSrcinfoSourceMetadata metadata) noexcept
    : outcome_(std::move(metadata)) {
}

SrcinfoSourceMetadataParseResult::SrcinfoSourceMetadataParseResult(
    SrcinfoSourceMetadataParseFailure failure) noexcept
    : outcome_(failure) {
}

bool SrcinfoSourceMetadataParseResult::is_success() const noexcept {
    return std::holds_alternative<ParsedSrcinfoSourceMetadata>(outcome_);
}

const ParsedSrcinfoSourceMetadata*
SrcinfoSourceMetadataParseResult::metadata() const noexcept {
    return std::get_if<ParsedSrcinfoSourceMetadata>(&outcome_);
}

const SrcinfoSourceMetadataParseFailure*
SrcinfoSourceMetadataParseResult::failure() const noexcept {
    return std::get_if<SrcinfoSourceMetadataParseFailure>(&outcome_);
}

SrcinfoSourceMetadataParseResult parse_srcinfo_source_metadata(
    std::string_view source) {
    ParsedSrcinfoSourceMetadata metadata;
    std::vector<std::string> package_names;
    bool is_child_section = false;

    std::size_t offset = 0;
    std::size_t line_number = 1;
    while(offset < source.size()) {
        const std::size_t newline = source.find('\n', offset);
        const std::size_t line_end =
            newline == std::string_view::npos ? source.size() : newline;
        std::string_view line = source.substr(offset, line_end - offset);
        if(!line.empty() && line.back() == '\r') line.remove_suffix(1);

        switch(validate_source_syntax_text(line)) {
            case SourceSyntaxTextStatus::InvalidUtf8:
                return SrcinfoSourceMetadataParseResult(make_failure(
                    SrcinfoSourceMetadataParseErrorCode::InvalidUtf8,
                    line_number));
            case SourceSyntaxTextStatus::ControlCharacter:
                return SrcinfoSourceMetadataParseResult(make_failure(
                    SrcinfoSourceMetadataParseErrorCode::ControlCharacter,
                    line_number));
            case SourceSyntaxTextStatus::Valid:
                break;
        }

        const std::string_view trimmed_line =
            trim_assignment_whitespace(line);
        if(!trimmed_line.empty() && trimmed_line.front() != '#') {
            const std::size_t separator = trimmed_line.find('=');
            if(separator == std::string_view::npos) {
                return SrcinfoSourceMetadataParseResult(make_failure(
                    SrcinfoSourceMetadataParseErrorCode::MalformedLine,
                    line_number));
            }

            const std::string_view field_name = trim_assignment_whitespace(
                trimmed_line.substr(0, separator));
            const std::string_view raw_value = trim_assignment_whitespace(
                trimmed_line.substr(separator + 1));
            if(!is_valid_field_name(field_name)) {
                return SrcinfoSourceMetadataParseResult(make_failure(
                    SrcinfoSourceMetadataParseErrorCode::MalformedLine,
                    line_number));
            }

            // SRCINFO requires the sole pkgbase header to begin the document;
            // unrelated fields cannot create that section implicitly.
            if(metadata.package_base.empty() && field_name != "pkgbase") {
                return SrcinfoSourceMetadataParseResult(make_failure(
                    SrcinfoSourceMetadataParseErrorCode::InvalidFieldScope,
                    line_number));
            }

            if(field_name == "pkgbase") {
                if(!is_valid_package_identity(raw_value)) {
                    return SrcinfoSourceMetadataParseResult(make_failure(
                        SrcinfoSourceMetadataParseErrorCode::
                            InvalidPackageIdentity,
                        line_number));
                }
                if(!metadata.package_base.empty()) {
                    const auto code = raw_value == metadata.package_base
                                          ? SrcinfoSourceMetadataParseErrorCode::
                                                DuplicatePackageBase
                                          : SrcinfoSourceMetadataParseErrorCode::
                                                ConflictingPackageBase;
                    return SrcinfoSourceMetadataParseResult(
                        make_failure(code, line_number));
                }
                metadata.package_base = std::string(raw_value);
            } else if(field_name == "pkgname") {
                if(!is_valid_package_identity(raw_value)) {
                    return SrcinfoSourceMetadataParseResult(make_failure(
                        SrcinfoSourceMetadataParseErrorCode::
                            InvalidPackageIdentity,
                        line_number));
                }
                if(std::find(
                       package_names.begin(), package_names.end(),
                       raw_value) != package_names.end()) {
                    return SrcinfoSourceMetadataParseResult(make_failure(
                        SrcinfoSourceMetadataParseErrorCode::
                            DuplicatePackageName,
                        line_number));
                }
                package_names.emplace_back(raw_value);
                is_child_section = true;
            } else {
                const std::optional<SourceFieldMatch> source_field =
                    match_source_field(field_name);
                if(source_field.has_value()) {
                    if(is_child_section) {
                        return SrcinfoSourceMetadataParseResult(make_failure(
                            SrcinfoSourceMetadataParseErrorCode::
                                InvalidFieldScope,
                            line_number));
                    }
                    if(raw_value.empty()) {
                        return SrcinfoSourceMetadataParseResult(make_failure(
                            SrcinfoSourceMetadataParseErrorCode::
                                EmptySourceValue,
                            line_number));
                    }

                    std::optional<std::string> architecture_qualifier;
                    if(source_field->architecture_qualifier.has_value()) {
                        if(!is_valid_architecture_qualifier(
                               *source_field->architecture_qualifier)) {
                            return SrcinfoSourceMetadataParseResult(
                                make_failure(
                                    SrcinfoSourceMetadataParseErrorCode::
                                        InvalidArchitectureQualifier,
                                    line_number));
                        }
                        architecture_qualifier = std::string(
                            *source_field->architecture_qualifier);
                    }

                    SourceEntryParseResult parsed =
                        parse_source_entry(raw_value);
                    if(!parsed.is_success()) {
                        return SrcinfoSourceMetadataParseResult(make_failure(
                            SrcinfoSourceMetadataParseErrorCode::
                                InvalidSourceEntry,
                            line_number,
                            parsed.failure()->code));
                    }
                    metadata.source_entries.push_back(
                        ParsedSrcinfoSourceEntry{
                            std::string(raw_value),
                            std::move(architecture_qualifier),
                            *parsed.entry()});
                }
                // Other valid fields are outside this source-only projection.
            }
        }

        if(newline == std::string_view::npos) break;
        offset = newline + 1;
        ++line_number;
    }

    if(metadata.package_base.empty()) {
        return SrcinfoSourceMetadataParseResult(make_failure(
            SrcinfoSourceMetadataParseErrorCode::MissingPackageBase,
            source_line_count(source) + 1));
    }
    return SrcinfoSourceMetadataParseResult(std::move(metadata));
}
