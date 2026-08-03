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

mkdir -p "$tmp_dir/cache" "$tmp_dir/config" "$tmp_dir/home" "$tmp_dir/state"
chmod 0700 "$tmp_dir/config"
command_log=$tmp_dir/commands.log
: > "$command_log"

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
export HOME=$tmp_dir/home
export XDG_CONFIG_HOME=$tmp_dir/config
export XDG_STATE_HOME=$tmp_dir/state
export XDG_CACHE_HOME=$tmp_dir/cache
export PATH=$repo_root/tests/stubs:/usr/bin:/bin
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"
export MOGUET_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:$port/rpc/
export MOGUET_TEST_COMMAND_LOG=$command_log
export MOGUET_TEST_PACMAN_EXIT_CODE=1
export MOGUET_TEST_SUDO_EXIT_CODE=99
unset MOGUET_TEST_PACMAN_QM_OUTPUT
unset MOGUET_TEST_PACMAN_REPO_PACKAGES
unset MOGUET_TEST_GIT_REMOTE_URL
unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
unset MOGUET_TEST_GIT_CLONE_FIXTURE_DIR
unset MOGUET_TEST_MAKEPKG_EXIT_CODE

run_ok() {
    output_file=$1
    shift
    "$test_binary" "$@" > "$output_file" 2>&1
}

run_fail() {
    output_file=$1
    shift
    if "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        exit 1
    fi
}

assert_contains() {
    pattern=$1
    file=$2
    if ! grep -F "$pattern" "$file" >/dev/null; then
        echo "missing expected output: $pattern" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_not_contains() {
    pattern=$1
    file=$2
    if grep -F "$pattern" "$file" >/dev/null; then
        echo "unexpected output: $pattern" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

run_ok "$tmp_dir/conflict-plan.out" plan conflict-only
assert_contains "conflicts: conflict-old, conflict-git" "$tmp_dir/conflict-plan.out"

run_ok "$tmp_dir/replace-plan.out" plan replace-only
assert_contains "replaces: replace-legacy" "$tmp_dir/replace-plan.out"

run_ok "$tmp_dir/dependency-plan.out" plan dependency-risk-root
assert_contains "risk-dep" "$tmp_dir/dependency-plan.out"
assert_contains "conflicts: dep-old>=2" "$tmp_dir/dependency-plan.out"
assert_contains "Plan status: incomplete" "$tmp_dir/dependency-plan.out"
if [ "$(grep -c '^  risk-dep$' "$tmp_dir/dependency-plan.out")" -ne 1 ]; then
    echo "risk-dep metadata was not deduplicated" >&2
    exit 1
fi

run_ok "$tmp_dir/clean-plan.out" plan clean-root
assert_not_contains "Metadata conflicts/replaces:" "$tmp_dir/clean-plan.out"
assert_not_contains "Plan status: incomplete" "$tmp_dir/clean-plan.out"

run_ok "$tmp_dir/deps.out" deps risk-root
assert_contains "Metadata conflicts/replaces:" "$tmp_dir/deps.out"
assert_contains "conflicts: root-old, root-git" "$tmp_dir/deps.out"
assert_contains "replaces: root-legacy" "$tmp_dir/deps.out"

run_ok "$tmp_dir/info.out" -Si risk-root
assert_contains "Conflicts With  : root-old  root-git" "$tmp_dir/info.out"
assert_contains "Replaces        : root-legacy" "$tmp_dir/info.out"

: > "$command_log"
run_fail "$tmp_dir/build.out" build dependency-risk-root
assert_contains "conflicts/replaces metadata requires manual review" "$tmp_dir/build.out"
assert_contains "risk-dep [conflicts: dep-old>=2; replaces: dep-legacy]" "$tmp_dir/build.out"
if grep -E '^(git|makepkg|sudo) ' "$command_log" >/dev/null; then
    echo "build guard allowed an external mutation command" >&2
    cat "$command_log" >&2
    exit 1
fi

: > "$command_log"
run_fail "$tmp_dir/noconfirm.out" --noconfirm -S risk-root
assert_contains "conflicts/replaces metadata requires manual review" "$tmp_dir/noconfirm.out"
if grep -E '^(git|makepkg|sudo) |^pacman -S ' "$command_log" >/dev/null; then
    echo "--noconfirm bypassed the metadata guard" >&2
    cat "$command_log" >&2
    exit 1
fi

: > "$command_log"
run_ok "$tmp_dir/fetch-first.out" fetch risk-root
assert_contains "fetch is allowed" "$tmp_dir/fetch-first.out"
assert_contains "git clone https://aur.archlinux.org/risk-dep.git risk-dep" "$command_log"
assert_contains "git clone https://aur.archlinux.org/risk-root.git risk-root" "$command_log"
run_ok "$tmp_dir/fetch-second.out" fetch risk-root
assert_contains "git fetch origin" "$command_log"
assert_not_contains "makepkg " "$command_log"
assert_not_contains "sudo " "$command_log"

run_ok "$tmp_dir/unresolved.out" plan unresolved-root
assert_contains "unresolved dependencies remain" "$tmp_dir/unresolved.out"
run_ok "$tmp_dir/ambiguous.out" plan ambiguous-root
assert_contains "ambiguous providers are not selected" "$tmp_dir/ambiguous.out"
run_ok "$tmp_dir/cycle.out" plan cycle-root
assert_contains "cyclic dependencies detected" "$tmp_dir/cycle.out"
run_ok "$tmp_dir/split.out" plan split-child
assert_contains "Split package install targets:" "$tmp_dir/split.out"
assert_contains "split-child (base: split-base)" "$tmp_dir/split.out"
assert_not_contains "Plan status: incomplete" "$tmp_dir/split.out"

for guard_target in unresolved-root ambiguous-root cycle-root; do
    : > "$command_log"
    run_fail "$tmp_dir/$guard_target-guard.out" build "$guard_target"
    if grep -E '^(git|makepkg|sudo) ' "$command_log" >/dev/null; then
        echo "existing plan guard regressed for $guard_target" >&2
        cat "$command_log" >&2
        exit 1
    fi
done

: > "$command_log"
run_ok "$tmp_dir/split-fetch.out" fetch split-child
assert_contains "git clone https://aur.archlinux.org/split-base.git split-base" "$command_log"
assert_not_contains "makepkg " "$command_log"
assert_not_contains "sudo " "$command_log"

echo "conflicts/replaces integration tests: all checks passed"
