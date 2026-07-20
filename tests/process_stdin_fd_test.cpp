#include "process.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

using MonotonicClock = std::chrono::steady_clock;

constexpr auto SIGNAL_TEST_TIMEOUT = std::chrono::seconds(5);
constexpr int  SIGNAL_TEST_RETRY_INTERVAL_MS = 10;
constexpr int  EXPECTED_CHILD_STATUS = 37;

struct WorkerResult {
    int   helper_status = -1;
    pid_t post_helper_wait_result = -2;
    int   post_helper_wait_errno = 0;
    int   setup_errno = 0;
};

void wait_before_retry() {
    int poll_result = poll(nullptr, 0, SIGNAL_TEST_RETRY_INTERVAL_MS);
    if(poll_result == -1 && errno != EINTR) {
        throw std::system_error(errno, std::generic_category(), "poll signal test deadline");
    }
}

class ProcessGroupGuard {
public:
    explicit ProcessGroupGuard(pid_t worker_pid) : worker_pid_(worker_pid) {}

    ~ProcessGroupGuard() {
        if(!is_active_) return;

        // Test失敗時もnamed FIFOで待つshellを残さず、suite全体のhangやprocess漏れを防ぐ。
        kill(-worker_pid_, SIGKILL);
        if(!is_reaped_) kill(worker_pid_, SIGKILL);

        auto deadline = MonotonicClock::now() + SIGNAL_TEST_TIMEOUT;
        while(!is_reaped_ && MonotonicClock::now() < deadline) {
            int   status = 0;
            pid_t wait_result = waitpid(worker_pid_, &status, WNOHANG);
            if(wait_result == worker_pid_ || (wait_result == -1 && errno == ECHILD)) {
                is_reaped_ = true;
                break;
            }
            if(wait_result == -1 && errno != EINTR) break;
            poll(nullptr, 0, SIGNAL_TEST_RETRY_INTERVAL_MS);
        }
    }

    ProcessGroupGuard(const ProcessGroupGuard&) = delete;
    ProcessGroupGuard& operator=(const ProcessGroupGuard&) = delete;

    int wait_for_worker() {
        auto deadline = MonotonicClock::now() + SIGNAL_TEST_TIMEOUT;
        while(MonotonicClock::now() < deadline) {
            int   status = 0;
            pid_t wait_result = waitpid(worker_pid_, &status, WNOHANG);
            if(wait_result == worker_pid_) {
                is_reaped_ = true;
                return status;
            }
            if(wait_result == -1) {
                if(errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "waitpid signal test worker");
            }
            wait_before_retry();
        }
        throw std::runtime_error("Timed out waiting for signal test worker");
    }

    void release() {
        is_active_ = false;
    }

private:
    pid_t worker_pid_ = -1;
    bool  is_active_ = true;
    bool  is_reaped_ = false;
};

class SignalStateGuard {
public:
    SignalStateGuard() {
        if(sigaction(SIGINT, nullptr, &original_sigint_) == -1) {
            throw std::system_error(errno, std::generic_category(), "capture SIGINT disposition");
        }
        if(sigaction(SIGQUIT, nullptr, &original_sigquit_) == -1) {
            throw std::system_error(errno, std::generic_category(), "capture SIGQUIT disposition");
        }
        if(sigprocmask(SIG_SETMASK, nullptr, &original_mask_) == -1) {
            throw std::system_error(errno, std::generic_category(), "capture process signal mask");
        }
    }

    ~SignalStateGuard() {
        if(is_active_) restore();
    }

    SignalStateGuard(const SignalStateGuard&) = delete;
    SignalStateGuard& operator=(const SignalStateGuard&) = delete;

