#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"
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
ln -s "$test_binary" "$tmp_dir/moguet-test"
test_runner=$tmp_dir/moguet-test

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
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"
export MOGUET_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:$port/rpc/
export MOGUET_TEST_PACMAN_EXIT_CODE=1
export MOGUET_TEST_SUDO_EXIT_CODE=0

fail() {
    echo "$*" >&2
    exit 1
}

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    home_dir=$case_dir/home
    xdg_state_dir=$case_dir/xdg-state
    xdg_cache_dir=$case_dir/xdg-cache
    outside_dir=$case_dir/outside
    command_log=$case_dir/commands.log
    editor_argv_log=$case_dir/editor-argv.log

    mkdir -p "$home_dir" "$xdg_state_dir" "$xdg_cache_dir" "$outside_dir"
    : > "$command_log"
    : > "$editor_argv_log"
    export HOME=$home_dir
    export XDG_STATE_HOME=$xdg_state_dir
    export XDG_CACHE_HOME=$xdg_cache_dir
    export MOGUET_TEST_COMMAND_LOG=$command_log
    export MOGUET_TEST_EDITOR_ARGV_LOG=$editor_argv_log
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
    unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
    unset MOGUET_TEST_EDITOR_REPLACE_TARGET
    unset MOGUET_TEST_EDITOR_REMOVE_TARGET
    unset MOGUET_TEST_EDITOR_SYMLINK_TARGET
    unset MOGUET_TEST_EDITOR_EXIT_CODE
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_PACMAN_REPO_PACKAGES
    unset MOGUET_TEST_PACKAGE_BUILD_DIR
    unset MOGUET_TEST_MAKEPKG_EXIT_CODE
    unset EDITOR

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

run_build_tty_ok() {
    output_file=$1
    answers=$2
    : > "$command_log"
    if ! printf '%b' "$answers" |
        script -qec "$test_runner --nodiff build clean-root" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive build to succeed" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_build_tty_fail() {
    output_file=$1
    answers=$2
    : > "$command_log"
    if printf '%b' "$answers" |
        script -qec "$test_runner --nodiff build clean-root" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive build to fail" >&2
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

assert_not_contains() {
    pattern=$1
    file=$2
    if grep -F -- "$pattern" "$file" >/dev/null; then
        echo "unexpected output: $pattern" >&2
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

assert_editor_argv_log() {
    expected=$1
    actual=$(cat "$editor_argv_log")
    if [ "$actual" != "$expected" ]; then
        echo "unexpected editor argv" >&2
        printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
        exit 1
    fi
}

assert_editor_targets_not_option_like() {
    if grep -E '^target=<-' "$editor_argv_log" >/dev/null; then
        echo "editor review target remained option-like" >&2
        cat "$editor_argv_log" >&2
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
    if grep -E '^(git|moguet-test-editor|makepkg|sudo) ' "$command_log" >/dev/null; then
        echo "unsafe cache path reached a git/build/install command" >&2
        cat "$command_log" >&2
        exit 1
    fi
    if grep '^pacman ' "$command_log" | grep -v '^pacman -Si ' >/dev/null; then
        echo "unsafe cache path reached a pacman mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_build_or_install_commands() {
    if grep -E '^(makepkg|sudo) ' "$command_log" >/dev/null; then
        echo "unsafe reviewed artifact reached a build/install command" >&2
        cat "$command_log" >&2
        exit 1
    fi
    if grep '^pacman ' "$command_log" | grep -v '^pacman -Si ' >/dev/null; then
        echo "unsafe reviewed artifact reached a pacman mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_only_clone_after_metadata() {
    expected_clone=$1
    assert_command "$expected_clone"
    while IFS= read -r command; do
        case $command in
            pacman\ -Si\ *)
                ;;
            pacman-conf\ --verbose\ RootDir\ DBPath)
                ;;
            "$expected_clone")
                ;;
            *)
                echo "unsafe cloned checkout reached an unexpected command" >&2
                cat "$command_log" >&2
                exit 1
                ;;
        esac
    done < "$command_log"
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

assert_descendant_rejection() {
    output_file=$1
    rejected_path=$2
    reason=$3
    # pathとreasonを結合して確認し、case directory名によるreasonの偽陽性を防ぐ。
    assert_contains "$rejected_path: $reason" "$output_file"
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
    printf 'pkgname=moguet-test-fixture\npkgver=1\npkgrel=1\n' > "$repo_dir/PKGBUILD"
}

