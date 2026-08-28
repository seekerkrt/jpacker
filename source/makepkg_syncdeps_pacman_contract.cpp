#include "makepkg_syncdeps_pacman_contract.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <utility>

namespace {

template <typename Issue>
void add_issue(std::vector<Issue>& issues, Issue issue) {
    if(std::find(issues.begin(), issues.end(), issue) == issues.end()) {
        issues.push_back(issue);
    }
}

template <typename Issue>
void canonicalize_issues(std::vector<Issue>& issues) {
    std::sort(issues.begin(), issues.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
}

bool is_long_option(
    const std::string& argument, std::string_view option) noexcept {
    return argument == option ||
           (argument.size() > option.size() &&
            argument.compare(0, option.size(), option) == 0 &&
            argument[option.size()] == '=');
}

bool is_forbidden_argument(const std::string& argument) noexcept {
    constexpr std::array<std::string_view, 14> forbidden_long_options{
        "--config",
        "--hookdir",
        "--root",
        "--sysroot",
        "--dbpath",
        "--cachedir",
        "--gpgdir",
        "--logfile",
        "--nodeps",
        "--assume-installed",
        "--dbonly",
        "--noscriptlet",
        "--remove",
        "--upgrade",
    };
    if(std::any_of(
           forbidden_long_options.begin(), forbidden_long_options.end(),
           [&argument](std::string_view option) {
               return is_long_option(argument, option);
           })) {
        return true;
    }
    return argument.size() >= 2 &&
           (argument.compare(0, 2, "-R") == 0 ||
            argument.compare(0, 2, "-U") == 0);
}

bool is_valid_pacman_setting_state(
    MakepkgSyncDependencyPacmanSettingState state) noexcept {
    switch(state) {
        case MakepkgSyncDependencyPacmanSettingState::Missing:
        case MakepkgSyncDependencyPacmanSettingState::
            InvocationOwnedInstalledAdapter:
        case MakepkgSyncDependencyPacmanSettingState::Custom:
        case MakepkgSyncDependencyPacmanSettingState::Conflict:
        case MakepkgSyncDependencyPacmanSettingState::Unknown:
            return true;
    }
    return false;
}

bool is_valid_pacman_auth_setting_state(
    MakepkgSyncDependencyPacmanAuthSettingState state) noexcept {
    switch(state) {
        case MakepkgSyncDependencyPacmanAuthSettingState::MakepkgDefault:
        case MakepkgSyncDependencyPacmanAuthSettingState::Custom:
        case MakepkgSyncDependencyPacmanAuthSettingState::Conflict:
        case MakepkgSyncDependencyPacmanAuthSettingState::Unknown:
            return true;
    }
    return false;
}

bool is_valid_installed_adapter_command(
    const std::string& command) noexcept {
    if(command.empty() || command.front() != '/' ||
       command.find('\0') != std::string::npos) {
        return false;
    }
    return std::none_of(
        command.begin(), command.end(), [](unsigned char character) {
            return std::isspace(character) != 0;
        });
}

} // namespace

MakepkgSyncDependencyPacmanCall::MakepkgSyncDependencyPacmanCall(
    MakepkgSyncDependencyPacmanCallKind kind,
    MakepkgSyncDependencyPacmanSafeOptions safe_options,
    std::vector<std::string> dependency_specifications,
    std::vector<std::string> arguments,
    std::vector<MakepkgSyncDependencyPacmanCallIssueKind> issues) noexcept
    : kind_(kind), safe_options_(safe_options),
      dependency_specifications_(std::move(dependency_specifications)),
      arguments_(std::move(arguments)), issues_(std::move(issues)) {
}

MakepkgSyncDependencyPacmanCallKind
MakepkgSyncDependencyPacmanCall::kind() const noexcept {
    return kind_;
}

bool MakepkgSyncDependencyPacmanCall::is_supported() const noexcept {
    return kind_ != MakepkgSyncDependencyPacmanCallKind::Unsupported &&
           issues_.empty();
}

const MakepkgSyncDependencyPacmanSafeOptions&
MakepkgSyncDependencyPacmanCall::safe_options() const noexcept {
    return safe_options_;
}

const std::vector<std::string>&
MakepkgSyncDependencyPacmanCall::dependency_specifications()
    const noexcept {
    return dependency_specifications_;
}

const std::vector<std::string>&
MakepkgSyncDependencyPacmanCall::arguments() const noexcept {
    return arguments_;
}

const std::vector<MakepkgSyncDependencyPacmanCallIssueKind>&
MakepkgSyncDependencyPacmanCall::issues() const noexcept {
    return issues_;
}

bool is_valid_makepkg_sync_dependency_specification(
    const std::string& specification) noexcept {
    return !specification.empty() && specification.front() != '-' &&
           specification.find('\0') == std::string::npos;
}

MakepkgSyncDependencyPacmanCall
parse_makepkg_sync_dependency_pacman_call(
    const std::vector<std::string>& arguments) {
    MakepkgSyncDependencyPacmanSafeOptions safe_options;
    std::vector<std::string> dependency_specifications;
    std::vector<MakepkgSyncDependencyPacmanCallIssueKind> issues;
    const auto unsupported = [&]() {
        canonicalize_issues(issues);
        return MakepkgSyncDependencyPacmanCall(
            MakepkgSyncDependencyPacmanCallKind::Unsupported,
            safe_options, dependency_specifications, arguments,
            std::move(issues));
    };

    if(arguments.empty()) {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::ArgumentsMissing);
        return unsupported();
    }

