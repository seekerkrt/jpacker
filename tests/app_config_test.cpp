#include "../src/app_config.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_config(const AppConfig& config) {
    std::cout << "NOEDIT=" << (config.no_edit ? "true" : "false") << '\n'
              << "NODIFF=" << (config.no_diff ? "true" : "false") << '\n'
              << "NOCONFIRM=" << (config.no_confirm ? "true" : "false") << '\n'
              << "REBUILD=" << (config.rebuild ? "true" : "false") << '\n'
              << "CLEANBUILD=" << (config.clean_build ? "true" : "false") << '\n'
              << "RMDEPS=" << (config.rm_deps ? "true" : "false") << '\n'
              << "EDITOR=" << config.editor << '\n'
              << "LOGFILE=" << config.log_file << '\n';
}

int run_test_driver(int argc, char* argv[]) {
    if(argc == 2 && std::string(argv[1]) == "defaults") {
        print_config(AppConfig{});
        return 0;
    }

    if(argc == 3 && std::string(argv[1]) == "load") {
        print_config(load_app_config(std::filesystem::path(argv[2])));
        return 0;
    }

    if(argc == 3 && std::string(argv[1]) == "expand") {
        std::cout << expand_config_path(argv[2]).string() << '\n';
        return 0;
    }

    std::cerr << "usage: app-config-test defaults | load <path> | expand <path>" << std::endl;
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
