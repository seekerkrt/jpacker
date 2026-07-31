#!/bin/sh
set -eu

test_binary=$1
repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
MOGUET_TEST_REPOSITORY_ROOT=$repo_root
export MOGUET_TEST_REPOSITORY_ROOT
. "$repo_root/tests/test-command-safety.sh"
fixture_dir=$repo_root/tests/fixtures/pkgbuild-export
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
    stdout_file=$case_dir/stdout
    stderr_file=$case_dir/stderr
    work_dir=$case_dir/work
    git_fixture_dir=$case_dir/git-fixture

    mkdir -p \
        "$case_dir/home" "$case_dir/xdg-state" "$case_dir/xdg-cache" \
        "$case_dir/package.build" "$case_dir/tmp" "$work_dir" \
        "$git_fixture_dir/.git"
    cp -a "$fixture_dir/." "$git_fixture_dir/"
    printf 'source preference marker\n' > "$case_dir/package.build/clean-root"
    : > "$command_log"
    : > "$stdout_file"
    : > "$stderr_file"
    : > "$normal_request_log"
    : > "$schema_request_log"

    export HOME=$case_dir/home
    export XDG_STATE_HOME=$case_dir/xdg-state
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export TMPDIR=$case_dir/tmp
    export MOGUET_TEST_AUR_RPC_BASE_URL=$normal_rpc_url
    export MOGUET_TEST_COMMAND_LOG=$command_log
    export MOGUET_TEST_PACKAGE_BUILD_DIR=$case_dir/package.build
    export MOGUET_TEST_GIT_CLONE_FIXTURE_DIR=$git_fixture_dir
    export MOGUET_TEST_PACMAN_EXIT_CODE=1
    export MOGUET_TEST_SUDO_EXIT_CODE=99
    export MOGUET_TEST_MAKEPKG_EXIT_CODE=99
    unset MOGUET_TEST_PACMAN_QM_OUTPUT
    unset MOGUET_TEST_PACMAN_REPO_PACKAGES
    unset MOGUET_TEST_GIT_REMOTE_URL
    unset MOGUET_TEST_GIT_CLONE_EXIT_CODE
    unset MOGUET_TEST_GIT_CLONE_SYMLINK_TARGET
}

