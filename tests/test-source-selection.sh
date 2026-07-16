#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
server_pid=

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

port_file=$tmp_dir/port
request_log=$tmp_dir/aur-requests.log
: > "$request_log"
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-conflicts-replaces.json" \
    "$port_file" "$request_log" &
server_pid=$!

attempt=0
while [ ! -s "$port_file" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -gt 100 ]; then
        echo "fixture server did not start" >&2
        exit 1
    fi
    sleep 0.05
done

port=$(cat "$port_file")
export PATH=$repo_root/tests/stubs:/usr/bin:/bin
export JPACKER_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:$port/rpc/

setup_case() {
    case_name=$1
    case_dir=$tmp_dir/cases/$case_name
    command_log=$case_dir/commands.log
    output_file=$case_dir/output

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache" "$case_dir/package.build"
    : > "$command_log"
    : > "$request_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=0
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
    unset JPACKER_TEST_GIT_CLONE_FIXTURE_DIR
    unset JPACKER_TEST_MAKEPKG_EXIT_CODE
}

run_ok() {
    : > "$command_log"
    : > "$request_log"
    if ! "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    : > "$request_log"
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

assert_only_command() {
    expected=$1
    assert_command "$expected"
    if [ "$(wc -l < "$command_log")" -ne 1 ]; then
        echo "unexpected additional command(s)" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "external command ran before source selection validation completed" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_request_log_empty() {
    if [ -s "$request_log" ]; then
        echo "unexpected AUR RPC request" >&2
        cat "$request_log" >&2
        exit 1
    fi
}

assert_request_log_nonempty() {
    if [ ! -s "$request_log" ]; then
        echo "expected an AUR RPC request" >&2
        exit 1
    fi
}

assert_cache_root_absent() {
    if [ -e "$XDG_CACHE_HOME/jpacker" ] || [ -L "$XDG_CACHE_HOME/jpacker" ]; then
        echo "jpacker cache root was created before source selection validation completed" >&2
        exit 1
    fi
}

assert_no_mutation_commands() {
    if grep -E '^(sudo|git|makepkg) |^pacman -S( |$)' "$command_log" >/dev/null; then
        echo "source selection preflight allowed an external mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_repo_search_command() {
    if grep -E '^(pacman|sudo pacman) -Ss?( |$)' "$command_log" >/dev/null; then
        echo "AUR-only search reached pacman search" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_repo_info_command() {
    if grep -E '^(pacman|sudo pacman) -Si( |$)' "$command_log" >/dev/null; then
        echo "AUR-only info reached pacman -Si" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_source_build_commands() {
    if grep -E '^(git|makepkg) ' "$command_log" >/dev/null; then
        echo "repository-only route reached source build command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_cache_entry_absent() {
    entry=$XDG_CACHE_HOME/jpacker/$1
    if [ -e "$entry" ] || [ -L "$entry" ]; then
        echo "cache entry was created before all targets passed preflight: $entry" >&2
        exit 1
    fi
}

prepare_source_preference() {
    package=$1
    printf 'CFLAGS=-Osource-selection-test\n' > "$JPACKER_TEST_PACKAGE_BUILD_DIR/$package"
}

run_exact() {
    case_name=$1
    expected=$2
    shift 2
    setup_case "$case_name"
    export JPACKER_TEST_PACMAN_EXIT_CODE=0
    run_ok "$@"
    assert_only_command "$expected"
    assert_request_log_empty
}

# Matrix A: selector parse、option value、opaque operand、conflict。
run_exact option-value-aur \
    "pacman -Q --root --aur filesystem" \
    -Q --root --aur filesystem
run_exact option-value-repo \
    "pacman -Q --config --repo filesystem" \
    -Q --config --repo filesystem

setup_case opaque-aur
run_ok -U -- --aur
assert_only_command "sudo pacman -U -- --aur"
assert_request_log_empty

setup_case opaque-repo
run_ok -U -- --repo
assert_only_command "sudo pacman -U -- --repo"
assert_request_log_empty

setup_case opaque-sync-target
run_fail -S -- --aur
assert_contains "Invalid package name: --aur" "$output_file"
assert_command_log_empty
assert_request_log_empty

setup_case selector-before-operation
run_ok --repo -S official-a
assert_only_command "sudo pacman -S official-a"
assert_request_log_empty

setup_case selector-after-operation
run_ok -S --repo official-a
assert_only_command "sudo pacman -S official-a"
assert_request_log_empty

setup_case aur-selector-before-operation
run_ok --aur -Ss virtual-dep-150
assert_contains "provider-a" "$output_file"
assert_command_log_empty
assert_request_log_nonempty

setup_case duplicate-selector
run_ok --repo -S --repo official-a
assert_only_command "sudo pacman -S official-a"
assert_request_log_empty

setup_case duplicate-aur-selector
run_ok --aur -Ss --aur virtual-dep-150
assert_contains "provider-a" "$output_file"
assert_command_log_empty
assert_request_log_nonempty

# selector付きでもseparated operation modifierをcanonical -Ss/-Siと同じrouteへ送る。
setup_case separated-aur-search-short
run_ok -S -s --aur virtual-dep-150
assert_contains "provider-a" "$output_file"
assert_command_log_empty
assert_request_log_nonempty

setup_case separated-repo-search-long
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -S --search --repo keyword
assert_only_command "pacman -S --search keyword"
assert_request_log_empty

setup_case separated-aur-info-long
run_ok -S --info --aur clean-root
assert_contains "Name            : clean-root" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

setup_case separated-repo-info-short
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -S -i --repo filesystem
assert_only_command "pacman -S -i filesystem"
assert_request_log_empty

# POLICY: Autoはoperation文字列だけでsearch/infoを分類し、separated modifierをinstall routeで扱う。
# selector routeとの現行非対称を共通化しないため、official targetでcall pathを固定する。
setup_case separated-auto-search-short
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok -S -s official-a
assert_command "pacman -Si official-a"
assert_command "sudo pacman -S -s official-a"
assert_request_log_empty

setup_case separated-auto-info-short
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok -S -i official-a
assert_command "pacman -Si official-a"
assert_command "sudo pacman -S -i official-a"
assert_request_log_empty

conflict_index=0
for conflict_order in \
    "--aur --repo -S conflict-target" \
    "--repo --aur -S conflict-target" \
    "-S --aur --repo conflict-target" \
    "-S --repo --aur conflict-target"
do
    conflict_index=$((conflict_index + 1))
    setup_case conflict-$conflict_index
    # The matrix contains no whitespace-bearing operand, so field splitting is intentional here.
    # shellcheck disable=SC2086
    run_fail $conflict_order
    assert_contains "Cannot combine --aur and --repo" "$output_file"
    assert_command_log_empty
    assert_request_log_empty
    assert_cache_root_absent
done

# Matrix F: 初期scope外のoperationはselectorを黙って無視しない。
assert_unsupported_operation() {
    selector=$1
    operation=$2
    shift 2
    unsupported_index=$((unsupported_index + 1))
    setup_case unsupported-$unsupported_index
    run_fail "$operation" "$selector" "$@"
    assert_contains "$selector is not supported for operation" "$output_file"
    assert_contains "$operation" "$output_file"
    assert_command_log_empty
    assert_request_log_empty
    assert_cache_root_absent
}

unsupported_index=0
for selector in --aur --repo; do
    assert_unsupported_operation "$selector" upgrade
    assert_unsupported_operation "$selector" -Su
    assert_unsupported_operation "$selector" -S -u
    if [ "$selector" = "--aur" ]; then
        unsupported_index=$((unsupported_index + 1))
        setup_case unsupported-$unsupported_index
        run_fail -Syu --aur
        assert_contains "Cannot combine --aur with pacman refresh" "$output_file"
        assert_contains "-Syu" "$output_file"
        assert_command_log_empty
        assert_request_log_empty

        unsupported_index=$((unsupported_index + 1))
        setup_case unsupported-$unsupported_index
        run_fail -Sy --aur sync-target
        assert_contains "Cannot combine --aur with pacman refresh" "$output_file"
        assert_contains "-Sy" "$output_file"
        assert_command_log_empty
        assert_request_log_empty
    else
        assert_unsupported_operation "$selector" -Syu
        assert_unsupported_operation "$selector" -Sy sync-target
    fi
    assert_unsupported_operation "$selector" -Sc
    assert_unsupported_operation "$selector" -S -c
    assert_unsupported_operation "$selector" -S --clean
    assert_unsupported_operation "$selector" -S --sysupgrade
    assert_unsupported_operation "$selector" -Qua
    assert_unsupported_operation "$selector" -Q filesystem
    assert_unsupported_operation "$selector" -F usr/bin/foo
    assert_unsupported_operation "$selector" -U package.pkg.tar.zst
    assert_unsupported_operation "$selector" build clean-root
    assert_unsupported_operation "$selector" fetch clean-root
    assert_unsupported_operation "$selector" deps clean-root
    assert_unsupported_operation "$selector" plan clean-root
done

# Matrix B: AurOnly install。root targetはrepo probe/preferenceを見ず、全件preflight後だけmutationする。
setup_case aur-install-success
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S --aur clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg -sic --noconfirm"
assert_command_absent "pacman -Si clean-root"
assert_request_log_nonempty

setup_case aur-install-official-same-name
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S --aur clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "pacman -Si clean-root"
assert_request_log_nonempty

setup_case aur-install-ignores-preference
prepare_source_preference clean-root
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S --aur clean-root
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command_absent "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git clean-root"
assert_command_absent "pacman -Si clean-root"
assert_request_log_nonempty
if [ ! -f "$JPACKER_TEST_PACKAGE_BUILD_DIR/clean-root" ]; then
    echo "--aur changed the source-build preference file" >&2
    exit 1
fi

setup_case aur-install-missing
run_fail -S --aur missing-aur-package
assert_contains "AUR package not found" "$output_file"
assert_no_mutation_commands
assert_request_log_nonempty
assert_cache_entry_absent missing-aur-package

setup_case aur-install-qualified-target
run_fail -S --aur core/filesystem
assert_contains "Invalid AUR package target" "$output_file"
assert_command_log_empty
assert_request_log_empty

setup_case aur-install-multiple-preflight
run_fail --noedit --noconfirm -S --aur clean-root missing-aur-package
assert_contains "AUR package not found" "$output_file"
assert_no_mutation_commands
assert_request_log_nonempty
assert_cache_entry_absent clean-root
assert_cache_entry_absent missing-aur-package

while IFS='|' read -r case_name target expected; do
    setup_case "aur-guard-$case_name"
    run_fail --noedit --noconfirm -S --aur "$target"
    assert_contains "$expected" "$output_file"
    assert_no_mutation_commands
    assert_request_log_nonempty
    assert_cache_entry_absent "$target"
done <<'AUR_GUARDS'
metadata-risk|risk-root|conflicts/replaces metadata requires manual review
ambiguous-provider|ambiguous-root|ambiguous providers
unresolved-dependency|unresolved-root|unresolved dependencies
cyclic-plan|cycle-root|cyclic dependencies
split-package|split-child|split package install target selection is not implemented
AUR_GUARDS

setup_case aur-install-unsupported-option
run_fail -S --aur clean-root --config custom.conf
assert_contains "Unsupported pacman option for AUR/source-build target: --config" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent clean-root

setup_case aur-install-noconfirm-rmdeps
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm --rmdeps -S --aur clean-root
assert_command "makepkg -sic --noconfirm -r"
assert_command_absent "sudo pacman -S --noconfirm clean-root"
assert_request_log_nonempty

# Matrix C: RepoOnly install。ordered argvを一度のbinary repository transactionへ渡す。
setup_case repo-install-success
run_ok -S --repo official-a
assert_only_command "sudo pacman -S official-a"
assert_no_source_build_commands
assert_request_log_empty

setup_case repo-install-overrides-preference
prepare_source_preference official-a
preference_checksum=$(cksum "$JPACKER_TEST_PACKAGE_BUILD_DIR/official-a")
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok -S --repo official-a
assert_only_command "sudo pacman -S official-a"
assert_no_source_build_commands
assert_request_log_empty
if [ ! -f "$JPACKER_TEST_PACKAGE_BUILD_DIR/official-a" ]; then
    echo "--repo removed or changed the source-build preference" >&2
    exit 1
fi
if [ "$(cksum "$JPACKER_TEST_PACKAGE_BUILD_DIR/official-a")" != "$preference_checksum" ]; then
    echo "--repo changed the source-build preference content" >&2
    exit 1
fi

setup_case repo-install-missing
export JPACKER_TEST_SUDO_EXIT_CODE=42
run_fail -S --repo clean-root
assert_only_command "sudo pacman -S clean-root"
assert_no_source_build_commands
assert_request_log_empty

setup_case repo-install-qualified
run_ok -S --repo core/filesystem
assert_only_command "sudo pacman -S core/filesystem"
assert_request_log_empty

setup_case repo-install-ordered-argv
run_ok -S --repo target-a --config custom.conf target-b
assert_only_command "sudo pacman -S target-a --config custom.conf target-b"
assert_request_log_empty

setup_case repo-install-noconfirm
run_ok --noconfirm -S --repo target-a
assert_only_command "sudo pacman -S --noconfirm target-a"
assert_request_log_empty

setup_case repo-install-rmdeps-does-not-leak
run_ok -S --repo target-a --rmdeps
assert_only_command "sudo pacman -S target-a"
assert_request_log_empty

# Auto install regression: selector未指定時のrepo/source-build/AUR分類と横断preflightを維持する。
setup_case auto-install-official
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_ok -S official-a
assert_command "pacman -Si official-a"
assert_command "sudo pacman -S official-a"
assert_no_source_build_commands
assert_request_log_empty

setup_case auto-install-preferred-official
prepare_source_preference clean-root
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S clean-root
assert_command "git clone https://gitlab.archlinux.org/archlinux/packaging/packages/clean-root.git clean-root"
assert_command "makepkg -sic --noconfirm"
assert_request_log_empty

setup_case auto-install-aur
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S clean-root
assert_command "pacman -Si clean-root"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg -sic --noconfirm"
assert_request_log_nonempty

setup_case auto-install-mixed-preflight
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_fail --noedit --noconfirm -S official-a missing-aur-package
assert_contains "not found" "$output_file"
assert_no_mutation_commands
assert_request_log_nonempty
assert_cache_entry_absent missing-aur-package

setup_case auto-install-mixed-success
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
export JPACKER_TEST_MAKEPKG_EXIT_CODE=0
run_ok --noedit --nodiff --noconfirm -S official-a clean-root
assert_command "sudo pacman -S --noconfirm official-a"
assert_command "git clone https://aur.archlinux.org/clean-root.git clean-root"
assert_command "makepkg -sic --noconfirm"
assert_request_log_nonempty

setup_case auto-install-unsupported-option
export JPACKER_TEST_PACMAN_REPO_PACKAGES=official-a
run_fail -S official-a --config custom.conf clean-root
assert_contains "Unsupported pacman option for AUR/source-build target: --config" "$output_file"
assert_no_mutation_commands
assert_request_log_empty

# Matrix D: search。AurOnlyはAURのみ、RepoOnlyはpacmanのみ、Autoは従来の統合表示。
setup_case aur-search
run_ok -Ss --aur virtual-dep-150
assert_contains "provider-a" "$output_file"
assert_no_repo_search_command
assert_command_log_empty
assert_request_log_nonempty

setup_case aur-search-missing
run_fail -Ss --aur missing-search-query
assert_no_repo_search_command
assert_command_log_empty
assert_request_log_nonempty

refresh_index=0
for refresh_command in \
    "-Ssy --aur virtual-dep-150" \
    "-Ss --refresh --aur virtual-dep-150" \
    "-Ss -y --aur virtual-dep-150"
do
    refresh_index=$((refresh_index + 1))
    setup_case aur-search-refresh-$refresh_index
    # shellcheck disable=SC2086
    run_fail $refresh_command
    assert_contains "Cannot combine --aur with pacman refresh" "$output_file"
    assert_command_log_empty
    assert_request_log_empty
done

setup_case repo-search
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Ss --repo keyword
assert_only_command "pacman -Ss keyword"
assert_request_log_empty

setup_case repo-search-missing
export JPACKER_TEST_PACMAN_EXIT_CODE=7
run_fail -Ss --repo keyword
assert_only_command "pacman -Ss keyword"
assert_request_log_empty

setup_case repo-search-refresh
run_ok -Ss --repo --refresh keyword
assert_only_command "sudo pacman -Ss --refresh keyword"
assert_request_log_empty

setup_case auto-search
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Ss virtual-dep-150
assert_command "pacman -Ss virtual-dep-150"
assert_contains "provider-a" "$output_file"
assert_request_log_nonempty

setup_case auto-search-refresh
run_ok -Ss --refresh virtual-dep-150
assert_command "sudo pacman -Ss --refresh virtual-dep-150"
assert_request_log_nonempty

# Matrix E: info。source限定中はfallbackせず、RepoOnly refreshはunqualified targetも許可する。
setup_case aur-info
run_ok -Si --aur clean-root
assert_contains "Name            : clean-root" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

setup_case aur-info-official-same-name
export JPACKER_TEST_PACMAN_REPO_PACKAGES=clean-root
run_ok -Si --aur clean-root
assert_contains "Name            : clean-root" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

setup_case aur-info-missing
run_fail -Si --aur missing-aur-package
assert_contains "AUR package not found" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

setup_case aur-info-qualified
run_fail -Si --aur core/filesystem
assert_contains "Invalid AUR package target" "$output_file"
assert_command_log_empty
assert_request_log_empty

info_refresh_index=0
for refresh_command in \
    "-Siy --aur clean-root" \
    "-Si --aur --refresh clean-root"
do
    info_refresh_index=$((info_refresh_index + 1))
    setup_case aur-info-refresh-$info_refresh_index
    # shellcheck disable=SC2086
    run_fail $refresh_command
    assert_contains "Cannot combine --aur with pacman refresh" "$output_file"
    assert_command_log_empty
    assert_request_log_empty
done

setup_case aur-info-multiple
run_ok -Si --aur clean-root risk-root
assert_contains "Name            : clean-root" "$output_file"
assert_contains "Name            : risk-root" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

# partial output契約: valid targetは表示するが、1件でもmissingならinvocationはnon-zero。
setup_case aur-info-multiple-partial
run_fail -Si --aur clean-root missing-aur-package
assert_contains "Name            : clean-root" "$output_file"
assert_contains "AUR package not found" "$output_file"
assert_no_repo_info_command
assert_request_log_nonempty

setup_case repo-info
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Si --repo filesystem
assert_only_command "pacman -Si filesystem"
assert_request_log_empty

setup_case repo-info-missing
run_fail -Si --repo clean-root
assert_only_command "pacman -Si clean-root"
assert_request_log_empty

setup_case repo-info-qualified
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Si --repo core/filesystem
assert_only_command "pacman -Si core/filesystem"
assert_request_log_empty

setup_case repo-info-refresh-unqualified
run_ok -Si --repo --refresh filesystem
assert_only_command "sudo pacman -Si --refresh filesystem"
assert_request_log_empty

setup_case repo-info-multiple
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Si --repo target-a target-b
assert_only_command "pacman -Si target-a target-b"
assert_request_log_empty

setup_case auto-info-official
export JPACKER_TEST_PACMAN_REPO_PACKAGES=filesystem
run_ok -Si filesystem
assert_command "pacman -Si filesystem"
assert_request_log_empty

setup_case auto-info-qualified
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_ok -Si core/filesystem
assert_only_command "pacman -Si core/filesystem"
assert_request_log_empty

setup_case auto-info-aur-fallback
run_ok -Si clean-root
assert_command "pacman -Si clean-root"
assert_contains "Name            : clean-root" "$output_file"
assert_request_log_nonempty

setup_case auto-info-refresh-guard
run_fail -Si --refresh filesystem
assert_contains "Cannot combine pacman refresh with AUR info fallback" "$output_file"
assert_command_log_empty
assert_request_log_empty

echo "source selection integration tests: all checks passed"
