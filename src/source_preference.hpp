#pragma once

#include <filesystem>
#include <string>

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

// cmd_build / cmd_add_src / preference file readが共有する既存assignment syntax。
bool split_env_assignment(
        const std::string& assignment, std::string& key, std::string& value);

std::string get_package_env(
        const std::string& package_name, SourcePreferenceLoadHandler on_load,
        SourcePreferenceWarningHandler on_warning);

// list-src向けに、quote-aware comment除去後のnonblank lineをread順で通知する。
void read_source_preference_entry(
        const std::filesystem::path& entry_path, SourcePreferenceLineHandler on_line);
