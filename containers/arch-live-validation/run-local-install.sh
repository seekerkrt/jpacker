#!/bin/sh

set -eu

export LC_ALL=C.UTF-8
export LANG=C.UTF-8
export LANGUAGE=en

repo_root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
fixture_root=/usr/libexec/moguet-live-local/fixtures/local-package
fixture_pkgbuild=$fixture_root/PKGBUILD
fixture_contract=$fixture_root/contract.env
pty_runner=$repo_root/tests/run-with-pty.py
production_moguet=$repo_root/moguet
case_root=$HOME/live-local-case
gateway_evidence_root=/var/log/moguet-live-local
gateway_staging_root=/var/lib/moguet-live-local/staging
gateway_case=local-root-install
gateway_status=97
fixture_name=moguet-live-fixture
fixture_version=1.0.0-1
fixture_arch=any
fixture_artifact=${fixture_name}-${fixture_version}-${fixture_arch}.pkg.tar.zst
current_phase=preflight
current_output=

fail() {
    printf 'arch-live-local: FAIL (%s): %s\n' "$current_phase" "$*" >&2
    if [ -n "$current_output" ] && [ -f "$current_output" ]; then
        printf 'arch-live-local: case output (%s):\n' "$current_output" >&2
        sed -n '1,360p' "$current_output" >&2
    fi
    if [ -d "$gateway_evidence_root/$gateway_case" ]; then
        printf 'arch-live-local: root gateway evidence:\n' >&2
        find "$gateway_evidence_root/$gateway_case" -maxdepth 1 -type f \
            -printf '  %f\n' | LC_ALL=C sort >&2
        for evidence_file in \
            stage-hashes.txt staged-artifact-path.txt accepted.argv PKGINFO \
            archive-members.txt expected-members.txt
        do
            if [ -f "$gateway_evidence_root/$gateway_case/$evidence_file" ]; then
                printf 'arch-live-local: %s:\n' "$evidence_file" >&2
                sed -n '1,180p' \
                    "$gateway_evidence_root/$gateway_case/$evidence_file" >&2
            fi
        done
    fi
    exit 1
}

assert_regular_non_symlink() {
    checked_path=$1
    checked_label=$2
    [ -f "$checked_path" ] && [ ! -L "$checked_path" ] ||
        fail "$checked_label must be a regular non-symlink: $checked_path"
}

assert_contains() {
    expected_text=$1
    checked_file=$2
    grep -F -- "$expected_text" "$checked_file" >/dev/null ||
        fail "missing expected output: $expected_text"
}

assert_not_contains() {
    unexpected_text=$1
    checked_file=$2
    if grep -F -- "$unexpected_text" "$checked_file" >/dev/null; then
        fail "unexpected output: $unexpected_text"
    fi
}

assert_metadata() {
    checked_path=$1
    expected_metadata=$2
    checked_label=$3
    actual_metadata=$(stat -c '%U:%G:%a:%F' "$checked_path")
    [ "$actual_metadata" = "$expected_metadata" ] ||
        fail "$checked_label metadata drift: $actual_metadata"
}

source_tree_manifest() {
    manifest_root=$1
    (
        cd "$manifest_root"
        sha256sum PKGBUILD contract.env
        find . -mindepth 1 -maxdepth 1 -printf '%y %m %f\n' | LC_ALL=C sort
    )
}

source_content_manifest() {
    manifest_root=$1
    (
        cd "$manifest_root"
        sha256sum PKGBUILD contract.env
        find . -mindepth 1 -maxdepth 1 -printf '%y %f\n' | LC_ALL=C sort
    )
}

fixture_manifest() {
    source_tree_manifest "$fixture_root"
}

