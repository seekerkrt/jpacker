#pragma once

#include "source_entry_parser.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct ParsedSrcinfoSourceEntry {
    std::string raw_value;
    std::optional<std::string> architecture_qualifier;
    ParsedSourceEntry parsed_source;

    bool operator==(const ParsedSrcinfoSourceEntry&) const = default;
};

// This type describes parsed, untrusted bytes. It intentionally has no
// conversion to TrustedDevelSourceMetadata or any production observer input.
struct ParsedSrcinfoSourceMetadata {
    std::string package_base;
    std::vector<ParsedSrcinfoSourceEntry> source_entries;

    bool operator==(const ParsedSrcinfoSourceMetadata&) const = default;
};

enum class SrcinfoSourceMetadataParseErrorCode {
    MalformedLine,
    InvalidUtf8,
    ControlCharacter,
    InvalidPackageIdentity,
    InvalidFieldScope,
    EmptySourceValue,
    InvalidArchitectureQualifier,
    InvalidSourceEntry,
    DuplicatePackageBase,
    ConflictingPackageBase,
    DuplicatePackageName,
    MissingPackageBase,
};

struct SrcinfoSourceMetadataParseFailure {
    SrcinfoSourceMetadataParseErrorCode code;
    std::size_t line;
    std::optional<SourceEntryParseErrorCode> source_entry_error;

    bool operator==(const SrcinfoSourceMetadataParseFailure&) const =
        default;
};

class SrcinfoSourceMetadataParseResult final {
public:
    SrcinfoSourceMetadataParseResult() = delete;
    SrcinfoSourceMetadataParseResult(
        const SrcinfoSourceMetadataParseResult&) = default;
    SrcinfoSourceMetadataParseResult(
        SrcinfoSourceMetadataParseResult&&) noexcept = default;
    SrcinfoSourceMetadataParseResult& operator=(
        const SrcinfoSourceMetadataParseResult&) = delete;
    SrcinfoSourceMetadataParseResult& operator=(
        SrcinfoSourceMetadataParseResult&&) noexcept = delete;
    ~SrcinfoSourceMetadataParseResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const ParsedSrcinfoSourceMetadata* metadata()
        const noexcept;
    [[nodiscard]] const SrcinfoSourceMetadataParseFailure* failure()
        const noexcept;

private:
    explicit SrcinfoSourceMetadataParseResult(
        ParsedSrcinfoSourceMetadata metadata) noexcept;
    explicit SrcinfoSourceMetadataParseResult(
        SrcinfoSourceMetadataParseFailure failure) noexcept;

    std::variant<
        ParsedSrcinfoSourceMetadata,
        SrcinfoSourceMetadataParseFailure>
        outcome_;

    friend SrcinfoSourceMetadataParseResult parse_srcinfo_source_metadata(
        std::string_view source);
};

// `.SRCINFO` textだけを入力とし、filesystem、network、PKGBUILD evaluation、
// process、environment、current directoryを参照しない。
[[nodiscard]] SrcinfoSourceMetadataParseResult parse_srcinfo_source_metadata(
    std::string_view source);
