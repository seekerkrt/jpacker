#include "reviewed_source_state_store.hpp"

#include "xdg_directory_safety.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct ReviewedSourceStateDirectoryAccess {
    static int descriptor(
            const xdg_directory_safety::PreparedDirectory& directory) {
        directory.require_unchanged_identity();
        return directory.directory_descriptor_;
    }
};

namespace {

namespace fs = std::filesystem;

constexpr mode_t REVIEWED_SOURCE_STATE_FILE_MODE = 0600;
constexpr mode_t REVIEWED_SOURCE_STATE_DIRECTORY_MODE = 0700;
constexpr std::string_view ENTRY_SUFFIX = ".toml";
constexpr std::string_view ATOMIC_TEMP_PREFIX = "-.moguet-reviewed-source-";
constexpr std::string_view ORIGIN_LEAF = "1.toml";
constexpr std::size_t MAX_PACKAGE_DIRECTORY_ENTRIES = 4096;

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
std::optional<ReviewedSourceStateStoreTestFailurePoint> g_injected_failure;
ReviewedSourceStateStoreTestRacePoint g_race_point =
        ReviewedSourceStateStoreTestRacePoint::BeforePublication;
ReviewedSourceStateStoreTestRaceHandler g_race_handler = nullptr;
bool g_race_pending = false;

bool consume_injected_failure(
        ReviewedSourceStateStoreTestFailurePoint point) {
    if(!g_injected_failure.has_value() || *g_injected_failure != point) {
        return false;
    }
    g_injected_failure.reset();
    return true;
}

void invoke_race(
        ReviewedSourceStateStoreTestRacePoint point,
        const ReviewedSourceStateStoreTestRaceContext& context) {
    if(!g_race_pending || g_race_handler == nullptr || g_race_point != point) {
        return;
    }
    g_race_pending = false;
    g_race_handler(context);
}
#endif

class OwnedDescriptor final {
    int descriptor_ = -1;

public:
    explicit OwnedDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}

    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;

    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}

    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) static_cast<void>(::close(descriptor_));
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }

    ~OwnedDescriptor() noexcept {
        if(descriptor_ >= 0) static_cast<void>(::close(descriptor_));
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        return std::exchange(descriptor_, -1);
    }
};

template<typename Fn>
auto retry_interruptible(Fn&& fn) -> decltype(fn()) {
    while(true) {
        const auto result = fn();
        if(result >= 0 || errno != EINTR) return result;
    }
}

void require_aur_known_package_base(const PackageBaseIdentity& package_base) {
    const PackageSourceIdentity& source = package_base.source();
    if(source.kind() != PackageSourceKind::Aur) {
        throw std::invalid_argument(
                "Reviewed source state store requires an AUR PackageBase identity.");
    }
    const SourceLocationIdentity& location = source.location();
    if(location.kind() != SourceLocationKind::GitRemote ||
       location.state() != SourceLocationState::Known ||
       location.value() == nullptr) {
        throw std::invalid_argument(
                "Reviewed source state store requires a known AUR Git remote.");
    }
}

std::string package_directory_leaf(const PackageBaseIdentity& package_base) {
    return package_base.package_base();
}

bool parse_unsigned_decimal(
        std::string_view text, bool allow_zero, std::uintmax_t& value) {
    if(text.empty() || (!allow_zero && text[0] == '0') ||
       (text.size() > 1 && text[0] == '0')) {
        return false;
    }
    std::uintmax_t parsed = 0;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto [pointer, error] =
            std::from_chars(first, last, parsed, 10);
    if(error != std::errc{} || pointer != last) return false;
    value = parsed;
    return true;
}

constexpr std::size_t CONTENT_DIGEST_HEX_SIZE = 64;

