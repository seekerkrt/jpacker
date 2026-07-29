#include "dependency_spec.hpp"

namespace {

// dependency parser をmonolithへ逆依存させず、汎用utilityを公開しないためのlocal helper。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

} // namespace

ParsedDependency parse_dependency_string(const std::string& dependency) {
    ParsedDependency parsed;
    parsed.raw = trim(dependency);

    size_t pos = parsed.raw.find_first_of("<>=");
    if(pos == std::string::npos) {
        parsed.name = parsed.raw;
        return parsed;
    }

    parsed.name = trim(parsed.raw.substr(0, pos));
    if((parsed.raw[pos] == '<' || parsed.raw[pos] == '>') && pos + 1 < parsed.raw.size() &&
       parsed.raw[pos + 1] == '=') {
        parsed.op = parsed.raw.substr(pos, 2);
        parsed.version = trim(parsed.raw.substr(pos + 2));
    } else {
        parsed.op = parsed.raw.substr(pos, 1);
        parsed.version = trim(parsed.raw.substr(pos + 1));
    }

    return parsed;
}

std::string dependency_package_name(const std::string& dependency) {
    return parse_dependency_string(dependency).name;
}

std::string provided_dependency_name(const std::string& provided) {
    return dependency_package_name(provided);
}

std::string dependency_constraint_note(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(!parsed.has_parseable_constraint()) return "";
    return " [constraint: " + parsed.op.value() + " " + parsed.version.value() + ", not verified]";
}

std::string dependency_constraint_unresolved_reason(const std::string& dependency) {
    ParsedDependency parsed = parse_dependency_string(dependency);
    if(parsed.has_malformed_constraint()) return parsed.raw + " (invalid version constraint)";
    // POLICY(#96): dependency の version constraint は表示・警告まで。Moguet 側で比較解決しない。
    if(parsed.has_constraint()) return parsed.raw + " (version constraint is not verified)";
    return parsed.raw;
}

std::string dependency_display_with_constraint_note(const std::string& display, const std::string& dependency) {
    return display + dependency_constraint_note(dependency);
}
