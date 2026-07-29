#include "cli_parser.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

enum class MoguetGlobalOption {
    NoEdit,
    NoDiff,
    NoConfirm,
    Rebuild,
    CleanBuild,
    RmDeps,
    Aur,
    Repo,
};

const std::array<std::pair<const char*, MoguetGlobalOption>, 8> MOGUET_GLOBAL_OPTIONS = {{
        {"--noedit", MoguetGlobalOption::NoEdit},
        {"--nodiff", MoguetGlobalOption::NoDiff},
        {"--noconfirm", MoguetGlobalOption::NoConfirm},
        {"--rebuild", MoguetGlobalOption::Rebuild},
        {"--cleanbuild", MoguetGlobalOption::CleanBuild},
        {"--rmdeps", MoguetGlobalOption::RmDeps},
        {"--aur", MoguetGlobalOption::Aur},
        {"--repo", MoguetGlobalOption::Repo},
}};

std::optional<MoguetGlobalOption> moguet_global_option_kind(const std::string& arg) {
    auto option = std::find_if(
            MOGUET_GLOBAL_OPTIONS.begin(), MOGUET_GLOBAL_OPTIONS.end(),
            [&arg](const auto& entry) { return arg == entry.first; });
    if(option == MOGUET_GLOBAL_OPTIONS.end()) return std::nullopt;
    return option->second;
}

void report_parse_error(const std::string& message) {
    // POLICY: parse failureはLogger初期化前に出す。既存のpre-log stderr形式をここで維持する。
    std::cerr << "\033[1;31m:: Error:\033[0m " << message << std::endl;
}

bool apply_moguet_global_option(const std::string& arg, ParsedCliArguments& parsed) {
    std::optional<MoguetGlobalOption> option = moguet_global_option_kind(arg);
    if(!option.has_value()) throw std::logic_error("Unknown Moguet global option: " + arg);

    switch(option.value()) {
    case MoguetGlobalOption::NoEdit:
        parsed.cli_overrides.no_edit = true;
        break;
    case MoguetGlobalOption::NoDiff:
        parsed.cli_overrides.no_diff = true;
        break;
    case MoguetGlobalOption::NoConfirm:
        parsed.cli_overrides.no_confirm = true;
        break;
    case MoguetGlobalOption::Rebuild:
        parsed.cli_overrides.rebuild = true;
        break;
    case MoguetGlobalOption::CleanBuild:
        parsed.cli_overrides.clean_build = true;
        break;
    case MoguetGlobalOption::RmDeps:
        parsed.cli_overrides.rm_deps = true;
        break;
    case MoguetGlobalOption::Aur:
        if(parsed.source_selection == PackageSourceSelection::RepoOnly) {
            report_parse_error("Cannot combine --aur and --repo.");
            return false;
        }
        parsed.source_selection = PackageSourceSelection::AurOnly;
        break;
    case MoguetGlobalOption::Repo:
        if(parsed.source_selection == PackageSourceSelection::AurOnly) {
            report_parse_error("Cannot combine --aur and --repo.");
            return false;
        }
        parsed.source_selection = PackageSourceSelection::RepoOnly;
        break;
    }
    return true;
}

} // namespace

bool is_moguet_global_option(const std::string& arg) {
    return moguet_global_option_kind(arg).has_value();
}

bool pacman_option_takes_value(const std::string& arg) {
    // POLICY: 固定の pacman option table。process lifetime の保持だが mutable な副作用は持たない。
    static const std::vector<std::string> s_long_opts = {
            "--arch", "--assume-installed", "--cachedir", "--color", "--config", "--dbpath",
            "--gpgdir", "--hookdir", "--ignore", "--ignoregroup", "--logfile", "--overwrite",
            "--print-format", "--root", "--sysroot"};
    static const std::vector<std::string> s_short_opts = {"-b", "-r"};

    if(arg.find('=') != std::string::npos) return false;
    if(std::find(s_long_opts.begin(), s_long_opts.end(), arg) != s_long_opts.end()) return true;
    return std::find(s_short_opts.begin(), s_short_opts.end(), arg) != s_short_opts.end();
}

std::optional<ParsedCliArguments> parse_cli_arguments(int argc, char* argv[]) {
    ParsedCliArguments parsed;
    bool               has_operation = false;

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if(arg.empty()) {
            report_parse_error("Empty arguments are not supported.");
            return std::nullopt;
        }

        if(!has_operation) {
            if(is_moguet_global_option(arg)) {
                if(!apply_moguet_global_option(arg, parsed)) return std::nullopt;
                parsed.tokens.push_back(
                        ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::MoguetGlobalOption});
                parsed.consumed_global_options.push_back(arg);
                continue;
            }

            has_operation = true;
            parsed.operation = arg;
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::Operation});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            continue;
        }

        // POLICY(#173): pacmanの構文状態を確定してから、通常位置のMoguet optionだけを消費する。
        if(parsed.pending_option.has_value()) {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::PacmanOptionValue});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            parsed.pending_option.reset();
            continue;
        }
        if(parsed.end_of_options) {
            std::size_t token_index = parsed.tokens.size();
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::OpaqueOperand});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.targets.push_back(arg);
            parsed.target_token_indices.push_back(token_index);
            continue;
        }
        if(arg == "--") {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::EndOfOptions});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            parsed.end_of_options = true;
            continue;
        }
        if(is_moguet_global_option(arg)) {
            if(!apply_moguet_global_option(arg, parsed)) return std::nullopt;
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::MoguetGlobalOption});
            parsed.consumed_global_options.push_back(arg);
            continue;
        }
        if(arg[0] == '-') {
            parsed.tokens.push_back(
                    ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::PacmanOption});
            parsed.ordered_pacman_args.push_back(arg);
            parsed.flags.push_back(arg);
            if(pacman_option_takes_value(arg)) parsed.pending_option = arg;
            continue;
        }

        std::size_t token_index = parsed.tokens.size();
        parsed.tokens.push_back(
                ParsedCliToken{arg, static_cast<std::size_t>(i), CliTokenRole::Target});
        parsed.ordered_pacman_args.push_back(arg);
        parsed.targets.push_back(arg);
        parsed.target_token_indices.push_back(token_index);
    }

    if(parsed.pending_option.has_value()) {
        report_parse_error("Missing value for option " + parsed.pending_option.value());
        return std::nullopt;
    }
    return parsed;
}

std::vector<std::string> ordered_pacman_args_excluding_targets(
        const ParsedCliArguments& parsed,
        const std::set<std::size_t>& excluded_target_token_indices) {
    std::vector<std::string> args;
    for(std::size_t i = 0; i < parsed.tokens.size(); ++i) {
        const ParsedCliToken& token = parsed.tokens[i];
        if(token.role == CliTokenRole::MoguetGlobalOption) continue;
        if((token.role == CliTokenRole::Target || token.role == CliTokenRole::OpaqueOperand) &&
           excluded_target_token_indices.contains(i)) {
            continue;
        }
        args.push_back(token.value);
    }
    return args;
}
