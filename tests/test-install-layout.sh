#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
. "$repo_root/scripts/validation-status.sh"
stage_root=$(mktemp -d)
stage_dir=$stage_root/root
test_home=$stage_dir/home/test-user
xdg_config_home=$test_home/.config
xdg_state_home=$test_home/.local/state
xdg_cache_home=$test_home/.cache

cleanup() {
    rm -rf "$stage_root"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'install-layout-test: %s\n' "$*" >&2
    exit 1
}

run_make() {
    HOME=$test_home \
    XDG_CONFIG_HOME=$xdg_config_home \
    XDG_STATE_HOME=$xdg_state_home \
    XDG_CACHE_HOME=$xdg_cache_home \
        make -C "$repo_root" --no-print-directory \
            PREFIX=/usr DESTDIR="$stage_dir" "$@"
}

binary_file=$stage_dir/usr/bin/moguet
legacy_binary_file=$stage_dir/usr/bin/jpacker
bash_completion_file=$stage_dir/usr/share/bash-completion/completions/moguet
zsh_completion_file=$stage_dir/usr/share/zsh/site-functions/_moguet
fish_completion_file=$stage_dir/usr/share/fish/vendor_completions.d/moguet.fish
english_man_file=$stage_dir/usr/share/man/man1/moguet.1
japanese_man_file=$stage_dir/usr/share/man/ja/man1/moguet.1
catalog_file=$stage_dir/usr/share/locale/ja/LC_MESSAGES/moguet.mo
built_catalog_file=$repo_root/build/locale/ja/LC_MESSAGES/moguet.mo
license_dir=$stage_dir/usr/share/licenses/moguet
doc_dir=$stage_dir/usr/share/doc/moguet
migration_dir=$doc_dir/docs/migration
licensing_file=$doc_dir/docs/LICENSING.md

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

