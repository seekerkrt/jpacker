#include "xdg_paths.hpp"

#include "application_identity.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace xdg_paths {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view CONFIG_FILE_NAME = "config.toml";
constexpr std::string_view DEFAULT_LOG_FILE_SUFFIX = ".log";

struct ResolvedBaseDirectory {
    fs::path                 directory;
    DirectorySource         source = DirectorySource::ExplicitXdg;
    fs::path                 existing_anchor;
    std::vector<std::string> creatable_components;
};

std::string_view directory_kind_name(DirectoryKind directory_kind) {
    switch(directory_kind) {
    case DirectoryKind::Config:
        return "config";
    case DirectoryKind::State:
        return "state";
    case DirectoryKind::Cache:
        return "cache";
    }
    throw std::logic_error("Unknown XDG directory kind.");
}

std::string_view environment_variable_name(
        EnvironmentVariable environment_variable) {
    switch(environment_variable) {
    case EnvironmentVariable::XdgConfigHome:
        return "XDG_CONFIG_HOME";
    case EnvironmentVariable::XdgStateHome:
        return "XDG_STATE_HOME";
    case EnvironmentVariable::XdgCacheHome:
        return "XDG_CACHE_HOME";
    case EnvironmentVariable::Home:
        return "HOME";
    }
    throw std::logic_error("Unknown XDG environment variable.");
}

EnvironmentVariable xdg_environment_variable(DirectoryKind directory_kind) {
    switch(directory_kind) {
    case DirectoryKind::Config:
        return EnvironmentVariable::XdgConfigHome;
    case DirectoryKind::State:
        return EnvironmentVariable::XdgStateHome;
    case DirectoryKind::Cache:
        return EnvironmentVariable::XdgCacheHome;
    }
    throw std::logic_error("Unknown XDG directory kind.");
}

std::string resolution_diagnostic(const ResolutionFailure& failure) {
    const std::string prefix =
            "Cannot resolve " +
            std::string(application_identity::PROJECT_NAME) + " " +
            std::string(directory_kind_name(failure.directory_kind)) +
            " directory: ";
    const std::string variable =
            std::string(environment_variable_name(failure.environment_variable));

    switch(failure.code) {
    case ResolutionErrorCode::MissingHome:
        return prefix +
                std::string(environment_variable_name(
                        xdg_environment_variable(failure.directory_kind))) +
                " is unset or empty, and HOME is not set.";
    case ResolutionErrorCode::EmptyHome:
        return prefix +
                std::string(environment_variable_name(
                        xdg_environment_variable(failure.directory_kind))) +
                " is unset or empty, and HOME is empty.";
    case ResolutionErrorCode::RelativePath:
        return prefix + variable + " must be an absolute path.";
    case ResolutionErrorCode::DotComponent:
        return prefix + variable +
                " contains a '.' or '..' path component.";
    case ResolutionErrorCode::AmbiguousLeadingDoubleSlash:
        return prefix + variable +
                " begins with the implementation-defined '//' form.";
    case ResolutionErrorCode::EmbeddedNull:
        return prefix + variable + " contains an embedded NUL byte.";
    case ResolutionErrorCode::MalformedPath:
        return prefix + variable + " is not a valid native path.";
    }
    throw std::logic_error("Unknown XDG path resolution error code.");
}

[[noreturn]] void throw_resolution_error(
        DirectoryKind directory_kind,
        EnvironmentVariable environment_variable,
        ResolutionErrorCode code) {
    throw ResolutionError(
            ResolutionFailure{directory_kind, environment_variable, code});
}

bool has_ambiguous_leading_double_slash(std::string_view value) {
    return value.size() >= 2 && value[0] == '/' && value[1] == '/' &&
           (value.size() == 2 || value[2] != '/');
}

fs::path require_absolute_base_path(
        const std::string& value,
        DirectoryKind directory_kind,
        EnvironmentVariable environment_variable) {
    if(value.find('\0') != std::string::npos) {
        throw_resolution_error(
                directory_kind, environment_variable,
                ResolutionErrorCode::EmbeddedNull);
    }
    if(has_ambiguous_leading_double_slash(value)) {
        throw_resolution_error(
                directory_kind, environment_variable,
                ResolutionErrorCode::AmbiguousLeadingDoubleSlash);
    }

    try {
        fs::path path(value);
        if(!path.is_absolute()) {
            throw_resolution_error(
                    directory_kind, environment_variable,
                    ResolutionErrorCode::RelativePath);
        }

        // LANDMINE(#175): lexical normalization before this check could erase
        // a symlink/.. boundary that a later filesystem validator must observe.
        for(const auto& component : path.relative_path()) {
            if(component == "." || component == "..") {
                throw_resolution_error(
                        directory_kind, environment_variable,
                        ResolutionErrorCode::DotComponent);
            }
        }
        fs::path normalized = path.lexically_normal();
        if(normalized != normalized.root_path() &&
           normalized.filename().empty()) {
            normalized = normalized.parent_path();
        }
        return normalized;
    } catch(const fs::filesystem_error&) {
        throw_resolution_error(
                directory_kind, environment_variable,
                ResolutionErrorCode::MalformedPath);
    }
}

std::vector<std::string> path_components(const fs::path& path) {
    std::vector<std::string> components;
    for(const auto& component : path) components.push_back(component.string());
    return components;
}

