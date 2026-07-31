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
    chmod 600 "$tmp_dir/unreadable.conf" 2>/dev/null || true
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

assert_config_field_count() {
    file=$1
    if [ "$(wc -l < "$file")" -ne 8 ]; then
        echo "AppConfig output did not contain exactly eight fields" >&2
        cat "$file" >&2
        exit 1
    fi
}

assert_config_defaults() {
    file=$1
    assert_line "NOEDIT=false" "$file"
    assert_line "NODIFF=false" "$file"
    assert_line "NOCONFIRM=false" "$file"
    assert_line "REBUILD=false" "$file"
    assert_line "CLEANBUILD=false" "$file"
    assert_line "RMDEPS=false" "$file"
    assert_line "EDITOR=nano" "$file"
    assert_line "LOGFILE=" "$file"
    assert_config_field_count "$file"
}

run_module_ok() {
    output_file=$1
    shift
    if ! "$module_test_binary" "$@" > "$output_file" 2>&1; then
        echo "app_config module test command failed: $*" >&2
        cat "$output_file" >&2
        exit 1
    fi
}

run_module_fail() {
    output_file=$1
    shift
    if "$module_test_binary" "$@" > "$output_file" 2>&1; then
        echo "app_config module test command unexpectedly succeeded: $*" >&2
        cat "$output_file" >&2
        exit 1
    fi
}

# AppConfigの8 fieldと、明示path loaderのproduction boundaryをhost /etcから切り離して固定する。
defaults_output=$tmp_dir/defaults.out
run_module_ok "$defaults_output" defaults
assert_config_defaults "$defaults_output"

syntax_config=$tmp_dir/syntax.conf
syntax_output=$tmp_dir/syntax.out
{
    printf '%s\n' '# whole-line comment'
    printf '%s\n' '   '
    printf '%s\n' '  NoEdIt = "YeS"   # trailing comment'
    printf '%s\n' "  nodiff = 'TrUe'"
    printf '%s\n' '  RmDePs = "yes"'
    printf '%s\n' '  EdItOr = "editor # literal = value" # comment outside quotes'
    printf '%s\n' "  LogFile = '/tmp/log # literal=still-value' # trailing comment"
    printf '%s\n' 'UNKNOWN_KEY=ignored'
    printf '%s\n' 'NOCONFIRM=true'
    printf '%s\n' 'malformed line without delimiter'
    printf '%s\n' '=value-with-empty-key'
} > "$syntax_config"
run_module_ok "$syntax_output" load "$syntax_config"
assert_line "NOEDIT=true" "$syntax_output"
assert_line "NODIFF=true" "$syntax_output"
assert_line "NOCONFIRM=false" "$syntax_output"
assert_line "REBUILD=false" "$syntax_output"
assert_line "CLEANBUILD=false" "$syntax_output"
assert_line "RMDEPS=true" "$syntax_output"
assert_line "EDITOR=editor # literal = value" "$syntax_output"
assert_line "LOGFILE=/tmp/log # literal=still-value" "$syntax_output"
assert_config_field_count "$syntax_output"

invalid_config=$tmp_dir/invalid.conf
invalid_output=$tmp_dir/invalid.out
{
    printf '%s\n' 'NOEDIT=not-a-boolean'
    printf '%s\n' 'NODIFF=false'
    printf '%s\n' 'RMDEPS=not-a-boolean'
    printf '%s\n' 'EDITOR='
    printf '%s\n' 'LOGFILE=""'
} > "$invalid_config"
run_module_ok "$invalid_output" load "$invalid_config"
assert_config_defaults "$invalid_output"

missing_output=$tmp_dir/missing.out
run_module_ok "$missing_output" load "$tmp_dir/does-not-exist.conf"
assert_config_defaults "$missing_output"

# POLICY: 現行loaderはopen/read不能を空のconfigとして扱い、既定値を返す。
unreadable_config=$tmp_dir/unreadable.conf
unreadable_output=$tmp_dir/unreadable.out
printf '%s\n' 'NOEDIT=true' > "$unreadable_config"
chmod 000 "$unreadable_config"
run_module_ok "$unreadable_output" load "$unreadable_config"
assert_config_defaults "$unreadable_output"

config_directory=$tmp_dir/config-directory
directory_output=$tmp_dir/directory.out
mkdir "$config_directory"
run_module_ok "$directory_output" load "$config_directory"
assert_config_defaults "$directory_output"

