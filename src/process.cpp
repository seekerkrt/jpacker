#include "process.hpp"

#include "logging.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

struct CommandSignalState {
    struct sigaction original_sigint {};
    struct sigaction original_sigquit {};
    sigset_t         original_mask {};
    bool             sigint_changed = false;
    bool             sigquit_changed = false;
    bool             mask_changed = false;
};

void log_signal_error(const std::string& operation, int error_number) {
    Logger::error(operation + ": " + std::strerror(error_number));
}

bool restore_parent_signal_state(
        const CommandSignalState& state,
        const char* context) {
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
        log_signal_error(
                std::string("Failed to restore SIGINT disposition ") + context,
                sigint_restore_error);
    }
    if(sigquit_restore_error != 0) {
        log_signal_error(
                std::string("Failed to restore SIGQUIT disposition ") + context,
                sigquit_restore_error);
    }
    if(mask_restore_error != 0) {
        log_signal_error(std::string("Failed to restore signal mask ") + context, mask_restore_error);
    }
    return sigint_restore_error == 0 && sigquit_restore_error == 0 && mask_restore_error == 0;
}

bool setup_command_signal_state(
        CommandSignalState& state,
        const char* command_context) {
    struct sigaction ignore_action {};
    ignore_action.sa_handler = SIG_IGN;
    if(sigemptyset(&ignore_action.sa_mask) == -1) {
        int setup_error = errno;
        log_signal_error("Failed to initialize ignored signal disposition", setup_error);
        return false;
    }

    sigset_t sigchld_mask;
    if(sigemptyset(&sigchld_mask) == -1 || sigaddset(&sigchld_mask, SIGCHLD) == -1) {
        int setup_error = errno;
        log_signal_error("Failed to initialize SIGCHLD mask", setup_error);
        return false;
    }

    if(sigaction(SIGINT, &ignore_action, &state.original_sigint) == -1) {
        int setup_error = errno;
        log_signal_error(
                std::string("Failed to ignore SIGINT while waiting for ") +
                        command_context,
                setup_error);
        return false;
    }
    state.sigint_changed = true;

    if(sigaction(SIGQUIT, &ignore_action, &state.original_sigquit) == -1) {
        int setup_error = errno;
        restore_parent_signal_state(state, "during command setup rollback");
        log_signal_error(
                std::string("Failed to ignore SIGQUIT while waiting for ") +
                        command_context,
                setup_error);
        return false;
    }
    state.sigquit_changed = true;

    if(sigprocmask(SIG_BLOCK, &sigchld_mask, &state.original_mask) == -1) {
        int setup_error = errno;
        restore_parent_signal_state(state, "during command setup rollback");
        log_signal_error(
                std::string("Failed to block SIGCHLD while waiting for ") +
                        command_context,
                setup_error);
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
    if(!setup_command_signal_state(signal_state, "explicit process")) {
        if(output_pipe[0] >= 0) static_cast<void>(close(output_pipe[0]));
        if(output_pipe[1] >= 0) static_cast<void>(close(output_pipe[1]));
        return CapturedCommandResult{};
    }

    const pid_t child_pid = fork();
    if(child_pid == -1) {
        if(output_pipe[0] >= 0) static_cast<void>(close(output_pipe[0]));
        if(output_pipe[1] >= 0) static_cast<void>(close(output_pipe[1]));
        restore_parent_signal_state(
                signal_state, "after explicit process fork failure");
        return CapturedCommandResult{};
    }
    if(child_pid == 0) {
        if(sigaction(SIGINT, &signal_state.original_sigint, nullptr) == -1 ||
           sigaction(SIGQUIT, &signal_state.original_sigquit, nullptr) == -1 ||
           sigprocmask(
                   SIG_SETMASK, &signal_state.original_mask, nullptr) == -1) {
            _exit(127);
        }

        if(capture_standard_output) {
            static_cast<void>(close(output_pipe[0]));
            if(dup2(output_pipe[1], STDOUT_FILENO) == -1) _exit(127);
            static_cast<void>(close(output_pipe[1]));
        }

        if(suppress_standard_output || suppress_standard_error) {
            const int null_descriptor =
                    open("/dev/null", O_WRONLY | O_CLOEXEC);
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

        execve(
                invocation.executable.c_str(), argument_vector.data(),
                environment_vector.data());
        _exit(127);
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
            signal_state, "after explicit process wait");
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

int run_command_with_stdin_fd(const std::string& command, int source_fd) {
    Logger::raw_cmd(command);
    const char* command_text = command.c_str();

    // POLICY: std::system()と同様、親はSIGINT/SIGQUITを無視し、SIGCHLDをblockしたままchildをreapする。
    CommandSignalState signal_state;
    if(!setup_command_signal_state(signal_state, "stdin fd command")) {
        return 127;
    }

    pid_t child_pid = fork();
    if(child_pid == -1) {
        restore_parent_signal_state(signal_state, "after stdin fd command fork failure");
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

    bool signal_state_restored = restore_parent_signal_state(signal_state, "after stdin fd command wait");
    if(wait_result == -1 || !signal_state_restored) return 127;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
