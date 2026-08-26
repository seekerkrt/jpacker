#pragma once

#include "filtered_aur_update_operation.hpp"

struct AppConfig;

// Query/static preflight remains before the default state log. Only a
// prepared executable capability may cross into the normal mutation route.
PreparedFilteredAurUpdateOperation prepare_upgrade_aur_operation(
        const AppConfig& config);

// Consume the prepared capability, execute only when ready, and present the
// owned typed result for executable, blocked, and no-op outcomes.
int cmd_upgrade_aur(
        PreparedFilteredAurUpdateOperation prepared,
        const AppConfig& config);
