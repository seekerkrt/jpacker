#define _POSIX_C_SOURCE 200809L

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t MAX_MANIFEST_BYTES = 1024U * 1024U;
constexpr std::size_t MAX_MANIFEST_ENTRIES = 4096U;

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~UniqueFd() { reset(); }

    [[nodiscard]] int get() const { return fd_; }

private:
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

struct ParsedPath {
    std::string text;
    std::vector<std::string> components;
};

struct Options {
    std::string manifest;
    std::vector<std::string> allowed_roots;
};

struct Candidate {
    ParsedPath manifest_path;
    std::string display_path;
    std::string leaf;
    bool exists = false;
    UniqueFd parent;
    struct stat identity {};
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string system_error_message(const std::string& action, int error_number) {
    return action + ": " + std::strerror(error_number);
}

ParsedPath parse_absolute_path(
    const std::string& path,
    const std::string& label,
    bool allow_root
) {
    if (path.empty() || path.front() != '/') {
        fail(label + " must be absolute: " + path);
    }
    if (path.size() > 1U && path.back() == '/') {
        fail(label + " must not have a trailing slash: " + path);
    }
    if (path.find('\0') != std::string::npos) {
        fail(label + " contains a NUL byte");
    }

    ParsedPath parsed{path, {}};
    std::size_t component_start = 1U;
    while (component_start < path.size()) {
        const std::size_t separator = path.find('/', component_start);
        const std::size_t component_end =
            separator == std::string::npos ? path.size() : separator;
        const std::string component = path.substr(
            component_start,
            component_end - component_start
        );
        if (component.empty() || component == "." || component == "..") {
            fail(label + " is not a canonical absolute path: " + path);
        }
        parsed.components.push_back(component);
        if (separator == std::string::npos) {
            break;
        }
        component_start = separator + 1U;
    }

    if (!allow_root && parsed.components.empty()) {
        fail(label + " must not be the filesystem root");
    }
    return parsed;
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--manifest") {
            if (++index >= argc || !options.manifest.empty()) {
                fail("--manifest requires exactly one value");
            }
            options.manifest = argv[index];
        } else if (argument == "--allowed-root") {
            if (++index >= argc) {
                fail("--allowed-root requires a value");
            }
            options.allowed_roots.emplace_back(argv[index]);
        } else {
            fail("unexpected argument: " + argument);
        }
    }

    if (options.manifest.empty()) {
        fail("--manifest is required");
    }
    if (options.allowed_roots.empty()) {
        fail("at least one --allowed-root is required");
    }
    return options;
}

std::string read_manifest(const std::string& manifest_path) {
    (void)parse_absolute_path(manifest_path, "install manifest path", false);

    const int manifest_fd = ::open(
        manifest_path.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW
    );
    if (manifest_fd < 0) {
        fail(system_error_message(
            "unable to open install manifest " + manifest_path,
            errno
        ));
    }
    UniqueFd manifest(manifest_fd);

    struct stat manifest_status {};
    if (::fstat(manifest.get(), &manifest_status) != 0) {
        fail(system_error_message(
            "unable to inspect install manifest " + manifest_path,
            errno
        ));
    }
    if (!S_ISREG(manifest_status.st_mode)) {
        fail("install manifest must be a regular non-symlink file: " + manifest_path);
    }
    if (manifest_status.st_size <= 0) {
        fail("install manifest is empty: " + manifest_path);
    }
    if (
        static_cast<unsigned long long>(manifest_status.st_size)
        > static_cast<unsigned long long>(MAX_MANIFEST_BYTES)
    ) {
        fail("install manifest exceeds the supported size limit");
    }

    std::string contents;
    contents.reserve(static_cast<std::size_t>(manifest_status.st_size));
    char buffer[4096];
    for (;;) {
        const ssize_t read_size = ::read(manifest.get(), buffer, sizeof(buffer));
        if (read_size < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail(system_error_message(
                "unable to read install manifest " + manifest_path,
                errno
            ));
        }
        if (read_size == 0) {
            break;
        }
        contents.append(buffer, static_cast<std::size_t>(read_size));
        if (contents.size() > MAX_MANIFEST_BYTES) {
            fail("install manifest exceeds the supported size limit");
        }
    }
    if (contents.empty()) {
        fail("install manifest is empty: " + manifest_path);
    }
    return contents;
}

