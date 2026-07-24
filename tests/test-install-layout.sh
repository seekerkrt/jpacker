#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
stage_root=$(mktemp -d)
stage_dir=

cleanup() {
    rm -rf "$stage_root"
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

set_stage() {
    stage_dir=$stage_root/$1
    binary_file=$stage_dir/usr/bin/jpacker
    config_dir=$stage_dir/etc/jpacker
    config_file=$config_dir/jpacker.conf
    preference_dir=$config_dir/package.build
    bash_completion_file=$stage_dir/usr/share/bash-completion/completions/jpacker
    zsh_completion_file=$stage_dir/usr/share/zsh/site-functions/_jpacker
    fish_completion_file=$stage_dir/usr/share/fish/vendor_completions.d/jpacker.fish
    man_file=$stage_dir/usr/share/man/man8/jpacker.8
    license_dir=$stage_dir/usr/share/licenses/jpacker
    doc_dir=$stage_dir/usr/share/doc/jpacker
}

assert_installed_file() {
    source_file=$1
    installed_file=$2
    expected_mode=${3:-644}

    [ -f "$installed_file" ] ||
        fail "$installed_file is missing or is not a regular file."
    [ ! -L "$installed_file" ] ||
        fail "$installed_file must not be a symbolic link."

    mode=$(stat -c '%a' "$installed_file")
    [ "$mode" = "$expected_mode" ] ||
        fail "$installed_file has mode $mode; expected $expected_mode."

    cmp -s "$source_file" "$installed_file" ||
        fail "$installed_file differs from $source_file."
}

assert_directory() {
    directory=$1

    [ -d "$directory" ] ||
        fail "$directory is missing or is not a directory."
    [ ! -L "$directory" ] ||
        fail "$directory must not be a symbolic link."
}

assert_file_text() {
    text_file=$1
    expected_text=$2

    [ -f "$text_file" ] ||
        fail "$text_file is missing or is not a regular file."
    [ ! -L "$text_file" ] ||
        fail "$text_file must not be a symbolic link."
    actual_text=$(cat "$text_file")
    [ "$actual_text" = "$expected_text" ] ||
        fail "$text_file content changed unexpectedly."
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
        fail "$path is present; expected it to be absent."
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

assert_package_artifacts_installed() {
    assert_installed_file "$repo_root/jpacker" "$binary_file" 755
    assert_installed_file "$repo_root/completions/jpacker_completion.bash" \
        "$bash_completion_file"
    assert_installed_file "$repo_root/completions/_jpacker" \
        "$zsh_completion_file"
    assert_installed_file "$repo_root/completions/jpacker.fish" \
        "$fish_completion_file"
    assert_installed_file "$repo_root/man/jpacker.8" "$man_file"
    assert_compliance_install
}

assert_package_artifacts_absent() {
    assert_absent "$binary_file"
    assert_absent "$bash_completion_file"
    assert_absent "$zsh_completion_file"
    assert_absent "$fish_completion_file"
    assert_absent "$man_file"
    assert_compliance_absent
}

# Phase 1: uninstall preserves user state while removing package-owned artifacts.
set_stage preserve-user-state
run_make install
assert_package_artifacts_installed
assert_installed_file "$repo_root/config/jpacker.conf" "$config_file"
assert_directory "$preference_dir"

preference_file=$preference_dir/fastfetch
preference_text='CFLAGS=-O3 -march=native'
printf '%s\n' "$preference_text" > "$preference_file"

# POLICY: reinstall may refresh the main config template, but must not touch
# runtime-managed source-build preference entries.
run_make install
assert_file_text "$preference_file" "$preference_text"

modified_config_text='NOEDIT=true
LOGFILE=/tmp/jpacker-preserved.log'
preference_sentinel=$preference_dir/foreign-file.keep
config_sentinel=$config_dir/foreign-file.keep
printf '%s\n' "$modified_config_text" > "$config_file"
printf '%s\n' 'preference sentinel' > "$preference_sentinel"
printf '%s\n' 'config sentinel' > "$config_sentinel"

run_make uninstall
assert_file_text "$preference_file" "$preference_text"
assert_file_text "$config_file" "$modified_config_text"
assert_file_text "$preference_sentinel" 'preference sentinel'
assert_file_text "$config_sentinel" 'config sentinel'
assert_directory "$preference_dir"
assert_directory "$config_dir"
assert_package_artifacts_absent
assert_absent "$license_dir"
assert_absent "$doc_dir"
assert_directory "$stage_dir/etc"
assert_directory "$stage_dir/usr/share/licenses"
assert_directory "$stage_dir/usr/share/doc"
assert_no_symlinks

# Phase 2: only empty jpacker-specific directories are removed; shared parents
# and foreign files in other package directories remain.
set_stage empty-config-directories
run_make install
assert_package_artifacts_installed
assert_installed_file "$repo_root/config/jpacker.conf" "$config_file"
assert_directory "$preference_dir"
printf '%s\n' 'license sentinel' > "$license_dir/foreign-file.keep"
printf '%s\n' 'documentation sentinel' > "$doc_dir/foreign-file.keep"
rm -f "$config_file"
assert_absent "$config_file"
[ -z "$(find "$preference_dir" -mindepth 1 -print -quit)" ] ||
    fail "$preference_dir is not empty before the empty-directory uninstall case."

run_make uninstall
assert_package_artifacts_absent
assert_absent "$preference_dir"
assert_absent "$config_dir"
assert_directory "$stage_dir/etc"

assert_file_text "$license_dir/foreign-file.keep" 'license sentinel'
assert_file_text "$doc_dir/foreign-file.keep" 'documentation sentinel'
assert_directory "$stage_dir/usr/share/licenses"
assert_directory "$stage_dir/usr/share/doc"
assert_no_symlinks

printf 'install-layout-test: all checks passed\n'
