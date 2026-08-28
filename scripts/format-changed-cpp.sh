#!/usr/bin/env bash

set -euo pipefail

usage() {
    printf 'Usage: %s (--write|--check)\n' "${0##*/}" >&2
}

fail() {
    printf 'format-changed-cpp: %s\n' "$*" >&2
    exit 1
}

if (( $# != 1 )); then
    usage
    exit 2
fi

mode=$1
case "$mode" in
    --write|--check)
        ;;
    *)
        usage
        exit 2
        ;;
esac

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if ! repo_root=$(git -C "$script_dir/.." rev-parse --show-toplevel 2>/dev/null); then
    fail 'unable to resolve the repository root.'
fi

if [[ ! -f "$repo_root/.clang-format" ]]; then
    fail 'repository .clang-format is missing.'
fi

cd "$repo_root"

if ! changed_path_list=$(mktemp); then
    fail 'unable to create a temporary changed-path list.'
fi

cleanup() {
    rm -f -- "$changed_path_list"
}
trap cleanup EXIT HUP INT TERM

if ! git diff --name-only -z --no-renames --diff-filter=ACMR \
    HEAD -- '*.cpp' '*.hpp' >"$changed_path_list"; then
    fail 'unable to detect changed tracked C++ files from HEAD.'
fi

mapfile -d '' -t changed_cpp <"$changed_path_list"
if (( ${#changed_cpp[@]} == 0 )); then
    printf '%s\n' 'No changed tracked C++ files.'
    exit 0
fi

for path in "${changed_cpp[@]}"; do
    if [[ ! -f "$path" || -L "$path" ]]; then
        fail "changed C++ path is not a regular repository file: $path"
    fi
done

clang_format=${CLANG_FORMAT:-clang-format}
if ! command -v -- "$clang_format" >/dev/null 2>&1; then
    fail "clang-format command not found: $clang_format"
fi

case "$mode" in
    --write)
        printf 'Formatting %d changed tracked C++ file(s).\n' \
            "${#changed_cpp[@]}"
        "$clang_format" --style=file -i -- "${changed_cpp[@]}"
        ;;
    --check)
        printf 'Checking %d changed tracked C++ file(s).\n' \
            "${#changed_cpp[@]}"
        "$clang_format" --style=file --dry-run --Werror -- \
            "${changed_cpp[@]}"
        ;;
esac
