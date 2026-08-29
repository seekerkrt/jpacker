#!/usr/bin/env python3

import hashlib
import json
import os
import stat
import sys
from pathlib import Path


def require_roots():
    if len(sys.argv) < 2:
        raise SystemExit("usage: fs_tree_snapshot.py ROOT...")
    return [Path(value) for value in sys.argv[1:]]


def iter_tree(root):
    yield root
    for current, directory_names, file_names in os.walk(root, followlinks=False):
        directory_names.sort()
        file_names.sort()
        current_path = Path(current)
        retained_directories = []
        for name in directory_names:
            child = current_path / name
            yield child
            if not child.is_symlink():
                retained_directories.append(name)
        directory_names[:] = retained_directories
        for name in file_names:
            yield current_path / name


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(65536)
            if not block:
                return digest.hexdigest()
            digest.update(block)


def path_record(root, path):
    information = path.lstat()
    record = {
        "root": os.fspath(root),
        "path": "." if path == root else os.fspath(path.relative_to(root)),
        "type": stat.S_IFMT(information.st_mode),
        "mode": stat.S_IMODE(information.st_mode),
        "uid": information.st_uid,
        "gid": information.st_gid,
        "device": information.st_dev,
        "inode": information.st_ino,
        "links": information.st_nlink,
        "size": information.st_size,
        "mtime_ns": information.st_mtime_ns,
        "ctime_ns": information.st_ctime_ns,
    }
    if stat.S_ISREG(information.st_mode):
        record["sha256"] = sha256_file(path)
    elif stat.S_ISLNK(information.st_mode):
        record["symlink_target"] = os.readlink(path)
    return record


def main():
    for root in require_roots():
        if not root.is_dir() or root.is_symlink():
            raise RuntimeError(f"snapshot root is not a directory: {root}")
        for path in iter_tree(root):
            print(json.dumps(path_record(root, path), sort_keys=True))


if __name__ == "__main__":
    main()
