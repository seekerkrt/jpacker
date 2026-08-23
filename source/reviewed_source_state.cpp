#include "reviewed_source_state.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <toml++/toml.hpp>

namespace {

constexpr std::string_view SCHEMA_VERSION_KEY = "schema_version";
constexpr std::string_view SOURCE_KIND_KEY = "source_kind";
constexpr std::string_view PACKAGE_BASE_KEY = "package_base";
constexpr std::string_view CANONICAL_GIT_REMOTE_KEY = "canonical_git_remote";
constexpr std::string_view REVIEWED_COMMIT_KEY = "reviewed_commit";
constexpr std::string_view AUR_SOURCE_KIND = "aur";

bool is_ascii_whitespace(char character) noexcept {
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
}

bool is_empty_or_whitespace(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), is_ascii_whitespace);
}

bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t offset = 0;
    while(offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t length = 0;
        if(first <= 0x7f) {
            length = 1;
        } else if(first >= 0xc2 && first <= 0xdf) {
            length = 2;
            if(offset + 1 >= text.size()) return false;
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            if(second < 0x80 || second > 0xbf) return false;
        } else if(first >= 0xe0 && first <= 0xef) {
            length = 3;
            if(offset + 2 >= text.size()) return false;
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            const auto third = static_cast<unsigned char>(text[offset + 2]);
            const bool valid_second =
                    first == 0xe0 ? second >= 0xa0 && second <= 0xbf
                                  : first == 0xed
                                          ? second >= 0x80 && second <= 0x9f
                                          : second >= 0x80 && second <= 0xbf;
            if(!valid_second || third < 0x80 || third > 0xbf) return false;
        } else if(first >= 0xf0 && first <= 0xf4) {
            length = 4;
            if(offset + 3 >= text.size()) return false;
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            const auto third = static_cast<unsigned char>(text[offset + 2]);
            const auto fourth = static_cast<unsigned char>(text[offset + 3]);
            const bool valid_second =
                    first == 0xf0 ? second >= 0x90 && second <= 0xbf
                                  : first == 0xf4
                                          ? second >= 0x80 && second <= 0x8f
                                          : second >= 0x80 && second <= 0xbf;
            if(!valid_second || third < 0x80 || third > 0xbf ||
               fourth < 0x80 || fourth > 0xbf) {
                return false;
            }
        } else {
            return false;
        }
        offset += length;
    }
    return true;
}

bool is_current_schema_key(std::string_view name) noexcept {
    return name == SCHEMA_VERSION_KEY || name == SOURCE_KIND_KEY ||
           name == PACKAGE_BASE_KEY || name == CANONICAL_GIT_REMOTE_KEY ||
           name == REVIEWED_COMMIT_KEY;
}

ReviewedSourceStateDocument invalid_document(
        ReviewedSourceStateInvalidReason reason, std::string_view field) {
    return ReviewedSourceStateInvalid{reason, std::string(field)};
}

char hex_digit(unsigned value) noexcept {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
}

std::string encode_toml_basic_string(std::string_view value) {
    std::string encoded = "\"";
    for(unsigned char character : value) {
        switch(character) {
        case '"':
            encoded += "\\\"";
            break;
        case '\\':
            encoded += "\\\\";
            break;
        case '\b':
            encoded += "\\b";
            break;
        case '\t':
            encoded += "\\t";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\f':
            encoded += "\\f";
            break;
        case '\r':
            encoded += "\\r";
            break;
        default:
            if(character < 0x20) {
                encoded += "\\u00";
                encoded += hex_digit(character >> 4);
                encoded += hex_digit(character & 0x0f);
            } else {
                encoded += static_cast<char>(character);
            }
            break;
        }
    }
    encoded += '"';
    return encoded;
}

std::optional<ReviewedSourceStateDocument> missing_or_untyped_string(
        const toml::table& root, std::string_view key,
        ReviewedSourceStateInvalidReason missing_reason) {
    const toml::node* node = root.get(key);
    if(node == nullptr) {
        return invalid_document(missing_reason, key);
    }
    if(!node->value_exact<std::string>().has_value()) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::UnexpectedRepresentation,
                key);
    }
    return std::nullopt;
}

