#!/bin/sh

set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)

fail() {
    printf 'license-check: %s\n' "$*" >&2
    exit 1
}

pass() {
    printf 'license-check: ok: %s\n' "$*"
}

require_regular_file() {
    file=$1
    [ -f "$file" ] || fail "$file is missing or is not a regular file."
    [ ! -L "$file" ] || fail "$file must not be a symbolic link."
    [ -s "$file" ] || fail "$file is empty."
}

require_text() {
    file=$1
    expected=$2
    grep -F -- "$expected" "$file" >/dev/null ||
        fail "$file is missing required text: $expected"
}

require_value_text() {
    label=$1
    value=$2
    expected=$3
    printf '%s\n' "$value" | grep -F -- "$expected" >/dev/null ||
        fail "$label is missing required text: $expected"
}

check_sha256() {
    file=$1
    expected=$2
    actual=$(sha256sum "$file" | awk '{print $1}')
    [ "$actual" = "$expected" ] ||
        fail "$file checksum mismatch: expected $expected, got $actual"
    pass "$file matches audited SHA-256"
}

check_pkgbuild_license_case() {
    case_version=$1
    expected_license=$2
    case_label=$3

    printf '%s\n' "$case_version" > "$pkgbuild_test_dir/VERSION"
    if ! case_metadata=$(
        cd "$pkgbuild_test_dir"
        makepkg --printsrcinfo
    ); then
        fail "PKGBUILD evaluation failed for $case_label."
    fi

    evaluated_version=$(printf '%s\n' "$case_metadata" |
        sed -n 's/^[[:space:]]*pkgver = //p')
    [ "$evaluated_version" = "$case_version" ] ||
        fail "PKGBUILD $case_label evaluated pkgver=$evaluated_version; expected $case_version."

    evaluated_license=$(printf '%s\n' "$case_metadata" |
        sed -n 's/^[[:space:]]*license = //p')
    [ "$evaluated_license" = "$expected_license" ] ||
        fail "PKGBUILD $case_label evaluated license=$evaluated_license; expected $expected_license."

    evaluated_source=$(printf '%s\n' "$case_metadata" |
        sed -n 's/^[[:space:]]*source = //p')
    expected_source="jpacker-src::git+https://github.com/seekerkrt/jpacker.git#tag=v$case_version"
    [ "$evaluated_source" = "$expected_source" ] ||
        fail "PKGBUILD $case_label source mismatch: expected $expected_source, got $evaluated_source."

    pass "PKGBUILD $case_label -> $expected_license"
}

cd "$repo_root"

command -v sha256sum >/dev/null 2>&1 ||
    fail "sha256sum is required for offline canonical-text verification."
command -v makepkg >/dev/null 2>&1 ||
    fail "makepkg is required for PKGBUILD metadata verification."
command -v vercmp >/dev/null 2>&1 ||
    fail "vercmp is required for PKGBUILD license boundary verification."

for file in \
    LICENSE \
    LICENSES/jpacker-MIT-legacy.txt \
    LICENSES/curl.txt \
    LICENSES/nlohmann-json-MIT.txt \
    THIRD_PARTY_NOTICES.md \
    docs/LICENSING.md
do
    require_regular_file "$file"
done
pass "required license and notice files are present and non-empty"

# POLICY: Pin the exact SPDX/Arch canonical bytes so release-check never needs network access.
check_sha256 LICENSE \
    fb981668c18a279e285fc4d83fba1e836cc84dd4daa73c9697d3cfd2d8aca6e0

installed_gpl=/usr/share/licenses/spdx/GPL-3.0-or-later.txt
if [ -f "$installed_gpl" ]; then
    cmp -s LICENSE "$installed_gpl" ||
        fail "LICENSE differs from the installed SPDX GPL-3.0-or-later canonical copy."
    pass "LICENSE matches the installed SPDX canonical copy"
else
    pass "installed SPDX copy unavailable; pinned checksum remains authoritative"
fi

check_sha256 LICENSES/jpacker-MIT-legacy.txt \
    2875473bd59e4ff421e2584c37cbff758702a7f291e719ce3ccd5ccc1f825261
check_sha256 LICENSES/curl.txt \
    82f2f4427d6545ee5aaac4f0b80428da6cc8ba41c2cf5da3a03680ec327b9681
check_sha256 LICENSES/nlohmann-json-MIT.txt \
    46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603

for installed_notice in \
    /usr/share/licenses/curl/COPYING \
    /usr/share/licenses/nlohmann-json/LICENSE.MIT
do
    if [ -f "$installed_notice" ]; then
        case "$installed_notice" in
            */curl/COPYING)
                repository_notice=LICENSES/curl.txt
                ;;
            */nlohmann-json/LICENSE.MIT)
                repository_notice=LICENSES/nlohmann-json-MIT.txt
                ;;
        esac
        cmp -s "$repository_notice" "$installed_notice" ||
            fail "$repository_notice differs from installed $installed_notice; re-audit the dependency notice."
        pass "$repository_notice matches the installed package notice"
    fi
done

