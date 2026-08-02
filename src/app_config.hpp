#pragma once

#include "user_config.hpp"

#include <string>

// typed user configとinvocation-only optionを1回の実行で参照する境界。
struct AppConfig {
    UserConfig  user_config;
    bool        no_confirm = false;
    bool        rm_deps = false;
    std::string editor = "nano";
};

// load / composition済みのfinal configをproduction consumer向けに一度だけ束ねる。
AppConfig make_app_config(
        UserConfig final_user_config, bool no_confirm, bool rm_deps);
