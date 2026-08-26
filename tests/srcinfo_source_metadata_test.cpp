#include "srcinfo_source_metadata.hpp"

#include "devel_package_classification.hpp"
#include "devel_update_model.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

static_assert(!std::is_convertible_v<
              ParsedSrcinfoSourceMetadata,
              TrustedDevelSourceMetadata>);
static_assert(!std::is_constructible_v<
              TrustedDevelSourceMetadata,
              const ParsedSrcinfoSourceMetadata&>);
static_assert(!std::is_constructible_v<
              DevelPackageClassification,
              const ParsedSrcinfoSourceMetadata&>);
static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              const ParsedSrcinfoSourceMetadata&>);
static_assert(!std::is_convertible_v<
              ParsedSrcinfoSourceEntry,
              TrustedDevelSourceMetadata>);
static_assert(!std::is_constructible_v<
              VcsSourceIdentity,
              const ParsedSrcinfoSourceEntry&>);

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ParsedSrcinfoSourceMetadata parse_success(
        std::string_view source, const std::string& context) {
    const SrcinfoSourceMetadataParseResult result =
            parse_srcinfo_source_metadata(source);
    if(!result.is_success()) {
        require(result.failure() != nullptr, context + ": failure is absent");
        throw std::runtime_error(
                context + ": parse failed with code " +
                std::to_string(static_cast<int>(result.failure()->code)) +
                " at line " + std::to_string(result.failure()->line));
    }
    require(result.metadata() != nullptr, context + ": metadata is absent");
    require(result.failure() == nullptr, context + ": failure is exposed");
    return *result.metadata();
}

void expect_failure(
        std::string_view source, SrcinfoSourceMetadataParseErrorCode code,
        std::size_t line, const std::string& context,
        std::optional<SourceEntryParseErrorCode> source_entry_error =
                std::nullopt) {
    const SrcinfoSourceMetadataParseResult result =
            parse_srcinfo_source_metadata(source);
    require(!result.is_success(), context + ": parse unexpectedly succeeded");
    require(result.metadata() == nullptr, context + ": partial metadata exposed");
    require(result.failure() != nullptr, context + ": failure is absent");
    require(
            *result.failure() == SrcinfoSourceMetadataParseFailure{
                                         code, line, source_entry_error},
            context + ": failure code, line, or nested source error differs");
}

void test_basic_and_repeated_source_order() {
    const ParsedSrcinfoSourceMetadata basic = parse_success(
            "pkgbase = foo\n"
            "source = https://example.org/foo.tar.zst\n",
            "basic source metadata");
    require(basic.package_base == "foo" && basic.source_entries.size() == 1 &&
                    basic.source_entries[0].raw_value ==
                            "https://example.org/foo.tar.zst" &&
                    !basic.source_entries[0].architecture_qualifier
                             .has_value() &&
                    basic.source_entries[0].parsed_source.kind ==
                            ParsedSourceEntryKind::Remote,
            "basic PackageBase source differs");

    const ParsedSrcinfoSourceMetadata repeated = parse_success(
            "pkgbase = repeated\n"
            "source = one\n"
            "source = two\n"
            "source = three\n",
            "repeated sources");
    require(repeated.source_entries.size() == 3 &&
                    repeated.source_entries[0].raw_value == "one" &&
                    repeated.source_entries[1].raw_value == "two" &&
                    repeated.source_entries[2].raw_value == "three",
            "repeated source lexical order changed");
}

void test_architecture_qualifiers_preserve_lexical_order() {
    const ParsedSrcinfoSourceMetadata metadata = parse_success(
            "pkgbase = architecture\n"
            "source = common\n"
            "source_x86_64 = x86\n"
            "source_aarch64 = arm\n"
            "source = common2\n",
            "architecture-qualified sources");

    require(metadata.source_entries.size() == 4 &&
                    !metadata.source_entries[0].architecture_qualifier
                             .has_value() &&
                    metadata.source_entries[1].architecture_qualifier ==
                            "x86_64" &&
                    metadata.source_entries[2].architecture_qualifier ==
                            "aarch64" &&
                    !metadata.source_entries[3].architecture_qualifier
                             .has_value() &&
                    metadata.source_entries[0].raw_value == "common" &&
                    metadata.source_entries[1].raw_value == "x86" &&
                    metadata.source_entries[2].raw_value == "arm" &&
                    metadata.source_entries[3].raw_value == "common2",
            "source order or architecture qualifier was flattened");
}

void test_package_base_and_child_section_boundary() {
    const ParsedSrcinfoSourceMetadata valid = parse_success(
            "# generated\n"
            "pkgbase = split\n"
            "pkgver = 1\n"
            "source = base-source\n"
            "pkgname = split-cli\n"
            "depends = runtime\n"
            "pkgname = split-docs\n",
            "PackageBase source and child sections");
    require(valid.package_base == "split" &&
                    valid.source_entries.size() == 1 &&
                    valid.source_entries[0].raw_value == "base-source",
            "unrelated child fields changed PackageBase sources");

    expect_failure(
            "pkgbase = split\n"
            "source = base-source\n"
            "pkgname = split\n"
            "source = child-source\n",
            SrcinfoSourceMetadataParseErrorCode::InvalidFieldScope, 4,
            "source in child section");
    expect_failure(
            "pkgbase = split\n"
            "pkgname = split\n"
            "source_x86_64 = child-source\n",
            SrcinfoSourceMetadataParseErrorCode::InvalidFieldScope, 3,
            "architecture source in child section");
    expect_failure(
            "source = before-base\n"
            "pkgbase = late\n",
            SrcinfoSourceMetadataParseErrorCode::InvalidFieldScope, 1,
            "source before PackageBase");
}

