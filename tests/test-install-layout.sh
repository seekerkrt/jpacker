#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
stage_dir=$(mktemp -d)

cleanup() {
    rm -rf "$stage_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'install-layout-test: %s\n' "$*" >&2
    exit 1
}

run_make() {
    make -C "$repo_root" --no-print-directory \
        PREFIX=/usr DESTDIR="$stage_dir" "$@"
}

assert_installed_file() {
    source_file=$1
    installed_file=$2

    [ -f "$installed_file" ] ||
        fail "$installed_file is missing or is not a regular file."
    [ ! -L "$installed_file" ] ||
        fail "$installed_file must not be a symbolic link."

    mode=$(stat -c '%a' "$installed_file")
    [ "$mode" = 644 ] ||
        fail "$installed_file has mode $mode; expected 644."

    cmp -s "$source_file" "$installed_file" ||
        fail "$installed_file differs from $source_file."
}

assert_installed_text() {
    installed_file=$1
    expected=$2

    grep -F -- "$expected" "$installed_file" >/dev/null ||
        fail "$installed_file is missing installed-path reference: $expected"
}

assert_absent() {
    path=$1
    if [ -e "$path" ] || [ -L "$path" ]; then
        fail "$path remains after uninstall."
    fi
}

assert_no_symlinks() {
    first_symlink=$(find "$stage_dir" -type l -print -quit)
    [ -z "$first_symlink" ] ||
        fail "staged tree contains a symbolic link: $first_symlink"
}

assert_unique_basename() {
    basename=$1
    expected_path=$2
    matches=$(find "$stage_dir" -type f -name "$basename" -print)
    [ "$matches" = "$expected_path" ] || {
        printf 'install-layout-test: unexpected locations for %s:\n%s\n' \
            "$basename" "$matches" >&2
        exit 1
    }
}

license_dir=$stage_dir/usr/share/licenses/jpacker
doc_dir=$stage_dir/usr/share/doc/jpacker

assert_compliance_install() {
    assert_installed_file "$repo_root/LICENSE" \
        "$license_dir/LICENSE"
    assert_installed_file "$repo_root/LICENSES/jpacker-MIT-legacy.txt" \
        "$license_dir/jpacker-MIT-legacy.txt"
    assert_installed_file "$repo_root/LICENSES/curl.txt" \
        "$license_dir/curl.txt"
    assert_installed_file "$repo_root/LICENSES/nlohmann-json-MIT.txt" \
        "$license_dir/nlohmann-json-MIT.txt"
    assert_installed_file "$repo_root/THIRD_PARTY_NOTICES.md" \
        "$doc_dir/THIRD_PARTY_NOTICES.md"
    assert_installed_file "$repo_root/docs/LICENSING.md" \
        "$doc_dir/LICENSING.md"

    # POLICY: Repository-relative links remain useful in source archives, while
    # installed copies must also identify the split doc/license layout.
    assert_installed_text "$doc_dir/LICENSING.md" \
        '/usr/share/licenses/jpacker/LICENSE'
    assert_installed_text "$doc_dir/LICENSING.md" \
        '/usr/share/licenses/jpacker/jpacker-MIT-legacy.txt'
    assert_installed_text "$doc_dir/LICENSING.md" \
        '/usr/share/doc/jpacker/THIRD_PARTY_NOTICES.md'
    assert_installed_text "$doc_dir/THIRD_PARTY_NOTICES.md" \
        '/usr/share/doc/jpacker/LICENSING.md'
    assert_installed_text "$doc_dir/THIRD_PARTY_NOTICES.md" \
        '/usr/share/licenses/jpacker/curl.txt'
    assert_installed_text "$doc_dir/THIRD_PARTY_NOTICES.md" \
        '/usr/share/licenses/jpacker/nlohmann-json-MIT.txt'

    assert_unique_basename LICENSE "$license_dir/LICENSE"
    assert_unique_basename jpacker-MIT-legacy.txt \
        "$license_dir/jpacker-MIT-legacy.txt"
    assert_unique_basename curl.txt "$license_dir/curl.txt"
    assert_unique_basename nlohmann-json-MIT.txt \
        "$license_dir/nlohmann-json-MIT.txt"
    assert_unique_basename THIRD_PARTY_NOTICES.md \
        "$doc_dir/THIRD_PARTY_NOTICES.md"
    assert_unique_basename LICENSING.md "$doc_dir/LICENSING.md"

    [ ! -e "$stage_dir/usr/local" ] && [ ! -L "$stage_dir/usr/local" ] ||
        fail "PREFIX=/usr install unexpectedly created /usr/local content."
    assert_no_symlinks
}

assert_compliance_absent() {
    assert_absent "$license_dir/LICENSE"
    assert_absent "$license_dir/jpacker-MIT-legacy.txt"
    assert_absent "$license_dir/curl.txt"
    assert_absent "$license_dir/nlohmann-json-MIT.txt"
    assert_absent "$doc_dir/THIRD_PARTY_NOTICES.md"
    assert_absent "$doc_dir/LICENSING.md"
}

# Phase 1: with no foreign files, uninstall removes only the now-empty jpacker directories.
run_make install
assert_compliance_install
run_make uninstall
assert_compliance_absent
assert_absent "$license_dir"
assert_absent "$doc_dir"
[ -d "$stage_dir/usr/share/licenses" ] ||
    fail "uninstall removed the shared license parent directory."
[ -d "$stage_dir/usr/share/doc" ] ||
    fail "uninstall removed the shared documentation parent directory."
assert_no_symlinks

# Phase 2: unknown files in jpacker-owned directories survive, and keep those directories non-empty.
run_make install
assert_compliance_install
printf '%s\n' 'license sentinel' > "$license_dir/foreign-file.keep"
printf '%s\n' 'documentation sentinel' > "$doc_dir/foreign-file.keep"
run_make uninstall
assert_compliance_absent

[ -f "$license_dir/foreign-file.keep" ] &&
    [ ! -L "$license_dir/foreign-file.keep" ] ||
    fail "uninstall removed or replaced the foreign license sentinel."
[ "$(cat "$license_dir/foreign-file.keep")" = "license sentinel" ] ||
    fail "uninstall changed the foreign license sentinel."
[ -f "$doc_dir/foreign-file.keep" ] &&
    [ ! -L "$doc_dir/foreign-file.keep" ] ||
    fail "uninstall removed or replaced the foreign documentation sentinel."
[ "$(cat "$doc_dir/foreign-file.keep")" = "documentation sentinel" ] ||
    fail "uninstall changed the foreign documentation sentinel."
assert_no_symlinks

printf 'install-layout-test: all checks passed\n'
