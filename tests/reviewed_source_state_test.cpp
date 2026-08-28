#include "reviewed_source_state.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <typename T, typename Variant>
constexpr bool variant_has_v = false;

template <typename T, typename... Alternatives>
constexpr bool variant_has_v<T, std::variant<Alternatives...>> =
    (std::is_same_v<T, Alternatives> || ...);

static_assert(!std::is_default_constructible_v<ReviewedSourceState>);
static_assert(!std::is_default_constructible_v<ReviewedSourceStateLoaded>);
static_assert(!variant_has_v<
              ReviewedSourceStateMissing, ReviewedSourceStateDocument>);
static_assert(!variant_has_v<
              ReviewedSourceStateSourceMismatch, ReviewedSourceStateDocument>);
static_assert(!variant_has_v<
              ReviewedSourceStateMissing, ReviewedSourceStateInterpretation>);
static_assert(variant_has_v<
              ReviewedSourceStateMissing, ReviewedSourceStateObservation>);
static_assert(variant_has_v<
              ReviewedSourceStateSourceMismatch, ReviewedSourceStateObservation>);
static_assert(variant_has_v<
              ReviewedSourceStateUnsupportedFuture,
              ReviewedSourceStateObservation>);

constexpr std::string_view SHA1_A =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view SHA1_B =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view SHA256_C =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

template <typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument.");
}

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Arm, typename Variant>
const Arm& require_arm(const Variant& value, std::string_view message) {
    const Arm* arm = std::get_if<Arm>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

PackageBaseIdentity aur_package_base(
    const std::string& package_base = "example-base",
    const std::string& remote =
        "https://aur.archlinux.org/example-base.git") {
    return PackageBaseIdentity::make(
        PackageSourceIdentity::aur(
            SourceLocationIdentity::known_git_remote(remote)),
        package_base);
}

ReviewedSourceState aur_reviewed_state(
    const std::string& package_base = "example-base",
    const std::string& remote =
        "https://aur.archlinux.org/example-base.git",
    const std::string& commit = std::string(SHA1_A)) {
    return ReviewedSourceState::make(
        aur_package_base(package_base, remote),
        SourceRevisionIdentity::git_commit(commit));
}

std::string current_schema_document(
    std::string_view package_base = "example-base",
    std::string_view remote =
        "https://aur.archlinux.org/example-base.git",
    std::string_view commit = SHA1_A,
    std::int64_t schema_version = 1) {
    return "schema_version = " + std::to_string(schema_version) + "\n"
                                                                  "source_kind = \"aur\"\n"
                                                                  "package_base = \"" +
           std::string(package_base) +
           "\"\n"
           "canonical_git_remote = \"" +
           std::string(remote) +
           "\"\n"
           "reviewed_commit = \"" +
           std::string(commit) + "\"\n";
}

void test_valid_construction_keeps_aur_package_base_and_complete_revision() {
    const ReviewedSourceState state = aur_reviewed_state();
    require(state.schema_version() == reviewed_source_state_schema_version,
            "Current schema version was not retained.");
    require(state.package_base() == aur_package_base(),
            "PackageBase identity was not retained.");
    require(state.package_base().source().kind() == PackageSourceKind::Aur,
            "Reviewed state lost its AUR source kind.");
    require(state.package_base().source().location().value() != nullptr &&
                *state.package_base().source().location().value() ==
                    "https://aur.archlinux.org/example-base.git",
            "Canonical Git remote was not retained.");
    require(state.reviewed_revision().state() == SourceRevisionState::Known &&
                state.reviewed_revision().git_commit() != nullptr &&
                *state.reviewed_revision().git_commit() == SHA1_A &&
                state.reviewed_revision().git_object_format() != nullptr &&
                *state.reviewed_revision().git_object_format() ==
                    GitObjectFormat::Sha1,
            "Complete reviewed commit identity was not retained.");
}

void test_construction_rejects_non_aur_or_incomplete_authority() {
    const SourceRevisionIdentity known_commit =
        SourceRevisionIdentity::git_commit(std::string(SHA1_A));
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::repository(
                    "extra",
                    SourceLocationIdentity::known_git_remote(
                        "https://gitlab.archlinux.org/archlinux/packaging/packages/example.git")),
                "example-base"),
            known_commit));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::local(
                    SourceLocationIdentity::known_local_path(
                        "/tmp/example")),
                "example-base"),
            known_commit));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote)),
                "example-base"),
            known_commit));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::unavailable(
                        SourceLocationKind::GitRemote,
                        IdentityUnavailableReason::
                            AuthorityUnavailable)),
                "example-base"),
            known_commit));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            aur_package_base(), SourceRevisionIdentity::unknown()));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            aur_package_base(), SourceRevisionIdentity::absent()));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            aur_package_base(),
            SourceRevisionIdentity::unavailable(
                IdentityUnavailableReason::ObservationFailed)));
    });
    expect_invalid_argument([&] {
        static_cast<void>(ReviewedSourceState::make(
            aur_package_base(), SourceRevisionIdentity::inapplicable()));
    });
}

