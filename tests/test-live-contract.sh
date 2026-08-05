#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
live_root=$repo_root/containers/arch-live-validation
readme_file=$live_root/README.md
local_package_file=$live_root/fixtures/local-package/PKGBUILD
local_contract_file=$live_root/fixtures/local-package/contract.env
aur_case_file=$live_root/aur-cases.tsv
aur_payload_file=$live_root/fixtures/aur/fetchfetch-payload-authority.tsv
live_dockerfile=$live_root/Dockerfile
provider_runner=$live_root/run-provider-selection.sh
pacman_sentinel=$live_root/pacman-sentinel.sh
aur_dockerfile=$live_root/Dockerfile.aur
aur_runner=$live_root/run-aur-install.sh
aur_gateway=$live_root/aur-pacman-gateway.sh
aur_stage_helper=$live_root/aur-stage-artifact.py
aur_metadata_helper=$live_root/aur-archive-metadata-check.c
dockerignore_file=$repo_root/.dockerignore
makefile=$repo_root/Makefile
offline_dockerfile=$repo_root/containers/arch-validation/Dockerfile
offline_runner=$repo_root/containers/arch-validation/run-tests.sh

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

assert_regular_file() {
    path=$1
    label=$2
    if [ ! -f "$path" ] || [ -L "$path" ]; then
        fail "required file must be a regular non-symlink: $label ($path)"
    fi
}

assert_contains() {
    file=$1
    expected=$2
    if ! grep -F -- "$expected" "$file" >/dev/null; then
        fail "missing contract entry in $file: $expected"
    fi
}

assert_not_contains() {
    file=$1
    unexpected=$2
    if grep -F -- "$unexpected" "$file" >/dev/null; then
        fail "forbidden contract entry in $file: $unexpected"
    fi
}

make_target_body() {
    target_name=$1
    awk -v target="$target_name" '
        $0 == target ":" {
            in_target = 1
        }
        in_target && $0 != target ":" &&
            $0 ~ /^[A-Za-z0-9_.-]+([[:space:]]+[A-Za-z0-9_.-]+)*:/ {
            exit
        }
        in_target {
            print
        }
    ' "$makefile"
}

assert_regular_file "$readme_file" 'live validation contract'
assert_regular_file "$local_package_file" 'local package PKGBUILD'
assert_regular_file "$local_contract_file" 'local package contract env'
assert_regular_file "$aur_case_file" 'AUR case authority table'
assert_regular_file "$aur_payload_file" 'AUR expected payload authority'
assert_regular_file "$live_dockerfile" 'live provider Dockerfile'
assert_regular_file "$provider_runner" 'live provider runner'
assert_regular_file "$pacman_sentinel" 'live pacman sentinel source'
assert_regular_file "$aur_dockerfile" 'live AUR Dockerfile'
assert_regular_file "$aur_runner" 'live AUR runner'
assert_regular_file "$aur_gateway" 'live AUR pacman gateway'
assert_regular_file "$aur_stage_helper" 'live AUR staging helper'
assert_regular_file "$aur_metadata_helper" 'live AUR libarchive metadata helper'
assert_regular_file "$dockerignore_file" 'Docker context exclusion contract'
assert_regular_file "$offline_dockerfile" 'offline validation Dockerfile'
assert_regular_file "$offline_runner" 'offline validation runner'

assert_contains "$readme_file" "live laneは既存のoffline validation lane"
assert_contains "$readme_file" "ベースイメージは \`archlinux:latest\` を利用する"
assert_contains "$readme_file" "実行時ネットワークは**有効**"
assert_contains "$readme_file" "--privileged"
assert_contains "$readme_file" "暗黙のフォールバックを許容せず"
assert_contains "$readme_file" "make test\` / \`release-check\` を再帰的に起動しない"
assert_contains "$local_package_file" "pkgname='moguet-live-fixture'"

if ! grep -E '^pkgver=1\.0\.0$' "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep pkgver 1.0.0'
fi
if ! grep -E '^pkgrel=1$' "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep pkgrel 1'
fi
if ! grep -F "makedepends=('cargo')" "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep makedepends=(\"cargo\")'
fi
if grep -E '^source=\(.+\)$' "$local_package_file" >/dev/null; then
    fail 'local fixture source list must be empty for no source download'
fi
if ! grep -F 'source=()' "$local_package_file" >/dev/null; then
    fail 'local fixture must not require external source'
fi
if ! grep -F 'live-fixture-marker' "$local_package_file" >/dev/null; then
    fail 'local fixture package() must install marker artifact'
fi
if [ -e "$live_root/fixtures/local-package/.SRCINFO" ]; then
    fail 'local fixture must not track .SRCINFO for live contract'
