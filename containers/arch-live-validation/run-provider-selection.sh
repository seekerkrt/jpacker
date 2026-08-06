#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
fixture_root=$repo_root/containers/arch-live-validation/fixtures/local-package
fixture_pkgbuild=$fixture_root/PKGBUILD
fixture_contract=$fixture_root/contract.env
pty_runner=$repo_root/tests/run-with-pty.py
production_moguet=$repo_root/moguet
case_root=$HOME/live-provider-cases
sentinel_log_root=/var/log/moguet-live-validation
sentinel_status=86

current_case=preflight
current_output=

fail() {
    printf 'arch-live-provider: FAIL: %s\n' "$*" >&2
    if [ -n "$current_output" ] && [ -f "$current_output" ]; then
        printf 'arch-live-provider: case log (%s):\n' "$current_output" >&2
        sed -n '1,320p' "$current_output" >&2
    fi
    exit 1
}

assert_regular_non_symlink() {
    checked_path=$1
    checked_label=$2
    if [ ! -f "$checked_path" ] || [ -L "$checked_path" ]; then
        fail "$checked_label must be a regular non-symlink: $checked_path"
    fi
}

assert_contains() {
    expected_text=$1
    checked_file=$2
    if ! grep -F -- "$expected_text" "$checked_file" >/dev/null; then
        fail "missing expected output: $expected_text"
    fi
}

assert_not_contains() {
    unexpected_text=$1
    checked_file=$2
    if grep -F -- "$unexpected_text" "$checked_file" >/dev/null; then
        fail "unexpected output: $unexpected_text"
    fi
}

