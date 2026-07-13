#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
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

port_file=$tmp_dir/port
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-conflicts-replaces.json" "$port_file" &
server_pid=$!

attempt=0
while [ ! -s "$port_file" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -gt 100 ]; then
        echo "fixture server did not start" >&2
        exit 1
    fi
    sleep 0.05
done

port=$(cat "$port_file")
export PATH=$repo_root/tests/stubs:/usr/bin:/bin
export JPACKER_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:$port/rpc/

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACMAN_EXIT_CODE=0
    export JPACKER_TEST_SUDO_EXIT_CODE=0
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_MAKEPKG_EXIT_CODE
}

run_ok() {
    : > "$command_log"
    if ! "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    if "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_contains() {
    pattern=$1
    file=$2
    if ! grep -F -- "$pattern" "$file" >/dev/null; then
        echo "missing expected output: $pattern" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_command() {
    expected=$1
    if ! grep -Fx -- "$expected" "$command_log" >/dev/null; then
        echo "missing expected command: $expected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_count() {
    expected=$1
    expected_count=$2
    actual_count=$(grep -Fxc -- "$expected" "$command_log" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected command count for: $expected" >&2
        echo "expected $expected_count, got $actual_count" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_only_command() {
    expected=$1
    assert_command "$expected"
    if [ "$(wc -l < "$command_log")" -ne 1 ]; then
        echo "unexpected additional command(s)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_absent() {
    unexpected=$1
    if grep -Fx -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "external command ran before CLI validation completed" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_mutation_commands() {
    if grep -E '^(sudo|git|makepkg) |^pacman -S( |$)' "$command_log" >/dev/null; then
        echo "CLI validation allowed an external mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_only_sudo_command() {
    expected=$1
    assert_command_count "$expected" 1
    sudo_count=$(grep -c '^sudo ' "$command_log" || true)
    if [ "$sudo_count" -ne 1 ]; then
        echo "unexpected additional sudo command(s)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_exact() {
    case_name=$1
    expected=$2
    shift 2
    setup_case "$case_name"
    run_ok "$@"
    assert_only_command "$expected"
}

# Matrix A: option value待ちはglobal option認識より優先する。
run_exact value-root-rmdeps \
    "pacman -Q --root --rmdeps filesystem" \
    -Q --root --rmdeps filesystem
run_exact value-config-noconfirm \
    "pacman -Q --config --noconfirm filesystem" \
    -Q --config --noconfirm filesystem
run_exact value-dbpath-rebuild \
    "pacman -Q --dbpath --rebuild filesystem" \
    -Q --dbpath --rebuild filesystem
run_exact value-cachedir-cleanbuild \
    "pacman -Q --cachedir --cleanbuild filesystem" \
    -Q --cachedir --cleanbuild filesystem
run_exact value-short-dbpath-noedit \
    "pacman -Q -b --noedit filesystem" \
    -Q -b --noedit filesystem
run_exact value-short-root-nodiff \
    "pacman -Q -r --nodiff filesystem" \
    -Q -r --nodiff filesystem

# Matrix B: semantic `--`後は全tokenをopaque operandとして保持する。
for global_option in --rmdeps --noconfirm --rebuild --cleanbuild --noedit --nodiff; do
    case_name=opaque-${global_option#--}
    run_exact "$case_name" "sudo pacman -U -- $global_option" -U -- "$global_option"
done

# Matrix C/D: 通常位置のglobalだけを消費し、generated optionはoperation直後へ1件置く。
run_exact global-leading-noconfirm \
    "pacman -Q --noconfirm filesystem" \
    --noconfirm -Q filesystem
run_exact global-trailing-noconfirm \
    "pacman -Q --noconfirm filesystem" \
    -Q filesystem --noconfirm
run_exact global-build-options-do-not-leak \
    "pacman -Q filesystem" \
    --noedit --nodiff --rebuild --cleanbuild --rmdeps -Q filesystem
run_exact generated-before-marker-leading \
    "pacman -Q --noconfirm -- filesystem" \
    --noconfirm -Q -- filesystem
run_exact generated-before-marker-trailing \
    "pacman -Q --noconfirm -- filesystem" \
    -Q --noconfirm -- filesystem
run_exact generated-distinct-from-option-value \
    "pacman -Q --noconfirm --root --noconfirm filesystem" \
    --noconfirm -Q --root --noconfirm filesystem
run_exact generated-distinct-from-opaque-token \
    "pacman -Q --noconfirm -- filesystem --noconfirm" \
    --noconfirm -Q -- filesystem --noconfirm

# Matrix E: separated formだけが次tokenをvalueとして消費する。
run_exact separated-root \
    "pacman -Q --root value filesystem" \
    -Q --root value filesystem
run_exact attached-root \
    "pacman -Q --root=value filesystem" \
    -Q --root=value filesystem
run_exact separated-config \
    "pacman -Q --config value filesystem" \
    -Q --config value filesystem
run_exact attached-config \
    "pacman -Q --config=value filesystem" \
    -Q --config=value filesystem

# Matrix F: tracked value optionのmissing valueはexternal command前に停止する。
missing_index=0
for missing_option in --root --config -b -r; do
    missing_index=$((missing_index + 1))
    setup_case missing-value-$missing_index
    run_fail -Q "$missing_option"
    assert_contains "Missing value for option" "$output_file"
    assert_contains "$missing_option" "$output_file"
    assert_command_log_empty
done

# Matrix G: value位置の`--`はmarkerではなくvalue。次の`--`だけがmarkerになる。
run_exact marker-as-root-value \
    "pacman -Q --root -- filesystem" \
    -Q --root -- filesystem
run_exact marker-after-root-value \
    "pacman -Q --root -- -- filesystem" \
    -Q --root -- -- filesystem

# Matrix H: direct route / file routeでも非global argvの相対順を維持する。
run_exact query-relative-order \
    "pacman -Q target-a --config custom.conf target-b" \
    -Q target-a --config custom.conf target-b
run_exact file-relative-order \
    "pacman -F usr/bin/a --config custom.conf usr/bin/b" \
    -F usr/bin/a --config custom.conf usr/bin/b
run_exact remove-relative-order \
    "sudo pacman -R target-a --config custom.conf target-b" \
    -R target-a --config custom.conf target-b
run_exact database-relative-order \
    "sudo pacman -D target-a --config custom.conf target-b" \
    -D target-a --config custom.conf target-b

# Help/versionはleading global後のoperation位置だけで扱う。
setup_case help-after-global
run_ok --noedit --help
assert_contains "USAGE" "$output_file"
assert_command_log_empty

setup_case version-after-global
run_ok --noedit --version
assert_contains "jpacker v" "$output_file"
assert_command_log_empty

setup_case help-operation
run_ok --help
assert_contains "USAGE" "$output_file"
assert_command_log_empty

setup_case version-operation
run_ok --version
assert_contains "jpacker v" "$output_file"
assert_command_log_empty

run_exact help-as-option-value \
    "pacman -Q --root --help filesystem" \
    -Q --root --help filesystem
run_exact version-as-option-value \
    "pacman -Q --root --version filesystem" \
    -Q --root --version filesystem
run_exact help-as-opaque-operand \
    "sudo pacman -U -- --help" \
    -U -- --help
run_exact version-as-opaque-operand \
    "sudo pacman -U -- --version" \
    -U -- --version

# Matrix I: official transactionはAUR targetだけを元indexで除外し、残りの順序を保つ。
setup_case official-sync-order
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a official-b'
run_ok -S official-a --config custom.conf official-b
assert_only_sudo_command "sudo pacman -S official-a --config custom.conf official-b"
assert_command_absent "sudo pacman -S --config custom.conf official-a official-b"

setup_case official-sync-generated-option
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a official-b'
run_ok --noconfirm -S official-a --config custom.conf official-b
assert_only_sudo_command "sudo pacman -S --noconfirm official-a --config custom.conf official-b"

setup_case mixed-sync-unsupported
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a official-c'
run_fail -S official-a --config custom.conf clean-root official-c
assert_contains "Unsupported pacman option for AUR/source-build target: --config" "$output_file"
assert_no_mutation_commands

setup_case mixed-sync-supported
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a official-c'
run_fail --noconfirm --noedit -S official-a clean-root official-c
assert_only_sudo_command "sudo pacman -S --noconfirm official-a official-c"
assert_command_absent "sudo pacman -S --noconfirm official-a clean-root official-c"

# 同名option valueとAUR targetを文字列一致でまとめて削除しない。
setup_case info-removes-target-by-index
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a'
run_ok -Si official-a --config clean-root clean-root
assert_command_count "pacman -Si official-a --config clean-root" 1
assert_command_absent "pacman -Si official-a --config"

# Matrix J: 通常位置のglobalはAUR/makepkgへ反映し、value/opaque位置では反映しない。
setup_case aur-global-options
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-only'
run_fail --noedit --nodiff --noconfirm --rebuild --cleanbuild --rmdeps -S clean-root
assert_command_count "makepkg -sic --noconfirm -f -C -r" 1

setup_case aur-trailing-rmdeps
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-only'
run_fail -S clean-root --rmdeps --noedit --noconfirm
assert_command_count "makepkg -sic --noconfirm -r" 1

setup_case aur-global-name-as-option-value
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-only'
run_fail -S --root --rmdeps clean-root
assert_contains "Unsupported pacman option for AUR/source-build target: --root" "$output_file"
assert_no_mutation_commands

setup_case aur-global-name-as-opaque-target
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-only'
run_fail -S -- --rmdeps
assert_contains "Invalid package name: --rmdeps" "$output_file"
assert_command_log_empty

echo "CLI parser integration tests: all checks passed"
