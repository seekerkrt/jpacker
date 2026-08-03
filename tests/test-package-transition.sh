#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'package-transition-test: %s\n' "$*" >&2
    exit 1
}

assert_source_archive_input() {
    archive_label=$1
    archive_path=$2

    [ -f "$archive_path" ] && [ ! -L "$archive_path" ] ||
        fail "$archive_label source archive is missing, not regular, or a symlink: $archive_path"
    [ -r "$archive_path" ] ||
        fail "$archive_label source archive is not readable: $archive_path"
    bsdtar -tf "$archive_path" >/dev/null 2>&1 ||
        fail "$archive_label source archive is not a readable tar archive: $archive_path"
}

run_logged() {
    run_description=$1
    run_log=$2
    shift 2

    if ! "$@" >"$run_log" 2>&1; then
        printf 'package-transition-test: %s failed; log follows:\n' \
            "$run_description" >&2
        sed -n '1,240p' "$run_log" >&2
        exit 1
    fi
}

assert_absent() {
    absent_path=$1
    if [ -e "$absent_path" ] || [ -L "$absent_path" ]; then
        fail "$absent_path is present; expected it to be absent"
    fi
}

assert_text_file() {
    text_path=$1
    expected_text=$2

    [ -f "$text_path" ] && [ ! -L "$text_path" ] ||
        fail "$text_path is missing or is not a regular file"
    actual_text=$(cat "$text_path")
    [ "$actual_text" = "$expected_text" ] ||
        fail "$text_path changed unexpectedly"
}

assert_mode() {
    mode_path=$1
    expected_mode=$2
    actual_mode=$(stat -c '%a' "$mode_path")
    [ "$actual_mode" = "$expected_mode" ] ||
        fail "$mode_path has mode $actual_mode; expected $expected_mode"
}

write_regular_manifest() {
    manifest_root=$1
    manifest_output=$2

    (
        cd "$manifest_root"
        find . -type f ! -path './.*' -print | LC_ALL=C sort
    ) >"$manifest_output"
}

write_directory_manifest() {
    manifest_root=$1
    manifest_output=$2

    (
        cd "$manifest_root"
        find . -type d ! -path . ! -path './.*' -print | LC_ALL=C sort -r
    ) >"$manifest_output"
}

write_checksum_snapshot() {
    snapshot_root=$1
    snapshot_output=$2

    (
        cd "$snapshot_root"
        find . -type f -print | LC_ALL=C sort |
            while IFS= read -r snapshot_path; do
                sha256sum "$snapshot_path"
            done
    ) >"$snapshot_output"
}

assert_snapshot_matches() {
    snapshot_root=$1
    expected_snapshot=$2

    if ! (cd "$snapshot_root" && sha256sum -c "$expected_snapshot" >/dev/null); then
        fail "$snapshot_root no longer matches $expected_snapshot"
    fi
}

assert_snapshot_exact() {
    snapshot_root=$1
    expected_snapshot=$2
    actual_snapshot=$tmp_dir/actual.snapshot

    write_checksum_snapshot "$snapshot_root" "$actual_snapshot"
    cmp -s "$expected_snapshot" "$actual_snapshot" ||
        fail "$snapshot_root has unexpected added, removed, or changed files"
}

assert_manifest_files_match() {
    source_root=$1
    installed_root=$2
    expected_manifest=$3

    while IFS= read -r relative_path; do
        [ -n "$relative_path" ] || continue
        source_path=$source_root/$relative_path
        installed_path=$installed_root/$relative_path
        [ -f "$installed_path" ] && [ ! -L "$installed_path" ] ||
            fail "$installed_path is missing or is not a regular file"
        cmp -s "$source_path" "$installed_path" ||
            fail "$installed_path differs from its archived payload"
    done <"$expected_manifest"
}

assert_manifest_absent() {
    installed_root=$1
    removed_manifest=$2

    while IFS= read -r relative_path; do
        [ -n "$relative_path" ] || continue
        assert_absent "$installed_root/$relative_path"
    done <"$removed_manifest"
}