capture_inventory() {
    inventory_prefix=$1
    pacman -Q > "$inventory_prefix.packages"
    pacman -Qeq > "$inventory_prefix.explicit"
    pacman -Qdq > "$inventory_prefix.dependency"
    python3 - \
        "$inventory_prefix.packages" \
        "$inventory_prefix.explicit" \
        "$inventory_prefix.dependency" \
        "$inventory_prefix.tsv" <<'PY'
from pathlib import Path
import sys

packages = Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
explicit = set(Path(sys.argv[2]).read_text(encoding="utf-8").splitlines())
dependency = set(Path(sys.argv[3]).read_text(encoding="utf-8").splitlines())
records = []
for line in packages:
    fields = line.split()
    if len(fields) != 2:
        raise SystemExit(f"unsafe pacman -Q record: {line!r}")
    name, version = fields
    if (name in explicit) == (name in dependency):
        raise SystemExit(f"install reason is not singular: {name}")
    records.append((name, version, "Explicit" if name in explicit else "Dependency"))
if len(records) != len({name for name, _version, _reason in records}):
    raise SystemExit("duplicate package identity in inventory")
Path(sys.argv[4]).write_text(
    "".join("\t".join(record) + "\n" for record in sorted(records)),
    encoding="utf-8",
)
PY
}

prepare_source_root() {
    prepared_root=$1
    fixture_before_copy=$(fixture_manifest)
    fixture_content_before_copy=$(source_content_manifest "$fixture_root")
    rm -rf -- "$prepared_root"
    mkdir -m 0700 "$prepared_root"
    cp -R "$fixture_root" "$prepared_root/source"
    chmod u+w "$prepared_root/source" \
        "$prepared_root/source/PKGBUILD" \
        "$prepared_root/source/contract.env"
    [ ! -e "$prepared_root/source/.SRCINFO" ] ||
        fail 'root-owned local fixture authority unexpectedly has generated .SRCINFO'
    fixture_after_copy=$(fixture_manifest)
    fixture_content_after_copy=$(source_content_manifest "$fixture_root")
    case_copy_manifest=$(source_content_manifest "$prepared_root/source")
    [ "$fixture_before_copy" = "$fixture_after_copy" ] &&
        [ "$fixture_content_before_copy" = "$fixture_content_after_copy" ] &&
        [ "$fixture_content_before_copy" = "$case_copy_manifest" ] ||
        fail 'case-local source copy differs from the root-owned fixture authority'
    assert_metadata "$prepared_root/source" \
        'moguet-validation:moguet-validation:755:directory' \
        'case-local source root after authority copy'
    assert_metadata "$prepared_root/source/PKGBUILD" \
        'moguet-validation:moguet-validation:644:regular file' \
        'case-local PKGBUILD after authority copy'
    assert_metadata "$prepared_root/source/contract.env" \
        'moguet-validation:moguet-validation:644:regular file' \
        'case-local fixture contract after authority copy'
}

assert_source_root_unchanged() {
    source_root=$1
    fixture_before=$2
    [ -d "$source_root" ] && [ ! -L "$source_root" ] ||
        fail 'case-local source root is not a regular directory'
    [ ! -e "$source_root/.SRCINFO" ] ||
        fail 'production local invocation wrote .SRCINFO into the case source root'
    [ ! -e "$source_root/src" ] && [ ! -e "$source_root/pkg" ] ||
        fail 'production local invocation wrote makepkg work directories into the case source root'
    case_manifest=$( (
        cd "$source_root"
        sha256sum PKGBUILD contract.env
        find . -mindepth 1 -maxdepth 1 -printf '%y %m %f\n' | LC_ALL=C sort
    ) )
    [ "$case_manifest" = "$fixture_before" ] ||
        fail 'case-local source root changed during production execution'
}

run_gateway_rejection() {
    rejection_name=$1
    case_mode=$2
    shift 2
    rejection_output=$case_root/reject-$rejection_name.output
    if env MOGUET_LIVE_LOCAL_CASE="$case_mode" \
        sudo pacman "$@" > "$rejection_output" 2>&1; then
        rejection_status=0
    else
        rejection_status=$?
    fi
    [ "$rejection_status" -eq "$gateway_status" ] ||
        fail "gateway rejection $rejection_name returned $rejection_status"
    assert_contains 'moguet-live-local-gateway: rejected:' "$rejection_output"
    [ ! -e "$gateway_evidence_root/$gateway_case" ] ||
        fail "gateway rejection $rejection_name consumed local install evidence"
    [ ! -e "$gateway_staging_root/$gateway_case" ] ||
        fail "gateway rejection $rejection_name consumed local install staging"
    printf '  gateway rejection: %s -> status %s\n' \
        "$rejection_name" "$rejection_status"
}