run_ok() {
    : > "$command_log"
    : > "$stdout_file"
    : > "$stderr_file"
    : > "$normal_request_log"
    : > "$schema_request_log"
    if ! (cd "$work_dir" && "$test_binary" "$@") > "$stdout_file" 2> "$stderr_file"; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$stdout_file" >&2
        sed -n '1,240p' "$stderr_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
    : > "$stdout_file"
    : > "$stderr_file"
    : > "$normal_request_log"
    : > "$schema_request_log"
    if (cd "$work_dir" && "$test_binary" "$@") > "$stdout_file" 2> "$stderr_file"; then
        echo "expected command to fail: $*" >&2
        sed -n '1,240p' "$stdout_file" >&2
        sed -n '1,240p' "$stderr_file" >&2
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

assert_stdout_empty() {
    if [ -s "$stdout_file" ]; then
        echo "stdout must be empty on failure" >&2
        sed -n '1,240p' "$stdout_file" >&2
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

assert_exact_info_request() {
    requested_name=$1
    request_log=${2:-$normal_request_log}
    expected_request="/rpc/?v=5&type=info&arg%5B%5D=$requested_name"
    if [ "$(wc -l < "$request_log")" -ne 1 ] ||
       ! grep -Fx -- "$expected_request" "$request_log" >/dev/null; then
        echo "unexpected AUR RPC request sequence for $requested_name" >&2
        cat "$request_log" >&2
        exit 1
    fi
}

assert_no_package_commands() {
    if grep -E '^(pacman|sudo|makepkg) ' "$command_log" >/dev/null; then
        echo "PKGBUILD export reached a package transaction/build command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_fetch_update_commands() {
    if grep -E '^git (fetch|pull|reset|merge|clean)( |$)' "$command_log" >/dev/null; then
        echo "PKGBUILD export updated an existing repository" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_cache_root_absent() {
    for cache_root in "$XDG_CACHE_HOME/moguet" "$XDG_CACHE_HOME/jpacker"; do
        if [ -e "$cache_root" ] || [ -L "$cache_root" ]; then
            echo "PKGBUILD export changed a cache root: $cache_root" >&2
            find "$cache_root" -maxdepth 2 -print >&2 || true
            exit 1
        fi
    done
}

assert_source_preference_unchanged() {
    preference_file=$MOGUET_TEST_PACKAGE_BUILD_DIR/clean-root
    if [ "$(find "$MOGUET_TEST_PACKAGE_BUILD_DIR" -mindepth 1 -maxdepth 1 -print | wc -l)" -ne 1 ] ||
       [ "$(cat "$preference_file")" != "source preference marker" ]; then
        echo "PKGBUILD export changed source-build preferences" >&2
        find "$MOGUET_TEST_PACKAGE_BUILD_DIR" -maxdepth 1 -print >&2 || true
        exit 1
    fi
}

assert_no_temporary_artifacts() {
    temporary_artifact=$(find "$work_dir" "$TMPDIR" -maxdepth 1 \
        -name '.moguet-pkgbuild-*' -print -quit)
    if [ -n "$temporary_artifact" ]; then
        echo "temporary clone artifact remains: $temporary_artifact" >&2
        exit 1
    fi
}

assert_export_git_commands() {
    package_base=$1
    validation_count=${2:-1}
    clone_count=$(grep -F -c -- \
        "git clone --quiet -- https://aur.archlinux.org/$package_base.git " \
        "$command_log" || true)
    config_count=$(grep -E -c '^git -C .+ config --local --get remote\.origin\.url$' \
        "$command_log" || true)
    if [ "$clone_count" -ne 1 ] || [ "$config_count" -ne "$validation_count" ] || \
       [ "$(wc -l < "$command_log")" -ne $((validation_count + 1)) ]; then
        echo "unexpected export git command sequence for $package_base" >&2
        cat "$command_log" >&2
        exit 1
    fi
    assert_no_package_commands
    assert_no_fetch_update_commands
}

assert_fixture_tree() {
    exported_dir=$1
    if [ ! -d "$exported_dir/.git" ]; then
        echo "exported repository is missing .git: $exported_dir" >&2
        exit 1
    fi
    if ! diff -r --exclude=.git "$fixture_dir" "$exported_dir" >/dev/null; then
        echo "exported repository does not match fixture" >&2
        diff -r --exclude=.git "$fixture_dir" "$exported_dir" >&2 || true
        exit 1
    fi
}

# Matrix A: exact operation、arity、option roleをnetwork/filesystem mutation前に固定する。
setup_case cli-validation
run_fail -G
assert_contains "requires exactly one AUR package target" "$stderr_file"
assert_contains "Usage: moguet -G <pkg>" "$stderr_file"
assert_command_log_empty
assert_normal_request_log_empty
assert_cache_root_absent

run_fail -Gp
assert_contains "requires exactly one AUR package target" "$stderr_file"
assert_contains "Usage: moguet -Gp <pkg>" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty
assert_cache_root_absent

run_fail -G clean-root risk-root
assert_contains "requires exactly one AUR package target" "$stderr_file"
assert_command_log_empty
assert_normal_request_log_empty

run_fail -Gp clean-root risk-root
assert_contains "requires exactly one AUR package target" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

for global_option in \
    --edit --noedit --diff --nodiff --noconfirm \
    --build-mode=normal --build-mode=rebuild --build-mode=clean \
    --rebuild --cleanbuild --rmdeps --aur --repo; do
    run_fail "$global_option" -G clean-root
    assert_contains "$global_option" "$stderr_file"
    assert_contains "-G" "$stderr_file"
    assert_command_log_empty
    assert_normal_request_log_empty

    run_fail -Gp "$global_option" clean-root
    assert_contains "$global_option" "$stderr_file"
    assert_contains "-Gp" "$stderr_file"
    assert_stdout_empty
    assert_command_log_empty
    assert_normal_request_log_empty
done

run_fail -G --root /tmp clean-root
assert_contains "Unsupported option --root for operation -G" "$stderr_file"
assert_command_log_empty
assert_normal_request_log_empty

run_fail -Gp --config fixture.conf clean-root
assert_contains "Unsupported option --config for operation -Gp" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

run_fail -Gp --config
assert_contains "Missing value for option --config" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

run_fail --aur --repo -Gp clean-root
assert_contains "Cannot combine --aur and --repo" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

run_fail -Gp --needed clean-root
assert_contains "Unsupported option --needed for operation -Gp" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

run_fail -Gp -- --needed
assert_contains "Unsupported option -- for operation -Gp" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_empty

for invalid_target in core/filesystem . .. ../escape; do
    run_fail -Gp "$invalid_target"
    assert_contains "Invalid AUR target" "$stderr_file"
    assert_stdout_empty
    assert_command_log_empty
    assert_normal_request_log_empty
done

run_fail -G core/filesystem
assert_contains "Invalid AUR target" "$stderr_file"
assert_command_log_empty
assert_normal_request_log_empty

setup_case parser-role-query
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_ok -Q --root -Gp filesystem
if ! grep -Fx -- "pacman -Q --root -Gp filesystem" "$command_log" >/dev/null; then
    echo "-Gp option value was not preserved for pacman" >&2
    cat "$command_log" >&2
    exit 1
fi

setup_case parser-role-opaque
export MOGUET_TEST_SUDO_EXIT_CODE=0
run_ok -U -- -G
if ! grep -Fx -- "sudo pacman -U -- -G" "$command_log" >/dev/null; then
    echo "-G opaque operand was not preserved for pacman" >&2
    cat "$command_log" >&2
    exit 1
fi

setup_case exact-operation
export MOGUET_TEST_PACMAN_EXIT_CODE=0
run_ok -Gx clean-root
if ! grep -Fx -- "pacman -Gx clean-root" "$command_log" >/dev/null; then
    echo "non-exact -G operation was incorrectly transformed" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_normal_request_log_empty

setup_case help-version
run_ok --help
assert_contains "Export one AUR PackageBase repository" "$stdout_file"
assert_contains "Print only one AUR PackageBase PKGBUILD" "$stdout_file"
assert_command_log_empty
run_ok --version
assert_contains "Moguet v" "$stdout_file"
assert_command_log_empty
run_fail -Gp --help
assert_contains "Unsupported option --help for operation -Gp" "$stderr_file"
assert_stdout_empty
assert_command_log_empty

# Matrix B: exact AUR metadataとPackageBaseをstrictにpreflightする。
setup_case missing-package
run_fail -Gp missing-export-package
assert_contains "AUR package not found" "$stderr_file"
assert_stdout_empty
assert_command_log_empty
assert_normal_request_log_nonempty
assert_cache_root_absent

for schema_target in \
    id-base-empty id-base-invalid single-mismatch-request depends-scalar-string; do
    setup_case "schema-$schema_target"
    export MOGUET_TEST_AUR_RPC_BASE_URL=$schema_rpc_url
    run_fail -Gp "$schema_target"
    assert_contains "Failed to resolve AUR package" "$stderr_file"
    assert_stdout_empty
    assert_command_log_empty
    assert_schema_request_log_nonempty
    assert_cache_root_absent
done

# Matrix C: -Gはcwd直下へroot PackageBaseだけをatomic exportする。
setup_case export-root
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
export MOGUET_TEST_PACMAN_REPO_PACKAGES=clean-root
run_ok -G clean-root
assert_stdout_empty
assert_fixture_tree "$work_dir/clean-root"
assert_export_git_commands clean-root 2
assert_exact_info_request clean-root
assert_cache_root_absent
assert_source_preference_unchanged
assert_no_temporary_artifacts

setup_case export-root-only
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/risk-root.git
run_ok -G risk-root
assert_fixture_tree "$work_dir/risk-root"
assert_export_git_commands risk-root 2
assert_exact_info_request risk-root
if grep -F -- "risk-dep.git" "$command_log" >/dev/null || [ -e "$work_dir/risk-dep" ]; then
    echo "-G cloned a dependency repository" >&2
    cat "$command_log" >&2
    exit 1
fi
assert_cache_root_absent
assert_no_temporary_artifacts

setup_case export-split
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/split-base.git
run_ok -G split-child
assert_fixture_tree "$work_dir/split-base"
if [ -e "$work_dir/split-child" ] || [ -L "$work_dir/split-child" ]; then
    echo "split package was exported under requested package name" >&2
    exit 1
fi
assert_contains "split-child -> PackageBase split-base" "$stderr_file"
assert_export_git_commands split-base 2
assert_exact_info_request split-child
assert_cache_root_absent
assert_no_temporary_artifacts

setup_case export-does-not-use-cache
mkdir -p "$XDG_CACHE_HOME/moguet/clean-root/.git"
printf 'cache marker\n' > "$XDG_CACHE_HOME/moguet/clean-root/marker"
cache_checksum=$(cksum "$XDG_CACHE_HOME/moguet/clean-root/marker")
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_ok -G clean-root
assert_fixture_tree "$work_dir/clean-root"
if [ "$(cksum "$XDG_CACHE_HOME/moguet/clean-root/marker")" != "$cache_checksum" ]; then
    echo "-G changed an existing cache repository" >&2
    exit 1
fi
assert_not_contains "git fetch origin" "$command_log"
assert_export_git_commands clean-root 2

# Matrix D: existing destinationは種類にかかわらず変更・削除しない。
for destination_kind in empty-dir non-git-dir matching-git file symlink dangling fifo; do
    setup_case "existing-$destination_kind"
    outside_dir=$case_dir/outside
    mkdir -p "$outside_dir"
    printf 'outside marker\n' > "$outside_dir/marker"
    case $destination_kind in
        empty-dir)
            mkdir "$work_dir/clean-root"
            ;;
        non-git-dir)
            mkdir "$work_dir/clean-root"
            printf 'user content\n' > "$work_dir/clean-root/user-file"
            ;;
        matching-git)
            mkdir -p "$work_dir/clean-root/.git"
            printf 'matching repository marker\n' > "$work_dir/clean-root/user-file"
            ;;
        file)
            printf 'user file\n' > "$work_dir/clean-root"
            ;;
        symlink)
            ln -s "$outside_dir" "$work_dir/clean-root"
            ;;
        dangling)
            ln -s "$outside_dir/missing" "$work_dir/clean-root"
            ;;
        fifo)
            mkfifo "$work_dir/clean-root"
            ;;
    esac

    export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
    run_fail -G clean-root
    assert_contains "Export destination already exists" "$stderr_file"
    assert_command_log_empty
    assert_normal_request_log_nonempty
    assert_no_temporary_artifacts

    case $destination_kind in
        empty-dir)
            [ -d "$work_dir/clean-root" ]
            ;;
        non-git-dir|matching-git)
            [ -f "$work_dir/clean-root/user-file" ]
            ;;
        file)
            assert_contains "user file" "$work_dir/clean-root"
            ;;
        symlink)
            [ -L "$work_dir/clean-root" ]
            [ "$(readlink "$work_dir/clean-root")" = "$outside_dir" ]
            ;;
        dangling)
            [ -L "$work_dir/clean-root" ]
            [ "$(readlink "$work_dir/clean-root")" = "$outside_dir/missing" ]
            ;;
        fifo)
            [ -p "$work_dir/clean-root" ]
            ;;
    esac
    assert_contains "outside marker" "$outside_dir/marker"
