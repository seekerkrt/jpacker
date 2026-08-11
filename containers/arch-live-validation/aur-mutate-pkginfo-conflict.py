#!/usr/bin/env python3

import os
from pathlib import Path
import re
import stat
import sys
import tarfile


class MutationError(Exception):
    pass


def parse_make_dependencies(authority: str) -> list[str]:
    if not authority:
        raise MutationError("make dependency authority is empty")

    dependencies = authority.split(",")
    if any(not dependency for dependency in dependencies):
        raise MutationError("make dependency authority contains an empty record")
    if len(dependencies) != len(set(dependencies)):
        raise MutationError("make dependency authority contains a duplicate record")

    dependency_pattern = re.compile(r"[A-Za-z0-9@._+:-]+(?:[<>=]+[A-Za-z0-9@._+~:-]+)?")
    if any(not dependency_pattern.fullmatch(dependency) for dependency in dependencies):
        raise MutationError("make dependency authority contains an unsafe record")
    return dependencies


def normalize_member_name(name: str) -> str:
    while name.startswith("./"):
        name = name[2:]
    return name


def mutate_pkginfo(archive_path: Path, make_dependencies: str) -> None:
    dependencies = parse_make_dependencies(make_dependencies)
    target_dependency = dependencies[0]

    try:
        archive_status = os.lstat(archive_path)
    except OSError as error:
        raise MutationError(f"unable to inspect raw package archive: {error}") from error
    if not stat.S_ISREG(archive_status.st_mode):
        raise MutationError("raw package archive is not a regular non-symlink")

    try:
        with tarfile.open(archive_path, mode="r:") as archive:
            pkginfo_members = [
                member
                for member in archive.getmembers()
                if normalize_member_name(member.name) == ".PKGINFO"
            ]
    except (OSError, tarfile.TarError) as error:
        raise MutationError(f"unable to read raw package archive: {error}") from error

    if len(pkginfo_members) != 1:
        raise MutationError(
            f"raw package archive has {len(pkginfo_members)} .PKGINFO members; expected 1"
        )
    pkginfo = pkginfo_members[0]
    if not pkginfo.isreg():
        raise MutationError("raw package archive .PKGINFO is not a regular file")

    expected_record = f"makedepend = {target_dependency}\n".encode("ascii")
    replacement_value = "x" * (len(target_dependency) + 2)
    replacement_record = f"conflict = {replacement_value}\n".encode("ascii")
    if len(expected_record) != len(replacement_record):
        raise MutationError("derived conflict record changes .PKGINFO member length")

    try:
        with archive_path.open("r+b") as archive_stream:
            archive_stream.seek(pkginfo.offset_data)
            contents = archive_stream.read(pkginfo.size)
            record_count = contents.splitlines(keepends=True).count(expected_record)
            if record_count != 1:
                raise MutationError(
                    "scenario-selected make dependency record occurs "
                    f"{record_count} times in .PKGINFO; expected 1"
                )
            mutated_contents = contents.replace(
                expected_record, replacement_record, 1
            )
            archive_stream.seek(pkginfo.offset_data)
            archive_stream.write(mutated_contents)
    except OSError as error:
        raise MutationError(f"unable to mutate raw package archive: {error}") from error


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            "usage: aur-mutate-pkginfo-conflict.py RAW_TAR MAKE_DEPENDENCIES",
            file=sys.stderr,
        )
        return 2

    try:
        mutate_pkginfo(Path(argv[1]), argv[2])
    except MutationError as error:
        print(f"aur-pkginfo-conflict-mutation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