assert_file_text() {
    text_file=$1
    expected_text=$2

    [ -f "$text_file" ] && [ ! -L "$text_file" ] ||
        fail "$text_file is missing, not regular, or a symbolic link."
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

assert_directory() {
    directory=$1
    [ -d "$directory" ] && [ ! -L "$directory" ] ||
        fail "$directory is missing, not a directory, or a symbolic link."
}

assert_mode() {
    mode_path=$1
    expected_mode=$2
    actual_mode=$(stat -c '%a' "$mode_path")
    [ "$actual_mode" = "$expected_mode" ] ||
        fail "$mode_path has mode $actual_mode; expected $expected_mode."
}

assert_no_symlinks() {
    first_symlink=$(find "$stage_dir" -type l -print -quit)
    [ -z "$first_symlink" ] ||
        fail "staged tree contains a symbolic link: $first_symlink"
}

assert_exact_payload() {
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
    payload_paths_raw=$stage_root/payload-paths.raw
    payload_paths_normalized=$stage_root/payload-paths.normalized
    payload_paths_sorted=$stage_root/payload-paths.sorted
    if validation_capture_output "$payload_paths_raw" \
        find "$stage_dir" -type f -print; then
        :
    else
        payload_status=$?
        fail "payload path producer failed with status $payload_status; raw=$payload_paths_raw"
    fi
    if sed "s|^$stage_dir||" "$payload_paths_raw" \
        >"$payload_paths_normalized"; then
        :
    else
        payload_status=$?
        fail "payload path normalization failed with status $payload_status"
    fi
    if LC_ALL=C sort "$payload_paths_normalized" >"$payload_paths_sorted"; then
        actual_payload=$(cat "$payload_paths_sorted")
    else
        payload_status=$?
        fail "payload path sorting failed with status $payload_status"
    fi
    [ "$actual_payload" = "$expected_payload" ] || {
        printf 'install-layout-test: unexpected payload:\n%s\n' \
            "$actual_payload" >&2
        exit 1
    }
}

assert_installed_markdown_links() {
    if ! python3 - "$doc_dir" <<'PY'
import pathlib
import re
import sys
import urllib.parse

doc_root = pathlib.Path(sys.argv[1]).resolve()
link_pattern = re.compile(r"\[[^]]*\]\(([^)]+)\)")

for markdown_file in sorted(doc_root.rglob("*.md")):
    text = markdown_file.read_text(encoding="utf-8")
    for match in link_pattern.finditer(text):
        raw_target = match.group(1).strip()
        if not raw_target or raw_target.startswith("#"):
            continue
        parsed = urllib.parse.urlparse(raw_target)
        if parsed.scheme or raw_target.startswith("/"):
            continue
        relative_target = urllib.parse.unquote(parsed.path)
        resolved_target = (markdown_file.parent / relative_target).resolve()
        try:
            resolved_target.relative_to(doc_root)
        except ValueError:
            print(
                f"{markdown_file}: relative link leaves packaged doc root: "
                f"{raw_target}",
                file=sys.stderr,
            )
            raise SystemExit(1)
        if not resolved_target.is_file():
            print(
                f"{markdown_file}: missing packaged relative-link target: "
                f"{raw_target}",
                file=sys.stderr,
            )
            raise SystemExit(1)
PY
    then
        fail 'installed Markdown contains an unresolved relative link.'
    fi
}

assert_package_artifacts_installed() {
    assert_installed_file "$repo_root/moguet" "$binary_file" 755
    assert_absent "$legacy_binary_file"
    assert_installed_file "$repo_root/completions/moguet.bash" \
        "$bash_completion_file"
    assert_installed_file "$repo_root/completions/_moguet" \
        "$zsh_completion_file"
    assert_installed_file "$repo_root/completions/moguet.fish" \
        "$fish_completion_file"
    assert_installed_file "$repo_root/man/moguet.1" "$english_man_file"
    assert_installed_file "$repo_root/man/ja/moguet.1" "$japanese_man_file"
    assert_installed_file "$built_catalog_file" "$catalog_file"

    assert_installed_file "$repo_root/LICENSE" "$license_dir/LICENSE"
    assert_installed_file "$repo_root/LICENSES/jpacker-MIT-legacy.txt" \
        "$license_dir/jpacker-MIT-legacy.txt"
    assert_installed_file "$repo_root/LICENSES/curl.txt" \
        "$license_dir/curl.txt"
    assert_installed_file "$repo_root/LICENSES/nlohmann-json-MIT.txt" \
        "$license_dir/nlohmann-json-MIT.txt"
    assert_installed_file "$repo_root/LICENSES/tomlplusplus-MIT.txt" \
        "$license_dir/tomlplusplus-MIT.txt"
    assert_installed_file "$repo_root/LICENSES/bjoern-hoehrmann-utf8-MIT.txt" \
        "$license_dir/bjoern-hoehrmann-utf8-MIT.txt"

    assert_installed_file "$repo_root/README.md" "$doc_dir/README.md"
    assert_installed_file "$repo_root/README.ja.md" "$doc_dir/README.ja.md"
    assert_installed_file "$repo_root/THIRD_PARTY_NOTICES.md" \
        "$doc_dir/THIRD_PARTY_NOTICES.md"
    assert_installed_file "$repo_root/docs/LICENSING.md" \
        "$licensing_file"
    assert_installed_file "$repo_root/docs/migration/v1-to-v2.md" \
        "$migration_dir/v1-to-v2.md"
    assert_installed_file "$repo_root/docs/migration/v1-to-v2.ja.md" \
        "$migration_dir/v1-to-v2.ja.md"

    assert_installed_text "$licensing_file" \
        '/usr/share/licenses/moguet/LICENSE'
    assert_installed_text "$doc_dir/THIRD_PARTY_NOTICES.md" \
        '/usr/share/licenses/moguet/tomlplusplus-MIT.txt'
    assert_installed_text "$doc_dir/THIRD_PARTY_NOTICES.md" \
        '/usr/share/doc/moguet/docs/LICENSING.md'
    assert_installed_markdown_links
    assert_no_symlinks
}

assert_package_artifacts_absent() {
    for path in \
        "$binary_file" \
        "$legacy_binary_file" \
        "$bash_completion_file" \
        "$zsh_completion_file" \
        "$fish_completion_file" \
        "$english_man_file" \
        "$japanese_man_file" \
        "$catalog_file" \
        "$license_dir/LICENSE" \
        "$license_dir/jpacker-MIT-legacy.txt" \
        "$license_dir/curl.txt" \
        "$license_dir/nlohmann-json-MIT.txt" \
        "$license_dir/tomlplusplus-MIT.txt" \
        "$license_dir/bjoern-hoehrmann-utf8-MIT.txt" \
        "$doc_dir/README.md" \
        "$doc_dir/README.ja.md" \
        "$doc_dir/THIRD_PARTY_NOTICES.md" \
        "$licensing_file" \
        "$migration_dir/v1-to-v2.md" \
        "$migration_dir/v1-to-v2.ja.md"
    do
        assert_absent "$path"
    done
}

# Fresh install: exact Moguet payload only, no legacy alias, /etc content, or
# user XDG data. Version execution is also read-only with respect to XDG.
run_make install
assert_package_artifacts_installed
assert_exact_payload
assert_absent "$stage_dir/etc"
assert_absent "$xdg_config_home/moguet"
assert_absent "$xdg_state_home/moguet"
assert_absent "$xdg_cache_home/moguet"
version_output=$(LC_ALL=C HOME=$test_home \
    XDG_CONFIG_HOME=$xdg_config_home \
    XDG_STATE_HOME=$xdg_state_home \
    XDG_CACHE_HOME=$xdg_cache_home \
    "$binary_file" --version)
[ "$version_output" = 'Moguet v2.2.0' ] ||
    fail "staged binary version mismatch: $version_output"
assert_absent "$xdg_config_home/moguet"
assert_absent "$xdg_state_home/moguet"
assert_absent "$xdg_cache_home/moguet"

# Reinstall refreshes only package-owned files and leaves foreign/user/legacy
# data untouched. These sentinels also cover uninstall preservation.
legacy_config=$stage_dir/etc/jpacker/jpacker.conf
legacy_preference=$stage_dir/etc/jpacker/package.build/fastfetch
foreign_system_file=$stage_dir/etc/moguet/foreign-admin-file
user_config=$xdg_config_home/moguet/config.toml
canonical_preference_dir=$xdg_config_home/moguet/source-build.d
canonical_preference=$canonical_preference_dir/fastfetch
user_state=$xdg_state_home/moguet/moguet.log
user_cache=$xdg_cache_home/moguet/cache-entry
foreign_doc=$doc_dir/foreign-file.keep
foreign_license=$license_dir/foreign-file.keep
foreign_completion=$(dirname "$bash_completion_file")/foreign-command
foreign_catalog=$(dirname "$catalog_file")/foreign-domain.mo

install -Dm644 /dev/null "$legacy_config"
printf '%s\n' 'NOEDIT=true' > "$legacy_config"
install -Dm644 /dev/null "$legacy_preference"
printf '%s\n' 'CFLAGS=-O3 -march=native' > "$legacy_preference"
install -Dm644 /dev/null "$foreign_system_file"
printf '%s\n' 'foreign admin data' > "$foreign_system_file"
install -Dm600 /dev/null "$user_config"
printf '%s\n' 'schema_version = 1' > "$user_config"
install -d -m700 "$canonical_preference_dir"
install -m600 /dev/null "$canonical_preference"
printf '%s\n' 'CFLAGS=-O2 -pipe' > "$canonical_preference"
install -Dm600 /dev/null "$user_state"
printf '%s\n' 'user state' > "$user_state"
install -Dm600 /dev/null "$user_cache"
printf '%s\n' 'user cache' > "$user_cache"
printf '%s\n' 'foreign documentation' > "$foreign_doc"
printf '%s\n' 'foreign license' > "$foreign_license"
printf '%s\n' 'foreign completion' > "$foreign_completion"
printf '%s\n' 'foreign catalog' > "$foreign_catalog"

run_make install
assert_package_artifacts_installed
assert_file_text "$legacy_config" 'NOEDIT=true'
assert_file_text "$legacy_preference" 'CFLAGS=-O3 -march=native'
assert_file_text "$foreign_system_file" 'foreign admin data'
assert_file_text "$user_config" 'schema_version = 1'
assert_file_text "$canonical_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$canonical_preference_dir" 700
assert_mode "$canonical_preference" 600
assert_file_text "$user_state" 'user state'
assert_file_text "$user_cache" 'user cache'

run_make uninstall
assert_package_artifacts_absent
assert_file_text "$legacy_config" 'NOEDIT=true'
assert_file_text "$legacy_preference" 'CFLAGS=-O3 -march=native'
assert_file_text "$foreign_system_file" 'foreign admin data'
assert_file_text "$user_config" 'schema_version = 1'
assert_file_text "$canonical_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$canonical_preference_dir" 700
assert_mode "$canonical_preference" 600
assert_file_text "$user_state" 'user state'
assert_file_text "$user_cache" 'user cache'
assert_file_text "$foreign_doc" 'foreign documentation'
assert_file_text "$foreign_license" 'foreign license'
assert_file_text "$foreign_completion" 'foreign completion'
assert_file_text "$foreign_catalog" 'foreign catalog'
assert_directory "$stage_dir/etc/jpacker/package.build"
assert_directory "$xdg_config_home/moguet"
assert_directory "$xdg_state_home/moguet"
assert_directory "$xdg_cache_home/moguet"
assert_no_symlinks

# A final reinstall/uninstall cycle proves that retained foreign entries do not
# prevent package-owned files from being restored or removed exactly.
run_make install
assert_package_artifacts_installed
run_make uninstall
assert_package_artifacts_absent
assert_file_text "$legacy_preference" 'CFLAGS=-O3 -march=native'
assert_file_text "$user_config" 'schema_version = 1'
assert_file_text "$canonical_preference" 'CFLAGS=-O2 -pipe'
assert_mode "$canonical_preference_dir" 700
assert_mode "$canonical_preference" 600
assert_file_text "$foreign_doc" 'foreign documentation'
assert_file_text "$foreign_license" 'foreign license'
assert_no_symlinks

printf 'install-layout-test: all checks passed\n'
