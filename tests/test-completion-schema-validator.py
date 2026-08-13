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


def option_record(
    identity: int = 0,
    token: str = "--help",
    completion_token: str | None = None,
    occurrence: str = "once",
    placement: str = "parser-global",
    value_kind: str = "none",
    allowed_values: str = "",
    conflict_rule: str = "none",
    conflicts: str = "",
    conflict_value_identity: str = "",
    semantic_scopes: str = "information",
    ownership: str = "moguet-owned",
    definition_role: str = "definition",
) -> str:
    return "\t".join(
        (
            "OPTION",
            str(identity),
            token,
            completion_token if completion_token is not None else token,
            occurrence,
            placement,
            value_kind,
            allowed_values,
            conflict_rule,
            conflicts,
            conflict_value_identity,
            semantic_scopes,
            ownership,
            definition_role,
        )
    )


def exported_schema(
    *,
    target_policy: str = "none",
    operand_ordering: str = "none",
    operand_terms: str = "",
    open_grammar: bool = False,
    syntax: str = "fixture <operand>",
    relations: str = "",
    options: tuple[str, ...] | None = None,
    delegated_relations: str = "",
    canonical: str | None = None,
) -> str:
    lines = [*(options if options is not None else (option_record(),))]
    lines.extend(
        (
            "OPERATION\tfixture\tclosed",
            "\t".join(
                (
                    "FORM",
                    "fixture",
                    syntax,
                    target_policy,
                    operand_ordering,
                    operand_terms,
                    relations,
                )
            )
        )
    )
    if open_grammar:
        lines.extend(
            (
                "OPERATION\tdelegated-example\topen",
                "\t".join(
                    (
                        "DELEGATED",
                        "delegated",
                        "delegated",
                        "delegated-pacman-argument:0:*",
                        delegated_relations,
                    )
                ),
            )
        )
    lines.append(f"CANONICAL\t{canonical if canonical is not None else syntax}")
    return "\n".join(lines) + "\n"


def without_record(schema: str, record: str) -> str:
    prefix = record + "\t"
    return "\n".join(
        line for line in schema.splitlines() if not line.startswith(prefix)
    ) + "\n"


