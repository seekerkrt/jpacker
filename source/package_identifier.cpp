#include "package_identifier.hpp"

#include "localization.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

bool is_valid_package_name(const std::string& name) {
    // POLICY: package identifierはpath componentにもなるため、"."と".."は常に拒否する。
    if(name.empty() || name == "." || name == ".." || name[0] == '-') return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '@' || ch == '.' || ch == '_' || ch == '+' || ch == '-';
    });
}

void require_valid_package_name(const std::string& name) {
    if(!is_valid_package_name(name)) {
        throw std::runtime_error(
                localization::format_translated_message(
                        // TRANSLATORS: The placeholder is a package identity.
                        "Invalid package name: {}", name));
    }
}