remove_package_payload() {
    installed_root=$1
    removed_manifest=$2
    removed_directories=$3

    while IFS= read -r relative_path; do
        [ -n "$relative_path" ] || continue
        case "$relative_path" in
            ./*) ;;
            *) fail "unsafe package manifest path: $relative_path" ;;
        esac
        rm -f "$installed_root/$relative_path"
    done <"$removed_manifest"

    while IFS= read -r relative_path; do
        [ -n "$relative_path" ] || continue
        case "$relative_path" in
            ./*) ;;
            *) fail "unsafe package directory path: $relative_path" ;;
        esac
        rmdir "$installed_root/$relative_path" 2>/dev/null || :
    done <"$removed_directories"
}

assert_no_symlinks() {
    symlink_root=$1
    first_symlink=$(find "$symlink_root" -type l -print -quit)
    [ -z "$first_symlink" ] ||
        fail "$symlink_root contains unexpected symlink $first_symlink"
}

metadata_values() {
    metadata_file=$1
    metadata_key=$2
    awk -v key="$metadata_key" \
        '$1 == key && $2 == "=" { $1 = ""; $2 = ""; sub(/^  */, ""); print }' \
        "$metadata_file"
}

assert_metadata_single() {
    metadata_file=$1
    metadata_key=$2
    expected_value=$3
    actual_value=$(metadata_values "$metadata_file" "$metadata_key")
    [ "$actual_value" = "$expected_value" ] ||
        fail "$metadata_key mismatch: expected '$expected_value', got '$actual_value'"
}

assert_metadata_set() {
    metadata_file=$1
    metadata_key=$2
    expected_values=$3
    actual_values=$(metadata_values "$metadata_file" "$metadata_key" |
        LC_ALL=C sort)
    [ "$actual_values" = "$expected_values" ] || {
        printf 'package-transition-test: %s mismatch\nexpected:\n%s\nactual:\n%s\n' \
            "$metadata_key" "$expected_values" "$actual_values" >&2
        exit 1
    }
}

assert_metadata_absent() {
    metadata_file=$1
    metadata_key=$2
    actual_values=$(metadata_values "$metadata_file" "$metadata_key")
    [ -z "$actual_values" ] ||
        fail "$metadata_key must be absent; found: $actual_values"
}

initialize_fixture_repository() {
    source_root=$1
    source_tag=$2

    git -C "$source_root" init -q
    git -C "$source_root" config core.hooksPath /dev/null
    git -C "$source_root" config commit.gpgSign false
    git -C "$source_root" config tag.gpgSign false
    git -C "$source_root" config user.name \
        'jpacker package transition fixture'
    git -C "$source_root" config user.email \
        'fixture.invalid@example.invalid'
    git -C "$source_root" add -- .
    git -C "$source_root" commit -q -m "fixture $source_tag"
    git -C "$source_root" tag "$source_tag"
}

prepare_test_pkgbuild() {
    production_pkgbuild=$1
    source_repository=$2
    package_work=$3
    production_source=$4

    fixture_source=git+file://$source_repository
    mkdir -p "$package_work"
    cp "$production_pkgbuild" "$package_work/PKGBUILD"
    cp "$(dirname "$production_pkgbuild")/VERSION" "$package_work/VERSION"

    source_occurrences=$(grep -Fo -- "$production_source" \
        "$package_work/PKGBUILD" | wc -l | tr -d '[:space:]')
    [ "$source_occurrences" = 1 ] ||
        fail "$production_pkgbuild must contain exactly one production source URL"
    sed -i "s|$production_source|$fixture_source|" \
        "$package_work/PKGBUILD"

    restored_pkgbuild=$package_work/PKGBUILD.restored
    sed "s|$fixture_source|$production_source|" \
        "$package_work/PKGBUILD" >"$restored_pkgbuild"
    cmp -s "$production_pkgbuild" "$restored_pkgbuild" ||
        fail "test PKGBUILD differs from production beyond its source URL"
    rm -f "$restored_pkgbuild"
}

run_makepkg_fixture() {
    package_work=$1
    build_root=$2
    source_cache=$3
    package_destination=$4
    source_package_destination=$5
    log_destination=$6
    xdg_cache=$7
    fixture_home=$build_root/home
    fixture_xdg_config=$build_root/xdg-config
    fixture_gnupg_home=$build_root/gnupg
    fixture_makepkg_config=$build_root/makepkg.conf

    mkdir -p "$fixture_home" "$fixture_xdg_config" "$xdg_cache"
    install -d -m700 "$fixture_gnupg_home"
    cp /etc/makepkg.conf "$fixture_makepkg_config"
    # Rolling Arch enables split debug packages by default. This fixture
    # validates the two production package archives, so disable only that
    # additional artifact while retaining the system compiler/tool settings.
    printf '\nOPTIONS+=(!debug)\n' >>"$fixture_makepkg_config"

    (
        cd "$package_work"
        env -u MAKEFLAGS -u MFLAGS \
            HOME="$fixture_home" \
            XDG_CONFIG_HOME="$fixture_xdg_config" \
            XDG_CACHE_HOME="$xdg_cache" \
            GNUPGHOME="$fixture_gnupg_home" \
            BUILDDIR="$build_root" \
            SRCDEST="$source_cache" \
            PKGDEST="$package_destination" \
            SRCPKGDEST="$source_package_destination" \
            LOGDEST="$log_destination" \
            CCACHE_DIR="$build_root/ccache" \
            CCACHE_TEMPDIR="$build_root/ccache-tmp" \
            makepkg --config "$fixture_makepkg_config" \
                --cleanbuild --force --noconfirm
    )
}

