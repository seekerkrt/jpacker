#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

export PATH=$repo_root/tests/stubs/commands-sync:$repo_root/tests/stubs:/usr/bin:/bin

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output
    config_file=$case_dir/jpacker.conf

    mkdir -p "$case_dir/work" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    : > "$output_file"
    : > "$config_file"

    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_CONFIG_FILE=$config_file
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export JPACKER_TEST_PACMAN_MAIN_STATUS=1
    export JPACKER_TEST_SUDO_MAIN_STATUS=0
    export JPACKER_TEST_GIT_CLONE_EXIT_CODE=0
    export JPACKER_TEST_MAKEPKG_EXIT_CODE=0

    unset JPACKER_TEST_PACMAN_MAIN_COMMAND
    unset JPACKER_TEST_PACMAN_MAIN_OUTPUT
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_INSTALLED_PACKAGES
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_SUDO_MAIN_OUTPUT
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_GIT_SYMBOLIC_REF
    unset JPACKER_TEST_GIT_SYMBOLIC_REF_EXIT_CODE
    unset JPACKER_TEST_SOURCE_PREFERENCE_EXTERNAL
}

run_status() {
    expected_status=$1
    shift
    : > "$command_log"
    : > "$output_file"

    actual_status=0
    (cd "$case_dir/work" && "$test_binary" "$@") > "$output_file" 2>&1 || actual_status=$?
    if [ "$actual_status" -ne "$expected_status" ]; then
        echo "unexpected status for case $case_name: $actual_status (expected $expected_status)" >&2
        echo "command: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_contains() {
    expected=$1
    file=$2
    if ! grep -F -- "$expected" "$file" >/dev/null; then
        echo "missing expected text in case $case_name: $expected" >&2
        sed -n '1,260p' "$file" >&2
        exit 1
    fi
}

assert_not_contains() {
    unexpected=$1
    file=$2
    if grep -F -- "$unexpected" "$file" >/dev/null; then
        echo "unexpected text in case $case_name: $unexpected" >&2
        sed -n '1,260p' "$file" >&2
        exit 1
    fi
}