fi

assert_contract_assignment() {
    expected=$1
    if ! grep -Fx -- "$expected" "$local_contract_file" >/dev/null; then
        fail "missing local fixture contract assignment: $expected"
    fi
}

assert_contract_assignment 'REQUIRED_MAKE_DEPENDENCY=cargo'
assert_contract_assignment 'EXPECTED_PROVIDER_PACKAGES=rust,rustup'
assert_contract_assignment 'PROVIDER_INSTALL_REASON=Dependency'
assert_contract_assignment 'ROOT_ARTIFACT_INSTALL_REASON=Explicit'

if grep -F 'SELECTED_MAKE_PROVIDER_EXPECTED=' "$local_contract_file" >/dev/null; then
    fail 'local fixture must not treat cargo as a selected provider package'
fi

tab=$(printf '\tX')
tab=${tab%X}
expected_case_header=$(printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s' \
    '# package' package_base expected_version runtime_dependencies \
    make_dependencies source_kind install_reason fallback_policy \
    review_required expected_aur_git_head expected_pkgbuild_sha256 \
    expected_srcinfo_sha256 expected_source_filename expected_source_url \
    expected_source_sha256 expected_rpc_url_path \
    expected_artifact_architecture)
case_header=
package_name=
package_base=
expected_version=
runtime_deps=
make_deps=
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
expected_artifact_architecture=
extra_field=
{
    IFS= read -r case_header || fail 'AUR case table has no header'
    IFS=$tab read -r \
        package_name package_base expected_version runtime_deps make_deps \
        source_kind install_reason fallback_policy review_required \
        expected_aur_git_head expected_pkgbuild_sha256 \
        expected_srcinfo_sha256 expected_source_filename expected_source_url \
        expected_source_sha256 expected_rpc_url_path \
        expected_artifact_architecture extra_field ||
        fail 'AUR case table has no case row'
    if IFS= read -r _; then
        fail 'AUR case table must contain exactly one case row'
    fi
} < "$aur_case_file"

[ "$case_header" = "$expected_case_header" ] || fail 'AUR case header drift'
[ -z "$extra_field" ] || fail 'AUR case table has extra columns'
[ "$package_name" = fetchfetch ] || fail 'AUR case package must be fetchfetch'
[ "$package_base" = fetchfetch ] || fail 'AUR PackageBase must be fetchfetch'
[ "$expected_version" = 2.0.0-1 ] || fail 'unexpected AUR case version'
[ "$runtime_deps" = glibc ] || fail 'unexpected AUR runtime dependencies'
[ "$make_deps" = gcc,make ] || fail 'unexpected AUR make dependencies'
[ "$source_kind" = single-release-archive ] || fail 'unexpected source kind'
[ "$install_reason" = Explicit ] || fail 'unexpected AUR install reason'
[ "$fallback_policy" = reject ] || fail 'AUR fallback policy must reject'
[ "$review_required" = required ] || fail 'AUR drift must require review'
[ "$expected_aur_git_head" = \
    353dc8ea947734b85175158cda87e5d084585c3f ] || fail 'pinned AUR HEAD drift'
[ "$expected_pkgbuild_sha256" = \
    6b9e09ae9c297c5be32b1bd6a038172ad1400cd0f4a5867fe2393b468d964232 ] ||
    fail 'pinned PKGBUILD hash drift'
[ "$expected_srcinfo_sha256" = \
    d685ae938e6cfe1d2001dba3772539564b2cd8e7b2d488e7e78d92762f411df1 ] ||
    fail 'pinned .SRCINFO hash drift'
[ "$expected_source_filename" = fetchfetch-2.0.0.tar.gz ] ||
    fail 'pinned source filename drift'
