#include "process.hpp"

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
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

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

} // namespace

int main() {
    try {
        TemporaryDirectory test_dir;
        test_current_offset_and_borrowed_ownership(test_dir.path());
        test_parent_stdin_is_unchanged(test_dir.path());
        test_source_fd_zero_with_close_on_exec(test_dir.path());
        test_exit_status_decode(test_dir.path());
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "process stdin fd tests: all checks passed\n";
    return 0;
}
