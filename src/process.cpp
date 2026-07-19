#include "process.hpp"

#include "logging.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string trim_captured_output(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

} // namespace

CapturedCommandResult capture_command_output(const char* cmd) {
    std::array<char, 128> buffer;
    std::string           result;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if(!pipe) return CapturedCommandResult{};
    while(fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
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
    return CapturedCommandResult{trim_captured_output(result), exit_code};
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

    pid_t child_pid = fork();
    if(child_pid == -1) return 127;
    if(child_pid == 0) {
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

    if(wait_result == -1) return 127;
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