[ "$expected_source_url" = \
    https://github.com/spenserblack/fetchfetch/archive/v2.0.0.tar.gz ] ||
    fail 'pinned source URL drift'
[ "$expected_source_sha256" = \
    e67be2f63497b6f75017873e06935218a407e46023fe10f422fb259e92a7d7ae ] ||
    fail 'pinned source SHA-256 drift'
[ "$expected_rpc_url_path" = \
    /cgit/aur.git/snapshot/fetchfetch.tar.gz ] || fail 'AUR URLPath drift'
[ "$expected_artifact_architecture" = x86_64 ] ||
    fail 'pinned artifact architecture drift'

python3 - "$aur_payload_file" <<'PY' || fail 'AUR payload authority format or identity drift'
from pathlib import Path
import sys

path = Path(sys.argv[1])
rows = [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()]
if not rows or rows[0] != ["# path", "type", "mode", "sha256"]:
    raise SystemExit("authority header drift")
if any(len(row) != 4 for row in rows) or any(not row[0] for row in rows[1:]):
    raise SystemExit("authority is not exact-tab TSV")
entries = {row[0]: tuple(row[1:]) for row in rows[1:]}
if len(entries) != len(rows) - 1 or [row[0] for row in rows[1:]] != sorted(entries):
    raise SystemExit("authority is not sorted and duplicate-free")
expected = {
    "usr/": ("directory", "0755", "-"),
    "usr/bin/": ("directory", "0755", "-"),
    "usr/bin/fetchfetch": ("regular", "0755", "-"),
    "usr/share/": ("directory", "0755", "-"),
    "usr/share/doc/": ("directory", "0755", "-"),
    "usr/share/doc/fetchfetch/": ("directory", "0755", "-"),
    "usr/share/doc/fetchfetch/README.md": (
        "regular", "0644",
        "26ac44a45dfae74d33d54e474bc14a2d677f0e720dade11882bd3bea3e5b0d9a",
    ),
    "usr/share/licenses/": ("directory", "0755", "-"),
    "usr/share/licenses/fetchfetch/": ("directory", "0755", "-"),
    "usr/share/licenses/fetchfetch/LICENSE": (
        "regular", "0644",
        "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986",
    ),
}
if entries != expected:
    raise SystemExit("authority entries drift")
if any(item.startswith("/") or "/../" in item or item.startswith("../") for item in entries):
    raise SystemExit("authority path traversal")
PY

if ! grep -R --line-number -- 'fetchfetch' "$live_root" >/dev/null; then
    fail 'AUR case contract file must include fetchfetch entry'
fi

# Ensure offline validation lane is not forced as a live dependency by contract text.
assert_contains "$readme_file" "offline validation lane"

# Slice 2 keeps a standalone image and production binary/runtime route.
assert_contains "$live_dockerfile" 'FROM archlinux:latest'
assert_contains "$live_dockerfile" 'pacman -Syu --needed --noconfirm'
assert_contains "$live_dockerfile" 'COPY --chown=moguet-validation:moguet-validation . .'
assert_contains "$live_dockerfile" 'USER moguet-validation:moguet-validation'
assert_contains "$live_dockerfile" 'make -j8 --output-sync=target'
assert_contains "$live_dockerfile" 'CMD ["containers/arch-live-validation/run-provider-selection.sh"]'
assert_contains "$provider_runner" 'production_moguet=$repo_root/moguet'
assert_contains "$provider_runner" 'makepkg --printsrcinfo > .SRCINFO'
assert_contains "$provider_runner" 'python3 "$pty_runner" --timeout 90 --'
assert_contains "$provider_runner" 'candidate_package'
assert_contains "$provider_runner" 'rust_choice=$(awk'
assert_contains "$provider_runner" 'rustup_choice=$(awk'
assert_contains "$provider_runner" 'fail "provider drift: unexpected cargo provider'

if grep -E '^[[:space:]]+(rust|rustup|cargo)([[:space:]\\]|$)' \
    "$live_dockerfile" >/dev/null; then
    fail 'live image must not preinstall rust, rustup, or cargo'
fi
if grep -E '(^|[[:space:]])(include|inherit|COPY)[[:space:]].*arch-validation' \
    "$live_dockerfile" >/dev/null; then
    fail 'live Dockerfile must not include, inherit, or copy the offline lane'
fi
assert_not_contains "$live_dockerfile" 'containers/arch-validation'

# Host metadata, credentials, state, and artifacts never enter COPY layers.
for excluded_path in \
    '.git' \
    '.agents' \
    '.codex' \
    '.ssh' \
    '.gnupg' \
    '.git-credentials' \
    '.netrc' \
    '*.sock' \
    '**/__pycache__' \
    '**/*.py[cod]'
do
    assert_contains "$dockerignore_file" "$excluded_path"
done

# The canonical pacman path is replaced only inside the fully provisioned
# image.  Both the source and runtime copy are immutable to the validation
# user, and the original executable is root-only and never exec'd by a case.
assert_contains "$live_dockerfile" 'install -o root -g root -m 0555'
assert_contains "$live_dockerfile" 'pacman-sentinel.sh'
assert_contains "$live_dockerfile" '/usr/bin/pacman'
assert_contains "$live_dockerfile" '/usr/libexec/moguet-live-validation/pacman.real'
assert_contains "$live_dockerfile" 'install -d -o root -g root -m 0711'
assert_contains "$live_dockerfile" 'moguet-validation ALL=(root) NOPASSWD: /usr/bin/pacman *'
assert_contains "$pacman_sentinel" 'readonly_sentinel_status=86'
assert_contains "$pacman_sentinel" "printf '%s\\0' sudo pacman"
assert_contains "$pacman_sentinel" "[ \"\$#\" -eq 5 ]"
assert_contains "$pacman_sentinel" "[ \"\$#\" -eq 6 ]"
assert_contains "$pacman_sentinel" "[ \"\$1\" = '-S' ]"
assert_contains "$pacman_sentinel" "[ \"\$2\" = '--asdeps' ]"
assert_contains "$pacman_sentinel" "[ \"\$3\" = '--needed' ]"
assert_contains "$pacman_sentinel" "[ \"\$4\" = '--noconfirm' ]"
assert_contains "$pacman_sentinel" 'extra/rust|extra/rustup'
assert_contains "$pacman_sentinel" "reject 'target is not an allowed repo-qualified provider'"
assert_contains "$provider_runner" "sentinel-reject-syu 'rejected argv'"
assert_not_contains "$pacman_sentinel" 'pacman.real'
assert_not_contains "$pacman_sentinel" 'exec '

# Candidate numbers are derived from identity fields rather than fixed order.
if grep -E 'rust(up)?_choice=[12]($|[^0-9])' "$provider_runner" >/dev/null; then
    fail 'provider runner must not hard-code rust/rustup candidate numbers'
fi
assert_contains "$provider_runner" "candidate_source\" != repository"
assert_contains "$provider_runner" "candidate_repository\" != extra"
assert_contains "$provider_runner" "candidate_dependency\" != cargo"
assert_contains "$provider_runner" 'provider candidate set contains duplicate package identities'
assert_contains "$provider_runner" 'provider presentation changed after discovery'
assert_contains "$provider_runner" "candidate_package\" in"
assert_contains "$provider_runner" 'rust|rustup)'
assert_not_contains "$provider_runner" 'fallback_package='
assert_not_contains "$provider_runner" 'fallback_dependency='

# Slice 3 is a standalone AUR image with a separate root gateway and runner.
assert_contains "$aur_dockerfile" 'FROM archlinux:latest'
assert_contains "$aur_dockerfile" 'pacman -Syu --needed --noconfirm'
for required_aur_image_package in \
    acl base-devel curl git libarchive python sudo util-linux zstd
do
    assert_contains "$aur_dockerfile" "$required_aur_image_package"
done
assert_contains "$aur_dockerfile" 'test "$(uname -m)" = x86_64'
assert_contains "$aur_dockerfile" '! pacman -Qq fetchfetch'
assert_contains "$aur_dockerfile" \
    'NoExtract = !usr/share/doc/fetchfetch/README.md'
assert_contains "$aur_dockerfile" \
    "grep -Fx -- '!usr/share/doc/fetchfetch/README.md'"
assert_contains "$aur_dockerfile" \
    'COPY --chown=moguet-validation:moguet-validation . .'
assert_contains "$aur_dockerfile" 'USER moguet-validation:moguet-validation'
assert_contains "$aur_dockerfile" 'make -j8 --output-sync=target'
assert_contains "$aur_dockerfile" '/usr/libexec/moguet-live-aur/pacman.real'
assert_contains "$aur_dockerfile" 'aur-pacman-gateway.sh'
assert_contains "$aur_dockerfile" '/usr/bin/pacman'
assert_contains "$aur_dockerfile" 'aur-cases.tsv'
assert_contains "$aur_dockerfile" 'fetchfetch-payload-authority.tsv'
assert_contains "$aur_dockerfile" 'aur-archive-metadata-check.c'
assert_contains "$aur_dockerfile" '-std=c11 -Wall -Wextra -Wpedantic -Werror'
assert_contains "$aur_dockerfile" '-larchive'
assert_contains "$aur_dockerfile" '/usr/libexec/moguet-live-aur/aur-archive-metadata-check'
assert_contains "$aur_dockerfile" 'chmod 0555'
assert_contains "$aur_dockerfile" 'install -o root -g root -m 0444'
assert_contains "$aur_dockerfile" '/usr/libexec/moguet-live-aur/fixtures/fetchfetch-payload.tsv'
assert_contains "$aur_dockerfile" '/usr/libexec/moguet-live-aur/fixtures/fetchfetch-pkginfo.tsv'
assert_contains "$aur_dockerfile" 'pkginfo-manifest'
assert_contains "$aur_dockerfile" 'reference-build'
assert_contains "$aur_dockerfile" 'git clone --no-checkout'
assert_contains "$aur_dockerfile" '353dc8ea947734b85175158cda87e5d084585c3f'
assert_contains "$aur_dockerfile" '6b9e09ae9c297c5be32b1bd6a038172ad1400cd0f4a5867fe2393b468d964232'
assert_contains "$aur_dockerfile" 'd685ae938e6cfe1d2001dba3772539564b2cd8e7b2d488e7e78d92762f411df1'
assert_contains "$aur_dockerfile" 'e67be2f63497b6f75017873e06935218a407e46023fe10f422fb259e92a7d7ae'
assert_contains "$aur_dockerfile" '/usr/bin/makepkg --config'
assert_contains "$aur_dockerfile" '--nodeps --noconfirm'
assert_contains "$aur_dockerfile" '/usr/bin/python3 -I /usr/libexec/moguet-live-aur/aur-stage-artifact.py'
assert_contains "$aur_dockerfile" 'manifest'
assert_contains "$aur_dockerfile" 'rm -rf -- "$reference_root"'
assert_contains "$aur_dockerfile" \
    'install -d -o root -g root -m 0755'
assert_contains "$aur_dockerfile" \
    'install -d -o root -g moguet-validation -m 0750'
assert_contains "$aur_dockerfile" \
    'moguet-validation ALL=(root) NOPASSWD: /usr/bin/pacman *'
assert_contains "$aur_dockerfile" \
    'Defaults:moguet-validation env_keep += "MOGUET_LIVE_AUR_CASE"'
assert_contains "$aur_dockerfile" \
    'visudo -cf /etc/sudoers.d/moguet-live-aur'
assert_contains "$aur_dockerfile" \
    'CMD ["containers/arch-live-validation/run-aur-install.sh"]'
assert_not_contains "$aur_dockerfile" \
    'NOPASSWD: /usr/libexec/moguet-live-aur/pacman.real'
assert_not_contains "$aur_dockerfile" 'pacman-sentinel.sh'
assert_not_contains "$aur_dockerfile" 'run-provider-selection.sh'
assert_not_contains "$aur_dockerfile" 'containers/arch-validation'
if grep -E '(^|[[:space:]])(include|inherit|COPY)[[:space:]].*Dockerfile' \
    "$aur_dockerfile" >/dev/null; then
    fail 'AUR Dockerfile must not include, inherit, or copy another Dockerfile'
fi

assert_contains "$aur_gateway" '"/proc/$$/status"'
assert_contains "$aur_gateway" 'if [ "$kernel_effective_uid" -ne 0 ]'
assert_contains "$aur_gateway" 'PATH=/usr/bin'
assert_contains "$aur_gateway" 'export PATH'
assert_contains "$aur_gateway" 'unset PYTHONPATH'
assert_contains "$aur_gateway" 'unset PYTHONHOME'
assert_contains "$aur_gateway" 'exec /usr/bin/env -i PATH=/usr/bin LC_ALL=C'
assert_contains "$aur_gateway" 'fetchfetch-content-drift-test'
assert_contains "$aur_gateway" 'fetchfetch-conflict-policy-test'
assert_contains "$aur_gateway" 'fetchfetch-xattr-metadata-test'
assert_contains "$aur_gateway" 'fetchfetch-acl-metadata-test'
assert_contains "$aur_gateway" 'fetchfetch-pkgdesc-authority-test'
assert_contains "$aur_gateway" \
    'missing or unknown MOGUET_LIVE_AUR_CASE'
assert_contains "$aur_gateway" '[ "$#" -ne 4 ]'
assert_contains "$aur_gateway" '[ "$1" != -U ]'
assert_contains "$aur_gateway" '[ "$2" != --noconfirm ]'
assert_contains "$aur_gateway" '[ "$3" != -- ]'
assert_contains "$aur_gateway" \
    'root argv must be exactly: -U --noconfirm -- <one artifact>'
assert_contains "$aur_gateway" \
    '/home/moguet-validation/.cache/moguet/.artifact-workspace~-*/*'
assert_contains "$aur_gateway" 'raw artifact path differs from its canonical realpath'
assert_contains "$aur_gateway" 'source artifact is group/other writable'
assert_contains "$aur_gateway" 'source artifact has an unexpected hard-link count'
assert_contains "$aur_gateway" 'mkdir -m 0750 -- "$evidence_directory"'
assert_contains "$aur_gateway" "printf '%s\\0' sudo pacman"
assert_contains "$aur_gateway" 'stage-hashes.txt'
assert_contains "$aur_gateway" 'archive-metadata-check.txt'
assert_contains "$aur_gateway" '/usr/bin/python3 -I "$stage_helper" stage'
assert_contains "$aur_gateway" 'metadata_helper=/usr/libexec/moguet-live-aur/aur-archive-metadata-check'
assert_contains "$aur_gateway" 'staged artifact direct metadata validation failed'
assert_contains "$aur_gateway" '/usr/bin/python3 -I "$stage_helper" validate'
assert_contains "$aur_gateway" 'fetchfetch-pkginfo.tsv'
assert_contains "$aur_gateway" 'staged artifact path, content, or PKGINFO validation failed'
assert_contains "$aur_gateway" 'negative test case must never invoke real pacman'
assert_contains "$aur_gateway" 'gateway_reject_status=97'
assert_contains "$aur_gateway" \
    'exec_real_pacman -U --noconfirm -- "$staged_artifact"'
metadata_check_line=$(grep -n -F '"$metadata_helper" "$staged_artifact"' \
    "$aur_gateway" | cut -d: -f1)
pkginfo_validation_line=$(grep -n -F \
    '/usr/bin/python3 -I "$stage_helper" validate' "$aur_gateway" | cut -d: -f1)
[ -n "$metadata_check_line" ] && [ -n "$pkginfo_validation_line" ] ||
    fail 'gateway must invoke the metadata helper and staged package validator'
[ "$metadata_check_line" -lt "$pkginfo_validation_line" ] ||
    fail 'gateway must inspect direct archive metadata before bsdtar-based validation'
assert_not_contains "$aur_gateway" 'pacman -S '
assert_not_contains "$aur_gateway" 'pacman -R '
assert_not_contains "$aur_gateway" 'pacman -Syu'

assert_contains "$aur_stage_helper" 'os.O_NOFOLLOW'
assert_contains "$aur_stage_helper" 'os.O_EXCL'
assert_contains "$aur_stage_helper" 'source.parent.parent != CACHE_ROOT'
assert_contains "$aur_stage_helper" 'source artifact is not owned by the validation user'
assert_contains "$aur_stage_helper" 'source artifact is group/other writable'
assert_contains "$aur_stage_helper" 'source artifact has an unexpected hard-link count'
assert_contains "$aur_stage_helper" 'source_hash_before'
assert_contains "$aur_stage_helper" 'copied_hash'
assert_contains "$aur_stage_helper" 'staged_hash'
assert_contains "$aur_stage_helper" 'source_hash_after'
assert_contains "$aur_stage_helper" 'len({source_hash_before, copied_hash, staged_hash, source_hash_after})'
assert_contains "$aur_stage_helper" 'EXPECTED_STATIC_AUTHORITY'
assert_contains "$aur_stage_helper" 'PKGINFO_FORBIDDEN_TRANSACTION_FIELDS'
assert_contains "$aur_stage_helper" 'PKGINFO_ALLOWED_KEYS'
assert_contains "$aur_stage_helper" 'PKGINFO_STABLE_KEYS'
assert_contains "$aur_stage_helper" 'PKGINFO_MANIFEST_HEADER'
assert_contains "$aur_stage_helper" 'Counter(reference_pairs)'
assert_contains "$aur_stage_helper" '.PKGINFO {key} authority mismatch'
assert_contains "$aur_stage_helper" 'builddate must be one non-negative ASCII decimal integer'
assert_contains "$aur_stage_helper" 'packager has unsafe whitespace or control characters'
assert_contains "$aur_stage_helper" 'archive member owner/group is not root/root'
assert_contains "$aur_stage_helper" '["/usr/bin/bsdtar", *arguments]'
assert_contains "$aur_stage_helper" 'archive payload content hash drift'

assert_contains "$aur_metadata_helper" 'archive_entry_acl_types'
assert_contains "$aur_metadata_helper" 'archive_entry_xattr_count'
assert_contains "$aur_metadata_helper" 'archive_entry_fflags'
assert_contains "$aur_metadata_helper" 'reject_entry("acl"'
assert_contains "$aur_metadata_helper" 'reject_entry("xattr"'
assert_contains "$aur_metadata_helper" 'reject_entry("fflags"'
assert_contains "$aur_metadata_helper" 'category=archive-read'

for gateway_rejection_shape in \
    'run_gateway_rejection syu "$gateway_case" -Syu' \
    'run_gateway_rejection remove "$gateway_case" -R "$package_name"' \
    '-U -- /tmp/fake.pkg.tar.zst' \
    '-U --noconfirm -- /tmp/path1.pkg.tar.zst /tmp/path2.pkg.tar.zst' \
    '-U --asdeps -- /tmp/fake.pkg.tar.zst' \
    'run_gateway_rejection unknown-case unknown-case -Syu' \
    'run_gateway_rejection missing-case missing -Syu'
do
    assert_contains "$aur_runner" "$gateway_rejection_shape"
done
assert_contains "$aur_runner" '"$production_moguet" -Si --aur "$package_name"'
assert_contains "$aur_runner" "printf 'n\\n' | env MOGUET_LIVE_AUR_CASE"
assert_contains "$aur_runner" \
    '"$production_moguet" --nodiff --noconfirm -S --aur "$package_name"'
if grep -E '"\$production_moguet".*--noedit' "$aur_runner" >/dev/null; then
    fail 'production AUR invocation must not use --noedit'
fi
assert_contains "$aur_runner" 'Review target: PKGBUILD'
assert_contains "$aur_runner" 'tests/run-with-pty.py'
assert_contains "$aur_runner" 'git ls-remote "$aur_git_url" HEAD'
assert_contains "$aur_runner" 'git clone --depth 1 --single-branch --no-checkout'
assert_contains "$aur_runner" "'makepkg' '--packagelist'"
assert_contains "$aur_runner" "'makepkg' '-sc' '--noconfirm'"
assert_contains "$aur_runner" 'PackageBase result: fetchfetch'
assert_contains "$aur_runner" \
    'fetchfetch-debug 2.0.0-1 (not selected; not installed)'
assert_contains "$aur_runner" 'production did not clean the original artifact workspace'
assert_contains "$aur_runner" 'root gateway binary-content fail-closed test'
assert_contains "$aur_runner" 'usr/bin/fetchfetch'
assert_contains "$aur_runner" 'content-drift gateway rejection changed package inventory'
assert_contains "$aur_runner" 'content-drift rejection consumed valid one-shot evidence'
assert_contains "$aur_runner" 'root gateway transaction-metadata fail-closed test'
assert_contains "$aur_runner" 'forbidden transaction field: conflict'
assert_contains "$aur_runner" 'conflict-policy gateway rejection changed package inventory'
assert_contains "$aur_runner" 'root gateway xattr metadata fail-closed test'
assert_contains "$aur_runner" 'user.moguet-live-test'
assert_contains "$aur_runner" '--xattrs --acls'
assert_contains "$aur_runner" 'category=xattr entry=usr/bin/fetchfetch'
assert_contains "$aur_runner" 'root gateway ACL metadata fail-closed test'
assert_contains "$aur_runner" 'setfacl -m u:65534:rx'
assert_contains "$aur_runner" 'category=acl entry=usr/bin/fetchfetch'
assert_contains "$aur_runner" 'root gateway PKGINFO authority fail-closed test'
assert_contains "$aur_runner" '.PKGINFO pkgdesc authority mismatch'
assert_contains "$aur_runner" 'assert_repacked_path_set'
assert_contains "$aur_runner" 'assert_independent_artifact_unchanged'
assert_contains "$aur_runner" 'negative artifact was retained near the positive install target'
assert_contains "$aur_runner" 'one positive and five independent negative evidence directories'
assert_contains "$aur_runner" 'real pacman not reached'
assert_contains "$aur_runner" 'pacman -Qe "$package_name"'
assert_contains "$aur_runner" 'pacman -Qd "$package_name"'
assert_contains "$aur_runner" 'dependency version or install reason changed'
assert_contains "$aur_runner" \
    'container pacman config does not retain the exact expected README'
assert_not_contains "$aur_runner" 'fallback_package='
assert_not_contains "$aur_runner" 'fallback_dependency='

# Provider and offline sources never acquire the AUR real-install gateway.
for protected_lane_file in \
    "$live_dockerfile" "$provider_runner" "$pacman_sentinel" \
    "$offline_dockerfile" "$offline_runner"
do
    assert_not_contains "$protected_lane_file" 'moguet-live-aur'
    assert_not_contains "$protected_lane_file" 'aur-pacman-gateway'
done

# The explicit live target has no host mount, privileged mode, Docker socket,
# host package state, or runtime network disablement.  Failure remains the
# docker command's non-zero status.
live_target=$(make_target_body test-container-live-provider)
printf '%s\n' "$live_target" | grep -F -- '$(DOCKER) build --pull' >/dev/null ||
    fail 'live target must use docker build --pull'
printf '%s\n' "$live_target" | grep -F -- \
    '--file containers/arch-live-validation/Dockerfile' >/dev/null ||
    fail 'live target must select the standalone live Dockerfile'
printf '%s\n' "$live_target" | grep -F -- '$(DOCKER) run --rm' >/dev/null ||
    fail 'live target must destroy its container with --rm'
for forbidden_runtime_option in \
    '--privileged' \
    '--network=none' \
    '--mount' \
    '--volume' \
    'docker.sock' \
    '/var/lib/pacman' \
    '/etc/pacman.conf' \
    '/var/cache/pacman'
do
    if printf '%s\n' "$live_target" | grep -F -- "$forbidden_runtime_option" >/dev/null; then
        fail "live Make target contains forbidden runtime option: $forbidden_runtime_option"
    fi
done

live_target_reference_count=$(grep -F -c \
    'test-container-live-provider' "$makefile")
if [ "$live_target_reference_count" -ne 2 ]; then
    fail 'live provider target must appear only in .PHONY and its explicit definition'
fi

aur_live_target=$(make_target_body test-container-live-aur)
printf '%s\n' "$aur_live_target" | grep -F -- \
    '$(DOCKER) build --pull' >/dev/null ||
    fail 'live AUR target must use docker build --pull'
printf '%s\n' "$aur_live_target" | grep -F -- \
    '--file containers/arch-live-validation/Dockerfile.aur' >/dev/null ||
    fail 'live AUR target must select the standalone AUR Dockerfile'
printf '%s\n' "$aur_live_target" | grep -F -- \
    '$(DOCKER) run --rm' >/dev/null ||
    fail 'live AUR target must destroy its container with --rm'
for forbidden_runtime_option in \
    '--privileged' \
    '--network=none' \
    '--mount' \
    '--volume' \
    'docker.sock' \
    '/var/lib/pacman' \
    '/etc/pacman.conf' \
    '/var/cache/pacman'
do
    if printf '%s\n' "$aur_live_target" |
        grep -F -- "$forbidden_runtime_option" >/dev/null; then
        fail "live AUR Make target contains forbidden runtime option: $forbidden_runtime_option"
    fi
done
aur_live_target_reference_count=$(grep -F -c \
    'test-container-live-aur' "$makefile")
if [ "$aur_live_target_reference_count" -ne 2 ]; then
    fail 'live AUR target must appear only in .PHONY and its explicit definition'
fi
test_target=$(make_target_body test)
release_target=$(make_target_body release-check)
if printf '%s\n%s\n' "$test_target" "$release_target" |
    grep -E 'test-container-live-(provider|aur)' >/dev/null; then
    fail 'make test or release-check must not recursively start the live lane'
fi

# Existing offline files and target remain isolated from the new live paths.
assert_not_contains "$offline_dockerfile" 'arch-live-validation'
assert_not_contains "$offline_runner" 'arch-live-validation'
offline_target=$(make_target_body test-container)
assert_not_contains_target=$(printf '%s\n' "$offline_target" |
    grep -F 'arch-live-validation' || true)
if [ -n "$assert_not_contains_target" ]; then
    fail 'existing test-container recipe contains live-lane paths'
fi
printf '%s\n' "$offline_target" | grep -F -- '--network=none' >/dev/null ||
    fail 'existing offline target lost its offline runtime network boundary'

# The tracked fixture remains the only source authority; generated metadata is
# case-local and real package/source execution remains beyond Slice 2.
if find "$live_root/fixtures/local-package" -mindepth 1 -name .SRCINFO -print |
    grep . >/dev/null; then
    fail 'tracked live fixture must not contain .SRCINFO'
fi
assert_contains "$provider_runner" "assert_sentinel_absent \"\$current_case\""
assert_contains "$provider_runner" "assert_not_contains \"Running: 'git'\""
assert_contains "$provider_runner" "assert_not_contains \"Running: 'makepkg'\""
assert_contains "$provider_runner" \
    "assert_not_contains \"Running: 'sudo' 'pacman' '-U'\""
assert_contains "$provider_runner" "-name '.artifact-workspace~-*'"
assert_contains "$provider_runner" 'package_database_manifest'
assert_contains "$provider_runner" 'tracked_fixture_manifest'

assert_contains "$readme_file" 'real Arch repository metadata'
assert_contains "$readme_file" 'review-required failure'
assert_contains "$readme_file" '実package transactionは行わない'
assert_contains "$readme_file" 'Slice 3: real AUR'
assert_contains "$readme_file" 'real public AUR'
assert_contains "$readme_file" 'AUR package **fetchfetchだけ**'
assert_contains "$readme_file" 'root-owned stagingへcopy'
assert_contains "$readme_file" 'reason **Explicit**'
assert_contains "$readme_file" 'version/reasonのbefore/afterが不変'
assert_contains "$readme_file" 'direct libarchive helper'
assert_contains "$readme_file" 'permission listingを'
assert_contains "$readme_file" 'ACL/xattr不在のauthorityには使わない'
assert_contains "$readme_file" 'exact multiset照合'
assert_contains "$readme_file" 'xattr、ACL、`pkgdesc` authority drift'
assert_contains "$readme_file" '!usr/share/doc/fetchfetch/README.md'
assert_contains "$readme_file" 'Slice 4: local PKGBUILD root'
assert_contains "$readme_file" 'image / layer cacheはhost localに残り得る'
assert_contains "$readme_file" 'host systemへpackage mutationは行わない'

printf '%s\n' 'live contract tests: all checks passed'