std::vector<ParsedPath> parse_manifest_entries(const std::string& contents) {
    std::vector<ParsedPath> entries;
    std::unordered_set<std::string> unique_entries;
    std::size_t line_start = 0U;
    while (line_start < contents.size()) {
        const std::size_t newline = contents.find('\n', line_start);
        const std::size_t line_end =
            newline == std::string::npos ? contents.size() : newline;
        const std::string line = contents.substr(line_start, line_end - line_start);
        if (line.empty() || line.find('\r') != std::string::npos) {
            fail("install manifest contains an empty or noncanonical line");
        }
        ParsedPath entry = parse_absolute_path(line, "install manifest entry", false);
        if (!unique_entries.insert(entry.text).second) {
            fail("install manifest contains a duplicate entry: " + entry.text);
        }
        entries.push_back(std::move(entry));
        if (entries.size() > MAX_MANIFEST_ENTRIES) {
            fail("install manifest exceeds the supported entry limit");
        }
        if (newline == std::string::npos) {
            break;
        }
        line_start = newline + 1U;
    }
    if (entries.empty()) {
        fail("install manifest contains no payload entries");
    }
    return entries;
}

bool is_strictly_beneath(
    const ParsedPath& path,
    const ParsedPath& allowed_root
) {
    if (path.components.size() <= allowed_root.components.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < allowed_root.components.size(); ++index) {
        if (path.components[index] != allowed_root.components[index]) {
            return false;
        }
    }
    return true;
}

std::vector<ParsedPath> parse_allowed_roots(
    const std::vector<std::string>& raw_roots
) {
    std::vector<ParsedPath> roots;
    std::unordered_set<std::string> unique_roots;
    for (const std::string& raw_root : raw_roots) {
        ParsedPath root = parse_absolute_path(raw_root, "allowed install root", false);
        if (unique_roots.insert(root.text).second) {
            roots.push_back(std::move(root));
        }
    }
    return roots;
}

void validate_allowed_entry(
    const ParsedPath& entry,
    const std::vector<ParsedPath>& allowed_roots
) {
    for (const ParsedPath& allowed_root : allowed_roots) {
        if (is_strictly_beneath(entry, allowed_root)) {
            return;
        }
    }
    fail("install manifest entry is outside the configured install roots: " + entry.text);
}

UniqueFd open_trusted_root(const std::string& destdir) {
    const int filesystem_root_fd = ::open(
        "/",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC
    );
    if (filesystem_root_fd < 0) {
        fail(system_error_message("unable to open filesystem root", errno));
    }
    UniqueFd current(filesystem_root_fd);
    if (destdir.empty() || destdir == "/") {
        return current;
    }

    const ParsedPath parsed_destdir = parse_absolute_path(destdir, "DESTDIR", true);
    for (const std::string& component : parsed_destdir.components) {
        const int child_fd = ::openat(
            current.get(),
            component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        );
        if (child_fd < 0) {
            fail(system_error_message(
                "unsafe or unavailable DESTDIR component " + component,
                errno
            ));
        }
        current = UniqueFd(child_fd);
    }
    return current;
}

std::optional<UniqueFd> open_candidate_parent(
    const UniqueFd& trusted_root,
    const ParsedPath& entry
) {
    const int duplicate_root_fd = ::openat(
        trusted_root.get(),
        ".",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC
    );
    if (duplicate_root_fd < 0) {
        fail(system_error_message("unable to duplicate trusted root", errno));
    }
    UniqueFd current(duplicate_root_fd);

    for (std::size_t index = 0U; index + 1U < entry.components.size(); ++index) {
        const std::string& component = entry.components[index];
        const int child_fd = ::openat(
            current.get(),
            component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        );
        if (child_fd < 0) {
            if (errno == ENOENT) {
                return std::nullopt;
            }
            fail(system_error_message(
                "unsafe or unavailable ancestor for " + entry.text,
                errno
            ));
        }
        current = UniqueFd(child_fd);
    }
    return current;
}

