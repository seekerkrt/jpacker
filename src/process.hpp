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
    // Borrowed directory descriptor. The child changes directory after fork
    // and before exec; the caller retains ownership for the whole call.
    std::optional<int> working_directory_fd = std::nullopt;
    // Borrowed input descriptor. The child binds it to stdin after fork; the
    // caller retains ownership for the whole call and current offset is used.
    std::optional<int> standard_input_fd = std::nullopt;
    // Borrowed descriptor whose open-file-description must outlive this
    // mutator even if the caller or supervisor dies. run_explicit_process()
    // duplicates it into both a non-execing subreaper supervisor and the
    // executed mutator tree. An unexpected long-lived descendant therefore
    // retains the guard fail-closed until that descendant terminates.
    std::optional<int> parent_independent_lifetime_guard_fd = std::nullopt;
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
