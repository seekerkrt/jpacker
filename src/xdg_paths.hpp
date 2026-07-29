#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace xdg_paths {

enum class DirectoryKind {
    Config,
    State,
    Cache,
};

enum class EnvironmentVariable {
    XdgConfigHome,
    XdgStateHome,
    XdgCacheHome,
    Home,
};

enum class ResolutionErrorCode {
    MissingHome,
    EmptyHome,
    RelativePath,
    DotComponent,
    AmbiguousLeadingDoubleSlash,
    EmbeddedNull,
    MalformedPath,
};

struct ResolutionFailure {
    DirectoryKind        directory_kind;
    EnvironmentVariable environment_variable;
    ResolutionErrorCode  code;
};

class ResolutionError final : public std::runtime_error {
    ResolutionFailure failure_;

public:
    explicit ResolutionError(ResolutionFailure failure);

    const ResolutionFailure& failure() const noexcept {
        return failure_;
    }
};

// unsetとdefined-emptyを区別し、environment値をowning copyとして保持する。
struct EnvironmentSnapshot {
    std::optional<std::string> xdg_config_home;
    std::optional<std::string> xdg_state_home;
    std::optional<std::string> xdg_cache_home;
    std::optional<std::string> home;
};

struct ConfigPaths {
    std::filesystem::path directory;
    std::filesystem::path config_file;
};

struct StatePaths {
    std::filesystem::path directory;
    std::filesystem::path default_log_file;
};

struct CachePaths {
    std::filesystem::path directory;
};

struct ResolvedPaths {
    ConfigPaths config;
    StatePaths  state;
    CachePaths  cache;
};

// Pure resolver。filesystemの参照・作成やcurrent working directory補正を行わない。
ResolvedPaths resolve(const EnvironmentSnapshot& environment);

// Production adapter。実processのXDG_CONFIG_HOME / XDG_STATE_HOME /
// XDG_CACHE_HOME / HOMEだけをsnapshot化し、pure resolverへ渡す。
ResolvedPaths resolve_process_environment();

} // namespace xdg_paths
