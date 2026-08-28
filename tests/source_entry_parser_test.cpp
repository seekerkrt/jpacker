#include "source_entry_parser.hpp"

#include "devel_package_classification.hpp"
#include "devel_update_model.hpp"
#include "vcs_source_identity.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

static_assert(!std::is_convertible_v<ParsedSourceEntry, VcsSourceIdentity>);
static_assert(!std::is_constructible_v<
              VcsSourceIdentity,
              const ParsedSourceEntry&>);
static_assert(!std::is_convertible_v<
              ParsedSourceEntry,
              TrustedDevelSourceMetadata>);
static_assert(!std::is_constructible_v<
              TrustedDevelSourceMetadata,
              const ParsedSourceEntry&>);
static_assert(!std::is_constructible_v<
              DevelPackageClassification,
              const ParsedSourceEntry&>);
static_assert(!std::is_constructible_v<
              DevelUpdateAssessment,
              const ParsedSourceEntry&>);
static_assert(!std::is_convertible_v<ParsedSourceVcsKind, VcsKind>);

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ParsedSourceEntry parse_success(
    std::string_view value, const std::string& context) {
    const SourceEntryParseResult result = parse_source_entry(value);
    require(result.is_success(), context + ": parse did not succeed");
    require(result.entry() != nullptr, context + ": entry is absent");
    require(result.failure() == nullptr, context + ": failure is exposed");
    return *result.entry();
}

void expect_failure(
    std::string_view value, SourceEntryParseErrorCode code,
    const std::string& context) {
    const SourceEntryParseResult result = parse_source_entry(value);
    require(!result.is_success(), context + ": parse unexpectedly succeeded");
    require(result.entry() == nullptr, context + ": partial entry is exposed");
    require(result.failure() != nullptr, context + ": failure is absent");
    require(
        *result.failure() == SourceEntryParseFailure{code},
        context + ": failure code differs");
}

void test_ordinary_and_destination_sources() {
    const ParsedSourceEntry remote = parse_success(
        "https://example.org/releases/foo.tar.zst",
        "ordinary HTTPS source");
    require(remote.kind == ParsedSourceEntryKind::Remote &&
                remote.raw_value ==
                    "https://example.org/releases/foo.tar.zst" &&
                remote.source_payload == remote.raw_value &&
                remote.source_location == remote.raw_value &&
                remote.transport_scheme == "https" &&
                !remote.destination_name.has_value() &&
                !remote.vcs.has_value(),
            "ordinary HTTPS syntax was changed");

    const ParsedSourceEntry local =
        parse_success("helper.patch", "local source");
    require(local.kind == ParsedSourceEntryKind::Local &&
                local.source_location == "helper.patch" &&
                !local.transport_scheme.has_value() &&
                !local.vcs.has_value(),
            "local source was promoted to a remote or VCS source");

    const ParsedSourceEntry local_with_space =
        parse_success("helper patch.diff", "local source with space");
    require(local_with_space.kind == ParsedSourceEntryKind::Local &&
                local_with_space.source_location == "helper patch.diff",
            "valid local source whitespace was not preserved");

    const ParsedSourceEntry renamed = parse_success(
        "foo.tar.zst::https://example.org/releases/foo.tar.zst",
        "renamed ordinary source");
    require(renamed.destination_name == "foo.tar.zst" &&
                renamed.source_payload ==
                    "https://example.org/releases/foo.tar.zst" &&
                renamed.source_location == renamed.source_payload &&
                renamed.kind == ParsedSourceEntryKind::Remote,
            "destination name was mixed into the source location");

    const ParsedSourceEntry delimiter_in_url = parse_success(
        "mirror::https://example.org/path::artifact",
        "first destination delimiter");
    require(delimiter_in_url.destination_name == "mirror" &&
                delimiter_in_url.source_payload ==
                    "https://example.org/path::artifact",
            "makepkg first-delimiter behavior was not retained");

    expect_failure(
        "::https://example.org/source",
        SourceEntryParseErrorCode::EmptyDestination,
        "empty destination");
    expect_failure(
        "directory/name::https://example.org/source",
        SourceEntryParseErrorCode::InvalidDestination,
        "non-leaf destination");
    expect_failure(
        "name::", SourceEntryParseErrorCode::EmptySource,
        "empty renamed source");
}