    if(std::any_of(
           arguments.begin(), arguments.end(), is_forbidden_argument)) {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::ForbiddenOption);
        return unsupported();
    }

    if(arguments.front() == "-T") {
        dependency_specifications.assign(
            arguments.begin() + 1, arguments.end());
        if(dependency_specifications.empty()) {
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanCallIssueKind::
                    DependencySpecificationsMissing);
        }
        if(std::any_of(
               dependency_specifications.begin(),
               dependency_specifications.end(),
               [](const std::string& specification) {
                   return !is_valid_makepkg_sync_dependency_specification(
                       specification);
               })) {
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanCallIssueKind::
                    InvalidDependencySpecification);
        }
        if(!issues.empty()) return unsupported();
        return MakepkgSyncDependencyPacmanCall(
            MakepkgSyncDependencyPacmanCallKind::DependencyCheck,
            safe_options, std::move(dependency_specifications), arguments,
            {});
    }

    if(arguments.front() == "-Qi") {
        if(arguments.size() != 1) {
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanCallIssueKind::UnexpectedArgument);
            return unsupported();
        }
        return MakepkgSyncDependencyPacmanCall(
            MakepkgSyncDependencyPacmanCallKind::InstalledPackageQuery,
            safe_options, {}, arguments, {});
    }

    std::size_t index = 0;
    while(index < arguments.size() && arguments[index] != "-S") {
        const std::string& argument = arguments[index];
        if(argument == "--noconfirm") {
            if(safe_options.no_confirm) {
                add_issue(
                    issues,
                    MakepkgSyncDependencyPacmanCallIssueKind::
                        DuplicateSafeOption);
            }
            safe_options.no_confirm = true;
            ++index;
            continue;
        }
        if(argument == "--noprogressbar") {
            if(safe_options.no_progress_bar) {
                add_issue(
                    issues,
                    MakepkgSyncDependencyPacmanCallIssueKind::
                        DuplicateSafeOption);
            }
            safe_options.no_progress_bar = true;
            ++index;
            continue;
        }
        if(argument == "--color") {
            if(safe_options.color_never) {
                add_issue(
                    issues,
                    MakepkgSyncDependencyPacmanCallIssueKind::
                        DuplicateSafeOption);
            }
            if(index + 1 >= arguments.size()) {
                add_issue(
                    issues,
                    MakepkgSyncDependencyPacmanCallIssueKind::
                        SafeOptionValueMissing);
                return unsupported();
            }
            if(arguments[index + 1] != "never") {
                add_issue(
                    issues,
                    MakepkgSyncDependencyPacmanCallIssueKind::
                        InvalidSafeOptionValue);
                return unsupported();
            }
            safe_options.color_never = true;
            index += 2;
            continue;
        }

        add_issue(
            issues,
            !argument.empty() && argument.front() == '-'
                ? MakepkgSyncDependencyPacmanCallIssueKind::UnsupportedOption
                : MakepkgSyncDependencyPacmanCallIssueKind::
                      UnsupportedOperation);
        return unsupported();
    }

    if(index >= arguments.size() || arguments[index] != "-S") {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::UnsupportedOperation);
        return unsupported();
    }
    ++index;

    if(index >= arguments.size() || arguments[index] != "--asdeps") {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::
                RequiredAsDependenciesOptionMissing);
        return unsupported();
    }
    ++index;

    dependency_specifications.assign(
        arguments.begin() + static_cast<std::ptrdiff_t>(index),
        arguments.end());
    if(dependency_specifications.empty()) {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::
                DependencySpecificationsMissing);
    }
    if(std::any_of(
           dependency_specifications.begin(),
           dependency_specifications.end(),
           [](const std::string& specification) {
               return !is_valid_makepkg_sync_dependency_specification(
                   specification);
           })) {
        add_issue(
            issues,
            MakepkgSyncDependencyPacmanCallIssueKind::
                InvalidDependencySpecification);
    }
    if(!issues.empty()) return unsupported();

    return MakepkgSyncDependencyPacmanCall(
        MakepkgSyncDependencyPacmanCallKind::DependencyInstall,
        safe_options, std::move(dependency_specifications), arguments, {});
}