ReviewedSourceStateDocument decode_current_schema(const toml::table& root) {
    for(const auto& [key, node] : root) {
        static_cast<void>(node);
        const std::string_view name = key.str();
        if(!is_current_schema_key(name)) {
            return invalid_document(
                    ReviewedSourceStateInvalidReason::UnknownKey, name);
        }
    }

    if(const auto failure = missing_or_untyped_string(
               root, SOURCE_KIND_KEY,
               ReviewedSourceStateInvalidReason::MissingSourceKind)) {
        return *failure;
    }
    if(const auto failure = missing_or_untyped_string(
               root, PACKAGE_BASE_KEY,
               ReviewedSourceStateInvalidReason::MissingPackageBase)) {
        return *failure;
    }
    if(const auto failure = missing_or_untyped_string(
               root, CANONICAL_GIT_REMOTE_KEY,
               ReviewedSourceStateInvalidReason::MissingCanonicalGitRemote)) {
        return *failure;
    }
    if(const auto failure = missing_or_untyped_string(
               root, REVIEWED_COMMIT_KEY,
               ReviewedSourceStateInvalidReason::MissingReviewedCommit)) {
        return *failure;
    }

    const std::string source_kind =
            root.get(SOURCE_KIND_KEY)->value_exact<std::string>().value();
    if(source_kind != AUR_SOURCE_KIND) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::UnsupportedSourceKind,
                SOURCE_KIND_KEY);
    }

    const std::string package_base_name =
            root.get(PACKAGE_BASE_KEY)->value_exact<std::string>().value();
    const std::string remote =
            root.get(CANONICAL_GIT_REMOTE_KEY)->value_exact<std::string>().value();
    const std::string commit =
            root.get(REVIEWED_COMMIT_KEY)->value_exact<std::string>().value();

    std::optional<SourceLocationIdentity> location;
    try {
        location = SourceLocationIdentity::known_git_remote(remote);
    } catch(const std::invalid_argument&) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MalformedCanonicalGitRemote,
                CANONICAL_GIT_REMOTE_KEY);
    }

    std::optional<PackageBaseIdentity> package_base;
    try {
        package_base = PackageBaseIdentity::make(
                PackageSourceIdentity::aur(std::move(*location)),
                package_base_name);
    } catch(const std::invalid_argument&) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MalformedPackageBase,
                PACKAGE_BASE_KEY);
    }

    std::optional<SourceRevisionIdentity> revision;
    try {
        revision = SourceRevisionIdentity::git_commit(commit);
    } catch(const std::invalid_argument&) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MalformedReviewedCommit,
                REVIEWED_COMMIT_KEY);
    }

    return ReviewedSourceStateLoaded{
            ReviewedSourceState::make(
                    std::move(*package_base), std::move(*revision))};
}

} // namespace

ReviewedSourceState::ReviewedSourceState(
        std::int64_t schema_version,
        PackageBaseIdentity package_base,
        SourceRevisionIdentity reviewed_revision) noexcept
    : schema_version_(schema_version),
      package_base_(std::move(package_base)),
      reviewed_revision_(std::move(reviewed_revision)) {}

ReviewedSourceState ReviewedSourceState::make(
        PackageBaseIdentity package_base,
        SourceRevisionIdentity reviewed_revision) {
    const PackageSourceIdentity& source = package_base.source();
    if(source.kind() != PackageSourceKind::Aur) {
        throw std::invalid_argument(
                "Reviewed source state requires an AUR source identity.");
    }

    const SourceLocationIdentity& location = source.location();
    if(location.kind() != SourceLocationKind::GitRemote ||
       location.state() != SourceLocationState::Known ||
       location.value() == nullptr) {
        throw std::invalid_argument(
                "Reviewed source state requires a known AUR Git remote.");
    }

    if(reviewed_revision.state() != SourceRevisionState::Known ||
       reviewed_revision.git_commit() == nullptr ||
       reviewed_revision.git_object_format() == nullptr) {
        throw std::invalid_argument(
                "Reviewed source state requires a complete known Git commit identity.");
    }

    return ReviewedSourceState(
            reviewed_source_state_schema_version,
            std::move(package_base),
            std::move(reviewed_revision));
}

std::int64_t ReviewedSourceState::schema_version() const noexcept {
    return schema_version_;
}

const PackageBaseIdentity& ReviewedSourceState::package_base() const noexcept {
    return package_base_;
}

const SourceRevisionIdentity&
ReviewedSourceState::reviewed_revision() const noexcept {
    return reviewed_revision_;
}

std::string encode_reviewed_source_state(const ReviewedSourceState& state) {
    const std::string* remote =
            state.package_base().source().location().value();
    const std::string* commit = state.reviewed_revision().git_commit();
    if(remote == nullptr || commit == nullptr) {
        throw std::logic_error(
                "Reviewed source state lost its Git remote or commit identity.");
    }

    std::string document;
    document += "schema_version = ";
    document += std::to_string(state.schema_version());
    document += '\n';
    document += "source_kind = \"aur\"\n";
    document += "package_base = ";
    document += encode_toml_basic_string(state.package_base().package_base());
    document += '\n';
    document += "canonical_git_remote = ";
    document += encode_toml_basic_string(*remote);
    document += '\n';
    document += "reviewed_commit = ";
    document += encode_toml_basic_string(*commit);
    document += '\n';
    return document;
}

