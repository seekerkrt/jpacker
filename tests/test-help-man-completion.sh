#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: test-help-man-completion.sh CLI_LOCALIZATION_BINARY\n' >&2
    exit 2
fi

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

for command_name in python3 bash zsh fish localedef; do
    command -v "$command_name" >/dev/null 2>&1 ||
        fail "$command_name is required."
done

PYTHONDONTWRITEBYTECODE=1 \
    python3 "$repo_root/scripts/generate_completions.py" --check
bash -n "$repo_root/completions/moguet.bash"
zsh -n "$repo_root/completions/_moguet"
fish --no-execute "$repo_root/completions/moguet.fish"

locale_root=$tmp_dir/locale
test_home=$tmp_dir/home
mkdir -p "$locale_root" "$test_home"
localedef --no-archive -i en_US -f UTF-8 "$locale_root/en_US.UTF-8"

english_help=$tmp_dir/help-en
english_help_short=$tmp_dir/help-en-short
japanese_help=$tmp_dir/help-ja
japanese_help_short=$tmp_dir/help-ja-short
HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LC_ALL=C \
LANGUAGE= \
    "$test_binary" --help > "$english_help" 2>&1

HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LC_ALL=C \
LANGUAGE= \
    "$test_binary" -h > "$english_help_short" 2>&1
cmp -s "$english_help" "$english_help_short" ||
    fail 'English -h and --help output differ.'

HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LOCPATH=$locale_root \
LANG=en_US.UTF-8 \
LC_ALL=en_US.UTF-8 \
LANGUAGE=ja \
    "$test_binary" --help > "$japanese_help" 2>&1

HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LOCPATH=$locale_root \
LANG=en_US.UTF-8 \
LC_ALL=en_US.UTF-8 \
LANGUAGE=ja \
    "$test_binary" -h > "$japanese_help_short" 2>&1
cmp -s "$japanese_help" "$japanese_help_short" ||
    fail 'Japanese -h and --help output differ.'

version_short=$tmp_dir/version-short
version_long=$tmp_dir/version-long
HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LC_ALL=C \
LANGUAGE= \
    "$test_binary" -V > "$version_short" 2>&1
HOME=$test_home \
XDG_CONFIG_HOME=$tmp_dir/config \
XDG_STATE_HOME=$tmp_dir/state \
XDG_CACHE_HOME=$tmp_dir/cache \
LC_ALL=C \
LANGUAGE= \
    "$test_binary" --version > "$version_long" 2>&1
cmp -s "$version_short" "$version_long" ||
    fail '-V and --version output differ.'
version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ "$(cat "$version_short")" = "Moguet v$version" ] ||
    fail 'version output does not match VERSION.'

PYTHONDONTWRITEBYTECODE=1 \
python3 "$repo_root/scripts/check_public_documentation.py" \
    --help-en "$english_help" \
    --help-ja "$japanese_help"

[ ! -e "$tmp_dir/config" ] &&
    [ ! -e "$tmp_dir/state" ] &&
    [ ! -e "$tmp_dir/cache" ] ||
    fail 'help-only checks created XDG consumer directories.'

printf 'help-man-completion-test: all checks passed\n'
