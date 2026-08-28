#include "package_identifier.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_valid_package_name(const std::string& name) {
    if(!is_valid_package_name(name)) {
        throw std::runtime_error("Expected valid package name: " + name);
    }
    require_valid_package_name(name);
}

void expect_invalid_package_name(const std::string& name) {
    if(is_valid_package_name(name)) {
        throw std::runtime_error("Expected invalid package name: " + name);
    }

    try {
        require_valid_package_name(name);
    } catch(const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("require_valid_package_name accepted: " + name);
}

} // namespace

int main() {
    try {
        const std::vector<std::string> valid_names = {
            "package",
            "foo.bar",
            "lib32.foo",
            "name.with.multiple.dots",
            "foo..bar",
            ".hidden",
            "...",
            "pkg@name_2+extra-value",
        };
        for(const auto& name : valid_names)
            expect_valid_package_name(name);

        const std::vector<std::string> invalid_names = {
            "",
            ".",
            "..",
            "-leading-hyphen",
            "invalid/name",
            "bad name",
            "name=version",
        };
        for(const auto& name : invalid_names)
            expect_invalid_package_name(name);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "package identifier tests: all checks passed\n";
    return 0;
}