bool is_lowercase_hex(std::string_view text) {
    for(const char ch : text) {
        if((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f')) {
            return false;
        }
    }
    return true;
}

std::uint32_t sha256_rotr(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

void sha256_process_block(
        std::array<std::uint32_t, 8>& hash, const std::uint8_t* block) {
    static constexpr std::array<std::uint32_t, 64> round_constants = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
            0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
            0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
            0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 64> schedule {};
    for(std::size_t i = 0; i < 16; ++i) {
        schedule[i] =
                (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for(std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = sha256_rotr(schedule[i - 15], 7) ^
                                 sha256_rotr(schedule[i - 15], 18) ^
                                 (schedule[i - 15] >> 3);
        const std::uint32_t s1 = sha256_rotr(schedule[i - 2], 17) ^
                                 sha256_rotr(schedule[i - 2], 19) ^
                                 (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }
    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for(std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^
                                 sha256_rotr(e, 25);
        const std::uint32_t ch =
                (e & f) ^ (static_cast<std::uint32_t>(~e) & g);
        const std::uint32_t temp1 =
                h + s1 + ch + round_constants[i] + schedule[i];
        const std::uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^
                                 sha256_rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
}

// POLICY: generation identity must bind observed raw contents, not only
// predecessor dev/ino. Linux has no rename-if-contents-match primitive, so
// the digest lives in the successor leaf and lookup refuses a stale
// successor after a same-inode rewrite.
std::string content_digest_hex(std::string_view data) {
    std::array<std::uint32_t, 8> hash = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block {};
    std::size_t block_used = 0;
    const auto consume_byte = [&](std::uint8_t byte) {
        block[block_used] = byte;
        ++block_used;
        if(block_used == block.size()) {
            sha256_process_block(hash, block.data());
            block_used = 0;
        }
    };
    for(const char ch : data) {
        consume_byte(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }
    const std::uint64_t bit_length =
            static_cast<std::uint64_t>(data.size()) * 8U;
    consume_byte(0x80);
    if(block_used > 56) {
        while(block_used != 0) {
            consume_byte(0);
        }
    }
    while(block_used < 56) {
        consume_byte(0);
    }
    for(std::size_t i = 0; i < 8; ++i) {
        consume_byte(static_cast<std::uint8_t>(
                bit_length >> (8U * (7U - i))));
    }

    std::string hex(CONTENT_DIGEST_HEX_SIZE, '0');
    constexpr char digits[] = "0123456789abcdef";
    for(std::size_t i = 0; i < hash.size(); ++i) {
        const std::uint32_t word = hash[i];
        hex[i * 8] = digits[(word >> 28) & 0x0fU];
        hex[i * 8 + 1] = digits[(word >> 24) & 0x0fU];
        hex[i * 8 + 2] = digits[(word >> 20) & 0x0fU];
        hex[i * 8 + 3] = digits[(word >> 16) & 0x0fU];
        hex[i * 8 + 4] = digits[(word >> 12) & 0x0fU];
        hex[i * 8 + 5] = digits[(word >> 8) & 0x0fU];
        hex[i * 8 + 6] = digits[(word >> 4) & 0x0fU];
        hex[i * 8 + 7] = digits[word & 0x0fU];
    }
    return hex;
}

struct GenerationLeaf {
    std::uint64_t generation = 0;
    bool          has_predecessor = false;
    std::uintmax_t predecessor_device = 0;
    std::uintmax_t predecessor_inode = 0;
    std::uintmax_t predecessor_ctime_seconds = 0;
    std::uintmax_t predecessor_ctime_nanoseconds = 0;
    std::string   predecessor_digest;
    std::string   leaf;
};

std::optional<GenerationLeaf> parse_generation_leaf(std::string_view name) {
    constexpr std::string_view suffix = ENTRY_SUFFIX;
    if(name.size() <= suffix.size() ||
       name.substr(name.size() - suffix.size()) != suffix) {
        return std::nullopt;
    }
    const std::string_view stem = name.substr(0, name.size() - suffix.size());
    if(stem == "1") {
        return GenerationLeaf{1, false, 0, 0, 0, 0, {}, std::string(name)};
    }
    const auto first_dot = stem.find('.');
    if(first_dot == std::string_view::npos || first_dot == 0 ||
       first_dot + 1 >= stem.size()) {
        return std::nullopt;
    }
    const auto second_dot = stem.find('.', first_dot + 1);
    if(second_dot == std::string_view::npos ||
       second_dot + 1 >= stem.size() ||
       stem.find('.', second_dot + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    std::uintmax_t generation_value = 0;
    if(!parse_unsigned_decimal(
               stem.substr(0, first_dot), false, generation_value) ||
       generation_value < 2 ||
       generation_value > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    const std::string_view identity =
            stem.substr(first_dot + 1, second_dot - first_dot - 1);
    const std::string_view digest = stem.substr(second_dot + 1);
    if(digest.size() != CONTENT_DIGEST_HEX_SIZE ||
       !is_lowercase_hex(digest)) {
        return std::nullopt;
    }
    const auto first_dash = identity.find('-');
    if(first_dash == std::string_view::npos || first_dash == 0 ||
       first_dash + 1 >= identity.size()) {
        return std::nullopt;
    }
    const auto second_dash = identity.find('-', first_dash + 1);
    if(second_dash == std::string_view::npos ||
       second_dash == first_dash + 1 || second_dash + 1 >= identity.size()) {
        return std::nullopt;
    }
    const auto third_dash = identity.find('-', second_dash + 1);
    if(third_dash == std::string_view::npos ||
       third_dash == second_dash + 1 || third_dash + 1 >= identity.size() ||
       identity.find('-', third_dash + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::uintmax_t ctime_seconds = 0;
    std::uintmax_t ctime_nanoseconds = 0;
    if(!parse_unsigned_decimal(identity.substr(0, first_dash), true, device) ||
       !parse_unsigned_decimal(
               identity.substr(first_dash + 1, second_dash - first_dash - 1),
               true, inode) ||
       !parse_unsigned_decimal(
               identity.substr(second_dash + 1, third_dash - second_dash - 1),
               true, ctime_seconds) ||
       !parse_unsigned_decimal(
               identity.substr(third_dash + 1), true, ctime_nanoseconds)) {
        return std::nullopt;
    }
    return GenerationLeaf{
            static_cast<std::uint64_t>(generation_value), true, device, inode,
            ctime_seconds, ctime_nanoseconds, std::string(digest),
            std::string(name)};
}

bool is_internal_temp_leaf(std::string_view name) {
    return name.size() >= ATOMIC_TEMP_PREFIX.size() &&
           name.substr(0, ATOMIC_TEMP_PREFIX.size()) == ATOMIC_TEMP_PREFIX;
}

fs::file_type file_type_from_mode(mode_t mode) {
    if(S_ISREG(mode)) return fs::file_type::regular;
    if(S_ISDIR(mode)) return fs::file_type::directory;
    if(S_ISLNK(mode)) return fs::file_type::symlink;
    if(S_ISBLK(mode)) return fs::file_type::block;
    if(S_ISCHR(mode)) return fs::file_type::character;
    if(S_ISFIFO(mode)) return fs::file_type::fifo;
    if(S_ISSOCK(mode)) return fs::file_type::socket;
    return fs::file_type::unknown;
}

std::error_code current_system_error() {
    return std::error_code(errno, std::generic_category());
}

ReviewedSourceStateStoreFailure store_failure(
        ReviewedSourceStateStoreFailureKind kind,
        const fs::path& entry_path,
        std::optional<std::error_code> system_error = std::nullopt,
        std::optional<fs::file_type> observed_file_type = std::nullopt,
        std::optional<fs::path> leftover_artifact = std::nullopt) {
    return ReviewedSourceStateStoreFailure{
            kind, entry_path, std::move(system_error),
            std::move(observed_file_type), std::move(leftover_artifact)};
}

ReviewedSourceStateRecordIdentity record_identity_from_status(
        const struct stat& status) {
    return ReviewedSourceStateRecordIdentity{
            static_cast<std::uintmax_t>(status.st_dev),
            static_cast<std::uintmax_t>(status.st_ino),
            static_cast<std::uintmax_t>(status.st_uid),
            static_cast<std::uintmax_t>(status.st_mode & 07777),
            static_cast<std::uintmax_t>(status.st_nlink),
            static_cast<std::intmax_t>(status.st_size),
            static_cast<std::intmax_t>(status.st_mtim.tv_sec),
            static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
            static_cast<std::intmax_t>(status.st_ctim.tv_sec),
            static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
}

bool same_filesystem_identity(
        const struct stat& expected, const struct stat& actual) {
    return expected.st_dev == actual.st_dev &&
           expected.st_ino == actual.st_ino &&
           (expected.st_mode & S_IFMT) == (actual.st_mode & S_IFMT);
}

// LANDMINE: st_nlink belongs here, and it has to be compared directly. A
// hardlink added outside the PackageBase directory leaves dev, ino, type, uid,
// mode, size, mtime and the bytes identical and never touches the directory
// inventory, so every other comparison in this function stays true. Inferring
// it from ctime is not a substitute: a coarse-granularity filesystem may
// represent the observation before and after the link with the same ctime.
bool same_record_state(
        const struct stat& expected, const struct stat& actual) {
    return same_filesystem_identity(expected, actual) &&
           expected.st_uid == actual.st_uid &&
           (expected.st_mode & 07777) == (actual.st_mode & 07777) &&
           expected.st_nlink == actual.st_nlink &&
           expected.st_size == actual.st_size &&
           expected.st_mtim.tv_sec == actual.st_mtim.tv_sec &&
           expected.st_mtim.tv_nsec == actual.st_mtim.tv_nsec &&
           expected.st_ctim.tv_sec == actual.st_ctim.tv_sec &&
           expected.st_ctim.tv_nsec == actual.st_ctim.tv_nsec;
}

// rename(2) may update ctime. Post-publication compares the stable
// security/content fields and allows only that relocation timestamp to differ.
//
// NOTE: st_nlink is deliberately absent. This compares the retained O_TMPFILE
// inode before and after linkat, and that transition is exactly 0 -> 1. The
// absolute single-link contract for the committed record is proven separately
// against the named entry.
bool same_relocated_record_state(
        const struct stat& expected, const struct stat& actual) {
    return same_filesystem_identity(expected, actual) &&
           expected.st_uid == actual.st_uid &&
           (expected.st_mode & 07777) == (actual.st_mode & 07777) &&
           expected.st_size == actual.st_size &&
           expected.st_mtim.tv_sec == actual.st_mtim.tv_sec &&
           expected.st_mtim.tv_nsec == actual.st_mtim.tv_nsec;
}

bool matches_record_identity(
        const struct stat& status,
        const ReviewedSourceStateRecordIdentity& identity) {
    return record_identity_from_status(status) == identity;
}

bool matches_predecessor_binding(
        const GenerationLeaf& leaf,
        const struct stat& predecessor,
        std::string_view predecessor_digest) {
    return leaf.has_predecessor &&
           leaf.predecessor_device ==
                   static_cast<std::uintmax_t>(predecessor.st_dev) &&
           leaf.predecessor_inode ==
                   static_cast<std::uintmax_t>(predecessor.st_ino) &&
           leaf.predecessor_ctime_seconds ==
                   static_cast<std::uintmax_t>(predecessor.st_ctim.tv_sec) &&
           leaf.predecessor_ctime_nanoseconds ==
                   static_cast<std::uintmax_t>(predecessor.st_ctim.tv_nsec) &&
           leaf.predecessor_digest == predecessor_digest;
}

bool matches_predecessor_identity(
        const struct stat& status,
        const ReviewedSourceStateRecordIdentity& identity) {
    return static_cast<std::uintmax_t>(status.st_dev) == identity.device &&
           static_cast<std::uintmax_t>(status.st_ino) == identity.inode &&
           static_cast<std::intmax_t>(status.st_ctim.tv_sec) ==
                   identity.status_change_time_seconds &&
           static_cast<std::intmax_t>(status.st_ctim.tv_nsec) ==
                   identity.status_change_time_nanoseconds;
}

std::optional<ReviewedSourceStateStoreFailure> validate_entry_status(
        const struct stat& status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
    const fs::file_type file_type = file_type_from_mode(status.st_mode);
    if(file_type != fs::file_type::regular) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
                entry_path, std::nullopt, file_type);
    }
    if(static_cast<std::uintmax_t>(status.st_uid) != expected_owner) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OwnershipMismatch,
                entry_path);
    }
    if((status.st_mode & 07777) != REVIEWED_SOURCE_STATE_FILE_MODE) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsafePermissions,
                entry_path);
    }
    if(status.st_nlink != 1) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::MultipleHardLinks,
                entry_path);
    }
    return std::nullopt;
}

std::optional<ReviewedSourceStateStoreFailure> validate_package_directory_status(
        const struct stat& status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
    const fs::file_type file_type = file_type_from_mode(status.st_mode);
    if(file_type != fs::file_type::directory) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
                entry_path, std::nullopt, file_type);
    }
    if(static_cast<std::uintmax_t>(status.st_uid) != expected_owner) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OwnershipMismatch,
                entry_path);
    }
    if((status.st_mode & 07777) != REVIEWED_SOURCE_STATE_DIRECTORY_MODE) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsafePermissions,
                entry_path);
    }
    return std::nullopt;
}

ReviewedSourceStateStoreFailure map_resolution_error(
        const xdg_paths::ResolutionError&, const fs::path& entry_path) {
    return store_failure(
            ReviewedSourceStateStoreFailureKind::AuthorityUnavailable,
            entry_path);
}

ReviewedSourceStateStoreFailure map_preparation_error(
        const xdg_directory_safety::PreparationError& error,
        const fs::path& entry_path) {
    const xdg_directory_safety::PreparationErrorCode code = error.failure().code;
    ReviewedSourceStateStoreFailureKind kind =
            ReviewedSourceStateStoreFailureKind::DirectoryPreparationFailed;
    switch(code) {
    case xdg_directory_safety::PreparationErrorCode::MissingAnchor:
    case xdg_directory_safety::PreparationErrorCode::InvalidCreationBoundary:
        kind = ReviewedSourceStateStoreFailureKind::AuthorityUnavailable;
        break;
    case xdg_directory_safety::PreparationErrorCode::Symlink:
    case xdg_directory_safety::PreparationErrorCode::NotDirectory:
        kind = ReviewedSourceStateStoreFailureKind::DirectoryUnavailable;
        break;
    case xdg_directory_safety::PreparationErrorCode::OwnershipMismatch:
        kind = ReviewedSourceStateStoreFailureKind::OwnershipMismatch;
        break;
    case xdg_directory_safety::PreparationErrorCode::UnsafePermissions:
        kind = ReviewedSourceStateStoreFailureKind::UnsafePermissions;
        break;
    case xdg_directory_safety::PreparationErrorCode::ConcurrentReplacement:
        kind = ReviewedSourceStateStoreFailureKind::ConcurrentReplacement;
        break;
    case xdg_directory_safety::PreparationErrorCode::PermissionDenied:
    case xdg_directory_safety::PreparationErrorCode::CreationFailed:
    case xdg_directory_safety::PreparationErrorCode::MetadataFailure:
        kind = ReviewedSourceStateStoreFailureKind::DirectoryPreparationFailed;
        break;
    }
    return store_failure(kind, entry_path, error.failure().system_error);
}

