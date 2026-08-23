#!/usr/bin/env python3

from __future__ import annotations

import ast
from pathlib import Path
import re
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOT = REPOSITORY_ROOT / "source"
POTFILES = REPOSITORY_ROOT / "po" / "POTFILES.in"
POT = REPOSITORY_ROOT / "po" / "moguet.pot"

EXTRACTION_CALL = re.compile(
    r"\b(?:translate_message|translate_plural_message|"
    r"format_translated_message|format_translated_plural_message|"
    r"add_reduction_issue|add_localized_operation_issue|"
    r"make_localized_execution_issue|make_localized_preparation_issue|"
    r"retain_localized_build_unit_selection_issue)\s*\("
)

# Issue #308 audited these programmer-contract diagnostics through their
# production callers. Keeping the exact source anchors here prevents a raw
# diagnostic from silently entering or leaving the explicit internal-only set.
INTERNAL_ONLY_ANCHORS: dict[str, tuple[str, ...]] = {
    "source/artifact_identity_selection.cpp": (
        "Artifact identity selection returned an incoherent result.",
    ),
    "source/package_base_artifact_install_plan.cpp": (
        "Unknown install reason directive.",
        "Unknown installed version state.",
        "Installed package must have an existing install reason.",
        "Unknown existing install reason.",
        "PackageBase install policy has an invalid package name.",
        "PackageBase install policy has an empty package version.",
        "Unknown transaction install reason directive.",
        "Transaction install reason would change a same-version skipped package.",
        "PackageBase install policy has an invalid PackageBase.",
        "PackageBase install policy requires at least one selected artifact.",
    ),
    "source/source_install_preparation.cpp": (
        "Production source-build work items have a partial cache authority.",
        "Production source-build selected provider is not repository-owned.",
        "Production source-build selected provider has an invalid repository name.",
        "Production source-build repository provider has an AUR PackageBase.",
        "Production source-build work item has an empty Git URL for ",
        "Production source-build work item has no required package target for PackageBase ",
        "Production source-build required target has a mismatched PackageBase: ",
        "Production source-build work item contains duplicate required package target: ",
        "Production source-build work item has an unknown install reason.",
        "Production source-build work item contains a duplicate selected repository provider.",
        "Production source-build singular request does not match its required package target: ",
        "Production source-build multiple-target work item must not expose a singular requested package.",
        "Production separated source-build requires exactly one required package target for PackageBase ",
        "Production separated source-build singular identity is inconsistent for PackageBase ",
        "Production source-build invocation must contain at least one work item.",
    ),
    "source/dependency_plan_model.cpp": (
        "Planned package target has no package role: ",
    ),
}

# A mixed source keeps its exact internal-only anchors while also owning an
# explicitly audited number of user-facing catalog calls. BuildPlan execution
# guards moved to the pure model owner without translating its programmer-only
# reducer invariant.
MIXED_INTERNAL_ONLY_EXTRACTION_COUNTS: dict[str, int] = {
    "source/dependency_plan_model.cpp": 12,
}

TECHNICAL_MSGID_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "project, tool, protocol, or schema identity",
        re.compile(
            r"(?<![A-Za-z0-9_])(?:"
            r"Arch|AUR|AurUpdatePlan|BuildPlan|cURL|FIFO|Git|git|HTTP|"
            r"j" r"packer|libalpm|makepkg|Moguet|moguet|PackageBase|pacman(?:-conf)?|"
            r"PKGBUILD|RPC|sudo|TOML|XDG"
            r")(?![A-Za-z0-9_])"
        ),
    ),
    (
        "environment or configuration key",
        re.compile(
            r"(?<![A-Za-z0-9_])(?:"
            r"DBPath|HOME|LC_ALL|LOGFILE|PATH|PKGDEST|RootDir|"
            r"XDG_CACHE_HOME|XDG_CONFIG_HOME|XDG_STATE_HOME"
            r")(?![A-Za-z0-9_])"
        ),
    ),
    (
        "stream or file identity",
        re.compile(
            r"(?<![A-Za-z0-9_])(?:stdin|stdout|stderr|remote\.origin\.url|"
            r"\.git|\.install|\.SRCINFO)(?![A-Za-z0-9_])"
        ),
    ),
    (
        "literal signal, enum, or operation token",
        re.compile(
            r"(?<![A-Za-z0-9_])(?:SIG[A-Z0-9]+|NotAttempted|PR[0-9]+|"
            r"upgrade-all|upgrade-aur|list-src|add-src|edit-src|del-src|revert-src)"
            r"(?![A-Za-z0-9_])"
        ),
    ),
    (
        "CLI option token",
        re.compile(r"(?<![A-Za-z0-9_])--[A-Za-z0-9][A-Za-z0-9_-]*"),
    ),
    ("literal file mode", re.compile(r"(?<![0-9])0600(?![0-9])")),
)


