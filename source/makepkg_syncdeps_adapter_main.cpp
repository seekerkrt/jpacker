#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "makepkg_syncdeps_adapter_protocol.hpp"
#include "makepkg_syncdeps_adapter_state.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <limits>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <linux/sched.h>

#ifndef MOGUET_MAKEPKG_SYNCDEPS_ADAPTER_PATH
#error "MOGUET_MAKEPKG_SYNCDEPS_ADAPTER_PATH is required"
#endif

extern char** environ;

namespace {

constexpr std::string_view SUDO_PATH = "/usr/bin/sudo";
constexpr int ROLE_TERMINATION_GRACE_MILLISECONDS = 1000;
constexpr int SECURITY_FIXTURE_TIMEOUT_MILLISECONDS = 5000;
constexpr std::string_view SECURITY_REJECTION_CREDENTIAL_PID =
    "CredentialPidMismatch";
constexpr std::string_view SECURITY_REJECTION_SESSION_TOKEN =
    "SessionTokenMismatch";
constexpr std::string_view SECURITY_REJECTION_RETAINED_PIDFD =
    "RetainedPidfdDead";

bool is_sec003_validation_scenario(std::string_view scenario) noexcept {
    return scenario == "peer-exit" || scenario == "old-packet-new-peer" ||
           scenario == "cross-session-replacement";
}

bool sec003_scenario_requires_pid_replacement(
    std::string_view scenario) noexcept {
    return scenario == "old-packet-new-peer" ||
           scenario == "cross-session-replacement";
}

class OwnedDescriptor final {
public:
    OwnedDescriptor() noexcept = default;
    explicit OwnedDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {
    }
    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        reset();
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
    }
    ~OwnedDescriptor() {
        reset();
    }

    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

private:
    void reset() noexcept {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = -1;
    }

    int descriptor_ = -1;
};

[[noreturn]] void throw_runtime_error(const std::string& action) {
    const int error_number = errno;
    throw MakepkgSyncdepsAdapterStateError(
        action + ": " + std::strerror(error_number));
}

[[noreturn]] void throw_runtime_error_message(const std::string& message) {
    throw MakepkgSyncdepsAdapterStateError(message);
}

