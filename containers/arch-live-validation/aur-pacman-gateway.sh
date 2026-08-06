#!/bin/sh

set -eu

PATH=/usr/bin
export PATH
unset PYTHONPATH
unset PYTHONHOME
export LC_ALL=C

real_pacman=/usr/libexec/moguet-live-aur/pacman.real
stage_helper=/usr/libexec/moguet-live-aur/aur-stage-artifact.py
metadata_helper=/usr/libexec/moguet-live-aur/aur-archive-metadata-check
policy_root=/usr/share/moguet-live-aur/policy
case_policy=$policy_root/aur-cases.tsv
payload_static_policy=$policy_root/fetchfetch-payload-authority.tsv
reference_manifest=/usr/libexec/moguet-live-aur/fixtures/fetchfetch-payload.tsv
pkginfo_manifest=/usr/libexec/moguet-live-aur/fixtures/fetchfetch-pkginfo.tsv
evidence_root=/var/log/moguet-live-aur
staging_root=/var/lib/moguet-live-aur/staging
validation_user=moguet-validation
validation_uid=1000
validation_gid=1000
gateway_reject_status=97

reject() {
    printf 'moguet-live-aur-gateway: rejected: %s\n' "$*" >&2
    exit "$gateway_reject_status"
}

exec_real_pacman() {
    exec /usr/bin/env -i PATH=/usr/bin LC_ALL=C \
        "$real_pacman" "$@"
}

kernel_effective_uid=$(/usr/bin/awk '$1 == "Uid:" { print $3 }' "/proc/$$/status")
case "$kernel_effective_uid" in
    ''|*[!0-9]*)
        reject 'kernel effective UID is unavailable'
        ;;
esac
if [ "$kernel_effective_uid" -ne 0 ]; then
    exec_real_pacman "$@"
fi

case_identity=${MOGUET_LIVE_AUR_CASE-}
case "$case_identity" in
    fetchfetch-install)
        negative_case=false
        ;;
    fetchfetch-content-drift-test|fetchfetch-conflict-policy-test|\
    fetchfetch-xattr-metadata-test|fetchfetch-acl-metadata-test|\
    fetchfetch-pkgdesc-authority-test)
        negative_case=true
        ;;
    *)
        reject 'missing or unknown MOGUET_LIVE_AUR_CASE'
        ;;
esac

require_runtime_authority() {
    authority_path=$1
    expected_metadata=$2
    authority_label=$3
    if [ ! -f "$authority_path" ] || [ -L "$authority_path" ]; then
        reject "runtime authority is not a regular non-symlink: $authority_label"
    fi
    actual_metadata=$(/usr/bin/stat -c '%u:%g:%a:%F' -- "$authority_path")
    if [ "$actual_metadata" != "$expected_metadata" ]; then
        reject "runtime authority metadata drift: $authority_label"
    fi
}

require_runtime_authority "$real_pacman" '0:0:755:regular file' 'real pacman'
require_runtime_authority "$stage_helper" '0:0:555:regular file' 'staging helper'
require_runtime_authority "$metadata_helper" \
    '0:0:555:regular file' 'archive metadata helper'
require_runtime_authority "$case_policy" '0:0:444:regular file' 'AUR case policy'
require_runtime_authority "$payload_static_policy" \
    '0:0:444:regular file' 'static payload authority'
require_runtime_authority "$reference_manifest" \
    '0:0:444:regular file' 'reference payload manifest'
require_runtime_authority "$pkginfo_manifest" \
    '0:0:444:regular file' 'reference PKGINFO manifest'

expected_header=$(printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
    '# package' package_base expected_version runtime_dependencies \
    make_dependencies source_kind install_reason fallback_policy \
    review_required expected_aur_git_head expected_pkgbuild_sha256 \
    expected_srcinfo_sha256 expected_source_filename expected_source_url \
    expected_source_sha256 expected_rpc_url_path \
    expected_artifact_architecture)
tab=$(printf '\tX')
tab=${tab%X}
policy_header=
package_name=
package_base=
expected_version=
runtime_dependencies=
make_dependencies=
source_kind=
install_reason=
fallback_policy=
review_required=
expected_aur_git_head=
expected_pkgbuild_sha256=
expected_srcinfo_sha256=
expected_source_filename=
expected_source_url=
expected_source_sha256=
expected_rpc_url_path=
expected_architecture=
extra_field=
{
    IFS= read -r policy_header || reject 'AUR case policy has no header'
    IFS=$tab read -r \
        package_name package_base expected_version runtime_dependencies \
        make_dependencies source_kind install_reason fallback_policy \
        review_required expected_aur_git_head expected_pkgbuild_sha256 \
        expected_srcinfo_sha256 expected_source_filename expected_source_url \
        expected_source_sha256 expected_rpc_url_path expected_architecture \
        extra_field || reject 'AUR case policy has no case row'
    if IFS= read -r _; then
        reject 'AUR case policy contains more than one case row'
    fi
} < "$case_policy"

