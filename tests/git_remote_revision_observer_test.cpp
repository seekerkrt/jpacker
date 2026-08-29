#include "git_remote_revision_observer.hpp"

#include "devel_package_classification.hpp"
#include "source_entry_parser.hpp"
#include "srcinfo_source_metadata.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

static_assert(!std::is_default_constructible_v<
              AuthorityApprovedGitSourceIdentity>);
static_assert(!std::is_constructible_v<
              AuthorityApprovedGitSourceIdentity,
              VcsSourceIdentity>);
static_assert(!std::is_constructible_v<
              AuthorityApprovedGitSourceIdentity,
              ParsedSourceEntry>);
static_assert(!std::is_constructible_v<
              AuthorityApprovedGitSourceIdentity,
              ParsedSrcinfoSourceMetadata>);
static_assert(!std::is_constructible_v<
              AuthorityApprovedGitSourceIdentity,
              TrustedDevelSourceMetadata>);
static_assert(!std::is_default_constructible_v<ValidatedHttpsGitRemote>);
static_assert(!std::is_default_constructible_v<ValidatedExactGitBranch>);
static_assert(!std::is_constructible_v<
              ValidatedExactGitBranch,
              std::string>);
static_assert(!std::is_default_constructible_v<
              ValidatedGitRemoteSelector>);
static_assert(!std::is_default_constructible_v<
              GitRemoteRevisionObservationKey>);
static_assert(!std::is_default_constructible_v<
              ValidatedGitRemoteRevisionRequest>);
static_assert(!std::is_constructible_v<
              ValidatedGitRemoteRevisionRequest,
              ParsedSourceEntry>);
static_assert(!std::is_constructible_v<
              ValidatedGitRemoteRevisionRequest,
              ParsedSrcinfoSourceMetadata>);
static_assert(!std::is_constructible_v<
              ValidatedGitRemoteRevisionRequest,
              VcsSourceIdentity>);
static_assert(!std::is_constructible_v<
              ValidatedGitRemoteRevisionRequest,
              TrustedDevelSourceMetadata>);
static_assert(!std::is_convertible_v<
              ParsedSourceEntry,
              ValidatedGitRemoteRevisionRequest>);
static_assert(!std::is_convertible_v<
              ParsedSrcinfoSourceMetadata,
              ValidatedGitRemoteRevisionRequest>);
static_assert(!std::is_convertible_v<
              VcsSourceIdentity,
              ValidatedGitRemoteRevisionRequest>);
static_assert(!std::is_convertible_v<
              SourceAwarePackageIdentity,
              UpstreamGitRevision>);
static_assert(!std::is_constructible_v<
              UpstreamGitRevision,
              const SourceAwarePackageIdentity&>);
static_assert(!std::is_constructible_v<
              ValidatedGitRemoteRevisionRequest,
              const SourceAwarePackageIdentity&>);
static_assert(std::variant_size_v<
                  GitRemoteRevisionObservationResult> == 8);

constexpr std::string_view SHA1 =
    "0123456789abcdef0123456789abcdef01234567";
constexpr std::string_view SHA256 =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_invalid_argument(Function&& function, const std::string& context) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(context + ": expected std::invalid_argument");
}

template <typename Expected>
const Expected& expect_result(
    const GitRemoteRevisionObservationResult& result,
    const std::string& context) {
    const Expected* expected = std::get_if<Expected>(&result);
    if(expected == nullptr) {
        throw std::runtime_error(
            context + ": unexpected result arm " +
            std::to_string(result.index()));
    }
    return *expected;
}

GitRemoteRevisionObservationKey default_key(std::string_view remote) {
    return GitRemoteRevisionObservationKey::make(
        ValidatedHttpsGitRemote::make(remote),
        ValidatedGitRemoteSelector::default_head());
}

GitRemoteRevisionObservationKey exact_key(
    std::string_view remote, std::string branch_name) {
    return GitRemoteRevisionObservationKey::make(
        ValidatedHttpsGitRemote::make(remote),
        ValidatedGitRemoteSelector::exact_branch(
            make_validated_exact_git_branch_fixture_for_test(
                std::move(branch_name))));
}