void test_state_identity_is_package_base_and_explicit_source() {
    const ReviewedSourceState first = aur_reviewed_state(
        "suite-base", "https://aur.archlinux.org/suite-base.git");
    const ReviewedSourceState second = aur_reviewed_state(
        "suite-base", "https://aur.archlinux.org/suite-base.git",
        std::string(SHA1_B));
    require(first.package_base() == second.package_base(),
            "Same PackageBase/source should share reviewed-state identity.");
    require(first != second,
            "Distinct reviewed revisions collapsed.");

    const PackageChildIdentity first_child = PackageChildIdentity::make(
        first.package_base(), "suite-cli");
    const PackageChildIdentity second_child = PackageChildIdentity::make(
        first.package_base(), "suite-gui");
    require(first_child.package_base() == second_child.package_base(),
            "Split children must keep one PackageBase identity.");
    require(first.package_base() == first_child.package_base(),
            "Reviewed state must stay on the shared PackageBase, not a child.");

    const ReviewedSourceState other_remote = aur_reviewed_state(
        "suite-base", "https://aur.archlinux.org/other-name.git");
    require(first.package_base() != other_remote.package_base(),
            "PackageBase name plus a different remote collapsed.");
    require(other_remote.package_base().package_base() == "suite-base",
            "Canonical remote must stay an explicit authority.");

    const ReviewedSourceState other_base = aur_reviewed_state(
        "other-base", "https://aur.archlinux.org/suite-base.git");
    require(first.package_base() != other_base.package_base(),
            "URL leaf was treated as PackageBase identity.");
}

void test_schema_round_trip_and_publication_form() {
    const ReviewedSourceState state = aur_reviewed_state(
        "example-base",
        "https://aur.archlinux.org/other-name.git",
        std::string(SHA256_C));
    const std::string encoded = encode_reviewed_source_state(state);
    require(encoded ==
                "schema_version = 1\n"
                "source_kind = \"aur\"\n"
                "package_base = \"example-base\"\n"
                "canonical_git_remote = \"https://aur.archlinux.org/other-name.git\"\n"
                "reviewed_commit = \"" +
                    std::string(SHA256_C) + "\"\n",
            "Canonical publication form drifted.");
    require(encoded.find("suite-cli") == std::string::npos &&
                encoded.find("package_child") == std::string::npos &&
                encoded.find("cache") == std::string::npos &&
                encoded.find("srcinfo") == std::string::npos &&
                encoded.find("pkgver") == std::string::npos &&
                encoded.find("canonical_source_key") == std::string::npos,
            "Publication form stored a non-authority field.");

    const ReviewedSourceStateDocument decoded =
        decode_reviewed_source_state(encoded);
    const ReviewedSourceStateLoaded& loaded =
        require_arm<ReviewedSourceStateLoaded>(
            decoded, "Canonical document did not decode as Loaded.");
    require(loaded.state == state, "Round-trip lost reviewed-state identity.");
    require(loaded.state.reviewed_revision().git_object_format() != nullptr &&
                *loaded.state.reviewed_revision().git_object_format() ==
                    GitObjectFormat::Sha256,
            "SHA-256 reviewed commit format was lost.");
}