# LOGFILEに使うpath expansionはloaderと同じpublic boundaryで検証する。
path_home=$tmp_dir/path-home
mkdir "$path_home"
path_output=$tmp_dir/path.out
HOME=$path_home run_module_ok "$path_output" expand '~'
assert_line "$path_home" "$path_output"
# shellcheck disable=SC2088 # `~/`を展開前のconfig値として渡すtest。
tilde_slash='~/'
HOME=$path_home run_module_ok "$path_output" expand "$tilde_slash"
assert_line "$path_home/" "$path_output"
HOME=$path_home run_module_ok "$path_output" expand '/var/log/jpacker.log'
assert_line "/var/log/jpacker.log" "$path_output"
HOME=$path_home run_module_ok "$path_output" expand ''
assert_line "" "$path_output"
HOME=$path_home run_module_fail "$path_output" expand '~another-user'
assert_line "Unsupported home expansion: ~another-user" "$path_output"
env -u HOME "$module_test_binary" expand '~' > "$path_output" 2>&1 &&
    fail "HOMEなしの~ expansionが成功した"
assert_line "HOME environment variable not set." "$path_output"

# CLI precedenceは実際のAUR build routeで観測する。localhost fixture以外へは接続しない。
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

port=$(cat "$port_file")
export MOGUET_TEST_AUR_RPC_BASE_URL="http://127.0.0.1:$port/rpc/"

setup_integration_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output
    config_file=$case_dir/jpacker.conf

    mkdir -p \
        "$case_dir/home" \
        "$case_dir/xdg-state" \
        "$case_dir/xdg-cache" \
        "$case_dir/package.build"
    : > "$command_log"
    {
        printf '%s\n' 'NOEDIT=true'
        printf '%s\n' 'NODIFF=true'
        printf '%s\n' 'LOGFILE=~/config-log/jpacker.log'
    } > "$config_file"

    export HOME="$case_dir/home"
    export XDG_STATE_HOME="$case_dir/xdg-state"
    export XDG_CACHE_HOME="$case_dir/xdg-cache"
    export MOGUET_TEST_CONFIG_FILE="$config_file"
    export MOGUET_TEST_COMMAND_LOG="$command_log"
    export MOGUET_TEST_PACKAGE_BUILD_DIR="$case_dir/package.build"
    export MOGUET_TEST_PACMAN_EXIT_CODE=1
    export MOGUET_TEST_SUDO_EXIT_CODE=0
    export MOGUET_TEST_MAKEPKG_EXIT_CODE=0
    unset EDITOR
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_PACMAN_REPO_PACKAGES
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
    unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
}

run_integration_ok() {
    : > "$command_log"
    if ! "$integration_test_binary" "$@" > "$output_file" 2>&1; then
        echo "integration command failed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_integration_fail() {
    : > "$command_log"
    if "$integration_test_binary" "$@" > "$output_file" 2>&1; then
        echo "integration command unexpectedly succeeded: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
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
    if grep -F -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command content: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "unexpected external command ran before the tested guard" >&2
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

enable_config_rmdeps() {
    printf '%s\n' 'RMDEPS=true' >> "$config_file"
}

setup_integration_case precedence
run_integration_ok --noconfirm --build-mode=clean --cleanbuild -S --aur clean-root
assert_line "git clone https://aur.archlinux.org/clean-root.git clean-root" "$command_log"
assert_command "makepkg -sc --noconfirm -C"
assert_contains "sudo pacman -U --noconfirm -- " "$command_log"
assert_contains "Skipping PKGBUILD/.install review (--noedit)." "$output_file"
assert_not_contains "Review target: PKGBUILD" "$output_file"
if [ ! -f "$HOME/config-log/jpacker.log" ]; then
    fail "config LOGFILE was not expanded and initialized"
fi
if [ -e "$XDG_CACHE_HOME/jpacker/jpacker.log" ]; then
    fail "config LOGFILE was replaced by the legacy default cache log"
fi
if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
    fail "explicit legacy LOGFILE also initialized the default state log"
fi

# 2回目はexisting cache routeへ入り、config由来NODIFFがgit diffを抑止する。
run_integration_ok --noconfirm --build-mode=clean --cleanbuild -S --aur clean-root
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_command "makepkg -sc --noconfirm -C"
assert_command_absent "git diff"

# legacy NOEDIT/NODIFF=trueもCLIのtyped final-valueで反転できる。
run_integration_ok \
    --edit --diff --noconfirm --build-mode=normal -S --aur clean-root
assert_command "git diff --quiet HEAD..origin/main"
assert_contains "Review target: PKGBUILD" "$output_file"
assert_not_contains "Skipping PKGBUILD/.install review (--noedit)." "$output_file"
assert_command "makepkg -sc --noconfirm"

# 同じ最終値のcanonical option / compatibility alias重複は1件のpolicyへ合成する。
run_integration_ok \
    --noconfirm --noconfirm --build-mode=clean \
    --cleanbuild --cleanbuild -S --aur clean-root
assert_command "makepkg -sc --noconfirm -C"
makepkg_count=$(grep -Fxc -- "makepkg -sc --noconfirm -C" "$command_log" || true)
if [ "$makepkg_count" -ne 1 ]; then
    echo "repeated CLI option changed the makepkg option multiplicity" >&2
    cat "$command_log" >&2
    exit 1
fi

# explicit legacy LOGFILEはstate resolverより優先する。Issue #306までは
# path-based compatibility contractを維持し、不正なunused state値も参照しない。
setup_integration_case legacy-logfile-invalid-state
XDG_STATE_HOME=relative/state-secret
export XDG_STATE_HOME
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_integration_ok -Q filesystem
assert_only_command "pacman -Q filesystem"
if [ ! -f "$HOME/config-log/jpacker.log" ]; then
    fail "legacy LOGFILE override was not selected before XDG state"
fi
assert_contains "[INFO] Started Moguet v" "$HOME/config-log/jpacker.log"

# legacy overrideのopen失敗も従来どおりbest-effortでoperationを続け、
# default state logへ暗黙fallbackしない。
setup_integration_case legacy-logfile-open-failure
not_a_directory=$case_dir/not-a-directory
printf '%s\n' 'legacy-path-parent-sentinel' > "$not_a_directory"
printf 'LOGFILE=%s/blocked.log\n' "$not_a_directory" >> "$config_file"
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_integration_ok -Q filesystem
assert_only_command "pacman -Q filesystem"
assert_line "legacy-path-parent-sentinel" "$not_a_directory"
if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
    fail "failed legacy LOGFILE override fell back to the default state log"
fi

# --rmdepsもenable-onlyでmergeされるが、separated source routeでは
# resolver/checkout/workspace/makepkg/sudoより前に拒否する。
setup_integration_case rmdeps-rejection
run_integration_fail --rmdeps --rmdeps -S --aur clean-root
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty

# config由来のRMDEPSもsilent ignoreせず、CLI optionなしで同じguardへ到達する。
setup_integration_case config-rmdeps-rejection
enable_config_rmdeps
run_integration_fail --noconfirm -S --aur clean-root
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty

# target-less AUR updateはtarget有無にかかわらず、log/cache/queryより前に拒否する。
setup_integration_case config-rmdeps-upgrade-aur
enable_config_rmdeps
run_integration_fail upgrade-aur
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty
if [ -e "$HOME/config-log/jpacker.log" ]; then
    fail "config RMDEPS upgrade-aur guard ran after log initialization"
fi
if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ] ||
   [ -e "$XDG_CACHE_HOME/jpacker" ] || [ -L "$XDG_CACHE_HOME/jpacker" ]; then
    fail "config RMDEPS upgrade-aur guard initialized the default cache"
