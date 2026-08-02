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

[ -x ./moguet ] || fail "./moguet is missing or not executable; run make first."
binary_version=$(./moguet --version)
expected_binary_version="Moguet v$version"
[ "$binary_version" = "$expected_binary_version" ] ||
    fail "./moguet --version mismatch: expected '$expected_binary_version', got '$binary_version'"
pass "./moguet --version matches VERSION"

for manpage in man/moguet.1 man/ja/moguet.1
do
    [ -f "$manpage" ] || fail "$manpage is missing; run make first."
    grep -Fq "\"Moguet $version\"" "$manpage" ||
        fail "$manpage does not contain generated version 'Moguet $version'."
    pass "$manpage contains VERSION"
done

[ -f PKGBUILD ] || fail "PKGBUILD is missing."
grep -Fq 'pkgver=$(_read_version_file VERSION)' PKGBUILD ||
    fail "PKGBUILD does not set pkgver from VERSION."
pass "PKGBUILD pkgver reads VERSION"

grep -Fq '#tag=v${pkgver}' PKGBUILD ||
    fail "PKGBUILD source does not use #tag=v\${pkgver}."
pass "PKGBUILD source uses v\${pkgver} tag"

printf 'release-check: all checks passed\n'
