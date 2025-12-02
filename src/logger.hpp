#pragma once
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class Logger {
private:
    static std::ofstream logFile;
    static bool          initialized;
    static fs::path      logPath;

    static std::string get_timestamp();

public:
    static void init(const fs::path& path);

    // 将来的にログファイル移動したい場合だけ使える安全API
    static void switch_log_file(const fs::path& path);

    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void raw_cmd(const std::string& cmd);
};
