#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
srcinfo_file=$tmp_dir/.SRCINFO
stage_root=$tmp_dir/stage
package_work=$tmp_dir/package-work
mkdir -p "$stage_root" "$package_work"

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'packaging-metadata-check: %s\n' "$*" >&2
    exit 1
}

srcinfo_values() {
    key=$1
    awk -v key="$key" '$1 == key && $2 == "=" { print $3 }' "$srcinfo_file"
}

assert_single_value() {
    key=$1
    expected=$2
    actual=$(srcinfo_values "$key")
    [ "$actual" = "$expected" ] ||
        fail "$key mismatch: expected '$expected', got '$actual'."
}

assert_no_values() {
    key=$1
    actual=$(srcinfo_values "$key")
    [ -z "$actual" ] ||
        fail "$key must be unset; found: $actual"
}

assert_value_set() {
    key=$1
    expected=$2
    actual=$(srcinfo_values "$key" | LC_ALL=C sort)
    [ "$actual" = "$expected" ] || {
        printf 'packaging-metadata-check: %s mismatch\nexpected:\n%s\nactual:\n%s\n' \
            "$key" "$expected" "$actual" >&2
        exit 1
    }
}

(
    cd "$repo_root"
    makepkg --printsrcinfo
) > "$srcinfo_file"

assert_single_value pkgbase moguet
assert_single_value pkgname moguet
assert_single_value pkgver 2.0.0
assert_single_value pkgrel 1
assert_single_value arch x86_64
assert_single_value license GPL-3.0-or-later
assert_single_value source \
    'moguet-src::git+https://github.com/seekerkrt/moguet.git#tag=v2.0.0'
assert_single_value sha256sums SKIP

expected_depends='curl
git
libalpm.so
libarchive
nano
pacman
sudo'
assert_value_set depends "$expected_depends"

expected_makedepends='nlohmann-json
tomlplusplus'
assert_value_set makedepends "$expected_makedepends"

# No system or user configuration belongs to the package. The disjoint v1/v2
# payload and lack of a command alias make all four transition fields harmful.
assert_no_values backup
assert_no_values conflicts
assert_no_values replaces
assert_no_values provides
assert_no_values optdepends

for build_only_dependency in nlohmann-json tomlplusplus
do
    if srcinfo_values depends | grep -Fx -- "$build_only_dependency" >/dev/null; then
        fail "$build_only_dependency must not be a runtime dependency."
    fi
done

for forbidden_tomlplusplus_flag in \
    -ltomlplusplus \
    TOML_HEADER_ONLY=0 \
    TOML_SHARED_LIB=1
do
    if grep -F -- "$forbidden_tomlplusplus_flag" "$repo_root/Makefile" >/dev/null; then
        fail "Makefile enables toml++ shared-library mode: $forbidden_tomlplusplus_flag"
    fi
done

# Evaluate the actual package() function against the current source tree. A
# clean archive build uses an isolated local-tag fixture in the package
# transition test because the public v2.0.0 tag belongs to the later cutover.
ln -s "$repo_root" "$package_work/moguet-src"
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

expected_payload='/usr/bin/moguet
/usr/share/bash-completion/completions/moguet
/usr/share/doc/moguet/README.ja.md
/usr/share/doc/moguet/README.md
/usr/share/doc/moguet/THIRD_PARTY_NOTICES.md
/usr/share/doc/moguet/docs/LICENSING.md
/usr/share/doc/moguet/docs/migration/v1-to-v2.ja.md
/usr/share/doc/moguet/docs/migration/v1-to-v2.md
/usr/share/fish/vendor_completions.d/moguet.fish
/usr/share/licenses/moguet/LICENSE
/usr/share/licenses/moguet/bjoern-hoehrmann-utf8-MIT.txt
/usr/share/licenses/moguet/curl.txt
/usr/share/licenses/moguet/jpacker-MIT-legacy.txt
/usr/share/licenses/moguet/nlohmann-json-MIT.txt
/usr/share/licenses/moguet/tomlplusplus-MIT.txt
/usr/share/locale/ja/LC_MESSAGES/moguet.mo
/usr/share/man/ja/man1/moguet.1
/usr/share/man/man1/moguet.1
/usr/share/zsh/site-functions/_moguet'
actual_payload=$(find "$stage_root" -type f -print |
    sed "s|^$stage_root||" | LC_ALL=C sort)
[ "$actual_payload" = "$expected_payload" ] || {
    printf 'packaging-metadata-check: archive payload mismatch:\n%s\n' \
        "$actual_payload" >&2
    exit 1
}

[ ! -e "$stage_root/usr/bin/jpacker" ] ||
    fail "package must not provide a jpacker binary alias."
[ ! -e "$stage_root/etc" ] ||
    fail "package must not install /etc content."
[ ! -e "$stage_root/home" ] ||
    fail "package must not install user XDG content."

command -v readelf >/dev/null 2>&1 ||
    fail "readelf is required for runtime dependency verification."
needed=$(LC_ALL=C readelf -d "$stage_root/usr/bin/moguet" |
    sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
printf '%s\n' "$needed" | grep -Eq '^libcurl\.so(\.|$)' ||
    fail "moguet ELF is missing the direct libcurl runtime dependency."
printf '%s\n' "$needed" | grep -Eq '^libalpm\.so(\.|$)' ||
    fail "moguet ELF is missing the direct libalpm runtime dependency."
if printf '%s\n' "$needed" | grep -Eq '^libintl\.so(\.|$)'; then
    fail "gettext unexpectedly became a linked runtime dependency."
fi

printf 'packaging-metadata-check: all checks passed\n'
