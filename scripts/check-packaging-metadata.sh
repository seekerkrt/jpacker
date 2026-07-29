#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
srcinfo_file=$(mktemp)
stage_root=$(mktemp -d)
package_work=$(mktemp -d)

cleanup() {
    rm -f "$srcinfo_file"
    rm -rf "$stage_root"
    rm -rf "$package_work"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'packaging-metadata-check: %s\n' "$*" >&2
    exit 1
}

(
    cd "$repo_root"
    makepkg --printsrcinfo
) > "$srcinfo_file"

backup_count=$(awk '$1 == "backup" && $2 == "=" { count++ } END { print count + 0 }' "$srcinfo_file")
expected_backup_count=$(awk '
    $1 == "backup" && $2 == "=" && NF == 3 &&
        $3 == "etc/jpacker/jpacker.conf" { count++ }
    END { print count + 0 }
' "$srcinfo_file")

[ "$backup_count" -eq 1 ] ||
    fail "expected exactly one backup entry; found $backup_count."
[ "$expected_backup_count" -eq 1 ] ||
    fail "backup entry must be exactly etc/jpacker/jpacker.conf."

source_config=$repo_root/config/jpacker.conf
[ -f "$source_config" ] && [ ! -L "$source_config" ] ||
    fail "config/jpacker.conf is missing or is not a regular source file."

ln -s "$repo_root" "$package_work/jpacker-src"
ln -s "$repo_root/VERSION" "$package_work/VERSION"
bash -c '
    set -eu
    package_work=$1
    package_stage=$2
    package_file=$3
    cd "$package_work"
    pkgdir=$package_stage
    source "$package_file"
    package
' bash "$package_work" "$stage_root" "$repo_root/PKGBUILD" >/dev/null

payload_config=$stage_root/etc/jpacker/jpacker.conf
[ -f "$payload_config" ] && [ ! -L "$payload_config" ] ||
    fail "package payload is missing etc/jpacker/jpacker.conf."
cmp -s "$source_config" "$payload_config" ||
    fail "package payload config differs from config/jpacker.conf."

payload_matches=$(find "$stage_root" \( -type f -o -type l \) \
    -name jpacker.conf -print)
[ "$payload_matches" = "$payload_config" ] ||
    fail "package payload contains a duplicate or misplaced jpacker.conf."

printf 'packaging-metadata-check: all checks passed\n'
