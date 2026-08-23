#pragma once

#include "aur_update_plan.hpp"
#include "package_metadata.hpp"

#include <string>
#include <vector>

// 継続可能なquery失敗を、external exception型から切り離したowned diagnostic。
struct AurUpdateQueryFailure {
    std::vector<std::string> package_names;
    std::string              diagnostic;
};

// 1 invocation分のread-only query結果。presentationや終了statusの判断はcallerが所有する。
struct AurUpdateQueryResult {
    AurUpdatePlan                      plan;
    std::vector<AurUpdateQueryFailure> recoverable_failures;
};

AurUpdateQueryResult query_installed_aur_updates();
AurUpdateQueryResult query_aur_updates_for_foreign_inventory(
        ForeignPackageInventory installed_packages);