ValidatedGitRemoteRevisionRequest default_request(
    std::string remote = "https://example.com/repo.git") {
    VcsSourceIdentity source = VcsSourceIdentity::make(
        VcsKind::Git,
        remote,
        VcsSelector::default_head());
    return ValidatedGitRemoteRevisionRequest::make(
        make_authority_approved_git_source_identity_fixture_for_test(
            std::move(source)),
        default_key(remote));
}

ValidatedGitRemoteRevisionRequest exact_request(
    std::string branch_name = "exact",
    std::string remote = "https://example.com/repo.git") {
    VcsSourceIdentity source = VcsSourceIdentity::make(
        VcsKind::Git,
        remote,
        VcsSelector::branch(branch_name));
    return ValidatedGitRemoteRevisionRequest::make(
        make_authority_approved_git_source_identity_fixture_for_test(
            std::move(source)),
        exact_key(remote, std::move(branch_name)));
}

std::string oid_record(std::string_view oid, std::string_view ref) {
    return std::string(oid) + '\t' + std::string(ref) + '\n';
}

std::string symref_record(
    std::string_view target, std::string_view ref = "HEAD") {
    return "ref: " + std::string(target) + '\t' + std::string(ref) + '\n';
}

ObservedGitRemoteRevision expect_observed(
    const ValidatedGitRemoteRevisionRequest& request,
    std::string_view output,
    std::string_view expected_oid,
    const std::string& context) {
    const GitRemoteRevisionObservationResult result =
        parse_git_remote_revision_observation(request, 0, output);
    const auto& observed =
        expect_result<ObservedGitRemoteRevision>(result, context);
    require(observed.request() == request,
            context + ": request identity was not retained");
    require(observed.revision().source() == request.source().source(),
            context + ": source identity was not retained");
    require(observed.revision().value().git_commit() != nullptr &&
                *observed.revision().value().git_commit() == expected_oid,
            context + ": observed OID differs");
    return observed;
}

void expect_malformed(
    const ValidatedGitRemoteRevisionRequest& request,
    int exit_status,
    std::string_view output,
    GitRemoteRevisionMalformedOutputReason reason,
    const std::string& context) {
    const auto result = parse_git_remote_revision_observation(
        request, exit_status, output);
    const auto& malformed_result =
        expect_result<GitRemoteRevisionMalformedOutput>(result, context);
    require(malformed_result.key == request.key(),
            context + ": observation key differs");
    require(malformed_result.reason == reason,
            context + ": malformed reason differs");
}

void expect_ambiguous(
    const ValidatedGitRemoteRevisionRequest& request,
    std::string_view output,
    GitRemoteRevisionAmbiguousOutputReason reason,
    std::size_t record_count,
    const std::string& context) {
    const auto result =
        parse_git_remote_revision_observation(request, 0, output);
    const auto& ambiguous_result =
        expect_result<GitRemoteRevisionAmbiguousOutput>(result, context);
    require(ambiguous_result.key == request.key(),
            context + ": observation key differs");
    require(ambiguous_result.reason == reason &&
                ambiguous_result.record_count == record_count,
            context + ": ambiguity classification differs");
}

