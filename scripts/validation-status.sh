#!/bin/sh

# Small portable primitives shared by repository validation scripts.  This
# file intentionally owns command/status handling only; callers retain their
# domain-specific snapshots, policy, and diagnostics.

validation_run_command() {
    if [ "$#" -eq 0 ]; then
        printf '%s\n' 'validation_run_command: missing command' >&2
        VALIDATION_COMMAND_STATUS=2
        return 0
    fi

    if "$@"; then
        VALIDATION_COMMAND_STATUS=0
    else
        VALIDATION_COMMAND_STATUS=$?
    fi
    return 0
}

validation_print_quoted_argv() {
    printf '  command:' >&2
    for validation_argument
    do
        validation_quoted_argument=$(printf '%s' "$validation_argument" |
            sed "s/'/'\\\\''/g")
        printf " '%s'" "$validation_quoted_argument" >&2
    done
    printf '\n' >&2
}

validation_assert_status() {
    if [ "$#" -lt 6 ]; then
        printf '%s\n' \
            'validation_assert_status: SCENARIO EXPECTED ACTUAL STDOUT STDERR COMMAND...' >&2
        return 2
    fi

    validation_scenario=$1
    validation_expected_status=$2
    validation_actual_status=$3
    validation_stdout_path=$4
    validation_stderr_path=$5
    shift 5

    if [ "$validation_actual_status" -eq "$validation_expected_status" ]; then
        return 0
    fi

    printf 'validation-status: scenario=%s expected=%s actual=%s\n' \
        "$validation_scenario" "$validation_expected_status" \
        "$validation_actual_status" >&2
    validation_print_quoted_argv "$@"
    if [ "$validation_stdout_path" = "$validation_stderr_path" ]; then
        printf '  combined-output: %s\n' "$validation_stdout_path" >&2
    else
        printf '  stdout: %s\n  stderr: %s\n' \
            "$validation_stdout_path" "$validation_stderr_path" >&2
    fi
    return 1
}

validation_expect_status() {
    if [ "$#" -lt 5 ]; then
        printf '%s\n' \
            'validation_expect_status: SCENARIO EXPECTED STDOUT STDERR COMMAND...' >&2
        return 2
    fi

    validation_expect_scenario=$1
    validation_expect_expected=$2
    validation_expect_stdout=$3
    validation_expect_stderr=$4
    shift 4

    if [ "$validation_expect_stdout" = "$validation_expect_stderr" ]; then
        validation_run_command "$@" \
            >"$validation_expect_stdout" 2>&1
    else
        validation_run_command "$@" \
            >"$validation_expect_stdout" 2>"$validation_expect_stderr"
    fi
    validation_expect_actual=$VALIDATION_COMMAND_STATUS

    validation_assert_status \
        "$validation_expect_scenario" "$validation_expect_expected" \
        "$validation_expect_actual" "$validation_expect_stdout" \
        "$validation_expect_stderr" "$@"
}

validation_capture_output() {
    if [ "$#" -lt 2 ]; then
        printf '%s\n' \
            'validation_capture_output: OUTPUT COMMAND...' >&2
        VALIDATION_COMMAND_STATUS=2
        return 2
    fi

    validation_capture_destination=$1
    shift
    if "$@" >"$validation_capture_destination"; then
        VALIDATION_COMMAND_STATUS=0
        return 0
    else
        VALIDATION_COMMAND_STATUS=$?
    fi
    return "$VALIDATION_COMMAND_STATUS"
}

validation_capture_sorted_output() {
    if [ "$#" -lt 3 ]; then
        printf '%s\n' \
            'validation_capture_sorted_output: RAW NORMALIZED COMMAND...' >&2
        VALIDATION_COMMAND_STATUS=2
        return 2
    fi

    validation_capture_raw=$1
    validation_capture_normalized=$2
    shift 2
    validation_capture_temporary=${validation_capture_normalized}.tmp.$$

    if ! rm -f -- "$validation_capture_normalized" \
        "$validation_capture_temporary"; then
        VALIDATION_COMMAND_STATUS=1
        return 1
    fi

    if validation_capture_output "$validation_capture_raw" "$@"; then
        :
    else
        validation_capture_status=$?
        VALIDATION_COMMAND_STATUS=$validation_capture_status
        return "$validation_capture_status"
    fi

    if LC_ALL=C sort "$validation_capture_raw" \
        >"$validation_capture_temporary"; then
        :
    else
        validation_capture_status=$?
        rm -f -- "$validation_capture_temporary" >/dev/null 2>&1 || :
        VALIDATION_COMMAND_STATUS=$validation_capture_status
        return "$validation_capture_status"
    fi

    if mv -- "$validation_capture_temporary" \
        "$validation_capture_normalized"; then
        VALIDATION_COMMAND_STATUS=0
        return 0
    else
        validation_capture_status=$?
    fi

    rm -f -- "$validation_capture_temporary" >/dev/null 2>&1 || :
    VALIDATION_COMMAND_STATUS=$validation_capture_status
    return "$validation_capture_status"
}

validation_grep_count() {
    if grep "$@"; then
        return 0
    else
        validation_grep_status=$?
    fi
    if [ "$validation_grep_status" -eq 1 ]; then
        # grep -c prints the canonical zero before returning status 1.
        return 0
    fi

    printf 'validation-status: grep count infrastructure failure (status %s)\n' \
        "$validation_grep_status" >&2
    validation_print_quoted_argv grep "$@"
    return "$validation_grep_status"
}
