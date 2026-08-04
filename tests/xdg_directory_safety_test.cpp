#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace safety = xdg_directory_safety;

constexpr mode_t PRIVATE_DIRECTORY_MODE = 0700;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_path(
        const fs::path& actual, const fs::path& expected,
        const std::string& context) {
    if(actual != expected) {
        throw std::runtime_error(
                context + ": expected [" + expected.string() +
                "], actual [" + actual.string() + "]");
    }
}

struct PathMetadata {
    std::uintmax_t device;
    std::uintmax_t inode;
    std::uintmax_t owner;
    mode_t         permissions;
};

PathMetadata path_metadata(const fs::path& path) {
    struct stat status {};
    if(lstat(path.c_str(), &status) != 0) {
        throw std::runtime_error(
                "Failed to inspect test path: " + path.string());
    }
    return PathMetadata{
            static_cast<std::uintmax_t>(status.st_dev),
            static_cast<std::uintmax_t>(status.st_ino),
            static_cast<std::uintmax_t>(status.st_uid),
            static_cast<mode_t>(status.st_mode & 07777)};
}

void set_mode(const fs::path& path, mode_t mode) {
    if(chmod(path.c_str(), mode) != 0) {
        throw std::runtime_error(
                "Failed to set test directory mode: " + path.string());
    }
}

void create_test_directory(
        const fs::path& path, mode_t mode = PRIVATE_DIRECTORY_MODE) {
    if(!fs::create_directory(path)) {
        throw std::runtime_error(
                "Failed to create test directory: " + path.string());
    }
    set_mode(path, mode);
}

void create_file(const fs::path& path, const std::string& contents = "sentinel") {
    std::ofstream file(path, std::ios::binary);
    if(!file) {
        throw std::runtime_error(
                "Failed to create test file: " + path.string());
    }
    file << contents;
}

void expect_metadata_unchanged(
        const PathMetadata& before, const PathMetadata& after,
        const std::string& context) {
    expect(before.device == after.device, context + ": device changed.");
    expect(before.inode == after.inode, context + ": inode changed.");
    expect(before.owner == after.owner, context + ": owner changed.");
    expect(
            before.permissions == after.permissions,
            context + ": permissions changed.");
}

void expect_private_directory(const fs::path& path, const std::string& context) {
    const PathMetadata metadata = path_metadata(path);
    expect(fs::is_directory(path), context + ": path is not a directory.");
    expect(
            metadata.owner == static_cast<std::uintmax_t>(geteuid()),
            context + ": owner is not the process effective UID.");
    expect(
            metadata.permissions == PRIVATE_DIRECTORY_MODE,
            context + ": new directory mode is not 0700.");
    expect(
            (metadata.permissions & 0022) == 0,
            context + ": new directory is group/other writable.");
}

class TemporaryDirectory final {
    fs::path path_;

public:
    TemporaryDirectory() {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "moguet-xdg-directory-safety-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create XDG directory safety test root.");
        }
        path_ = created_path;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() noexcept {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }
};

class ScopedUmask final {
    mode_t previous_;

public:
    explicit ScopedUmask(mode_t mask) : previous_(umask(mask)) {
    }

    ScopedUmask(const ScopedUmask&) = delete;
    ScopedUmask& operator=(const ScopedUmask&) = delete;

    ~ScopedUmask() noexcept {
        static_cast<void>(umask(previous_));
    }
};

class ScopedPathMode final {
    fs::path path_;
    mode_t   previous_;

public:
    ScopedPathMode(const fs::path& path, mode_t mode)
        : path_(path), previous_(path_metadata(path).permissions) {
        set_mode(path_, mode);
    }

    ScopedPathMode(const ScopedPathMode&) = delete;
    ScopedPathMode& operator=(const ScopedPathMode&) = delete;

    ~ScopedPathMode() noexcept {
        static_cast<void>(chmod(path_.c_str(), previous_));
    }
};

class ScopedEnvironmentVariable final {
    std::string                name_;
    std::optional<std::string> previous_value_;

public:
    ScopedEnvironmentVariable(
            std::string name, const std::optional<std::string>& value)
        : name_(std::move(name)) {
        const char* previous = std::getenv(name_.c_str());
        if(previous != nullptr) previous_value_ = previous;

        const int result = value.has_value()
                                   ? setenv(name_.c_str(), value->c_str(), 1)
                                   : unsetenv(name_.c_str());
        if(result != 0) {
            throw std::runtime_error(
                    "Failed to set test environment variable: " + name_);
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(
            const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_value_.has_value()) {
            static_cast<void>(setenv(
                    name_.c_str(), previous_value_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv(name_.c_str()));
        }
    }
};

class ScopedCurrentDirectory final {
    fs::path previous_;

public:
    explicit ScopedCurrentDirectory(const fs::path& directory)
        : previous_(fs::current_path()) {
        fs::current_path(directory);
    }

    ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
    ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

    ~ScopedCurrentDirectory() noexcept {
        std::error_code error;
        fs::current_path(previous_, error);
    }
};

xdg_paths::ResolvedPaths resolve_explicit(const fs::path& root) {
    return xdg_paths::resolve(xdg_paths::EnvironmentSnapshot{
            .xdg_config_home = (root / "config").string(),
            .xdg_state_home = (root / "state").string(),
            .xdg_cache_home = (root / "cache").string(),
            .home = std::nullopt,
    });
}

void create_explicit_anchors(const fs::path& root) {
    create_test_directory(root / "config");
    create_test_directory(root / "state");
    create_test_directory(root / "cache");
}

xdg_paths::ResolvedPaths resolve_home_fallback(const fs::path& home) {
    return xdg_paths::resolve(xdg_paths::EnvironmentSnapshot{
            .xdg_config_home = std::nullopt,
            .xdg_state_home = std::nullopt,
            .xdg_cache_home = std::nullopt,
            .home = home.string(),
    });
}

xdg_paths::SourcePreferencePaths resolve_source_preference_explicit(
        const fs::path& config_home) {
    return xdg_paths::resolve_source_preference(
            xdg_paths::EnvironmentSnapshot{
                    .xdg_config_home = config_home.string(),
                    .xdg_state_home = std::nullopt,
                    .xdg_cache_home = std::nullopt,
                    .home = std::nullopt,
            });
}

xdg_paths::SourcePreferencePaths resolve_source_preference_fallback(
        const fs::path& home) {
    return xdg_paths::resolve_source_preference(
            xdg_paths::EnvironmentSnapshot{
                    .xdg_config_home = std::nullopt,
                    .xdg_state_home = std::nullopt,
                    .xdg_cache_home = std::nullopt,
                    .home = home.string(),
            });
}

template <typename Callable>
safety::PreparationFailure expect_preparation_error(
        Callable callable, xdg_paths::DirectoryKind expected_kind,
        safety::PreparationErrorCode expected_code,
        std::optional<safety::PreparationStage> expected_stage = std::nullopt,
        std::optional<std::size_t> expected_component_index = std::nullopt,
        const std::string& forbidden_diagnostic_fragment = "") {
    try {
        static_cast<void>(callable());
    } catch(const safety::PreparationError& error) {
        const safety::PreparationFailure failure = error.failure();
        expect(
                failure.directory_kind == expected_kind,
                "Unexpected directory kind in preparation failure.");
        expect(
                failure.code == expected_code,
                "Unexpected directory preparation error code.");
        if(expected_stage.has_value()) {
            expect(
                    failure.stage == expected_stage.value(),
                    "Unexpected directory preparation stage.");
        }
        if(expected_component_index.has_value()) {
            expect(
                    failure.component_index == expected_component_index,
                    "Unexpected directory preparation component index.");
        }
        if(!forbidden_diagnostic_fragment.empty()) {
            expect(
                    std::string(error.what()).find(
                            forbidden_diagnostic_fragment) ==
                            std::string::npos,
                    "Directory diagnostic exposed an authority path.");
        }
        return failure;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                "Unexpected directory preparation exception category: " +
                std::string(error.what()));
    }
    throw std::runtime_error("Expected directory preparation failure.");
}

safety::DirectorySafetyTestOverrides injected_failure(
        safety::DirectorySafetyTestFailurePoint failure_point,
        std::size_t component_index, int error_number) {
    safety::DirectorySafetyTestOverrides overrides;
    overrides.injected_failure = safety::DirectorySafetyInjectedFailure{
            failure_point, component_index, error_number};
    return overrides;
}

void test_explicit_preparation_is_lazy_private_and_kind_safe() {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());

