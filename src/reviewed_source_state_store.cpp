#include "reviewed_source_state_store.hpp"

#include "xdg_directory_safety.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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

std::atomic<std::uint64_t> g_atomic_temp_sequence{0};

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

std::string atomic_temp_leaf() {
    const std::uint64_t sequence =
            g_atomic_temp_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    return std::string(ATOMIC_TEMP_PREFIX) + std::to_string(::getpid()) +
           "-" + std::to_string(sequence);
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
        return GenerationLeaf{1, false, 0, 0, {}, std::string(name)};
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
    const auto dash = identity.find('-');
    if(dash == std::string_view::npos || dash == 0 ||
       dash + 1 >= identity.size()) {
        return std::nullopt;
    }
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    if(!parse_unsigned_decimal(identity.substr(0, dash), true, device) ||
       !parse_unsigned_decimal(identity.substr(dash + 1), true, inode)) {
        return std::nullopt;
    }
    return GenerationLeaf{
            static_cast<std::uint64_t>(generation_value), true, device, inode,
            std::string(digest), std::string(name)};
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

bool same_record_state(
        const struct stat& expected, const struct stat& actual) {
    return same_filesystem_identity(expected, actual) &&
           expected.st_uid == actual.st_uid &&
           (expected.st_mode & 07777) == (actual.st_mode & 07777) &&
           expected.st_size == actual.st_size &&
           expected.st_mtim.tv_sec == actual.st_mtim.tv_sec &&
           expected.st_mtim.tv_nsec == actual.st_mtim.tv_nsec &&
           expected.st_ctim.tv_sec == actual.st_ctim.tv_sec &&
           expected.st_ctim.tv_nsec == actual.st_ctim.tv_nsec;
}

// rename(2) may update ctime. Post-publication compares the stable
// security/content fields and allows only that relocation timestamp to differ.
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
           leaf.predecessor_digest == predecessor_digest;
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
        std::optional<ReviewedSourceStateStoreFailure>& failure) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Status)) {
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

int renameat2_checked(
        int old_directory_descriptor,
        const char* old_leaf,
        int new_directory_descriptor,
        const char* new_leaf,
        unsigned int flags) {
#ifdef SYS_renameat2
    return retry_interruptible([&] {
        return static_cast<int>(::syscall(
                SYS_renameat2, old_directory_descriptor, old_leaf,
                new_directory_descriptor, new_leaf, flags));
    });
#else
    static_cast<void>(old_directory_descriptor);
    static_cast<void>(old_leaf);
    static_cast<void>(new_directory_descriptor);
    static_cast<void>(new_leaf);
    static_cast<void>(flags);
    errno = ENOSYS;
    return -1;
#endif
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
        const struct stat& expected_status) {
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Read)) {
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

    struct stat after_status {};
    if(auto failure = fstat_descriptor(descriptor, after_status, entry_path)) {
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
// orphans in place rather than deleting a replacement we cannot prove.

struct ScannedGeneration {
    GenerationLeaf leaf;
    struct stat    status {};
};

std::variant<std::vector<ScannedGeneration>, ReviewedSourceStateStoreFailure>
scan_generation_files(
        int package_directory_descriptor, const fs::path& package_path) {
    const int scan_descriptor = retry_interruptible(
            [&] { return ::dup(package_directory_descriptor); });
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

    std::vector<ScannedGeneration> generations;
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
        if(is_internal_temp_leaf(name)) continue;
        const std::optional<GenerationLeaf> parsed = parse_generation_leaf(name);
        if(!parsed.has_value()) continue;

        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> status = inspect_named_entry(
                package_directory_descriptor, parsed->leaf, package_path,
                inspect_failure);
        if(inspect_failure.has_value()) return *inspect_failure;
        if(!status.has_value()) continue;
        generations.push_back(ScannedGeneration{*parsed, *status});
    }
    return generations;
}

std::optional<const ScannedGeneration*> find_origin(
        const std::vector<ScannedGeneration>& generations) {
    for(const ScannedGeneration& candidate : generations) {
        if(candidate.leaf.generation == 1 && !candidate.leaf.has_predecessor) {
            return &candidate;
        }
    }
    return std::nullopt;
}

std::optional<const ScannedGeneration*> find_successor(
        const std::vector<ScannedGeneration>& generations,
        const ScannedGeneration& predecessor,
        std::string_view predecessor_digest) {
    const std::uint64_t next = predecessor.leaf.generation + 1;
    for(const ScannedGeneration& candidate : generations) {
        if(candidate.leaf.generation == next &&
           matches_predecessor_binding(
                   candidate.leaf, predecessor.status, predecessor_digest)) {
            return &candidate;
        }
    }
    return std::nullopt;
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
    auto contents = read_all_bytes(descriptor.get(), path, generation.status);
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
reread_descriptor_contents(int descriptor, const fs::path& entry_path) {
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
    return read_all_bytes(descriptor, entry_path, status);
}

std::variant<std::optional<ScannedGeneration>, ReviewedSourceStateStoreFailure>
resolve_chain_tip(
        int package_directory_descriptor,
        const std::vector<ScannedGeneration>& generations,
        const fs::path& package_path,
        std::uintmax_t expected_owner) {
    const auto origin = find_origin(generations);
    if(!origin.has_value()) return std::optional<ScannedGeneration>{};
    if(auto failure = validate_entry_status(
               (*origin)->status, package_path / (*origin)->leaf.leaf,
               expected_owner)) {
        return *failure;
    }
    ScannedGeneration tip = **origin;
    while(true) {
        auto contents = read_generation_contents(
                package_directory_descriptor, tip, package_path,
                expected_owner);
        if(const auto* failure =
                   std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
            return *failure;
        }
        const std::string digest =
                content_digest_hex(std::get<std::string>(contents));
        const auto successor =
                find_successor(generations, tip, digest);
        if(!successor.has_value()) break;
        const fs::path successor_path =
                package_path / (*successor)->leaf.leaf;
        if(auto failure = validate_entry_status(
                   (*successor)->status, successor_path, expected_owner)) {
            return *failure;
        }
        tip = **successor;
    }
    return std::optional<ScannedGeneration>{tip};
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

    if(!created) {
        if(auto failure = validate_package_directory_status(
                   *named_status, package_path, expected_owner)) {
            return *failure;
        }
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
    if(!same_filesystem_identity(*named_status, opened_status) && !created) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                package_path);
    }
    return descriptor;
}

std::variant<OwnedDescriptor, ReviewedSourceStateStoreFailure>
create_temporary_file(
        int package_directory_descriptor,
        const fs::path& package_path,
        std::string& temporary_leaf) {
    for(std::size_t attempt = 0; attempt < 128; ++attempt) {
        temporary_leaf = atomic_temp_leaf();
        const int temporary_descriptor = retry_interruptible([&] {
            return ::openat(
                    package_directory_descriptor, temporary_leaf.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    REVIEWED_SOURCE_STATE_FILE_MODE);
        });
        if(temporary_descriptor >= 0) {
            return OwnedDescriptor(temporary_descriptor);
        }
        if(errno != EEXIST) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::OpenFailed,
                    package_path, current_system_error());
        }
    }
    return store_failure(
            ReviewedSourceStateStoreFailureKind::OpenFailed, package_path,
            std::make_error_code(std::errc::file_exists));
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
    return std::to_string(next_generation) + "." +
           std::to_string(predecessor.device) + "-" +
           std::to_string(predecessor.inode) + "." +
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

    auto scanned = scan_generation_files(package_directory.get(), package_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&scanned)) {
        return *failure;
    }
    auto tip = resolve_chain_tip(
            package_directory.get(),
            std::get<std::vector<ScannedGeneration>>(scanned), package_path,
            directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&tip)) {
        return *failure;
    }
    const std::optional<ScannedGeneration> current =
            std::get<std::optional<ScannedGeneration>>(std::move(tip));
    if(!current.has_value()) {
        return ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt};
    }

    const fs::path tip_path = package_path / current->leaf.leaf;
    auto opened = open_existing_entry(
            package_directory.get(), current->leaf.leaf, current->status,
            tip_path, directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&opened)) {
        return *failure;
    }
    OwnedDescriptor file_descriptor =
            std::get<OwnedDescriptor>(std::move(opened));

    inspect_failure.reset();
    const std::optional<struct stat> revalidated = inspect_named_entry(
            package_directory.get(), current->leaf.leaf, tip_path,
            inspect_failure);
    if(inspect_failure.has_value()) return *inspect_failure;
    if(!revalidated.has_value() ||
       !same_record_state(current->status, *revalidated)) {
        return store_failure(
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                tip_path);
    }

    auto contents = read_all_bytes(
            file_descriptor.get(), tip_path, current->status);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&contents)) {
        return *failure;
    }
    std::string raw = std::get<std::string>(std::move(contents));
    if(auto close_failure =
               close_descriptor_checked(file_descriptor, tip_path)) {
        return *close_failure;
    }
    if(auto close_failure =
               close_descriptor_checked(package_directory, package_path)) {
        return *close_failure;
    }
    if(auto close_failure =
               close_descriptor_checked(lock_descriptor, directory->path())) {
        return *close_failure;
    }

    const ReviewedSourceStateObservation observation =
            interpret_observed_contents(raw, expected_package_base);
    return make_present_read(
            std::move(observation), current->leaf.generation,
            current->leaf.leaf, current->status, std::move(raw));
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

    auto scanned = scan_generation_files(package_directory.get(), package_path);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&scanned)) {
        return *failure;
    }
    const std::vector<ScannedGeneration> generations =
            std::get<std::vector<ScannedGeneration>>(std::move(scanned));
    auto tip = resolve_chain_tip(
            package_directory.get(), generations, package_path,
            directory->owner());
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&tip)) {
        return *failure;
    }
    const std::optional<ScannedGeneration> current =
            std::get<std::optional<ScannedGeneration>>(std::move(tip));

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
                retained_current.get(), current_path, current->status);
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
    std::string temporary_leaf;
    auto created_temporary = create_temporary_file(
            package_directory.get(), package_path, temporary_leaf);
    if(const auto* failure =
               std::get_if<ReviewedSourceStateStoreFailure>(&created_temporary)) {
        return *failure;
    }
    OwnedDescriptor temporary =
            std::get<OwnedDescriptor>(std::move(created_temporary));
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    race_context.temporary_leaf = temporary_leaf;
#endif
    const fs::path temporary_path = package_path / temporary_leaf;

    bool was_published = false;
    auto abandon_temporary = [&]() {
        if(was_published || temporary_leaf.empty()) return;
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
        invoke_race(
                ReviewedSourceStateStoreTestRacePoint::BeforeCleanup,
                race_context);
#endif
    };

    if(retry_interruptible([&] {
           return ::fchmod(temporary.get(), REVIEWED_SOURCE_STATE_FILE_MODE);
       }) != 0) {
        abandon_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::WriteFailed, package_path,
                current_system_error(), std::nullopt, temporary_path);
    }
    if(auto write_failure =
               write_all_bytes(temporary.get(), publication, package_path)) {
        abandon_temporary();
        write_failure->leftover_artifact = temporary_path;
        return *write_failure;
    }