done

setup_case existing-noconfirm
mkdir "$work_dir/clean-root"
run_fail -G --noconfirm clean-root
assert_contains "--noconfirm" "$stderr_file"
[ -d "$work_dir/clean-root" ]
assert_command_log_empty
assert_normal_request_log_empty

# Matrix E: clone/post-clone failureはfinal pathとtemporary pathを残さない。
setup_case clone-failure
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
export MOGUET_TEST_GIT_CLONE_EXIT_CODE=42
run_fail -G clean-root
assert_contains "Failed to clone AUR PackageBase clean-root" "$stderr_file"
assert_stdout_empty
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts
assert_cache_root_absent
assert_no_package_commands

setup_case remote-mismatch
export MOGUET_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail -G clean-root
assert_contains "Remote URL mismatch" "$stderr_file"
assert_stdout_empty
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case missing-local-remote
: > "$git_fixture_dir/.moguet-test-missing-remote"
run_fail -G clean-root
assert_contains "Failed to read local remote.origin.url" "$stderr_file"
assert_stdout_empty
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case missing-git-directory
rm -rf "$git_fixture_dir/.git"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "missing a regular .git directory" "$stderr_file"
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts

setup_case symlink-git-directory
outside_git_dir=$case_dir/outside-git
mkdir "$outside_git_dir"
printf 'outside git marker\n' > "$outside_git_dir/marker"
rm -rf "$git_fixture_dir/.git"
ln -s "$outside_git_dir" "$git_fixture_dir/.git"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "missing a regular .git directory" "$stderr_file"
assert_contains "outside git marker" "$outside_git_dir/marker"
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts

