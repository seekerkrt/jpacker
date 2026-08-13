#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import difflib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
from typing import Callable


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CLI_AUTHORITY_EXPORTER = REPOSITORY_ROOT / "scripts/export_cli_authority.cpp"
CLI_PUBLIC_PROJECTION = REPOSITORY_ROOT / "src/cli_public_projection.cpp"
DESCRIPTION_ROOT = REPOSITORY_ROOT / "completions/descriptions"

# The exporter owns the C++ authority projection.  The loader does not trust
# wire values that it does not understand: adding an enum value in C++ also
# requires an explicit completion projection before rendering may continue.
KNOWN_OPERAND_KINDS = frozenset(
    {
        "none",
        "package",
        "directory",
        "query",
        "source-preference-item",
        "environment-assignment",
        "delegated-pacman-argument",
    }
)
KNOWN_OPERAND_ORDERINGS = frozenset(
    {
        "none",
        "preserve-input-order",
        "primary-then-environment-assignments",
        "package-introduces-following-assignment-scope",
        "delegated",
    }
)
KNOWN_TARGET_POLICIES = frozenset(
    {"none", "exactly-one", "one-or-more", "ordered-items", "delegated"}
)
PRIMARY_OPERAND_KINDS = frozenset({"package", "directory", "query"})
ASSIGNMENT_PRIMARY_OPERAND_KINDS = frozenset({"package", "directory"})


@dataclass(frozen=True)
class Option:
    identity: int
    token: str
    completion_token: str
    occurrence: str
    placement: str
    conflicts: tuple[int, ...]
    has_public_definition: bool


@dataclass(frozen=True)
class OperandTerm:
    kind: str
    min_count: int
    max_count: int | None


@dataclass(frozen=True)
class Form:
    syntax: str
    target_policy: str
    operand_ordering: str
    operand_terms: tuple[OperandTerm, ...]
    option_ids: tuple[int, ...]
    selector_ids: tuple[int, ...]


@dataclass(frozen=True)
class Operation:
    token: str
    forms: tuple[Form, ...] = ()
    open_grammar: bool = False


@dataclass(frozen=True)
class CliSchema:
    operations: tuple[Operation, ...]
    options: tuple[Option, ...]
    delegated_option_ids: tuple[int, ...]
    terminal_tokens: tuple[str, ...]
    canonical_grammar: tuple[str, ...]


@dataclass(frozen=True)
class Descriptions:
    operations: dict[str, str]
    options: dict[str, str]


