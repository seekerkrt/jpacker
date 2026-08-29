#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// processのstdoutとdecode済み終了status。bounded captureでは超過分を保持しない。
struct CapturedCommandResult {
    std::string output;
    int exit_code = 127;
    bool stdout_capture_limit_exceeded = false;
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

// The bounded companion requires an explicit cwd and stdin descriptor. It
// owns only the child lifecycle; all descriptors in the invocation remain
// borrowed from the caller for the duration of the call.
struct BoundedProcessPolicy {
    std::chrono::milliseconds hard_timeout;
    std::chrono::milliseconds termination_grace;
    std::size_t stdout_capture_limit;
    bool suppress_standard_error = true;
};

enum class BoundedProcessLaunchStage {
    InvocationValidation,
    StandardOutputPipe,
    ExecStatusPipe,
    SignalMask,
    SignalDescriptor,
    Subreaper,
    Fork,
    ParentProcessGroup,
    ChildProcessGroup,
    ParentDeathSignal,
    ChildSignalMask,
    WorkingDirectory,
    StandardInput,
    StandardOutput,
    StandardError,
    DescriptorHygiene,
    Execve,
};

enum class BoundedProcessIoStage {
    StandardOutputNonblocking,
    ExecStatusNonblocking,
    Poll,
    StandardOutputRead,
    ExecStatusRead,
    SignalRead,
    Wait,
    ProcessGroupSignal,
    ProcessGroupObservation,
    SignalMaskRestore,
    SubreaperRestore,
};

struct BoundedProcessExited {
    int exit_code;

    bool operator==(const BoundedProcessExited&) const = default;
};

struct BoundedProcessSignaled {
    int signal_number;

    bool operator==(const BoundedProcessSignaled&) const = default;
};

struct BoundedProcessLaunchOrSetupFailure {
    BoundedProcessLaunchStage stage;
    int error_number;

    bool operator==(
        const BoundedProcessLaunchOrSetupFailure&) const = default;
};

struct BoundedProcessIoOrWaitFailure {
    BoundedProcessIoStage stage;
    int error_number;

    bool operator==(
        const BoundedProcessIoOrWaitFailure&) const = default;
};

struct BoundedProcessTimedOut {
    bool operator==(const BoundedProcessTimedOut&) const = default;
};

struct BoundedProcessCaptureLimitExceeded {
    std::size_t capture_limit;

    bool operator==(
        const BoundedProcessCaptureLimitExceeded&) const = default;
};

using BoundedProcessOutcome = std::variant<
    BoundedProcessExited,
    BoundedProcessSignaled,
    BoundedProcessLaunchOrSetupFailure,
    BoundedProcessIoOrWaitFailure,
    BoundedProcessTimedOut,
    BoundedProcessCaptureLimitExceeded>;

struct BoundedCapturedProcessResult {
    std::string output;
    BoundedProcessOutcome outcome;
};

CapturedCommandResult capture_command_output(const char* cmd);

// stdoutの境界whitespaceにも意味があるparser向け。exit statusのdecode契約は上と同じ。
CapturedCommandResult capture_command_output_raw(const char* cmd);

CapturedCommandResult capture_explicit_process_output_raw(
    const ExplicitProcessInvocation& invocation,
    bool suppress_standard_error = false);

// Runs one shell-free process in a dedicated process group. The monotonic hard
// deadline is absolute for the whole child tree and is never extended by
// stdout activity. Timeout and capture overflow terminate the group with
// SIGTERM, wait only termination_grace, then escalate to SIGKILL. stderr is
// either inherited or redirected to /dev/null and is never captured here.
// The direct child also receives a Linux parent-death SIGKILL. Normal
// same-group descendants are reaped/removed before return; an executable that
// deliberately escapes with setsid() is outside this fixed-program contract.
BoundedCapturedProcessResult capture_bounded_explicit_process_output_raw(
    const ExplicitProcessInvocation& invocation,
    const BoundedProcessPolicy& policy);

int run_explicit_process(
    const ExplicitProcessInvocation& invocation,
    bool suppress_standard_output = false,
    bool suppress_standard_error = false);

std::string exec_command(const char* cmd);
int command_status(const std::string& cmd);
int run_command(const std::string& cmd);

// std::system()-compatible shell/environment/cwd semantics with the same
// parent-independent lifetime guard used by exact Git mutators.
int run_command_with_parent_independent_lifetime_guard(
    const std::string& command,
    int lifetime_guard_fd,
    const std::string& display_command = {});

// source_fdはcaller所有のままborrowし、childだけがcurrent offsetからstdinとして読む。
int run_command_with_stdin_fd(const std::string& command, int source_fd);
