#!/bin/sh
set -eu

test_binary=$1
config_test_binary=$2
upgrade_metadata_test_binary=$3
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"
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
ln -s "$test_binary" "$tmp_dir/moguet-test"
ln -s "$config_test_binary" "$tmp_dir/moguet-config-test"
ln -s "$upgrade_metadata_test_binary" "$tmp_dir/moguet-upgrade-metadata-test"
test_runner=$tmp_dir/moguet-test
config_test_runner=$tmp_dir/moguet-config-test
upgrade_metadata_test_runner=$tmp_dir/moguet-upgrade-metadata-test

export PATH=$repo_root/tests/stubs:/usr/bin:/bin
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"
upgrade_metadata_path=$repo_root/tests/stubs/upgrade-baseline-metadata:$PATH
(
    PATH=$upgrade_metadata_path
    export PATH
    require_exact_test_command pacman-conf \
        "$repo_root/tests/stubs/upgrade-baseline-metadata/pacman-conf"
)
official_url=https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    editor_argv_log=$case_dir/editor-argv.log
    output_file=$case_dir/output
    config_file=$case_dir/jpacker.conf
    checkout_dir=$case_dir/xdg-cache/jpacker/clean-root
    package_metadata_state=$case_dir/package-metadata-state

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    : > "$editor_argv_log"
    : > "$package_metadata_state"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export MOGUET_TEST_COMMAND_LOG=$command_log
    export MOGUET_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export MOGUET_TEST_PACMAN_REPO_PACKAGES=clean-root
    export MOGUET_TEST_PACMAN_EXIT_CODE=1
    export MOGUET_TEST_SUDO_EXIT_CODE=0
    export MOGUET_TEST_MAKEPKG_EXIT_CODE=0
    export MOGUET_TEST_PACKAGE_METADATA_STATE_FILE=$package_metadata_state
    export MOGUET_TEST_PACKAGE_METADATA_EVENT_LOG=$command_log
    unset MOGUET_TEST_PACMAN_Q_OUTPUT
    unset MOGUET_TEST_PACMAN_Q_EXIT_CODE
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
    unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
    unset MOGUET_TEST_GIT_SYMBOLIC_REF
    unset MOGUET_TEST_GIT_SYMBOLIC_REF_EXIT_CODE
    unset MOGUET_TEST_GIT_MAIN_REF_EXIT_CODE
    unset MOGUET_TEST_GIT_MASTER_REF_EXIT_CODE
    unset MOGUET_TEST_GIT_DIFF_QUIET_EXIT_CODE
    unset MOGUET_TEST_GIT_DIFF_NAME_ONLY_EXIT_CODE
    unset MOGUET_TEST_GIT_DIFF_COLOR_EXIT_CODE
    unset MOGUET_TEST_GIT_CHANGED_FILES
    unset MOGUET_TEST_VERCMP_OUTPUT
    unset MOGUET_TEST_VERCMP_EXIT_CODE
    unset EDITOR
    unset MOGUET_TEST_CONFIG_FILE
    unset MOGUET_TEST_EDITOR_ARGV_LOG
    unset MOGUET_TEST_EDITOR_EXIT_CODE
    unset MOGUET_TEST_EDITOR_REPLACE_TARGET
    unset MOGUET_TEST_EDITOR_REMOVE_TARGET
    unset MOGUET_TEST_EDITOR_SYMLINK_TARGET
    unset MOGUET_TEST_APP_CONFIG_CASE
    unset MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE
    unset MOGUET_TEST_PACKAGE_METADATA_INITIALIZE_FAILURE_AT
    unset MOGUET_TEST_PACKAGE_METADATA_QUERY_FAILURE_PACKAGE
    unset MOGUET_TEST_PACKAGE_METADATA_QUERY_FAILURE_AT
    unset MOGUET_TEST_PACKAGE_METADATA_UNKNOWN_REASON_PACKAGE
    unset MOGUET_TEST_PACKAGE_METADATA_PACMAN_CONF_EXIT_CODE
    unset MOGUET_TEST_PACKAGE_METADATA_PACMAN_CONF_FAILURE_AT
    unset MOGUET_TEST_MAKEPKG_PACKAGE_METADATA_STATE_AFTER_SUCCESS_FILE
    unset MOGUET_TEST_MAKEPKG_PACKAGELIST_EXIT_CODE
    unset MOGUET_TEST_MAKEPKG_PACKAGELIST_OUTPUT_FILE
}

