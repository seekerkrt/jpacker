#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd)
cmake_command=${1:-cmake}
test_root=$(mktemp -d)

cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'cmake-frontend-contract-test: %s\n' "$*" >&2
    exit 1
}

cache_value() {
    cache_file=$1
    cache_key=$2
    sed -n "s/^$cache_key:[^=]*=//p" "$cache_file"
}

assert_cache_value() {
    cache_file=$1
    cache_key=$2
    expected_value=$3
    actual_value=$(cache_value "$cache_file" "$cache_key")
    [ "$actual_value" = "$expected_value" ] ||
        fail "$cache_key is '$actual_value'; expected '$expected_value'"
}

assert_contains() {
    inspected_file=$1
    expected_text=$2
    grep -F -- "$expected_text" "$inspected_file" >/dev/null ||
        fail "$inspected_file does not contain: $expected_text"
}

assert_not_contains() {
    inspected_file=$1
    unexpected_text=$2
    if grep -F -- "$unexpected_text" "$inspected_file" >/dev/null; then
        fail "$inspected_file unexpectedly contains: $unexpected_text"
    fi
}

launcher_a=$test_root/launcher-a
launcher_b=$test_root/launcher-b
ln -s /usr/bin/env "$launcher_a"
ln -s /usr/bin/env "$launcher_b"

# Opted-in frontends must export every input, using an explicit empty value
# when no flag or launcher is requested. Undefined input is rejected before
# project() can initialize a partial persistent cache.
undefined_tree=$test_root/undefined-input
if CPPFLAGS='' CXXFLAGS='' LDFLAGS='' env -u CCACHE \
    "$cmake_command" -S "$repo_root" -B "$undefined_tree" \
        -DMOGUET_SYNC_EXTERNAL_BUILD_INPUTS=ON \
        -DBUILD_TESTING=OFF
then
    fail 'synchronized configure accepted an undefined CCACHE input'
fi
if [ -f "$undefined_tree/CMakeCache.txt" ] &&
    grep -q '^CMAKE_CXX_COMPILER:' "$undefined_tree/CMakeCache.txt"
then
    fail 'undefined synchronized input reached compiler initialization'
fi

# Direct CMake keeps explicit cache inputs authoritative even when similarly
# named environment variables are present. The frontend sync signal is absent.
direct_tree=$test_root/direct
CPPFLAGS='-DMOGUET_ENV_CPP=1' \
CXXFLAGS='-DMOGUET_ENV_CXX=1' \
LDFLAGS='-Wl,--build-id=sha1' \
CCACHE=$launcher_a \
    "$cmake_command" -S "$repo_root" -B "$direct_tree" \
        -DBUILD_TESTING=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        '-DMOGUET_CPPFLAGS:STRING=-DMOGUET_DIRECT_CPP=1' \
        '-DCMAKE_CXX_FLAGS:STRING=-DMOGUET_DIRECT_CXX=1' \
        '-DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--build-id=md5' \
        "-DCMAKE_CXX_COMPILER_LAUNCHER:STRING=$launcher_b" \
        -DMOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS:BOOL=ON
direct_cache=$direct_tree/CMakeCache.txt
assert_cache_value "$direct_cache" MOGUET_CPPFLAGS '-DMOGUET_DIRECT_CPP=1'
assert_cache_value "$direct_cache" CMAKE_CXX_FLAGS '-DMOGUET_DIRECT_CXX=1'
assert_cache_value \
    "$direct_cache" CMAKE_EXE_LINKER_FLAGS '-Wl,--build-id=md5'
assert_cache_value \
    "$direct_cache" CMAKE_CXX_COMPILER_LAUNCHER "$launcher_b"
assert_cache_value \
    "$direct_cache" MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS ON
assert_contains "$direct_tree/compile_commands.json" MOGUET_DIRECT_CPP
assert_contains "$direct_tree/compile_commands.json" MOGUET_DIRECT_CXX
assert_not_contains "$direct_tree/compile_commands.json" MOGUET_ENV_CPP
assert_not_contains "$direct_tree/compile_commands.json" MOGUET_ENV_CXX
if grep -q '^MOGUET_SYNC_EXTERNAL_BUILD_INPUTS:' "$direct_cache"; then
    fail 'frontend synchronization signal leaked into the persistent cache'
fi

# With no explicit project cache value, direct CMake still initializes the
# CPPFLAGS bridge from the environment once, matching the Slice 2B contract.
direct_environment_tree=$test_root/direct-environment
CPPFLAGS='-DMOGUET_DIRECT_ENVIRONMENT_CPP=1' \
    "$cmake_command" -S "$repo_root" -B "$direct_environment_tree" \
        -DBUILD_TESTING=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
