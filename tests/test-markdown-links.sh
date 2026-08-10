#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
. "$repo_root/scripts/validation-status.sh"
checker_source=${1:-$repo_root/scripts/check-markdown-links.sh}
checker_source=$(realpath -- "$checker_source")
tmp_dir=$(mktemp -d)

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'markdown-link-test: %s\n' "$*" >&2
    exit 1
}

assert_contains() {
    expected=$1
    file=$2
    grep -F -- "$expected" "$file" >/dev/null || {
        printf 'missing expected output: %s\n' "$expected" >&2
        sed -n '1,160p' "$file" >&2
        exit 1
    }
}

create_fixture() {
    case_name=$1
    case_root=$tmp_dir/$case_name
    fixture_repo=$case_root/repository
    fixture_link=$case_root/repository-link
    real_output=$case_root/real-output
    symlink_output=$case_root/symlink-output

    mkdir -p "$fixture_repo/scripts"
    cp "$checker_source" "$fixture_repo/scripts/check-markdown-links.sh"
    git -C "$fixture_repo" init -q
    ln -s "$fixture_repo" "$fixture_link"
}

track_fixture() {
    git -C "$fixture_repo" add -- .
}

run_checker_ok() {
    checker=$1
    output=$2
    if ! env -u MOGUET_TEST_CURRENT_SOURCE_ARCHIVE \
        sh "$checker" > "$output" 2>&1; then
        printf 'expected checker to pass: %s\n' "$checker" >&2
        sed -n '1,160p' "$output" >&2
        exit 1
    fi
    assert_contains 'markdown-link-check: all checks passed' "$output"
}

run_checker_fail() {
    checker=$1
    output=$2
    missing_destination=$3
    if ! validation_expect_status markdown-link-business-failure 1 \
        "$output" "$output" \
        env -u MOGUET_TEST_CURRENT_SOURCE_ARCHIVE sh "$checker"; then
        sed -n '1,160p' "$output" >&2
        exit 1
    fi
    assert_contains "missing local target $missing_destination" "$output"
    assert_contains \
        'markdown-link-check: tracked Markdown contains missing repository-local targets.' \
        "$output"
}

run_both_ok() {
    run_checker_ok \
        "$fixture_repo/scripts/check-markdown-links.sh" "$real_output"
    run_checker_ok \
        "$fixture_link/scripts/check-markdown-links.sh" "$symlink_output"
    cmp -s "$real_output" "$symlink_output" ||
        fail 'real and symlink invocation produced different successful output.'
}

run_both_fail() {
    missing_destination=$1
    run_checker_fail \
        "$fixture_repo/scripts/check-markdown-links.sh" "$real_output" \
        "$missing_destination"
    run_checker_fail \
        "$fixture_link/scripts/check-markdown-links.sh" "$symlink_output" \
        "$missing_destination"
    cmp -s "$real_output" "$symlink_output" ||
        fail 'real and symlink invocation produced different failure output.'
}

# F1: a repository-local broken link must fail through both physical and
# symlinked repository paths with the same diagnostic.
create_fixture broken-local
printf '%s\n' '[broken](docs/MISSING.md)' > "$fixture_repo/README.md"
track_fixture
run_both_fail 'docs/MISSING.md'

# F1: an existing local target must pass through both invocation paths.
create_fixture valid-local
mkdir -p "$fixture_repo/docs"
printf '%s\n' '# Good' > "$fixture_repo/docs/GOOD.md"
printf '%s\n' '[good](docs/GOOD.md)' > "$fixture_repo/README.md"
track_fixture
run_both_ok

# F1: the existing policy continues to skip relative references which resolve
# outside the repository, even when the repository itself is reached by a
# symlink.
create_fixture outside-relative
printf '%s\n' '[outside](../OUTSIDE.md)' > "$fixture_repo/README.md"
track_fixture
run_both_ok

# F2: a colon in a fragment belongs to the local target and must not turn a
# missing path into an external URI.
create_fixture missing-colon-fragment
printf '%s\n' \
    '[missing](docs/MISSING.md#sec:1)' > "$fixture_repo/README.md"
track_fixture
run_both_fail 'docs/MISSING.md#sec:1'

# F2: real URI schemes and fragment-only links remain excluded, while local
# query/fragment suffixes are stripped only for filesystem resolution.
create_fixture valid-colon-fragment
mkdir -p "$fixture_repo/docs"
printf '%s\n' '# Good' > "$fixture_repo/docs/GOOD.md"
printf '%s\n' \
    '[https](https://example.com/path#sec:1)' \
    '[mail](mailto:user@example.com)' \
    '[fragment](#sec:1)' \
    '[local fragment](docs/GOOD.md#sec:1)' \
    '[local query fragment](docs/GOOD.md?view=1#sec:1)' \
    > "$fixture_repo/README.md"
track_fixture
run_both_ok

printf 'markdown-link-test: all checks passed\n'
