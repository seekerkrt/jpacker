#include "package_identifier.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

bool is_valid_package_name(const std::string& name) {
    if(name.empty() || name[0] == '-') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '@' || ch == '.' || ch == '_' || ch == '+' || ch == '-';
    });
}

void require_valid_package_name(const std::string& name) {
    if(!is_valid_package_name(name)) {
        throw std::runtime_error("Invalid package name: " + name);
    }
}
