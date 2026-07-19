#include "logging.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::ofstream log_file;
bool initialized = false;
bool diagnostics_to_stderr = false;

std::ostream& diagnostic_stream() {
    return diagnostics_to_stderr ? std::cerr : std::cout;
}

std::string get_timestamp() {
    auto              now = std::chrono::system_clock::now();
    auto              in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace

void Logger::set_diagnostics_to_stderr() {
    diagnostics_to_stderr = true;
}

void Logger::init(const std::filesystem::path& path) {
    if(path.has_parent_path() && !std::filesystem::exists(path.parent_path())) {
        std::filesystem::create_directories(path.parent_path());
    }
    if(log_file.is_open()) log_file.close();
    log_file.clear();
    log_file.open(path, std::ios::app);
    initialized = log_file.is_open();
}

void Logger::info(const std::string& msg) {
    diagnostic_stream() << "\033[1;32m::\033[0m " << msg << std::endl;
    if(initialized) log_file << "[" << get_timestamp() << "] [INFO] " << msg << std::endl;
}

void Logger::warn(const std::string& msg) {
    diagnostic_stream() << "\033[1;33m:: Warning:\033[0m " << msg << std::endl;
    if(initialized) log_file << "[" << get_timestamp() << "] [WARN] " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
    std::cerr << "\033[1;31m:: Error:\033[0m " << msg << std::endl;
    if(initialized) log_file << "[" << get_timestamp() << "] [ERROR] " << msg << std::endl;
}

void Logger::raw_cmd(const std::string& cmd) {
    diagnostic_stream() << "\033[1;33m::\033[0m Running: " << cmd << std::endl;
    if(initialized) log_file << "[" << get_timestamp() << "] [EXEC] " << cmd << std::endl;
}