assert_count() {
    expected_count=$1
    pattern=$2
    checked_file=$3
    actual_count=$(grep -F -c -- "$pattern" "$checked_file" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        fail "expected $expected_count occurrences of '$pattern', observed $actual_count"
    fi
}

assert_exact_message_count() {
    expected_count=$1
    expected_message=$2
    checked_file=$3
    actual_count=$(python3 - "$expected_message" "$checked_file" <<'PY'
from pathlib import Path
import re
import sys

expected = sys.argv[1].encode("utf-8")
output = Path(sys.argv[2]).read_bytes()
ansi_csi = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")
ansi_osc = re.compile(rb"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)")
count = 0
for line in output.splitlines():
    line = ansi_osc.sub(b"", ansi_csi.sub(b"", line))
    if line == expected or line.endswith(b" " + expected):
        count += 1
print(count)
PY
)
    if [ "$actual_count" -ne "$expected_count" ]; then
        fail "expected $expected_count exact occurrences of '$expected_message', observed $actual_count"
    fi
}

tracked_fixture_manifest() {
    (
        cd "$fixture_root"
        sha256sum PKGBUILD contract.env
        find . -mindepth 1 -maxdepth 1 -printf '%y %m %f\n' | LC_ALL=C sort
    )
}

package_database_manifest() {
    (
        cd /var/lib/pacman/local
        find . -mindepth 1 -printf '%P\n' |
            LC_ALL=C sort |
            while IFS= read -r relative_path; do
                database_path=./$relative_path
                entry_type=$(find "$database_path" -prune -printf '%y')
                entry_mode=$(stat -c '%a' -- "$database_path")
                entry_owner_group=$(stat -c '%u:%g' -- "$database_path")
                content_hash=-
                if [ "$entry_type" = f ]; then
                    content_hash=$(sha256sum -- "$database_path" | awk '{ print $1 }')
                fi
                printf '%s\ttype=%s\tmode=%s\towner_group=%s\tsha256=%s\n' \
                    "$relative_path" "$entry_type" "$entry_mode" \
                    "$entry_owner_group" "$content_hash"
            done
    ) |
        sha256sum
}

source_manifest() {
    manifest_root=$1
    (
        cd "$manifest_root"
        sha256sum PKGBUILD .SRCINFO
        stat -c '%u:%g:%a:%F:%n' PKGBUILD .SRCINFO
    )
}

assert_sentinel_log() {
    checked_case=$1
    shift
    checked_log=$sentinel_log_root/$checked_case/sentinel.argv
    assert_regular_non_symlink "$checked_log" "sentinel argv log for $checked_case"
    checked_metadata=$(stat -c '%U:%G:%a:%F' "$checked_log")
    if [ "$checked_metadata" != 'root:moguet-validation:640:regular file' ]; then
        fail "unsafe sentinel log ownership or mode for $checked_case: $checked_metadata"
    fi
    if ! python3 -c '
import pathlib
import sys

actual = pathlib.Path(sys.argv[1]).read_bytes().split(b"\0")
if not actual or actual[-1] != b"":
    raise SystemExit("sentinel log is not NUL-terminated")
actual = [value.decode("utf-8", "strict") for value in actual[:-1]]
expected = sys.argv[2:]
if actual != expected:
    raise SystemExit(f"sentinel argv mismatch: actual={actual!r} expected={expected!r}")
' "$checked_log" "$@"; then
        fail "byte-safe sentinel argv mismatch for $checked_case"
    fi
}

assert_sentinel_absent() {
    checked_case=$1
    if [ -e "$sentinel_log_root/$checked_case/sentinel.argv" ]; then
        fail "transaction sentinel was invoked before an allowed selection: $checked_case"
    fi
}

run_sentinel_policy_case() {
    policy_case=$1
    expected_verdict=$2
    shift 2
    current_case=$policy_case
    current_output=$case_root/$policy_case.output
    policy_status=0
    if env MOGUET_LIVE_SENTINEL_CASE="$policy_case" \
        sudo pacman "$@" >"$current_output" 2>&1; then
        policy_status=0
    else
        policy_status=$?
    fi
    if [ "$policy_status" -ne "$sentinel_status" ]; then
        fail "sentinel policy case returned $policy_status instead of $sentinel_status"
    fi
    assert_contains "moguet-live-pacman-sentinel: $expected_verdict" "$current_output"
    assert_sentinel_log "$policy_case" sudo pacman "$@"
    printf '  sentinel policy: %s -> %s (status %s)\n' \
        "$policy_case" "$expected_verdict" "$policy_status"
}

assert_runtime_sentinel_contract() {
    sentinel_metadata=$(stat -c '%U:%G:%a:%F' /usr/bin/pacman)
    if [ "$sentinel_metadata" != 'root:root:555:regular file' ]; then
        fail "runtime pacman sentinel is not root-owned mode 0555: $sentinel_metadata"
    fi
    if [ -w /usr/bin/pacman ]; then
        fail 'validation user can write the runtime pacman sentinel'
    fi
    real_pacman_metadata=$(stat -c '%U:%G:%a:%F' \
        /usr/libexec/moguet-live-validation/pacman.real)
    if [ "$real_pacman_metadata" != 'root:root:700:regular file' ]; then
        fail "isolated real pacman has unsafe ownership or mode: $real_pacman_metadata"
    fi
    if [ -x /usr/libexec/moguet-live-validation/pacman.real ]; then
        fail 'validation user can execute the isolated real pacman'
    fi
    printf '%s\n' ':: live provider sentinel policy preflight'
    run_sentinel_policy_case \
        sentinel-accept-rust 'accepted and blocked' \
        -S --asdeps --needed -- extra/rust
    run_sentinel_policy_case \
        sentinel-accept-rustup-noconfirm 'accepted and blocked' \
        -S --asdeps --needed --noconfirm -- extra/rustup
    run_sentinel_policy_case \
        sentinel-reject-pacman-u 'rejected argv' \
        -U -- /tmp/package.pkg.tar.zst
    run_sentinel_policy_case \
        sentinel-reject-remove 'rejected argv' \
        -R -- extra/rust
    run_sentinel_policy_case \
        sentinel-reject-syu 'rejected argv' \
        -Syu --noconfirm
    run_sentinel_policy_case \
        sentinel-reject-multiple 'rejected argv' \
        -S --asdeps --needed -- extra/rust extra/rustup
    run_sentinel_policy_case \
        sentinel-reject-unqualified 'rejected argv' \
        -S --asdeps --needed -- rust
    run_sentinel_policy_case \
        sentinel-reject-option 'rejected argv' \
        -S --asdeps --needed --overwrite '*' -- extra/rust
    run_sentinel_policy_case \
        sentinel-reject-unknown-target 'rejected argv' \
        -S --asdeps --needed -- extra/unknown-provider
}

prepare_case() {
    current_case=$1
    case_directory=$(mktemp -d "$case_root/$current_case.XXXXXX")
    case_source=$case_directory/local-package
    case_config=$case_directory/xdg-config
    case_cache=$case_directory/xdg-cache
    case_state=$case_directory/xdg-state
    case_output=$case_directory/moguet.output
    current_output=$case_output
    mkdir -m 0755 "$case_source"
    mkdir -m 0700 "$case_config" "$case_cache" "$case_state"
    cp "$fixture_pkgbuild" "$case_source/PKGBUILD"
    (
        cd "$case_source"
        makepkg --printsrcinfo > .SRCINFO
    )
    assert_regular_non_symlink "$case_source/PKGBUILD" 'case PKGBUILD'
    assert_regular_non_symlink "$case_source/.SRCINFO" 'generated case .SRCINFO'
    source_owner=$(stat -c '%u' "$case_source/.SRCINFO")
    if [ "$source_owner" -ne "$(id -u)" ]; then
        fail "generated .SRCINFO is not validation-user-owned: $source_owner"
    fi
    source_mode=$(stat -c '%a' "$case_source/.SRCINFO")
    if [ $((0$source_mode & 022)) -ne 0 ]; then
        fail "generated .SRCINFO is group/other writable: $source_mode"
    fi
    case_source_before=$(source_manifest "$case_source")
    case_package_database_before=$(package_database_manifest)
    assert_sentinel_absent "$current_case"
}

run_pty_case() {
    input_file=$1
    shift
    case_status=0
    if env \
        HOME="$HOME" \
        XDG_CONFIG_HOME="$case_config" \
        XDG_CACHE_HOME="$case_cache" \
        XDG_STATE_HOME="$case_state" \
        MOGUET_LIVE_SENTINEL_CASE="$current_case" \
        python3 "$pty_runner" --timeout 90 -- \
            "$production_moguet" "$@" \
            <"$input_file" >"$case_output" 2>&1; then
        case_status=0
    else
        case_status=$?
    fi
}

run_non_tty_case() {
    piped_value=$1
    shift
    case_status=0
    if printf '%s\n' "$piped_value" | env \
        HOME="$HOME" \
        XDG_CONFIG_HOME="$case_config" \
        XDG_CACHE_HOME="$case_cache" \
        XDG_STATE_HOME="$case_state" \
        MOGUET_LIVE_SENTINEL_CASE="$current_case" \
        "$production_moguet" "$@" \
        >"$case_output" 2>&1; then
        case_status=0
    else
        case_status=$?
    fi
}

assert_blocked_status() {
    if [ "$case_status" -eq 0 ]; then
        fail 'case unexpectedly succeeded'
    fi
    if [ "$case_status" -eq 124 ]; then
        fail 'case timed out instead of failing closed'
    fi
}

assert_no_source_or_install_execution() {
    assert_not_contains "Running: 'git'" "$case_output"
    assert_not_contains "Running: 'makepkg'" "$case_output"
    assert_not_contains "Running: 'sudo' 'pacman' '-U'" "$case_output"
    if find "$case_cache" -type d \
        \( -name '.local-source-workspace~-*' \
        -o -name '.artifact-workspace~-*' \) -print |
        grep . >/dev/null; then
        fail 'source or artifact workspace exists after blocked phase'
    fi
    if [ -e "$case_source/src" ] || [ -e "$case_source/pkg" ]; then
        fail 'local makepkg build directories were created'
    fi
}

assert_common_case_integrity() {
    case_source_after=$(source_manifest "$case_source")
    if [ "$case_source_after" != "$case_source_before" ]; then
        fail 'case-local PKGBUILD or generated .SRCINFO changed'
    fi
    case_package_database_after=$(package_database_manifest)
    if [ "$case_package_database_after" != "$case_package_database_before" ]; then
        fail 'container package installation state changed'
    fi
    tracked_fixture_after=$(tracked_fixture_manifest)
    if [ "$tracked_fixture_after" != "$tracked_fixture_before" ]; then
        fail 'tracked live fixture changed during an E2E case'
    fi
    if [ -e "$fixture_root/.SRCINFO" ]; then
        fail 'tracked live fixture gained .SRCINFO'
    fi
    assert_no_source_or_install_execution
}

parse_candidate_contract() {
    parsed_output=$1
    parsed_table=$2
    normalized_output=$parsed_output.normalized
    tr -d '\r' < "$parsed_output" > "$normalized_output"

    dependency_header_count=$(grep -F -c \
        ':: provider dependency=cargo' "$normalized_output" || true)
    if [ "$dependency_header_count" -ne 1 ]; then
        fail "expected one cargo provider header, observed $dependency_header_count"
    fi

    presented_count=$(grep -E -c '^[0-9]+\) ' "$normalized_output" || true)
    awk '
/^[0-9]+\) / {
    number = $1
    sub(/\)$/, "", number)
    source = package_name = repository = provided = ""
    for (field_index = 2; field_index <= NF; ++field_index) {
        split($field_index, field, "=")
        if (field[1] == "source") source = field[2]
        else if (field[1] == "package") package_name = field[2]
        else if (field[1] == "repository") repository = field[2]
        else if (field[1] == "provided") provided = field[2]
    }
    if (number !~ /^[0-9]+$/ || source == "" || package_name == "" ||
        repository == "" || provided == "") {
        exit 9
    }
    print number "\t" source "\t" package_name "\t" repository "\t" provided
}
' "$normalized_output" > "$parsed_table" ||
        fail 'provider candidate presentation could not be parsed safely'

    parsed_count=$(wc -l < "$parsed_table")
    if [ "$presented_count" -ne "$parsed_count" ] || [ "$parsed_count" -ne 2 ]; then
        fail "provider drift: expected exactly 2 parseable candidates, observed $presented_count/$parsed_count"
    fi
    if grep -F 'source=AUR' "$normalized_output" >/dev/null; then
        fail 'provider drift: AUR candidate entered the cargo candidate set'
    fi
    if [ "$(cut -f1 "$parsed_table" | LC_ALL=C sort -n | tr '\n' ' ')" != '1 2 ' ]; then
        fail 'provider presentation has duplicate, non-contiguous, or unsafe numbers'
    fi
    if [ "$(cut -f1 "$parsed_table" | LC_ALL=C sort -n | uniq | wc -l)" -ne 2 ]; then
        fail 'provider presentation contains duplicate numbers'
    fi
    if [ "$(cut -f3 "$parsed_table" | LC_ALL=C sort | uniq | wc -l)" -ne 2 ]; then
        fail 'provider candidate set contains duplicate package identities'
    fi
    while IFS="$(printf '\t')" read -r \
        candidate_number candidate_source candidate_package \
        candidate_repository candidate_dependency; do
        if [ "$candidate_source" != repository ] ||
            [ "$candidate_repository" != extra ] ||
            [ "$candidate_dependency" != cargo ]; then
            fail "provider drift: unsafe candidate identity $candidate_number/$candidate_source/$candidate_repository/$candidate_dependency"
        fi
        case "$candidate_package" in
            rust|rustup)
                ;;
            *)
                fail "provider drift: unexpected cargo provider $candidate_package"
                ;;
        esac
    done < "$parsed_table"
    if [ "$(awk -F '\t' '$3 == "rust" { count++ } END { print count + 0 }' "$parsed_table")" -ne 1 ] ||
        [ "$(awk -F '\t' '$3 == "rustup" { count++ } END { print count + 0 }' "$parsed_table")" -ne 1 ]; then
        fail 'provider drift: exact extra/rust and extra/rustup identities were not both present once'
    fi
}

