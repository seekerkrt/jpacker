#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass
import difflib
import json
from pathlib import Path
import re
import shlex
import sys
from typing import Callable


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CLI_AUTHORITY_PATH = REPOSITORY_ROOT / "src/cli_authority.hpp"
DESCRIPTION_ROOT = REPOSITORY_ROOT / "completions/descriptions"

EXTRA_OPERATION_CONSTANTS = (
    "PKGBUILD_EXPORT_OPERATION",
    "PKGBUILD_PRINT_OPERATION",
)
PACMAN_OPERATION_SYNTAX_CONSTANTS = (
    "PACMAN_SYNC_INSTALL_SYNTAX",
    "PACMAN_SYSTEM_UPGRADE_SYNTAX",
    "PACMAN_SYNC_SEARCH_SYNTAX",
    "PACMAN_SYNC_INFO_SYNTAX",
    "PACMAN_FOREIGN_UPDATES_SYNTAX",
)
INFO_OPTION_CONSTANTS = (
    "HELP_SHORT_OPTION",
    "HELP_LONG_OPTION",
    "VERSION_SHORT_OPTION",
    "VERSION_LONG_OPTION",
)


@dataclass(frozen=True)
class Operation:
    token: str
    help_syntax: str


@dataclass(frozen=True)
class Option:
    token: str
    completion_token: str


@dataclass(frozen=True)
class CliSchema:
    operations: tuple[Operation, ...]
    options: tuple[Option, ...]


@dataclass(frozen=True)
class Descriptions:
    operations: dict[str, str]
    options: dict[str, str]


def fail(message: str) -> None:
    print(f"completion-generator: {message}", file=sys.stderr)
    raise SystemExit(1)


def extract_array_block(authority: str, name: str) -> str:
    marker = f"{name} = {{{{"
    start = authority.find(marker)
    if start < 0:
        fail(f"missing {name} in {CLI_AUTHORITY_PATH}")
    start += len(marker)
    end = authority.find("}};", start)
    if end < 0:
        fail(f"unterminated {name} in {CLI_AUTHORITY_PATH}")
    return authority[start:end]


def extract_constant(authority: str, name: str) -> str:
    match = re.search(
        rf"\b{re.escape(name)}\s*=\s*\"([^\"]+)\"\s*;",
        authority,
        flags=re.DOTALL,
    )
    if match is None:
        fail(f"missing string constant {name} in {CLI_AUTHORITY_PATH}")
    return match.group(1)


def extract_enum_members(authority: str, name: str) -> tuple[str, ...]:
    match = re.search(
        rf"\benum\s+class\s+{re.escape(name)}\s*\{{(.*?)\}}\s*;",
        authority,
        flags=re.DOTALL,
    )
    if match is None:
        fail(f"missing enum class {name} in {CLI_AUTHORITY_PATH}")

    members = tuple(
        re.findall(
            r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*(?:=[^,]+)?\s*,",
            match.group(1),
            flags=re.MULTILINE,
        )
    )
    if not members or members[-1] != "Count":
        fail(f"{name} must end in the Count sentinel")
    if len(set(members)) != len(members):
        fail(f"{name} contains duplicate members")
    return members[:-1]


def append_unique(items: list[str], value: str, category: str) -> None:
    if value in items:
        fail(f"duplicate {category} token in CLI authority: {value}")
    items.append(value)


