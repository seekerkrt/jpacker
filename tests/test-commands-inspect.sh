#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

if ! command -v script >/dev/null 2>&1; then
    echo "script(1) is required for command inspection tests" >&2
    exit 1
fi

export PATH=$repo_root/tests/stubs:/usr/bin:/bin
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"
require_exact_test_command vercmp "$repo_root/tests/stubs/vercmp"

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    stdout_file=$case_dir/stdout
    stderr_file=$case_dir/stderr
    command_log=$case_dir/commands.log
    repository_metadata_state=$case_dir/repository-metadata.state

    mkdir -p \
        "$case_dir/home" "$case_dir/xdg-config" \
        "$case_dir/xdg-state" "$case_dir/xdg-cache"
    chmod 0700 "$case_dir/xdg-config"
    : > "$command_log"
    : > "$repository_metadata_state"
    export HOME=$case_dir/home
    export XDG_CONFIG_HOME=$case_dir/xdg-config
    export XDG_STATE_HOME=$case_dir/xdg-state
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export MOGUET_TEST_COMMAND_LOG=$command_log
    export MOGUET_TEST_PACKAGE_METADATA_EVENT_LOG=$command_log
    export MOGUET_TEST_REPOSITORY_METADATA_STATE_FILE=$repository_metadata_state
    MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST='core
extra'
    export MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST
    export MOGUET_TEST_PACMAN_EXIT_CODE=1
    export MOGUET_TEST_SUDO_EXIT_CODE=99
    export MOGUET_TEST_VERCMP_OUTPUT=1
    unset MOGUET_TEST_INSPECTION_SCENARIO
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_PACMAN_Q_OUTPUT
    unset MOGUET_TEST_PACMAN_Q_EXIT_CODE
    unset MOGUET_TEST_PACMAN_REPO_PACKAGES
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_FAIL_DESTINATION
    unset MOGUET_TEST_GIT_CLONE_FAIL_DESTINATION_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
    unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
    unset MOGUET_TEST_MAKEPKG_EXIT_CODE
    unset MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE
    unset MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE_AT
    unset MOGUET_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE
    unset MOGUET_TEST_PACKAGE_METADATA_QUERY_FAILURE_AT
    unset MOGUET_TEST_FOREIGN_PACKAGE_INVENTORY_STATE_FILE
    unset MOGUET_TEST_PACKAGE_METADATA_PACMAN_CONF_EXIT_CODE
    unset MOGUET_TEST_PACKAGE_METADATA_PACMAN_CONF_FAILURE_AT
    unset MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST_EXIT_CODE
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

run_ok_with_pipe() {
    input=$1
    shift
    : > "$command_log"
    if ! printf '%s\n' "$input" |
        "$test_binary" "$@" > "$stdout_file" 2> "$stderr_file"; then
        fail_case "expected piped command to succeed: $*"
    fi
}

run_fail_with_pipe() {
    input=$1
    shift
    : > "$command_log"
    set +e
    printf '%s\n' "$input" |
        "$test_binary" "$@" > "$stdout_file" 2> "$stderr_file"
    exit_status=$?
    set -e
    if [ "$exit_status" -ne 1 ]; then
        fail_case "expected piped command status 1, got $exit_status: $*"
    fi
}

run_tty_ok() {
    answer=$1
    shift
    : > "$command_log"
    if ! printf '%s\n' "$answer" |
        script -qec "$test_binary $*" /dev/null > "$stdout_file" 2> "$stderr_file"; then
        fail_case "expected interactive command to succeed: $*"
    fi
}

run_tty_fail() {
    answer=$1
    shift
    : > "$command_log"
    set +e
    printf '%s\n' "$answer" |
        script -qec "$test_binary $*" /dev/null \
            > "$stdout_file" 2> "$stderr_file"
    exit_status=$?
    set -e
    if [ "$exit_status" -ne 1 ]; then
        fail_case "expected interactive command status 1, got $exit_status: $*"
    fi
}