    bool restore() {
        bool restored = true;
        if(sigaction(SIGINT, &original_sigint_, nullptr) == -1) restored = false;
        if(sigaction(SIGQUIT, &original_sigquit_, nullptr) == -1) restored = false;
        if(sigprocmask(SIG_SETMASK, &original_mask_, nullptr) == -1) restored = false;
        if(restored) is_active_ = false;
        return restored;
    }

private:
    struct sigaction original_sigint_ {};
    struct sigaction original_sigquit_ {};
    sigset_t         original_mask_ {};
    bool             is_active_ = true;
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        char directory_template[] = "/tmp/jpacker-process-stdin-fd.XXXXXX";
        char* created_path = mkdtemp(directory_template);
        if(created_path == nullptr) {
            throw std::system_error(errno, std::generic_category(), "mkdtemp");
        }
        path_ = created_path;
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const {
        return path_;
    }

private:
    fs::path path_;
};

class StdinGuard {
public:
    StdinGuard() {
        saved_stdin_ = dup(STDIN_FILENO);
        if(saved_stdin_ == -1) {
            if(errno != EBADF) {
                throw std::system_error(errno, std::generic_category(), "dup stdin");
            }
            stdin_was_open_ = false;
            return;
        }

        stdin_was_open_ = true;
        int descriptor_flags = fcntl(saved_stdin_, F_GETFD);
        if(descriptor_flags != -1) {
            fcntl(saved_stdin_, F_SETFD, descriptor_flags | FD_CLOEXEC);
        }
    }

    ~StdinGuard() {
        if(stdin_was_open_) {
            dup2(saved_stdin_, STDIN_FILENO);
            close(saved_stdin_);
        } else {
            close(STDIN_FILENO);
        }
    }

private:
    int  saved_stdin_ = -1;
    bool stdin_was_open_ = true;
};

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void require_signal_sets_equal(
        const sigset_t& actual,
        const sigset_t& expected,
        const std::string& context) {
    for(int signal_number = 1; signal_number < NSIG; ++signal_number) {
        int actual_membership = sigismember(&actual, signal_number);
        int expected_membership = sigismember(&expected, signal_number);
        require(actual_membership != -1 && expected_membership != -1,
                context + ": failed to inspect signal " + std::to_string(signal_number));
        require(actual_membership == expected_membership,
                context + ": membership changed for signal " + std::to_string(signal_number));
    }
}

void require_sigaction_equal(
        const struct sigaction& actual,
        const struct sigaction& expected,
        const std::string& context) {
    require(actual.sa_handler == expected.sa_handler, context + ": handler changed");
    require(actual.sa_flags == expected.sa_flags, context + ": flags changed");
    require_signal_sets_equal(actual.sa_mask, expected.sa_mask, context + " handler mask");
}

void restored_sigint_handler(int) {}

void restored_sigquit_handler(int) {}

void worker_signal_handler(int signal_number) {
    _exit(signal_number == SIGINT ? 90 : 91);
}

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.close();
    if(!file) throw std::runtime_error("Failed to write " + path.string());
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("Failed to read " + path.string());
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

int open_source(const fs::path& path, const std::string& content) {
    write_file(path, content);
    int source_fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if(source_fd == -1) {
        throw std::system_error(errno, std::generic_category(), "open " + path.string());
    }
    return source_fd;
}

void require_seek(int fd, off_t offset, const std::string& context) {
    if(lseek(fd, offset, SEEK_SET) == -1) {
        throw std::system_error(errno, std::generic_category(), context);
    }
}

