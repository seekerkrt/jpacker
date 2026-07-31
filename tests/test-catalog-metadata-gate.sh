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
    target_name=$1
    target_log=$tmp_dir/$target_name.log
    target_build_dir=$tmp_dir/build-$target_name

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
            "$target_name accepted a catalog with missing c++-format metadata."
    fi

    expected_error="error: c++-format metadata missing from $fixture_po_dir/ja.po for messages required by $fixture_po_dir/moguet.pot; run 'make update-po'"
    grep -Fqx "$expected_error" "$target_log" ||
        show_log_and_fail "$target_log" \
            "$target_name failed without detecting catalog metadata drift."

    printf '  ok: %s rejects c++-format metadata drift\n' "$target_name"
}

mkdir -p "$fixture_po_dir"
cp -R "$source_po_dir"/. "$fixture_po_dir"

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

assert_target_rejects_catalog check-catalogs
assert_target_rejects_catalog release-check

printf 'catalog-metadata-gate-test: all checks passed\n'
