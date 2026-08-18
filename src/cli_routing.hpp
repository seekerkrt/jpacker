#pragma once

#include "cli_parser.hpp"
#include "source_environment.hpp"

#include <optional>
#include <string>
#include <vector>

enum class PkgbuildExportMode {
    Tree,
    PkgbuildStdout,
};

// `-G` / `-Gp`のstrict grammarで確定したoperation-local request。
// output_directoryは`-G`だけが所有し、raw argvへ戻さない。
struct PkgbuildExportInvocation {
    PkgbuildExportMode          mode = PkgbuildExportMode::Tree;
    std::string                 target;
    std::optional<std::string> output_directory;
};

enum class SourceSelectableSyncOperation {
    Install,
    Search,
    Info,
    Unsupported,
};

// `--dry-run`がproduction preflightへ接続できるowned routeだけを列挙する。
// Unsupportedはgeneric pacman pass-throughを含むfail-closed結果。
enum class DryRunOperation {
    SyncInstall,
    SyncSystemUpdate,
    Fetch,
    RemoteBuild,
    LocalBuild,
    Upgrade,
    UpgradeAur,
    UpgradeAll,
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

// `build --local`のstrict entry validationで確定したowned request。
// directoryはfilesystem inspection前のlexical operandをそのまま保持する。
struct LocalSourceBuildInvocation {
    std::string            directory;
    SourceBuildEnvironment source_environment;
};

std::optional<PkgbuildExportMode> pkgbuild_export_mode(const ParsedCliArguments& parsed);
// parse済みargvだけを参照し、filesystem / AUR / Gitへ到達する前に
// attached-value、scope、occurrence、targetを一つのrequestへ確定する。
PkgbuildExportInvocation require_pkgbuild_export_invocation(
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

// exactなoperation-local selectorだけをsemantic requestとして扱う。
// option valueや`--`後の同じ綴りはrequestへ昇格させない。
bool local_source_build_requested(const ParsedCliArguments& parsed);
// parse済みargvだけを参照し、process / filesystem / networkへ到達しない。
// sync routeはdry-runで意味を維持できる明示的な-S grammarだけを受理する。
DryRunOperation classify_dry_run_operation(
        const ParsedCliArguments& parsed);
// local source buildがrequestedなinvocationだけを受け、directory accessより前に
// option / operand grammarとordered environment assignmentを確定する。
LocalSourceBuildInvocation require_local_source_build_invocation(
        const ParsedCliArguments& parsed);