void test_https_remote_canonicalization() {
    const auto canonical = ValidatedHttpsGitRemote::make(
        "https://example.com/repo.git");
    require(canonical.canonical_url() ==
                "https://example.com/repo.git",
            "Canonical HTTPS remote changed unexpectedly.");

    const auto normalized = ValidatedHttpsGitRemote::make(
        "HTTPS://EXAMPLE.COM:443/a/../repo.git");
    require(normalized == canonical,
            "Scheme, host, default port, or dot path was not canonicalized.");
    require(ValidatedHttpsGitRemote::make(
                "https://example.com/a/./repo.git")
                    .canonical_url() ==
                "https://example.com/a/repo.git",
            "Single-dot path component behavior differs.");
    require(ValidatedHttpsGitRemote::make(
                "https://example.com/%2e/repo.git") ==
                    canonical &&
                ValidatedHttpsGitRemote::make(
                    "https://example.com/%2E%2E/a/../repo.git") ==
                    canonical,
            "Percent-encoded dot component behavior differs.");
    require(ValidatedHttpsGitRemote::make(
                "https://example.com/%72epo.git")
                    .canonical_url() ==
                "https://example.com/%72epo.git",
            "Non-dot percent encoding was rewritten heuristically.");
    require(ValidatedHttpsGitRemote::make("https://example.com") ==
                ValidatedHttpsGitRemote::make("https://example.com/"),
            "Empty path and slash canonical forms differ.");
    require(ValidatedHttpsGitRemote::make(
                "https://example.com:444/repo.git")
                    .canonical_url() ==
                "https://example.com:444/repo.git",
            "Non-default HTTPS port was discarded.");
    require(ValidatedHttpsGitRemote::make(
                "https://[2001:DB8::1]/repo.git")
                    .canonical_url() ==
                "https://[2001:db8::1]/repo.git",
            "IPv6 literal canonical form differs.");
    require(ValidatedHttpsGitRemote::make(
                "HTTPS://XN--BCHER-KVA.EXAMPLE/repo.git")
                    .canonical_url() ==
                "https://xn--bcher-kva.example/repo.git",
            "ASCII punycode host canonical form differs.");

    const std::string unicode_idn =
        std::string("https://b") + "\xc3\xbc" +
        "cher.example/repo.git";
    expect_invalid_argument(
        [&unicode_idn] {
            static_cast<void>(ValidatedHttpsGitRemote::make(unicode_idn));
        },
        "Raw Unicode IDN host");

    std::string maximum = "https://example.com/";
    maximum.append(
        VALIDATED_HTTPS_GIT_REMOTE_MAX_INPUT_BYTES - maximum.size(),
        'a');
    require(ValidatedHttpsGitRemote::make(maximum).canonical_url() == maximum,
            "8 KiB URL boundary was not accepted.");

    std::string oversized = maximum;
    oversized.push_back('a');
    expect_invalid_argument(
        [&oversized] {
            static_cast<void>(ValidatedHttpsGitRemote::make(oversized));
        },
        "Oversized HTTPS remote");
}

void test_https_remote_rejections() {
    for(const std::string remote : {
            "https://user@example.com/repo.git",
            "https://user:password@example.com/repo.git",
            "https://:password@example.com/repo.git",
            "https://example.com/repo.git?",
            "https://example.com/repo.git?q=1",
            "https://example.com/repo.git#",
            "https://example.com/repo.git#fragment",
            "http://example.com/repo.git",
            "file:///tmp/repo.git",
            "ssh://example.com/repo.git",
            "https://[fe80::1%25eth0]/repo.git",
            "git@example.com:repo.git",
            "git+https://example.com/repo.git",
            "ext::helper command",
            "https://",
            "https:///repo.git"}) {
        expect_invalid_argument(
            [&remote] {
                static_cast<void>(ValidatedHttpsGitRemote::make(remote));
            },
            "Rejected remote " + remote);
    }

    for(char forbidden : {' ', '\t', '\n', '\r', '\f', '\v', '\x01',
                          '\x7f'}) {
        std::string remote = "https://example.com/repo";
        remote.push_back(forbidden);
        remote += ".git";
        expect_invalid_argument(
            [&remote] {
                static_cast<void>(ValidatedHttpsGitRemote::make(remote));
            },
            "Remote with forbidden byte");
    }

    std::string nul = "https://example.com/repo";
    nul.push_back('\0');
    nul += ".git";
    expect_invalid_argument(
        [&nul] {
            static_cast<void>(ValidatedHttpsGitRemote::make(nul));
        },
        "Remote with embedded NUL");
}