std::string display_path(const std::string& destdir, const std::string& entry) {
    if (destdir.empty() || destdir == "/") {
        return entry;
    }
    return destdir + entry;
}

Candidate inspect_candidate(
    const UniqueFd& trusted_root,
    ParsedPath entry,
    const std::string& destdir
) {
    Candidate candidate;
    candidate.display_path = display_path(destdir, entry.text);
    candidate.leaf = entry.components.back();
    candidate.manifest_path = std::move(entry);

    std::optional<UniqueFd> parent = open_candidate_parent(
        trusted_root,
        candidate.manifest_path
    );
    if (!parent) {
        return candidate;
    }

    struct stat status {};
    if (
        ::fstatat(
            parent->get(),
            candidate.leaf.c_str(),
            &status,
            AT_SYMLINK_NOFOLLOW
        ) != 0
    ) {
        if (errno == ENOENT) {
            return candidate;
        }
        fail(system_error_message(
            "unable to inspect uninstall candidate " + candidate.display_path,
            errno
        ));
    }
    if (S_ISDIR(status.st_mode)) {
        fail("refusing to uninstall a directory: " + candidate.display_path);
    }
    if (!S_ISREG(status.st_mode) && !S_ISLNK(status.st_mode)) {
        fail("refusing to uninstall a non-file payload: " + candidate.display_path);
    }

    candidate.exists = true;
    candidate.parent = std::move(*parent);
    candidate.identity = status;
    return candidate;
}

bool same_identity(const struct stat& left, const struct stat& right) {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino
        && (left.st_mode & S_IFMT) == (right.st_mode & S_IFMT);
}

void verify_candidate_identity(const Candidate& candidate) {
    if (!candidate.exists) {
        return;
    }
    struct stat current_status {};
    if (
        ::fstatat(
            candidate.parent.get(),
            candidate.leaf.c_str(),
            &current_status,
            AT_SYMLINK_NOFOLLOW
        ) != 0
    ) {
        fail(system_error_message(
            "uninstall candidate changed after preflight " + candidate.display_path,
            errno
        ));
    }
    if (!same_identity(candidate.identity, current_status)) {
        fail("uninstall candidate identity changed after preflight: "
            + candidate.display_path);
    }
}

void remove_candidates(std::vector<Candidate>& candidates) {
    // Validate the complete manifest and every retained filesystem identity
    // before the first unlink. Parent directory descriptors stay open so a
    // later pathname replacement cannot redirect deletion through a symlink.
    for (const Candidate& candidate : candidates) {
        verify_candidate_identity(candidate);
    }

    for (Candidate& candidate : candidates) {
        if (!candidate.exists) {
            std::cout << "Already absent: " << candidate.display_path << '\n';
            continue;
        }
        std::cout << "Removing " << candidate.display_path << '\n';
        if (
            ::unlinkat(
                candidate.parent.get(),
                candidate.leaf.c_str(),
                0
            ) != 0
        ) {
            fail(system_error_message(
                "failed to remove " + candidate.display_path,
                errno
            ));
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        const std::vector<ParsedPath> allowed_roots = parse_allowed_roots(
            options.allowed_roots
        );
        std::vector<ParsedPath> manifest_entries = parse_manifest_entries(
            read_manifest(options.manifest)
        );
        for (const ParsedPath& entry : manifest_entries) {
            validate_allowed_entry(entry, allowed_roots);
        }

        const char* raw_destdir = std::getenv("DESTDIR");
        const std::string destdir = raw_destdir == nullptr ? "" : raw_destdir;
        UniqueFd trusted_root = open_trusted_root(destdir);

        std::vector<Candidate> candidates;
        candidates.reserve(manifest_entries.size());
        for (ParsedPath& entry : manifest_entries) {
            candidates.push_back(inspect_candidate(
                trusted_root,
                std::move(entry),
                destdir
            ));
        }
        remove_candidates(candidates);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "moguet-uninstall: " << error.what() << '\n';
        return 1;
    }
}