def load_schema(authority_path: Path = CLI_AUTHORITY_PATH) -> CliSchema:
    authority = authority_path.read_text(encoding="utf-8")

    operation_block = extract_array_block(authority, "MOGUET_OPERATIONS")
    operation_matches = re.findall(
        r"\{OperationId::([A-Za-z0-9_]+),\s*\"([^\"]+)\",\s*"
        r"\"([^\"]+)\",\s*(?:true|false),\s*(?:true|false)\}",
        operation_block,
    )
    operation_ids = tuple(operation_id for operation_id, _, _ in operation_matches)
    expected_operation_ids = extract_enum_members(authority, "OperationId")
    if operation_ids != expected_operation_ids:
        fail(
            "MOGUET_OPERATIONS entries do not exactly match OperationId: "
            f"parsed={operation_ids}, expected={expected_operation_ids}"
        )

    operations = [
        Operation(token, syntax) for _, token, syntax in operation_matches
    ]
    operation_tokens = [operation.token for operation in operations]

    for constant_name in EXTRA_OPERATION_CONSTANTS:
        token = extract_constant(authority, constant_name)
        append_unique(operation_tokens, token, "operation")
        operations.append(Operation(token, token))

    for constant_name in PACMAN_OPERATION_SYNTAX_CONSTANTS:
        syntax = extract_constant(authority, constant_name)
        token = syntax.split(maxsplit=1)[0]
        append_unique(operation_tokens, token, "operation")
        operations.append(Operation(token, syntax))

    options: list[Option] = []
    option_tokens: list[str] = []
    for constant_name in INFO_OPTION_CONSTANTS:
        token = extract_constant(authority, constant_name)
        append_unique(option_tokens, token, "option")
        options.append(Option(token, token))

    global_option_block = extract_array_block(authority, "MOGUET_GLOBAL_OPTIONS")
    global_option_matches = re.findall(
        r"\{GlobalOptionId::([A-Za-z0-9_]+),\s*\"([^\"]+)\",\s*"
        r"\"[^\"]+\",\s*(true|false)\}",
        global_option_block,
    )
    global_option_ids = tuple(
        option_id for option_id, _, _ in global_option_matches
    )
    expected_global_option_ids = extract_enum_members(authority, "GlobalOptionId")
    if global_option_ids != expected_global_option_ids:
        fail(
            "MOGUET_GLOBAL_OPTIONS entries do not exactly match GlobalOptionId: "
            f"parsed={global_option_ids}, expected={expected_global_option_ids}"
        )
    for _, token, accepts_attached_value in global_option_matches:
        append_unique(option_tokens, token, "option")
        completion_token = token + "=" if accepts_attached_value == "true" else token
        options.append(Option(token, completion_token))

    needed_token = extract_constant(authority, "PACMAN_NEEDED_OPTION_SYNTAX")
    append_unique(option_tokens, needed_token, "option")
    options.append(Option(needed_token, needed_token))

    for operation in operations:
        for token in re.findall(r"(?<![A-Za-z0-9-])--[a-z][a-z0-9-]*", operation.help_syntax):
            if token in option_tokens:
                continue
            append_unique(option_tokens, token, "option")
            options.append(Option(token, token))

    return CliSchema(tuple(operations), tuple(options))


def validate_description_map(
    category: str, descriptions: object, expected_tokens: tuple[str, ...]
) -> dict[str, str]:
    if not isinstance(descriptions, dict):
        fail(f"description category '{category}' must be a JSON object")
    if not all(isinstance(key, str) and isinstance(value, str) for key, value in descriptions.items()):
        fail(f"description category '{category}' must contain string keys and values")

    expected = set(expected_tokens)
    actual = set(descriptions)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing or extra:
        details = []
        if missing:
            details.append("missing: " + ", ".join(missing))
        if extra:
            details.append("extra: " + ", ".join(extra))
        fail(f"{category} descriptions do not match CLI authority ({'; '.join(details)})")

    for token, description in descriptions.items():
        if not description or "\n" in description or "\r" in description:
            fail(f"invalid single-line description for {token!r}")
    return descriptions


def load_descriptions(schema: CliSchema, locale: str) -> Descriptions:
    if re.fullmatch(r"[a-z][a-z0-9_-]*", locale) is None:
        fail(f"invalid description locale: {locale}")
    path = DESCRIPTION_ROOT / f"{locale}.json"
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        fail(f"missing description authority: {path}")
    except json.JSONDecodeError as error:
        fail(f"invalid JSON in {path}: {error}")
    if not isinstance(raw, dict):
        fail(f"description authority must be a JSON object: {path}")

    operation_tokens = tuple(operation.token for operation in schema.operations)
    option_tokens = tuple(option.token for option in schema.options)
    return Descriptions(
        validate_description_map("operations", raw.get("operations"), operation_tokens),
        validate_description_map("options", raw.get("options"), option_tokens),
    )


def shell_quote(value: str) -> str:
    return shlex.quote(value)


