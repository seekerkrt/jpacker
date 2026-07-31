#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"

tmp_dir=$(mktemp -d)
case_count=0

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

export PATH=$repo_root/tests/stubs:/usr/bin:/bin
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"
require_exact_test_command vercmp "$repo_root/tests/stubs/vercmp"

setup_case() {
    case_name=$1
    scenario_name=$2
    case_dir=$tmp_dir/cases/$case_name
    stdout_file=$case_dir/stdout
    stderr_file=$case_dir/stderr
    command_log=$case_dir/events.log
    config_file=$case_dir/config.toml
    foreign_inventory_file=$case_dir/foreign-packages

    mkdir -p \
        "$case_dir/home" "$case_dir/xdg-state" "$case_dir/xdg-cache" \
        "$case_dir/work" \
        "$case_dir/package.build"
    : > "$stdout_file"
    : > "$stderr_file"
    : > "$command_log"
    printf '%s\n' 'schema_version = 1' > "$config_file"
    : > "$foreign_inventory_file"

    export HOME=$case_dir/home
    export XDG_STATE_HOME=$case_dir/xdg-state
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export MOGUET_TEST_CONFIG_FILE=$config_file
    export MOGUET_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export MOGUET_TEST_COMMAND_LOG=$command_log
    export MOGUET_TEST_PACKAGE_METADATA_EVENT_LOG=$command_log
    export MOGUET_TEST_FOREIGN_PACKAGE_INVENTORY_STATE_FILE=$foreign_inventory_file
    export MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST=core
    export MOGUET_TEST_UPGRADE_ALL_SCENARIO=$scenario_name
    export MOGUET_TEST_AUR_UPDATE_SCENARIO=no-installed-foreign
    export MOGUET_TEST_PACMAN_EXIT_CODE=91
    export MOGUET_TEST_SUDO_EXIT_CODE=92
    case_count=$((case_count + 1))
}

show_case_diagnostics() {
    echo "--- stdout ---" >&2
    sed -n '1,260p' "$stdout_file" >&2
    echo "--- stderr ---" >&2
    sed -n '1,260p' "$stderr_file" >&2
    echo "--- event log ---" >&2
    sed -n '1,320p' "$command_log" >&2
}

fail_case() {
    echo "$1" >&2
    show_case_diagnostics
    exit 1
}

run_status() {
    expected_status=$1
    shift
    actual_status=0
    (cd "$case_dir/work" && "$test_binary" "$@") \
        > "$stdout_file" 2> "$stderr_file" || actual_status=$?
    if [ "$actual_status" -ne "$expected_status" ]; then
        fail_case "unexpected status $actual_status (expected $expected_status): $*"
    fi
}

assert_exact_line() {
    expected=$1
    file=$2
    if ! grep -Fx -- "$expected" "$file" >/dev/null; then
        fail_case "missing exact line: $expected"
    fi
}

assert_contains() {
    expected=$1
    file=$2
    if ! grep -F -- "$expected" "$file" >/dev/null; then
        fail_case "missing expected text: $expected"
    fi
}

assert_not_contains() {
    unexpected=$1
    file=$2
    if grep -F -- "$unexpected" "$file" >/dev/null; then
        fail_case "unexpected text: $unexpected"
    fi
}

assert_not_exact_line() {
    unexpected=$1
    file=$2
    if grep -Fx -- "$unexpected" "$file" >/dev/null; then
        fail_case "unexpected exact line: $unexpected"
    fi
}

