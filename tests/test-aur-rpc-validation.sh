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
python3 "$repo_root/tests/aur_rpc_fixture_server.py" \
    "$repo_root/tests/fixtures/aur-rpc-validation.json" "$port_file" &
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

    mkdir -p "$case_dir/home" "$case_dir/xdg-cache"
    : > "$command_log"
    export HOME=$case_dir/home
    export XDG_CACHE_HOME=$case_dir/xdg-cache
    export JPACKER_TEST_COMMAND_LOG=$command_log
    export JPACKER_TEST_PACMAN_EXIT_CODE=1
    export JPACKER_TEST_SUDO_EXIT_CODE=99
    unset JPACKER_TEST_PACMAN_QM_OUTPUT
    unset JPACKER_TEST_PACMAN_REPO_PACKAGES
    unset JPACKER_TEST_PACKAGE_BUILD_DIR
    unset JPACKER_TEST_GIT_REMOTE_URL
    unset JPACKER_TEST_GIT_CLONE_EXIT_CODE
    unset JPACKER_TEST_GIT_CLONE_SYMLINK_TARGET
}

run_ok() {
    : > "$command_log"
    if ! "$test_binary" "$@" > "$output_file" 2>&1; then
        echo "expected command to succeed: $*" >&2
        sed -n '1,240p' "$output_file" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

run_fail() {
    : > "$command_log"
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

assert_no_mutation_commands() {
    if grep -E '^(git|makepkg|sudo) |^pacman -S( |$)' "$command_log" >/dev/null; then
        echo "schema validation allowed an external mutation command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_command_log_empty() {
    if [ -s "$command_log" ]; then
        echo "external command ran before AUR RPC schema preflight completed" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_only_system_upgrade_sudo() {
    system_upgrade_count=$(grep -Fxc -- "sudo pacman -Syu" "$command_log" || true)
    sudo_count=$(grep -c '^sudo ' "$command_log" || true)
    if [ "$system_upgrade_count" -ne 1 ] || [ "$sudo_count" -ne 1 ]; then
        echo "upgrade schema test ran an unexpected sudo command" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_no_source_build_commands() {
    if grep -E '^(git|makepkg) |^pacman -S( |$)' "$command_log" >/dev/null; then
        echo "upgrade continued source mutation after AUR RPC schema error" >&2
        cat "$command_log" >&2
        exit 1
    fi
}

assert_validation_error() {
    context=$1
    assert_contains "AUR RPC response validation failed for $context" "$output_file"
}

assert_cache_entry_absent() {
    entry=$XDG_CACHE_HOME/jpacker/$1
    if [ -e "$entry" ] || [ -L "$entry" ]; then
        echo "cache entry was created before metadata preflight completed: $entry" >&2
        exit 1
    fi
}

prepare_source_preferences() {
    preference_dir=$case_dir/package.build
    mkdir -p "$preference_dir"
    for package in "$@"; do
        : > "$preference_dir/$package"
    done
    export JPACKER_TEST_PACKAGE_BUILD_DIR=$preference_dir
}

# missing/null/emptyはoptional arrayの正常契約。全fieldを同じentryで通す。
for package in valid-minimal arrays-null arrays-empty arrays-valid valid-split; do
    setup_case "normal-$package"
    run_ok -Si "$package"
    assert_contains "Name            : $package" "$output_file"
    if [ "$package" = "arrays-valid" ]; then
        assert_contains "Depends On      : foo>=1" "$output_file"
        assert_contains "Optional Deps   : optional-package: optional description" "$output_file"
    fi
done

# safety-critical arrayはfieldごとに独立して刺激する。1 entryにまとめると最初のerrorで後続fieldを検証できない。
while IFS='|' read -r package detail; do
    setup_case "array-$package"
    run_fail -Si "$package"
    assert_validation_error "package info $package"
    assert_contains "$detail" "$output_file"
    assert_no_mutation_commands
done <<'CASES'
depends-scalar-string|field Depends expected array or null, got string
makedepends-scalar-number|field MakeDepends expected array or null, got number
checkdepends-scalar-object|field CheckDepends expected array or null, got object
optdepends-scalar-boolean|field OptDepends expected array or null, got boolean
provides-element-number|field Provides[1] expected string, got number
conflicts-element-null|field Conflicts[0] expected string, got null
replaces-element-object|field Replaces[0] expected string, got object
depends-element-boolean|field Depends[0] expected string, got boolean
CASES

while IFS='|' read -r package detail; do
    setup_case "identifier-$package"
    run_fail -Si "$package"
    assert_validation_error "package info $package"
    assert_contains "$detail" "$output_file"
done <<'CASES'
id-name-missing|field Name expected string, got missing
id-name-null|field Name expected string, got null
id-name-number|field Name expected string, got number
id-name-empty|field Name expected non-empty string
id-name-whitespace|field Name expected non-empty string
id-name-invalid|invalid Name "../escape"
id-base-missing|field PackageBase expected string, got missing
id-base-null|field PackageBase expected string, got null
id-base-number|field PackageBase expected string, got number
id-base-empty|field PackageBase expected non-empty string
id-base-whitespace|field PackageBase expected non-empty string
id-base-invalid|invalid PackageBase "../escape"
single-mismatch-request|requested single-mismatch-request but response Name was other-package
single-multiple-request|expected zero or one result, got 2
CASES

while IFS='|' read -r package detail; do
    setup_case "scalar-$package"
    run_fail -Si "$package"
    assert_validation_error "package info $package"
    assert_contains "$detail" "$output_file"
done <<'CASES'
version-missing|field Version expected string, got missing
version-null|field Version expected string, got null
version-number|field Version expected string, got number
version-empty|field Version expected non-empty string
description-number|field Description expected string or null, got number
maintainer-boolean|field Maintainer expected string or null, got boolean
outofdate-float|field OutOfDate expected integer or null, got number
outofdate-overflow|field OutOfDate integer is outside supported range
CASES

while IFS='|' read -r package detail; do
    setup_case "semantic-$package"
    run_fail -Si "$package"
    assert_validation_error "package info $package"
    assert_contains "$detail" "$output_file"
done <<'CASES'
semantic-depends|field Depends[0] contains invalid package identifier "../escape"
semantic-makedepends|field MakeDepends[0] contains invalid package identifier "bad/name"
semantic-checkdepends|field CheckDepends[0] contains invalid package identifier ""
semantic-provides|field Provides[0] contains invalid package identifier "../virtual"
semantic-conflicts|field Conflicts[0] contains invalid package identifier "bad name"
semantic-replaces|field Replaces[0] contains invalid package identifier "-bad"
CASES

# read-only inspection経路のgeneric catchがschema errorをunknown/unresolvedへ潰さないことを確認する。
setup_case direct-deps
run_fail deps direct-root
assert_validation_error "package info malformed-direct"
assert_not_contains "Unknown dependencies:" "$output_file"

setup_case recursive-deps
run_fail deps --recursive recursive-root
assert_validation_error "package info recursive-malformed"

setup_case direct-plan
run_fail plan direct-root
assert_validation_error "package info malformed-direct"
assert_not_contains "unresolved dependencies remain" "$output_file"

setup_case recursive-plan
run_fail plan recursive-root
assert_validation_error "package info recursive-malformed"
assert_not_contains "unresolved dependencies remain" "$output_file"

while IFS='|' read -r root context detail; do
    setup_case "provider-$root"
    run_fail plan "$root"
    assert_validation_error "$context"
    assert_contains "$detail" "$output_file"
    assert_not_contains "unresolved dependencies remain" "$output_file"
done <<'CASES'
provider-name-root|provides search virtual-provider-name|invalid Name "../provider"
provider-base-root|provides search virtual-provider-base|invalid PackageBase "../provider-base"
provider-provides-root|provides search virtual-provider-provides|field Provides expected array or null, got string
provider-candidate-root|package info provider-candidate|field Depends expected array or null, got number
provider-mismatch-root|package info provider-mismatch|requested provider-mismatch but response Name was other-provider
CASES

# mutation-capable commandはplan全体のvalidation完了前にclone/build/installへ進めない。
setup_case preflight-fetch-root
run_fail fetch invalid-root-preflight
assert_validation_error "package info invalid-root-preflight"
assert_no_mutation_commands
assert_cache_entry_absent valid-dep
assert_cache_entry_absent invalid-root-preflight
assert_not_contains "Review target:" "$output_file"

setup_case preflight-fetch-multiple-targets
run_fail fetch valid-minimal invalid-root-preflight
assert_validation_error "package info invalid-root-preflight"
assert_no_mutation_commands
assert_cache_entry_absent valid-minimal
assert_cache_entry_absent valid-dep
assert_cache_entry_absent invalid-root-preflight

while IFS='|' read -r case_name root context; do
    setup_case "$case_name"
    run_fail --noconfirm -S "$root"
    assert_validation_error "$context"
    assert_no_mutation_commands
    assert_not_contains "Review target:" "$output_file"
done <<'CASES'
preflight-sync-root|invalid-root-preflight|package info invalid-root-preflight
preflight-sync-direct|direct-root|package info malformed-direct
preflight-sync-recursive|recursive-root|package info recursive-malformed
preflight-sync-provider|provider-candidate-root|package info provider-candidate
CASES

# AUR search側のschema errorは、read-only pacman searchが成功してもcommand全体を成功扱いしない。
setup_case normal-search
run_ok -Ss search-valid-query
assert_contains "search-valid-result" "$output_file"
assert_command "pacman -Ss search-valid-query"

setup_case malformed-search
export JPACKER_TEST_PACMAN_EXIT_CODE=0
run_fail -Ss search-invalid-query
assert_validation_error "search query search-invalid-query"
assert_contains "field Description expected string or null, got object" "$output_file"
assert_no_mutation_commands

setup_case malformed-refresh-search
export JPACKER_TEST_PACMAN_EXIT_CODE=0
export JPACKER_TEST_SUDO_EXIT_CODE=0
run_fail -Ssy search-invalid-query
assert_validation_error "search query search-invalid-query"
assert_contains "field Description expected string or null, got object" "$output_file"
assert_command_log_empty

# info_manyはrequested setとのmappingとduplicateを厳格に確認する。
setup_case multi-duplicate
JPACKER_TEST_PACMAN_QM_OUTPUT='multi-dup-a 1.0-1
multi-dup-b 1.0-1'
export JPACKER_TEST_PACMAN_QM_OUTPUT
run_fail -Qua
assert_validation_error multiinfo
assert_contains "duplicate response Name multi-dup-a" "$output_file"
assert_no_mutation_commands

setup_case multi-unrequested
JPACKER_TEST_PACMAN_QM_OUTPUT='multi-unrequested-a 1.0-1
multi-unrequested-b 1.0-1'
export JPACKER_TEST_PACMAN_QM_OUTPUT
run_fail -Qua
assert_validation_error multiinfo
assert_contains "response Name multi-outside was not requested" "$output_file"
assert_no_mutation_commands

setup_case multi-missing-allowed
JPACKER_TEST_PACMAN_QM_OUTPUT='multi-present 1.0-1
multi-missing 1.0-1'
export JPACKER_TEST_PACMAN_QM_OUTPUT
run_ok -Qua
assert_contains "Foreign package not found in AUR: multi-missing" "$output_file"
assert_no_mutation_commands

# 正常schemaの主要CLIと既存のprovider/split/cycle/unresolved guardをsmoke確認する。
setup_case normal-deps
run_ok deps valid-root
assert_contains "valid-dep" "$output_file"

setup_case normal-plan
run_ok plan valid-root
assert_contains "1. valid-dep" "$output_file"
assert_contains "2. valid-root" "$output_file"

setup_case normal-provider
run_ok plan single-provider-root
assert_contains "provider-one" "$output_file"

setup_case ambiguous-provider
run_ok plan ambiguous-provider-root
assert_contains "ambiguous providers are not selected" "$output_file"

setup_case cycle
run_ok plan cycle-root-174
assert_contains "cyclic dependencies detected" "$output_file"

setup_case unresolved
run_ok plan unresolved-root-174
assert_contains "unresolved dependencies remain" "$output_file"

setup_case split
run_ok plan valid-split
assert_contains "split package install target selection is not implemented" "$output_file"

setup_case normal-fetch
run_ok fetch valid-root
assert_command "git clone https://aur.archlinux.org/valid-dep.git valid-dep"
assert_command "git clone https://aur.archlinux.org/valid-root.git valid-root"

setup_case normal-build
run_fail --noedit build valid-minimal
assert_command "git clone https://aur.archlinux.org/valid-minimal.git valid-minimal"
assert_command "makepkg -sic"

setup_case build-guard
run_fail build ambiguous-provider-root
assert_contains "ambiguous providers" "$output_file"
assert_no_mutation_commands

# upgrade preflightはsplit install guardより先にdependency/provider schemaを全走査する。
setup_case upgrade-split-dependency-preflight
prepare_source_preferences upgrade-split-root
export JPACKER_TEST_SUDO_EXIT_CODE=0
run_fail upgrade
assert_validation_error "package info upgrade-split-malformed"
assert_contains "field Conflicts expected array or null, got string" "$output_file"
assert_not_contains "split package install target selection is not implemented" "$output_file"
assert_no_mutation_commands
assert_cache_entry_absent upgrade-split-root
assert_cache_entry_absent upgrade-split-base
assert_cache_entry_absent upgrade-split-malformed

# preflight後の再RPCでschema errorを検出したら、後続source packageへ進まない。
# fixtureの先頭6 responseは、2 packageそれぞれのpreflight 3回分。
# 7回目だけrequested entryへ不正fieldを重ね、directory iteratorの順序には依存させない。
setup_case upgrade-execution-schema-stops-following-source
prepare_source_preferences upgrade-sequence-a upgrade-sequence-b
export JPACKER_TEST_SUDO_EXIT_CODE=0
run_fail upgrade
assert_validation_error "package info upgrade-sequence-"
assert_contains "field Depends expected array or null, got string" "$output_file"
assert_command "sudo pacman -Syu"
assert_only_system_upgrade_sudo
assert_no_source_build_commands
assert_cache_entry_absent upgrade-sequence-a
assert_cache_entry_absent upgrade-sequence-b

echo "AUR RPC validation integration tests: all checks passed"
