#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

export PATH=$repo_root/tests/stubs:/usr/bin:/bin

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    stdout_file=$case_dir/stdout
    stderr_file=$case_dir/stderr
    command_log=$case_dir/commands.log

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache"
    : > "$command_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=99
    export JPACKER_TEST_VERCMP_OUTPUT=1
    unset JPACKER_TEST_INSPECTION_SCENARIO
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_Q_OUTPUT
    unset JPACKER_TEST_PACMAN_Q_EXIT_CODE
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_PACKAGE_BUILD_DIR
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_MAKEPKG_EXIT_CODE
}

show_case_diagnostics() {
    echo "--- stdout ---" >&2
    sed -n '1,260p' "$stdout_file" >&2
    echo "--- stderr ---" >&2
    sed -n '1,260p' "$stderr_file" >&2
    echo "--- command log ---" >&2
    sed -n '1,320p' "$command_log" >&2
}

fail_case() {
    echo "$1" >&2
    show_case_diagnostics
    exit 1
}

run_ok() {
    : > "$command_log"
    if ! "$test_binary" "$@" > "$stdout_file" 2> "$stderr_file"; then
        fail_case "expected command to succeed: $*"
    fi
}

run_fail() {
    : > "$command_log"
    set +e
    "$test_binary" "$@" > "$stdout_file" 2> "$stderr_file"
    exit_status=$?
    set -e
    if [ "$exit_status" -ne 1 ]; then
        fail_case "expected command status 1, got $exit_status: $*"
    fi
}

assert_contains() {
    pattern=$1
    file=$2
    if ! grep -F -- "$pattern" "$file" >/dev/null; then
        fail_case "missing expected output: $pattern"
    fi
}

assert_not_contains() {
    pattern=$1
    file=$2
    if grep -F -- "$pattern" "$file" >/dev/null; then
        fail_case "unexpected output: $pattern"
    fi
}

assert_exact_line() {
    expected=$1
    file=$2
    if ! grep -Fx -- "$expected" "$file" >/dev/null; then
        fail_case "missing expected exact line: $expected"
    fi
}

assert_before() {
    first=$1
    second=$2
    file=$3
    first_line=$(grep -nF -- "$first" "$file" | head -n 1 | cut -d: -f1)
    second_line=$(grep -nF -- "$second" "$file" | head -n 1 | cut -d: -f1)
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        fail_case "expected '$first' before '$second'"
    fi
}

assert_exact_command_before() {
    first=$1
    second=$2
    first_line=$(grep -nFx -- "$first" "$command_log" | head -n 1 | cut -d: -f1)
    second_line=$(grep -nFx -- "$second" "$command_log" | head -n 1 | cut -d: -f1)
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        fail_case "expected command '$first' before '$second'"
    fi
}

assert_single_blank_before_occurrence() {
    marker=$1
    occurrence=$2
    file=$3
    if ! awk -v marker="$marker" -v occurrence="$occurrence" '
        $0 == marker {
            count++
            if(count == occurrence) {
                found = 1
                if(previous != "" || before_previous == "") bad = 1
            }
        }
        {
            before_previous = previous
            previous = $0
        }
        END {
            if(!found || bad) exit 1
        }
    ' "$file"; then
        fail_case "expected one blank line before occurrence $occurrence of: $marker"
    fi
}

assert_no_git_mutation() {
    if grep -E '^git (clone|fetch)( |$)' "$command_log" >/dev/null; then
        fail_case "fetch mutation ran before every target passed preflight"
    fi
}

set_foreign_packages_101() {
    qm_output=
    expected_updates_file=$case_dir/expected-updates
    : > "$expected_updates_file"
    package_index=1
    while [ "$package_index" -le 101 ]; do
        package_name=$(printf 'foreign-%03d' "$package_index")
        if [ -z "$qm_output" ]; then
            qm_output="$package_name 1.0-1"
        else
            qm_output="$qm_output
$package_name 1.0-1"
        fi
        printf '%s 1.0-1 -> 2.0-1\n' "$package_name" >> "$expected_updates_file"
        package_index=$((package_index + 1))
    done
    export JPACKER_TEST_PACMAN_QM_OUTPUT=$qm_output
}