xdg_paths::ReviewedSourceStatePaths resolve_store_paths(
        std::optional<ReviewedSourceStateStoreFailure>& failure,
        const fs::path& diagnostic_path) {
    try {
        return xdg_paths::resolve_reviewed_source_state_process_environment();
    } catch(const xdg_paths::ResolutionError& error) {
        failure = map_resolution_error(error, diagnostic_path);
        return xdg_paths::ReviewedSourceStatePaths{};
    }
}

std::optional<struct stat> inspect_named_entry(
        int directory_descriptor,
        const std::string& leaf_name,
        const fs::path& entry_path,
        std::optional<ReviewedSourceStateStoreFailure>& failure
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        ,
        ReviewedSourceStateStoreTestFailurePoint failure_point =
                ReviewedSourceStateStoreTestFailurePoint::Status
#endif
        ) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(failure_point)) {
        failure = store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::make_error_code(std::errc::permission_denied));
        return std::nullopt;
    }
#endif
    struct stat status {};
    const int status_result = retry_interruptible([&] {
        return ::fstatat(
                directory_descriptor, leaf_name.c_str(), &status,
                AT_SYMLINK_NOFOLLOW);
    });
    if(status_result != 0) {
        const int status_error = errno;
        if(status_error == ENOENT) return std::nullopt;
        failure = store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::error_code(status_error, std::generic_category()));
        return std::nullopt;
    }
    return status;
}

int linkat_retained_inode(
        int source_descriptor,
        int destination_directory_descriptor,
        const char* destination_leaf) {
    const std::string proc_path =
            std::string("/proc/self/fd/") + std::to_string(source_descriptor);
    return retry_interruptible([&] {
        return ::linkat(
                AT_FDCWD, proc_path.c_str(), destination_directory_descriptor,
                destination_leaf, AT_SYMLINK_FOLLOW);
    });
}

std::optional<ReviewedSourceStateStoreFailure> close_descriptor_checked(
        OwnedDescriptor& descriptor, const fs::path& entry_path) {
    const int raw_descriptor = descriptor.release();
    if(raw_descriptor < 0) return std::nullopt;
    // LANDMINE: Linux close(2) releases the fd even when it returns EINTR.
    // Retrying can close a reused descriptor.
    if(::close(raw_descriptor) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::CloseFailed, entry_path,
                current_system_error());
    }
    return std::nullopt;
}

std::optional<ReviewedSourceStateStoreFailure> fsync_descriptor(
        int descriptor, const fs::path& entry_path) {
    if(retry_interruptible([&] { return ::fsync(descriptor); }) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed, entry_path,
                current_system_error());
    }
    return std::nullopt;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
lock_store_directory(
        const xdg_directory_safety::PreparedDirectory& directory,
        int lock_operation,
        const fs::path& entry_path) {
#ifndef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    static_cast<void>(entry_path);
#endif
    const int directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(directory);
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Lock)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::LockFailed, entry_path,
                std::make_error_code(std::errc::resource_unavailable_try_again));
    }
#endif
    const int lock_descriptor = retry_interruptible([&] {
        return ::openat(
                directory_descriptor, ".",
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    });
    if(lock_descriptor < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed,
                directory.path(), current_system_error());
    }
    OwnedDescriptor descriptor(lock_descriptor);
    if(retry_interruptible([&] {
           return ::flock(descriptor.get(), lock_operation);
       }) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::LockFailed,
                directory.path(), current_system_error());
    }
    try {
        directory.require_unchanged_identity();
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, directory.path());
    }
    return descriptor;
}

std::optional<ReviewedSourceStateStoreFailure> fstat_descriptor(
        int descriptor,
        struct stat& status,
        const fs::path& entry_path) {
    if(retry_interruptible([&] { return ::fstat(descriptor, &status); }) != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                current_system_error());
    }
    return std::nullopt;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
open_existing_entry(
        int directory_descriptor,
        const std::string& leaf_name,
        const struct stat& observed_status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Open)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::make_error_code(std::errc::permission_denied));
    }
#endif
    const int raw_descriptor = retry_interruptible([&] {
        return ::openat(
                directory_descriptor, leaf_name.c_str(),
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    });
    if(raw_descriptor < 0) {
        const int open_error = errno;
        if(open_error == ENOENT || open_error == ELOOP ||
           open_error == ENOTDIR) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    entry_path);
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, entry_path,
                std::error_code(open_error, std::generic_category()));
    }
    OwnedDescriptor descriptor(raw_descriptor);
    struct stat opened_status {};
    if(auto failure =
               fstat_descriptor(descriptor.get(), opened_status, entry_path)) {
        return *failure;
    }
    if(auto failure = validate_entry_status(
               opened_status, entry_path, expected_owner)) {
        return *failure;
    }
    if(!same_record_state(observed_status, opened_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    return descriptor;
}

std::variant<std::string, ReviewedSourceStateStoreFailure> read_all_bytes(
        int descriptor,
        const fs::path& entry_path,
        const struct stat& expected_status,
        std::uintmax_t expected_owner
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        ,
        ReviewedSourceStateStoreTestFailurePoint failure_point =
                ReviewedSourceStateStoreTestFailurePoint::Read
#endif
        ) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(failure_point)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ReadFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    if(expected_status.st_size < 0 ||
       static_cast<std::uintmax_t>(expected_status.st_size) >
               reviewed_source_state_store_max_record_bytes) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RecordTooLarge,
                entry_path);
    }
    std::string contents;
    if(expected_status.st_size > 0) {
        contents.reserve(static_cast<std::size_t>(expected_status.st_size));
    }
    std::array<char, 4096> buffer {};
    std::size_t total = 0;
    while(true) {
        const ssize_t count = retry_interruptible([&] {
            return ::read(descriptor, buffer.data(), buffer.size());
        });
        if(count < 0) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ReadFailed,
                    entry_path, current_system_error());
        }
        if(count == 0) break;
        const std::size_t chunk = static_cast<std::size_t>(count);
        if(total > reviewed_source_state_store_max_record_bytes ||
           chunk > reviewed_source_state_store_max_record_bytes - total) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::RecordTooLarge,
                    entry_path);
        }
        contents.append(buffer.data(), chunk);
        total += chunk;
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    ReviewedSourceStateStoreTestRaceContext record_context;
    record_context.package_directory = entry_path.parent_path();
    record_context.record_path = entry_path;
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AfterRecordContentsRead,
            record_context);
#endif

    struct stat after_status {};
    if(auto failure = fstat_descriptor(descriptor, after_status, entry_path)) {
        return *failure;
    }
    // POLICY(#411): the security status is proven where the bytes are handed
    // back, not only where the descriptor was opened. Between the two, a record
    // can gain a hardlink, and the alias may live outside the PackageBase
    // directory, so no rescan of that directory can explain it. Re-asserting
    // the absolute contract here - regular, same owner, 0600, single link - is
    // what keeps ordinary Loaded and ordinary Published from being returned for
    // a record the very next lookup rejects as MultipleHardLinks.
    if(auto failure =
               validate_entry_status(after_status, entry_path, expected_owner)) {
        return *failure;
    }
    if(!same_record_state(expected_status, after_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    if(contents.size() != static_cast<std::size_t>(expected_status.st_size)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                entry_path);
    }
    return contents;
}

ReviewedSourceStateObservation interpret_observed_contents(
        const std::string& raw_contents,
        const PackageBaseIdentity& expected_package_base) {
    const ReviewedSourceStateInterpretation interpreted =
            interpret_reviewed_source_state(raw_contents, expected_package_base);
    return std::visit(
            [](const auto& arm) -> ReviewedSourceStateObservation {
                return arm;
            },
            interpreted);
}

ReviewedSourceStateStoreRead make_present_read(
        ReviewedSourceStateObservation observation,
        std::uint64_t generation,
        std::string leaf_name,
        const struct stat& status,
        std::string raw_contents) {
    return ReviewedSourceStateStoreRead{
            std::move(observation),
            ReviewedSourceStateObservedRecord{
                    generation, std::move(leaf_name),
                    record_identity_from_status(status),
                    std::move(raw_contents)}};
}

std::optional<ReviewedSourceStateStoreFailure> write_all_bytes(
        int descriptor,
        std::string_view contents,
        const fs::path& entry_path) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Write)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, entry_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    while(!contents.empty()) {
        const ssize_t count = retry_interruptible([&] {
            return ::write(descriptor, contents.data(), contents.size());
        });
        if(count <= 0) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::WriteFailed,
                    entry_path,
                    count < 0 ? current_system_error()
                              : std::make_error_code(std::errc::io_error));
        }
        contents.remove_prefix(static_cast<std::size_t>(count));
    }
    return std::nullopt;
}

// POLICY: named unlink is never identity-atomic. Leave unpublished temps and
// published-looking artifacts in place rather than deleting a replacement
// we cannot prove. Temps are non-authoritative residue; everything else in
// the PackageBase directory is inventory for a single-chain proof.

struct ScannedGeneration {
    GenerationLeaf leaf;
    struct stat    status {};
};