void write_all(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while(offset < bytes.size()) {
        const ssize_t written = write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if(written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if(written == -1 && errno == EINTR) continue;
        throw_runtime_error("unable to write protocol output");
    }
}

std::string read_to_end_bounded(
    int descriptor, std::size_t maximum_bytes) {
    std::string output;
    std::array<char, 4096> buffer{};
    while(true) {
        const ssize_t count = read(descriptor, buffer.data(), buffer.size());
        if(count > 0) {
            const std::size_t count_size = static_cast<std::size_t>(count);
            if(output.size() > maximum_bytes ||
               count_size > maximum_bytes - output.size()) {
                throw_runtime_error_message("protocol output is too large");
            }
            output.append(buffer.data(), count_size);
            continue;
        }
        if(count == 0) return output;
        if(errno == EINTR) continue;
        throw_runtime_error("unable to read protocol output");
    }
}

void require_root_owned_component(
    int descriptor, mode_t expected_type,
    std::optional<mode_t> exact_mode,
    const std::string& description) {
    struct stat metadata{};
    if(fstat(descriptor, &metadata) == -1) {
        throw_runtime_error("unable to inspect " + description);
    }
    if((metadata.st_mode & S_IFMT) != expected_type || metadata.st_uid != 0) {
        throw_runtime_error_message(
            description + " is not root-owned with the expected type");
    }
    const mode_t permissions = metadata.st_mode & 07777;
    if(exact_mode.has_value()) {
        if(permissions != *exact_mode) {
            throw_runtime_error_message(
                description + " has unexpected permissions");
        }
    } else if((permissions & (S_IWGRP | S_IWOTH)) != 0) {
        throw_runtime_error_message(
            description + " is group- or world-writable");
    }
}

void require_named_descriptor_identity(
    int parent_fd, const std::string& name, int descriptor,
    const std::string& description) {
    struct stat opened{};
    struct stat named{};
    if(fstat(descriptor, &opened) == -1 ||
       fstatat(
           parent_fd, name.c_str(), &named,
           AT_SYMLINK_NOFOLLOW) == -1) {
        throw_runtime_error("unable to revalidate " + description);
    }
    if(opened.st_dev != named.st_dev || opened.st_ino != named.st_ino ||
       opened.st_mode != named.st_mode || opened.st_uid != named.st_uid) {
        throw_runtime_error_message(description + " identity changed");
    }
}

std::vector<std::string> split_absolute_path(std::string_view path) {
    if(path.empty() || path.front() != '/' || path == "/") {
        throw_runtime_error_message("installed adapter path is invalid");
    }
    std::vector<std::string> components;
    std::size_t offset = 1;
    while(offset <= path.size()) {
        const std::size_t separator = path.find('/', offset);
        const std::size_t end = separator == std::string_view::npos
                                    ? path.size()
                                    : separator;
        const std::string_view component = path.substr(offset, end - offset);
        if(component.empty() || component == "." || component == "..") {
            throw_runtime_error_message(
                "installed adapter path has an unsafe component");
        }
        components.emplace_back(component);
        if(separator == std::string_view::npos) break;
        offset = separator + 1;
    }
    return components;
}

struct InstalledExecutable {
    OwnedDescriptor descriptor;
    MakepkgSyncdepsInstalledExecutableIdentity identity;
};

InstalledExecutable open_installed_executable() {
    const std::vector<std::string> components =
        split_absolute_path(MOGUET_MAKEPKG_SYNCDEPS_ADAPTER_PATH);
    const int root_fd = open(
        "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_fd == -1) throw_runtime_error("unable to open filesystem root");
    OwnedDescriptor current(root_fd);
    require_root_owned_component(
        current.get(), S_IFDIR, std::nullopt, "filesystem root");
    for(std::size_t index = 0; index + 1U < components.size(); ++index) {
        const int next_fd = openat(
            current.get(), components[index].c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(next_fd == -1) {
            throw_runtime_error("unable to open installed adapter ancestor");
        }
        OwnedDescriptor next(next_fd);
        require_root_owned_component(
            next.get(), S_IFDIR, std::nullopt,
            "installed adapter ancestor");
        require_named_descriptor_identity(
            current.get(), components[index], next.get(),
            "installed adapter ancestor");
        current = std::move(next);
    }
    const int executable_fd = openat(
        current.get(), components.back().c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(executable_fd == -1) {
        throw_runtime_error("unable to open installed adapter executable");
    }
    OwnedDescriptor executable(executable_fd);
    require_root_owned_component(
        executable.get(), S_IFREG, 0755,
        "installed adapter executable");
    require_named_descriptor_identity(
        current.get(), components.back(), executable.get(),
        "installed adapter executable");
    struct stat metadata{};
    if(fstat(executable.get(), &metadata) == -1) {
        throw_runtime_error("unable to inspect installed adapter executable");
    }
    return InstalledExecutable{
        std::move(executable),
        {static_cast<std::uint64_t>(metadata.st_dev),
         static_cast<std::uint64_t>(metadata.st_ino)}};
}

InstalledExecutable require_installed_self() {
    InstalledExecutable installed = open_installed_executable();
    MakepkgSyncdepsObservedProcess self =
        observe_makepkg_syncdeps_process(getpid());
    if(self.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           self.executable, installed.identity)) {
        throw_runtime_error_message(
            "traced, build-tree, or replaced executable cannot establish authority");
    }
    return installed;
}

bool has_unsafe_loader_environment() noexcept {
    if(environ == nullptr) return true;
    for(char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view value(*entry);
        const std::size_t separator = value.find('=');
        const std::string_view name = value.substr(0, separator);
        if(name.starts_with("LD_") || name == "GLIBC_TUNABLES") return true;
    }
    return false;
}

template <typename Integer>
std::optional<Integer> parse_environment_integer(const char* value) {
    if(value == nullptr || *value == '\0' || *value == '-' || *value == '+') {
        return std::nullopt;
    }
    const std::string_view bytes(value);
    Integer parsed{};
    const auto result = std::from_chars(
        bytes.data(), bytes.data() + bytes.size(), parsed);
    if(result.ec != std::errc() ||
       result.ptr != bytes.data() + bytes.size()) {
        return std::nullopt;
    }
    return parsed;
}

struct SudoInvocationIdentity {
    uid_t uid = 0;
    gid_t gid = 0;
};

SudoInvocationIdentity require_sudo_invocation_identity() {
    if(getuid() != 0 || geteuid() != 0 || getgid() != 0 || getegid() != 0) {
        throw_runtime_error_message("root synthetic supervisor is required");
    }
    const auto uid = parse_environment_integer<unsigned long>(getenv("SUDO_UID"));
    const auto gid = parse_environment_integer<unsigned long>(getenv("SUDO_GID"));
    if(!uid.has_value() || *uid == 0 ||
       *uid > static_cast<unsigned long>(std::numeric_limits<uid_t>::max()) ||
       !gid.has_value() ||
       *gid > static_cast<unsigned long>(std::numeric_limits<gid_t>::max())) {
        throw_runtime_error_message(
            "root synthetic supervisor has no exact sudo invoker identity");
    }
    return SudoInvocationIdentity{
        static_cast<uid_t>(*uid), static_cast<gid_t>(*gid)};
}

struct CredentialedPacket {
    std::string payload;
    struct ucred credentials{};
};

void enable_packet_credentials(int descriptor) {
    const int enabled = 1;
    if(setsockopt(
           descriptor, SOL_SOCKET, SO_PASSCRED, &enabled,
           sizeof(enabled)) == -1) {
        throw_runtime_error("unable to enable exact packet credentials");
    }
}

struct RoleChannelPair {
    OwnedDescriptor supervisor;
    OwnedDescriptor role;
};

RoleChannelPair create_role_channel_pair() {
    std::array<int, 2> descriptors{};
    if(socketpair(
           AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
           descriptors.data()) == -1) {
        throw_runtime_error("unable to create exact role channel");
    }
    OwnedDescriptor supervisor(descriptors[0]);
    OwnedDescriptor role(descriptors[1]);
    enable_packet_credentials(supervisor.get());
    enable_packet_credentials(role.get());
    return RoleChannelPair{std::move(supervisor), std::move(role)};
}

void send_packet_bytes(int descriptor, std::string_view bytes) {
    if(bytes.empty() ||
       bytes.size() > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES + 1U) {
        throw_runtime_error_message("role packet has an invalid size");
    }
    ssize_t sent;
    do {
        sent = send(descriptor, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    } while(sent == -1 && errno == EINTR);
    if(sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
        throw_runtime_error("unable to send exact role packet");
    }
}

CredentialedPacket receive_credentialed_packet(int descriptor) {
    std::vector<char> buffer(MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES + 1U);
    std::array<char, CMSG_SPACE(sizeof(struct ucred))> control{};
    struct iovec payload_iovec{buffer.data(), buffer.size()};
    struct msghdr message{};
    message.msg_iov = &payload_iovec;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    ssize_t count;
    do {
        count = recvmsg(descriptor, &message, MSG_CMSG_CLOEXEC);
    } while(count == -1 && errno == EINTR);
    if(count <= 0 || static_cast<std::size_t>(count) > buffer.size() ||
       (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        throw_runtime_error_message(
            "exact role packet is malformed or truncated");
    }
    std::optional<struct ucred> credentials;
    for(struct cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr;
        header = CMSG_NXTHDR(&message, header)) {
        if(header->cmsg_level == SOL_SOCKET &&
           header->cmsg_type == SCM_CREDENTIALS &&
           header->cmsg_len == CMSG_LEN(sizeof(struct ucred)) &&
           !credentials.has_value()) {
            struct ucred observed{};
            std::memcpy(&observed, CMSG_DATA(header), sizeof(observed));
            credentials = observed;
            continue;
        }
        throw_runtime_error_message(
            "exact role packet has unexpected ancillary authority");
    }
    if(!credentials.has_value() || credentials->pid <= 0) {
        throw_runtime_error_message(
            "exact role packet has no kernel sender identity");
    }
    return CredentialedPacket{
        std::string(buffer.data(), static_cast<std::size_t>(count)),
        *credentials};
}

void require_security_fixture_readable(
    int descriptor, const std::string& description) {
    struct pollfd event{
        descriptor, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    int result;
    do {
        result = poll(
            &event, 1, SECURITY_FIXTURE_TIMEOUT_MILLISECONDS);
    } while(result == -1 && errno == EINTR);
    if(result == -1) {
        throw_runtime_error("unable to poll " + description);
    }
    if(result == 0 || (event.revents & POLLIN) == 0) {
        throw_runtime_error_message(description + " did not become readable");
    }
}

CredentialedPacket receive_security_fixture_packet(
    int descriptor, const std::string& description) {
    require_security_fixture_readable(descriptor, description);
    return receive_credentialed_packet(descriptor);
}

void require_exact_role_sender(
    const CredentialedPacket& packet,
    const MakepkgSyncdepsPidfd& expected_process, uid_t expected_uid,
    gid_t expected_gid, const InstalledExecutable& installed,
    const std::string& role_description) {
    if(packet.credentials.pid != expected_process.identity().pid ||
       packet.credentials.uid != expected_uid ||
       packet.credentials.gid != expected_gid ||
       !expected_process.is_alive()) {
        throw_runtime_error_message(
            role_description + " packet sender is not the retained role");
    }
    if(geteuid() != 0 && expected_uid == 0) {
        // The dropped transaction adapter must not receive CAP_SYS_PTRACE.
        // Its root-supervisor authority is the inherited exact pidfd plus the
        // non-rebindable socketpair and per-packet kernel credentials. The
        // root supervisor already verified its own installed executable
        // before creating either capability.
        return;
    }
    const MakepkgSyncdepsRetainedProcessObservation observed =
        observe_makepkg_syncdeps_retained_process(expected_process);
    if(observed.uid != expected_uid || observed.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           observed.executable, installed.identity) ||
       !expected_process.is_alive()) {
        throw_runtime_error_message(
            role_description + " execution identity changed");
    }
}

void send_role_request(
    int descriptor, const MakepkgSyncdepsAdapterInvocation& invocation) {
    send_packet_bytes(
        descriptor, serialize_makepkg_syncdeps_adapter_request(invocation));
}

MakepkgSyncdepsAdapterInvocation parse_role_request(
    const CredentialedPacket& packet) {
    const MakepkgSyncdepsAdapterInvocationResult parsed =
        parse_makepkg_syncdeps_adapter_request(packet.payload);
    const auto* invocation =
        std::get_if<MakepkgSyncdepsAdapterInvocation>(&parsed);
    if(invocation == nullptr) {
        throw_runtime_error_message("exact role request protocol is invalid");
    }
    return *invocation;
}

void send_role_response(
    int descriptor, bool success, std::string_view payload = {}) {
    if(payload.size() + 1U > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES) {
        throw_runtime_error_message("exact role response is too large");
    }
    std::string packet;
    packet.reserve(payload.size() + 1U);
    packet.push_back(success ? '\0' : '\1');
    packet.append(payload);
    send_packet_bytes(descriptor, packet);
}

std::pair<bool, std::string> receive_role_response(
    int descriptor, const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed) {
    const CredentialedPacket packet = receive_credentialed_packet(descriptor);
    require_exact_role_sender(
        packet, supervisor, 0, 0, installed, "root supervisor");
    if(packet.payload.empty() ||
       (packet.payload[0] != '\0' && packet.payload[0] != '\1')) {
        throw_runtime_error_message("exact role response status is invalid");
    }
    return {packet.payload[0] == '\0', packet.payload.substr(1)};
}

bool send_request_expect_status(
    int descriptor, const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const MakepkgSyncdepsAdapterInvocation& invocation,
    std::string* response = nullptr) {
    send_role_request(descriptor, invocation);
    auto [success, payload] =
        receive_role_response(descriptor, supervisor, installed);
    if(response != nullptr) *response = std::move(payload);
    return success;
}

void send_barrier_release(int descriptor) {
    const char release = 'R';
    ssize_t sent;
    do {
        sent = send(descriptor, &release, 1, MSG_NOSIGNAL);
    } while(sent == -1 && errno == EINTR);
    if(sent != 1) throw_runtime_error("unable to release role barrier");
}

void await_barrier_release(int descriptor) {
    char release = 0;
    ssize_t count;
    do {
        count = recv(descriptor, &release, 1, 0);
    } while(count == -1 && errno == EINTR);
    if(count != 1 || release != 'R') {
        throw_runtime_error_message("role barrier was not released");
    }
}

void send_pidfd_signal(
    const MakepkgSyncdepsPidfd& process, int signal_number) {
#ifdef SYS_pidfd_send_signal
    long result;
    do {
        result = syscall(
            SYS_pidfd_send_signal, process.descriptor(), signal_number,
            nullptr, 0U);
    } while(result == -1 && errno == EINTR);
    if(result == -1 && errno != ESRCH) {
        throw_runtime_error("unable to signal exact role process");
    }
#else
    static_cast<void>(process);
    static_cast<void>(signal_number);
    throw_runtime_error_message(
        "pidfd_send_signal is unavailable for exact role cleanup");
#endif
}

bool wait_for_pidfd_exit(
    const MakepkgSyncdepsPidfd& process, int timeout_milliseconds) {
    struct pollfd event{
        process.descriptor(),
        static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
    int result;
    do {
        result = poll(&event, 1, timeout_milliseconds);
    } while(result == -1 && errno == EINTR);
    if(result == -1) throw_runtime_error("unable to poll exact role process");
    return result > 0;
}

int wait_child(pid_t pid) {
    int status = 0;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while(result == -1 && errno == EINTR);
    if(result == -1) throw_runtime_error("unable to wait for child process");
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 255;
}

void reap_if_child(pid_t pid) noexcept {
    if(pid <= 0) return;
    int status = 0;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while(result == -1 && errno == EINTR);
    static_cast<void>(result);
}

struct SpawnedProcess {
    pid_t pid = -1;
    OwnedDescriptor output;
};

[[noreturn]] void execute_sudo(
    const std::vector<std::string>& adapter_arguments) {
    std::vector<std::string> owned_arguments{
        std::string(SUDO_PATH), "--",
        MOGUET_MAKEPKG_SYNCDEPS_ADAPTER_PATH};
    owned_arguments.insert(
        owned_arguments.end(), adapter_arguments.begin(),
        adapter_arguments.end());
    std::vector<char*> argv;
    argv.reserve(owned_arguments.size() + 1U);
    for(std::string& argument : owned_arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::array<char, 14> path_environment{};
    std::copy_n("PATH=/usr/bin", 13, path_environment.begin());
    std::array<char, 9> locale_environment{};
    std::copy_n("LC_ALL=C", 8, locale_environment.begin());
    std::array<char*, 3> environment{
        path_environment.data(), locale_environment.data(), nullptr};
    execve(std::string(SUDO_PATH).c_str(), argv.data(), environment.data());
    _exit(127);
}

SpawnedProcess spawn_sudo_with_output(
    const std::vector<std::string>& adapter_arguments) {
    std::array<int, 2> pipe_descriptors{};
    if(pipe2(pipe_descriptors.data(), O_CLOEXEC) == -1) {
        throw_runtime_error("unable to create root supervisor output pipe");
    }
    OwnedDescriptor read_end(pipe_descriptors[0]);
    OwnedDescriptor write_end(pipe_descriptors[1]);
    const pid_t child = fork();
    if(child == -1) throw_runtime_error("unable to fork sudo process");
    if(child == 0) {
        if(dup2(write_end.get(), STDOUT_FILENO) == -1) _exit(127);
        read_end = OwnedDescriptor();
        write_end = OwnedDescriptor();
        execute_sudo(adapter_arguments);
    }
    write_end = OwnedDescriptor();
    return SpawnedProcess{child, std::move(read_end)};
}

struct CapturedProcessResult {
    std::string output;
    int exit_code = 0;
};

CapturedProcessResult run_sudo_capture(
    const std::vector<std::string>& adapter_arguments) {
    SpawnedProcess process = spawn_sudo_with_output(adapter_arguments);
    const std::string output = read_to_end_bounded(
        process.output.get(), MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES);
    process.output = OwnedDescriptor();
    return CapturedProcessResult{output, wait_child(process.pid)};
}

void drop_to_invoking_identity(
    const SudoInvocationIdentity& invoking_identity, pid_t expected_parent) {
    if(prctl(PR_SET_PDEATHSIG, SIGKILL) == -1 ||
       getppid() != expected_parent ||
       prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1 ||
       setgroups(0, nullptr) == -1 ||
       setresgid(
           invoking_identity.gid, invoking_identity.gid,
           invoking_identity.gid) == -1 ||
       setresuid(
           invoking_identity.uid, invoking_identity.uid,
           invoking_identity.uid) == -1 ||
       prctl(PR_SET_DUMPABLE, 0) == -1 ||
       getuid() != invoking_identity.uid ||
       geteuid() != invoking_identity.uid ||
       getgid() != invoking_identity.gid ||
       getegid() != invoking_identity.gid || getppid() != expected_parent) {
        _exit(126);
    }
}

MakepkgSyncdepsAdapterInvocation transaction_prepare_invocation(
    const std::string& session_token, std::size_t ordinal,
    std::string specification) {
    MakepkgSyncdepsAdapterInvocation invocation;
    invocation.command = MakepkgSyncdepsAdapterCommand::TransactionPrepare;
    invocation.session_token = session_token;
    invocation.ordinal = ordinal;
    invocation.dependency_specifications.push_back(std::move(specification));
    return invocation;
}

enum class Sec003FixtureRoleKind {
    Hold,
    ExpectedPeer,
    CrossSessionA,
    CrossSessionB,
};

struct Sec003FixtureRole {
    pid_t pid = -1;
    MakepkgSyncdepsPidfd pidfd;
    OwnedDescriptor release;
    OwnedDescriptor status;
    bool reaped = false;
};

struct Sec003ReplacementProcess {
    pid_t pid = -1;
    MakepkgSyncdepsObservedProcess observed;
    bool reaped = false;
};

void send_security_fixture_status(int descriptor, char status) {
    send_packet_bytes(descriptor, std::string_view(&status, 1));
}

void expect_security_fixture_status(
    int descriptor, char expected, const std::string& description) {
    require_security_fixture_readable(descriptor, description);
    char status = 0;
    ssize_t count;
    do {
        count = recv(descriptor, &status, 1, 0);
    } while(count == -1 && errno == EINTR);
    if(count != 1 || status != expected) {
        throw_runtime_error_message(description + " returned an invalid checkpoint");
    }
}

void close_fixture_descriptor(int descriptor) noexcept {
    if(descriptor >= 0) static_cast<void>(close(descriptor));
}

int run_sec003_fixture_role(
    Sec003FixtureRoleKind role_kind, int own_transaction_channel,
    int other_transaction_channel, int status_descriptor,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const std::string& session_token,
    const std::string& other_session_token) {
    if(role_kind == Sec003FixtureRoleKind::Hold) {
        while(true)
            pause();
    }
    if(role_kind == Sec003FixtureRoleKind::ExpectedPeer) {
        send_role_request(
            own_transaction_channel,
            transaction_prepare_invocation(
                session_token, 1, "expected-peer-exit"));
        send_security_fixture_status(status_descriptor, 'P');
        return 0;
    }
    if(role_kind == Sec003FixtureRoleKind::CrossSessionB) {
        std::string response;
        if(send_request_expect_status(
               other_transaction_channel, supervisor, installed,
               transaction_prepare_invocation(
                   other_session_token, 1, "b-role-to-a-channel"),
               &response) ||
           response != SECURITY_REJECTION_CREDENTIAL_PID) {
            return 31;
        }
        response.clear();
        if(send_request_expect_status(
               own_transaction_channel, supervisor, installed,
               transaction_prepare_invocation(
                   other_session_token, 1, "b-role-with-a-token"),
               &response) ||
           response != SECURITY_REJECTION_SESSION_TOKEN) {
            return 32;
        }
        send_security_fixture_status(status_descriptor, 'B');
        while(true)
            pause();
    }

    std::string response;
    if(send_request_expect_status(
           own_transaction_channel, supervisor, installed,
           transaction_prepare_invocation(
               other_session_token, 1, "a-role-with-b-token"),
           &response) ||
       response != SECURITY_REJECTION_SESSION_TOKEN) {
        return 33;
    }
    send_role_request(
        own_transaction_channel,
        transaction_prepare_invocation(
            session_token, 1, "stale-a-request"));
    send_security_fixture_status(status_descriptor, 'P');
    return 0;
}

Sec003FixtureRole spawn_sec003_fixture_role(
    Sec003FixtureRoleKind role_kind, int own_transaction_channel,
    int other_transaction_channel, int security_control_channel,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity,
    const std::string& session_token,
    const std::string& other_session_token) {
    std::array<int, 2> release_descriptors{};
    std::array<int, 2> status_descriptors{};
    if(socketpair(
           AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
           release_descriptors.data()) == -1 ||
       socketpair(
           AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
           status_descriptors.data()) == -1) {
        throw_runtime_error("unable to create SEC-003 fixture role barriers");
    }
    OwnedDescriptor release_parent(release_descriptors[0]);
    OwnedDescriptor release_child(release_descriptors[1]);
    OwnedDescriptor status_parent(status_descriptors[0]);
    OwnedDescriptor status_child(status_descriptors[1]);
    const pid_t launcher_pid = getpid();
    const pid_t role_pid = fork();
    if(role_pid == -1) {
        throw_runtime_error("unable to fork SEC-003 fixture role");
    }
    if(role_pid == 0) {
        release_parent = OwnedDescriptor();
        status_parent = OwnedDescriptor();
        close_fixture_descriptor(security_control_channel);
        if(role_kind == Sec003FixtureRoleKind::Hold) {
            close_fixture_descriptor(own_transaction_channel);
            if(other_transaction_channel != own_transaction_channel) {
                close_fixture_descriptor(other_transaction_channel);
            }
        }
        drop_to_invoking_identity(invoking_identity, launcher_pid);
        try {
            send_security_fixture_status(status_child.get(), 'D');
            await_barrier_release(release_child.get());
            _exit(run_sec003_fixture_role(
                role_kind, own_transaction_channel,
                other_transaction_channel, status_child.get(), supervisor,
                installed, session_token, other_session_token));
        } catch(...) {
            _exit(34);
        }
    }
    release_child = OwnedDescriptor();
    status_child = OwnedDescriptor();
    try {
        expect_security_fixture_status(
            status_parent.get(), 'D', "SEC-003 fixture role drop");
        return Sec003FixtureRole{
            role_pid, MakepkgSyncdepsPidfd::open(role_pid),
            std::move(release_parent), std::move(status_parent), false};
    } catch(...) {
        static_cast<void>(kill(role_pid, SIGKILL));
        reap_if_child(role_pid);
        throw;
    }
}

void release_sec003_fixture_role(Sec003FixtureRole& role) {
    send_barrier_release(role.release.get());
    role.release = OwnedDescriptor();
}

int reap_sec003_fixture_role(Sec003FixtureRole& role) {
    if(role.reaped) return 0;
    const int exit_code = wait_child(role.pid);
    role.reaped = true;
    return exit_code;
}

void terminate_sec003_fixture_role(Sec003FixtureRole& role) noexcept {
    try {
        if(role.pidfd.is_alive()) {
            send_pidfd_signal(role.pidfd, SIGKILL);
        }
    } catch(...) {
    }
    if(!role.reaped) {
        reap_if_child(role.pid);
        role.reaped = true;
    }
}

Sec003ReplacementProcess spawn_sec003_pid_replacement(
    const MakepkgSyncdepsPidfdIdentity& replaced_identity,
    int transaction_channel_a, int transaction_channel_b,
    int security_control_channel,
    const SudoInvocationIdentity& invoking_identity,
    const InstalledExecutable& installed) {
    if(replaced_identity.pid <= 0) {
        throw_runtime_error_message("SEC-003 replaced pid is invalid");
    }
    std::array<int, 2> status_descriptors{};
    if(socketpair(
           AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
           status_descriptors.data()) == -1) {
        throw_runtime_error(
            "unable to create SEC-003 replacement checkpoint");
    }
    OwnedDescriptor status_parent(status_descriptors[0]);
    OwnedDescriptor status_child(status_descriptors[1]);
#ifdef SYS_clone3
    pid_t requested_pid = replaced_identity.pid;
    struct clone_args arguments{};
    arguments.exit_signal = SIGCHLD;
    arguments.set_tid = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&requested_pid));
    arguments.set_tid_size = 1;
    long clone_result;
    do {
        clone_result = syscall(
            SYS_clone3, &arguments, sizeof(arguments));
    } while(clone_result == -1 && errno == EINTR);
    if(clone_result == -1) {
        throw_runtime_error(
            "unable to create deterministic SEC-003 PID replacement");
    }
    if(clone_result == 0) {
        status_parent = OwnedDescriptor();
        close_fixture_descriptor(transaction_channel_a);
        if(transaction_channel_b != transaction_channel_a) {
            close_fixture_descriptor(transaction_channel_b);
        }
        close_fixture_descriptor(security_control_channel);
        drop_to_invoking_identity(invoking_identity, getppid());
        try {
            send_security_fixture_status(status_child.get(), 'D');
            while(true)
                pause();
        } catch(...) {
            _exit(35);
        }
    }
    status_child = OwnedDescriptor();
    const pid_t replacement_pid = static_cast<pid_t>(clone_result);
    try {
        if(replacement_pid != replaced_identity.pid) {
            throw_runtime_error_message(
                "SEC-003 replacement did not reuse the expected numeric PID");
        }
        expect_security_fixture_status(
            status_parent.get(), 'D', "SEC-003 PID replacement drop");
        MakepkgSyncdepsObservedProcess replacement =
            observe_makepkg_syncdeps_process(replacement_pid);
        if(replacement.parent_pid != getpid() || replacement.tracer_pid != 0 ||
           replacement.pidfd.identity().uid != invoking_identity.uid ||
           replacement.pidfd.identity().pid != replaced_identity.pid ||
           makepkg_syncdeps_pidfd_identity_matches(
               replacement.pidfd.identity(), replaced_identity) ||
           !makepkg_syncdeps_executable_identity_matches(
               replacement.executable, installed.identity)) {
            throw_runtime_error_message(
                "SEC-003 PID replacement identity is invalid");
        }
        return Sec003ReplacementProcess{
            replacement_pid, std::move(replacement), false};
    } catch(...) {
        static_cast<void>(kill(replacement_pid, SIGKILL));
        reap_if_child(replacement_pid);
        throw;
    }
#else
    static_cast<void>(transaction_channel_a);
    static_cast<void>(transaction_channel_b);
    static_cast<void>(security_control_channel);
    static_cast<void>(invoking_identity);
    static_cast<void>(installed);
    throw_runtime_error_message(
        "clone3 is unavailable for deterministic SEC-003 PID replacement");
#endif
}

void terminate_sec003_replacement(
    Sec003ReplacementProcess& replacement) noexcept {
    try {
        if(replacement.observed.pidfd.is_alive()) {
            send_pidfd_signal(replacement.observed.pidfd, SIGKILL);
        }
    } catch(...) {
    }
    if(!replacement.reaped) {
        reap_if_child(replacement.pid);
        replacement.reaped = true;
    }
}

int run_transaction_adapter_normal(
    int channel, const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed, const std::string& session_token,
    std::size_t transaction_count) {
    for(std::size_t ordinal = 1; ordinal <= transaction_count; ++ordinal) {
        std::string prepared_protocol;
        const bool prepared_success = send_request_expect_status(
            channel, supervisor, installed,
            transaction_prepare_invocation(
                session_token, ordinal,
                "synthetic-dependency-" + std::to_string(ordinal)),
            &prepared_protocol);
        if(ordinal > 2) return prepared_success ? 92 : 91;
        if(!prepared_success) return 81;
        const auto parsed =
            parse_makepkg_syncdeps_transaction_prepare_response(
                prepared_protocol);
        const auto* response =
            std::get_if<MakepkgSyncdepsTransactionPrepareResponse>(&parsed);
        if(response == nullptr || response->session_token != session_token ||
           response->ordinal != ordinal) {
            return 82;
        }
        MakepkgSyncdepsAdapterInvocation record;
        record.command = MakepkgSyncdepsAdapterCommand::TransactionRecord;
        record.session_token = session_token;
        record.ordinal = ordinal;
        record.transaction_token = response->transaction_token;
        record.synthetic_observation =
            MakepkgSyncdepsSyntheticObservation::Observed;
        if(!send_request_expect_status(channel, supervisor, installed, record)) {
            return 83;
        }
        MakepkgSyncdepsAdapterInvocation finalize;
        finalize.command = MakepkgSyncdepsAdapterCommand::TransactionFinalize;
        finalize.session_token = session_token;
        finalize.ordinal = ordinal;
        finalize.transaction_token = response->transaction_token;
        finalize.command_outcome = MakepkgSyncdepsCommandOutcome::Succeeded;
        finalize.exit_code = 0;
        if(!send_request_expect_status(
               channel, supervisor, installed, finalize)) {
            return 84;
        }
        MakepkgSyncdepsAdapterInvocation consume;
        consume.command = MakepkgSyncdepsAdapterCommand::TransactionConsume;
        consume.session_token = session_token;
        consume.ordinal = ordinal;
        consume.transaction_token = response->transaction_token;
        if(!send_request_expect_status(
               channel, supervisor, installed, consume)) {
            return 85;
        }
        if(send_request_expect_status(
               channel, supervisor, installed, consume)) {
            return 86;
        }
    }
    return 0;
}

int descendant_probe(
    int channel, const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const MakepkgSyncdepsAdapterInvocation& request,
    bool read_response) {
    try {
        send_role_request(channel, request);
        if(!read_response) return 0;
        const auto [success, payload] =
            receive_role_response(channel, supervisor, installed);
        return !success && payload.empty() ? 0 : 1;
    } catch(...) {
        return 2;
    }
}

int run_descendant_security_probe(
    int channel, const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed, const std::string& session_token,
    std::string_view scenario) {
    if(is_sec003_validation_scenario(scenario)) return 9;
    MakepkgSyncdepsAdapterInvocation request =
        transaction_prepare_invocation(session_token, 1, "security-probe");
    if(prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) return 10;
    if(scenario == "intermediate-parent-exit") {
        const pid_t intermediate = fork();
        if(intermediate == -1) return 11;
        if(intermediate == 0) {
            const pid_t probe = fork();
            if(probe == -1) _exit(12);
            if(probe == 0) {
                static_cast<void>(usleep(50000));
                _exit(descendant_probe(
                    channel, supervisor, installed, request, true));
            }
            _exit(0);
        }
        if(wait_child(intermediate) != 0) return 13;
        int status = 0;
        pid_t result;
        do {
            result = waitpid(-1, &status, 0);
        } while(result == -1 && errno == EINTR);
        return result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0
                   ? 0
                   : 14;
    }
    const std::size_t probe_count =
        scenario == "old-packet-new-peer" ? 2U : 1U;
    for(std::size_t index = 0; index < probe_count; ++index) {
        const pid_t probe = fork();
        if(probe == -1) return 15;
        if(probe == 0) {
            const bool read_response = scenario != "peer-exit";
            _exit(descendant_probe(
                channel, supervisor, installed, request, read_response));
        }
        const int exit_code = wait_child(probe);
        if(exit_code != 0) return 16;
        if(scenario == "peer-exit") {
            const auto [success, payload] =
                receive_role_response(channel, supervisor, installed);
            if(success || !payload.empty()) return 17;
        }
    }
    return 0;
}

[[noreturn]] void exec_post_prepare_probe(
    int channel, const std::string& request_protocol) {
    const int flags = fcntl(channel, F_GETFD);
    if(flags == -1 || fcntl(channel, F_SETFD, flags & ~FD_CLOEXEC) == -1) {
        _exit(18);
    }
    const std::string script =
        "printf %s \"$MOGUET_ROLE_PACKET\" >&" +
        std::to_string(channel) + "; sleep 1";
    std::vector<std::string> arguments{"/bin/sh", "-c", script};
    std::vector<char*> argv;
    for(std::string& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::string packet_environment =
        "MOGUET_ROLE_PACKET=" + request_protocol;
    std::array<char, 14> path_environment{};
    std::copy_n("PATH=/usr/bin", 13, path_environment.begin());
    std::array<char*, 3> environment{
        path_environment.data(), packet_environment.data(), nullptr};
    execve("/bin/sh", argv.data(), environment.data());
    _exit(19);
}

int run_transaction_adapter(
    int channel, int barrier_descriptor,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed, const std::string& session_token,
    const MakepkgSyncdepsAdapterInvocation& invocation) {
    await_barrier_release(barrier_descriptor);
    if(invocation.command ==
       MakepkgSyncdepsAdapterCommand::RootSyntheticSession) {
        return run_transaction_adapter_normal(
            channel, supervisor, installed, session_token,
            invocation.synthetic_transaction_count);
    }
    const std::string& scenario = invocation.synthetic_security_scenario;
    if(scenario == "post-exec") {
        exec_post_prepare_probe(
            channel,
            serialize_makepkg_syncdeps_adapter_request(
                transaction_prepare_invocation(
                    session_token, 1, "post-exec-probe")));
    }
    return run_descendant_security_probe(
        channel, supervisor, installed, session_token, scenario);
}

int run_bound_child(
    int transaction_channel, int launcher_channel,
    int child_barrier_descriptor, int adapter_barrier_descriptor,
    int adapter_pid_output, int probe_output,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity,
    const std::string& session_token,
    const MakepkgSyncdepsAdapterInvocation& invocation,
    pid_t launcher_pid) {
    drop_to_invoking_identity(invoking_identity, launcher_pid);
    const pid_t child_pid = getpid();
    const pid_t adapter_pid = fork();
    if(adapter_pid == -1) return 20;
    if(adapter_pid == 0) {
        if(prctl(PR_SET_PDEATHSIG, SIGKILL) == -1 ||
           getppid() != child_pid) {
            _exit(21);
        }
        static_cast<void>(close(child_barrier_descriptor));
        static_cast<void>(close(adapter_pid_output));
        static_cast<void>(close(probe_output));
        static_cast<void>(close(launcher_channel));
        try {
            _exit(run_transaction_adapter(
                transaction_channel, adapter_barrier_descriptor,
                supervisor, installed, session_token, invocation));
        } catch(...) {
            _exit(22);
        }
    }
    static_cast<void>(close(adapter_barrier_descriptor));
    static_cast<void>(close(transaction_channel));
    if(write(adapter_pid_output, &adapter_pid, sizeof(adapter_pid)) !=
       static_cast<ssize_t>(sizeof(adapter_pid))) {
        return 23;
    }
    static_cast<void>(close(adapter_pid_output));
    if(invocation.synthetic_security_scenario == "barrier-failure") {
        static_cast<void>(close(child_barrier_descriptor));
        while(true)
            pause();
    }
    await_barrier_release(child_barrier_descriptor);
    if(invocation.synthetic_security_scenario == "child-session") {
        MakepkgSyncdepsAdapterInvocation request;
        request.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
        request.session_token = session_token;
        request.launcher_pid = launcher_pid;
        const bool rejected = !send_request_expect_status(
            launcher_channel, supervisor, installed, request);
        const char result = rejected ? '1' : '0';
        static_cast<void>(write(probe_output, &result, 1));
        return rejected ? 0 : 24;
    }
    static_cast<void>(close(launcher_channel));
    static_cast<void>(close(probe_output));
    const int adapter_exit = wait_child(adapter_pid);
    if(invocation.command ==
           MakepkgSyncdepsAdapterCommand::RootSyntheticSession &&
       invocation.synthetic_hold && adapter_exit == 0) {
        while(true)
            pause();
    }
    return adapter_exit;
}

struct SpawnedRoleTree {
    pid_t child_pid = -1;
    pid_t adapter_pid = -1;
    MakepkgSyncdepsPidfd child_pidfd;
    MakepkgSyncdepsPidfd adapter_pidfd;
    OwnedDescriptor child_barrier;
    OwnedDescriptor adapter_barrier;
    OwnedDescriptor probe_input;
};

SpawnedRoleTree spawn_role_tree(
    int launcher_channel, int transaction_channel,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity,
    const std::string& session_token,
    const MakepkgSyncdepsAdapterInvocation& invocation) {
    std::array<int, 2> child_barrier{};
    std::array<int, 2> adapter_barrier{};
    std::array<int, 2> adapter_pid_pipe{};
    std::array<int, 2> probe_pipe{};
    if(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
                  child_barrier.data()) == -1 ||
       socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0,
                  adapter_barrier.data()) == -1 ||
       pipe2(adapter_pid_pipe.data(), O_CLOEXEC) == -1 ||
       pipe2(probe_pipe.data(), O_CLOEXEC) == -1) {
        throw_runtime_error("unable to create exact role barriers");
    }
    OwnedDescriptor child_barrier_send(child_barrier[0]);
    OwnedDescriptor child_barrier_receive(child_barrier[1]);
    OwnedDescriptor adapter_barrier_send(adapter_barrier[0]);
    OwnedDescriptor adapter_barrier_receive(adapter_barrier[1]);
    OwnedDescriptor adapter_pid_input(adapter_pid_pipe[0]);
    OwnedDescriptor adapter_pid_output(adapter_pid_pipe[1]);
    OwnedDescriptor probe_input(probe_pipe[0]);
    OwnedDescriptor probe_output(probe_pipe[1]);
    const pid_t child_pid = fork();
    if(child_pid == -1) throw_runtime_error("unable to fork bound child role");
    if(child_pid == 0) {
        child_barrier_send = OwnedDescriptor();
        adapter_barrier_send = OwnedDescriptor();
        adapter_pid_input = OwnedDescriptor();
        probe_input = OwnedDescriptor();
        static_cast<void>(close(STDOUT_FILENO));
        try {
            _exit(run_bound_child(
                transaction_channel, launcher_channel,
                child_barrier_receive.get(), adapter_barrier_receive.get(),
                adapter_pid_output.get(), probe_output.get(), supervisor,
                installed, invoking_identity, session_token, invocation,
                getppid()));
        } catch(...) {
            _exit(25);
        }
    }
    child_barrier_receive = OwnedDescriptor();
    adapter_barrier_receive = OwnedDescriptor();
    adapter_pid_output = OwnedDescriptor();
    probe_output = OwnedDescriptor();
    try {
        pid_t adapter_pid = -1;
        std::size_t offset = 0;
        while(offset < sizeof(adapter_pid)) {
            const ssize_t count = read(
                adapter_pid_input.get(),
                reinterpret_cast<char*>(&adapter_pid) + offset,
                sizeof(adapter_pid) - offset);
            if(count > 0) {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if(count == -1 && errno == EINTR) continue;
            throw_runtime_error_message(
                "bound child did not publish transaction adapter pid");
        }
        if(adapter_pid <= 0) {
            throw_runtime_error_message("transaction adapter pid is invalid");
        }
        return SpawnedRoleTree{
            child_pid, adapter_pid, MakepkgSyncdepsPidfd::open(child_pid),
            MakepkgSyncdepsPidfd::open(adapter_pid),
            std::move(child_barrier_send), std::move(adapter_barrier_send),
            std::move(probe_input)};
    } catch(...) {
        child_barrier_send = OwnedDescriptor();
        adapter_barrier_send = OwnedDescriptor();
        static_cast<void>(kill(child_pid, SIGKILL));
        reap_if_child(child_pid);
        while(true) {
            pid_t reaped;
            do {
                reaped = waitpid(-1, nullptr, 0);
            } while(reaped == -1 && errno == EINTR);
            if(reaped == -1 && errno == ECHILD) break;
            if(reaped == -1) break;
        }
        throw;
    }
}

void terminate_role_tree(SpawnedRoleTree& roles) noexcept {
    try {
        if(roles.adapter_pidfd.is_alive()) {
            send_pidfd_signal(roles.adapter_pidfd, SIGTERM);
            if(!wait_for_pidfd_exit(
                   roles.adapter_pidfd,
                   ROLE_TERMINATION_GRACE_MILLISECONDS)) {
                send_pidfd_signal(roles.adapter_pidfd, SIGKILL);
                static_cast<void>(wait_for_pidfd_exit(
                    roles.adapter_pidfd,
                    ROLE_TERMINATION_GRACE_MILLISECONDS));
            }
        }
        if(roles.child_pidfd.is_alive()) {
            send_pidfd_signal(roles.child_pidfd, SIGTERM);
            if(!wait_for_pidfd_exit(
                   roles.child_pidfd,
                   ROLE_TERMINATION_GRACE_MILLISECONDS)) {
                send_pidfd_signal(roles.child_pidfd, SIGKILL);
                static_cast<void>(wait_for_pidfd_exit(
                    roles.child_pidfd,
                    ROLE_TERMINATION_GRACE_MILLISECONDS));
            }
        }
    } catch(...) {
        try {
            if(roles.adapter_pidfd.is_alive()) {
                send_pidfd_signal(roles.adapter_pidfd, SIGKILL);
            }
            if(roles.child_pidfd.is_alive()) {
                send_pidfd_signal(roles.child_pidfd, SIGKILL);
            }
        } catch(...) {
        }
    }
    reap_if_child(roles.child_pid);
    reap_if_child(roles.adapter_pid);
}

int wait_bound_child_or_supervisor(
    SpawnedRoleTree& roles, const MakepkgSyncdepsPidfd& supervisor) {
    std::array<struct pollfd, 2> events{{
        {roles.child_pidfd.descriptor(),
         static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
        {supervisor.descriptor(),
         static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
    }};
    while(true) {
        int result;
        do {
            result = poll(events.data(), events.size(), -1);
        } while(result == -1 && errno == EINTR);
        if(result == -1) throw_runtime_error("unable to poll exact role lifetimes");
        if(events[1].revents != 0) {
            terminate_role_tree(roles);
            throw_runtime_error_message(
                "root supervisor exited before bound child completion");
        }
        if(events[0].revents != 0) return wait_child(roles.child_pid);
    }
}

void require_manifest_matches_root_session(
    const MakepkgSyncdepsSessionManifest& manifest,
    const std::string& session_token,
    const SudoInvocationIdentity& invoking_identity,
    const InstalledExecutable& installed,
    const MakepkgSyncdepsPidfd& supervisor,
    const MakepkgSyncdepsPidfd& launcher,
    const SpawnedRoleTree& roles, std::size_t requested_count,
    MakepkgSyncdepsCommandOutcome expected_outcome, int expected_exit) {
    const std::size_t expected_count = std::min<std::size_t>(requested_count, 2);
    const bool expected_unsupported = requested_count > 2;
    if(manifest.prepared.session_token != session_token ||
       manifest.binding.session_token != session_token ||
       manifest.terminal.session_token != session_token ||
       manifest.prepared.invoking_uid != invoking_identity.uid ||
       !makepkg_syncdeps_executable_identity_matches(
           manifest.prepared.installed_executable, installed.identity) ||
       !makepkg_syncdeps_pidfd_identity_matches(
           manifest.prepared.supervisor, supervisor.identity()) ||
       !makepkg_syncdeps_pidfd_identity_matches(
           manifest.prepared.launcher, launcher.identity()) ||
       !makepkg_syncdeps_pidfd_identity_matches(
           manifest.binding.child, roles.child_pidfd.identity()) ||
       !makepkg_syncdeps_pidfd_identity_matches(
           manifest.binding.transaction_adapter,
           roles.adapter_pidfd.identity()) ||
       manifest.terminal.makepkg_outcome != expected_outcome ||
       manifest.terminal.makepkg_exit_code != expected_exit ||
       manifest.terminal.transaction_count != expected_count ||
       manifest.transactions.size() != expected_count ||
       manifest.evidence_kind != MakepkgSyncdepsEvidenceKind::Synthetic ||
       (manifest.terminal.terminal_state ==
        MakepkgSyncdepsTerminalState::Unsupported) != expected_unsupported ||
       (manifest.terminal.coverage ==
        MakepkgSyncdepsAdapterCoverage::Unsupported) != expected_unsupported) {
        throw_runtime_error_message(
            "root session manifest does not match exact retained roles");
    }
    for(std::size_t index = 0; index < manifest.transactions.size(); ++index) {
        const auto& transaction = manifest.transactions[index];
        if(transaction.prepared.session_token != session_token ||
           transaction.observation.session_token != session_token ||
           transaction.outcome.session_token != session_token ||
           transaction.prepared.ordinal != index + 1U ||
           transaction.observation.ordinal != index + 1U ||
           transaction.outcome.ordinal != index + 1U ||
           transaction.observation.observation !=
               MakepkgSyncdepsSyntheticObservation::Observed ||
           transaction.outcome.outcome !=
               MakepkgSyncdepsCommandOutcome::Succeeded ||
           transaction.outcome.exit_code != 0) {
            throw_runtime_error_message(
                "root session manifest transaction semantics changed");
        }
    }
}

int run_root_launcher(
    const MakepkgSyncdepsAdapterInvocation& invocation,
    const SudoInvocationIdentity& invoking_identity,
    const InstalledExecutable& installed, int launcher_channel,
    int transaction_channel,
    const MakepkgSyncdepsPidfd& supervisor) {
    if(prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) {
        throw_runtime_error("unable to establish launcher subreaper boundary");
    }
    const MakepkgSyncdepsRetainedProcessObservation supervisor_observation =
        observe_makepkg_syncdeps_retained_process(supervisor);
    if(supervisor_observation.uid != 0 ||
       supervisor_observation.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           supervisor_observation.executable, installed.identity)) {
        throw_runtime_error_message(
            "root supervisor execution identity is invalid");
    }
    const CredentialedPacket prepared_packet =
        receive_credentialed_packet(launcher_channel);
    require_exact_role_sender(
        prepared_packet, supervisor, 0, 0, installed, "root supervisor");
    const auto prepared = parse_makepkg_syncdeps_session_prepare_response(
        prepared_packet.payload);
    const auto* session =
        std::get_if<MakepkgSyncdepsSessionPrepareResponse>(&prepared);
    if(session == nullptr || session->invoking_uid != invoking_identity.uid) {
        throw_runtime_error_message("root session prepare response is invalid");
    }
    SpawnedRoleTree roles = spawn_role_tree(
        launcher_channel, transaction_channel, supervisor, installed,
        invoking_identity, session->session_token, invocation);
    bool session_retired = false;
    bool child_reaped = false;
    try {
        if(invocation.synthetic_security_scenario == "launcher-transaction") {
            if(send_request_expect_status(
                   launcher_channel, supervisor, installed,
                   transaction_prepare_invocation(
                       session->session_token, 1,
                       "launcher-role-probe"))) {
                throw_runtime_error_message("launcher obtained transaction role");
            }
            terminate_role_tree(roles);
            child_reaped = true;
            MakepkgSyncdepsAdapterInvocation abort;
            abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
            abort.session_token = session->session_token;
            abort.launcher_pid = getpid();
            if(!send_request_expect_status(
                   launcher_channel, supervisor, installed, abort)) {
                throw_runtime_error_message("security session abort failed");
            }
            session_retired = true;
            return 0;
        }
        MakepkgSyncdepsAdapterInvocation bind;
        bind.command = MakepkgSyncdepsAdapterCommand::SessionBind;
        bind.session_token = session->session_token;
        bind.launcher_pid = getpid();
        bind.child_pid = roles.child_pid;
        bind.transaction_adapter_pid = roles.adapter_pid;
        const bool bind_success = send_request_expect_status(
            launcher_channel, supervisor, installed, bind);
        if(invocation.synthetic_security_scenario == "bind-failure") {
            if(bind_success) {
                throw_runtime_error_message("synthetic bind failure was accepted");
            }
            terminate_role_tree(roles);
            child_reaped = true;
            MakepkgSyncdepsAdapterInvocation abort;
            abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
            abort.session_token = session->session_token;
            abort.launcher_pid = getpid();
            if(!send_request_expect_status(
                   launcher_channel, supervisor, installed, abort)) {
                throw_runtime_error_message("failed-bind session abort failed");
            }
            session_retired = true;
            return 0;
        }
        if(!bind_success) {
            throw_runtime_error_message("root session role binding failed");
        }
        if(invocation.synthetic_security_scenario == "launcher-failure") {
            terminate_role_tree(roles);
            child_reaped = true;
            return 93;
        }
        if(invocation.synthetic_security_scenario == "barrier-failure") {
            bool rejected = false;
            try {
                send_barrier_release(roles.child_barrier.get());
            } catch(const std::exception&) {
                rejected = true;
            }
            if(!rejected) {
                throw_runtime_error_message(
                    "closed child barrier unexpectedly accepted release");
            }
            terminate_role_tree(roles);
            child_reaped = true;
            MakepkgSyncdepsAdapterInvocation abort;
            abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
            abort.session_token = session->session_token;
            abort.launcher_pid = getpid();
            if(!send_request_expect_status(
                   launcher_channel, supervisor, installed, abort)) {
                throw_runtime_error_message(
                    "barrier-failure session abort failed");
            }
            session_retired = true;
            return 0;
        }
        if(invocation.synthetic_security_scenario == "child-session") {
            send_barrier_release(roles.child_barrier.get());
            char result = 0;
            ssize_t count;
            do {
                count = read(roles.probe_input.get(), &result, 1);
            } while(count == -1 && errno == EINTR);
            if(count != 1 || result != '1') {
                throw_runtime_error_message(
                    "bound child obtained launcher session role");
            }
            terminate_role_tree(roles);
            child_reaped = true;
            MakepkgSyncdepsAdapterInvocation abort;
            abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
            abort.session_token = session->session_token;
            abort.launcher_pid = getpid();
            if(!send_request_expect_status(
                   launcher_channel, supervisor, installed, abort)) {
                throw_runtime_error_message("child-probe session abort failed");
            }
            session_retired = true;
            return 0;
        }
        send_barrier_release(roles.adapter_barrier.get());
        send_barrier_release(roles.child_barrier.get());
        const int child_exit = wait_bound_child_or_supervisor(roles, supervisor);
        child_reaped = true;
        if(invocation.command ==
           MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity) {
            MakepkgSyncdepsAdapterInvocation abort;
            abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
            abort.session_token = session->session_token;
            abort.launcher_pid = getpid();
            if(!send_request_expect_status(
                   launcher_channel, supervisor, installed, abort)) {
                throw_runtime_error_message("security session abort failed");
            }
            session_retired = true;
            return child_exit;
        }
        const MakepkgSyncdepsCommandOutcome outcome =
            child_exit == 0 ? MakepkgSyncdepsCommandOutcome::Succeeded
                            : MakepkgSyncdepsCommandOutcome::Failed;
        MakepkgSyncdepsAdapterInvocation finalize;
        finalize.command = MakepkgSyncdepsAdapterCommand::SessionFinalize;
        finalize.session_token = session->session_token;
        finalize.launcher_pid = getpid();
        finalize.command_outcome = outcome;
        finalize.exit_code = child_exit;
        if(!send_request_expect_status(
               launcher_channel, supervisor, installed, finalize)) {
            throw_runtime_error_message("root session finalize failed");
        }
        MakepkgSyncdepsAdapterInvocation consume;
        consume.command = MakepkgSyncdepsAdapterCommand::SessionConsume;
        consume.session_token = session->session_token;
        consume.launcher_pid = getpid();
        std::string manifest_protocol;
        if(!send_request_expect_status(
               launcher_channel, supervisor, installed, consume,
               &manifest_protocol)) {
            throw_runtime_error_message("root session consume failed");
        }
        session_retired = true;
        const auto parsed_manifest =
            parse_makepkg_syncdeps_session_manifest(manifest_protocol);
        const auto* manifest =
            std::get_if<MakepkgSyncdepsSessionManifest>(&parsed_manifest);
        if(manifest == nullptr) {
            throw_runtime_error_message("root session manifest is invalid");
        }
        MakepkgSyncdepsObservedProcess launcher =
            observe_makepkg_syncdeps_process(getpid());
        require_manifest_matches_root_session(
            *manifest, session->session_token, invoking_identity, installed,
            supervisor, launcher.pidfd, roles,
            invocation.synthetic_transaction_count, outcome, child_exit);
        write_all(STDOUT_FILENO, manifest_protocol);
        return child_exit == 0 || invocation.synthetic_transaction_count == 3
                   ? 0
                   : child_exit;
    } catch(...) {
        if(!child_reaped) terminate_role_tree(roles);
        if(!session_retired && supervisor.is_alive()) {
            try {
                MakepkgSyncdepsAdapterInvocation abort;
                abort.command = MakepkgSyncdepsAdapterCommand::SessionAbort;
                abort.session_token = session->session_token;
                abort.launcher_pid = getpid();
                session_retired = send_request_expect_status(
                    launcher_channel, supervisor, installed, abort);
            } catch(...) {
            }
        }
        throw;
    }
}

enum class RoleChannelKind { Launcher,
                             TransactionAdapter };

bool command_allowed_for_role(
    MakepkgSyncdepsAdapterCommand command, RoleChannelKind role) noexcept {
    if(role == RoleChannelKind::Launcher) {
        return command == MakepkgSyncdepsAdapterCommand::SessionBind ||
               command == MakepkgSyncdepsAdapterCommand::SessionFinalize ||
               command == MakepkgSyncdepsAdapterCommand::SessionConsume ||
               command == MakepkgSyncdepsAdapterCommand::SessionAbort;
    }
    return command == MakepkgSyncdepsAdapterCommand::TransactionPrepare ||
           command == MakepkgSyncdepsAdapterCommand::TransactionRecord ||
           command == MakepkgSyncdepsAdapterCommand::TransactionFinalize ||
           command == MakepkgSyncdepsAdapterCommand::TransactionConsume ||
           command == MakepkgSyncdepsAdapterCommand::TransactionAbort;
}

struct SessionServeResult {
    bool retired = false;
    bool security_rejection_observed = false;
};

SessionServeResult serve_private_session(
    MakepkgSyncdepsAdapterStateStore& store,
    const MakepkgSyncdepsAdapterInvocation& root_invocation,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity,
    int launcher_channel, int transaction_channel,
    const MakepkgSyncdepsPidfd& launcher) {
    std::optional<MakepkgSyncdepsObservedProcess> child;
    std::optional<MakepkgSyncdepsObservedProcess> transaction_adapter;
    bool transaction_channel_closed = false;
    SessionServeResult result;
    while(!result.retired) {
        std::array<struct pollfd, 3> events{{
            {launcher_channel,
             static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
            {transaction_channel_closed ? -1 : transaction_channel,
             static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
            {launcher.descriptor(),
             static_cast<short>(POLLIN | POLLHUP | POLLERR), 0},
        }};
        int poll_result;
        do {
            poll_result = poll(events.data(), events.size(), -1);
        } while(poll_result == -1 && errno == EINTR);
        if(poll_result == -1) {
            throw_runtime_error("unable to poll private session channels");
        }
        if(events[2].revents != 0) {
            store.abort_session();
            result.retired = true;
            break;
        }
        int channel = -1;
        RoleChannelKind role = RoleChannelKind::Launcher;
        if((events[0].revents & POLLIN) != 0) {
            channel = launcher_channel;
        } else if((events[1].revents & POLLIN) != 0) {
            channel = transaction_channel;
            role = RoleChannelKind::TransactionAdapter;
        } else {
            if(events[1].revents != 0) transaction_channel_closed = true;
            if(events[0].revents != 0) {
                store.abort_session();
                result.retired = true;
            }
            continue;
        }
        try {
            const CredentialedPacket packet = receive_credentialed_packet(channel);
            try {
                if(role == RoleChannelKind::Launcher) {
                    require_exact_role_sender(
                        packet, launcher, 0, 0, installed, "root launcher");
                } else {
                    if(!transaction_adapter.has_value()) {
                        throw_runtime_error_message(
                            "transaction adapter role is not bound");
                    }
                    require_exact_role_sender(
                        packet, transaction_adapter->pidfd,
                        invoking_identity.uid, invoking_identity.gid,
                        installed, "transaction adapter");
                }
                const MakepkgSyncdepsAdapterInvocation invocation =
                    parse_role_request(packet);
                if(!command_allowed_for_role(invocation.command, role) ||
                   invocation.session_token != store.session_token()) {
                    throw_runtime_error_message(
                        "root verb is not authorized for this exact role");
                }
                if(invocation.command ==
                   MakepkgSyncdepsAdapterCommand::SessionBind) {
                    if(child.has_value() || transaction_adapter.has_value() ||
                       invocation.launcher_pid != launcher.identity().pid) {
                        throw_runtime_error_message(
                            "session role bind is not one-shot");
                    }
                    if(root_invocation.synthetic_security_scenario ==
                       "bind-failure") {
                        result.security_rejection_observed = true;
                        send_role_response(channel, false);
                        continue;
                    }
                    MakepkgSyncdepsObservedProcess observed_child =
                        observe_makepkg_syncdeps_process(invocation.child_pid);
                    MakepkgSyncdepsObservedProcess observed_adapter =
                        observe_makepkg_syncdeps_process(
                            invocation.transaction_adapter_pid);
                    if(observed_child.parent_pid != launcher.identity().pid ||
                       observed_adapter.parent_pid !=
                           observed_child.pidfd.identity().pid ||
                       observed_child.pidfd.identity().uid !=
                           invoking_identity.uid ||
                       observed_adapter.pidfd.identity().uid !=
                           invoking_identity.uid ||
                       observed_child.tracer_pid != 0 ||
                       observed_adapter.tracer_pid != 0 ||
                       !makepkg_syncdeps_executable_identity_matches(
                           observed_child.executable, installed.identity) ||
                       !makepkg_syncdeps_executable_identity_matches(
                           observed_adapter.executable, installed.identity)) {
                        throw_runtime_error_message(
                            "bound role provenance is invalid");
                    }
                    store.bind_child(MakepkgSyncdepsBoundChildState{
                        store.session_token(), observed_child.pidfd.identity(),
                        observed_adapter.pidfd.identity()});
                    child = std::move(observed_child);
                    transaction_adapter = std::move(observed_adapter);
                    send_role_response(channel, true);
                    continue;
                }
                std::string response;
                switch(invocation.command) {
                    case MakepkgSyncdepsAdapterCommand::TransactionPrepare:
                        response =
                            serialize_makepkg_syncdeps_transaction_prepare_response(
                                store.prepare_transaction(
                                    invocation.ordinal,
                                    invocation.dependency_specifications));
                        break;
                    case MakepkgSyncdepsAdapterCommand::TransactionRecord:
                        store.record_transaction(
                            invocation.ordinal, invocation.transaction_token,
                            invocation.synthetic_observation);
                        break;
                    case MakepkgSyncdepsAdapterCommand::TransactionFinalize:
                        if(!invocation.exit_code.has_value()) {
                            throw_runtime_error_message(
                                "transaction outcome has no exit code");
                        }
                        store.finalize_transaction(
                            invocation.ordinal, invocation.transaction_token,
                            invocation.command_outcome,
                            *invocation.exit_code);
                        break;
                    case MakepkgSyncdepsAdapterCommand::TransactionConsume:
                        store.consume_transaction(
                            invocation.ordinal, invocation.transaction_token);
                        break;
                    case MakepkgSyncdepsAdapterCommand::TransactionAbort:
                        store.abort_transaction(
                            invocation.ordinal, invocation.transaction_token);
                        break;
                    case MakepkgSyncdepsAdapterCommand::SessionFinalize:
                        if(invocation.launcher_pid != launcher.identity().pid ||
                           !child.has_value() ||
                           !transaction_adapter.has_value() ||
                           child->pidfd.is_alive() ||
                           transaction_adapter->pidfd.is_alive() ||
                           !invocation.exit_code.has_value()) {
                            throw_runtime_error_message(
                                "session terminal role lifetime is invalid");
                        }
                        store.finalize_session(
                            invocation.command_outcome,
                            *invocation.exit_code);
                        break;
                    case MakepkgSyncdepsAdapterCommand::SessionConsume:
                        if(invocation.launcher_pid != launcher.identity().pid ||
                           !child.has_value() ||
                           !transaction_adapter.has_value() ||
                           child->pidfd.is_alive() ||
                           transaction_adapter->pidfd.is_alive()) {
                            throw_runtime_error_message(
                                "session consume role lifetime is invalid");
                        }
                        response = store.consume_session();
                        result.retired = true;
                        break;
                    case MakepkgSyncdepsAdapterCommand::SessionAbort:
                        if(invocation.launcher_pid != launcher.identity().pid) {
                            throw_runtime_error_message(
                                "session abort launcher identity is invalid");
                        }
                        store.abort_session();
                        result.retired = true;
                        break;
                    case MakepkgSyncdepsAdapterCommand::SessionPrepare:
                    case MakepkgSyncdepsAdapterCommand::SessionBind:
                    case MakepkgSyncdepsAdapterCommand::SyntheticSession:
                    case MakepkgSyncdepsAdapterCommand::RootSyntheticSession:
                    case MakepkgSyncdepsAdapterCommand::SyntheticSecurity:
                    case MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity:
                        throw_runtime_error_message(
                            "invalid command for private role channel");
                }
                send_role_response(channel, true, response);
            } catch(const std::exception&) {
                result.security_rejection_observed = true;
                try {
                    send_role_response(channel, false);
                } catch(...) {
                }
            }
        } catch(const std::exception&) {
            result.security_rejection_observed = true;
        }
    }
    return result;
}

struct Sec003FixtureBoundSession {
    MakepkgSyncdepsObservedProcess child;
    MakepkgSyncdepsObservedProcess expected_role;
};

MakepkgSyncdepsSessionPrepareResponse receive_sec003_prepared_session(
    int security_control_channel,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity) {
    const auto [success, payload] = receive_role_response(
        security_control_channel, supervisor, installed);
    const auto parsed =
        parse_makepkg_syncdeps_session_prepare_response(payload);
    const auto* response =
        std::get_if<MakepkgSyncdepsSessionPrepareResponse>(&parsed);
    if(!success || response == nullptr ||
       response->invoking_uid != invoking_identity.uid) {
        throw_runtime_error_message(
            "SEC-003 fixture session prepare response is invalid");
    }
    return *response;
}

MakepkgSyncdepsAdapterInvocation sec003_bind_invocation(
    const std::string& session_token, pid_t launcher_pid,
    const Sec003FixtureRole& child,
    const Sec003FixtureRole& expected_role) {
    MakepkgSyncdepsAdapterInvocation bind;
    bind.command = MakepkgSyncdepsAdapterCommand::SessionBind;
    bind.session_token = session_token;
    bind.launcher_pid = launcher_pid;
    bind.child_pid = child.pid;
    bind.transaction_adapter_pid = expected_role.pid;
    return bind;
}

void require_sec003_control_response(
    int security_control_channel,
    const MakepkgSyncdepsPidfd& supervisor,
    const InstalledExecutable& installed,
    std::string_view expected_payload) {
    const auto [success, payload] = receive_role_response(
        security_control_channel, supervisor, installed);
    if(!success || payload != expected_payload) {
        throw_runtime_error_message(
            "SEC-003 fixture control response is invalid");
    }
}

int run_sec003_fixture_launcher(
    const MakepkgSyncdepsAdapterInvocation& invocation,
    const SudoInvocationIdentity& invoking_identity,
    const InstalledExecutable& installed,
    int security_control_channel, int transaction_channel_a,
    int transaction_channel_b,
    const MakepkgSyncdepsPidfd& supervisor) {
    if(prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) {
        throw_runtime_error(
            "unable to establish SEC-003 fixture subreaper boundary");
    }
    const MakepkgSyncdepsRetainedProcessObservation supervisor_observation =
        observe_makepkg_syncdeps_retained_process(supervisor);
    if(supervisor_observation.uid != 0 ||
       supervisor_observation.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           supervisor_observation.executable, installed.identity)) {
        throw_runtime_error_message(
            "SEC-003 fixture supervisor identity is invalid");
    }

    const bool cross_session =
        invocation.synthetic_security_scenario ==
        "cross-session-replacement";
    const MakepkgSyncdepsSessionPrepareResponse session_a =
        receive_sec003_prepared_session(
            security_control_channel, supervisor, installed,
            invoking_identity);
    std::optional<MakepkgSyncdepsSessionPrepareResponse> session_b;
    if(cross_session) {
        session_b = receive_sec003_prepared_session(
            security_control_channel, supervisor, installed,
            invoking_identity);
        if(session_b->session_token == session_a.session_token) {
            throw_runtime_error_message(
                "SEC-003 fixture sessions reused one token");
        }
    }

    std::optional<Sec003FixtureRole> child_a;
    std::optional<Sec003FixtureRole> role_a;
    std::optional<Sec003FixtureRole> child_b;
    std::optional<Sec003FixtureRole> role_b;
    std::optional<Sec003ReplacementProcess> replacement;
    try {
        child_a.emplace(spawn_sec003_fixture_role(
            Sec003FixtureRoleKind::Hold, transaction_channel_a,
            transaction_channel_b, security_control_channel, supervisor,
            installed, invoking_identity, session_a.session_token,
            session_b.has_value() ? session_b->session_token
                                  : session_a.session_token));
        role_a.emplace(spawn_sec003_fixture_role(
            cross_session ? Sec003FixtureRoleKind::CrossSessionA
                          : Sec003FixtureRoleKind::ExpectedPeer,
            transaction_channel_a, transaction_channel_b,
            security_control_channel, supervisor, installed,
            invoking_identity, session_a.session_token,
            session_b.has_value() ? session_b->session_token
                                  : session_a.session_token));
        if(cross_session) {
            child_b.emplace(spawn_sec003_fixture_role(
                Sec003FixtureRoleKind::Hold, transaction_channel_b,
                transaction_channel_a, security_control_channel, supervisor,
                installed, invoking_identity, session_b->session_token,
                session_a.session_token));
            role_b.emplace(spawn_sec003_fixture_role(
                Sec003FixtureRoleKind::CrossSessionB,
                transaction_channel_b, transaction_channel_a,
                security_control_channel, supervisor, installed,
                invoking_identity, session_b->session_token,
                session_a.session_token));
        }

        if(!send_request_expect_status(
               security_control_channel, supervisor, installed,
               sec003_bind_invocation(
                   session_a.session_token, getpid(), *child_a, *role_a))) {
            throw_runtime_error_message(
                "SEC-003 fixture Session A bind failed");
        }
        if(cross_session &&
           !send_request_expect_status(
               security_control_channel, supervisor, installed,
               sec003_bind_invocation(
                   session_b->session_token, getpid(), *child_b, *role_b))) {
            throw_runtime_error_message(
                "SEC-003 fixture Session B bind failed");
        }
        require_sec003_control_response(
            security_control_channel, supervisor, installed, "START");

        if(cross_session) {
            release_sec003_fixture_role(*role_b);
            expect_security_fixture_status(
                role_b->status.get(), 'B',
                "SEC-003 Session B cross-session attempts");
            if(!role_b->pidfd.is_alive()) {
                throw_runtime_error_message(
                    "SEC-003 Session B role exited before replacement proof");
            }
        }
        release_sec003_fixture_role(*role_a);
        expect_security_fixture_status(
            role_a->status.get(), 'P',
            "SEC-003 expected-role packet submission");
        if(reap_sec003_fixture_role(*role_a) != 0 ||
           role_a->pidfd.is_alive()) {
            throw_runtime_error_message(
                "SEC-003 expected role did not end before authorization");
        }

        if(sec003_scenario_requires_pid_replacement(
               invocation.synthetic_security_scenario)) {
            replacement.emplace(spawn_sec003_pid_replacement(
                role_a->pidfd.identity(), transaction_channel_a,
                transaction_channel_b, security_control_channel,
                invoking_identity, installed));
        }

        send_packet_bytes(security_control_channel, "R");
        const auto [request_success, rejection] = receive_role_response(
            transaction_channel_a, supervisor, installed);
        if(request_success || rejection != SECURITY_REJECTION_RETAINED_PIDFD) {
            throw_runtime_error_message(
                "SEC-003 stale expected-role request was not rejected by retained pidfd");
        }
        const auto [evidence_success, evidence] = receive_role_response(
            security_control_channel, supervisor, installed);
        const std::string scenario_record =
            "SCENARIO\t" + invocation.synthetic_security_scenario + "\n";
        if(!evidence_success ||
           !evidence.starts_with(
               "MOGUET-MAKEPKG-SYNCDEPS-SECURITY-EVIDENCE\t1\n") ||
           evidence.find(scenario_record) == std::string::npos ||
           !evidence.ends_with("END\n")) {
            throw_runtime_error_message(
                "SEC-003 fixture evidence response is invalid");
        }
        std::cerr << evidence;

        if(replacement.has_value()) {
            terminate_sec003_replacement(*replacement);
        }
        if(role_b.has_value()) terminate_sec003_fixture_role(*role_b);
        if(child_b.has_value()) terminate_sec003_fixture_role(*child_b);
        terminate_sec003_fixture_role(*child_a);
        return 0;
    } catch(...) {
        if(replacement.has_value()) {
            terminate_sec003_replacement(*replacement);
        }
        if(role_a.has_value()) terminate_sec003_fixture_role(*role_a);
        if(role_b.has_value()) terminate_sec003_fixture_role(*role_b);
        if(child_a.has_value()) terminate_sec003_fixture_role(*child_a);
        if(child_b.has_value()) terminate_sec003_fixture_role(*child_b);
        throw;
    }
}

Sec003FixtureBoundSession receive_and_bind_sec003_fixture_session(
    MakepkgSyncdepsAdapterStateStore& store,
    int security_control_channel,
    const MakepkgSyncdepsPidfd& launcher,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity) {
    const CredentialedPacket packet = receive_security_fixture_packet(
        security_control_channel, "SEC-003 fixture bind request");
    require_exact_role_sender(
        packet, launcher, 0, 0, installed, "SEC-003 fixture launcher");
    const MakepkgSyncdepsAdapterInvocation bind = parse_role_request(packet);
    if(bind.command != MakepkgSyncdepsAdapterCommand::SessionBind ||
       bind.session_token != store.session_token() ||
       bind.launcher_pid != launcher.identity().pid) {
        throw_runtime_error_message(
            "SEC-003 fixture bind request identity is invalid");
    }
    MakepkgSyncdepsObservedProcess child =
        observe_makepkg_syncdeps_process(bind.child_pid);
    MakepkgSyncdepsObservedProcess expected_role =
        observe_makepkg_syncdeps_process(bind.transaction_adapter_pid);
    if(child.parent_pid != launcher.identity().pid ||
       expected_role.parent_pid != launcher.identity().pid ||
       child.pidfd.identity().uid != invoking_identity.uid ||
       expected_role.pidfd.identity().uid != invoking_identity.uid ||
       child.tracer_pid != 0 || expected_role.tracer_pid != 0 ||
       makepkg_syncdeps_pidfd_identity_matches(
           child.pidfd.identity(), expected_role.pidfd.identity()) ||
       !makepkg_syncdeps_executable_identity_matches(
           child.executable, installed.identity) ||
       !makepkg_syncdeps_executable_identity_matches(
           expected_role.executable, installed.identity)) {
        throw_runtime_error_message(
            "SEC-003 fixture bound role provenance is invalid");
    }
    store.bind_child(MakepkgSyncdepsBoundChildState{
        store.session_token(), child.pidfd.identity(),
        expected_role.pidfd.identity()});
    send_role_response(security_control_channel, true);
    return Sec003FixtureBoundSession{
        std::move(child), std::move(expected_role)};
}

MakepkgSyncdepsAdapterInvocation require_sec003_transaction_prepare(
    const CredentialedPacket& packet, const std::string& expected_token,
    std::string_view expected_specification) {
    const MakepkgSyncdepsAdapterInvocation request = parse_role_request(packet);
    if(request.command != MakepkgSyncdepsAdapterCommand::TransactionPrepare ||
       request.session_token != expected_token || request.ordinal != 1 ||
       request.dependency_specifications !=
           std::vector<std::string>{std::string(expected_specification)}) {
        throw_runtime_error_message(
            "SEC-003 fixture transaction request is invalid");
    }
    return request;
}

void require_sec003_sender_rejection(
    const CredentialedPacket& packet,
    const MakepkgSyncdepsPidfd& expected_role, uid_t expected_uid,
    gid_t expected_gid, const InstalledExecutable& installed,
    const std::string& description) {
    bool rejected = false;
    try {
        require_exact_role_sender(
            packet, expected_role, expected_uid, expected_gid, installed,
            description);
    } catch(const std::exception&) {
        rejected = true;
    }
    if(!rejected) {
        throw_runtime_error_message(
            description + " unexpectedly passed exact-role authorization");
    }
}

std::string sec003_evidence_report(
    std::string_view scenario,
    const MakepkgSyncdepsAdapterStateStore& store_a,
    const std::optional<MakepkgSyncdepsAdapterStateStore>& store_b,
    const CredentialedPacket& stale_packet,
    const MakepkgSyncdepsPidfdIdentity& old_identity,
    const std::optional<MakepkgSyncdepsObservedProcess>& replacement,
    const std::optional<Sec003FixtureBoundSession>& bound_b,
    int transaction_channel_a, int transaction_channel_b) {
    std::string evidence =
        "MOGUET-MAKEPKG-SYNCDEPS-SECURITY-EVIDENCE\t1\n";
    evidence.append("SCENARIO\t").append(scenario).push_back('\n');
    evidence.append("SESSION_A\t")
        .append(store_a.session_token())
        .push_back('\n');
    evidence.append("EXPECTED_ROLE_PACKET_SUBMITTED\t1\n");
    evidence.append("PACKET_CREDENTIAL_MATCHED_EXPECTED_ROLE\t1\n");
    evidence.append("EXPECTED_PACKET_PID\t")
        .append(std::to_string(stale_packet.credentials.pid))
        .push_back('\n');
    evidence.append("SESSION_A_EXPECTED_ROLE_PID\t")
        .append(std::to_string(old_identity.pid))
        .push_back('\n');
    evidence.append("SENDER_LIFETIME_ENDED\t1\n");
    evidence.append("RETAINED_PIDFD_DEAD_REJECTION\t1\n");
    evidence.append("OLD_PIDFD_INODE\t")
        .append(std::to_string(old_identity.inode))
        .push_back('\n');
    if(replacement.has_value()) {
        evidence.append("PID_REPLACEMENT_OCCURRED\t1\n");
        evidence.append("REPLACEMENT_PID\t")
            .append(std::to_string(replacement->pidfd.identity().pid))
            .push_back('\n');
        evidence.append("REPLACEMENT_PIDFD_INODE\t")
            .append(std::to_string(replacement->pidfd.identity().inode))
            .push_back('\n');
        evidence.append("REPLACEMENT_UID_MATCHED\t1\n");
        evidence.append("REPLACEMENT_EXECUTABLE_MATCHED\t1\n");
    }
    if(store_b.has_value() && bound_b.has_value()) {
        evidence.append("SESSION_B\t")
            .append(store_b->session_token())
            .push_back('\n');
        evidence.append("DISTINCT_LIVE_SESSIONS\t1\n");
        evidence.append("DISTINCT_PRIVATE_CHANNELS\t")
            .append(transaction_channel_a != transaction_channel_b ? "1\n"
                                                                   : "0\n");
        evidence.append("SESSION_B_EXPECTED_ROLE_ALIVE\t")
            .append(bound_b->expected_role.pidfd.is_alive() ? "1\n" : "0\n");
        evidence.append("SESSION_B_EXPECTED_ROLE_PID\t")
            .append(std::to_string(bound_b->expected_role.pidfd.identity().pid))
            .push_back('\n');
        evidence.append("DISTINCT_EXPECTED_ROLE_PIDS\t")
            .append(old_identity.pid !=
                            bound_b->expected_role.pidfd.identity().pid
                        ? "1\n"
                        : "0\n");
        evidence.append("B_ROLE_TO_A_CHANNEL\tRejected\n");
        evidence.append("A_ROLE_WITH_B_TOKEN\tRejected\n");
        evidence.append("B_ROLE_WITH_A_TOKEN\tRejected\n");
        evidence.append("REPLACEMENT_WITH_STALE_A_REQUEST\tRejected\n");
    }
    evidence.append("STATE_MUTATION_COUNT\t0\n");
    evidence.append("POSITIVE_RESPONSE_COUNT\t0\n");
    evidence.append("GUARD\tRetainedPidfdDead\n");
    evidence.append("END\n");
    return evidence;
}

void serve_sec003_validation_fixture(
    MakepkgSyncdepsAdapterStateStore& store_a,
    std::optional<MakepkgSyncdepsAdapterStateStore>& store_b,
    const MakepkgSyncdepsAdapterInvocation& invocation,
    const InstalledExecutable& installed,
    const SudoInvocationIdentity& invoking_identity,
    int security_control_channel, int transaction_channel_a,
    int transaction_channel_b,
    const MakepkgSyncdepsPidfd& launcher) {
    const bool cross_session =
        invocation.synthetic_security_scenario ==
        "cross-session-replacement";
    Sec003FixtureBoundSession bound_a =
        receive_and_bind_sec003_fixture_session(
            store_a, security_control_channel, launcher, installed,
            invoking_identity);
    std::optional<Sec003FixtureBoundSession> bound_b;
    if(cross_session) {
        if(!store_b.has_value()) {
            throw_runtime_error_message(
                "SEC-003 fixture Session B store is missing");
        }
        bound_b.emplace(receive_and_bind_sec003_fixture_session(
            *store_b, security_control_channel, launcher, installed,
            invoking_identity));
    }
    send_role_response(security_control_channel, true, "START");

    if(cross_session) {
        const CredentialedPacket b_to_a = receive_security_fixture_packet(
            transaction_channel_a, "SEC-003 B-role to A-channel request");
        static_cast<void>(require_sec003_transaction_prepare(
            b_to_a, store_a.session_token(), "b-role-to-a-channel"));
        if(b_to_a.credentials.pid !=
               bound_b->expected_role.pidfd.identity().pid ||
           !bound_a.expected_role.pidfd.is_alive() ||
           !bound_b->expected_role.pidfd.is_alive()) {
            throw_runtime_error_message(
                "SEC-003 B-role to A-channel setup is invalid");
        }
        require_sec003_sender_rejection(
            b_to_a, bound_a.expected_role.pidfd, invoking_identity.uid,
            invoking_identity.gid, installed,
            "SEC-003 B-role to A-channel request");
        send_role_response(
            transaction_channel_a, false,
            SECURITY_REJECTION_CREDENTIAL_PID);

        const CredentialedPacket b_with_a_token =
            receive_security_fixture_packet(
                transaction_channel_b,
                "SEC-003 B-role with A-token request");
        require_exact_role_sender(
            b_with_a_token, bound_b->expected_role.pidfd,
            invoking_identity.uid, invoking_identity.gid, installed,
            "SEC-003 Session B expected role");
        static_cast<void>(require_sec003_transaction_prepare(
            b_with_a_token, store_a.session_token(),
            "b-role-with-a-token"));
        if(store_a.session_token() == store_b->session_token()) {
            throw_runtime_error_message(
                "SEC-003 cross-token setup reused one session token");
        }
        send_role_response(
            transaction_channel_b, false,
            SECURITY_REJECTION_SESSION_TOKEN);

        const CredentialedPacket a_with_b_token =
            receive_security_fixture_packet(
                transaction_channel_a,
                "SEC-003 A-role with B-token request");
        require_exact_role_sender(
            a_with_b_token, bound_a.expected_role.pidfd,
            invoking_identity.uid, invoking_identity.gid, installed,
            "SEC-003 Session A expected role");
        static_cast<void>(require_sec003_transaction_prepare(
            a_with_b_token, store_b->session_token(),
            "a-role-with-b-token"));
        send_role_response(
            transaction_channel_a, false,
            SECURITY_REJECTION_SESSION_TOKEN);
    }

    const std::string expected_specification =
        cross_session ? "stale-a-request" : "expected-peer-exit";
    const CredentialedPacket stale_packet = receive_security_fixture_packet(
        transaction_channel_a, "SEC-003 deferred expected-role packet");
    static_cast<void>(require_sec003_transaction_prepare(
        stale_packet, store_a.session_token(), expected_specification));

    const CredentialedPacket checkpoint = receive_security_fixture_packet(
        security_control_channel, "SEC-003 launcher lifetime checkpoint");
    require_exact_role_sender(
        checkpoint, launcher, 0, 0, installed,
        "SEC-003 fixture launcher checkpoint");
    if(checkpoint.payload != "R" ||
       stale_packet.credentials.pid !=
           bound_a.expected_role.pidfd.identity().pid ||
       stale_packet.credentials.uid != invoking_identity.uid ||
       stale_packet.credentials.gid != invoking_identity.gid ||
       bound_a.expected_role.pidfd.is_alive()) {
        throw_runtime_error_message(
            "SEC-003 expected-role lifetime checkpoint is invalid");
    }

    std::optional<MakepkgSyncdepsObservedProcess> replacement;
    if(sec003_scenario_requires_pid_replacement(
           invocation.synthetic_security_scenario)) {
        // This numeric lookup is fixture evidence only. Authorization below
        // remains fixed to bound_a.expected_role.pidfd, which is the dead old
        // handle retained from role creation.
        replacement.emplace(observe_makepkg_syncdeps_process(
            bound_a.expected_role.pidfd.identity().pid));
        if(replacement->parent_pid != launcher.identity().pid ||
           replacement->tracer_pid != 0 ||
           replacement->pidfd.identity().uid != invoking_identity.uid ||
           replacement->pidfd.identity().pid !=
               bound_a.expected_role.pidfd.identity().pid ||
           makepkg_syncdeps_pidfd_identity_matches(
               replacement->pidfd.identity(),
               bound_a.expected_role.pidfd.identity()) ||
           !makepkg_syncdeps_executable_identity_matches(
               replacement->executable, installed.identity)) {
            throw_runtime_error_message(
                "SEC-003 replacement proof did not observe a new exact lifetime");
        }
    }
    if(cross_session && !bound_b->expected_role.pidfd.is_alive()) {
        throw_runtime_error_message(
            "SEC-003 Session B was not live during replacement proof");
    }

    require_sec003_sender_rejection(
        stale_packet, bound_a.expected_role.pidfd, invoking_identity.uid,
        invoking_identity.gid, installed,
        "SEC-003 retained old pidfd guard");
    send_role_response(
        transaction_channel_a, false,
        SECURITY_REJECTION_RETAINED_PIDFD);

    const std::string evidence = sec003_evidence_report(
        invocation.synthetic_security_scenario, store_a, store_b,
        stale_packet, bound_a.expected_role.pidfd.identity(), replacement,
        bound_b, transaction_channel_a, transaction_channel_b);
    store_a.abort_session();
    if(store_b.has_value()) store_b->abort_session();
    send_role_response(security_control_channel, true, evidence);
}

int run_root_sec003_validation_fixture(
    const MakepkgSyncdepsAdapterInvocation& invocation,
    const InstalledExecutable& installed) {
    const SudoInvocationIdentity invoking_identity =
        require_sudo_invocation_identity();
    MakepkgSyncdepsObservedProcess supervisor =
        observe_makepkg_syncdeps_process(getpid());
    if(supervisor.pidfd.identity().uid != 0 || supervisor.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           supervisor.executable, installed.identity)) {
        throw_runtime_error_message(
            "SEC-003 fixture root supervisor provenance is invalid");
    }
    RoleChannelPair security_control = create_role_channel_pair();
    RoleChannelPair transaction_channel_a = create_role_channel_pair();
    RoleChannelPair transaction_channel_b = create_role_channel_pair();
    const pid_t launcher_pid = fork();
    if(launcher_pid == -1) {
        throw_runtime_error("unable to fork SEC-003 fixture launcher");
    }
    if(launcher_pid == 0) {
        security_control.supervisor = OwnedDescriptor();
        transaction_channel_a.supervisor = OwnedDescriptor();
        transaction_channel_b.supervisor = OwnedDescriptor();
        try {
            _exit(run_sec003_fixture_launcher(
                invocation, invoking_identity, installed,
                security_control.role.get(), transaction_channel_a.role.get(),
                transaction_channel_b.role.get(), supervisor.pidfd));
        } catch(const std::exception& error) {
            std::cerr << "moguet-makepkg-syncdeps-adapter: SEC-003 launcher: "
                      << error.what() << '\n';
            _exit(95);
        }
    }
    security_control.role = OwnedDescriptor();
    transaction_channel_a.role = OwnedDescriptor();
    transaction_channel_b.role = OwnedDescriptor();
    MakepkgSyncdepsObservedProcess launcher =
        observe_makepkg_syncdeps_process(launcher_pid);
    if(launcher.parent_pid != getpid() ||
       launcher.pidfd.identity().uid != 0 || launcher.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           launcher.executable, installed.identity)) {
        send_pidfd_signal(launcher.pidfd, SIGKILL);
        reap_if_child(launcher_pid);
        throw_runtime_error_message(
            "SEC-003 fixture launcher provenance is invalid");
    }

    const std::optional<std::string> session_token_a =
        generate_makepkg_syncdeps_adapter_token();
    const bool cross_session =
        invocation.synthetic_security_scenario ==
        "cross-session-replacement";
    const std::optional<std::string> session_token_b =
        cross_session ? generate_makepkg_syncdeps_adapter_token()
                      : std::optional<std::string>{};
    if(!session_token_a.has_value() ||
       (cross_session &&
        (!session_token_b.has_value() ||
         *session_token_b == *session_token_a))) {
        send_pidfd_signal(launcher.pidfd, SIGKILL);
        reap_if_child(launcher_pid);
        throw_runtime_error_message(
            "SEC-003 fixture session token generation failed");
    }

    const int runtime_fd = open(
        "/run", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(runtime_fd == -1) throw_runtime_error("unable to open /run");
    OwnedDescriptor runtime(runtime_fd);
    std::optional<MakepkgSyncdepsAdapterStateStore> store_a;
    std::optional<MakepkgSyncdepsAdapterStateStore> store_b;
    bool launcher_reaped = false;
    try {
        store_a.emplace(
            MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
                runtime.get(), 0,
                MakepkgSyncdepsPreparedSessionState{
                    *session_token_a,
                    static_cast<std::uint32_t>(invoking_identity.uid),
                    installed.identity, launcher.pidfd.identity(),
                    supervisor.pidfd.identity()}));
        if(cross_session) {
            store_b.emplace(
                MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
                    runtime.get(), 0,
                    MakepkgSyncdepsPreparedSessionState{
                        *session_token_b,
                        static_cast<std::uint32_t>(invoking_identity.uid),
                        installed.identity, launcher.pidfd.identity(),
                        supervisor.pidfd.identity()}));
        }
        send_role_response(
            security_control.supervisor.get(), true,
            serialize_makepkg_syncdeps_session_prepare_response(
                MakepkgSyncdepsSessionPrepareResponse{
                    *session_token_a,
                    static_cast<std::uint32_t>(invoking_identity.uid)}));
        if(cross_session) {
            send_role_response(
                security_control.supervisor.get(), true,
                serialize_makepkg_syncdeps_session_prepare_response(
                    MakepkgSyncdepsSessionPrepareResponse{
                        *session_token_b,
                        static_cast<std::uint32_t>(invoking_identity.uid)}));
        }
        serve_sec003_validation_fixture(
            *store_a, store_b, invocation, installed, invoking_identity,
            security_control.supervisor.get(),
            transaction_channel_a.supervisor.get(),
            transaction_channel_b.supervisor.get(), launcher.pidfd);
        const int launcher_exit = wait_child(launcher_pid);
        launcher_reaped = true;
        if(launcher_exit != 0) {
            throw_runtime_error_message(
                "SEC-003 fixture launcher returned failure");
        }
        return 0;
    } catch(...) {
        if(store_a.has_value()) {
            try {
                store_a->abort_session();
            } catch(...) {
            }
        }
        if(store_b.has_value()) {
            try {
                store_b->abort_session();
            } catch(...) {
            }
        }
        if(!launcher_reaped) {
            try {
                if(launcher.pidfd.is_alive()) {
                    send_pidfd_signal(launcher.pidfd, SIGKILL);
                }
            } catch(...) {
            }
            reap_if_child(launcher_pid);
        }
        throw;
    }
}

int run_root_synthetic_session(
    const MakepkgSyncdepsAdapterInvocation& invocation,
    const InstalledExecutable& installed) {
    if(invocation.command ==
           MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity &&
       is_sec003_validation_scenario(
           invocation.synthetic_security_scenario)) {
        return run_root_sec003_validation_fixture(invocation, installed);
    }
    const SudoInvocationIdentity invoking_identity =
        require_sudo_invocation_identity();
    MakepkgSyncdepsObservedProcess supervisor =
        observe_makepkg_syncdeps_process(getpid());
    if(supervisor.pidfd.identity().uid != 0 || supervisor.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           supervisor.executable, installed.identity)) {
        throw_runtime_error_message("root supervisor provenance is invalid");
    }
    RoleChannelPair launcher_channel = create_role_channel_pair();
    RoleChannelPair transaction_channel = create_role_channel_pair();
    const pid_t launcher_pid = fork();
    if(launcher_pid == -1) {
        throw_runtime_error("unable to fork root-owned launcher role");
    }
    if(launcher_pid == 0) {
        launcher_channel.supervisor = OwnedDescriptor();
        transaction_channel.supervisor = OwnedDescriptor();
        try {
            _exit(run_root_launcher(
                invocation, invoking_identity, installed,
                launcher_channel.role.get(), transaction_channel.role.get(),
                supervisor.pidfd));
        } catch(const std::exception& error) {
            std::cerr << "moguet-makepkg-syncdeps-adapter: root launcher: "
                      << error.what() << '\n';
            _exit(94);
        }
    }
    launcher_channel.role = OwnedDescriptor();
    transaction_channel.role = OwnedDescriptor();
    MakepkgSyncdepsObservedProcess launcher =
        observe_makepkg_syncdeps_process(launcher_pid);
    if(launcher.parent_pid != getpid() ||
       launcher.pidfd.identity().uid != 0 || launcher.tracer_pid != 0 ||
       !makepkg_syncdeps_executable_identity_matches(
           launcher.executable, installed.identity)) {
        send_pidfd_signal(launcher.pidfd, SIGKILL);
        reap_if_child(launcher_pid);
        throw_runtime_error_message("root launcher provenance is invalid");
    }
    const std::optional<std::string> session_token =
        generate_makepkg_syncdeps_adapter_token();
    if(!session_token.has_value()) {
        send_pidfd_signal(launcher.pidfd, SIGKILL);
        reap_if_child(launcher_pid);
        throw_runtime_error_message(
            "cryptographic session token generation failed");
    }
    const MakepkgSyncdepsPreparedSessionState prepared{
        *session_token, static_cast<std::uint32_t>(invoking_identity.uid),
        installed.identity, launcher.pidfd.identity(),
        supervisor.pidfd.identity()};
    const int runtime_fd = open(
        "/run", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(runtime_fd == -1) throw_runtime_error("unable to open /run");
    OwnedDescriptor runtime(runtime_fd);
    std::optional<MakepkgSyncdepsAdapterStateStore> store;
    bool retired = false;
    try {
        store.emplace(
            MakepkgSyncdepsAdapterStateStore::create_below_runtime_parent(
                runtime.get(), 0, prepared));
        send_packet_bytes(
            launcher_channel.supervisor.get(),
            serialize_makepkg_syncdeps_session_prepare_response(
                MakepkgSyncdepsSessionPrepareResponse{
                    *session_token, prepared.invoking_uid}));
        const SessionServeResult serve_result = serve_private_session(
            *store, invocation, installed, invoking_identity,
            launcher_channel.supervisor.get(),
            transaction_channel.supervisor.get(), launcher.pidfd);
        retired = serve_result.retired;
        const int launcher_exit = wait_child(launcher_pid);
        const bool security_scenario =
            invocation.command ==
            MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity;
        if(security_scenario &&
           invocation.synthetic_security_scenario == "launcher-failure") {
            return retired && launcher_exit == 93 ? 0 : 1;
        }
        if(security_scenario) {
            const bool lifecycle_only =
                invocation.synthetic_security_scenario ==
                "barrier-failure";
            return retired && launcher_exit == 0 &&
                           (lifecycle_only ||
                            serve_result.security_rejection_observed)
                       ? 0
                       : 1;
        }
        return retired ? launcher_exit : 1;
    } catch(...) {
        if(store.has_value() && !retired) {
            try {
                store->abort_session();
                retired = true;
            } catch(...) {
            }
        }
        try {
            if(launcher.pidfd.is_alive()) {
                send_pidfd_signal(launcher.pidfd, SIGKILL);
            }
        } catch(...) {
        }
        reap_if_child(launcher_pid);
        throw;
    }
}

int run_unprivileged_synthetic(
    const MakepkgSyncdepsAdapterInvocation& invocation) {
    static_cast<void>(require_installed_self());
    if(getuid() == 0 || geteuid() == 0) {
        throw_runtime_error_message("synthetic entry must run as a normal user");
    }
    if(has_unsafe_loader_environment()) {
        throw_runtime_error_message(
            "unsafe dynamic loader environment is unsupported");
    }
    MakepkgSyncdepsObservedProcess self =
        observe_makepkg_syncdeps_process(getpid());
    if(self.tracer_pid != 0) {
        throw_runtime_error_message("traced synthetic entry is unsupported");
    }
    std::vector<std::string> root_arguments;
    if(invocation.command == MakepkgSyncdepsAdapterCommand::SyntheticSession) {
        root_arguments = {
            "root-synthetic-session",
            std::to_string(invocation.synthetic_transaction_count)};
        if(invocation.synthetic_hold) root_arguments.emplace_back("hold");
    } else {
        root_arguments = {
            "root-synthetic-security",
            invocation.synthetic_security_scenario};
    }
    const CapturedProcessResult result = run_sudo_capture(root_arguments);
    if(result.exit_code != 0) return result.exit_code;
    if(invocation.command ==
       MakepkgSyncdepsAdapterCommand::SyntheticSecurity) {
        if(!result.output.empty()) {
            throw_runtime_error_message(
                "security rejection returned positive manifest bytes");
        }
        return 0;
    }
    const auto parsed = parse_makepkg_syncdeps_session_manifest(result.output);
    const auto* manifest =
        std::get_if<MakepkgSyncdepsSessionManifest>(&parsed);
    const std::size_t expected_count = std::min<std::size_t>(
        invocation.synthetic_transaction_count, 2);
    const bool unsupported = invocation.synthetic_transaction_count > 2;
    if(manifest == nullptr ||
       manifest->prepared.invoking_uid != getuid() ||
       manifest->terminal.transaction_count != expected_count ||
       manifest->transactions.size() != expected_count ||
       manifest->evidence_kind != MakepkgSyncdepsEvidenceKind::Synthetic ||
       (manifest->terminal.terminal_state ==
        MakepkgSyncdepsTerminalState::Unsupported) != unsupported) {
        throw_runtime_error_message(
            "root-owned synthetic manifest response is invalid");
    }
    write_all(STDOUT_FILENO, result.output);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for(int index = 1; index < argc; ++index)
        arguments.emplace_back(argv[index]);
    const MakepkgSyncdepsAdapterInvocationResult parsed =
        parse_makepkg_syncdeps_adapter_arguments(arguments);
    const auto* invocation =
        std::get_if<MakepkgSyncdepsAdapterInvocation>(&parsed);
    if(invocation == nullptr) {
        std::cerr << "moguet-makepkg-syncdeps-adapter: invalid fixed protocol invocation\n";
        return 2;
    }
    try {
        switch(invocation->command) {
            case MakepkgSyncdepsAdapterCommand::SyntheticSession:
            case MakepkgSyncdepsAdapterCommand::SyntheticSecurity:
                return run_unprivileged_synthetic(*invocation);
            case MakepkgSyncdepsAdapterCommand::RootSyntheticSession:
            case MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity: {
                const InstalledExecutable installed = require_installed_self();
                return run_root_synthetic_session(*invocation, installed);
            }
            case MakepkgSyncdepsAdapterCommand::SessionPrepare:
            case MakepkgSyncdepsAdapterCommand::SessionBind:
            case MakepkgSyncdepsAdapterCommand::TransactionPrepare:
            case MakepkgSyncdepsAdapterCommand::TransactionRecord:
            case MakepkgSyncdepsAdapterCommand::TransactionFinalize:
            case MakepkgSyncdepsAdapterCommand::TransactionConsume:
            case MakepkgSyncdepsAdapterCommand::TransactionAbort:
            case MakepkgSyncdepsAdapterCommand::SessionFinalize:
            case MakepkgSyncdepsAdapterCommand::SessionConsume:
            case MakepkgSyncdepsAdapterCommand::SessionAbort:
                throw_runtime_error_message(
                    "external root verbs are disabled; a root-owned private role channel is required");
        }
    } catch(const std::exception& error) {
        std::cerr << "moguet-makepkg-syncdeps-adapter: " << error.what()
                  << '\n';
        return 1;
    }
    return 1;
}