assert_event() {
    expected=$1
    if ! grep -Fx -- "$expected" "$command_log" >/dev/null; then
        echo "missing expected event in case $case_name: $expected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_absent() {
    unexpected=$1
    if grep -Fx -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected event in case $case_name: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_prefix_absent() {
    unexpected_pattern=$1
    if grep -E -- "$unexpected_pattern" "$command_log" >/dev/null; then
        echo "unexpected event pattern in case $case_name: $unexpected_pattern" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_count() {
    expected_count=$1
    expected=$2
    actual_count=$(grep -Fxc -- "$expected" "$command_log" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected event count in case $case_name: $expected" >&2
        echo "actual: $actual_count, expected: $expected_count" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_at() {
    line_number=$1
    expected=$2
    actual=$(sed -n "${line_number}p" "$command_log")
    if [ "$actual" != "$expected" ]; then
        echo "unexpected event at line $line_number in case $case_name" >&2
        echo "actual: $actual" >&2
        echo "expected: $expected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_before() {
    first=$1
    second=$2
    first_line=$(grep -nFx -- "$first" "$command_log" | sed -n '1s/:.*//p')
    second_line=$(grep -nFx -- "$second" "$command_log" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected event order in case $case_name" >&2
        echo "expected before: $first" >&2
        echo "expected after:  $second" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_event_count_before() {
    expected_count=$1
    expected=$2
    boundary=$3
    boundary_line=$(grep -nFx -- "$boundary" "$command_log" | sed -n '1s/:.*//p')
    if [ -z "$boundary_line" ]; then
        echo "missing boundary event in case $case_name: $boundary" >&2
        cat "$command_log" >&2
        exit 1
    fi
    actual_count=$(sed -n "1,$((boundary_line - 1))p" "$command_log" | grep -Fxc -- "$expected" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected pre-boundary event count in case $case_name: $expected" >&2
        echo "actual: $actual_count, expected: $expected_count" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "unexpected external event in case $case_name" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_output_line_before() {
    first=$1
    second=$2
    first_line=$(grep -nF -- "$first" "$output_file" | sed -n '1s/:.*//p')
    second_line=$(grep -nF -- "$second" "$output_file" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected output order in case $case_name" >&2
        echo "expected before: $first" >&2
        echo "expected after:  $second" >&2
        sed -n '1,260p' "$output_file" >&2
        exit 1
    fi
}

assert_one_blank_line_between_output_lines() {
    first=$1
    second=$2
    first_line=$(grep -nF -- "$first" "$output_file" | sed -n '1s/:.*//p')
    second_line=$(grep -nF -- "$second" "$output_file" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] ||
       [ "$second_line" -ne $((first_line + 2)) ]; then
        echo "unexpected blank-line separation in case $case_name" >&2
        echo "expected before: $first" >&2
        echo "expected after:  $second" >&2
        sed -n '1,260p' "$output_file" >&2
        exit 1
    fi
    separator=$(sed -n "$((first_line + 1))p" "$output_file")
    if [ -n "$separator" ]; then
        echo "expected one blank line in case $case_name" >&2
        sed -n '1,260p' "$output_file" >&2
        exit 1
    fi
}

assert_two_info_blocks_have_one_blank_line() {
    first_end_line=$(grep -nF -- "Out of Date     :" "$output_file" | sed -n '1s/:.*//p')
    second_start_line=$(grep -nF -- "Repository      : aur" "$output_file" | sed -n '2s/:.*//p')
    if [ -z "$first_end_line" ] || [ -z "$second_start_line" ] ||
       [ "$second_start_line" -ne $((first_end_line + 2)) ]; then
        echo "unexpected blank-line separation between AUR info blocks in case $case_name" >&2
        sed -n '1,260p' "$output_file" >&2
        exit 1
    fi
    separator=$(sed -n "$((first_end_line + 1))p" "$output_file")
    if [ -n "$separator" ]; then
        echo "expected one blank line between AUR info blocks in case $case_name" >&2
        sed -n '1,260p' "$output_file" >&2
        exit 1
    fi
}

assert_no_mutation_events() {
    if grep -E '^(sudo pacman -S|git clone |makepkg )' "$command_log" >/dev/null; then
        echo "mutation event occurred before validation/plan barrier in case $case_name" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_cache_entry_absent() {
    entry=$XDG_CACHE_HOME/jpacker/$1
    if [ -e "$entry" ] || [ -L "$entry" ]; then
        echo "unexpected cache entry in case $case_name: $entry" >&2
        exit 1
    fi
}

assert_cache_entry_present() {
    entry=$XDG_CACHE_HOME/jpacker/$1
    if [ ! -d "$entry" ]; then
        echo "missing cache entry in case $case_name: $entry" >&2
        exit 1
    fi
}

ESC=$(printf '\033')

# P0-1/P0-2: search handler validation, selector behavior, preflight order, continuation, status, presentation.
setup_case search-missing-query
run_status 1 -Ss --aur
assert_contains "Missing search query." "$output_file"
assert_command_log_empty

setup_case aur-search-rejects-needed
run_status 1 -Ss --aur --needed search-hit-a
assert_contains "Unsupported pacman option for AUR search: --needed" "$output_file"
assert_command_log_empty

setup_case aur-search-presentation-no-installed-query
export JPACKER_TEST_PACMAN_QM_OUTPUT='search-presented 1.0-1'
run_status 0 -Ss --aur search-presented
assert_event_at 1 "aur search search-presented"
assert_event_count 1 "aur search search-presented"
assert_event_prefix_absent '^(pacman|sudo) '
assert_contains "Searching AUR..." "$output_file"
assert_contains "${ESC}[1;35maur${ESC}[0m/${ESC}[1msearch-presented${ESC}[0m ${ESC}[1;32m2.0-1${ESC}[0m ${ESC}[1;31m[out-of-date]${ESC}[0m ${ESC}[1;33m[orphaned]${ESC}[0m" "$output_file"
assert_output_line_before "Searching AUR..." "aur${ESC}[0m/${ESC}[1msearch-presented"
assert_not_contains "[installed]" "$output_file"
assert_contains "    search presentation fixture" "$output_file"

setup_case aur-search-empty
run_status 1 -Ss --aur search-empty
assert_event_at 1 "aur search search-empty"
assert_event_prefix_absent '^(pacman|sudo) '

setup_case repo-search-status-and-ordered-args
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Ss repo-only'
export JPACKER_TEST_PACMAN_MAIN_STATUS=13
run_status 13 -Ss --repo repo-only
assert_event_at 1 "pacman -Ss repo-only"
assert_event_count 1 "pacman -Ss repo-only"
assert_event_prefix_absent '^aur '
assert_event_prefix_absent '^sudo '

setup_case repo-search-refresh-sudo-and-global-option
export JPACKER_TEST_SUDO_MAIN_STATUS=17
run_status 17 --noconfirm -Ss --repo --refresh repo-a --config config-value repo-b
assert_event_at 1 "sudo pacman -Ss --noconfirm --refresh repo-a --config config-value repo-b"
assert_event_count 1 "sudo pacman -Ss --noconfirm --refresh repo-a --config config-value repo-b"
assert_event_prefix_absent '^aur '
assert_event_prefix_absent '^pacman '

setup_case auto-search-pacman-failure-aur-success
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Ss search-hit-a'
export JPACKER_TEST_PACMAN_MAIN_OUTPUT='repo search failed output'
export JPACKER_TEST_PACMAN_MAIN_STATUS=9
run_status 0 -Ss search-hit-a
assert_event_at 1 "pacman -Ss search-hit-a"
assert_event_at 2 "pacman -Qm"
assert_event_at 3 "aur search search-hit-a"
assert_output_line_before "repo search failed output" "Searching AUR..."
assert_output_line_before "Searching AUR..." "aur${ESC}[0m/${ESC}[1msearch-hit-a"

setup_case auto-search-pacman-success-aur-empty
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Ss search-empty'
export JPACKER_TEST_PACMAN_MAIN_STATUS=0
run_status 0 -Ss search-empty
assert_event_at 1 "pacman -Ss search-empty"
assert_event_at 2 "pacman -Qm"
assert_event_at 3 "aur search search-empty"

setup_case auto-search-both-empty-fail
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Ss search-empty'
export JPACKER_TEST_PACMAN_MAIN_STATUS=8
run_status 1 -Ss search-empty
assert_event_at 1 "pacman -Ss search-empty"
assert_event_at 2 "pacman -Qm"
assert_event_at 3 "aur search search-empty"

setup_case auto-search-installed-presentation
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Ss search-presented'
export JPACKER_TEST_PACMAN_MAIN_STATUS=0
export JPACKER_TEST_PACMAN_QM_OUTPUT='search-presented 1.0-1'
run_status 0 -Ss search-presented
assert_event_at 1 "pacman -Ss search-presented"
assert_event_at 2 "pacman -Qm"
assert_event_at 3 "aur search search-presented"
assert_contains "${ESC}[1;36m[installed]${ESC}[0m ${ESC}[1;31m[out-of-date]${ESC}[0m ${ESC}[1;33m[orphaned]${ESC}[0m" "$output_file"

setup_case auto-search-refresh-preflight-and-deferred-failure
export JPACKER_TEST_SUDO_MAIN_OUTPUT='repo refresh search output'
export JPACKER_TEST_SUDO_MAIN_STATUS=7
run_status 0 -Ssy search-deferred search-hit-b -- -skip
assert_event_at 1 "aur search search-deferred"
assert_event_at 2 "aur search search-hit-b"
assert_event_at 3 "sudo pacman -Ssy search-deferred search-hit-b -- -skip"
assert_event_at 4 "pacman -Qm"
assert_event_at 5 "aur search search-deferred"
assert_event_at 6 "aur search search-hit-b"
assert_event_absent "aur search -skip"
assert_output_line_before "repo refresh search output" "Searching AUR..."

setup_case auto-search-refresh-schema-stop
run_status 1 -Ssy search-hit-a search-schema
assert_event_at 1 "aur search search-hit-a"
assert_event_at 2 "aur search search-schema"
assert_event_count 1 "aur search search-schema"
assert_event_prefix_absent '^(pacman|sudo) '
assert_contains "fixture search schema failure" "$output_file"

# P0-3/P0-4: info validation, per-target continuation, Installed query, layout, filtering, aggregate status.
setup_case aur-info-rejects-needed
run_status 1 -Si --aur --needed info-a
assert_contains "Unsupported pacman option for AUR info: --needed" "$output_file"
assert_command_log_empty

setup_case aur-info-missing-target
run_status 1 -Si --aur
assert_contains "Missing AUR package target." "$output_file"
assert_command_log_empty

setup_case aur-info-validates-all-targets-before-rpc
run_status 1 -Si --aur info-a core/filesystem info-b
assert_contains "Invalid AUR package target: core/filesystem" "$output_file"
assert_command_log_empty

setup_case aur-info-continuation-installed-and-layout
export JPACKER_TEST_PACMAN_INSTALLED_PACKAGES='info-installed'
run_status 1 -Si --aur info-installed info-missing info-error info-uninstalled
assert_event_at 1 "aur info info-installed"
assert_event_at 2 "aur info info-missing"
assert_event_at 3 "aur info info-error"
assert_event_at 4 "aur info info-uninstalled"
assert_event_at 5 "pacman -Q info-installed"
assert_event_at 6 "pacman -Q info-uninstalled"
assert_event_count 1 "pacman -Q info-installed"
assert_event_count 1 "pacman -Q info-uninstalled"
assert_event_absent "pacman -Q info-missing"
assert_event_absent "pacman -Q info-error"
assert_event_prefix_absent '^pacman -Si( |$)'
assert_event_prefix_absent '^sudo '
assert_contains "AUR package not found: info-missing" "$output_file"
assert_contains "Failed to fetch AUR info for info-error: fixture info failure" "$output_file"
assert_contains "Installed       : ${ESC}[1;36myes${ESC}[0m" "$output_file"
assert_contains "Installed       : no" "$output_file"
assert_output_line_before "Name            : info-installed" "Name            : info-uninstalled"
assert_two_info_blocks_have_one_blank_line

setup_case aur-info-all-success
run_status 0 -Si --aur info-a info-b
assert_event_at 1 "aur info info-a"
assert_event_at 2 "aur info info-b"
assert_event_at 3 "pacman -Q info-a"
assert_event_at 4 "pacman -Q info-b"
assert_event_count 1 "pacman -Q info-a"
assert_event_count 1 "pacman -Q info-b"
assert_event_prefix_absent '^pacman -Si( |$)'
assert_event_prefix_absent '^sudo '
assert_output_line_before "Name            : info-a" "Name            : info-b"
assert_two_info_blocks_have_one_blank_line

setup_case repo-info-refresh-status
export JPACKER_TEST_SUDO_MAIN_STATUS=19
run_status 19 -Si --repo --refresh core/filesystem
assert_event_at 1 "sudo pacman -Si --refresh core/filesystem"
assert_event_count 1 "sudo pacman -Si --refresh core/filesystem"
assert_event_prefix_absent '^aur '
assert_event_prefix_absent '^pacman '

setup_case auto-info-refresh-barrier
run_status 1 -Siy core/qualified info-a
assert_contains "Cannot combine pacman refresh with AUR info fallback for unqualified target: info-a" "$output_file"
assert_contains "Use a repository-qualified target such as repo/package, or run refresh and -Si separately." "$output_file"
assert_command_log_empty

setup_case auto-info-mixed-continuation-filtering-and-layout
export JPACKER_TEST_PACMAN_REPO_PACKAGES='repo-local'
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Si core/qualified --config info-a repo-local'
export JPACKER_TEST_PACMAN_MAIN_OUTPUT='repo info transaction output'
export JPACKER_TEST_PACMAN_MAIN_STATUS=0
run_status 1 -Si core/qualified --config info-a repo-local info-a info-missing info-error info-b
assert_event_at 1 "pacman -Si repo-local"
assert_event_at 2 "pacman -Si info-a"
assert_event_at 3 "aur info info-a"
assert_event_at 4 "pacman -Si info-missing"
assert_event_at 5 "aur info info-missing"
assert_event_at 6 "pacman -Si info-error"
assert_event_at 7 "aur info info-error"
assert_event_at 8 "pacman -Si info-b"
assert_event_at 9 "aur info info-b"
assert_event_at 10 "pacman -Si core/qualified --config info-a repo-local"
assert_event_at 11 "pacman -Q info-a"
assert_event_at 12 "pacman -Q info-b"
assert_event_absent "pacman -Si core/qualified --config info-a repo-local info-missing info-error"
assert_contains "Package not found in repos or AUR: info-missing" "$output_file"
assert_contains "Failed to fetch AUR info for info-error: fixture info failure" "$output_file"
assert_output_line_before "repo info transaction output" "Repository      : aur"
assert_one_blank_line_between_output_lines "repo info transaction output" "Repository      : aur"
assert_output_line_before "Name            : info-a" "Name            : info-b"
assert_two_info_blocks_have_one_blank_line

setup_case auto-info-pacman-failure-not-hidden
export JPACKER_TEST_PACMAN_MAIN_COMMAND='-Si core/qualified'
export JPACKER_TEST_PACMAN_MAIN_OUTPUT='repo info failed output'
export JPACKER_TEST_PACMAN_MAIN_STATUS=6
run_status 1 -Si core/qualified info-a
assert_event_at 1 "pacman -Si info-a"
assert_event_at 2 "aur info info-a"
assert_event_at 3 "pacman -Si core/qualified"
assert_event_at 4 "pacman -Q info-a"
assert_contains "Name            : info-a" "$output_file"
assert_output_line_before "repo info failed output" "Repository      : aur"
assert_one_blank_line_between_output_lines "repo info failed output" "Repository      : aur"

# P0-5/P0-6/P0-7: install transaction boundary, all-root/all-source barriers, ordering and failure stops.
setup_case repo-install-one-ordered-transaction
printf 'CFLAGS=-Oshould-not-load\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/repo-a"
export JPACKER_TEST_SUDO_MAIN_STATUS=31
run_status 31 --noconfirm -S --repo repo-a --config config-value repo-b
assert_event_at 1 "sudo pacman -S --noconfirm repo-a --config config-value repo-b"
assert_event_count 1 "sudo pacman -S --noconfirm repo-a --config config-value repo-b"
assert_event_prefix_absent '^aur '
assert_event_prefix_absent '^pacman '
assert_event_prefix_absent '^(git|makepkg) '
assert_not_contains "Loading custom build flags" "$output_file"

setup_case aur-install-missing-target
run_status 1 -S --aur
assert_contains "Missing AUR package target." "$output_file"
assert_command_log_empty

setup_case aur-install-validates-all-targets-before-plan
run_status 1 -S --aur plan-a core/filesystem plan-b
assert_contains "Invalid AUR package target: core/filesystem" "$output_file"
assert_command_log_empty

setup_case aur-install-rejects-option-before-plan
run_status 1 -S --aur plan-a --config config-value plan-b
assert_contains "Unsupported pacman option for AUR/source-build target: --config" "$output_file"
assert_contains "Rerun --aur without this option." "$output_file"
assert_command_log_empty

setup_case aur-install-all-root-plan-barrier
run_status 1 --noedit --nodiff --noconfirm -S --aur plan-a plan-missing
assert_event_at 1 "aur info plan-a"
assert_event_at 2 "aur info plan-a"
assert_event_at 3 "aur info plan-missing"
assert_event_count 2 "aur info plan-a"
assert_event_count 1 "aur info plan-missing"
assert_no_mutation_events
assert_event_prefix_absent '^sudo '
assert_cache_entry_absent plan-a
assert_cache_entry_absent plan-missing

setup_case aur-install-plan-order-needed-and-preferences-disabled
printf 'CFLAGS=-Oaur-only-must-ignore\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/plan-a"
printf 'CFLAGS=-Oaur-only-must-ignore\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/plan-b"
run_status 0 --noedit --nodiff --noconfirm -S --aur --needed plan-a plan-b
assert_event_at 1 "aur info plan-a"
assert_event_at 2 "aur info plan-a"
assert_event_at 3 "aur info plan-b"
assert_event_at 4 "aur info plan-b"
assert_event_at 5 "git clone https://aur.archlinux.org/plan-a.git plan-a"
assert_event_at 6 "git config --get remote.origin.url"
assert_event_at 7 "makepkg -sic --noconfirm --needed"
assert_event_at 8 "git clone https://aur.archlinux.org/plan-b.git plan-b"
assert_event_at 9 "git config --get remote.origin.url"
assert_event_at 10 "makepkg -sic --noconfirm --needed"
assert_event_count 2 "makepkg -sic --noconfirm --needed"
assert_event_absent "sudo pacman -S --noconfirm --needed plan-a plan-b"
assert_not_contains "Loading custom build flags" "$output_file"
assert_not_contains "Applying custom build flags" "$output_file"
assert_cache_entry_present plan-a
assert_cache_entry_present plan-b

setup_case aur-install-first-execution-failure-stops-later-plan
export JPACKER_TEST_MAKEPKG_EXIT_CODE=42
run_status 1 --noedit --nodiff --noconfirm -S --aur --needed plan-a plan-b
assert_event_at 1 "aur info plan-a"
assert_event_at 2 "aur info plan-a"
assert_event_at 3 "aur info plan-b"
assert_event_at 4 "aur info plan-b"
assert_event_at 5 "git clone https://aur.archlinux.org/plan-a.git plan-a"
assert_event_at 7 "makepkg -sic --noconfirm --needed"
assert_event_absent "git clone https://aur.archlinux.org/plan-b.git plan-b"
assert_event_absent "sudo pacman -S --noconfirm --needed plan-a plan-b"
assert_cache_entry_present plan-a
assert_cache_entry_absent plan-b

setup_case auto-install-targetless-pacman-pass-through
export JPACKER_TEST_SUDO_MAIN_STATUS=23
run_status 23 --noconfirm -S
assert_event_at 1 "sudo pacman -S --noconfirm"
assert_event_count 1 "sudo pacman -S --noconfirm"
assert_event_prefix_absent '^aur '
assert_event_prefix_absent '^pacman '
assert_event_prefix_absent '^(git|makepkg) '

setup_case auto-install-target-validation-and-classification-order
run_status 1 -S source-a core/filesystem source-b
assert_event_at 1 "pacman -Si source-a"
assert_event_count 1 "pacman -Si source-a"
assert_event_absent "pacman -Si source-b"
assert_event_prefix_absent '^aur '
assert_no_mutation_events
assert_contains "Invalid package name: core/filesystem" "$output_file"

setup_case auto-install-unsupported-option-before-source-guard
run_status 1 -S source-a --config config-value
assert_event_at 1 "pacman -Si source-a"
assert_event_prefix_absent '^aur '
assert_no_mutation_events
assert_contains "Unsupported pacman option for AUR/source-build target: --config" "$output_file"
assert_contains "Split official repository and AUR/source-build targets, or rerun without this option." "$output_file"

setup_case auto-install-all-source-guard-before-pacman
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a'
run_status 1 --noedit --nodiff --noconfirm -S official-a source-a plan-missing
assert_event_at 1 "pacman -Si official-a"
assert_event_at 2 "pacman -Si source-a"
assert_event_at 3 "pacman -Si plan-missing"
assert_event_at 4 "pacman -Si source-a"
assert_event_at 5 "aur info source-a"
assert_event_at 6 "aur info source-a"
assert_event_at 7 "pacman -Si plan-missing"
assert_event_at 8 "aur info plan-missing"
assert_event_prefix_absent '^sudo pacman -S( |$)'
assert_event_prefix_absent '^(git|makepkg) '
assert_cache_entry_absent source-a
assert_cache_entry_absent plan-missing

setup_case auto-install-mixed-order-filtering-and-source-asymmetry
printf 'CFLAGS=-Oforced-official\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/forced-official"
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a forced-official'
export JPACKER_TEST_SUDO_MAIN_STATUS=0
run_status 0 --noedit --nodiff --noconfirm -S official-a --needed source-a forced-official source-b
official_transaction='sudo pacman -S --noconfirm official-a --needed'
assert_event "$official_transaction"
assert_event_count 1 "$official_transaction"
assert_event_count_before 2 "aur info source-a" "$official_transaction"
assert_event_count_before 2 "aur info source-b" "$official_transaction"
assert_event_count_before 2 "pacman -Si forced-official" "$official_transaction"
assert_event_before "$official_transaction" "git clone https://aur.archlinux.org/source-a.git source-a"
assert_event_before "git clone https://aur.archlinux.org/source-a.git source-a" "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/forced-official.git forced-official"
assert_event_before "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/forced-official.git forced-official" "git clone https://aur.archlinux.org/source-b.git source-b"
assert_event_count 3 "makepkg -sic --noconfirm --needed"
assert_event_absent "sudo pacman -S --noconfirm official-a --needed source-a forced-official source-b"
assert_event_absent "sudo pacman -S --noconfirm source-a"
assert_event_absent "sudo pacman -S --noconfirm forced-official"
assert_event_absent "sudo pacman -S --noconfirm source-b"
assert_contains "Loading custom build flags from $JPACKER_TEST_PACKAGE_BUILD_DIR/forced-official" "$output_file"
assert_cache_entry_present source-a
assert_cache_entry_present forced-official
assert_cache_entry_present source-b

setup_case auto-install-pacman-failure-stops-source-execution
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a'
export JPACKER_TEST_SUDO_MAIN_STATUS=42
run_status 1 --noedit --nodiff --noconfirm -S official-a source-a source-b
failed_transaction='sudo pacman -S --noconfirm official-a'
assert_event "$failed_transaction"
assert_event_count_before 2 "aur info source-a" "$failed_transaction"
assert_event_count_before 2 "aur info source-b" "$failed_transaction"
assert_event_prefix_absent '^(git|makepkg) '
assert_contains "Pacman failed." "$output_file"
assert_cache_entry_absent source-a
assert_cache_entry_absent source-b

setup_case auto-install-first-source-failure-stops-later-target
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a'
export JPACKER_TEST_SUDO_MAIN_STATUS=0
export JPACKER_TEST_MAKEPKG_EXIT_CODE=42
run_status 1 --noedit --nodiff --noconfirm -S official-a source-a source-b
assert_event "sudo pacman -S --noconfirm official-a"
assert_event_before "sudo pacman -S --noconfirm official-a" "git clone https://aur.archlinux.org/source-a.git source-a"
assert_event "makepkg -sic --noconfirm"
assert_event_absent "git clone https://aur.archlinux.org/source-b.git source-b"
assert_cache_entry_present source-a
assert_cache_entry_absent source-b

setup_case auto-install-sysupgrade-without-official-target
export JPACKER_TEST_SUDO_MAIN_STATUS=0
run_status 0 --noedit --nodiff --noconfirm -Syu source-a
assert_event "sudo pacman -Syu --noconfirm"
assert_event_count_before 2 "aur info source-a" "sudo pacman -Syu --noconfirm"
assert_event_before "sudo pacman -Syu --noconfirm" "git clone https://aur.archlinux.org/source-a.git source-a"
assert_event "makepkg -sic --noconfirm"
assert_event_absent "sudo pacman -Syu --noconfirm source-a"

echo "sync command characterization tests: all checks passed"