assert_cache_value \
    "$direct_environment_tree/CMakeCache.txt" \
    MOGUET_CPPFLAGS \
    '-DMOGUET_DIRECT_ENVIRONMENT_CPP=1'
assert_contains \
    "$direct_environment_tree/compile_commands.json" \
    MOGUET_DIRECT_ENVIRONMENT_CPP

# A fresh plain direct configure retains the Slice 2B -O2/-pipe defaults.
plain_tree=$test_root/plain
env -u CPPFLAGS -u CXXFLAGS -u LDFLAGS -u CCACHE \
    "$cmake_command" -S "$repo_root" -B "$plain_tree" \
        -DBUILD_TESTING=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
assert_cache_value \
    "$plain_tree/CMakeCache.txt" MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS ON
assert_contains "$plain_tree/compile_commands.json" '-O2 -pipe'

run_compiler_preflight() {
    preflight_tree=$1
    requested_compiler=$2
    "$cmake_command" \
        "-DMOGUET_COMPILER_PREFLIGHT_BUILD_DIR=$preflight_tree" \
        "-DMOGUET_REQUESTED_CXX=$requested_compiler" \
        -P "$repo_root/cmake/MoguetCompilerPreflight.cmake"
}

configure_synced_tree() {
    synced_tree=$1
    build_testing=$2
    build_type=$3
    cppflags=$4
    cxxflags=$5
    ldflags=$6
    ccache=$7

    run_compiler_preflight "$synced_tree" g++
    CPPFLAGS=$cppflags \
    CXXFLAGS=$cxxflags \
    LDFLAGS=$ldflags \
    CCACHE=$ccache \
        "$cmake_command" -S "$repo_root" -B "$synced_tree" \
            -DCMAKE_CXX_COMPILER=g++ \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -DCMAKE_INSTALL_PREFIX=/opt/moguet-contract \
            -DCMAKE_INSTALL_BINDIR=/custom/bin \
            -DMOGUET_INSTALL_DOCUMENT_DIRECTORY=/custom/doc/moguet \
            -DMOGUET_LOCALE_DIRECTORY=/custom/locale \
            -DMOGUET_SYNC_EXTERNAL_BUILD_INPUTS=ON \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DBUILD_TESTING="$build_testing"
}

verify_synced_state() {
    synced_tree=$1
    target=$2
    target_directory=$3
    state=$4
    cppflags=$5
    cxxflags=$6
    ldflags=$7
    ccache=$8
    build_log=$test_root/build-$state.log

    assert_cache_value "$synced_tree/CMakeCache.txt" MOGUET_CPPFLAGS "$cppflags"
    assert_cache_value "$synced_tree/CMakeCache.txt" CMAKE_CXX_FLAGS "$cxxflags"
    assert_cache_value \
        "$synced_tree/CMakeCache.txt" CMAKE_EXE_LINKER_FLAGS "$ldflags"
    assert_cache_value \
        "$synced_tree/CMakeCache.txt" CMAKE_CXX_COMPILER_LAUNCHER "$ccache"
    "$cmake_command" --build "$synced_tree" \
        --target "$target" --clean-first --verbose > "$build_log" 2>&1

    link_command=$synced_tree/CMakeFiles/$target_directory.dir/link.txt
    [ -f "$link_command" ] || fail "link command is missing: $link_command"
    if [ -n "$cppflags" ]; then
        assert_contains "$build_log" "$cppflags"
    fi
    if [ -n "$cxxflags" ]; then
        assert_contains "$build_log" "$cxxflags"
    fi
    if [ -n "$ldflags" ]; then
        assert_contains "$link_command" "$ldflags"
    fi
    if [ -n "$ccache" ]; then
        assert_contains "$build_log" "$ccache"
        assert_not_contains "$link_command" "$ccache"
    fi
}

