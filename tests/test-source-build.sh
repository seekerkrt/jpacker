#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

# Interactive diff/update-status branches are exercised through a real PTY,
# without adding prompt controls to the production API.
if ! command -v script >/dev/null 2>&1; then
    echo "script(1) is required for source-build tests" >&2
    exit 1
fi
ln -s "$test_binary" "$tmp_dir/jpacker-test"
test_runner=$tmp_dir/jpacker-test

export PATH=$repo_root/tests/stubs:/usr/bin:/bin
official_url=https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output
    checkout_dir=$case_dir/xdg-cache/jpacker/clean-root

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=0
    export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
    unset JPACKER_TEST_PACMAN_Q_OUTPUT
    unset JPACKER_TEST_PACMAN_Q_EXIT_CODE
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_GIT_SYMBOLIC_REF
    unset JPACKER_TEST_GIT_SYMBOLIC_REF_EXIT_CODE
    unset JPACKER_TEST_GIT_MAIN_REF_EXIT_CODE
    unset JPACKER_TEST_GIT_MASTER_REF_EXIT_CODE
    unset JPACKER_TEST_GIT_DIFF_QUIET_EXIT_CODE
    unset JPACKER_TEST_GIT_DIFF_NAME_ONLY_EXIT_CODE
    unset JPACKER_TEST_GIT_DIFF_COLOR_EXIT_CODE
    unset JPACKER_TEST_GIT_CHANGED_FILES
    unset JPACKER_TEST_VERCMP_OUTPUT
    unset JPACKER_TEST_VERCMP_EXIT_CODE
    unset EDITOR
}

create_existing_checkout() {
    mkdir -p "$checkout_dir/.git"
    printf 'pkgname=clean-root\npkgver=1\npkgrel=1\n' > "$checkout_dir/PKGBUILD"
    printf '%s\n' "$official_url" > "$checkout_dir/.git/.jpacker-test-remote-url"
}

prepare_upgrade_case() {
    create_existing_checkout
    : > "$JPACKER_TEST_PACKAGE_BUILD_DIR/clean-root"
    export JPACKER_TEST_PACMAN_Q_OUTPUT='clean-root 1.0-1'
}

write_srcinfo() {
    version=$1
    release=$2
    {
        printf 'pkgbase = clean-root\n'
        printf 'pkgver = %s\n' "$version"
        printf 'pkgrel = %s\n' "$release"
    } > "$checkout_dir/.SRCINFO"
}

