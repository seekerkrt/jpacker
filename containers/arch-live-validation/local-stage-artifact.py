#!/usr/bin/python3

import hashlib
import os
from pathlib import Path
import stat
import sys
import time


CACHE_ROOT = Path("/home/moguet-validation/live-local-case/actual/cache/moguet")
STAGING_ROOT = Path("/var/lib/moguet-live-local/staging")
WORKSPACE_PREFIX = ".artifact-workspace~-"
MAX_ARTIFACT_AGE_SECONDS = 60 * 60
FUTURE_SKEW_SECONDS = 5


def fail(message: str) -> None:
    raise RuntimeError(message)


def stable_identity(value: os.stat_result) -> tuple[int, ...]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_uid,
        value.st_gid,
        value.st_nlink,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def hash_descriptor(descriptor: int) -> str:
    os.lseek(descriptor, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    while True:
        chunk = os.read(descriptor, 1024 * 1024)
        if not chunk:
            return digest.hexdigest()
        digest.update(chunk)


def require_no_symlink_components(path: Path) -> None:
    current = Path(path.anchor)
    for component in path.parts[1:]:
        current /= component
        if stat.S_ISLNK(os.lstat(current).st_mode):
            fail(f"path contains a symlink component: {current}")


def require_source_path(source: Path) -> None:
    if not source.is_absolute():
        fail("source artifact path is not absolute")
    if Path(os.path.normpath(source)) != source:
        fail("source artifact path is not lexically canonical")
    if Path(os.path.realpath(source)) != source:
        fail("source artifact path is not its canonical realpath")
    if source.parent.parent != CACHE_ROOT:
        fail("source artifact is outside the exact Moguet cache root")
    if not source.parent.name.startswith(WORKSPACE_PREFIX):
        fail("source artifact is outside an invocation-owned workspace")
    if len(source.parent.name) != len(WORKSPACE_PREFIX) + 6:
        fail("source artifact workspace identity is malformed")
    require_no_symlink_components(source)


def require_source_status(value: os.stat_result, expected_uid: int) -> None:
    if not stat.S_ISREG(value.st_mode):
        fail("source artifact is not a regular file")
    if value.st_uid != expected_uid:
        fail("source artifact is not owned by the validation user")
    if value.st_mode & 0o022:
        fail("source artifact is group/other writable")
    if value.st_nlink != 1:
        fail("source artifact has an unexpected hard-link count")
    now_ns = time.time_ns()
    if value.st_mtime_ns > now_ns + FUTURE_SKEW_SECONDS * 1_000_000_000:
        fail("source artifact timestamp is in the future")
    if now_ns - value.st_mtime_ns > MAX_ARTIFACT_AGE_SECONDS * 1_000_000_000:
        fail("source artifact is not fresh enough for this live invocation")


def require_path_bound_to_descriptor(source: Path, expected: os.stat_result) -> None:
    current = os.lstat(source)
    if stat.S_ISLNK(current.st_mode):
        fail("source artifact path became a symlink")
    if stable_identity(current) != stable_identity(expected):
        fail("source artifact path changed identity or metadata")


def copy_and_hash(source_descriptor: int, destination: Path, expected_gid: int) -> tuple[str, str]:
    destination_descriptor = os.open(
        destination,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o440,
    )
    copied_digest = hashlib.sha256()
    try:
        os.lseek(source_descriptor, 0, os.SEEK_SET)
        while True:
            chunk = os.read(source_descriptor, 1024 * 1024)
            if not chunk:
                break
            copied_digest.update(chunk)
            offset = 0
            while offset < len(chunk):
                offset += os.write(destination_descriptor, chunk[offset:])
        os.fsync(destination_descriptor)
        os.fchown(destination_descriptor, 0, expected_gid)
        os.fchmod(destination_descriptor, 0o440)
    finally:
        os.close(destination_descriptor)

    staged_descriptor = os.open(destination, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        staged_status = os.fstat(staged_descriptor)
        if (
            not stat.S_ISREG(staged_status.st_mode)
            or staged_status.st_uid != 0
            or staged_status.st_gid != expected_gid
            or stat.S_IMODE(staged_status.st_mode) != 0o440
            or staged_status.st_nlink != 1
        ):
            fail("staged artifact has unsafe type, ownership, mode, or links")
        staged_hash = hash_descriptor(staged_descriptor)
    finally:
        os.close(staged_descriptor)
    return copied_digest.hexdigest(), staged_hash


def write_evidence(path: Path, value: str, expected_gid: int) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o640,
    )
    try:
        os.write(descriptor, value.encode("utf-8"))
        os.fsync(descriptor)
        os.fchown(descriptor, 0, expected_gid)
        os.fchmod(descriptor, 0o640)
    finally:
        os.close(descriptor)


def stage(arguments: list[str]) -> int:
    if len(arguments) != 4:
        print(
            "usage: local-stage-artifact.py SOURCE DESTINATION EVIDENCE_DIRECTORY UID:GID",
            file=sys.stderr,
        )
        return 2
    source = Path(arguments[0])
    destination = Path(arguments[1])
    evidence_directory = Path(arguments[2])
    uid_text, separator, gid_text = arguments[3].partition(":")
    if not separator:
        fail("validation user identity is malformed")
    expected_uid, expected_gid = int(uid_text), int(gid_text)

    require_source_path(source)
    if destination.parent.parent != STAGING_ROOT or not destination.is_absolute():
        fail("staged artifact is outside the local root staging directory")
    if Path(os.path.realpath(destination.parent.parent)) != STAGING_ROOT:
        fail("root staging directory changed canonical identity")
    destination_parent_status = os.lstat(destination.parent)
    if (
        not stat.S_ISDIR(destination_parent_status.st_mode)
        or stat.S_ISLNK(destination_parent_status.st_mode)
        or destination_parent_status.st_uid != 0
        or destination_parent_status.st_gid != expected_gid
        or stat.S_IMODE(destination_parent_status.st_mode) != 0o750
    ):
        fail("root staging directory has unsafe identity")
    evidence_status = os.lstat(evidence_directory)
    if (
        not stat.S_ISDIR(evidence_status.st_mode)
        or stat.S_ISLNK(evidence_status.st_mode)
        or evidence_status.st_uid != 0
        or evidence_status.st_gid != expected_gid
        or stat.S_IMODE(evidence_status.st_mode) != 0o750
    ):
        fail("root evidence directory has unsafe identity")

    source_descriptor = os.open(
        source, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    )
    try:
        initial_status = os.fstat(source_descriptor)
        require_source_status(initial_status, expected_uid)
        require_path_bound_to_descriptor(source, initial_status)
        source_hash_before = hash_descriptor(source_descriptor)
        if stable_identity(os.fstat(source_descriptor)) != stable_identity(initial_status):
            fail("source artifact changed during the before hash")
        require_path_bound_to_descriptor(source, initial_status)
        copied_hash, staged_hash = copy_and_hash(source_descriptor, destination, expected_gid)
        if stable_identity(os.fstat(source_descriptor)) != stable_identity(initial_status):
            fail("source artifact changed during copy")
        require_path_bound_to_descriptor(source, initial_status)
        source_hash_after = hash_descriptor(source_descriptor)
        if source_hash_before != copied_hash or copied_hash != staged_hash or staged_hash != source_hash_after:
            fail("source and staged artifact content hashes differ")
        write_evidence(
            evidence_directory / "stage-hashes.txt",
            "".join(
                (
                    f"source_before={source_hash_before}\n",
                    f"copied={copied_hash}\n",
                    f"staged={staged_hash}\n",
                    f"source_after={source_hash_after}\n",
                )
            ),
            expected_gid,
        )
        write_evidence(
            evidence_directory / "staged-artifact-path.txt",
            f"{destination}\n",
            expected_gid,
        )
    finally:
        os.close(source_descriptor)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(stage(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"moguet-live-local-stage: {error}", file=sys.stderr)
        raise SystemExit(1)
