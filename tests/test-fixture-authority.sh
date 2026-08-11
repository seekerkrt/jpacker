#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
. "$repo_root/scripts/validation-status.sh"
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf -- "$tmp_dir" >/dev/null 2>&1 || :
}
trap cleanup EXIT HUP INT TERM

fail() {
    printf 'fixture authority: FAIL: %s\n' "$*" >&2
    exit 1
}

print_srcinfo() {
    fixture_directory=$1
    (
        cd "$fixture_directory" || exit $?
        makepkg --printsrcinfo
    )
}

assert_projection() {
    fixture_directory=$1
    expected_projection=$2
    label=$3
    actual_projection=$tmp_dir/$label.srcinfo
    if validation_capture_output "$actual_projection" \
        print_srcinfo "$fixture_directory"; then
        :
    else
        projection_status=$?
        fail "$label makepkg projection failed with status $projection_status"
    fi
    if ! cmp -s "$expected_projection" "$actual_projection"; then
        diff -u "$expected_projection" "$actual_projection" >&2 || :
        fail "$label tracked .SRCINFO projection drift"
    fi
}

live_root=$repo_root/containers/arch-live-validation
local_fixture=$live_root/fixtures/local-package
aur_case=$live_root/aur-cases.tsv
aur_payload=$live_root/fixtures/aur/payload-authority.tsv
current_package_fixture=$repo_root/tests/fixtures/current-package

assert_projection \
    "$repo_root/tests/fixtures/unified-plan-local-blocked" \
    "$repo_root/tests/fixtures/unified-plan-local-blocked/.SRCINFO" \
    deterministic-local-plan
assert_projection "$local_fixture" "$local_fixture/expected.srcinfo" live-local

python3 - \
    "$local_fixture/contract.env" \
    "$local_fixture/expected.srcinfo" \
    "$local_fixture/payload-authority.tsv" \
    "$aur_case" "$aur_payload" \
    "$current_package_fixture/contract.env" \
    "$current_package_fixture/runtime-dependencies.txt" \
    "$current_package_fixture/build-dependencies.txt" \
    "$current_package_fixture/install-payload.txt" <<'PY'
from pathlib import Path
import re
import sys

(
    local_contract_path,
    local_srcinfo_path,
    local_payload_path,
    aur_case_path,
    aur_payload_path,
    current_contract_path,
    current_runtime_dependencies_path,
    current_build_dependencies_path,
    current_install_payload_path,
) = map(Path, sys.argv[1:])


