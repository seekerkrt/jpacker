#include "cli_routing.hpp"

#include "application_identity.hpp"
#include "cli_authority.hpp"
#include "localization.hpp"

#include <algorithm>
#include <stdexcept>

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
