#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stderr
import io
from pathlib import Path
import re
import sys
import tempfile


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))

from check_public_documentation import (  # noqa: E402
    exact_man_public_surface,
    expected_surface,
)
from generate_completions import load_schema  # noqa: E402


SYNC_SELECT_SYNTAX = "-S <pkg> | -S --select [--needed] <query>"


def fail(message: str) -> None:
    print(f"public-documentation-checker-test: {message}", file=sys.stderr)
    raise SystemExit(1)


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        fail(f"mutation source must occur exactly once: {old!r}")
    return text.replace(old, new, 1)


def remove_needed_definition(text: str) -> str:
    mutated, count = re.subn(
        r'\.TP\n\.B "--needed"\n.*?(?=\.TP\n)',
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )
    if count != 1:
        fail("PUBLIC OPTIONS --needed definition was not found exactly once")
    return mutated


def expect_rejected(label: str, text: str, schema, expected) -> None:
    with tempfile.TemporaryDirectory(prefix="moguet-doc-checker-") as directory:
        path = Path(directory) / "moguet.1.in"
        path.write_text(text, encoding="utf-8")
        diagnostic = io.StringIO()
        try:
            with redirect_stderr(diagnostic):
                exact_man_public_surface(path, expected, schema)
        except SystemExit as error:
            if error.code == 1 and "public-documentation-check:" in diagnostic.getvalue():
                print(f"  ok: rejected {label}")
                return
            fail(f"{label} returned unexpected status {error.code!r}")
    fail(f"{label} unexpectedly passed")


def main() -> int:
    schema = load_schema()
    expected = expected_surface(schema)
    source = (REPOSITORY_ROOT / "man/moguet.1.in").read_text(encoding="utf-8")

    exact_man_public_surface(
        REPOSITORY_ROOT / "man/moguet.1.in", expected, schema
    )
    mutations = (
        (
            "duplicate trailing option",
            replace_once(
                source,
                SYNC_SELECT_SYNTAX,
                SYNC_SELECT_SYNTAX + " --needed",
            ),
        ),
        (
            "missing canonical option",
            replace_once(
                source,
                SYNC_SELECT_SYNTAX,
                "-S <pkg> | -S --select <query>",
            ),
        ),
        (
            "unexpected trailing token",
            replace_once(
                source,
                SYNC_SELECT_SYNTAX,
                SYNC_SELECT_SYNTAX + " extra",
            ),
        ),
        (
            "reordered canonical option",
            replace_once(
                source,
                SYNC_SELECT_SYNTAX,
                "-S <pkg> | -S [--needed] --select <query>",
            ),
        ),
        ("missing PUBLIC OPTIONS definition", remove_needed_definition(source)),
    )
    for label, mutated in mutations:
        expect_rejected(label, mutated, schema, expected)

    print(
        "public-documentation-checker-test: "
        f"{len(mutations) + 1} scenarios passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