struct DirectoryInventory {
    std::vector<std::string>     observed_leaves;
    std::vector<std::string>     managed_leaves;
    std::vector<std::string>     unrecognized_leaves;
    std::vector<ScannedGeneration> parsed;
};

struct ChainProofRecord {
    std::string                       leaf;
    ReviewedSourceStateRecordIdentity identity;
    std::string                       content_digest;

    bool operator==(const ChainProofRecord&) const = default;
};

// POLICY(#411): the complete evidence an accepted history rests on. It holds
// every managed (non-temp) leaf present in the PackageBase directory plus, for
// each record on the proven chain, the filesystem identity observed - including
// its security status and link count - and the digest of the bytes actually
// read.
//
// LANDMINE: this must stay a value that can be re-derived and compared. It is
// the difference between "the chain was single and complete when we started"
// and "the chain is single and complete at the boundary where we answer". Do
// not reduce it to the tip: an ancestor rewrite, an ancestor inode
// replacement, a same-generation fork, or a new future-owned entry appearing
// after the first proof are exactly the mutations a tip-only recheck misses.
// Do not reduce the record identity either: a mutation that only changes a
// record's security status - a hardlink added outside this directory is the
// concrete case - leaves the leaf names and every content digest intact.
struct ChainProofToken {
    std::vector<std::string>      managed_leaves;
    std::vector<ChainProofRecord> records;

    bool operator==(const ChainProofToken&) const = default;
};

ReviewedSourceStateStoreUnsafeHistory unsafe_history(
        ReviewedSourceStateStoreHistoryIssue issue,
        const fs::path& package_path,
        const DirectoryInventory& inventory,
        std::optional<std::uint64_t> matching_generation = std::nullopt,
        std::optional<std::uint64_t> highest_generation = std::nullopt) {
    return ReviewedSourceStateStoreUnsafeHistory{
            issue, package_path, inventory.observed_leaves, matching_generation,
            highest_generation};
}

std::variant<DirectoryInventory, ReviewedSourceStateStoreFailure>
scan_package_directory(
        int package_directory_descriptor,
        const fs::path& package_path,
        std::uintmax_t expected_owner) {
    // LANDMINE: dup(2) shares the file description, so a duplicated directory
    // descriptor also shares the readdir offset. Reusing it leaves the second
    // sweep of the same operation at EOF and reports an empty PackageBase
    // directory. Open a fresh description relative to the retained descriptor
    // instead: same directory inode, independent offset, no path resolution.
    const int scan_descriptor = retry_interruptible([&] {
        return ::openat(
                package_directory_descriptor, ".",
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    });
    if(scan_descriptor < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
                current_system_error());
    }
    DIR* stream = ::fdopendir(scan_descriptor);
    if(stream == nullptr) {
        const int open_error = errno;
        static_cast<void>(::close(scan_descriptor));
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
                std::error_code(open_error, std::generic_category()));
    }
    std::unique_ptr<DIR, int (*)(DIR*)> directory(stream, &::closedir);

    DirectoryInventory inventory;
    std::size_t inspected = 0;
    while(true) {
        errno = 0;
        const struct dirent* entry = ::readdir(directory.get());
        if(entry == nullptr) {
            if(errno != 0) {
                return store_failure(
                        ReviewedSourceStateStoreFailureKind::ReadFailed,
                        package_path, current_system_error());
            }
            break;
        }
        const std::string_view name(entry->d_name);
        if(name == "." || name == "..") continue;
        ++inspected;
        if(inspected > MAX_PACKAGE_DIRECTORY_ENTRIES) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::RecordTooLarge,
                    package_path);
        }
        inventory.observed_leaves.emplace_back(name);
    }
    std::sort(inventory.observed_leaves.begin(), inventory.observed_leaves.end());

    for(const std::string& name : inventory.observed_leaves) {
        // POLICY: an operation-private crash temp is non-authoritative residue,
        // so it stays out of the proof evidence. Everything else in the
        // PackageBase directory is inventory a single-chain proof must explain.
        if(is_internal_temp_leaf(name)) continue;
        inventory.managed_leaves.push_back(name);
        const std::optional<GenerationLeaf> parsed = parse_generation_leaf(name);
        if(!parsed.has_value()) {
            inventory.unrecognized_leaves.push_back(name);
            continue;
        }
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> status = inspect_named_entry(
                package_directory_descriptor, parsed->leaf,
                package_path / parsed->leaf, inspect_failure);
        if(inspect_failure.has_value()) return *inspect_failure;
        if(!status.has_value()) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    package_path / parsed->leaf);
        }
        if(auto failure = validate_entry_status(
                   *status, package_path / parsed->leaf, expected_owner)) {
            return *failure;
        }
        inventory.parsed.push_back(ScannedGeneration{*parsed, *status});
    }
    return inventory;
}

std::variant<std::string, ReviewedSourceStateStoreFailure>
read_generation_contents(
        int package_directory_descriptor,
        const ScannedGeneration& generation,
        const fs::path& package_path,
        std::uintmax_t expected_owner) {
    const fs::path path = package_path / generation.leaf.leaf;
    auto opened = open_existing_entry(
            package_directory_descriptor, generation.leaf.leaf,
            generation.status, path, expected_owner);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
        return *failure;
    }
    OwnedDescriptor descriptor = std::get<OwnedDescriptor>(std::move(opened));
    auto contents = read_all_bytes(
            descriptor.get(), path, generation.status, expected_owner);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
        return *failure;
    }
    if(auto close_failure = close_descriptor_checked(descriptor, path)) {
        return *close_failure;
    }
    return std::get<std::string>(std::move(contents));
}

std::variant<std::string, ReviewedSourceStateStoreFailure>
reread_descriptor_contents(
        int descriptor,
        const fs::path& entry_path,
        std::uintmax_t expected_owner
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        ,
        ReviewedSourceStateStoreTestFailurePoint failure_point =
                ReviewedSourceStateStoreTestFailurePoint::Read
#endif
        ) {
    if(retry_interruptible([&] {
           return ::lseek(descriptor, 0, SEEK_SET);
       }) < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ReadFailed, entry_path,
                current_system_error());
    }
    struct stat status {};
    if(auto failure = fstat_descriptor(descriptor, status, entry_path)) {
        return *failure;
    }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    return read_all_bytes(
            descriptor, entry_path, status, expected_owner, failure_point);
#else
    return read_all_bytes(descriptor, entry_path, status, expected_owner);
#endif
}

bool is_future_schema_contents(const std::string& raw_contents) {
    return std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
            decode_reviewed_source_state(raw_contents));
}

struct ProvenChain {
    std::optional<ScannedGeneration> tip;
    std::string                      tip_contents;
    ChainProofToken                  token;
};

using ChainProof = std::variant<
        ProvenChain,
        ReviewedSourceStateStoreUnsafeHistory,
        ReviewedSourceStateStoreFailure>;

