#pragma once

struct AppConfig;

// Installed foreign inventoryからAUR update operationを実行し、typed resultを表示する。
int cmd_upgrade_aur(const AppConfig& config);