assert_numbered_foreign_batches() {
    actual_batches_file=$case_dir/actual-batches
    grep '^aur info-many ' "$command_log" > "$actual_batches_file"
    expected_batches_file=$case_dir/expected-batches
    printf '%s\n' \
        'aur info-many 100 foreign-001 foreign-100' \
        'aur info-many 1 foreign-101 foreign-101' > "$expected_batches_file"
    if ! diff -u "$expected_batches_file" "$actual_batches_file"; then
        fail_case "foreign query did not issue exactly one 100-package and one 1-package batch"
    fi
}

# P0-1: ordinary failureはtarget単位で集約し、元のindexに基づく空行を保って後続へ進む。
setup_case deps-partial-failure
export JPACKER_TEST_INSPECTION_SCENARIO=deps-partial-failure
run_fail deps deps-first deps-fail deps-third
assert_contains "Failed to inspect dependencies for deps-fail: fixture query failure" "$stderr_file"
assert_before "Package         : deps-first" "Package         : deps-third" "$stdout_file"
assert_not_contains "Package         : deps-fail" "$stdout_file"
assert_single_blank_before_occurrence "Package         : deps-third" 1 "$stdout_file"
assert_exact_command_before "aur info deps-first" "aur info deps-fail"
assert_exact_command_before "aur info deps-fail" "aur info deps-third"
echo "  ok: deps partial failure continues in target order"

setup_case deps-validation-position
export JPACKER_TEST_INSPECTION_SCENARIO=deps-validation-position
run_fail deps deps-first invalid/name deps-third
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to inspect dependencies for invalid/name" "$stderr_file"
assert_exact_line "aur info deps-first" "$command_log"
assert_not_contains "aur info deps-third" "$command_log"
echo "  ok: deps target validation remains outside the target catch"

setup_case deps-provider-order
export JPACKER_TEST_INSPECTION_SCENARIO=deps-provider-order
run_ok deps deps-provider-root
assert_exact_line "      1. aur/provider-z" "$stdout_file"
assert_exact_line "      2. aur/provider-a" "$stdout_file"
assert_before "      1. aur/provider-z" "      2. aur/provider-a" "$stdout_file"
echo "  ok: deps provider numbering preserves candidate order"

setup_case plan-partial-failure
export JPACKER_TEST_INSPECTION_SCENARIO=plan-partial-failure
run_fail plan plan-first plan-fail plan-third
assert_contains "Failed to plan build order for plan-fail: fixture plan failure" "$stderr_file"
assert_before "  1. plan-first" "  1. plan-third" "$stdout_file"
assert_single_blank_before_occurrence "Build plan:" 2 "$stdout_file"
assert_exact_command_before "aur info plan-first" "aur info plan-fail"
assert_exact_command_before "aur info plan-fail" "aur info plan-third"
echo "  ok: plan partial failure continues in target order"

setup_case plan-validation-position
export JPACKER_TEST_INSPECTION_SCENARIO=plan-validation-position
run_fail plan plan-first invalid/name plan-third
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to plan build order for invalid/name" "$stderr_file"
assert_exact_line "aur info plan-first" "$command_log"
assert_not_contains "aur info plan-third" "$command_log"
echo "  ok: plan target validation remains outside the target catch"

# Handler-owned usage/option messagesも、抽出でrunner側へずらさない。
setup_case deps-empty
run_fail deps
assert_contains "Usage: jpacker deps [--recursive] <pkg>" "$stderr_file"

setup_case deps-unsupported
run_fail deps --unsupported deps-first
assert_contains "Unsupported deps option: --unsupported" "$stderr_file"
assert_contains "Usage: jpacker deps [--recursive] <pkg>" "$stderr_file"

setup_case plan-empty
run_fail plan
assert_contains "Usage: jpacker plan <pkg>" "$stderr_file"

setup_case plan-unsupported
run_fail plan --unsupported plan-first
assert_contains "Unsupported plan option: --unsupported" "$stderr_file"
assert_contains "Usage: jpacker plan <pkg>" "$stderr_file"

setup_case fetch-empty
run_fail fetch
assert_contains "Usage: jpacker fetch <pkg>" "$stderr_file"

setup_case fetch-unsupported
run_fail fetch --unsupported fetch-preflight-root
assert_contains "Unsupported fetch option: --unsupported" "$stderr_file"
assert_contains "Usage: jpacker fetch <pkg>" "$stderr_file"

setup_case fetch-validation-position
export JPACKER_TEST_INSPECTION_SCENARIO=fetch-validation-position
run_fail fetch fetch-preflight-root invalid/name fetch-after-root
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to fetch repositories for invalid/name" "$stderr_file"
assert_exact_line "aur info fetch-preflight-root" "$command_log"
assert_not_contains "aur info fetch-after-root" "$command_log"
assert_no_git_mutation
echo "  ok: fetch target validation remains outside the target catch"

