#pragma once

#include <string>
#include <vector>

// source-buildへ渡すassignmentは、shell文字列へ直列化するまで入力順とemptyを保持する。
struct SourceEnvironmentAssignment {
    std::string key;
    std::string value;
};

struct SourceBuildEnvironment {
    std::vector<SourceEnvironmentAssignment> ordered_assignments;

    bool defines(const std::string& key) const;

    // POLICY(#242): requested/PackageBase fallbackは、従来どおりnonempty assignmentの
    // 有無で決める。empty definitionの存在判定にはdefines()を使う。
    bool has_forwarded_nonempty_assignment() const;
};

// CLI buildはemptyをforwardし、source preferenceはemptyを省く既存契約を表す。
enum class SourceEnvironmentEmptyValuePolicy {
    Omit,
    Forward,
};

// cmd_build / cmd_add_src / preference file readが共有する既存assignment syntax。
bool split_env_assignment(
    const std::string& assignment, std::string& key, std::string& value);

// Empty policy適用後のordered assignmentを、shell textではない1 argv word
// (`KEY=value`)ずつへmaterializeする。orderとduplicateは保持する。
std::vector<std::string> materialize_source_build_environment_assignment_words(
    const SourceBuildEnvironment& environment,
    SourceEnvironmentEmptyValuePolicy empty_value_policy);

// makepkg command environmentとuser-facing表示が共有するserializer。
// 末尾spaceも既存表示契約に含む。
std::string serialize_source_build_environment(
    const SourceBuildEnvironment& environment,
    SourceEnvironmentEmptyValuePolicy empty_value_policy);
