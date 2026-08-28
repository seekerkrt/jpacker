#!/usr/bin/env bash

set -euo pipefail

repo_root=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
helper_source=${1:-$repo_root/scripts/format-changed-cpp.sh}
clang_format_name=${CLANG_FORMAT:-clang-format}

fail() {
    printf 'format-changed-cpp-test: %s\n' "$*" >&2
    exit 1
}

assert_contains() {
    local expected=$1
    local output=$2

    grep -F -- "$expected" "$output" >/dev/null || {
        printf 'missing expected output: %s\n' "$expected" >&2
        sed -n '1,160p' "$output" >&2
        exit 1
    }
}

expect_status() {
    local expected_status=$1
    local output=$2
    shift 2
    local actual_status=0

    "$@" >"$output" 2>&1 || actual_status=$?
    if (( actual_status != expected_status )); then
        printf 'expected status %d, got %d: %s\n' \
            "$expected_status" "$actual_status" "$*" >&2
        sed -n '1,160p' "$output" >&2
        exit 1
    fi
}

expect_failure() {
    local output=$1
    shift
    local actual_status=0

    "$@" >"$output" 2>&1 || actual_status=$?
    if (( actual_status == 0 )); then
        printf 'expected failure, got status 0: %s\n' "$*" >&2
        sed -n '1,160p' "$output" >&2
        exit 1
    fi
}

assert_changed() {
    local actual=$1
    local before=$2
    local label=$3

    if cmp -s -- "$actual" "$before"; then
        fail "$label was not formatted by --write."
    fi
}

assert_unchanged() {
    local actual=$1
    local before=$2
    local label=$3

    if ! cmp -s -- "$actual" "$before"; then
        fail "$label was modified implicitly."
    fi
}

[[ -f "$helper_source" ]] || fail "helper source is missing: $helper_source"
if ! clang_format=$(command -v -- "$clang_format_name"); then
    fail "clang-format command not found: $clang_format_name"
fi

