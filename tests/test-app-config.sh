#!/bin/sh
set -eu

module_test_binary=$1
integration_test_binary=$2
repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"

export PATH="$repo_root/tests/stubs:/usr/bin:/bin"
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"

tmp_dir=$(mktemp -d)
server_pid=

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    echo "$*" >&2
    exit 1
}

assert_line() {
    expected=$1
    file=$2
    if ! grep -Fx -- "$expected" "$file" >/dev/null; then
        echo "missing expected line: $expected" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_contains() {
    expected=$1
    file=$2
    if ! grep -F -- "$expected" "$file" >/dev/null; then
        echo "missing expected content: $expected" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_not_contains() {
    unexpected=$1
    file=$2
    if grep -F -- "$unexpected" "$file" >/dev/null; then
        echo "unexpected content: $unexpected" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_app_config() {
    file=$1
    pkgbuild=$2
    diff=$3
    mode=$4
    no_confirm=$5
    rm_deps=$6
    editor=$7

    assert_line "SCHEMA_VERSION=1" "$file"
    assert_line "REVIEW_PKGBUILD=$pkgbuild" "$file"
    assert_line "REVIEW_DIFF=$diff" "$file"
    assert_line "BUILD_MODE=$mode" "$file"
    assert_line "NOCONFIRM=$no_confirm" "$file"
    assert_line "RMDEPS=$rm_deps" "$file"
    assert_line "EDITOR=$editor" "$file"
    if [ "$(wc -l < "$file")" -ne 7 ]; then
        echo "AppConfig output did not contain exactly seven fields" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

run_module_ok() {
    output_file=$1
    shift
    if ! "$module_test_binary" "$@" > "$output_file" 2>&1; then
        echo "app_config module test command failed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        exit 1
    fi
}

module_output=$tmp_dir/module-output
unset EDITOR
unset VISUAL
run_module_ok "$module_output" defaults
assert_app_config "$module_output" prompt prompt normal false false nano

run_module_ok "$module_output" projection
assert_app_config "$module_output" skip skip clean true true nano

# VISUALはEDITORより優先する。
export VISUAL='visual-editor --visual-option'
export EDITOR='editor-editor --editor-option'
run_module_ok "$module_output" projection
assert_app_config \
    "$module_output" skip skip clean true true \
    'visual-editor --visual-option'

# VISUAL未指定時はEDITORを使う。
unset VISUAL
run_module_ok "$module_output" projection
assert_app_config \
    "$module_output" skip skip clean true true \
    'editor-editor --editor-option'

# EmptyなVISUALも未指定としてEDITORへfallbackする。
VISUAL=
export VISUAL
run_module_ok "$module_output" projection
assert_app_config \
    "$module_output" skip skip clean true true \
    'editor-editor --editor-option'

# 両方emptyならbuilt-inのnanoへfallbackする。
EDITOR=
export EDITOR
run_module_ok "$module_output" projection
assert_app_config "$module_output" skip skip clean true true nano

# Source-build consumerをlocalhost fixtureだけで観測する。
port_file=$tmp_dir/port
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-conflicts-replaces.json" "$port_file" &
server_pid=$!

attempt=0
while [ ! -s "$port_file" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -gt 100 ]; then
        fail "fixture server did not start"
    fi
    sleep 0.05
done

port=$(sed -n '1p' "$port_file")
export MOGUET_TEST_AUR_RPC_BASE_URL="http://127.0.0.1:$port/rpc/"

setup_integration_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output
    config_root=$case_dir/xdg-config
    config_file=$config_root/moguet/config.toml

    mkdir -p \
        "$case_dir/home" \
        "$case_dir/xdg-state" \
        "$case_dir/xdg-cache" \
        "$case_dir/package.build"
    : > "$command_log"
    : > "$output_file"

    export HOME="$case_dir/home"
    export XDG_CONFIG_HOME="$config_root"
    export XDG_STATE_HOME="$case_dir/xdg-state"
    export XDG_CACHE_HOME="$case_dir/xdg-cache"
    export MOGUET_TEST_COMMAND_LOG="$command_log"
    export MOGUET_TEST_PACKAGE_BUILD_DIR="$case_dir/package.build"
    export MOGUET_TEST_PACMAN_EXIT_CODE=1
    export MOGUET_TEST_SUDO_EXIT_CODE=0
    export MOGUET_TEST_MAKEPKG_EXIT_CODE=0

    unset MOGUET_TEST_CONFIG_FILE
    unset MOGUET_TEST_APP_CONFIG_CASE
    unset MOGUET_TEST_RELEASE_STATE_LOG_BEFORE_DISPATCH
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_PACMAN_REPO_PACKAGES
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
    unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
    unset EDITOR
    unset VISUAL
}

write_full_user_config() {
    mkdir -p "$(dirname "$config_file")"
    {
        printf '%s\n' 'schema_version = 1'
        printf '%s\n' '[review]'
        printf '%s\n' 'pkgbuild = "skip"'
        printf '%s\n' 'diff = "skip"'
        printf '%s\n' '[build]'
        printf '%s\n' 'mode = "clean"'
    } > "$config_file"
}

run_integration_ok() {
    : > "$command_log"
    if ! "$integration_test_binary" "$@" > "$output_file" 2>&1; then
        echo "integration command failed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

run_integration_fail() {
    : > "$command_log"
    if "$integration_test_binary" "$@" > "$output_file" 2>&1; then
        echo "integration command unexpectedly succeeded: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

assert_command() {
    expected=$1
    if ! grep -Fx -- "$expected" "$command_log" >/dev/null; then
        echo "missing expected command: $expected" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

assert_command_absent() {
    unexpected=$1
    if grep -F -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command content: $unexpected" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "unexpected external command ran before config rejection" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

assert_only_command() {
    expected=$1
    assert_command "$expected"
    if [ "$(wc -l < "$command_log")" -ne 1 ]; then
        echo "unexpected additional command(s)" >&2
        sed -n '1,240p' "$command_log" >&2
        exit 1
    fi
}

assert_no_runtime_storage() {
    if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
        fail "config rejection initialized the default state directory"
    fi
    if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ]; then
        fail "config rejection initialized the cache directory"
    fi
}

# Missing configはXDG config treeを作らず、built-in defaultで通常経路へ進む。
setup_integration_case missing-config
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_integration_ok -Q filesystem
assert_only_command "pacman -Q filesystem"
if [ -e "$config_root" ] || [ -L "$config_root" ]; then
    fail "missing config resolution created the XDG config tree"
fi
state_log=$XDG_STATE_HOME/moguet/moguet.log
if [ ! -f "$state_log" ]; then
    fail "missing config did not continue to normal state log initialization"
fi
assert_contains "[INFO] Started Moguet v" "$state_log"
if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ]; then
    fail "pacman-only missing-config case initialized Moguet cache"
fi

# XDG user configのtyped valuesをreal review/build consumerへ渡す。
setup_integration_case xdg-precedence
write_full_user_config
cp "$config_file" "$case_dir/config.before"
run_integration_ok --noconfirm -S --aur clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg -sc --noconfirm -C"
assert_contains "Skipping PKGBUILD/.install review (--noedit)." "$output_file"
assert_not_contains "Review target: PKGBUILD" "$output_file"
cmp -s "$case_dir/config.before" "$config_file" ||
    fail "production load modified the user config"

# CLI final valueはuser configを両方向に反転し、last-value semanticsを使わない。
run_integration_ok \
    --edit --diff --noconfirm --build-mode=normal -S --aur clean-root
assert_command "git diff --quiet HEAD..origin/main"
assert_contains "Review target: PKGBUILD" "$output_file"
assert_not_contains "Skipping PKGBUILD/.install review (--noedit)." "$output_file"
assert_command "makepkg -sc --noconfirm"
assert_command_absent "makepkg -sc --noconfirm -C"
assert_command_absent "makepkg -sc --noconfirm -f"

# XDG_CONFIG_HOME unset時はHOME fallbackのconfig.tomlをauthorityにする。
setup_integration_case home-fallback
unset XDG_CONFIG_HOME
config_file=$HOME/.config/moguet/config.toml
mkdir -p "$(dirname "$config_file")"
{
    printf '%s\n' 'schema_version = 1'
    printf '%s\n' '[review]'
    printf '%s\n' 'pkgbuild = "skip"'
    printf '%s\n' '[build]'
    printf '%s\n' 'mode = "rebuild"'
} > "$config_file"
run_integration_ok --noconfirm -S --aur clean-root
assert_contains "Skipping PKGBUILD/.install review (--noedit)." "$output_file"
assert_command "makepkg -sc --noconfirm -f"

verify_config_rejection() {
    expected=$1
    cp "$config_file" "$case_dir/config.before"
    run_integration_fail -Q filesystem
    assert_contains "$config_file" "$output_file"
    assert_contains "$expected" "$output_file"
    assert_command_log_empty
    assert_no_runtime_storage
    cmp -s "$case_dir/config.before" "$config_file" ||
        fail "rejected config was modified"
}

# Syntax、unknown、future、legacy KEY=VALUEはwarning fallbackせず停止する。
setup_integration_case invalid-syntax
mkdir -p "$(dirname "$config_file")"
{
    printf '%s\n' 'schema_version = 1'
    printf '%s\n' '[review'
} > "$config_file"
verify_config_rejection "TOML parse error"

setup_integration_case unknown-key
mkdir -p "$(dirname "$config_file")"
{
    printf '%s\n' 'schema_version = 1'
    printf '%s\n' 'unexpected = true'
} > "$config_file"
verify_config_rejection "top-level key 'unexpected'"

setup_integration_case future-schema
mkdir -p "$(dirname "$config_file")"
printf '%s\n' 'schema_version = 2' > "$config_file"
verify_config_rejection "unsupported schema version 2"

setup_integration_case legacy-key-value
mkdir -p "$(dirname "$config_file")"
printf '%s\n' 'NOEDIT=true' > "$config_file"
verify_config_rejection "top-level key 'NOEDIT'"

# Invalid XDG authorityはHOMEへ推測fallbackせず、storage/command前に停止する。
setup_integration_case invalid-xdg-config-home
XDG_CONFIG_HOME=relative/config-secret
export XDG_CONFIG_HOME
run_integration_fail -Q filesystem
assert_contains "XDG_CONFIG_HOME must be an absolute path" "$output_file"
assert_not_contains "relative/config-secret" "$output_file"
assert_command_log_empty
assert_no_runtime_storage

# Parse failureもloaded configや途中までのCLI overrideをglobalへpublishしない。
setup_integration_case parse-failure
mkdir -p "$(dirname "$config_file")"
printf '%s\n' 'schema_version = 1' > "$config_file"
export MOGUET_TEST_APP_CONFIG_CASE=parse-failure-cli-overrides
run_integration_ok
unset MOGUET_TEST_APP_CONFIG_CASE
assert_contains "Missing value for option --config" "$output_file"
assert_command_log_empty
assert_no_runtime_storage

echo "app config production cutover tests passed"
