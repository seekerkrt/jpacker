#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
. "$repo_root/scripts/validation-status.sh"
current_package_fixture=$repo_root/tests/fixtures/current-package
current_package_contract=$current_package_fixture/contract.env
runtime_dependency_authority=$current_package_fixture/runtime-dependencies.txt
build_dependency_authority=$current_package_fixture/build-dependencies.txt
install_payload_authority=$current_package_fixture/install-payload.txt
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

for authority_file in \
    "$current_package_contract" \
    "$runtime_dependency_authority" \
    "$build_dependency_authority" \
    "$install_payload_authority"
do
    [ -f "$authority_file" ] && [ ! -L "$authority_file" ] &&
        [ -s "$authority_file" ] ||
        fail "current package authority must be a non-empty regular non-symlink: $authority_file"
done
# shellcheck source=../tests/fixtures/current-package/contract.env
. "$current_package_contract"

[ -f "$repo_root/VERSION" ] && [ ! -L "$repo_root/VERSION" ] ||
    fail 'VERSION must be a regular non-symlink'
current_version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ -n "$current_version" ] || fail 'VERSION is empty'

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
    values_raw=$tmp_dir/srcinfo-$key.raw
    values_sorted=$tmp_dir/srcinfo-$key.sorted
    if validation_capture_sorted_output "$values_raw" "$values_sorted" \
        srcinfo_values "$key"; then
        actual=$(cat "$values_sorted")
    else
        producer_status=$?
        fail "$key producer or normalization failed with status $producer_status; raw=$values_raw"
    fi
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

assert_single_value pkgbase "$PACKAGE_BASE"
assert_single_value pkgname "$PACKAGE_NAME"
assert_single_value pkgver "$current_version"
assert_single_value pkgrel "$PACKAGE_RELEASE"
assert_single_value arch "$PACKAGE_ARCHITECTURE"
assert_single_value license "$PACKAGE_LICENSE"
assert_single_value source \
    "$PACKAGE_SOURCE_NAME::git+$PROJECT_REPOSITORY_URL.git#tag=v$current_version"
assert_single_value sha256sums SKIP

if expected_depends=$(cat "$runtime_dependency_authority"); then
    :
else
    producer_status=$?
    fail "runtime dependency authority read failed with status $producer_status"
fi
assert_value_set depends "$expected_depends"

if expected_makedepends=$(cat "$build_dependency_authority"); then
    :
else
    producer_status=$?
    fail "build dependency authority read failed with status $producer_status"
fi
assert_value_set makedepends "$expected_makedepends"

# No system or user configuration belongs to the package. The disjoint v1/v2
# payload and lack of a command alias make all four transition fields harmful.
assert_no_values backup
assert_no_values conflicts
assert_no_values replaces
assert_no_values provides
assert_no_values optdepends

while IFS= read -r build_only_dependency; do
    [ -n "$build_only_dependency" ] || continue
    depends_values=$tmp_dir/srcinfo-depends.values
    validation_capture_output "$depends_values" srcinfo_values depends ||
        fail "depends producer failed with status $?"
    if grep -Fx -- "$build_only_dependency" "$depends_values" >/dev/null; then
        dependency_status=0
    else
        dependency_status=$?
    fi
    case $dependency_status in
        0) fail "$build_only_dependency must not be a runtime dependency." ;;
        1) ;;
        *) fail "depends inspection failed with status $dependency_status" ;;
    esac
done <"$build_dependency_authority"

for forbidden_tomlplusplus_flag in \
    -ltomlplusplus \
    TOML_HEADER_ONLY=0 \
    TOML_SHARED_LIB=1
do
    if grep -E "^[[:space:]]*[^#].*$forbidden_tomlplusplus_flag" \
        "$repo_root/CMakeLists.txt" "$repo_root"/cmake/*.cmake >/dev/null; then
        fail "CMake enables toml++ shared-library mode: $forbidden_tomlplusplus_flag"
    fi
done

# Evaluate the actual package() function against the current source tree. A
# clean archive build uses an isolated local-tag fixture in the package
# transition test because the public v2.0.0 tag belongs to the later cutover.
ln -s "$repo_root" "$package_work/$PACKAGE_SOURCE_NAME"
ln -s "$repo_root/VERSION" "$package_work/VERSION"
bash -c '
    set -eu
    package_work=$1
    package_stage=$2
    package_file=$3
    cd "$package_work"
    pkgdir=$package_stage
    source "$package_file"
    build
    package
' bash "$package_work" "$stage_root" "$repo_root/PKGBUILD" >/dev/null

if expected_payload=$(cat "$install_payload_authority"); then
    :
else
    producer_status=$?
    fail "install payload authority read failed with status $producer_status"
fi
payload_paths_raw=$tmp_dir/payload-paths.raw
payload_paths_stripped=$tmp_dir/payload-paths.stripped
payload_paths_sorted=$tmp_dir/payload-paths.sorted
validation_capture_output "$payload_paths_raw" \
    find "$stage_root" -type f -print ||
    fail "payload path producer failed with status $?; raw=$payload_paths_raw"
if sed "s|^$stage_root||" "$payload_paths_raw" \
    >"$payload_paths_stripped"; then
    :
else
    producer_status=$?
    fail "payload path normalization failed with status $producer_status"
fi
if LC_ALL=C sort "$payload_paths_stripped" >"$payload_paths_sorted"; then
    actual_payload=$(cat "$payload_paths_sorted")
else
    producer_status=$?
    fail "payload path sorting failed with status $producer_status"
fi
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
readelf_raw=$tmp_dir/readelf.dynamic.raw
needed_file=$tmp_dir/readelf.needed
if validation_capture_output "$readelf_raw" \
    env LC_ALL=C readelf -d "$stage_root/usr/bin/$COMMAND_NAME"; then
    :
else
    producer_status=$?
    fail "readelf producer failed with status $producer_status; raw=$readelf_raw"
fi
if sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' \
    "$readelf_raw" >"$needed_file"; then
    :
else
    producer_status=$?
    fail "readelf dependency normalization failed with status $producer_status"
fi
grep -Eq '^libcurl\.so(\.|$)' "$needed_file" ||
    fail "$COMMAND_NAME ELF is missing the direct libcurl runtime dependency."
grep -Eq '^libalpm\.so(\.|$)' "$needed_file" ||
    fail "$COMMAND_NAME ELF is missing the direct libalpm runtime dependency."
if grep -Eq '^libintl\.so(\.|$)' "$needed_file"; then
    fail "gettext unexpectedly became a linked runtime dependency."
else
    grep_status=$?
    case $grep_status in
        1) ;;
        *) fail "readelf dependency absence check failed with status $grep_status" ;;
    esac
fi

printf 'packaging-metadata-check: all checks passed\n'