#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Sync)) {
        abandon_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::SyncFailed, package_path,
                std::make_error_code(std::errc::io_error), std::nullopt,
                temporary_path);
    }
#endif
    if(auto sync_failure = fsync_descriptor(temporary.get(), package_path)) {
        abandon_temporary();
        sync_failure->leftover_artifact = temporary_path;
        return *sync_failure;
    }

    struct stat temporary_status {};
    if(auto failure =
               fstat_descriptor(temporary.get(), temporary_status, package_path)) {
        abandon_temporary();
        failure->leftover_artifact = temporary_path;
        return *failure;
    }
    if(auto failure = validate_entry_status(
               temporary_status, package_path, directory->owner())) {
        abandon_temporary();
        failure->leftover_artifact = temporary_path;
        return *failure;
    }

    if(current.has_value()) {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> pre_commit_status = inspect_named_entry(
                package_directory.get(), current->leaf.leaf, *current_path,
                inspect_failure);
        if(inspect_failure.has_value()) {
            abandon_temporary();
            inspect_failure->leftover_artifact = temporary_path;
            return *inspect_failure;
        }
        if(!pre_commit_status.has_value() ||
           !same_filesystem_identity(current->status, *pre_commit_status)) {
            abandon_temporary();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    *current_path, std::nullopt, std::nullopt, temporary_path);
        }
        auto pre_commit_contents = reread_descriptor_contents(
                retained_current.get(), *current_path);
        if(const auto* failure = std::get_if<ReviewedSourceStateStoreFailure>(
                   &pre_commit_contents)) {
            abandon_temporary();
            ReviewedSourceStateStoreFailure reported = *failure;
            reported.leftover_artifact = temporary_path;
            return reported;
        }
        if(std::get<std::string>(pre_commit_contents) !=
           expected_observed->raw_contents) {
            abandon_temporary();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    *current_path, std::nullopt, std::nullopt, temporary_path);
        }
    } else {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> origin_status = inspect_named_entry(
                package_directory.get(), publication_leaf, publication_path,
                inspect_failure);
        if(inspect_failure.has_value()) {
            abandon_temporary();
            inspect_failure->leftover_artifact = temporary_path;
            return *inspect_failure;
        }
        if(origin_status.has_value()) {
            abandon_temporary();
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path, std::nullopt, std::nullopt,
                    temporary_path);
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    // LANDMINE: this window can same-inode rewrite predecessor contents after
    // the last recheck. Authority is the digest-bound successor name, not the
    // exclusive create alone.
    invoke_race(
            ReviewedSourceStateStoreTestRacePoint::AtPublicationBoundary,
            race_context);
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::Rename)) {
        abandon_temporary();
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed,
                publication_path, std::make_error_code(std::errc::io_error),
                std::nullopt, temporary_path);
    }
