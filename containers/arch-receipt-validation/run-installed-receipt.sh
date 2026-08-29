#!/bin/sh

set -eu

helper=/usr/libexec/moguet/moguet-alpm-receipt-helper
syncdeps_adapter=/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter
build_tree_syncdeps_adapter=/home/moguet-validation/work/moguet-receipt/build/cmake-production/moguet-makepkg-syncdeps-adapter
owner=selected-repository-provider
package_root=/home/moguet-validation/receipt-packages
repository_root=/var/lib/moguet-receipt-repository
pacman_config=/etc/moguet-receipt-pacman.conf

fail() {
    printf 'trusted-alpm-receipt-installed-validation: %s\n' "$*" >&2
    exit 1
}

assert_contains_line() {
    expected_line=$1
    inspected_file=$2
    grep -Fqx -- "$expected_line" "$inspected_file" ||
        fail "$inspected_file does not contain exact line: $expected_line"
}

expect_helper_rejection() {
    rejection_label=$1
    shift
    if "$helper" "$@" >/dev/null 2>&1; then
        fail "$rejection_label unexpectedly succeeded"
    fi
}

expect_syncdeps_rejection() {
    rejection_label=$1
    shift
    if "$syncdeps_adapter" "$@" >/dev/null 2>&1; then
        fail "$rejection_label unexpectedly succeeded"
    fi
}

prepare_transaction() {
    transaction_token=$1
    shift
    prepare_output=$repository_root/prepare-$transaction_token.out
    "$helper" prepare "$transaction_token" "$owner" -- "$@" \
        > "$prepare_output"
    assert_contains_line 'MOGUET-ALPM-PREPARE-RESPONSE	1' "$prepare_output"
    assert_contains_line "TOKEN	$transaction_token" "$prepare_output"
    assert_contains_line "OWNER	$owner" "$prepare_output"
    assert_contains_line 'END' "$prepare_output"
    hook_directory=$(sed -n 's/^HOOKDIR	//p' "$prepare_output")
    [ "$hook_directory" = \
        "/run/moguet/alpm-receipts/active/$transaction_token/hooks" ] ||
        fail 'prepare response returned an unexpected hook directory'
    printf '%s\n' "$hook_directory"
}

consume_transaction() {
    transaction_token=$1
    receipt_output=$2
    "$helper" consume "$transaction_token" "$owner" > "$receipt_output"
    assert_contains_line 'MOGUET-ALPM-RECEIPT	1' "$receipt_output"
    assert_contains_line "TOKEN	$transaction_token" "$receipt_output"
    assert_contains_line "OWNER	$owner" "$receipt_output"
    assert_contains_line 'END' "$receipt_output"
}

[ "$(id -u)" -eq 0 ] || fail 'installed validation must run as container root'
[ "$(stat -c '%U:%G:%a:%F' "$helper")" = \
    'root:root:755:regular file' ] ||
    fail 'installed helper provenance/mode changed'
[ "$(stat -c '%U:%G:%a:%F' "$(dirname "$helper")")" = \
    'root:root:755:directory' ] ||
    fail 'installed helper directory provenance/mode changed'
[ "$(stat -c '%U:%G:%a:%F' "$syncdeps_adapter")" = \
    'root:root:755:regular file' ] ||
    fail 'installed makepkg syncdeps adapter provenance/mode changed'
/usr/bin/runuser -u moguet-validation -- test ! -w "$syncdeps_adapter" ||
    fail 'validation user can modify installed makepkg syncdeps adapter'
[ "$(stat -c '%U:%G:%a:%F' "$(dirname "$syncdeps_adapter")")" = \
    'root:root:755:directory' ] ||
    fail 'installed makepkg syncdeps adapter directory provenance/mode changed'