setup_case missing-pkgbuild
rm -f "$git_fixture_dir/PKGBUILD"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "PKGBUILD is not a regular non-symlink file" "$stderr_file"
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts

setup_case symlink-pkgbuild
rm -f "$git_fixture_dir/PKGBUILD"
ln -s clean-root.install "$git_fixture_dir/PKGBUILD"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "PKGBUILD is not a regular non-symlink file" "$stderr_file"
[ ! -e "$work_dir/clean-root" ]
assert_no_temporary_artifacts

setup_case publish-destination-race
printf 'clean-root\n' > "$git_fixture_dir/.moguet-test-final-destination"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "Export destination already exists" "$stderr_file"
assert_contains "concurrent user path" "$work_dir/clean-root/user-file"
assert_no_temporary_artifacts

setup_case current-directory-rename
: > "$git_fixture_dir/.moguet-test-rename-cwd"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_ok -G clean-root
assert_fixture_tree "$work_dir-moved/clean-root"
if [ -e "$work_dir/clean-root" ] || [ -L "$work_dir/clean-root" ]; then
    echo "-G published into a replacement for the command-start cwd" >&2
    exit 1
fi
assert_export_git_commands clean-root 2
assert_cache_root_absent
assert_no_temporary_artifacts

setup_case temporary-identity-swap
: > "$git_fixture_dir/.moguet-test-swap-temp-identity"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -G clean-root
assert_contains "Refusing changed temporary directory path" "$stderr_file"
assert_stdout_empty
replacement_temporary=$(find "$work_dir" -maxdepth 1 -type d \
    -name '.moguet-pkgbuild-*' ! -name '*.owned-original' -print -quit)
