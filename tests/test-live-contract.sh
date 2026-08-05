#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
live_root=$repo_root/containers/arch-live-validation
readme_file=$live_root/README.md
local_package_file=$live_root/fixtures/local-package/PKGBUILD
local_contract_file=$live_root/fixtures/local-package/contract.env
aur_case_file=$live_root/aur-cases.tsv
live_dockerfile=$live_root/Dockerfile
provider_runner=$live_root/run-provider-selection.sh
pacman_sentinel=$live_root/pacman-sentinel.sh
dockerignore_file=$repo_root/.dockerignore
makefile=$repo_root/Makefile
offline_dockerfile=$repo_root/containers/arch-validation/Dockerfile
offline_runner=$repo_root/containers/arch-validation/run-tests.sh

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

assert_regular_file() {
    path=$1
    label=$2
    if [ ! -f "$path" ] || [ -L "$path" ]; then
        fail "required file must be a regular non-symlink: $label ($path)"
    fi
}

assert_contains() {
    file=$1
    expected=$2
    if ! grep -F -- "$expected" "$file" >/dev/null; then
        fail "missing contract entry in $file: $expected"
    fi
}

assert_not_contains() {
    file=$1
    unexpected=$2
    if grep -F -- "$unexpected" "$file" >/dev/null; then
        fail "forbidden contract entry in $file: $unexpected"
    fi
}

make_target_body() {
    target_name=$1
    awk -v target="$target_name" '
        $0 == target ":" {
            in_target = 1
        }
        in_target && $0 != target ":" &&
            $0 ~ /^[A-Za-z0-9_.-]+([[:space:]]+[A-Za-z0-9_.-]+)*:/ {
            exit
        }
        in_target {
            print
        }
    ' "$makefile"
}

assert_regular_file "$readme_file" 'live validation contract'
assert_regular_file "$local_package_file" 'local package PKGBUILD'
assert_regular_file "$local_contract_file" 'local package contract env'
assert_regular_file "$aur_case_file" 'AUR case authority table'
assert_regular_file "$live_dockerfile" 'live provider Dockerfile'
assert_regular_file "$provider_runner" 'live provider runner'
assert_regular_file "$pacman_sentinel" 'live pacman sentinel source'
assert_regular_file "$dockerignore_file" 'Docker context exclusion contract'
assert_regular_file "$offline_dockerfile" 'offline validation Dockerfile'
assert_regular_file "$offline_runner" 'offline validation runner'

assert_contains "$readme_file" "live laneは既存のoffline validation lane"
assert_contains "$readme_file" "ベースイメージは \`archlinux:latest\` を利用する"
assert_contains "$readme_file" "実行時ネットワークは**有効**"
assert_contains "$readme_file" "--privileged"
assert_contains "$readme_file" "暗黙のフォールバックを許容せず"
assert_contains "$readme_file" "make test\` / \`release-check\` を再帰的に起動しない"
assert_contains "$local_package_file" "pkgname='moguet-live-fixture'"

if ! grep -E '^pkgver=1\.0\.0$' "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep pkgver 1.0.0'
fi
if ! grep -E '^pkgrel=1$' "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep pkgrel 1'
fi
if ! grep -F "makedepends=('cargo')" "$local_package_file" >/dev/null; then
    fail 'local PKGBUILD must keep makedepends=(\"cargo\")'
fi
if grep -E '^source=\(.+\)$' "$local_package_file" >/dev/null; then
    fail 'local fixture source list must be empty for no source download'
fi
if ! grep -F 'source=()' "$local_package_file" >/dev/null; then
    fail 'local fixture must not require external source'
fi
if ! grep -F 'live-fixture-marker' "$local_package_file" >/dev/null; then
    fail 'local fixture package() must install marker artifact'
fi
if [ -e "$live_root/fixtures/local-package/.SRCINFO" ]; then
    fail 'local fixture must not track .SRCINFO for live contract'
fi

assert_contract_assignment() {
    expected=$1
    if ! grep -Fx -- "$expected" "$local_contract_file" >/dev/null; then
        fail "missing local fixture contract assignment: $expected"
    fi
}

assert_contract_assignment 'REQUIRED_MAKE_DEPENDENCY=cargo'
assert_contract_assignment 'EXPECTED_PROVIDER_PACKAGES=rust,rustup'
assert_contract_assignment 'PROVIDER_INSTALL_REASON=Dependency'
assert_contract_assignment 'ROOT_ARTIFACT_INSTALL_REASON=Explicit'

if grep -F 'SELECTED_MAKE_PROVIDER_EXPECTED=' "$local_contract_file" >/dev/null; then
    fail 'local fixture must not treat cargo as a selected provider package'
fi

