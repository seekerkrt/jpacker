#include "cli_routing.hpp"

#include "application_identity.hpp"
#include "cli_authority.hpp"
#include "localization.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

std::string package_source_selection_option(PackageSourceSelection selection) {
    switch(selection) {
    case PackageSourceSelection::Auto:
        throw std::logic_error(localization::translate_message(
                "Automatic source selection has no explicit option."));
    case PackageSourceSelection::AurOnly:
        return "--aur";
    case PackageSourceSelection::RepoOnly:
        return "--repo";
    }
    throw std::logic_error(localization::translate_message(
            "Unknown package source selection."));
}

bool is_ascii_whitespace(char character) noexcept {
    switch(character) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v':
        return true;
    default:
        return false;
    }
}

std::string_view trim_ascii_whitespace(std::string_view value) noexcept {
    while(!value.empty() && is_ascii_whitespace(value.front())) {
        value.remove_prefix(1);
    }
    while(!value.empty() && is_ascii_whitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

[[noreturn]] void reject_root_package_selection_invocation(
        const std::string& diagnostic) {
    throw std::invalid_argument(diagnostic);
}

[[noreturn]] void reject_local_source_build_invocation(
        const std::string& diagnostic) {
    throw std::invalid_argument(diagnostic);
}

bool local_source_build_accepts_global_option(
        const std::string& option) {
    const cli_authority::GlobalOptionSpec* spec =
            cli_authority::find_moguet_global_option(option);
    if(spec == nullptr) return false;

    switch(spec->id) {
    case cli_authority::GlobalOptionId::Edit:
    case cli_authority::GlobalOptionId::NoEdit:
    case cli_authority::GlobalOptionId::NoConfirm:
    case cli_authority::GlobalOptionId::BuildMode:
    case cli_authority::GlobalOptionId::Rebuild:
    case cli_authority::GlobalOptionId::CleanBuild:
        return true;
    case cli_authority::GlobalOptionId::Diff:
    case cli_authority::GlobalOptionId::NoDiff:
    case cli_authority::GlobalOptionId::RmDeps:
    case cli_authority::GlobalOptionId::Select:
    case cli_authority::GlobalOptionId::Aur:
    case cli_authority::GlobalOptionId::Repo:
    case cli_authority::GlobalOptionId::Count:
        return false;
    }
    return false;
}

[[noreturn]] void reject_local_source_build_operand_count() {
    // TRANSLATORS: Both placeholders are literal CLI syntax tokens.
    reject_local_source_build_invocation(
            localization::format_translated_message(
                    "Operation {} requires exactly one {} operand.",
                    "build --local", "<directory>"));
}

} // namespace

std::optional<PkgbuildExportMode> pkgbuild_export_mode(const ParsedCliArguments& parsed) {
    if(parsed.operation == cli_authority::PKGBUILD_EXPORT_OPERATION) return PkgbuildExportMode::Tree;
    if(parsed.operation == cli_authority::PKGBUILD_PRINT_OPERATION) return PkgbuildExportMode::PkgbuildStdout;
    return std::nullopt;
}

std::vector<std::string> validate_pkgbuild_export_invocation(
        const ParsedCliArguments& parsed) {
    // POLICY(#173): operation/target 以外の role は、綴りを再解釈せず元 argv 位置のまま拒否する。
    for(const auto& token : parsed.tokens) {
        if(token.role == CliTokenRole::Operation || token.role == CliTokenRole::Target) continue;

        // TRANSLATORS: The placeholders are literal CLI tokens.
        return {localization::format_translated_message(
                "Unsupported option {} for operation {}.", token.value,
                parsed.operation)};
    }

    if(parsed.targets.size() != 1) {
        // TRANSLATORS: The placeholders are a literal CLI operation and the AUR project identity.
        const std::string target_error =
                localization::format_translated_message(
                        "Operation {} requires exactly one {} package target.",
                        parsed.operation, "AUR");
        // TRANSLATORS: The placeholders are literal command, operation, and operand tokens.
        const std::string usage = localization::format_translated_message(
                "Usage: {} {} {}", application_identity::COMMAND_NAME,
                parsed.operation, "<pkg>");
        return {target_error, usage};
    }

    return {};
}

bool parsed_has_semantic_pacman_option(
        const ParsedCliArguments& parsed, const std::string& option) {
    return std::any_of(parsed.tokens.begin(), parsed.tokens.end(), [&](const ParsedCliToken& token) {
        return token.role == CliTokenRole::PacmanOption && token.value == option;
    });
}

SourceSyncOptions parse_source_sync_options(const ParsedCliArguments& parsed) {
    SourceSyncOptions options;
    // POLICY(#173): option valueや`--`後のoperandをsemantic optionへ昇格させない。
    options.needed = parsed_has_semantic_pacman_option(parsed, "--needed");
    return options;
}

SourceSelectableSyncOperation source_selectable_sync_operation(const ParsedCliArguments& parsed) {
    if(!parsed.operation.starts_with("-S")) return SourceSelectableSyncOperation::Unsupported;

    bool has_search = false;
    bool has_info = false;
    bool has_unsupported_modifier = false;

    auto inspect_short_modifiers = [&](const std::string& option, size_t first_modifier) {
        if(option.size() <= first_modifier || option[0] != '-' || option.starts_with("--")) return;
        for(size_t i = first_modifier; i < option.size(); ++i) {
            switch(option[i]) {
            case 's':
                has_search = true;
                break;
            case 'i':
                has_info = true;
                break;
            case 'c':
            case 'g':
            case 'l':
            case 'u':
                has_unsupported_modifier = true;
                break;
            default:
                break;
            }
        }
    };

    inspect_short_modifiers(parsed.operation, 2);
    for(const auto& token : parsed.tokens) {
        if(token.role != CliTokenRole::PacmanOption) continue;
        if(token.value == "--search")
            has_search = true;
        else if(token.value == "--info")
            has_info = true;
        else if(token.value == "--clean" || token.value == "--groups" ||
                token.value == "--list" || token.value == "--sysupgrade")
            has_unsupported_modifier = true;
        else
            inspect_short_modifiers(token.value, 1);
    }

    if(has_unsupported_modifier || (has_search && has_info)) {
        return SourceSelectableSyncOperation::Unsupported;
    }
    if(has_search) return SourceSelectableSyncOperation::Search;
    if(has_info) return SourceSelectableSyncOperation::Info;
    return SourceSelectableSyncOperation::Install;
}

bool pacman_operation_requests_refresh(
        const std::string& operation, const std::vector<std::string>& flags) {
    auto short_option_requests_refresh = [](const std::string& option) {
        if(option.size() < 2 || option[0] != '-' || option[1] == '-') return false;
        return std::find(option.begin() + 1, option.end(), 'y') != option.end();
    };

    if(short_option_requests_refresh(operation)) return true;

    bool option_value_expected = false;
    // POLICY(#172): flags[0] は operation。残りは元の option 順で、値を取る option の次は判定対象外にする。
    for(size_t i = 1; i < flags.size(); ++i) {
        const std::string& flag = flags[i];
        if(option_value_expected) {
            option_value_expected = false;
            continue;
        }
        if(flag == "--") break;
        if(flag == "--refresh" || short_option_requests_refresh(flag)) return true;
        option_value_expected = pacman_option_takes_value(flag);
    }
    return false;
}

std::optional<std::string> validate_source_selection_operation(
        const ParsedCliArguments& parsed) {
    if(parsed.source_selection == PackageSourceSelection::Auto) return std::nullopt;

    const std::string selector = package_source_selection_option(parsed.source_selection);
    const bool requests_refresh = pacman_operation_requests_refresh(parsed.operation, parsed.flags);
    SourceSelectableSyncOperation sync_operation = source_selectable_sync_operation(parsed);

    if(parsed.source_selection == PackageSourceSelection::AurOnly && requests_refresh) {
        // TRANSLATORS: The placeholders are literal CLI/program tokens.
        return localization::format_translated_message(
                "Cannot combine {} with {} refresh for operation {}.",
                "--aur", "pacman", parsed.operation);
    }
    if(sync_operation == SourceSelectableSyncOperation::Unsupported ||
       (sync_operation == SourceSelectableSyncOperation::Install && requests_refresh)) {
        // TRANSLATORS: The placeholders are a source selector description or
        // literal option and a literal CLI operation.
        return localization::format_translated_message(
                "{} is not supported for operation {}.", selector,
                parsed.operation);
    }
    return std::nullopt;
}

std::optional<std::string> unsupported_source_sync_option(
        const ParsedCliArguments& parsed) {
    // POLICY(#56): -S の y/u modifier は official repository update にだけ作用する。
    // AUR/source-build 側へ意味を移せない他の pacman option は、黙って無視せず build 前に止める。
    const std::string& operation = parsed.operation;
    if(operation.size() < 2 || operation[0] != '-' || operation[1] != 'S' ||
       !std::all_of(operation.begin() + 2, operation.end(), [](char modifier) {
           return modifier == 'y' || modifier == 'u';
       })) {
        return operation;
    }

    for(const auto& token : parsed.tokens) {
        if(token.role != CliTokenRole::PacmanOption) continue;
        if(token.value == "--needed") continue;
        return token.value;
    }
    return std::nullopt;
}

RootPackageSelectionInvocation require_root_package_selection_invocation(
        const ParsedCliArguments& parsed) {
    if(!parsed.root_package_selection_requested) {
        throw std::logic_error(localization::translate_message(
                "Root package selection was not requested."));
    }

    if(parsed.operation != "-S") {
        // TRANSLATORS: Both placeholders are literal CLI syntax tokens.
        reject_root_package_selection_invocation(
                localization::format_translated_message(
                        "Option {} is supported only with plain {}.",
                        "--select", "-S"));
    }
    if(parsed.cli_overrides.rm_deps) {
        // TRANSLATORS: Both placeholders are literal CLI option tokens.
        reject_root_package_selection_invocation(
                localization::format_translated_message(
                        "Cannot combine {} and {}.",
                        "--select", "--rmdeps"));
    }

    for(const ParsedCliToken& token : parsed.tokens) {
        switch(token.role) {
        case CliTokenRole::EndOfOptions:
            // TRANSLATORS: Both placeholders are literal CLI tokens.
            reject_root_package_selection_invocation(
                    localization::format_translated_message(
                            "Cannot use {} with {}.", "--", "--select"));
        case CliTokenRole::OpaqueOperand:
            // TRANSLATORS: The placeholders are a literal CLI option and an operand.
            reject_root_package_selection_invocation(
                    localization::format_translated_message(
                            "Opaque operand is not supported with {}: {}",
                            "--select", token.value));
        case CliTokenRole::PacmanOption:
            if(token.value != "--needed") {
                // TRANSLATORS: The placeholders are literal CLI tokens.
                reject_root_package_selection_invocation(
                        localization::format_translated_message(
                                "Unsupported option {} for {}.",
                                token.value, "-S --select"));
            }
            break;
        case CliTokenRole::PacmanOptionValue:
            // The owning pacman option is rejected before this token is reached.
            break;
        case CliTokenRole::MoguetGlobalOption:
        case CliTokenRole::Operation:
        case CliTokenRole::Target:
            break;
        }
    }

    if(parsed.targets.size() != 1 ||
       parsed.target_token_indices.size() != 1 ||
       parsed.tokens[parsed.target_token_indices.front()].role !=
               CliTokenRole::Target) {
        // TRANSLATORS: The placeholders are literal CLI syntax tokens.
        reject_root_package_selection_invocation(
                localization::format_translated_message(
                        "Operation {} requires exactly one {} operand.",
                        "-S --select", "<query>"));
    }

    const std::string_view query =
            trim_ascii_whitespace(parsed.targets.front());
    if(query.empty()) {
        reject_root_package_selection_invocation(
                localization::translate_message(
                        "Root package search query must not be empty."));
    }
    if(std::any_of(
               query.begin(), query.end(), [](unsigned char character) {
                   return std::iscntrl(character) != 0;
               })) {
        reject_root_package_selection_invocation(
                localization::translate_message(
                        "Root package search query contains a control character."));
    }

    return RootPackageSelectionInvocation{
            std::string(query),
            parsed_has_semantic_pacman_option(parsed, "--needed")};
}

bool local_source_build_requested(const ParsedCliArguments& parsed) {
    // POLICY(#271): operation-local selectorはglobal optionへ昇格させず、
    // parserが確定したsemantic pacman-option位置のexact tokenだけを解釈する。
    return parsed_has_semantic_pacman_option(
            parsed, std::string(cli_authority::LOCAL_SOURCE_OPTION));
}

LocalSourceBuildInvocation require_local_source_build_invocation(
        const ParsedCliArguments& parsed) {
    if(!local_source_build_requested(parsed)) {
        throw std::logic_error(localization::translate_message(
                "Local source build was not requested."));
    }

    const std::string build_operation(
            cli_authority::operation_spec(
                    cli_authority::OperationId::Build)
                    .token);
    const std::string local_source_option(
            cli_authority::LOCAL_SOURCE_OPTION);
    if(parsed.operation != build_operation) {
        // TRANSLATORS: Both placeholders are literal CLI tokens.
        reject_local_source_build_invocation(
                localization::format_translated_message(
                        "Option {} is supported only with operation {}.",
                        local_source_option, build_operation));
    }

    const std::size_t selector_count = static_cast<std::size_t>(
            std::count_if(
                    parsed.tokens.begin(), parsed.tokens.end(),
                    [&](const ParsedCliToken& token) {
                        return token.role == CliTokenRole::PacmanOption &&
                               token.value == local_source_option;
                    }));
    if(selector_count != 1) {
        // TRANSLATORS: Both placeholders are literal CLI syntax tokens.
        reject_local_source_build_invocation(
                localization::format_translated_message(
                        "Option {} may be specified only once for operation {}.",
                        local_source_option, build_operation));
    }

    LocalSourceBuildInvocation invocation;
    bool                       has_directory = false;

    for(const ParsedCliToken& token : parsed.tokens) {
        switch(token.role) {
        case CliTokenRole::MoguetGlobalOption:
            if(!local_source_build_accepts_global_option(token.value)) {
                // TRANSLATORS: The placeholders are literal CLI syntax tokens.
                reject_local_source_build_invocation(
                        localization::format_translated_message(
                                "Unsupported option {} for {}.",
                                token.value, "build --local"));
            }
            break;
        case CliTokenRole::PacmanOption:
            if(token.value != local_source_option) {
                // TRANSLATORS: The placeholders are literal CLI syntax tokens.
                reject_local_source_build_invocation(
                        localization::format_translated_message(
                                "Unsupported option {} for {}.",
                                token.value, "build --local"));
            }
            break;
        case CliTokenRole::PacmanOptionValue:
            // The owning pacman option is rejected before this token is reached.
            break;
        case CliTokenRole::EndOfOptions:
            // TRANSLATORS: Both placeholders are literal CLI syntax tokens.
            reject_local_source_build_invocation(
                    localization::format_translated_message(
                            "Cannot use {} with {}.", "--",
                            "build --local"));
        case CliTokenRole::OpaqueOperand:
            // TRANSLATORS: The placeholders are literal CLI syntax and operand tokens.
            reject_local_source_build_invocation(
                    localization::format_translated_message(
                            "Opaque operand is not supported with {}: {}",
                            "build --local", token.value));
        case CliTokenRole::Target: {
            std::string key;
            std::string value;
            const bool  is_assignment =
                    split_env_assignment(token.value, key, value);
            if(!has_directory) {
                if(is_assignment) {
                    // TRANSLATORS: The placeholder is a literal environment assignment.
                    reject_local_source_build_invocation(
                            localization::format_translated_message(
                                    "Environment assignment requires a preceding directory: {}",
                                    token.value));
                }
                if(token.value.find('=') != std::string::npos) {
                    // TRANSLATORS: The placeholder is a literal environment assignment.
                    reject_local_source_build_invocation(
                            localization::format_translated_message(
                                    "Invalid environment assignment: {}",
                                    token.value));
                }
                invocation.directory = token.value;
                has_directory = true;
                break;
            }

            if(is_assignment) {
                invocation.source_environment.ordered_assignments.push_back(
                        SourceEnvironmentAssignment{
                                std::move(key), std::move(value)});
                break;
            }
            if(token.value.find('=') != std::string::npos) {
                // TRANSLATORS: The placeholder is a literal environment assignment.
                reject_local_source_build_invocation(
                        localization::format_translated_message(
                                "Invalid environment assignment: {}",
                                token.value));
            }
            reject_local_source_build_operand_count();
        }
        case CliTokenRole::Operation:
            break;
        }
    }

    if(!has_directory) reject_local_source_build_operand_count();
    return invocation;
}