void test_structural_and_source_failures() {
    using ErrorCode = SrcinfoSourceMetadataParseErrorCode;
    expect_failure(
            "pkgbase = empty-source\n"
            "source =\n",
            ErrorCode::EmptySourceValue, 2, "empty source");
    expect_failure(
            "pkgbase = malformed\n"
            "source without assignment\n",
            ErrorCode::MalformedLine, 2, "malformed assignment");
    expect_failure(
            "pkgbase = malformed-field\n"
            "Source = value\n",
            ErrorCode::MalformedLine, 2, "invalid field key");
    expect_failure(
            "pkgbase = empty-qualifier\n"
            "source_ = value\n",
            ErrorCode::InvalidArchitectureQualifier, 2,
            "empty architecture qualifier");
    expect_failure(
            "pkgbase = any-qualifier\n"
            "source_any = value\n",
            ErrorCode::InvalidArchitectureQualifier, 2,
            "any architecture qualifier");
    expect_failure(
            "pkgbase = invalid-qualifier\n"
            "source_x86-64 = value\n",
            ErrorCode::MalformedLine, 2,
            "invalid architecture qualifier token");
    expect_failure("", ErrorCode::MissingPackageBase, 1, "missing PackageBase");
    expect_failure(
            "pkgbase = duplicate\n"
            "pkgbase = duplicate\n",
            ErrorCode::DuplicatePackageBase, 2, "duplicate PackageBase");
    expect_failure(
            "pkgbase = first\n"
            "pkgbase = second\n",
            ErrorCode::ConflictingPackageBase, 2,
            "conflicting PackageBase");
    expect_failure(
            "pkgbase = duplicate-child\n"
            "pkgname = duplicate-child\n"
            "pkgname = duplicate-child\n",
            ErrorCode::DuplicatePackageName, 3,
            "duplicate child section");
    expect_failure(
            "pkgbase = invalid-package\n"
            "pkgname = bad/name\n",
            ErrorCode::InvalidPackageIdentity, 2,
            "invalid child identity");

    expect_failure(
            "pkgbase = invalid-source\n"
            "source = git+https://example.org/project#branch=one#tag=two\n",
            ErrorCode::InvalidSourceEntry, 2,
            "duplicate selector in source entry",
            SourceEntryParseErrorCode::DuplicateSelector);
    expect_failure(
            "pkgbase = invalid-source\n"
            "source = git+https://example.org/local file\n",
            ErrorCode::InvalidSourceEntry, 2,
            "VCS source whitespace",
            SourceEntryParseErrorCode::Whitespace);
}

void test_unrecognized_syntax_remains_explicit() {
    const ParsedSrcinfoSourceMetadata metadata = parse_success(
            "pkgbase = future-vcs\n"
            "source_aarch64 = repo::cvs+https://example.org/repo#change=1\n",
            "unrecognized VCS metadata");
    const ParsedSrcinfoSourceEntry& entry = metadata.source_entries[0];
    require(entry.architecture_qualifier == "aarch64" &&
                    entry.parsed_source.kind ==
                            ParsedSourceEntryKind::UnrecognizedVcs &&
                    entry.parsed_source.destination_name == "repo" &&
                    entry.parsed_source.vcs.has_value() &&
                    !entry.parsed_source.vcs->recognized_kind.has_value() &&
                    entry.parsed_source.raw_value == entry.raw_value,
            "unrecognized or architecture-specific source gained authority");
}

void test_input_safety_and_line_endings() {
    using ErrorCode = SrcinfoSourceMetadataParseErrorCode;

    std::string nul_source = "pkgbase = control";
    nul_source.push_back('\0');
    nul_source += "identity\n";
    expect_failure(
            nul_source, ErrorCode::ControlCharacter, 1,
            "NUL in SRCINFO");

    std::string control_source = "pkgbase = control";
    control_source.push_back('\x01');
    control_source += "identity\n";
    expect_failure(
            control_source, ErrorCode::ControlCharacter, 1,
            "control byte in SRCINFO");

    const std::string invalid_utf8("pkgbase = bad\xf4\x90\x80\x80\n", 18);
    expect_failure(
            invalid_utf8, ErrorCode::InvalidUtf8, 1,
            "invalid UTF-8 in SRCINFO");

    const std::string unicode_line_separator =
            "pkgbase = bad\xe2\x80\xa8identity\n";
    expect_failure(
            unicode_line_separator, ErrorCode::ControlCharacter, 1,
            "Unicode line separator in SRCINFO");

    expect_failure(
            "pkgbase = embedded-newline\n"
            "source = first\n"
            "continued-value\n",
            ErrorCode::MalformedLine, 3,
            "embedded newline equivalent");

    const ParsedSrcinfoSourceMetadata crlf = parse_success(
            "pkgbase = crlf\r\n"
            "\tsource = https://example.org/source\r\n",
            "CRLF source metadata");
    require(crlf.source_entries.size() == 1,
            "CRLF metadata lost its source");
}

} // namespace

void run_srcinfo_source_metadata_tests() {
    test_basic_and_repeated_source_order();
    test_architecture_qualifiers_preserve_lexical_order();
    test_package_base_and_child_section_boundary();
    test_structural_and_source_failures();
    test_unrecognized_syntax_remains_explicit();
    test_input_safety_and_line_endings();
}
