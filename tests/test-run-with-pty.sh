#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
pty_runner=$repo_root/tests/run-with-pty.py
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

run_case() {
    case_name=$1
    expected_status=$2
    expected_pattern=$3
    shift 3

    output_file=$tmp_dir/$case_name.output
    if python3 "$pty_runner" "$@" </dev/null >"$output_file" 2>&1; then
        actual_status=0
    else
        actual_status=$?
    fi

    if [ "$actual_status" -ne "$expected_status" ]; then
        printf '%s\n' "unexpected status for $case_name: $actual_status" >&2
        printf 'command: %s\n' "$*" >&2
        cat "$output_file" >&2
        exit 1
    fi

    if [ -n "$expected_pattern" ]; then
        if ! grep -F -- "$expected_pattern" "$output_file" >/dev/null; then
            printf '%s\n' "expected output not observed for $case_name: $expected_pattern" >&2
            cat "$output_file" >&2
            exit 1
        fi
    fi
}

run_case default_exit_success 0 'legacy-mode' \
    -- /bin/sh -c 'printf "legacy-mode\\n"'
run_case explicit_timeout_success 0 'explicit-timeout' \
    --timeout 5 -- /bin/sh -c 'printf "explicit-timeout\\n"'
run_case default_exit_code 7 'error 7' -- /bin/sh -c 'printf "error 7\\n"; exit 7'
run_case timeout_expired 124 'PTY command timed out.' --timeout 1 -- /bin/sh -c 'sleep 2'
run_case usage_missing_command 2 'usage:'
run_case usage_missing_timeout_value 2 'usage:' --timeout
run_case usage_zero_timeout 2 'usage:' --timeout 0 -- /bin/true
run_case usage_negative_timeout 2 'usage:' --timeout -1 -- /bin/true
run_case usage_non_numeric_timeout 2 'usage:' --timeout abc -- /bin/true

if ! grep -F 'TIMEOUT_SECONDS = 20' "$repo_root/tests/run-with-pty.py" >/dev/null; then
    fail 'default timeout value regression in run-with-pty.py'
fi

printf '%s\n' 'run-with-pty focused tests: all checks passed'
