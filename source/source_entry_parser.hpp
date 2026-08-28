#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

enum class SourceSyntaxTextStatus {
    Valid,
    InvalidUtf8,
    ControlCharacter,
};

enum class ParsedSourceEntryKind {
    Local,
    Remote,
    Vcs,
    UnrecognizedVcs,
};

// This is makepkg source syntax, not Moguet's production tracking policy.
// Fossil is retained here because current makepkg recognizes it, while the
// Issue #270 VcsKind candidate set intentionally remains unchanged.
enum class ParsedSourceVcsKind {
    Bzr,
    Fossil,
    Git,
    Hg,
    Svn,
};

enum class ParsedSourceVcsDeclarationKind {
    ExplicitPrefix,
    NativeScheme,
};

enum class ParsedSourceSelectorRole {
    Branch,
    Commit,
    Revision,
    Tag,
};

enum class ParsedSourceQueryFlag {
    Signed,
};

enum class ParsedSourceVcsComponentOrder {
    None,
    QueryOnly,
    FragmentOnly,
    QueryThenFragment,
    FragmentThenQuery,
};

struct ParsedSourceSelector {
    std::string raw_fragment;
    std::string key;
    std::string value;
    std::optional<ParsedSourceSelectorRole> recognized_role;

    bool operator==(const ParsedSourceSelector&) const = default;
};

struct ParsedSourceQuery {
    std::string raw_query;
    std::optional<ParsedSourceQueryFlag> recognized_flag;

    bool operator==(const ParsedSourceQuery&) const = default;
};

struct ParsedSourceVcsSyntax {
    std::string raw_identifier;
    std::optional<ParsedSourceVcsKind> recognized_kind;
    ParsedSourceVcsDeclarationKind declaration_kind;
    std::optional<ParsedSourceSelector> selector;
    std::optional<ParsedSourceQuery> query;
    ParsedSourceVcsComponentOrder component_order;

    bool operator==(const ParsedSourceVcsSyntax&) const = default;
};

// ParsedSourceEntry preserves syntax only. It does not validate transport
// policy, confer trust, authorize network access, or imply tracking support.
struct ParsedSourceEntry {
    std::string raw_value;
    std::optional<std::string> destination_name;
    std::string source_payload;
    ParsedSourceEntryKind kind;
    std::string source_location;
    std::optional<std::string> transport_scheme;
    std::optional<ParsedSourceVcsSyntax> vcs;

    bool operator==(const ParsedSourceEntry&) const = default;
};

enum class SourceEntryParseErrorCode {
    EmptyValue,
    InvalidUtf8,
    ControlCharacter,
    Whitespace,
    EmptyDestination,
    InvalidDestination,
    EmptySource,
    InvalidSourceLocation,
    MalformedSelector,
    MalformedQuery,
    DuplicateSelector,
    DuplicateQuery,
};

struct SourceEntryParseFailure {
    SourceEntryParseErrorCode code;

    bool operator==(const SourceEntryParseFailure&) const = default;
};

class SourceEntryParseResult final {
public:
    SourceEntryParseResult() = delete;
    SourceEntryParseResult(const SourceEntryParseResult&) = default;
    SourceEntryParseResult(SourceEntryParseResult&&) noexcept = default;
    SourceEntryParseResult& operator=(const SourceEntryParseResult&) = delete;
    SourceEntryParseResult& operator=(SourceEntryParseResult&&) noexcept =
        delete;
    ~SourceEntryParseResult() = default;

    [[nodiscard]] bool is_success() const noexcept;
    [[nodiscard]] const ParsedSourceEntry* entry() const noexcept;
    [[nodiscard]] const SourceEntryParseFailure* failure() const noexcept;

private:
    explicit SourceEntryParseResult(ParsedSourceEntry entry) noexcept;
    explicit SourceEntryParseResult(SourceEntryParseFailure failure) noexcept;

    std::variant<ParsedSourceEntry, SourceEntryParseFailure> outcome_;

    friend SourceEntryParseResult parse_source_entry(std::string_view value);
};

// Shared by the two Slice 3A pure parsers so byte validation stays identical.
[[nodiscard]] SourceSyntaxTextStatus validate_source_syntax_text(
    std::string_view value) noexcept;

[[nodiscard]] SourceEntryParseResult parse_source_entry(
    std::string_view value);
