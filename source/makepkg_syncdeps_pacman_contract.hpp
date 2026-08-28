#pragma once

#include <string>
#include <vector>

// This parser owns only the argv grammar received by the documented makepkg
// PACMAN command boundary. It neither executes pacman nor interprets package
// dependency semantics.
enum class MakepkgSyncDependencyPacmanCallKind {
    DependencyCheck,
    InstalledPackageQuery,
    DependencyInstall,
    Unsupported,
};

struct MakepkgSyncDependencyPacmanSafeOptions {
    bool no_confirm = false;
    bool no_progress_bar = false;
    bool color_never = false;

    bool operator==(
        const MakepkgSyncDependencyPacmanSafeOptions&) const = default;
};

// Declaration order is the canonical issue order.
enum class MakepkgSyncDependencyPacmanCallIssueKind {
    ArgumentsMissing,
    ForbiddenOption,
    UnsupportedOperation,
    UnsupportedOption,
    RequiredAsDependenciesOptionMissing,
    DependencySpecificationsMissing,
    InvalidDependencySpecification,
    DuplicateSafeOption,
    SafeOptionValueMissing,
    InvalidSafeOptionValue,
    UnexpectedArgument,
};

class MakepkgSyncDependencyPacmanCall final {
public:
    MakepkgSyncDependencyPacmanCall() = delete;
    MakepkgSyncDependencyPacmanCall(
        const MakepkgSyncDependencyPacmanCall&) = default;
    MakepkgSyncDependencyPacmanCall(
        MakepkgSyncDependencyPacmanCall&&) noexcept = default;
    MakepkgSyncDependencyPacmanCall& operator=(
        const MakepkgSyncDependencyPacmanCall&) = default;
    MakepkgSyncDependencyPacmanCall& operator=(
        MakepkgSyncDependencyPacmanCall&&) noexcept = default;
    ~MakepkgSyncDependencyPacmanCall() = default;

    [[nodiscard]] MakepkgSyncDependencyPacmanCallKind kind() const noexcept;
    [[nodiscard]] bool is_supported() const noexcept;
    [[nodiscard]] const MakepkgSyncDependencyPacmanSafeOptions& safe_options()
        const noexcept;
    // The exact argv elements are retained as opaque data. In particular,
    // version and provider syntax is not parsed or normalized here.
    [[nodiscard]] const std::vector<std::string>& dependency_specifications()
        const noexcept;
    [[nodiscard]] const std::vector<std::string>& arguments() const noexcept;
    [[nodiscard]] const std::vector<
        MakepkgSyncDependencyPacmanCallIssueKind>&
    issues() const noexcept;

private:
    MakepkgSyncDependencyPacmanCall(
        MakepkgSyncDependencyPacmanCallKind kind,
        MakepkgSyncDependencyPacmanSafeOptions safe_options,
        std::vector<std::string> dependency_specifications,
        std::vector<std::string> arguments,
        std::vector<MakepkgSyncDependencyPacmanCallIssueKind> issues) noexcept;

    MakepkgSyncDependencyPacmanCallKind kind_;
    MakepkgSyncDependencyPacmanSafeOptions safe_options_;
    std::vector<std::string> dependency_specifications_;
    std::vector<std::string> arguments_;
    std::vector<MakepkgSyncDependencyPacmanCallIssueKind> issues_;

    friend MakepkgSyncDependencyPacmanCall
    parse_makepkg_sync_dependency_pacman_call(
        const std::vector<std::string>& arguments);
};

// Empty elements, embedded NUL bytes, and option-shaped elements cannot be
// forwarded as dependency specifications through this strict argv boundary.
// No package/version/provider semantics are evaluated.
[[nodiscard]] bool is_valid_makepkg_sync_dependency_specification(
    const std::string& specification) noexcept;

// arguments excludes the PACMAN executable itself.
[[nodiscard]] MakepkgSyncDependencyPacmanCall
parse_makepkg_sync_dependency_pacman_call(
    const std::vector<std::string>& arguments);