ResolvedBaseDirectory resolve_base_directory(
        const std::optional<std::string>& xdg_value,
        const std::optional<std::string>& home_value,
        DirectoryKind directory_kind,
        const fs::path& fallback_suffix) {
    // XDG Base Directory Specification: defined-empty is the same as unset.
    if(xdg_value.has_value() && !xdg_value->empty()) {
        fs::path base_directory = require_absolute_base_path(
                *xdg_value, directory_kind,
                xdg_environment_variable(directory_kind));
        return ResolvedBaseDirectory{
                base_directory, DirectorySource::ExplicitXdg,
                std::move(base_directory), {}};
    }

    if(!home_value.has_value()) {
        throw_resolution_error(
                directory_kind, EnvironmentVariable::Home,
                ResolutionErrorCode::MissingHome);
    }
    if(home_value->empty()) {
        throw_resolution_error(
                directory_kind, EnvironmentVariable::Home,
                ResolutionErrorCode::EmptyHome);
    }

    fs::path home = require_absolute_base_path(
            *home_value, directory_kind, EnvironmentVariable::Home);
    return ResolvedBaseDirectory{
            (home / fallback_suffix).lexically_normal(),
            DirectorySource::HomeFallback, std::move(home),
            path_components(fallback_suffix)};
}

DirectoryCreationBoundary make_creation_boundary(
        ResolvedBaseDirectory base_directory,
        const std::string& application_component) {
    base_directory.creatable_components.push_back(application_component);
    return DirectoryCreationBoundary{
            base_directory.source, std::move(base_directory.directory),
            std::move(base_directory.existing_anchor),
            std::move(base_directory.creatable_components)};
}

std::optional<std::string> process_environment_value(const char* name) {
    const char* value = std::getenv(name);
    if(value == nullptr) return std::nullopt;
    return std::string(value);
}

} // namespace

ResolutionError::ResolutionError(ResolutionFailure failure)
    : std::runtime_error(resolution_diagnostic(failure)), failure_(failure) {
}

ResolvedPaths resolve(const EnvironmentSnapshot& environment) {
    ResolvedBaseDirectory config_base = resolve_base_directory(
            environment.xdg_config_home, environment.home,
            DirectoryKind::Config, ".config");
    ResolvedBaseDirectory state_base = resolve_base_directory(
            environment.xdg_state_home, environment.home,
            DirectoryKind::State, fs::path(".local") / "state");
    ResolvedBaseDirectory cache_base = resolve_base_directory(
            environment.xdg_cache_home, environment.home,
            DirectoryKind::Cache, ".cache");

    const std::string application_component(application_identity::XDG_IDENTITY);
    const fs::path config_directory =
            config_base.directory / application_component;
    const fs::path state_directory = state_base.directory / application_component;
    const fs::path cache_directory = cache_base.directory / application_component;

    fs::path default_log_file = state_directory / application_component;
    default_log_file += DEFAULT_LOG_FILE_SUFFIX;

    return ResolvedPaths{
            ConfigPaths{
                    config_directory,
                    config_directory / std::string(CONFIG_FILE_NAME),
                    make_creation_boundary(
                            std::move(config_base), application_component)},
            StatePaths{
                    state_directory, std::move(default_log_file),
                    make_creation_boundary(
                            std::move(state_base), application_component)},
            CachePaths{
                    cache_directory,
                    make_creation_boundary(
                            std::move(cache_base), application_component)}};
}

StatePaths resolve_state(const EnvironmentSnapshot& environment) {
    ResolvedBaseDirectory state_base = resolve_base_directory(
            environment.xdg_state_home, environment.home,
            DirectoryKind::State, fs::path(".local") / "state");
    const std::string application_component(application_identity::XDG_IDENTITY);
    const fs::path state_directory =
            state_base.directory / application_component;
    fs::path default_log_file = state_directory / application_component;
    default_log_file += DEFAULT_LOG_FILE_SUFFIX;
    return StatePaths{
            state_directory, std::move(default_log_file),
            make_creation_boundary(
                    std::move(state_base), application_component)};
}

CachePaths resolve_cache(const EnvironmentSnapshot& environment) {
    ResolvedBaseDirectory cache_base = resolve_base_directory(
            environment.xdg_cache_home, environment.home,
            DirectoryKind::Cache, ".cache");
    const std::string application_component(application_identity::XDG_IDENTITY);
    const fs::path cache_directory =
            cache_base.directory / application_component;
    return CachePaths{
            cache_directory,
            make_creation_boundary(
                    std::move(cache_base), application_component)};
}

ResolvedPaths resolve_process_environment() {
    const EnvironmentSnapshot environment{
            .xdg_config_home = process_environment_value("XDG_CONFIG_HOME"),
            .xdg_state_home = process_environment_value("XDG_STATE_HOME"),
            .xdg_cache_home = process_environment_value("XDG_CACHE_HOME"),
            .home = process_environment_value("HOME"),
    };
    return resolve(environment);
}

StatePaths resolve_state_process_environment() {
    const EnvironmentSnapshot environment{
            .xdg_config_home = std::nullopt,
            .xdg_state_home = process_environment_value("XDG_STATE_HOME"),
            .xdg_cache_home = std::nullopt,
            .home = process_environment_value("HOME"),
    };
    return resolve_state(environment);
}

CachePaths resolve_cache_process_environment() {
    const EnvironmentSnapshot environment{
            .xdg_config_home = std::nullopt,
            .xdg_state_home = std::nullopt,
            .xdg_cache_home = process_environment_value("XDG_CACHE_HOME"),
            .home = process_environment_value("HOME"),
    };
    return resolve_cache(environment);
}

} // namespace xdg_paths
