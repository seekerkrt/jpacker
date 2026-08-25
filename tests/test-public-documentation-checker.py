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
    assert_semantic_text_contract,
    check_reviewed_source_documentation,
    exact_man_public_surface,
    expected_surface,
    reviewed_source_documentation_contracts,
    reviewed_source_runtime_help_contracts,
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


def copy_reviewed_source_documentation_fixture(directory: str) -> Path:
    fixture_root = Path(directory)
    for source in reviewed_source_documentation_contracts(REPOSITORY_ROOT):
        relative = source.relative_to(REPOSITORY_ROOT)
        destination = fixture_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(
            source.read_text(encoding="utf-8"), encoding="utf-8"
        )
    return fixture_root


def expect_reviewed_source_documentation_rejected(
    label: str,
    relative_path: str,
    old: str,
    new: str,
) -> None:
    with tempfile.TemporaryDirectory(
        prefix="moguet-reviewed-doc-checker-"
    ) as directory:
        fixture_root = copy_reviewed_source_documentation_fixture(directory)
        path = fixture_root / relative_path
        path.write_text(
            replace_once(path.read_text(encoding="utf-8"), old, new),
            encoding="utf-8",
        )
        diagnostic = io.StringIO()
        try:
            with redirect_stderr(diagnostic):
                check_reviewed_source_documentation(fixture_root)
        except SystemExit as error:
            if (
                error.code == 1
                and "public-documentation-check:" in diagnostic.getvalue()
            ):
                print(f"  ok: rejected {label}")
                return
            fail(f"{label} returned unexpected status {error.code!r}")
    fail(f"{label} unexpectedly passed")


def expect_runtime_help_rejected(
    label: str,
    locale: str,
    syntax: str,
    description: str,
) -> None:
    contract = reviewed_source_runtime_help_contracts(locale).get(syntax)
    if contract is None:
        fail(f"unknown runtime-help mutation entry: {locale} {syntax}")
    diagnostic = io.StringIO()
    try:
        with redirect_stderr(diagnostic):
            assert_semantic_text_contract(
                f"{locale} runtime help entry {syntax!r}",
                description,
                contract,
            )
    except SystemExit as error:
        if (
            error.code == 1
            and "public-documentation-check:" in diagnostic.getvalue()
        ):
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

    with tempfile.TemporaryDirectory(
        prefix="moguet-reviewed-doc-checker-"
    ) as directory:
        check_reviewed_source_documentation(
            copy_reviewed_source_documentation_fixture(directory)
        )

    reviewed_source_mutations = (
        (
            "missing legacy-cache migration contract",
            "README.md",
            "No manual migration is required",
            "Manual migration may be required",
        ),
        (
            "generic identity promoted from Unknown",
            "docs/contracts/source-package-identity.md",
            "common projectionの`Unknown`を`Known`へ昇格させたりしない",
            "common projectionを`Known`へ昇格させる",
        ),
        (
            "missing reviewed-state CAS contract",
            "docs/contracts/reviewed-source-state.md",
            "CAS semantics",
            "last-writer-wins semantics",
        ),
        (
            "stale runtime review.pkgbuild source wording",
            "source/moguet.cpp",
            "Invocation-local {} / {} editor policy; not reviewed-source acceptance",
            "{} review policy",
        ),
        (
            "stale runtime review.diff source wording",
            "source/moguet.cpp",
            "Repository diff / {} reviewed-source review policy; skipping does not advance reviewed state",
            "Repository update diff policy",
        ),
        (
            "completion wording without initial full review",
            "completions/descriptions/en.json",
            "Review repository updates; for AUR, review the exact target from the previous reviewed revision or all tracked source initially",
            "Prompt to view repository update diffs",
        ),
        (
            "ambiguous completion --nodiff wording",
            "completions/descriptions/en.json",
            "Skip repository diff / reviewed-source review without advancing reviewed state",
            "Skip reviewed source changes",
        ),
    )
    for label, relative_path, old, new in reviewed_source_mutations:
        expect_reviewed_source_documentation_rejected(
            label, relative_path, old, new
        )

    runtime_help_mutations = (
        (
            "stale English review.pkgbuild config wording",
            "en",
            "review.pkgbuild = prompt|skip",
            "PKGBUILD review policy",
        ),
        (
            "stale English review.diff config wording",
            "en",
            "review.diff = prompt|skip",
            "Repository update diff policy",
        ),
        (
            "ambiguous English --nodiff wording",
            "en",
            "--nodiff",
            "Skip reviewed source changes",
        ),
        (
            "English --diff without initial full review",
            "en",
            "--diff",
            (
                "Review repository updates; for AUR, review from the previous "
                "reviewed revision to the exact target. Advance reviewed state "
                "only after explicit acceptance"
            ),
        ),
        (
            "editor action described as review acceptance",
            "en",
            "review.pkgbuild = prompt|skip",
            (
                "PKGBUILD / .install editor action for this invocation is "
                "reviewed-source acceptance"
            ),
        ),
        (
            "stale Japanese review.pkgbuild config wording",
            "ja",
            "review.pkgbuild = prompt|skip",
            "PKGBUILDの確認方針",
        ),
    )
    for label, locale, syntax, description in runtime_help_mutations:
        expect_runtime_help_rejected(
            label,
            locale,
            syntax,
            description,
        )

    print(
        "public-documentation-checker-test: "
        f"{len(mutations) + len(reviewed_source_mutations) + len(runtime_help_mutations) + 2} scenarios passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