ChainProof prove_authoritative_chain(
        int package_directory_descriptor,
        const DirectoryInventory& inventory,
        const fs::path& package_path,
        std::uintmax_t expected_owner) {
    std::optional<std::uint64_t> highest_generation;
    for(const ScannedGeneration& candidate : inventory.parsed) {
        if(!highest_generation.has_value() ||
           candidate.leaf.generation > *highest_generation) {
            highest_generation = candidate.leaf.generation;
        }
    }

    if(!inventory.unrecognized_leaves.empty()) {
        return unsafe_history(
                ReviewedSourceStateStoreHistoryIssue::UnrecognizedManagedEntry,
                package_path, inventory, std::nullopt, highest_generation);
    }
    if(inventory.parsed.empty()) {
        return ProvenChain{
                std::nullopt, {},
                ChainProofToken{inventory.managed_leaves, {}}};
    }

    const ScannedGeneration* origin = nullptr;
    for(const ScannedGeneration& candidate : inventory.parsed) {
        if(candidate.leaf.generation == 1 && !candidate.leaf.has_predecessor) {
            origin = &candidate;
            break;
        }
    }
    if(origin == nullptr) {
        return unsafe_history(
                ReviewedSourceStateStoreHistoryIssue::MissingOriginWithArtifacts,
                package_path, inventory, std::nullopt, highest_generation);
    }

    // LANDMINE: generation numbers come from untrusted leaf names and span the
    // whole uint64 range. Never size or index a container by a generation
    // value: UINT64_MAX + 1 wraps to an empty vector and a large sparse value
    // asks for an allocation the typed failure contract must not depend on.
    // Order the records that actually exist instead, and answer membership by
    // search over that ordering.
    std::vector<const ScannedGeneration*> ordered;
    ordered.reserve(inventory.parsed.size());
    for(const ScannedGeneration& candidate : inventory.parsed) {
        ordered.push_back(&candidate);
    }
    std::sort(
            ordered.begin(), ordered.end(),
            [](const ScannedGeneration* left, const ScannedGeneration* right) {
                if(left->leaf.generation != right->leaf.generation) {
                    return left->leaf.generation < right->leaf.generation;
                }
                return left->leaf.leaf < right->leaf.leaf;
            });
    for(std::size_t index = 1; index < ordered.size(); ++index) {
        if(ordered[index]->leaf.generation ==
           ordered[index - 1]->leaf.generation) {
            return unsafe_history(
                    ReviewedSourceStateStoreHistoryIssue::ForkDetected,
                    package_path, inventory, origin->leaf.generation,
                    highest_generation);
        }
    }
    const auto find_generation =
            [&ordered](std::uint64_t generation) -> const ScannedGeneration* {
        const auto position = std::lower_bound(
                ordered.begin(), ordered.end(), generation,
                [](const ScannedGeneration* candidate, std::uint64_t value) {
                    return candidate->leaf.generation < value;
                });
        if(position == ordered.end() ||
           (*position)->leaf.generation != generation) {
            return nullptr;
        }
        return *position;
    };

    std::vector<const ScannedGeneration*> chain;
    std::vector<std::string> chain_contents;
    const ScannedGeneration* current = origin;
    while(true) {
        const fs::path current_path = package_path / current->leaf.leaf;
        if(auto failure = validate_entry_status(
                   current->status, current_path, expected_owner)) {
            return *failure;
        }
        auto contents = read_generation_contents(
                package_directory_descriptor, *current, package_path,
                expected_owner);
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
            return *failure;
        }
        chain.push_back(current);
        chain_contents.push_back(std::get<std::string>(std::move(contents)));
        const std::string digest = content_digest_hex(chain_contents.back());
        const std::uint64_t next = current->leaf.generation + 1;
        if(next == 0 || !highest_generation.has_value() ||
           next > *highest_generation) {
            break;
        }
        const ScannedGeneration* candidate = find_generation(next);
        if(candidate == nullptr ||
           !matches_predecessor_binding(
                   candidate->leaf, current->status, digest)) {
            break;
        }
        current = candidate;
    }

    std::optional<std::uint64_t> matching_generation = chain.back()->leaf.generation;
    // Forks are already rejected and generation 1 is present, so the parsed set
    // covers exactly 1..highest when the count equals the highest value. A
    // UINT64_MAX or otherwise sparse generation makes this a plain gap.
    const bool has_hole =
            !highest_generation.has_value() ||
            *highest_generation != static_cast<std::uint64_t>(ordered.size());

    const bool has_extras = chain.size() != inventory.parsed.size();
    bool has_future_owned = false;
    for(std::size_t index = 0; index + 1 < chain_contents.size(); ++index) {
        if(is_future_schema_contents(chain_contents[index])) {
            has_future_owned = true;
            break;
        }
    }
    if(has_extras) {
        for(const ScannedGeneration& candidate : inventory.parsed) {
            bool on_chain = false;
            for(const ScannedGeneration* member : chain) {
                if(member->leaf.leaf == candidate.leaf.leaf) {
                    on_chain = true;
                    break;
                }
            }
            if(on_chain) continue;
            auto extra_contents = read_generation_contents(
                    package_directory_descriptor, candidate, package_path,
                    expected_owner);
            if(const auto* failure = std::get_if<ReviewedSourceStateStoreFailure>(
                       &extra_contents)) {
                return *failure;
            }
            if(is_future_schema_contents(
                       std::get<std::string>(extra_contents))) {
                has_future_owned = true;
                break;
            }
        }
    }

    if(has_future_owned) {
        return unsafe_history(
                ReviewedSourceStateStoreHistoryIssue::FutureOwnedArtifact,
                package_path, inventory, matching_generation, highest_generation);
    }
    if(has_hole) {
        return unsafe_history(
                ReviewedSourceStateStoreHistoryIssue::ChainGap, package_path,
                inventory, matching_generation, highest_generation);
    }
    if(has_extras) {
        const ReviewedSourceStateStoreHistoryIssue issue =
                (highest_generation.has_value() &&
                 *highest_generation > *matching_generation)
                        ? ReviewedSourceStateStoreHistoryIssue::
                                  HigherGenerationUnreachable
                        : ReviewedSourceStateStoreHistoryIssue::OrphanSuccessor;
        return unsafe_history(
                issue, package_path, inventory, matching_generation,
                highest_generation);
    }

    ChainProofToken token{inventory.managed_leaves, {}};
    token.records.reserve(chain.size());
    for(std::size_t index = 0; index < chain.size(); ++index) {
        token.records.push_back(ChainProofRecord{
                chain[index]->leaf.leaf,
                record_identity_from_status(chain[index]->status),
                content_digest_hex(chain_contents[index])});
    }
    return ProvenChain{
            *chain.back(), std::move(chain_contents.back()), std::move(token)};
}

// Re-derive the whole proof and require it to match the evidence the operation
// already committed to. An ancestor rewrite, an ancestor inode replacement, a
// same-generation fork, a gap, or a new managed entry appearing after the first
// proof surfaces here either as a typed unsafe history or as a token mismatch.
//
// WHY a full rescan instead of retaining every proven descriptor: the retained
// form would still have to re-read each record to detect a same-inode rewrite,
// so it buys no additional authority while holding one descriptor per
// generation for the whole operation. Chains here are a handful of records per
// PackageBase, so reproving costs one extra readdir plus one read per record.
ChainProof reprove_authoritative_chain(
        int package_directory_descriptor,
        const fs::path& package_path,
        std::uintmax_t expected_owner,
        const ChainProofToken& expected_token) {
    auto scanned = scan_package_directory(
            package_directory_descriptor, package_path, expected_owner);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&scanned)) {
        return *failure;
    }
    const DirectoryInventory inventory =
            std::get<DirectoryInventory>(std::move(scanned));
    ChainProof proof = prove_authoritative_chain(
            package_directory_descriptor, inventory, package_path,
            expected_owner);
    const ProvenChain* proven = std::get_if<ProvenChain>(&proof);
    if(proven == nullptr) return proof;
    if(proven->token != expected_token) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                package_path);
    }
    return proof;
}

// The proof a publication must satisfy after its own successor is linked: the
// evidence it proved before the commit, plus exactly that one new record.
ChainProofToken extend_proof_token(
        const ChainProofToken& token,
        const std::string& published_leaf,
        const ReviewedSourceStateRecordIdentity& published_identity,
        std::string_view published_contents) {
    ChainProofToken extended = token;
    extended.managed_leaves.push_back(published_leaf);
    std::sort(extended.managed_leaves.begin(), extended.managed_leaves.end());
    extended.records.push_back(ChainProofRecord{
            published_leaf, published_identity,
            content_digest_hex(published_contents)});
    return extended;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
open_package_directory(
        int store_directory_descriptor,
        const std::string& leaf_name,
        const fs::path& package_path,
        std::uintmax_t expected_owner,
        bool create_if_missing,
        bool& created) {
    created = false;
    std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
    std::optional<struct stat> named_status = inspect_named_entry(
            store_directory_descriptor, leaf_name, package_path,
            inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;

    if(!named_status.has_value()) {
        if(!create_if_missing) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::OpenFailed,
                    package_path,
                    std::make_error_code(std::errc::no_such_file_or_directory));
        }
        const int mkdir_result = retry_interruptible([&] {
            return ::mkdirat(
                    store_directory_descriptor, leaf_name.c_str(),
                    REVIEWED_SOURCE_STATE_DIRECTORY_MODE);
        });
        if(mkdir_result != 0 && errno != EEXIST) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::
                            DirectoryPreparationFailed,
                    package_path, current_system_error());
        }
        created = mkdir_result == 0;
        inspect_failure.reset();
        named_status = inspect_named_entry(
                store_directory_descriptor, leaf_name, package_path,
                inspect_failure);
        if(inspect_failure.has_value()) return *inspect_failure;
        if(!named_status.has_value()) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    package_path);
        }
    }

    if(auto failure = validate_package_directory_status(
               *named_status, package_path, expected_owner)) {
        return *failure;
    }

    const int raw_descriptor = retry_interruptible([&] {
        return ::openat(
                store_directory_descriptor, leaf_name.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    });
    if(raw_descriptor < 0) {
        const int open_error = errno;
        if(open_error == ENOENT || open_error == ELOOP ||
           open_error == ENOTDIR) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    package_path);
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
                std::error_code(open_error, std::generic_category()));
    }
    OwnedDescriptor descriptor(raw_descriptor);
    if(created) {
        if(retry_interruptible([&] {
               return ::fchmod(
                       descriptor.get(), REVIEWED_SOURCE_STATE_DIRECTORY_MODE);
           }) != 0) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::
                            DirectoryPreparationFailed,
                    package_path, current_system_error());
        }
    }
    struct stat opened_status {};
    if(auto failure =
               fstat_descriptor(descriptor.get(), opened_status, package_path)) {
        return *failure;
    }
    if(auto failure = validate_package_directory_status(
               opened_status, package_path, expected_owner)) {
        return *failure;
    }
    inspect_failure.reset();
    const std::optional<struct stat> named_after_open = inspect_named_entry(
            store_directory_descriptor, leaf_name, package_path, inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!named_after_open.has_value() ||
       !same_filesystem_identity(*named_after_open, opened_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                package_path);
    }
    if(auto failure = validate_package_directory_status(
               *named_after_open, package_path, expected_owner)) {
        return *failure;
    }
    return descriptor;
}

std::optional<ReviewedSourceStateStoreFailure>
revalidate_named_package_directory(
        int store_directory_descriptor,
        int package_directory_descriptor,
        const std::string& leaf_name,
        const fs::path& package_path,
        std::uintmax_t expected_owner) {
    struct stat opened_status {};
    if(auto failure = fstat_descriptor(
               package_directory_descriptor, opened_status, package_path)) {
        return failure;
    }
    if(auto failure = validate_package_directory_status(
               opened_status, package_path, expected_owner)) {
        return failure;
    }
    std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
    const std::optional<struct stat> named_status = inspect_named_entry(
            store_directory_descriptor, leaf_name, package_path, inspect_failure);
    if(inspect_failure.has_value()) return inspect_failure;
    if(!named_status.has_value() ||
       !same_filesystem_identity(*named_status, opened_status)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                package_path);
    }
    if(auto failure = validate_package_directory_status(
               *named_status, package_path, expected_owner)) {
        return failure;
    }
    return std::nullopt;
}