create_clone_fixture() {
    clone_fixture=$case_dir/clone-fixture
    mkdir -p "$clone_fixture/.git"
    printf 'pkgname=moguet-test-fixture\npkgver=1\npkgrel=1\n' > "$clone_fixture/PKGBUILD"
    export MOGUET_TEST_GIT_CLONE_FIXTURE_DIR=$clone_fixture
}

snapshot_directory() {
    snapshot_dir=$1
    snapshot_file=$2
    (
        CDPATH= cd "$snapshot_dir"
        find . -exec stat --printf \
            'entry type=%F mode=%f uid=%u gid=%g size=%s mtime=%y ctime=%z path=%n target=%N\n' -- {} \;
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
# is below the legacy jpacker path component, while its resolved checkout is
# outside the trusted root.
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
# the legacy jpacker path component. Component-based containment and the
# symlink policy must reject it.
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

# --- Persistent checkout descendant boundaries ---

setup_case fetch-git-directory-symlink
mkdir -p "$entry_path" "$outside_dir/external.git"
printf 'external git metadata\n' > "$outside_dir/external.git/marker"
printf 'pkgname=clean-root\n' > "$entry_path/PKGBUILD"
ln -s "$outside_dir/external.git" "$entry_path/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" fetch clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "symlink."
assert_log_empty
assert_symlink "$entry_path/.git"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-git-dangling-symlink
mkdir -p "$entry_path"
printf 'pkgname=clean-root\n' > "$entry_path/PKGBUILD"
ln -s "$outside_dir/missing.git" "$entry_path/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "symlink."
assert_no_cache_mutation_commands
assert_symlink "$entry_path/.git"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-absolute-external-gitdir
mkdir -p "$entry_path" "$outside_dir/external.git"
printf 'external git metadata\n' > "$outside_dir/external.git/marker"
printf 'pkgname=clean-root\n' > "$entry_path/PKGBUILD"
printf 'gitdir: %s\n' "$outside_dir/external.git" > "$entry_path/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" fetch clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "gitfile / redirect."
assert_not_contains "$outside_dir/external.git" "$case_dir/output"
assert_log_empty
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-relative-external-gitdir
mkdir -p "$entry_path" "$outside_dir/external.git"
printf 'external git metadata\n' > "$outside_dir/external.git/marker"
printf 'pkgname=clean-root\n' > "$entry_path/PKGBUILD"
printf 'gitdir: ../../../outside/external.git\n' > "$entry_path/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "gitfile / redirect."
assert_not_contains "$outside_dir/external.git" "$case_dir/output"
assert_no_cache_mutation_commands
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-pkgbuild-symlink
create_regular_repo "$entry_path"
printf 'external PKGBUILD\n' > "$outside_dir/PKGBUILD"
rm "$entry_path/PKGBUILD"
ln -s "$outside_dir/PKGBUILD" "$entry_path/PKGBUILD"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "symlink."
assert_no_cache_mutation_commands
assert_symlink "$entry_path/PKGBUILD"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-pkgbuild-dangling-symlink
create_regular_repo "$entry_path"
rm "$entry_path/PKGBUILD"
ln -s "$outside_dir/missing-PKGBUILD" "$entry_path/PKGBUILD"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "symlink."
assert_no_cache_mutation_commands
assert_symlink "$entry_path/PKGBUILD"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-install-symlink
create_regular_repo "$entry_path"
printf 'external install script\n' > "$outside_dir/clean-root.install"
ln -s "$outside_dir/clean-root.install" "$entry_path/clean-root.install"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/clean-root.install" "symlink."
assert_no_cache_mutation_commands
assert_symlink "$entry_path/clean-root.install"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-pkgbuild-directory
create_regular_repo "$entry_path"
rm "$entry_path/PKGBUILD"
mkdir "$entry_path/PKGBUILD"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "non-regular file."
assert_no_cache_mutation_commands

setup_case build-install-fifo
create_regular_repo "$entry_path"
mkfifo "$entry_path/clean-root.install"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/clean-root.install" "non-regular file."
assert_no_cache_mutation_commands

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
export MOGUET_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_symlink_rejection "$case_dir/output" "$entry_path"
assert_no_cache_mutation_commands
assert_symlink "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# --- Review-time replacement and successful-clone descendant boundaries ---

setup_case build-review-pkgbuild-replaced
create_regular_repo "$entry_path"
printf 'external reviewed artifact\n' > "$outside_dir/PKGBUILD"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export EDITOR=$repo_root/tests/stubs/moguet-test-editor
export MOGUET_TEST_EDITOR_REPLACE_TARGET=./PKGBUILD
export MOGUET_TEST_EDITOR_SYMLINK_TARGET=$outside_dir/PKGBUILD
run_build_tty_fail "$case_dir/output" 'y\n'
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "symlink."
assert_command "moguet-test-editor ./PKGBUILD"
assert_command_before "git reset --hard origin/main" "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands
assert_symlink "$entry_path/PKGBUILD"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-review-install-removed
create_regular_repo "$entry_path"
printf 'post_install() { :; }\n' > "$entry_path/clean-root.install"
export EDITOR=$repo_root/tests/stubs/moguet-test-editor
export MOGUET_TEST_EDITOR_REMOVE_TARGET=clean-root.install
run_build_tty_fail "$case_dir/output" 'y\n'
assert_descendant_rejection "$case_dir/output" "$entry_path/clean-root.install" "non-regular file."
assert_command "moguet-test-editor ./PKGBUILD"
assert_command_absent "moguet-test-editor ./clean-root.install"
assert_no_build_or_install_commands
assert_path_absent "$entry_path/clean-root.install"

setup_case fetch-clone-unsafe-git-symlink
create_clone_fixture
rmdir "$clone_fixture/.git"
mkdir -p "$outside_dir/external.git"
printf 'external git metadata\n' > "$outside_dir/external.git/marker"
ln -s "$outside_dir/external.git" "$clone_fixture/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" fetch clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-clone-unsafe-pkgbuild-symlink
create_clone_fixture
printf 'external PKGBUILD\n' > "$outside_dir/PKGBUILD"
rm "$clone_fixture/PKGBUILD"
ln -s "$outside_dir/PKGBUILD" "$clone_fixture/PKGBUILD"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" fetch clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-clone-unsafe-install-symlink
create_clone_fixture
printf 'external install script\n' > "$outside_dir/clean-root.install"
ln -s "$outside_dir/clean-root.install" "$clone_fixture/clean-root.install"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" fetch clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/clean-root.install" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-unsafe-git-symlink
create_clone_fixture
rmdir "$clone_fixture/.git"
mkdir -p "$outside_dir/external.git"
printf 'external git metadata\n' > "$outside_dir/external.git/marker"
ln -s "$outside_dir/external.git" "$clone_fixture/.git"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/.git" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-unsafe-pkgbuild-symlink
create_clone_fixture
printf 'external PKGBUILD\n' > "$outside_dir/PKGBUILD"
rm "$clone_fixture/PKGBUILD"
ln -s "$outside_dir/PKGBUILD" "$clone_fixture/PKGBUILD"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/PKGBUILD" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-unsafe-install-symlink
create_clone_fixture
printf 'external install script\n' > "$outside_dir/clean-root.install"
ln -s "$outside_dir/clean-root.install" "$clone_fixture/clean-root.install"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_descendant_rejection "$case_dir/output" "$entry_path/clean-root.install" "symlink."
assert_only_clone_after_metadata "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case fetch-clone-remote-mismatch-rollback
create_clone_fixture
printf 'outside sibling marker\n' > "$outside_dir/marker"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export MOGUET_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail "$case_dir/output" fetch clean-root
assert_contains "Remote URL mismatch for clean-root: https://example.invalid/wrong.git" "$case_dir/output"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "git config --get remote.origin.url"
assert_command_before "git clone https://aur.archlinux.org/clean-root.git clean-root" "git config --get remote.origin.url"
assert_command_absent "git fetch origin"
assert_command_absent "git reset --hard origin/main"
assert_command_absent "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-remote-mismatch-rollback
create_clone_fixture
printf 'outside sibling marker\n' > "$outside_dir/marker"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export MOGUET_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_contains "Remote URL mismatch for clean-root" "$case_dir/output"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "git config --get remote.origin.url"
assert_command_before "git clone https://aur.archlinux.org/clean-root.git clean-root" "git config --get remote.origin.url"
assert_command_absent "git fetch origin"
assert_command_absent "git reset --hard origin/main"
assert_command_absent "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-valid-descendants
create_clone_fixture
printf 'post_install() { :; }\n' > "$clone_fixture/clean-root.install"
export MOGUET_TEST_MAKEPKG_EXIT_CODE=0
run_ok "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "git config --get remote.origin.url"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "git clone https://aur.archlinux.org/clean-root.git clean-root" "git config --get remote.origin.url"
assert_command_before "git config --get remote.origin.url" "makepkg --packagelist"
if [ ! -d "$entry_path/.git" ] || [ -L "$entry_path/.git" ] ||
   [ ! -f "$entry_path/PKGBUILD" ] || [ -L "$entry_path/PKGBUILD" ] ||
   [ ! -f "$entry_path/clean-root.install" ] || [ -L "$entry_path/clean-root.install" ]; then
    fail "valid clone descendants were not retained as regular entries: $entry_path"
fi

# --- Regular paths retain fetch/build/re-clone behavior ---

setup_case regular-existing-fetch
create_regular_repo "$entry_path"
run_ok "$case_dir/output" fetch clean-root
assert_command "git config --get remote.origin.url"
assert_command "git fetch origin"
assert_command_before "git config --get remote.origin.url" "git fetch origin"
assert_command_absent "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "git reset --hard origin/main"
assert_command_absent "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands

setup_case regular-missing-fetch
run_ok "$case_dir/output" fetch clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "git config --get remote.origin.url"
assert_command_before "git clone https://aur.archlinux.org/clean-root.git clean-root" "git config --get remote.origin.url"
assert_command_absent "git fetch origin"
assert_command_absent "git reset --hard origin/main"
assert_command_absent "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands
if [ ! -d "$entry_path/.git" ] || [ -L "$entry_path/.git" ] ||
   [ ! -f "$entry_path/PKGBUILD" ] || [ -L "$entry_path/PKGBUILD" ]; then
    fail "regular missing fetch did not create a repository: $entry_path"
fi

setup_case regular-existing-build
create_regular_repo "$entry_path"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git config --get remote.origin.url"
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "makepkg --packagelist"
assert_command_before "git config --get remote.origin.url" "git fetch origin"
assert_command_before "git fetch origin" "git reset --hard origin/main"
assert_command_before "git reset --hard origin/main" "makepkg --packagelist"

setup_case regular-existing-review
create_regular_repo "$entry_path"
printf 'post_install() { :; }\n' > "$entry_path/clean-root.install"
export EDITOR=$repo_root/tests/stubs/moguet-test-editor
export MOGUET_TEST_MAKEPKG_EXIT_CODE=0
run_build_tty_ok "$case_dir/output" 'y\ny\ny\n'
assert_contains "Review target: PKGBUILD" "$case_dir/output"
assert_contains "clean-root.install" "$case_dir/output"
assert_command "moguet-test-editor ./PKGBUILD"
assert_command "moguet-test-editor ./clean-root.install"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "git reset --hard origin/main" "moguet-test-editor ./PKGBUILD"
assert_command_before "moguet-test-editor ./PKGBUILD" "moguet-test-editor ./clean-root.install"
assert_command_before "moguet-test-editor ./clean-root.install" "makepkg --packagelist"
assert_editor_argv_log 'argv-begin
arg[0]=<./PKGBUILD>
target=<./PKGBUILD>
argv-end
argv-begin
arg[0]=<./clean-root.install>
target=<./clean-root.install>
argv-end'
assert_editor_targets_not_option_like

setup_case regular-existing-leading-hyphen-review
create_regular_repo "$entry_path"
printf 'post_install() { :; }\n' > "$entry_path/-option.install"
export EDITOR=$repo_root/tests/stubs/moguet-test-editor
export MOGUET_TEST_MAKEPKG_EXIT_CODE=0
run_build_tty_ok "$case_dir/output" 'y\ny\ny\n'
assert_contains "-option.install" "$case_dir/output"
assert_command "moguet-test-editor ./PKGBUILD"
assert_command "moguet-test-editor ./-option.install"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "moguet-test-editor ./PKGBUILD" "moguet-test-editor ./-option.install"
assert_command_before "moguet-test-editor ./-option.install" "makepkg --packagelist"
assert_editor_argv_log 'argv-begin
arg[0]=<./PKGBUILD>
target=<./PKGBUILD>
argv-end
argv-begin
arg[0]=<./-option.install>
target=<./-option.install>
argv-end'
assert_editor_targets_not_option_like

setup_case regular-existing-leading-hyphen-editor-failure
create_regular_repo "$entry_path"
printf 'post_install() { :; }\n' > "$entry_path/-option.install"
export EDITOR=$repo_root/tests/stubs/moguet-test-editor
export MOGUET_TEST_EDITOR_EXIT_CODE=42
run_build_tty_fail "$case_dir/output" 'n\ny\n'
assert_contains "Editor failed." "$case_dir/output"
assert_command "moguet-test-editor ./-option.install"
assert_no_build_or_install_commands
assert_editor_argv_log 'argv-begin
arg[0]=<./-option.install>
target=<./-option.install>
argv-end'
assert_editor_targets_not_option_like

setup_case fetch-existing-remote-mismatch
create_regular_repo "$entry_path"
printf 'old clone marker\n' > "$entry_path/old-marker"
printf 'https://example.invalid/wrong.git\n' > "$entry_path/.git/.moguet-test-remote-url"
run_fail "$case_dir/output" fetch clean-root
assert_contains "Remote URL mismatch for clean-root: https://example.invalid/wrong.git" "$case_dir/output"
assert_command "git config --get remote.origin.url"
assert_command_absent "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "git fetch origin"
assert_command_absent "git reset --hard origin/main"
assert_command_absent "moguet-test-editor ./PKGBUILD"
assert_no_build_or_install_commands
assert_contains "old clone marker" "$entry_path/old-marker"
if [ ! -d "$entry_path/.git" ]; then
    fail "fetch remote mismatch removed the existing checkout"
fi

setup_case build-existing-remote-mismatch
create_regular_repo "$entry_path"
printf 'old clone marker\n' > "$entry_path/old-marker"
printf 'https://example.invalid/wrong.git\n' > "$entry_path/.git/.moguet-test-remote-url"
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_contains "Remote URL mismatch. Re-cloning..." "$case_dir/output"
assert_output_before "Remote URL mismatch. Re-cloning..." "Running: git clone" "$case_dir/output"
assert_command "git config --get remote.origin.url"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg --packagelist"
assert_command_before "git config --get remote.origin.url" "git clone https://aur.archlinux.org/clean-root.git clean-root"
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
export MOGUET_TEST_GIT_CLONE_EXIT_CODE=42
run_fail "$case_dir/output" fetch clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

setup_case build-clone-failure-rollback
printf 'outside sibling marker\n' > "$outside_dir/marker"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export MOGUET_TEST_GIT_CLONE_EXIT_CODE=42
run_fail "$case_dir/output" --noedit --nodiff build clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "makepkg --packagelist"
assert_command_absent "makepkg -sc"
assert_path_absent "$entry_path"
assert_directory_unchanged "$outside_dir" "$case_dir/before.snapshot"

# clone failureがsymlinkを残しても、destructorは再検証でoutside cleanupを拒否する。
setup_case fetch-clone-symlink-rollback-refusal
create_checkout "$outside_dir/checkout"
snapshot_directory "$outside_dir" "$case_dir/before.snapshot"
export MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET=$outside_dir/checkout
export MOGUET_TEST_GIT_CLONE_EXIT_CODE=42
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
legacy_log=$cache_root/jpacker.log
printf 'legacy log sentinel\n' > "$legacy_log"
chmod 0640 "$legacy_log"
legacy_log_checksum=$(cksum "$legacy_log")
legacy_log_mode=$(stat -c '%a' -- "$legacy_log")
legacy_log_identity=$(stat -c '%d:%i' -- "$legacy_log")
run_clean_tty_ok "$case_dir/output"
assert_only_command "sudo pacman -Sc"
assert_path_absent "$cache_root/regular-entry"
if [ ! -f "$legacy_log" ]; then
    fail "regular clean removed the legacy jpacker.log file"
fi
if [ "$(cksum "$legacy_log")" != "$legacy_log_checksum" ]; then
    fail "regular clean changed the legacy jpacker.log content"
fi
if [ "$(stat -c '%a' -- "$legacy_log")" != "$legacy_log_mode" ]; then
    fail "regular clean changed the legacy jpacker.log mode"
fi
if [ "$(stat -c '%d:%i' -- "$legacy_log")" != "$legacy_log_identity" ]; then
    fail "regular clean replaced the legacy jpacker.log inode"
fi
assert_contains "Moguet cache cleaned." "$case_dir/output"
assert_output_before "Running: sudo pacman" "Clean Moguet build cache" "$case_dir/output"

echo "build cache symlink integration tests: all checks passed"