# The installed CLI itself has no path/executable capability and rejects the
# protocol-negative matrix before creating root runtime state.
valid_negative_token=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
uppercase_token=$(printf '%064d' 0 | tr 0 A)
short_token=$(printf '%063d' 0 | tr 0 d)
long_token=$(printf '%065d' 0 | tr 0 d)
expect_helper_rejection 'path traversal token' \
    prepare '../x' "$owner" -- package
expect_helper_rejection 'newline token' \
    prepare "$(printf 'dddd\ndddd')" "$owner" -- package
expect_helper_rejection 'uppercase token' \
    prepare "$uppercase_token" "$owner" -- package
expect_helper_rejection 'short token' \
    prepare "$short_token" "$owner" -- package
expect_helper_rejection 'long token' \
    prepare "$long_token" "$owner" -- package
expect_helper_rejection 'invalid owner' \
    prepare "$valid_negative_token" source-artifact-install -- package
expect_helper_rejection 'package traversal' \
    prepare "$valid_negative_token" "$owner" -- '../x'
expect_helper_rejection 'package control character' \
    prepare "$valid_negative_token" "$owner" -- "$(printf 'bad\001name')"
expect_helper_rejection 'arbitrary destination argument' \
    consume "$valid_negative_token" "$owner" /tmp/receipt
expect_helper_rejection 'arbitrary executable argument' \
    record "$valid_negative_token" "$owner" /bin/sh
[ ! -e /run/moguet ] || fail 'invalid helper argv created runtime state'
expect_syncdeps_rejection 'makepkg path traversal token' \
    session-consume '../x' 1
expect_syncdeps_rejection 'makepkg uppercase token' \
    session-consume "$uppercase_token" 1
expect_syncdeps_rejection 'makepkg owner argument' \
    session-consume "$valid_negative_token" 1 selected-repository-provider
expect_syncdeps_rejection 'makepkg arbitrary executable argument' \
    session-bind "$valid_negative_token" 1 2 /bin/sh
[ ! -e /run/moguet/makepkg-syncdeps ] ||
    fail 'invalid makepkg adapter argv created root session state'

# Normal-user loader/tracer state is negative evidence only. It must stop
# before sudo and cannot create a root session or positive manifest.
user_loader=$package_root/user-owned-loader.so
install -o moguet-validation -g moguet-validation -m 0644 \
    /usr/lib/libm.so.6 "$user_loader"
if /usr/bin/runuser -u moguet-validation -- \
    env LD_PRELOAD="$user_loader" \
    "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'user-owned LD_PRELOAD reached root session authority'
fi
if /usr/bin/runuser -u moguet-validation -- \
    env LD_AUDIT="$user_loader" \
    "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'user-owned LD_AUDIT reached root session authority'
fi
if ! /usr/bin/runuser -u moguet-validation -- \
    python3 containers/arch-receipt-validation/syncdeps-security-probe.py \
        trace "$syncdeps_adapter" >/dev/null 2>&1; then
    fail 'pre-attached tracer was not rejected before root state'
fi
[ ! -e /run/moguet/makepkg-syncdeps ] ||
    fail 'loader/tracer negative created root session state'

install -d -o root -g root -m 0755 "$repository_root"
cp "$package_root"/moguet-receipt-dependency-1-1-any.pkg.tar.zst \
    "$package_root"/moguet-receipt-target-1-1-any.pkg.tar.zst \
    "$repository_root/"
