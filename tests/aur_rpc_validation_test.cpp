#include "aur_rpc.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_info(const std::optional<AurPackageInfo>& info) {
    if(!info.has_value()) {
        std::cout << "not-found\n";
        return;
    }
    std::cout << info->Name << '\n';
}

void print_names(const std::vector<std::string>& names) {
    for(const auto& name : names) std::cout << name << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    if(argc != 3) {
        std::cerr << "usage: aur-rpc-validation-test <operation> <subject>\n";
        return 2;
    }

    CurlGlobal       curl_global;
    const std::string operation = argv[1];
    const std::string subject = argv[2];
    try {
        if(operation == "info-strict") {
            print_info(AurClient::info_strict(subject));
        } else if(operation == "info-legacy") {
            print_info(AurClient::info(subject));
        } else if(operation == "provides-strict") {
            print_names(AurClient::search_names_by_provides_strict(subject));
        } else if(operation == "provides-legacy") {
            print_names(AurClient::search_names_by_provides(subject));
        } else {
            throw std::invalid_argument("unknown test operation: " + operation);
        }
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
