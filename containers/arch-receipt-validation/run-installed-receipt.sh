#!/bin/sh

set -eu

helper=/usr/libexec/moguet/moguet-alpm-receipt-helper
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

printf '%s\n' \
    'trusted-alpm-receipt-installed-validation: Install/Upgrade/solver/failure checks passed'