    expect(
            !fs::exists(paths.config.directory),
            "Resolver or startup created the explicit config directory.");
    expect(
            !fs::exists(paths.state.directory),
            "Resolver or startup created the explicit state directory.");
    expect(
            !fs::exists(paths.cache.directory),
            "Resolver or startup created the explicit cache directory.");

    ScopedUmask permissive_umask(0000);
    safety::PreparedDirectory config =
            safety::prepare_directory(paths.config);
    expect(
            config.directory_kind() == xdg_paths::DirectoryKind::Config,
            "Config preparation returned the wrong directory kind.");
    expect_path(config.path(), paths.config.directory, "Prepared config path");
    expect(config.created_component_count() == 1, "Config creation count mismatch.");
    expect_private_directory(paths.config.directory, "Explicit config directory");
    expect(
            config.owner() == static_cast<std::uintmax_t>(geteuid()),
            "Prepared config owner does not match effective UID.");
    expect(
            config.permissions() == PRIVATE_DIRECTORY_MODE,
            "Prepared config permissions are not 0700.");
    expect(
            !fs::exists(paths.config.config_file),
            "Directory preparation created the config file.");
    expect(
            !fs::exists(paths.state.directory) &&
                    !fs::exists(paths.cache.directory),
            "Config preparation created a different directory kind.");

    safety::PreparedDirectory state = safety::prepare_directory(paths.state);
    safety::PreparedDirectory cache = safety::prepare_directory(paths.cache);
    expect(
            state.directory_kind() == xdg_paths::DirectoryKind::State,
            "State preparation returned the wrong directory kind.");
    expect(
            cache.directory_kind() == xdg_paths::DirectoryKind::Cache,
            "Cache preparation returned the wrong directory kind.");
    expect_path(state.path(), paths.state.directory, "Prepared state path");
    expect_path(cache.path(), paths.cache.directory, "Prepared cache path");
    expect_private_directory(paths.state.directory, "Explicit state directory");
    expect_private_directory(paths.cache.directory, "Explicit cache directory");
    expect(
            !fs::exists(paths.state.default_log_file),
            "Directory preparation created the default log file.");
    config.require_unchanged_identity();
    state.require_unchanged_identity();
    cache.require_unchanged_identity();
}

void test_home_fallback_creates_only_resolver_owned_components() {
    TemporaryDirectory temporary_directory;
    const fs::path home = temporary_directory.path() / "home";
    create_test_directory(home);
    const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);

    expect(!fs::exists(home / ".config"), "Resolver created .config.");
    expect(!fs::exists(home / ".local"), "Resolver created .local.");
    expect(!fs::exists(home / ".cache"), "Resolver created .cache.");

    safety::PreparedDirectory config =
            safety::prepare_directory(paths.config);
    safety::PreparedDirectory state = safety::prepare_directory(paths.state);
    safety::PreparedDirectory cache = safety::prepare_directory(paths.cache);

    expect(config.created_component_count() == 2, "HOME config creation count mismatch.");
    expect(state.created_component_count() == 3, "HOME state creation count mismatch.");
    expect(cache.created_component_count() == 2, "HOME cache creation count mismatch.");
    expect_private_directory(home / ".config", "HOME .config");
    expect_private_directory(paths.config.directory, "HOME config application directory");
    expect_private_directory(home / ".local", "HOME .local");
    expect_private_directory(home / ".local" / "state", "HOME state base");
    expect_private_directory(paths.state.directory, "HOME state application directory");
    expect_private_directory(home / ".cache", "HOME .cache");
    expect_private_directory(paths.cache.directory, "HOME cache application directory");
}

void test_source_preference_explicit_preparation_and_existing_open() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);

    expect(
            !fs::exists(paths.directory.parent_path()),
            "Source preference resolver created the application directory.");
    ScopedUmask permissive_umask(0000);
    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths);
    expect(
            prepared.directory_kind() == xdg_paths::DirectoryKind::Config,
            "Source preference preparation returned the wrong directory kind.");
    expect_path(
            prepared.path(), paths.directory,
            "Prepared source preference directory");
    expect(
            prepared.created_component_count() == 2,
            "Explicit source preference creation count mismatch.");
    expect_private_directory(
            paths.directory.parent_path(),
            "Explicit source preference application directory");
    expect_private_directory(
            paths.directory,
            "Explicit source preference directory");

    std::optional<safety::PreparedDirectory> opened =
            safety::open_existing_directory(paths);
    expect(
            opened.has_value(),
            "Existing source preference directory was reported absent.");
    expect(
            opened->created_component_count() == 0,
            "Read-only source preference open reported created components.");
    opened->require_unchanged_identity();
    prepared.require_unchanged_identity();
}

