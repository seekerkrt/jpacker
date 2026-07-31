#include "../src/app_config.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::string_view review_policy_name(ReviewPolicy policy) {
    switch(policy) {
        case ReviewPolicy::Prompt:
            return "prompt";
        case ReviewPolicy::Skip:
            return "skip";
    }
    throw std::runtime_error("Unexpected ReviewPolicy value.");
}

std::string_view build_mode_name(BuildMode mode) {
    switch(mode) {
        case BuildMode::Normal:
            return "normal";
        case BuildMode::Rebuild:
            return "rebuild";
        case BuildMode::Clean:
            return "clean";
    }
    throw std::runtime_error("Unexpected BuildMode value.");
}

void print_config(const AppConfig& config) {
    std::cout << "SCHEMA_VERSION=" << config.user_config.schema_version << '\n'
              << "REVIEW_PKGBUILD="
              << review_policy_name(config.user_config.review.pkgbuild) << '\n'
              << "REVIEW_DIFF="
              << review_policy_name(config.user_config.review.diff) << '\n'
              << "BUILD_MODE="
              << build_mode_name(config.user_config.build.mode) << '\n'
              << "NOCONFIRM=" << (config.no_confirm ? "true" : "false") << '\n'
              << "RMDEPS=" << (config.rm_deps ? "true" : "false") << '\n'
              << "EDITOR=" << config.editor << '\n';
}

int run_test_driver(int argc, char* argv[]) {
    if(argc == 2 && std::string(argv[1]) == "defaults") {
        print_config(AppConfig{});
        return 0;
    }

    if(argc == 2 && std::string(argv[1]) == "projection") {
        UserConfig final_user_config;
        final_user_config.review.pkgbuild = ReviewPolicy::Skip;
        final_user_config.review.diff = ReviewPolicy::Skip;
        final_user_config.build.mode = BuildMode::Clean;
        print_config(make_app_config(
                std::move(final_user_config), true, true));
        return 0;
    }

    std::cerr << "usage: app-config-test defaults | projection" << std::endl;
    return 2;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run_test_driver(argc, argv);
    } catch(const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