std::optional<ReviewedSourceStateStoreFailure> validate_anonymous_record_status(
        const struct stat& status,
        const fs::path& entry_path,
        std::uintmax_t expected_owner) {
    const fs::file_type file_type = file_type_from_mode(status.st_mode);
    if(file_type != fs::file_type::regular) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsupportedFileType,
                entry_path, std::nullopt, file_type);
    }
    if(static_cast<std::uintmax_t>(status.st_uid) != expected_owner) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OwnershipMismatch,
                entry_path);
    }
    if((status.st_mode & 07777) != REVIEWED_SOURCE_STATE_FILE_MODE) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::UnsafePermissions,
                entry_path);
    }
    if(status.st_nlink != 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::MultipleHardLinks,
                entry_path);
    }
    return std::nullopt;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
create_anonymous_record(
        int package_directory_descriptor, const fs::path& package_path) {
#ifndef O_TMPFILE
    static_cast<void>(package_directory_descriptor);
    return store_failure(
            ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
            std::make_error_code(std::errc::function_not_supported));
#else
    const int temporary_descriptor = retry_interruptible([&] {
        return ::openat(
                package_directory_descriptor, ".",
                O_TMPFILE | O_RDWR | O_CLOEXEC, REVIEWED_SOURCE_STATE_FILE_MODE);
    });
    if(temporary_descriptor < 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
                current_system_error());
    }
    return OwnedDescriptor(temporary_descriptor);
#endif
}

ReviewedSourceStateStorePublishedUncertain published_uncertain(
        const ReviewedSourceState& state,
        std::optional<ReviewedSourceStateObservedRecord> observed,
        ReviewedSourceStatePostPublicationIssue issue,
        ReviewedSourceStateStoreFailureKind failure_kind,
        const fs::path& entry_path,
        std::optional<fs::path> leftover_artifact = std::nullopt,
        std::optional<std::error_code> system_error = std::nullopt) {
    return ReviewedSourceStateStorePublishedUncertain{
            state, std::move(observed), issue, failure_kind, entry_path,
            std::move(leftover_artifact), std::move(system_error)};
}

} // namespace

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
void fail_next_reviewed_source_state_store_operation_for_test(
        ReviewedSourceStateStoreTestFailurePoint failure_point) {
    g_injected_failure = failure_point;
}

void run_reviewed_source_state_store_race_once_for_test(
        ReviewedSourceStateStoreTestRacePoint race_point,
        ReviewedSourceStateStoreTestRaceHandler handler) {
    g_race_point = race_point;
    g_race_handler = handler;
    g_race_pending = handler != nullptr;
}

void reset_reviewed_source_state_store_test_hooks() {
    g_injected_failure.reset();
    g_race_handler = nullptr;
    g_race_pending = false;
}
#endif

xdg_paths::ReviewedSourceStatePaths reviewed_source_state_store_paths() {
    return xdg_paths::resolve_reviewed_source_state_process_environment();
}

std::filesystem::path reviewed_source_state_store_directory() {
    return reviewed_source_state_store_paths().directory;
}

std::filesystem::path reviewed_source_state_store_entry_path(
        const PackageBaseIdentity& package_base) {
    require_aur_known_package_base(package_base);
    return reviewed_source_state_store_directory() /
           package_directory_leaf(package_base);
}

std::string reviewed_source_state_store_origin_leaf() {
    return std::string(ORIGIN_LEAF);
}

std::string reviewed_source_state_store_successor_leaf(
        std::uint64_t next_generation,
        const ReviewedSourceStateRecordIdentity& predecessor,
        std::string_view predecessor_raw_contents) {
    const std::uintmax_t ctime_seconds =
            predecessor.status_change_time_seconds < 0
                    ? 0
                    : static_cast<std::uintmax_t>(
                              predecessor.status_change_time_seconds);
    const std::uintmax_t ctime_nanoseconds =
            predecessor.status_change_time_nanoseconds < 0
                    ? 0
                    : static_cast<std::uintmax_t>(
                              predecessor.status_change_time_nanoseconds);
    return std::to_string(next_generation) + "." +
           std::to_string(predecessor.device) + "-" +
           std::to_string(predecessor.inode) + "-" +
           std::to_string(ctime_seconds) + "-" +
           std::to_string(ctime_nanoseconds) + "." +
           content_digest_hex(predecessor_raw_contents) +
           std::string(ENTRY_SUFFIX);
}

ReviewedSourceStateStoreReadResult read_reviewed_source_state(
        const PackageBaseIdentity& expected_package_base) {
    require_aur_known_package_base(expected_package_base);
    const std::string package_leaf =
            package_directory_leaf(expected_package_base);
    const fs::path diagnostic_path(package_leaf);
    std::optional<ReviewedSourceStateStoreFailure> resolve_failure;
    const xdg_paths::ReviewedSourceStatePaths paths =
            resolve_store_paths(resolve_failure, diagnostic_path);
    if(resolve_failure.has_value()) return *resolve_failure;
    const fs::path package_path = paths.directory / package_leaf;

    std::unique_ptr<xdg_directory_safety::PreparedDirectory> directory;
    try {
        std::optional<xdg_directory_safety::PreparedDirectory> opened =
                xdg_directory_safety::open_existing_directory(paths);
        if(!opened.has_value()) {
            return ReviewedSourceStateStoreRead{
                    ReviewedSourceStateMissing{}, std::nullopt};
        }
        directory = std::make_unique<xdg_directory_safety::PreparedDirectory>(
                std::move(*opened));
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, package_path);
    }
    if(!directory) {
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }

    // POLICY(#411): a lookup answer is authoritative only if the retained XDG
    // lineage still denotes the named store when we answer. Publication already
    // reproves this after its commit; lookup must be symmetric, otherwise an
    // aur/ tree renamed away mid-lookup is reported as the current baseline
    // while the replacement tree holds the real state.
    const auto final_lineage_failure =
            [&directory, &package_path]()
            -> std::optional<ReviewedSourceStateStoreFailure> {
        try {
            directory->require_unchanged_identity();
        } catch(const xdg_directory_safety::PreparationError& error) {
            return map_preparation_error(error, package_path);
        }
        return std::nullopt;
    };

    auto lock = lock_store_directory(*directory, LOCK_SH, package_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&lock)) {
        return *failure;
    }
    OwnedDescriptor lock_descriptor =
            std::get<OwnedDescriptor>(std::move(lock));

    const int store_directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(*directory);
    std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
    const std::optional<struct stat> package_status = inspect_named_entry(
            store_directory_descriptor, package_leaf, package_path,
            inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!package_status.has_value()) {
        if(auto lineage = final_lineage_failure()) return *lineage;
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }
    if(auto failure = validate_package_directory_status(
               *package_status, package_path, directory->owner())) {
        return *failure;
    }

    bool created = false;
    auto opened_package = open_package_directory(
            store_directory_descriptor, package_leaf, package_path,
            directory->owner(), false, created);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened_package)) {
        return *failure;
    }
    OwnedDescriptor package_directory =
            std::get<OwnedDescriptor>(std::move(opened_package));

    auto scanned = scan_package_directory(
            package_directory.get(), package_path, directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&scanned)) {
        return *failure;
    }
    const DirectoryInventory inventory =
            std::get<DirectoryInventory>(std::move(scanned));
    auto proof = prove_authoritative_chain(
            package_directory.get(), inventory, package_path,
            directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&proof)) {
        return *failure;
    }
    if(const auto* unsafe =
               std::get_if<ReviewedSourceStateStoreUnsafeHistory>(&proof)) {
        if(auto lineage = final_lineage_failure()) return *lineage;
        return *unsafe;
    }
    const ProvenChain proven = std::get<ProvenChain>(std::move(proof));

    // The boundary reproof for every present / Missing lookup arm. Ancestors
    // were read and closed above, so nothing but this re-derivation can tell
    // that generation 1 or 2 changed while the tip stayed still. On success it
    // hands back the freshly proven chain, which is the observation the answer
    // is built from.
    std::optional<ProvenChain> boundary_proven;
    const auto boundary_reproof =
            [&]() -> std::optional<ReviewedSourceStateStoreReadResult> {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        ReviewedSourceStateStoreTestRaceContext read_context;
        read_context.package_directory = package_path;
        invoke_race(
                ReviewedSourceStateStoreTestRacePoint::AfterReadAuthorityProof,
                read_context);
#endif
        ChainProof reproof = reprove_authoritative_chain(
                package_directory.get(), package_path, directory->owner(),
                proven.token);
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&reproof)) {
            return ReviewedSourceStateStoreReadResult{*failure};
        }
        if(const auto* unsafe =
                   std::get_if<ReviewedSourceStateStoreUnsafeHistory>(
                           &reproof)) {
            if(auto lineage = final_lineage_failure()) {
                return ReviewedSourceStateStoreReadResult{*lineage};
            }
            return ReviewedSourceStateStoreReadResult{*unsafe};
        }
        boundary_proven = std::get<ProvenChain>(std::move(reproof));
        return std::nullopt;
    };

    if(!proven.tip.has_value()) {
        if(auto diverged = boundary_reproof()) return *diverged;
        if(auto revalidate = revalidate_named_package_directory(
                   store_directory_descriptor, package_directory.get(),
                   package_leaf, package_path, directory->owner())) {
            return *revalidate;
        }
        if(auto lineage = final_lineage_failure()) return *lineage;
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }
    const ScannedGeneration current = *proven.tip;

    const fs::path tip_path = package_path / current.leaf.leaf;
    auto opened = open_existing_entry(
            package_directory.get(), current.leaf.leaf, current.status,
            tip_path, directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
        return *failure;
    }
    OwnedDescriptor file_descriptor =
            std::get<OwnedDescriptor>(std::move(opened));

    inspect_failure.reset();
    const std::optional<struct stat> revalidated = inspect_named_entry(
            package_directory.get(), current.leaf.leaf, tip_path,
            inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!revalidated.has_value() ||
       !same_record_state(current.status, *revalidated)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                tip_path);
    }

    auto contents = read_all_bytes(
            file_descriptor.get(), tip_path, current.status, directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
        return *failure;
    }
    std::string raw = std::get<std::string>(std::move(contents));
    if(auto close_failure =
               close_descriptor_checked(file_descriptor, tip_path)) {
        return *close_failure;
    }
    if(auto diverged = boundary_reproof()) return *diverged;
    // The token matched, so the tip record and its digest are unchanged.
    // Comparing the bytes we are about to return against the bytes the boundary
    // proof actually read states that implication instead of inferring it.
    if(!boundary_proven.has_value() || raw != boundary_proven->tip_contents) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                tip_path);
    }
    if(auto revalidate = revalidate_named_package_directory(
               store_directory_descriptor, package_directory.get(),
               package_leaf, package_path, directory->owner())) {
        return *revalidate;
    }
    if(auto close_failure =
               close_descriptor_checked(package_directory, package_path)) {
        return *close_failure;
    }
    if(auto close_failure =
               close_descriptor_checked(lock_descriptor, directory->path())) {
        return *close_failure;
    }
    if(auto lineage = final_lineage_failure()) return *lineage;

    const ReviewedSourceStateObservation observation =
            interpret_observed_contents(raw, expected_package_base);
    return make_present_read(
            std::move(observation), current.leaf.generation,
            current.leaf.leaf, current.status, std::move(raw));
}

