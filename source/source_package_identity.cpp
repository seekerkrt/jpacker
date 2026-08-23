#include "source_package_identity.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

bool is_continuation_byte(unsigned char byte) noexcept {
    return byte >= 0x80 && byte <= 0xbf;
}

bool decode_utf8_code_point(
        std::string_view value, std::size_t offset,
        std::uint32_t& code_point, std::size_t& length) noexcept {
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
        if(!is_continuation_byte(second)) return false;
        code_point =
                (static_cast<std::uint32_t>(first & 0x1f) << 6) |
                static_cast<std::uint32_t>(second & 0x3f);
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
                                      : is_continuation_byte(second);
        if(!valid_second || !is_continuation_byte(third)) return false;
        code_point =
                (static_cast<std::uint32_t>(first & 0x0f) << 12) |
                (static_cast<std::uint32_t>(second & 0x3f) << 6) |
                static_cast<std::uint32_t>(third & 0x3f);
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
                                      : is_continuation_byte(second);
        if(!valid_second || !is_continuation_byte(third) ||
           !is_continuation_byte(fourth)) {
            return false;
        }
        code_point =
                (static_cast<std::uint32_t>(first & 0x07) << 18) |
                (static_cast<std::uint32_t>(second & 0x3f) << 12) |
                (static_cast<std::uint32_t>(third & 0x3f) << 6) |
                static_cast<std::uint32_t>(fourth & 0x3f);
        length = 4;
        return true;
    }
    return false;
}

bool is_single_line_code_point(std::uint32_t code_point) noexcept {
    return code_point > 0x1f &&
           !(code_point >= 0x7f && code_point <= 0x9f) &&
           code_point != 0x2028 && code_point != 0x2029;
}

bool is_valid_single_line_utf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while(offset < value.size()) {
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        if(!decode_utf8_code_point(value, offset, code_point, length) ||
           !is_single_line_code_point(code_point)) {
            return false;
        }
        offset += length;
    }
    return true;
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

bool is_valid_nonempty_text(std::string_view value) noexcept {
    return !value.empty() && is_valid_single_line_utf8(value);
}

bool is_valid_token(std::string_view value) noexcept {
    return is_valid_nonempty_text(value) &&
           !contains_ascii_whitespace(value);
}

bool is_valid_package_identity(const std::string& value) {
    return is_valid_nonempty_text(value) && is_valid_package_name(value);
}

void require_package_identity(
        const std::string& value, std::string_view field_name) {
    if(!is_valid_package_identity(value)) {
        throw std::invalid_argument(
                std::string(field_name) + " is not a valid package identity.");
    }
}

void require_unavailable_reason(IdentityUnavailableReason reason) {
    switch(reason) {
    case IdentityUnavailableReason::AuthorityUnavailable:
    case IdentityUnavailableReason::ObservationFailed:
    case IdentityUnavailableReason::InvalidObservation:
        return;
    }
    throw std::invalid_argument("Identity unavailable reason is invalid.");
}

void require_source_location_kind(SourceLocationKind kind) {
    switch(kind) {
    case SourceLocationKind::GitRemote:
    case SourceLocationKind::LocalPath:
        return;
    }
    throw std::invalid_argument("Source location kind is invalid.");
}

void require_location_kind(
        const SourceLocationIdentity& location,
        SourceLocationKind expected_kind) {
    if(location.kind() != expected_kind) {
        throw std::invalid_argument(
                "Package source and source location kinds are inconsistent.");
    }
}

bool is_lower_hex(char character) noexcept {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
}

GitObjectFormat detect_git_object_format(const std::string& object_id) {
    GitObjectFormat format;
    if(object_id.size() == 40) {
        format = GitObjectFormat::Sha1;
    } else if(object_id.size() == 64) {
        format = GitObjectFormat::Sha256;
    } else {
        throw std::invalid_argument(
                "Git commit identity must be a complete SHA-1 or SHA-256 object ID.");
    }
    if(!std::all_of(object_id.begin(), object_id.end(), is_lower_hex)) {
        throw std::invalid_argument(
                "Git commit identity must use canonical lowercase hexadecimal.");
    }
    return format;
}

std::string composite_package_version(
        const std::optional<std::string>& epoch,
        const std::string& pkgver,
        const std::string& pkgrel) {
    std::string full_version;
    if(epoch.has_value()) full_version = epoch.value() + ":";
    full_version += pkgver;
    full_version += '-';
    full_version += pkgrel;
    return full_version;
}

} // namespace

SourceLocationIdentity::SourceLocationIdentity(
        SourceLocationKind kind, SourceLocationState state,
        std::optional<std::string> value,
        std::optional<IdentityUnavailableReason> unavailable_reason) noexcept
    : kind_(kind), state_(state), value_(std::move(value)),
      unavailable_reason_(unavailable_reason) {}