def read_lines(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    if not text.endswith("\n"):
        raise SystemExit(f"{path} lacks a terminal newline")
    return text.splitlines()


local_keys = [
    "PACKAGE_NAME",
    "PACKAGE_BASE",
    "PACKAGE_VERSION",
    "PACKAGE_ARCHITECTURE",
    "REQUIRED_MAKE_DEPENDENCY",
    "EXPECTED_PROVIDER_REPOSITORY",
    "EXPECTED_PROVIDER_PACKAGES",
    "LOCAL_INSTALL_PROVIDER",
    "PROVIDER_INSTALL_REASON",
    "ROOT_ARTIFACT_INSTALL_REASON",
]
local_contract: dict[str, str] = {}
for line in read_lines(local_contract_path):
    match = re.fullmatch(r"([A-Z][A-Z0-9_]*)=([^\s]+)", line)
    if not match or match.group(1) in local_contract:
        raise SystemExit(f"unsafe local contract assignment: {line!r}")
    local_contract[match.group(1)] = match.group(2)
if list(local_contract) != local_keys:
    raise SystemExit("local contract keys or ordering drift")
package_pattern = re.compile(r"[a-z0-9][a-z0-9@._+-]*")
for key in ("PACKAGE_NAME", "PACKAGE_BASE", "REQUIRED_MAKE_DEPENDENCY"):
    if not package_pattern.fullmatch(local_contract[key]):
        raise SystemExit(f"unchecked local package identity in {key}")
if local_contract["PACKAGE_NAME"] != local_contract["PACKAGE_BASE"]:
    raise SystemExit("local single-package fixture PackageBase drift")
providers = local_contract["EXPECTED_PROVIDER_PACKAGES"].split(",")
if len(providers) < 2 or len(set(providers)) != len(providers):
    raise SystemExit("local provider authority is not a unique ambiguity set")
if any(not package_pattern.fullmatch(provider) for provider in providers):
    raise SystemExit("local provider authority contains an invalid package name")
if local_contract["LOCAL_INSTALL_PROVIDER"] not in providers:
    raise SystemExit("local install provider is outside the reviewed provider set")
if local_contract["PROVIDER_INSTALL_REASON"] != "Dependency":
    raise SystemExit("local provider reason must remain Dependency")
if local_contract["ROOT_ARTIFACT_INSTALL_REASON"] != "Explicit":
    raise SystemExit("local root reason must remain Explicit")

srcinfo: dict[str, list[str]] = {}
for line in read_lines(local_srcinfo_path):
    if not line:
        continue
    match = re.fullmatch(r"\t*([a-z][a-z0-9_]*) = (.+)", line)
    if not match:
        raise SystemExit(f"unparseable local expected projection: {line!r}")
    srcinfo.setdefault(match.group(1), []).append(match.group(2))
for key in ("pkgbase", "pkgver", "pkgrel", "arch", "makedepends"):
    if len(srcinfo.get(key, [])) != 1:
        raise SystemExit(f"local expected projection lacks one {key}")
if srcinfo["pkgbase"][0] != local_contract["PACKAGE_BASE"]:
    raise SystemExit("local expected projection PackageBase drift")
if srcinfo.get("pkgname") != [local_contract["PACKAGE_NAME"]]:
    raise SystemExit("local expected projection package identity drift")
if f"{srcinfo['pkgver'][0]}-{srcinfo['pkgrel'][0]}" != local_contract["PACKAGE_VERSION"]:
    raise SystemExit("local expected projection version drift")
if srcinfo["arch"][0] != local_contract["PACKAGE_ARCHITECTURE"]:
    raise SystemExit("local expected projection architecture drift")
if srcinfo["makedepends"][0] != local_contract["REQUIRED_MAKE_DEPENDENCY"]:
    raise SystemExit("local expected projection dependency drift")


def parse_payload(path: Path) -> dict[str, tuple[str, str, str]]:
    rows = [line.split("\t") for line in read_lines(path)]
    if not rows or rows[0] != ["# path", "type", "mode", "sha256"]:
        raise SystemExit(f"{path} payload header drift")
    if any(len(row) != 4 for row in rows[1:]):
        raise SystemExit(f"{path} is not exact-tab payload authority")
    payload: dict[str, tuple[str, str, str]] = {}
    for fixture_path, entry_type, mode, content_hash in rows[1:]:
        if fixture_path in payload or not fixture_path:
            raise SystemExit(f"{path} has an empty or duplicate payload entry")
        if fixture_path.startswith("/") or "//" in fixture_path or ".." in fixture_path.split("/"):
            raise SystemExit(f"{path} has an unsafe payload path")
        if entry_type not in {"directory", "regular"} or not re.fullmatch(r"0[0-7]{3}", mode):
            raise SystemExit(f"{path} has invalid payload type or mode")
        if (entry_type == "directory") != fixture_path.endswith("/"):
            raise SystemExit(f"{path} has inconsistent directory identity")
        if content_hash != "-" and not re.fullmatch(r"[0-9a-f]{64}", content_hash):
            raise SystemExit(f"{path} has an invalid content hash")
        payload[fixture_path] = (entry_type, mode, content_hash)
    if list(payload) != sorted(payload):
        raise SystemExit(f"{path} payload authority is not sorted")
    return payload


local_payload = parse_payload(local_payload_path)
local_package = local_contract["PACKAGE_NAME"]
local_binary = f"usr/bin/{local_package}"
if local_payload.get(local_binary) != ("regular", "0755", "-"):
    raise SystemExit("local payload lost its dynamic package executable")
static_payload = [
    path for path, (entry_type, _mode, digest) in local_payload.items()
    if entry_type == "regular" and digest != "-"
]
if len(static_payload) != 1:
    raise SystemExit("local payload must keep one independently pinned marker")

aur_header = [
    "# package", "package_base", "expected_version", "runtime_dependencies",
    "make_dependencies", "source_kind", "install_reason", "fallback_policy",
    "review_required", "expected_aur_git_head", "expected_pkgbuild_sha256",
    "expected_srcinfo_sha256", "expected_source_filename", "expected_source_url",
    "expected_source_sha256", "expected_rpc_url_path",
    "expected_artifact_architecture",
]
aur_rows = [line.split("\t") for line in read_lines(aur_case_path)]
if len(aur_rows) != 2 or aur_rows[0] != aur_header or len(aur_rows[1]) != len(aur_header):
    raise SystemExit("live AUR case must be one exact-tab scenario")
aur = dict(zip(aur_header, aur_rows[1], strict=True))
if not package_pattern.fullmatch(aur["# package"]) or aur["# package"] != aur["package_base"]:
    raise SystemExit("live AUR single-package identity drift")
if aur["source_kind"] != "single-release-archive":
    raise SystemExit("live AUR source-kind drift")
if (aur["install_reason"], aur["fallback_policy"], aur["review_required"]) != (
    "Explicit", "reject", "required"
):
    raise SystemExit("live AUR scenario policy drift")
if not re.fullmatch(r"[0-9a-f]{40}", aur["expected_aur_git_head"]):
    raise SystemExit("live AUR commit pin is malformed")
for key in ("expected_pkgbuild_sha256", "expected_srcinfo_sha256", "expected_source_sha256"):
    if not re.fullmatch(r"[0-9a-f]{64}", aur[key]):
        raise SystemExit(f"live AUR {key} is malformed")
if not aur["expected_source_url"].startswith("https://"):
    raise SystemExit("live AUR source URL is not HTTPS")
if not aur["expected_rpc_url_path"].startswith("/"):
    raise SystemExit("live AUR RPC path is not absolute")

aur_payload = parse_payload(aur_payload_path)
aur_package = aur["# package"]
required_aur_entries = {
    f"usr/bin/{aur_package}": ("regular", "0755", "-"),
    f"usr/share/doc/{aur_package}/README.md": None,
    f"usr/share/licenses/{aur_package}/LICENSE": None,
}
for fixture_path, exact_identity in required_aur_entries.items():
    if fixture_path not in aur_payload:
        raise SystemExit(f"live AUR payload lacks {fixture_path}")
    if exact_identity is not None and aur_payload[fixture_path] != exact_identity:
        raise SystemExit(f"live AUR payload identity is unsafe: {fixture_path}")
for fixture_path in required_aur_entries:
    if fixture_path != f"usr/bin/{aur_package}" and aur_payload[fixture_path][2] == "-":
        raise SystemExit(f"live AUR static payload lacks an exact hash: {fixture_path}")

current_keys = [
    "PROJECT_NAME",
    "COMMAND_NAME",
    "XDG_IDENTITY",
    "GETTEXT_DOMAIN",
    "PACKAGE_NAME",
    "PACKAGE_BASE",
    "PACKAGE_RELEASE",
    "PACKAGE_ARCHITECTURE",
    "PACKAGE_LICENSE",
    "PACKAGE_SOURCE_NAME",
    "PROJECT_REPOSITORY_URL",
]
current_contract: dict[str, str] = {}
for line in read_lines(current_contract_path):
    match = re.fullmatch(r"([A-Z][A-Z0-9_]*)=([^\s]+)", line)
    if not match or match.group(1) in current_contract:
        raise SystemExit(f"unsafe current package contract assignment: {line!r}")
    current_contract[match.group(1)] = match.group(2)
if list(current_contract) != current_keys:
    raise SystemExit("current package contract keys or ordering drift")
for key in (
    "COMMAND_NAME", "XDG_IDENTITY", "GETTEXT_DOMAIN", "PACKAGE_NAME",
    "PACKAGE_BASE", "PACKAGE_SOURCE_NAME",
):
    if not package_pattern.fullmatch(current_contract[key]):
        raise SystemExit(f"invalid current project identity in {key}")
if not re.fullmatch(r"[1-9][0-9]*", current_contract["PACKAGE_RELEASE"]):
    raise SystemExit("current package release is not a positive integer")
if not current_contract["PROJECT_REPOSITORY_URL"].startswith("https://"):
    raise SystemExit("current project repository authority is not HTTPS")
if current_contract["PROJECT_REPOSITORY_URL"].endswith(".git"):
    raise SystemExit("current project repository authority must be the canonical web URL")


def dependency_authority(path: Path) -> list[str]:
    dependencies = read_lines(path)
    if (
        not dependencies
        or dependencies != sorted(dependencies)
        or len(dependencies) != len(set(dependencies))
        or any(not re.fullmatch(r"[A-Za-z0-9@._+:-]+", item) for item in dependencies)
    ):
        raise SystemExit(f"{path} is not a sorted unique dependency authority")
    return dependencies


runtime_dependencies = dependency_authority(current_runtime_dependencies_path)
build_dependencies = dependency_authority(current_build_dependencies_path)
if set(runtime_dependencies) & set(build_dependencies):
    raise SystemExit("current runtime and build dependency authorities overlap")

install_payload = read_lines(current_install_payload_path)
if (
    not install_payload
    or install_payload != sorted(install_payload)
    or len(install_payload) != len(set(install_payload))
):
    raise SystemExit("current install payload authority is not sorted and unique")
for payload_path in install_payload:
    path_parts = Path(payload_path).parts
    if not payload_path.startswith("/") or ".." in path_parts or "//" in payload_path:
        raise SystemExit(f"unsafe current install payload path: {payload_path}")
required_current_payload = {
    f"/usr/bin/{current_contract['COMMAND_NAME']}",
    f"/usr/share/doc/{current_contract['PACKAGE_NAME']}/README.md",
    f"/usr/share/licenses/{current_contract['PACKAGE_NAME']}/LICENSE",
    f"/usr/share/locale/ja/LC_MESSAGES/{current_contract['GETTEXT_DOMAIN']}.mo",
}
missing_current_payload = required_current_payload - set(install_payload)
if missing_current_payload:
    raise SystemExit(
        "current install payload lacks identity projections: "
        + ", ".join(sorted(missing_current_payload))
    )
if any(path.endswith(".gz") for path in install_payload):
    raise SystemExit("make install payload must not absorb makepkg compression")
PY

# This pair is deliberately not a projection: test-pkgbuild-export exercises
# repository-export bytes, including a split child absent from its independent
# .SRCINFO blob.  Treating it as generated projection would erase that case.
export_fixture=$repo_root/tests/fixtures/pkgbuild-export
grep -F "'clean-root-cli'" "$export_fixture/PKGBUILD" >/dev/null ||
    fail 'export fixture lost the independent split-package input'
if grep -F 'pkgname = clean-root-cli' "$export_fixture/.SRCINFO" >/dev/null; then
    fail 'export fixture .SRCINFO was incorrectly unified with PKGBUILD projection'
fi

# VERSION is the canonical current-release input.  makepkg and built-binary
# output remain separate actual paths in their owning checks; validation code
# must not copy the current literal into another authority.
current_version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ -n "$current_version" ] || fail 'root VERSION is empty'
current_version_matches=$tmp_dir/current-version-literals.txt
if grep -R -F -n -- "$current_version" \
    "$repo_root/tests" \
    "$repo_root/scripts" \
    "$repo_root/containers" \
    "$repo_root/PKGBUILD" \
    "$repo_root/Makefile" \
    "$repo_root/.github" \
    "$repo_root/.gitlab" >"$current_version_matches"; then
    sed -n '1,120p' "$current_version_matches" >&2
    fail 'validation or packaging code duplicates the current VERSION literal'
else
    version_grep_status=$?
fi
case $version_grep_status in
    1) ;;
    *) fail "current VERSION scan failed with status $version_grep_status" ;;
esac

printf '%s\n' 'fixture authority tests: all checks passed'
