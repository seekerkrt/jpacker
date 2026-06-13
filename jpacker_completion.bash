# jpacker_completion.bash

_jpacker() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # 主要コマンドとオプション
    opts="build upgrade clean add-src del-src edit-src list-src revert -S -Syu -Ss -R -Rs -Rns -Q -h --help --noedit --nodiff"

    # 第1引数（コマンド）の補完
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${opts}" -- ${cur}) )
        return 0
    fi

    # 直前が特定のコマンドだった場合の挙動
    case "${prev}" in
        del-src|edit-src|revert)
            # 登録済みのパッケージ名を補完 (package.build内のファイル一覧)
            if [[ -d /etc/jpacker/package.build ]]; then
                local build_pkgs=$(ls /etc/jpacker/package.build 2>/dev/null)
                COMPREPLY=( $(compgen -W "${build_pkgs}" -- ${cur}) )
            fi
            return 0
            ;;
        -S|-Syu)
            # パッケージ名の補完は重いので、--noedit などのオプションのみ提示
            COMPREPLY=( $(compgen -W "--noedit --nodiff" -- ${cur}) )
            return 0
            ;;
    esac
}

complete -F _jpacker jpacker
