#!/usr/bin/env python3

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import stat
import subprocess
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
CURRENT_COMMAND = "moguet"
LEGACY_NAME = "j" + "packer"
LEGACY_TITLE = LEGACY_NAME.capitalize()
LEGACY_UPPER = LEGACY_NAME.upper()
FORMER_CANDIDATE = "pac" + "tune"
REJECTED_ROMANIZATION = "MUG" + "UET"
REJECTED_JAPANESE_NAME = "\u30df\u30e5\u30b2"


@dataclass(frozen=True)
class Finding:
    check: str
    path: str
    line_number: int
    line: str


@dataclass(frozen=True)
class LegacyAllowance:
    category: str
    pattern: re.Pattern[str]


def fail(message: str) -> None:
    print(f"internal-identity-audit: {message}", file=sys.stderr)
    raise SystemExit(1)


def repository_paths() -> list[str]:
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        cwd=REPOSITORY_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        fail(
            "unable to enumerate repository files: "
            + result.stderr.decode("utf-8", errors="replace").strip()
        )

    paths: list[str] = []
    for raw_path in result.stdout.split(b"\0"):
        if not raw_path:
            continue
        path = raw_path.decode("utf-8")
        absolute_path = REPOSITORY_ROOT / path
        if absolute_path.is_file():
            paths.append(path)
    return sorted(set(paths))


def read_text(path: str) -> str | None:
    data = (REPOSITORY_ROOT / path).read_bytes()
    if b"\0" in data:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def is_active_identity_path(path: str) -> bool:
    return (
        path in {".gitignore", "Makefile"}
        or path.startswith("src/")
        or path.startswith("tests/")
        or path.startswith("scripts/")
    )


DEFERRED_GENERAL_DOCUMENTS = {
    "AGENTS.md",
    "CLAUDE.md",
    "CONTRIBUTING.md",
    "README.md",
    "docs/CODING_CONVENTIONS.md",
    "docs/COMPATIBILITY.md",
    "docs/DECISIONS.md",
    "docs/DEVELOPMENT.md",
    "docs/LICENSING.md",
    "docs/PROJECT_STANCE.md",
    "docs/VERSIONING.md",
}
HISTORICAL_DOCUMENTS = {
    "docs/audit/v1.8.0-claude-code.md",
    "docs/audit/v1.8.0-codex.md",
}
DEFERRED_MAN_AND_COMPLETION = {
    f"completions/_{LEGACY_NAME}",
    f"completions/{LEGACY_NAME}.fish",
    f"completions/{LEGACY_NAME}_completion.bash",
    f"man/{LEGACY_NAME}.8",
    f"man/{LEGACY_NAME}.8.in",
}
DEFERRED_PACKAGING_AND_LICENSE = {
    "PKGBUILD",
    "THIRD_PARTY_NOTICES.md",
    f"config/{LEGACY_NAME}.conf",
    f"LICENSES/{LEGACY_NAME}-MIT-legacy.txt",
}
FORMER_CANDIDATE_DOCUMENTS = {
    "docs/DECISIONS.md",
    "docs/PROJECT_STANCE.md",
    "docs/VERSIONING.md",
}


def allowances(category: str, *patterns: str) -> tuple[LegacyAllowance, ...]:
    return tuple(
        LegacyAllowance(category, re.compile(pattern)) for pattern in patterns
    )


legacy = re.escape(LEGACY_NAME)
identity_start = r"(?<![A-Za-z0-9_.-])"
identity_end = r"(?=$|/|[\"'\s,;:#)=}\]]|\.(?=[\"'\s]|$))"
config_filename = rf"{identity_start}{legacy}\.conf{identity_end}"
log_filename = rf"{identity_start}{legacy}\.log{identity_end}"
xdg_cache_component = rf"\$XDG_CACHE_HOME/{legacy}{identity_end}"
case_cache_component = rf"\$case_dir/xdg-cache/{legacy}{identity_end}"
legacy_cache_phrase = (
    rf"legacy {legacy}(?: cache(?: root| symlink)?| path component|\.log file|"
    rf" package directories){identity_end}"
)


