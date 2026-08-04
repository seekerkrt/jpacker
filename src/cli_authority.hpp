#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cli_authority {

enum class OperationId {
    Build,
    Upgrade,
    UpgradeAur,
    UpgradeAll,
    Clean,
    Deps,
    Plan,
    Fetch,
    AddSource,
    DeleteSource,
    Revert,
    EditSource,
    ListSources,
    Count,
};

struct OperationSpec {
    OperationId      id;
    std::string_view token;
    std::string_view help_syntax;
    bool             requires_target;
    bool             rejects_options_before_dispatch;
};

inline constexpr std::array<OperationSpec, static_cast<std::size_t>(OperationId::Count)>
        MOGUET_OPERATIONS = {{
                {OperationId::Build, "build", "build <pkg> [V=K]", true, true},
                {OperationId::Upgrade, "upgrade", "upgrade", false, true},
                {OperationId::UpgradeAur, "upgrade-aur", "upgrade-aur", false, true},
                {OperationId::UpgradeAll, "upgrade-all", "upgrade-all", false, true},
                {OperationId::Clean, "clean", "clean", false, true},
                {OperationId::Deps, "deps", "deps [--recursive] <pkg>", true, false},
                {OperationId::Plan, "plan", "plan <pkg>", true, false},
                {OperationId::Fetch, "fetch", "fetch <pkg>", true, false},
                {OperationId::AddSource, "add-src", "add-src <pkg> [V=K]", true, true},
                {OperationId::DeleteSource, "del-src", "del-src <pkg>", true, true},
                {OperationId::Revert, "revert", "revert <pkg>", true, true},
                {OperationId::EditSource, "edit-src", "edit-src <pkg>", true, true},
                {OperationId::ListSources, "list-src", "list-src", false, true},
        }};

constexpr const OperationSpec& operation_spec(OperationId id) noexcept {
    return MOGUET_OPERATIONS[static_cast<std::size_t>(id)];
}

constexpr const OperationSpec* find_moguet_operation(
        std::string_view token) noexcept {
    for(const OperationSpec& spec : MOGUET_OPERATIONS) {
        if(spec.token == token) return &spec;
    }
    return nullptr;
}

enum class GlobalOptionId {
    Edit,
    NoEdit,
    Diff,
    NoDiff,
    NoConfirm,
    BuildMode,
    Rebuild,
    CleanBuild,
    RmDeps,
    Select,
    Aur,
    Repo,
    Count,
};

struct GlobalOptionSpec {
    GlobalOptionId   id;
    std::string_view token;
    std::string_view help_syntax;
    bool             accepts_attached_value;
};

inline constexpr std::array<GlobalOptionSpec, static_cast<std::size_t>(GlobalOptionId::Count)>
        MOGUET_GLOBAL_OPTIONS = {{
                {GlobalOptionId::Edit, "--edit", "--edit", false},
                {GlobalOptionId::NoEdit, "--noedit", "--noedit", false},
                {GlobalOptionId::Diff, "--diff", "--diff", false},
                {GlobalOptionId::NoDiff, "--nodiff", "--nodiff", false},
                {GlobalOptionId::NoConfirm, "--noconfirm", "--noconfirm", false},
                {GlobalOptionId::BuildMode, "--build-mode", "--build-mode=normal|rebuild|clean", true},
                {GlobalOptionId::Rebuild, "--rebuild", "--rebuild", false},
                {GlobalOptionId::CleanBuild, "--cleanbuild", "--cleanbuild", false},
                {GlobalOptionId::RmDeps, "--rmdeps", "--rmdeps", false},
                {GlobalOptionId::Select, "--select", "--select", false},
                {GlobalOptionId::Aur, "--aur", "--aur", false},
                {GlobalOptionId::Repo, "--repo", "--repo", false},
        }};

constexpr const GlobalOptionSpec& global_option_spec(
        GlobalOptionId id) noexcept {
    return MOGUET_GLOBAL_OPTIONS[static_cast<std::size_t>(id)];
}

constexpr const GlobalOptionSpec* find_moguet_global_option(
        std::string_view argument) noexcept {
    for(const GlobalOptionSpec& spec : MOGUET_GLOBAL_OPTIONS) {
        if(argument == spec.token) return &spec;
        if(spec.accepts_attached_value &&
           argument.size() > spec.token.size() &&
           argument.starts_with(spec.token) &&
           argument[spec.token.size()] == '=') {
            return &spec;
        }
    }
    return nullptr;
}

inline constexpr std::string_view BUILD_MODE_NORMAL = "normal";
inline constexpr std::string_view BUILD_MODE_REBUILD = "rebuild";
inline constexpr std::string_view BUILD_MODE_CLEAN = "clean";
inline constexpr std::string_view BUILD_MODE_REBUILD_OPTION =
        "--build-mode=rebuild";
inline constexpr std::string_view BUILD_MODE_CLEAN_OPTION =
        "--build-mode=clean";

inline constexpr std::string_view HELP_SHORT_OPTION = "-h";
inline constexpr std::string_view HELP_LONG_OPTION = "--help";
inline constexpr std::string_view HELP_OPTION_SYNTAX = "-h, --help";
inline constexpr std::string_view VERSION_SHORT_OPTION = "-V";
inline constexpr std::string_view VERSION_LONG_OPTION = "--version";
inline constexpr std::string_view VERSION_OPTION_SYNTAX = "-V, --version";

inline constexpr std::string_view PKGBUILD_EXPORT_OPERATION = "-G";
inline constexpr std::string_view PKGBUILD_EXPORT_SYNTAX = "-G <pkg>";
inline constexpr std::string_view PKGBUILD_PRINT_OPERATION = "-Gp";
inline constexpr std::string_view PKGBUILD_PRINT_SYNTAX = "-Gp <pkg>";

// pacman-compatible entries are documentation examples, not a parser allowlist.
inline constexpr std::string_view PACMAN_SYNC_INSTALL_SYNTAX = "-S <pkg>";
inline constexpr std::string_view PACMAN_SYNC_SELECT_SYNTAX =
        "-S --select <query>";
inline constexpr std::string_view PACMAN_SYSTEM_UPGRADE_SYNTAX = "-Syu";
inline constexpr std::string_view PACMAN_SYNC_SEARCH_SYNTAX = "-Ss <query>";
inline constexpr std::string_view PACMAN_SYNC_INFO_SYNTAX = "-Si <pkg>";
inline constexpr std::string_view PACMAN_FOREIGN_UPDATES_SYNTAX = "-Qua";
inline constexpr std::string_view PACMAN_NEEDED_OPTION_SYNTAX = "--needed";

} // namespace cli_authority
