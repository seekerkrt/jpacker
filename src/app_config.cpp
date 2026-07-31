#include "app_config.hpp"

#include <utility>

AppConfig make_app_config(
        UserConfig final_user_config, bool no_confirm, bool rm_deps) {
    return AppConfig{
            std::move(final_user_config), no_confirm, rm_deps, "nano"};
}