ReviewedSourceStateStorePublishResult publish_reviewed_source_state(
        const ReviewedSourceState& next_state,
        const std::optional<ReviewedSourceStateObservedRecord>&
                expected_observed) {
    const PackageBaseIdentity& expected_package_base = next_state.package_base();
    require_aur_known_package_base(expected_package_base);
    if(next_state.schema_version() != reviewed_source_state_schema_version) {
        throw std::invalid_argument(
                "Reviewed source state store cannot publish a non-current schema.");
    }

    const std::string package_leaf =
            package_directory_leaf(expected_package_base);
    const fs::path diagnostic_path(package_leaf);
    std::optional<ReviewedSourceStateStoreFailure> resolve_failure;
    const xdg_paths::ReviewedSourceStatePaths paths =
            resolve_store_paths(resolve_failure, diagnostic_path);
    if(resolve_failure.has_value()) return *resolve_failure;
    const fs::path package_path = paths.directory / package_leaf;

    std::unique_ptr<xdg_directory_safety::PreparedDirectory> directory;
    try {
        directory = std::make_unique<xdg_directory_safety::PreparedDirectory>(
                xdg_directory_safety::prepare_directory(paths));
    } catch(const xdg_directory_safety::PreparationError& error) {
        return map_preparation_error(error, package_path);
    }

    auto lock = lock_store_directory(*directory, LOCK_EX, package_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&lock)) {
        return *failure;
    }
    OwnedDescriptor store_sync = std::get<OwnedDescriptor>(std::move(lock));

    const int store_directory_descriptor =
            ReviewedSourceStateDirectoryAccess::descriptor(*directory);
    bool created_package_directory = false;
    auto opened_package = open_package_directory(
            store_directory_descriptor, package_leaf, package_path,
            directory->owner(), true, created_package_directory);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened_package)) {
        return *failure;
    }
    OwnedDescriptor package_directory =
            std::get<OwnedDescriptor>(std::move(opened_package));
    if(created_package_directory) {
        if(auto sync_failure =
                   fsync_descriptor(store_sync.get(), directory->path())) {
            return *sync_failure;
        }
    }

    auto scanned = scan_package_directory(
            package_directory.get(), package_path, directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&scanned)) {
        return *failure;
    }
    const DirectoryInventory inventory =
            std::get<DirectoryInventory>(std::move(scanned));
    auto proof = prove_authoritative_chain(
            package_directory.get(), inventory, package_path,
            directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&proof)) {
        return *failure;
    }
    if(const auto* unsafe =
               std::get_if<ReviewedSourceStateStoreUnsafeHistory>(&proof)) {
        return *unsafe;
    }
    const ProvenChain proven = std::get<ProvenChain>(std::move(proof));
    const std::optional<ScannedGeneration> current = proven.tip;

    if(expected_observed.has_value() != current.has_value()) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                package_path);
    }

    OwnedDescriptor retained_current;
    std::string existing_raw;
    if(current.has_value()) {
        const fs::path current_path = package_path / current->leaf.leaf;
        if(current->leaf.generation != expected_observed->generation ||
           current->leaf.leaf != expected_observed->leaf_name ||
           !matches_record_identity(
                   current->status, expected_observed->identity)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    current_path);
        }
        auto opened = open_existing_entry(
                package_directory.get(), current->leaf.leaf, current->status,
                current_path, directory->owner());
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
            return *failure;
        }
        retained_current = std::get<OwnedDescriptor>(std::move(opened));
        auto contents = read_all_bytes(
                retained_current.get(), current_path, current->status,
                directory->owner());
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
            return *failure;
        }
        existing_raw = std::get<std::string>(std::move(contents));
        if(existing_raw != expected_observed->raw_contents) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    current_path);
        }
        const ReviewedSourceStateDocument decoded =
                decode_reviewed_source_state(existing_raw);
        if(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
                   decoded)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::
                            FutureSchemaOverwriteRefused,
                    current_path);
        }
    }

    const std::uint64_t next_generation =
            current.has_value() ? current->leaf.generation + 1 : 1;
    if(next_generation == 0) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, package_path);
    }
    const std::string publication_leaf =
            current.has_value()
                    ? reviewed_source_state_store_successor_leaf(
                              next_generation, expected_observed->identity,
                              expected_observed->raw_contents)
                    : reviewed_source_state_store_origin_leaf();
    const fs::path publication_path = package_path / publication_leaf;
    const std::optional<fs::path> current_path =
            current.has_value()
                    ? std::optional<fs::path>(package_path / current->leaf.leaf)
                    : std::nullopt;

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    ReviewedSourceStateStoreTestRaceContext race_context{
            package_path, publication_path, publication_leaf, current_path,
            {}, next_generation};
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::BeforePublication,
            race_context);
#endif

    const std::string publication = encode_reviewed_source_state(next_state);
    auto created_anonymous = create_anonymous_record(
            package_directory.get(), package_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&created_anonymous)) {
        return *failure;
    }
    OwnedDescriptor temporary =
            std::get<OwnedDescriptor>(std::move(created_anonymous));

    bool was_published = false;
    auto abandon_unpublished = [&]() {
        if(was_published) return;
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        invoke_race(
                ReviewedSourceStateStoreTestRacePoint::BeforeCleanup,
                race_context);
#endif
    };

    if(retry_interruptible([&] {
           return ::fchmod(temporary.get(), REVIEWED_SOURCE_STATE_FILE_MODE);
       }) != 0) {
        abandon_unpublished();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, package_path,
                current_system_error());
    }
    if(auto write_failure =
               write_all_bytes(temporary.get(), publication, package_path)) {
        abandon_unpublished();
        return *write_failure;
    }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Sync)) {
        abandon_unpublished();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed, package_path,
                std::make_error_code(std::errc::io_error));
    }
#endif
    if(auto sync_failure = fsync_descriptor(temporary.get(), package_path)) {
        abandon_unpublished();
        return *sync_failure;
    }

    struct stat temporary_status {};
    if(auto failure =
               fstat_descriptor(temporary.get(), temporary_status, package_path)) {
        abandon_unpublished();
        return *failure;
    }
    if(auto failure = validate_anonymous_record_status(
               temporary_status, package_path, directory->owner())) {
        abandon_unpublished();
        return *failure;
    }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    race_context.source_device =
            static_cast<std::uintmax_t>(temporary_status.st_dev);
    race_context.source_inode =
            static_cast<std::uintmax_t>(temporary_status.st_ino);