run_pty() {
    input_file=$1
    output_file=$2
    source_root=$3
    cache_root=$4
    state_root=$5
    mkdir -m 0700 -- "$cache_root" "$state_root"
    if env \
        XDG_CONFIG_HOME="$HOME/.config" \
        XDG_CACHE_HOME="$cache_root" \
        XDG_STATE_HOME="$state_root" \
        MOGUET_LIVE_LOCAL_CASE="$gateway_case" \
        python3 "$pty_runner" --timeout 900 -- \
            "$production_moguet" --noedit build --local "$source_root" \
            < "$input_file" > "$output_file" 2>&1; then
        run_status=0
    else
        run_status=$?
    fi
}

parse_rust_choice() {
    output_file=$1
    candidate_table=$2
    tr -d '\r' < "$output_file" > "$output_file.normalized"
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
        repository == "" || provided == "") exit 9
    print number "\t" source "\t" package_name "\t" repository "\t" provided
}
' "$output_file.normalized" > "$candidate_table" ||
        fail 'provider presentation could not be parsed safely'
    [ "$(wc -l < "$candidate_table")" -eq 2 ] ||
        fail 'cargo provider candidate count drifted from the reviewed two choices'
    while IFS="$(printf '\t')" read -r number source package repository provided; do
        [ "$source" = repository ] && [ "$repository" = extra ] && \
            [ "$provided" = cargo ] ||
            fail "unsafe provider identity: $number/$source/$repository/$provided"
        case "$package" in
            rust|rustup) ;;
            *) fail "unexpected cargo provider: $package" ;;
        esac
    done < "$candidate_table"
    rust_choice=$(awk -F '\t' '$3 == "rust" { print $1; count++ } END { if (count != 1) exit 1 }' "$candidate_table") ||
        fail 'cargo provider set does not contain exactly one extra/rust choice'
    [ "$(awk -F '\t' '$3 == "rustup" { count++ } END { print count + 0 }' "$candidate_table")" -eq 1 ] ||
        fail 'cargo provider set does not contain exactly one extra/rustup choice'
    printf '%s\n' "$rust_choice"
}

assert_inventory_transition() {
    before_file=$1
    after_file=$2
    python3 - "$before_file" "$after_file" "$fixture_name" <<'PY'
from pathlib import Path
import sys

def parse(path: str) -> dict[str, tuple[str, str]]:
    records = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) != 3 or not all(fields):
            raise SystemExit(f"unsafe inventory row: {line!r}")
        name, version, reason = fields
        if reason not in {"Explicit", "Dependency"} or name in records:
            raise SystemExit(f"unsafe inventory identity: {line!r}")
        records[name] = (version, reason)
    return records

before = parse(sys.argv[1])
after = parse(sys.argv[2])
fixture = sys.argv[3]
for name, record in before.items():
    if after.get(name) != record:
        raise SystemExit(f"baseline package version or reason changed: {name}")
if fixture not in after or after[fixture] != ("1.0.0-1", "Explicit"):
    raise SystemExit("local root did not install as the exact explicit fixture package")
if "rust" not in after or after["rust"][1] != "Dependency":
    raise SystemExit("selected rust provider did not retain dependency install reason")
if fixture in before or "rust" in before or "rustup" in before:
    raise SystemExit("fixture or cargo provider was installed before the local case")
if "moguet-live-fixture-debug" in after:
    raise SystemExit("unselected local debug artifact was installed")
new_names = set(after) - set(before)
if fixture not in new_names or "rust" not in new_names:
    raise SystemExit("local fixture or selected provider was not newly installed")
for name in new_names - {fixture}:
    if after[name][1] != "Dependency":
        raise SystemExit(f"new provider transaction package is not a dependency: {name}")
print(f"inventory transition: {len(new_names)} new package(s), root explicit and providers dependency")
PY
}

