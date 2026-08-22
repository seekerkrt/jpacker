#include "process.hpp"

#include "logging.hpp"
#include "localization.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace {

struct CommandSignalState {
    struct sigaction original_sigint {};
    struct sigaction original_sigquit {};
    sigset_t         original_mask {};
    bool             sigint_changed = false;
    bool             sigquit_changed = false;
    bool             mask_changed = false;
};

enum class SignalRestoreTarget {
    SigintDisposition,
    SigquitDisposition,
    SignalMask
};

enum class SignalRestoreContext {
    CommandSetupRollback,
    ExplicitProcessForkFailure,
    ExplicitProcessWait,
    StdinFdCommandForkFailure,
    StdinFdCommandWait
};

enum class CommandSignalContext {
    ExplicitProcess,
    StdinFdCommand
};

std::string signal_restore_failure_message(
        SignalRestoreTarget target, SignalRestoreContext context,
        int error_number) {
    const std::string_view error = std::strerror(error_number);
    switch(context) {
    case SignalRestoreContext::CommandSetupRollback:
        switch(target) {
        case SignalRestoreTarget::SigintDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition during command setup rollback: {}",
                    "SIGINT", error);
        case SignalRestoreTarget::SigquitDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition during command setup rollback: {}",
                    "SIGQUIT", error);
        case SignalRestoreTarget::SignalMask:
            return localization::format_translated_message(
                    "Failed to restore the signal mask during command setup rollback: {}",
                    error);
        }
        break;
    case SignalRestoreContext::ExplicitProcessForkFailure:
        switch(target) {
        case SignalRestoreTarget::SigintDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after an explicit-process fork failure: {}",
                    "SIGINT", error);
        case SignalRestoreTarget::SigquitDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after an explicit-process fork failure: {}",
                    "SIGQUIT", error);
        case SignalRestoreTarget::SignalMask:
            return localization::format_translated_message(
                    "Failed to restore the signal mask after an explicit-process fork failure: {}",
                    error);
        }
        break;
    case SignalRestoreContext::ExplicitProcessWait:
        switch(target) {
        case SignalRestoreTarget::SigintDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after waiting for an explicit process: {}",
                    "SIGINT", error);
        case SignalRestoreTarget::SigquitDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after waiting for an explicit process: {}",
                    "SIGQUIT", error);
        case SignalRestoreTarget::SignalMask:
            return localization::format_translated_message(
                    "Failed to restore the signal mask after waiting for an explicit process: {}",
                    error);
        }
        break;
    case SignalRestoreContext::StdinFdCommandForkFailure:
        switch(target) {
        case SignalRestoreTarget::SigintDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after a {}-file-descriptor command fork failure: {}",
                    "SIGINT", "stdin", error);
        case SignalRestoreTarget::SigquitDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after a {}-file-descriptor command fork failure: {}",
                    "SIGQUIT", "stdin", error);
        case SignalRestoreTarget::SignalMask:
            return localization::format_translated_message(
                    "Failed to restore the signal mask after a {}-file-descriptor command fork failure: {}",
                    "stdin", error);
        }
        break;
    case SignalRestoreContext::StdinFdCommandWait:
        switch(target) {
        case SignalRestoreTarget::SigintDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after waiting for a {}-file-descriptor command: {}",
                    "SIGINT", "stdin", error);
        case SignalRestoreTarget::SigquitDisposition:
            return localization::format_translated_message(
                    "Failed to restore the {} disposition after waiting for a {}-file-descriptor command: {}",
                    "SIGQUIT", "stdin", error);
        case SignalRestoreTarget::SignalMask:
            return localization::format_translated_message(
                    "Failed to restore the signal mask after waiting for a {}-file-descriptor command: {}",
                    "stdin", error);
        }
        break;
    }
    return localization::translate_message(
            "Failed to restore an unknown process signal state.");
}