void test_source_preference_home_fallback_is_lazy() {
    TemporaryDirectory temporary_directory;
    const fs::path home = temporary_directory.path() / "home";
    create_test_directory(home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_fallback(home);

    std::optional<safety::PreparedDirectory> missing =
            safety::open_existing_directory(paths);
    expect(
            !missing.has_value(),
            "Missing source preference directory was reported present.");
    expect(
            !fs::exists(home / ".config"),
            "Read-only source preference open created HOME config state.");

    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths);
    expect(
            prepared.created_component_count() == 3,
            "HOME source preference creation count mismatch.");
    expect_private_directory(home / ".config", "Source preference HOME .config");
    expect_private_directory(
            paths.directory.parent_path(),
            "Source preference HOME application directory");
    expect_private_directory(
            paths.directory,
            "Source preference HOME directory");
}

void test_source_preference_read_only_open_does_not_complete_partial_tree() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);

    std::optional<safety::PreparedDirectory> missing_application =
            safety::open_existing_directory(paths);
    expect(
            !missing_application.has_value(),
            "Missing source preference application directory was reported present.");
    expect(
            !fs::exists(paths.directory.parent_path()),
            "Read-only open created a missing application directory.");

    create_test_directory(paths.directory.parent_path(), 0755);
    const PathMetadata application_before =
            path_metadata(paths.directory.parent_path());
    std::optional<safety::PreparedDirectory> missing_store =
            safety::open_existing_directory(paths);
    expect(
            !missing_store.has_value(),
            "Missing source preference store was reported present.");
    expect(
            !fs::exists(paths.directory),
            "Read-only open created a missing source preference store.");
    expect_metadata_unchanged(
            application_before,
            path_metadata(paths.directory.parent_path()),
            "Read-only source preference application directory");
}

void test_source_preference_missing_component_is_reinspected_before_absent() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);
    bool missing_component_reappeared = false;

    safety::DirectorySafetyTestOverrides overrides;
    overrides.event_hook =
            [&missing_component_reappeared](
                    safety::DirectorySafetyTestEvent event,
                    xdg_paths::DirectoryKind directory_kind,
                    std::size_t component_index, const fs::path& path) {
                if(missing_component_reappeared ||
                   event != safety::DirectorySafetyTestEvent::BeforeAbsentLineageRevalidation ||
                   directory_kind != xdg_paths::DirectoryKind::Config ||
                   component_index != 0) {
                    return;
                }
                create_test_directory(path);
                create_test_directory(path / "source-build.d");
                missing_component_reappeared = true;
            };

    std::optional<safety::PreparedDirectory> opened =
            safety::open_existing_directory_for_test(paths, overrides);
    expect(
            missing_component_reappeared,
            "Missing-component reappearance hook did not run.");
    expect(
            opened.has_value(),
            "A source preference store that reappeared before the absence "
            "proof was reported absent.");
    expect(
            opened->created_component_count() == 0,
            "Read-only reinspection reported created components.");
    opened->require_unchanged_identity();
}

void test_source_preference_absence_revalidation_detects_lineage_replacement() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);
    create_test_directory(paths.directory.parent_path());
    const fs::path displaced_application =
            config_home / "displaced-moguet";
    bool lineage_replaced = false;

    safety::DirectorySafetyTestOverrides overrides;
    overrides.event_hook =
            [&lineage_replaced, &displaced_application](
                    safety::DirectorySafetyTestEvent event,
                    xdg_paths::DirectoryKind directory_kind,
                    std::size_t component_index, const fs::path& path) {
                if(lineage_replaced ||
                   event != safety::DirectorySafetyTestEvent::BeforeAbsentLineageRevalidation ||
                   directory_kind != xdg_paths::DirectoryKind::Config ||
                   component_index != 1) {
                    return;
                }
                fs::rename(path.parent_path(), displaced_application);
                create_test_directory(path.parent_path());
                create_test_directory(path);
                lineage_replaced = true;
            };

    expect_preparation_error(
            [&paths, &overrides]() {
                return safety::open_existing_directory_for_test(
                        paths, overrides);
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::ConcurrentReplacement,
            safety::PreparationStage::DirectoryRevalidation);
    expect(
            lineage_replaced,
            "Absence-lineage replacement hook did not run.");
    expect(
            fs::is_directory(displaced_application),
            "Absence revalidation modified the displaced application directory.");
    expect(
            fs::is_directory(paths.directory),
            "Absence revalidation modified the replacement source preference store.");
}

void test_source_preference_requires_private_final_directory() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home, 0755);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);
    create_test_directory(paths.directory.parent_path(), 0755);
    create_test_directory(paths.directory, 0755);

    expect_preparation_error(
            [&paths]() {
                return safety::open_existing_directory(paths);
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::UnsafePermissions,
            safety::PreparationStage::ComponentValidation, 1);
    expect(
            path_metadata(paths.directory.parent_path()).permissions == 0755,
            "Safe existing source preference ancestor was modified.");

    set_mode(paths.directory, PRIVATE_DIRECTORY_MODE);
    std::optional<safety::PreparedDirectory> opened =
            safety::open_existing_directory(paths);
    expect(
            opened.has_value(),
            "Private source preference directory was not accepted.");
    expect(
            opened->permissions() == PRIVATE_DIRECTORY_MODE,
            "Source preference capability did not retain mode 0700.");
}