assert_regular_non_symlink "$fixture_pkgbuild" 'root-owned local PKGBUILD fixture authority'
assert_regular_non_symlink "$fixture_contract" 'root-owned local fixture contract authority'
assert_regular_non_symlink "$pty_runner" 'PTY runner'
assert_regular_non_symlink "$production_moguet" 'production Moguet binary'
[ "$(id -u)" -eq 1000 ] && [ "$(id -g)" -eq 1000 ] ||
    fail 'live local runner must execute as validation uid/gid 1000'
[ ! -e "$repo_root/.git" ] || fail 'container source copy unexpectedly includes .git'
assert_metadata /usr/bin/pacman 'root:root:555:regular file' 'canonical local gateway'
assert_metadata /usr/libexec/moguet-live-local/pacman.real \
    'root:root:755:regular file' 'isolated real pacman'
assert_metadata /usr/libexec/moguet-live-local/local-stage-artifact.py \
    'root:root:755:regular file' 'root staging helper'
assert_metadata "$fixture_root" \
    'root:root:555:directory' 'local fixture authority root'
assert_metadata "$fixture_pkgbuild" \
    'root:root:444:regular file' 'local PKGBUILD fixture authority'
assert_metadata "$fixture_contract" \
    'root:root:444:regular file' 'local fixture contract authority'
[ ! -w "$fixture_root" ] && [ ! -w "$fixture_pkgbuild" ] &&
    [ ! -w "$fixture_contract" ] ||
    fail 'validation user can modify the root-owned local fixture authority'
assert_metadata "$gateway_evidence_root" \
    'root:moguet-validation:750:directory' 'local gateway evidence root'
assert_metadata "$gateway_staging_root" \
    'root:moguet-validation:750:directory' 'local gateway staging root'

mkdir -m 0700 "$case_root/runtime"
fixture_authority_before=$(fixture_manifest)
capture_inventory "$case_root/runtime/before-rejection"
printf '%s\n' ':: root gateway fail-closed self-test'
run_gateway_rejection system-upgrade "$gateway_case" -Syu
run_gateway_rejection removal "$gateway_case" -R rust
run_gateway_rejection outside-artifact "$gateway_case" \
    -U -- /tmp/unsafe.pkg.tar.zst
run_gateway_rejection wrong-provider "$gateway_case" \
    -S --asdeps --needed -- extra/cargo
run_gateway_rejection missing-case missing -Syu
capture_inventory "$case_root/runtime/after-rejection"
cmp -s "$case_root/runtime/before-rejection.tsv" \
    "$case_root/runtime/after-rejection.tsv" ||
    fail 'gateway rejection self-test changed package inventory'

printf '%s\n' ':: live local PKGBUILD provider discovery'
discovery_root=$case_root/discovery
prepare_source_root "$discovery_root"
discovery_source=$discovery_root/source
discovery_source_before=$(source_tree_manifest "$discovery_source")
discovery_input=$discovery_root/input.txt
discovery_output=$discovery_root/output.txt
printf 'y\nq\n' > "$discovery_input"
run_pty "$discovery_input" "$discovery_output" "$discovery_source" \
    "$discovery_root/cache" "$discovery_root/state"
[ "$run_status" -ne 0 ] && [ "$run_status" -ne 124 ] ||
    fail 'provider discovery did not cancel cleanly before a transaction'
tr -d '\r' < "$discovery_output" > "$discovery_output.normalized"
current_phase=provider-discovery
current_output=$discovery_output
assert_contains 'Select a provider from [1-2]' "$discovery_output.normalized"
assert_contains 'ambiguous providers: cargo' "$discovery_output.normalized"
assert_source_root_unchanged "$discovery_source" "$discovery_source_before"
capture_inventory "$discovery_root/after"
cmp -s "$case_root/runtime/after-rejection.tsv" "$discovery_root/after.tsv" ||
    fail 'provider discovery changed package inventory'
rust_choice=$(parse_rust_choice "$discovery_output" "$discovery_root/candidates.tsv")
printf '  reviewed provider choice: %s=extra/rust\n' "$rust_choice"