void test_decode_accepts_equivalent_current_schema_layout() {
    const std::string shuffled =
        "reviewed_commit = \"" + std::string(SHA1_A) +
        "\"\n"
        "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
        "\n"
        "package_base = \"example-base\"\n"
        "source_kind = \"aur\"\n"
        "schema_version = 1\n";
    const ReviewedSourceStateDocument decoded =
        decode_reviewed_source_state(shuffled);
    const ReviewedSourceStateLoaded& loaded =
        require_arm<ReviewedSourceStateLoaded>(
            decoded, "Equivalent current-schema layout was rejected.");
    require(loaded.state == aur_reviewed_state(),
            "Key order or blank lines changed decoded identity.");
}

void test_required_fields_and_malformed_values_are_invalid() {
    const auto missing_schema = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            "source_kind = \"aur\"\n"
            "package_base = \"example-base\"\n"
            "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
            "reviewed_commit = \"" +
            std::string(SHA1_A) + "\"\n"),
        "Missing schema_version was not Invalid.");
    require(missing_schema.reason ==
                ReviewedSourceStateInvalidReason::MissingSchemaVersion,
            "Missing schema_version reason drifted.");

    const auto comment_only = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state("# no reviewed state fields\n"),
        "Comment-only TOML was not Invalid.");
    require(comment_only.reason ==
                ReviewedSourceStateInvalidReason::MissingSchemaVersion,
            "Comment-only document was not treated as missing schema_version.");

    const auto missing_remote = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            "schema_version = 1\n"
            "source_kind = \"aur\"\n"
            "package_base = \"example-base\"\n"
            "reviewed_commit = \"" +
            std::string(SHA1_A) + "\"\n"),
        "Missing canonical Git remote was not Invalid.");
    require(missing_remote.reason ==
                ReviewedSourceStateInvalidReason::
                    MissingCanonicalGitRemote,
            "Missing remote reason drifted.");

    const auto repository_kind = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            "schema_version = 1\n"
            "source_kind = \"repository\"\n"
            "package_base = \"example-base\"\n"
            "canonical_git_remote = \"https://gitlab.archlinux.org/archlinux/packaging/packages/example.git\"\n"
            "reviewed_commit = \"" +
            std::string(SHA1_A) + "\"\n"),
        "Non-AUR source kind was not Invalid.");
    require(repository_kind.reason ==
                ReviewedSourceStateInvalidReason::UnsupportedSourceKind,
            "Repository source kind was not rejected as unsupported.");

    const auto bad_base = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(current_schema_document(
            "invalid/base")),
        "Malformed PackageBase was not Invalid.");
    require(bad_base.reason ==
                ReviewedSourceStateInvalidReason::MalformedPackageBase,
            "Malformed PackageBase reason drifted.");

    const auto whitespace_remote = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(current_schema_document(
            "example-base",
            "https://aur.archlinux.org/example base.git")),
        "Whitespace Git remote was not Invalid.");
    require(whitespace_remote.reason ==
                ReviewedSourceStateInvalidReason::
                    MalformedCanonicalGitRemote,
            "Malformed remote reason drifted.");

    const auto abbreviated = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(current_schema_document(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            "aaaaaaaa")),
        "Abbreviated commit was not Invalid.");
    require(abbreviated.reason ==
                ReviewedSourceStateInvalidReason::MalformedReviewedCommit,
            "Abbreviated commit reason drifted.");

    const auto uppercase = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(current_schema_document(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")),
        "Uppercase commit was not Invalid.");
    require(uppercase.reason ==
                ReviewedSourceStateInvalidReason::MalformedReviewedCommit,
            "Uppercase commit reason drifted.");

    const auto wrong_type = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            "schema_version = 1\n"
            "source_kind = \"aur\"\n"
            "package_base = \"example-base\"\n"
            "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
            "reviewed_commit = 1\n"),
        "Non-string reviewed commit was not Invalid.");
    require(wrong_type.reason ==
                ReviewedSourceStateInvalidReason::UnexpectedRepresentation,
            "Wrong-typed field was not UnexpectedRepresentation.");

    const auto unknown_key = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            current_schema_document() + "reviewed_at = \"now\"\n"),
        "Unknown current-schema key was not Invalid.");
    require(unknown_key.reason ==
                    ReviewedSourceStateInvalidReason::UnknownKey &&
                unknown_key.field == "reviewed_at",
            "Unknown key was flattened or lost.");

    const auto zero_schema = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(current_schema_document(
            "example-base",
            "https://aur.archlinux.org/example-base.git",
            SHA1_A, 0)),
        "schema_version 0 was not Invalid.");
    require(zero_schema.reason ==
                ReviewedSourceStateInvalidReason::MalformedSchemaVersion,
            "Historical/zero schema was treated as future or loaded.");

    const auto string_schema = require_arm<ReviewedSourceStateInvalid>(
        decode_reviewed_source_state(
            "schema_version = \"1\"\n"
            "source_kind = \"aur\"\n"
            "package_base = \"example-base\"\n"
            "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
            "reviewed_commit = \"" +
            std::string(SHA1_A) + "\"\n"),
        "String schema_version was not Invalid.");
    require(string_schema.reason ==
                ReviewedSourceStateInvalidReason::MalformedSchemaVersion,
            "String schema_version reason drifted.");
}

