#!/bin/sh

set -eu

[ "$#" -eq 3 ] || {
    printf '%s\n' \
        'usage: check-nm-symbol-firewall.sh OBJECT FORBIDDEN_PATTERN LABEL' >&2
    exit 2
}

object_file=$1
forbidden_pattern=$2
firewall_label=$3
script_dir=$(CDPATH='' cd "$(dirname "$0")" && pwd)
. "$script_dir/validation-status.sh"

nm_command=${NM:-nm}
tmp_dir=$(mktemp -d)
nm_output=$tmp_dir/nm.raw
forbidden_output=$tmp_dir/forbidden.txt

cleanup() {
    rm -rf -- "$tmp_dir" >/dev/null 2>&1 || :
}
trap cleanup EXIT HUP INT TERM

if validation_capture_output "$nm_output" \
    "$nm_command" -C -u "$object_file"; then
    :
else
    nm_status=$?
    printf 'error: %s nm producer failed with status %s\n' \
        "$firewall_label" "$nm_status" >&2 || :
    if [ -s "$nm_output" ]; then
        printf '%s\n' \
            'partial nm output (not accepted as firewall evidence):' \
            >&2 || :
        sed -n '1,160p' "$nm_output" >&2 || :
    fi
    exit "$nm_status"
fi

if grep -E "$forbidden_pattern" "$nm_output" >"$forbidden_output"; then
    grep_status=0
else
    grep_status=$?
fi
case $grep_status in
    0) ;;
    1) : >"$forbidden_output" ;;
    *)
        printf 'error: %s symbol filter failed with status %s\n' \
            "$firewall_label" "$grep_status" >&2
        exit "$grep_status"
        ;;
esac

if [ -s "$forbidden_output" ]; then
    printf 'error: %s imports forbidden symbols\n' "$firewall_label" >&2
    sed -n '1,160p' "$forbidden_output" >&2
    exit 1
fi
