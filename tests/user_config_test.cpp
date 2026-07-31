#include "user_config.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

void print_config(const UserConfig& config) {
    std::cout << "SCHEMA_VERSION=" << config.schema_version << '\n'
              << "REVIEW_PKGBUILD="
              << review_policy_name(config.review.pkgbuild) << '\n'
              << "REVIEW_DIFF=" << review_policy_name(config.review.diff)
              << '\n'
              << "BUILD_MODE=" << build_mode_name(config.build.mode) << '\n';
}

int run_test_driver(int argc, char* argv[]) {
    if(argc == 2 && std::string(argv[1]) == "defaults") {
        print_config(UserConfig{});
        return 0;
    }

    if(argc == 3 && std::string(argv[1]) == "load") {
        print_config(load_user_config(std::filesystem::path(argv[2])));
        return 0;
    }

    std::cerr << "usage: user-config-test defaults | load <path>" << std::endl;
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