repo-add "$repository_root/moguet-receipt.db.tar.zst" \
    "$repository_root"/*.pkg.tar.zst >/dev/null

cat > "$pacman_config" <<EOF
[options]
Architecture = auto
SigLevel = Never
LocalFileSigLevel = Never
CacheDir = /var/cache/pacman/pkg
LogFile = /var/log/moguet-receipt-pacman.log

[moguet-receipt]
Server = file://$repository_root
EOF
chmod 0644 "$pacman_config"
pacman --config "$pacman_config" -Syy --noconfirm >/dev/null

# A: requested target plus solver-introduced dependency are actual Install
# records. Requested input is not used as a receipt filter.
install_token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
install_hook_directory=$(prepare_transaction \
    "$install_token" moguet-receipt-target)
[ "$(stat -c '%U:%G:%a:%F' /run/moguet)" = \
    'root:root:700:directory' ] || fail '/run/moguet policy changed'
[ "$(stat -c '%U:%G:%a:%F' \
    "/run/moguet/alpm-receipts/active/$install_token")" = \
    'root:root:700:directory' ] || fail 'transaction directory policy changed'
install_hook_file=$(find "$install_hook_directory" -maxdepth 1 \
    -type f -name '*.hook' -print)
[ "$(printf '%s\n' "$install_hook_file" | wc -l)" -eq 1 ] ||
    fail 'transaction hook inventory is not exactly one regular hook'
[ "$(stat -c '%U:%G:%a:%F' "$install_hook_file")" = \
    'root:root:600:regular file' ] || fail 'transaction hook policy changed'
pacman --config "$pacman_config" -S --asdeps --needed --noconfirm \
    --hookdir "$install_hook_directory" -- \
    moguet-receipt/moguet-receipt-target >/dev/null
install_receipt=$repository_root/install-receipt.out
consume_transaction "$install_token" "$install_receipt"
assert_contains_line 'STATE	Complete' "$install_receipt"
assert_contains_line 'INSTALL	moguet-receipt-target' "$install_receipt"
assert_contains_line 'INSTALL	moguet-receipt-dependency' "$install_receipt"
[ "$(grep -c '^INSTALL	' "$install_receipt")" -eq 2 ] ||
    fail 'actual Install receipt has an unexpected package count'
pacman -Q moguet-receipt-target moguet-receipt-dependency >/dev/null
if "$helper" consume "$install_token" "$owner" >/dev/null 2>&1; then
    fail 'second consume unexpectedly succeeded'
fi

# B: an already installed candidate is upgraded by the exact instrumented
# transaction. The Install-only hook does not turn Upgrade into causal proof.
cp "$package_root"/moguet-receipt-dependency-2-1-any.pkg.tar.zst \
    "$repository_root/"
repo-add "$repository_root/moguet-receipt.db.tar.zst" \
    "$repository_root/moguet-receipt-dependency-2-1-any.pkg.tar.zst" \
    >/dev/null
pacman --config "$pacman_config" -Syy --noconfirm >/dev/null
upgrade_token=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
upgrade_hook_directory=$(prepare_transaction \
    "$upgrade_token" moguet-receipt-dependency)
pacman --config "$pacman_config" -S --asdeps --needed --noconfirm \
    --hookdir "$upgrade_hook_directory" -- \
    moguet-receipt/moguet-receipt-dependency >/dev/null
upgrade_receipt=$repository_root/upgrade-receipt.out
consume_transaction "$upgrade_token" "$upgrade_receipt"
assert_contains_line 'STATE	Missing' "$upgrade_receipt"
if grep -q '^INSTALL	' "$upgrade_receipt"; then
    fail 'Upgrade transaction produced an Install receipt'
fi
[ "$(pacman -Q moguet-receipt-dependency)" = \
    'moguet-receipt-dependency 2-1' ] || fail 'upgrade fixture did not execute'

# C/D: failed transaction has no PostTransaction Complete receipt. The exact
# token is aborted and becomes a one-shot tombstone.
failure_token=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
failure_hook_directory=$(prepare_transaction \
    "$failure_token" nonexistent-receipt-target)
if pacman --config "$pacman_config" -S --asdeps --needed --noconfirm \
    --hookdir "$failure_hook_directory" -- \
    moguet-receipt/nonexistent-receipt-target >/dev/null 2>&1; then
    fail 'missing-package transaction unexpectedly succeeded'
fi
[ ! -e "/run/moguet/alpm-receipts/active/$failure_token/receipt" ] ||
    fail 'failed transaction published a Complete receipt'
"$helper" abort "$failure_token" "$owner"
[ ! -e "/run/moguet/alpm-receipts/active/$failure_token" ] ||
    fail 'abort left active transaction state'
[ -d "/run/moguet/alpm-receipts/used/$failure_token" ] ||
    fail 'abort did not retain a replay tombstone'

# E: the disjoint makepkg owner state proves installed/root provenance and
# exact pidfd-bound synthetic 0/1/2 cardinality without invoking real pacman.
run_syncdeps_session() {
    transaction_count=$1
    output_file=$2
    expected_count=${3:-$transaction_count}
    /usr/bin/runuser -u moguet-validation -- \
        "$syncdeps_adapter" synthetic-session "$transaction_count" \
        > "$output_file"
    assert_contains_line 'MOGUET-MAKEPKG-SYNCDEPS-SESSION	1' "$output_file"
    assert_contains_line 'OWNER	makepkg-sync-dependencies' "$output_file"
    assert_contains_line 'PROCESS_BINDING	RootOwnedLauncherAndExactRoleChannels' "$output_file"
    assert_contains_line 'EVIDENCE	Synthetic' "$output_file"
    assert_contains_line "TRANSACTION_COUNT	$expected_count" "$output_file"
}

syncdeps_evidence_root=$repository_root/makepkg-syncdeps
install -d -o root -g root -m 0700 "$syncdeps_evidence_root"
zero_manifest=$syncdeps_evidence_root/zero.out
one_manifest=$syncdeps_evidence_root/one.out
two_manifest=$syncdeps_evidence_root/two.out
run_syncdeps_session 0 "$zero_manifest"
run_syncdeps_session 1 "$one_manifest"
run_syncdeps_session 2 "$two_manifest"
zero_session_token=$(sed -n 's/^SESSION	//p' "$zero_manifest" | head -n 1)
[ "$(stat -c '%U:%G:%a:%F' /run/moguet/makepkg-syncdeps)" = \
    'root:root:700:directory' ] ||
    fail 'makepkg syncdeps root state provenance/mode changed'
[ "$(stat -c '%U:%G:%a:%F' /run/moguet/makepkg-syncdeps/active)" = \
    'root:root:700:directory' ] ||
    fail 'makepkg syncdeps active state provenance/mode changed'
[ "$(stat -c '%U:%G:%a:%F' /run/moguet/makepkg-syncdeps/used)" = \
    'root:root:700:directory' ] ||
    fail 'makepkg syncdeps used state provenance/mode changed'
[ "$(stat -c '%U:%G:%a:%F' "/run/moguet/makepkg-syncdeps/used/$zero_session_token")" = \
    'root:root:700:directory' ] &&
    [ -z "$(find "/run/moguet/makepkg-syncdeps/used/$zero_session_token" \
        -mindepth 1 -maxdepth 1 -print -quit)" ] ||
    fail 'consumed session tombstone provenance or one-shot shape changed'
assert_contains_line 'TERMINAL	Complete' "$zero_manifest"
assert_contains_line 'COVERAGE	Complete' "$zero_manifest"
[ "$(awk '$0 == "TRANSACTION-BEGIN" { count++ } END { print count + 0 }' "$zero_manifest")" -eq 0 ] ||
    fail 'trusted terminal zero contains a transaction entry'
[ "$(grep -c '^TRANSACTION-BEGIN$' "$one_manifest")" -eq 1 ] ||
    fail 'one-transaction manifest has the wrong cardinality'
[ "$(grep -c '^TRANSACTION-BEGIN$' "$two_manifest")" -eq 2 ] ||
    fail 'two-transaction manifest has the wrong cardinality'
first_token=$(sed -n 's/^TRANSACTION_TOKEN	//p' "$two_manifest" | sed -n '1p')
second_token=$(sed -n 's/^TRANSACTION_TOKEN	//p' "$two_manifest" | sed -n '2p')
[ "${#first_token}" -eq 64 ] && [ "${#second_token}" -eq 64 ] &&
    [ "$first_token" != "$second_token" ] ||
    fail 'two transactions did not retain independent 256-bit tokens'
validation_uid=$(id -u moguet-validation)
assert_contains_line 'LAUNCHER_UID	0' "$two_manifest"
assert_contains_line 'SUPERVISOR_UID	0' "$two_manifest"
assert_contains_line "MAKEPKG_CHILD_UID	$validation_uid" "$two_manifest"
assert_contains_line "TRANSACTION_ADAPTER_UID	$validation_uid" "$two_manifest"

third_manifest=$syncdeps_evidence_root/third.out
run_syncdeps_session 3 "$third_manifest" 2
assert_contains_line 'TERMINAL	Unsupported' "$third_manifest"
assert_contains_line 'COVERAGE	Unsupported' "$third_manifest"
assert_contains_line 'TRANSACTION_COUNT	2' "$third_manifest"
[ "$(grep -c '^TRANSACTION-BEGIN$' "$third_manifest")" -eq 2 ] ||
    fail 'third transaction created a transaction slot before rejection'

# Root and the user-owned build-tree executable cannot become a normal-user
# installed launcher authority.
if "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'root invocation became an invoking-user session'
fi
if /usr/bin/runuser -u moguet-validation -- \
    "$build_tree_syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'build-tree makepkg syncdeps adapter became installed authority'
fi

# Root-owned private channels assign one exact retained pidfd to each role.
# The negative scenarios must retire without terminal or manifest bytes.
for security_scenario in \
    launcher-transaction \
    child-session \
    descendant-transaction \
    post-exec \
    peer-exit \
    intermediate-parent-exit \
    old-packet-new-peer \
    cross-session-replacement \
    bind-failure \
    barrier-failure \
    launcher-failure; do
    security_output=$syncdeps_evidence_root/security-$security_scenario.out
    security_evidence=$syncdeps_evidence_root/security-$security_scenario.evidence
    if ! /usr/bin/runuser -u moguet-validation -- \
        "$syncdeps_adapter" synthetic-security "$security_scenario" \
        > "$security_output" 2> "$security_evidence"; then
        fail "security role scenario failed: $security_scenario"
    fi
    [ ! -s "$security_output" ] ||
        fail "security role rejection returned manifest: $security_scenario"
    [ -z "$(find /run/moguet/makepkg-syncdeps/active \
        -mindepth 1 -maxdepth 1 -print -quit)" ] ||
        fail "security role rejection left active state: $security_scenario"

    case "$security_scenario" in
        peer-exit|old-packet-new-peer|cross-session-replacement)
            assert_contains_line \
                'MOGUET-MAKEPKG-SYNCDEPS-SECURITY-EVIDENCE	1' \
                "$security_evidence"
            assert_contains_line "SCENARIO	$security_scenario" \
                "$security_evidence"
            assert_contains_line 'EXPECTED_ROLE_PACKET_SUBMITTED	1' \
                "$security_evidence"
            assert_contains_line \
                'PACKET_CREDENTIAL_MATCHED_EXPECTED_ROLE	1' \
                "$security_evidence"
            assert_contains_line 'SENDER_LIFETIME_ENDED	1' \
                "$security_evidence"
            assert_contains_line 'RETAINED_PIDFD_DEAD_REJECTION	1' \
                "$security_evidence"
            assert_contains_line 'STATE_MUTATION_COUNT	0' \
                "$security_evidence"
            assert_contains_line 'POSITIVE_RESPONSE_COUNT	0' \
                "$security_evidence"
            assert_contains_line 'GUARD	RetainedPidfdDead' \
                "$security_evidence"
            assert_contains_line 'END' "$security_evidence"
            ;;
    esac

    case "$security_scenario" in
        old-packet-new-peer|cross-session-replacement)
            assert_contains_line 'PID_REPLACEMENT_OCCURRED	1' \
                "$security_evidence"
            assert_contains_line 'REPLACEMENT_UID_MATCHED	1' \
                "$security_evidence"
            assert_contains_line 'REPLACEMENT_EXECUTABLE_MATCHED	1' \
                "$security_evidence"
            expected_packet_pid=$(sed -n \
                's/^EXPECTED_PACKET_PID	//p' "$security_evidence")
            replacement_pid=$(sed -n \
                's/^REPLACEMENT_PID	//p' "$security_evidence")
            old_pidfd_inode=$(sed -n \
                's/^OLD_PIDFD_INODE	//p' "$security_evidence")
            replacement_pidfd_inode=$(sed -n \
                's/^REPLACEMENT_PIDFD_INODE	//p' "$security_evidence")
            [ -n "$expected_packet_pid" ] && \
                [ "$expected_packet_pid" = "$replacement_pid" ] ||
                fail "$security_scenario did not reuse the old numeric PID"
            [ -n "$old_pidfd_inode" ] && \
                [ -n "$replacement_pidfd_inode" ] && \
                [ "$old_pidfd_inode" != "$replacement_pidfd_inode" ] ||
                fail "$security_scenario did not prove a new pidfd lifetime"
            ;;
    esac

    if [ "$security_scenario" = cross-session-replacement ]; then
        assert_contains_line 'DISTINCT_LIVE_SESSIONS	1' "$security_evidence"
        assert_contains_line 'DISTINCT_PRIVATE_CHANNELS	1' "$security_evidence"
        assert_contains_line 'SESSION_B_EXPECTED_ROLE_ALIVE	1' \
            "$security_evidence"
        assert_contains_line 'DISTINCT_EXPECTED_ROLE_PIDS	1' \
            "$security_evidence"
        assert_contains_line 'B_ROLE_TO_A_CHANNEL	Rejected' \
            "$security_evidence"
        assert_contains_line 'A_ROLE_WITH_B_TOKEN	Rejected' \
            "$security_evidence"
        assert_contains_line 'B_ROLE_WITH_A_TOKEN	Rejected' \
            "$security_evidence"
        assert_contains_line 'REPLACEMENT_WITH_STALE_A_REQUEST	Rejected' \
            "$security_evidence"
        session_a=$(sed -n 's/^SESSION_A	//p' "$security_evidence")
        session_b=$(sed -n 's/^SESSION_B	//p' "$security_evidence")
        role_a_pid=$(sed -n \
            's/^SESSION_A_EXPECTED_ROLE_PID	//p' "$security_evidence")
        role_b_pid=$(sed -n \
            's/^SESSION_B_EXPECTED_ROLE_PID	//p' "$security_evidence")
        [ -n "$session_a" ] && [ -n "$session_b" ] && \
            [ "$session_a" != "$session_b" ] ||
            fail 'cross-session fixture did not prove distinct live sessions'
        [ -n "$role_a_pid" ] && [ -n "$role_b_pid" ] && \
            [ "$role_a_pid" != "$role_b_pid" ] ||
            fail 'cross-session fixture did not prove distinct expected roles'
    fi
done

# Two live sessions retain disjoint supervisors, pidfds, tokens, state, and
# one-shot tombstones.
concurrent_one=$syncdeps_evidence_root/concurrent-one.out
concurrent_two=$syncdeps_evidence_root/concurrent-two.out
/usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 1 > "$concurrent_one" &
concurrent_one_pid=$!
/usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 2 > "$concurrent_two" &
concurrent_two_pid=$!
wait "$concurrent_one_pid"
wait "$concurrent_two_pid"
assert_contains_line 'TRANSACTION_COUNT	1' "$concurrent_one"
assert_contains_line 'TRANSACTION_COUNT	2' "$concurrent_two"
concurrent_one_session=$(sed -n 's/^SESSION	//p' "$concurrent_one" | head -n 1)
concurrent_two_session=$(sed -n 's/^SESSION	//p' "$concurrent_two" | head -n 1)
[ "$concurrent_one_session" != "$concurrent_two_session" ] ||
    fail 'concurrent sessions reused one session token'
concurrent_one_transaction=$(sed -n 's/^TRANSACTION_TOKEN	//p' \
    "$concurrent_one" | head -n 1)
if grep -Fqx -- "TRANSACTION_TOKEN	$concurrent_one_transaction" \
    "$concurrent_two"; then
    fail 'concurrent sessions cross-contaminated a transaction token'
fi
[ ! -e /run/moguet/makepkg-syncdeps/active/"$concurrent_one_session" ] &&
    [ ! -e /run/moguet/makepkg-syncdeps/active/"$concurrent_two_session" ] &&
    [ -d /run/moguet/makepkg-syncdeps/used/"$concurrent_one_session" ] &&
    [ -d /run/moguet/makepkg-syncdeps/used/"$concurrent_two_session" ] ||
    fail 'concurrent sessions did not retire independently'

# Kill only the root supervisor while the bound child is held. The root-owned
# launcher must terminate/reap both nonroot roles without a positive manifest.
# A nonroot abstract listener may reuse the public token, but no production
# broker connects because authority is the inherited private role channel.
stale_output=$syncdeps_evidence_root/stale.out
/usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 0 hold > "$stale_output" 2>/dev/null &
stale_runuser_pid=$!
stale_session_directory=
attempt=0
while [ "$attempt" -lt 100 ]; do
    stale_session_directory=$(find /run/moguet/makepkg-syncdeps/active \
        -mindepth 1 -maxdepth 1 -type d -print -quit)
    [ -n "$stale_session_directory" ] && \
        [ -f "$stale_session_directory/session" ] && \
        [ -f "$stale_session_directory/binding" ] && break
    attempt=$((attempt + 1))
    sleep 0.05
done
[ -n "$stale_session_directory" ] || fail 'held stale session was not published'
stale_session_token=${stale_session_directory##*/}
stale_supervisor_pid=$(sed -n 's/^SUPERVISOR_PID	//p' \
    "$stale_session_directory/session")
