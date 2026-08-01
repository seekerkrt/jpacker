#!/bin/sh

set -eu

[ "$#" -eq 7 ] || {
    printf '%s\n' \
        'usage: test-catalog-metadata-gate.sh MAKE REPO_ROOT PO_DIR XGETTEXT MSGCMP MSGFMT MSGGREP' >&2
    exit 2
}

make_command=$1
repo_root=$2
source_po_dir=$3
xgettext_command=$4
msgcmp_command=$5
msgfmt_command=$6
msggrep_command=$7
tmp_dir=$(mktemp -d)
fixture_po_dir=$tmp_dir/po

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'catalog-metadata-gate-test: %s\n' "$*" >&2
    exit 1
}

show_log_and_fail() {
    log_file=$1
    shift
    sed -n '1,200p' "$log_file" >&2
    fail "$*"
}

assert_target_rejects_catalog() {
    case_name=$1
    target_name=$2
    expected_error=$3
    failure_reason=$4
    target_log=$tmp_dir/$case_name-$target_name.log
    target_build_dir=$tmp_dir/build-$case_name-$target_name

    # release-check owns this negative test, so its child invocation skips
    # only this phony target to avoid recursively running the same assertion.
    if "$make_command" --no-print-directory -j1 \
            --old-file=test-catalog-metadata-gate -C "$repo_root" \
            BUILD_DIR="$target_build_dir" \
            PO_DIR="$fixture_po_dir" \
            XGETTEXT="$xgettext_command" \
            MSGCMP="$msgcmp_command" \
            MSGFMT="$msgfmt_command" \
            MSGGREP="$msggrep_command" \
            "$target_name" > "$target_log" 2>&1; then
        show_log_and_fail "$target_log" \
            "$target_name accepted the $case_name catalog."
    fi

    grep -Fqx "$expected_error" "$target_log" ||
        show_log_and_fail "$target_log" \
            "$target_name failed without detecting $failure_reason."

    printf '  ok: %s rejects %s\n' "$target_name" "$failure_reason"
}

restore_catalog_fixture() {
    rm -rf "$fixture_po_dir"
    mkdir -p "$fixture_po_dir"
    cp -R "$source_po_dir"/. "$fixture_po_dir"
}

restore_catalog_fixture

awk '
    BEGIN {
        target_msgid = "msgid \"Do not run {} as root or with sudo.\""
    }
    $0 == "#, c++-format" {
        pending_format_flag = $0
        next
    }
    pending_format_flag != "" {
        if ($0 != target_msgid) {
            print pending_format_flag
        }
        pending_format_flag = ""
    }
    { print }
    END {
        if (pending_format_flag != "") {
            print pending_format_flag
        }
    }
' "$source_po_dir/ja.po" > "$tmp_dir/ja-without-format-flag.po"

sed \
    's/^msgstr "{}をrootまたはsudoで実行しないでください。"$/msgstr "Moguetをrootまたはsudoで実行しないでください。"/' \
    "$tmp_dir/ja-without-format-flag.po" > "$fixture_po_dir/ja.po"

grep -Fqx \
    'msgstr "Moguetをrootまたはsudoで実行しないでください。"' \
    "$fixture_po_dir/ja.po" ||
    fail 'failed to hardcode the identity in the invalid metadata catalog.'

control_log=$tmp_dir/msgfmt-control.log
if ! "$msgfmt_command" --check --check-format --check-domain \
        --output-file=/dev/null "$fixture_po_dir/ja.po" \
        > "$control_log" 2>&1; then
    show_log_and_fail "$control_log" \
        'invalid metadata fixture did not isolate the msgfmt flag-removal bypass.'
fi
printf '  ok: msgfmt accepts the flag-removed control catalog\n'

format_metadata_error="error: c++-format metadata missing from $fixture_po_dir/ja.po for messages required by $fixture_po_dir/moguet.pot; run 'make update-po'"
assert_target_rejects_catalog \
    missing-format-metadata check-catalogs \
    "$format_metadata_error" 'c++-format metadata drift'
assert_target_rejects_catalog \
    missing-format-metadata release-check \
    "$format_metadata_error" 'c++-format metadata drift'

restore_catalog_fixture
awk '
    BEGIN {
        target_msgid = "msgid \"Show this help message and exit\""
    }
    $0 == target_msgid {
        replace_translation = 1
        print
        next
    }
    replace_translation && /^msgstr / {
        print "msgstr \"\""
        replace_translation = 0
        next
    }
    { print }
' "$source_po_dir/ja.po" > "$fixture_po_dir/ja.po"

grep -A1 -F 'msgid "Show this help message and exit"' \
    "$fixture_po_dir/ja.po" | grep -Fqx 'msgstr ""' ||
    fail 'failed to create the untranslated catalog fixture.'

coverage_error="error: $fixture_po_dir/ja.po has untranslated or fuzzy messages required by $fixture_po_dir/moguet.pot; run 'make update-po' and complete the translations"
assert_target_rejects_catalog \
    untranslated-message check-catalogs \
    "$coverage_error" 'untranslated catalog coverage'
assert_target_rejects_catalog \
    untranslated-message release-check \
    "$coverage_error" 'untranslated catalog coverage'

restore_catalog_fixture
awk '
    BEGIN {
        target_msgid = "msgid \"Show this help message and exit\""
    }
    $0 == target_msgid {
        print "#, fuzzy"
    }
    { print }
' "$source_po_dir/ja.po" > "$fixture_po_dir/ja.po"

grep -B1 -F 'msgid "Show this help message and exit"' \
    "$fixture_po_dir/ja.po" | grep -Fqx '#, fuzzy' ||
    fail 'failed to create the fuzzy catalog fixture.'

assert_target_rejects_catalog \
    fuzzy-message check-catalogs \
    "$coverage_error" 'fuzzy catalog coverage'
assert_target_rejects_catalog \
    fuzzy-message release-check \
    "$coverage_error" 'fuzzy catalog coverage'

printf 'catalog-metadata-gate-test: all checks passed\n'