ReviewedSourceStateDocument decode_reviewed_source_state(
        std::string_view document) {
    if(!is_valid_utf8(document)) {
        return ReviewedSourceStateCorrupted{
                ReviewedSourceStateCorruptedReason::InvalidUtf8};
    }
    if(is_empty_or_whitespace(document)) {
        return ReviewedSourceStateCorrupted{
                ReviewedSourceStateCorruptedReason::EmptyDocument};
    }

    toml::table root;
    try {
        root = toml::parse(document);
    } catch(const toml::parse_error&) {
        return ReviewedSourceStateCorrupted{
                ReviewedSourceStateCorruptedReason::UnparseableDocument};
    }

    const toml::node* version_node = root.get(SCHEMA_VERSION_KEY);
    if(version_node == nullptr) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MissingSchemaVersion,
                SCHEMA_VERSION_KEY);
    }

    const auto version = version_node->value_exact<std::int64_t>();
    if(!version.has_value()) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MalformedSchemaVersion,
                SCHEMA_VERSION_KEY);
    }
    // LANDMINE: future schema must be identified from the version integer
    // alone. Applying current-schema key/value rules would make a later
    // writer treat unread future fields as invalid current state and
    // overwrite them.
    if(*version > reviewed_source_state_schema_version) {
        return ReviewedSourceStateUnsupportedFuture{*version};
    }
    if(*version != reviewed_source_state_schema_version) {
        return invalid_document(
                ReviewedSourceStateInvalidReason::MalformedSchemaVersion,
                SCHEMA_VERSION_KEY);
    }

    return decode_current_schema(root);
}

ReviewedSourceStateMatch match_reviewed_source_state(
        const ReviewedSourceState& state,
        const PackageBaseIdentity& expected_package_base) {
    if(state.package_base() == expected_package_base) {
        return ReviewedSourceStateLoaded{state};
    }

    ReviewedSourceStateSourceMismatch mismatch{
            state, expected_package_base, {}};

    const PackageSourceIdentity& observed_source = state.package_base().source();
    const PackageSourceIdentity& expected_source =
            expected_package_base.source();
    if(observed_source.kind() != expected_source.kind()) {
        mismatch.reasons.push_back(
                ReviewedSourceStateMismatchReason::SourceKindMismatch);
    }

    const SourceLocationIdentity& observed_location =
            observed_source.location();
    const SourceLocationIdentity& expected_location =
            expected_source.location();
    const std::string* observed_remote = observed_location.value();
    const std::string* expected_remote = expected_location.value();
    const bool same_remote =
            observed_location.kind() == expected_location.kind() &&
            observed_location.state() == expected_location.state() &&
            observed_remote != nullptr && expected_remote != nullptr &&
            *observed_remote == *expected_remote;
    if(!same_remote) {
        mismatch.reasons.push_back(
                ReviewedSourceStateMismatchReason::CanonicalGitRemoteMismatch);
    }

    if(state.package_base().package_base() !=
       expected_package_base.package_base()) {
        mismatch.reasons.push_back(
                ReviewedSourceStateMismatchReason::PackageBaseMismatch);
    }

    return mismatch;
}

ReviewedSourceStateInterpretation interpret_reviewed_source_state(
        std::string_view document,
        const PackageBaseIdentity& expected_package_base) {
    const ReviewedSourceStateDocument decoded =
            decode_reviewed_source_state(document);
    if(const auto* loaded =
               std::get_if<ReviewedSourceStateLoaded>(&decoded)) {
        const ReviewedSourceStateMatch matched = match_reviewed_source_state(
                loaded->state, expected_package_base);
        if(const auto* bound =
                   std::get_if<ReviewedSourceStateLoaded>(&matched)) {
            return *bound;
        }
        return std::get<ReviewedSourceStateSourceMismatch>(matched);
    }
    if(const auto* invalid =
               std::get_if<ReviewedSourceStateInvalid>(&decoded)) {
        return *invalid;
    }
    if(const auto* corrupted =
               std::get_if<ReviewedSourceStateCorrupted>(&decoded)) {
        return *corrupted;
    }
    return std::get<ReviewedSourceStateUnsupportedFuture>(decoded);
}