stale_launcher_pid=$(sed -n 's/^LAUNCHER_PID	//p' \
    "$stale_session_directory/session")
stale_child_pid=$(sed -n 's/^CHILD_PID	//p' \
    "$stale_session_directory/binding")
stale_adapter_pid=$(sed -n 's/^TRANSACTION_ADAPTER_PID	//p' \
    "$stale_session_directory/binding")
[ -n "$stale_supervisor_pid" ] && [ -n "$stale_launcher_pid" ] && \
    [ -n "$stale_child_pid" ] && [ -n "$stale_adapter_pid" ] ||
    fail 'held session did not retain bound process identities'
kill -9 "$stale_supervisor_pid"
attempt=0
while kill -0 "$stale_runuser_pid" 2>/dev/null && [ "$attempt" -lt 200 ]; do
    attempt=$((attempt + 1))
    sleep 0.05
done
if kill -0 "$stale_runuser_pid" 2>/dev/null; then
    fail 'launcher did not exit after root supervisor death'
fi
wait "$stale_runuser_pid" 2>/dev/null || :
[ ! -s "$stale_output" ] ||
    fail 'supervisor death returned positive manifest bytes'
for retired_role_pid in \
    "$stale_launcher_pid" "$stale_child_pid" "$stale_adapter_pid"; do
    if kill -0 "$retired_role_pid" 2>/dev/null; then
        fail "supervisor death left role process alive: $retired_role_pid"
    fi
