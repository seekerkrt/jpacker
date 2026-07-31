#!/bin/sh

set -eu

[ "$#" -eq 6 ] || {
    printf 'usage: test-localization.sh VALID_BINARY MISSING_BINARY CATALOG_DIR MISSING_DIR INVALID_PO MSGFMT\n' >&2
    exit 2
}

valid_binary=$1
missing_binary=$2
catalog_dir=$3
missing_catalog_dir=$4
invalid_format_po=$5
msgfmt_command=$6
tmp_dir=$(mktemp -d)
locale_root=$tmp_dir/locales

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'localization-test: %s\n' "$*" >&2
    exit 1
}

assert_line() {
    expected=$1
    output_file=$2
    grep -Fqx -- "$expected" "$output_file" || {
        printf 'localization-test: missing line: %s\n' "$expected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    }
}

assert_not_line() {
    unexpected=$1
    output_file=$2
    if grep -Fqx -- "$unexpected" "$output_file"; then
        printf 'localization-test: unexpected line: %s\n' "$unexpected" >&2
        sed -n '1,160p' "$output_file" >&2
        exit 1
    fi
}

run_case() {
    case_name=$1
    binary=$2
    process_locale=$3
    language=$4
    count_case=$5
    output_file=$tmp_dir/$case_name.out

    LOCPATH=$locale_root \
    LANG=$process_locale \
    LC_ALL=$process_locale \
    LANGUAGE=$language \
        "$binary" "$count_case" > "$output_file" 2>&1 || {
        sed -n '1,160p' "$output_file" >&2
        fail "$case_name execution failed."
    }
    printf '%s\n' "$output_file"
}

assert_identity_contract() {
    output_file=$1
    assert_line 'domain=moguet' "$output_file"
    assert_line 'codeset=UTF-8' "$output_file"
    assert_line 'ctype_locale=C' "$output_file"
    assert_line 'command=moguet' "$output_file"
    assert_line 'option=--help' "$output_file"
    assert_line 'external=pacman output' "$output_file"
}

assert_english_messages() {
    output_file=$1
    assert_line 'help=Show this help message and exit' "$output_file"
    assert_line 'diagnostic_project=Do not run Moguet as root or with sudo.' "$output_file"
    assert_line 'diagnostic_command=Run moguet as a normal user; Moguet will invoke sudo/pacman when needed.' "$output_file"
    assert_line 'prompt=Rebuild package?' "$output_file"
    assert_line 'missing=Missing catalog entry.' "$output_file"
    assert_line 'braces=Use {name} as data.' "$output_file"
    assert_line 'data=Selected package: {danger}' "$output_file"
    assert_line 'plural=Processed 2 packages.' "$output_file"
}

command -v localedef >/dev/null 2>&1 ||
    fail 'localedef is required for a controlled non-C LC_MESSAGES locale.'
mkdir -p "$locale_root"
localedef --no-archive -i en_US -f UTF-8 \
    "$locale_root/en_US.UTF-8"

[ ! -e "$missing_catalog_dir" ] && [ ! -L "$missing_catalog_dir" ] ||
    fail "missing-catalog fixture path already exists: $missing_catalog_dir"

c_output=$(run_case c-locale "$valid_binary" C '' two)
assert_identity_contract "$c_output"
assert_line "locale_directory=$catalog_dir" "$c_output"
assert_line 'message_locale=C' "$c_output"
assert_english_messages "$c_output"

ja_output=$(run_case japanese "$valid_binary" en_US.UTF-8 ja two)
assert_identity_contract "$ja_output"
assert_line "locale_directory=$catalog_dir" "$ja_output"
assert_not_line 'message_locale=C' "$ja_output"
assert_line 'help=このヘルプを表示して終了' "$ja_output"
assert_line 'diagnostic_project=Moguetをrootまたはsudoで実行しないでください。' "$ja_output"
assert_line 'diagnostic_command=moguetを通常ユーザーとして実行してください。必要な場合はMoguetがsudo/pacmanを呼び出します。' "$ja_output"
assert_line 'prompt=パッケージを再ビルドしますか？' "$ja_output"
assert_line 'missing=Missing catalog entry.' "$ja_output"
assert_line 'braces=Use {name} as data.' "$ja_output"
assert_line 'data=Selected package: {danger}' "$ja_output"
assert_line 'plural=Processed 2 packages.' "$ja_output"

missing_output=$(run_case missing-catalog "$missing_binary" en_US.UTF-8 ja two)
assert_identity_contract "$missing_output"
assert_line "locale_directory=$missing_catalog_dir" "$missing_output"
assert_english_messages "$missing_output"

unsupported_output=$(run_case unsupported-locale "$valid_binary" moguet_INVALID.UTF-8 ja two)
assert_identity_contract "$unsupported_output"
assert_line 'message_locale=C' "$unsupported_output"
assert_english_messages "$unsupported_output"

zz_one_output=$(run_case additional-locale-one "$valid_binary" en_US.UTF-8 zz one)
assert_identity_contract "$zz_one_output"
assert_line 'help=ZZ help' "$zz_one_output"
assert_line 'diagnostic_project=ZZ do not run Moguet' "$zz_one_output"
assert_line 'diagnostic_command=ZZ run moguet; Moguet uses sudo/pacman' "$zz_one_output"
assert_line 'prompt=ZZ rebuild?' "$zz_one_output"
assert_line 'data=ZZ selected: {danger}' "$zz_one_output"
assert_line 'plural=ZZ processed 1 package.' "$zz_one_output"

zz_two_output=$(run_case additional-locale-two "$valid_binary" en_US.UTF-8 zz two)
assert_line 'plural=ZZ processed 2 packages.' "$zz_two_output"

broken_output=$(run_case broken-runtime-catalog "$valid_binary" en_US.UTF-8 broken two)
assert_identity_contract "$broken_output"
assert_line 'data=Selected package: {danger}' "$broken_output"
assert_line 'plural=Processed 2 packages.' "$broken_output"

invalid_log=$tmp_dir/invalid-format.log
if "$msgfmt_command" --check --check-format --check-domain \
        --output-file="$tmp_dir/invalid-format.mo" \
        "$invalid_format_po" > "$invalid_log" 2>&1; then
    fail 'msgfmt accepted a catalog with mismatched C++ format placeholders.'
fi

printf 'localization-test: all checks passed\n'