ACTIVE_LEGACY_ALLOWANCES: dict[str, tuple[LegacyAllowance, ...]] = {
    ".gitignore": allowances(
        "production-artifact-cleanup",
        rf"^\s*/{legacy}\s*$",
    ),
    "Makefile": allowances(
        "deferred-production-packaging",
        rf"^\s*LEGACY_PRODUCTION_TARGET\s*:=\s*{legacy}\s*$",
        rf"^\s*PACKAGE_NAME\s*:=\s*{legacy}\s*$",
        rf"man/{legacy}\.8(?:\.in)?{identity_end}",
        rf"config/{legacy}\.conf{identity_end}",
        rf"completions/{legacy}(?:_completion\.bash|\.fish)?{identity_end}",
        rf"completions/_{legacy}{identity_end}",
        rf"LICENSES/{legacy}-MIT-legacy\.txt{identity_end}",
        rf"SYSCONFDIR\)/{legacy}{identity_end}",
        rf"COMPDIR\)/{legacy}{identity_end}",
        rf"ZSHCOMPDIR\)/_{legacy}{identity_end}",
        rf"FISHCOMPDIR\)/{legacy}\.fish{identity_end}",
        rf"MANDIR\)/{legacy}\.8{identity_end}",
        rf"LICENSEDIR\)/{legacy}-MIT-legacy\.txt{identity_end}",
        config_filename,
    ),
    "scripts/check-release-version.sh": allowances(
        "deferred-release-metadata-check",
        rf"man/{legacy}\.8{identity_end}",
        rf"{identity_start}{legacy} \$version",
    ),
    "scripts/check-packaging-metadata.sh": allowances(
        "deferred-release-metadata-check",
        rf"etc/{legacy}/{legacy}\.conf{identity_end}",
        rf"config/{legacy}\.conf{identity_end}",
        rf"{identity_start}{legacy}-src{identity_end}",
        config_filename,
    ),
    "scripts/check-license-compliance.sh": allowances(
        "deferred-release-metadata-check",
        rf"{identity_start}{legacy}-src{identity_end}",
        rf"seekerkrt/{legacy}(?:\.git)?{identity_end}",
        rf"{identity_start}{legacy}-MIT-legacy\.txt{identity_end}",
        rf"{identity_start}{legacy} v1\.14\.0",
        rf"v1\.15\.0[^\n]*{identity_start}{legacy}{identity_end}",
        rf"v1\.15\.0以降の{legacy}(?=は)",
        rf"GPL-licensed {legacy} development series",
        rf"{identity_start}{legacy} releases",
        rf"current\( {legacy}\)\?",
        rf"linked into {legacy}{identity_end}",
        rf"bundled with {legacy}{identity_end}",
    ),
    ".github/workflows/mirror-gitlab-release.yml": allowances(
        "deferred-repository-automation",
        rf"seekerkrt%2F{legacy}{identity_end}",
    ),
    ".github/workflows/mirror-gitlab.yml": allowances(
        "deferred-repository-automation",
        rf"seekerkrt/{legacy}\.git{identity_end}",
    ),
    ".gitlab/mirror-github-release.sh": allowances(
        "deferred-repository-automation",
        rf"seekerkrt/{legacy}{identity_end}",
    ),
    "src/app_config.cpp": allowances(
        "storage-path",
        rf"/etc/{legacy}/{legacy}\.conf{identity_end}",
    ),
    "src/app_config.hpp": allowances(
        "storage-path",
        rf"legacy {legacy}\.conf{identity_end}",
    ),
    "src/moguet.cpp": allowances(
        "storage-path",
        rf"legacy {legacy}\.conf{identity_end}",
    ),
    "src/source_preference.cpp": allowances(
        "storage-path",
        rf"/etc/{legacy}/package\.build",
    ),
    "tests/test-help-man-completion.sh": allowances(
        "deferred-artifact-contract-test",
        rf"man/{legacy}\.8(?:\.in)?{identity_end}",
        rf"completions/{legacy}_completion\.bash{identity_end}",
        rf"{identity_start}_{legacy}_global_options{identity_end}",
        rf"completions/_{legacy}{identity_end}",
        rf"{identity_start}{legacy}_global_options{identity_end}",
        rf"completions/{legacy}\.fish{identity_end}",
    ),
    "tests/test-upgrade-all-completion.sh": allowances(
        "deferred-artifact-contract-test",
        rf"completions/{legacy}_completion\.bash{identity_end}",
        rf"{identity_start}_{legacy}{identity_end}",
        rf"run_completion\s+{legacy}(?=\s)",
    ),
    "tests/test-install-layout.sh": allowances(
        "deferred-artifact-contract-test",
        rf"/usr/bin/{legacy}{identity_end}",
        rf"/etc/{legacy}{identity_end}",
        rf"/completions/{legacy}{identity_end}",
        rf"completions/{legacy}_completion\.bash{identity_end}",
        rf"/_{legacy}{identity_end}",
        rf"/{legacy}\.fish{identity_end}",
        rf"/{legacy}\.8{identity_end}",
        rf"/licenses/{legacy}{identity_end}",
        rf"/doc/{legacy}{identity_end}",
        rf"{identity_start}{legacy}-MIT-legacy\.txt{identity_end}",
        rf"config/{legacy}\.conf{identity_end}",
        config_filename,
        legacy_cache_phrase,
    ),
    "tests/trusted_cache_test.cpp": (
        allowances(
            "legacy-cache-preservation-fixture",
            rf'cache_home / "{legacy}"',
            rf'legacy_root / "{legacy}\.log"',
            rf'create_symlink\("{legacy}\.log"',
            rf'Legacy {legacy} cache tree changed\.',
        )
        + allowances(
            "new-cache-ordinary-legacy-name-fixture",
            rf'root\.path\(\) / "{legacy}\.log"',
        )
    ),
    "tests/test-commands-sync.sh": allowances(
        "storage-fixture",
        config_filename,
        log_filename,
        xdg_cache_component,
    ),
    "tests/test-pkgbuild-export.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
        legacy_cache_phrase,
    ),
    "tests/test-needed-contract.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
        legacy_cache_phrase,
    ),
    "tests/test-aur-rpc-validation.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
    ),
    "tests/test-source-build.sh": allowances(
        "storage-fixture",
        config_filename,
        case_cache_component,
    ),
    "tests/test-source-selection.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
        legacy_cache_phrase,
    ),
    "tests/test-app-config.sh": allowances(
        "storage-fixture",
        config_filename,
        log_filename,
        xdg_cache_component,
    ),
    "tests/test-upgrade-all-command.sh": allowances(
        "storage-fixture",
        config_filename,
        xdg_cache_component,
    ),
    "tests/test-cli-parser.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
    ),
    "tests/test-build-cache-symlink.sh": allowances(
        "storage-fixture",
        xdg_cache_component,
        rf"\$ancestor_target/{legacy}(?=/)",
        rf"{identity_start}{legacy}-escape{identity_end}",
        log_filename,
        legacy_cache_phrase,
    ),
    "tests/test-commands-source-maintenance.sh": allowances(
        "storage-fixture",
        config_filename,
        log_filename,
        case_cache_component,
    ),
    "tests/test-aur-update-command.sh": allowances(
        "storage-fixture",
        config_filename,
        xdg_cache_component,
        legacy_cache_phrase,
    ),
    "tests/stubs/source-maintenance/sudo": allowances(
        "storage-fixture",
        xdg_cache_component,
    ),
    "tests/stubs/commands-sync/sudo": allowances(
        "storage-fixture",
        xdg_cache_component,
    ),
    "tests/test-runtime-identity.sh": (
        allowances(
            "storage-fixture",
            config_filename,
            log_filename,
            rf"\$root_case_dir/xdg-cache/{legacy}(?=/|[\"']|\s|$)",
            rf"\$startup_case_dir/xdg-cache/{legacy}(?=/|[\"']|\s|$)",
            rf"\$fallback_case_dir/xdg-cache/{legacy}(?=/|[\"']|\s|$)",
        )
        + allowances(
            "negative-runtime-assertion",
            rf"grep -Fi -- '{legacy}'",
            rf"unintended {legacy} project identity",
            rf"assert_not_contains \"{legacy}\"",
            rf"assert_not_contains \"Started {legacy}\"",
        )
    ),
}