done
[ -d "/run/moguet/makepkg-syncdeps/active/$stale_session_token" ] ||
    fail 'killed supervisor state disappeared without bounded retirement'

fake_ready=/tmp/moguet-syncdeps-fake-ready-$$
fake_connected=/tmp/moguet-syncdeps-fake-connected-$$
rm -f "$fake_ready" "$fake_connected"
/usr/bin/runuser -u moguet-validation -- \
    python3 containers/arch-receipt-validation/syncdeps-security-probe.py \
        fake "$stale_session_token" "$fake_ready" "$fake_connected" &
fake_server_pid=$!
attempt=0
while [ ! -e "$fake_ready" ] && [ "$attempt" -lt 100 ]; do
    attempt=$((attempt + 1))
    sleep 0.02
done
[ -e "$fake_ready" ] || fail 'nonroot fake server did not bind'
if /usr/bin/runuser -u moguet-validation -- sudo -- \
    "$syncdeps_adapter" session-consume \
        "$stale_session_token" "$stale_launcher_pid" >/dev/null 2>&1; then
    fail 'stale fake server authorized session-consume'
fi
if /usr/bin/runuser -u moguet-validation -- sudo -- \
    "$syncdeps_adapter" transaction-prepare \
        "$stale_session_token" 1 -- fake >/dev/null 2>&1; then
    fail 'stale fake server authorized transaction verb'