def render_bash(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    del descriptions
    operation_lines = "\n".join(
        f"        {shell_quote(operation.token)}" for operation in schema.operations
    )
    option_lines = "\n".join(
        f"        {shell_quote(option.completion_token)}" for option in schema.options
    )
    return f"""# Generated by scripts/generate_completions.py; do not edit.
# Description locale: {locale}

_moguet() {{
    local cur
    local -a operations=(
{operation_lines}
    )
    local -a options=(
{option_lines}
    )

    COMPREPLY=()
    cur="${{COMP_WORDS[COMP_CWORD]}}"
    COMPREPLY=( $(compgen -W "${{operations[*]}} ${{options[*]}}" -- "$cur" || true) )
}}

complete -F _moguet moguet
"""


def zsh_described_value(token: str, description: str) -> str:
    escaped_description = description.replace("\\", "\\\\").replace(":", "\\:")
    return shell_quote(f"{token}:{escaped_description}")


def render_zsh(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    operation_lines = "\n".join(
        "    " + zsh_described_value(operation.token, descriptions.operations[operation.token])
        for operation in schema.operations
    )
    option_lines = "\n".join(
        "    " + zsh_described_value(option.completion_token, descriptions.options[option.token])
        for option in schema.options
    )
    return f"""#compdef moguet
# Generated by scripts/generate_completions.py; do not edit.
# Description locale: {locale}

local -a _moguet_operations=(
{operation_lines}
)
local -a _moguet_options=(
{option_lines}
)

_moguet_complete_operations() {{
    _describe -t operations 'moguet operation' _moguet_operations
}}

_moguet_complete_options() {{
    _describe -t options 'moguet option' _moguet_options
}}

_alternative \\
    'operations:moguet operation:_moguet_complete_operations' \\
    'options:moguet option:_moguet_complete_options'
"""


def render_fish(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    lines = [
        "# Generated by scripts/generate_completions.py; do not edit.",
        f"# Description locale: {locale}",
        "",
    ]
    for operation in schema.operations:
        lines.append(
            "complete -c moguet -f -a "
            f"{shell_quote(operation.token)} -d "
            f"{shell_quote(descriptions.operations[operation.token])}"
        )
    for option in schema.options:
        lines.append(
            "complete -c moguet -f -a "
            f"{shell_quote(option.completion_token)} -d "
            f"{shell_quote(descriptions.options[option.token])}"
        )
    return "\n".join(lines) + "\n"


def generated_files(
    schema: CliSchema, descriptions: Descriptions, locale: str, output_dir: Path
) -> dict[Path, str]:
    renderers: tuple[tuple[str, Callable[[CliSchema, Descriptions, str], str]], ...] = (
        ("moguet.bash", render_bash),
        ("_moguet", render_zsh),
        ("moguet.fish", render_fish),
    )
    return {
        output_dir / filename: renderer(schema, descriptions, locale)
        for filename, renderer in renderers
    }


def check_generated(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"completion-generator: missing generated file: {path}", file=sys.stderr)
        return False
    if actual == expected:
        return True

    print(f"completion-generator: generated file is stale: {path}", file=sys.stderr)
    diff = difflib.unified_diff(
        actual.splitlines(),
        expected.splitlines(),
        fromfile=str(path),
        tofile=f"generated:{path.name}",
        lineterm="",
    )
    for line in diff:
        print(line, file=sys.stderr)
    return False


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Moguet static shell completions from the public CLI authority."
    )
    parser.add_argument("--check", action="store_true", help="fail if tracked output differs")
    parser.add_argument("--locale", default="en", help="description locale (default: en)")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=REPOSITORY_ROOT / "completions",
        help="destination directory (default: repository completions directory)",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    output_dir = arguments.output_dir
    if not output_dir.is_absolute():
        output_dir = Path.cwd() / output_dir

    schema = load_schema()
    descriptions = load_descriptions(schema, arguments.locale)
    outputs = generated_files(schema, descriptions, arguments.locale, output_dir)

    if arguments.check:
        return 0 if all(check_generated(path, content) for path, content in outputs.items()) else 1

    output_dir.mkdir(parents=True, exist_ok=True)
    for path, content in outputs.items():
        path.write_text(content, encoding="utf-8")
        print(f"generated {path.relative_to(REPOSITORY_ROOT) if path.is_relative_to(REPOSITORY_ROOT) else path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