void test_selector_key_and_request_model() {
    const auto default_selector =
        ValidatedGitRemoteSelector::default_head();
    require(default_selector.kind() ==
                    ValidatedGitRemoteSelectorKind::DefaultHead &&
                default_selector.exact_branch() == nullptr,
            "Default HEAD selector gained a string payload.");

    const auto exact_selector = ValidatedGitRemoteSelector::exact_branch(
        make_validated_exact_git_branch_fixture_for_test("feature/exact"));
    require(exact_selector.kind() ==
                    ValidatedGitRemoteSelectorKind::ExactBranch &&
                exact_selector.exact_branch() != nullptr &&
                exact_selector.exact_branch()->name() == "feature/exact",
            "Exact branch selector lost its closed payload.");

    std::string maximum_branch(
        VALIDATED_EXACT_GIT_BRANCH_MAX_INPUT_BYTES, 'a');
    require(make_validated_exact_git_branch_fixture_for_test(maximum_branch)
                    .name() == maximum_branch,
            "4 KiB exact branch fixture boundary was not accepted.");
    expect_invalid_argument(
        [] {
            static_cast<void>(
                make_validated_exact_git_branch_fixture_for_test(""));
        },
        "Empty exact branch fixture");
    expect_invalid_argument(
        [] {
            static_cast<void>(
                make_validated_exact_git_branch_fixture_for_test(
                    std::string(
                        VALIDATED_EXACT_GIT_BRANCH_MAX_INPUT_BYTES + 1,
                        'a')));
        },
        "Oversized exact branch fixture");
    expect_invalid_argument(
        [] {
            std::string branch = "bad";
            branch.push_back('\0');
            branch += "branch";
            static_cast<void>(
                make_validated_exact_git_branch_fixture_for_test(
                    std::move(branch)));
        },
        "NUL exact branch fixture");

    const auto canonical_default = default_key(
        "HTTPS://EXAMPLE.COM:443/repo.git");
    require(canonical_default ==
                default_key("https://example.com/repo.git"),
            "Canonical remote was not the key identity.");
    require(canonical_default !=
                default_key("https://example.com/other.git"),
            "Different remotes collapsed into one key.");
    require(canonical_default !=
                exact_key("https://example.com/repo.git", "main"),
            "Default HEAD and exact branch keys collapsed.");
    require(exact_key("https://example.com/repo.git", "main") !=
                exact_key("https://example.com/repo.git", "develop"),
            "Distinct exact branch keys collapsed.");

    const auto request = default_request(
        "HTTPS://EXAMPLE.COM:443/repo.git");
    require(request.key().remote().canonical_url() ==
                    "https://example.com/repo.git" &&
                request.source().source().source_location() ==
                    "HTTPS://EXAMPLE.COM:443/repo.git",
            "Request lost canonical network key or source identity.");

    expect_invalid_argument(
        [] {
            VcsSourceIdentity source = VcsSourceIdentity::make(
                VcsKind::Svn,
                "https://example.com/repo",
                VcsSelector::default_head());
            static_cast<void>(
                make_authority_approved_git_source_identity_fixture_for_test(
                    std::move(source)));
        },
        "Non-Git authority fixture");
    expect_invalid_argument(
        [] {
            VcsSourceIdentity source = VcsSourceIdentity::make(
                VcsKind::Git,
                "https://example.com/repo.git",
                VcsSelector::default_head());
            static_cast<void>(ValidatedGitRemoteRevisionRequest::make(
                make_authority_approved_git_source_identity_fixture_for_test(
                    std::move(source)),
                default_key("https://example.com/other.git")));
        },
        "Mismatched request remote");
    expect_invalid_argument(
        [] {
            VcsSourceIdentity source = VcsSourceIdentity::make(
                VcsKind::Git,
                "https://example.com/repo.git",
                VcsSelector::branch("main"));
            static_cast<void>(ValidatedGitRemoteRevisionRequest::make(
                make_authority_approved_git_source_identity_fixture_for_test(
                    std::move(source)),
                exact_key(
                    "https://example.com/repo.git", "develop")));
        },
        "Mismatched request branch");
    expect_invalid_argument(
        [] {
            VcsSourceIdentity source = VcsSourceIdentity::make(
                VcsKind::Git,
                "https://example.com/repo.git",
                VcsSelector::tag("v1"));
            static_cast<void>(ValidatedGitRemoteRevisionRequest::make(
                make_authority_approved_git_source_identity_fixture_for_test(
                    std::move(source)),
                default_key("https://example.com/repo.git")));
        },
        "Unsupported request selector");
}

