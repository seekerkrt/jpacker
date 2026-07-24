#include "source_environment.hpp"

#include "shell_words.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string unquote(const std::string& str) {
    if(str.length() >= 2) {
        char first = str.front();
        char last = str.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return str.substr(1, str.length() - 2);
        }
    }
    return str;
}

bool is_valid_env_key(const std::string& key) {
    if(key.empty()) return false;
    if(!(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_')) return false;
    return std::all_of(key.begin() + 1, key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

} // namespace

bool SourceBuildEnvironment::defines(const std::string& key) const {
    return std::any_of(
            ordered_assignments.begin(), ordered_assignments.end(),
            [&key](const SourceEnvironmentAssignment& assignment) {
                return assignment.key == key;
            });
}

bool SourceBuildEnvironment::has_forwarded_nonempty_assignment() const {
    return std::any_of(
            ordered_assignments.begin(), ordered_assignments.end(),
            [](const SourceEnvironmentAssignment& assignment) {
                return !assignment.value.empty();
            });
}

bool split_env_assignment(
        const std::string& assignment, std::string& key, std::string& value) {
    size_t equals_position = assignment.find('=');
    if(equals_position == std::string::npos) return false;
    key = trim(assignment.substr(0, equals_position));
    value = unquote(trim(assignment.substr(equals_position + 1)));
    // POLICY: makepkgへ渡す環境変数名はshell identifier相当に制限する。
    return is_valid_env_key(key);
}

std::string serialize_source_build_environment(
        const SourceBuildEnvironment& environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy) {
    std::string serialized;
    for(const auto& assignment : environment.ordered_assignments) {
        if(assignment.value.empty() &&
           empty_value_policy == SourceEnvironmentEmptyValuePolicy::Omit) {
            continue;
        }
        serialized += assignment.key + "=" + shell_words::quote(assignment.value) + " ";
    }
    return serialized;
}
