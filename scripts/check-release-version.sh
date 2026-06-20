#!/bin/sh

set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)

fail() {
    printf 'release-check: %s\n' "$*" >&2
    exit 1
}

pass() {
    printf 'release-check: ok: %s\n' "$*"
}

cd "$repo_root"

[ -f VERSION ] || fail "VERSION file is missing."

version=$(tr -d '[:space:]' < VERSION)
[ -n "$version" ] || fail "VERSION is empty."
printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' ||
    fail "VERSION must look like X.Y.Z: $version"
pass "VERSION=$version"

[ -x ./jpacker ] || fail "./jpacker is missing or not executable; run make first."
binary_version=$(./jpacker --version)
expected_binary_version="jpacker v$version"
[ "$binary_version" = "$expected_binary_version" ] ||
    fail "./jpacker --version mismatch: expected '$expected_binary_version', got '$binary_version'"
pass "./jpacker --version matches VERSION"

[ -f jpacker.8 ] || fail "jpacker.8 is missing; run make first."
grep -Fq "\"jpacker $version\"" jpacker.8 ||
    fail "jpacker.8 does not contain generated version 'jpacker $version'."
pass "jpacker.8 contains VERSION"

[ -f PKGBUILD ] || fail "PKGBUILD is missing."
grep -Fq 'pkgver=$(_read_version_file VERSION)' PKGBUILD ||
    fail "PKGBUILD does not set pkgver from VERSION."
pass "PKGBUILD pkgver reads VERSION"

grep -Fq '#tag=v${pkgver}' PKGBUILD ||
    fail "PKGBUILD source does not use #tag=v\${pkgver}."
pass "PKGBUILD source uses v\${pkgver} tag"

printf 'release-check: all checks passed\n'