# P0-2: planning/guardは全rootを先に走査し、1件でも失敗すればmutationを開始しない。
setup_case fetch-preflight-barrier
export JPACKER_TEST_INSPECTION_SCENARIO=fetch-preflight-barrier
run_fail fetch fetch-preflight-root fetch-guard-root fetch-after-root
assert_contains "Failed to fetch repositories for fetch-guard-root: Cannot execute build plan for fetch-guard-root; cyclic dependencies: fetch-guard-root" "$stderr_file"
assert_exact_line "aur info fetch-after-root" "$command_log"
assert_no_git_mutation
echo "  ok: fetch waits for every root preflight before mutation"

# execution phaseの失敗はentry単位。同じplanの後続entryと後続rootへ進む。
setup_case fetch-entry-continue
export JPACKER_TEST_INSPECTION_SCENARIO=fetch-entry-continue
export JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION=fetch-entry-fail
run_fail fetch fetch-exec-root fetch-later-root
assert_contains "Failed to fetch repositories for fetch-exec-root: Failed to clone fetch-entry-fail." "$stderr_file"
assert_exact_line "git clone https://aur.archlinux.org/fetch-entry-fail.git fetch-entry-fail" "$command_log"
assert_exact_line "git clone https://aur.archlinux.org/fetch-entry-after.git fetch-entry-after" "$command_log"
assert_exact_line "git clone https://aur.archlinux.org/fetch-exec-root.git fetch-exec-root" "$command_log"
assert_exact_line "git clone https://aur.archlinux.org/fetch-later-root.git fetch-later-root" "$command_log"
assert_exact_command_before "git clone https://aur.archlinux.org/fetch-entry-fail.git fetch-entry-fail" "git clone https://aur.archlinux.org/fetch-entry-after.git fetch-entry-after"
assert_exact_command_before "git clone https://aur.archlinux.org/fetch-entry-after.git fetch-entry-after" "git clone https://aur.archlinux.org/fetch-exec-root.git fetch-exec-root"
assert_exact_command_before "git clone https://aur.archlinux.org/fetch-exec-root.git fetch-exec-root" "git clone https://aur.archlinux.org/fetch-later-root.git fetch-later-root"
actual_clone_file=$case_dir/actual-clones
grep '^git clone ' "$command_log" > "$actual_clone_file"
expected_clone_file=$case_dir/expected-clones
printf '%s\n' \
    'git clone https://aur.archlinux.org/fetch-entry-fail.git fetch-entry-fail' \
    'git clone https://aur.archlinux.org/fetch-entry-after.git fetch-entry-after' \
    'git clone https://aur.archlinux.org/fetch-exec-root.git fetch-exec-root' \
    'git clone https://aur.archlinux.org/fetch-later-root.git fetch-later-root' > "$expected_clone_file"
if ! diff -u "$expected_clone_file" "$actual_clone_file"; then
    fail_case "fetch execution did not visit each entry exactly once in plan/root order"
fi
last_plan_line=$(grep -nFx -- "aur info fetch-later-root" "$command_log" | tail -n 1 | cut -d: -f1)
first_fetch_line=$(grep -nF -- "git clone " "$command_log" | head -n 1 | cut -d: -f1)
if [ -z "$last_plan_line" ] || [ -z "$first_fetch_line" ] || [ "$last_plan_line" -ge "$first_fetch_line" ]; then
    fail_case "fetch execution started before the later root completed planning"
fi
echo "  ok: fetch entry failure continues through the plan and later roots"

# P0-3: 101 packageを100+1へ分け、emptyだったbatchだけper-package fallbackする。
setup_case foreign-batch-fallback
export JPACKER_TEST_INSPECTION_SCENARIO=foreign-fallback
set_foreign_packages_101
run_ok -Qua
assert_exact_line "aur info-many 100 foreign-001 foreign-100" "$command_log"
assert_exact_line "aur info-many 1 foreign-101 foreign-101" "$command_log"
assert_numbered_foreign_batches
assert_exact_command_before "aur info-many 100 foreign-001 foreign-100" "aur info foreign-001"
assert_exact_command_before "aur info foreign-100" "aur info-many 1 foreign-101 foreign-101"
assert_not_contains "aur info foreign-101" "$command_log"
fallback_count=$(grep -c '^aur info ' "$command_log" || true)
if [ "$fallback_count" -ne 100 ]; then
    fail_case "expected 100 per-package fallback calls, got $fallback_count"
