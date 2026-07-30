#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

enum class DirectorySource {
    ExplicitXdg,
    HomeFallback,
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

// Directory safety capabilityがenvironmentやpath suffixを再解決せずに使う、
// resolver-ownedの作成境界。existing_anchor自体は作成対象に含めない。
struct DirectoryCreationBoundary {
    DirectorySource         source = DirectorySource::ExplicitXdg;
    std::filesystem::path   base_directory;
    std::filesystem::path   existing_anchor;
    std::vector<std::string> creatable_components;
};

struct ConfigPaths {
    std::filesystem::path directory;
    std::filesystem::path config_file;
    DirectoryCreationBoundary creation_boundary;
};

struct StatePaths {
    std::filesystem::path directory;
    std::filesystem::path default_log_file;
    DirectoryCreationBoundary creation_boundary;
};

struct CachePaths {
    std::filesystem::path directory;
    DirectoryCreationBoundary creation_boundary;
};

struct ResolvedPaths {
    ConfigPaths config;
    StatePaths  state;
    CachePaths  cache;
};

// Pure resolver。filesystemの参照・作成やcurrent working directory補正を行わない。
ResolvedPaths resolve(const EnvironmentSnapshot& environment);

// State log consumerが無関係なconfig/cache environmentをauthorityへ
// 取り込まず、state pathだけを解決するpure resolver。
StatePaths resolve_state(const EnvironmentSnapshot& environment);

// Production adapter。実processのXDG_CONFIG_HOME / XDG_STATE_HOME /
// XDG_CACHE_HOME / HOMEだけをsnapshot化し、pure resolverへ渡す。
ResolvedPaths resolve_process_environment();

// Default state log専用adapter。XDG_STATE_HOME / HOMEだけをsnapshot化する。
StatePaths resolve_state_process_environment();

} // namespace xdg_paths