void test_source_preference_final_type_is_rejected() {
    {
        TemporaryDirectory temporary_directory;
        const fs::path config_home = temporary_directory.path() / "config";
        create_test_directory(config_home);
        const xdg_paths::SourcePreferencePaths paths =
                resolve_source_preference_explicit(config_home);
        create_test_directory(paths.directory.parent_path());
        const fs::path outside = temporary_directory.path() / "outside";
        create_test_directory(outside);
        fs::create_symlink(outside, paths.directory);

        expect_preparation_error(
                [&paths]() {
                    return safety::open_existing_directory(paths);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::ComponentValidation, 1);
    }
    {
        TemporaryDirectory temporary_directory;
        const fs::path config_home = temporary_directory.path() / "config";
        create_test_directory(config_home);
        const xdg_paths::SourcePreferencePaths paths =
                resolve_source_preference_explicit(config_home);
        create_test_directory(paths.directory.parent_path());
        create_file(paths.directory);

        expect_preparation_error(
                [&paths]() {
                    return safety::open_existing_directory(paths);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::NotDirectory,
                safety::PreparationStage::ComponentValidation, 1);
    }
}

void test_source_preference_missing_explicit_anchor_is_hard_error() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "missing-config";
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);

    expect_preparation_error(
            [&paths]() {
                return safety::open_existing_directory(paths);
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::MissingAnchor,
            safety::PreparationStage::AnchorTraversal);
    expect(
            !fs::exists(config_home),
            "Read-only source preference open created an explicit anchor.");
}

void test_source_preference_retained_mode_and_identity_are_revalidated() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);
    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths);

    set_mode(paths.directory, 0755);
    expect_preparation_error(
            [&prepared]() {
                prepared.require_unchanged_identity();
                return 0;
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::UnsafePermissions,
            safety::PreparationStage::DirectoryRevalidation);
}

void test_source_preference_nested_replacement_is_detected() {
    TemporaryDirectory temporary_directory;
    const fs::path config_home = temporary_directory.path() / "config";
    create_test_directory(config_home);
    const xdg_paths::SourcePreferencePaths paths =
            resolve_source_preference_explicit(config_home);
    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths);
    static_cast<void>(prepared);
    const fs::path displaced =
            paths.directory.parent_path() / "displaced-source-build.d";
    bool replacement_performed = false;

    safety::DirectorySafetyTestOverrides overrides;
    overrides.event_hook =
            [&replacement_performed, &displaced](
                    safety::DirectorySafetyTestEvent event,
                    xdg_paths::DirectoryKind directory_kind,
                    std::size_t component_index, const fs::path& path) {
                if(replacement_performed ||
                   event != safety::DirectorySafetyTestEvent::AfterManagedMetadata ||
                   directory_kind != xdg_paths::DirectoryKind::Config ||
                   component_index != 1) {
                    return;
                }
                fs::rename(path, displaced);
                create_test_directory(path);
                replacement_performed = true;
            };

    expect_preparation_error(
            [&paths, &overrides]() {
                return safety::open_existing_directory_for_test(
                        paths, overrides);
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::ConcurrentReplacement,
            safety::PreparationStage::ComponentValidation, 1);
    expect(
            replacement_performed,
            "Nested source preference replacement hook did not run.");
}

void test_execute_only_non_managed_ancestor_is_traversable() {
    TemporaryDirectory temporary_directory;
    const fs::path traversal_parent =
            temporary_directory.path() / "execute-only-parent";
    const fs::path explicit_anchor = traversal_parent / "config";
    create_test_directory(traversal_parent);
    create_test_directory(explicit_anchor);
    const xdg_paths::ResolvedPaths paths = xdg_paths::resolve(
            xdg_paths::EnvironmentSnapshot{
                    .xdg_config_home = explicit_anchor.string(),
                    .xdg_state_home = explicit_anchor.string(),
                    .xdg_cache_home = explicit_anchor.string(),
                    .home = std::nullopt,
            });

    ScopedPathMode execute_only(traversal_parent, 0100);
    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths.config);
    expect_path(
            prepared.path(), explicit_anchor / "moguet",
            "Execute-only ancestor preparation path");
    expect_private_directory(
            paths.config.directory,
            "Directory below execute-only non-managed ancestor");
    prepared.require_unchanged_identity();
}

void test_existing_safe_directory_is_unchanged_and_idempotent() {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());
    create_test_directory(paths.config.directory, 0755);
    create_file(paths.config.directory / "keep", "unchanged");
    const PathMetadata before = path_metadata(paths.config.directory);

    safety::PreparedDirectory first = safety::prepare_directory(paths.config);
    const PathMetadata after_first = path_metadata(paths.config.directory);
    expect(first.created_component_count() == 0, "Existing directory was reported as created.");
    expect(first.permissions() == 0755, "Existing 0755 directory was not accepted.");
    expect_metadata_unchanged(before, after_first, "First existing-directory preparation");

    safety::PreparedDirectory second = safety::prepare_directory(paths.config);
    const PathMetadata after_second = path_metadata(paths.config.directory);
    expect(second.created_component_count() == 0, "Repeated preparation created a component.");
    expect_metadata_unchanged(before, after_second, "Repeated preparation");
    expect(fs::is_regular_file(paths.config.directory / "keep"), "Existing sentinel was replaced.");
    first.require_unchanged_identity();
    second.require_unchanged_identity();
}

void expect_unsafe_mode_rejected_without_repair(mode_t unsafe_mode) {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());
    create_test_directory(paths.config.directory, unsafe_mode);
    const PathMetadata before = path_metadata(paths.config.directory);

    expect_preparation_error(
            [&paths]() { return safety::prepare_directory(paths.config); },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::UnsafePermissions,
            safety::PreparationStage::ComponentValidation, 0);
    const PathMetadata after = path_metadata(paths.config.directory);
    expect_metadata_unchanged(before, after, "Unsafe existing directory");
    expect(
            after.permissions == unsafe_mode,
            "Unsafe existing directory permissions were repaired.");
}

void test_unsafe_permissions_are_rejected_without_chmod() {
    expect_unsafe_mode_rejected_without_repair(0775);
    expect_unsafe_mode_rejected_without_repair(01775);
}

void test_final_symlink_forms_are_rejected() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        const fs::path outside = temporary_directory.path() / "outside";
        create_test_directory(outside);
        fs::create_symlink(outside, paths.config.directory);
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::ComponentValidation, 0);
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        fs::create_symlink(
                temporary_directory.path() / "missing-target",
                paths.config.directory);
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::ComponentValidation, 0);
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        const fs::path outside = temporary_directory.path() / "outside";
        const fs::path second_link =
                paths.config.creation_boundary.existing_anchor / "second-link";
        create_test_directory(outside);
        fs::create_symlink(outside, second_link);
        fs::create_symlink("second-link", paths.config.directory);
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::ComponentValidation, 0);
    }
}