assert_same_candidate_presentation() {
    candidate_table=$case_directory/candidates.tsv
    parse_candidate_contract "$case_output" "$candidate_table"
    if ! cmp -s "$discovery_table" "$candidate_table"; then
        fail 'provider presentation changed after discovery; refusing implicit number reuse'
    fi
}

print_candidate_summary() {
    summary_table=$1
    printf '  candidate identities:'
    while IFS="$(printf '\t')" read -r \
        summary_number _ summary_package summary_repository _; do
        printf ' %s=%s/%s' \
            "$summary_number" "$summary_repository" "$summary_package"
    done < "$summary_table"
    printf '\n'
}

assert_ambiguous_diagnostic() {
    if ! grep -i 'ambiguous provider' "$case_output" >/dev/null; then
        fail 'ambiguous provider diagnostic was not emitted'
    fi
}

assert_selection_failure_case() {
    expected_prompt_count=$1
    assert_blocked_status
    assert_same_candidate_presentation
    assert_count "$expected_prompt_count" 'Select a provider from [1-2]' \
        "$case_output.normalized"
    assert_ambiguous_diagnostic
    assert_sentinel_absent "$current_case"
    assert_not_contains 'Installing selected repository providers:' "$case_output"
    assert_not_contains "Running: 'sudo'" "$case_output"
    assert_common_case_integrity
}