SourceLocationIdentity SourceLocationIdentity::known_git_remote(
        std::string location) {
    if(!is_valid_token(location)) {
        throw std::invalid_argument(
                "Git source location must be nonempty single-line UTF-8 without whitespace.");
    }
    return SourceLocationIdentity(
            SourceLocationKind::GitRemote,
            SourceLocationState::Known,
            std::move(location),
            std::nullopt);
}

SourceLocationIdentity SourceLocationIdentity::known_local_path(
        std::string location) {
    if(!is_valid_nonempty_text(location) || !location.starts_with('/')) {
        throw std::invalid_argument(
                "Local source location must be an absolute single-line UTF-8 path.");
    }
    return SourceLocationIdentity(
            SourceLocationKind::LocalPath,
            SourceLocationState::Known,
            std::move(location),
            std::nullopt);
}

SourceLocationIdentity SourceLocationIdentity::unknown(
        SourceLocationKind kind) {
    require_source_location_kind(kind);
    return SourceLocationIdentity(
            kind, SourceLocationState::Unknown, std::nullopt, std::nullopt);
}

SourceLocationIdentity SourceLocationIdentity::unavailable(
        SourceLocationKind kind, IdentityUnavailableReason reason) {
    require_source_location_kind(kind);
    require_unavailable_reason(reason);
    return SourceLocationIdentity(
            kind, SourceLocationState::Unavailable, std::nullopt, reason);
}

SourceLocationKind SourceLocationIdentity::kind() const noexcept {
    return kind_;
}

SourceLocationState SourceLocationIdentity::state() const noexcept {
    return state_;
}

const std::string* SourceLocationIdentity::value() const noexcept {
    return value_.has_value() ? &value_.value() : nullptr;
}

const IdentityUnavailableReason*
SourceLocationIdentity::unavailable_reason() const noexcept {
    return unavailable_reason_.has_value() ? &unavailable_reason_.value()
                                           : nullptr;
}

PackageSourceIdentity::PackageSourceIdentity(
        PackageSourceKind kind,
        std::optional<std::string> repository_name,
        SourceLocationIdentity location) noexcept
    : kind_(kind), repository_name_(std::move(repository_name)),
      location_(std::move(location)) {}

PackageSourceIdentity PackageSourceIdentity::repository(
        std::string repository_name, SourceLocationIdentity location) {
    if(!is_valid_nonempty_text(repository_name)) {
        throw std::invalid_argument(
                "Repository identity must be nonempty single-line UTF-8.");
    }
    require_location_kind(location, SourceLocationKind::GitRemote);
    return PackageSourceIdentity(
            PackageSourceKind::Repository,
            std::move(repository_name),
            std::move(location));
}

PackageSourceIdentity PackageSourceIdentity::aur(
        SourceLocationIdentity location) {
    require_location_kind(location, SourceLocationKind::GitRemote);
    return PackageSourceIdentity(
            PackageSourceKind::Aur, std::nullopt, std::move(location));
}

PackageSourceIdentity PackageSourceIdentity::local(
        SourceLocationIdentity location) {
    require_location_kind(location, SourceLocationKind::LocalPath);
    return PackageSourceIdentity(
            PackageSourceKind::Local, std::nullopt, std::move(location));
}

PackageSourceKind PackageSourceIdentity::kind() const noexcept {
    return kind_;
}

const std::string* PackageSourceIdentity::repository_name() const noexcept {
    return repository_name_.has_value() ? &repository_name_.value() : nullptr;
}

const SourceLocationIdentity& PackageSourceIdentity::location() const noexcept {
    return location_;
}

PackageBaseIdentity::PackageBaseIdentity(
        PackageSourceIdentity source,
        std::string package_base) noexcept
    : source_(std::move(source)), package_base_(std::move(package_base)) {}

PackageBaseIdentity PackageBaseIdentity::make(
        PackageSourceIdentity source, std::string package_base) {
    require_package_identity(package_base, "PackageBase");
    return PackageBaseIdentity(std::move(source), std::move(package_base));
}

const PackageSourceIdentity& PackageBaseIdentity::source() const noexcept {
    return source_;
}

const std::string& PackageBaseIdentity::package_base() const noexcept {
    return package_base_;
}

PackageChildIdentity::PackageChildIdentity(
        PackageBaseIdentity package_base,
        std::string package_name) noexcept
    : package_base_(std::move(package_base)),
      package_name_(std::move(package_name)) {}

PackageChildIdentity PackageChildIdentity::make(
        PackageBaseIdentity package_base, std::string package_name) {
    require_package_identity(package_name, "Package child");
    return PackageChildIdentity(
            std::move(package_base), std::move(package_name));
}