void test_result_arms_are_distinct() {
    const auto key = default_key("https://example.com/repo.git");
    const GitRemoteRevisionObservationResult timeout =
        GitRemoteRevisionTimeout{key};
    const GitRemoteRevisionObservationResult process_failure =
        GitRemoteRevisionProcessFailure{key};
    const GitRemoteRevisionObservationResult git_exit_failure =
        GitRemoteRevisionGitExitFailure{key, 128};
    const GitRemoteRevisionObservationResult capture_failure =
        GitRemoteRevisionCaptureLimitExceeded{key, 16U * 1024U};

    static_cast<void>(
        expect_result<GitRemoteRevisionTimeout>(timeout, "Timeout arm"));
    static_cast<void>(expect_result<GitRemoteRevisionProcessFailure>(
        process_failure, "ProcessFailure arm"));
    require(expect_result<GitRemoteRevisionGitExitFailure>(
                git_exit_failure, "GitExitFailure arm")
                    .exit_code == 128,
            "Git exit code was not retained.");
    require(expect_result<GitRemoteRevisionCaptureLimitExceeded>(
                capture_failure, "CaptureLimitExceeded arm")
                    .capture_limit == 16U * 1024U,
            "Capture limit was not retained.");
}

void test_default_head_success_and_status_mapping() {
    const auto request = default_request();
    const auto& symbolic_sha1 = expect_observed(
        request,
        symref_record("refs/heads/main") + oid_record(SHA1, "HEAD"),
        SHA1,
        "Symbolic SHA-1 HEAD");
    require(symbolic_sha1.revision().value().git_object_format() != nullptr &&
                *symbolic_sha1.revision().value().git_object_format() ==
                    GitObjectFormat::Sha1,
            "Symbolic SHA-1 object format differs.");
    static_cast<void>(expect_observed(
        request, oid_record(SHA1, "HEAD"), SHA1,
        "Detached SHA-1 HEAD"));

    const auto& symbolic_sha256 = expect_observed(
        request,
        symref_record("refs/heads/main") + oid_record(SHA256, "HEAD"),
        SHA256,
        "Symbolic SHA-256 HEAD");
    require(
        symbolic_sha256.revision().value().git_object_format() != nullptr &&
            *symbolic_sha256.revision().value().git_object_format() ==
                GitObjectFormat::Sha256,
        "Symbolic SHA-256 object format differs.");
    static_cast<void>(expect_observed(
        request, oid_record(SHA256, "HEAD"), SHA256,
        "Detached SHA-256 HEAD"));

    expect_malformed(
        request,
        0,
        "",
        GitRemoteRevisionMalformedOutputReason::EmptyOutput,
        "Status 0 empty output");
    const auto ref_not_found = parse_git_remote_revision_observation(
        request, 2, "");
    require(expect_result<GitRemoteRevisionRefNotFound>(
                ref_not_found, "Status 2 empty output")
                    .key == request.key(),
            "RefNotFound key differs.");
    expect_malformed(
        request,
        2,
        oid_record(SHA1, "HEAD"),
        GitRemoteRevisionMalformedOutputReason::
            RefNotFoundStatusWithOutput,
        "Status 2 nonempty output");
    const auto git_failure = parse_git_remote_revision_observation(
        request, 128, oid_record(SHA1, "HEAD"));
    require(expect_result<GitRemoteRevisionGitExitFailure>(
                git_failure, "Nonzero Git exit")
                    .exit_code == 128,
            "Nonzero Git exit was parsed as success.");
}