case_count=0
tab=$(printf '\tX')
tab=${tab%X}
while IFS=$tab read -r package_name package_base expected_version runtime_deps make_deps source_kind install_reason fallback_policy review_required; do
    if [ -z "$package_name" ] || [ "${package_name#'#'}" != "$package_name" ]; then
        continue
    fi

    case_count=$((case_count + 1))
    if [ "$package_name" != 'fetchfetch' ]; then
        fail "AUR case package must be fetchfetch: $package_name"
    fi
    if [ "$package_base" != 'fetchfetch' ]; then
        fail "AUR case PackageBase must be fetchfetch: $package_base"
    fi
    if [ "$expected_version" != '2.0.0-1' ]; then
        fail "unexpected AUR case version: $expected_version"
    fi
    if [ "$runtime_deps" != 'glibc' ]; then
        fail "unexpected AUR runtime dependency contract: $runtime_deps"
    fi
    if [ "$make_deps" != 'gcc,make' ]; then
        fail "unexpected AUR make dependencies contract: $make_deps"
    fi
    if [ "$source_kind" != 'single-release-archive' ]; then
        fail "unexpected AUR source-kind contract: $source_kind"
    fi
    if [ "$install_reason" != 'Explicit' ]; then
        fail "unexpected AUR install reason contract: $install_reason"
    fi
    if [ "$fallback_policy" != 'reject' ]; then
        fail "AUR implicit fallback must be rejected: $fallback_policy"
    fi
    if [ "$review_required" != 'required' ]; then
        fail "AUR case must request review on authoritative drift: $review_required"
    fi
done < "$aur_case_file"

if [ "$case_count" -ne 1 ]; then
    fail "expected exactly one AUR case for slice-1 contract"
fi
if ! grep -R --line-number -- 'fetchfetch' "$live_root" >/dev/null; then
    fail 'AUR case contract file must include fetchfetch entry'
fi

# Ensure offline validation lane is not forced as a live dependency by contract text.
assert_contains "$readme_file" "offline validation lane"

# Slice 2 keeps a standalone image and production binary/runtime route.
assert_contains "$live_dockerfile" 'FROM archlinux:latest'
assert_contains "$live_dockerfile" 'pacman -Syu --needed --noconfirm'
assert_contains "$live_dockerfile" 'COPY --chown=moguet-validation:moguet-validation . .'
assert_contains "$live_dockerfile" 'USER moguet-validation:moguet-validation'
assert_contains "$live_dockerfile" 'make -j8 --output-sync=target'
assert_contains "$live_dockerfile" 'CMD ["containers/arch-live-validation/run-provider-selection.sh"]'
assert_contains "$provider_runner" 'production_moguet=$repo_root/moguet'
assert_contains "$provider_runner" 'makepkg --printsrcinfo > .SRCINFO'
assert_contains "$provider_runner" 'python3 "$pty_runner" --timeout 90 --'
assert_contains "$provider_runner" 'candidate_package'
assert_contains "$provider_runner" 'rust_choice=$(awk'
assert_contains "$provider_runner" 'rustup_choice=$(awk'
assert_contains "$provider_runner" 'fail "provider drift: unexpected cargo provider'

if grep -E '^[[:space:]]+(rust|rustup|cargo)([[:space:]\\]|$)' \
    "$live_dockerfile" >/dev/null; then
    fail 'live image must not preinstall rust, rustup, or cargo'
fi
if grep -E '(^|[[:space:]])(include|inherit|COPY)[[:space:]].*arch-validation' \
    "$live_dockerfile" >/dev/null; then
    fail 'live Dockerfile must not include, inherit, or copy the offline lane'
fi
assert_not_contains "$live_dockerfile" 'containers/arch-validation'

# Host metadata, credentials, state, and artifacts never enter COPY layers.
for excluded_path in \
    '.git' \
    '.agents' \
    '.codex' \
    '.ssh' \
    '.gnupg' \
    '.git-credentials' \
    '.netrc' \
    '*.sock' \
    '**/__pycache__' \
    '**/*.py[cod]'
do
    assert_contains "$dockerignore_file" "$excluded_path"
done

# The canonical pacman path is replaced only inside the fully provisioned
# image.  Both the source and runtime copy are immutable to the validation
# user, and the original executable is root-only and never exec'd by a case.
assert_contains "$live_dockerfile" 'install -o root -g root -m 0555'
assert_contains "$live_dockerfile" 'pacman-sentinel.sh'
assert_contains "$live_dockerfile" '/usr/bin/pacman'
assert_contains "$live_dockerfile" '/usr/libexec/moguet-live-validation/pacman.real'
assert_contains "$live_dockerfile" 'install -d -o root -g root -m 0711'
assert_contains "$live_dockerfile" 'moguet-validation ALL=(root) NOPASSWD: /usr/bin/pacman *'
assert_contains "$pacman_sentinel" 'readonly_sentinel_status=86'
assert_contains "$pacman_sentinel" "printf '%s\\0' sudo pacman"
assert_contains "$pacman_sentinel" "[ \"\$#\" -eq 5 ]"
assert_contains "$pacman_sentinel" "[ \"\$#\" -eq 6 ]"
assert_contains "$pacman_sentinel" "[ \"\$1\" = '-S' ]"
assert_contains "$pacman_sentinel" "[ \"\$2\" = '--asdeps' ]"
assert_contains "$pacman_sentinel" "[ \"\$3\" = '--needed' ]"
assert_contains "$pacman_sentinel" "[ \"\$4\" = '--noconfirm' ]"
assert_contains "$pacman_sentinel" 'extra/rust|extra/rustup'
assert_contains "$pacman_sentinel" "reject 'target is not an allowed repo-qualified provider'"
assert_contains "$provider_runner" "sentinel-reject-syu 'rejected argv'"
assert_not_contains "$pacman_sentinel" 'pacman.real'
assert_not_contains "$pacman_sentinel" 'exec '