assert_selected_provider_transaction() {
    expected_package=$1
    other_package=rust
    if [ "$expected_package" = rust ]; then
        other_package=rustup
    fi

    selected_install_intent="Installing selected repository providers: 'extra/$expected_package'"
    selected_transaction_intent="Running: 'sudo' 'pacman' '-S' '--asdeps' '--needed' '--' 'extra/$expected_package'"
    selected_sentinel_diagnostic="moguet-live-pacman-sentinel: accepted and blocked sudo pacman argv for extra/$expected_package"
    other_install_intent="Installing selected repository providers: 'extra/$other_package'"
    other_transaction_intent="Running: 'sudo' 'pacman' '-S' '--asdeps' '--needed' '--' 'extra/$other_package'"

    assert_exact_message_count 1 "$selected_install_intent" "$case_output.normalized"
    assert_exact_message_count 1 "$selected_transaction_intent" "$case_output.normalized"
    assert_exact_message_count 1 "$selected_sentinel_diagnostic" "$case_output.normalized"
    assert_exact_message_count 0 "$other_install_intent" "$case_output.normalized"
    assert_exact_message_count 0 "$other_transaction_intent" "$case_output.normalized"
    assert_contains 'Failed to install selected repository providers.' "$case_output"
    assert_sentinel_log "$current_case" \
        sudo pacman -S --asdeps --needed -- "extra/$expected_package"
}

