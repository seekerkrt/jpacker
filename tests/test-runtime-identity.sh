#!/bin/sh

set -eu

# English identity assertions must not depend on an installed Moguet catalog
# or the developer machine's ambient message locale.
LC_ALL=C
LANGUAGE=
export LC_ALL LANGUAGE

production_binary=$1
root_test_binary=$2
default_test_binary=$3
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'runtime-identity-test: %s\n' "$*" >&2
    exit 1
}

assert_contains() {
    expected=$1
    output_file=$2
    grep -F -- "$expected" "$output_file" >/dev/null || {
        printf 'runtime-identity-test: missing expected output: %s\n' \
            "$expected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    }
}

assert_not_contains() {
    unexpected=$1
    output_file=$2
    if grep -F -- "$unexpected" "$output_file" >/dev/null; then
        printf 'runtime-identity-test: unexpected output: %s\n' \
            "$unexpected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    fi
}

version=$(tr -d '[:space:]' < "$repo_root/VERSION")

info_case_dir=$tmp_dir/info-only
mkdir -p \
    "$info_case_dir/home" \
    "$info_case_dir/xdg-config" \
    "$info_case_dir/xdg-state" \
    "$info_case_dir/xdg-cache"
help_output=$tmp_dir/help.out
HOME=$info_case_dir/home \
XDG_CONFIG_HOME=$info_case_dir/xdg-config \
XDG_STATE_HOME=$info_case_dir/xdg-state \
XDG_CACHE_HOME=$info_case_dir/xdg-cache \
    "$production_binary" --help > "$help_output" 2>&1
assert_contains "Moguet" "$help_output"
assert_contains "    moguet <op> [options] [targets...]" "$help_output"
assert_contains "    legacy jpacker.conf: EDITOR=" "$help_output"
grep -Fv 'jpacker.conf:' "$help_output" > "$tmp_dir/help-without-legacy-config"
if grep -Fi -- 'jpacker' "$tmp_dir/help-without-legacy-config" >/dev/null; then
    fail "help retains an unintended jpacker project identity."
fi

version_output=$tmp_dir/version.out
HOME=$info_case_dir/home \
XDG_CONFIG_HOME=$info_case_dir/xdg-config \
XDG_STATE_HOME=$info_case_dir/xdg-state \
XDG_CACHE_HOME=$info_case_dir/xdg-cache \
    "$production_binary" --version > "$version_output" 2>&1
[ "$(cat "$version_output")" = "Moguet v$version" ] ||
    fail "production version output does not match Moguet v$version."
assert_not_contains "jpacker" "$version_output"
for info_only_directory in \
    "$info_case_dir/xdg-config/moguet" \
    "$info_case_dir/xdg-state/moguet" \
    "$info_case_dir/xdg-cache/moguet"
do
    if [ -e "$info_only_directory" ] || [ -L "$info_only_directory" ]; then
        fail "help/version created an XDG consumer directory."
    fi
done

root_case_dir=$tmp_dir/root
mkdir -p \
    "$root_case_dir/home" \
    "$root_case_dir/xdg-config" \
    "$root_case_dir/xdg-state" \
    "$root_case_dir/xdg-cache"
root_output=$root_case_dir/output
root_status=0
if HOME=$root_case_dir/home \
        XDG_CONFIG_HOME=$root_case_dir/xdg-config \
        XDG_STATE_HOME=$root_case_dir/xdg-state \
        XDG_CACHE_HOME=$root_case_dir/xdg-cache \
        SUDO_USER=untrusted-other-user \
        "$root_test_binary" -Q filesystem > "$root_output" 2>&1; then
    fail "root execution test unexpectedly succeeded."
else
    root_status=$?
fi
[ "$root_status" -eq 1 ] ||
    fail "root execution returned $root_status; expected 1."
assert_contains "Do not run Moguet as root or with sudo." "$root_output"
assert_contains \
    "Run moguet as a normal user; Moguet will invoke sudo/pacman when needed." \
    "$root_output"
assert_not_contains "jpacker" "$root_output"
if [ -e "$root_case_dir/xdg-cache/jpacker" ] || \
   [ -L "$root_case_dir/xdg-cache/jpacker" ]; then
    fail "root guard allowed legacy cache/log initialization."
fi
for root_guard_directory in \
    "$root_case_dir/xdg-config/moguet" \
    "$root_case_dir/xdg-state/moguet" \
    "$root_case_dir/xdg-cache/moguet"
do
    if [ -e "$root_guard_directory" ] || [ -L "$root_guard_directory" ]; then
        fail "root guard allowed XDG consumer initialization."
    fi
done

MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
PATH=$repo_root/tests/stubs:/usr/bin:/bin
export PATH
. "$repo_root/tests/test-command-safety.sh"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"

startup_case_dir=$tmp_dir/startup
mkdir -p \
    "$startup_case_dir/home" \
    "$startup_case_dir/xdg-config" \
    "$startup_case_dir/xdg-state" \
    "$startup_case_dir/xdg-cache/jpacker"
startup_output=$startup_case_dir/output
command_log=$startup_case_dir/commands.log
: > "$command_log"
legacy_log=$startup_case_dir/xdg-cache/jpacker/jpacker.log
printf '%s\n' 'legacy-log-must-remain-unchanged' > "$legacy_log"
chmod 600 "$legacy_log"
legacy_checksum_before=$(cksum "$legacy_log")
legacy_identity_before=$(stat -c '%d:%i:%a' "$legacy_log")
export MOGUET_TEST_COMMAND_LOG=$command_log
export MOGUET_TEST_PACMAN_EXIT_CODE=0
default_config=$tmp_dir/config.toml
printf '%s\n' 'schema_version = 1' > "$default_config"
export MOGUET_TEST_CONFIG_FILE=$default_config
HOME=$startup_case_dir/home \
XDG_CONFIG_HOME=$startup_case_dir/xdg-config \
XDG_STATE_HOME=$startup_case_dir/xdg-state \
XDG_CACHE_HOME=$startup_case_dir/xdg-cache \
SUDO_USER=untrusted-other-user \
    "$default_test_binary" -Q filesystem > "$startup_output" 2>&1 ||
    fail "safe startup identity command failed."
assert_contains "Started Moguet v$version" "$startup_output"
assert_not_contains "Started jpacker" "$startup_output"
[ "$(cat "$command_log")" = "pacman -Q filesystem" ] ||
    fail "startup identity check did not stay on the safe pacman stub route."
state_log=$startup_case_dir/xdg-state/moguet/moguet.log
[ -f "$state_log" ] || fail "explicit XDG state log was not created."
[ "$(stat -c '%a' "$state_log")" = 600 ] ||
    fail "new explicit XDG state log mode is not 0600."
[ "$(stat -c '%u' "$state_log")" = "$(id -u)" ] ||
    fail "new explicit XDG state log owner is not the effective user."
assert_contains "[INFO] Started Moguet v$version" "$state_log"
if [ -e "$startup_case_dir/xdg-config/moguet" ] || \
   [ -L "$startup_case_dir/xdg-config/moguet" ] || \
   [ -e "$startup_case_dir/xdg-cache/moguet" ] || \
   [ -L "$startup_case_dir/xdg-cache/moguet" ]; then
    fail "default state log connected an unrelated XDG consumer."
fi
[ "$(cksum "$legacy_log")" = "$legacy_checksum_before" ] ||
    fail "default state-log cutover changed the legacy cache log."
[ "$(stat -c '%d:%i:%a' "$legacy_log")" = "$legacy_identity_before" ] ||
    fail "default state-log cutover replaced or remoded the legacy cache log."

unsafe_case_dir=$tmp_dir/unsafe-existing-log
mkdir -p \
    "$unsafe_case_dir/home" \
    "$unsafe_case_dir/xdg-config" \
    "$unsafe_case_dir/xdg-state/moguet" \
    "$unsafe_case_dir/xdg-cache"
chmod 700 "$unsafe_case_dir/xdg-state/moguet"
unsafe_log=$unsafe_case_dir/xdg-state/moguet/moguet.log
printf '%s\n' 'unsafe-log-must-remain-unchanged' > "$unsafe_log"
chmod 644 "$unsafe_log"
unsafe_checksum_before=$(cksum "$unsafe_log")
unsafe_identity_before=$(stat -c '%d:%i:%a' "$unsafe_log")
unsafe_output=$unsafe_case_dir/output
: > "$command_log"
unsafe_status=0
if HOME=$unsafe_case_dir/home \
        XDG_CONFIG_HOME=$unsafe_case_dir/xdg-config \
        XDG_STATE_HOME=$unsafe_case_dir/xdg-state \
        XDG_CACHE_HOME=$unsafe_case_dir/xdg-cache \
        "$default_test_binary" -Q filesystem > "$unsafe_output" 2>&1; then
    fail "unsafe existing default state log unexpectedly allowed startup."
else
    unsafe_status=$?
fi
[ "$unsafe_status" -eq 1 ] ||
    fail "unsafe existing default state log returned $unsafe_status; expected 1."
assert_contains "file permissions are not private mode 0600" "$unsafe_output"
[ ! -s "$command_log" ] ||
    fail "unsafe existing default state log allowed an external command."
[ "$(cksum "$unsafe_log")" = "$unsafe_checksum_before" ] ||
    fail "unsafe default state-log validation changed file content."
[ "$(stat -c '%d:%i:%a' "$unsafe_log")" = "$unsafe_identity_before" ] ||
    fail "unsafe default state-log validation replaced or remoded the file."

fallback_case_dir=$tmp_dir/fallback
mkdir -p \
    "$fallback_case_dir/home" \
    "$fallback_case_dir/xdg-config" \
    "$fallback_case_dir/xdg-cache"
fallback_output=$fallback_case_dir/output
: > "$command_log"
env -u XDG_STATE_HOME \
    HOME=$fallback_case_dir/home \
    XDG_CONFIG_HOME=$fallback_case_dir/xdg-config \
    XDG_CACHE_HOME=$fallback_case_dir/xdg-cache \
    SUDO_USER=another-untrusted-user \
    "$default_test_binary" -Q filesystem > "$fallback_output" 2>&1 ||
    fail "HOME fallback state log command failed."
fallback_log=$fallback_case_dir/home/.local/state/moguet/moguet.log
[ -f "$fallback_log" ] || fail "HOME fallback state log was not created."
[ "$(stat -c '%a' "$fallback_log")" = 600 ] ||
    fail "new HOME fallback state log mode is not 0600."
assert_contains "[INFO] Started Moguet v$version" "$fallback_log"
[ "$(cat "$command_log")" = "pacman -Q filesystem" ] ||
    fail "HOME fallback check left the safe pacman stub route."
if [ -e "$fallback_case_dir/xdg-config/moguet" ] || \
   [ -L "$fallback_case_dir/xdg-config/moguet" ] || \
   [ -e "$fallback_case_dir/xdg-cache/moguet" ] || \
   [ -L "$fallback_case_dir/xdg-cache/moguet" ] || \
   [ -e "$fallback_case_dir/xdg-cache/jpacker" ] || \
   [ -L "$fallback_case_dir/xdg-cache/jpacker" ]; then
    fail "HOME fallback log connected an unrelated XDG consumer."
fi

printf 'runtime-identity-test: all checks passed\n'