// PACMAN provenance is an independent route gate. Matching an installed path
// string alone is not enough: only the invocation-owned installed-adapter
// state can enter the supported arm.
enum class MakepkgSyncDependencyPacmanSettingState {
    Missing,
    InvocationOwnedInstalledAdapter,
    Custom,
    Conflict,
    Unknown,
};

// PACMAN_AUTH remains makepkg's privilege-prefix setting. It is never a
// receipt authority, even when the default route is accepted.
enum class MakepkgSyncDependencyPacmanAuthSettingState {
    MakepkgDefault,
    Custom,
    Conflict,
    Unknown,
};

struct MakepkgSyncDependencyPacmanSettingObservation {
    MakepkgSyncDependencyPacmanSettingState state;
    std::vector<std::string> observed_values;
};

struct MakepkgSyncDependencyPacmanAuthSettingObservation {
    MakepkgSyncDependencyPacmanAuthSettingState state;
    std::vector<std::string> observed_values;
};

struct MakepkgSyncDependencyPacmanRouteObservation {
    MakepkgSyncDependencyPacmanSettingObservation pacman;
    MakepkgSyncDependencyPacmanAuthSettingObservation pacman_auth;
};

enum class MakepkgSyncDependencyPacmanRouteState {
    Supported,
    Missing,
    Unsupported,
    Conflict,
    Unknown,
    Invalid,
};

// Declaration order is the canonical issue order.
enum class MakepkgSyncDependencyPacmanRouteIssueKind {
    InvalidPacmanSettingState,
    InvalidPacmanAuthSettingState,
    PacmanObservationShapeMismatch,
    PacmanAuthObservationShapeMismatch,
    InstalledAdapterCommandInvalid,
    PacmanMissing,
    PacmanCustom,
    PacmanConflict,
    PacmanUnknown,
    PacmanAuthCustom,
    PacmanAuthConflict,
    PacmanAuthUnknown,
};

class MakepkgSyncDependencyPacmanRoutePolicy final {
public:
    MakepkgSyncDependencyPacmanRoutePolicy() = delete;
    MakepkgSyncDependencyPacmanRoutePolicy(
        const MakepkgSyncDependencyPacmanRoutePolicy&) = default;
    MakepkgSyncDependencyPacmanRoutePolicy(
        MakepkgSyncDependencyPacmanRoutePolicy&&) noexcept = default;
    MakepkgSyncDependencyPacmanRoutePolicy& operator=(
        const MakepkgSyncDependencyPacmanRoutePolicy&) = default;
    MakepkgSyncDependencyPacmanRoutePolicy& operator=(
        MakepkgSyncDependencyPacmanRoutePolicy&&) noexcept = default;
    ~MakepkgSyncDependencyPacmanRoutePolicy() = default;

    [[nodiscard]] MakepkgSyncDependencyPacmanRouteState state()
        const noexcept;
    [[nodiscard]] const MakepkgSyncDependencyPacmanRouteObservation&
    observation() const noexcept;
    [[nodiscard]] const std::vector<
        MakepkgSyncDependencyPacmanRouteIssueKind>&
    issues() const noexcept;

    // Route support is only a precondition for later session evidence. It
    // never promotes PACMAN or PACMAN_AUTH configuration into receipt proof.
    [[nodiscard]] bool establishes_receipt_authority() const noexcept;

private:
    MakepkgSyncDependencyPacmanRoutePolicy(
        MakepkgSyncDependencyPacmanRouteState state,
        MakepkgSyncDependencyPacmanRouteObservation observation,
        std::vector<MakepkgSyncDependencyPacmanRouteIssueKind> issues) noexcept;

    MakepkgSyncDependencyPacmanRouteState state_;
    MakepkgSyncDependencyPacmanRouteObservation observation_;
    std::vector<MakepkgSyncDependencyPacmanRouteIssueKind> issues_;

    friend MakepkgSyncDependencyPacmanRoutePolicy
    evaluate_makepkg_sync_dependency_pacman_route(
        const MakepkgSyncDependencyPacmanRouteObservation& observation);
};

[[nodiscard]] MakepkgSyncDependencyPacmanRoutePolicy
evaluate_makepkg_sync_dependency_pacman_route(
    const MakepkgSyncDependencyPacmanRouteObservation& observation);