void test_intermediate_and_anchor_symlinks_are_rejected_without_traversal() {
    {
        TemporaryDirectory temporary_directory;
        const fs::path home = temporary_directory.path() / "home";
        const fs::path outside = temporary_directory.path() / "outside";
        create_test_directory(home);
        create_test_directory(outside);
        const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);
        fs::create_symlink(outside, home / ".local");

        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.state); },
                xdg_paths::DirectoryKind::State,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::ComponentValidation, 0);
        expect(
                !fs::exists(outside / "state"),
                "Preparation followed an intermediate symlink.");
    }
    {
        TemporaryDirectory temporary_directory;
        const fs::path real_parent = temporary_directory.path() / "real";
        const fs::path linked_parent = temporary_directory.path() / "linked";
        create_test_directory(real_parent);
        create_test_directory(real_parent / "config");
        fs::create_symlink(real_parent, linked_parent);
        const xdg_paths::ResolvedPaths paths = xdg_paths::resolve(
                xdg_paths::EnvironmentSnapshot{
                        .xdg_config_home =
                                (linked_parent / "config").string(),
                        .xdg_state_home = (real_parent / "config").string(),
                        .xdg_cache_home = (real_parent / "config").string(),
                        .home = std::nullopt,
                });

        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::Symlink,
                safety::PreparationStage::AnchorTraversal);
        expect(
                !fs::exists(real_parent / "config" / "moguet"),
                "Preparation traversed a symlink inside the anchor path.");
    }
}

void test_non_directory_components_are_rejected() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        create_file(paths.config.directory);
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::NotDirectory,
                safety::PreparationStage::ComponentValidation, 0);
        expect(fs::is_regular_file(paths.config.directory), "Final file was replaced.");
    }
    {
        TemporaryDirectory temporary_directory;
        const fs::path home = temporary_directory.path() / "home";
        create_test_directory(home);
        const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);
        create_file(home / ".local");
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.state); },
                xdg_paths::DirectoryKind::State,
                safety::PreparationErrorCode::NotDirectory,
                safety::PreparationStage::ComponentValidation, 0);
        expect(fs::is_regular_file(home / ".local"), "Intermediate file was replaced.");
        expect(
                !fs::exists(paths.state.directory),
                "Preparation created below an intermediate non-directory.");
    }
}

void test_ownership_and_effective_uid_policy() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::DirectorySafetyTestOverrides overrides;
        const std::uintmax_t effective_user =
                static_cast<std::uintmax_t>(geteuid());
        overrides.observed_owner = effective_user == 0 ? 1 : 0;
        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::OwnershipMismatch,
                safety::PreparationStage::AnchorValidation);
        expect(
                !fs::exists(paths.config.directory),
                "Ownership failure created the application directory.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        ScopedEnvironmentVariable sudo_user(
                "SUDO_USER", std::optional<std::string>{"untrusted-caller"});
        safety::PreparedDirectory config =
                safety::prepare_directory(paths.config);
        expect(
                config.owner() == static_cast<std::uintmax_t>(geteuid()),
                "SUDO_USER changed the directory owner authority.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        create_test_directory(paths.config.directory);
        ScopedEnvironmentVariable sudo_user(
                "SUDO_USER", std::optional<std::string>{"ordinary-user"});

        safety::DirectorySafetyTestOverrides mismatch;
        mismatch.effective_user = 0;
        mismatch.observed_owner = 1;
        expect_preparation_error(
                [&paths, &mismatch]() {
                    return safety::prepare_directory_for_test(
                            paths.config, mismatch);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::OwnershipMismatch);

        safety::DirectorySafetyTestOverrides root_identity;
        root_identity.effective_user = 0;
        root_identity.observed_owner = 0;
        safety::PreparedDirectory prepared =
                safety::prepare_directory_for_test(
                        paths.config, root_identity);
        expect(prepared.owner() == 0, "UID 0 was not treated as its own authority.");
        expect(
                prepared.created_component_count() == 0,
                "Root-equivalent validation recreated an existing directory.");
    }
}

void test_validation_failure_stops_before_unsafe_descendants() {
    TemporaryDirectory temporary_directory;
    const fs::path home = temporary_directory.path() / "home";
    create_test_directory(home);
    const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);
    create_test_directory(home / ".local", 0775);
    const PathMetadata before = path_metadata(home / ".local");

    expect_preparation_error(
            [&paths]() { return safety::prepare_directory(paths.state); },
            xdg_paths::DirectoryKind::State,
            safety::PreparationErrorCode::UnsafePermissions,
            safety::PreparationStage::ComponentValidation, 0);
    expect_metadata_unchanged(
            before, path_metadata(home / ".local"),
            "Unsafe intermediate directory");
    expect(
            !fs::exists(home / ".local" / "state"),
            "Preparation continued below an unsafe component.");
}

void test_missing_anchor_and_invalid_boundary_fail_before_mutation() {
    {
        TemporaryDirectory temporary_directory;
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        expect_preparation_error(
                [&paths]() { return safety::prepare_directory(paths.config); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::MissingAnchor,
                safety::PreparationStage::AnchorTraversal);
        expect(
                !fs::exists(paths.config.creation_boundary.existing_anchor),
                "Missing explicit anchor was created.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        xdg_paths::ConfigPaths invalid = paths.config;
        invalid.creation_boundary.existing_anchor = "/";
        const std::string authority_path = paths.config.directory.string();
        expect_preparation_error(
                [&invalid]() { return safety::prepare_directory(invalid); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::InvalidCreationBoundary,
                safety::PreparationStage::BoundaryValidation, std::nullopt,
                authority_path);
        expect(
                !fs::exists(paths.config.directory),
                "Invalid creation boundary mutated the filesystem.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        xdg_paths::ConfigPaths invalid = paths.config;
        invalid.config_file = paths.cache.directory / "config.toml";
        expect_preparation_error(
                [&invalid]() { return safety::prepare_directory(invalid); },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::InvalidCreationBoundary,
                safety::PreparationStage::BoundaryValidation);
        expect(
                !fs::exists(paths.config.directory),
                "Mismatched typed derived path mutated the filesystem.");
    }
}

void test_injected_syscall_failures_are_classified() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::DirectorySafetyTestOverrides overrides = injected_failure(
                safety::DirectorySafetyTestFailurePoint::ManagedMetadata,
                0, EIO);
        const safety::PreparationFailure failure = expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::MetadataFailure,
                safety::PreparationStage::ComponentInspection, 0);
        expect(failure.system_error.has_value(), "Metadata failure lost errno.");
        expect(!fs::exists(paths.config.directory), "Metadata failure created a directory.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::DirectorySafetyTestOverrides overrides = injected_failure(
                safety::DirectorySafetyTestFailurePoint::ManagedMetadata,
                0, EACCES);
        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::PermissionDenied,
                safety::PreparationStage::ComponentInspection, 0);
        expect(!fs::exists(paths.config.directory), "Permission failure created a directory.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::DirectorySafetyTestOverrides overrides = injected_failure(
                safety::DirectorySafetyTestFailurePoint::ComponentCreation,
                0, EIO);
        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::CreationFailed,
                safety::PreparationStage::ComponentCreation, 0);
        expect(!fs::exists(paths.config.directory), "Creation failure created a directory.");
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::DirectorySafetyTestOverrides overrides = injected_failure(
                safety::DirectorySafetyTestFailurePoint::DescriptorMetadata,
                0, EIO);
        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::MetadataFailure,
                safety::PreparationStage::ComponentValidation, 0);
        expect_private_directory(
                paths.config.directory,
                "Safely created directory retained after descriptor failure");
    }
}

