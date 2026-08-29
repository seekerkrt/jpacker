#!/usr/bin/env python3

import ctypes
import errno
import os
import signal
import socket
import sys


def run_traced(adapter: str) -> int:
    libc = ctypes.CDLL(None, use_errno=True)
    ptrace_traceme = 0
    ptrace_continue = 7
    child = os.fork()
    if child == 0:
        if libc.ptrace(ptrace_traceme, 0, None, None) == -1:
            os._exit(120)
        os.kill(os.getpid(), signal.SIGSTOP)
        os.execv(adapter, [adapter, "synthetic-session", "0"])
        os._exit(121)

    while True:
        waited, status = os.waitpid(child, 0)
        if waited != child:
            return 122
        if os.WIFEXITED(status):
            return 0 if os.WEXITSTATUS(status) != 0 else 123
        if os.WIFSIGNALED(status):
            return 0
        if not os.WIFSTOPPED(status):
            return 124
        if libc.ptrace(ptrace_continue, child, None, None) == -1:
            return 125


def run_fake_server(token: str, ready: str, connected: str) -> int:
    server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    server.bind("\0moguet-msd-" + token)
    server.listen(8)
    server.settimeout(3.0)
    with open(ready, "x", encoding="ascii"):
        pass
    try:
        connection, _ = server.accept()
    except TimeoutError:
        return 0
    except OSError as error:
        if error.errno == errno.EAGAIN:
            return 0
        raise
    with connection:
        with open(connected, "x", encoding="ascii"):
            pass
        try:
            connection.recv(4096)
            connection.sendall(b"\0forged-manifest\n")
        except OSError:
            pass
    return 126


def main() -> int:
    if len(sys.argv) < 3:
        return 2
    if sys.argv[1] == "trace" and len(sys.argv) == 3:
        return run_traced(sys.argv[2])
    if sys.argv[1] == "fake" and len(sys.argv) == 5:
        return run_fake_server(sys.argv[2], sys.argv[3], sys.argv[4])
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