run_tty_ok_with_eof() {
    : > "$command_log"
    if ! script -qec "$test_binary $*" /dev/null </dev/null \
        > "$stdout_file" 2> "$stderr_file"; then
        fail_case "expected interactive EOF command to succeed: $*"
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

assert_not_exact_line() {
    unexpected=$1
    file=$2
    if grep -Fx -- "$unexpected" "$file" >/dev/null; then
        fail_case "unexpected exact line: $unexpected"
    fi
}

assert_exact_line_count() {
    expected=$1
    line=$2
    file=$3
    actual=$(grep -Fxc -- "$line" "$file" || true)
    if [ "$actual" -ne "$expected" ]; then
        fail_case "expected $expected occurrence(s) of '$line', got $actual"
    fi
}

assert_contains_count() {
    expected=$1
    pattern=$2
    file=$3
    actual=$(grep -Fc -- "$pattern" "$file" || true)
    if [ "$actual" -ne "$expected" ]; then
        fail_case "expected $expected line(s) containing '$pattern', got $actual"
    fi
}

assert_file_line_count() {
    expected=$1
    file=$2
    actual=$(wc -l < "$file" | tr -d ' ')
    if [ "$actual" -ne "$expected" ]; then
        fail_case "expected $expected line(s) in $file, got $actual"
    fi
}

assert_empty_file() {
    file=$1
    if [ -s "$file" ]; then
        fail_case "expected empty file: $file"
    fi
}

assert_line_immediately_after() {
    first=$1
    second=$2
    file=$3
    if ! awk -v first="$first" -v second="$second" '
        previous == first && $0 == second { found = 1 }
        { previous = $0 }
        END { if(!found) exit 1 }
    ' "$file"; then
        fail_case "expected '$second' immediately after '$first'"
    fi
}

set_repository_metadata() {
    repository=$1
    package=$2
    package_size=$3
    installed_size=$4
    printf '%s %s %s %s\n' \
        "$repository" "$package" "$package_size" "$installed_size" >> \
        "$repository_metadata_state"
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

assert_cache_root_absent() {
    if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ]; then
        fail_case "fetch created the cache before validation and provider preflight completed"
    fi
}

assert_no_foreign_update_mutation() {
    if grep -E '^(git|makepkg|sudo)( |$)' "$command_log" >/dev/null ||
       grep -E '^pacman( |$)' "$command_log" >/dev/null; then
        fail_case "foreign update query unexpectedly ran a forbidden command"
    fi
}

set_foreign_inventory() {
    inventory_state=$case_dir/foreign-inventory.state
    printf '%s\n' "$1" > "$inventory_state"
    export MOGUET_TEST_FOREIGN_PACKAGE_INVENTORY_STATE_FILE=$inventory_state
}

set_empty_foreign_inventory() {
    inventory_state=$case_dir/foreign-inventory.state
    : > "$inventory_state"
    export MOGUET_TEST_FOREIGN_PACKAGE_INVENTORY_STATE_FILE=$inventory_state
}

set_foreign_packages_101() {
    inventory_state=$case_dir/foreign-inventory.state
    : > "$inventory_state"
    expected_updates_file=$case_dir/expected-updates
    : > "$expected_updates_file"
    package_index=1
    while [ "$package_index" -le 101 ]; do
        package_name=$(printf 'foreign-%03d' "$package_index")
        printf '%s 1.0-1 explicit\n' "$package_name" >> "$inventory_state"
        printf '%s 1.0-1 -> 2.0-1\n' "$package_name" >> "$expected_updates_file"
        package_index=$((package_index + 1))
    done
    export MOGUET_TEST_FOREIGN_PACKAGE_INVENTORY_STATE_FILE=$inventory_state
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
export MOGUET_TEST_INSPECTION_SCENARIO=deps-partial-failure
run_fail deps deps-first deps-fail deps-third
assert_contains "Failed to inspect dependencies for deps-fail: fixture query failure" "$stderr_file"
assert_before "Package         : deps-first" "Package         : deps-third" "$stdout_file"
assert_not_contains "Package         : deps-fail" "$stdout_file"
assert_single_blank_before_occurrence "Package         : deps-third" 1 "$stdout_file"
assert_exact_command_before "aur info deps-first" "aur info deps-fail"
assert_exact_command_before "aur info deps-fail" "aur info deps-third"
echo "  ok: deps partial failure continues in target order"

setup_case deps-validation-position
export MOGUET_TEST_INSPECTION_SCENARIO=deps-validation-position
run_fail deps deps-first invalid/name deps-third
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to inspect dependencies for invalid/name" "$stderr_file"
assert_exact_line "aur info deps-first" "$command_log"
assert_not_contains "aur info deps-third" "$command_log"
echo "  ok: deps target validation remains outside the target catch"

setup_case deps-provider-order
export MOGUET_TEST_INSPECTION_SCENARIO=deps-provider-order
run_ok deps deps-provider-root
assert_exact_line "      1. aur/provider-z" "$stdout_file"
assert_exact_line "      2. aur/provider-a" "$stdout_file"
assert_before "      1. aur/provider-z" "      2. aur/provider-a" "$stdout_file"
echo "  ok: deps provider numbering preserves candidate order"

# Issue #272: provider choice is numbered, metadata-complete, and shared by the
# top-level classification and recursive view in one invocation.
setup_case deps-provider-interactive-selection
export MOGUET_TEST_INSPECTION_SCENARIO=deps-provider-interactive-selection
run_tty_ok 2 deps --recursive provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_contains \
    "2) source=AUR package=provider-a PackageBase=provider-a provided=moguet-inspect-203-virtual-provider provided-specification=moguet-inspect-203-virtual-provider version=2.0-1" \
    "$stdout_file"
assert_contains "Selected provided dependencies:" "$stdout_file"
assert_contains \
    "moguet-inspect-203-virtual-provider -> aur/provider-a" \
    "$stdout_file"
assert_contains \
    "moguet-inspect-203-virtual-provider [provided] by aur/provider-a" \
    "$stdout_file"
echo "  ok: deps shares one interactive provider choice with recursive display"

# A cancellation is an invocation-local decision for the canonical dependency;
# the recursive pass must retain ambiguity without asking again.
setup_case deps-provider-interactive-cancel
export MOGUET_TEST_INSPECTION_SCENARIO=deps-provider-interactive-cancel
run_tty_ok q deps --recursive provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_not_contains "Selected provided dependencies:" "$stdout_file"
assert_contains "Ambiguous provided dependencies:" "$stdout_file"
assert_contains \
    "moguet-inspect-203-virtual-provider [ambiguous-provider]" \
    "$stdout_file"
assert_no_git_mutation
assert_not_contains "makepkg " "$command_log"
assert_not_contains "sudo " "$command_log"
echo "  ok: deps retains an explicit provider cancellation across recursive resolution"

# EOF has the same cancellation contract and must not start a second prompt.
setup_case deps-provider-interactive-eof
export MOGUET_TEST_INSPECTION_SCENARIO=deps-provider-interactive-eof
run_tty_ok_with_eof deps --recursive provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_not_contains "Selected provided dependencies:" "$stdout_file"
assert_contains "Ambiguous provided dependencies:" "$stdout_file"
assert_no_git_mutation
assert_not_contains "makepkg " "$command_log"
assert_not_contains "sudo " "$command_log"
echo "  ok: deps retains provider EOF cancellation across recursive resolution"

# stdin pipe is never a provider-selection input source.
setup_case deps-provider-non-tty-pipe
export MOGUET_TEST_INSPECTION_SCENARIO=deps-provider-non-tty-pipe
run_ok_with_pipe 2 deps provider-root
assert_not_contains ":: provider dependency=" "$stdout_file"
assert_not_contains "Selected provided dependencies:" "$stdout_file"
assert_exact_line "      1. aur/provider-z" "$stdout_file"
assert_exact_line "      2. aur/provider-a" "$stdout_file"
echo "  ok: deps ignores piped provider input and remains ambiguous"

setup_case plan-partial-failure
export MOGUET_TEST_INSPECTION_SCENARIO=plan-partial-failure
run_fail plan plan-first plan-fail plan-third
assert_contains "Failed to plan build order for plan-fail: fixture plan failure" "$stderr_file"
assert_before "  1. plan-first" "  1. plan-third" "$stdout_file"
assert_single_blank_before_occurrence "Build plan:" 2 "$stdout_file"
assert_exact_command_before "aur info-strict plan-first" "aur info-strict plan-fail"
assert_exact_command_before "aur info-strict plan-fail" "aur info-strict plan-third"
echo "  ok: plan partial failure continues in target order"

setup_case plan-validation-position
export MOGUET_TEST_INSPECTION_SCENARIO=plan-validation-position
run_fail plan plan-first invalid/name plan-third
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to plan build order for invalid/name" "$stderr_file"
assert_exact_line "aur info-strict plan-first" "$command_log"
assert_not_contains "aur info-strict plan-third" "$command_log"
echo "  ok: plan target validation remains outside the target catch"

setup_case plan-provider-interactive-selection
export MOGUET_TEST_INSPECTION_SCENARIO=plan-provider-interactive-selection
run_tty_ok 2 plan provider-root provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_contains_count 2 \
    "moguet-inspect-203-virtual-provider -> aur/provider-a (selected)" \
    "$stdout_file"
assert_contains_count 2 "  1. provider-a" "$stdout_file"
assert_not_contains "Plan status: incomplete" "$stdout_file"
echo "  ok: plan reuses one interactive provider choice across targets"

setup_case plan-provider-interactive-cancel
export MOGUET_TEST_INSPECTION_SCENARIO=plan-provider-interactive-cancel
run_tty_ok q plan provider-root provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_not_contains " (selected)" "$stdout_file"
assert_contains_count 2 "Plan status: incomplete" "$stdout_file"
assert_no_git_mutation
assert_not_contains "makepkg " "$command_log"
assert_not_contains "sudo " "$command_log"
echo "  ok: plan retains provider cancellation across multiple targets"

# --noconfirm suppresses provider selection even when stdin is a TTY.
setup_case plan-provider-noconfirm-tty
export MOGUET_TEST_INSPECTION_SCENARIO=plan-provider-noconfirm-tty
run_tty_ok 2 --noconfirm plan provider-root
assert_not_contains ":: provider dependency=" "$stdout_file"
assert_not_contains " (selected)" "$stdout_file"
assert_contains "Ambiguous provided dependencies:" "$stdout_file"
assert_contains "Plan status: incomplete" "$stdout_file"
echo "  ok: --noconfirm keeps an ambiguous provider fail-closed on a TTY"

# Issue #125: formatterはprivate helperのまま、repository metadataからplan表示へ流して固定する。
setup_case plan-repository-size-formatter
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-formatter
MOGUET_TEST_PACMAN_REPO_PACKAGES='format-0 format-1023 format-1024 format-1152 format-1536 format-1048570 format-1048571 format-1048576 format-991730 format-5283285 format-int64-max'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
set_repository_metadata core format-0 0 0
set_repository_metadata core format-1023 1023 1023
set_repository_metadata core format-1024 1024 1024
set_repository_metadata core format-1152 1152 1152
set_repository_metadata core format-1536 1536 1536
set_repository_metadata core format-1048570 1048570 1048570
set_repository_metadata core format-1048571 1048571 1048571
set_repository_metadata core format-1048576 1048576 1048576
set_repository_metadata core format-991730 991730 991730
set_repository_metadata core format-5283285 5283285 5283285
set_repository_metadata core format-int64-max 9223372036854775807 9223372036854775807
run_ok plan plan-formatter-root
assert_line_immediately_after "  core/format-0" "    Package size   : 0 B" "$stdout_file"
assert_line_immediately_after "  core/format-1023" "    Package size   : 1023 B" "$stdout_file"
assert_line_immediately_after "  core/format-1024" "    Package size   : 1.00 KiB" "$stdout_file"
assert_line_immediately_after "  core/format-1152" "    Package size   : 1.13 KiB" "$stdout_file"
assert_line_immediately_after "  core/format-1536" "    Package size   : 1.50 KiB" "$stdout_file"
assert_line_immediately_after "  core/format-1048570" "    Package size   : 1023.99 KiB" "$stdout_file"
assert_line_immediately_after "  core/format-1048571" "    Package size   : 1.00 MiB" "$stdout_file"
assert_line_immediately_after "  core/format-1048576" "    Package size   : 1.00 MiB" "$stdout_file"
assert_line_immediately_after "  core/format-991730" "    Package size   : 968.49 KiB" "$stdout_file"
assert_line_immediately_after "  core/format-5283285" "    Package size   : 5.04 MiB" "$stdout_file"
assert_line_immediately_after "  core/format-int64-max" "    Package size   : 8.00 EiB" "$stdout_file"
echo "  ok: plan repository size formatting uses integer IEC round-half-up"

# success/zero/not-found/query failure/malformedは別stateとして表示し、plan statusを汚さない。
setup_case plan-repository-size-results
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-results
MOGUET_TEST_PACMAN_REPO_PACKAGES='result-zero result-missing result-query-failure result-malformed result-after-failure result-later-target'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
export MOGUET_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE=result-query-failure
set_repository_metadata core result-zero 0 0
set_repository_metadata core result-malformed -1 4096
set_repository_metadata core result-after-failure 1024 1536
set_repository_metadata core result-later-target 2048 3072
run_ok plan plan-result-root plan-result-later-root
assert_line_immediately_after "  core/result-zero" "    Package size   : 0 B" "$stdout_file"
assert_exact_line "    Installed size : 0 B" "$stdout_file"
assert_line_immediately_after "  result-missing" "    Metadata       : not found" "$stdout_file"
assert_line_immediately_after "  result-query-failure" "    Metadata       : unavailable (query failed)" "$stdout_file"
assert_line_immediately_after "  result-malformed" "    Metadata       : unavailable (invalid metadata)" "$stdout_file"
assert_line_immediately_after "  core/result-after-failure" "    Package size   : 1.00 KiB" "$stdout_file"
assert_line_immediately_after "  core/result-later-target" "    Package size   : 2.00 KiB" "$stdout_file"
assert_before "  result-query-failure" "  core/result-after-failure" "$stdout_file"
assert_before "  result-query-failure" "  1. plan-result-later-root" "$stdout_file"
assert_not_contains "Plan status: incomplete" "$stdout_file"
echo "  ok: plan repository metadata preserves zero, absence, failure, and malformed states"

# semantic lookupは(exact repository, package)、成功表示はreturned repo/packageでdedupeする。
setup_case plan-repository-size-identities
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-identities
MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST='core
extra
aur'
export MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST
MOGUET_TEST_PACMAN_REPO_PACKAGES='same-package different-package same-semantic repository-aur-package'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
set_repository_metadata extra same-package 1024 2048
set_repository_metadata core different-package 2048 3072
set_repository_metadata extra different-package 4096 5120
set_repository_metadata core same-semantic 6144 7168
set_repository_metadata aur repository-aur-package 8192 9216
run_ok plan plan-identity-root
assert_exact_line_count 1 "  extra/same-package" "$stdout_file"
assert_exact_line_count 1 "  core/different-package" "$stdout_file"
assert_exact_line_count 1 "  extra/different-package" "$stdout_file"
assert_exact_line_count 1 "  core/same-semantic" "$stdout_file"
assert_exact_line_count 1 "  aur/repository-aur-package" "$stdout_file"
assert_exact_line "  - identity-repository-aur-virtual -> aur/repository-aur-package" "$stdout_file"
assert_exact_line "  - identity-aur-virtual -> aur/identity-aur-provider" "$stdout_file"
assert_before "  extra/same-package" "  core/different-package" "$stdout_file"
assert_before "  core/different-package" "  extra/different-package" "$stdout_file"
assert_before "  extra/different-package" "  core/same-semantic" "$stdout_file"
assert_before "  core/same-semantic" "  aur/repository-aur-package" "$stdout_file"
assert_exact_line_count 1 "alpm sync-query core/same-package" "$command_log"
assert_exact_line_count 2 "alpm sync-query extra/same-package" "$command_log"
assert_exact_line_count 1 "alpm sync-query core/different-package" "$command_log"
assert_exact_line_count 1 "alpm sync-query extra/different-package" "$command_log"
assert_exact_line_count 1 "alpm sync-query core/same-semantic" "$command_log"
assert_exact_line_count 1 "alpm sync-query aur/repository-aur-package" "$command_log"
assert_not_contains "alpm sync-query stale/" "$command_log"
assert_not_contains "alpm sync-query core/identity-aur-provider" "$command_log"
assert_not_contains "alpm sync-query extra/identity-aur-provider" "$command_log"
assert_not_contains "alpm sync-query aur/identity-aur-provider" "$command_log"
assert_not_exact_line "  aur/identity-aur-provider" "$stdout_file"
assert_not_exact_line "  stale/stale-package" "$stdout_file"
echo "  ok: plan lookup/cache/display identities remain distinct and first-seen"

setup_case deps-typed-provider-display
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-identities
MOGUET_TEST_PACMAN_REPO_PACKAGES='same-package different-package same-semantic repository-aur-package'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
run_ok deps --recursive plan-identity-root
assert_exact_line "  - identity-repository-aur-virtual [provided] by aur/repository-aur-package" "$stdout_file"
assert_exact_line "  - identity-aur-virtual [provided] by aur/identity-aur-provider" "$stdout_file"
echo "  ok: recursive dependency display preserves typed provider labels"

# AUR build units、AUR provider、ambiguous/unknown edgeだけならmetadata contextを起動しない。
setup_case plan-repository-size-no-candidates
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-no-candidates
run_ok plan plan-no-metadata-root
assert_exact_line "  1. no-metadata-aur-child" "$stdout_file"
assert_exact_line "  2. no-metadata-aur-provider" "$stdout_file"
assert_not_contains "Repository package sizes:" "$stdout_file"
assert_not_contains "pacman-conf --verbose RootDir DBPath" "$command_log"
assert_not_contains "pacman-conf --repo-list" "$command_log"
assert_not_contains "alpm initialize" "$command_log"
assert_not_contains "alpm sync-register" "$command_log"
assert_not_contains "alpm sync-query" "$command_log"
echo "  ok: plan skips repository metadata calls when no eligible edge exists"

# sessionとquery cacheはcmd_plan invocation全体で共有するが、各targetのsectionは表示する。
setup_case plan-repository-size-multi-target-cache
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-multi-target-cache
export MOGUET_TEST_PACMAN_REPO_PACKAGES=cache-shared
set_repository_metadata core cache-shared 991730 5283285
run_ok plan plan-cache-first plan-cache-second
assert_exact_line_count 2 "Repository package sizes:" "$stdout_file"
assert_exact_line_count 2 "  core/cache-shared" "$stdout_file"
assert_exact_line_count 1 "pacman-conf --verbose RootDir DBPath" "$command_log"
assert_exact_line_count 1 "pacman-conf --repo-list" "$command_log"
assert_exact_line_count 1 "alpm initialize" "$command_log"
assert_exact_line_count 1 "alpm sync-register core" "$command_log"
assert_exact_line_count 1 "alpm sync-register extra" "$command_log"
assert_exact_line_count 1 "alpm sync-query core/cache-shared" "$command_log"
assert_exact_line_count 1 "alpm release" "$command_log"
echo "  ok: plan shares one metadata session/query cache and renders each target"

# open failureはplan本文とexit 0を維持し、後続targetでsession openを再試行しない。
setup_case plan-repository-size-open-failure
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-open-failure
MOGUET_TEST_PACMAN_REPO_PACKAGES='open-first-package open-second-package'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
export MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE=1
run_ok plan plan-open-failure-first plan-open-failure-second
assert_exact_line "  1. plan-open-failure-first" "$stdout_file"
assert_exact_line "  1. plan-open-failure-second" "$stdout_file"
assert_exact_line_count 2 "Repository package sizes:" "$stdout_file"
assert_exact_line_count 2 "  Metadata       : unavailable (initialization failed)" "$stdout_file"
assert_exact_line_count 1 "pacman-conf --verbose RootDir DBPath" "$command_log"
assert_exact_line_count 1 "pacman-conf --repo-list" "$command_log"
assert_exact_line_count 1 "alpm initialize" "$command_log"
assert_not_contains "alpm sync-register" "$command_log"
assert_not_contains "alpm sync-query" "$command_log"
assert_not_contains "Failed to plan build order" "$stderr_file"
echo "  ok: plan metadata session failure is sticky and non-fatal"

# configuration failureもinvocation内で記憶し、raw command failureをplan failureへ誤分類しない。
setup_case plan-repository-size-configuration-failure
export MOGUET_TEST_INSPECTION_SCENARIO=plan-repository-size-configuration-failure
MOGUET_TEST_PACMAN_REPO_PACKAGES='open-first-package open-second-package'
export MOGUET_TEST_PACMAN_REPO_PACKAGES
export MOGUET_TEST_PACMAN_CONF_REPOSITORY_LIST_EXIT_CODE=42
run_ok plan plan-open-failure-first plan-open-failure-second
assert_exact_line_count 2 "  Metadata       : unavailable (configuration unavailable)" "$stdout_file"
assert_exact_line_count 1 "pacman-conf --verbose RootDir DBPath" "$command_log"
assert_exact_line_count 1 "pacman-conf --repo-list" "$command_log"
assert_not_contains "alpm initialize" "$command_log"
assert_not_contains "Failed to plan build order" "$stderr_file"
echo "  ok: plan metadata configuration failure is sticky and non-fatal"

# pacman search/info routingはplan presentationへ接続せず、新metadata callを増やさない。
setup_case repository-size-search-routing
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_ok -Ss --repo keyword
assert_exact_line "pacman -Ss keyword" "$command_log"
assert_not_contains "pacman-conf " "$command_log"
assert_not_contains "alpm " "$command_log"

setup_case repository-size-info-routing
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_ok -Si --repo filesystem
assert_exact_line "pacman -Si filesystem" "$command_log"
assert_not_contains "pacman-conf " "$command_log"
assert_not_contains "alpm " "$command_log"
echo "  ok: pacman -Ss/-Si routes do not initialize repository metadata"

# Handler-owned usage/option messagesも、抽出でrunner側へずらさない。
setup_case deps-empty
run_fail deps
assert_contains "Usage: moguet deps [--recursive] <pkg>" "$stderr_file"

setup_case deps-unsupported
run_fail deps --unsupported deps-first
assert_contains "Unsupported deps option: --unsupported" "$stderr_file"
assert_contains "Usage: moguet deps [--recursive] <pkg>" "$stderr_file"

setup_case plan-empty
run_fail plan
assert_contains "Usage: moguet plan <pkg>" "$stderr_file"

setup_case plan-unsupported
run_fail plan --unsupported plan-first
assert_contains "Unsupported plan option: --unsupported" "$stderr_file"
assert_contains "Usage: moguet plan <pkg>" "$stderr_file"

setup_case fetch-empty
run_fail fetch
assert_contains "Usage: moguet fetch <pkg>" "$stderr_file"

setup_case fetch-unsupported
run_fail fetch --unsupported fetch-preflight-root
assert_contains "Unsupported fetch option: --unsupported" "$stderr_file"
assert_contains "Usage: moguet fetch <pkg>" "$stderr_file"

setup_case fetch-validation-position
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-validation-position
run_fail fetch fetch-preflight-root invalid/name fetch-after-root
assert_contains "Invalid package name: invalid/name" "$stderr_file"
assert_not_contains "Failed to fetch repositories for invalid/name" "$stderr_file"
if [ -s "$command_log" ]; then
    fail_case "fetch queried external metadata before all targets were valid"
fi
assert_cache_root_absent
assert_not_contains "aur info-strict fetch-after-root" "$command_log"
assert_no_git_mutation
echo "  ok: fetch validates every target before cache/network preparation"

setup_case fetch-provider-interactive-selection
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-provider-interactive-selection
run_tty_ok 2 fetch provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_contains "  1. provider-a -> https://aur.archlinux.org/provider-a.git" \
    "$stdout_file"
assert_contains "  2. provider-root -> https://aur.archlinux.org/provider-root.git" \
    "$stdout_file"
assert_contains "Selected provided dependencies:" "$stdout_file"
assert_contains \
    "moguet-inspect-203-virtual-provider -> aur/provider-a" \
    "$stdout_file"
assert_exact_line \
    "git clone https://aur.archlinux.org/provider-a.git provider-a" \
    "$command_log"
assert_exact_line \
    "git clone https://aur.archlinux.org/provider-root.git provider-root" \
    "$command_log"
assert_exact_command_before \
    "git clone https://aur.archlinux.org/provider-a.git provider-a" \
    "git clone https://aur.archlinux.org/provider-root.git provider-root"
assert_not_contains \
    "git clone https://aur.archlinux.org/provider-z.git provider-z" \
    "$command_log"
echo "  ok: fetch retrieves only the interactively selected AUR provider"

setup_case fetch-provider-interactive-cancel
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-provider-interactive-cancel
run_tty_fail q fetch provider-root
assert_contains_count 1 \
    ":: provider dependency=moguet-inspect-203-virtual-provider" \
    "$stdout_file"
assert_not_contains "Selected provided dependencies:" "$stdout_file"
assert_contains "Ambiguous provided dependencies:" "$stdout_file"
assert_cache_root_absent
assert_no_git_mutation
echo "  ok: fetch cancellation stops before cache or Git mutation"

setup_case fetch-provider-non-tty-pipe
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-provider-non-tty-pipe
run_fail_with_pipe 2 fetch provider-root
assert_not_contains ":: provider dependency=" "$stdout_file"
assert_not_contains "Selected provided dependencies:" "$stdout_file"
assert_contains "Ambiguous provided dependencies:" "$stdout_file"
assert_contains "Cannot execute build plan for provider-root; ambiguous providers:" \
    "$stderr_file"
assert_cache_root_absent
assert_no_git_mutation
echo "  ok: fetch ignores piped provider input and fails before cache or Git mutation"

# P0-2: planning/guardは全rootを先に走査し、1件でも失敗すればmutationを開始しない。
setup_case fetch-preflight-barrier
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-preflight-barrier
run_fail fetch fetch-preflight-root fetch-guard-root fetch-after-root
assert_contains "Failed to fetch repositories for fetch-guard-root: Cannot execute build plan for fetch-guard-root; cyclic dependencies: fetch-guard-root" "$stderr_file"
assert_exact_line "aur info-strict fetch-after-root" "$command_log"
assert_no_git_mutation
echo "  ok: fetch waits for every root preflight before mutation"

# execution phaseの失敗はentry単位。同じplanの後続entryと後続rootへ進む。
setup_case fetch-entry-continue
export MOGUET_TEST_INSPECTION_SCENARIO=fetch-entry-continue
export MOGUET_TEST_GIT_CLONE_FAIL_DESTINATION=fetch-entry-fail
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
last_plan_line=$(grep -nFx -- "aur info-strict fetch-later-root" "$command_log" | tail -n 1 | cut -d: -f1)
first_fetch_line=$(grep -nF -- "git clone " "$command_log" | head -n 1 | cut -d: -f1)
if [ -z "$last_plan_line" ] || [ -z "$first_fetch_line" ] || [ "$last_plan_line" -ge "$first_fetch_line" ]; then
    fail_case "fetch execution started before the later root completed planning"
fi
echo "  ok: fetch entry failure continues through the plan and later roots"

# foreign inventoryが空ならAUR queryへ進まず、従来messageとstatus 0を維持する。
setup_case foreign-empty
set_empty_foreign_inventory
run_ok -Qua
assert_contains "No foreign packages found." "$stdout_file"
assert_file_line_count 2 "$stdout_file"
assert_empty_file "$stderr_file"
assert_not_exact_line "pacman -Qm" "$command_log"
assert_exact_line_count 1 "alpm release" "$command_log"
assert_not_contains "aur " "$command_log"
assert_not_contains "Checking package" "$stdout_file"
assert_no_foreign_update_mutation
echo "  ok: empty foreign inventory returns success without AUR queries"

# inventory failureは正常emptyへ落とさず、AUR RPCとvercmpを開始しない。
setup_case foreign-inventory-failure
set_foreign_inventory 'foreign-never-queried 1.0-1 explicit'
export MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE=1
run_fail -Qua
assert_contains "Failed to initialize foreign package inventory" "$stderr_file"
assert_file_line_count 1 "$stdout_file"
assert_file_line_count 1 "$stderr_file"
assert_not_contains "No foreign packages found." "$stdout_file"
assert_not_contains "aur " "$command_log"
assert_not_contains "vercmp " "$command_log"
assert_not_contains "Checking package" "$stdout_file"
assert_no_foreign_update_mutation
echo "  ok: foreign inventory failure stops before AUR query"

# P0-3: 101 packageを100+1へ分け、emptyだったbatchだけper-package fallbackする。
setup_case foreign-batch-fallback
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-fallback
set_foreign_packages_101
run_ok -Qua
assert_exact_line "aur info-many 100 foreign-001 foreign-100" "$command_log"
assert_exact_line "aur info-many 1 foreign-101 foreign-101" "$command_log"
assert_exact_command_before "alpm release" "aur info-many 100 foreign-001 foreign-100"
assert_not_exact_line "pacman -Qm" "$command_log"
assert_numbered_foreign_batches
assert_exact_command_before "aur info-many 100 foreign-001 foreign-100" "aur info-strict foreign-001"
assert_exact_command_before "aur info-strict foreign-100" "aur info-many 1 foreign-101 foreign-101"
assert_not_contains "aur info-strict foreign-101" "$command_log"
fallback_count=$(grep -c '^aur info-strict ' "$command_log" || true)
if [ "$fallback_count" -ne 100 ]; then
    fail_case "expected 100 per-package fallback calls, got $fallback_count"
fi
actual_updates_file=$case_dir/actual-updates
grep -E '^foreign-[0-9][0-9][0-9] 1\.0-1 -> 2\.0-1$' "$stdout_file" > "$actual_updates_file"
if ! diff -u "$expected_updates_file" "$actual_updates_file"; then
    fail_case "foreign updates did not preserve installed package order"
fi
echo "  ok: foreign query batches 101 packages and scopes empty-result fallback"

# fallback中のschema/semantic response errorもordinary failureへ落とさず即時伝播する。
setup_case foreign-fallback-schema-failure
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-fallback-schema-failure
set_foreign_packages_101
run_fail -Qua
assert_contains "schema fallback failure" "$stderr_file"
assert_not_contains "Failed to fetch AUR info:" "$stderr_file"
assert_exact_line "aur info-many 100 foreign-001 foreign-100" "$command_log"
assert_exact_line "aur info-strict foreign-001" "$command_log"
assert_not_contains "aur info-strict foreign-002" "$command_log"
fallback_schema_info_many_count=$(grep -c '^aur info-many ' "$command_log" || true)
if [ "$fallback_schema_info_many_count" -ne 1 ]; then
    fail_case "fallback AurRpcResponseError should stop before the second batch"
fi
assert_not_contains "Checking package" "$stdout_file"
echo "  ok: foreign fallback AurRpcResponseError escapes the batch loop"

# ordinary batch failureはaggregate failureにしつつ、次batchと最終package走査を続ける。
setup_case foreign-ordinary-failure
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-ordinary-failure
set_foreign_packages_101
run_fail -Qua
assert_contains "Failed to fetch AUR info: ordinary batch failure" "$stderr_file"
assert_exact_line "aur info-many 100 foreign-001 foreign-100" "$command_log"
assert_exact_line "aur info-many 1 foreign-101 foreign-101" "$command_log"
assert_numbered_foreign_batches
assert_exact_command_before "aur info-many 100 foreign-001 foreign-100" "aur info-many 1 foreign-101 foreign-101"
if grep -E '^aur info(-strict)? ' "$command_log" >/dev/null; then
    fail_case "ordinary batch failure unexpectedly entered per-package fallback"
fi
assert_contains "Checking package 1/101: foreign-001" "$stdout_file"
assert_contains "Foreign package not found in AUR: foreign-001" "$stdout_file"
assert_before "Foreign package not found in AUR: foreign-001" "Checking package 101/101: foreign-101" "$stdout_file"
assert_exact_line "foreign-101 1.0-1 -> 2.0-1" "$stdout_file"
echo "  ok: foreign ordinary batch failure continues with aggregate failure"

# schema/semantic response errorはordinary failureとして握らず、後続batchへ進めない。
setup_case foreign-schema-failure
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-schema-failure
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

# result mapのkey順ではなく、libalpm local inventory順でwarning/updateを表示する。
setup_case foreign-display-order
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-order
set_foreign_inventory 'foreign-order-z 1.0-1 explicit
foreign-order-missing 1.0-1
foreign-order-a 1.0-1'
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

# up-to-dateとAUR非存在を同じbatchで分類し、query-only境界を維持する。
setup_case foreign-classification
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-classification
set_foreign_inventory 'foreign-up-to-date 2.0-1 dependency
foreign-non-aur 1.0-1'
export MOGUET_TEST_VERCMP_OUTPUT=0
run_ok -Qua
assert_exact_line_count 0 "pacman -Qm" "$command_log"
assert_exact_line_count 1 "aur info-many 2 foreign-up-to-date foreign-non-aur" "$command_log"
assert_not_contains "aur info-strict foreign-" "$command_log"
assert_exact_line "vercmp 2.0-1 2.0-1" "$command_log"
assert_not_contains "foreign-up-to-date 2.0-1 ->" "$stdout_file"
assert_contains "Foreign package not found in AUR: foreign-non-aur" "$stdout_file"
assert_file_line_count 6 "$stdout_file"
assert_empty_file "$stderr_file"
assert_no_foreign_update_mutation
echo "  ok: foreign query classifies up-to-date and non-AUR without mutation"

# vercmp parse failureはwarningを出し、fail-closedでupdateに分類しない。
setup_case foreign-invalid-vercmp
export MOGUET_TEST_INSPECTION_SCENARIO=foreign-classification
set_foreign_inventory 'foreign-up-to-date 1.0-1 unknown
foreign-non-aur 1.0-1'
export MOGUET_TEST_VERCMP_OUTPUT=invalid
run_ok -Qua
assert_exact_line "vercmp 2.0-1 1.0-1" "$command_log"
assert_contains "Failed to compare versions: 1.0-1 -> 2.0-1" "$stdout_file"
assert_not_contains "foreign-up-to-date 1.0-1 ->" "$stdout_file"
assert_contains "Foreign package not found in AUR: foreign-non-aur" "$stdout_file"
assert_no_foreign_update_mutation
echo "  ok: foreign version parse failure remains fail-closed"

echo "command inspection characterization tests: all checks passed"
