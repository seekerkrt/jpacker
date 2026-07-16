#include "source_preference.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>

// source-build preferenceのroot、entry path、read/parse、raw enumerationを所有する。
// POLICY: mutation、directory作成、Logger、command handler policyはconsumer側に残す。
namespace {

// POLICY: test overrideもproduction rootもmain前にprocessごとに一度だけcaptureする。
#ifdef JPACKER_ENABLE_TEST_OVERRIDES
const std::string PACKAGE_BUILD_DIR = [] {
    const char* test_package_build_dir = std::getenv("JPACKER_TEST_PACKAGE_BUILD_DIR");
    if(test_package_build_dir && test_package_build_dir[0] != '\0') {
        return std::string(test_package_build_dir);
    }
    return std::string("/etc/jpacker/package.build");
}();
#else
const std::string PACKAGE_BUILD_DIR = "/etc/jpacker/package.build";
#endif

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

std::string strip_comment(const std::string& line) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for(size_t i = 0; i < line.length(); ++i) {
        char ch = line[i];
        if(escaped) {
            escaped = false;
            continue;
        }
        if(ch == '\\' && in_double_quote) {
            escaped = true;
            continue;
        }
        if(ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if(ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }
        if(ch == '#' && !in_single_quote && !in_double_quote) {
            return line.substr(0, i);
        }
    }
    return line;
}

bool is_valid_env_key(const std::string& key) {
    if(key.empty()) return false;
    if(!(std::isalpha(static_cast<unsigned char>(key[0])) || key[0] == '_')) return false;
    return std::all_of(key.begin() + 1, key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

std::string expand_config_vars(
        std::string value, const std::map<std::string, std::string>& variables) {
    std::regex  brace_pattern(R"(\$\{([A-Za-z0-9_]+)\})");
    std::regex  simple_pattern(R"(\$([A-Za-z0-9_]+))");
    std::smatch match;

    for(int i = 0; i < 32 && std::regex_search(value, match, brace_pattern); ++i) {
        std::string variable_name = match[1];
        std::string replacement =
                variables.count(variable_name) ? variables.at(variable_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == value) break;
        value = next;
    }
    for(int i = 0; i < 32 && std::regex_search(value, match, simple_pattern); ++i) {
        std::string variable_name = match[1];
        std::string replacement =
                variables.count(variable_name) ? variables.at(variable_name) : "";
        std::string next = match.prefix().str() + replacement + match.suffix().str();
        if(next == value) break;
        value = next;
    }
    return value;
}

std::string shell_quote(const std::string& str) {
    std::string quoted = "'";
    for(char ch : str) {
        if(ch == '\'')
            quoted += "'\\''";
        else
            quoted += ch;
    }
    quoted += "'";
    return quoted;
}

} // namespace

std::filesystem::path source_preference_root() {
    return PACKAGE_BUILD_DIR;
}

std::filesystem::path source_preference_entry_path(const std::string& package_name) {
    require_valid_package_name(package_name);
    return std::filesystem::path(PACKAGE_BUILD_DIR) / package_name;
}

std::filesystem::directory_iterator source_preference_entries() {
    return std::filesystem::directory_iterator(PACKAGE_BUILD_DIR);
}

bool is_force_source(const std::string& package_name) {
    return std::filesystem::exists(source_preference_entry_path(package_name));
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

std::string get_package_env(
        const std::string& package_name, SourcePreferenceLoadHandler on_load,
        SourcePreferenceWarningHandler on_warning) {
    std::filesystem::path entry_path = source_preference_entry_path(package_name);
    if(!std::filesystem::exists(entry_path)) return "";

    std::ifstream                      file(entry_path);
    std::string                        line;
    std::string                        environment;
    std::map<std::string, std::string> variables;
    if(on_load) on_load(entry_path);

    while(std::getline(file, line)) {
        line = strip_comment(line);
        if(trim(line).empty()) continue;

        std::string key, value;
        if(split_env_assignment(line, key, value)) {
            try {
                value = expand_config_vars(value, variables);
            } catch(const std::exception& error) {
                if(on_warning) {
                    on_warning(
                            "Failed to expand variables for " + key + ": " + error.what());
                }
            }
            variables[key] = value;
            if(!value.empty()) {
                environment += key + "=" + shell_quote(value) + " ";
            }
        } else if(line.find('=') != std::string::npos && on_warning) {
            on_warning("Ignoring invalid environment assignment: " + trim(line));
        }
    }
    return environment;
}

void read_source_preference_entry(
        const std::filesystem::path& entry_path, SourcePreferenceLineHandler on_line) {
    std::ifstream file(entry_path);
    std::string   line;
    while(std::getline(file, line)) {
        std::string display_line = trim(strip_comment(line));
        if(!display_line.empty() && on_line) on_line(display_line);
    }
}