assert_valid_selection_case() {
    expected_package=$1
    assert_blocked_status
    assert_same_candidate_presentation
    assert_count 1 'Select a provider from [1-2]' "$case_output.normalized"
    assert_selected_provider_transaction "$expected_package"
    assert_common_case_integrity
}

assert_invalid_retry_selection_before_mutation() {
    selected_package=$1
    if ! python3 - "$case_output.normalized" "$selected_package" <<'PY'
from pathlib import Path
import sys

output = Path(sys.argv[1]).read_bytes()
selected_package = sys.argv[2].encode("ascii")

invalid_diagnostic = b"Invalid choice. Enter a number from [1-2]"
provider_prompt = b"Select a provider from [1-2]"
install_intent = b"Installing selected repository providers: 'extra/" + selected_package + b"'"
transaction_intent = (
    b"Running: 'sudo' 'pacman' '-S' '--asdeps' '--needed' '--' 'extra/" +
    selected_package + b"'"
)

def positions(needle):
    found = []
    start = 0
    while True:
        position = output.find(needle, start)
        if position < 0:
            return found
        found.append(position)
        start = position + len(needle)

invalid_positions = positions(invalid_diagnostic)
prompt_positions = positions(provider_prompt)
install_positions = positions(install_intent)
transaction_positions = positions(transaction_intent)
if len(invalid_positions) != 3 or len(prompt_positions) != 4:
    raise SystemExit("invalid-retry diagnostic or prompt count changed before position check")
if len(install_positions) != 1 or len(transaction_positions) != 1:
    raise SystemExit("invalid-retry transaction intent is not exactly once before position check")

transaction_position = transaction_positions[0]
if any(position >= transaction_position for position in invalid_positions):
    raise SystemExit("an invalid-choice diagnostic appeared after transaction intent")
if prompt_positions[3] >= install_positions[0]:
    raise SystemExit("the fourth provider prompt did not precede selected-provider intent")
if install_positions[0] > transaction_position:
    raise SystemExit("provider install intent appeared after its transaction intent")
PY
    then
        fail 'invalid-retry did not prove selection-before-mutation ordering'
    fi
}