def fail(message: str) -> None:
    print(f"cli-localization-surface: {message}", file=sys.stderr)
    raise SystemExit(1)


def extraction_source_paths() -> set[str]:
    paths: set[str] = set()
    for path in sorted(SOURCE_ROOT.glob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        if EXTRACTION_CALL.search(text):
            paths.add(path.relative_to(REPOSITORY_ROOT).as_posix())
    return paths


def potfile_paths() -> set[str]:
    paths: set[str] = set()
    for raw_line in POTFILES.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line in paths:
            fail(f"duplicate POTFILES entry: {line}")
        paths.add(line)
    return paths


def check_internal_only_anchors() -> None:
    total = 0
    for relative_path, anchors in INTERNAL_ONLY_ANCHORS.items():
        text = (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")
        if "NO_TRANSLATE(Issue #308)" not in text:
            fail(f"missing Issue #308 no-translate rationale: {relative_path}")
        extraction_count = len(EXTRACTION_CALL.findall(text))
        expected_extraction_count = MIXED_INTERNAL_ONLY_EXTRACTION_COUNTS.get(
            relative_path, 0)
        if extraction_count != expected_extraction_count:
            fail(
                f"internal-only translation unit contains {extraction_count} "
                "catalog extraction calls instead of "
                f"{expected_extraction_count}: {relative_path}"
            )
        for anchor in anchors:
            count = text.count(anchor)
            if count != 1:
                fail(
                    f"internal-only anchor occurs {count} times instead of 1 "
                    f"in {relative_path}: {anchor}"
                )
            total += 1
    if total != 27:
        fail(f"internal-only classification contains {total} entries instead of 27")


def parse_po_string(line: str, prefix: str) -> str:
    try:
        value = ast.literal_eval(line[len(prefix):].strip())
    except (SyntaxError, ValueError) as error:
        fail(f"cannot parse {POT.relative_to(REPOSITORY_ROOT)}: {error}")
    if not isinstance(value, str):
        fail(f"non-string PO value in {POT.relative_to(REPOSITORY_ROOT)}")
    return value


def pot_message_entries() -> list[tuple[str, tuple[str, ...]]]:
    entries: list[tuple[str, tuple[str, ...]]] = []
    for block in re.split(r"\n\s*\n", POT.read_text(encoding="utf-8")):
        references: list[str] = []
        fields: dict[str, str] = {}
        current_field: str | None = None
        for line in block.splitlines():
            if line.startswith("#: "):
                references.extend(line[3:].split())
            elif line.startswith("msgid "):
                current_field = "msgid"
                fields[current_field] = parse_po_string(line, "msgid ")
            elif line.startswith("msgid_plural "):
                current_field = "msgid_plural"
                fields[current_field] = parse_po_string(
                    line, "msgid_plural ")
            elif line.startswith("\"") and current_field is not None:
                fields[current_field] += parse_po_string(line, "")
            elif line.startswith("msgstr"):
                current_field = None
        for field in ("msgid", "msgid_plural"):
            message_id = fields.get(field, "")
            if message_id:
                entries.append((message_id, tuple(references)))
    return entries


def check_technical_msgid_boundaries() -> None:
    violations: list[str] = []
    for message_id, references in pot_message_entries():
        for label, pattern in TECHNICAL_MSGID_PATTERNS:
            match = pattern.search(message_id)
            if match is None:
                continue
            location = references[0] if references else "unknown source"
            violations.append(
                f"{location}: {label} {match.group(0)!r} must be runtime data "
                f"instead of catalog text: {message_id!r}"
            )
    if violations:
        fail("technical token leaked into msgid:\n  " + "\n  ".join(violations))


def main() -> None:
    extracted = extraction_source_paths()
    listed = potfile_paths()
    missing = sorted(extracted - listed)
    stale = sorted(listed - extracted)
    if missing:
        fail("POTFILES is missing extraction sources: " + ", ".join(missing))
    if stale:
        fail("POTFILES contains sources without extraction calls: " + ", ".join(stale))

    check_internal_only_anchors()
    check_technical_msgid_boundaries()
    print(
        "cli-localization-surface: "
        f"{len(extracted)} extraction sources and 27 internal-only entries verified"
    )


if __name__ == "__main__":
    main()