void test_git_source_forms() {
    const ParsedSourceEntry default_head = parse_success(
        "git+https://example.org/foo.git", "Git default selector");
    require(default_head.kind == ParsedSourceEntryKind::Vcs &&
                default_head.source_location ==
                    "https://example.org/foo.git" &&
                default_head.transport_scheme == "https" &&
                default_head.vcs.has_value() &&
                default_head.vcs->raw_identifier == "git" &&
                default_head.vcs->recognized_kind ==
                    ParsedSourceVcsKind::Git &&
                default_head.vcs->declaration_kind ==
                    ParsedSourceVcsDeclarationKind::ExplicitPrefix &&
                !default_head.vcs->selector.has_value() &&
                !default_head.vcs->query.has_value() &&
                default_head.vcs->component_order ==
                    ParsedSourceVcsComponentOrder::None,
            "Git default selector gained unsupported meaning");

    const ParsedSourceEntry renamed = parse_success(
        "repo::git+https://example.org/foo.git",
        "renamed Git source");
    require(renamed.destination_name == "repo" &&
                renamed.source_payload ==
                    "git+https://example.org/foo.git" &&
                renamed.source_location ==
                    "https://example.org/foo.git",
            "Git destination and location were flattened");

    const ParsedSourceEntry native = parse_success(
        "git://example.org/foo.git#branch=main",
        "native Git scheme");
    require(native.transport_scheme == "git" && native.vcs.has_value() &&
                native.vcs->declaration_kind ==
                    ParsedSourceVcsDeclarationKind::NativeScheme &&
                native.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Branch,
            "native Git scheme was not retained separately");

    const ParsedSourceEntry branch = parse_success(
        "git+https://example.org/foo.git#branch=main", "Git branch");
    const ParsedSourceEntry tag = parse_success(
        "git+https://example.org/foo.git#tag=v1.0.0", "Git tag");
    const ParsedSourceEntry commit = parse_success(
        "git+https://example.org/foo.git#commit=0123456789abcdef",
        "Git commit");
    require(branch.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Branch &&
                branch.vcs->selector->value == "main" &&
                tag.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Tag &&
                commit.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Commit,
            "Git selector roles were flattened");

    const ParsedSourceEntry unknown_selector = parse_success(
        "git+https://example.org/foo.git#revision=42",
        "unrecognized Git selector");
    require(unknown_selector.vcs->selector.has_value() &&
                unknown_selector.vcs->selector->key == "revision" &&
                unknown_selector.vcs->selector->value == "42" &&
                !unknown_selector.vcs->selector->recognized_role
                     .has_value(),
            "unrecognized Git selector became default HEAD");
}

void test_vcs_specific_selector_value_boundaries() {
    const ParsedSourceEntry git = parse_success(
        "git+https://example.org/repo#branch=foo=bar",
        "Git selector with multiple value separators");
    const ParsedSourceEntry hg = parse_success(
        "hg+https://example.org/repo#branch=stable=tip",
        "Hg selector with multiple value separators");
    const ParsedSourceEntry svn = parse_success(
        "svn+https://example.org/repo#revision=r1=r2",
        "SVN selector with multiple value separators");
    const ParsedSourceEntry fossil = parse_success(
        "fossil+https://example.org/repo#tag=v1=latest",
        "Fossil selector with multiple value separators");
    const ParsedSourceEntry bzr = parse_success(
        "bzr+https://example.org/repo#revision=rev=part",
        "Bzr selector with multiple value separators");

    require(git.vcs->selector->raw_fragment == "branch=foo=bar" &&
                git.vcs->selector->key == "branch" &&
                git.vcs->selector->value == "bar" &&
                git.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Branch &&
                hg.vcs->selector->value == "tip" &&
                svn.vcs->selector->value == "r2" &&
                fossil.vcs->selector->value == "latest" &&
                bzr.vcs->selector->value == "rev=part",
            "makepkg VCS-specific selector value boundaries were flattened");

    expect_failure(
        "git+https://example.org/repo#branch==",
        SourceEntryParseErrorCode::MalformedSelector,
        "empty makepkg-equivalent Git selector value");
}