def duplicate_first_record(schema: str, record: str) -> str:
    lines = schema.splitlines()
    prefix = record + "\t"
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            lines.insert(index + 1, line)
            return "\n".join(lines) + "\n"
    fail(f"fixture record was not found: {record}")


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
            "delegated open option projection",
            exported_schema(
                open_grammar=True,
                delegated_relations=(
                    "0:optional:delegated:hidden:upstream-argument:"
                    "pacman:preserve-all"
                ),
            ),
        ),
        (
            "attached enum option semantics",
            exported_schema(
                options=(
                    option_record(
                        token="--mode",
                        completion_token="--mode=",
                        occurrence="repeat-same-value",
                        value_kind="attached-enum",
                        allowed_values="normal,rebuild,clean",
                        semantic_scopes="source-build",
                    ),
                )
            ),
        ),
        (
            "public optional operation-local syntax",
            exported_schema(
                target_policy="one-or-more",
                operand_ordering="preserve-input-order",
                operand_terms="package:1:*",
                syntax="fixture [--recursive] <operand>",
                relations=(
                    "0:optional:repeat-idempotent:optional:"
                    "moguet-control:none:none"
                ),
                options=(
                    option_record(
                        token="--recursive",
                        occurrence="repeat-idempotent",
                        placement="operation-local",
                        semantic_scopes="dependency-inspection",
                        definition_role="syntax-only",
                    ),
                ),
            ),
        ),
        (
            "public required operation-local syntax",
            exported_schema(
                target_policy="exactly-one",
                operand_ordering="preserve-input-order",
                operand_terms="directory:1:1",
                syntax="fixture --local <operand>",
                relations=(
                    "0:required:once:required:moguet-control:none:none"
                ),
                options=(
                    option_record(
                        token="--local",
                        placement="operation-local",
                        semantic_scopes="local-source-build",
                        definition_role="syntax-only",
                    ),
                ),
            ),
        ),
        (
            "symmetric final-value conflicts",
            exported_schema(
                options=(
                    option_record(
                        identity=0,
                        token="--left",
                        occurrence="repeat-idempotent",
                        conflict_rule="final-value-must-agree",
                        conflicts="1",
                        conflict_value_identity="fixture.choice",
                    ),
                    option_record(
                        identity=1,
                        token="--right",
                        occurrence="repeat-idempotent",
                        conflict_rule="final-value-must-agree",
                        conflicts="0",
                        conflict_value_identity="fixture.choice",
                    ),
                )
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
        (
            "unknown option occurrence",
            exported_schema(
                options=(option_record(occurrence="unknown-occurrence"),)
            ),
            "unsupported option occurrence projection",
        ),
        (
            "unknown option placement",
            exported_schema(
                options=(option_record(placement="unknown-placement"),)
            ),
            "unsupported option placement projection",
        ),
        (
            "unknown option value kind",
            exported_schema(
                options=(option_record(value_kind="unknown-value"),)
            ),
            "unsupported option value projection",
        ),
        (
            "unknown option conflict rule",
            exported_schema(
                options=(option_record(conflict_rule="unknown-conflict"),)
            ),
            "unsupported option conflict projection",
        ),
        (
            "unknown option semantic scope",
            exported_schema(
                options=(option_record(semantic_scopes="unknown-scope"),)
            ),
            "unsupported option semantic scope projection",
        ),
        (
            "unknown option ownership",
            exported_schema(
                options=(option_record(ownership="unknown-owner"),)
            ),
            "unsupported option ownership projection",
        ),
        (
            "asymmetric option conflict",
            exported_schema(
                options=(
                    option_record(
                        identity=0,
                        token="--left",
                        conflict_rule="mutually-exclusive",
                        conflicts="1",
                    ),
                    option_record(identity=1, token="--right"),
                )
            ),
            "asymmetric option conflict projection",
        ),
        (
            "unknown option conflict identity",
            exported_schema(
                options=(
                    option_record(
                        token="--left",
                        conflict_rule="mutually-exclusive",
                        conflicts="9",
                    ),
                )
            ),
            "references unknown conflict identity",
        ),
        (
            "unknown relation option",
            exported_schema(
                relations="1:optional:once:hidden:moguet-control:none:none"
            ),
            "references unknown option identity",
        ),
        (
            "duplicate relation option",
            exported_schema(
                relations=(
                    "0:optional:once:hidden:moguet-control:none:none,"
                    "0:optional:once:hidden:moguet-control:none:none"
                )
            ),
            "duplicate option relation projection",
        ),
        (
            "unknown relation requirement",
            exported_schema(
                relations="0:unknown:once:hidden:moguet-control:none:none"
            ),
            "unsupported option requirement projection",
        ),
        (
            "unknown relation occurrence",
            exported_schema(
                relations="0:optional:unknown:hidden:moguet-control:none:none"
            ),
            "unsupported relation occurrence projection",
        ),
        (
            "delegated relation occurrence in closed form",
            exported_schema(
                relations="0:optional:delegated:hidden:moguet-control:none:none"
            ),
            "delegated option occurrence in closed form",
        ),
        (
            "unknown public syntax",
            exported_schema(
                relations="0:optional:once:unknown:moguet-control:none:none"
            ),
            "unsupported public syntax projection",
        ),
        (
            "unknown semantic effect",
            exported_schema(
                relations="0:optional:once:hidden:unknown:none:none"
            ),
            "unsupported semantic effect projection",
        ),
        (
            "unknown forwarding target",
            exported_schema(
                relations="0:optional:once:hidden:moguet-control:unknown:none"
            ),
            "unsupported forwarding target projection",
        ),
        (
            "unknown forwarding occurrence",
            exported_schema(
                relations="0:optional:once:hidden:moguet-control:none:unknown"
            ),
            "unsupported forwarding occurrence projection",
        ),
        (
            "inconsistent forwarding occurrence",
            exported_schema(
                relations="0:optional:once:hidden:upstream-argument:pacman:none"
            ),
            "inconsistent option forwarding projection",
        ),
        (
            "public syntax missing from rendered form",
            exported_schema(
                relations="0:required:once:required:moguet-control:none:none"
            ),
            "public option syntax is absent from form projection",
        ),
        (
            "canonical grammar mismatch",
            exported_schema(canonical="fixture changed"),
            "canonical grammar differs from closed form projection",
        ),
        (
            "duplicate closed grammar form",
            duplicate_first_record(exported_schema(), "FORM"),
            "duplicate closed grammar forms",
        ),
        (
            "open grammar without delegated projection",
            without_record(exported_schema(open_grammar=True), "DELEGATED"),
            "open grammar has no delegated operand projection",
        ),
        (
            "syntax-only option without public form",
            exported_schema(
                options=(
                    option_record(
                        token="--local",
                        placement="operation-local",
                        semantic_scopes="local-source-build",
                        definition_role="syntax-only",
                    ),
                )
            ),
            "syntax-only option has no public grammar form",
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
