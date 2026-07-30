#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// processのstdoutとdecode済み終了status。bounded captureでは超過分を保持しない。
struct CapturedCommandResult {
    std::string output;
    int         exit_code = 127;
    bool        stdout_capture_limit_exceeded = false;
};

// shell/environment inheritanceを通さずexecve(2)へ渡すcommand境界。
// argumentsはargv[1]以降、environmentはNAME=value形式の完全なchild環境。
struct ExplicitProcessInvocation {
    std::string executable;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::optional<std::size_t> stdout_capture_limit = std::nullopt;
};

CapturedCommandResult capture_command_output(const char* cmd);

// stdoutの境界whitespaceにも意味があるparser向け。exit statusのdecode契約は上と同じ。
CapturedCommandResult capture_command_output_raw(const char* cmd);

CapturedCommandResult capture_explicit_process_output_raw(
        const ExplicitProcessInvocation& invocation,
        bool suppress_standard_error = false);

int run_explicit_process(
        const ExplicitProcessInvocation& invocation,
        bool suppress_standard_output = false,
        bool suppress_standard_error = false);

std::string exec_command(const char* cmd);
int command_status(const std::string& cmd);
int run_command(const std::string& cmd);

// source_fdはcaller所有のままborrowし、childだけがcurrent offsetからstdinとして読む。
int run_command_with_stdin_fd(const std::string& command, int source_fd);