assert_regular_non_symlink "$fixture_pkgbuild" 'tracked live PKGBUILD fixture'
assert_regular_non_symlink "$fixture_contract" 'tracked live fixture contract'
assert_regular_non_symlink "$pty_runner" 'production PTY helper'
assert_regular_non_symlink "$production_moguet" 'production Moguet binary'
if [ "$(id -u)" -eq 0 ]; then
    fail 'live provider runner must execute as an unprivileged validation user'
fi
if [ -e "$repo_root/.git" ]; then
    fail 'Docker build context leaked host .git metadata into the image'
fi
if find "$repo_root" -xdev \
    \( -name .ssh -o -name .gnupg -o -name .git-credentials \
    -o -name .netrc -o -name docker.sock \) -print |
    grep . >/dev/null; then
    fail 'Docker build context leaked credential or Docker socket state'
fi

mkdir -p "$case_root"
tracked_fixture_before=$(tracked_fixture_manifest)
tracked_pkgbuild_before=$(sha256sum "$fixture_pkgbuild")
initial_package_database=$(package_database_manifest)

printf '%s\n' ':: Arch live provider-selection validation'
printf '  production binary: %s\n' "$production_moguet"
printf '  runtime user: uid=%s gid=%s\n' "$(id -u)" "$(id -g)"
printf '  expected blocked phase: real pacman repository transaction\n'

assert_runtime_sentinel_contract

printf '%s\n' ':: case=provider-discovery'
prepare_case provider-discovery
discovery_input=$case_directory/input
printf 'q\n' > "$discovery_input"
run_pty_case "$discovery_input" --noedit build --local "$case_source"
assert_blocked_status
discovery_table=$case_directory/candidates.tsv
parse_candidate_contract "$case_output" "$discovery_table"
rust_choice=$(awk -F '\t' '$3 == "rust" { print $1 }' "$discovery_table")
rustup_choice=$(awk -F '\t' '$3 == "rustup" { print $1 }' "$discovery_table")
if [ -z "$rust_choice" ] || [ -z "$rustup_choice" ] ||
    [ "$rust_choice" = "$rustup_choice" ]; then
    fail 'provider choices could not be resolved from candidate identities'
fi
assert_count 1 'Select a provider from [1-2]' "$case_output.normalized"
assert_ambiguous_diagnostic
assert_sentinel_absent provider-discovery
assert_not_contains "Running: 'sudo'" "$case_output"
assert_common_case_integrity
print_candidate_summary "$discovery_table"
printf '  resolved choices from identities: extra/rust=%s extra/rustup=%s\n' \
    "$rust_choice" "$rustup_choice"
printf '%s\n' '  expected blocked phase: provider selection cancellation'

printf '%s\n' ':: case=rust-selection'
prepare_case rust-selection
rust_input=$case_directory/input
printf '%s\n' "$rust_choice" > "$rust_input"
run_pty_case "$rust_input" --noedit build --local "$case_source"
assert_valid_selection_case rust
print_candidate_summary "$candidate_table"
printf '  resolved choice: extra/rust=%s\n' "$rust_choice"
printf '%s\n' '  sentinel argv: sudo pacman -S --asdeps --needed -- extra/rust'
printf '%s\n' '  expected blocked phase: repository provider transaction'

printf '%s\n' ':: case=rustup-selection'
prepare_case rustup-selection
rustup_input=$case_directory/input
printf '%s\n' "$rustup_choice" > "$rustup_input"
run_pty_case "$rustup_input" --noedit build --local "$case_source"
assert_valid_selection_case rustup
print_candidate_summary "$candidate_table"
printf '  resolved choice: extra/rustup=%s\n' "$rustup_choice"
printf '%s\n' '  sentinel argv: sudo pacman -S --asdeps --needed -- extra/rustup'
printf '%s\n' '  expected blocked phase: repository provider transaction'

printf '%s\n' ':: case=invalid-retry'
prepare_case invalid-retry
invalid_input=$case_directory/input
out_of_range_choice=3
printf 'not-a-number\n0\n%s\n%s\n' \
    "$out_of_range_choice" "$rust_choice" > "$invalid_input"
run_pty_case "$invalid_input" --noedit build --local "$case_source"
assert_blocked_status
assert_same_candidate_presentation
assert_count 3 'Invalid choice. Enter a number from [1-2]' \
    "$case_output.normalized"