fi
cross_token=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
if /usr/bin/runuser -u moguet-validation -- sudo -- \
    "$syncdeps_adapter" session-abort \
        "$cross_token" "$stale_launcher_pid" >/dev/null 2>&1; then
    fail 'fake server authorized cross-token session verb'
fi
wait "$fake_server_pid" || fail 'root broker connected to nonroot fake server'
[ ! -e "$fake_connected" ] ||
    fail 'nonroot fake server received a root broker connection'
rm -f "$fake_ready" "$fake_connected"

stale_gc_manifest=$syncdeps_evidence_root/stale-gc.out
run_syncdeps_session 0 "$stale_gc_manifest"
[ ! -e "/run/moguet/makepkg-syncdeps/active/$stale_session_token" ] &&
    [ -d "/run/moguet/makepkg-syncdeps/used/$stale_session_token" ] &&
    [ -z "$(find /run/moguet/makepkg-syncdeps/active -mindepth 1 -maxdepth 1 -print -quit)" ] ||
    fail 'stale session was reusable or retirement was incomplete'

# Unknown, symlink, owner, and mode drift in used state remain fail-closed and
# are not partially deleted by interrupted-retirement recovery.
malformed_token=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
mkdir "/run/moguet/makepkg-syncdeps/used/$malformed_token"
chmod 0700 "/run/moguet/makepkg-syncdeps/used/$malformed_token"
ln -s terminal "/run/moguet/makepkg-syncdeps/used/$malformed_token/session"
if /usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'used symlink drift was recovered'
fi
[ -L "/run/moguet/makepkg-syncdeps/used/$malformed_token/session" ] ||
    fail 'used symlink drift was partially deleted'