void test_concurrent_replacement_is_detected() {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());
    create_test_directory(paths.config.directory);
    const fs::path displaced =
            paths.config.creation_boundary.existing_anchor / "displaced-moguet";
    bool replacement_performed = false;

    safety::DirectorySafetyTestOverrides overrides;
    overrides.event_hook =
            [&replacement_performed, &displaced](
                    safety::DirectorySafetyTestEvent event,
                    xdg_paths::DirectoryKind directory_kind,
                    std::size_t component_index, const fs::path& path) {
                if(replacement_performed ||
                   event != safety::DirectorySafetyTestEvent::AfterManagedMetadata ||
                   directory_kind != xdg_paths::DirectoryKind::Config ||
                   component_index != 0) {
                    return;
                }
                fs::rename(path, displaced);
                create_test_directory(path);
                replacement_performed = true;
            };

    expect_preparation_error(
            [&paths, &overrides]() {
                return safety::prepare_directory_for_test(
                        paths.config, overrides);
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::ConcurrentReplacement,
            safety::PreparationStage::ComponentValidation, 0);
    expect(replacement_performed, "Concurrent replacement hook did not run.");
    expect(fs::is_directory(displaced), "Original directory was not retained by the fixture.");
}

void test_security_ancestor_mode_changes_are_revalidated() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        const fs::path anchor =
                paths.config.creation_boundary.existing_anchor;
        bool anchor_changed = false;
        safety::DirectorySafetyTestOverrides overrides;
        overrides.event_hook =
                [&anchor, &anchor_changed](
                        safety::DirectorySafetyTestEvent event,
                        xdg_paths::DirectoryKind directory_kind,
                        std::size_t component_index, const fs::path&) {
                    if(anchor_changed ||
                       event != safety::DirectorySafetyTestEvent::AfterManagedMetadata ||
                       directory_kind != xdg_paths::DirectoryKind::Config ||
                       component_index != 0) {
                        return;
                    }
                    set_mode(anchor, 0777);
                    anchor_changed = true;
                };

        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.config, overrides);
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::UnsafePermissions,
                safety::PreparationStage::DirectoryRevalidation);
        expect(anchor_changed, "Anchor mode-change hook did not run.");
    }
    {
        TemporaryDirectory temporary_directory;
        const fs::path home = temporary_directory.path() / "home";
        create_test_directory(home);
        const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);
        bool intermediate_changed = false;
        safety::DirectorySafetyTestOverrides overrides;
        overrides.event_hook =
                [&home, &intermediate_changed](
                        safety::DirectorySafetyTestEvent event,
                        xdg_paths::DirectoryKind directory_kind,
                        std::size_t component_index, const fs::path&) {
                    if(intermediate_changed ||
                       event != safety::DirectorySafetyTestEvent::AfterManagedMetadata ||
                       directory_kind != xdg_paths::DirectoryKind::State ||
                       component_index != 1) {
                        return;
                    }
                    set_mode(home / ".local", 0777);
                    intermediate_changed = true;
                };

        expect_preparation_error(
                [&paths, &overrides]() {
                    return safety::prepare_directory_for_test(
                            paths.state, overrides);
                },
                xdg_paths::DirectoryKind::State,
                safety::PreparationErrorCode::UnsafePermissions,
                safety::PreparationStage::DirectoryRevalidation);
        expect(
                intermediate_changed,
                "Fallback intermediate mode-change hook did not run.");
    }
}

void test_safe_partial_creation_is_retained() {
    TemporaryDirectory temporary_directory;
    const fs::path home = temporary_directory.path() / "home";
    create_test_directory(home);
    const xdg_paths::ResolvedPaths paths = resolve_home_fallback(home);
    safety::DirectorySafetyTestOverrides overrides = injected_failure(
            safety::DirectorySafetyTestFailurePoint::ComponentCreation,
            2, EIO);

    expect_preparation_error(
            [&paths, &overrides]() {
                return safety::prepare_directory_for_test(
                        paths.state, overrides);
            },
            xdg_paths::DirectoryKind::State,
            safety::PreparationErrorCode::CreationFailed,
            safety::PreparationStage::ComponentCreation, 2);
    expect_private_directory(home / ".local", "Partial .local directory");
    expect_private_directory(home / ".local" / "state", "Partial state directory");
    expect(
            !fs::exists(paths.state.directory),
            "Failed final component unexpectedly exists.");
}

void test_creation_precondition_rejects_before_mutation() {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());

    const auto require_rejected_without_creation =
            [](const auto& directory_paths,
               const std::string& context) {
                const PathMetadata expected_parent = path_metadata(
                        directory_paths.creation_boundary.existing_anchor);
                bool callback_ran = false;
                bool sentinel_observed = false;
                try {
                    static_cast<void>(safety::prepare_directory(
                            directory_paths,
                            [&](const safety::DirectoryIdentity& identity) {
                                callback_ran = true;
                                expect(
                                        identity.device ==
                                                        expected_parent.device &&
                                                identity.inode ==
                                                        expected_parent.inode,
                                        context +
                                                ": callback did not receive the retained parent identity.");
                                throw std::runtime_error(
                                        "creation-precondition-rejected");
                            }));
                } catch(const std::runtime_error& error) {
                    sentinel_observed =
                            std::string(error.what()) ==
                            "creation-precondition-rejected";
                }
                expect(callback_ran, context + ": callback did not run.");
                expect(
                        sentinel_observed,
                        context + ": callback exception was not preserved.");
                expect(
                        !fs::exists(directory_paths.directory),
                        context +
                                ": managed directory was created before rejection.");
            };

    require_rejected_without_creation(
            paths.state, "State creation precondition");
    require_rejected_without_creation(
            paths.cache, "Cache creation precondition");
}