def fail(message: str) -> None:
    print(f"completion-generator: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_id_list(value: str) -> tuple[int, ...]:
    return tuple(int(item) for item in value.split(",") if item)


def parse_operand_terms(value: str) -> tuple[OperandTerm, ...]:
    terms: list[OperandTerm] = []
    for encoded in value.split(","):
        if not encoded:
            continue
        fields = encoded.split(":")
        if len(fields) != 3:
            fail(f"invalid operand term: {encoded!r}")
        if fields[0] not in KNOWN_OPERAND_KINDS:
            fail(f"unsupported operand kind projection: {fields[0]!r}")
        try:
            min_count = int(fields[1])
            max_count = None if fields[2] == "*" else int(fields[2])
        except ValueError:
            fail(f"invalid operand cardinality: {encoded!r}")
        if min_count < 0 or (max_count is not None and max_count < min_count):
            fail(f"invalid operand cardinality range: {encoded!r}")
        terms.append(OperandTerm(fields[0], min_count, max_count))
    return tuple(terms)


def export_authority() -> str:
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        fail("CXX does not name a compiler")
    with tempfile.TemporaryDirectory(prefix="moguet-cli-authority-") as directory:
        executable = Path(directory) / "export-cli-authority"
        compile_result = subprocess.run(
            [
                *compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                f"-I{REPOSITORY_ROOT / 'src'}",
                str(CLI_AUTHORITY_EXPORTER),
                str(CLI_PUBLIC_PROJECTION),
                "-o",
                str(executable),
            ],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        if compile_result.returncode != 0:
            fail(
                "could not compile the CLI authority exporter:\n"
                + compile_result.stderr.rstrip()
            )
        export_result = subprocess.run(
            [str(executable)],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        if export_result.returncode != 0:
            fail(
                "CLI authority projection failed:\n"
                + export_result.stderr.rstrip()
            )
        return export_result.stdout


def parse_exported_schema(exported_schema: str) -> CliSchema:
    """Parse and validate the exporter wire schema before any renderer uses it."""

    options: list[Option] = []
    operations: dict[str, Operation] = {}
    delegated_option_ids: tuple[int, ...] = ()
    terminal_tokens: list[str] = []
    canonical_grammar: list[str] = []

    for line in exported_schema.splitlines():
        fields = line.split("\t")
        record = fields[0]
        if record == "OPTION" and len(fields) == 8:
            if fields[7] not in {"definition", "syntax-only"}:
                fail(f"invalid public option projection: {line!r}")
            options.append(
                Option(
                    identity=int(fields[1]),
                    token=fields[2],
                    completion_token=fields[3],
                    occurrence=fields[4],
                    placement=fields[5],
                    conflicts=parse_id_list(fields[6]),
                    has_public_definition=fields[7] == "definition",
                )
            )
        elif record == "OPERATION" and len(fields) == 3:
            token = fields[1]
            existing = operations.get(token, Operation(token))
            operations[token] = replace(
                existing,
                open_grammar=existing.open_grammar or fields[2] == "open",
            )
        elif record == "FORM" and len(fields) == 8:
            token = fields[1]
            existing = operations.get(token, Operation(token))
            form = Form(
                syntax=fields[2],
                target_policy=fields[3],
                operand_ordering=fields[4],
                operand_terms=parse_operand_terms(fields[5]),
                option_ids=parse_id_list(fields[6]),
                selector_ids=parse_id_list(fields[7]),
            )
            operations[token] = replace(existing, forms=existing.forms + (form,))
        elif record == "DELEGATED_OPTIONS" and len(fields) == 2:
            delegated_option_ids = parse_id_list(fields[1])
        elif record == "TERMINAL" and len(fields) == 2:
            terminal_tokens.append(fields[1])
        elif record == "CANONICAL" and len(fields) == 2:
            canonical_grammar.append(fields[1])
        else:
            fail(f"invalid exporter record: {line!r}")

    if not options or not operations or not canonical_grammar:
        fail("CLI authority exporter returned an incomplete schema")
    if len({option.token for option in options}) != len(options):
        fail("CLI authority exporter returned duplicate option tokens")
    if any(not operation.forms and not operation.open_grammar for operation in operations.values()):
        fail("CLI authority exporter returned an operation without a grammar form")

    schema = CliSchema(
        operations=tuple(operations.values()),
        options=tuple(options),
        delegated_option_ids=delegated_option_ids,
        terminal_tokens=tuple(terminal_tokens),
        canonical_grammar=tuple(canonical_grammar),
    )
    validate_schema_operand_projection(schema)
    return schema


def load_schema() -> CliSchema:
    return parse_exported_schema(export_authority())


def validate_description_map(
    category: str, descriptions: object, expected_tokens: tuple[str, ...]
) -> dict[str, str]:
    if not isinstance(descriptions, dict):
        fail(f"description category '{category}' must be a JSON object")
    if not all(
        isinstance(key, str) and isinstance(value, str)
        for key, value in descriptions.items()
    ):
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
        fail(
            f"{category} descriptions do not match CLI authority "
            f"({'; '.join(details)})"
        )

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

    return Descriptions(
        validate_description_map(
            "operations",
            raw.get("operations"),
            tuple(operation.token for operation in schema.operations),
        ),
        validate_description_map(
            "options",
            raw.get("options"),
            tuple(option.token for option in schema.options),
        ),
    )


def shell_quote(value: str) -> str:
    return shlex.quote(value)


def options_for_ids(schema: CliSchema, identities: tuple[int, ...]) -> tuple[Option, ...]:
    by_identity: dict[int, list[Option]] = {}
    for option in schema.options:
        by_identity.setdefault(option.identity, []).append(option)
    return tuple(
        option
        for identity in identities
        for option in by_identity.get(identity, ())
    )


def unique_completion_tokens(options: tuple[Option, ...]) -> tuple[str, ...]:
    return tuple(dict.fromkeys(option.completion_token for option in options))


def union_form_ids(forms: tuple[Form, ...]) -> tuple[int, ...]:
    return tuple(
        dict.fromkeys(
            identity for form in forms for identity in form.option_ids
        )
    )


def option_case_patterns(schema: CliSchema) -> list[tuple[int, tuple[str, ...]]]:
    grouped: dict[int, list[str]] = {}
    for option in schema.options:
        patterns = grouped.setdefault(option.identity, [])
        patterns.append(option.token)
        if option.completion_token.endswith("="):
            patterns.append(option.token + "=*")
    return [(identity, tuple(patterns)) for identity, patterns in grouped.items()]


def validate_operand_projection(operation: Operation, form: Form) -> None:
    terms = form.operand_terms
    if form.target_policy not in KNOWN_TARGET_POLICIES:
        fail(f"unsupported target policy projection: {form.target_policy!r}")
    if form.operand_ordering not in KNOWN_OPERAND_ORDERINGS:
        fail(f"unsupported operand ordering projection: {form.operand_ordering!r}")

    if form.target_policy == "none":
        if form.operand_ordering != "none" or terms:
            fail(f"invalid targetless operand projection: {form.syntax}")
        return

    if form.target_policy == "exactly-one":
        if form.operand_ordering == "preserve-input-order":
            if (
                len(terms) != 1
                or terms[0].kind not in PRIMARY_OPERAND_KINDS
                or terms[0].min_count != 1
                or terms[0].max_count != 1
            ):
                fail(f"invalid exactly-one operand projection: {form.syntax}")
            return
        if form.operand_ordering == "primary-then-environment-assignments":
            if (
                len(terms) != 2
                or terms[0].kind not in ASSIGNMENT_PRIMARY_OPERAND_KINDS
                or terms[0].min_count != 1
                or terms[0].max_count != 1
                or terms[1].kind != "environment-assignment"
                or terms[1].min_count != 0
                or terms[1].max_count is not None
            ):
                fail(
                    "invalid exactly-one primary/assignment projection: "
                    f"{form.syntax}"
                )
            return
        fail(f"invalid exactly-one operand ordering: {form.syntax}")

    if form.target_policy == "one-or-more":
        if (
            form.operand_ordering != "preserve-input-order"
            or len(terms) != 1
            or terms[0].kind not in PRIMARY_OPERAND_KINDS
            or terms[0].min_count != 1
            or terms[0].max_count is not None
        ):
            fail(f"invalid one-or-more operand projection: {form.syntax}")
        return

    if form.target_policy == "ordered-items":
        if (
            form.operand_ordering
            != "package-introduces-following-assignment-scope"
            or len(terms) != 1
            or terms[0].kind != "source-preference-item"
            or terms[0].min_count != 1
            or terms[0].max_count is not None
        ):
            fail(f"invalid ordered-items operand projection: {form.syntax}")
        return

    if form.target_policy == "delegated":
        if (
            not operation.open_grammar
            or form.operand_ordering != "delegated"
            or len(terms) != 1
            or terms[0].kind != "delegated-pacman-argument"
            or terms[0].min_count != 0
            or terms[0].max_count is not None
        ):
            fail(f"invalid delegated operand projection: {form.syntax}")
        return

    fail(f"unsupported target policy projection: {form.target_policy!r}")


def validate_schema_operand_projection(schema: CliSchema) -> None:
    for operation in schema.operations:
        for form in operation.forms:
            validate_operand_projection(operation, form)


def finite_operand_max(form: Form) -> int | None:
    if form.operand_ordering == "none":
        return 0
    if form.operand_ordering == "preserve-input-order":
        return form.operand_terms[0].max_count
    return None


def bash_form_prefix_cases(schema: CliSchema) -> str:
    cases: list[str] = []
    for operation in schema.operations:
        for form_index, form in enumerate(operation.forms):
            key = shell_quote(f"{operation.token}:{form_index}")
            maximum = finite_operand_max(form)
            if maximum is not None:
                body = (
                    f"            (( ${{#operands[@]}} <= {maximum} )) && return 0\n"
                    "            return 1"
                )
            elif form.operand_ordering == "preserve-input-order":
                body = "            return 0"
            elif form.operand_ordering == "primary-then-environment-assignments":
                body = (
                    "            (( ${#operands[@]} == 0 )) && return 0\n"
                    "            _moguet_is_assignment_operand \"${operands[0]}\" && return 1\n"
                    "            for word in \"${operands[@]:1}\"; do\n"
                    "                _moguet_is_assignment_operand \"$word\" || return 1\n"
                    "            done\n"
                    "            return 0"
                )
            elif form.operand_ordering == "package-introduces-following-assignment-scope":
                body = (
                    "            (( ${#operands[@]} == 0 )) && return 0\n"
                    "            _moguet_is_assignment_operand \"${operands[0]}\" && return 1\n"
                    "            return 0"
                )
            elif form.operand_ordering == "delegated":
                body = "            return 0"
            else:
                fail(f"unsupported Bash operand projection: {form.syntax}")
            cases.append(f"        {key})\n{body}\n            ;;")
    return "\n".join(cases)


def zsh_form_prefix_cases(schema: CliSchema) -> str:
    cases: list[str] = []
    for operation in schema.operations:
        for form_index, form in enumerate(operation.forms):
            key = shell_quote(f"{operation.token}:{form_index}")
            maximum = finite_operand_max(form)
            if maximum is not None:
                body = (
                    f"            (( ${{#operands[@]}} <= {maximum} )) && return 0\n"
                    "            return 1"
                )
            elif form.operand_ordering == "preserve-input-order":
                body = "            return 0"
            elif form.operand_ordering == "primary-then-environment-assignments":
                body = (
                    "            (( ${#operands[@]} == 0 )) && return 0\n"
                    "            _moguet_is_assignment_operand \"${operands[1]}\" && return 1\n"
                    "            for (( operand_index=2; operand_index<=${#operands[@]}; ++operand_index )); do\n"
                    "                _moguet_is_assignment_operand \"${operands[operand_index]}\" || return 1\n"
                    "            done\n"
                    "            return 0"
                )
            elif form.operand_ordering == "package-introduces-following-assignment-scope":
                body = (
                    "            (( ${#operands[@]} == 0 )) && return 0\n"
                    "            _moguet_is_assignment_operand \"${operands[1]}\" && return 1\n"
                    "            return 0"
                )
            elif form.operand_ordering == "delegated":
                body = "            return 0"
            else:
                fail(f"unsupported Zsh operand projection: {form.syntax}")
            cases.append(f"        {key})\n{body}\n            ;;")
    return "\n".join(cases)


def fish_form_prefix_cases(schema: CliSchema) -> list[str]:
    cases: list[str] = []
    for operation in schema.operations:
        for form_index, form in enumerate(operation.forms):
            key = fish_quote(f"{operation.token}:{form_index}")
            maximum = finite_operand_max(form)
            if maximum is not None:
                body = [
                    f"            test (count $operands) -le {maximum}; and return 0",
                    "            return 1",
                ]
            elif form.operand_ordering == "preserve-input-order":
                body = ["            return 0"]
            elif form.operand_ordering == "primary-then-environment-assignments":
                body = [
                    "            test (count $operands) -eq 0; and return 0",
                    "            __moguet_is_assignment_operand \"$operands[1]\"; and return 1",
                    "            for word in $operands[2..-1]",
                    "                __moguet_is_assignment_operand \"$word\"; or return 1",
                    "            end",
                    "            return 0",
                ]
            elif form.operand_ordering == "package-introduces-following-assignment-scope":
                body = [
                    "            test (count $operands) -eq 0; and return 0",
                    "            __moguet_is_assignment_operand \"$operands[1]\"; and return 1",
                    "            return 0",
                ]
            elif form.operand_ordering == "delegated":
                body = ["            return 0"]
            else:
                fail(f"unsupported Fish operand projection: {form.syntax}")
            cases.extend([f"        case {key}", *body])
    return cases


def bash_array(values: tuple[str, ...] | list[str], indent: str = "            ") -> str:
    return " ".join(shell_quote(value) for value in values)


def render_bash(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    del descriptions
    operations = tuple(operation.token for operation in schema.operations)
    root_options = tuple(
        option
        for option in schema.options
        if option.placement in {"parser-global", "first-non-global"}
    )
    terminal_pattern = "|".join(schema.terminal_tokens)
    operation_pattern = "|".join(operations)
    once_ids = tuple(
        dict.fromkeys(
            option.identity
            for option in schema.options
            if option.occurrence == "once"
        )
    )

    option_id_cases = "\n".join(
        f"        {'|'.join(patterns)}) printf '%s' {identity} ;;"
        for identity, patterns in option_case_patterns(schema)
    )
    conflict_cases = "\n".join(
        f"        {option.identity}) "
        + " || ".join(
            f"_moguet_has_option_id {conflict}"
            for conflict in option.conflicts
        )
        + " ;;"
        for option in schema.options
        if option.conflicts
    )

    operation_cases: list[str] = []
    delegated_tokens = unique_completion_tokens(
        options_for_ids(schema, schema.delegated_option_ids)
    )
    for operation in schema.operations:
        if len(operation.forms) > 1:
            selector_forms = [form for form in operation.forms if form.selector_ids]
            default_forms = [form for form in operation.forms if not form.selector_ids]
            if len(selector_forms) != 1 or len(default_forms) != 1:
                fail(f"unsupported multi-form completion projection: {operation.token}")
            selected = selector_forms[0]
            default = default_forms[0]
            selected_index = operation.forms.index(selected)
            default_index = operation.forms.index(default)
            selector_checks = " || ".join(
                f"_moguet_has_option_id {identity}"
                for identity in selected.selector_ids
            )
            selected_tokens = unique_completion_tokens(
                options_for_ids(schema, selected.option_ids)
            )
            default_tokens = unique_completion_tokens(
                options_for_ids(schema, default.option_ids)
            )
            union_tokens = unique_completion_tokens(
                options_for_ids(schema, union_form_ids(operation.forms))
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if {selector_checks}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} {selected_index}; then\n"
                f"                    candidates=({bash_array(selected_tokens)})\n"
                f"                else\n"
                f"                    candidates=()\n"
                f"                fi\n"
                f"            elif _moguet_has_operand {shell_quote(operation.token)}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} {default_index}; then\n"
                f"                    candidates=({bash_array(default_tokens)})\n"
                f"                else\n"
                f"                    candidates=()\n"
                f"                fi\n"
                f"            else\n"
                f"                candidates=({bash_array(union_tokens)})\n"
                f"            fi\n"
                f"            ;;"
            )
        elif operation.forms and operation.open_grammar:
            form = operation.forms[0]
            selector_checks = " || ".join(
                f"_moguet_has_option_id {identity}"
                for identity in form.selector_ids
            )
            selected_tokens = unique_completion_tokens(
                options_for_ids(schema, form.option_ids)
            )
            preselection_ids = tuple(
                dict.fromkeys(schema.delegated_option_ids + form.selector_ids)
            )
            preselection_tokens = unique_completion_tokens(
                options_for_ids(schema, preselection_ids)
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if {selector_checks}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} 0; then\n"
                f"                    candidates=({bash_array(selected_tokens)})\n"
                f"                else\n"
                f"                    candidates=()\n"
                f"                fi\n"
                f"            else\n"
                f"                candidates=({bash_array(preselection_tokens)})\n"
                f"            fi\n"
                f"            ;;"
            )
        elif operation.forms:
            form = operation.forms[0]
            tokens = unique_completion_tokens(
                options_for_ids(schema, form.option_ids)
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if _moguet_form_prefix_valid {shell_quote(operation.token)} 0; then\n"
                f"                candidates=({bash_array(tokens)})\n"
                f"            else\n"
                f"                candidates=()\n"
                f"            fi\n"
                f"            ;;"
            )
        else:
            operation_cases.append(
                f"        {operation.token}) candidates=({bash_array(delegated_tokens)}) ;;"
            )
    for terminal in schema.terminal_tokens:
        operation_cases.append(f"        {terminal}) candidates=() ;;")

    canonical_comments = "\n".join(
        f"#   {syntax}" for syntax in schema.canonical_grammar
    )
    return f"""# Generated by scripts/generate_completions.py; do not edit.
# Description locale: {locale}
# Canonical closed grammar (projected from src/cli_authority.hpp):
{canonical_comments}

_moguet_option_id() {{
    case "$1" in
{option_id_cases}
        *) return 1 ;;
    esac
}}

_moguet_has_option_id() {{
    local expected="$1" word actual
    for word in "${{COMP_WORDS[@]:1:COMP_CWORD-1}}"; do
        actual="$(_moguet_option_id "$word" || true)"
        [[ $actual == "$expected" ]] && return 0
    done
    return 1
}}

_moguet_find_operation() {{
    local word
    for word in "${{COMP_WORDS[@]:1:COMP_CWORD-1}}"; do
        case "$word" in
        {terminal_pattern}|{operation_pattern}) printf '%s' "$word"; return 0 ;;
        esac
        _moguet_option_id "$word" >/dev/null && continue
        if [[ $word == -* ]]; then
            printf '%s' __delegated__
            return 0
        fi
        printf '%s' __invalid__
        return 0
    done
    return 1
}}

_moguet_has_operand() {{
    local expected_operation="$1" word actual seen_operation=false
    for word in "${{COMP_WORDS[@]:1:COMP_CWORD-1}}"; do
        if [[ $seen_operation == false ]]; then
            if [[ $word == "$expected_operation" ]]; then
                seen_operation=true
            fi
            continue
        fi
        actual="$(_moguet_option_id "$word" || true)"
        [[ -z $actual ]] && return 0
    done
    return 1
}}

_moguet_is_assignment_operand() {{
    [[ $1 == *=* ]]
}}

_moguet_form_prefix_valid() {{
    local expected_operation="$1" form_index="$2" word actual
    local seen_operation=false
    local -a operands=()
    for word in "${{COMP_WORDS[@]:1:COMP_CWORD-1}}"; do
        if [[ $seen_operation == false ]]; then
            [[ $word == "$expected_operation" ]] && seen_operation=true
            continue
        fi
        actual="$(_moguet_option_id "$word" || true)"
        [[ -n $actual ]] && continue
        operands+=("$word")
    done
    case "$expected_operation:$form_index" in
{bash_form_prefix_cases(schema)}
        *) return 1 ;;
    esac
}}

_moguet_conflicts_with_present_option() {{
    case "$1" in
{conflict_cases}
        *) return 1 ;;
    esac
}}

_moguet() {{
    local cur operation candidate option_id
    local -a candidates filtered
    cur="${{COMP_WORDS[COMP_CWORD]}}"
    operation="$(_moguet_find_operation || true)"

    if [[ -z $operation ]]; then
        candidates=({bash_array(list(operations) + [option.completion_token for option in root_options])})
    else
        case "$operation" in
{chr(10).join(operation_cases)}
        __delegated__) candidates=({bash_array(delegated_tokens)}) ;;
        *) candidates=() ;;
        esac
    fi

    filtered=()
    for candidate in "${{candidates[@]}}"; do
        option_id="$(_moguet_option_id "$candidate" || true)"
        if [[ -n $option_id ]]; then
            case "$option_id" in
            {'|'.join(str(identity) for identity in once_ids)})
                _moguet_has_option_id "$option_id" && continue
                ;;
            esac
            _moguet_conflicts_with_present_option "$option_id" && continue
        fi
        filtered+=("$candidate")
    done

    COMPREPLY=()
    COMPREPLY=( $(compgen -W "${{filtered[*]}}" -- "$cur" || true) )
}}

complete -F _moguet moguet
"""


def zsh_case_values(values: tuple[str, ...] | list[str]) -> str:
    return " ".join(shell_quote(value) for value in values)


def render_zsh(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    operations = tuple(operation.token for operation in schema.operations)
    root_options = tuple(
        option
        for option in schema.options
        if option.placement in {"parser-global", "first-non-global"}
    )
    terminal_pattern = "|".join(schema.terminal_tokens)
    operation_pattern = "|".join(operations)
    once_ids = tuple(
        dict.fromkeys(
            option.identity
            for option in schema.options
            if option.occurrence == "once"
        )
    )
    option_id_cases = "\n".join(
        f"        {'|'.join(patterns)}) REPLY={identity} ;;"
        for identity, patterns in option_case_patterns(schema)
    )
    conflict_cases = "\n".join(
        f"        {option.identity}) "
        + " || ".join(
            f"_moguet_has_option_id {conflict}"
            for conflict in option.conflicts
        )
        + " ;;"
        for option in schema.options
        if option.conflicts
    )

    delegated_tokens = unique_completion_tokens(
        options_for_ids(schema, schema.delegated_option_ids)
    )
    operation_cases: list[str] = []
    for operation in schema.operations:
        if len(operation.forms) > 1:
            selected = next(form for form in operation.forms if form.selector_ids)
            default = next(form for form in operation.forms if not form.selector_ids)
            selected_index = operation.forms.index(selected)
            default_index = operation.forms.index(default)
            selector_checks = " || ".join(
                f"_moguet_has_option_id {identity}"
                for identity in selected.selector_ids
            )
            selected_tokens = unique_completion_tokens(
                options_for_ids(schema, selected.option_ids)
            )
            default_tokens = unique_completion_tokens(
                options_for_ids(schema, default.option_ids)
            )
            union_tokens = unique_completion_tokens(
                options_for_ids(schema, union_form_ids(operation.forms))
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if {selector_checks}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} {selected_index}; then\n"
                f"                    reply=({zsh_case_values(selected_tokens)})\n"
                f"                else\n"
                f"                    reply=()\n"
                f"                fi\n"
                f"            elif _moguet_has_operand {shell_quote(operation.token)}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} {default_index}; then\n"
                f"                    reply=({zsh_case_values(default_tokens)})\n"
                f"                else\n"
                f"                    reply=()\n"
                f"                fi\n"
                f"            else\n"
                f"                reply=({zsh_case_values(union_tokens)})\n"
                f"            fi\n"
                f"            ;;"
            )
        elif operation.forms and operation.open_grammar:
            form = operation.forms[0]
            selector_checks = " || ".join(
                f"_moguet_has_option_id {identity}"
                for identity in form.selector_ids
            )
            selected_tokens = unique_completion_tokens(
                options_for_ids(schema, form.option_ids)
            )
            preselection_ids = tuple(
                dict.fromkeys(schema.delegated_option_ids + form.selector_ids)
            )
            preselection_tokens = unique_completion_tokens(
                options_for_ids(schema, preselection_ids)
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if {selector_checks}; then\n"
                f"                if _moguet_form_prefix_valid {shell_quote(operation.token)} 0; then\n"
                f"                    reply=({zsh_case_values(selected_tokens)})\n"
                f"                else\n"
                f"                    reply=()\n"
                f"                fi\n"
                f"            else\n"
                f"                reply=({zsh_case_values(preselection_tokens)})\n"
                f"            fi\n"
                f"            ;;"
            )
        elif operation.forms:
            form = operation.forms[0]
            tokens = unique_completion_tokens(
                options_for_ids(schema, form.option_ids)
            )
            operation_cases.append(
                f"        {operation.token})\n"
                f"            if _moguet_form_prefix_valid {shell_quote(operation.token)} 0; then\n"
                f"                reply=({zsh_case_values(tokens)})\n"
                f"            else\n"
                f"                reply=()\n"
                f"            fi\n"
                f"            ;;"
            )
        else:
            operation_cases.append(
                f"        {operation.token}) reply=({zsh_case_values(delegated_tokens)}) ;;"
            )
    for terminal in schema.terminal_tokens:
        operation_cases.append(f"        {terminal}) reply=() ;;")

    description_cases: list[str] = []
    for operation in schema.operations:
        description = descriptions.operations[operation.token].replace(":", r"\:")
        description_cases.append(
            f"        {operation.token}) REPLY={shell_quote(description)} ;;"
        )
    for option in schema.options:
        description = descriptions.options[option.token].replace(":", r"\:")
        description_cases.append(
            f"        {shell_quote(option.completion_token)}) "
            f"REPLY={shell_quote(description)} ;;"
        )

    canonical_comments = "\n".join(
        f"#   {syntax}" for syntax in schema.canonical_grammar
    )
    root_candidates = list(operations) + [
        option.completion_token for option in root_options
    ]
    return f"""#compdef moguet
# Generated by scripts/generate_completions.py; do not edit.
# Description locale: {locale}
# Canonical closed grammar (projected from src/cli_authority.hpp):
{canonical_comments}

_moguet_option_id() {{
    REPLY=
    case "$1" in
{option_id_cases}
    esac
    [[ -n $REPLY ]]
}}

_moguet_has_option_id() {{
    local expected="$1" word actual
    local index
    for (( index=2; index<CURRENT; ++index )); do
        word=$words[index]
        _moguet_option_id "$word" || continue
        actual=$REPLY
        [[ $actual == "$expected" ]] && return 0
    done
    return 1
}}

_moguet_find_operation() {{
    local word index
    REPLY=
    for (( index=2; index<CURRENT; ++index )); do
        word=$words[index]
        case "$word" in
        {terminal_pattern}|{operation_pattern}) REPLY=$word; return 0 ;;
        esac
        _moguet_option_id "$word" && continue
        if [[ $word == -* ]]; then
            REPLY=__delegated__
            return 0
        fi
        REPLY=__invalid__
        return 0
    done
    return 1
}}

_moguet_has_operand() {{
    local expected_operation="$1" word index
    local seen_operation=false
    for (( index=2; index<CURRENT; ++index )); do
        word=$words[index]
        if [[ $seen_operation == false ]]; then
            [[ $word == "$expected_operation" ]] && seen_operation=true
            continue
        fi
        _moguet_option_id "$word" || return 0
    done
    return 1
}}

_moguet_is_assignment_operand() {{
    [[ $1 == *=* ]]
}}

_moguet_form_prefix_valid() {{
    local expected_operation="$1" form_index="$2" word actual operand_index
    local seen_operation=false
    local -a operands
    for (( operand_index=2; operand_index<CURRENT; ++operand_index )); do
        word=$words[operand_index]
        if [[ $seen_operation == false ]]; then
            [[ $word == "$expected_operation" ]] && seen_operation=true
            continue
        fi
        _moguet_option_id "$word" && continue
        operands+=("$word")
    done
    case "$expected_operation:$form_index" in
{zsh_form_prefix_cases(schema)}
        *) return 1 ;;
    esac
}}

_moguet_conflicts_with_present_option() {{
    case "$1" in
{conflict_cases}
        *) return 1 ;;
    esac
}}

_moguet_collect_candidates() {{
    local operation="$1"
    typeset -ga reply
    reply=()
    case "$operation" in
{chr(10).join(operation_cases)}
        __delegated__) reply=({zsh_case_values(delegated_tokens)}) ;;
    esac
}}

_moguet_description() {{
    REPLY=
    case "$1" in
{chr(10).join(description_cases)}
    esac
}}

_moguet() {{
    local operation candidate option_id
    local -a candidates filtered described
    _moguet_find_operation
    operation=$REPLY

    if [[ -z $operation ]]; then
        candidates=({zsh_case_values(root_candidates)})
    else
        _moguet_collect_candidates "$operation"
        candidates=("${{reply[@]}}")
    fi

    for candidate in "${{candidates[@]}}"; do
        if _moguet_option_id "$candidate"; then
            option_id=$REPLY
            case "$option_id" in
            {'|'.join(str(identity) for identity in once_ids)})
                _moguet_has_option_id "$option_id" && continue
                ;;
            esac
            _moguet_conflicts_with_present_option "$option_id" && continue
        fi
        filtered+=("$candidate")
    done

    for candidate in "${{filtered[@]}}"; do
        _moguet_description "$candidate"
        described+=("$candidate:$REPLY")
    done
    _describe -t moguet-values 'moguet value' described
}}

compdef _moguet moguet
"""


def fish_quote(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def fish_contains(ids: tuple[int, ...]) -> str:
    if not ids:
        return "return 1"
    return (
        "contains -- $option_id "
        + " ".join(str(identity) for identity in ids)
        + "; and return 0; or return 1"
    )


def render_fish(schema: CliSchema, descriptions: Descriptions, locale: str) -> str:
    operations = tuple(operation.token for operation in schema.operations)
    root_ids = tuple(
        dict.fromkeys(
            option.identity
            for option in schema.options
            if option.placement in {"parser-global", "first-non-global"}
        )
    )
    once_ids = tuple(
        dict.fromkeys(
            option.identity
            for option in schema.options
            if option.occurrence == "once"
        )
    )
    option_id_cases = "\n".join(
        "        case "
        + " ".join(fish_quote(pattern) for pattern in patterns)
        + f"\n            echo {identity}\n            return 0"
        for identity, patterns in option_case_patterns(schema)
    )
    terminal_cases = " ".join(fish_quote(token) for token in schema.terminal_tokens)
    operation_cases = " ".join(fish_quote(token) for token in operations)

    allow_cases: list[str] = []
    delegated_ids = tuple(
        identity
        for identity in schema.delegated_option_ids
        if any(option.identity == identity for option in schema.options)
    )
    for operation in schema.operations:
        if len(operation.forms) > 1:
            selected = next(form for form in operation.forms if form.selector_ids)
            default = next(form for form in operation.forms if not form.selector_ids)
            selected_index = operation.forms.index(selected)
            default_index = operation.forms.index(default)
            selector_test = "\n".join(
                f"            __moguet_has_option_id {identity}; and set selected true"
                for identity in selected.selector_ids
            )
            allow_cases.append(
                f"        case {fish_quote(operation.token)}\n"
                f"            set -l selected false\n"
                f"{selector_test}\n"
                f"            if test $selected = true\n"
                f"                __moguet_form_prefix_valid {fish_quote(operation.token)} {selected_index}; or return 1\n"
                f"                {fish_contains(selected.option_ids)}\n"
                f"            else if __moguet_has_operand {fish_quote(operation.token)}\n"
                f"                __moguet_form_prefix_valid {fish_quote(operation.token)} {default_index}; or return 1\n"
                f"                {fish_contains(default.option_ids)}\n"
                f"            else\n"
                f"                {fish_contains(union_form_ids(operation.forms))}\n"
                f"            end"
            )
        elif operation.forms and operation.open_grammar:
            form = operation.forms[0]
            selector_test = "\n".join(
                f"            __moguet_has_option_id {identity}; and set selected true"
                for identity in form.selector_ids
            )
            preselection_ids = tuple(
                dict.fromkeys(delegated_ids + form.selector_ids)
            )
            allow_cases.append(
                f"        case {fish_quote(operation.token)}\n"
                f"            set -l selected false\n"
                f"{selector_test}\n"
                f"            if test $selected = true\n"
                f"                __moguet_form_prefix_valid {fish_quote(operation.token)} 0; or return 1\n"
                f"                {fish_contains(form.option_ids)}\n"
                f"            else\n"
                f"                {fish_contains(preselection_ids)}\n"
                f"            end"
            )
        elif operation.forms:
            form = operation.forms[0]
            allow_cases.append(
                f"        case {fish_quote(operation.token)}\n"
                f"            __moguet_form_prefix_valid {fish_quote(operation.token)} 0; or return 1\n"
                f"            {fish_contains(form.option_ids)}"
            )
        else:
            allow_cases.append(
                f"        case {fish_quote(operation.token)}\n"
                f"            {fish_contains(delegated_ids)}"
            )

    conflict_cases = "\n".join(
        f"        case {option.identity}\n"
        + "\n".join(
            f"            __moguet_has_option_id {conflict}; and return 1"
            for conflict in option.conflicts
        )
        for option in schema.options
        if option.conflicts
    )
    canonical_comments = "\n".join(
        f"#   {syntax}" for syntax in schema.canonical_grammar
    )

    lines = [
        "# Generated by scripts/generate_completions.py; do not edit.",
        f"# Description locale: {locale}",
        "# Canonical closed grammar (projected from src/cli_authority.hpp):",
        canonical_comments,
        "",
        "function __moguet_option_id --argument-names word",
        "    switch $word",
        option_id_cases,
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_has_option_id --argument-names expected",
        "    for word in (commandline -opc)[2..-1]",
        "        set -l actual (__moguet_option_id $word)",
        "        test \"$actual\" = \"$expected\"; and return 0",
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_operation",
        "    for word in (commandline -opc)[2..-1]",
        "        switch $word",
        f"        case {terminal_cases} {operation_cases}",
        "            echo $word",
        "            return 0",
        "        end",
        "        __moguet_option_id $word >/dev/null; and continue",
        "        string match -q -- '-*' $word; and echo __delegated__; and return 0",
        "        echo __invalid__",
        "        return 0",
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_has_operand --argument-names expected_operation",
        "    set -l seen_operation false",
        "    for word in (commandline -opc)[2..-1]",
        "        if test $seen_operation = false",
        "            test \"$word\" = \"$expected_operation\"; and set seen_operation true",
        "            continue",
        "        end",
        "        __moguet_option_id $word >/dev/null; or return 0",
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_is_assignment_operand --argument-names word",
        "    string match -q -- '*=*' \"$word\"",
        "end",
        "",
        "function __moguet_form_prefix_valid --argument-names expected_operation form_index",
        "    set -l seen_operation false",
        "    set -l operands",
        "    for word in (commandline -opc)[2..-1]",
        "        if test $seen_operation = false",
        "            test \"$word\" = \"$expected_operation\"; and set seen_operation true",
        "            continue",
        "        end",
        "        __moguet_option_id $word >/dev/null; and continue",
        "        set -a operands \"$word\"",
        "    end",
        "    switch \"$expected_operation:$form_index\"",
        *fish_form_prefix_cases(schema),
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_operation_allows --argument-names option_id",
        "    set -l operation (__moguet_operation)",
        "    if test -z \"$operation\"",
        f"        {fish_contains(root_ids)}",
        "    end",
        "    switch $operation",
        *allow_cases,
        "        case __delegated__",
        f"            {fish_contains(delegated_ids)}",
        "    end",
        "    return 1",
        "end",
        "",
        "function __moguet_candidate_available --argument-names option_id",
        "    __moguet_operation_allows $option_id; or return 1",
        f"    contains -- $option_id {' '.join(str(identity) for identity in once_ids)}; and __moguet_has_option_id $option_id; and return 1",
        "    switch $option_id",
        conflict_cases,
        "    end",
        "    return 0",
        "end",
        "",
        "function __moguet_no_operation",
        "    not __moguet_operation >/dev/null",
        "end",
        "",
    ]
    for operation in schema.operations:
        lines.append(
            "complete -c moguet -f -n '__moguet_no_operation' -a "
            f"{fish_quote(operation.token)} -d "
            f"{fish_quote(descriptions.operations[operation.token])}"
        )
    for option in schema.options:
        lines.append(
            "complete -c moguet -f -n "
            f"{fish_quote(f'__moguet_candidate_available {option.identity}')} "
            f"-a {fish_quote(option.completion_token)} -d "
            f"{fish_quote(descriptions.options[option.token])}"
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
        return 0 if all(
            check_generated(path, content) for path, content in outputs.items()
        ) else 1

    output_dir.mkdir(parents=True, exist_ok=True)
    for path, content in outputs.items():
        path.write_text(content, encoding="utf-8")
        shown = path.relative_to(REPOSITORY_ROOT) if path.is_relative_to(REPOSITORY_ROOT) else path
        print(f"generated {shown}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
