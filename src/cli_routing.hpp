#pragma once

#include "cli_parser.hpp"

#include <optional>
#include <string>
#include <vector>

enum class PkgbuildExportMode {
    Tree,
    PkgbuildStdout,
};

enum class SourceSelectableSyncOperation {
    Install,
    Search,
    Info,
    Unsupported,
};

// pacman-compatible sync optionのうち、source buildへ意味を保って変換できるinvocation-level policy。
struct SourceSyncOptions {
    bool needed = false;
};

// `--select`のpre-query validationで確定したowned request。
// queryはASCII whitespace trim済みで、pacman argvへは戻さない。
struct RootPackageSelectionInvocation {
    std::string query;
    bool        needed = false;
};

std::optional<PkgbuildExportMode> pkgbuild_export_mode(const ParsedCliArguments& parsed);
// Emptyならvalid。複数messageの順序はCLI presentation側でも維持する。
std::vector<std::string> validate_pkgbuild_export_invocation(
        const ParsedCliArguments& parsed);

bool parsed_has_semantic_pacman_option(
        const ParsedCliArguments& parsed, const std::string& option);
SourceSyncOptions parse_source_sync_options(const ParsedCliArguments& parsed);
SourceSelectableSyncOperation source_selectable_sync_operation(
        const ParsedCliArguments& parsed);
// nulloptならvalid。messageの表示と終了statusはrunnerが所有する。
std::optional<std::string> validate_source_selection_operation(
        const ParsedCliArguments& parsed);
bool pacman_operation_requests_refresh(
        const std::string& operation, const std::vector<std::string>& flags);
std::optional<std::string> unsupported_source_sync_option(
        const ParsedCliArguments& parsed);

// `root_package_selection_requested`がtrueのinvocationだけを受ける。
// invalid invocationはcandidate query前に表示できるdiagnosticで拒否する。
RootPackageSelectionInvocation require_root_package_selection_invocation(
        const ParsedCliArguments& parsed);