fi
actual_updates_file=$case_dir/actual-updates
grep -E '^foreign-[0-9][0-9][0-9] 1\.0-1 -> 2\.0-1$' "$stdout_file" > "$actual_updates_file"
if ! diff -u "$expected_updates_file" "$actual_updates_file"; then
    fail_case "foreign updates did not preserve installed package order"
fi
echo "  ok: foreign query batches 101 packages and scopes empty-result fallback"

# ordinary batch failureはaggregate failureにしつつ、次batchと最終package走査を続ける。
setup_case foreign-ordinary-failure
export JPACKER_TEST_INSPECTION_SCENARIO=foreign-ordinary-failure
set_foreign_packages_101
run_fail -Qua
assert_contains "Failed to fetch AUR info: ordinary batch failure" "$stderr_file"
assert_exact_line "aur info-many 100 foreign-001 foreign-100" "$command_log"
assert_exact_line "aur info-many 1 foreign-101 foreign-101" "$command_log"
assert_numbered_foreign_batches
assert_exact_command_before "aur info-many 100 foreign-001 foreign-100" "aur info-many 1 foreign-101 foreign-101"
if grep '^aur info ' "$command_log" >/dev/null; then
    fail_case "ordinary batch failure unexpectedly entered per-package fallback"
fi
assert_exact_line "foreign-101 1.0-1 -> 2.0-1" "$stdout_file"
echo "  ok: foreign ordinary batch failure continues with aggregate failure"

# schema/semantic response errorはordinary failureとして握らず、後続batchへ進めない。
setup_case foreign-schema-failure
export JPACKER_TEST_INSPECTION_SCENARIO=foreign-schema-failure
set_foreign_packages_101
run_fail -Qua
assert_contains "schema batch failure" "$stderr_file"
assert_not_contains "Failed to fetch AUR info:" "$stderr_file"
info_many_count=$(grep -c '^aur info-many ' "$command_log" || true)
if [ "$info_many_count" -ne 1 ]; then
    fail_case "AurRpcResponseError should stop before the second batch"
fi
assert_not_contains "Checking package" "$stdout_file"
echo "  ok: foreign AurRpcResponseError escapes the batch loop"

# result mapのkey順ではなく、pacman -Qmから得たinstalled package順でwarning/updateを表示する。
setup_case foreign-display-order
export JPACKER_TEST_INSPECTION_SCENARIO=foreign-order
JPACKER_TEST_PACMAN_QM_OUTPUT='foreign-order-z 1.0-1
foreign-order-missing 1.0-1
foreign-order-a 1.0-1'
export JPACKER_TEST_PACMAN_QM_OUTPUT
run_ok -Qua
assert_before "Checking package 1/3: foreign-order-z" "Checking package 2/3: foreign-order-missing" "$stdout_file"
assert_before "Checking package 2/3: foreign-order-missing" "Checking package 3/3: foreign-order-a" "$stdout_file"
assert_contains "Foreign package not found in AUR: foreign-order-missing" "$stdout_file"
assert_before "Checking package 1/3: foreign-order-z" "foreign-order-z 1.0-1 -> 2.0-1" "$stdout_file"
assert_before "foreign-order-z 1.0-1 -> 2.0-1" "Checking package 2/3: foreign-order-missing" "$stdout_file"
assert_before "Checking package 2/3: foreign-order-missing" "Foreign package not found in AUR: foreign-order-missing" "$stdout_file"
assert_before "Foreign package not found in AUR: foreign-order-missing" "Checking package 3/3: foreign-order-a" "$stdout_file"
assert_before "Checking package 3/3: foreign-order-a" "foreign-order-a 1.0-1 -> 2.0-1" "$stdout_file"
actual_updates_file=$case_dir/actual-updates
grep -E '^foreign-order-(z|a) 1\.0-1 -> 2\.0-1$' "$stdout_file" > "$actual_updates_file"
expected_updates_file=$case_dir/expected-updates
printf '%s\n' \
    'foreign-order-z 1.0-1 -> 2.0-1' \
    'foreign-order-a 1.0-1 -> 2.0-1' > "$expected_updates_file"
if ! diff -u "$expected_updates_file" "$actual_updates_file"; then
    fail_case "foreign update lines did not preserve installed package order"
fi
echo "  ok: foreign warning and update display preserves installed order"

echo "command inspection characterization tests: all checks passed"
