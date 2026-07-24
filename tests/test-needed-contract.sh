#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
JPACKER_TEST_REPOSITORY_ROOT=$repo_root
export JPACKER_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"
tmp_dir=$(mktemp -d)
normal_server_pid=
schema_server_pid=

cleanup() {
    for server_pid in $normal_server_pid $schema_server_pid; do
        if [ -n "$server_pid" ]; then
            kill "$server_pid" 2>/dev/null || true
            wait "$server_pid" 2>/dev/null || true
        fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

wait_for_fixture() {
    port_file=$1
    attempt=0
    while [ ! -s "$port_file" ]; do
        attempt=$((attempt + 1))
        if [ "$attempt" -gt 100 ]; then
            echo "fixture server did not start: $port_file" >&2
            exit 1
        fi
        sleep 0.05
    done
}

normal_port_file=$tmp_dir/normal-port
normal_request_log=$tmp_dir/normal-requests.log
schema_port_file=$tmp_dir/schema-port
schema_request_log=$tmp_dir/schema-requests.log
: > "$normal_request_log"
: > "$schema_request_log"

python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-conflicts-replaces.json" \
    "$normal_port_file" "$normal_request_log" &
normal_server_pid=$!
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-validation.json" \
    "$schema_port_file" "$schema_request_log" &
schema_server_pid=$!

wait_for_fixture "$normal_port_file"
wait_for_fixture "$schema_port_file"
normal_port=$(cat "$normal_port_file")
schema_port=$(cat "$schema_port_file")
normal_rpc_url=http://127.0.0.1:$normal_port/rpc/
schema_rpc_url=http://127.0.0.1:$schema_port/rpc/

export PATH=$repo_root/tests/stubs:/usr/bin:/bin
require_exact_test_command pacman-conf "$repo_root/tests/stubs/pacman-conf"
require_exact_test_command makepkg "$repo_root/tests/stubs/makepkg"
require_exact_test_command pacman "$repo_root/tests/stubs/pacman"
require_exact_test_command sudo "$repo_root/tests/stubs/sudo"
require_exact_test_command git "$repo_root/tests/stubs/git"

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    : > "$normal_request_log"
    : > "$schema_request_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_AUR_RPC_BASE_URL=$normal_rpc_url
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=0
    export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_PACKAGE_METADATA_PACMAN_CONF_EXIT_CODE
    unset JPACKER_TEST_PACKAGE_METADATA_PACMAN_CONF_FAILURE_AT
}