void test_other_vcs_and_unrecognized_prefixes() {
    const ParsedSourceEntry svn = parse_success(
        "svn+https://example.org/project#revision=r123", "SVN source");
    const ParsedSourceEntry hg = parse_success(
        "hg+https://example.org/project#branch=stable", "Hg source");
    const ParsedSourceEntry bzr = parse_success(
        "bzr+lp:example#revision=42", "Bzr Launchpad source");
    const ParsedSourceEntry fossil = parse_success(
        "fossil+https://example.org/project#tag=v1", "Fossil source");

    require(svn.vcs->recognized_kind == ParsedSourceVcsKind::Svn &&
                svn.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Revision &&
                hg.vcs->recognized_kind == ParsedSourceVcsKind::Hg &&
                hg.vcs->selector->recognized_role ==
                    ParsedSourceSelectorRole::Branch &&
                bzr.transport_scheme == "lp" &&
                bzr.vcs->recognized_kind == ParsedSourceVcsKind::Bzr &&
                fossil.vcs->recognized_kind ==
                    ParsedSourceVcsKind::Fossil,
            "recognized non-Git VCS syntax was flattened");

    const ParsedSourceEntry unknown = parse_success(
        "cvs+https://example.org/project#branch=main",
        "curated unrecognized VCS prefix");
    require(unknown.kind == ParsedSourceEntryKind::UnrecognizedVcs &&
                unknown.vcs.has_value() &&
                unknown.vcs->raw_identifier == "cvs" &&
                !unknown.vcs->recognized_kind.has_value() &&
                !unknown.vcs->selector.has_value() &&
                unknown.source_location ==
                    "https://example.org/project#branch=main",
            "unknown VCS-like prefix became an ordinary or known source");

    const ParsedSourceEntry composite_https = parse_success(
        "https+unix://socket/path", "ordinary composite HTTPS scheme");
    const ParsedSourceEntry composite_custom = parse_success(
        "foo+bar://example.org/path", "arbitrary composite URI scheme");
    require(composite_https.kind == ParsedSourceEntryKind::Remote &&
                composite_https.transport_scheme == "https+unix" &&
                composite_https.source_location ==
                    "https+unix://socket/path" &&
                !composite_https.vcs.has_value() &&
                composite_custom.kind == ParsedSourceEntryKind::Remote &&
                composite_custom.transport_scheme == "foo+bar" &&
                !composite_custom.vcs.has_value(),
            "arbitrary composite URI scheme became VCS evidence");

    const ParsedSourceEntry file_url =
        parse_success("file:///tmp/source", "file URL source");
    const ParsedSourceEntry ssh_url = parse_success(
        "ssh://example.org/source", "ordinary SSH URL source");
    const ParsedSourceEntry custom_transport = parse_success(
        "git+custom://example.org/project", "custom Git transport");
    const ParsedSourceEntry credential_url = parse_success(
        "git+https://user:secret@example.org/project.git",
        "credential-bearing Git URL syntax");
    require(file_url.kind == ParsedSourceEntryKind::Remote &&
                file_url.transport_scheme == "file" &&
                ssh_url.kind == ParsedSourceEntryKind::Remote &&
                ssh_url.transport_scheme == "ssh" &&
                custom_transport.kind == ParsedSourceEntryKind::Vcs &&
                custom_transport.transport_scheme == "custom" &&
                credential_url.kind == ParsedSourceEntryKind::Vcs &&
                credential_url.source_location ==
                    "https://user:secret@example.org/project.git",
            "parseable transport syntax gained or lost policy meaning");
}

void test_query_fragment_and_malformed_components() {
    const ParsedSourceEntry query_then_fragment = parse_success(
        "git+https://example.org/project?signed#branch=next",
        "signed query before fragment");
    const ParsedSourceEntry fragment_then_query = parse_success(
        "git+https://example.org/project#branch=next?signed",
        "signed query after fragment");
    require(query_then_fragment.vcs->query->recognized_flag ==
                    ParsedSourceQueryFlag::Signed &&
                query_then_fragment.vcs->query->raw_query == "signed" &&
                query_then_fragment.vcs->selector->value == "next" &&
                query_then_fragment.vcs->component_order ==
                    ParsedSourceVcsComponentOrder::QueryThenFragment &&
                fragment_then_query.vcs->query->recognized_flag ==
                    ParsedSourceQueryFlag::Signed &&
                fragment_then_query.vcs->selector->value == "next" &&
                fragment_then_query.vcs->component_order ==
                    ParsedSourceVcsComponentOrder::FragmentThenQuery,
            "makepkg signed query ordering was lost");

    const ParsedSourceEntry ordinary_query = parse_success(
        "https://example.org/archive.tar.zst?download=1#mirror",
        "ordinary URL query");
    require(ordinary_query.kind == ParsedSourceEntryKind::Remote &&
                ordinary_query.source_location ==
                    "https://example.org/archive.tar.zst?download=1#mirror" &&
                !ordinary_query.vcs.has_value(),
            "ordinary URL query gained makepkg VCS semantics");

    const ParsedSourceEntry unknown_query = parse_success(
        "git+https://example.org/project?depth=1",
        "unrecognized Git query");
    require(unknown_query.vcs->query.has_value() &&
                unknown_query.vcs->query->raw_query == "depth=1" &&
                !unknown_query.vcs->query->recognized_flag.has_value(),
            "unrecognized Git query became signed or disappeared");

    expect_failure(
        "git+https://example.org/project#branch",
        SourceEntryParseErrorCode::MalformedSelector,
        "selector without value separator");
    expect_failure(
        "git+https://example.org/project#branch=",
        SourceEntryParseErrorCode::MalformedSelector,
        "empty selector value");
    expect_failure(
        "git+https://example.org/project#branch=main#tag=v1",
        SourceEntryParseErrorCode::DuplicateSelector,
        "duplicate selector");
    expect_failure(
        "git+https://example.org/project?signed?signed",
        SourceEntryParseErrorCode::DuplicateQuery,
        "duplicate query");
    expect_failure(
        "git+https://example.org/project?",
        SourceEntryParseErrorCode::MalformedQuery,
        "empty VCS query");
}