# Candidate numbers are derived from identity fields rather than fixed order.
if grep -E 'rust(up)?_choice=[12]($|[^0-9])' "$provider_runner" >/dev/null; then
    fail 'provider runner must not hard-code rust/rustup candidate numbers'
fi
assert_contains "$provider_runner" "candidate_source\" != repository"
assert_contains "$provider_runner" "candidate_repository\" != extra"
assert_contains "$provider_runner" "candidate_dependency\" != cargo"
assert_contains "$provider_runner" 'provider candidate set contains duplicate package identities'
assert_contains "$provider_runner" 'provider presentation changed after discovery'
assert_contains "$provider_runner" "candidate_package\" in"
assert_contains "$provider_runner" 'rust|rustup)'
assert_not_contains "$provider_runner" 'fallback_package='
assert_not_contains "$provider_runner" 'fallback_dependency='

# The explicit live target has no host mount, privileged mode, Docker socket,
# host package state, or runtime network disablement.  Failure remains the
# docker command's non-zero status.
live_target=$(make_target_body test-container-live-provider)
printf '%s\n' "$live_target" | grep -F -- '$(DOCKER) build --pull' >/dev/null ||
    fail 'live target must use docker build --pull'
printf '%s\n' "$live_target" | grep -F -- \
    '--file containers/arch-live-validation/Dockerfile' >/dev/null ||
    fail 'live target must select the standalone live Dockerfile'
printf '%s\n' "$live_target" | grep -F -- '$(DOCKER) run --rm' >/dev/null ||
    fail 'live target must destroy its container with --rm'
for forbidden_runtime_option in \
    '--privileged' \
    '--network=none' \
    '--mount' \
    '--volume' \
    'docker.sock' \
    '/var/lib/pacman' \
    '/etc/pacman.conf' \
    '/var/cache/pacman'
do
    if printf '%s\n' "$live_target" | grep -F -- "$forbidden_runtime_option" >/dev/null; then
        fail "live Make target contains forbidden runtime option: $forbidden_runtime_option"
    fi
done

live_target_reference_count=$(grep -F -c \
    'test-container-live-provider' "$makefile")
if [ "$live_target_reference_count" -ne 2 ]; then
    fail 'live provider target must appear only in .PHONY and its explicit definition'
fi
test_target=$(make_target_body test)
release_target=$(make_target_body release-check)
if printf '%s\n%s\n' "$test_target" "$release_target" |
    grep -F 'test-container-live-provider' >/dev/null; then
    fail 'make test or release-check must not recursively start the live lane'
fi

# Existing offline files and target remain isolated from the new live paths.
assert_not_contains "$offline_dockerfile" 'arch-live-validation'
assert_not_contains "$offline_runner" 'arch-live-validation'
offline_target=$(make_target_body test-container)
assert_not_contains_target=$(printf '%s\n' "$offline_target" |
    grep -F 'arch-live-validation' || true)
if [ -n "$assert_not_contains_target" ]; then
    fail 'existing test-container recipe contains live-lane paths'
fi
printf '%s\n' "$offline_target" | grep -F -- '--network=none' >/dev/null ||
    fail 'existing offline target lost its offline runtime network boundary'

# The tracked fixture remains the only source authority; generated metadata is
# case-local and real package/source execution remains beyond Slice 2.
if find "$live_root/fixtures/local-package" -mindepth 1 -name .SRCINFO -print |
    grep . >/dev/null; then
    fail 'tracked live fixture must not contain .SRCINFO'
fi
assert_contains "$provider_runner" "assert_sentinel_absent \"\$current_case\""
assert_contains "$provider_runner" "assert_not_contains \"Running: 'git'\""
assert_contains "$provider_runner" "assert_not_contains \"Running: 'makepkg'\""
assert_contains "$provider_runner" \
    "assert_not_contains \"Running: 'sudo' 'pacman' '-U'\""
assert_contains "$provider_runner" "-name '.artifact-workspace~-*'"
assert_contains "$provider_runner" 'package_database_manifest'
assert_contains "$provider_runner" 'tracked_fixture_manifest'

assert_contains "$readme_file" 'real Arch repository metadata'
assert_contains "$readme_file" 'review-required failure'
assert_contains "$readme_file" '実package transactionは行わない'
assert_contains "$readme_file" 'Slice 3: real AUR'
assert_contains "$readme_file" 'Slice 4: real dependency transaction'
assert_contains "$readme_file" 'image / layer cacheはhost localに残り得る'
assert_contains "$readme_file" 'host systemへpackage mutationは行わない'

printf '%s\n' 'live contract tests: all checks passed'