void log_signal_setup_error(
        CommandSignalContext context, std::string_view action,
        int error_number) {
    const std::string_view error = std::strerror(error_number);
    if(context == CommandSignalContext::ExplicitProcess) {
        if(action == "SIGINT") {
            Logger::error(localization::format_translated_message(
                    "Failed to ignore {} while waiting for an explicit process: {}",
                    action, error));
        } else if(action == "SIGQUIT") {
            Logger::error(localization::format_translated_message(
                    "Failed to ignore {} while waiting for an explicit process: {}",
                    action, error));
        } else {
            Logger::error(localization::format_translated_message(
                    "Failed to block {} while waiting for an explicit process: {}",
                    action, error));
        }
        return;
    }
    if(action == "SIGINT") {
        Logger::error(localization::format_translated_message(
                "Failed to ignore {} while waiting for a {}-file-descriptor command: {}",
                action, "stdin", error));
    } else if(action == "SIGQUIT") {
        Logger::error(localization::format_translated_message(
                "Failed to ignore {} while waiting for a {}-file-descriptor command: {}",
                action, "stdin", error));
    } else {
        Logger::error(localization::format_translated_message(
                "Failed to block {} while waiting for a {}-file-descriptor command: {}",
                action, "stdin", error));
    }
}

bool restore_parent_signal_state(
        const CommandSignalState& state,
        SignalRestoreContext context) {
    int sigint_restore_error = 0;
    int sigquit_restore_error = 0;
    int mask_restore_error = 0;
    if(state.sigint_changed && sigaction(SIGINT, &state.original_sigint, nullptr) == -1) {
        sigint_restore_error = errno;
    }
    if(state.sigquit_changed && sigaction(SIGQUIT, &state.original_sigquit, nullptr) == -1) {
        sigquit_restore_error = errno;
    }
    if(state.mask_changed && sigprocmask(SIG_SETMASK, &state.original_mask, nullptr) == -1) {
        mask_restore_error = errno;
    }

    // 復元処理をすべて試した後でloggingし、先の失敗で後続の復元を飛ばさない。
    if(sigint_restore_error != 0) {
        Logger::error(signal_restore_failure_message(
                SignalRestoreTarget::SigintDisposition, context,
                sigint_restore_error));
    }
    if(sigquit_restore_error != 0) {
        Logger::error(signal_restore_failure_message(
                SignalRestoreTarget::SigquitDisposition, context,
                sigquit_restore_error));
    }
    if(mask_restore_error != 0) {
        Logger::error(signal_restore_failure_message(
                SignalRestoreTarget::SignalMask, context,
                mask_restore_error));
    }
    return sigint_restore_error == 0 && sigquit_restore_error == 0 && mask_restore_error == 0;
}

bool setup_command_signal_state(
        CommandSignalState& state,
        CommandSignalContext command_context) {
    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    if(sigemptyset(&ignore_action.sa_mask) == -1) {
        int setup_error = errno;
        Logger::error(localization::format_translated_message(
                "Failed to initialize the ignored signal disposition: {}",
                std::string_view(std::strerror(setup_error))));
        return false;
    }

    sigset_t sigchld_mask;
    if(sigemptyset(&sigchld_mask) == -1 || sigaddset(&sigchld_mask, SIGCHLD) == -1) {
        int setup_error = errno;
        Logger::error(localization::format_translated_message(
                "Failed to initialize the {} mask: {}", "SIGCHLD",
                std::string_view(std::strerror(setup_error))));
        return false;
    }

    if(sigaction(SIGINT, &ignore_action, &state.original_sigint) == -1) {
        int setup_error = errno;
        log_signal_setup_error(command_context, "SIGINT", setup_error);
        return false;
    }
    state.sigint_changed = true;

    if(sigaction(SIGQUIT, &ignore_action, &state.original_sigquit) == -1) {
        int setup_error = errno;
        restore_parent_signal_state(
                state, SignalRestoreContext::CommandSetupRollback);
        log_signal_setup_error(command_context, "SIGQUIT", setup_error);
        return false;
    }
    state.sigquit_changed = true;

    if(sigprocmask(SIG_BLOCK, &sigchld_mask, &state.original_mask) == -1) {
        int setup_error = errno;
        restore_parent_signal_state(
                state, SignalRestoreContext::CommandSetupRollback);
        log_signal_setup_error(command_context, "SIGCHLD", setup_error);
        return false;
    }
    state.mask_changed = true;
    return true;
}

