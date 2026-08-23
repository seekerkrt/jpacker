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

# VERSION is the canonical current-release input.  Every tracked production
# file under source/ is part of the current-version semantic domain; classified
# tests and release tooling remain explicit because they can also own synthetic
# or historical versions.  Generated man/catalog files are verified as
# projections.
current_version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ -n "$current_version" ] || fail 'root VERSION is empty'
python3 - "$repo_root" "$current_version" "$tmp_dir" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

repo_root = Path(sys.argv[1])
canonical_version = sys.argv[2]
scratch_root = Path(sys.argv[3])

if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", canonical_version):
    raise SystemExit(f"root VERSION is not X.Y.Z: {canonical_version!r}")


def tracked_paths(repository: Path) -> set[str]:
    try:
        result = subprocess.run(
            ["git", "-C", str(repository), "ls-files", "-z"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"unable to enumerate tracked current-version consumers: {error}")
    return {
        value.decode("utf-8")
        for value in result.stdout.split(b"\0")
        if value
    }


def production_source_paths(
    repository: Path, repository_tracked_paths: set[str] | None
) -> tuple[str, ...]:
    source_root = repository / "source"
    if not source_root.is_dir() or source_root.is_symlink():
        raise SystemExit("production source root is missing, non-directory, or a symlink")
    if repository_tracked_paths is not None:
        candidates = sorted(
            relative_path
            for relative_path in repository_tracked_paths
            if relative_path.startswith("source/")
        )
    else:
        # Docker and release source snapshots intentionally omit .git.  Their
        # source/ tree is the exported production set, so inspect it in full.
        candidates = sorted(
            path.relative_to(repository).as_posix()
            for path in source_root.rglob("*")
            if path.is_file() or path.is_symlink()
        )
    if not candidates:
        raise SystemExit("production source semantic domain is empty")
    return tuple(candidates)


repository_tracked_paths = (
    tracked_paths(repo_root) if (repo_root / ".git").exists() else None
)
production_source_consumers = production_source_paths(
    repo_root, repository_tracked_paths
)

# These non-production paths consume the current project version as
# source/build/validation input.  A version-looking value in other tests can
# instead be synthetic, external, or historical and is not classified by
# spelling.
classified_source_consumers = (
    "Makefile",
    "PKGBUILD",
    "scripts/check-license-compliance.sh",
    "scripts/check-packaging-metadata.sh",
    "scripts/check-release-version.sh",
    "scripts/check_public_documentation.py",
    "scripts/extract-release-notes.sh",
    "tests/application_identity_test.cpp",
    "tests/test-aur-rpc-validation.sh",
    "tests/test-fixture-authority.sh",
    "tests/test-help-man-completion.sh",
    "tests/test-install-layout.sh",
    "tests/test-package-transition.sh",
    "tests/test-runtime-identity.sh",
)
source_consumers = (
    *classified_source_consumers,
    *production_source_consumers,
)
man_projections = (
    ("man/moguet.1.in", "man/moguet.1"),
    ("man/ja/moguet.1.in", "man/ja/moguet.1"),
)
catalog_projections = ("po/moguet.pot", "po/ja.po")
owned_paths = {
    "VERSION",
    *source_consumers,
    *catalog_projections,
    *(path for pair in man_projections for path in pair),
}
missing_or_unsafe_owners = sorted(
    relative_path
    for relative_path in owned_paths
    if not (repo_root / relative_path).is_file()
    or (repo_root / relative_path).is_symlink()
)
if missing_or_unsafe_owners:
    raise SystemExit(
        "current-version owner is missing, non-regular, or a symlink: "
        + ", ".join(missing_or_unsafe_owners)
    )

# A host checkout additionally proves that every classified or projected owner
# is tracked.  Production source owners already come from this same complete set.
if repository_tracked_paths is not None:
    untracked_owners = sorted(owned_paths - repository_tracked_paths)
    if untracked_owners:
        raise SystemExit(
            "current-version owner is untracked: " + ", ".join(untracked_owners)
        )

version_token = re.compile(
    r"(?<![A-Za-z0-9])v?"
    r"(?P<value>[0-9]+\.[0-9]+\.[0-9]+"
    r"(?:-[A-Za-z0-9][A-Za-z0-9._+~:-]*)?)"
    r"(?![A-Za-z0-9._+~:-])"
)


def duplicate_current_literals(
    repository: Path, consumers: list[str] | tuple[str, ...], version: str
) -> list[str]:
    duplicates: list[str] = []
    for relative_path in consumers:
        path = repository / relative_path
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if any(match.group("value") == version for match in version_token.finditer(line)):
                duplicates.append(f"{relative_path}:{line_number}:{line}")
    return duplicates


duplicates = duplicate_current_literals(
    repo_root, source_consumers, canonical_version
)
if duplicates:
    raise SystemExit(
        "tracked current-version source consumer duplicates root VERSION:\n"
        + "\n".join(duplicates)
    )

makefile = (repo_root / "Makefile").read_text(encoding="utf-8")
if not re.search(r"(?m)^VERSION_FILE\s*:=\s*VERSION\s*$", makefile):
    raise SystemExit("Makefile no longer names root VERSION as its input")
if not re.search(
    r"(?m)^VERSION\s*:=\s*\$\(strip \$\(shell cat \$\(VERSION_FILE\)",
    makefile,
):
    raise SystemExit("Makefile no longer derives its current version from VERSION_FILE")

pkgbuild = (repo_root / "PKGBUILD").read_text(encoding="utf-8")
if len(re.findall(r"(?m)^pkgver=\$\(_read_version_file VERSION\)\s*$", pkgbuild)) != 1:
    raise SystemExit("PKGBUILD current pkgver is not one projection of root VERSION")

for template_name, projection_name in man_projections:
    template = (repo_root / template_name).read_text(encoding="utf-8")
    if "@VERSION@" not in template:
        raise SystemExit(f"{template_name} lacks its current-version projection token")
    expected_projection = template.replace("@VERSION@", canonical_version)
    actual_projection = (repo_root / projection_name).read_text(encoding="utf-8")
    if actual_projection != expected_projection:
        raise SystemExit(
            f"{projection_name} is not the actual projection of {template_name} and VERSION"
        )

catalog_header = re.compile(
    r'^"Project-Id-Version: Moguet (?P<value>[^"\\]+)\\n"$', re.MULTILINE
)
for catalog_name in catalog_projections:
    catalog = (repo_root / catalog_name).read_text(encoding="utf-8")
    projected_versions = [
        match.group("value") for match in catalog_header.finditer(catalog)
    ]
    if projected_versions != [canonical_version]:
        raise SystemExit(
            f"{catalog_name} current-version projection is {projected_versions!r}; "
            f"expected {[canonical_version]!r}"
        )

# Focused regression: a newly tracked production TU automatically enters the
# semantic domain without an owner-list update.  A synthetic package release
# with the canonical X.Y.Z prefix is not the current project version, while the
# exact current literal in source/version_presentation.cpp is rejected.  Ignored
# and untracked files do not enter the Git-owned set.
regression_root = scratch_root / "current-version-semantic-regression"
regression_root.mkdir()
(regression_root / "source").mkdir()
(regression_root / ".gitignore").write_text("ignored-current.txt\n", encoding="utf-8")
(regression_root / "source/version_presentation.cpp").write_text(
    f'constexpr auto package_version = "{canonical_version}-1";\n', encoding="utf-8"
)
(regression_root / "ignored-current.txt").write_text(
    f"current_version={canonical_version}\n", encoding="utf-8"
)
(regression_root / "untracked-current.txt").write_text(
    f"current_version={canonical_version}\n", encoding="utf-8"
)
try:
    subprocess.run(
        ["git", "-C", str(regression_root), "init", "--quiet"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    subprocess.run(
        [
            "git", "-C", str(regression_root), "add", ".gitignore",
            "source/version_presentation.cpp",
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
except (OSError, subprocess.CalledProcessError) as error:
    raise SystemExit(f"current-version tracked-only regression setup failed: {error}")

regression_tracked = tracked_paths(regression_root)
if regression_tracked != {".gitignore", "source/version_presentation.cpp"}:
    raise SystemExit(
        f"current-version tracked-only regression has unexpected owners: {regression_tracked!r}"
    )
regression_production_sources = production_source_paths(
    regression_root, regression_tracked
)
if regression_production_sources != ("source/version_presentation.cpp",):
    raise SystemExit(
        "new tracked production TU did not enter the current-version semantic domain"
    )
if duplicate_current_literals(
    regression_root, regression_production_sources, canonical_version
):
    raise SystemExit("synthetic package release was mistaken for current VERSION")

(regression_root / "source/version_presentation.cpp").write_text(
    f'constexpr auto current_version = "{canonical_version}";\n', encoding="utf-8"
)
if len(
    duplicate_current_literals(
        regression_root, regression_production_sources, canonical_version
    )
) != 1:
    raise SystemExit(
        "tracked source/version_presentation.cpp current-version duplicate was not rejected"
    )
print("current-version production-TU counterexample: rejected")
PY

printf '%s\n' 'fixture authority tests: all checks passed'