fi
if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
    fail "config RMDEPS upgrade-aur guard initialized the default state log"
fi

# aggregate側もupdate target 0件をno-op成功へ丸めず、全phaseより前に拒否する。
setup_integration_case config-rmdeps-upgrade-all-no-updates
enable_config_rmdeps
run_integration_fail upgrade-all
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty
if [ -e "$HOME/config-log/jpacker.log" ]; then
    fail "config RMDEPS upgrade-all guard ran after log initialization"
fi
if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ] ||
   [ -e "$XDG_CACHE_HOME/jpacker" ] || [ -L "$XDG_CACHE_HOME/jpacker" ]; then
    fail "config RMDEPS upgrade-all guard initialized the default cache"
fi
if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
    fail "config RMDEPS upgrade-all guard initialized the default state log"
fi

# registered source targetがあるupgradeはsource/system mutationの前に拒否する。
setup_integration_case config-rmdeps-upgrade-with-source
enable_config_rmdeps
: > "$MOGUET_TEST_PACKAGE_BUILD_DIR/clean-root"
run_integration_fail upgrade
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty

# source targetがなければpacman-onlyへ縮退し、config requestを転送しない。
setup_integration_case config-rmdeps-upgrade-without-source
enable_config_rmdeps
run_integration_ok upgrade
assert_only_command "sudo pacman -Syu"
assert_command_absent "--rmdeps"
assert_command_absent " -r"

# 同一processでparse failure後のg_configを検査し、途中までのCLI overrideがpublishされないことを固定する。
setup_integration_case parse-failure
: > "$config_file"
export MOGUET_TEST_APP_CONFIG_CASE=parse-failure-cli-overrides
run_integration_ok
unset MOGUET_TEST_APP_CONFIG_CASE
assert_contains "Missing value for option --config" "$output_file"
assert_command_log_empty
if [ -e "$HOME/config-log/jpacker.log" ]; then
    fail "CLI parse failure initialized the config log"
fi
if [ -e "$XDG_CACHE_HOME/moguet" ] || [ -L "$XDG_CACHE_HOME/moguet" ] ||
   [ -e "$XDG_CACHE_HOME/jpacker" ] || [ -L "$XDG_CACHE_HOME/jpacker" ]; then
    fail "CLI parse failure initialized the default cache"
fi
if [ -e "$XDG_STATE_HOME/moguet" ] || [ -L "$XDG_STATE_HOME/moguet" ]; then
    fail "CLI parse failure initialized the default state log"
fi
