#!/bin/sh
set -eu

test_binary=$1
source_install_test_binary=$2
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

if ! command -v script >/dev/null 2>&1; then
    echo "script(1) is required for source/maintenance command tests" >&2
    exit 1
fi

port_file=$tmp_dir/port
request_log=$tmp_dir/aur-requests.log
: > "$request_log"
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-conflicts-replaces.json" \
    "$port_file" "$request_log" &
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
export PATH=$repo_root/tests/stubs/source-maintenance:$repo_root/tests/stubs:/usr/bin:/bin
export JPACKER_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:$port/rpc/

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output
    preference_dir=$case_dir/package.build
    cache_root=$case_dir/xdg-cache/jpacker
    sudo_failures=$case_dir/sudo-failures
    config_file=$case_dir/jpacker.conf

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$preference_dir"
    : > "$command_log"
    : > "$request_log"
    : > "$sudo_failures"
    {
        printf 'EDITOR=jpacker-test-editor --config\n'
        printf 'LOGFILE=%s\n' "$case_dir/jpacker.log"
    } > "$config_file"

    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$preference_dir
    export JPACKER_TEST_CONFIG_FILE=$config_file
    export JPACKER_TEST_SOURCE_MAINTENANCE_SUDO_MUTATE=1
    export JPACKER_TEST_SOURCE_MAINTENANCE_FAIL_EXACT_FILE=$sudo_failures
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=0
    export JPACKER_TEST_MAKEPKG_EXIT_CODE=0

    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_PACMAN_Q_OUTPUT
    unset JPACKER_TEST_PACMAN_Q_EXIT_CODE
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_APP_CONFIG_CASE
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_SOURCE_MAINTENANCE_FAIL_SUBSTRING
    unset JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_PATH
    unset JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_TARGET
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_SNAPSHOT_FILE
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_FAIL_SUBSTRING
    unset EDITOR
}

run_ok() {
    : > "$command_log"
    : > "$request_log"
    if ! "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    : > "$request_log"
    if "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_clean_tty_ok() {
    answer=$1
    : > "$command_log"
    if ! printf '%s\n' "$answer" |
        script -qec "$test_binary clean" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive clean to succeed with answer: $answer" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_clean_tty_fail() {
    answer=$1
    : > "$command_log"
    if printf '%s\n' "$answer" |
        script -qec "$test_binary clean" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive clean to fail with answer: $answer" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_source_ok() {
    : > "$command_log"
    if ! "$source_install_test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected source-install scenario to succeed: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_source_fail() {
    : > "$command_log"
    if "$source_install_test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected source-install scenario to fail: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_contains() {
    pattern=$1
    file=$2
    if ! grep -F -- "$pattern" "$file" >/dev/null; then
        echo "missing expected output: $pattern" >&2
        sed -n '1,260p' "$file" >&2
        exit 1
    fi
}