assert_count 4 'Select a provider from [1-2]' "$case_output.normalized"
assert_selected_provider_transaction rust
assert_invalid_retry_selection_before_mutation rust
assert_common_case_integrity
print_candidate_summary "$candidate_table"
printf '  retry inputs: non-numeric, zero, out-of-range=%s; valid=%s\n' \
    "$out_of_range_choice" "$rust_choice"
printf '%s\n' '  retry prompts=4 invalid diagnostics=3 sentinel calls before valid=0'
printf '%s\n' '  expected blocked phase: repository provider transaction after valid retry'

printf '%s\n' ':: case=cancel-empty'
prepare_case cancel-empty
cancel_empty_input=$case_directory/input
printf '\n' > "$cancel_empty_input"
run_pty_case "$cancel_empty_input" --noedit build --local "$case_source"
assert_selection_failure_case 1
printf '%s\n' '  input: empty; default selection: none'
printf '%s\n' '  expected blocked phase: provider selection cancellation'

printf '%s\n' ':: case=cancel-q'
prepare_case cancel-q
cancel_q_input=$case_directory/input
printf 'q\n' > "$cancel_q_input"
run_pty_case "$cancel_q_input" --noedit build --local "$case_source"
assert_selection_failure_case 1
printf '%s\n' '  input: q; default selection: none'
printf '%s\n' '  expected blocked phase: provider selection cancellation'

printf '%s\n' ':: case=provider-eof'
prepare_case provider-eof
eof_input=$case_directory/input
# In canonical PTY mode, VEOF at an empty line makes getline observe EOF while
# retaining a real terminal on stdin.
printf '\004' > "$eof_input"
run_pty_case "$eof_input" --noedit build --local "$case_source"
assert_selection_failure_case 1
printf '%s\n' '  input: PTY VEOF; default selection: none'
printf '%s\n' '  expected blocked phase: provider selection EOF cancellation'

printf '%s\n' ':: case=non-tty-pipe'
prepare_case non-tty-pipe
run_non_tty_case "$rust_choice" --noedit build --local "$case_source"
assert_blocked_status
tr -d '\r' < "$case_output" > "$case_output.normalized"
assert_not_contains ':: provider dependency=cargo' "$case_output.normalized"
assert_not_contains 'Select a provider from' "$case_output.normalized"
assert_ambiguous_diagnostic
assert_sentinel_absent non-tty-pipe
assert_not_contains "Running: 'sudo'" "$case_output"
assert_common_case_integrity
printf '  piped candidate-like value: %s; consumed as selection: no\n' "$rust_choice"
printf '%s\n' '  expected blocked phase: non-TTY ambiguous provider guard'

printf '%s\n' ':: case=noconfirm-tty'
prepare_case noconfirm-tty
noconfirm_input=$case_directory/input
printf '%s\n' "$rust_choice" > "$noconfirm_input"
run_pty_case "$noconfirm_input" --noedit --noconfirm build --local "$case_source"
assert_blocked_status
tr -d '\r' < "$case_output" > "$case_output.normalized"
assert_not_contains ':: provider dependency=cargo' "$case_output.normalized"
assert_not_contains 'Select a provider from' "$case_output.normalized"
assert_ambiguous_diagnostic
assert_sentinel_absent noconfirm-tty
assert_not_contains "Running: 'sudo'" "$case_output"
assert_common_case_integrity
printf '  TTY candidate-like value: %s; auto-selected: no\n' "$rust_choice"
printf '%s\n' '  expected blocked phase: --noconfirm ambiguous provider guard'

final_tracked_fixture=$(tracked_fixture_manifest)
final_tracked_pkgbuild=$(sha256sum "$fixture_pkgbuild")
final_package_database=$(package_database_manifest)
if [ "$final_tracked_fixture" != "$tracked_fixture_before" ] ||
    [ "$final_tracked_pkgbuild" != "$tracked_pkgbuild_before" ]; then
    fail 'tracked provider fixture checksum changed across the live E2E lane'
fi
if [ "$final_package_database" != "$initial_package_database" ]; then
    fail 'container package database changed across the live E2E lane'
fi

printf '%s\n' 'arch-live-provider: all provider-selection cases passed'
