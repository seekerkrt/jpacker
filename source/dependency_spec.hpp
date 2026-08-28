#pragma once

#include <optional>
#include <string>

// AUR dependency string の raw/name/operator/version を失わないための最小表現。
// POLICY: v1.x では version compare は行わず、constraint の検出と表示だけを担当する。
struct ParsedDependency {
    std::string raw;
    std::string name;
    std::optional<std::string> op;
    std::optional<std::string> version;

    bool has_constraint() const {
        return op.has_value();
    }

    bool has_parseable_constraint() const {
        return op.has_value() && version.has_value() && !version->empty() &&
               version->find_first_of("<>=") != 0;
    }

    bool has_malformed_constraint() const {
        return has_constraint() && !has_parseable_constraint();
    }
};

ParsedDependency parse_dependency_string(const std::string& dependency);
std::string dependency_package_name(const std::string& dependency);
std::string provided_dependency_name(const std::string& provided);
std::string dependency_constraint_note(const std::string& dependency);
std::string dependency_constraint_unresolved_reason(const std::string& dependency);
std::string dependency_display_with_constraint_note(const std::string& display, const std::string& dependency);
