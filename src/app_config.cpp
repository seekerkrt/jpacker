#include "app_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

const std::filesystem::path CONFIG_FILE = "/etc/jpacker/jpacker.conf";

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return str;
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

} // namespace

AppConfig load_app_config(const std::filesystem::path& config_path) {
    AppConfig config;
    if(!std::filesystem::exists(config_path)) return config;

    std::ifstream file(config_path);
    std::string   line;
    while(std::getline(file, line)) {
        line = strip_comment(line);
        if(trim(line).empty()) continue;
        std::stringstream ss(line);
        std::string       key, val;
        if(std::getline(ss, key, '=') && std::getline(ss, val)) {
            key = to_lower(trim(key));
            val = unquote(trim(val));
            if(key == "noedit") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") config.no_edit = true;
            } else if(key == "nodiff") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") config.no_diff = true;
            } else if(key == "rmdeps") {
                std::string v = to_lower(val);
                if(v == "true" || v == "1" || v == "yes") config.rm_deps = true;
            } else if(key == "editor") {
                if(!val.empty()) config.editor = val;
            } else if(key == "logfile") {
                if(!val.empty()) config.log_file = val;
            }
        }
    }
    return config;
}

AppConfig load_default_app_config() {
    return load_app_config(CONFIG_FILE);
}

std::filesystem::path expand_config_path(const std::string& path) {
    if(path.empty()) return "";
    if(path[0] == '~') {
        const char* home = std::getenv("HOME");
        if(!home) throw std::runtime_error("HOME environment variable not set.");
        if(path.length() == 1) return std::filesystem::path(home);
        if(path[1] == '/') return std::filesystem::path(home) / path.substr(2);
        throw std::runtime_error("Unsupported home expansion: " + path);
    }
    return std::filesystem::path(path);
}