run_ok() {
    : > "$command_log"
    if ! "$test_runner" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    if "$test_runner" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_tty_ok() {
    answers=$1
    shift
    : > "$command_log"
    if ! printf '%b' "$answers" |
        script -qec "$test_runner $*" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive command to succeed: $*" >&2
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

assert_command_prefix_absent() {
    unexpected=$1
    if grep -F -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command content: $unexpected" >&2
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

assert_checkout_retained() {
    if [ ! -d "$checkout_dir/.git" ] || [ -L "$checkout_dir/.git" ] ||
       [ ! -f "$checkout_dir/PKGBUILD" ] || [ -L "$checkout_dir/PKGBUILD" ]; then
        echo "validated checkout was not retained: $checkout_dir" >&2
        exit 1
    fi
}

# P0-1: origin/HEAD takes precedence, followed by main and then master.
setup_case branch-origin-head
create_existing_checkout
export JPACKER_TEST_GIT_SYMBOLIC_REF=origin/trunk
run_ok --noedit --nodiff build clean-root
assert_contains "Detected branch: trunk" "$output_file"
assert_command "git reset --hard origin/trunk"

setup_case branch-main
create_existing_checkout
run_ok --noedit --nodiff build clean-root
assert_contains "Detected branch: main" "$output_file"
assert_command "git show-ref --verify --quiet refs/remotes/origin/main"
assert_command "git reset --hard origin/main"

setup_case branch-master
create_existing_checkout
export JPACKER_TEST_GIT_MAIN_REF_EXIT_CODE=1
run_ok --noedit --nodiff build clean-root
assert_contains "Detected branch: master" "$output_file"
assert_command "git show-ref --verify --quiet refs/remotes/origin/main"
assert_command "git show-ref --verify --quiet refs/remotes/origin/master"
assert_command "git reset --hard origin/master"

# P0-2: the changed-diff prompt controls display only; reset always follows.
setup_case changed-diff-yes
create_existing_checkout
export JPACKER_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export JPACKER_TEST_GIT_CHANGED_FILES='PKGBUILD\nclean-root.install\n'
run_tty_ok 'y\n' --noedit build clean-root
assert_contains "Update diff range: HEAD..origin/main (existing cache repository)." "$output_file"
assert_contains "Review-sensitive file changes: PKGBUILD, clean-root.install" "$output_file"
assert_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command "git diff HEAD..origin/main --color=always"
assert_command "git reset --hard origin/main"
assert_command_before "git diff HEAD..origin/main --color=always" "git reset --hard origin/main"

setup_case changed-diff-no
create_existing_checkout
export JPACKER_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export JPACKER_TEST_GIT_CHANGED_FILES='PKGBUILD\n'
run_tty_ok 'n\n' --noedit build clean-root
assert_contains "Update diff range: HEAD..origin/main (existing cache repository)." "$output_file"
assert_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command_absent "git diff HEAD..origin/main --color=always"
assert_command "git reset --hard origin/main"

setup_case changed-diff-nodiff
create_existing_checkout
export JPACKER_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export JPACKER_TEST_GIT_CHANGED_FILES='PKGBUILD\n'
run_ok --noedit --nodiff build clean-root
assert_not_contains "Update diff range:" "$output_file"
assert_not_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command_prefix_absent "git diff "
assert_command "git reset --hard origin/main"

# P0-3: only_if_updated runs after fetch/reset and preserves all unknown-status branches.
setup_case update-newer
prepare_upgrade_case
write_srcinfo 2.0 1
export JPACKER_TEST_VERCMP_OUTPUT=1
run_ok --noedit --nodiff upgrade
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "vercmp 2.0-1 1.0-1"
assert_command "makepkg -sic"
assert_command_before "git reset --hard origin/main" "vercmp 2.0-1 1.0-1"
assert_command_before "vercmp 2.0-1 1.0-1" "makepkg -sic"

setup_case update-up-to-date
prepare_upgrade_case
write_srcinfo 1.0 1
export JPACKER_TEST_VERCMP_OUTPUT=0
run_ok --noedit --nodiff upgrade
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "vercmp 1.0-1 1.0-1"
assert_contains "clean-root is up to date (1.0-1). Skipping." "$output_file"
assert_command_prefix_absent "makepkg "
assert_command_prefix_absent "jpacker-test-editor "

setup_case update-unknown-noconfirm
prepare_upgrade_case
run_ok --noedit --nodiff --noconfirm upgrade
assert_contains "Unable to determine update status from .SRCINFO for clean-root." "$output_file"
assert_contains "Skipping clean-root: update status is unknown and --noconfirm is set." "$output_file"
assert_command "git reset --hard origin/main"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-noninteractive
prepare_upgrade_case
run_ok --noedit --nodiff upgrade
assert_contains "Unable to determine update status from .SRCINFO for clean-root." "$output_file"
assert_contains "Skipping clean-root: update status is unknown and stdin is non-interactive." "$output_file"
assert_command "git reset --hard origin/main"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-interactive-no
prepare_upgrade_case
run_tty_ok 'n\n' --noedit --nodiff upgrade
assert_contains "Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?" "$output_file"
assert_command "git reset --hard origin/main"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-interactive-yes
prepare_upgrade_case
run_tty_ok 'y\n' --noedit --nodiff upgrade
assert_contains "Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?" "$output_file"
assert_command "git reset --hard origin/main"
assert_command "makepkg -sic"
assert_command_before "git reset --hard origin/main" "makepkg -sic"

# P0-4: clone ownership ends after validation; a later makepkg failure keeps the checkout.
setup_case makepkg-failure-retains-checkout
export JPACKER_TEST_MAKEPKG_EXIT_CODE=42
run_fail --noedit --nodiff build clean-root
assert_contains "Build Error: Build failed." "$output_file"
assert_command "git clone $official_url clean-root"
assert_command "makepkg -sic"
assert_command_before "git clone $official_url clean-root" "makepkg -sic"
assert_checkout_retained

echo "source-build characterization tests: all checks passed"