require_text docs/LICENSING.md \
    "The current v1.15.0 development series and v1.15.0 or later releases are distributed under GPL-3.0-or-later."
require_text docs/LICENSING.md \
    "jpacker v1.14.0 and earlier releases were distributed under the MIT License."
require_text docs/LICENSING.md \
    "Those historical releases remain available under their original license."
require_text README.md '現在のv1.15.0開発系列とv1.15.0以降のjpackerは、`GPL-3.0-or-later`で提供します。'
require_text README.md "v1.14.0以前のreleaseはMIT License"
require_text README.md 'The current v1.15.0 development series and v1.15.0 or later releases are distributed under `GPL-3.0-or-later`.'
require_text README.md "jpacker v1.14.0 and earlier releases"
require_text README.md "[LICENSE](LICENSE)"
require_text README.md "[docs/LICENSING.md](docs/LICENSING.md)"
require_text README.md "[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)"
pass "project notice and README preserve the version boundary"

for heading in \
    "## Linked or compiled components" \
    "## External programs invoked" \
    "## System/toolchain runtime" \
    "## Distribution notes" \
    "### libalpm" \
    "### libcurl" \
    "### nlohmann-json"
do
    require_text THIRD_PARTY_NOTICES.md "$heading"
done
require_text THIRD_PARTY_NOTICES.md '`GPL-2.0-or-later`'
require_text THIRD_PARTY_NOTICES.md '`curl` (SPDX identifier)'
require_text THIRD_PARTY_NOTICES.md '[`LICENSES/curl.txt`](LICENSES/curl.txt)'
require_text THIRD_PARTY_NOTICES.md '[`LICENSES/nlohmann-json-MIT.txt`](LICENSES/nlohmann-json-MIT.txt)'
require_text THIRD_PARTY_NOTICES.md "Every entry below is a separately installed program"
require_text THIRD_PARTY_NOTICES.md "It is not linked into jpacker and is not bundled with jpacker."

linked_headings=$(awk '
    /^## Linked or compiled components$/ { in_section = 1; next }
    /^## / && in_section { exit }
    in_section && /^### / { print }
' THIRD_PARTY_NOTICES.md)
expected_linked_headings=$(printf '%s\n' '### libalpm' '### libcurl' '### nlohmann-json')
[ "$linked_headings" = "$expected_linked_headings" ] ||
    fail "linked/compiled component headings contain an unexpected classification."

external_program_section=$(awk '
    /^## External programs invoked$/ { in_section = 1; next }
    /^## / && in_section { exit }
    in_section { print }
' THIRD_PARTY_NOTICES.md)

for program in pacman pacman-conf makepkg vercmp git bsdtar sudo
do
    require_value_text "THIRD_PARTY_NOTICES.md external-program section" \
        "$external_program_section" "| \`$program\` |"
done
require_value_text "THIRD_PARTY_NOTICES.md external-program section" \
    "$external_program_section" '| `touch`, `tee`, `install`, `rm` |'
require_value_text "THIRD_PARTY_NOTICES.md external-program section" \
    "$external_program_section" '| `/bin/sh` |'
require_value_text "THIRD_PARTY_NOTICES.md external-program section" \
    "$external_program_section" '| User-selected editor (default `nano`) |'
pass "third-party components and external-process boundary are classified"

require_regular_file VERSION
require_regular_file PKGBUILD
current_version=$(tr -d '[:space:]' < VERSION)
[ -n "$current_version" ] || fail "VERSION is empty."

current_comparison=$(vercmp "$current_version" 1.15.0)
if [ "$current_comparison" -lt 0 ]; then
    current_expected_license=MIT
else
    current_expected_license=GPL-3.0-or-later
fi

# POLICY: Evaluate the actual makepkg metadata without rewriting repository VERSION.
pkgbuild_test_dir=$(mktemp -d)
cleanup_pkgbuild_test() {
    rm -rf "$pkgbuild_test_dir"
}
trap cleanup_pkgbuild_test EXIT INT TERM
cp PKGBUILD "$pkgbuild_test_dir/PKGBUILD"

check_pkgbuild_license_case "$current_version" "$current_expected_license" \
    "repository VERSION=$current_version"
check_pkgbuild_license_case 1.14.0 MIT 1.14.0
check_pkgbuild_license_case 1.14.1 MIT 1.14.1
check_pkgbuild_license_case 1.15.0 GPL-3.0-or-later 1.15.0
check_pkgbuild_license_case 1.15.1 GPL-3.0-or-later 1.15.1
check_pkgbuild_license_case 1.100.0 GPL-3.0-or-later 1.100.0

require_text PKGBUILD "if [[ \${license[0]} == 'MIT' ]]; then"
require_text PKGBUILD \
    'install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"'
pass "PKGBUILD retains the pre-v1.15 MIT license-file fallback"

if grep -Fx -- "MIT License" README.md >/dev/null; then
    fail "README still contains the stale standalone current-project MIT label."
fi
pass "PKGBUILD metadata and current-project README follow the license boundary"

printf 'license-check: all checks passed\n'
