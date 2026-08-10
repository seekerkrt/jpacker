#!/usr/bin/env python3

import ctypes
import os
import select
import struct
import sys
from pathlib import Path


IN_MODIFY = 0x00000002
IN_ATTRIB = 0x00000004
IN_CLOSE_WRITE = 0x00000008
IN_MOVED_FROM = 0x00000040
IN_MOVED_TO = 0x00000080
IN_CREATE = 0x00000100
IN_DELETE = 0x00000200
IN_DELETE_SELF = 0x00000400
IN_MOVE_SELF = 0x00000800
IN_DONT_FOLLOW = 0x02000000

MUTATION_MASK = (
    IN_MODIFY
    | IN_ATTRIB
    | IN_CLOSE_WRITE
    | IN_MOVED_FROM
    | IN_MOVED_TO
    | IN_CREATE
    | IN_DELETE
    | IN_DELETE_SELF
    | IN_MOVE_SELF
)
EVENT_HEADER = struct.Struct("iIII")


def require_arguments():
    if len(sys.argv) < 5:
        raise SystemExit(
            "usage: fs_mutation_sentinel.py READY STOP EVENTS ROOT..."
        )
    return Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), [
        Path(value) for value in sys.argv[4:]
    ]


def watched_directories(roots):
    directories = []
    for root in roots:
        if not root.is_dir() or root.is_symlink():
            raise RuntimeError(f"sentinel root is not a directory: {root}")
        directories.append(root)
        for current, names, _ in os.walk(root, followlinks=False):
            current_path = Path(current)
            for name in names:
                child = current_path / name
                if child.is_dir() and not child.is_symlink():
                    directories.append(child)
    return directories


def drain_events(descriptor, watch_paths, events):
    while True:
        try:
            payload = os.read(descriptor, 65536)
        except BlockingIOError:
            return
        if not payload:
            return
        offset = 0
        while offset < len(payload):
            watch, mask, cookie, name_length = EVENT_HEADER.unpack_from(
                payload, offset
            )
            offset += EVENT_HEADER.size
            raw_name = payload[offset : offset + name_length].split(b"\0", 1)[0]
            offset += name_length
            watched_path = watch_paths.get(watch, Path("<unknown-watch>"))
            event_path = (
                watched_path / os.fsdecode(raw_name)
                if raw_name
                else watched_path
            )
            events.append(
                f"path={os.fspath(event_path)!r} mask=0x{mask:08X} cookie={cookie}"
            )


def main():
    ready_path, stop_path, events_path, roots = require_arguments()
    libc = ctypes.CDLL(None, use_errno=True)
    libc.inotify_init1.argtypes = [ctypes.c_int]
    libc.inotify_init1.restype = ctypes.c_int
    libc.inotify_add_watch.argtypes = [
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_uint32,
    ]
    libc.inotify_add_watch.restype = ctypes.c_int

    descriptor = libc.inotify_init1(os.O_CLOEXEC | os.O_NONBLOCK)
    if descriptor < 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))

    watch_paths = {}
    try:
        for directory in watched_directories(roots):
            watch = libc.inotify_add_watch(
                descriptor,
                os.fsencode(directory),
                MUTATION_MASK | IN_DONT_FOLLOW,
            )
            if watch < 0:
                error = ctypes.get_errno()
                raise OSError(error, os.strerror(error), directory)
            watch_paths[watch] = directory

        events_path.write_text("", encoding="utf-8")
        ready_path.write_text("ready\n", encoding="utf-8")
        events = []
        while not stop_path.exists():
            readable, _, _ = select.select([descriptor], [], [], 0.05)
            if readable:
                drain_events(descriptor, watch_paths, events)

        while True:
            readable, _, _ = select.select([descriptor], [], [], 0.05)
            if not readable:
                break
            drain_events(descriptor, watch_paths, events)
        if events:
            events_path.write_text("\n".join(events) + "\n", encoding="utf-8")
    finally:
        os.close(descriptor)


if __name__ == "__main__":
    main()
