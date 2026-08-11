#!/bin/sh

set -eu

PATH=/usr/bin
export PATH
LC_ALL=C
export LC_ALL

reject_status=97
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

case $# in
    2)
        fixture_root=$script_dir/fixtures/local-package
        ;;
    3)
        fixture_root=$3
        ;;
    *)
        reject 'archive validator requires artifact, evidence directory, and optional authority directory'
        ;;
esac
fixture_contract=$fixture_root/contract.env
payload_authority=$fixture_root/payload-authority.tsv
if authority_owner=$(/usr/bin/id -u) &&
    authority_group=$(/usr/bin/id -g); then
    :
else
    producer_status=$?
    reject "authority owner inspection failed with status $producer_status"
fi
expected_authority_metadata=$authority_owner:$authority_group:444:regular\ file
[ -f "$fixture_contract" ] && [ ! -L "$fixture_contract" ] ||
    reject 'local fixture contract is not a regular non-symlink'
[ "$(/usr/bin/stat -c '%u:%g:%a:%F' -- "$fixture_contract")" = \
    "$expected_authority_metadata" ] || reject 'local fixture contract metadata drift'
[ -f "$payload_authority" ] && [ ! -L "$payload_authority" ] ||
    reject 'payload authority is not a regular non-symlink'
[ "$(/usr/bin/stat -c '%u:%g:%a:%F' -- "$payload_authority")" = \
    "$expected_authority_metadata" ] || reject 'payload authority metadata drift'
# shellcheck source=fixtures/local-package/contract.env
. "$fixture_contract"
fixture_name=$PACKAGE_NAME
fixture_version=$PACKAGE_VERSION
fixture_arch=$PACKAGE_ARCHITECTURE

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
require_pkginfo_line "pkgbase = $PACKAGE_BASE" 'artifact PackageBase drift'
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

expected_member_paths() {
    printf '%s\n' \
        .BUILDINFO \
        .MTREE \
        .PKGINFO || return $?
    /usr/bin/awk -F '\t' '
        NR == 1 {
            if ($0 != "# path\ttype\tmode\tsha256") exit 2
            next
        }
        NF != 4 || $1 == "" { exit 2 }
        $1 ~ /^\// || $1 ~ /(^|\/)\.\.(\/|$)/ || $1 ~ /\/\// { exit 2 }
        $2 != "directory" && $2 != "regular" { exit 2 }
        $3 !~ /^0[0-7][0-7][0-7]$/ { exit 2 }
        $4 != "-" && (length($4) != 64 || $4 !~ /^[0-9a-f]+$/) { exit 2 }
        { print $1; count++ }
        END { if (count == 0) exit 3 }
    ' "$payload_authority"
}

if validation_capture_sorted_output "$member_list_raw" "$member_list" \
    /usr/bin/bsdtar -tf "$staged_artifact"; then
    :
else
    producer_status=$?
    reject "artifact member listing failed with status $producer_status"
fi

if validation_capture_sorted_output \
    "$expected_members_raw" "$expected_members" \
    expected_member_paths; then
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

static_payload_record=$(/usr/bin/awk -F '\t' '
    NR == 1 {
        if ($0 != "# path\ttype\tmode\tsha256") exit 2
        next
    }
    NF != 4 || $1 == "" { exit 2 }
    $2 == "regular" && $4 != "-" {
        print $1 "\t" $4
        count++
    }
    END { if (count != 1) exit 3 }
' "$payload_authority") || reject 'payload authority lacks one static payload record'
tab=$(printf '\tX')
tab=${tab%X}
if IFS=$tab read -r static_payload_path static_payload_sha256 <<EOF
$static_payload_record
EOF
then
    :
else
    producer_status=$?
    reject "static payload authority parsing failed with status $producer_status"
fi

if validation_capture_output "$marker_raw" /usr/bin/bsdtar -xOf \
    "$staged_artifact" "$static_payload_path"; then
    :
else
    producer_status=$?
    reject "static payload producer failed with status $producer_status"
fi
if marker_checksum_output=$(/usr/bin/sha256sum -- "$marker_raw"); then
    marker_sha256=${marker_checksum_output%% *}
else
    producer_status=$?
    reject "static payload checksum failed with status $producer_status"
fi
[ "$marker_sha256" = "$static_payload_sha256" ] ||
    reject 'artifact static payload drift'
printf '%s\t%s\n' "$static_payload_path" "$static_payload_sha256" \
    >"$expected_marker" || reject 'expected static payload evidence creation failed'
printf '%s\t%s\n' "$static_payload_path" "$marker_sha256" \
    >"$marker_file" || reject 'validated static payload evidence creation failed'

printf '%s\n' "$pkginfo" >"$pkginfo_file" ||
    reject 'validated PKGINFO evidence creation failed'
