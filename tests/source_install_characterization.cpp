#if __has_include("source_install.hpp")
#include "dependency_plan.hpp"
#include "source_install.hpp"
#define JPACKER_HAS_EXTRACTED_SOURCE_INSTALL 1
#else
#define JPACKER_HAS_EXTRACTED_SOURCE_INSTALL 0
#endif

// POLICY(#203): 抽出前後で同じscenarioをdirect実行し、shared orchestrationの挙動を比較する。
#define main jpacker_program_main
#include "../src/jpacker.cpp"
#undef main

#include <exception>
#include <iostream>
#include <string>

namespace {

AppConfig characterization_config() {
    AppConfig config;
    config.no_edit = true;
    config.no_diff = true;
    config.no_confirm = true;
    return config;
}

BuildPlan two_entry_plan() {
    BuildPlan plan;
    plan.order.push_back(BuildPlanEntry{"dep-base", {"dep-target"}});
    plan.order.push_back(BuildPlanEntry{"root-base", {"root-target"}});
    return plan;
}

BuildPlan fallback_plan() {
    BuildPlan plan;
    plan.order.push_back(BuildPlanEntry{"base-target", {"requested-target"}});
    return plan;
}

void execute_characterized_plan(
        const BuildPlan& plan, bool use_source_build_preferences, bool needed,
        const AppConfig& config) {
#if JPACKER_HAS_EXTRACTED_SOURCE_INSTALL
    execute_aur_build_plan(plan, use_source_build_preferences, needed, config);
#else
    static_cast<void>(config);
    SourceSyncOptions source_sync_options;
    source_sync_options.needed = needed;
    execute_aur_build_plan(plan, use_source_build_preferences, source_sync_options);
#endif
}

void install_characterized_smart_source(
        const std::string& package_name, bool only_if_updated, bool needed,
        const AppConfig& config) {
#if JPACKER_HAS_EXTRACTED_SOURCE_INSTALL
    install_smart_source(package_name, only_if_updated, needed, config);
#else
    static_cast<void>(config);
    SourceSyncOptions source_sync_options;
    source_sync_options.needed = needed;
    install_smart_source(package_name, only_if_updated, source_sync_options);
#endif
}

int run_scenario(const std::string& scenario, const AppConfig& config) {
    if(scenario == "plan-success" || scenario == "plan-failure") {
        execute_characterized_plan(two_entry_plan(), false, true, config);
        return 0;
    }
    if(scenario == "fallback") {
        execute_characterized_plan(fallback_plan(), true, false, config);
        return 0;
    }
    if(scenario == "smart-source") {
        install_characterized_smart_source("clean-root", false, false, config);
        return 0;
    }
    if(scenario == "smart-source-missing-post-snapshot") {
        install_characterized_smart_source("clean-root", true, false, config);
        return 0;
    }

    std::cerr << "Unknown source-install characterization scenario: " << scenario << '\n';
    return 2;
}

} // namespace

int main(int argc, char* argv[]) {
    if(argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <plan-success|plan-failure|fallback|smart-source|"
                     "smart-source-missing-post-snapshot>\n";
        return 2;
    }

    AppConfig config = characterization_config();
#if !JPACKER_HAS_EXTRACTED_SOURCE_INSTALL
    // 抽出前実装はrunner-private configを参照するため、比較用processへ同じ設定をpublishする。
    g_config = config;
#endif

    try {
        return run_scenario(argv[1], config);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
