#pragma once

#include <string>

// shell経由で実行したcommandのstdoutとdecode済み終了status。
struct CapturedCommandResult {
    std::string output;
    int         exit_code = 127;
};

CapturedCommandResult capture_command_output(const char* cmd);
std::string exec_command(const char* cmd);
int command_status(const std::string& cmd);
int run_command(const std::string& cmd);
