#include "runtime_diagnostic.hpp"

#include "localization.hpp"
#include "logging.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void append_escaped_byte(std::string& display, unsigned char byte) {
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    display += "\\x";
    display.push_back(HEX_DIGITS[(byte >> 4) & 0x0f]);
    display.push_back(HEX_DIGITS[byte & 0x0f]);
}

bool is_utf8_continuation_byte(unsigned char byte) noexcept {
    return byte >= 0x80 && byte <= 0xbf;
}

bool decode_utf8_code_point(
    std::string_view value, std::size_t offset,
    char32_t& code_point, std::size_t& length) noexcept {
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
        if(!is_utf8_continuation_byte(second)) return false;
        code_point =
            (static_cast<char32_t>(first & 0x1f) << 6) |
            static_cast<char32_t>(second & 0x3f);
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
                : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third)) return false;
        code_point =
            (static_cast<char32_t>(first & 0x0f) << 12) |
            (static_cast<char32_t>(second & 0x3f) << 6) |
            static_cast<char32_t>(third & 0x3f);
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
                : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third) ||
           !is_utf8_continuation_byte(fourth)) {
            return false;
        }
        code_point =
            (static_cast<char32_t>(first & 0x07) << 18) |
            (static_cast<char32_t>(second & 0x3f) << 12) |
            (static_cast<char32_t>(third & 0x3f) << 6) |
            static_cast<char32_t>(fourth & 0x3f);
        length = 4;
        return true;
    }
    return false;
}

bool is_bidi_control(char32_t code_point) noexcept {
    return code_point == 0x061c || code_point == 0x200e ||
           code_point == 0x200f ||
           (code_point >= 0x202a && code_point <= 0x202e) ||
           (code_point >= 0x2066 && code_point <= 0x2069);
}

bool is_terminal_safe_code_point(char32_t code_point) noexcept {
    return code_point >= 0x20 &&
           !(code_point >= 0x7f && code_point <= 0x9f) &&
           code_point != static_cast<char32_t>('\\') &&
           code_point != 0x2028 && code_point != 0x2029 &&
           code_point != 0xfeff && !is_bidi_control(code_point);
}

} // namespace

std::string terminal_safe_runtime_diagnostic_detail(std::string_view value) {
    std::string display;
    display.reserve(value.size());
    std::size_t offset = 0;
    while(offset < value.size()) {
        char32_t code_point = 0;
        std::size_t length = 0;
        if(!decode_utf8_code_point(value, offset, code_point, length)) {
            append_escaped_byte(
                display, static_cast<unsigned char>(value[offset++]));
            continue;
        }
        if(is_terminal_safe_code_point(code_point)) {
            display.append(value.substr(offset, length));
        } else {
            for(std::size_t index = 0; index < length; ++index) {
                append_escaped_byte(
                    display,
                    static_cast<unsigned char>(value[offset + index]));
            }
        }
        offset += length;
    }
    return display;
}

std::string diagnostic_class_label(DiagnosticClass classification) {
    switch(classification) {
        case DiagnosticClass::Invalid:
            return localization::translate_message("Invalid");
        case DiagnosticClass::Unsupported:
            return localization::translate_message("Unsupported");
        case DiagnosticClass::Ambiguous:
            return localization::translate_message("Ambiguous");
        case DiagnosticClass::Declined:
            return localization::translate_message("Declined");
        case DiagnosticClass::Cancelled:
            return localization::translate_message("Cancelled");
        case DiagnosticClass::Unavailable:
            return localization::translate_message("Unavailable");
        case DiagnosticClass::InputFailure:
            return localization::translate_message("Input failure");
        case DiagnosticClass::QueryFailure:
            return localization::translate_message("Query failure");
        case DiagnosticClass::MetadataFailure:
            return localization::translate_message("Metadata failure");
        case DiagnosticClass::RequiresCheck:
            return localization::translate_message("Requires check");
        case DiagnosticClass::Blocked:
            return localization::translate_message("Blocked");
        case DiagnosticClass::PartialFailure:
            return localization::translate_message("Partial failure");
        case DiagnosticClass::ExecutionFailure:
            return localization::translate_message("Execution failure");
        case DiagnosticClass::InternalInconsistency:
            return localization::translate_message("Internal inconsistency");
    }
    throw std::logic_error(localization::translate_message(
        "Unknown diagnostic classification."));
}

std::string diagnostic_source_label(DiagnosticSourceKind source_kind) {
    // NO_TRANSLATE(Issue #350): Stable typed source tokens.
    switch(source_kind) {
        case DiagnosticSourceKind::Unspecified:
            return "unspecified";
        case DiagnosticSourceKind::RepositoryBinary:
            return "repository-binary";
        case DiagnosticSourceKind::RepositorySource:
            return "repository-source";
        case DiagnosticSourceKind::Aur:
            return "aur";
        case DiagnosticSourceKind::Local:
            return "local";
        case DiagnosticSourceKind::Pacman:
            return "pacman";
    }
    throw std::logic_error(localization::translate_message(
        "Unknown diagnostic source kind."));
}

std::string diagnostic_identity_suffix(const DiagnosticIdentity& identity) {
    std::string suffix;
    auto append = [&suffix](const std::string& field, const std::string& value) {
        suffix += suffix.empty() ? " [" : ", ";
        suffix += field;
        suffix += "=";
        suffix += value;
    };
    if(identity.source_kind != DiagnosticSourceKind::Unspecified) {
        append(localization::translate_message("source"),
               diagnostic_source_label(identity.source_kind));
    }
    if(identity.repository.has_value()) {
        append(localization::translate_message("repository"),
               identity.repository.value());
    }
    if(identity.requested_package.has_value()) {
        append(localization::translate_message("package"),
               identity.requested_package.value());
    }
    if(identity.package_base.has_value()) {
        append("PackageBase", identity.package_base.value());
    }
    if(identity.canonical_source_identity.has_value()) {
        append(localization::translate_message("source identity"),
               identity.canonical_source_identity.value());
    }
    if(identity.local_root.has_value()) {
        append(localization::translate_message("local root"),
               identity.local_root->string());
    }
    if(!suffix.empty()) suffix += "]";
    return suffix;
}

void report_runtime_diagnostic(
    const RuntimeDiagnosticPresentation& diagnostic) {
    switch(diagnostic.severity) {
        case DiagnosticSeverity::Info:
            Logger::info(diagnostic.message);
            return;
        case DiagnosticSeverity::Warning:
            Logger::warn(diagnostic.message);
            return;
        case DiagnosticSeverity::Error:
            Logger::error(diagnostic.message);
            return;
    }
    throw std::logic_error("Unknown runtime diagnostic severity.");
}