void test_default_head_malformed_transcripts() {
    const auto request = default_request();
    expect_malformed(
        request,
        0,
        oid_record(std::string(40, 'A'), "HEAD"),
        GitRemoteRevisionMalformedOutputReason::InvalidRecord,
        "Uppercase HEAD OID");
    for(std::size_t width : {39U, 41U, 63U, 65U}) {
        expect_malformed(
            request,
            0,
            oid_record(std::string(width, 'a'), "HEAD"),
            GitRemoteRevisionMalformedOutputReason::InvalidRecord,
            "Bad HEAD OID width " + std::to_string(width));
    }
    std::string bad_hex(40, 'a');
    bad_hex[20] = 'g';
    expect_malformed(
        request,
        0,
        oid_record(bad_hex, "HEAD"),
        GitRemoteRevisionMalformedOutputReason::InvalidRecord,
        "Non-hex HEAD OID");
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "refs/heads/main"),
        GitRemoteRevisionMalformedOutputReason::WrongRef,
        "HEAD OID wrong ref");
    expect_malformed(
        request,
        0,
        symref_record("refs/tags/v1") + oid_record(SHA1, "HEAD"),
        GitRemoteRevisionMalformedOutputReason::InvalidSymbolicHeadTarget,
        "Unexpected symbolic HEAD namespace");
    expect_malformed(
        request,
        0,
        symref_record("refs/heads/") + oid_record(SHA1, "HEAD"),
        GitRemoteRevisionMalformedOutputReason::InvalidSymbolicHeadTarget,
        "Empty symbolic HEAD branch tail");
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "HEAD") + symref_record("refs/heads/main"),
        GitRemoteRevisionMalformedOutputReason::WrongRecordOrder,
        "Reversed symbolic HEAD records");
    expect_malformed(
        request,
        0,
        symref_record("refs/heads/main") +
            symref_record("refs/heads/other") +
            oid_record(SHA1, "HEAD"),
        GitRemoteRevisionMalformedOutputReason::UnexpectedSymref,
        "Multiple symbolic HEAD records");

    expect_malformed(
        request,
        0,
        std::string(SHA1) + " HEAD\n",
        GitRemoteRevisionMalformedOutputReason::InvalidRecord,
        "Space delimiter");
    expect_malformed(
        request,
        0,
        std::string(SHA1) + "\tHEAD\r\n",
        GitRemoteRevisionMalformedOutputReason::UnexpectedControlByte,
        "CRLF transcript");
    expect_malformed(
        request,
        0,
        std::string(SHA1) + "\tHEAD",
        GitRemoteRevisionMalformedOutputReason::MissingFinalLineFeed,
        "Partial final line");

    std::string nul = std::string(SHA1) + "\tHE";
    nul.push_back('\0');
    nul += "AD\n";
    expect_malformed(
        request,
        0,
        nul,
        GitRemoteRevisionMalformedOutputReason::UnexpectedControlByte,
        "NUL transcript");
    std::string c0 = std::string(SHA1) + "\tHE";
    c0.push_back('\x01');
    c0 += "AD\n";
    expect_malformed(
        request,
        0,
        c0,
        GitRemoteRevisionMalformedOutputReason::UnexpectedControlByte,
        "C0 transcript");
    std::string del = std::string(SHA1) + "\tHE";
    del.push_back('\x7f');
    del += "AD\n";
    expect_malformed(
        request,
        0,
        del,
        GitRemoteRevisionMalformedOutputReason::UnexpectedControlByte,
        "DEL transcript");
}

void test_default_head_ambiguous_transcripts() {
    const auto request = default_request();
    expect_ambiguous(
        request,
        oid_record(SHA1, "HEAD") + oid_record(SHA1, "HEAD"),
        GitRemoteRevisionAmbiguousOutputReason::DuplicateExpectedRecord,
        2,
        "Duplicate detached HEAD");
    expect_ambiguous(
        request,
        oid_record(SHA1, "HEAD") +
            oid_record(SHA1, "refs/archive/HEAD"),
        GitRemoteRevisionAmbiguousOutputReason::ExtraOidRecord,
        2,
        "Extra HEAD tail-match ref");
    expect_ambiguous(
        request,
        symref_record("refs/heads/main") + oid_record(SHA1, "HEAD") +
            oid_record(SHA1, "refs/archive/HEAD"),
        GitRemoteRevisionAmbiguousOutputReason::ExtraOidRecord,
        3,
        "Third HEAD record");
}

