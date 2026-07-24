#pragma once

#include <string>

// shell経由で実行したcommandのstdoutとdecode済み終了status。
struct CapturedCommandResult {
    std::string output;
    int         exit_code = 127;
};

CapturedCommandResult capture_command_output(const char* cmd);

// stdoutの境界whitespaceにも意味があるparser向け。exit statusのdecode契約は上と同じ。
CapturedCommandResult capture_command_output_raw(const char* cmd);

std::string exec_command(const char* cmd);
int command_status(const std::string& cmd);
int run_command(const std::string& cmd);

// source_fdはcaller所有のままborrowし、childだけがcurrent offsetからstdinとして読む。
int run_command_with_stdin_fd(const std::string& command, int source_fd);