def classify_legacy_path(path: str) -> str | None:
    if path in DEFERRED_MAN_AND_COMPLETION:
        return "deferred-man-completion"
    if path in DEFERRED_PACKAGING_AND_LICENSE:
        return "deferred-packaging-license"
    return None


def classify_legacy_reference(
    path: str, line: str, match_start: int, match_end: int
) -> str | None:
    if path in HISTORICAL_DOCUMENTS:
        return "historical-audit-record"
    if path in DEFERRED_GENERAL_DOCUMENTS:
        return "deferred-general-documentation"
    if path in DEFERRED_MAN_AND_COMPLETION:
        return "deferred-man-completion"
    if path in DEFERRED_PACKAGING_AND_LICENSE:
        return "deferred-packaging-license"

    for allowance in ACTIVE_LEGACY_ALLOWANCES.get(path, ()):
        for context_match in allowance.pattern.finditer(line):
            if (
                context_match.start() <= match_start
                and match_end <= context_match.end()
            ):
                return allowance.category

    return None


def finding_for_line(check: str, path: str, line_number: int, line: str) -> Finding:
    return Finding(check, path, line_number, line.strip())


def legacy_categories_for_line(path: str, line: str) -> list[str | None]:
    pattern = re.compile(re.escape(LEGACY_NAME), re.IGNORECASE)
    return [
        classify_legacy_reference(path, line, match.start(), match.end())
        for match in pattern.finditer(line)
    ]


