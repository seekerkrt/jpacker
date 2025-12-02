#include "logger.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

std::ofstream Logger::logFile;
bool          Logger::initialized = false;
fs::path      Logger::logPath;

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// -------------------------------
// init() : 一度だけ初期化される
// -------------------------------
void Logger::init(const fs::path& path) {
    if(initialized) return;// ★ 安全：二重初期化禁止

    logPath = path;

    if(path.has_parent_path() && !fs::exists(path.parent_path())) {
        fs::create_directories(path.parent_path());
    }

    logFile.open(path, std::ios::app);
    initialized = logFile.is_open();
}

// -------------------------------
// 安全にログファイル切り替え
// -------------------------------
void Logger::switch_log_file(const fs::path& path) {
    // NOTE: 必要なときだけ使用する想定
    if(logFile.is_open()) logFile.close();

    logPath = path;

    if(path.has_parent_path() && !fs::exists(path.parent_path())) {
        fs::create_directories(path.parent_path());
    }

    logFile.open(path, std::ios::app);
    initialized = logFile.is_open();
}

// -------------------------------
void Logger::info(const std::string& msg) {
    std::cout << "\033[1;32m::\033[0m " << msg << std::endl;
    if(initialized)
        logFile << "[" << get_timestamp() << "] [INFO]  " << msg << "\n";
}

void Logger::warn(const std::string& msg) {
    std::cout << "\033[1;33m:: Warning:\033[0m " << msg << std::endl;
    if(initialized)
        logFile << "[" << get_timestamp() << "] [WARN]  " << msg << "\n";
}

void Logger::error(const std::string& msg) {
    std::cerr << "\033[1;31m:: Error:\033[0m " << msg << std::endl;
    if(initialized)
        logFile << "[" << get_timestamp() << "] [ERROR] " << msg << "\n";
}

void Logger::raw_cmd(const std::string& cmd) {
    std::cout << "\033[1;33m::\033[0m Running: " << cmd << std::endl;
    if(initialized)
        logFile << "[" << get_timestamp() << "] [EXEC]  " << cmd << "\n";
}