void require_close_on_exec(int fd, const std::string& context) {
    int descriptor_flags = fcntl(fd, F_GETFD);
    if(descriptor_flags == -1 ||
       fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == -1) {
        throw std::system_error(errno, std::generic_category(), context);
    }
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for(char character : value) {
        if(character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    quoted += "'";
    return quoted;
}

bool write_full(int fd, const void* buffer, size_t size) {
    const char* bytes = static_cast<const char*>(buffer);
    size_t      written = 0;
    while(written < size) {
        ssize_t write_result = write(fd, bytes + written, size - written);
        if(write_result == -1) {
            if(errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(write_result);
    }
    return true;
}

bool read_full(int fd, void* buffer, size_t size) {
    char*  bytes = static_cast<char*>(buffer);
    size_t bytes_read = 0;
    while(bytes_read < size) {
        ssize_t read_result = read(fd, bytes + bytes_read, size - bytes_read);
        if(read_result == 0) return false;
        if(read_result == -1) {
            if(errno == EINTR) continue;
            return false;
        }
        bytes_read += static_cast<size_t>(read_result);
    }
    return true;
}

[[noreturn]] void fail_signal_worker(int result_fd, WorkerResult& result, int setup_errno) {
    result.setup_errno = setup_errno;
    write_full(result_fd, &result, sizeof(result));
    _exit(70);
}

[[noreturn]] void run_signal_worker(
        const std::string& command,
        int source_fd,
        int result_fd) {
    WorkerResult result;

    if(setpgid(0, 0) == -1) fail_signal_worker(result_fd, result, errno);

    // caught dispositionはforkで継承され、execでdefaultへ戻る。
    // 親のignoreまたはchildのrestoreが欠けた場合はworkerの早期終了かchildのtimeoutとして検出する。
    struct sigaction worker_action {};
    worker_action.sa_handler = worker_signal_handler;
    if(sigemptyset(&worker_action.sa_mask) == -1 ||
       sigaction(SIGINT, &worker_action, nullptr) == -1 ||
       sigaction(SIGQUIT, &worker_action, nullptr) == -1) {
        fail_signal_worker(result_fd, result, errno);
    }

    sigset_t unblocked_signals;
    if(sigemptyset(&unblocked_signals) == -1 ||
       sigaddset(&unblocked_signals, SIGINT) == -1 ||
       sigaddset(&unblocked_signals, SIGQUIT) == -1 ||
       sigprocmask(SIG_UNBLOCK, &unblocked_signals, nullptr) == -1) {
        fail_signal_worker(result_fd, result, errno);
    }

    result.helper_status = run_command_with_stdin_fd(command, source_fd);

    int unused_status = 0;
    errno = 0;
    result.post_helper_wait_result = waitpid(-1, &unused_status, WNOHANG);
    result.post_helper_wait_errno = errno;

    if(!write_full(result_fd, &result, sizeof(result))) _exit(71);
    _exit(0);
}

void wait_for_marker(const fs::path& marker_path) {
    auto deadline = MonotonicClock::now() + SIGNAL_TEST_TIMEOUT;
    while(MonotonicClock::now() < deadline) {
        struct stat marker_status {};
        if(stat(marker_path.c_str(), &marker_status) == 0) return;
        if(errno != ENOENT) {
            throw std::system_error(errno, std::generic_category(),
                                    "stat signal test marker " + marker_path.string());
        }
        wait_before_retry();
    }
    throw std::runtime_error("Timed out waiting for signal test marker " + marker_path.string());
}

void release_fifo_reader(const fs::path& fifo_path) {
    auto deadline = MonotonicClock::now() + SIGNAL_TEST_TIMEOUT;
    while(MonotonicClock::now() < deadline) {
        int fifo_fd = open(fifo_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if(fifo_fd != -1) {
            close(fifo_fd);
            return;
        }
        if(errno != ENXIO && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(),
                                    "open signal test release FIFO " + fifo_path.string());
        }
        wait_before_retry();
    }
    throw std::runtime_error("Timed out releasing signal test FIFO " + fifo_path.string());
}

void wait_for_process_group_exit(pid_t process_group) {
    auto deadline = MonotonicClock::now() + SIGNAL_TEST_TIMEOUT;
    while(MonotonicClock::now() < deadline) {
        if(kill(-process_group, 0) == -1) {
            if(errno == ESRCH) return;
            if(errno != EINTR) {
                throw std::system_error(errno, std::generic_category(),
                                        "inspect signal test process group");
            }
        }
        wait_before_retry();
    }
    throw std::runtime_error("Timed out waiting for signal test process group to disappear");
}

size_t occurrence_count(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t offset = 0;
    while((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void test_current_offset_and_borrowed_ownership(const fs::path& test_dir) {
    const std::string source_bytes{"skip\0payload\n", 13};
    fs::path          source_path = test_dir / "offset-source";
    fs::path          output_path = test_dir / "offset-output";
    int               source_fd = open_source(source_path, source_bytes);
    require_seek(source_fd, 4, "seek source offset");

    std::string command = "/usr/bin/cat > " + shell_quote(output_path.string());
    std::ostringstream logged_command;
    std::streambuf* original_stdout = std::cout.rdbuf(logged_command.rdbuf());
    int status = run_command_with_stdin_fd(command, source_fd);
    std::cout.rdbuf(original_stdout);

    require(status == 0, "stdin fd command failed");
    require(read_file(output_path) == source_bytes.substr(4), "child did not read from current source offset");
    require(fcntl(source_fd, F_GETFD) != -1, "helper closed caller-owned source fd");
    require(lseek(source_fd, 0, SEEK_CUR) == static_cast<off_t>(source_bytes.size()),
            "source fd offset did not advance with child read");
    require(occurrence_count(logged_command.str(), "Running: " + command) == 1,
            "command was not logged exactly once");
    close(source_fd);
}

void test_parent_stdin_is_unchanged(const fs::path& test_dir) {
    StdinGuard guard;

    int parent_stdin = open_source(test_dir / "parent-stdin", "parent-input");
    require_seek(parent_stdin, 3, "seek parent stdin");
    require(dup2(parent_stdin, STDIN_FILENO) != -1, "failed to install parent stdin fixture");
    if(parent_stdin != STDIN_FILENO) close(parent_stdin);
    require_close_on_exec(STDIN_FILENO, "set parent stdin close-on-exec");

    struct stat before_status {};
    require(fstat(STDIN_FILENO, &before_status) == 0, "failed to inspect parent stdin before command");
    off_t before_offset = lseek(STDIN_FILENO, 0, SEEK_CUR);
    int   before_flags = fcntl(STDIN_FILENO, F_GETFD);

    int source_fd = open_source(test_dir / "parent-check-source", "child-input");
    fs::path output_path = test_dir / "parent-check-output";
    int status = run_command_with_stdin_fd(
            "/usr/bin/cat > " + shell_quote(output_path.string()), source_fd);
    require(status == 0, "parent stdin preservation command failed");

    struct stat after_status {};
    require(fstat(STDIN_FILENO, &after_status) == 0, "parent stdin was closed");
    require(after_status.st_dev == before_status.st_dev && after_status.st_ino == before_status.st_ino,
            "parent stdin descriptor target changed");
    require(lseek(STDIN_FILENO, 0, SEEK_CUR) == before_offset, "parent stdin offset changed");
    require(fcntl(STDIN_FILENO, F_GETFD) == before_flags, "parent stdin descriptor flags changed");
    require(read_file(output_path) == "child-input", "child stdin payload was incorrect");
    close(source_fd);
}

void test_source_fd_zero_with_close_on_exec(const fs::path& test_dir) {
    StdinGuard guard;

    const std::string source_bytes{"fd-zero\0payload", 15};
    int source_fd = open_source(test_dir / "fd-zero-source", source_bytes);
    require(dup2(source_fd, STDIN_FILENO) != -1, "failed to move source onto fd 0");
    if(source_fd != STDIN_FILENO) close(source_fd);
    require_close_on_exec(STDIN_FILENO, "set source fd 0 close-on-exec");

    fs::path output_path = test_dir / "fd-zero-output";
    int status = run_command_with_stdin_fd(
            "/usr/bin/cat > " + shell_quote(output_path.string()), STDIN_FILENO);

    require(status == 0, "source fd 0 was closed during exec");
    require(read_file(output_path) == source_bytes, "source fd 0 payload was not copied byte-exactly");
    int descriptor_flags = fcntl(STDIN_FILENO, F_GETFD);
    require(descriptor_flags != -1 && (descriptor_flags & FD_CLOEXEC) != 0,
            "child setup changed parent fd 0 flags");
}

void test_exit_status_decode(const fs::path& test_dir) {
    int source_fd = open_source(test_dir / "status-source", "unused");
    require(run_command_with_stdin_fd("exit 37", source_fd) == 37,
            "normal exit status was not decoded");
    require(run_command_with_stdin_fd("kill -TERM $$", source_fd) == 128 + SIGTERM,
            "signal exit status was not decoded");
    close(source_fd);
}

enum class SignalTarget {
    WorkerOnly,
    ProcessGroup,
};

void test_signal_wait_contract(
        const fs::path& test_dir,
        const std::string& case_name,
        int signal_number,
        SignalTarget target,
        int expected_helper_status) {
    fs::path marker_path = test_dir / (case_name + "-marker");
    fs::path fifo_path = test_dir / (case_name + "-release");
    require(mkfifo(fifo_path.c_str(), 0600) == 0,
            "failed to create signal test FIFO for " + case_name);

    int source_fd = open_source(test_dir / (case_name + "-source"), "unused");
    int result_pipe[2];
    if(pipe2(result_pipe, O_CLOEXEC) == -1) {
        int pipe_error = errno;
        close(source_fd);
        throw std::system_error(pipe_error, std::generic_category(),
                                "pipe2 signal test result for " + case_name);
    }

    std::string command = ": > " + shell_quote(marker_path.string()) +
                          "; IFS= read -r release < " + shell_quote(fifo_path.string()) +
                          "; exit " + std::to_string(EXPECTED_CHILD_STATUS);
    pid_t worker_pid = fork();
    if(worker_pid == -1) {
        int fork_error = errno;
        close(result_pipe[0]);
        close(result_pipe[1]);
        close(source_fd);
        throw std::system_error(fork_error, std::generic_category(),
                                "fork signal test worker for " + case_name);
    }
    if(worker_pid == 0) {
        close(result_pipe[0]);
        run_signal_worker(command, source_fd, result_pipe[1]);
    }

    close(result_pipe[1]);
    close(source_fd);
    ProcessGroupGuard worker_guard(worker_pid);
    if(setpgid(worker_pid, worker_pid) == -1) {
        int group_error = errno;
        close(result_pipe[0]);
        throw std::system_error(group_error, std::generic_category(),
                                "set signal test worker process group for " + case_name);
    }

    wait_for_marker(marker_path);
    pid_t signal_target = target == SignalTarget::WorkerOnly ? worker_pid : -worker_pid;
    require(kill(signal_target, signal_number) == 0,
            "failed to send signal for " + case_name);
    if(target == SignalTarget::WorkerOnly) release_fifo_reader(fifo_path);

    int worker_status = worker_guard.wait_for_worker();
    WorkerResult worker_result;
    bool has_worker_result = read_full(result_pipe[0], &worker_result, sizeof(worker_result));
    close(result_pipe[0]);

    require(WIFEXITED(worker_status) && WEXITSTATUS(worker_status) == 0,
            "signal test worker exited before completing helper checks for " + case_name);
    require(has_worker_result, "signal test worker did not report results for " + case_name);
    require(worker_result.setup_errno == 0,
            "signal test worker setup failed for " + case_name + ": " +
                    std::strerror(worker_result.setup_errno));
    require(worker_result.helper_status == expected_helper_status,
            "unexpected helper status for " + case_name + ": " +
                    std::to_string(worker_result.helper_status));
    require(worker_result.post_helper_wait_result == -1 &&
                    worker_result.post_helper_wait_errno == ECHILD,
            "helper did not reap the worker's direct child for " + case_name);

    wait_for_process_group_exit(worker_pid);
    worker_guard.release();
}

void test_parent_signal_state_restoration(const fs::path& test_dir) {
    SignalStateGuard signal_guard;

    struct sigaction sigint_action {};
    sigint_action.sa_handler = restored_sigint_handler;
    sigint_action.sa_flags = SA_RESTART;
    require(sigemptyset(&sigint_action.sa_mask) == 0 &&
                    sigaddset(&sigint_action.sa_mask, SIGUSR1) == 0 &&
                    sigaddset(&sigint_action.sa_mask, SIGTERM) == 0,
            "failed to initialize SIGINT restoration fixture");
    require(sigaction(SIGINT, &sigint_action, nullptr) == 0,
            "failed to install SIGINT restoration fixture");

    struct sigaction sigquit_action {};
    sigquit_action.sa_handler = restored_sigquit_handler;
    sigquit_action.sa_flags = SA_NODEFER;
    require(sigemptyset(&sigquit_action.sa_mask) == 0 &&
                    sigaddset(&sigquit_action.sa_mask, SIGUSR2) == 0 &&
                    sigaddset(&sigquit_action.sa_mask, SIGCHLD) == 0,
            "failed to initialize SIGQUIT restoration fixture");
    require(sigaction(SIGQUIT, &sigquit_action, nullptr) == 0,
            "failed to install SIGQUIT restoration fixture");

    int source_fd = open_source(test_dir / "signal-restoration-source", "unused");
    for(int mask_case = 0; mask_case < 2; ++mask_case) {
        bool block_sigchld = mask_case == 1;
        sigset_t configured_mask;
        require(sigprocmask(SIG_SETMASK, nullptr, &configured_mask) == 0,
                "failed to read signal restoration fixture mask");
        require(sigaddset(&configured_mask, SIGUSR2) == 0,
                "failed to add SIGUSR2 to signal restoration fixture mask");
        int update_result = block_sigchld ? sigaddset(&configured_mask, SIGCHLD) :
                                            sigdelset(&configured_mask, SIGCHLD);
        require(update_result == 0, "failed to configure SIGCHLD restoration fixture");
        require(sigprocmask(SIG_SETMASK, &configured_mask, nullptr) == 0,
                "failed to install signal restoration fixture mask");

        struct sigaction expected_sigint {};
        struct sigaction expected_sigquit {};
        sigset_t         expected_mask;
        require(sigaction(SIGINT, nullptr, &expected_sigint) == 0 &&
                        sigaction(SIGQUIT, nullptr, &expected_sigquit) == 0 &&
                        sigprocmask(SIG_SETMASK, nullptr, &expected_mask) == 0,
                "failed to capture configured parent signal state");

        int expected_status = block_sigchld ? EXPECTED_CHILD_STATUS : 0;
        std::string command = "exit " + std::to_string(expected_status);
        require(run_command_with_stdin_fd(command, source_fd) == expected_status,
                "signal restoration command returned an unexpected status");

        struct sigaction actual_sigint {};
        struct sigaction actual_sigquit {};
        sigset_t         actual_mask;
        require(sigaction(SIGINT, nullptr, &actual_sigint) == 0 &&
                        sigaction(SIGQUIT, nullptr, &actual_sigquit) == 0 &&
                        sigprocmask(SIG_SETMASK, nullptr, &actual_mask) == 0,
                "failed to capture restored parent signal state");
        require_sigaction_equal(actual_sigint, expected_sigint, "SIGINT restoration");
        require_sigaction_equal(actual_sigquit, expected_sigquit, "SIGQUIT restoration");
        require_signal_sets_equal(actual_mask, expected_mask, "process signal mask restoration");
        require(sigismember(&actual_mask, SIGCHLD) == (block_sigchld ? 1 : 0),
                "SIGCHLD block state was not preserved");
    }
    close(source_fd);
    require(signal_guard.restore(), "failed to restore test process signal state");
}

} // namespace

int main() {
    try {
        TemporaryDirectory test_dir;
        test_current_offset_and_borrowed_ownership(test_dir.path());
        test_parent_stdin_is_unchanged(test_dir.path());
        test_source_fd_zero_with_close_on_exec(test_dir.path());
        test_exit_status_decode(test_dir.path());
        test_parent_signal_state_restoration(test_dir.path());
        test_signal_wait_contract(test_dir.path(), "parent-sigint", SIGINT,
                                  SignalTarget::WorkerOnly, EXPECTED_CHILD_STATUS);
        test_signal_wait_contract(test_dir.path(), "parent-sigquit", SIGQUIT,
                                  SignalTarget::WorkerOnly, EXPECTED_CHILD_STATUS);
        test_signal_wait_contract(test_dir.path(), "process-group-sigint", SIGINT,
                                  SignalTarget::ProcessGroup, 128 + SIGINT);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "process stdin fd tests: all checks passed\n";
    return 0;
}