if [ -z "$replacement_temporary" ]; then
    echo "temporary identity replacement was unexpectedly removed" >&2
    exit 1
fi
assert_contains "replacement user path" "$replacement_temporary/user-file"
if [ ! -d "$replacement_temporary.owned-original" ]; then
    echo "owned temporary inode was not preserved after identity loss" >&2
    exit 1
fi
rm -rf "$replacement_temporary" "$replacement_temporary.owned-original"
assert_no_temporary_artifacts
assert_cache_root_absent

# Matrix F: -Gp stdoutはcleanup後のPKGBUILD bytesだけに限定する。
setup_case print-root
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_ok -Gp clean-root
if ! cmp -s "$fixture_dir/PKGBUILD" "$stdout_file"; then
    echo "-Gp stdout did not exactly match PKGBUILD bytes" >&2
    cmp -l "$fixture_dir/PKGBUILD" "$stdout_file" >&2 || true
    exit 1
fi
assert_not_contains "Started Moguet" "$stdout_file"
assert_not_contains "Running:" "$stdout_file"
assert_export_git_commands clean-root
assert_exact_info_request clean-root
[ ! -e "$work_dir/clean-root" ]
assert_cache_root_absent
assert_source_preference_unchanged
assert_no_temporary_artifacts

setup_case print-temporary-identity-swap
: > "$git_fixture_dir/.moguet-test-swap-temp-identity"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_contains "Refusing changed temporary directory path" "$stderr_file"
assert_stdout_empty
replacement_temporary=$(find "$TMPDIR" -maxdepth 1 -type d \
    -name '.moguet-pkgbuild-*' ! -name '*.owned-original' -print -quit)
if [ -z "$replacement_temporary" ]; then
    echo "-Gp temporary identity replacement was unexpectedly removed" >&2
    exit 1
fi
assert_contains "replacement user path" "$replacement_temporary/user-file"
if [ ! -d "$replacement_temporary.owned-original" ]; then
    echo "-Gp owned temporary inode was not preserved after identity loss" >&2
    exit 1
fi
rm -rf "$replacement_temporary" "$replacement_temporary.owned-original"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-ignores-cache-symlinks
outside_cache_dir=$case_dir/outside-cache
legacy_outside_cache_dir=$case_dir/legacy-outside-cache
mkdir "$outside_cache_dir" "$legacy_outside_cache_dir"
printf 'outside cache marker\n' > "$outside_cache_dir/marker"
printf 'legacy outside cache marker\n' > "$legacy_outside_cache_dir/marker"
ln -s "$outside_cache_dir" "$XDG_CACHE_HOME/moguet"
ln -s "$legacy_outside_cache_dir" "$XDG_CACHE_HOME/jpacker"
outside_cache_checksum=$(cksum "$outside_cache_dir/marker")
legacy_outside_cache_checksum=$(cksum "$legacy_outside_cache_dir/marker")
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_ok -Gp clean-root
cmp -s "$fixture_dir/PKGBUILD" "$stdout_file"
if [ "$(cksum "$outside_cache_dir/marker")" != "$outside_cache_checksum" ] ||
   [ "$(find "$outside_cache_dir" -mindepth 1 -maxdepth 1 -print | wc -l)" -ne 1 ]; then
    echo "-Gp followed or changed the active Moguet cache symlink" >&2
    find "$outside_cache_dir" -maxdepth 2 -print >&2 || true
    exit 1
