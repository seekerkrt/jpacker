#!/bin/sh

set -eu

repo_root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
fixture_root=/home/moguet-validation/fixtures/package-transition
MOGUET_TEST_LEGACY_SOURCE_ARCHIVE=$fixture_root/jpacker-v1.16.0-source.tar
MOGUET_TEST_CURRENT_SOURCE_ARCHIVE=$fixture_root/moguet-current-source.tar
export MOGUET_TEST_LEGACY_SOURCE_ARCHIVE
export MOGUET_TEST_CURRENT_SOURCE_ARCHIVE

fail() {
    printf 'arch-validation: %s\n' "$*" >&2
    exit 1
}

for source_archive in \
    "$MOGUET_TEST_LEGACY_SOURCE_ARCHIVE" \
    "$MOGUET_TEST_CURRENT_SOURCE_ARCHIVE"
do
    [ -f "$source_archive" ] && [ ! -L "$source_archive" ] ||
        fail "source archive is missing, not regular, or a symlink: $source_archive"
done

run_validation_step() {
    step_name=$1
    shift

    printf ':: Arch validation: %s\n' "$step_name"
    printf 'arch-validation: command:'
    printf ' %s' "$@"
    printf '\n'
    if "$@"; then
        return 0
    else
        step_status=$?
        printf 'arch-validation: %s failed with exit status %s\n' \
            "$step_name" "$step_status" >&2
        return "$step_status"
    fi
}

cd "$repo_root"
run_validation_step clean \
    env -u MAKEFLAGS -u MFLAGS make clean
run_validation_step parallel-build \
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target
run_validation_step parallel-test \
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target test
run_validation_step parallel-release-check \
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target release-check

printf 'arch-validation: all validation steps passed\n'