assert_not_contains() {
    pattern=$1
    file=$2
    if grep -F -- "$pattern" "$file" >/dev/null; then
        echo "unexpected output: $pattern" >&2
        sed -n '1,260p' "$file" >&2
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

assert_command_absent() {
    unexpected=$1
    if grep -Fx -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_content_absent() {
    unexpected=$1
    if grep -F -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command content: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_at() {
    line_number=$1
    expected=$2
    actual=$(sed -n "${line_number}p" "$command_log")
    if [ "$actual" != "$expected" ]; then
        echo "unexpected command at line $line_number: $actual" >&2
        echo "expected: $expected" >&2
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

assert_total_command_count() {
    expected=$1
    actual=$(wc -l < "$command_log")
    if [ "$actual" -ne "$expected" ]; then
        echo "unexpected command count: $actual (expected $expected)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_before() {
    first=$1
    second=$2
    first_line=$(grep -nFx -- "$first" "$command_log" | sed -n '1s/:.*//p')
    second_line=$(grep -nFx -- "$second" "$command_log" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected command order: $first -> $second" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_output_before() {
    first=$1
    second=$2
    file=$3
    first_line=$(grep -nF -- "$first" "$file" | sed -n '1s/:.*//p')
    second_line=$(grep -nF -- "$second" "$file" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected output order: $first -> $second" >&2
        sed -n '1,260p' "$file" >&2
        exit 1
    fi
}

assert_path_absent() {
    path=$1
    if [ -e "$path" ] || [ -L "$path" ]; then
        echo "expected path to be absent: $path" >&2
        exit 1
    fi
}

assert_file_equals() {
    expected_file=$1
    actual_file=$2
    if ! cmp -s "$expected_file" "$actual_file"; then
        echo "unexpected file content: $actual_file" >&2
        diff -u "$expected_file" "$actual_file" >&2 || true
        exit 1
    fi
}

# P0-1: build handler parsing, validation, catch boundary, and source request mapping.
setup_case build-missing-argument
run_fail build
assert_contains "Usage: jpacker build <pkg> [VAR=VAL...]" "$output_file"
assert_total_command_count 0

setup_case build-environment-without-package
run_fail build FIRST=one SECOND=two
assert_contains "No package specified." "$output_file"
assert_total_command_count 0

setup_case build-invalid-environment
run_fail build 1INVALID=value clean-root
assert_contains "Invalid environment assignment: 1INVALID=value" "$output_file"
assert_not_contains "Build Error:" "$output_file"
assert_total_command_count 0

setup_case build-environment-order
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
run_ok --noedit --nodiff --noconfirm build \
    FIRST=one clean-root "SECOND=two words" FIRST=last ignored
assert_contains "Ignoring extra arg 'ignored'" "$output_file"
assert_contains "Applying custom build flags: FIRST='one' SECOND='two words' FIRST='last' " "$output_file"
assert_command "pacman -Si clean-root"
assert_command "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git clean-root"
assert_command "makepkg -sic --noconfirm"

setup_case build-resolve-failure
run_fail build missing-source-package
assert_contains "Build Error: Package not found in repos or AUR: missing-source-package" "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case build-split-guard
run_fail build split-child
assert_contains "Build Error: Cannot build/install split AUR package split-child from PackageBase split-base" "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case build-direct-split-priority
run_fail build split-metadata-ambiguous-root
assert_contains "Build Error: Cannot build/install split AUR package split-metadata-ambiguous-root from PackageBase split-metadata-ambiguous-base" "$output_file"
assert_not_contains "ambiguous providers" "$output_file"
assert_not_contains "conflicts/replaces metadata" "$output_file"

setup_case sync-plan-metadata-before-split
run_fail -S --aur split-metadata-root
assert_contains "conflicts/replaces metadata requires manual review" "$output_file"
assert_not_contains "split package install target selection is not implemented" "$output_file"

setup_case sync-plan-provider-before-metadata-split
run_fail -S --aur split-metadata-ambiguous-root
assert_contains "ambiguous providers" "$output_file"
assert_not_contains "conflicts/replaces metadata requires manual review" "$output_file"
assert_not_contains "split package install target selection is not implemented" "$output_file"

setup_case build-execution-failure
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
export JPACKER_TEST_MAKEPKG_EXIT_CODE=42
run_fail --noedit --nodiff build clean-root
assert_contains "Build Error: Build failed." "$output_file"
assert_command "makepkg -sic"

echo "  ok: P0-1 cmd_build"

# P0-2: add-src is a streaming mutation with cumulative assignment scope.
setup_case add-src-streaming
run_ok add-src alpha beta FIRST=one gamma SECOND=two
assert_command_at 1 "sudo touch $preference_dir/alpha"
assert_command_at 2 "sudo touch $preference_dir/beta"
assert_command_at 3 "sudo tee -a $preference_dir/alpha"
assert_command_at 4 "sudo tee -a $preference_dir/beta"
assert_command_at 5 "sudo touch $preference_dir/gamma"
assert_command_at 6 "sudo tee -a $preference_dir/alpha"
assert_command_at 7 "sudo tee -a $preference_dir/beta"
assert_command_at 8 "sudo tee -a $preference_dir/gamma"
assert_contains "Running: printf '%s\\n' 'FIRST=one' | sudo tee -a '$preference_dir/alpha' > /dev/null" "$output_file"
printf 'FIRST=one\nSECOND=two\n' > "$case_dir/alpha.expected"
printf 'FIRST=one\nSECOND=two\n' > "$case_dir/beta.expected"
printf 'SECOND=two\n' > "$case_dir/gamma.expected"
assert_file_equals "$case_dir/alpha.expected" "$preference_dir/alpha"
assert_file_equals "$case_dir/beta.expected" "$preference_dir/beta"
assert_file_equals "$case_dir/gamma.expected" "$preference_dir/gamma"
entry_count=$(find "$preference_dir" -maxdepth 1 -type f | wc -l)
if [ "$entry_count" -ne 3 ]; then
    echo "add-src did not preserve one preference file per package" >&2
    exit 1
fi

setup_case add-src-leading-assignment
run_fail add-src FIRST=one alpha SECOND=two
assert_contains "Environment assignment requires a preceding package: FIRST=one" "$output_file"
assert_command_at 1 "sudo touch $preference_dir/alpha"
assert_command_at 2 "sudo tee -a $preference_dir/alpha"
printf 'SECOND=two\n' > "$case_dir/alpha.expected"
assert_file_equals "$case_dir/alpha.expected" "$preference_dir/alpha"

setup_case add-src-package-failure
printf 'touch %s\n' "$preference_dir/failed" > "$sudo_failures"
run_fail add-src alpha failed beta FLAGS=value
assert_contains "Failed to add failed" "$output_file"
assert_command "sudo touch $preference_dir/failed"
assert_command "sudo tee -a $preference_dir/alpha"
assert_command "sudo tee -a $preference_dir/beta"
assert_command_content_absent "tee -a $preference_dir/failed"
assert_path_absent "$preference_dir/failed"
printf 'FLAGS=value\n' > "$case_dir/alpha.expected"
assert_file_equals "$case_dir/alpha.expected" "$preference_dir/alpha"
assert_file_equals "$case_dir/alpha.expected" "$preference_dir/beta"

echo "  ok: P0-2 cmd_add_src"

# P0-3: edit-src temp lifecycle, editor precedence, and target-local continuation.
setup_case edit-src-config-editor
printf 'CFLAGS=-Oexisting\n' > "$preference_dir/alpha"
printf 'CFLAGS=-Oexisting\n' > "$case_dir/existing.expected"
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_SNAPSHOT_FILE=$case_dir/editor.snapshot
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='LDFLAGS=-Wl,--as-needed'
run_ok edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
case $editor_command in
    "jpacker-test-editor --config /tmp/jpacker-edit-src-alpha."??????)
        ;;
    *)
        echo "unexpected config editor command: $editor_command" >&2
        cat "$command_log" >&2
        exit 1
        ;;
esac
edit_temp_path=${editor_command##* }
assert_command_at 2 "sudo install -Dm644 $edit_temp_path $preference_dir/alpha"
assert_file_equals "$case_dir/existing.expected" "$case_dir/editor.snapshot"
printf 'CFLAGS=-Oexisting\nLDFLAGS=-Wl,--as-needed\n' > "$case_dir/edited.expected"
assert_file_equals "$case_dir/edited.expected" "$preference_dir/alpha"
assert_path_absent "$edit_temp_path"

setup_case edit-src-environment-editor
printf 'CFLAGS=-Oexisting\n' > "$preference_dir/alpha"
export EDITOR='jpacker-test-editor --environment'
run_ok edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
case $editor_command in
    "jpacker-test-editor --environment /tmp/jpacker-edit-src-alpha."??????)
        ;;
    *)
        echo "EDITOR did not override configured editor: $editor_command" >&2
        cat "$command_log" >&2
        exit 1
        ;;
esac

setup_case edit-src-editor-failure
printf 'CFLAGS=-Oexisting\n' > "$preference_dir/alpha"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_FAIL_SUBSTRING=jpacker-edit-src-alpha.
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "Editor failed for $preference_dir/alpha" "$output_file"
assert_path_absent "$edit_temp_path"
assert_command_content_absent "sudo install"

setup_case edit-src-install-failure
printf 'CFLAGS=-Oexisting\n' > "$preference_dir/alpha"
printf 'CFLAGS=-Oexisting\n' > "$case_dir/existing.expected"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='EDITED=yes'
export JPACKER_TEST_SOURCE_MAINTENANCE_FAIL_SUBSTRING='install -Dm644'
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "edited file kept at $edit_temp_path" "$output_file"
if [ ! -f "$edit_temp_path" ]; then
    echo "edit-src removed the retained file after install failure" >&2
    exit 1
fi
assert_contains "EDITED=yes" "$edit_temp_path"
assert_file_equals "$case_dir/existing.expected" "$preference_dir/alpha"
rm -f "$edit_temp_path"

setup_case edit-src-continues
printf 'FIRST=original\n' > "$preference_dir/first"
printf 'SECOND=original\n' > "$preference_dir/second"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='EDITED=yes'
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_FAIL_SUBSTRING=jpacker-edit-src-first.
run_fail edit-src first second
first_editor_command=$(sed -n '1p' "$command_log")
second_editor_command=$(sed -n '2p' "$command_log")
first_temp_path=${first_editor_command##* }
second_temp_path=${second_editor_command##* }
case $second_editor_command in
    "jpacker-test-editor /tmp/jpacker-edit-src-second."??????)
        ;;
    *)
        echo "edit-src did not continue to second target" >&2
        cat "$command_log" >&2
        exit 1
        ;;
esac
assert_command_at 3 "sudo install -Dm644 $second_temp_path $preference_dir/second"
assert_path_absent "$first_temp_path"
assert_path_absent "$second_temp_path"
assert_contains "EDITED=yes" "$preference_dir/second"

echo "  ok: P0-3 cmd_edit_src"

# Existing list-src characterization remains owned by test-source-selection.

# P0-4: del-src and revert continue per target and preserve error priority.
setup_case del-src-partial-failure
for package in alpha beta gamma; do
    printf 'SOURCE=yes\n' > "$preference_dir/$package"
done
printf 'rm -f %s\n' "$preference_dir/beta" > "$sudo_failures"
run_fail del-src alpha beta gamma
assert_command_at 1 "sudo rm -f $preference_dir/alpha"
assert_command_at 2 "sudo rm -f $preference_dir/beta"
assert_command_at 3 "sudo rm -f $preference_dir/gamma"
assert_path_absent "$preference_dir/alpha"
if [ ! -f "$preference_dir/beta" ]; then
    echo "failed del-src target was unexpectedly removed" >&2
    exit 1
fi
assert_path_absent "$preference_dir/gamma"
assert_contains "Failed to remove beta" "$output_file"

setup_case revert-grouped-reinstall
for package in official-a remove-fail aur-a; do
    printf 'SOURCE=yes\n' > "$preference_dir/$package"
done
printf 'rm -f %s\n' "$preference_dir/remove-fail" > "$sudo_failures"
export JPACKER_TEST_PACMAN_REPO_PACKAGES='official-a official-b remove-fail'
run_fail --noconfirm revert official-a remove-fail aur-a official-b
assert_command_at 1 "sudo rm -f $preference_dir/official-a"
assert_command_at 2 "pacman -Si official-a"
assert_command_at 3 "sudo rm -f $preference_dir/remove-fail"
assert_command_absent "pacman -Si remove-fail"
assert_command_at 4 "sudo rm -f $preference_dir/aur-a"
assert_command_at 5 "pacman -Si aur-a"
assert_command_absent "sudo rm -f $preference_dir/official-b"
assert_command_at 6 "pacman -Si official-b"
assert_command_at 7 "sudo pacman -S --noconfirm official-a official-b"
assert_total_command_count 7
assert_command_count "sudo pacman -S --noconfirm official-a official-b" 1
assert_contains "official-b was not marked." "$output_file"
assert_contains "aur-a is likely an AUR package. Config removed only." "$output_file"
assert_contains "Failed to revert one or more packages." "$output_file"
assert_path_absent "$preference_dir/official-a"
assert_path_absent "$preference_dir/aur-a"
if [ ! -f "$preference_dir/remove-fail" ]; then
    echo "failed revert target was unexpectedly removed" >&2
    exit 1
fi

setup_case revert-reinstall-error-priority
printf 'SOURCE=yes\n' > "$preference_dir/delete-fail"
printf 'SOURCE=yes\n' > "$preference_dir/official-a"
{
    printf 'rm -f %s\n' "$preference_dir/delete-fail"
    printf 'pacman -S official-a\n'
} > "$sudo_failures"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_fail revert delete-fail official-a
assert_contains "Failed to reinstall binaries." "$output_file"
assert_not_contains "Failed to revert one or more packages." "$output_file"

setup_case revert-delete-aggregate
printf 'SOURCE=yes\n' > "$preference_dir/delete-fail"
printf 'rm -f %s\n' "$preference_dir/delete-fail" > "$sudo_failures"
run_fail revert delete-fail
assert_contains "Failed to revert one or more packages." "$output_file"
assert_command_content_absent "sudo pacman -S"

echo "  ok: P0-4 cmd_del_src/cmd_revert"

# P0-5: clean preflights every target, then preserves pacman/prompt/cleanup continuation.
setup_case clean-unsafe-preflight
mkdir -p "$cache_root/preflighted-safe" "$case_dir/outside"
ln -s "$case_dir/outside" "$cache_root/unsafe"
run_fail clean
assert_contains "symlink component is not allowed" "$output_file"
assert_total_command_count 0
if [ ! -d "$cache_root/preflighted-safe" ]; then
    echo "clean mutated a safe target before all cache targets passed preflight" >&2
    exit 1
fi
if [ ! -L "$cache_root/unsafe" ]; then
    echo "clean mutated the unsafe target after preflight failed" >&2
    exit 1
fi

setup_case clean-success
mkdir -p "$cache_root/alpha" "$cache_root/beta"
run_clean_tty_ok y
assert_command "sudo pacman -Sc"
assert_path_absent "$cache_root/alpha"
assert_path_absent "$cache_root/beta"
assert_contains "jpacker cache cleaned." "$output_file"

setup_case clean-pacman-failure-continues
mkdir -p "$cache_root/alpha" "$cache_root/beta"
printf 'pacman -Sc\n' > "$sudo_failures"
run_clean_tty_fail y
assert_contains "Pacman clean failed or cancelled." "$output_file"
assert_contains "jpacker cache cleaned." "$output_file"
assert_path_absent "$cache_root/alpha"
assert_path_absent "$cache_root/beta"

setup_case clean-partial-removal
mkdir -p "$cache_root/alpha" "$cache_root/beta" "$case_dir/outside"
export JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_PATH=$cache_root/alpha
export JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_TARGET=$case_dir/outside
run_clean_tty_fail y
if [ ! -L "$cache_root/alpha" ]; then
    echo "clean race fixture did not replace the first target" >&2
    exit 1
fi
assert_path_absent "$cache_root/beta"
assert_contains "Failed to remove $cache_root/alpha" "$output_file"
assert_contains "jpacker cache cleanup was incomplete." "$output_file"

setup_case clean-prompt-no
mkdir -p "$cache_root/alpha"
run_clean_tty_ok n
if [ ! -d "$cache_root/alpha" ]; then
    echo "clean removed cache after a no answer" >&2
    exit 1
fi
assert_contains "Skipped jpacker cache cleaning." "$output_file"

setup_case clean-empty-cache
mkdir -p "$cache_root"
run_ok clean
assert_command "sudo pacman -Sc"
assert_contains "jpacker cache is empty." "$output_file"

setup_case clean-no-confirm-default
mkdir -p "$cache_root/alpha"
run_ok --noconfirm clean
assert_command "sudo pacman -Sc --noconfirm"
assert_contains "Skipping prompt (--noconfirm): Clean jpacker build cache ($cache_root)? -> no" "$output_file"
assert_contains "Skipped jpacker cache cleaning." "$output_file"
if [ ! -d "$cache_root/alpha" ]; then
    echo "--noconfirm changed the clean prompt default" >&2
    exit 1
fi

echo "  ok: P0-5 cmd_clean"

# P0-6: upgrade keeps metadata preflight -> pacman -> source execution and catch hierarchy.
setup_case upgrade-ordinary-preflight-errors
: > "$preference_dir/missing-upgrade-a"
: > "$preference_dir/missing-upgrade-b"
run_fail --noconfirm upgrade
assert_command "sudo pacman -Syu --noconfirm"
assert_contains "Error updating missing-upgrade-a:" "$output_file"
assert_contains "Error updating missing-upgrade-b:" "$output_file"
assert_output_before "System upgrade..." "Error updating missing-upgrade-a:" "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case upgrade-pacman-failure
: > "$preference_dir/clean-root"
printf 'pacman -Syu --noconfirm\n' > "$sudo_failures"
run_fail --noedit --nodiff --noconfirm upgrade
assert_command "pacman -Si clean-root"
assert_command "sudo pacman -Syu --noconfirm"
assert_contains "Update failed." "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case upgrade-continues-source-packages
: > "$preference_dir/clean-root"
: > "$preference_dir/missing-upgrade"
: > "$preference_dir/bad name"
run_fail --noedit --nodiff --noconfirm upgrade
assert_contains "Ignoring invalid source-build preference filename: bad name" "$output_file"
assert_contains "Error updating missing-upgrade:" "$output_file"
assert_command "sudo pacman -Syu --noconfirm"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "pacman -Q clean-root"
assert_command "makepkg -sic --noconfirm"
assert_command_before "sudo pacman -Syu --noconfirm" "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_before "git clone https://aur.archlinux.org/clean-root.git clean-root" "pacman -Q clean-root"
assert_command_before "pacman -Q clean-root" "makepkg -sic --noconfirm"

echo "  ok: P0-6 cmd_upgrade"

# Schema-fatal upgrade cases remain owned by test-aur-rpc-validation.

# P0-7: direct shared-orchestration characterization, including the CLI-unreachable fallback branch.
setup_case source-plan-order
run_source_ok plan-success
assert_contains "Building AUR PackageBase: dep-base" "$output_file"
assert_contains "Target package(s): dep-target" "$output_file"
assert_contains "Building AUR PackageBase: root-base" "$output_file"
assert_contains "Target package(s): root-target" "$output_file"
assert_command_before \
    "git clone https://aur.archlinux.org/dep-base.git dep-base" \
    "git clone https://aur.archlinux.org/root-base.git root-base"
assert_command_count "makepkg -sic --noconfirm --needed" 2

setup_case source-plan-failure-context
export JPACKER_TEST_MAKEPKG_EXIT_CODE=42
run_source_fail plan-failure
assert_contains "Failed while building/installing PackageBase dep-base (dep-target): Build failed." "$output_file"
assert_command "git clone https://aur.archlinux.org/dep-base.git dep-base"
assert_command_absent "git clone https://aur.archlinux.org/root-base.git root-base"

setup_case source-preference-fallback
printf 'FALLBACK=base-value\n' > "$preference_dir/base-target"
run_source_ok fallback
assert_contains "Loading custom build flags from $preference_dir/base-target" "$output_file"
assert_contains "Applying custom build flags: FALLBACK='base-value' " "$output_file"
assert_not_contains "Loading custom build flags from $preference_dir/requested-target" "$output_file"

setup_case source-preference-requested-wins
printf 'REQUESTED=requested-value\n' > "$preference_dir/requested-target"
printf 'FALLBACK=base-value\n' > "$preference_dir/base-target"
run_source_ok fallback
assert_contains "Loading custom build flags from $preference_dir/requested-target" "$output_file"
assert_not_contains "Loading custom build flags from $preference_dir/base-target" "$output_file"
assert_contains "Applying custom build flags: REQUESTED='requested-value' " "$output_file"

setup_case smart-source-order
printf 'SMART=preference\n' > "$preference_dir/clean-root"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
run_source_ok smart-source
assert_output_before \
    "Loading custom build flags from $preference_dir/clean-root" \
    "Processing clean-root..." "$output_file"
assert_command_before \
    "pacman -Si clean-root" \
    "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git clean-root"
assert_contains "Applying custom build flags: SMART='preference' " "$output_file"

echo "  ok: P0-7 shared source-install orchestration"

echo "source/maintenance command characterization tests: all checks passed"
