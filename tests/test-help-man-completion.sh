#!/bin/sh

set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'help-man-completion-test: %s\n' "$*" >&2
    exit 1
}

count_occurrences() {
    needle=$1
    file=$2
    NEEDLE=$needle awk '
        BEGIN { needle = ENVIRON["NEEDLE"] }
        {
            rest = $0
            while((position = index(rest, needle)) != 0) {
                count++
                rest = substr(rest, position + length(needle))
            }
        }
        END { print count + 0 }
    ' "$file"
}

assert_occurrence_count() {
    expected=$1
    needle=$2
    file=$3
    actual=$(count_occurrences "$needle" "$file")
    [ "$actual" -eq "$expected" ] ||
        fail "$file contains '$needle' $actual times; expected $expected."
}

assert_line_count() {
    expected=$1
    line=$2
    file=$3
    actual=$(grep -Fxc -- "$line" "$file" || true)
    [ "$actual" -eq "$expected" ] ||
        fail "$file contains exact line '$line' $actual times; expected $expected."
}

"$test_binary" -h > "$tmp_dir/help-short" 2>&1
"$test_binary" --help > "$tmp_dir/help-long" 2>&1
cmp -s "$tmp_dir/help-short" "$tmp_dir/help-long" ||
    fail "-h and --help output differ."
assert_occurrence_count 1 '-h, --help' "$tmp_dir/help-short"
assert_occurrence_count 1 '-V, --version' "$tmp_dir/help-short"

"$test_binary" -V > "$tmp_dir/version-short" 2>&1
"$test_binary" --version > "$tmp_dir/version-long" 2>&1
cmp -s "$tmp_dir/version-short" "$tmp_dir/version-long" ||
    fail "-V and --version output differ."
version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ "$(cat "$tmp_dir/version-short")" = "jpacker v$version" ] ||
    fail "version output does not match VERSION."

sed "s/@VERSION@/$version/g" "$repo_root/man/jpacker.8.in" > \
    "$tmp_dir/generated-man"
cmp -s "$tmp_dir/generated-man" "$repo_root/man/jpacker.8" ||
    fail "man/jpacker.8 does not match the formal generator output."
assert_occurrence_count 1 '.BR \-h , \ \-\-help' \
    "$repo_root/man/jpacker.8.in"
assert_occurrence_count 1 '.BR \-V , \ \-\-version' \
    "$repo_root/man/jpacker.8.in"

grep '^    opts=' "$repo_root/completions/jpacker_completion.bash" > \
    "$tmp_dir/bash-options"
assert_occurrence_count 1 ' -h ' "$tmp_dir/bash-options"
assert_occurrence_count 1 ' --help ' "$tmp_dir/bash-options"
assert_occurrence_count 1 ' -V ' "$tmp_dir/bash-options"
assert_occurrence_count 1 ' --version ' "$tmp_dir/bash-options"

sed -n '/^_jpacker_global_options=(/,/^)/p' \
    "$repo_root/completions/_jpacker" > "$tmp_dir/zsh-options"
assert_line_count 1 '    -h' "$tmp_dir/zsh-options"
assert_line_count 1 '    --help' "$tmp_dir/zsh-options"
assert_line_count 1 '    -V' "$tmp_dir/zsh-options"
assert_line_count 1 '    --version' "$tmp_dir/zsh-options"

sed -n '/^set -l jpacker_global_options \\/,/^$/p' \
    "$repo_root/completions/jpacker.fish" > "$tmp_dir/fish-options"
assert_occurrence_count 1 '    -h ' "$tmp_dir/fish-options"
assert_occurrence_count 1 '    --help ' "$tmp_dir/fish-options"
assert_occurrence_count 1 '    -V ' "$tmp_dir/fish-options"
assert_occurrence_count 1 '    --version ' "$tmp_dir/fish-options"

printf 'help-man-completion-test: all checks passed\n'