MakepkgSyncDependencyPacmanRoutePolicy::
    MakepkgSyncDependencyPacmanRoutePolicy(
        MakepkgSyncDependencyPacmanRouteState state,
        MakepkgSyncDependencyPacmanRouteObservation observation,
        std::vector<MakepkgSyncDependencyPacmanRouteIssueKind> issues) noexcept
    : state_(state), observation_(std::move(observation)),
      issues_(std::move(issues)) {
}

MakepkgSyncDependencyPacmanRouteState
MakepkgSyncDependencyPacmanRoutePolicy::state() const noexcept {
    return state_;
}

const MakepkgSyncDependencyPacmanRouteObservation&
MakepkgSyncDependencyPacmanRoutePolicy::observation() const noexcept {
    return observation_;
}

const std::vector<MakepkgSyncDependencyPacmanRouteIssueKind>&
MakepkgSyncDependencyPacmanRoutePolicy::issues() const noexcept {
    return issues_;
}

bool MakepkgSyncDependencyPacmanRoutePolicy::establishes_receipt_authority()
    const noexcept {
    return false;
}

MakepkgSyncDependencyPacmanRoutePolicy
evaluate_makepkg_sync_dependency_pacman_route(
    const MakepkgSyncDependencyPacmanRouteObservation& observation) {
    std::vector<MakepkgSyncDependencyPacmanRouteIssueKind> issues;
    bool is_invalid = false;
    const auto invalidate = [&issues, &is_invalid](
                                MakepkgSyncDependencyPacmanRouteIssueKind
                                    issue) {
        add_issue(issues, issue);
        is_invalid = true;
    };

    if(!is_valid_pacman_setting_state(observation.pacman.state)) {
        invalidate(
            MakepkgSyncDependencyPacmanRouteIssueKind::
                InvalidPacmanSettingState);
    }
    if(!is_valid_pacman_auth_setting_state(
           observation.pacman_auth.state)) {
        invalidate(
            MakepkgSyncDependencyPacmanRouteIssueKind::
                InvalidPacmanAuthSettingState);
    }

    switch(observation.pacman.state) {
        case MakepkgSyncDependencyPacmanSettingState::Missing:
            if(!observation.pacman.observed_values.empty()) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanObservationShapeMismatch);
            }
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanMissing);
            break;
        case MakepkgSyncDependencyPacmanSettingState::
            InvocationOwnedInstalledAdapter:
            if(observation.pacman.observed_values.size() != 1) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanObservationShapeMismatch);
            } else if(!is_valid_installed_adapter_command(
                          observation.pacman.observed_values.front())) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        InstalledAdapterCommandInvalid);
            }
            break;
        case MakepkgSyncDependencyPacmanSettingState::Custom:
            if(observation.pacman.observed_values.empty()) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanObservationShapeMismatch);
            }
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanCustom);
            break;
        case MakepkgSyncDependencyPacmanSettingState::Conflict:
            if(observation.pacman.observed_values.size() < 2) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanObservationShapeMismatch);
            }
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanConflict);
            break;
        case MakepkgSyncDependencyPacmanSettingState::Unknown:
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanUnknown);
            break;
    }

    switch(observation.pacman_auth.state) {
        case MakepkgSyncDependencyPacmanAuthSettingState::MakepkgDefault:
            if(!observation.pacman_auth.observed_values.empty()) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanAuthObservationShapeMismatch);
            }
            break;
        case MakepkgSyncDependencyPacmanAuthSettingState::Custom:
            if(observation.pacman_auth.observed_values.empty()) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanAuthObservationShapeMismatch);
            }
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanAuthCustom);
            break;
        case MakepkgSyncDependencyPacmanAuthSettingState::Conflict:
            if(observation.pacman_auth.observed_values.size() < 2) {
                invalidate(
                    MakepkgSyncDependencyPacmanRouteIssueKind::
                        PacmanAuthObservationShapeMismatch);
            }
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::
                    PacmanAuthConflict);
            break;
        case MakepkgSyncDependencyPacmanAuthSettingState::Unknown:
            add_issue(
                issues,
                MakepkgSyncDependencyPacmanRouteIssueKind::PacmanAuthUnknown);
            break;
    }

    canonicalize_issues(issues);
    if(is_invalid) {
        return MakepkgSyncDependencyPacmanRoutePolicy(
            MakepkgSyncDependencyPacmanRouteState::Invalid, observation,
            std::move(issues));
    }
    if(observation.pacman.state ==
           MakepkgSyncDependencyPacmanSettingState::Conflict ||
       observation.pacman_auth.state ==
           MakepkgSyncDependencyPacmanAuthSettingState::Conflict) {
        return MakepkgSyncDependencyPacmanRoutePolicy(
            MakepkgSyncDependencyPacmanRouteState::Conflict, observation,
            std::move(issues));
    }
    if(observation.pacman.state ==
           MakepkgSyncDependencyPacmanSettingState::Unknown ||
       observation.pacman_auth.state ==
           MakepkgSyncDependencyPacmanAuthSettingState::Unknown) {
        return MakepkgSyncDependencyPacmanRoutePolicy(
            MakepkgSyncDependencyPacmanRouteState::Unknown, observation,
            std::move(issues));
    }
    if(observation.pacman.state ==
           MakepkgSyncDependencyPacmanSettingState::Custom ||
       observation.pacman_auth.state ==
           MakepkgSyncDependencyPacmanAuthSettingState::Custom) {
        return MakepkgSyncDependencyPacmanRoutePolicy(
            MakepkgSyncDependencyPacmanRouteState::Unsupported, observation,
            std::move(issues));
    }
    if(observation.pacman.state ==
       MakepkgSyncDependencyPacmanSettingState::Missing) {
        return MakepkgSyncDependencyPacmanRoutePolicy(
            MakepkgSyncDependencyPacmanRouteState::Missing, observation,
            std::move(issues));
    }
    return MakepkgSyncDependencyPacmanRoutePolicy(
        MakepkgSyncDependencyPacmanRouteState::Supported, observation,
        std::move(issues));
}