const PackageBaseIdentity& PackageChildIdentity::package_base() const noexcept {
    return package_base_;
}

const std::string& PackageChildIdentity::package_name() const noexcept {
    return package_name_;
}

SourceRevisionIdentity::SourceRevisionIdentity(
        SourceRevisionState state,
        std::optional<GitObjectFormat> git_object_format,
        std::optional<std::string> git_commit,
        std::optional<IdentityUnavailableReason> unavailable_reason) noexcept
    : state_(state), git_object_format_(git_object_format),
      git_commit_(std::move(git_commit)),
      unavailable_reason_(unavailable_reason) {}

SourceRevisionIdentity SourceRevisionIdentity::git_commit(
        std::string object_id) {
    const GitObjectFormat format = detect_git_object_format(object_id);
    return SourceRevisionIdentity(
            SourceRevisionState::Known,
            format,
            std::move(object_id),
            std::nullopt);
}

SourceRevisionIdentity SourceRevisionIdentity::unknown() noexcept {
    return SourceRevisionIdentity(
            SourceRevisionState::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt);
}

SourceRevisionIdentity SourceRevisionIdentity::absent() noexcept {
    return SourceRevisionIdentity(
            SourceRevisionState::Absent,
            std::nullopt,
            std::nullopt,
            std::nullopt);
}

SourceRevisionIdentity SourceRevisionIdentity::unavailable(
        IdentityUnavailableReason reason) {
    require_unavailable_reason(reason);
    return SourceRevisionIdentity(
            SourceRevisionState::Unavailable,
            std::nullopt,
            std::nullopt,
            reason);
}

SourceRevisionIdentity SourceRevisionIdentity::inapplicable() noexcept {
    return SourceRevisionIdentity(
            SourceRevisionState::Inapplicable,
            std::nullopt,
            std::nullopt,
            std::nullopt);
}

SourceRevisionState SourceRevisionIdentity::state() const noexcept {
    return state_;
}

const GitObjectFormat*
SourceRevisionIdentity::git_object_format() const noexcept {
    return git_object_format_.has_value() ? &git_object_format_.value()
                                          : nullptr;
}

const std::string* SourceRevisionIdentity::git_commit() const noexcept {
    return git_commit_.has_value() ? &git_commit_.value() : nullptr;
}

const IdentityUnavailableReason*
SourceRevisionIdentity::unavailable_reason() const noexcept {
    return unavailable_reason_.has_value() ? &unavailable_reason_.value()
                                           : nullptr;
}

PackageVersionIdentity::PackageVersionIdentity(
        PackageVersionState state,
        std::optional<PackageVersionRepresentation> representation,
        std::optional<std::string> full_version,
        std::optional<std::string> epoch,
        std::optional<std::string> pkgver,
        std::optional<std::string> pkgrel,
        std::optional<IdentityUnavailableReason> unavailable_reason) noexcept
    : state_(state), representation_(representation),
      full_version_(std::move(full_version)), epoch_(std::move(epoch)),
      pkgver_(std::move(pkgver)), pkgrel_(std::move(pkgrel)),
      unavailable_reason_(unavailable_reason) {}

PackageVersionIdentity PackageVersionIdentity::composite(
        std::string full_version) {
    if(!is_valid_token(full_version)) {
        throw std::invalid_argument(
                "Composite package version must be nonempty single-line UTF-8 without whitespace.");
    }
    return PackageVersionIdentity(
            PackageVersionState::Known,
            PackageVersionRepresentation::Composite,
            std::move(full_version),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt);
}

PackageVersionIdentity PackageVersionIdentity::pkgver_pkgrel(
        std::optional<std::string> epoch,
        std::string pkgver,
        std::string pkgrel) {
    if(epoch.has_value() &&
       (epoch->empty() ||
        !std::all_of(epoch->begin(), epoch->end(), [](char character) {
            return character >= '0' && character <= '9';
        }))) {
        throw std::invalid_argument(
                "Package epoch must contain one or more ASCII digits.");
    }
    if(!is_valid_token(pkgver)) {
        throw std::invalid_argument(
                "pkgver must be nonempty single-line UTF-8 without whitespace.");
    }
    if(!is_valid_token(pkgrel)) {
        throw std::invalid_argument(
                "pkgrel must be nonempty single-line UTF-8 without whitespace.");
    }
    std::string full_version =
            composite_package_version(epoch, pkgver, pkgrel);
    return PackageVersionIdentity(
            PackageVersionState::Known,
            PackageVersionRepresentation::PkgverPkgrel,
            std::move(full_version),
            std::move(epoch),
            std::move(pkgver),
            std::move(pkgrel),
            std::nullopt);
}

