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

void print_packages(const std::vector<AurPackageInfo>& packages) {
    for(const auto& package : packages) {
        std::cout << package.Name << '|' << package.PackageBase << '|'
                  << package.Version << '\n';
    }
}

void print_info_map(const std::map<std::string, AurPackageInfo>& packages) {
    for(const auto& [name, package] : packages) {
        static_cast<void>(package);
        std::cout << name << '\n';
    }
}

#ifdef MOGUET_ENABLE_AUR_RPC_TEST_HOOKS
void test_write_callback_contract() {
    char        payload[] = "abc";
    std::string buffer = "prefix:";

    set_aur_rpc_write_append_failure_for_test(false);
    const std::size_t success_result =
            invoke_aur_rpc_write_callback_for_test(
                    payload, 1, 3, buffer);
    if(success_result != 3 || buffer != "prefix:abc") {
        throw std::runtime_error(
                "AUR write callback changed successful append behavior.");
    }

    buffer = "unchanged";
    set_aur_rpc_write_append_failure_for_test(true);
    const std::size_t failure_result =
            invoke_aur_rpc_write_callback_for_test(
                    payload, 1, 3, buffer);
    const std::size_t zero_byte_failure_result =
            invoke_aur_rpc_write_callback_for_test(
                    payload, 0, 0, buffer);
    set_aur_rpc_write_append_failure_for_test(false);
    if(failure_result == 3 || zero_byte_failure_result == 0) {
        throw std::runtime_error(
                "AUR write callback did not return a libcurl write failure.");
    }
    if(buffer != "unchanged") {
        throw std::runtime_error(
                "AUR write callback treated an append exception as partial success.");
    }
    std::cout << "write-callback-contract-ok\n";
}
#endif

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
        } else if(operation == "search-strict") {
            print_packages(AurClient::search_strict(subject));
        } else if(operation == "provides-strict") {
            print_names(AurClient::search_names_by_provides_strict(subject));
        } else if(operation == "provides-legacy") {
            print_names(AurClient::search_names_by_provides(subject));
#ifdef MOGUET_ENABLE_AUR_RPC_TEST_HOOKS
        } else if(operation == "write-callback-contract") {
            test_write_callback_contract();
        } else if(operation == "write-failure-strict") {
            set_aur_rpc_write_append_failure_for_test(true);
            print_info(AurClient::info_strict(subject));
        } else if(operation == "search-encode-failure-strict") {
            set_aur_rpc_encode_failure_search_query_for_test(subject);
            print_packages(AurClient::search_strict(subject));
        } else if(operation == "info-many-normal") {
            print_info_map(AurClient::info_many(
                    {"valid-minimal", "arrays-null"}));
        } else if(operation == "info-many-fail-first") {
            set_aur_rpc_encode_failure_package_for_test("valid-minimal");
            print_info_map(AurClient::info_many(
                    {"valid-minimal", "arrays-null"}));
        } else if(operation == "info-many-fail-middle") {
            set_aur_rpc_encode_failure_package_for_test("arrays-null");
            print_info_map(AurClient::info_many(
                    {"valid-minimal", "arrays-null", "arrays-empty"}));
#endif
        } else {
            throw std::invalid_argument("unknown test operation: " + operation);
        }
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
