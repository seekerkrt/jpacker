#!/bin/sh

set -eu

production_binary=$1
root_test_binary=$2
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'runtime-identity-test: %s\n' "$*" >&2
    exit 1
}

assert_contains() {
    expected=$1
    output_file=$2
    grep -F -- "$expected" "$output_file" >/dev/null || {
        printf 'runtime-identity-test: missing expected output: %s\n' \
            "$expected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    }
}

assert_not_contains() {
    unexpected=$1
    output_file=$2
    if grep -F -- "$unexpected" "$output_file" >/dev/null; then
        printf 'runtime-identity-test: unexpected output: %s\n' \
            "$unexpected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    fi
}

version=$(tr -d '[:space:]' < "$repo_root/VERSION")

help_output=$tmp_dir/help.out
"$production_binary" --help > "$help_output" 2>&1
assert_contains "Moguet" "$help_output"
assert_contains "    moguet <op> [options] [targets...]" "$help_output"
assert_contains "    legacy jpacker.conf: EDITOR=" "$help_output"
grep -Fv 'jpacker.conf:' "$help_output" > "$tmp_dir/help-without-legacy-config"
if grep -Fi -- 'jpacker' "$tmp_dir/help-without-legacy-config" >/dev/null; then
    fail "help retains an unintended jpacker project identity."
fi

version_output=$tmp_dir/version.out
"$production_binary" --version > "$version_output" 2>&1
[ "$(cat "$version_output")" = "Moguet v$version" ] ||
    fail "production version output does not match Moguet v$version."
assert_not_contains "jpacker" "$version_output"

root_case_dir=$tmp_dir/root
mkdir -p "$root_case_dir/home" "$root_case_dir/xdg-cache"
root_output=$root_case_dir/output
root_status=0
if HOME=$root_case_dir/home XDG_CACHE_HOME=$root_case_dir/xdg-cache \
        "$root_test_binary" -Q filesystem > "$root_output" 2>&1; then
    fail "root execution test unexpectedly succeeded."
else
    root_status=$?
fi
[ "$root_status" -eq 1 ] ||
    fail "root execution returned $root_status; expected 1."
assert_contains "Do not run Moguet as root or with sudo." "$root_output"
assert_contains \
    "Run moguet as a normal user; Moguet will invoke sudo/pacman when needed." \
    "$root_output"
assert_not_contains "jpacker" "$root_output"
if [ -e "$root_case_dir/xdg-cache/jpacker" ] || \
   [ -L "$root_case_dir/xdg-cache/jpacker" ]; then
    fail "root guard allowed legacy cache/log initialization."
fi

MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
PATH=$repo_root/tests/stubs:/usr/bin:/bin
export PATH
. "$repo_root/tests/test-command-safety.sh"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"

startup_case_dir=$tmp_dir/startup
mkdir -p "$startup_case_dir/home" "$startup_case_dir/xdg-cache"
startup_output=$startup_case_dir/output
command_log=$startup_case_dir/commands.log
: > "$command_log"
export MOGUET_TEST_COMMAND_LOG=$command_log
export MOGUET_TEST_PACMAN_EXIT_CODE=0
HOME=$startup_case_dir/home XDG_CACHE_HOME=$startup_case_dir/xdg-cache \
    "$production_binary" -Q filesystem > "$startup_output" 2>&1 ||
    fail "safe startup identity command failed."
assert_contains "Started Moguet v$version" "$startup_output"
assert_not_contains "Started jpacker" "$startup_output"
[ "$(cat "$command_log")" = "pacman -Q filesystem" ] ||
    fail "startup identity check did not stay on the safe pacman stub route."

printf 'runtime-identity-test: all checks passed\n'