NON_HOOK_UPPERCASE_TOKENS = {
    "SIGNAL_TEST_RETRY_INTERVAL_MS",
    "SIGNAL_TEST_TIMEOUT",
}
TEST_INFRASTRUCTURE_TOKEN = re.compile(
    r"_TEST_(?:TARGET|SRCS|SUPPORT_SRCS)$"
)


def extract_test_hook_tokens(text: str) -> set[str]:
    normalized_text = re.sub(r"-D(?=[A-Z])", "", text)
    uppercase_tokens = re.findall(r"\b[A-Z][A-Z0-9_]+\b", normalized_text)
    tokens: set[str] = set()
    for token in uppercase_tokens:
        if "_ENABLE_" not in token and "_TEST_" not in token:
            continue
        if token in NON_HOOK_UPPERCASE_TOKENS:
            continue
        if TEST_INFRASTRUCTURE_TOKEN.search(token):
            continue
        tokens.add(token)
    return tokens


def check_classifier_contract() -> None:
    allowed_categories = legacy_categories_for_line(
        "src/moguet.cpp", f'legacy {LEGACY_NAME}.conf: LOGFILE=...'
    )
    if allowed_categories != ["storage-path"]:
        fail("internal legacy-storage classifier self-test failed")

    rejected_categories = legacy_categories_for_line(
        "src/moguet.cpp", f'char program[] = "{LEGACY_NAME}";'
    )
    if rejected_categories != [None]:
        fail("internal active-identity classifier is too broad")

    active_cache_categories = legacy_categories_for_line(
        "src/trusted_cache.cpp",
        f'return base / "{LEGACY_NAME}";',
    )
    if active_cache_categories != [None]:
        fail("internal active cache classifier is too broad")

    artifact_categories = legacy_categories_for_line(
        "tests/test-install-layout.sh",
        f"active {LEGACY_NAME} application label",
    )
    if artifact_categories != [None]:
        fail("internal artifact-test classifier is too broad")

    test_path_categories = legacy_categories_for_line(
        "tests/example.sh",
        f'runner="$tmp_dir/{LEGACY_NAME}-helper"',
    )
    if test_path_categories != [None]:
        fail("internal test-storage classifier is too broad")

    if classify_legacy_path(f"tests/{LEGACY_NAME}-cli-stub") is not None:
        fail("internal legacy-path classifier is too broad")

    rejected_allowance_cases = (
        (
            "tests/test-install-layout.sh",
            f'test -e "$DESTDIR/usr/bin/{LEGACY_NAME}-helper"',
        ),
        (
            "tests/test-app-config.sh",
            f'runner="$tmp_dir/{LEGACY_NAME}.conf-helper"',
        ),
        (
            "tests/test-app-config.sh",
            f'runner="$tmp_dir/{LEGACY_NAME}.log-helper"',
        ),
        (
            "tests/test-runtime-identity.sh",
            f'assert_not_contains "Started {LEGACY_NAME}-helper" "$output"',
        ),
        (
            "tests/test-runtime-identity.sh",
            f'assert_contains "Started {LEGACY_NAME}" "$output"',
        ),
        (
            "scripts/check-license-compliance.sh",
            f"v1.15.0 or later {LEGACY_NAME}-helper releases",
        ),
        (
            "scripts/check-license-compliance.sh",
            f"linked into {LEGACY_NAME}-helper",
        ),
        (
            "scripts/check-license-compliance.sh",
            f"bundled with {LEGACY_NAME}-helper",
        ),
        (
            "scripts/check-release-version.sh",
            f'expected="x{LEGACY_NAME} $version"',
        ),
        (
            "scripts/check-license-compliance.sh",
            f"x{LEGACY_NAME}-src",
        ),
        (
            "tests/test-help-man-completion.sh",
            f"x{LEGACY_NAME}_global_options",
        ),
        (
            "tests/test-install-layout.sh",
            f"x{LEGACY_NAME}-MIT-legacy.txt",
        ),
    )
    for path, line in rejected_allowance_cases:
        if legacy_categories_for_line(path, line) != [None]:
            fail("internal active legacy classifier is too broad")

    deferred_categories = legacy_categories_for_line(
        "README.md", f"{LEGACY_NAME} command"
    )
    if deferred_categories != ["deferred-general-documentation"]:
        fail("internal deferred-document classifier self-test failed")

    wrong_hooks = (
        "OTHER" + "_TEST_FAKE",
        "MOGUET_INTERNAL" + "_TEST_BAD",
        "OTHER_NAMESPACE" + "_ENABLE_FAKE",
    )
    for wrong_hook in wrong_hooks:
        if wrong_hook not in extract_test_hook_tokens(wrong_hook):
            fail("internal test-hook classifier self-test failed")