void test_trailing_separators_preserve_creation_authority() {
    TemporaryDirectory temporary_directory;
    const fs::path explicit_root = temporary_directory.path() / "explicit";
    const fs::path home = temporary_directory.path() / "home";
    create_test_directory(explicit_root);
    create_test_directory(home);
    create_explicit_anchors(explicit_root);

    const xdg_paths::ResolvedPaths explicit_paths = xdg_paths::resolve(
            xdg_paths::EnvironmentSnapshot{
                    .xdg_config_home =
                            (explicit_root / "config").string() + "///",
                    .xdg_state_home =
                            (explicit_root / "state").string() + "///",
                    .xdg_cache_home =
                            (explicit_root / "cache").string() + "///",
                    .home = std::nullopt,
            });
    expect_path(
            explicit_paths.config.creation_boundary.base_directory,
            explicit_root / "config",
            "Trailing-separator explicit config base");
    expect_path(
            explicit_paths.config.creation_boundary.existing_anchor,
            explicit_root / "config",
            "Trailing-separator explicit config anchor");
    expect(
            explicit_paths.config.creation_boundary.creatable_components ==
                    std::vector<std::string>{"moguet"},
            "Trailing-separator explicit boundary retained an empty component.");
    safety::PreparedDirectory explicit_config =
            safety::prepare_directory(explicit_paths.config);
    safety::PreparedDirectory explicit_state =
            safety::prepare_directory(explicit_paths.state);
    safety::PreparedDirectory explicit_cache =
            safety::prepare_directory(explicit_paths.cache);
    expect(
            explicit_config.created_component_count() == 1 &&
                    explicit_state.created_component_count() == 1 &&
                    explicit_cache.created_component_count() == 1,
            "Trailing-separator explicit paths did not prepare cleanly.");

    const xdg_paths::ResolvedPaths fallback_paths = xdg_paths::resolve(
            xdg_paths::EnvironmentSnapshot{
                    .xdg_config_home = std::nullopt,
                    .xdg_state_home = std::nullopt,
                    .xdg_cache_home = std::nullopt,
                    .home = home.string() + "///",
            });
    expect_path(
            fallback_paths.state.creation_boundary.existing_anchor, home,
            "Trailing-separator HOME anchor");
    expect_path(
            fallback_paths.state.creation_boundary.base_directory,
            home / ".local" / "state",
            "Trailing-separator HOME state base");
    expect(
            fallback_paths.state.creation_boundary.creatable_components ==
                    std::vector<std::string>{".local", "state", "moguet"},
            "Trailing-separator HOME boundary retained an empty component.");
    safety::PreparedDirectory fallback_config =
            safety::prepare_directory(fallback_paths.config);
    safety::PreparedDirectory fallback_state =
            safety::prepare_directory(fallback_paths.state);
    safety::PreparedDirectory fallback_cache =
            safety::prepare_directory(fallback_paths.cache);
    expect(
            fallback_config.created_component_count() == 2 &&
                    fallback_state.created_component_count() == 3 &&
                    fallback_cache.created_component_count() == 2,
            "Trailing-separator HOME paths did not prepare cleanly.");
}

void test_preparation_does_not_reread_environment_or_cwd() {
    TemporaryDirectory temporary_directory;
    const fs::path resolved_root = temporary_directory.path() / "resolved";
    const fs::path environment_root = temporary_directory.path() / "environment";
    const fs::path working_directory = temporary_directory.path() / "cwd";
    create_test_directory(resolved_root);
    create_test_directory(environment_root);
    create_test_directory(working_directory);
    create_explicit_anchors(resolved_root);
    create_explicit_anchors(environment_root);
    const xdg_paths::ResolvedPaths paths = resolve_explicit(resolved_root);

    ScopedEnvironmentVariable config_home(
            "XDG_CONFIG_HOME",
            std::optional<std::string>{
                    (environment_root / "config").string()});
    ScopedEnvironmentVariable state_home(
            "XDG_STATE_HOME",
            std::optional<std::string>{
                    (environment_root / "state").string()});
    ScopedEnvironmentVariable cache_home(
            "XDG_CACHE_HOME",
            std::optional<std::string>{
                    (environment_root / "cache").string()});
    ScopedEnvironmentVariable home(
            "HOME", std::optional<std::string>{environment_root.string()});
    ScopedCurrentDirectory changed_cwd(working_directory);

    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths.config);
    expect_path(prepared.path(), paths.config.directory, "Snapshot-owned preparation path");
    expect(fs::is_directory(paths.config.directory), "Resolved directory was not created.");
    expect(
            !fs::exists(environment_root / "config" / "moguet"),
            "Preparation reread XDG_CONFIG_HOME.");
    expect(
            !fs::exists(working_directory / "moguet"),
            "Preparation resolved a path from current working directory.");
}

void test_retained_descriptor_detects_later_replacement() {
    TemporaryDirectory temporary_directory;
    create_explicit_anchors(temporary_directory.path());
    const xdg_paths::ResolvedPaths paths =
            resolve_explicit(temporary_directory.path());
    safety::PreparedDirectory prepared =
            safety::prepare_directory(paths.config);
    const fs::path displaced =
            paths.config.creation_boundary.existing_anchor / "original-moguet";
    fs::rename(paths.config.directory, displaced);
    create_test_directory(paths.config.directory);

    expect_preparation_error(
            [&prepared]() {
                prepared.require_unchanged_identity();
                return 0;
            },
            xdg_paths::DirectoryKind::Config,
            safety::PreparationErrorCode::ConcurrentReplacement,
            safety::PreparationStage::DirectoryRevalidation);
}