assert_archive_layout() {
    archive_path=$1
    archive_listing=$2
    executable_path=$3

    LC_ALL=C bsdtar --numeric-owner -tvf "$archive_path" >"$archive_listing"
    if ! awk '
        $3 != "0" || $4 != "0" {
            print "non-root archive entry: " $0
            bad = 1
        }
        END { exit bad }
    ' "$archive_listing" >"$tmp_dir/archive-owner-errors.txt"; then
        sed -n '1,120p' "$tmp_dir/archive-owner-errors.txt" >&2
        fail "$archive_path contains a non-root-owned entry"
    fi

    if ! awk -v executable_path="$executable_path" '
        {
            entry_type = substr($1, 1, 1)
            entry_path = $NF
            if (entry_type == "d") {
                expected_mode = "drwxr-xr-x"
            } else if (entry_type == "-") {
                expected_mode = entry_path == executable_path \
                    ? "-rwxr-xr-x" : "-rw-r--r--"
            } else {
                print "unexpected archive entry type: " $0
                bad = 1
                next
            }
            if ($1 != expected_mode) {
                print "unexpected archive mode: " $0 \
                    " (expected " expected_mode ")"
                bad = 1
            }
        }
        END { exit bad }
    ' "$archive_listing" >"$tmp_dir/archive-mode-errors.txt"; then
        sed -n '1,120p' "$tmp_dir/archive-mode-errors.txt" >&2
        fail "$archive_path contains an entry with an unexpected type or mode"
    fi
}

for required_command in bsdtar git makepkg sha256sum
do
    command -v "$required_command" >/dev/null 2>&1 ||
        fail "$required_command is required"
done

legacy_source_archive_input=${MOGUET_TEST_LEGACY_SOURCE_ARCHIVE-}
current_source_archive_input=${MOGUET_TEST_CURRENT_SOURCE_ARCHIVE-}
if [ -n "$legacy_source_archive_input" ]; then
    assert_source_archive_input legacy "$legacy_source_archive_input"
fi
if [ -n "$current_source_archive_input" ]; then
    assert_source_archive_input current "$current_source_archive_input"
fi

v1_source=$tmp_dir/jpacker-v1.16.0-source
v1_makepkg_work=$tmp_dir/jpacker-v1.16.0-makepkg
v1_package_destination=$tmp_dir/jpacker-v1.16.0-packages
v1_archive_root=$tmp_dir/jpacker-v1.16.0-archive-root
v1_manifest=$tmp_dir/jpacker-v1.16.0-files.txt
v1_directory_manifest=$tmp_dir/jpacker-v1.16.0-directories.txt
v1_removable_manifest=$tmp_dir/jpacker-v1.16.0-removable-files.txt

v2_source=$tmp_dir/moguet-v2.0.1-source
v2_source_manifest=$tmp_dir/moguet-v2.0.1-source-files.txt
v2_makepkg_work=$tmp_dir/moguet-v2.0.1-makepkg
v2_package_destination=$tmp_dir/moguet-v2.0.1-packages
v2_archive_root=$tmp_dir/moguet-v2.0.1-archive-root
v2_manifest=$tmp_dir/moguet-v2.0.1-files.txt
v2_directory_manifest=$tmp_dir/moguet-v2.0.1-directories.txt

coinstall_root=$tmp_dir/coinstall-root
transition_root=$tmp_dir/transition-root

current_version=$(tr -d '[:space:]' <"$repo_root/VERSION")
[ "$current_version" = 2.0.1 ] ||
    fail "current VERSION is $current_version; expected 2.0.1"

if [ -n "$legacy_source_archive_input" ]; then
    v1_source_archive=$legacy_source_archive_input
else
    v1_source_archive=$tmp_dir/jpacker-v1.16.0-source.tar
    git -C "$repo_root" rev-parse --verify 'refs/tags/v1.16.0^{commit}' \
        >/dev/null || fail 'local tag v1.16.0 is unavailable'
    git -C "$repo_root" archive --format=tar \
        --output="$v1_source_archive" v1.16.0
