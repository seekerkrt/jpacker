#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
completion_file="${1:-${script_dir}/../completions/moguet.bash}"

# Production static fallbackをそのまま読み込み、Bashが渡すcontextを再現する。
source "${completion_file}"

case_count=0

run_completion() {
    COMP_WORDS=("$@")
    COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
    COMPREPLY=()
    _moguet
}

assert_reply() {
    local label="$1"
    shift
    local -a expected=("$@")
    local index

    case_count=$((case_count + 1))
    if [[ ${#COMPREPLY[@]} -ne ${#expected[@]} ]]; then
        printf 'FAIL: %s\nexpected: %s\nactual:   %s\n' \
            "${label}" "${expected[*]}" "${COMPREPLY[*]}" >&2
        exit 1
    fi

    for ((index = 0; index < ${#expected[@]}; ++index)); do
        if [[ ${COMPREPLY[index]} != "${expected[index]}" ]]; then
            printf 'FAIL: %s\nexpected: %s\nactual:   %s\n' \
                "${label}" "${expected[*]}" "${COMPREPLY[*]}" >&2
            exit 1
        fi
    done
}

all_candidates=(
    build upgrade upgrade-aur upgrade-all clean deps plan fetch
    add-src del-src revert edit-src list-src
    -G -Gp -S -Syu -Ss -Si -Qua
    -h --help -V --version
    --edit --noedit --diff --nodiff --noconfirm --build-mode=
    --rebuild --cleanbuild --rmdeps --select --aur --repo --needed --recursive
)

registration=$(complete -p moguet)
[[ ${registration} == 'complete -F _moguet moguet' ]] || {
    printf 'FAIL: unexpected completion registration: %s\n' "${registration}" >&2
    exit 1
}
case_count=$((case_count + 1))

run_completion moguet ""
assert_reply "全public tokenのstatic fallback" "${all_candidates[@]}"

# operation別filtering、使用済みoption除外、target stateは#253のscope。
run_completion moguet upgrade-all --noedit ""
assert_reply "operation contextでも同じstatic fallback" "${all_candidates[@]}"

run_completion moguet upgrade-all unexpected-target ""
assert_reply "target contextでも同じstatic fallback" "${all_candidates[@]}"

run_completion moguet up
assert_reply "operation prefix" upgrade upgrade-aur upgrade-all

run_completion moguet deps --r
assert_reply "option prefix" --rebuild --rmdeps --repo --recursive

run_completion moguet --s
assert_reply "root discovery option prefix" --select

run_completion moguet --b
assert_reply "attached-value option token" --build-mode=

# enum / typed value候補は#253へ残し、#309ではoption tokenだけを提示する。
run_completion moguet --build-mode=n
assert_reply "typed build-mode valueは提示しない"

printf 'Moguet static Bash completion tests: %d scenarios passed\n' "${case_count}"
