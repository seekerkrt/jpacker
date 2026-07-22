#!/bin/sh
set -eu

test_binary=$1
source_install_test_binary=$2
upgrade_metadata_test_binary=$3
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
upgrade_metadata_path=$repo_root/tests/stubs/upgrade-baseline-metadata:$PATH
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
    package_metadata_state=$case_dir/package-metadata-state

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$preference_dir"
    : > "$command_log"
    : > "$request_log"
    : > "$sudo_failures"
    : > "$package_metadata_state"
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
    export JPACKER_TEST_PACKAGE_METADATA_STATE_FILE=$package_metadata_state
    export JPACKER_TEST_PACKAGE_METADATA_EVENT_LOG=$command_log

    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_PACMAN_Q_OUTPUT
    unset JPACKER_TEST_PACMAN_Q_OUTPUT_FILE
    unset JPACKER_TEST_PACMAN_Q_EXIT_CODE
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_APP_CONFIG_CASE
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION
    unset JPACKER_TEST_GIT_CLONE_FAIL_DESTINATION_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_GIT_RESET_SRCINFO_FIXTURE
    unset JPACKER_TEST_GIT_SYMBOLIC_REF
    unset JPACKER_TEST_GIT_SYMBOLIC_REF_EXIT_CODE
    unset JPACKER_TEST_GIT_MAIN_REF_EXIT_CODE
    unset JPACKER_TEST_GIT_MASTER_REF_EXIT_CODE
    unset JPACKER_TEST_VERCMP_OUTPUT
    unset JPACKER_TEST_VERCMP_EXIT_CODE
    unset JPACKER_TEST_VERCMP_ARGV_LOG
    unset JPACKER_TEST_MAKEPKG_ARGV_LOG
    unset JPACKER_TEST_SOURCE_MAINTENANCE_FAIL_SUBSTRING
    unset JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SYU_Q_OUTPUT_FILE
    unset JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_PATH
    unset JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SC_RACE_TARGET
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_SNAPSHOT_FILE
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_FAIL_SUBSTRING
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SUBSTRING
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_TARGET
    unset JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SOURCE
    unset JPACKER_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE
    unset JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE
    unset JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_AT
    unset JPACKER_TEST_PACKAGE_METADATA_PACMAN_CONF_EXIT_CODE
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