[ "$policy_header" = "$expected_header" ] || reject 'AUR case policy header drift'
[ -z "$extra_field" ] || reject 'AUR case policy contains extra columns'
[ "$package_name" = fetchfetch ] || reject 'package identity drift'
[ "$package_base" = fetchfetch ] || reject 'PackageBase identity drift'
[ "$expected_version" = 2.0.0-1 ] || reject 'package version policy drift'
[ "$runtime_dependencies" = glibc ] || reject 'runtime dependency policy drift'
[ "$make_dependencies" = gcc,make ] || reject 'make dependency policy drift'
[ "$source_kind" = single-release-archive ] || reject 'source-kind policy drift'
[ "$install_reason" = Explicit ] || reject 'install-reason policy drift'
[ "$fallback_policy" = reject ] || reject 'fallback policy drift'
[ "$review_required" = required ] || reject 'review-required policy drift'
[ "$expected_aur_git_head" = \
    353dc8ea947734b85175158cda87e5d084585c3f ] ||
    reject 'pinned AUR HEAD policy drift'
[ "$expected_pkgbuild_sha256" = \
    6b9e09ae9c297c5be32b1bd6a038172ad1400cd0f4a5867fe2393b468d964232 ] ||
    reject 'pinned PKGBUILD policy drift'
[ "$expected_srcinfo_sha256" = \
    d685ae938e6cfe1d2001dba3772539564b2cd8e7b2d488e7e78d92762f411df1 ] ||
    reject 'pinned .SRCINFO policy drift'
[ "$expected_source_filename" = fetchfetch-2.0.0.tar.gz ] ||
    reject 'source filename policy drift'