void test_exact_branch_success() {
    const auto request = exact_request();
    static_cast<void>(expect_observed(
        request,
        oid_record(SHA1, "refs/heads/exact"),
        SHA1,
        "Exact branch SHA-1"));
    const auto& sha256 = expect_observed(
        request,
        oid_record(SHA256, "refs/heads/exact"),
        SHA256,
        "Exact branch SHA-256");
    require(sha256.revision().value().git_object_format() != nullptr &&
                *sha256.revision().value().git_object_format() ==
                    GitObjectFormat::Sha256,
            "Exact branch SHA-256 object format differs.");
}

void test_exact_branch_rejections() {
    const auto request = exact_request();
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "refs/heads/other"),
        GitRemoteRevisionMalformedOutputReason::WrongRef,
        "Wrong exact branch");
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "refs/archive/refs/heads/exact"),
        GitRemoteRevisionMalformedOutputReason::WrongRef,
        "Tail-match other namespace");
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "refs/heads/archive/refs/heads/exact"),
        GitRemoteRevisionMalformedOutputReason::WrongRef,
        "Nested branch tail match");
    expect_ambiguous(
        request,
        oid_record(SHA1, "refs/heads/exact") +
            oid_record(SHA1, "refs/heads/exact"),
        GitRemoteRevisionAmbiguousOutputReason::DuplicateExpectedRecord,
        2,
        "Duplicate same exact branch OID");
    expect_ambiguous(
        request,
        oid_record(SHA1, "refs/heads/exact") +
            oid_record(std::string(40, 'a'), "refs/heads/exact"),
        GitRemoteRevisionAmbiguousOutputReason::DuplicateExpectedRecord,
        2,
        "Duplicate different exact branch OID");
    expect_ambiguous(
        request,
        oid_record(SHA1, "refs/heads/exact") +
            oid_record(SHA1, "refs/heads/other"),
        GitRemoteRevisionAmbiguousOutputReason::ExtraOidRecord,
        2,
        "Extra exact branch record");
    expect_malformed(
        request,
        0,
        symref_record("refs/heads/exact", "refs/heads/exact"),
        GitRemoteRevisionMalformedOutputReason::UnexpectedSymref,
        "Exact branch symref");
    expect_malformed(
        request,
        0,
        oid_record(SHA1, "refs/heads/exact^{}"),
        GitRemoteRevisionMalformedOutputReason::WrongRef,
        "Exact branch peeled-looking record");
    expect_malformed(
        request,
        0,
        oid_record(std::string(40, 'A'), "refs/heads/exact"),
        GitRemoteRevisionMalformedOutputReason::InvalidRecord,
        "Exact branch uppercase OID");
    expect_malformed(
        request,
        0,
        std::string(SHA1) + "\trefs/heads/exact",
        GitRemoteRevisionMalformedOutputReason::MissingFinalLineFeed,
        "Exact branch partial line");

    std::string control = std::string(SHA1) + "\trefs/heads/ex";
    control.push_back('\x01');
    control += "act\n";
    expect_malformed(
        request,
        0,
        control,
        GitRemoteRevisionMalformedOutputReason::UnexpectedControlByte,
        "Exact branch control byte");
}

} // namespace

int main() {
    try {
        test_https_remote_canonicalization();
        test_https_remote_rejections();
        test_selector_key_and_request_model();
        test_result_arms_are_distinct();
        test_default_head_success_and_status_mapping();
        test_default_head_malformed_transcripts();
        test_default_head_ambiguous_transcripts();
        test_exact_branch_success();
        test_exact_branch_rejections();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "Git remote revision observer Slice 1 tests: all checks passed\n";
    return 0;
}
