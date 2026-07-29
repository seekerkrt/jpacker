#include "source_preference.hpp"

#include "package_identifier.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <istream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// source-build preferenceのroot、entry path、read/parse、raw enumerationを所有する。
// POLICY: mutation、directory作成、Logger、command handler policyはconsumer側に残す。
namespace {

// POLICY: test overrideもproduction rootもmain前にprocessごとに一度だけcaptureする。
#ifdef MOGUET_ENABLE_TEST_OVERRIDES
const std::string PACKAGE_BUILD_DIR = [] {
    const char* test_package_build_dir = std::getenv("MOGUET_TEST_PACKAGE_BUILD_DIR");
    if(test_package_build_dir && test_package_build_dir[0] != '\0') {
        return std::string(test_package_build_dir);
    }
    return std::string("/etc/jpacker/package.build");
}();
#else
const std::string PACKAGE_BUILD_DIR = "/etc/jpacker/package.build";
#endif

class SourcePreferenceDescriptor final {
    int descriptor_;

public:
    explicit SourcePreferenceDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {
    }

    SourcePreferenceDescriptor(const SourcePreferenceDescriptor&) = delete;
    SourcePreferenceDescriptor& operator=(const SourcePreferenceDescriptor&) = delete;

    ~SourcePreferenceDescriptor() noexcept {
        if(descriptor_ >= 0) static_cast<void>(::close(descriptor_));
    }

