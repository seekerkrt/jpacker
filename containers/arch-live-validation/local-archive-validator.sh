#!/bin/sh

set -eu

PATH=/usr/bin
export PATH
LC_ALL=C
export LC_ALL

reject_status=97
fixture_name=moguet-live-fixture
fixture_version=1.0.0-1
fixture_arch=any
script_dir=$(CDPATH='' cd "$(dirname "$0")" && pwd)
status_library=$script_dir/validation-status.sh

reject() {
    printf 'moguet-live-local-gateway: rejected: %s\n' "$*" >&2
    exit "$reject_status"
}

[ -f "$status_library" ] && [ ! -L "$status_library" ] ||
    reject 'archive status library is not a regular non-symlink'
# shellcheck source=../../scripts/validation-status.sh
. "$status_library"

[ "$#" -eq 2 ] || reject 'archive validator requires artifact and evidence directory'
staged_artifact=$1
evidence_directory=$2
[ -f "$staged_artifact" ] && [ ! -L "$staged_artifact" ] ||
    reject 'artifact path is not a regular non-symlink'
[ -d "$evidence_directory" ] && [ ! -L "$evidence_directory" ] ||
    reject 'archive evidence path is not a directory non-symlink'

pkginfo_raw=$evidence_directory/PKGINFO.raw
pkginfo_file=$evidence_directory/PKGINFO
member_list_raw=$evidence_directory/archive-members.raw
member_list=$evidence_directory/archive-members.txt
expected_members_raw=$evidence_directory/expected-members.raw
expected_members=$evidence_directory/expected-members.txt
marker_raw=$evidence_directory/marker.raw
marker_file=$evidence_directory/marker.txt
expected_marker=$evidence_directory/expected-marker.txt

for evidence_path in \
    "$pkginfo_raw" "$pkginfo_file" \
    "$member_list_raw" "$member_list" \
    "$expected_members_raw" "$expected_members" \
    "$marker_raw" "$marker_file" "$expected_marker"
do
    [ ! -e "$evidence_path" ] && [ ! -L "$evidence_path" ] ||
        reject "archive evidence already exists: $evidence_path"
done

if validation_capture_output "$pkginfo_raw" \
    /usr/bin/bsdtar -xOf "$staged_artifact" .PKGINFO; then
    :
else
    producer_status=$?
    reject "artifact PKGINFO producer failed with status $producer_status"
fi
if pkginfo=$(/usr/bin/cat "$pkginfo_raw"); then
    :
else
    producer_status=$?
    reject "artifact PKGINFO normalization failed with status $producer_status"
fi

require_pkginfo_line() {
    expected_line=$1
    drift_label=$2
    if /usr/bin/grep -Fx -- "$expected_line" "$pkginfo_raw" >/dev/null; then
        return 0
    else
        grep_status=$?
    fi
    case $grep_status in
        1) reject "$drift_label" ;;
        *) reject "PKGINFO inspection failed with status $grep_status" ;;
    esac
}

require_pkginfo_line "pkgname = $fixture_name" 'artifact package name drift'
require_pkginfo_line "pkgbase = $fixture_name" 'artifact PackageBase drift'
require_pkginfo_line "pkgver = $fixture_version" 'artifact version drift'
require_pkginfo_line "arch = $fixture_arch" 'artifact architecture drift'
if /usr/bin/grep -E \
    '^install = |^conflict = |^replaces = |^provides = ' \
    "$pkginfo_raw" >/dev/null; then
    reject 'artifact gained a transaction-affecting PKGINFO field'
else
    grep_status=$?
    case $grep_status in
        1) ;;
        *) reject "PKGINFO forbidden-field inspection failed with status $grep_status" ;;
    esac
fi

if validation_capture_sorted_output "$member_list_raw" "$member_list" \
    /usr/bin/bsdtar -tf "$staged_artifact"; then
    :
else
    producer_status=$?
    reject "artifact member listing failed with status $producer_status"
fi

if validation_capture_sorted_output \
    "$expected_members_raw" "$expected_members" \
    printf '%s\n' \
        .BUILDINFO \
        .MTREE \
        .PKGINFO \
        usr/ \
        usr/bin/ \
        usr/bin/moguet-live-fixture \
        usr/share/ \
        usr/share/moguet-live-validation/ \
        usr/share/moguet-live-validation/live-fixture-marker; then
    :
else
    producer_status=$?
    reject "expected member normalization failed with status $producer_status"
fi

if /usr/bin/cmp -s "$expected_members" "$member_list"; then
    :
else
    compare_status=$?
    case $compare_status in
        1) reject 'artifact payload path set drift' ;;
        *) reject "artifact payload comparison failed with status $compare_status" ;;
    esac
fi

if validation_capture_output "$marker_raw" /usr/bin/bsdtar -xOf \
    "$staged_artifact" \
    usr/share/moguet-live-validation/live-fixture-marker; then
    :
else
    producer_status=$?
    reject "artifact marker producer failed with status $producer_status"
fi
if marker=$(/usr/bin/cat "$marker_raw"); then
    :
else
    producer_status=$?
    reject "artifact marker normalization failed with status $producer_status"
fi
printf '%s\n' 'live-validation-local-package-fixture marker' >"$expected_marker" ||
    reject 'expected marker creation failed'
[ "$marker" = 'live-validation-local-package-fixture marker' ] ||
    reject 'artifact marker payload drift'

printf '%s\n' "$pkginfo" >"$pkginfo_file" ||
    reject 'validated PKGINFO evidence creation failed'
printf '%s\n' "$marker" >"$marker_file" ||
    reject 'validated marker evidence creation failed'