create_existing_checkout() {
    mkdir -p "$checkout_dir/.git"
    printf 'pkgname=clean-root\npkgver=1\npkgrel=1\n' > "$checkout_dir/PKGBUILD"
    printf '%s\n' "$official_url" > "$checkout_dir/.git/.moguet-test-remote-url"
}

prepare_upgrade_case() {
    create_existing_checkout
    : > "$MOGUET_TEST_PACKAGE_BUILD_DIR/clean-root"
    printf 'clean-root 1.0-1\n' > "$package_metadata_state"
    export MOGUET_TEST_PACMAN_Q_OUTPUT='clean-root 1.0-1'
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

write_editor_config() {
    editor_command=$1
    {
        printf 'EDITOR=%s\n' "$editor_command"
        printf '%s\n' 'NODIFF=true'
    } > "$config_file"
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

run_upgrade_ok() {
    : > "$command_log"
    if ! PATH=$upgrade_metadata_path "$upgrade_metadata_test_runner" "$@" > "$output_file" 2>&1; then
        echo "expected upgrade metadata command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
    # POLICY(#152): upgrade characterization全体でlegacy installed-version queryを禁止する。
    assert_command_prefix_absent "pacman -Q "
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

run_upgrade_tty_ok() {
    answers=$1
    shift
    : > "$command_log"
    if ! printf '%b' "$answers" |
        PATH=$upgrade_metadata_path script -qec "$upgrade_metadata_test_runner $*" /dev/null > "$output_file" 2>&1; then
        echo "expected interactive upgrade metadata command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
    assert_command_prefix_absent "pacman -Q "
}

run_config_tty_ok() {
    answers=$1
    shift
    : > "$command_log"
    if ! printf '%b' "$answers" |
        script -qec "$config_test_runner $*" /dev/null > "$output_file" 2>&1; then
        echo "expected config-aware interactive command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_config_tty_fail() {
    answers=$1
    shift
    : > "$command_log"
    if printf '%b' "$answers" |
        script -qec "$config_test_runner $*" /dev/null > "$output_file" 2>&1; then
        echo "expected config-aware interactive command to fail: $*" >&2
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

assert_editor_argv_log() {
    expected=$1
    expected_file=$case_dir/expected-editor-argv.log
    printf '%s\n' "$expected" > "$expected_file"
    if ! cmp "$expected_file" "$editor_argv_log" >/dev/null 2>&1; then
        echo "unexpected editor argv log" >&2
        echo "expected:" >&2
        sed -n '1,240p' "$expected_file" >&2
        echo "actual:" >&2
        sed -n '1,240p' "$editor_argv_log" >&2
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
export MOGUET_TEST_GIT_SYMBOLIC_REF=origin/trunk
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
export MOGUET_TEST_GIT_MAIN_REF_EXIT_CODE=1
run_ok --noedit --nodiff build clean-root
assert_contains "Detected branch: master" "$output_file"
assert_command "git show-ref --verify --quiet refs/remotes/origin/main"
assert_command "git show-ref --verify --quiet refs/remotes/origin/master"
assert_command "git reset --hard origin/master"

# P0-2: the changed-diff prompt controls display only; reset always follows.
setup_case changed-diff-yes
create_existing_checkout
export MOGUET_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export MOGUET_TEST_GIT_CHANGED_FILES='PKGBUILD\nclean-root.install\n'
run_tty_ok 'y\n' --noedit build clean-root
assert_contains "Update diff range: HEAD..origin/main (existing cache repository)." "$output_file"
assert_contains "Review-sensitive file changes: PKGBUILD, clean-root.install" "$output_file"
assert_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command "git diff HEAD..origin/main --color=always"
assert_command "git reset --hard origin/main"
assert_command_before "git diff HEAD..origin/main --color=always" "git reset --hard origin/main"

setup_case changed-diff-no
create_existing_checkout
export MOGUET_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export MOGUET_TEST_GIT_CHANGED_FILES='PKGBUILD\n'
run_tty_ok 'n\n' --noedit build clean-root
assert_contains "Update diff range: HEAD..origin/main (existing cache repository)." "$output_file"
assert_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command_absent "git diff HEAD..origin/main --color=always"
assert_command "git reset --hard origin/main"

setup_case changed-diff-nodiff
create_existing_checkout
export MOGUET_TEST_GIT_DIFF_QUIET_EXIT_CODE=1
export MOGUET_TEST_GIT_CHANGED_FILES='PKGBUILD\n'
run_ok --noedit --nodiff build clean-root
assert_not_contains "Update diff range:" "$output_file"
assert_not_contains "Updates detected in existing cache repository. View git diff?" "$output_file"
assert_command_prefix_absent "git diff "
assert_command "git reset --hard origin/main"

# P0-3: only_if_updated runs after fetch/reset and preserves all unknown-status branches.
setup_case update-newer
prepare_upgrade_case
write_srcinfo 2.0 1
export MOGUET_TEST_VERCMP_OUTPUT=1
run_upgrade_ok --noedit --nodiff upgrade
assert_command_count "pacman-conf --verbose RootDir DBPath" 1
assert_command_count "alpm initialize" 3
assert_command_count "alpm query clean-root" 3
assert_command_count "alpm release" 3
assert_command "sudo pacman -Syu"
assert_command_count "pacman -Si clean-root" 1
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command_absent "pacman -Q clean-root"
assert_command "vercmp 2.0-1 1.0-1"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_occurrence_before "alpm query clean-root" 1 "alpm release" 1
assert_command_occurrence_before "alpm release" 1 "sudo pacman -Syu" 1
assert_command_occurrence_before "sudo pacman -Syu" 1 "alpm query clean-root" 2
assert_command_occurrence_before "alpm query clean-root" 2 "alpm release" 2
assert_command_occurrence_before "pacman -Si clean-root" 1 "pacman-conf --verbose RootDir DBPath" 1
assert_command_occurrence_before "alpm release" 2 "git fetch origin" 1
assert_command_before "git reset --hard origin/main" "vercmp 2.0-1 1.0-1"
assert_command_before "vercmp 2.0-1 1.0-1" "makepkg --packagelist"

setup_case update-up-to-date
prepare_upgrade_case
write_srcinfo 1.0 1
export MOGUET_TEST_VERCMP_OUTPUT=0
run_upgrade_ok --noedit --nodiff upgrade
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "vercmp 1.0-1 1.0-1"
assert_command_absent "pacman -Q clean-root"
assert_contains "clean-root is up to date (1.0-1). Skipping." "$output_file"
assert_command_prefix_absent "makepkg "
assert_command_prefix_absent "moguet-test-editor "

setup_case update-unknown-noconfirm
prepare_upgrade_case
run_upgrade_ok --noedit --nodiff --noconfirm upgrade
assert_contains "Unable to determine update status from .SRCINFO for clean-root." "$output_file"
assert_contains "Skipping clean-root: update status is unknown and --noconfirm is set." "$output_file"
assert_command "git reset --hard origin/main"
assert_command_absent "pacman -Q clean-root"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-noninteractive
prepare_upgrade_case
run_upgrade_ok --noedit --nodiff upgrade
assert_contains "Unable to determine update status from .SRCINFO for clean-root." "$output_file"
assert_contains "Skipping clean-root: update status is unknown and stdin is non-interactive." "$output_file"
assert_command "git reset --hard origin/main"
assert_command_absent "pacman -Q clean-root"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-interactive-no
prepare_upgrade_case
run_upgrade_tty_ok 'n\n' --noedit --nodiff upgrade
assert_contains "Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?" "$output_file"
assert_command "git reset --hard origin/main"
assert_command_absent "pacman -Q clean-root"
assert_command_prefix_absent "makepkg "

setup_case update-unknown-interactive-yes
prepare_upgrade_case
run_upgrade_tty_ok 'y\n' --noedit --nodiff upgrade
assert_contains "Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?" "$output_file"
assert_command "git reset --hard origin/main"
assert_command_absent "pacman -Q clean-root"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "git reset --hard origin/main" "makepkg --packagelist"

# P0-4: clone ownership ends after validation; a later makepkg failure keeps the checkout.
setup_case makepkg-failure-retains-checkout
export MOGUET_TEST_MAKEPKG_EXIT_CODE=42
export MOGUET_TEST_MAKEPKG_PACKAGELIST_EXIT_CODE=0
run_fail --noedit --nodiff build clean-root
assert_contains "Build-only makepkg failed with exit code 42." "$output_file"
assert_command "git clone $official_url clean-root"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "git clone $official_url clean-root" "makepkg --packagelist"
assert_checkout_retained

# Issue #226: configured editor / environment overrideの優先順位と実argv境界を固定する。
setup_case editor-configured-argv
create_existing_checkout
printf 'post_install() { :; }\n' > "$checkout_dir/-option.install"
write_editor_config 'moguet-test-editor --configured-option'
export MOGUET_TEST_CONFIG_FILE="$config_file"
export MOGUET_TEST_EDITOR_ARGV_LOG="$editor_argv_log"
run_config_tty_ok 'y\ny\ny\n' build clean-root
assert_command "moguet-test-editor --configured-option ./PKGBUILD"
assert_command "moguet-test-editor --configured-option ./-option.install"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "moguet-test-editor --configured-option ./PKGBUILD" "moguet-test-editor --configured-option ./-option.install"
assert_command_before "moguet-test-editor --configured-option ./-option.install" "makepkg --packagelist"
assert_editor_argv_log 'argv-begin
arg[0]=<--configured-option>
arg[1]=<./PKGBUILD>
target=<./PKGBUILD>
argv-end
argv-begin
arg[0]=<--configured-option>
arg[1]=<./-option.install>
target=<./-option.install>
argv-end'

setup_case editor-environment-override-argv
create_existing_checkout
printf 'post_install() { :; }\n' > "$checkout_dir/-option.install"
write_editor_config 'moguet-test-editor --configured-option'
export MOGUET_TEST_CONFIG_FILE="$config_file"
export MOGUET_TEST_EDITOR_ARGV_LOG="$editor_argv_log"
export EDITOR='moguet-test-editor --environment-option'
run_config_tty_ok 'y\ny\ny\n' build clean-root
assert_command "moguet-test-editor --environment-option ./PKGBUILD"
assert_command "moguet-test-editor --environment-option ./-option.install"
assert_command_prefix_absent "moguet-test-editor --configured-option"
assert_not_contains "--configured-option" "$editor_argv_log"
assert_command "makepkg --packagelist"
assert_command "makepkg -sc"
assert_command_before "moguet-test-editor --environment-option ./PKGBUILD" "moguet-test-editor --environment-option ./-option.install"
assert_command_before "moguet-test-editor --environment-option ./-option.install" "makepkg --packagelist"
assert_editor_argv_log 'argv-begin
arg[0]=<--environment-option>
arg[1]=<./PKGBUILD>
target=<./PKGBUILD>
argv-end
argv-begin
arg[0]=<--environment-option>
arg[1]=<./-option.install>
target=<./-option.install>
argv-end'

setup_case editor-configured-failure
create_existing_checkout
printf 'post_install() { :; }\n' > "$checkout_dir/-option.install"
write_editor_config 'moguet-test-editor --configured-option'
export MOGUET_TEST_CONFIG_FILE="$config_file"
export MOGUET_TEST_EDITOR_ARGV_LOG="$editor_argv_log"
export MOGUET_TEST_EDITOR_EXIT_CODE=42
run_config_tty_fail 'y\n' build clean-root
assert_contains "Build Error: Failed while building/installing PackageBase clean-root (clean-root): Editor failed." "$output_file"
assert_not_contains "Edit install script -option.install?" "$output_file"
assert_command "moguet-test-editor --configured-option ./PKGBUILD"
assert_command_absent "moguet-test-editor --configured-option ./-option.install"
assert_command_prefix_absent "makepkg "
assert_checkout_retained
if [ ! -f "$checkout_dir/-option.install" ] || [ -L "$checkout_dir/-option.install" ]; then
    echo "install script was not retained after editor failure: $checkout_dir/-option.install" >&2
    exit 1
fi
assert_editor_argv_log 'argv-begin
arg[0]=<--configured-option>
arg[1]=<./PKGBUILD>
target=<./PKGBUILD>
argv-end'

echo "source-build characterization tests: all checks passed"