exercise_synced_tree() {
    synced_tree=$1
    build_testing=$2
    build_type=$3
    target=$4
    target_directory=$5
    label=$6

    configure_synced_tree \
        "$synced_tree" "$build_testing" "$build_type" \
        '-DMOGUET_SYNC_CPP_A=1' \
        '-O1 -DMOGUET_SYNC_CXX_A=1' \
        '-Wl,--build-id=sha1' \
        "$launcher_a"
    verify_synced_state \
        "$synced_tree" "$target" "$target_directory" "$label-a" \
        '-DMOGUET_SYNC_CPP_A=1' \
        '-O1 -DMOGUET_SYNC_CXX_A=1' \
        '-Wl,--build-id=sha1' \
        "$launcher_a"

    configure_synced_tree \
        "$synced_tree" "$build_testing" "$build_type" \
        '-DMOGUET_SYNC_CPP_B=1' \
        '-O3 -DMOGUET_SYNC_CXX_B=1' \
        '-Wl,--build-id=md5' \
        "$launcher_b"
    verify_synced_state \
        "$synced_tree" "$target" "$target_directory" "$label-b" \
        '-DMOGUET_SYNC_CPP_B=1' \
        '-O3 -DMOGUET_SYNC_CXX_B=1' \
        '-Wl,--build-id=md5' \
        "$launcher_b"

    configure_synced_tree \
        "$synced_tree" "$build_testing" "$build_type" '' '' '' ''
    verify_synced_state \
        "$synced_tree" "$target" "$target_directory" "$label-empty" \
        '' '' '' ''
    assert_not_contains "$synced_tree/compile_commands.json" MOGUET_SYNC_CPP_A
    assert_not_contains "$synced_tree/compile_commands.json" MOGUET_SYNC_CPP_B
    assert_not_contains "$synced_tree/compile_commands.json" MOGUET_SYNC_CXX_A
    assert_not_contains "$synced_tree/compile_commands.json" MOGUET_SYNC_CXX_B
    assert_not_contains \
        "$synced_tree/compile_commands.json" "$launcher_a"
    assert_not_contains \
        "$synced_tree/compile_commands.json" "$launcher_b"
    assert_not_contains "$test_root/build-$label-empty.log" "$launcher_a"
    assert_not_contains "$test_root/build-$label-empty.log" "$launcher_b"
    assert_cache_value \
        "$synced_tree/CMakeCache.txt" MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS OFF
}

production_tree=$test_root/production
testing_tree=$test_root/testing
package_tree=$test_root/package
exercise_synced_tree \
    "$production_tree" OFF '' \
    moguet-uninstall-helper moguet-uninstall-helper production
exercise_synced_tree \
    "$testing_tree" ON '' \
    application-identity-test application-identity-test testing
exercise_synced_tree \
    "$package_tree" OFF None \
    moguet-uninstall-helper moguet-uninstall-helper package

assert_cache_value "$production_tree/CMakeCache.txt" BUILD_TESTING OFF
assert_cache_value "$testing_tree/CMakeCache.txt" BUILD_TESTING ON
assert_cache_value "$package_tree/CMakeCache.txt" BUILD_TESTING OFF
assert_cache_value "$package_tree/CMakeCache.txt" CMAKE_BUILD_TYPE None
assert_cache_value "$package_tree/CMakeCache.txt" CMAKE_INSTALL_PREFIX /opt/moguet-contract
assert_cache_value "$package_tree/CMakeCache.txt" CMAKE_INSTALL_BINDIR /custom/bin
assert_cache_value \
    "$package_tree/CMakeCache.txt" MOGUET_INSTALL_DOCUMENT_DIRECTORY /custom/doc/moguet
assert_cache_value \
    "$package_tree/CMakeCache.txt" MOGUET_LOCALE_DIRECTORY /custom/locale

# Equivalent compiler spellings resolve to the same executable. A genuinely
# different compiler fails before CMake can regenerate or mutate the cache.
run_compiler_preflight "$production_tree" /usr/bin/g++
compiler_alias=$test_root/compiler-alias
ln -s "$(command -v g++)" "$compiler_alias"
run_compiler_preflight "$production_tree" "$compiler_alias"

different_compiler=$(command -v clang++ || command -v false)
for mismatch_tree in "$production_tree" "$testing_tree" "$package_tree"
do
    cache_snapshot=$mismatch_tree/CMakeCache.before-mismatch
    cp "$mismatch_tree/CMakeCache.txt" "$cache_snapshot"
    if run_compiler_preflight "$mismatch_tree" "$different_compiler"; then
        fail "compiler mismatch unexpectedly succeeded for $mismatch_tree"
    fi
    cmp -s "$cache_snapshot" "$mismatch_tree/CMakeCache.txt" ||
        fail "compiler mismatch mutated $mismatch_tree/CMakeCache.txt"
done

if command -v clang++ >/dev/null 2>&1; then
    reverse_tree=$test_root/reverse-compiler
    CPPFLAGS='' CXXFLAGS='' LDFLAGS='' CCACHE='' \
        "$cmake_command" -S "$repo_root" -B "$reverse_tree" \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DMOGUET_SYNC_EXTERNAL_BUILD_INPUTS=ON \
            -DBUILD_TESTING=OFF
    if run_compiler_preflight "$reverse_tree" g++; then
        fail 'clang++ to g++ compiler mismatch unexpectedly succeeded'
    fi
fi

printf 'cmake-frontend-contract-test: all checks passed\n'