    int get() const noexcept {
        return descriptor_;
    }
};

#ifdef MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
struct SourcePreferenceInjectedFailure {
    std::filesystem::path            entry_path;
    SourcePreferenceTestFailurePoint failure_point;
    bool                             partial_read_completed = false;
};

std::optional<SourcePreferenceInjectedFailure> g_source_preference_injected_failure;

bool has_source_preference_failure_for_test(
        const std::filesystem::path& entry_path,
        SourcePreferenceTestFailurePoint failure_point) {
    return g_source_preference_injected_failure.has_value() &&
            g_source_preference_injected_failure->entry_path == entry_path &&
            g_source_preference_injected_failure->failure_point == failure_point;
}

bool consume_source_preference_failure_for_test(
        const std::filesystem::path& entry_path,
        SourcePreferenceTestFailurePoint failure_point) {
    if(!has_source_preference_failure_for_test(entry_path, failure_point)) return false;
    g_source_preference_injected_failure.reset();
    return true;
}

ssize_t read_source_preference_bytes_for_test(
        const std::filesystem::path& entry_path,
        int descriptor,
        char* buffer,
        size_t buffer_size) {
    if(!has_source_preference_failure_for_test(
               entry_path, SourcePreferenceTestFailurePoint::Read)) {
        return ::read(descriptor, buffer, buffer_size);
    }

    if(g_source_preference_injected_failure->partial_read_completed) {
        g_source_preference_injected_failure.reset();
        errno = EIO;
        return -1;
    }

    // One real byteを先に返し、通常read error pathがpartial resultをpublishしないことを試す。
    const ssize_t read_size = ::read(descriptor, buffer, buffer_size == 0 ? 0 : 1);
    if(read_size > 0) {
        g_source_preference_injected_failure->partial_read_completed = true;
        return read_size;
    }

    const int read_errno = errno;
    if(read_size < 0 && read_errno == EINTR) return read_size;
    g_source_preference_injected_failure.reset();
    if(read_size == 0) {
        errno = EIO;
        return -1;
    }
    errno = read_errno;
    return read_size;
}
#endif

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string strip_comment(const std::string& line) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for(size_t i = 0; i < line.length(); ++i) {
        char ch = line[i];
        if(escaped) {
            escaped = false;
            continue;
        }
        if(ch == '\\' && in_double_quote) {
            escaped = true;
            continue;
        }
        if(ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if(ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if(ch == '#' && !in_single_quote && !in_double_quote) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string expand_config_vars(
        std::string value, const std::map<std::string, std::string>& variables) {
    std::regex  brace_pattern(R"(\$\{([A-Za-z0-9_]+)\})");
    std::regex  simple_pattern(R"(\$([A-Za-z0-9_]+))");
    std::smatch match;

    for(int i = 0; i < 32 && std::regex_search(value, match, brace_pattern); ++i) {
        std::string variable_name = match[1];
        std::string replacement =
                variables.count(variable_name) ? variables.at(variable_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == value) break;
        value = next;
    }
    for(int i = 0; i < 32 && std::regex_search(value, match, simple_pattern); ++i) {
        std::string variable_name = match[1];
        std::string replacement =
                variables.count(variable_name) ? variables.at(variable_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == value) break;
        value = next;
    }
    return value;
}

template<typename WarningHandler>
SourceBuildEnvironment parse_source_preference(
        std::istream& file, bool should_emit_warnings,
        WarningHandler&& on_warning) {
    // POLICY: warningはparse中に通知し、legacy callbackのtimingとthrow伝播を維持する。
    std::string                        line;
    SourceBuildEnvironment             environment;
    std::map<std::string, std::string> variables;

    while(std::getline(file, line)) {
        line = strip_comment(line);
        if(trim(line).empty()) continue;

        std::string key, value;
        if(split_env_assignment(line, key, value)) {
            try {
                value = expand_config_vars(value, variables);
            } catch(const std::exception& error) {
                if(should_emit_warnings) {
                    on_warning(
                            "Failed to expand variables for " + key + ": " + error.what());
                }
            }
            variables[key] = value;
            // POLICY(#242): expansion用last-value mapとは別に、valid assignmentを
            // emptyも含めてread順のまま保持する。
            environment.ordered_assignments.push_back({key, value});
        } else if(line.find('=') != std::string::npos && should_emit_warnings) {
            on_warning("Ignoring invalid environment assignment: " + trim(line));
        }
    }
    return environment;
}

std::error_code current_system_error() {
    return std::error_code(errno, std::generic_category());
}

std::string source_preference_system_diagnostic(
        const std::string& operation,
        const std::filesystem::path& entry_path,
        const std::error_code& error) {
    return operation + " source preference entry " + entry_path.string() + ": " +
            error.message();
}

SourcePreferenceFailure source_preference_system_failure(
        SourcePreferenceFailureKind kind,
        const std::filesystem::path& entry_path,
        const std::error_code& error,
        const std::string& operation) {
    return {
            .kind = kind,
            .entry_path = entry_path,
            .system_error = error,
            .observed_file_type = std::nullopt,
            .diagnostic = source_preference_system_diagnostic(
                    operation, entry_path, error),
    };
}

std::filesystem::file_type file_type_from_mode(mode_t mode) {
    if(S_ISREG(mode)) return std::filesystem::file_type::regular;
    if(S_ISDIR(mode)) return std::filesystem::file_type::directory;
    if(S_ISLNK(mode)) return std::filesystem::file_type::symlink;
    if(S_ISBLK(mode)) return std::filesystem::file_type::block;
    if(S_ISCHR(mode)) return std::filesystem::file_type::character;
    if(S_ISFIFO(mode)) return std::filesystem::file_type::fifo;
    if(S_ISSOCK(mode)) return std::filesystem::file_type::socket;
    return std::filesystem::file_type::unknown;
}

SourcePreferenceFailure source_preference_file_type_failure(
        const std::filesystem::path& entry_path,
        std::filesystem::file_type observed_file_type) {
    return {
            .kind = SourcePreferenceFailureKind::UnsupportedFileType,
            .entry_path = entry_path,
            .system_error = std::nullopt,
            .observed_file_type = observed_file_type,
            .diagnostic = "Source preference entry is not a regular file: " +
                    entry_path.string(),
    };
}

} // namespace

std::filesystem::path source_preference_root() {
    return PACKAGE_BUILD_DIR;
}

std::filesystem::path source_preference_entry_path(const std::string& package_name) {
    require_valid_package_name(package_name);
    return std::filesystem::path(PACKAGE_BUILD_DIR) / package_name;
}

std::filesystem::directory_iterator source_preference_entries() {
    return std::filesystem::directory_iterator(PACKAGE_BUILD_DIR);
}

SourcePreferenceDirectorySnapshot snapshot_source_preference_directory() {
    SourcePreferenceDirectorySnapshot snapshot;
    if(!std::filesystem::exists(PACKAGE_BUILD_DIR)) return snapshot;

    snapshot.root_exists = true;
    std::size_t original_index = 0;
    for(const auto& entry : source_preference_entries()) {
        snapshot.entries.push_back(SourcePreferenceEntrySnapshot{
                original_index,
                entry.path(),
                entry.path().filename().string(),
                entry.is_regular_file()});
        ++original_index;
    }
    return snapshot;
}

bool is_force_source(const std::string& package_name) {
    return std::filesystem::exists(source_preference_entry_path(package_name));
}

StrictSourcePreferenceResult read_source_preference_strict(
        const std::string& package_name) {
    const std::filesystem::path entry_path =
            source_preference_entry_path(package_name);

    std::error_code status_error;
    std::filesystem::file_status entry_status;
#ifdef MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
    if(consume_source_preference_failure_for_test(
               entry_path, SourcePreferenceTestFailurePoint::Status)) {
        status_error = std::make_error_code(std::errc::permission_denied);
    } else
#endif
    {
        entry_status = std::filesystem::symlink_status(entry_path, status_error);
    }
    if(status_error) {
        if(status_error == std::errc::no_such_file_or_directory) {
            return SourcePreferenceAbsent{};
        }
        return source_preference_system_failure(
                SourcePreferenceFailureKind::StatusUnavailable,
                entry_path, status_error, "Failed to inspect");
    }
    if(entry_status.type() == std::filesystem::file_type::not_found) {
        return SourcePreferenceAbsent{};
    }
    if(entry_status.type() != std::filesystem::file_type::regular) {
        return source_preference_file_type_failure(
                entry_path, entry_status.type());
    }

    // POLICY(#267): final componentはsymlinkをfollowせず、nonblocking open後にも
    // object typeを再検査する。parent directory pin、in-place rewrite、version tokenは
    // このreaderのthreat model外とする。
    int raw_descriptor = -1;
#ifdef MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
    if(consume_source_preference_failure_for_test(
               entry_path, SourcePreferenceTestFailurePoint::Open)) {
        errno = EACCES;
    } else
#endif
    {
        do {
            raw_descriptor = ::open(
                    entry_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        } while(raw_descriptor < 0 && errno == EINTR);
    }
    if(raw_descriptor < 0) {
        const std::error_code error = current_system_error();
        return source_preference_system_failure(
                SourcePreferenceFailureKind::OpenFailed,
                entry_path, error, "Failed to open");
    }
    SourcePreferenceDescriptor descriptor(raw_descriptor);

    struct stat opened_status {};
    int opened_status_result = -1;
    do {
        opened_status_result = ::fstat(descriptor.get(), &opened_status);
    } while(opened_status_result != 0 && errno == EINTR);
    if(opened_status_result != 0) {
        const std::error_code error = current_system_error();
        return source_preference_system_failure(
                SourcePreferenceFailureKind::StatusUnavailable,
                entry_path, error, "Failed to inspect opened");
    }
    const std::filesystem::file_type opened_file_type =
            file_type_from_mode(opened_status.st_mode);
    if(opened_file_type != std::filesystem::file_type::regular) {
        return source_preference_file_type_failure(
                entry_path, opened_file_type);
    }

    std::string            contents;
    std::array<char, 4096> buffer{};
    while(true) {
        ssize_t read_size = 0;
#ifdef MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
        read_size = read_source_preference_bytes_for_test(
                entry_path, descriptor.get(), buffer.data(), buffer.size());
#else
        read_size = ::read(descriptor.get(), buffer.data(), buffer.size());
#endif
        if(read_size > 0) {
            contents.append(buffer.data(), static_cast<size_t>(read_size));
            continue;
        }
        if(read_size == 0) break;
        if(errno == EINTR) continue;

        const std::error_code error = current_system_error();
        return source_preference_system_failure(
                SourcePreferenceFailureKind::ReadFailed,
                entry_path, error, "Failed to read");
    }

    std::vector<std::string> warnings;
    std::istringstream       input(contents);
    SourceBuildEnvironment environment = parse_source_preference(
            input, true,
            [&warnings](const std::string& warning) {
                warnings.push_back(warning);
            });
    return SourcePreferenceLoaded{
            .entry_path = entry_path,
            .environment = std::move(environment),
            .warnings = std::move(warnings),
    };
}

SourceBuildEnvironment get_package_env(
        const std::string& package_name, SourcePreferenceLoadHandler on_load,
        SourcePreferenceWarningHandler on_warning) {
    std::filesystem::path entry_path = source_preference_entry_path(package_name);
    if(!std::filesystem::exists(entry_path)) return {};

    std::ifstream file(entry_path);
    if(on_load) on_load(entry_path);
    return parse_source_preference(
            file, on_warning != nullptr,
            [on_warning](const std::string& warning) {
                if(on_warning) on_warning(warning);
            });
}

void read_source_preference_entry(
        const std::filesystem::path& entry_path, SourcePreferenceLineHandler on_line) {
    std::ifstream file(entry_path);
    std::string   line;
    while(std::getline(file, line)) {
        std::string display_line = trim(strip_comment(line));
        if(!display_line.empty() && on_line) on_line(display_line);
    }
}

#ifdef MOGUET_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
void fail_next_source_preference_operation_for_test(
        const std::string& package_name,
        SourcePreferenceTestFailurePoint failure_point) {
    g_source_preference_injected_failure = SourcePreferenceInjectedFailure{
            .entry_path = source_preference_entry_path(package_name),
            .failure_point = failure_point,
    };
}

void reset_source_preference_test_hooks() {
    g_source_preference_injected_failure.reset();
}
#endif