void test_non_git_query_bytes_follow_vcs_owner_semantics() {
    const ParsedSourceEntry hg_suffix = parse_success(
        "hg+https://example.org/repo#branch=stable?custom",
        "Hg question mark after selector");
    const ParsedSourceEntry svn_suffix = parse_success(
        "svn+https://example.org/repo#revision=1?custom",
        "SVN question mark after selector");
    const ParsedSourceEntry bzr_suffix = parse_success(
        "bzr+https://example.org/repo#revision=rev?custom",
        "Bzr question mark after selector");
    require(hg_suffix.vcs->selector->raw_fragment ==
                    "branch=stable?custom" &&
                hg_suffix.vcs->selector->value == "stable?custom" &&
                !hg_suffix.vcs->query.has_value() &&
                hg_suffix.vcs->component_order ==
                    ParsedSourceVcsComponentOrder::FragmentOnly &&
                svn_suffix.vcs->selector->value == "1?custom" &&
                !svn_suffix.vcs->query.has_value() &&
                bzr_suffix.vcs->selector->value == "rev?custom" &&
                !bzr_suffix.vcs->query.has_value(),
            "non-Git selector suffix was normalized as a Git query");

    const ParsedSourceEntry hg_location = parse_success(
        "hg+https://example.org/repo?token=1#branch=stable",
        "Hg question mark before selector");
    const ParsedSourceEntry svn_location = parse_success(
        "svn+https://example.org/repo?token=1#revision=2",
        "SVN question mark before selector");
    const ParsedSourceEntry bzr_location = parse_success(
        "bzr+https://example.org/repo?token=1#revision=3",
        "Bzr question mark before selector");
    require(hg_location.source_location ==
                    "https://example.org/repo?token=1" &&
                hg_location.vcs->selector->value == "stable" &&
                !hg_location.vcs->query.has_value() &&
                svn_location.source_location ==
                    "https://example.org/repo?token=1" &&
                svn_location.vcs->selector->value == "2" &&
                !svn_location.vcs->query.has_value() &&
                bzr_location.source_location ==
                    "https://example.org/repo?token=1" &&
                bzr_location.vcs->selector->value == "3" &&
                !bzr_location.vcs->query.has_value(),
            "non-Git remote location lost question-mark bytes");

    const ParsedSourceEntry fossil_signed = parse_success(
        "fossil+https://example.org/repo#tag=v1?signed",
        "Fossil signed query syntax");
    require(fossil_signed.vcs->selector->value == "v1" &&
                fossil_signed.vcs->query->raw_query == "signed" &&
                !fossil_signed.vcs->query->recognized_flag.has_value(),
            "non-Git signed text gained the Git signature flag");
}

void test_input_safety_and_raw_preservation() {
    expect_failure("", SourceEntryParseErrorCode::EmptyValue, "empty source");
    expect_failure(
        "git+https://example.org/source with-space",
        SourceEntryParseErrorCode::Whitespace,
        "VCS source whitespace");
    expect_failure(
        "git+https://", SourceEntryParseErrorCode::InvalidSourceLocation,
        "empty VCS URL");

    std::string nul_source = "local";
    nul_source.push_back('\0');
    nul_source += "file";
    expect_failure(
        nul_source, SourceEntryParseErrorCode::ControlCharacter,
        "NUL in source");

    const std::string invalid_utf8("bad\xf4\x90\x80\x80", 7);
    expect_failure(
        invalid_utf8, SourceEntryParseErrorCode::InvalidUtf8,
        "invalid UTF-8 source");

    const std::string raw =
        "repo::git+ssh://git@example.org/project.git#branch=next?signed";
    const ParsedSourceEntry parsed = parse_success(raw, "raw preservation");
    require(parsed.raw_value == raw &&
                parsed.source_payload == raw.substr(6) &&
                parsed.transport_scheme == "ssh" &&
                parsed.source_location ==
                    "ssh://git@example.org/project.git" &&
                parsed.vcs->selector->raw_fragment == "branch=next" &&
                parsed.vcs->query->raw_query == "signed",
            "raw source syntax was not preserved losslessly");
}

} // namespace

void run_source_entry_parser_tests() {
    test_ordinary_and_destination_sources();
    test_git_source_forms();
    test_vcs_specific_selector_value_boundaries();
    test_other_vcs_and_unrecognized_prefixes();
    test_query_fragment_and_malformed_components();
    test_non_git_query_bytes_follow_vcs_owner_semantics();
    test_input_safety_and_raw_preservation();
}