run_upgrade_ok() {
    : > "$command_log"
    : > "$request_log"
    if ! PATH=$upgrade_metadata_path "$upgrade_metadata_test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected upgrade metadata command to succeed: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_upgrade_fail() {
    : > "$command_log"
    : > "$request_log"
    exit_code=0
    if PATH=$upgrade_metadata_path "$upgrade_metadata_test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected upgrade metadata command to fail: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    else
        exit_code=$?
    fi
    if [ "$exit_code" -ne 1 ]; then
        echo "unexpected upgrade metadata exit code: $exit_code (expected 1)" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_ok_stdin_closed() {
    : > "$command_log"
    : > "$request_log"
    if ! "$test_binary" "$@" <&- > "$output_file" 2>&1; then
        echo "expected command with closed stdin to succeed: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail_nonblocking() {
    : > "$command_log"
    : > "$request_log"
    exit_code=0
    if timeout 5 "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail without blocking: $*" >&2
        sed -n '1,260p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    else
        exit_code=$?
    fi
    if [ "$exit_code" -eq 124 ]; then
        echo "command blocked until timeout: $*" >&2
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

assert_output_line_count() {
    pattern=$1
    expected_count=$2
    file=$3
    actual_count=$(grep -Fc -- "$pattern" "$file" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected output count for: $pattern" >&2
        echo "expected $expected_count, got $actual_count" >&2
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

assert_command_prefix_count() {
    expected_prefix=$1
    expected_count=$2
    actual_count=$(awk -v prefix="$expected_prefix" '
        index($0, prefix) == 1 { count++ }
        END { print count + 0 }
    ' "$command_log")
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected command prefix count for: $expected_prefix" >&2
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

assert_request_log_empty() {
    if [ -s "$request_log" ]; then
        echo "unexpected AUR RPC request before package validation completed" >&2
        cat "$request_log" >&2
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

assert_command_occurrence_before() {
    first=$1
    first_occurrence=$2
    second=$3
    second_occurrence=$4
    first_line=$(grep -nFx -- "$first" "$command_log" | sed -n "${first_occurrence}s/:.*//p")
    second_line=$(grep -nFx -- "$second" "$command_log" | sed -n "${second_occurrence}s/:.*//p")
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected command order: $first (#$first_occurrence) -> $second (#$second_occurrence)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_single_package_metadata_snapshot_before_syu() {
    package_name=$1
    assert_command_count "pacman-conf --verbose RootDir DBPath" 1
    assert_command_count "alpm initialize" 1
    assert_command_count "alpm query $package_name" 1
    assert_command_count "alpm release" 1
    assert_command_occurrence_before "alpm query $package_name" 1 "alpm release" 1
    assert_command_occurrence_before "alpm release" 1 "sudo pacman -Syu --noconfirm" 1
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

assert_file_empty() {
    file=$1
    if [ -s "$file" ]; then
        echo "expected file to be empty: $file" >&2
        sed -n '1,260p' "$file" >&2
        exit 1
    fi
}

assert_argv_log() {
    actual_file=$1
    expected=$2
    expected_file=$case_dir/expected-argv.log
    printf '%s\n' "$expected" > "$expected_file"
    assert_file_equals "$expected_file" "$actual_file"
}

write_upgrade_srcinfo() {
    srcinfo_file=$1
    srcinfo_version=$2
    srcinfo_release=$3
    {
        printf 'pkgbase = clean-root\n'
        printf 'pkgver = %s\n' "$srcinfo_version"
        printf 'pkgrel = %s\n' "$srcinfo_release"
        printf 'pkgname = clean-root\n'
    } > "$srcinfo_file"
}

setup_upgrade_transition_case() {
    upgrade_case_name=$1
    installed_version_before_syu=$2
    installed_version_after_syu=$3
    source_preference_state=$4
    source_git_url=$5

    setup_case "$upgrade_case_name"

    upgrade_package=clean-root
    installed_version_state=$case_dir/installed-version-state
    installed_version_before=$case_dir/installed-version-before
    installed_version_after=$case_dir/installed-version-after-syu
    initial_srcinfo=$case_dir/initial.SRCINFO
    remote_srcinfo=$case_dir/remote.SRCINFO
    checkout_dir=$cache_root/$upgrade_package
    vercmp_argv_log=$case_dir/vercmp-argv.log
    makepkg_argv_log=$case_dir/makepkg-argv.log

    : > "$installed_version_before"
    if [ "$installed_version_before_syu" != not-installed ]; then
        printf '%s %s\n' "$upgrade_package" "$installed_version_before_syu" > "$installed_version_before"
    fi
    printf '%s %s\n' "$upgrade_package" "$installed_version_after_syu" > "$installed_version_after"
    cp "$installed_version_before" "$installed_version_state"

    write_upgrade_srcinfo "$initial_srcinfo" 1.0 1
    write_upgrade_srcinfo "$remote_srcinfo" 2.0 1
    mkdir -p "$checkout_dir/.git"
    cp "$initial_srcinfo" "$checkout_dir/.SRCINFO"
    printf 'pkgname=%s\npkgver=1.0\npkgrel=1\n' "$upgrade_package" > "$checkout_dir/PKGBUILD"
    printf '%s\n' "$source_git_url" > "$checkout_dir/.git/.jpacker-test-remote-url"

    if [ "$source_preference_state" = enabled ]; then
        : > "$preference_dir/$upgrade_package"
    fi

    : > "$vercmp_argv_log"
    : > "$makepkg_argv_log"
    export JPACKER_TEST_PACMAN_Q_OUTPUT_FILE=$installed_version_state
    export JPACKER_TEST_PACKAGE_METADATA_STATE_FILE=$installed_version_state
    export JPACKER_TEST_SOURCE_MAINTENANCE_PACMAN_SYU_Q_OUTPUT_FILE=$installed_version_after
    export JPACKER_TEST_GIT_RESET_SRCINFO_FIXTURE=$remote_srcinfo
    export JPACKER_TEST_VERCMP_ARGV_LOG=$vercmp_argv_log
    export JPACKER_TEST_MAKEPKG_ARGV_LOG=$makepkg_argv_log
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

# dot path componentは、source preferenceの副作用を始める前にpackage validationで拒否する。
for dot_target in . ..; do
    case $dot_target in
        .) dot_target_label=dot ;;
        ..) dot_target_label=dot-dot ;;
    esac
    for operation in add-src edit-src del-src revert; do
        setup_case "$operation-reject-$dot_target_label"
        printf 'ROOT_GUARD=yes\n' > "$preference_dir/root-guard"
        printf 'PARENT_GUARD=yes\n' > "$case_dir/parent-guard"
        root_guard_checksum=$(cksum "$preference_dir/root-guard")
        parent_guard_checksum=$(cksum "$case_dir/parent-guard")

        run_fail "$operation" "$dot_target"

        assert_contains "Invalid package name: $dot_target" "$output_file"
        assert_total_command_count 0
        assert_request_log_empty
        if [ "$(cksum "$preference_dir/root-guard")" != "$root_guard_checksum" ] ||
           [ "$(cksum "$case_dir/parent-guard")" != "$parent_guard_checksum" ]; then
            echo "$operation $dot_target changed the source preference fixture" >&2
            exit 1
        fi
        entry_count=$(find "$preference_dir" -mindepth 1 -maxdepth 1 -print | wc -l)
        if [ "$entry_count" -ne 1 ]; then
            echo "$operation $dot_target changed source preference entries" >&2
            find "$preference_dir" -maxdepth 1 -print >&2
            exit 1
        fi
    done
done

echo "  ok: dot package names stop before source preference side effects"

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

# P0-3: edit-src pins the user-opened source fd and gives root only /dev/stdin + destination.
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
assert_command_at 2 "sudo install -Dm644 -- /dev/stdin $preference_dir/alpha"
privileged_command=$(sed -n '2p' "$command_log")
case $privileged_command in
    *"$edit_temp_path"*)
        echo "privileged command exposed temporary pathname: $privileged_command" >&2
        exit 1
        ;;
esac
assert_contains "Running: sudo install -Dm644 -- /dev/stdin '$preference_dir/alpha'" "$output_file"
logged_install_count=$(grep -Fc -- "Running: sudo install -Dm644 -- /dev/stdin '$preference_dir/alpha'" "$output_file" || true)
if [ "$logged_install_count" -ne 1 ]; then
    echo "privileged install command was not logged exactly once" >&2
    sed -n '1,260p' "$output_file" >&2
    exit 1
fi
assert_file_equals "$case_dir/existing.expected" "$case_dir/editor.snapshot"
printf 'CFLAGS=-Oexisting\nLDFLAGS=-Wl,--as-needed\n' > "$case_dir/edited.expected"
assert_file_equals "$case_dir/edited.expected" "$preference_dir/alpha"
if [ "$(stat -c '%a' "$preference_dir/alpha")" != "644" ]; then
    echo "edit-src destination mode is not 0644" >&2
    exit 1
fi
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
assert_command_at 2 "sudo install -Dm644 -- /dev/stdin $preference_dir/alpha"

setup_case edit-src-create-missing-preference
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='CREATED=yes'
run_ok edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
printf 'CREATED=yes\n' > "$case_dir/created.expected"
assert_file_equals "$case_dir/created.expected" "$preference_dir/alpha"
assert_command_at 2 "sudo install -Dm644 -- /dev/stdin $preference_dir/alpha"
assert_path_absent "$edit_temp_path"

setup_case edit-src-empty-content
: > "$case_dir/replacement-content"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=regular
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SOURCE=$case_dir/replacement-content
run_ok edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_file_equals "$case_dir/replacement-content" "$preference_dir/alpha"
assert_path_absent "$edit_temp_path"

setup_case edit-src-no-final-newline
printf 'NO_FINAL_NEWLINE' > "$case_dir/replacement-content"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=regular
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SOURCE=$case_dir/replacement-content
run_ok edit-src alpha
assert_file_equals "$case_dir/replacement-content" "$preference_dir/alpha"

setup_case edit-src-binary-regular-replacement
printf 'BINARY\000PAYLOAD\n' > "$case_dir/replacement-content"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=regular
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SOURCE=$case_dir/replacement-content
run_ok edit-src alpha
assert_file_equals "$case_dir/replacement-content" "$preference_dir/alpha"

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

setup_case edit-src-reject-symlink
printf 'ORIGINAL=yes\n' > "$preference_dir/alpha"
printf 'ORIGINAL=yes\n' > "$case_dir/original.expected"
printf 'UNREVIEWED=yes\n' > "$case_dir/symlink-target"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=symlink
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_TARGET=$case_dir/symlink-target
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "Failed to open edited temporary file $edit_temp_path" "$output_file"
assert_total_command_count 1
assert_path_absent "$edit_temp_path"
assert_file_equals "$case_dir/original.expected" "$preference_dir/alpha"

setup_case edit-src-reject-directory
printf 'ORIGINAL=yes\n' > "$preference_dir/alpha"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=directory
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "Edited temporary file is not a regular file: $edit_temp_path" "$output_file"
assert_total_command_count 1
assert_path_absent "$edit_temp_path"

setup_case edit-src-reject-fifo-without-blocking
printf 'ORIGINAL=yes\n' > "$preference_dir/alpha"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=fifo
run_fail_nonblocking edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "Edited temporary file is not a regular file: $edit_temp_path" "$output_file"
assert_total_command_count 1
assert_path_absent "$edit_temp_path"

setup_case edit-src-reject-missing-source
printf 'ORIGINAL=yes\n' > "$preference_dir/alpha"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=missing
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_contains "Failed to open edited temporary file $edit_temp_path" "$output_file"
assert_total_command_count 1
assert_path_absent "$edit_temp_path"

setup_case edit-src-install-failure
printf 'CFLAGS=-Oexisting\n' > "$preference_dir/alpha"
printf 'CFLAGS=-Oexisting\n' > "$case_dir/existing.expected"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='EDITED=yes'
export JPACKER_TEST_SOURCE_MAINTENANCE_FAIL_SUBSTRING='install -Dm644'
run_fail edit-src alpha
editor_command=$(sed -n '1p' "$command_log")
edit_temp_path=${editor_command##* }
assert_command_at 2 "sudo install -Dm644 -- /dev/stdin $preference_dir/alpha"
assert_contains "edited file kept at $edit_temp_path" "$output_file"
if [ ! -f "$edit_temp_path" ]; then
    echo "edit-src removed the retained file after install failure" >&2
    exit 1
fi
assert_contains "EDITED=yes" "$edit_temp_path"
assert_file_equals "$case_dir/existing.expected" "$preference_dir/alpha"
rm -f "$edit_temp_path"

setup_case edit-src-editor-failure-continues
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
        echo "edit-src did not continue after editor failure" >&2
        cat "$command_log" >&2
        exit 1
        ;;
esac
assert_command_at 3 "sudo install -Dm644 -- /dev/stdin $preference_dir/second"
assert_path_absent "$first_temp_path"
assert_path_absent "$second_temp_path"
assert_contains "EDITED=yes" "$preference_dir/second"

setup_case edit-src-source-validation-continues
printf 'FIRST=original\n' > "$preference_dir/first"
printf 'SECOND=original\n' > "$preference_dir/second"
printf 'UNREVIEWED=yes\n' > "$case_dir/symlink-target"
export EDITOR=jpacker-test-editor
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_APPEND_LINE='EDITED=yes'
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=symlink
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SUBSTRING=jpacker-edit-src-first.
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_TARGET=$case_dir/symlink-target
run_fail edit-src first second
first_editor_command=$(sed -n '1p' "$command_log")
second_editor_command=$(sed -n '2p' "$command_log")
first_temp_path=${first_editor_command##* }
second_temp_path=${second_editor_command##* }
assert_contains "Failed to open edited temporary file $first_temp_path" "$output_file"
assert_command_at 3 "sudo install -Dm644 -- /dev/stdin $preference_dir/second"
assert_path_absent "$first_temp_path"
assert_path_absent "$second_temp_path"
assert_contains "EDITED=yes" "$preference_dir/second"

setup_case edit-src-source-fd-zero
: > "$case_dir/log-parent"
{
    printf 'EDITOR=jpacker-test-editor\n'
    printf 'LOGFILE=%s\n' "$case_dir/log-parent/jpacker.log"
} > "$config_file"
printf 'FD_ZERO\000PAYLOAD' > "$case_dir/replacement-content"
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_KIND=regular
export JPACKER_TEST_SOURCE_MAINTENANCE_EDITOR_REPLACEMENT_SOURCE=$case_dir/replacement-content
run_ok_stdin_closed edit-src alpha
assert_command_at 2 "sudo install -Dm644 -- /dev/stdin $preference_dir/alpha"
assert_file_equals "$case_dir/replacement-content" "$preference_dir/alpha"

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
setup_case upgrade-metadata-no-target
run_upgrade_ok --noconfirm upgrade
assert_command "sudo pacman -Syu --noconfirm"
assert_command_content_absent "pacman-conf "
assert_command_content_absent "alpm "
assert_total_command_count 1

setup_case upgrade-metadata-no-preference-root
rmdir "$preference_dir"
run_upgrade_ok --noconfirm upgrade
assert_command "sudo pacman -Syu --noconfirm"
assert_command_content_absent "pacman-conf "
assert_command_content_absent "alpm "
assert_total_command_count 1

setup_case upgrade-metadata-nonregular-only
mkdir "$preference_dir/alpha"
run_upgrade_ok --noconfirm upgrade
assert_command "sudo pacman -Syu --noconfirm"
assert_command_content_absent "pacman-conf "
assert_command_content_absent "alpm "
assert_total_command_count 1

setup_case upgrade-metadata-invalid-preference-only
: > "$preference_dir/bad name"
run_upgrade_fail --noconfirm upgrade
assert_contains "Ignoring invalid source-build preference filename: bad name" "$output_file"
assert_output_line_count "Ignoring invalid source-build preference filename: bad name" 1 "$output_file"
assert_command "sudo pacman -Syu --noconfirm"
assert_command_content_absent "pacman-conf "
assert_command_content_absent "alpm "
assert_command_content_absent "git "
assert_command_content_absent "makepkg "

setup_case upgrade-metadata-resolver-failure
: > "$preference_dir/alpha"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=alpha
export JPACKER_TEST_PACKAGE_METADATA_PACMAN_CONF_EXIT_CODE=42
run_upgrade_fail --noconfirm upgrade
assert_contains "pacman-conf failed with exit code 42." "$output_file"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_content_absent "alpm "
assert_command_absent "sudo pacman -Syu --noconfirm"
assert_command_content_absent "git "
assert_command_content_absent "makepkg "

setup_case upgrade-metadata-session-open-failure
: > "$preference_dir/alpha"
printf 'alpha 1.0-1\n' > "$package_metadata_state"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=alpha
export JPACKER_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE=1
run_upgrade_fail --noconfirm upgrade
assert_contains "Failed to initialize package metadata session: system error." "$output_file"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_content_absent "alpm query "
assert_command_absent "alpm release"
assert_command_absent "sudo pacman -Syu --noconfirm"
assert_command_content_absent "git "
assert_command_content_absent "makepkg "

setup_case upgrade-metadata-query-failure
: > "$preference_dir/alpha"
printf 'alpha 1.0-1\n' > "$package_metadata_state"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=alpha
export JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE=alpha
run_upgrade_fail --noconfirm upgrade
assert_contains "Failed to query installed package metadata for alpha: Installed package query failed: database open failed." "$output_file"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_count "alpm query alpha" 1
assert_command_count "alpm release" 1
assert_command_absent "sudo pacman -Syu --noconfirm"
assert_command_content_absent "git "
assert_command_content_absent "makepkg "

setup_case upgrade-metadata-multi-target-failure
for package in alpha beta gamma; do
    : > "$preference_dir/$package"
    printf '%s 1.0-1\n' "$package" >> "$package_metadata_state"
done
export JPACKER_TEST_PACMAN_REPO_PACKAGES='alpha beta gamma'
export JPACKER_TEST_PACKAGE_METADATA_QUERY_FAILURE_AT=2
run_upgrade_fail --noconfirm upgrade
assert_contains "Failed to query installed package metadata for " "$output_file"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_prefix_count "alpm query " 2
assert_command_count "alpm release" 1
assert_command_absent "sudo pacman -Syu --noconfirm"
assert_command_content_absent "git "
assert_command_content_absent "makepkg "

setup_case upgrade-ordinary-preflight-errors
: > "$preference_dir/missing-upgrade-a"
: > "$preference_dir/missing-upgrade-b"
run_upgrade_fail --noconfirm upgrade
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_count "alpm query missing-upgrade-a" 1
assert_command_count "alpm query missing-upgrade-b" 1
assert_command_count "alpm release" 1
assert_command "sudo pacman -Syu --noconfirm"
assert_command_before "alpm release" "sudo pacman -Syu --noconfirm"
assert_contains "Error updating missing-upgrade-a:" "$output_file"
assert_contains "Error updating missing-upgrade-b:" "$output_file"
assert_output_before "System upgrade..." "Error updating missing-upgrade-a:" "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case upgrade-pacman-failure
: > "$preference_dir/clean-root"
printf 'pacman -Syu --noconfirm\n' > "$sudo_failures"
run_upgrade_fail --noedit --nodiff --noconfirm upgrade
assert_command "pacman -Si clean-root"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_count "alpm query clean-root" 1
assert_command_count "alpm release" 1
assert_command_absent "pacman -Q clean-root"
assert_command "sudo pacman -Syu --noconfirm"
assert_command_before "alpm release" "sudo pacman -Syu --noconfirm"
assert_contains "Update failed." "$output_file"
assert_command_content_absent "git clone"
assert_command_content_absent "makepkg"

setup_case upgrade-continues-source-packages
: > "$preference_dir/clean-root"
: > "$preference_dir/missing-upgrade"
: > "$preference_dir/bad name"
run_upgrade_fail --noedit --nodiff --noconfirm upgrade
assert_contains "Ignoring invalid source-build preference filename: bad name" "$output_file"
assert_output_line_count "Ignoring invalid source-build preference filename: bad name" 1 "$output_file"
assert_contains "Error updating missing-upgrade:" "$output_file"
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 1
assert_command_count "alpm query clean-root" 1
assert_command_count "alpm query missing-upgrade" 1
assert_command_absent "alpm query bad name"
assert_command_count "alpm release" 1
assert_command "sudo pacman -Syu --noconfirm"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_count "pacman -Q clean-root" 1
assert_command "makepkg -sic --noconfirm"
assert_command_occurrence_before "alpm query clean-root" 1 "alpm release" 1
assert_command_occurrence_before "alpm release" 1 "sudo pacman -Syu --noconfirm" 1
assert_command_occurrence_before "sudo pacman -Syu --noconfirm" 1 "git clone https://aur.archlinux.org/clean-root.git clean-root" 1
assert_command_occurrence_before "git clone https://aur.archlinux.org/clean-root.git clean-root" 1 "pacman -Q clean-root" 1
assert_command_occurrence_before "pacman -Q clean-root" 1 "makepkg -sic --noconfirm" 1

# Issue #215 regression: system transactionでofficial binaryへ置換された場合も、
# source-build preferenceを実際のinstalled packageへ反映する。
official_source_url=https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git
aur_source_url=https://aur.archlinux.org/clean-root.git

setup_upgrade_transition_case \
    issue-215-case-1-rebuild-after-official-binary-replacement \
    1.0-1 2.0-1 enabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "Loading custom build flags from $preference_dir/$upgrade_package" "$output_file"
assert_contains "$upgrade_package was updated by the system transaction (1.0-1 -> 2.0-1); rebuilding the preferred source package." "$output_file"
assert_not_contains "$upgrade_package is up to date (2.0-1). Skipping." "$output_file"
assert_command_count "pacman -Si $upgrade_package" 2
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "vercmp 2.0-1 2.0-1"
assert_command_count "makepkg -sic --noconfirm" 1
assert_command_content_absent "git clone"
assert_command_occurrence_before "sudo pacman -Syu --noconfirm" 1 "git fetch origin" 1
assert_command_occurrence_before "git fetch origin" 1 "git reset --hard origin/main" 1
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 2.0-1" 1
assert_command_occurrence_before "vercmp 2.0-1 2.0-1" 1 "makepkg -sic --noconfirm" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<2.0-1>
argv-end'
assert_argv_log "$makepkg_argv_log" 'argv-begin
arg[0]=<-sic>
arg[1]=<--noconfirm>
argv-end'
assert_request_log_empty

# Case 1とのfixture差は、fake pacman transaction後のinstalled versionだけ。
setup_upgrade_transition_case \
    issue-215-case-2-official-version-unchanged \
    1.0-1 1.0-1 enabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "Loading custom build flags from $preference_dir/$upgrade_package" "$output_file"
assert_not_contains "rebuilding the preferred source package." "$output_file"
assert_command_count "pacman -Si $upgrade_package" 2
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "vercmp 2.0-1 1.0-1"
assert_command_count "makepkg -sic --noconfirm" 1
assert_command_content_absent "git clone"
assert_command_occurrence_before "sudo pacman -Syu --noconfirm" 1 "git fetch origin" 1
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 1.0-1" 1
assert_command_occurrence_before "vercmp 2.0-1 1.0-1" 1 "makepkg -sic --noconfirm" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<1.0-1>
argv-end'
assert_argv_log "$makepkg_argv_log" 'argv-begin
arg[0]=<-sic>
arg[1]=<--noconfirm>
argv-end'
assert_request_log_empty

setup_upgrade_transition_case \
    issue-215-case-3-no-source-preference \
    1.0-1 2.0-1 disabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$initial_srcinfo" "$checkout_dir/.SRCINFO"
assert_not_contains "rebuilding the preferred source package." "$output_file"
assert_command "sudo pacman -Syu --noconfirm"
assert_command_content_absent "pacman-conf "
assert_command_content_absent "alpm "
assert_command_absent "pacman -Si $upgrade_package"
assert_command_content_absent "git "
assert_command_absent "pacman -Q $upgrade_package"
assert_command_content_absent "vercmp"
assert_command_content_absent "makepkg"
assert_total_command_count 1
assert_file_empty "$vercmp_argv_log"
assert_file_empty "$makepkg_argv_log"
assert_request_log_empty

setup_upgrade_transition_case \
    issue-215-case-4-aur-build-after-system-upgrade \
    1.0-1 1.0-1 enabled "$aur_source_url"
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "Loading custom build flags from $preference_dir/$upgrade_package" "$output_file"
assert_not_contains "rebuilding the preferred source package." "$output_file"
assert_command "pacman -Si $upgrade_package"
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "vercmp 2.0-1 1.0-1"
assert_command_count "makepkg -sic --noconfirm" 1
assert_command_content_absent "git clone"
assert_command_occurrence_before "pacman -Si $upgrade_package" 1 "alpm query $upgrade_package" 1
assert_command_occurrence_before "sudo pacman -Syu --noconfirm" 1 "git fetch origin" 1
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 1.0-1" 1
assert_command_occurrence_before "vercmp 2.0-1 1.0-1" 1 "makepkg -sic --noconfirm" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<1.0-1>
argv-end'
assert_argv_log "$makepkg_argv_log" 'argv-begin
arg[0]=<-sic>
arg[1]=<--noconfirm>
argv-end'
if [ ! -s "$request_log" ]; then
    echo "expected AUR RPC request for $upgrade_package" >&2
    exit 1
fi

setup_upgrade_transition_case \
    issue-215-case-5-equal-version-without-transaction-change \
    2.0-1 2.0-1 enabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "$upgrade_package is up to date (2.0-1). Skipping." "$output_file"
assert_not_contains "rebuilding the preferred source package." "$output_file"
assert_command_count "pacman -Si $upgrade_package" 2
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git reset --hard origin/main"
assert_command "vercmp 2.0-1 2.0-1"
assert_command_content_absent "makepkg"
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 2.0-1" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<2.0-1>
argv-end'
assert_file_empty "$makepkg_argv_log"
assert_request_log_empty

setup_upgrade_transition_case \
    issue-215-case-6-older-source-does-not-downgrade \
    1.0-1 3.0-1 enabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_equals "$installed_version_before" "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "$upgrade_package is up to date (3.0-1). Skipping." "$output_file"
assert_not_contains "rebuilding the preferred source package." "$output_file"
assert_command_count "pacman -Si $upgrade_package" 2
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git reset --hard origin/main"
assert_command "vercmp 2.0-1 3.0-1"
assert_command_content_absent "makepkg"
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 3.0-1" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<3.0-1>
argv-end'
assert_file_empty "$makepkg_argv_log"
assert_request_log_empty

setup_upgrade_transition_case \
    issue-215-case-7-installed-by-system-transaction \
    not-installed 2.0-1 enabled "$official_source_url"
export JPACKER_TEST_PACMAN_REPO_PACKAGES=$upgrade_package
assert_file_empty "$installed_version_state"
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_file_equals "$installed_version_after" "$installed_version_state"
assert_file_equals "$remote_srcinfo" "$checkout_dir/.SRCINFO"
assert_contains "$upgrade_package was installed by the system transaction as 2.0-1; rebuilding the preferred source package." "$output_file"
assert_not_contains "$upgrade_package is up to date (2.0-1). Skipping." "$output_file"
assert_command_count "pacman -Si $upgrade_package" 2
assert_command_count "pacman -Q $upgrade_package" 1
assert_command "sudo pacman -Syu --noconfirm"
assert_single_package_metadata_snapshot_before_syu "$upgrade_package"
assert_command "git reset --hard origin/main"
assert_command "vercmp 2.0-1 2.0-1"
assert_command_count "makepkg -sic --noconfirm" 1
assert_command_occurrence_before "git reset --hard origin/main" 1 "pacman -Q $upgrade_package" 1
assert_command_occurrence_before "pacman -Q $upgrade_package" 1 "vercmp 2.0-1 2.0-1" 1
assert_command_occurrence_before "vercmp 2.0-1 2.0-1" 1 "makepkg -sic --noconfirm" 1
assert_argv_log "$vercmp_argv_log" 'argv-begin
arg[0]=<2.0-1>
arg[1]=<2.0-1>
argv-end'
assert_argv_log "$makepkg_argv_log" 'argv-begin
arg[0]=<-sic>
arg[1]=<--noconfirm>
argv-end'
assert_request_log_empty

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