fi
mkdir -p "$v1_source"
bsdtar -xf "$v1_source_archive" -C "$v1_source"

legacy_version=$(tr -d '[:space:]' <"$v1_source/VERSION")
[ "$legacy_version" = 1.16.0 ] ||
    fail "v1.16.0 archive reports VERSION=$legacy_version"
initialize_fixture_repository "$v1_source" v1.16.0
prepare_test_pkgbuild "$v1_source/PKGBUILD" "$v1_source" \
    "$v1_makepkg_work" \
    'git+https://github.com/seekerkrt/jpacker.git'

mkdir -p "$v2_source"
if [ -n "$current_source_archive_input" ]; then
    bsdtar -xf "$current_source_archive_input" -C "$v2_source"
else
    # Preserve the issue branch's dirty edits and tracked deletions while
    # keeping .git, ignored build output, binaries, and package artifacts out.
    git -C "$repo_root" ls-files --cached --others --exclude-standard |
        while IFS= read -r source_path; do
            case "$source_path" in
                .git|.git/*|build|build/*|moguet|*.pkg.tar.*|*.src.tar.*)
                    continue
                    ;;
            esac
            if [ -f "$repo_root/$source_path" ] ||
                [ -L "$repo_root/$source_path" ]; then
                printf '%s\n' "$source_path"
            fi
        done >"$v2_source_manifest"
    [ -s "$v2_source_manifest" ] || fail 'current source manifest is empty'
    while IFS= read -r source_path; do
        mkdir -p "$v2_source/$(dirname "$source_path")"
        cp -a "$repo_root/$source_path" "$v2_source/$source_path"
    done <"$v2_source_manifest"
fi

assert_absent "$v2_source/.git"
assert_absent "$v2_source/build"
assert_absent "$v2_source/moguet"
assert_absent "$v2_source/config/jpacker.conf"
cmp -s "$repo_root/tests/test-package-transition.sh" \
    "$v2_source/tests/test-package-transition.sh" ||
    fail 'dirty working-tree transition test was not copied into v2 source'
first_package_artifact=$(find "$v2_source" -type f \
    \( -name '*.pkg.tar.*' -o -name '*.src.tar.*' \) -print -quit)
[ -z "$first_package_artifact" ] ||
    fail "source fixture contains package artifact $first_package_artifact"

initialize_fixture_repository "$v2_source" v2.0.1
prepare_test_pkgbuild "$repo_root/PKGBUILD" "$v2_source" \
    "$v2_makepkg_work" \
    'git+https://github.com/seekerkrt/moguet.git'

mkdir -p \
    "$v1_package_destination" \
    "$v2_package_destination" \
    "$v1_archive_root" \
    "$v2_archive_root"
run_logged 'jpacker v1.16.0 clean package build' "$tmp_dir/v1-makepkg.log" \
    run_makepkg_fixture \
        "$v1_makepkg_work" \
        "$tmp_dir/v1-build" \
        "$tmp_dir/v1-sources" \
        "$v1_package_destination" \
        "$tmp_dir/v1-source-packages" \
        "$tmp_dir/v1-logs" \
        "$tmp_dir/v1-xdg-cache"
run_logged 'Moguet v2.0.1 clean package build' "$tmp_dir/v2-makepkg.log" \
    run_makepkg_fixture \
        "$v2_makepkg_work" \
        "$tmp_dir/v2-build" \
        "$tmp_dir/v2-sources" \
        "$v2_package_destination" \
        "$tmp_dir/v2-source-packages" \
        "$tmp_dir/v2-logs" \
        "$tmp_dir/v2-xdg-cache"

v1_package_archive=$v1_package_destination/jpacker-1.16.0-1-x86_64.pkg.tar.zst
v2_package_archive=$v2_package_destination/moguet-2.0.1-1-x86_64.pkg.tar.zst
[ -f "$v1_package_archive" ] ||
    fail "expected package archive is missing: $v1_package_archive"
[ -f "$v2_package_archive" ] ||
    fail "expected package archive is missing: $v2_package_archive"
[ "$(find "$v1_package_destination" -maxdepth 1 -type f \
    -name '*.pkg.tar.*' | wc -l | tr -d '[:space:]')" = 1 ] ||
    fail 'v1 package build produced an unexpected archive set'
[ "$(find "$v2_package_destination" -maxdepth 1 -type f \
    -name '*.pkg.tar.*' | wc -l | tr -d '[:space:]')" = 1 ] ||
    fail 'v2 package build produced an unexpected archive set'
printf '%s  %s\n' \
    "$(sha256sum "$v1_package_archive" | awk '{ print $1 }')" \
    "$(basename "$v1_package_archive")" >"$tmp_dir/v1-package.sha256"
printf '%s  %s\n' \
    "$(sha256sum "$v2_package_archive" | awk '{ print $1 }')" \
    "$(basename "$v2_package_archive")" >"$tmp_dir/v2-package.sha256"

bsdtar -xOf "$v1_package_archive" .PKGINFO >"$tmp_dir/v1.PKGINFO"
bsdtar -xOf "$v2_package_archive" .PKGINFO >"$tmp_dir/v2.PKGINFO"
assert_metadata_single "$tmp_dir/v1.PKGINFO" pkgname jpacker
assert_metadata_single "$tmp_dir/v1.PKGINFO" pkgver 1.16.0-1
assert_metadata_single "$tmp_dir/v1.PKGINFO" arch x86_64
assert_metadata_single "$tmp_dir/v1.PKGINFO" license GPL-3.0-or-later
assert_metadata_single "$tmp_dir/v1.PKGINFO" backup \
    etc/jpacker/jpacker.conf

assert_metadata_single "$tmp_dir/v2.PKGINFO" pkgname moguet
assert_metadata_single "$tmp_dir/v2.PKGINFO" pkgver 2.0.1-1
assert_metadata_single "$tmp_dir/v2.PKGINFO" arch x86_64
assert_metadata_single "$tmp_dir/v2.PKGINFO" license GPL-3.0-or-later
for transition_key in backup conflict conflicts provides replaces
do
    assert_metadata_absent "$tmp_dir/v2.PKGINFO" "$transition_key"
done

actual_libalpm_dependency=$(metadata_values "$tmp_dir/v2.PKGINFO" depend |
    grep -E '^libalpm\.so(=[0-9]+-[0-9]+)?$' || :)
[ -n "$actual_libalpm_dependency" ] &&
    [ "$(printf '%s\n' "$actual_libalpm_dependency" | wc -l | tr -d '[:space:]')" = 1 ] ||
    fail 'v2 package has an invalid libalpm soname dependency'
normalized_dependencies=$(metadata_values "$tmp_dir/v2.PKGINFO" depend |
    sed -E 's/^libalpm\.so(=[0-9]+-[0-9]+)?$/libalpm.so/' |
    LC_ALL=C sort)
expected_dependencies='curl
git
libalpm.so
libarchive
nano
pacman
sudo'
[ "$normalized_dependencies" = "$expected_dependencies" ] || {
    printf 'package-transition-test: depend mismatch\nexpected:\n%s\nactual:\n%s\n' \
        "$expected_dependencies" "$normalized_dependencies" >&2
    exit 1
}
expected_makedepends='nlohmann-json
tomlplusplus'
assert_metadata_set "$tmp_dir/v2.PKGINFO" makedepend "$expected_makedepends"

assert_archive_layout "$v1_package_archive" \
    "$tmp_dir/v1-archive-listing.txt" usr/bin/jpacker
assert_archive_layout "$v2_package_archive" \
    "$tmp_dir/v2-archive-listing.txt" usr/bin/moguet
bsdtar -xf "$v1_package_archive" -C "$v1_archive_root"
bsdtar -xf "$v2_package_archive" -C "$v2_archive_root"
assert_no_symlinks "$v1_archive_root"
assert_no_symlinks "$v2_archive_root"
write_regular_manifest "$v1_archive_root" "$v1_manifest"
write_directory_manifest "$v1_archive_root" "$v1_directory_manifest"
write_regular_manifest "$v2_archive_root" "$v2_manifest"
write_directory_manifest "$v2_archive_root" "$v2_directory_manifest"

expected_v2_payload='./usr/bin/moguet
./usr/share/bash-completion/completions/moguet
./usr/share/doc/moguet/README.ja.md
./usr/share/doc/moguet/README.md
./usr/share/doc/moguet/THIRD_PARTY_NOTICES.md
./usr/share/doc/moguet/docs/LICENSING.md
./usr/share/doc/moguet/docs/migration/v1-to-v2.ja.md
./usr/share/doc/moguet/docs/migration/v1-to-v2.md
./usr/share/fish/vendor_completions.d/moguet.fish
./usr/share/licenses/moguet/LICENSE
./usr/share/licenses/moguet/bjoern-hoehrmann-utf8-MIT.txt
./usr/share/licenses/moguet/curl.txt
./usr/share/licenses/moguet/jpacker-MIT-legacy.txt
./usr/share/licenses/moguet/nlohmann-json-MIT.txt
./usr/share/licenses/moguet/tomlplusplus-MIT.txt
./usr/share/locale/ja/LC_MESSAGES/moguet.mo
./usr/share/man/ja/man1/moguet.1.gz
./usr/share/man/man1/moguet.1.gz
./usr/share/zsh/site-functions/_moguet'
[ "$(cat "$v2_manifest")" = "$expected_v2_payload" ] || {
    printf 'package-transition-test: package payload mismatch:\n%s\n' \
        "$(cat "$v2_manifest")" >&2
    exit 1
}

archive_binary_mode=$(stat -c '%a' "$v2_archive_root/usr/bin/moguet")
[ "$archive_binary_mode" = 755 ] ||
    fail "archived Moguet binary mode is $archive_binary_mode; expected 755"
archive_home=$tmp_dir/archive-home
archive_config_home=$tmp_dir/archive-xdg/config
archive_state_home=$tmp_dir/archive-xdg/state
archive_cache_home=$tmp_dir/archive-xdg/cache
archive_version=$(LC_ALL=C \
    HOME="$archive_home" \
    XDG_CONFIG_HOME="$archive_config_home" \
    XDG_STATE_HOME="$archive_state_home" \
    XDG_CACHE_HOME="$archive_cache_home" \
    "$v2_archive_root/usr/bin/moguet" --version)
[ "$archive_version" = 'Moguet v2.0.1' ] ||
    fail "archived Moguet version mismatch: $archive_version"
assert_absent "$archive_config_home"
assert_absent "$archive_state_home"
assert_absent "$archive_cache_home"

file_conflicts=$tmp_dir/regular-file-conflicts.txt
LC_ALL=C comm -12 "$v1_manifest" "$v2_manifest" >"$file_conflicts"
[ ! -s "$file_conflicts" ] || {
    printf 'package-transition-test: v1/v2 regular-file conflicts:\n' >&2
    sed -n '1,240p' "$file_conflicts" >&2
    exit 1
}
type_conflicts=$tmp_dir/file-directory-conflicts.txt
v1_directories_sorted=$tmp_dir/v1-directories-sorted.txt
v2_directories_sorted=$tmp_dir/v2-directories-sorted.txt
LC_ALL=C sort "$v1_directory_manifest" >"$v1_directories_sorted"
LC_ALL=C sort "$v2_directory_manifest" >"$v2_directories_sorted"
{
    LC_ALL=C comm -12 "$v1_manifest" "$v2_directories_sorted"
    LC_ALL=C comm -12 "$v2_manifest" "$v1_directories_sorted"
} >"$type_conflicts"
[ ! -s "$type_conflicts" ] || {
    printf 'package-transition-test: v1/v2 file-directory conflicts:\n' >&2
    sed -n '1,240p' "$type_conflicts" >&2
    exit 1
}

# Moguet provides no legacy command alias and owns no legacy/system config.
assert_absent "$v2_archive_root/usr/bin/jpacker"
assert_absent "$v2_archive_root/usr/share/bash-completion/completions/jpacker"
assert_absent "$v2_archive_root/usr/share/zsh/site-functions/_jpacker"
assert_absent "$v2_archive_root/usr/share/fish/vendor_completions.d/jpacker.fish"
assert_absent "$v2_archive_root/usr/share/man/man8/jpacker.8"
assert_absent "$v2_archive_root/usr/share/man/man8/jpacker.8.gz"
assert_absent "$v2_archive_root/etc"
assert_absent "$v2_archive_root/home"

# The legacy config is user-modifiable package data. package.build entries and
# paths outside either manifest model runtime-managed and foreign data.
mkdir -p "$coinstall_root"
bsdtar -xf "$v1_package_archive" -C "$coinstall_root" etc usr
modified_legacy_config='NOEDIT=true
NODIFF=true'
legacy_preference='CFLAGS=-O3 -march=native'
coinstall_config_dir=$coinstall_root/user-home/.config/moguet
coinstall_source_preference_dir=$coinstall_config_dir/source-build.d
coinstall_source_preference=$coinstall_source_preference_dir/fastfetch
mkdir -p \
    "$coinstall_root/etc/jpacker/package.build" \
    "$coinstall_config_dir" \
    "$coinstall_root/user-home/.local/state/moguet" \
    "$coinstall_root/user-home/.cache/moguet" \
    "$coinstall_root/usr/share/doc/moguet" \
    "$coinstall_root/usr/share/locale/ja/LC_MESSAGES"
printf '%s\n' "$modified_legacy_config" \
    >"$coinstall_root/etc/jpacker/jpacker.conf"
printf '%s\n' "$legacy_preference" \
    >"$coinstall_root/etc/jpacker/package.build/fastfetch"
printf '%s\n' 'legacy foreign data' \
    >"$coinstall_root/etc/jpacker/foreign-file.keep"
printf '%s\n' 'schema_version = 1' \
    >"$coinstall_config_dir/config.toml"
install -d -m700 "$coinstall_source_preference_dir"
install -m600 /dev/null "$coinstall_source_preference"
printf '%s\n' 'CFLAGS=-O2 -pipe' >"$coinstall_source_preference"
printf '%s\n' 'persistent state' \
    >"$coinstall_root/user-home/.local/state/moguet/state.keep"
printf '%s\n' 'reproducible cache fixture' \
    >"$coinstall_root/user-home/.cache/moguet/cache.keep"
printf '%s\n' 'foreign documentation' \
    >"$coinstall_root/usr/share/doc/moguet/foreign-file.keep"
printf '%s\n' 'foreign locale catalog' \
    >"$coinstall_root/usr/share/locale/ja/LC_MESSAGES/foreign-domain.mo"

coinstall_baseline=$tmp_dir/coinstall-baseline.sha256
write_checksum_snapshot "$coinstall_root" "$coinstall_baseline"

# The two actual package archives coinstall without overwriting v1 or user
# data. No host pacman database, /etc tree, user XDG tree, or sudo is involved.
bsdtar -xf "$v2_package_archive" -C "$coinstall_root" usr
assert_snapshot_matches "$coinstall_root" "$coinstall_baseline"
assert_manifest_files_match "$v2_archive_root" "$coinstall_root" "$v2_manifest"

remove_package_payload "$coinstall_root" "$v2_manifest" \
    "$v2_directory_manifest"
assert_manifest_absent "$coinstall_root" "$v2_manifest"
assert_snapshot_exact "$coinstall_root" "$coinstall_baseline"
assert_text_file "$coinstall_root/etc/jpacker/jpacker.conf" \
    "$modified_legacy_config"
assert_text_file "$coinstall_root/etc/jpacker/package.build/fastfetch" \
    "$legacy_preference"
assert_text_file "$coinstall_source_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$coinstall_source_preference_dir" 700
assert_mode "$coinstall_source_preference" 600

# Reinstalling the same archive restores only its exact payload and preserves
# the v1 package, legacy data, user XDG data, and foreign files byte-for-byte.
bsdtar -xf "$v2_package_archive" -C "$coinstall_root" usr
assert_snapshot_matches "$coinstall_root" "$coinstall_baseline"
assert_manifest_files_match "$v2_archive_root" "$coinstall_root" "$v2_manifest"
assert_mode "$coinstall_source_preference_dir" 700
assert_mode "$coinstall_source_preference" 600

# Removing v1 follows its .PKGINFO backup contract: a modified package-owned
# config becomes .pacsave, while runtime-managed and foreign legacy data stays.
mkdir -p "$transition_root"
bsdtar -xf "$v1_package_archive" -C "$transition_root" etc usr
transition_config_dir=$transition_root/user-home/.config/moguet
transition_source_preference_dir=$transition_config_dir/source-build.d
transition_source_preference=$transition_source_preference_dir/fastfetch
mkdir -p \
    "$transition_root/etc/jpacker/package.build" \
    "$transition_config_dir" \
    "$transition_root/user-home/.local/state/moguet" \
    "$transition_root/user-home/.cache/moguet"
printf '%s\n' "$modified_legacy_config" \
    >"$transition_root/etc/jpacker/jpacker.conf"
printf '%s\n' "$legacy_preference" \
    >"$transition_root/etc/jpacker/package.build/fastfetch"
printf '%s\n' 'legacy transition sentinel' \
    >"$transition_root/etc/jpacker/foreign-file.keep"
printf '%s\n' 'schema_version = 1' \
    >"$transition_config_dir/config.toml"
install -d -m700 "$transition_source_preference_dir"
install -m600 /dev/null "$transition_source_preference"
printf '%s\n' 'CFLAGS=-O2 -pipe' >"$transition_source_preference"
printf '%s\n' 'persistent transition state' \
    >"$transition_root/user-home/.local/state/moguet/state.keep"
printf '%s\n' 'transition cache fixture' \
    >"$transition_root/user-home/.cache/moguet/cache.keep"

expected_legacy_after_removal=$tmp_dir/expected-legacy-after-removal
mkdir -p "$expected_legacy_after_removal"
cp -a "$transition_root/etc/jpacker/." "$expected_legacy_after_removal/"
mv "$expected_legacy_after_removal/jpacker.conf" \
    "$expected_legacy_after_removal/jpacker.conf.pacsave"
legacy_transition_snapshot=$tmp_dir/legacy-transition.sha256
xdg_transition_snapshot=$tmp_dir/xdg-transition.sha256
write_checksum_snapshot "$expected_legacy_after_removal" \
    "$legacy_transition_snapshot"
write_checksum_snapshot "$transition_root/user-home" "$xdg_transition_snapshot"
grep -Fvx './etc/jpacker/jpacker.conf' "$v1_manifest" \
    >"$v1_removable_manifest"

mv "$transition_root/etc/jpacker/jpacker.conf" \
    "$transition_root/etc/jpacker/jpacker.conf.pacsave"
remove_package_payload "$transition_root" "$v1_manifest" \
    "$v1_directory_manifest"
assert_manifest_absent "$transition_root" "$v1_manifest"
assert_snapshot_exact "$transition_root/etc/jpacker" \
    "$legacy_transition_snapshot"
assert_snapshot_exact "$transition_root/user-home" "$xdg_transition_snapshot"
assert_text_file "$transition_root/etc/jpacker/jpacker.conf.pacsave" \
    "$modified_legacy_config"
assert_text_file "$transition_source_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$transition_source_preference_dir" 700
assert_mode "$transition_source_preference" 600

bsdtar -xf "$v2_package_archive" -C "$transition_root" usr
assert_manifest_files_match "$v2_archive_root" "$transition_root" "$v2_manifest"
assert_snapshot_exact "$transition_root/etc/jpacker" \
    "$legacy_transition_snapshot"
assert_snapshot_exact "$transition_root/user-home" "$xdg_transition_snapshot"
assert_mode "$transition_source_preference_dir" 700
assert_mode "$transition_source_preference" 600

# Rollback removes only archive-owned Moguet files, reinstalls the trusted v1
# archive, then manually restores its pacsaved config over the package default.
remove_package_payload "$transition_root" "$v2_manifest" \
    "$v2_directory_manifest"
assert_manifest_absent "$transition_root" "$v2_manifest"
bsdtar -xf "$v1_package_archive" -C "$transition_root" etc usr
cp "$transition_root/etc/jpacker/jpacker.conf.pacsave" \
    "$transition_root/etc/jpacker/jpacker.conf"

assert_manifest_files_match "$v1_archive_root" "$transition_root" \
    "$v1_removable_manifest"
assert_manifest_absent "$transition_root" "$v2_manifest"
assert_snapshot_exact "$transition_root/user-home" "$xdg_transition_snapshot"
assert_text_file "$transition_root/etc/jpacker/jpacker.conf" \
    "$modified_legacy_config"
assert_text_file "$transition_root/etc/jpacker/jpacker.conf.pacsave" \
    "$modified_legacy_config"
assert_text_file "$transition_root/etc/jpacker/package.build/fastfetch" \
    "$legacy_preference"
assert_text_file "$transition_root/etc/jpacker/foreign-file.keep" \
    'legacy transition sentinel'
assert_text_file "$transition_source_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$transition_source_preference_dir" 700
assert_mode "$transition_source_preference" 600

rollback_version=$(LC_ALL=C \
    HOME="$tmp_dir/rollback-home" \
    "$transition_root/usr/bin/jpacker" --version)
[ "$rollback_version" = 'jpacker v1.16.0' ] ||
    fail "rollback jpacker version mismatch: $rollback_version"

(cd "$v1_package_destination" && \
    sha256sum -c "$tmp_dir/v1-package.sha256" >/dev/null) ||
    fail 'v1 package archive changed during transition validation'
(cd "$v2_package_destination" && \
    sha256sum -c "$tmp_dir/v2-package.sha256" >/dev/null) ||
    fail 'v2 package archive changed during transition validation'

printf 'package-transition-test: actual v1/v2 package archives have 0 path conflicts\n'
printf 'package-transition-test: v2 .PKGINFO identity/dependencies and transition metadata passed\n'
printf 'package-transition-test: archive owners, modes, and entry types passed\n'
printf 'package-transition-test: Moguet archive payload (19 regular files):\n'
sed 's|^\./|/|' "$v2_manifest"
printf 'package-transition-test: all checks passed\n'