def main() -> int:
    check_classifier_contract()
    paths = repository_paths()
    texts: dict[str, str] = {}
    for path in paths:
        text = read_text(path)
        if text is not None:
            texts[path] = text

    findings: list[Finding] = []
    category_counts: Counter[str] = Counter()
    legacy_pattern = re.compile(re.escape(LEGACY_NAME), re.IGNORECASE)

    for path, text in texts.items():
        if legacy_pattern.search(path):
            category = classify_legacy_path(path)
            if category is None:
                findings.append(Finding("unclassified-legacy-path", path, 0, path))
            else:
                category_counts[category] += 1

        for line_number, line in enumerate(text.splitlines(), start=1):
            matches = list(legacy_pattern.finditer(line))
            if not matches:
                continue
            for match in matches:
                category = classify_legacy_reference(
                    path, line, match.start(), match.end()
                )
                if category is None:
                    findings.append(
                        finding_for_line(
                            "unclassified-legacy-reference", path, line_number, line
                        )
                    )
                else:
                    category_counts[category] += 1

    active_texts = {
        path: text for path, text in texts.items() if is_active_identity_path(path)
    }
    old_hook_pattern = re.compile(
        rf"\b{re.escape(LEGACY_UPPER)}_(?:ENABLE|TEST)_[A-Z0-9_]+\b"
    )
    removed_symbols = (
        f"{LEGACY_TITLE}GlobalOption",
        f"{LEGACY_UPPER}_GLOBAL_OPTIONS",
        f"{LEGACY_NAME}_global_option_kind",
        f"is_{LEGACY_NAME}_global_option",
        f"apply_{LEGACY_NAME}_global_option",
        f"{LEGACY_UPPER}_HAS_EXTRACTED_SOURCE_INSTALL",
        f"{LEGACY_NAME}_program_main",
    )
    removed_test_identity_pattern = re.compile(
        rf"(?:build/tests/{re.escape(LEGACY_NAME)}-|"
        rf"{re.escape(LEGACY_NAME)}-test|"
        rf"{re.escape(LEGACY_NAME)}-(?:config-test|upgrade-metadata-test|"
        rf"artifact|aur-update|edit-src|inspect|issue|multiple|package|"
        rf"pkgbuild|process|production|repository|separated|preserved|options))"
    )

    for path, text in active_texts.items():
        for line_number, line in enumerate(text.splitlines(), start=1):
            if old_hook_pattern.search(line):
                findings.append(
                    finding_for_line("old-test-hook-prefix", path, line_number, line)
                )
            if any(symbol in line for symbol in removed_symbols):
                findings.append(
                    finding_for_line("removed-internal-symbol", path, line_number, line)
                )
            if removed_test_identity_pattern.search(line):
                findings.append(
                    finding_for_line("old-active-test-identity", path, line_number, line)
                )

        if removed_test_identity_pattern.search(path):
            findings.append(Finding("old-active-test-path", path, 0, path))

    rejected_names = (
        ("rejected-romanization", REJECTED_ROMANIZATION, None),
        ("rejected-japanese-name", REJECTED_JAPANESE_NAME, None),
        ("former-name-candidate", FORMER_CANDIDATE, FORMER_CANDIDATE_DOCUMENTS),
    )
    deferred_candidate_count = 0
    for check, rejected_name, allowed_paths in rejected_names:
        pattern = re.compile(re.escape(rejected_name), re.IGNORECASE)
        for path, text in texts.items():
            if pattern.search(path):
                if allowed_paths is not None and path in allowed_paths:
                    deferred_candidate_count += 1
                else:
                    findings.append(Finding(check + "-path", path, 0, path))
            for line_number, line in enumerate(text.splitlines(), start=1):
                matches = list(pattern.finditer(line))
                if not matches:
                    continue
                if allowed_paths is not None and path in allowed_paths:
                    deferred_candidate_count += len(matches)
                    continue
                findings.append(finding_for_line(check, path, line_number, line))

    identity_header = texts.get("src/application_identity.hpp", "")
    command_match = re.search(r'COMMAND_NAME\s*=\s*"([^"]+)"', identity_header)
    prefix_match = re.search(
        r'ENVIRONMENT_PREFIX\s*=\s*"([A-Z0-9_]+)"', identity_header
    )
    if command_match is None or prefix_match is None:
        findings.append(
            Finding(
                "missing-application-identity-authority",
                "src/application_identity.hpp",
                0,
                "COMMAND_NAME or ENVIRONMENT_PREFIX is missing",
            )
        )
        environment_prefix = ""
    else:
        command_name = command_match.group(1)
        environment_prefix = prefix_match.group(1)
        expected_prefix = command_name.upper() + "_"
        if command_name != CURRENT_COMMAND or environment_prefix != expected_prefix:
            findings.append(
                Finding(
                    "application-identity-prefix-mismatch",
                    "src/application_identity.hpp",
                    0,
                    f"command={command_name}, prefix={environment_prefix}, "
                    f"expected={CURRENT_COMMAND}/{expected_prefix}",
                )
            )

    hook_tokens: set[str] = set()
    for path, text in active_texts.items():
        for line_number, line in enumerate(text.splitlines(), start=1):
            line_hook_tokens = extract_test_hook_tokens(line)
            hook_tokens.update(line_hook_tokens)
            for hook_token in line_hook_tokens:
                valid_prefix = environment_prefix and (
                    hook_token.startswith(environment_prefix + "ENABLE_")
                    or hook_token.startswith(environment_prefix + "TEST_")
                )
                if not valid_prefix:
                    findings.append(
                        finding_for_line(
                            "test-hook-prefix-mismatch",
                            path,
                            line_number,
                            line,
                        )
                    )
    if not hook_tokens:
        findings.append(
            Finding(
                "missing-test-hook-contract",
                "<active-source-build-tests>",
                0,
                "no ENABLE/TEST hook tokens were found",
            )
        )

    makefile = texts.get("Makefile", "")
    legacy_target_lines = [
        line.strip()
        for line in makefile.splitlines()
        if "LEGACY_PRODUCTION_TARGET" in line
    ]
    expected_legacy_target_lines = [
        f"LEGACY_PRODUCTION_TARGET := {LEGACY_NAME}",
        "rm -f $(TARGET) $(LEGACY_PRODUCTION_TARGET)",
    ]
    if legacy_target_lines != expected_legacy_target_lines:
        findings.append(
            Finding(
                "unsafe-legacy-production-target-consumer",
                "Makefile",
                0,
                "LEGACY_PRODUCTION_TARGET must only remove a stale artifact in clean",
            )
        )

    required_test_targets = {
        "TEST_TARGET": "moguet-test",
        "ROOT_EXECUTION_IDENTITY_TEST_TARGET": "moguet-root-execution-identity-test",
        "COMMANDS_INSPECT_TEST_TARGET": "moguet-commands-inspect-test",
        "AUR_UPDATE_COMMAND_TEST_TARGET": "moguet-aur-update-command-test",
        "UPGRADE_ALL_COMMAND_TEST_TARGET": "moguet-upgrade-all-command-test",
        "AUR_RPC_VALIDATION_TEST_TARGET": "moguet-aur-rpc-validation-test",
        "COMMANDS_SYNC_TEST_TARGET": "moguet-commands-sync-test",
        "SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET": "moguet-source-install-characterization-test",
        "APP_CONFIG_INTEGRATION_TEST_TARGET": "moguet-app-config-test",
        "UPGRADE_BASELINE_METADATA_TEST_TARGET": "moguet-upgrade-baseline-metadata-test",
    }
    for variable, basename in required_test_targets.items():
        assignment_pattern = re.compile(
            rf"^{re.escape(variable)}\s*:?=.*"
            rf"(?:build|\$\(BUILD_DIR\))/tests/{re.escape(basename)}$",
            re.MULTILINE,
        )
        if assignment_pattern.search(makefile) is None:
            findings.append(
                Finding(
                    "missing-moguet-test-target",
                    "Makefile",
                    0,
                    f"{variable} -> build/tests/{basename}",
                )
            )

    required_internal_symbols = (
        "MoguetGlobalOption",
        "MOGUET_GLOBAL_OPTIONS",
        "moguet_global_option_kind",
        "apply_moguet_global_option",
        "is_moguet_global_option",
    )
    parser_text = texts.get("src/cli_parser.cpp", "") + texts.get(
        "src/cli_parser.hpp", ""
    )
    for symbol in required_internal_symbols:
        if symbol not in parser_text:
            findings.append(
                Finding("missing-moguet-internal-symbol", "src/cli_parser.cpp", 0, symbol)
            )

    moguet_source = texts.get("src/moguet.cpp", "")
    if 'char program[] = "moguet";' not in moguet_source:
        findings.append(
            Finding(
                "test-argv-zero-identity",
                "src/moguet.cpp",
                0,
                "test argv[0] must be moguet",
            )
        )
    if "Clean Moguet build cache" not in texts.get(
        "src/commands_source_maintenance.cpp", ""
    ):
        findings.append(
            Finding(
                "moguet-cache-presentation",
                "src/commands_source_maintenance.cpp",
                0,
                "Moguet cache prompt is missing",
            )
        )

    required_stubs = (
        "tests/stubs/moguet-test-editor",
        "tests/stubs/source-maintenance/moguet-test-editor",
    )
    for stub_path in required_stubs:
        absolute_path = REPOSITORY_ROOT / stub_path
        try:
            mode = absolute_path.lstat().st_mode
        except FileNotFoundError:
            findings.append(Finding("missing-moguet-test-stub", stub_path, 0, stub_path))
            continue
        if absolute_path.is_symlink() or not stat.S_ISREG(mode) or not mode & stat.S_IXUSR:
            findings.append(
                Finding(
                    "unsafe-moguet-test-stub",
                    stub_path,
                    0,
                    "stub must be a regular executable file",
                )
            )

    if findings:
        for finding in sorted(
            set(findings),
            key=lambda item: (item.path, item.line_number, item.check, item.line),
        ):
            location = (
                f"{finding.path}:{finding.line_number}"
                if finding.line_number > 0
                else finding.path
            )
            print(
                f"internal-identity-audit: {finding.check}: "
                f"{location}: {finding.line}",
                file=sys.stderr,
            )
        print(
            f"internal-identity-audit: failed with {len(set(findings))} finding(s)",
            file=sys.stderr,
        )
        return 1

    print(
        "internal-identity-audit: ok: "
        f"{len(hook_tokens)} test hook names use {environment_prefix}"
    )
    for category, count in sorted(category_counts.items()):
        print(f"internal-identity-audit: legacy {category}: {count}")
    print(
        "internal-identity-audit: legacy deferred-former-candidate-documentation: "
        f"{deferred_candidate_count}"
    )
    print("internal-identity-audit: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