void test_retained_lineage_detects_anchor_and_ancestor_replacement() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::PreparedDirectory prepared =
                safety::prepare_directory(paths.cache);
        const fs::path moved_anchor =
                temporary_directory.path() / "moved-cache-anchor";
        fs::rename(paths.cache.creation_boundary.existing_anchor, moved_anchor);
        create_test_directory(
                paths.cache.creation_boundary.existing_anchor);
        create_test_directory(paths.cache.directory);

        expect_preparation_error(
                [&prepared]() {
                    prepared.require_unchanged_identity();
                    return 0;
                },
                xdg_paths::DirectoryKind::Cache,
                safety::PreparationErrorCode::ConcurrentReplacement,
                safety::PreparationStage::DirectoryRevalidation,
                std::nullopt,
                paths.cache.creation_boundary.existing_anchor.string());
        expect(
                fs::is_directory(moved_anchor / "moguet"),
                "Revalidation changed the original cache root after its "
                "anchor moved.");
    }

    {
        TemporaryDirectory temporary_directory;
        const fs::path authority_parent =
                temporary_directory.path() / "authority-parent";
        create_test_directory(authority_parent);
        create_explicit_anchors(authority_parent);
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(authority_parent);
        safety::PreparedDirectory prepared =
                safety::prepare_directory(paths.cache);
        const fs::path moved_parent =
                temporary_directory.path() / "moved-authority-parent";
        fs::rename(authority_parent, moved_parent);
        create_test_directory(authority_parent);
        create_explicit_anchors(authority_parent);
        create_test_directory(paths.cache.directory);

        expect_preparation_error(
                [&prepared]() {
                    prepared.require_unchanged_identity();
                    return 0;
                },
                xdg_paths::DirectoryKind::Cache,
                safety::PreparationErrorCode::ConcurrentReplacement,
                safety::PreparationStage::DirectoryRevalidation,
                std::nullopt, authority_parent.string());
        expect(
                fs::is_directory(moved_parent / "cache" / "moguet"),
                "Revalidation followed an application root whose ancestor "
                "moved outside its authoritative lineage.");
    }
}

void test_retained_directory_rejects_later_security_changes() {
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::PreparedDirectory prepared =
                safety::prepare_directory(paths.config);
        set_mode(paths.config.directory, 0777);

        expect_preparation_error(
                [&prepared]() {
                    prepared.require_unchanged_identity();
                    return 0;
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::UnsafePermissions,
                safety::PreparationStage::DirectoryRevalidation);
    }
    {
        TemporaryDirectory temporary_directory;
        create_explicit_anchors(temporary_directory.path());
        const xdg_paths::ResolvedPaths paths =
                resolve_explicit(temporary_directory.path());
        safety::PreparedDirectory prepared =
                safety::prepare_directory(paths.config);
        set_mode(paths.config.creation_boundary.existing_anchor, 0777);

        expect_preparation_error(
                [&prepared]() {
                    prepared.require_unchanged_identity();
                    return 0;
                },
                xdg_paths::DirectoryKind::Config,
                safety::PreparationErrorCode::UnsafePermissions,
                safety::PreparationStage::DirectoryRevalidation);
    }
}

template <typename Callable>
void run_case(const std::string& name, Callable callable) {
    callable();
    std::cout << "  ok: " << name << '\n';
}

} // namespace

int main() {
    try {
        run_case(
                "explicit lazy private kind-safe preparation",
                test_explicit_preparation_is_lazy_private_and_kind_safe);
        run_case(
                "HOME fallback creation boundary",
                test_home_fallback_creates_only_resolver_owned_components);
        run_case(
                "source preference explicit preparation and open",
                test_source_preference_explicit_preparation_and_existing_open);
        run_case(
                "source preference HOME fallback is lazy",
                test_source_preference_home_fallback_is_lazy);
        run_case(
                "source preference read-only open stays non-creating",
                test_source_preference_read_only_open_does_not_complete_partial_tree);
        run_case(
                "source preference missing component is reinspected",
                test_source_preference_missing_component_is_reinspected_before_absent);
        run_case(
                "source preference absence lineage replacement detected",
                test_source_preference_absence_revalidation_detects_lineage_replacement);
        run_case(
                "source preference final directory is private",
                test_source_preference_requires_private_final_directory);
        run_case(
                "source preference final type rejected",
                test_source_preference_final_type_is_rejected);
        run_case(
                "source preference explicit anchor required",
                test_source_preference_missing_explicit_anchor_is_hard_error);
        run_case(
                "source preference retained mode revalidated",
                test_source_preference_retained_mode_and_identity_are_revalidated);
        run_case(
                "source preference nested replacement detected",
                test_source_preference_nested_replacement_is_detected);
        run_case(
                "execute-only non-managed ancestor traversal",
                test_execute_only_non_managed_ancestor_is_traversable);
        run_case(
                "existing safe directory unchanged and idempotent",
                test_existing_safe_directory_is_unchanged_and_idempotent);
        run_case(
                "unsafe permissions rejected without chmod",
                test_unsafe_permissions_are_rejected_without_chmod);
        run_case("final symlink forms rejected", test_final_symlink_forms_are_rejected);
        run_case(
                "intermediate and anchor symlinks rejected",
                test_intermediate_and_anchor_symlinks_are_rejected_without_traversal);
        run_case(
                "non-directory components rejected",
                test_non_directory_components_are_rejected);
        run_case(
                "effective UID ownership policy",
                test_ownership_and_effective_uid_policy);
        run_case(
                "unsafe descendants not traversed",
                test_validation_failure_stops_before_unsafe_descendants);
        run_case(
                "missing and invalid boundaries fail before mutation",
                test_missing_anchor_and_invalid_boundary_fail_before_mutation);
        run_case(
                "injected syscall failures classified",
                test_injected_syscall_failures_are_classified);
        run_case(
                "concurrent replacement detected",
                test_concurrent_replacement_is_detected);
        run_case(
                "security ancestor mode changes revalidated",
                test_security_ancestor_mode_changes_are_revalidated);
        run_case(
                "safe partial creation retained",
                test_safe_partial_creation_is_retained);
        run_case(
                "creation precondition rejects before mutation",
                test_creation_precondition_rejects_before_mutation);
        run_case(
                "trailing separators preserve creation authority",
                test_trailing_separators_preserve_creation_authority);
        run_case(
                "environment and cwd are not reread",
                test_preparation_does_not_reread_environment_or_cwd);
        run_case(
                "retained descriptor detects replacement",
                test_retained_descriptor_detects_later_replacement);
        run_case(
                "retained lineage detects anchor and ancestor replacement",
                test_retained_lineage_detects_anchor_and_ancestor_replacement);
        run_case(
                "retained directory rejects security changes",
                test_retained_directory_rejects_later_security_changes);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "XDG directory safety tests: all checks passed\n";
    return 0;
}