PackageVersionIdentity PackageVersionIdentity::unknown() noexcept {
    return PackageVersionIdentity(
            PackageVersionState::Unknown,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt);
}

PackageVersionIdentity PackageVersionIdentity::unavailable(
        IdentityUnavailableReason reason) {
    require_unavailable_reason(reason);
    return PackageVersionIdentity(
            PackageVersionState::Unavailable,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            reason);
}

PackageVersionState PackageVersionIdentity::state() const noexcept {
    return state_;
}

const PackageVersionRepresentation*
PackageVersionIdentity::representation() const noexcept {
    return representation_.has_value() ? &representation_.value() : nullptr;
}

const std::string* PackageVersionIdentity::full_version() const noexcept {
    return full_version_.has_value() ? &full_version_.value() : nullptr;
}

const std::string* PackageVersionIdentity::epoch() const noexcept {
    return epoch_.has_value() ? &epoch_.value() : nullptr;
}

const std::string* PackageVersionIdentity::pkgver() const noexcept {
    return pkgver_.has_value() ? &pkgver_.value() : nullptr;
}

const std::string* PackageVersionIdentity::pkgrel() const noexcept {
    return pkgrel_.has_value() ? &pkgrel_.value() : nullptr;
}

const IdentityUnavailableReason*
PackageVersionIdentity::unavailable_reason() const noexcept {
    return unavailable_reason_.has_value() ? &unavailable_reason_.value()
                                           : nullptr;
}

PackageArchitectureIdentity::PackageArchitectureIdentity(
        PackageArchitectureState state,
        std::vector<std::string> architectures,
        std::optional<IdentityUnavailableReason> unavailable_reason) noexcept
    : state_(state), architectures_(std::move(architectures)),
      unavailable_reason_(unavailable_reason) {}

PackageArchitectureIdentity PackageArchitectureIdentity::known(
        std::vector<std::string> architectures) {
    if(architectures.empty()) {
        throw std::invalid_argument(
                "Known package architecture identity must not be empty.");
    }
    for(const std::string& architecture : architectures) {
        if(!is_valid_token(architecture)) {
            throw std::invalid_argument(
                    "Package architecture must be nonempty single-line UTF-8 without whitespace.");
        }
    }
    std::sort(architectures.begin(), architectures.end());
    if(std::adjacent_find(architectures.begin(), architectures.end()) !=
       architectures.end()) {
        throw std::invalid_argument(
                "Package architecture identity contains a duplicate value.");
    }
    return PackageArchitectureIdentity(
            PackageArchitectureState::Known,
            std::move(architectures),
            std::nullopt);
}

PackageArchitectureIdentity PackageArchitectureIdentity::unknown() noexcept {
    return PackageArchitectureIdentity(
            PackageArchitectureState::Unknown, {}, std::nullopt);
}

PackageArchitectureIdentity PackageArchitectureIdentity::unavailable(
        IdentityUnavailableReason reason) {
    require_unavailable_reason(reason);
    return PackageArchitectureIdentity(
            PackageArchitectureState::Unavailable, {}, reason);
}

PackageArchitectureState PackageArchitectureIdentity::state() const noexcept {
    return state_;
}

const std::vector<std::string>&
PackageArchitectureIdentity::architectures() const noexcept {
    return architectures_;
}

const IdentityUnavailableReason*
PackageArchitectureIdentity::unavailable_reason() const noexcept {
    return unavailable_reason_.has_value() ? &unavailable_reason_.value()
                                           : nullptr;
}

SourceAwarePackageIdentity::SourceAwarePackageIdentity(
        PackageChildIdentity package,
        SourceRevisionIdentity source_revision,
        PackageVersionIdentity package_version,
        PackageArchitectureIdentity architecture) noexcept
    : package_(std::move(package)),
      source_revision_(std::move(source_revision)),
      package_version_(std::move(package_version)),
      architecture_(std::move(architecture)) {}

SourceAwarePackageIdentity SourceAwarePackageIdentity::make(
        PackageChildIdentity package,
        SourceRevisionIdentity source_revision,
        PackageVersionIdentity package_version,
        PackageArchitectureIdentity architecture) {
    return SourceAwarePackageIdentity(
            std::move(package),
            std::move(source_revision),
            std::move(package_version),
            std::move(architecture));
}

const PackageChildIdentity&
SourceAwarePackageIdentity::package() const noexcept {
    return package_;
}

const SourceRevisionIdentity&
SourceAwarePackageIdentity::source_revision() const noexcept {
    return source_revision_;
}

const PackageVersionIdentity&
SourceAwarePackageIdentity::package_version() const noexcept {
    return package_version_;
}

const PackageArchitectureIdentity&
SourceAwarePackageIdentity::architecture() const noexcept {
    return architecture_;
}