unlink "/run/moguet/makepkg-syncdeps/used/$malformed_token/session"
rmdir "/run/moguet/makepkg-syncdeps/used/$malformed_token"

mkdir "/run/moguet/makepkg-syncdeps/used/$malformed_token"
chmod 0755 "/run/moguet/makepkg-syncdeps/used/$malformed_token"
if /usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'used mode drift was recovered'
fi
[ "$(stat -c '%a' "/run/moguet/makepkg-syncdeps/used/$malformed_token")" = 755 ] ||
    fail 'used mode drift was mutated'
chmod 0700 "/run/moguet/makepkg-syncdeps/used/$malformed_token"
chown moguet-validation:moguet-validation \
    "/run/moguet/makepkg-syncdeps/used/$malformed_token"
if /usr/bin/runuser -u moguet-validation -- \
    "$syncdeps_adapter" synthetic-session 0 >/dev/null 2>&1; then
    fail 'used owner drift was recovered'
fi
[ "$(stat -c '%U' "/run/moguet/makepkg-syncdeps/used/$malformed_token")" = \
    moguet-validation ] || fail 'used owner drift was mutated'
chown root:root "/run/moguet/makepkg-syncdeps/used/$malformed_token"
rmdir "/run/moguet/makepkg-syncdeps/used/$malformed_token"

printf '%s\n' \
    'trusted-alpm-receipt-installed-validation: selected-provider and makepkg syncdeps installed checks passed'