run_ok() {
    : > "$command_log"
    : > "$normal_request_log"
    : > "$schema_request_log"
    if ! "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    : > "$normal_request_log"
    : > "$schema_request_log"
    if "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to fail: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_contains() {
    pattern=$1
    file=$2
    if ! grep -F -- "$pattern" "$file" >/dev/null; then
        echo "missing expected output: $pattern" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_not_contains() {
    pattern=$1
    file=$2
    if grep -F -- "$pattern" "$file" >/dev/null; then
        echo "unexpected output: $pattern" >&2
        sed -n '1,240p' "$file" >&2
        exit 1
    fi
}

assert_command() {
    expected=$1
    if ! grep -Fx -- "$expected" "$command_log" >/dev/null; then
        echo "missing expected command: $expected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_absent() {
    unexpected=$1
    if grep -Fx -- "$unexpected" "$command_log" >/dev/null; then
        echo "unexpected command: $unexpected" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_count() {
    expected=$1
    expected_count=$2
    actual_count=$(grep -Fxc -- "$expected" "$command_log" || true)
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected command count for: $expected" >&2
        echo "expected $expected_count, got $actual_count" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_prefix_count() {
    expected_prefix=$1
    expected_count=$2
    actual_count=$(awk -v prefix="$expected_prefix" \
        'index($0, prefix) == 1 { ++count } END { print count + 0 }' \
        "$command_log")
    if [ "$actual_count" -ne "$expected_count" ]; then
        echo "unexpected command prefix count for: $expected_prefix" >&2
        echo "expected $expected_count, got $actual_count" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_prefix() {
    assert_command_prefix_count "$1" 1
}

assert_only_command() {
    expected=$1
    assert_command_count "$expected" 1
    if [ "$(wc -l < "$command_log")" -ne 1 ]; then
        echo "unexpected additional command(s)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "external command ran before validation completed" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_source_build_commands() {
    if grep -E '^(git|makepkg) ' "$command_log" >/dev/null; then
        echo "pacman-only route reached a source-build command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_mutation_commands() {
    if grep -E '^(sudo|git|makepkg) |^pacman -S( |$)' "$command_log" >/dev/null; then
        echo "preflight guard allowed an external mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_installed_version_precheck() {
    if grep -E '^pacman -Q(m)?( |$)' "$command_log" >/dev/null; then
        echo "sync --needed added an installed-version build-skip precheck" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_normal_request_log_empty() {
    if [ -s "$normal_request_log" ]; then
        echo "unexpected AUR RPC request" >&2
        cat "$normal_request_log" >&2
        exit 1
    fi
}

assert_normal_request_log_nonempty() {
    if [ ! -s "$normal_request_log" ]; then
        echo "expected an AUR RPC request" >&2
        exit 1
    fi
}

assert_schema_request_log_nonempty() {
    if [ ! -s "$schema_request_log" ]; then
        echo "expected a schema fixture request" >&2
        exit 1
    fi
}

assert_cache_entry_absent() {
    entry=$XDG_CACHE_HOME/jpacker/$1
    if [ -e "$entry" ] || [ -L "$entry" ]; then
        echo "cache entry was created before all preflight guards passed: $entry" >&2
        exit 1
    fi
}

assert_cache_root_absent() {
    if [ -e "$XDG_CACHE_HOME/jpacker" ] || [ -L "$XDG_CACHE_HOME/jpacker" ]; then
        echo "jpacker cache root was created before CLI validation completed" >&2
        exit 1
    fi
}

assert_command_before() {
    first=$1
    second=$2
    first_line=$(grep -nFx -- "$first" "$command_log" | sed -n '1s/:.*//p')
    second_line=$(grep -nFx -- "$second" "$command_log" | sed -n '1s/:.*//p')
    if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
        echo "unexpected command order: $first -> $second" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_separated_source_install() {
    expected_makepkg=$1
    expected_sudo_prefix=$2

    assert_command_count "pacman-conf --verbose RootDir DBPath" 1
    assert_command_count "makepkg --packagelist" 1
    assert_command_count "$expected_makepkg" 1
    makepkg_count=$(grep -c '^makepkg ' "$command_log" || true)
    if [ "$makepkg_count" -ne 2 ]; then
        echo "expected packagelist and build-only makepkg commands" >&2
        cat "$command_log" >&2
        exit 1
    fi
    if grep -E '^makepkg .*--needed( |$)' "$command_log" >/dev/null; then
        echo "build-only makepkg received install-only --needed" >&2
        cat "$command_log" >&2
        exit 1
    fi
    assert_command_prefix "pacman -U --print --print-format "
    assert_command_prefix "$expected_sudo_prefix"
}

prepare_source_preference() {
    package=$1
    printf 'CFLAGS=-Oneeded-contract-test\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/$package"
}

assert_preference_unchanged() {
    package=$1
    expected_checksum=$2
    preference=$JPACKER_TEST_PACKAGE_BUILD_DIR/$package
    if [ ! -f "$preference" ] || [ "$(cksum "$preference")" != "$expected_checksum" ]; then
        echo "source-build preference changed unexpectedly: $preference" >&2
        exit 1
    fi
}

# Matrix A: pacman-only routeではordered argvをそのまま保持する。
setup_case pacman-auto
export JPACKER_TEST_PACMAN_REPO_PACKAGES=repo-pkg
run_ok -S --needed repo-pkg
assert_command_count "pacman -Si repo-pkg" 1
assert_command_count "sudo pacman -S --needed repo-pkg" 1
if [ "$(wc -l < "$command_log")" -ne 2 ]; then
    echo "automatic repository route ran unexpected commands" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_command_before "pacman -Si repo-pkg" "sudo pacman -S --needed repo-pkg"
assert_no_source_build_commands
assert_normal_request_log_empty

setup_case pacman-repo-only
run_ok -S --repo --needed repo-pkg
assert_only_command "sudo pacman -S --needed repo-pkg"
assert_no_source_build_commands
assert_normal_request_log_empty

setup_case pacman-system-upgrade
run_ok -Syu --needed
assert_only_command "sudo pacman -Syu --needed"
assert_no_source_build_commands
assert_normal_request_log_empty

setup_case system-upgrade-with-source-target
run_ok --noedit --nodiff -Syu --needed clean-root
assert_command_count "sudo pacman -Syu --needed" 1
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
sudo_count=$(grep -c '^sudo ' "$command_log" || true)
if [ "$sudo_count" -ne 2 ]; then
    echo "system upgrade with a source target ran an unexpected sudo transaction" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_command_before "sudo pacman -Syu --needed" "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_normal_request_log_nonempty

setup_case pacman-repo-search
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Ss --repo --needed keyword
assert_only_command "pacman -Ss --needed keyword"
assert_normal_request_log_empty

setup_case pacman-repo-info
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Si --repo --needed repo-pkg
assert_only_command "pacman -Si --needed repo-pkg"
assert_normal_request_log_empty

# Matrix B: AUR/source-buildでは通常のplan/buildを通り、final installへだけ変換する。
setup_case aur-only
run_ok --noedit --nodiff -S --aur --needed clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
assert_command_absent "pacman -Si clean-root"
assert_normal_request_log_nonempty

setup_case aur-review-not-skipped
run_ok --nodiff --noconfirm -S --aur --needed clean-root
assert_contains "Review target: PKGBUILD" "$output_file"
assert_not_contains "Skipping PKGBUILD/.install review (--noedit)" "$output_file"
assert_separated_source_install \
    "makepkg -sc --noconfirm" \
    "sudo pacman -U --noconfirm --needed -- "
assert_normal_request_log_nonempty

setup_case aur-auto
run_ok --noedit --nodiff -S --needed clean-root
assert_command "pacman -Si clean-root"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
assert_command_absent "sudo pacman -S --needed clean-root"
assert_normal_request_log_nonempty

setup_case preferred-official
prepare_source_preference clean-root
preference_checksum=$(cksum "$JPACKER_TEST_PACKAGE_BUILD_DIR/clean-root")
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
run_ok --noedit --nodiff -S --needed clean-root
assert_command "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git clean-root"
assert_command_absent "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
assert_command_absent "sudo pacman -S --needed clean-root"
assert_preference_unchanged clean-root "$preference_checksum"
assert_normal_request_log_empty

setup_case preferred-official-repo-override
prepare_source_preference clean-root
preference_checksum=$(cksum "$JPACKER_TEST_PACKAGE_BUILD_DIR/clean-root")
run_ok -S --repo --needed clean-root
assert_only_command "sudo pacman -S --needed clean-root"
assert_no_source_build_commands
assert_preference_unchanged clean-root "$preference_checksum"
assert_normal_request_log_empty

# Matrix C: mixed routeは全source preflight後にofficial transactionを行い、両routeへneededを保持する。
setup_case mixed-success
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok --noedit --nodiff -S --needed official-a clean-root
assert_command_count "sudo pacman -S --needed official-a" 1
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
sudo_count=$(grep -c '^sudo ' "$command_log" || true)
if [ "$sudo_count" -ne 2 ]; then
    echo "mixed route ran an unexpected sudo transaction" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_command_before "sudo pacman -S --needed official-a" "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_normal_request_log_nonempty

setup_case mixed-ordered-index
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok --noedit --nodiff -S official-a --needed clean-root
assert_command_count "sudo pacman -S official-a --needed" 1
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
sudo_count=$(grep -c '^sudo ' "$command_log" || true)
if [ "$sudo_count" -ne 2 ]; then
    echo "mixed ordered route ran an unexpected sudo transaction" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_normal_request_log_nonempty

setup_case mixed-preflight-failure
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_fail --noedit --nodiff -S --needed official-a missing-aur-package
assert_contains "not found" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent missing-aur-package
assert_normal_request_log_nonempty

# Matrix D: installed stateやlocal artifactをjpacker独自のbuild skip判定へ流用しない。
setup_case same-installed-version
JPACKER_TEST_PACMAN_QM_OUTPUT='clean-root 1.0-1'
export JPACKER_TEST_PACMAN_QM_OUTPUT
run_ok --noedit --nodiff -S --aur --needed clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
assert_no_installed_version_precheck
assert_normal_request_log_nonempty

setup_case local-artifact
mkdir -p "$XDG_CACHE_HOME/jpacker/clean-root/.git"
: > "$XDG_CACHE_HOME/jpacker/clean-root/PKGBUILD"
: > "$XDG_CACHE_HOME/jpacker/clean-root/clean-root-1.0-1-any.pkg.tar.zst"
run_ok --noedit --nodiff --noconfirm -S --aur --needed clean-root
assert_command "git fetch origin"
assert_command "git reset --hard origin/main"
assert_separated_source_install \
    "makepkg -sc --noconfirm" \
    "sudo pacman -U --noconfirm --needed -- "
assert_normal_request_log_nonempty

# Matrix E: build optionとinstall-only optionは独立し、makepkg argv順序を安定させる。
run_aur_combination() {
    combination_name=$1
    expected_makepkg=$2
    expected_sudo_prefix=$3
    shift 3
    setup_case "combination-$combination_name"
    run_ok --noedit --nodiff -S --aur "$@" clean-root
    assert_separated_source_install "$expected_makepkg" "$expected_sudo_prefix"
    assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
    assert_normal_request_log_nonempty
}

run_aur_combination rebuild \
    "makepkg -sc -f" \
    "sudo pacman -U --needed -- " \
    --needed --rebuild
run_aur_combination cleanbuild \
    "makepkg -sc -C" \
    "sudo pacman -U --needed -- " \
    --needed --cleanbuild
run_aur_combination noconfirm \
    "makepkg -sc --noconfirm" \
    "sudo pacman -U --noconfirm --needed -- " \
    --needed --noconfirm
run_aur_combination all-supported \
    "makepkg -sc --noconfirm -f -C" \
    "sudo pacman -U --noconfirm --needed -- " \
    --needed --rebuild --cleanbuild --noconfirm

setup_case combination-rmdeps
run_fail --noedit --nodiff -S --aur --needed --rmdeps clean-root
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty
assert_cache_entry_absent clean-root
assert_normal_request_log_empty

setup_case combination-rmdeps-all
run_fail --noedit --nodiff -S --aur \
    --needed --rebuild --cleanbuild --rmdeps --noconfirm clean-root
assert_contains "Separated build/install does not support --rmdeps." "$output_file"
assert_command_log_empty
assert_cache_entry_absent clean-root
assert_normal_request_log_empty

# Matrix F: token roleを無視してoption value/opaque/attached formをsemantic neededへ昇格させない。
setup_case parser-query-option-value
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Q --config --needed filesystem
assert_only_command "pacman -Q --config --needed filesystem"

setup_case parser-root-option-value
run_fail -S --root --needed clean-root
assert_contains "Unsupported pacman option for AUR/source-build target: --root" "$output_file"
assert_no_mutation_commands
assert_normal_request_log_empty

setup_case parser-opaque-direct
run_ok -U -- --needed
assert_only_command "sudo pacman -U -- --needed"
assert_normal_request_log_empty

setup_case parser-opaque-sync
run_fail -S -- --needed
assert_contains "Invalid package name: --needed" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case parser-attached
run_fail -S --aur --needed=true clean-root
assert_contains "Unsupported pacman option for AUR/source-build target: --needed=true" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case parser-duplicate-source
run_ok --noedit --nodiff -S --aur --needed --needed clean-root
assert_separated_source_install "makepkg -sc" "sudo pacman -U --needed -- "
assert_normal_request_log_nonempty

setup_case parser-duplicate-repo
run_ok -S --repo --needed --needed repo-pkg
assert_only_command "sudo pacman -S --needed --needed repo-pkg"
assert_normal_request_log_empty

# Matrix G: search/infoのAurOnlyではinstall optionを黙って無視せず、RPC前に停止する。
setup_case aur-search-rejects-needed
run_fail -Ss --aur --needed keyword
assert_contains "Unsupported pacman option for AUR search: --needed" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case aur-info-rejects-needed
run_fail -Si --aur --needed clean-root
assert_contains "Unsupported pacman option for AUR info: --needed" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

# Matrix H: --needed併用でもplan/schema/identifier guardを突破しない。
setup_case guard-missing-root
run_fail -S --aur --needed
assert_contains "Missing AUR package target" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case guard-missing
run_fail --noedit --nodiff -S --aur --needed missing-aur-package
assert_contains "AUR package not found" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent missing-aur-package
assert_normal_request_log_nonempty

setup_case guard-multiple-second-missing
run_fail --noedit --nodiff -S --aur --needed clean-root missing-aur-package
assert_contains "AUR package not found" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent clean-root
assert_cache_entry_absent missing-aur-package
assert_normal_request_log_nonempty

while IFS='|' read -r guard_name guard_target expected; do
    setup_case "guard-$guard_name"
    run_fail --noedit --nodiff -S --aur --needed "$guard_target"
    assert_contains "$expected" "$output_file"
    assert_no_mutation_commands
    assert_cache_entry_absent "$guard_target"
    assert_normal_request_log_nonempty
done <<'GUARDS'
metadata-risk|risk-root|conflicts/replaces metadata requires manual review
unresolved|unresolved-root|unresolved dependencies
ambiguous-provider|ambiguous-root|ambiguous providers
cycle|cycle-root|cyclic dependencies
split|split-child|split package install target selection is not implemented
GUARDS

setup_case guard-invalid-identifier
run_fail -S --aur --needed ../escape
assert_contains "Invalid AUR package target" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case guard-repository-qualified
run_fail -S --aur --needed core/filesystem
assert_contains "Invalid AUR package target" "$output_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case guard-schema
export JPACKER_TEST_AUR_RPC_BASE_URL=$schema_rpc_url
run_fail --noedit --nodiff -S --aur --needed invalid-root-preflight
assert_contains "AUR RPC response validation failed for package info invalid-root-preflight" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent invalid-root-preflight
assert_schema_request_log_nonempty

# Matrix I: custom upgradeはoptionless、generic -Syuはpacman-onlyという境界を固定する。
setup_case custom-upgrade-rejects-needed
prepare_source_preference clean-root
run_fail upgrade --needed
assert_contains "Unsupported upgrade option: --needed" "$output_file"
assert_command_log_empty
assert_cache_root_absent
assert_normal_request_log_empty

setup_case generic-system-upgrade-separation
prepare_source_preference clean-root
preference_checksum=$(cksum "$JPACKER_TEST_PACKAGE_BUILD_DIR/clean-root")
run_ok -Syu --needed
assert_only_command "sudo pacman -Syu --needed"
assert_no_source_build_commands
assert_preference_unchanged clean-root "$preference_checksum"
assert_normal_request_log_empty

echo "--needed contract integration tests: all checks passed"
