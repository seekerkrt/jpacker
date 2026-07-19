#pragma once

#include <filesystem>
#include <string>

// CLI 表示と log file 出力をまとめる薄い logger。
class Logger {
public:
    static void set_diagnostics_to_stderr();
    static void init(const std::filesystem::path& path);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void raw_cmd(const std::string& cmd);
};