assert_occurrence_count() {
    expected_count=$1
    expected=$2
    file=$3
    actual_count=$(grep -Fc -- "$expected" "$file" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        fail_case "occurrence count for '$expected': $actual_count (expected $expected_count)"
    fi
}

assert_line_before() {
    first=$1
    second=$2
    file=$3
    first_line=$(grep -nFx -- "$first" "$file" | sed -n '1s/:.*//p')
    second_line=$(grep -nFx -- "$second" "$file" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] ||
       [ "$first_line" -ge "$second_line" ]; then
        fail_case "expected '$first' before '$second'"
    fi
}

assert_event_count() {
    expected_count=$1
    expected_event=$2
    actual_count=$(grep -Fxc -- "$expected_event" "$command_log" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        fail_case "event count for '$expected_event': $actual_count (expected $expected_count)"
    fi
}

assert_cache_absent() {
    if [ -e "$XDG_CACHE_HOME/moguet" ]; then
        fail_case "invalid upgrade-all invocation initialized the cache"
    fi
}

assert_event_log_empty() {
    if [ -s "$command_log" ]; then
        fail_case "invalid upgrade-all invocation performed observable work"
    fi
}

assert_aggregate_absent() {
    if grep -E '^upgrade-all (prepare|execute)( |$)' "$command_log" >/dev/null; then
        fail_case "unrelated route entered the aggregate operation"
    fi
}

assert_no_external_mutation() {
    if grep -E '^(git|makepkg|pacman|sudo|external)( |$)' \
        "$command_log" >/dev/null; then
        fail_case "aggregate command fixture reached an external mutation"
    fi
}

assert_validation_rejected_without_work() {
    expected_diagnostic=$1
    assert_contains "$expected_diagnostic" "$stderr_file"
    assert_event_log_empty
    assert_cache_absent
}

run_matrix_case() {
    matrix_kind=$1
    matrix_index=$2
    expected_matrix_status=$3
    expected_stdout_fragment=$4
    expected_stderr_fragment=$5

    setup_case \
        "matrix-$matrix_kind-$matrix_index" \
        aur-presentation-matrix
    export MOGUET_TEST_UPGRADE_ALL_MATRIX_KIND=$matrix_kind
    export MOGUET_TEST_UPGRADE_ALL_MATRIX_INDEX=$matrix_index
    run_status "$expected_matrix_status" upgrade-all

    if [ "$expected_stdout_fragment" != "-" ]; then
        assert_contains "$expected_stdout_fragment" "$stdout_file"
        assert_not_contains "$expected_stdout_fragment" "$stderr_file"
    fi
    if [ "$expected_stderr_fragment" != "-" ]; then
        assert_contains "$expected_stderr_fragment" "$stderr_file"
        assert_not_contains "$expected_stderr_fragment" "$stdout_file"
    elif [ -s "$stderr_file" ]; then
        fail_case "matrix case unexpectedly wrote to stderr"
    fi
}

run_matrix_table() {
    matrix_table_kind=$1
    expected_matrix_count=$2
    matrix_table_index=0
    while IFS='|' read -r \
        expected_matrix_status \
        expected_stdout_fragment \
        expected_stderr_fragment; do
        if [ -z "$expected_matrix_status" ]; then
            continue
        fi
        run_matrix_case \
            "$matrix_table_kind" \
            "$matrix_table_index" \
            "$expected_matrix_status" \
            "$expected_stdout_fragment" \
            "$expected_stderr_fragment"
        matrix_table_index=$((matrix_table_index + 1))
    done
    if [ "$matrix_table_index" -ne "$expected_matrix_count" ]; then
        fail_case \
            "$matrix_table_kind matrix row count drifted: $matrix_table_index (expected $expected_matrix_count)"
    fi
}

default_snapshot='noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false'

# 1: exact custom route. Production parser/routing/presentation are linked;
# only the aggregate API is deterministic.
setup_case exact-route no-updates
run_status 0 upgrade-all
assert_exact_line "upgrade-all prepare $default_snapshot" "$command_log"
assert_exact_line "upgrade-all execute $default_snapshot" "$command_log"
assert_line_before \
    "upgrade-all prepare $default_snapshot" \
    "upgrade-all execute $default_snapshot" "$command_log"
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line "package state: no change" "$stdout_file"
assert_exact_line "upgrade-all completed: no updates" "$stdout_file"
assert_exact_line "no package state change" "$stdout_file"
assert_not_contains "pacman upgrade-all" "$command_log"
assert_no_external_mutation

# 2-5: existing route compatibility.
setup_case legacy-upgrade no-updates
export MOGUET_TEST_SUDO_EXIT_CODE=0
run_status 0 upgrade
assert_exact_line "sudo pacman -Syu" "$command_log"
assert_aggregate_absent

setup_case legacy-upgrade-aur no-updates
run_status 0 upgrade-aur
assert_exact_line "AUR update: no updates" "$stdout_file"
assert_aggregate_absent

setup_case generic-syu no-updates
export MOGUET_TEST_SUDO_EXIT_CODE=0
run_status 0 -Syu
assert_exact_line "sudo pacman -Syu" "$command_log"
assert_aggregate_absent

setup_case legacy-qua no-updates
run_status 0 -Qua
assert_contains "No foreign packages found." "$stdout_file"
assert_aggregate_absent

# 6-11: semantic misuse must stop before log/cache, metadata, preparation, or
# any package command. The shared event file remaining empty proves all four.
setup_case reject-target no-updates
run_status 1 upgrade-all unexpected-target
assert_validation_rejected_without_work \
    "upgrade-all does not accept target operands: unexpected-target"

setup_case reject-opaque no-updates
run_status 1 upgrade-all -- opaque-target
assert_validation_rejected_without_work \
    "upgrade-all does not accept opaque operands: opaque-target"

setup_case reject-rmdeps no-updates
run_status 1 upgrade-all --rmdeps
assert_validation_rejected_without_work \
    "Unsupported upgrade-all option: --rmdeps"

setup_case reject-needed no-updates
run_status 1 upgrade-all --needed
assert_validation_rejected_without_work \
    "Unsupported upgrade-all option or operand: --needed"

setup_case reject-aur no-updates
run_status 1 upgrade-all --aur
assert_validation_rejected_without_work \
    "Unsupported upgrade-all option: --aur"

setup_case reject-repo no-updates
run_status 1 upgrade-all --repo
assert_validation_rejected_without_work \
    "Unsupported upgrade-all option: --repo"

# 12-21: supported typed and invocation-only options are independently propagated to both aggregate
# boundaries without CLI-side reinterpretation.
setup_case option-edit no-updates
run_status 0 upgrade-all --edit
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"

setup_case option-noedit no-updates
run_status 0 upgrade-all --noedit
assert_event_count 1 \
    "upgrade-all prepare noedit=true nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=true nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"

setup_case option-diff no-updates
run_status 0 upgrade-all --diff
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"

setup_case option-nodiff no-updates
run_status 0 upgrade-all --nodiff
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=true noconfirm=false rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=true noconfirm=false rebuild=false cleanbuild=false rmdeps=false"

setup_case option-noconfirm no-updates
run_status 0 upgrade-all --noconfirm
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=true rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=true rebuild=false cleanbuild=false rmdeps=false"

setup_case option-build-mode-normal no-updates
run_status 0 upgrade-all --build-mode=normal
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=false rmdeps=false"

setup_case option-build-mode-rebuild no-updates
run_status 0 upgrade-all --build-mode=rebuild
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=true cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=true cleanbuild=false rmdeps=false"

setup_case option-rebuild no-updates
run_status 0 upgrade-all --rebuild
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=true cleanbuild=false rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=true cleanbuild=false rmdeps=false"

setup_case option-build-mode-clean no-updates
run_status 0 upgrade-all --build-mode=clean
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=true rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=true rmdeps=false"

setup_case option-cleanbuild no-updates
run_status 0 upgrade-all --cleanbuild
assert_event_count 1 \
    "upgrade-all prepare noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=true rmdeps=false"
assert_event_count 1 \
    "upgrade-all execute noedit=false nodiff=false noconfirm=false rebuild=false cleanbuild=true rmdeps=false"

# 22-27: success matrix and normal detailed presentation.
setup_case completed-changed completed-changed
run_status 0 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line "package state: changed" "$stdout_file"
assert_exact_line "registered source: source-updated: updated" "$stdout_file"
assert_exact_line "registered source: source-no-change: no change" "$stdout_file"
assert_exact_line "AUR target: aur-updated: updated" "$stdout_file"
assert_exact_line "AUR target: aur-no-change: no change" "$stdout_file"
assert_not_contains "PackageBase result:" "$stdout_file"
assert_contains "fixture registered source warning" "$stdout_file"
assert_contains "fixture AUR preparation warning" "$stdout_file"
assert_not_contains "fixture registered source warning" "$stderr_file"
assert_exact_line "upgrade-all completed" "$stdout_file"
assert_exact_line "package state changed" "$stdout_file"

setup_case aur-split-multiple aur-split-multiple
run_status 0 upgrade-all
assert_exact_line "registered source: source-updated: updated" "$stdout_file"
assert_exact_line "AUR target: aur-updated: updated" "$stdout_file"
assert_exact_line "AUR target: aur-split-main: updated" "$stdout_file"
assert_exact_line "PackageBase result: aur-split-suite" "$stdout_file"
assert_exact_line \
    "  required child: aur-split-main -> aur-split-main 7.0.1-2 (explicit): installed / updated" \
    "$stdout_file"
assert_exact_line \
    "  required child: aur-split-dependency -> aur-split-dependency 7.0.1-2 (dependency): skipped as needed / no change" \
    "$stdout_file"
assert_exact_line \
    "  produced artifact: aur-split-sibling 7.0.1-2 (not selected; not installed)" \
    "$stdout_file"
assert_exact_line \
    "  produced artifact: aur-split-suite-debug 7.0.1-2 (not selected; not installed)" \
    "$stdout_file"
assert_line_before \
    "  required child: aur-split-main -> aur-split-main 7.0.1-2 (explicit): installed / updated" \
    "  required child: aur-split-dependency -> aur-split-dependency 7.0.1-2 (dependency): skipped as needed / no change" \
    "$stdout_file"
assert_not_contains "required child: aur-split-sibling" "$stdout_file"
assert_not_contains "required child: aur-split-suite-debug" "$stdout_file"

setup_case completed-no-change completed-no-change
run_status 0 upgrade-all
assert_exact_line "package state: no change" "$stdout_file"
assert_exact_line "no package state change" "$stdout_file"

setup_case completed-unknown completed-unknown
run_status 0 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line "package state: unknown" "$stdout_file"
assert_exact_line "package state change unknown" "$stdout_file"
assert_not_contains "system: failed" "$stdout_file"

setup_case normal-aur-skips aur-skips
run_status 0 upgrade-all
assert_exact_line \
    "AUR target: aur-up-to-date: skipped: up to date" "$stdout_file"
assert_exact_line \
    "AUR target: non-aur-foreign: skipped: non-AUR foreign" "$stdout_file"

setup_case duplicate-exclusions duplicate-exclusions
run_status 0 upgrade-all
assert_exact_line \
    "excluded from AUR update: duplicate-by-name" "$stdout_file"
assert_exact_line \
    "reason: package name handled by explicit source preference" "$stdout_file"
assert_exact_line \
    "matched explicit source package: duplicate-by-name" "$stdout_file"
assert_exact_line \
    "excluded from AUR update: duplicate-by-base-child" "$stdout_file"
assert_exact_line \
    "reason: PackageBase handled by explicit source preference" "$stdout_file"
assert_exact_line "matched PackageBase: shared-base" "$stdout_file"
assert_exact_line "duplicate AUR targets excluded" "$stdout_file"

setup_case external-satisfaction external-satisfaction
run_status 0 upgrade-all
assert_exact_line \
    "AUR build unit externally satisfied: external-base" "$stdout_file"
assert_contains \
    "provided by explicit source preference: repository:https://sources.example/explicit-provider" \
    "$stdout_file"
assert_exact_line \
    "affected AUR root: aur-root (build dependency)" "$stdout_file"
assert_exact_line "AUR build units externally satisfied" "$stdout_file"

# 23-33: every aggregate failure status, source/AUR result category, phase
# NotAttempted reason, partial completion, and cleanup summary.
setup_case blocked-before-mutation blocked-before-mutation
run_status 1 upgrade-all
assert_event_count 1 "upgrade-all prepare $default_snapshot"
assert_event_count 0 "upgrade-all execute $default_snapshot"
assert_exact_line "system: not attempted" "$stdout_file"
assert_exact_line \
    "registered source: source-unsupported: unsupported: fixture unsupported source preference" \
    "$stdout_file"
assert_exact_line \
    "registered source: source-incomplete: incomplete: fixture source preparation failed" \
    "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: preparation blocked" "$stdout_file"
assert_exact_line "upgrade-all blocked before mutation" "$stdout_file"
assert_contains "fixture aggregate preparation blocked" "$stderr_file"

setup_case stopped-system stopped-on-system-failure
run_status 1 upgrade-all
assert_exact_line "system: failed" "$stdout_file"
assert_exact_line "package state: unknown" "$stdout_file"
assert_exact_line \
    "registered source: source-after-system: not attempted: prior phase stopped" \
    "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: system failure" "$stdout_file"
assert_exact_line "upgrade-all stopped on system failure" "$stdout_file"
assert_contains "fixture system upgrade failed" "$stderr_file"

setup_case stopped-source stopped-on-source-failure
run_status 1 upgrade-all
assert_exact_line \
    "registered source: source-failed: failed: fixture source build or install failed" \
    "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: source failure" "$stdout_file"
assert_exact_line "upgrade-all stopped on source failure" "$stdout_file"
assert_exact_line "partial completion" "$stdout_file"
assert_contains "fixture source build or install failed" "$stderr_file"

setup_case stopped-source-cleanup stopped-after-source-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "registered source: source-cleanup-failed: updated, but cleanup failed" \
    "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: source cleanup failure" "$stdout_file"
assert_exact_line \
    "upgrade-all stopped after source cleanup failure" "$stdout_file"
assert_exact_line "cleanup failure occurred" "$stdout_file"
assert_contains "fixture source cleanup failed" "$stderr_file"

setup_case stopped-source-no-change-cleanup \
    stopped-after-source-no-change-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "registered source: source-no-change-cleanup-failed: no package change, but cleanup failed" \
    "$stdout_file"
assert_exact_line "package state change unknown" "$stdout_file"

setup_case stopped-before-aur stopped-before-aur-execution
run_status 1 upgrade-all
assert_exact_line "AUR phase: blocked before execution" "$stdout_file"
assert_exact_line \
    "AUR target: aur-preflight-blocked: unsupported: split package selection required" \
    "$stdout_file"
assert_exact_line \
    "AUR target: aur-preparation-blocked: incomplete: source preference unavailable" \
    "$stdout_file"
assert_contains "fixture AUR preflight blocker" "$stderr_file"
assert_contains "fixture AUR preparation blocker" "$stderr_file"
assert_exact_line "upgrade-all stopped before AUR execution" "$stdout_file"

setup_case stopped-aur stopped-on-aur-failure
run_status 1 upgrade-all
assert_exact_line "AUR target: aur-first-updated: updated" "$stdout_file"
assert_exact_line \
    "AUR target: aur-failed: failed: package transaction failed (exit code 86)" \
    "$stdout_file"
assert_exact_line \
    "AUR target: aur-later: not attempted: prior work item stopped" \
    "$stdout_file"
assert_contains \
    "execution failure for PackageBase aur-failed: package transaction failed (exit code 86)" \
    "$stderr_file"
assert_contains \
    "transaction attempt: aur-failed 4.2.0-1 (explicit)" "$stderr_file"
assert_exact_line \
    "  required child: aur-failed (explicit): no successful outcome" \
    "$stdout_file"
assert_not_contains "required child: aur-failed ->" "$stdout_file"
assert_not_contains "/private/workspace/upgrade-all-secret" "$stdout_file"
assert_not_contains "/private/workspace/upgrade-all-secret" "$stderr_file"
assert_not_contains "/private/artifacts/" "$stdout_file"
assert_not_contains "/private/artifacts/" "$stderr_file"
assert_exact_line "upgrade-all stopped on AUR failure" "$stdout_file"
assert_exact_line "partial completion" "$stdout_file"
assert_exact_line "some phases were not attempted" "$stdout_file"

setup_case stopped-aur-cleanup stopped-after-aur-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "AUR target: aur-cleanup-failed: updated, but cleanup failed" \
    "$stdout_file"
assert_exact_line \
    "upgrade-all stopped after AUR cleanup failure" "$stdout_file"
assert_exact_line "cleanup failure occurred" "$stdout_file"
assert_contains \
    "execution failure for PackageBase aur-cleanup-failed: cleanup failure after successful package transaction" \
    "$stderr_file"

setup_case stopped-aur-no-change-cleanup \
    stopped-after-aur-no-change-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "AUR target: aur-no-change-cleanup-failed: no package change, but cleanup failed" \
    "$stdout_file"

setup_case stopped-aur-mixed-cleanup \
    stopped-after-aur-mixed-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "AUR target: aur-cleanup-main: updated, but cleanup failed" "$stdout_file"
assert_exact_line \
    "AUR target: aur-cleanup-later: not attempted: prior work item stopped" \
    "$stdout_file"
assert_exact_line "PackageBase result: aur-cleanup-suite" "$stdout_file"
assert_exact_line \
    "  required child: aur-cleanup-main -> aur-cleanup-main 8.3.0-5 (explicit): installed / updated, but cleanup failed" \
    "$stdout_file"
assert_exact_line \
    "  required child: aur-cleanup-dependency -> aur-cleanup-dependency 8.3.0-5 (dependency): skipped as needed / no change, but cleanup failed" \
    "$stdout_file"
assert_exact_line \
    "  produced artifact: aur-cleanup-suite-debug 8.3.0-5 (not selected; not installed)" \
    "$stdout_file"
assert_contains \
    "execution failure for PackageBase aur-cleanup-suite: cleanup failure after successful package transaction" \
    "$stderr_file"
assert_not_contains "/private/workspace/upgrade-all-secret" "$stdout_file"
assert_not_contains "/private/workspace/upgrade-all-secret" "$stderr_file"
assert_exact_line "partial completion" "$stdout_file"
assert_exact_line "some phases were not attempted" "$stdout_file"
assert_exact_line "cleanup failure occurred" "$stdout_file"

setup_case foreign-inventory-failure foreign-inventory-failure
run_status 1 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line "registered source packages: none" "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: foreign inventory failure" "$stdout_file"
assert_contains "foreign inventory read failed" "$stderr_file"
assert_contains \
    "foreign inventory failure [package query failed]: fixture foreign inventory read failed" \
    "$stderr_file"
assert_occurrence_count 1 "fixture foreign inventory read failed" "$stderr_file"
assert_exact_line "upgrade-all stopped before AUR execution" "$stdout_file"

# Direct inventory fields are authoritative even when a synthetic result keeps
# Completed and contains no aggregate issue copy.
setup_case defensive-direct-foreign-inventory \
    completed-direct-foreign-inventory-failure
run_status 1 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line "registered source packages: none" "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: foreign inventory failure" "$stdout_file"
assert_not_contains "AUR phase: completed" "$stdout_file"
assert_contains \
    "foreign inventory failure [local database unavailable]: fixture direct foreign inventory failure" \
    "$stderr_file"
assert_occurrence_count 1 \
    "fixture direct foreign inventory failure" "$stderr_file"
assert_contains \
    "failure details despite a successful aggregate status" "$stderr_file"

setup_case aggregate-inconsistent inconsistent-result
run_status 1 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line \
    "AUR phase not attempted: prior aggregate inconsistency" "$stdout_file"
assert_contains "fixture aggregate correlation inconsistency" "$stderr_file"
assert_exact_line "upgrade-all result inconsistent" "$stdout_file"

setup_case nested-system-unavailable nested-system-unavailable
run_status 1 upgrade-all
assert_exact_line "system: failed" "$stdout_file"
assert_not_exact_line "system: not attempted" "$stdout_file"
assert_contains \
    "System result unavailable after phase started due to an unexpected exception" \
    "$stderr_file"
assert_exact_line "upgrade-all result inconsistent" "$stdout_file"

setup_case nested-source-preserved nested-source-preserved
run_status 1 upgrade-all
assert_exact_line "system: completed" "$stdout_file"
assert_exact_line \
    "registered source: source-recorded-before-exception: updated" \
    "$stdout_file"
assert_exact_line \
    "registered source: source-unavailable-after-start: incomplete: Registered source result unavailable after phase started due to an unexpected exception" \
    "$stdout_file"
assert_not_contains \
    "registered source: source-unavailable-after-start: not attempted" \
    "$stdout_file"
assert_exact_line \
    "registered source: source-not-started: not attempted: prior phase stopped" \
    "$stdout_file"
assert_exact_line "partial completion" "$stdout_file"
assert_contains \
    "Registered source result unavailable after phase started due to an unexpected exception" \
    "$stderr_file"

# A synthetically successful aggregate status must never mask typed
# query/planning/mapping/reduction/inconsistency/cleanup/NotAttempted details.
setup_case defensive-query completed-query-failure
run_status 1 upgrade-all
assert_contains "AUR query failure for query-broken, query-also-broken" \
    "$stderr_file"
assert_exact_line "upgrade-all completed" "$stdout_file"
assert_contains \
    "failure details despite a successful aggregate status" "$stderr_file"

setup_case defensive-planning completed-planning-issue
run_status 1 upgrade-all
assert_contains "AUR planner issue: conflicting explicit PackageBase" \
    "$stderr_file"
assert_contains "AUR mapping issue: target/planner mapping inconsistent" \
    "$stderr_file"
assert_exact_line "upgrade-all completed" "$stdout_file"

setup_case defensive-inconsistency completed-inconsistency
run_status 1 upgrade-all
assert_contains "AUR reduction issue:" "$stderr_file"
assert_contains "fixture completed aggregate inconsistency" "$stderr_file"
assert_exact_line "upgrade-all completed" "$stdout_file"

setup_case defensive-cleanup completed-cleanup-failure
run_status 1 upgrade-all
assert_exact_line \
    "AUR target: defensive-cleanup: no package change, but cleanup failed" \
    "$stdout_file"
assert_exact_line "cleanup failure occurred" "$stdout_file"
assert_contains \
    "execution failure: cleanup failure after successful package transaction" \
    "$stderr_file"

setup_case defensive-not-attempted completed-not-attempted
run_status 1 upgrade-all
assert_exact_line \
    "AUR target: defensive-not-attempted: not attempted: prior work item stopped" \
    "$stdout_file"
assert_exact_line "some phases were not attempted" "$stdout_file"
assert_contains \
    "failure details despite a successful aggregate status" "$stderr_file"

# The top-level command boundary maps known and unknown exceptions to
# stderr + nonzero without fabricating a typed phase result.
setup_case prepare-exception prepare-exception
run_status 1 upgrade-all
assert_contains \
    "Unexpected upgrade-all command failure: fixture upgrade-all preparation exception" \
    "$stderr_file"
assert_not_contains "upgrade-all completed" "$stdout_file"

setup_case execute-unknown-exception execute-unknown-exception
run_status 1 upgrade-all
assert_contains \
    "Unexpected upgrade-all command failure: unknown exception." "$stderr_file"
assert_not_contains "upgrade-all completed" "$stdout_file"

# Every AUR-related enum value mapped by the production upgrade-all
# presenter. The row order mirrors the explicit enum arrays in the operation
# stub; the last row of each enum table injects an unknown value and proves
# that the command boundary fails closed.
run_matrix_table aur-phase-status 8 <<'EOF'
1|AUR phase not attempted: preparation blocked|upgrade-all result contains failure details despite a successful aggregate status.
0|AUR phase: no updates|-
0|AUR phase: completed|-
1|AUR phase: blocked before execution|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase: stopped on work-item failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase: stopped after cleanup failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase: inconsistent result|upgrade-all result contains failure details despite a successful aggregate status.
1|system: completed|Unexpected upgrade-all command failure: Unknown upgrade-all AUR phase status.
EOF

run_matrix_table not-attempted-reason 9 <<'EOF'
1|AUR phase not attempted: preparation blocked|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: system failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: source failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: source cleanup failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: system/source phase incomplete|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: foreign inventory failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: cache authority failure|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase not attempted: prior aggregate inconsistency|upgrade-all result contains failure details despite a successful aggregate status.
1|system: completed|Unexpected upgrade-all command failure: Unknown upgrade-all NotAttempted reason.
EOF

run_matrix_table target-status 10 <<'EOF'
0|AUR target: matrix-target: updated|-
0|AUR target: matrix-target: no change|-
0|AUR target: matrix-target: skipped: reason unavailable|-
1|AUR target: matrix-target: unsupported: reason unavailable|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: incomplete: reason unavailable|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: failed: build or install failure|execution failure: build or install failure
1|AUR target: matrix-target: updated, but cleanup failed|execution failure: cleanup failure after successful package transaction
1|AUR target: matrix-target: no package change, but cleanup failed|execution failure: cleanup failure after successful package transaction
1|AUR target: matrix-target: not attempted: prior work item stopped|upgrade-all result contains failure details despite a successful aggregate status.
1|system: completed|Unexpected upgrade-all command failure: Unknown AUR update target status.
EOF

run_matrix_table operation-status 7 <<'EOF'
1|AUR target: matrix-target: not attempted: result inconsistent|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: not attempted: result inconsistent|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: not attempted: operation blocked before execution|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: not attempted: prior work item stopped|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: not attempted: prior work item stopped|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: not attempted: result inconsistent|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown AUR update operation status.
EOF

run_matrix_table preflight-reason 21 <<'EOF'
1|AUR target: matrix-target: unsupported: none|AUR preflight issue: none: matrix preflight diagnostic
0|AUR target: matrix-target: skipped: up to date|-
0|AUR target: matrix-target: skipped: non-AUR foreign|-
1|AUR target: matrix-target: unsupported: AUR metadata unavailable|AUR preflight issue: AUR metadata unavailable: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: version comparison unavailable|AUR preflight issue: version comparison unavailable: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: installed reason unknown|AUR preflight issue: installed reason unknown: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: update plan inconsistent|AUR preflight issue: update plan inconsistent: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: duplicate update target|AUR preflight issue: duplicate update target: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: repository metadata unavailable|AUR preflight issue: repository metadata unavailable: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: AUR dependency metadata unavailable|AUR preflight issue: AUR dependency metadata unavailable: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: provider metadata unavailable|AUR preflight issue: provider metadata unavailable: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: unresolved dependency|AUR preflight issue: unresolved dependency: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: version constraint unverified|AUR preflight issue: version constraint unverified: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: dependency cycle|AUR preflight issue: dependency cycle: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: build plan inconsistent|AUR preflight issue: build plan inconsistent: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: package base mismatch|AUR preflight issue: package base mismatch: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: split package selection required|AUR preflight issue: split package selection required: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: multiple package targets for package base|AUR preflight issue: multiple package targets for package base: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: ambiguous provider|AUR preflight issue: ambiguous provider: matrix preflight diagnostic
1|AUR target: matrix-target: unsupported: conflicts/replaces unresolved|AUR preflight issue: conflicts/replaces unresolved: matrix preflight diagnostic
1|system: completed|Unexpected upgrade-all command failure: Unknown AUR update preflight reason.
EOF

run_matrix_table preparation-reason 16 <<'EOF'
1|AUR target: matrix-target: incomplete: none|AUR preparation issue: none: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: blocking preflight|AUR preparation issue: blocking preflight: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: preflight inconsistent|AUR preparation issue: preflight inconsistent: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: build plan missing|AUR preparation issue: build plan missing: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: build plan order empty|AUR preparation issue: build plan order empty: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: root attribution inconsistent|AUR preparation issue: root attribution inconsistent: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: package target attribution inconsistent|AUR preparation issue: package target attribution inconsistent: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: desired install reason missing|AUR preparation issue: desired install reason missing: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: source preference unavailable|AUR preparation issue: source preference unavailable: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: source preference PKGDEST conflict|AUR preparation issue: source preference PKGDEST conflict: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: static work item invalid|AUR preparation issue: static work item invalid: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: pacman database unavailable|AUR preparation issue: pacman database unavailable: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: generic preparation inconsistent|AUR preparation issue: generic preparation inconsistent: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: build unit selection inconsistent|AUR preparation issue: build unit selection inconsistent: matrix preparation diagnostic
1|AUR target: matrix-target: incomplete: external satisfaction inconsistent|AUR preparation issue: external satisfaction inconsistent: matrix preparation diagnostic
1|system: completed|Unexpected upgrade-all command failure: Unknown AUR update preparation reason.
EOF

run_matrix_table execution-failure-kind 6 <<'EOF'
1|AUR target: matrix-target: failed: failure category unavailable|upgrade-all result contains failure details despite a successful aggregate status.
1|AUR target: matrix-target: failed: build or install failure|execution failure: build or install failure
1|AUR target: matrix-target: updated, but cleanup failed|execution failure: cleanup failure after successful package transaction
1|AUR target: matrix-target: failed: unknown exception|execution failure: unknown exception
1|AUR target: matrix-target: not attempted: prior work item stopped|upgrade-all result contains failure details despite a successful aggregate status.
1|-|Unexpected upgrade-all command failure: Unknown AUR work-item failure kind.
EOF

run_matrix_table reduction-stage 4 <<'EOF'
1|AUR phase: completed|AUR reduction issue: preflight: duplicate preflight update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preparation: duplicate preflight update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: execution: duplicate preflight update plan index: matrix reduction diagnostic
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown AUR reduction stage.
EOF

run_matrix_table reduction-reason 26 <<'EOF'
1|AUR phase: completed|AUR reduction issue: preflight: duplicate preflight update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: out-of-range preflight update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: preflight target order inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: duplicate preparation attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unknown preparation update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: preparation attribution inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: preparation target snapshot inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: duplicate execution work item index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: execution work item order inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: duplicate execution attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unknown execution update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: missing execution attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: duplicate execution child attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: missing execution child attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unexpected execution child attribution: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unknown execution child update plan index: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: execution child snapshot inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unexpected selected artifact: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unexpected unselected artifact identity: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: execution result with preparation issues: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: missing execution result: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: unknown enum value: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: work item result inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: invocation result inconsistent: matrix reduction diagnostic
1|AUR phase: completed|AUR reduction issue: preflight: other correlation inconsistency: matrix reduction diagnostic
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown AUR reduction reason.
EOF

run_matrix_table filtered-issue-kind 16 <<'EOF'
1|AUR phase: completed|AUR mapping issue: unknown update classification: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: target/planner mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: filtered target mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: preflight target mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: preflight invocation index out of range: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: preflight invocation identity mismatch: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-plan root index missing: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-plan root index out of range: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-plan root identity mismatch: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-plan root package identity mismatch: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-unit order identity mismatch: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-unit root attribution inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: build-unit selection mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: execution build-unit mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|AUR mapping issue: reduced target mapping inconsistent: package matrix-target: PackageBase matrix-base: matrix mapping diagnostic
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown filtered AUR operation issue kind.
EOF

run_matrix_table planning-issue-kind 25 <<'EOF'
1|AUR phase: completed|AUR planner issue: explicit preference package name missing: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit produced package name missing: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit PackageBase absent: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit PackageBase resolution failed: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit PackageBase empty: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit source identity absent: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit source identity resolution failed: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: explicit source identity empty: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: conflicting explicit source identity definition: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: conflicting explicit package name: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: conflicting explicit PackageBase: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: AUR target package name missing: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: AUR target PackageBase absent: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: AUR target PackageBase resolution failed: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: AUR target PackageBase empty: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: unsupported AUR target: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: incomplete AUR target: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: build-unit PackageBase absent: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: build-unit PackageBase resolution failed: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: build-unit PackageBase empty: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: build unit has no root attribution: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: build-unit target index out of range: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: duplicate selected target PackageBase: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|AUR planner issue: duplicate selected build-unit PackageBase: package matrix-target: PackageBase matrix-base
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown upgrade-all planning issue kind.
EOF

run_matrix_table target-disposition 8 <<'EOF'
1|AUR phase: completed|Unexpected upgrade-all command failure: Non-exclusion target disposition reached duplicate presentation.
0|reason: package name handled by explicit source preference|-
0|reason: PackageBase handled by explicit source preference|-
1|AUR phase: completed|Unexpected upgrade-all command failure: Non-exclusion target disposition reached duplicate presentation.
1|AUR phase: completed|Unexpected upgrade-all command failure: Non-exclusion target disposition reached duplicate presentation.
1|AUR phase: completed|Unexpected upgrade-all command failure: Non-exclusion target disposition reached duplicate presentation.
1|AUR phase: completed|Unexpected upgrade-all command failure: Non-exclusion target disposition reached duplicate presentation.
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown upgrade-all target disposition.
EOF

run_matrix_table build-unit-role 5 <<'EOF'
0|affected AUR root: aur-root (root)|-
0|affected AUR root: aur-root (runtime dependency)|-
0|affected AUR root: aur-root (build dependency)|-
0|affected AUR root: aur-root (check dependency)|-
1|AUR phase: completed|Unexpected upgrade-all command failure: Unknown upgrade-all build-unit role.
EOF

run_matrix_table preparation-warning 1 <<'EOF'
0|AUR preparation warning: matrix-preference: matrix preparation warning diagnostic|-
EOF

run_matrix_table external-attribution-missing 1 <<'EOF'
1|AUR phase: completed|Unexpected upgrade-all command failure: External satisfaction has no explicit source identity.
EOF

if [ "$case_count" -ne 213 ]; then
    echo "upgrade-all command test scenario count drifted: $case_count" >&2
    exit 1
fi
echo "upgrade-all command integration tests: $case_count scenarios passed"
