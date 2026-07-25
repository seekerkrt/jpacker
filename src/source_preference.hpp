#pragma once

#include "source_environment.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

// source preferenceのread時messageはconsumerが表示し、moduleはLoggerへ依存しない。
using SourcePreferenceLoadHandler = void (*)(const std::filesystem::path& path);
using SourcePreferenceLineHandler = void (*)(const std::string& line);
using SourcePreferenceWarningHandler = void (*)(const std::string& warning);

// Rootはsource_preference.cppのprocess-lifetime定数からread-only copyを返す。
std::filesystem::path source_preference_root();
std::filesystem::path source_preference_entry_path(const std::string& package_name);

// POLICY: sortやpackage validationを加えず、consumer固有の列挙policyを維持する。
std::filesystem::directory_iterator source_preference_entries();

bool is_force_source(const std::string& package_name);

struct SourcePreferenceAbsent {
    bool operator==(const SourcePreferenceAbsent&) const = default;
};

struct SourcePreferenceLoaded {
    std::filesystem::path entry_path;
    SourceBuildEnvironment environment;
    std::vector<std::string> warnings;
};

enum class SourcePreferenceFailureKind {
    StatusUnavailable,
    UnsupportedFileType,
    OpenFailed,
    ReadFailed,
};

// Status/Open/Read failureはsystem_error、unsupported typeはobserved_file_typeを正本にする。
struct SourcePreferenceFailure {
    SourcePreferenceFailureKind kind;
    std::filesystem::path entry_path;
    std::optional<std::error_code> system_error;
    std::optional<std::filesystem::file_type> observed_file_type;
    // diagnosticは表示用の補助情報であり、control flowはkindとtyped fieldで行う。
    std::string diagnostic;
};

using StrictSourcePreferenceResult = std::variant<
        SourcePreferenceAbsent,
        SourcePreferenceLoaded,
        SourcePreferenceFailure>;

// Absentはentryが実在しない場合だけ。regular fileは全byte read後にowned resultへparseする。
StrictSourcePreferenceResult read_source_preference_strict(
        const std::string& package_name);

SourceBuildEnvironment get_package_env(
        const std::string& package_name, SourcePreferenceLoadHandler on_load,
        SourcePreferenceWarningHandler on_warning);

// list-src向けに、quote-aware comment除去後のnonblank lineをread順で通知する。
void read_source_preference_entry(
        const std::filesystem::path& entry_path, SourcePreferenceLineHandler on_line);

#ifdef JPACKER_ENABLE_SOURCE_PREFERENCE_TEST_HOOKS
enum class SourcePreferenceTestFailurePoint {
    Status,
    Open,
    Read,
};

// Strict readerの一回限りのfailureだけを注入し、production IO APIは差し替えない。
void fail_next_source_preference_operation_for_test(
        const std::string& package_name,
        SourcePreferenceTestFailurePoint failure_point);
void reset_source_preference_test_hooks();
#endif