fi
if [ "$(cksum "$legacy_outside_cache_dir/marker")" != "$legacy_outside_cache_checksum" ] ||
   [ "$(find "$legacy_outside_cache_dir" -mindepth 1 -maxdepth 1 -print | wc -l)" -ne 1 ]; then
    echo "-Gp followed or changed the legacy jpacker cache symlink" >&2
    find "$legacy_outside_cache_dir" -maxdepth 2 -print >&2 || true
    exit 1
fi
assert_export_git_commands clean-root
assert_no_temporary_artifacts

setup_case print-split
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/split-base.git
run_ok -Gp split-child
cmp -s "$fixture_dir/PKGBUILD" "$stdout_file"
assert_contains "split-child -> PackageBase split-base" "$stderr_file"
assert_export_git_commands split-base
[ ! -e "$work_dir/split-base" ]
assert_cache_root_absent
assert_no_temporary_artifacts

# Matrix G: -Gp failureはstdoutを空に保ち、secret/special fileを読まない。
setup_case print-clone-failure
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
export MOGUET_TEST_GIT_CLONE_EXIT_CODE=42
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "Failed to clone" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-remote-mismatch
export MOGUET_TEST_GIT_REMOTE_URL=https://example.invalid/wrong.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "Remote URL mismatch" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-missing-git
rm -rf "$git_fixture_dir/.git"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "missing a regular .git directory" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-missing-pkgbuild
rm -f "$git_fixture_dir/PKGBUILD"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "PKGBUILD is not a regular non-symlink file" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-pkgbuild-symlink
secret_file=$case_dir/secret
printf 'secret must not reach stdout\n' > "$secret_file"
rm -f "$git_fixture_dir/PKGBUILD"
ln -s "$secret_file" "$git_fixture_dir/PKGBUILD"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "PKGBUILD is not a regular non-symlink file" "$stderr_file"
assert_not_contains "secret must not reach stdout" "$stdout_file"
assert_contains "secret must not reach stdout" "$secret_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-pkgbuild-directory
rm -f "$git_fixture_dir/PKGBUILD"
mkdir "$git_fixture_dir/PKGBUILD"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "PKGBUILD is not a regular non-symlink file" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-pkgbuild-unreadable
: > "$git_fixture_dir/.moguet-test-pkgbuild-unreadable"
export MOGUET_TEST_GIT_REMOTE_URL=https://aur.archlinux.org/clean-root.git
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "Failed to open PKGBUILD" "$stderr_file"
assert_no_temporary_artifacts
assert_cache_root_absent

setup_case print-network-failure
export MOGUET_TEST_AUR_RPC_BASE_URL=http://127.0.0.1:9/rpc/
run_fail -Gp clean-root
assert_stdout_empty
assert_contains "AUR request failed" "$stderr_file"
assert_command_log_empty
assert_no_temporary_artifacts
assert_cache_root_absent

# Matrix H: existing fetchは依存込みinternal cache、-G/-Gpはroot-only export/temporary clone。
setup_case fetch-regression
unset MOGUET_TEST_GIT_REMOTE_URL
run_ok fetch risk-root
if [ ! -d "$XDG_CACHE_HOME/moguet/risk-dep/.git" ] || \
   [ ! -d "$XDG_CACHE_HOME/moguet/risk-root/.git" ]; then
    echo "existing fetch no longer cloned dependency/root into internal cache" >&2
    find "$XDG_CACHE_HOME" -maxdepth 3 -print >&2 || true
    exit 1
fi
if ! grep -F -- "git clone https://aur.archlinux.org/risk-dep.git risk-dep" "$command_log" >/dev/null || \
   ! grep -F -- "git clone https://aur.archlinux.org/risk-root.git risk-root" "$command_log" >/dev/null; then
    echo "existing fetch command contract changed" >&2
    cat "$command_log" >&2
    exit 1
fi
if [ -e "$work_dir/risk-root" ] || [ -e "$work_dir/risk-dep" ]; then
    echo "existing fetch unexpectedly exported into cwd" >&2
    exit 1
fi
if grep -E '^(sudo|makepkg) ' "$command_log" >/dev/null; then
    echo "existing fetch reached build/install" >&2
    cat "$command_log" >&2
    exit 1
fi

echo "PKGBUILD export integration tests: all checks passed"
