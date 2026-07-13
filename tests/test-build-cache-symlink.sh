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

# script(1) gives cmd_clean() a TTY so the destructive yes branch is exercised
# without adding a test-only prompt override to the production binary.
if ! command -v script >/dev/null 2>&1; then
    echo "script(1) is required for build cache cleanup tests" >&2
    exit 1
fi
ln -s "$test_binary" "$tmp_dir/jpacker-test"
test_runner=$tmp_dir/jpacker-test

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
export JPACKER_TEST_PACMAN_EXIT_CODE=1
export JPACKER_TEST_SUDO_EXIT_CODE=0

fail() {
    echo "$*" >&2
    exit 1
}

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    home_dir=$case_dir/home
    xdg_cache_dir=$case_dir/xdg-cache
    outside_dir=$case_dir/outside
    command_log=$case_dir/commands.log

    mkdir -p "$home_dir" "$xdg_cache_dir" "$outside_dir"
    : > "$command_log"
    export HOME=$home_dir
    export XDG_CACHE_HOME=$xdg_cache_dir
    export JPACKER_TEST_COMMAND_LOG=$command_log
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_PACKAGE_BUILD_DIR

    cache_root=$XDG_CACHE_HOME/jpacker
    entry_path=$cache_root/clean-root
}

run_ok() {
    output_file=$1
    shift
    : > "$command_log"
    if ! "$test_runner" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    output_file=$1
    shift
    : > "$command_log"
    if "$test_runner" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_clean_tty_ok() {
    output_file=$1
    : > "$command_log"
    if ! printf 'y\n' | script -qec "$test_runner clean" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive clean to succeed" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_clean_tty_fail() {
    output_file=$1
    : > "$command_log"
    if printf 'y\n' | script -qec "$test_runner clean" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive clean to fail" >&2
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

assert_command_absent() {
    unexpected=$1
    if grep -Fx -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command: $unexpected" >&2
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

assert_log_empty() {
    if [ -s "$command_log" ]; then
        echo "external command ran before unsafe cache path was rejected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_cache_mutation_commands() {
    if grep -E '^(git|makepkg|sudo) ' "$command_log" >/dev/null; then
        echo "unsafe cache path reached a git/build/install command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_symlink() {
    path=$1
    if [ ! -L "$path" ]; then
        fail "expected symlink to remain: $path"
    fi
}

assert_path_absent() {
    path=$1
    if [ -e "$path" ] || [ -L "$path" ]; then
        fail "expected path to be absent: $path"
    fi
}

assert_symlink_rejection() {
    output_file=$1
    rejected_path=$2
    assert_contains "$rejected_path" "$output_file"
    assert_contains "symlink component is not allowed" "$output_file"
}

create_checkout() {
    checkout_dir=$1
    mkdir -p "$checkout_dir/.git"
    printf 'outside marker\n' > "$checkout_dir/marker"
    printf 'uncommitted content\n' > "$checkout_dir/uncommitted-content"
}

create_regular_repo() {
    repo_dir=$1
    mkdir -p "$repo_dir/.git"
}

snapshot_directory() {
    snapshot_dir=$1
    snapshot_file=$2
    (
        CDPATH= cd "$snapshot_dir"
        find . -mindepth 1 -printf 'entry %y %p\n'
        find . -type f -exec cksum {} \;
    ) | LC_ALL=C sort > "$snapshot_file"
}

assert_directory_unchanged() {
    checked_dir=$1
    before_snapshot=$2
    after_snapshot=$case_dir/after.snapshot
    if [ ! -d "$checked_dir" ]; then
        fail "outside fixture directory was removed: $checked_dir"
    fi
    snapshot_directory "$checked_dir" "$after_snapshot"
    if ! cmp -s "$before_snapshot" "$after_snapshot"; then
        echo "outside fixture changed: $checked_dir" >&2
        diff -u "$before_snapshot" "$after_snapshot" >&2 || true
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
        echo "expected output ordering: $first before $second" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

# --- Existing package entry symlinks ---

setup_case fetch-directory-symlink
mkdir -p "$cache_root"
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/checkout" "$entry_path"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-file-symlink
mkdir -p "$cache_root"
printf 'outside regular file\n' > "$outside_dir/package-entry"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/package-entry" "$entry_path"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-dangling-symlink
mkdir -p "$cache_root"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/missing-package-entry" "$entry_path"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# This case is the direct canonical escape reproducer: the lexical package entry
# is below jpacker, while its resolved checkout is outside the trusted root.
setup_case fetch-canonical-outside
mkdir -p "$cache_root"
create_checkout "$outside_dir/canonical-outside"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/canonical-outside" "$entry_path"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# A string-prefix check would incorrectly accept jpacker-escape as being below
# jpacker. Component-based containment and the symlink policy must reject it.
setup_case fetch-prefix-lookalike
mkdir -p "$cache_root"
prefix_sibling=$XDG_CACHE_HOME/jpacker-escape
create_checkout "$prefix_sibling/clean-root"
snapshot_directory "$prefix_sibling" "$case_dir/before.snapshot"
ln -s "$prefix_sibling/clean-root" "$entry_path"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$prefix_sibling" "$case_dir/before.snapshot"

# --- Cache root and ancestor symlink boundaries ---

setup_case fetch-cache-root-symlink
root_target=$outside_dir/cache-root
create_checkout "$root_target/clean-root"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$root_target" "$cache_root"
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$cache_root"
assert_log_empty
assert_symlink "$cache_root"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-cache-ancestor-symlink
ancestor_target=$outside_dir/cache-parent
ancestor_link=$case_dir/cache-parent-link
create_checkout "$ancestor_target/jpacker/clean-root"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$ancestor_target" "$ancestor_link"
export XDG_CACHE_HOME=$ancestor_link
cache_root=$XDG_CACHE_HOME/jpacker
entry_path=$cache_root/clean-root
run_fail "$case_dir/output" fetch clean-root
assert_symlink_rejection "$case_dir/output" "$cache_root"
assert_log_empty
assert_symlink "$ancestor_link"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# --- Build/update/reset and re-clone boundaries ---

setup_case build-directory-symlink
mkdir -p "$cache_root"
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/checkout" "$entry_path"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_no_cache_mutation_commands
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-reclone-symlink
mkdir -p "$cache_root"
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/checkout" "$entry_path"
export JPACKER_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_no_cache_mutation_commands
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# --- Regular paths retain fetch/build/re-clone behavior ---

setup_case regular-existing-fetch
create_regular_repo "$entry_path"
run_ok "$case_dir/output" fetch clean-root
assert_command "git config --get remote.origin.url"
assert_command "git fetch origin"

setup_case regular-missing-fetch
run_ok "$case_dir/output" fetch clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
if [ ! -d "$entry_path/.git" ]; then
    fail "regular missing fetch did not create a repository: $entry_path"
fi

setup_case regular-existing-build
create_regular_repo "$entry_path"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git config --get remote.origin.url"
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "makepkg -sic"

setup_case regular-remote-mismatch
create_regular_repo "$entry_path"
printf 'old clone marker\n' > "$entry_path/old-marker"
export JPACKER_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git config --get remote.origin.url"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg -sic"
assert_command_absent "git fetch origin"
assert_command_absent "git reset --hard origin/main"
if [ -e "$entry_path/old-marker" ]; then
    fail "remote mismatch did not replace the old regular clone"
fi
if [ ! -d "$entry_path/.git" ]; then
    fail "remote mismatch re-clone did not create a repository"
fi

# --- Clone failure rollback for both fetch and build call sites ---

setup_case fetch-clone-failure-rollback
printf 'outside sibling marker\n' > "$outside_dir/marker"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export JPACKER_TEST_GIT_CLONE_EXIT_CODE=42
run_fail "$case_dir/output" fetch clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-failure-rollback
printf 'outside sibling marker\n' > "$outside_dir/marker"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export JPACKER_TEST_GIT_CLONE_EXIT_CODE=42
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "makepkg -sic"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# clone failureがsymlinkを残しても、destructorは再検証でoutside cleanupを拒否する。
setup_case fetch-clone-symlink-rollback-refusal
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET=$outside_dir/checkout
export JPACKER_TEST_GIT_CLONE_EXIT_CODE=42
run_fail "$case_dir/output" fetch clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_contains "Refusing unsafe clone rollback" "$case_dir/output"
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# --- Cache cleanup preflight and safe-path ordering ---

setup_case clean-entry-symlink
mkdir -p "$cache_root"
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$outside_dir/checkout" "$entry_path"
run_clean_tty_fail "$case_dir/output"
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_log_empty
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case clean-cache-root-symlink
root_target=$outside_dir/cache-root
create_checkout "$root_target/clean-root"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
ln -s "$root_target" "$cache_root"
run_clean_tty_fail "$case_dir/output"
assert_symlink_rejection "$case_dir/output" "$cache_root"
assert_log_empty
assert_symlink "$cache_root"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case regular-clean
mkdir -p "$cache_root/regular-entry"
printf 'cached build content\n' > "$cache_root/regular-entry/artifact"
run_clean_tty_ok "$case_dir/output"
assert_only_command "sudo pacman -Sc"
assert_path_absent "$cache_root/regular-entry"
if [ ! -f "$cache_root/jpacker.log" ]; then
    fail "regular clean removed the jpacker log"
fi
assert_contains "jpacker cache cleaned." "$case_dir/output"
assert_output_before "Running: sudo pacman" "Clean jpacker build cache" "$case_dir/output"

echo "build cache symlink integration tests: all checks passed"
