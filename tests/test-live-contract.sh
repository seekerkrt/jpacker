#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
live_root=$repo_root/containers/arch-live-validation
readme_file=$live_root/README.md
local_package_file=$live_root/fixtures/local-package/PKGBUILD
local_contract_file=$live_root/fixtures/local-package/contract.env
aur_case_file=$live_root/aur-cases.tsv

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

assert_regular_file "$readme_file" 'live validation contract'
assert_regular_file "$local_package_file" 'local package PKGBUILD'
assert_regular_file "$local_contract_file" 'local package contract env'
assert_regular_file "$aur_case_file" 'AUR case authority table'

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

case_count=0
tab=$(printf '\tX')
tab=${tab%X}
while IFS=$tab read -r package_name package_base expected_version runtime_deps make_deps source_kind install_reason fallback_policy review_required; do
    if [ -z "$package_name" ] || [ "${package_name#'#'}" != "$package_name" ]; then
        continue
    fi

    case_count=$((case_count + 1))
    if [ "$package_name" != 'fetchfetch' ]; then
        fail "AUR case package must be fetchfetch: $package_name"
    fi
    if [ "$package_base" != 'fetchfetch' ]; then
        fail "AUR case PackageBase must be fetchfetch: $package_base"
    fi
    if [ "$expected_version" != '2.0.0-1' ]; then
        fail "unexpected AUR case version: $expected_version"
    fi
    if [ "$runtime_deps" != 'glibc' ]; then
        fail "unexpected AUR runtime dependency contract: $runtime_deps"
    fi
    if [ "$make_deps" != 'gcc,make' ]; then
        fail "unexpected AUR make dependencies contract: $make_deps"
    fi
    if [ "$source_kind" != 'single-release-archive' ]; then
        fail "unexpected AUR source-kind contract: $source_kind"
    fi
    if [ "$install_reason" != 'Explicit' ]; then
        fail "unexpected AUR install reason contract: $install_reason"
    fi
    if [ "$fallback_policy" != 'reject' ]; then
        fail "AUR implicit fallback must be rejected: $fallback_policy"
    fi
    if [ "$review_required" != 'required' ]; then
        fail "AUR case must request review on authoritative drift: $review_required"
    fi
done < "$aur_case_file"

if [ "$case_count" -ne 1 ]; then
    fail "expected exactly one AUR case for slice-1 contract"
fi
if ! grep -R --line-number -- 'fetchfetch' "$live_root" >/dev/null; then
    fail 'AUR case contract file must include fetchfetch entry'
fi

# Ensure offline validation lane is not forced as a live dependency by contract text.
assert_contains "$readme_file" "offline validation lane"

printf '%s\n' 'live contract tests: all checks passed'