#endif

    if(current.has_value()) {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> pre_commit_status = inspect_named_entry(
                package_directory.get(), current->leaf.leaf, *current_path,
                inspect_failure);
        if(inspect_failure.has_value()) {
            abandon_unpublished();
            return *inspect_failure;
        }
        if(!pre_commit_status.has_value() ||
           !matches_predecessor_identity(
                   *pre_commit_status, expected_observed->identity)) {
            abandon_unpublished();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    *current_path);
        }
        auto pre_commit_contents = reread_descriptor_contents(
                retained_current.get(), *current_path, directory->owner());
        if(const auto* failure = std::get_if<ReviewedSourceStateStoreFailure>(
                   &pre_commit_contents)) {
            abandon_unpublished();
            return *failure;
        }
        if(std::get<std::string>(pre_commit_contents) !=
           expected_observed->raw_contents) {
            abandon_unpublished();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    *current_path);
        }
    } else {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> origin_status = inspect_named_entry(
                package_directory.get(), publication_leaf, publication_path,
                inspect_failure);
        if(inspect_failure.has_value()) {
            abandon_unpublished();
            return *inspect_failure;
        }
        if(origin_status.has_value()) {
            abandon_unpublished();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path);
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    // LANDMINE: predecessor identity/bytes can still change after this
    // recheck. The successor leaf binds inode+ctime+digest, so a rewritten
    // predecessor cannot make this publication the chain tip.
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            race_context);
#endif
    if(auto revalidate = revalidate_named_package_directory(
               store_directory_descriptor, package_directory.get(),
               package_leaf, package_path, directory->owner())) {
        abandon_unpublished();
        return *revalidate;
    }

    // POLICY(#411): the authority proof must still hold at the commit point,
    // not merely when the operation started. Rechecking only the current tip
    // lets an ancestor rewrite, an ancestor inode replacement, a fork, or a new
    // future-owned entry slip between the proof and linkat, which would make
    // this call return ordinary Published for a state the very next lookup
    // reports as unsafe history. Failing here is still pre-commit: the
    // successor inode is unnamed, so no artifact survives the refusal.
    {
        ChainProof reproof = reprove_authoritative_chain(
                package_directory.get(), package_path, directory->owner(),
                proven.token);
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&reproof)) {
            abandon_unpublished();
            return *failure;
        }
        if(const auto* unsafe =
                   std::get_if<ReviewedSourceStateStoreUnsafeHistory>(
                           &reproof)) {
            abandon_unpublished();
            return *unsafe;
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AfterAuthorityProof,
            race_context);
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Rename)) {
        abandon_unpublished();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed,
                publication_path, std::make_error_code(std::errc::io_error));
    }
#endif

    // POLICY: linkat of /proc/self/fd/<retained> follows the descriptor's
    // inode. The source capability is the unnamed O_TMPFILE inode, not a
    // replaceable pathname in the PackageBase directory. EEXIST is the
    // destination no-replace equivalent of RENAME_NOREPLACE.
    if(linkat_retained_inode(
               temporary.get(), package_directory.get(),
               publication_leaf.c_str()) != 0) {
        const int link_error = errno;
        abandon_unpublished();
        if(link_error == EEXIST || link_error == ENOTEMPTY ||
           link_error == ENOENT || link_error == ENOTDIR ||
           link_error == ELOOP) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path,
                    std::error_code(link_error, std::generic_category()));
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed,
                publication_path,
                std::error_code(link_error, std::generic_category()));
    }
    was_published = true;

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AfterPublication,
            race_context);
#endif

    struct stat published_descriptor_status {};
    if(auto failure = fstat_descriptor(
               temporary.get(), published_descriptor_status, publication_path)) {
        return published_uncertain(
                next_state, std::nullopt,
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                failure->kind, publication_path, std::nullopt,
                failure->system_error);
    }

    // LANDMINE: past this point the record is named and permanent. The store
    // cannot unlink it, so it must never report an ordinary definite failure
    // here. Disowning a committed successor is exactly how a record the caller
    // was told did not happen can later be picked up as the chain tip once the
    // predecessor is restored, and no filesystem timestamp granularity makes
    // that observable. Every post-commit divergence is PublishedUncertain,
    // which says "the record exists and its authority is unproven".
    const ReviewedSourceStateObservedRecord committed_observed{
            next_generation, publication_leaf,
            record_identity_from_status(published_descriptor_status),
            publication};
    const auto history_uncertain =
            [&](ReviewedSourceStateStoreFailureKind failure_kind,
                std::optional<std::error_code> system_error)
            -> ReviewedSourceStateStorePublishedUncertain {
        return published_uncertain(
                next_state, committed_observed,
                ReviewedSourceStatePostPublicationIssue::
                        AuthoritativeHistoryUncertain,
                failure_kind, publication_path, publication_path,
                std::move(system_error));
    };

    if(current.has_value()) {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        const std::optional<struct stat> predecessor_status =
                inspect_named_entry(
                        package_directory.get(), current->leaf.leaf,
                        *current_path, inspect_failure,
                        ReviewedSourceStateStoreTestFailurePoint::
                                PostCommitPredecessorStatus);
#else
        const std::optional<struct stat> predecessor_status =
                inspect_named_entry(
                        package_directory.get(), current->leaf.leaf,
                        *current_path, inspect_failure);
#endif
        if(inspect_failure.has_value()) {
            return published_uncertain(
                    next_state,
                    ReviewedSourceStateObservedRecord{
                            next_generation, publication_leaf,
                            record_identity_from_status(
                                    published_descriptor_status),
                            publication},
                    ReviewedSourceStatePostPublicationIssue::
                            PredecessorObservationUncertain,
                    inspect_failure->kind, publication_path, publication_path,
                    inspect_failure->system_error);
        }
        if(!predecessor_status.has_value() ||
           !matches_predecessor_identity(
                   *predecessor_status, expected_observed->identity)) {
            return history_uncertain(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    std::nullopt);
        }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        auto post_commit_contents = reread_descriptor_contents(
                retained_current.get(), *current_path, directory->owner(),
                ReviewedSourceStateStoreTestFailurePoint::
                        PostCommitPredecessorRead);
#else
        auto post_commit_contents = reread_descriptor_contents(
                retained_current.get(), *current_path, directory->owner());
#endif
        if(const auto* failure = std::get_if<ReviewedSourceStateStoreFailure>(
                   &post_commit_contents)) {
            return published_uncertain(
                    next_state, committed_observed,
                    ReviewedSourceStatePostPublicationIssue::
                            PredecessorObservationUncertain,
                    failure->kind, publication_path, publication_path,
                    failure->system_error);
        }
        if(std::get<std::string>(post_commit_contents) !=
           expected_observed->raw_contents) {
            return history_uncertain(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    std::nullopt);
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::PostCommitVerify)) {
        return published_uncertain(
                next_state, committed_observed,
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                publication_path);
    }
#endif

    struct stat named_published {};
    const int named_result = retry_interruptible([&] {
        return ::fstatat(
                package_directory.get(), publication_leaf.c_str(),
                &named_published, AT_SYMLINK_NOFOLLOW);
    });
    if(named_result != 0 ||
       !same_relocated_record_state(
               temporary_status, published_descriptor_status) ||
       !same_record_state(published_descriptor_status, named_published)) {
        return published_uncertain(
                next_state, committed_observed,
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                publication_path);
    }
    if(auto failure = validate_entry_status(
               named_published, publication_path, directory->owner())) {
        return published_uncertain(
                next_state, committed_observed,
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                failure->kind, publication_path);
    }

    // The named successor matches the retained inode, so the proof the caller
    // is about to be handed is "the chain proved before the commit, plus
    // exactly this record". Anything else on disk now - a mutated ancestor, a
    // fork, a higher generation, a new managed entry - means ordinary Published
    // would contradict the very next lookup.
    {
        ChainProof reproof = reprove_authoritative_chain(
                package_directory.get(), package_path, directory->owner(),
                extend_proof_token(
                        proven.token, publication_leaf,
                        record_identity_from_status(
                                published_descriptor_status),
                        publication));
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&reproof)) {
            return history_uncertain(failure->kind, failure->system_error);
        }
        if(std::holds_alternative<ReviewedSourceStateStoreUnsafeHistory>(
                   reproof)) {
            return history_uncertain(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    std::nullopt);
        }
    }

    ReviewedSourceStateObservedRecord published_observed = committed_observed;

    if(auto close_failure =
               close_descriptor_checked(temporary, publication_path)) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                close_failure->kind, publication_path, std::nullopt,
                close_failure->system_error);
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::DirectorySync)) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::DirectorySyncUncertain,
                ReviewedSourceStateStoreFailureKind::SyncFailed, package_path,
                std::nullopt, std::make_error_code(std::errc::io_error));
    }
#endif
    if(auto sync_failure =
               fsync_descriptor(package_directory.get(), package_path)) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::DirectorySyncUncertain,
                sync_failure->kind, package_path, std::nullopt,
                sync_failure->system_error);
    }
    if(auto revalidate = revalidate_named_package_directory(
               store_directory_descriptor, package_directory.get(),
               package_leaf, package_path, directory->owner())) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::
                        PackageDirectoryIdentityUncertain,
                revalidate->kind, package_path, publication_path,
                revalidate->system_error);
    }
    if(auto close_failure =
               close_descriptor_checked(package_directory, package_path)) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::DirectoryCloseFailed,
                close_failure->kind, package_path, std::nullopt,
                close_failure->system_error);
    }
    if(auto close_failure =
               close_descriptor_checked(store_sync, directory->path())) {
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::DirectoryCloseFailed,
                close_failure->kind, directory->path(), std::nullopt,
                close_failure->system_error);
    }
    try {
        directory->require_unchanged_identity();
    } catch(const xdg_directory_safety::PreparationError& error) {
        const ReviewedSourceStateStoreFailure mapped =
                map_preparation_error(error, package_path);
        return published_uncertain(
                next_state, published_observed,
                ReviewedSourceStatePostPublicationIssue::
                        LineageRevalidationFailed,
                mapped.kind, package_path, std::nullopt, mapped.system_error);
    }

    return ReviewedSourceStateStorePublished{
            next_state, std::move(published_observed)};
}
