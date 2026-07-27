#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
completion_file="${1:-${script_dir}/../completions/jpacker_completion.bash}"

# production completion functionをそのまま読み込み、Bashが渡すcontextを再現する。
source "${completion_file}"

case_count=0

run_completion() {
    COMP_WORDS=("$@")
    COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
    COMPREPLY=()
    _jpacker
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

upgrade_all_options=(--noedit --nodiff --noconfirm --rebuild --cleanbuild)

run_completion jpacker upgrade-all ""
assert_reply "upgrade-all直後" "${upgrade_all_options[@]}"

run_completion jpacker upgrade-all --noedit ""
assert_reply "1 option使用後" --nodiff --noconfirm --rebuild --cleanbuild

run_completion jpacker upgrade-all --noedit --nodiff ""
assert_reply "2 options使用後" --noconfirm --rebuild --cleanbuild

run_completion jpacker upgrade-all --noedit --nodiff --noconfirm --rebuild --cleanbuild ""
assert_reply "全option使用後"

run_completion jpacker upgrade-all --noedit --no
assert_reply "入力中prefixと使用済みoption" --nodiff --noconfirm

run_completion jpacker --noedit upgrade-all --nodiff ""
assert_reply "command前optionを含むcontext" --noconfirm --rebuild --cleanbuild

run_completion jpacker upgrade-all unexpected-target ""
assert_reply "target候補を提示しない" "${upgrade_all_options[@]}"

run_completion jpacker build upgrade-all ""
assert_reply "別commandのtargetを誤認しない"

# upgrade-aur / upgradeの従来の直前tokenベースの挙動を維持する。
run_completion jpacker upgrade-aur ""
assert_reply "upgrade-aur直後" "${upgrade_all_options[@]}"

run_completion jpacker upgrade-aur --noedit ""
assert_reply "upgrade-aur option後の既存挙動"

run_completion jpacker upgrade ""
assert_reply "upgradeの既存挙動"

printf 'upgrade-all Bash completion tests: %d scenarios passed\n' "${case_count}"
