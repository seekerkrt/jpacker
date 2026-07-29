#pragma once

#include <filesystem>
#include <string>

// legacy jpacker.conf と runner の CLI override 合成後に、1回の実行で参照する設定状態。
struct AppConfig {
    bool        no_edit = false;
    bool        no_diff = false;
    bool        no_confirm = false;
    bool        rebuild = false;
    bool        clean_build = false;
    bool        rm_deps = false;
    std::string editor = "nano";
    std::string log_file = "";
};

// 明示path loaderはproduction defaultとtest fixtureで同じconfig syntaxを共有する境界。
AppConfig load_app_config(const std::filesystem::path& config_path);
AppConfig load_default_app_config();

// config由来のpathだけに適用するhome directory展開。
std::filesystem::path expand_config_path(const std::string& path);