#endif

    if(renameat2_checked(
               package_directory.get(), temporary_leaf.c_str(),
               package_directory.get(), publication_leaf.c_str(),
               RENAME_NOREPLACE) != 0) {
        const int rename_error = errno;
        abandon_temporary();
        if(rename_error == EEXIST || rename_error == ENOTEMPTY ||
           rename_error == ENOENT || rename_error == ENOTDIR ||
           rename_error == ELOOP) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path,
                    std::error_code(rename_error, std::generic_category()),
                    std::nullopt, temporary_path);
        }
        return store_failure(
                ReviewedSourceStateStoreFailureKind::RenameFailed,
                publication_path,
                std::error_code(rename_error, std::generic_category()),
                std::nullopt, temporary_path);
    }
    was_published = true;

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

    if(current.has_value()) {
        std::optional<ReviewedSourceStateStoreFailure> inspect_failure;
        const std::optional<struct stat> predecessor_status =
                inspect_named_entry(
                        package_directory.get(), current->leaf.leaf,
                        *current_path, inspect_failure);
        if(inspect_failure.has_value() || !predecessor_status.has_value() ||
           !same_filesystem_identity(current->status, *predecessor_status)) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path, std::nullopt, std::nullopt,
                    publication_path);
        }
        auto post_commit_contents = reread_descriptor_contents(
                retained_current.get(), *current_path);
        if(std::holds_alternative<ReviewedSourceStateStoreFailure>(
                   post_commit_contents) ||
           std::get<std::string>(post_commit_contents) !=
                   expected_observed->raw_contents) {
            return store_failure(
                    ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                    publication_path, std::nullopt, std::nullopt,
                    publication_path);
        }
    }

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_STATE_STORE_TEST_HOOKS
    if(consume_injected_failure(
               ReviewedSourceStateStoreTestFailurePoint::PostCommitVerify)) {
        return published_uncertain(
                next_state,
                ReviewedSourceStateObservedRecord{
                        next_generation, publication_leaf,
                        record_identity_from_status(published_descriptor_status),
                        publication},
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
                next_state,
                ReviewedSourceStateObservedRecord{
                        next_generation, publication_leaf,
                        record_identity_from_status(published_descriptor_status),
                        publication},
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                ReviewedSourceStateStoreFailureKind::ConcurrentReplacement,
                publication_path);
    }
    if(auto failure = validate_entry_status(
               named_published, publication_path, directory->owner())) {
        return published_uncertain(
                next_state,
                ReviewedSourceStateObservedRecord{
                        next_generation, publication_leaf,
                        record_identity_from_status(published_descriptor_status),
                        publication},
                ReviewedSourceStatePostPublicationIssue::
                        PublishedIdentityUncertain,
                failure->kind, publication_path);
    }

    ReviewedSourceStateObservedRecord published_observed{
            next_generation, publication_leaf,
            record_identity_from_status(published_descriptor_status),
            publication};

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