printf '%s\n' ':: real local PKGBUILD build and install'
actual_root=$case_root/actual
prepare_source_root "$actual_root"
actual_source=$actual_root/source
actual_source_before=$(source_tree_manifest "$actual_source")
actual_input=$actual_root/input.txt
actual_output=$actual_root/output.txt
printf 'y\n%s\ny\ny\n' "$rust_choice" > "$actual_input"
current_phase=local-build-install
current_output=$actual_output
run_pty "$actual_input" "$actual_output" "$actual_source" \
    "$actual_root/cache" "$actual_root/state"
[ "$run_status" -eq 0 ] || fail "production local build/install returned $run_status"
tr -d '\r' < "$actual_output" > "$actual_output.normalized"
assert_contains "Installing selected repository providers: 'extra/rust'" \
    "$actual_output.normalized"
assert_contains "Running: 'sudo' 'pacman' '-S' '--asdeps' '--needed' '--' 'extra/rust'" \
    "$actual_output.normalized"
assert_contains "Running: 'sudo' 'pacman' '-U' '--'" \
    "$actual_output.normalized"
assert_contains 'Local PackageBase result: moguet-live-fixture' \
    "$actual_output.normalized"
assert_contains 'required child: moguet-live-fixture 1.0.0-1 (explicit): installed' \
    "$actual_output.normalized"
assert_not_contains "Running: 'git'" "$actual_output.normalized"
assert_source_root_unchanged "$actual_source" "$actual_source_before"
[ "$fixture_authority_before" = "$(fixture_manifest)" ] ||
    fail 'root-owned local fixture authority changed during live execution'
find "$actual_root/cache" -type d \
    \( -name '.local-source-workspace~-*' -o -name '.artifact-workspace~-*' \) \
    -print | grep . >/dev/null &&
    fail 'production did not clean its local source or artifact workspace'
capture_inventory "$actual_root/after"
assert_inventory_transition "$case_root/runtime/after-rejection.tsv" \
    "$actual_root/after.tsv"
pacman -Qe "$fixture_name" >/dev/null ||
    fail 'local root is not installed as explicit'
if pacman -Qd "$fixture_name" >/dev/null 2>&1; then
    fail 'local root is installed as a dependency'
fi
pacman -Qd rust >/dev/null ||
    fail 'selected rust provider is not installed as a dependency'
if pacman -Qe rust >/dev/null 2>&1; then
    fail 'selected rust provider is installed as explicit'
fi

assert_metadata "$gateway_evidence_root/$gateway_case" \
    'root:moguet-validation:750:directory' 'accepted root evidence directory'
assert_metadata "$gateway_staging_root/$gateway_case" \
    'root:moguet-validation:750:directory' 'accepted root staging directory'
for evidence_file in \
    stage-hashes.txt staged-artifact-path.txt accepted.argv PKGINFO \
    archive-members.txt expected-members.txt
do
    assert_regular_non_symlink "$gateway_evidence_root/$gateway_case/$evidence_file" \
        "accepted root evidence $evidence_file"
    assert_metadata "$gateway_evidence_root/$gateway_case/$evidence_file" \
        'root:moguet-validation:640:regular file' \
        "accepted root evidence $evidence_file"
done
python3 - "$gateway_evidence_root/$gateway_case/accepted.argv" <<'PY'
from pathlib import Path
import sys

argv = Path(sys.argv[1]).read_bytes().split(b"\0")
if argv[-1:] != [b""]:
    raise SystemExit("gateway argv evidence is not NUL terminated")
actual = [item.decode("utf-8", "strict") for item in argv[:-1]]
if actual[:4] != ["sudo", "pacman", "-U", "--"] or len(actual) != 5:
    raise SystemExit(f"unexpected accepted local gateway argv: {actual!r}")
if not actual[-1].endswith("/moguet-live-fixture-1.0.0-1-any.pkg.tar.zst"):
    raise SystemExit("accepted local gateway artifact identity drift")
PY

printf '%s\n' 'arch-live-local: all checks passed'
