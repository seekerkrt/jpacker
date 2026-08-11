#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
. "$repo_root/scripts/validation-status.sh"

tmp_dir=$(mktemp -d)
cleanup() {
    rm -rf -- "$tmp_dir" >/dev/null 2>&1 || :
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'validation-status-test: %s\n' "$*" >&2
    exit 1
}

partial_producer() {
    printf '%s\n' zebra alpha
    return 42
}

partial_raw=$tmp_dir/partial.raw
partial_normalized=$tmp_dir/partial.normalized
if validation_capture_sorted_output \
    "$partial_raw" "$partial_normalized" partial_producer; then
    fail 'partial producer unexpectedly produced normalized evidence'
else
    partial_status=$?
fi
[ "$partial_status" -eq 42 ] ||
    fail "partial producer status changed: $partial_status"
[ "$(cat "$partial_raw")" = "zebra
alpha" ] || fail 'partial raw output was not retained for diagnostics'
[ ! -e "$partial_normalized" ] ||
    fail 'partial producer output was promoted to normalized evidence'

snapshot_fixture=$tmp_dir/snapshot-fixture
mkdir -p "$snapshot_fixture/subdirectory"
printf '%s\n' 'snapshot payload' >"$snapshot_fixture/subdirectory/payload"

failing_stat=$tmp_dir/failing-stat
printf '%s\n' \
    '#!/bin/sh' \
    "printf '%s\\n' 'partial stat snapshot evidence'" \
    'exit 42' >"$failing_stat"
chmod 0755 "$failing_stat"
stat_snapshot=$tmp_dir/stat-fault.snapshot
if MOGUET_TEST_SNAPSHOT_FAULT_ROOT=$snapshot_fixture \
    MOGUET_TEST_SNAPSHOT_FAULT_OUTPUT=$stat_snapshot \
    MOGUET_TEST_SNAPSHOT_STAT_COMMAND=$failing_stat \
    /bin/sh "$repo_root/tests/test-build-cache-symlink.sh" /bin/true \
    >"$tmp_dir/stat-fault.stdout" 2>"$tmp_dir/stat-fault.stderr"; then
    fail 'stat child failure did not fail the build-cache snapshot test'
else
    stat_snapshot_status=$?
fi
[ "$stat_snapshot_status" -eq 1 ] ||
    fail "stat snapshot test status changed: $stat_snapshot_status"
[ -f "$stat_snapshot.raw" ] || {
    sed -n '1,160p' "$tmp_dir/stat-fault.stderr" >&2 || :
    fail 'stat snapshot test failed before retaining raw evidence'
}
grep -F 'partial stat snapshot evidence' "$stat_snapshot.raw" >/dev/null ||
    fail 'stat partial raw snapshot evidence was not retained'
[ ! -e "$stat_snapshot" ] ||
    fail 'stat partial output was promoted to a normalized snapshot'

failing_cksum=$tmp_dir/failing-cksum
printf '%s\n' \
    '#!/bin/sh' \
    "printf '%s\\n' 'partial cksum snapshot evidence'" \
    'exit 42' >"$failing_cksum"
chmod 0755 "$failing_cksum"
cksum_snapshot=$tmp_dir/cksum-fault.snapshot
if MOGUET_TEST_SNAPSHOT_FAULT_ROOT=$snapshot_fixture \
    MOGUET_TEST_SNAPSHOT_FAULT_OUTPUT=$cksum_snapshot \
    MOGUET_TEST_SNAPSHOT_CKSUM_COMMAND=$failing_cksum \
    /bin/sh "$repo_root/tests/test-commands-source-maintenance.sh" \
    /bin/true /bin/true /bin/true \
    >"$tmp_dir/cksum-fault.stdout" 2>"$tmp_dir/cksum-fault.stderr"; then
    fail 'cksum child failure did not fail the source-tree snapshot test'
else
    cksum_snapshot_status=$?
fi
[ "$cksum_snapshot_status" -eq 1 ] ||
    fail "cksum snapshot test status changed: $cksum_snapshot_status"
[ -f "$cksum_snapshot.raw" ] || {
    sed -n '1,160p' "$tmp_dir/cksum-fault.stderr" >&2 || :
    fail 'cksum snapshot test failed before retaining raw evidence'
}
grep -F 'partial cksum snapshot evidence' \
    "$cksum_snapshot.raw" >/dev/null ||
    fail 'cksum partial raw snapshot evidence was not retained'
[ ! -e "$cksum_snapshot" ] ||
    fail 'cksum partial output was promoted to a normalized snapshot'

grep_fixture=$tmp_dir/grep.txt
printf '%s\n' match other match >"$grep_fixture"
match_count=$(validation_grep_count -F -c -- match "$grep_fixture")
[ "$match_count" -eq 2 ] || fail "grep status 0 count drift: $match_count"
zero_count=$(validation_grep_count -F -c -- absent "$grep_fixture")
[ "$zero_count" -eq 0 ] || fail "grep status 1 did not become zero: $zero_count"
grep_failure_diagnostic=$tmp_dir/grep-failure.stderr
if missing_count=$(validation_grep_count -F -c -- match \
    "$tmp_dir/missing-grep-input" 2>"$grep_failure_diagnostic"); then
    fail "grep infrastructure failure became count: $missing_count"
else
    grep_failure_status=$?
fi
[ "$grep_failure_status" -ge 2 ] ||
    fail "grep infrastructure status was flattened: $grep_failure_status"
grep -F 'grep count infrastructure failure (status 2)' \
    "$grep_failure_diagnostic" >/dev/null ||
    fail 'grep infrastructure diagnostic lost the exact status'

assert_rejected_actual_status() {
    case_name=$1
    expected_actual=$2
    shift 2
    stdout_file=$tmp_dir/$case_name.stdout
    stderr_file=$tmp_dir/$case_name.stderr
    assertion_diagnostic=$tmp_dir/$case_name.assertion.stderr
    if validation_expect_status "$case_name" 1 \
        "$stdout_file" "$stderr_file" "$@" \
        2>"$assertion_diagnostic"; then
        fail "$case_name was accepted as canonical business status 1"
    fi
    [ "$VALIDATION_COMMAND_STATUS" -eq "$expected_actual" ] ||
        fail "$case_name actual status drift: $VALIDATION_COMMAND_STATUS"
    grep -F "expected=1 actual=$expected_actual" \
        "$assertion_diagnostic" >/dev/null ||
        fail "$case_name assertion diagnostic lost the exact status"
}

validation_expect_status canonical-business-failure 1 \
    "$tmp_dir/business.stdout" "$tmp_dir/business.stderr" \
    /bin/sh -c 'exit 1' || fail 'canonical business status 1 was rejected'
assert_rejected_actual_status unrelated-usage 2 /bin/sh -c 'exit 2'

non_executable=$tmp_dir/non-executable
printf '%s\n' '#!/bin/sh' 'exit 1' >"$non_executable"
chmod 0644 "$non_executable"
assert_rejected_actual_status exec-126 126 "$non_executable"
assert_rejected_actual_status command-not-found-127 127 \
    "$tmp_dir/command-does-not-exist"
assert_rejected_actual_status timeout-124 124 \
    timeout 1 /bin/sh -c 'sleep 2'
assert_rejected_actual_status signal-term-143 143 \
    /bin/sh -c 'kill -TERM $$'

nm_checker=$repo_root/scripts/check-nm-symbol-firewall.sh
failing_nm=$tmp_dir/failing-nm
printf '%s\n' \
    '#!/bin/sh' \
    "printf '%s\\n' '                 U harmless_partial_symbol'" \
    'exit 42' >"$failing_nm"
chmod 0755 "$failing_nm"
: >"$tmp_dir/dummy.o"
if NM=$failing_nm "$nm_checker" "$tmp_dir/dummy.o" \
    'forbidden_symbol' 'fault-injected firewall' \
    >"$tmp_dir/nm.stdout" 2>"$tmp_dir/nm.stderr"; then
    fail 'nm producer failure was masked by the symbol filter'
else
    nm_status=$?
fi
[ "$nm_status" -eq 42 ] || fail "nm producer status changed: $nm_status"
grep -F 'partial nm output (not accepted as firewall evidence)' \
    "$tmp_dir/nm.stderr" >/dev/null ||
    fail 'nm partial-output rejection diagnostic is absent'

failing_diagnostic_tools=$tmp_dir/failing-diagnostic-tools
mkdir "$failing_diagnostic_tools"
printf '%s\n' '#!/bin/sh' 'exit 73' \
    >"$failing_diagnostic_tools/sed"
chmod 0755 "$failing_diagnostic_tools/sed"
if PATH=$failing_diagnostic_tools:/usr/bin:/bin NM=$failing_nm \
    "$nm_checker" "$tmp_dir/dummy.o" \
    'forbidden_symbol' 'fault-injected diagnostic' \
    >"$tmp_dir/nm-diagnostic.stdout" \
    2>"$tmp_dir/nm-diagnostic.stderr"; then
    fail 'nm producer failure became success after diagnostic failure'
else
    nm_diagnostic_status=$?
fi
[ "$nm_diagnostic_status" -eq 42 ] ||
    fail "nm diagnostic failure replaced producer status: $nm_diagnostic_status"

archive_tool_root=$tmp_dir/archive-tools
mkdir -p "$archive_tool_root"
cp "$repo_root/scripts/validation-status.sh" \
    "$archive_tool_root/validation-status.sh"
cp "$repo_root/containers/arch-live-validation/local-archive-validator.sh" \
    "$archive_tool_root/local-archive-validator.sh"
chmod 0555 "$archive_tool_root/local-archive-validator.sh"
archive_validator=$archive_tool_root/local-archive-validator.sh
archive_authority=$archive_tool_root/archive-authority
mkdir -p "$archive_authority"
cp "$repo_root/tests/fixtures/local-archive-validator/contract.env" \
    "$repo_root/tests/fixtures/local-archive-validator/payload-authority.tsv" \
    "$archive_authority/"
chmod 0444 \
    "$archive_authority/contract.env" \
    "$archive_authority/payload-authority.tsv"
# shellcheck source=fixtures/local-archive-validator/contract.env
. "$archive_authority/contract.env"

archive_root=$tmp_dir/archive-root
mkdir -p "$archive_root"
payload_directories=$tmp_dir/archive-payload-directories.txt
if awk -F '\t' '$2 == "directory" { print $1 }' \
    "$archive_authority/payload-authority.tsv" >"$payload_directories"; then
    :
else
    authority_status=$?
    fail "archive payload directory projection failed with status $authority_status"
fi
while IFS= read -r payload_directory; do
    [ -n "$payload_directory" ] || continue
    mkdir -p "$archive_root/$payload_directory"
done <"$payload_directories"

dynamic_payload_path=$(awk -F '\t' '
    $2 == "regular" && $4 == "-" { print $1; count++ }
    END { if (count != 1) exit 1 }
' "$archive_authority/payload-authority.tsv") ||
    fail 'archive authority must identify one dynamic regular payload'
static_payload_record=$(awk -F '\t' '
    $2 == "regular" && $4 != "-" { print $1 "\t" $4; count++ }
    END { if (count != 1) exit 1 }
' "$archive_authority/payload-authority.tsv") ||
    fail 'archive authority must identify one static regular payload'
tab=$(printf '\tX')
tab=${tab%X}
if IFS=$tab read -r static_payload_path static_payload_sha256 <<EOF
$static_payload_record
EOF
then
    :
else
    authority_status=$?
    fail "archive static payload parsing failed with status $authority_status"
fi

printf '%s\n' \
    "pkgname = $PACKAGE_NAME" \
    "pkgbase = $PACKAGE_BASE" \
    "pkgver = $PACKAGE_VERSION" \
    "arch = $PACKAGE_ARCHITECTURE" >"$archive_root/.PKGINFO"
printf '%s\n' 'build info' >"$archive_root/.BUILDINFO"
printf '%s\n' 'mtree' >"$archive_root/.MTREE"
dd if=/dev/zero of="$archive_root/$dynamic_payload_path" \
    bs=1024 count=256 status=none
printf '%s\n' 'validation-status archive fixture marker' \
    >"$archive_root/$static_payload_path"
static_payload_checksum=$(sha256sum -- "$archive_root/$static_payload_path") ||
    fail 'archive static payload checksum producer failed'
[ "${static_payload_checksum%% *}" = "$static_payload_sha256" ] ||
    fail 'archive static payload bytes differ from their independent authority'

valid_archive=$tmp_dir/valid.pkg.tar
bsdtar -cf "$valid_archive" -C "$archive_root" \
    .BUILDINFO .MTREE .PKGINFO usr
valid_evidence=$tmp_dir/valid-evidence
mkdir "$valid_evidence"
validation_expect_status valid-archive 0 \
    "$tmp_dir/valid.stdout" "$tmp_dir/valid.stderr" \
    "$archive_validator" "$valid_archive" "$valid_evidence" \
    "$archive_authority" ||
    fail 'valid archive did not pass the local gateway inspection consumer'
[ -s "$valid_evidence/archive-members.raw" ] &&
    [ -s "$valid_evidence/archive-members.txt" ] ||
    fail 'valid archive did not retain raw and normalized member evidence'

assert_archive_rejected() {
    case_name=$1
    archive_path=$2
    evidence_path=$tmp_dir/$case_name-evidence
    stdout_path=$tmp_dir/$case_name.stdout
    stderr_path=$tmp_dir/$case_name.stderr
    mkdir "$evidence_path"
    validation_expect_status "$case_name" 97 \
        "$stdout_path" "$stderr_path" \
        "$archive_validator" "$archive_path" "$evidence_path" \
        "$archive_authority" ||
        fail "$case_name did not return canonical gateway reject status 97"
    grep -F 'moguet-live-local-gateway: rejected:' "$stderr_path" >/dev/null ||
        fail "$case_name lost the canonical gateway rejection diagnostic"
    [ ! -e "$evidence_path/accepted.argv" ] &&
        [ ! -e "$evidence_path/real-pacman-exec.txt" ] ||
        fail "$case_name retained real pacman execution evidence"
}

malformed_archive=$tmp_dir/malformed.pkg.tar
printf '%s\n' 'not an archive' >"$malformed_archive"
assert_archive_rejected malformed-archive "$malformed_archive"

truncated_archive=$tmp_dir/truncated.pkg.tar
cp "$valid_archive" "$truncated_archive"
truncate -s 32768 "$truncated_archive"
assert_archive_rejected truncated-archive "$truncated_archive"
truncated_evidence=$tmp_dir/truncated-archive-evidence
[ -s "$truncated_evidence/PKGINFO.raw" ] ||
    fail 'truncated archive did not retain partial raw PKGINFO output'
[ ! -e "$truncated_evidence/PKGINFO" ] ||
    fail 'truncated archive partial PKGINFO became validated evidence'
[ ! -e "$truncated_evidence/archive-members.txt" ] ||
    fail 'truncated archive partial members became normalized evidence'

printf '%s\n' 'validation status tests: all checks passed'