void test_corrupted_documents_are_not_missing() {
    const auto empty = require_arm<ReviewedSourceStateCorrupted>(
        decode_reviewed_source_state(""),
        "Empty document was not Corrupted.");
    require(empty.reason == ReviewedSourceStateCorruptedReason::EmptyDocument,
            "Empty document reason drifted.");

    const auto whitespace = require_arm<ReviewedSourceStateCorrupted>(
        decode_reviewed_source_state(" \n\t"),
        "Whitespace-only document was not Corrupted.");
    require(whitespace.reason ==
                ReviewedSourceStateCorruptedReason::EmptyDocument,
            "Whitespace-only document reason drifted.");

    const auto invalid_utf8 = require_arm<ReviewedSourceStateCorrupted>(
        decode_reviewed_source_state(std::string("schema_version = 1\n") +
                                     std::string("\xc0\xaf", 2)),
        "Invalid UTF-8 was not Corrupted.");
    require(invalid_utf8.reason ==
                ReviewedSourceStateCorruptedReason::InvalidUtf8,
            "Invalid UTF-8 reason drifted.");

    const auto unparseable = require_arm<ReviewedSourceStateCorrupted>(
        decode_reviewed_source_state("schema_version = [\n"),
        "Unparseable TOML was not Corrupted.");
    require(unparseable.reason ==
                ReviewedSourceStateCorruptedReason::UnparseableDocument,
            "Unparseable document reason drifted.");
}

void test_future_schema_is_identified_without_current_decode() {
    const auto sparse_future =
        require_arm<ReviewedSourceStateUnsupportedFuture>(
            decode_reviewed_source_state("schema_version = 2\n"),
            "Sparse future schema was not UnsupportedFuture.");
    require(sparse_future.schema_version == 2,
            "Future schema version was lost.");

    const auto extra_future =
        require_arm<ReviewedSourceStateUnsupportedFuture>(
            decode_reviewed_source_state(
                "schema_version = 3\n"
                "source_kind = \"aur\"\n"
                "package_base = \"example-base\"\n"
                "canonical_git_remote = \"https://aur.archlinux.org/example-base.git\"\n"
                "reviewed_commit = \"" +
                std::string(SHA1_A) +
                "\"\n"
                "next_field = { keep = true }\n"),
            "Future schema with extra fields was not UnsupportedFuture.");
    require(extra_future.schema_version == 3,
            "Decorated future schema version was lost.");

    const ReviewedSourceStateInterpretation interpreted =
        interpret_reviewed_source_state(
            "schema_version = 2\nreviewed_commit = \"not-a-commit\"\n",
            aur_package_base());
    require_arm<ReviewedSourceStateUnsupportedFuture>(
        interpreted,
        "Future schema was interpreted as current Invalid/Loaded.");
}

