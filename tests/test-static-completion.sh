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

root_candidates=(
    build upgrade upgrade-aur upgrade-all clean deps plan fetch
    add-src edit-src list-src del-src revert
    -G -Gp -S -Syu -Ss -Si -Qua
    -h --help -V --version
    --edit --noedit --diff --nodiff --noconfirm --dry-run --build-mode=
    --rebuild --cleanbuild --rmdeps --select --aur --repo
)

registration=$(complete -p moguet)
[[ ${registration} == 'complete -F _moguet moguet' ]] || {
    printf 'FAIL: unexpected completion registration: %s\n' "${registration}" >&2
    exit 1
}
case_count=$((case_count + 1))

run_completion moguet ""
assert_reply "operation未選択時のpublic root候補" "${root_candidates[@]}"

run_completion moguet upgrade-all --noedit ""
assert_reply \
    "upgrade-allのoption scopeとconflict" \
    --noedit --diff --nodiff --noconfirm --dry-run --build-mode= \
    --rebuild --cleanbuild

run_completion moguet upgrade-all unexpected-target ""
assert_reply "targetless operationの不正operand後は候補を提示しない"

run_completion moguet list-src unexpected-target ""
assert_reply "optionなしtargetless operationも不正operand後は閉じる"

run_completion moguet up
assert_reply "operation prefix" upgrade upgrade-aur upgrade-all

run_completion moguet deps --r
assert_reply "deps固有option prefix" --recursive

run_completion moguet --s
assert_reply "root discovery option prefix" --select

run_completion moguet --l
assert_reply "operation-local optionはrootで提示しない"

run_completion moguet --b
assert_reply "attached-value option token" --build-mode=

run_completion moguet --d
assert_reply "dry-run option prefix" --diff --dry-run

# enum / package候補のdynamic completionは#253へ残す。
run_completion moguet --build-mode=n
assert_reply "typed build-mode valueは提示しない"

run_completion moguet build ""
assert_reply \
    "build form未選択時はremote/localのunion" \
    --edit --noedit --diff --nodiff --noconfirm --dry-run --build-mode= \
    --rebuild --cleanbuild --local

run_completion moguet build pkg ""
assert_reply \
    "remote build target後はlocal selectorを提示しない" \
    --edit --noedit --diff --nodiff --noconfirm --dry-run --build-mode= \
    --rebuild --cleanbuild

run_completion moguet build --local ""
assert_reply \
    "local build固有scopeとonce selector" \
    --edit --noedit --noconfirm --dry-run --build-mode= --rebuild --cleanbuild

run_completion moguet clean ""
assert_reply "cleanのroute-owned option" --noconfirm

run_completion moguet list-src ""
assert_reply "list-srcはoptionを持たない"

run_completion moguet deps first ""
assert_reply "deps multi-target formを閉じない" --noconfirm --recursive

run_completion moguet -S ""
assert_reply "plain -Sはopen grammarとselect入口を維持" --needed --noconfirm --select

run_completion moguet -S --select ""
assert_reply \
    "source-aware select固有option scope" \
    --select --needed --edit --noedit --diff --nodiff --noconfirm --dry-run \
    --build-mode= --rebuild --cleanbuild --aur --repo

run_completion moguet -Q ""
assert_reply "未列挙pacman operationもopen grammarとして扱う" --needed --noconfirm

run_completion moguet build --rebuild ""
assert_reply \
    "repeat可能aliasを維持しconflict候補を除外" \
    --edit --noedit --diff --nodiff --noconfirm --dry-run --rebuild --local

zsh_completion="$(dirname -- "${completion_file}")/_moguet"
fish_completion="$(dirname -- "${completion_file}")/moguet.fish"
if command -v zsh >/dev/null 2>&1; then
    zsh -n "${zsh_completion}"
    MOGUET_COMPLETION_FILE="${zsh_completion}" zsh -f <<'ZSH'
compdef() { return 0 }
source "$MOGUET_COMPLETION_FILE"

fail() {
    print -u2 -- "FAIL: Zsh completion semantic projection: $1"
    exit 1
}

has_candidate() {
    local expected="$1" candidate
    for candidate in "${reply[@]}"; do
        [[ $candidate == "$expected" ]] && return 0
    done
    return 1
}

words=(moguet deps '')
CURRENT=3
_moguet_find_operation || fail 'deps operation not found'
[[ $REPLY == deps ]] || fail 'deps operation identity differs'
_moguet_collect_candidates "$REPLY"
has_candidate --recursive || fail 'deps lost --recursive'
has_candidate --local && fail 'deps leaked --local'

words=(moguet build --local '')
CURRENT=4
_moguet_collect_candidates build
has_candidate --edit || fail 'local build lost --edit'
has_candidate --diff && fail 'local build leaked --diff'

words=(moguet upgrade-all unexpected-target '')
CURRENT=4
_moguet_collect_candidates upgrade-all
(( ${#reply[@]} == 0 )) || fail 'targetless operation remained open'

words=(moguet -S --select '')
CURRENT=4
_moguet_collect_candidates -S
has_candidate --needed || fail 'selected -S lost --needed'
has_candidate --recursive && fail 'selected -S leaked --recursive'

words=(moguet -Q '')
CURRENT=3
_moguet_find_operation || fail 'delegated operation not found'
[[ $REPLY == __delegated__ ]] || fail 'delegated operation was closed'
_moguet_collect_candidates "$REPLY"
has_candidate --noconfirm || fail 'delegated grammar lost --noconfirm'
ZSH
    case_count=$((case_count + 1))
fi
if command -v fish >/dev/null 2>&1; then
    fish -n "${fish_completion}"
    MOGUET_COMPLETION_FILE="${fish_completion}" fish --no-config <<'FISH'
set -g mock_words

function commandline
    printf '%s\n' $mock_words
end

source "$MOGUET_COMPLETION_FILE"

function fail --argument-names detail
    echo "FAIL: Fish completion semantic projection: $detail" >&2
    exit 1
end

set mock_words moguet deps
test (__moguet_operation) = deps; or fail 'deps operation identity differs'
__moguet_candidate_available 16; or fail 'deps lost --recursive'
__moguet_candidate_available 15; and fail 'deps leaked --local'

set mock_words moguet build --local
__moguet_candidate_available 0; or fail 'local build lost --edit'
__moguet_candidate_available 2; and fail 'local build leaked --diff'
__moguet_candidate_available 15; and fail 'once --local remained available'

set mock_words moguet upgrade-all
__moguet_candidate_available 4; or fail 'upgrade-all lost --noconfirm'
set mock_words moguet upgrade-all unexpected-target
__moguet_candidate_available 4; and fail 'targetless operation remained open'

set mock_words moguet -S --select
__moguet_candidate_available 17; or fail 'selected -S lost --needed'
__moguet_candidate_available 16; and fail 'selected -S leaked --recursive'

set mock_words moguet -Q
test (__moguet_operation) = __delegated__; or fail 'delegated operation was closed'
__moguet_candidate_available 4; or fail 'delegated grammar lost --noconfirm'
FISH
    case_count=$((case_count + 1))
fi

printf 'Moguet static Bash/Zsh/Fish completion tests: %d scenarios passed\n' "${case_count}"