std::string trim_captured_output(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

CapturedCommandResult capture_command_output_impl(
        const char* cmd,
        bool trim_output) {
    std::array<char, 128> buffer;
    std::string           result;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if(!pipe) return CapturedCommandResult{};
    // POLICY(#242): strict machine-output parserへNULも含む全byteを渡し、
    // C-string appendによる途中切り捨てを起こさない。
    while(true) {
        std::size_t bytes_read =
                std::fread(buffer.data(), 1, buffer.size(), pipe.get());
        result.append(buffer.data(), bytes_read);
        if(bytes_read == 0) break;
    }
    bool read_failed = ferror(pipe.get()) != 0;
    int  status = pclose(pipe.release());
    int  exit_code = 127;
    if(status != -1) {
        if(WIFEXITED(status))
            exit_code = WEXITSTATUS(status);
        else if(WIFSIGNALED(status))
            exit_code = 128 + WTERMSIG(status);
        else
            exit_code = 1;
    }
    if(read_failed) exit_code = 1;
    return CapturedCommandResult{
            trim_output ? trim_captured_output(result) : std::move(result),
            exit_code};
}

int decode_process_status(int status) noexcept {
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

bool has_embedded_nul(const std::string& value) noexcept {
    return value.find('\0') != std::string::npos;
}

[[noreturn]] void exec_explicit_process_child(
        const ExplicitProcessInvocation& invocation,
        std::vector<char*>& argument_vector,
        std::vector<char*>& environment_vector,
        const int output_pipe[2],
        bool capture_standard_output,
        bool suppress_standard_output,
        bool suppress_standard_error,
        const CommandSignalState& signal_state,
        std::optional<int> inherited_lifetime_guard_fd = std::nullopt) {
    if(sigaction(SIGINT, &signal_state.original_sigint, nullptr) == -1 ||
       sigaction(SIGQUIT, &signal_state.original_sigquit, nullptr) == -1 ||
       sigprocmask(
               SIG_SETMASK, &signal_state.original_mask, nullptr) == -1) {
        _exit(127);
    }

    if(invocation.working_directory_fd.has_value() &&
       fchdir(*invocation.working_directory_fd) == -1) {
        _exit(127);
    }

    if(invocation.standard_input_fd.has_value()) {
        const int source_fd = *invocation.standard_input_fd;
        if(source_fd == STDIN_FILENO) {
            // LANDMINE: dup2(0, 0) does not clear an inherited CLOEXEC bit.
            const int descriptor_flags = fcntl(STDIN_FILENO, F_GETFD);
            if(descriptor_flags == -1 ||
               fcntl(
                       STDIN_FILENO, F_SETFD,
                       descriptor_flags & ~FD_CLOEXEC) == -1) {
                _exit(127);
            }
        } else if(dup2(source_fd, STDIN_FILENO) == -1) {
            _exit(127);
        } else if(source_fd > STDERR_FILENO) {
            static_cast<void>(close(source_fd));
        }
    }

    if(capture_standard_output) {
        static_cast<void>(close(output_pipe[0]));
        if(dup2(output_pipe[1], STDOUT_FILENO) == -1) _exit(127);
        static_cast<void>(close(output_pipe[1]));
    }

    if(suppress_standard_output || suppress_standard_error) {
        const int null_descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if(null_descriptor < 0) _exit(127);
        const auto redirect_to_null = [null_descriptor](int target) {
            if(null_descriptor == target) {
                const int flags = fcntl(target, F_GETFD);
                return flags != -1 &&
                       fcntl(target, F_SETFD, flags & ~FD_CLOEXEC) != -1;
            }
            return dup2(null_descriptor, target) != -1;
        };
        if(suppress_standard_output &&
           !redirect_to_null(STDOUT_FILENO)) _exit(127);
        if(suppress_standard_error &&
           !redirect_to_null(STDERR_FILENO)) _exit(127);
        if(null_descriptor != STDOUT_FILENO &&
           null_descriptor != STDERR_FILENO) {
            static_cast<void>(close(null_descriptor));
        }
    }

    if(inherited_lifetime_guard_fd.has_value()) {
        const int guard_descriptor = *inherited_lifetime_guard_fd;
        const int descriptor_flags = fcntl(guard_descriptor, F_GETFD);
        if(descriptor_flags == -1 ||
           fcntl(
                   guard_descriptor, F_SETFD,
                   descriptor_flags & ~FD_CLOEXEC) == -1) {
            _exit(127);
        }
    }

    execve(
            invocation.executable.c_str(), argument_vector.data(),
            environment_vector.data());
    _exit(127);
}

bool close_descriptor_range(
        unsigned int first, unsigned int last) noexcept {
    if(first > last) return true;
#ifdef SYS_close_range
    long result;
    do {
        result = syscall(SYS_close_range, first, last, 0U);
    } while(result == -1 && errno == EINTR);
    return result == 0;
#else
    static_cast<void>(first);
    static_cast<void>(last);
    return false;
#endif
}

bool close_supervisor_descriptors_except(int preserved_descriptor) noexcept {
    if(preserved_descriptor < 3) return false;
    if(preserved_descriptor > 3 &&
       !close_descriptor_range(
               3U, static_cast<unsigned int>(preserved_descriptor - 1))) {
        return false;
    }
    if(preserved_descriptor < INT_MAX &&
       !close_descriptor_range(
               static_cast<unsigned int>(preserved_descriptor + 1),
               UINT_MAX)) {
        return false;
    }
    return true;
}

[[noreturn]] void supervise_explicit_process_lifetime(
        const ExplicitProcessInvocation& invocation,
        std::vector<char*>& argument_vector,
        std::vector<char*>& environment_vector,
        bool suppress_standard_output,
        bool suppress_standard_error,
        const CommandSignalState& signal_state) {
    const int guard_descriptor =
            *invocation.parent_independent_lifetime_guard_fd;
    int supervisor_guard;
    do {
        supervisor_guard =
                fcntl(guard_descriptor, F_DUPFD_CLOEXEC, 3);
    } while(supervisor_guard == -1 && errno == EINTR);
    if(supervisor_guard == -1 ||
       !close_supervisor_descriptors_except(supervisor_guard) ||
       prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == -1) {
        _exit(127);
    }

    const pid_t mutator_pid = fork();
    if(mutator_pid == -1) _exit(127);
    if(mutator_pid == 0) {
        const int no_output_pipe[2] = {-1, -1};
        exec_explicit_process_child(
                invocation, argument_vector, environment_vector,
                no_output_pipe, false, suppress_standard_output,
                suppress_standard_error, signal_state, supervisor_guard);
    }

    // The supervisor and actual mutator tree retain the same open-file-
    // description. If either side dies independently, the other keeps the
    // guard. A descendant that outlives its expected Git parent intentionally
    // keeps the guard fail-closed until it exits; the subreaper prevents normal
    // completion from returning while such a descendant remains.
    int root_status = 0;
    bool root_reaped = false;
    while(true) {
        int descendant_status = 0;
        const pid_t waited = waitpid(-1, &descendant_status, 0);
        if(waited > 0) {
            if(waited == mutator_pid) {
                root_status = descendant_status;
                root_reaped = true;
            }
            continue;
        }
        if(waited == -1 && errno == EINTR) continue;
        if(waited == -1 && errno == ECHILD) break;
        _exit(127);
    }
    _exit(root_reaped ? decode_process_status(root_status) : 127);
}

CapturedCommandResult execute_explicit_process(
        const ExplicitProcessInvocation& invocation,
        bool capture_standard_output,
        bool suppress_standard_output,
        bool suppress_standard_error) {
    if(invocation.executable.empty() ||
       has_embedded_nul(invocation.executable)) {
        return CapturedCommandResult{};
    }
    for(const std::string& argument : invocation.arguments) {
        if(has_embedded_nul(argument)) return CapturedCommandResult{};
    }
    for(const std::string& assignment : invocation.environment) {
        const std::size_t separator = assignment.find('=');
        if(separator == std::string::npos || separator == 0 ||
           has_embedded_nul(assignment)) {
            return CapturedCommandResult{};
        }
    }
    if(invocation.parent_independent_lifetime_guard_fd.has_value()) {
        const int guard_descriptor =
                *invocation.parent_independent_lifetime_guard_fd;
        if(capture_standard_output ||
           invocation.working_directory_fd.has_value() ||
           invocation.standard_input_fd.has_value() ||
           guard_descriptor < 0 ||
           fcntl(guard_descriptor, F_GETFD) == -1) {
            return CapturedCommandResult{};
        }
    }

    std::vector<char*> argument_vector;
    argument_vector.reserve(invocation.arguments.size() + 2);
    argument_vector.push_back(
            const_cast<char*>(invocation.executable.c_str()));
    for(const std::string& argument : invocation.arguments) {
        argument_vector.push_back(const_cast<char*>(argument.c_str()));
    }
    argument_vector.push_back(nullptr);

    std::vector<char*> environment_vector;
    environment_vector.reserve(invocation.environment.size() + 1);
    for(const std::string& assignment : invocation.environment) {
        environment_vector.push_back(const_cast<char*>(assignment.c_str()));
    }
    environment_vector.push_back(nullptr);

    int output_pipe[2] = {-1, -1};
    if(capture_standard_output && pipe2(output_pipe, O_CLOEXEC) != 0) {
        return CapturedCommandResult{};
    }

    CommandSignalState signal_state;
    if(!setup_command_signal_state(
               signal_state, CommandSignalContext::ExplicitProcess)) {
        if(output_pipe[0] >= 0) static_cast<void>(close(output_pipe[0]));
        if(output_pipe[1] >= 0) static_cast<void>(close(output_pipe[1]));
        return CapturedCommandResult{};
    }

    const pid_t child_pid = fork();
    if(child_pid == -1) {
        if(output_pipe[0] >= 0) static_cast<void>(close(output_pipe[0]));
        if(output_pipe[1] >= 0) static_cast<void>(close(output_pipe[1]));
        restore_parent_signal_state(
                signal_state,
                SignalRestoreContext::ExplicitProcessForkFailure);
        return CapturedCommandResult{};
    }
    if(child_pid == 0) {
        if(invocation.parent_independent_lifetime_guard_fd.has_value()) {
            supervise_explicit_process_lifetime(
                    invocation, argument_vector, environment_vector,
                    suppress_standard_output, suppress_standard_error,
                    signal_state);
        }
        exec_explicit_process_child(
                invocation, argument_vector, environment_vector,
                output_pipe, capture_standard_output,
                suppress_standard_output, suppress_standard_error,
                signal_state);
    }

    if(output_pipe[1] >= 0) static_cast<void>(close(output_pipe[1]));
    std::string output;
    bool read_failed = false;
    bool stdout_capture_limit_exceeded = false;
    std::exception_ptr pending_exception;
    if(capture_standard_output) {
        std::array<char, 4096> buffer;
        while(true) {
            const ssize_t bytes_read =
                    read(output_pipe[0], buffer.data(), buffer.size());
            if(bytes_read > 0) {
                const std::size_t chunk_size =
                        static_cast<std::size_t>(bytes_read);
                std::size_t stored_size = chunk_size;
                if(invocation.stdout_capture_limit.has_value()) {
                    const std::size_t limit =
                            *invocation.stdout_capture_limit;
                    const std::size_t remaining =
                            output.size() < limit ? limit - output.size() : 0;
                    stored_size = std::min(chunk_size, remaining);
                    if(stored_size < chunk_size) {
                        stdout_capture_limit_exceeded = true;
                    }
                }

                if(stored_size > 0 && !pending_exception) {
                    try {
                        output.append(buffer.data(), stored_size);
                    } catch(...) {
                        // append失敗後もpipeをEOFまでdrainし、childを確実にreapしてから再送出する。
                        pending_exception = std::current_exception();
                    }
                }
                continue;
            }
            if(bytes_read == 0) break;
            if(errno == EINTR) continue;
            read_failed = true;
            break;
        }
        static_cast<void>(close(output_pipe[0]));
    }

    int status = 0;
    pid_t wait_result;
    do {
        wait_result = waitpid(child_pid, &status, 0);
    } while(wait_result == -1 && errno == EINTR);
    const bool signal_state_restored = restore_parent_signal_state(
            signal_state, SignalRestoreContext::ExplicitProcessWait);
    if(pending_exception) std::rethrow_exception(pending_exception);
    if(wait_result == -1 || !signal_state_restored) {
        return CapturedCommandResult{
                std::move(output), 127, stdout_capture_limit_exceeded};
    }
    return CapturedCommandResult{
            std::move(output),
            read_failed ? 1 : decode_process_status(status),
            stdout_capture_limit_exceeded};
}

} // namespace

CapturedCommandResult capture_command_output(const char* cmd) {
    return capture_command_output_impl(cmd, true);
}

CapturedCommandResult capture_command_output_raw(const char* cmd) {
    return capture_command_output_impl(cmd, false);
}

CapturedCommandResult capture_explicit_process_output_raw(
        const ExplicitProcessInvocation& invocation,
        bool suppress_standard_error) {
    return execute_explicit_process(
            invocation, true, false, suppress_standard_error);
}

int run_explicit_process(
        const ExplicitProcessInvocation& invocation,
        bool suppress_standard_output,
        bool suppress_standard_error) {
    return execute_explicit_process(
                   invocation, false, suppress_standard_output,
                   suppress_standard_error)
            .exit_code;
}

std::string exec_command(const char* cmd) {
    return capture_command_output(cmd).output;
}

int command_status(const std::string& cmd) {
    int status = std::system(cmd.c_str());
    if(status == -1) return 127;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

int run_command(const std::string& cmd) {
    Logger::raw_cmd(cmd);
    return command_status(cmd);
}

int run_command_with_parent_independent_lifetime_guard(
        const std::string& command,
        int lifetime_guard_fd,
        const std::string& display_command) {
    Logger::raw_cmd(display_command.empty() ? command : display_command);
    std::vector<std::string> environment;
    for(char** current = ::environ;
        current != nullptr && *current != nullptr; ++current) {
        environment.emplace_back(*current);
    }
    ExplicitProcessInvocation invocation;
    invocation.executable = "/bin/sh";
    invocation.arguments = {"-c", command};
    invocation.environment = std::move(environment);
    invocation.parent_independent_lifetime_guard_fd =
            lifetime_guard_fd;
    return run_explicit_process(invocation);
}

int run_command_with_stdin_fd(const std::string& command, int source_fd) {
    Logger::raw_cmd(command);
    const char* command_text = command.c_str();

    // POLICY: std::system()と同様、親はSIGINT/SIGQUITを無視し、SIGCHLDをblockしたままchildをreapする。
    CommandSignalState signal_state;
    if(!setup_command_signal_state(
               signal_state, CommandSignalContext::StdinFdCommand)) {
        return 127;
    }

    pid_t child_pid = fork();
    if(child_pid == -1) {
        restore_parent_signal_state(
                signal_state,
                SignalRestoreContext::StdinFdCommandForkFailure);
        return 127;
    }
    if(child_pid == 0) {
        // fork後に親専用のignore/block状態を持ち込まず、exec先へcallerのsignal契約を引き渡す。
        if(sigaction(SIGINT, &signal_state.original_sigint, nullptr) == -1 ||
           sigaction(SIGQUIT, &signal_state.original_sigquit, nullptr) == -1 ||
           sigprocmask(SIG_SETMASK, &signal_state.original_mask, nullptr) == -1) {
            _exit(127);
        }

        if(source_fd == STDIN_FILENO) {
            // LANDMINE: open(O_CLOEXEC)がfd 0を返した場合、dup2(0, 0)ではCLOEXECが解除されない。
            int descriptor_flags = fcntl(STDIN_FILENO, F_GETFD);
            if(descriptor_flags == -1 ||
               fcntl(STDIN_FILENO, F_SETFD, descriptor_flags & ~FD_CLOEXEC) == -1) {
                _exit(127);
            }
        } else if(dup2(source_fd, STDIN_FILENO) == -1) {
            _exit(127);
        }

        // source_fdにはO_CLOEXECがあるため、dup2後の元descriptorはexec時に閉じる。
        execl("/bin/sh", "sh", "-c", command_text, static_cast<char*>(nullptr));
        _exit(127);
    }

    int   status = 0;
    pid_t wait_result;
    do {
        wait_result = waitpid(child_pid, &status, 0);
    } while(wait_result == -1 && errno == EINTR);

    bool signal_state_restored = restore_parent_signal_state(
            signal_state, SignalRestoreContext::StdinFdCommandWait);
    if(wait_result == -1 || !signal_state_restored) return 127;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