void test_source_mismatch_is_distinct_from_invalid() {
    const ReviewedSourceState state = aur_reviewed_state();
    const PackageBaseIdentity other_remote = aur_package_base(
        "example-base", "https://aur.archlinux.org/other-name.git");
    const PackageBaseIdentity other_base = aur_package_base(
        "other-base", "https://aur.archlinux.org/example-base.git");
    const PackageBaseIdentity repository = PackageBaseIdentity::make(
        PackageSourceIdentity::repository(
            "extra",
            SourceLocationIdentity::known_git_remote(
                "https://aur.archlinux.org/example-base.git")),
        "example-base");
    const PackageBaseIdentity child_as_base = aur_package_base(
        "example-child", "https://aur.archlinux.org/example-base.git");

    const auto remote_mismatch = require_arm<ReviewedSourceStateSourceMismatch>(
        match_reviewed_source_state(state, other_remote),
        "Different canonical remote was not SourceMismatch.");
    require(remote_mismatch.observed == state &&
                remote_mismatch.expected == other_remote &&
                remote_mismatch.reasons ==
                    std::vector<ReviewedSourceStateMismatchReason>{
                        ReviewedSourceStateMismatchReason::
                            CanonicalGitRemoteMismatch},
            "Remote mismatch details drifted.");

    const auto base_mismatch = require_arm<ReviewedSourceStateSourceMismatch>(
        match_reviewed_source_state(state, other_base),
        "Different PackageBase was not SourceMismatch.");
    require(base_mismatch.reasons ==
                std::vector<ReviewedSourceStateMismatchReason>{
                    ReviewedSourceStateMismatchReason::
                        PackageBaseMismatch},
            "PackageBase mismatch details drifted.");

    const auto kind_mismatch = require_arm<ReviewedSourceStateSourceMismatch>(
        match_reviewed_source_state(state, repository),
        "Repository expected identity was not SourceMismatch.");
    require(kind_mismatch.reasons ==
                std::vector<ReviewedSourceStateMismatchReason>{
                    ReviewedSourceStateMismatchReason::
                        SourceKindMismatch},
            "Source kind mismatch details drifted.");

    const auto child_mismatch = require_arm<ReviewedSourceStateSourceMismatch>(
        interpret_reviewed_source_state(
            encode_reviewed_source_state(state), child_as_base),
        "Looking up by a child name was not SourceMismatch.");
    require(child_mismatch.reasons ==
                std::vector<ReviewedSourceStateMismatchReason>{
                    ReviewedSourceStateMismatchReason::
                        PackageBaseMismatch},
            "Child name was accepted as PackageBase identity.");

    const ReviewedSourceStateInterpretation interpreted = interpret_reviewed_source_state(
        encode_reviewed_source_state(state), aur_package_base());
    const ReviewedSourceStateLoaded& matched =
        require_arm<ReviewedSourceStateLoaded>(
            interpreted, "Matching AUR PackageBase did not load.");
    require(matched.state == state, "Matched interpret lost observed state.");

    require(std::holds_alternative<ReviewedSourceStateLoaded>(
                decode_reviewed_source_state(
                    encode_reviewed_source_state(state))),
            "Decode without expected identity produced a match outcome.");
}

} // namespace

int main() {
    try {
        test_valid_construction_keeps_aur_package_base_and_complete_revision();
        test_construction_rejects_non_aur_or_incomplete_authority();
        test_state_identity_is_package_base_and_explicit_source();
        test_schema_round_trip_and_publication_form();
        test_decode_accepts_equivalent_current_schema_layout();
        test_required_fields_and_malformed_values_are_invalid();
        test_corrupted_documents_are_not_missing();
        test_future_schema_is_identified_without_current_decode();
        test_source_mismatch_is_distinct_from_invalid();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "reviewed source state tests: all checks passed\n";
    return 0;
}
