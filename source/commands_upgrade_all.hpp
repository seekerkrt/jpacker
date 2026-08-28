#pragma once

#include <string>
#include <vector>

struct AppConfig;
struct ParsedCliArguments;

// upgrade-all固有のtarget-less CLI契約をdefault log/cache初期化前に検証する。
std::vector<std::string> validate_upgrade_all_invocation(
    const ParsedCliArguments& parsed);

// system/source/AUR aggregate operationをtyped APIから実行し、CLIへ表示する。
int cmd_upgrade_all(const AppConfig& config);
