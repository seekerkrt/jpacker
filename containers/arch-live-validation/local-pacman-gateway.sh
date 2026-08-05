#!/bin/sh

set -eu

PATH=/usr/bin
export PATH
unset PYTHONPATH
unset PYTHONHOME
export LC_ALL=C

real_pacman=/usr/libexec/moguet-live-local/pacman.real
stage_helper=/usr/libexec/moguet-live-local/local-stage-artifact.py
staging_root=/var/lib/moguet-live-local/staging
evidence_root=/var/log/moguet-live-local
validation_user=moguet-validation
validation_uid=1000
validation_gid=1000
gateway_reject_status=97
fixture_name=moguet-live-fixture
fixture_version=1.0.0-1
fixture_arch=any
fixture_artifact=${fixture_name}-${fixture_version}-${fixture_arch}.pkg.tar.zst

reject() {
    printf 'moguet-live-local-gateway: rejected: %s\n' "$*" >&2
    exit "$gateway_reject_status"
}

exec_real_pacman() {
    exec /usr/bin/env -i PATH=/usr/bin LC_ALL=C "$real_pacman" "$@"
}

require_root_readonly_file() {
    checked_path=$1
    checked_label=$2
    [ -f "$checked_path" ] && [ ! -L "$checked_path" ] ||
        reject "$checked_label is not a regular non-symlink"
    metadata=$(/usr/bin/stat -c '%u:%g:%a:%F' -- "$checked_path")
    [ "$metadata" = '0:0:755:regular file' ] ||
        reject "$checked_label has unsafe metadata"
}

kernel_effective_uid=$(/usr/bin/awk '$1 == "Uid:" { print $3 }' "/proc/$$/status")
case "$kernel_effective_uid" in
    ''|*[!0-9]*) reject 'kernel effective UID is unavailable' ;;
esac
if [ "$kernel_effective_uid" -ne 0 ]; then
    exec_real_pacman "$@"
fi

case_identity=${MOGUET_LIVE_LOCAL_CASE-}
[ "$case_identity" = local-root-install ] ||
    reject 'missing or unknown MOGUET_LIVE_LOCAL_CASE'

require_root_readonly_file "$real_pacman" 'real pacman'
require_root_readonly_file "$stage_helper" 'staging helper'

# The provider is chosen by production Moguet from the current real sync DB;
# this gateway permits only the reviewed cargo providers and no other system
# transaction shape.
if [ "$#" -eq 5 ] && [ "$1" = -S ] && [ "$2" = --asdeps ] && \
   [ "$3" = --needed ] && [ "$4" = -- ]; then
    case "$5" in
        extra/rust|extra/rustup)
            # Moguet's terminal reader can prefetch scripted PTY input before
            # this child inherits the terminal.  The gateway auto-confirms
            # only this already-validated transaction shape.
            exec_real_pacman --noconfirm "$@"
            ;;
    esac
fi

if [ "$#" -ne 3 ] || [ "$1" != -U ] || [ "$2" != -- ]; then
    reject 'root argv must be one selected provider transaction or local artifact install'
fi

source_artifact=$3
case "$source_artifact" in
    /home/moguet-validation/live-local-case/actual/cache/moguet/.artifact-workspace~-*/*) ;;
    *) reject 'artifact path is outside the invocation-owned cache prefix' ;;
esac
[ "$(/usr/bin/basename -- "$source_artifact")" = "$fixture_artifact" ] ||
    reject 'artifact filename identity drift'
[ -f "$source_artifact" ] && [ ! -L "$source_artifact" ] ||
    reject 'artifact path is not a regular non-symlink'

evidence_directory=$evidence_root/$case_identity
staging_directory=$staging_root/$case_identity
/usr/bin/mkdir -m 0750 -- "$evidence_directory" ||
    reject 'case transaction already exists; refusing a second root install'
/usr/bin/chown root:"$validation_user" "$evidence_directory"
/usr/bin/chmod 0750 "$evidence_directory"
/usr/bin/mkdir -m 0750 -- "$staging_directory" ||
    reject 'case staging already exists; refusing a second root install'
/usr/bin/chown root:"$validation_user" "$staging_directory"
/usr/bin/chmod 0750 "$staging_directory"
staged_artifact=$staging_directory/$fixture_artifact

/usr/bin/python3 -I "$stage_helper" \
    "$source_artifact" "$staged_artifact" "$evidence_directory" \
    "$validation_uid:$validation_gid" ||
    reject 'safe artifact staging failed'

pkginfo=$(/usr/bin/bsdtar -xOf "$staged_artifact" .PKGINFO) ||
    reject 'artifact has no readable PKGINFO'
printf '%s\n' "$pkginfo" | /usr/bin/grep -Fx -- "pkgname = $fixture_name" >/dev/null ||
    reject 'artifact package name drift'
printf '%s\n' "$pkginfo" | /usr/bin/grep -Fx -- "pkgbase = $fixture_name" >/dev/null ||
    reject 'artifact PackageBase drift'
printf '%s\n' "$pkginfo" | /usr/bin/grep -Fx -- "pkgver = $fixture_version" >/dev/null ||
    reject 'artifact version drift'
printf '%s\n' "$pkginfo" | /usr/bin/grep -Fx -- "arch = $fixture_arch" >/dev/null ||
    reject 'artifact architecture drift'
if printf '%s\n' "$pkginfo" | /usr/bin/grep -E '^install = |^conflict = |^replaces = |^provides = ' >/dev/null; then
    reject 'artifact gained a transaction-affecting PKGINFO field'
fi

member_list=$evidence_directory/archive-members.txt
/usr/bin/bsdtar -tf "$staged_artifact" | LC_ALL=C /usr/bin/sort > "$member_list" ||
    reject 'artifact member listing failed'
expected_members=$evidence_directory/expected-members.txt
printf '%s\n' \
    .BUILDINFO \
    .MTREE \
    .PKGINFO \
    usr/ \
    usr/bin/ \
    usr/bin/moguet-live-fixture \
    usr/share/ \
    usr/share/moguet-live-validation/ \
    usr/share/moguet-live-validation/live-fixture-marker \
    | LC_ALL=C /usr/bin/sort > "$expected_members"
/usr/bin/cmp -s "$expected_members" "$member_list" ||
    reject 'artifact payload path set drift'
marker=$(/usr/bin/bsdtar -xOf "$staged_artifact" \
    usr/share/moguet-live-validation/live-fixture-marker) ||
    reject 'artifact marker payload is unreadable'
[ "$marker" = 'live-validation-local-package-fixture marker' ] ||
    reject 'artifact marker payload drift'

printf '%s\n' "$pkginfo" > "$evidence_directory/PKGINFO"
/usr/bin/chown root:"$validation_user" "$evidence_directory/PKGINFO" \
    "$member_list" "$expected_members"
/usr/bin/chmod 0640 "$evidence_directory/PKGINFO" \
    "$member_list" "$expected_members"
printf '%s\0' sudo pacman "$@" > "$evidence_directory/accepted.argv"
/usr/bin/chown root:"$validation_user" "$evidence_directory/accepted.argv"
/usr/bin/chmod 0640 "$evidence_directory/accepted.argv"

exec_real_pacman --noconfirm -U --asexplicit -- "$staged_artifact"
