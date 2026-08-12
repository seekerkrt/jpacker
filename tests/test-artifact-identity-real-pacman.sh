#!/bin/sh
set -eu

export LANG=C
export LC_ALL=C
unset LANGUAGE

fail() {
    printf 'artifact identity real-pacman test failed: %s\n' "$1" >&2
    exit 1
}

for required_command in /usr/bin/bsdtar /usr/bin/pacman /usr/bin/repo-add; do
    [ -x "$required_command" ] ||
        fail "required command is unavailable: $required_command"
done

[ "$(id -u)" -ne 0 ] || fail 'the private pacman fixture must run rootless'

test_root=$(mktemp -d)
cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

repository_dir=$test_root/repository
private_root=$test_root/root
private_db=$test_root/db
private_cache=$test_root/cache
private_gpg=$test_root/gpg
package_workspace=$test_root/packages
mkdir -p \
    "$repository_dir" \
    "$private_root" \
    "$private_db/local" \
    "$private_db/sync" \
    "$private_cache" \
    "$private_gpg" \
    "$package_workspace"

create_package_archive() {
    package_name=$1
    package_version=$2
    package_dependency=$3
    archive_path=$4
    package_root=$package_workspace/$package_name-$package_version

    mkdir -p "$package_root"
    {
        printf 'pkgname = %s\n' "$package_name"
        printf 'pkgbase = %s\n' "$package_name"
        printf 'xdata = pkgtype=pkg\n'
        printf 'pkgver = %s\n' "$package_version"
        printf 'pkgdesc = Moguet Issue 406 isolated regression fixture\n'
        printf 'url = https://example.invalid/moguet-issue-406\n'
        printf 'builddate = 1\n'
        printf 'packager = Moguet tests\n'
        printf 'size = 0\n'
        printf 'arch = any\n'
        printf 'license = MIT\n'
        if [ -n "$package_dependency" ]; then
            printf 'depend = %s\n' "$package_dependency"
        fi
    } > "$package_root/.PKGINFO"

    /usr/bin/bsdtar --zstd -cf "$archive_path" \
        -C "$package_root" .PKGINFO
}

repository_base=$repository_dir/obs-studio-32.2.1-6-any.pkg.tar.zst
local_base=$package_workspace/obs-studio-32.2.1-7-any.pkg.tar.zst
local_plugin=$package_workspace/obs-studio-plugin-browser-32.2.1-7-any.pkg.tar.zst
local_debug=$package_workspace/obs-studio-debug-32.2.1-7-any.pkg.tar.zst

create_package_archive 'obs-studio' '32.2.1-6' '' "$repository_base"
create_package_archive 'obs-studio' '32.2.1-7' '' "$local_base"
create_package_archive \
    'obs-studio-plugin-browser' '32.2.1-7' 'obs-studio' "$local_plugin"
create_package_archive \
    'obs-studio-debug' '32.2.1-7' 'obs-studio=32.2.1-7' "$local_debug"

/usr/bin/repo-add "$repository_dir/issue406.db.tar.zst" \
    "$repository_base" > "$test_root/repo-add.log"
cp "$repository_dir/issue406.db.tar.zst" "$private_db/sync/issue406.db"

pacman_config=$test_root/pacman.conf
{
    printf '[options]\n'
    printf 'RootDir = %s\n' "$private_root"
    printf 'DBPath = %s\n' "$private_db"
    printf 'CacheDir = %s\n' "$private_cache"
    printf 'GPGDir = %s\n' "$private_gpg"
    printf 'LogFile = %s\n' "$test_root/pacman.log"
    printf 'Architecture = auto\n'
    printf 'SigLevel = Never\n'
    printf 'LocalFileSigLevel = Never\n'
    printf '\n[issue406]\n'
    printf 'SigLevel = Never\n'
    printf 'Server = file://%s\n' "$repository_dir"
} > "$pacman_config"

# Control: the former transaction projection expands the unversioned dependency
# from the private repository and therefore emits two records.
transaction_format=$(printf '%s\t%s' '%n' '%v')
transaction_output=$test_root/transaction-output
if ! /usr/bin/pacman --config "$pacman_config" \
    -U --print --print-format "$transaction_format" -- \
    "$local_plugin" > "$transaction_output" 2> "$test_root/transaction-error"; then
    fail 'private-DB transaction projection did not reproduce the dependency expansion'
fi
expected_transaction_output=$test_root/expected-transaction-output
printf 'obs-studio\t32.2.1-6\nobs-studio-plugin-browser\t32.2.1-7\n' \
    > "$expected_transaction_output"
cmp -s "$expected_transaction_output" "$transaction_output" ||
    fail 'private-DB transaction projection did not emit the expected two records'

# Acceptance: each archive-only query is one record and reaches base, plugin,
# then debug without consulting transaction dependency resolution.
query_output=$test_root/query-output
query_paths=$test_root/query-paths
: > "$query_output"
: > "$query_paths"
for artifact_path in "$local_base" "$local_plugin" "$local_debug"; do
    single_output=$test_root/single-query-output
    if ! /usr/bin/pacman --config "$pacman_config" \
        -Qp --color never -- "$artifact_path" > "$single_output" \
        2> "$test_root/query-error"; then
        fail "archive-only identity query failed: $artifact_path"
    fi
    cat "$single_output" >> "$query_output"
    printf '%s\n' "$artifact_path" >> "$query_paths"
done

expected_query_output=$test_root/expected-query-output
printf 'obs-studio 32.2.1-7\nobs-studio-plugin-browser 32.2.1-7\nobs-studio-debug 32.2.1-7\n' \
    > "$expected_query_output"
cmp -s "$expected_query_output" "$query_output" ||
    fail 'archive-only base/plugin/debug identities differ'

expected_query_paths=$test_root/expected-query-paths
printf '%s\n%s\n%s\n' "$local_base" "$local_plugin" "$local_debug" \
    > "$expected_query_paths"
cmp -s "$expected_query_paths" "$query_paths" ||
    fail 'base/plugin/debug archives were not queried exactly once in order'

installed_entry=$(find "$private_db/local" \
    -mindepth 1 -maxdepth 1 -type d -print -quit)
[ -z "$installed_entry" ] ||
    fail 'the archive identity regression modified the private installed database'

printf '%s\n' 'artifact identity real-pacman test passed'