[ "$expected_source_url" = \
    https://github.com/spenserblack/fetchfetch/archive/v2.0.0.tar.gz ] ||
    reject 'source URL policy drift'
[ "$expected_source_sha256" = \
    e67be2f63497b6f75017873e06935218a407e46023fe10f422fb259e92a7d7ae ] ||
    reject 'source checksum policy drift'
[ "$expected_rpc_url_path" = \
    /cgit/aur.git/snapshot/fetchfetch.tar.gz ] ||
    reject 'AUR RPC URLPath policy drift'
[ "$expected_architecture" = x86_64 ] || reject 'artifact architecture policy drift'

if [ "$#" -ne 4 ] ||
    [ "$1" != -U ] ||
    [ "$2" != --noconfirm ] ||
    [ "$3" != -- ]
then
    reject 'root argv must be exactly: -U --noconfirm -- <one artifact>'
fi

source_artifact=$4
case "$source_artifact" in
    /home/moguet-validation/.cache/moguet/.artifact-workspace~-*/*)
        ;;
    *)
        reject 'artifact path is outside the invocation-owned cache prefix'
        ;;
esac

case "$source_artifact" in
    /*)
        ;;
    *)
        reject 'artifact path must be absolute'
        ;;
esac
if [ ! -f "$source_artifact" ] || [ -L "$source_artifact" ]; then
    reject 'artifact path must be a regular non-symlink'
fi
canonical_artifact=$(/usr/bin/realpath -e -- "$source_artifact") ||
    reject 'artifact path has no canonical realpath'
[ "$canonical_artifact" = "$source_artifact" ] ||
    reject 'raw artifact path differs from its canonical realpath'
artifact_workspace=$(/usr/bin/dirname -- "$source_artifact")
[ "$(/usr/bin/dirname -- "$artifact_workspace")" = \
    /home/moguet-validation/.cache/moguet ] ||
    reject 'artifact workspace is not a direct cache child'
case "$(/usr/bin/basename -- "$artifact_workspace")" in
    .artifact-workspace~-??????)
        ;;
    *)
        reject 'artifact workspace identity is invalid'
        ;;
esac

expected_artifact_filename="${package_name}-${expected_version}-${expected_architecture}.pkg.tar.zst"
[ "$(/usr/bin/basename -- "$source_artifact")" = "$expected_artifact_filename" ] ||
    reject 'artifact filename identity drift'
[ "$(/usr/bin/stat -c '%u' -- "$source_artifact")" -eq "$validation_uid" ] ||
    reject 'source artifact is not validation-user-owned'
source_mode=$(/usr/bin/stat -c '%a' -- "$source_artifact")
[ $((0$source_mode & 022)) -eq 0 ] ||
    reject 'source artifact is group/other writable'
[ "$(/usr/bin/stat -c '%h' -- "$source_artifact")" -eq 1 ] ||
    reject 'source artifact has an unexpected hard-link count'

evidence_directory=$evidence_root/$case_identity
if ! /usr/bin/mkdir -m 0750 -- "$evidence_directory"; then
    reject 'case transaction already exists; refusing a second root transaction'
fi
/usr/bin/chown root:"$validation_user" "$evidence_directory"
/usr/bin/chmod 0750 "$evidence_directory"

write_evidence_line() {
    evidence_file=$1
    shift
    : > "$evidence_file"
    /usr/bin/chown root:"$validation_user" "$evidence_file"
    /usr/bin/chmod 0640 "$evidence_file"
    printf '%s\n' "$@" >> "$evidence_file"
}

argv_evidence=$evidence_directory/original-argv.nul
: > "$argv_evidence"
/usr/bin/chown root:"$validation_user" "$argv_evidence"
/usr/bin/chmod 0640 "$argv_evidence"
printf '%s\0' sudo pacman "$@" >> "$argv_evidence"
write_evidence_line \
    "$evidence_directory/original-artifact-path.txt" \
    "$canonical_artifact"

staging_directory=$staging_root/$case_identity
/usr/bin/mkdir -m 0750 -- "$staging_directory"
/usr/bin/chown root:"$validation_user" "$staging_directory"
/usr/bin/chmod 0750 "$staging_directory"
staged_artifact=$staging_directory/$expected_artifact_filename
write_evidence_line \
    "$evidence_directory/staged-artifact-path.txt" \
    "$staged_artifact"

stage_evidence=$evidence_directory/stage-hashes.txt
: > "$stage_evidence"
/usr/bin/chown root:"$validation_user" "$stage_evidence"
/usr/bin/chmod 0640 "$stage_evidence"
if ! /usr/bin/python3 -I "$stage_helper" stage \
    "$source_artifact" "$staged_artifact" \
    "$validation_uid" "$validation_gid" >> "$stage_evidence"
then
    reject 'root staging copy or TOCTOU validation failed'
fi

metadata_evidence=$evidence_directory/archive-metadata-check.txt
if ! "$metadata_helper" "$staged_artifact" > "$metadata_evidence" 2>&1; then
    /usr/bin/chown root:"$validation_user" "$metadata_evidence"
    /usr/bin/chmod 0640 "$metadata_evidence"
    /usr/bin/cat "$metadata_evidence" >&2
    reject 'staged artifact direct metadata validation failed'
fi
/usr/bin/chown root:"$validation_user" "$metadata_evidence"
/usr/bin/chmod 0640 "$metadata_evidence"

if ! /usr/bin/python3 -I "$stage_helper" validate \
    "$payload_static_policy" "$reference_manifest" "$pkginfo_manifest" \
    "$staged_artifact" \
    "$evidence_directory" "$validation_gid"
then
    reject 'staged artifact path, content, or PKGINFO validation failed'
fi

if [ "$negative_case" = true ]; then
    reject 'negative test case must never invoke real pacman'
fi

pacman_identity_format=$(printf '%%n\t%%v')
pacman_identity=$(/usr/bin/env -i PATH=/usr/bin LC_ALL=C \
    "$real_pacman" -U --print --print-format "$pacman_identity_format" -- \
    "$staged_artifact") || reject 'real pacman package identity query failed'
expected_pacman_identity=$(printf '%s\t%s' \
    "$package_name" "$expected_version")
[ "$pacman_identity" = "$expected_pacman_identity" ] ||
    reject 'real pacman package identity query drift'

write_evidence_line "$evidence_directory/package-identity.txt" \
    "package_name=$package_name" \
    "package_version=$expected_version" \
    "package_architecture=$expected_architecture" \
    "package_dependency=$runtime_dependencies" \
    "pacman_query=$pacman_identity"
write_evidence_line "$evidence_directory/validation-timestamp.txt" \
    "utc=$(/usr/bin/date -u '+%Y-%m-%dT%H:%M:%SZ')" \
    "epoch=$(/usr/bin/date '+%s')"
write_evidence_line "$evidence_directory/validation-complete.txt" \
    'validated=true' \
    'transaction=exactly-once' \
    'install_reason=Explicit'
write_evidence_line "$evidence_directory/real-pacman-exec.txt" \
    "argv=-U --noconfirm -- $staged_artifact"

# Every retained diagnostic is immutable to the validation user and readable
# only through the evidence group. The directory is one-shot, so this cannot
# overwrite evidence from another transaction.
for retained_evidence in "$evidence_directory"/*; do
    if [ -f "$retained_evidence" ] && [ ! -L "$retained_evidence" ]; then
        /usr/bin/chown root:"$validation_user" "$retained_evidence"
        /usr/bin/chmod 0640 "$retained_evidence"
    fi
done

exec_real_pacman -U --noconfirm -- "$staged_artifact"
