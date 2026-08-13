#!/usr/bin/env python3

from __future__ import annotations

from contextlib import redirect_stderr
import io
from pathlib import Path
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))

from generate_completions import parse_exported_schema  # noqa: E402


def fail(message: str) -> None:
    print(f"completion-schema-validator-test: {message}", file=sys.stderr)
    raise SystemExit(1)


def exported_schema(
    *,
    target_policy: str | None = None,
    operand_ordering: str | None = None,
    operand_terms: str = "",
    open_grammar: bool = False,
) -> str:
    lines = [
        "OPTION\t0\t--help\t--help\tonce\tparser-global\t\tdefinition",
        f"OPERATION\tfixture\t{'open' if open_grammar else 'closed'}",
    ]
    if target_policy is not None and operand_ordering is not None:
        lines.append(
            "\t".join(
                (
                    "FORM",
                    "fixture",
                    "fixture <operand>",
                    target_policy,
                    operand_ordering,
                    operand_terms,
                    "",
                    "",
                )
            )
        )
    lines.append("CANONICAL\tfixture <operand>")
    return "\n".join(lines) + "\n"


def expect_accepted(label: str, schema: str) -> None:
    try:
        parse_exported_schema(schema)
    except SystemExit as error:
        fail(f"{label} unexpectedly failed with status {error.code!r}")
    print(f"  ok: accepted {label}")


def expect_rejected(label: str, schema: str, expected_diagnostic: str) -> None:
    diagnostic = io.StringIO()
    try:
        with redirect_stderr(diagnostic):
            parse_exported_schema(schema)
    except SystemExit as error:
        output = diagnostic.getvalue()
        if error.code == 1 and expected_diagnostic in output:
            print(f"  ok: rejected {label}")
            return
        fail(
            f"{label} returned status {error.code!r} with unexpected diagnostic: "
            f"{output.strip()!r}"
        )
    fail(f"{label} unexpectedly passed")


def main() -> int:
    positive_controls = (
        (
            "remote build primary plus repeated assignments",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="primary-then-environment-assignments",
                operand_terms="package:1:1,environment-assignment:0:*",
            ),
        ),
        (
            "local build primary plus repeated assignments",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="primary-then-environment-assignments",
                operand_terms="directory:1:1,environment-assignment:0:*",
            ),
        ),
        (
            "ordinary exactly-one form",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:1",
            ),
        ),
        (
            "one-or-more form",
            exported_schema(
                target_policy="one-or-more",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:*",
            ),
        ),
        (
            "ordered source-preference items",
            exported_schema(
                target_policy="ordered-items",
                operand_ordering="package-introduces-following-assignment-scope",
                operand_terms="source-preference-item:1:*",
            ),
        ),
        (
            "targetless form",
            exported_schema(
                target_policy="none",
                operand_ordering="none",
            ),
        ),
        (
            "delegated open grammar without a closed form",
            exported_schema(open_grammar=True),
        ),
        (
            "delegated open operand projection",
            exported_schema(
                target_policy="delegated",
                operand_ordering="delegated",
                operand_terms="delegated-pacman-argument:0:*",
                open_grammar=True,
            ),
        ),
    )
    for label, schema in positive_controls:
        expect_accepted(label, schema)

    rejected_controls = (
        (
            "unknown operand kind",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="preserve-input-order",
                operand_terms="unknown-kind:1:1",
            ),
            "unsupported operand kind projection",
        ),
        (
            "exactly-one with unbounded maximum",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:*",
            ),
            "invalid exactly-one operand projection",
        ),
        (
            "one-or-more with finite maximum",
            exported_schema(
                target_policy="one-or-more",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:1",
            ),
            "invalid one-or-more operand projection",
        ),
        (
            "ordered-items with wrong kind",
            exported_schema(
                target_policy="ordered-items",
                operand_ordering="package-introduces-following-assignment-scope",
                operand_terms="package:1:*",
            ),
            "invalid ordered-items operand projection",
        ),
        (
            "missing operand term",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="preserve-input-order",
            ),
            "invalid exactly-one operand projection",
        ),
        (
            "unknown target policy",
            exported_schema(
                target_policy="unknown-policy",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:1",
            ),
            "unsupported target policy projection",
        ),
        (
            "unknown operand ordering",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="unknown-ordering",
                operand_terms="package:1:1",
            ),
            "unsupported operand ordering projection",
        ),
        (
            "targetless form with an operand",
            exported_schema(
                target_policy="none",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:1",
            ),
            "invalid targetless operand projection",
        ),
        (
            "closed form with delegated operands",
            exported_schema(
                target_policy="delegated",
                operand_ordering="delegated",
                operand_terms="delegated-pacman-argument:0:*",
            ),
            "invalid delegated operand projection",
        ),
    )
    for label, schema, expected_diagnostic in rejected_controls:
        expect_rejected(label, schema, expected_diagnostic)

    print(
        "completion-schema-validator-test: "
        f"{len(positive_controls) + len(rejected_controls)} scenarios passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