tmp_dir=$(mktemp -d)
cleanup() {
    rm -rf -- "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM

GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
export GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM

git_template=$tmp_dir/git-template
mkdir -p "$git_template"

initialize_repository() {
    local fixture_root=$1

    git init -q -b main --template="$git_template" "$fixture_root"
    git -C "$fixture_root" config --local core.hooksPath /dev/null
    git -C "$fixture_root" config --local commit.gpgSign false
    git -C "$fixture_root" config --local user.name \
        'Moguet format helper fixture'
    git -C "$fixture_root" config --local user.email \
        'format-helper@example.invalid'
    mkdir -p "$fixture_root/scripts"
    cp -- "$helper_source" "$fixture_root/scripts/format-changed-cpp.sh"
    chmod 755 "$fixture_root/scripts/format-changed-cpp.sh"
    cp -- "$repo_root/.clang-format" "$fixture_root/.clang-format"
}

fixture_repo=$tmp_dir/repository
initialize_repository "$fixture_repo"
fixture_helper=$fixture_repo/scripts/format-changed-cpp.sh

printf '%s\n' 'int main() { return 0; }' >"$fixture_repo/modified.cpp"
printf '%s\n' '#pragma once' >"$fixture_repo/modified.hpp"
printf '%s\n' 'tracked text' >"$fixture_repo/notes.txt"
git -C "$fixture_repo" add -- .
git -C "$fixture_repo" commit -q -m 'initial fixture'

missing_mode_output=$tmp_dir/missing-mode.out
invalid_mode_output=$tmp_dir/invalid-mode.out
extra_mode_output=$tmp_dir/extra-mode.out
expect_status 2 "$missing_mode_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper"
expect_status 2 "$invalid_mode_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --invalid
expect_status 2 "$extra_mode_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --check extra
assert_contains 'Usage:' "$missing_mode_output"
assert_contains 'Usage:' "$invalid_mode_output"

no_candidates_output=$tmp_dir/no-candidates.out
expect_status 0 "$no_candidates_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --check
assert_contains 'No changed tracked C++ files.' "$no_candidates_output"

printf '%s\n' 'int  main( ){return  0;}' >"$fixture_repo/modified.cpp"
printf '%s\n' 'inline int  value( ){return  1;}' >"$fixture_repo/modified.hpp"
git -C "$fixture_repo" add -- modified.hpp

printf '%s\n' 'int  staged( ){return  2;}' >"$fixture_repo/staged-new.cpp"
git -C "$fixture_repo" add -- staged-new.cpp

spaced_cpp='directory with spaces/spaced file.cpp'
mkdir -p "$fixture_repo/directory with spaces"
printf '%s\n' 'int  spaced( ){return  3;}' >"$fixture_repo/$spaced_cpp"
git -C "$fixture_repo" add -- "$spaced_cpp"

option_hpp='-option.hpp'
printf '%s\n' 'inline int  option_value( ){return  4;}' \
    >"$fixture_repo/$option_hpp"
git -C "$fixture_repo" add -- "$option_hpp"

newline_cpp=$'line\nbreak.cpp'
printf '%s\n' 'int  newline_path( ){return  5;}' \
    >"$fixture_repo/$newline_cpp"
git -C "$fixture_repo" add -- "$newline_cpp"

printf '%s\n' 'tracked text changed' >"$fixture_repo/notes.txt"
printf '%s\n' 'int  untracked( ){return  6;}' \
    >"$fixture_repo/untracked.cpp"

snapshot_dir=$tmp_dir/snapshots
mkdir -p "$snapshot_dir"
cp -- "$fixture_repo/modified.cpp" "$snapshot_dir/modified.cpp"
cp -- "$fixture_repo/modified.hpp" "$snapshot_dir/modified.hpp"
cp -- "$fixture_repo/staged-new.cpp" "$snapshot_dir/staged-new.cpp"
cp -- "$fixture_repo/$spaced_cpp" "$snapshot_dir/spaced.cpp"
cp -- "$fixture_repo/$option_hpp" "$snapshot_dir/option.hpp"
cp -- "$fixture_repo/$newline_cpp" "$snapshot_dir/newline.cpp"
cp -- "$fixture_repo/notes.txt" "$snapshot_dir/notes.txt"
cp -- "$fixture_repo/untracked.cpp" "$snapshot_dir/untracked.cpp"

drift_output=$tmp_dir/drift-check.out
expect_failure "$drift_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --check

write_output=$tmp_dir/write.out
expect_status 0 "$write_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --write

assert_changed "$fixture_repo/modified.cpp" \
    "$snapshot_dir/modified.cpp" 'unstaged tracked .cpp'
assert_changed "$fixture_repo/modified.hpp" \
    "$snapshot_dir/modified.hpp" 'staged tracked .hpp'
assert_changed "$fixture_repo/staged-new.cpp" \
    "$snapshot_dir/staged-new.cpp" 'staged new .cpp'
assert_changed "$fixture_repo/$spaced_cpp" \
    "$snapshot_dir/spaced.cpp" 'space-containing .cpp path'
assert_changed "$fixture_repo/$option_hpp" \
    "$snapshot_dir/option.hpp" 'option-like .hpp path'
assert_changed "$fixture_repo/$newline_cpp" \
    "$snapshot_dir/newline.cpp" 'newline-containing .cpp path'
assert_unchanged "$fixture_repo/notes.txt" \
    "$snapshot_dir/notes.txt" 'tracked non-C++ file'
assert_unchanged "$fixture_repo/untracked.cpp" \
    "$snapshot_dir/untracked.cpp" 'untracked C++ file'

post_write_check_output=$tmp_dir/post-write-check.out
expect_status 0 "$post_write_check_output" \
    env CLANG_FORMAT="$clang_format" "$fixture_helper" --check

unborn_repo=$tmp_dir/unborn-repository
initialize_repository "$unborn_repo"
unborn_output=$tmp_dir/unborn.out
expect_failure "$unborn_output" \
    env CLANG_FORMAT="$clang_format" \
    "$unborn_repo/scripts/format-changed-cpp.sh" --check
assert_contains \
    'unable to detect changed tracked C++ files from HEAD.' \
    "$unborn_output"

printf '%s\n' 'format-changed-cpp-test: all checks passed'
