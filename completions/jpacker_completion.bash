# jpacker_completion.bash

_jpacker() {
    local cur prev opts deps_opts targetless_upgrade_opts jpacker_global_opts
    local upgrade_all_selected option used word_index remaining_upgrade_all_opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # 主要コマンドとオプション
    opts="build upgrade upgrade-aur upgrade-all clean deps plan fetch add-src del-src edit-src list-src revert -G -Gp -S -Syu -Ss -Si -R -Rs -Rns -Q -Qua -h --help -V --version --noedit --nodiff --noconfirm --rebuild --cleanbuild --rmdeps --aur --repo"
    deps_opts="--recursive"
    targetless_upgrade_opts="--noedit --nodiff --noconfirm --rebuild --cleanbuild"
    jpacker_global_opts="${targetless_upgrade_opts} --rmdeps --aur --repo"

    # 第1引数（コマンド）の補完
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${opts}" -- ${cur}) )
        return 0
    fi

    # upgrade-allはtargetを取らないため、複数option入力後もcommand contextを維持し、
    # 未使用の対応optionだけを提示する。既存commandの補完経路には影響させない。
    upgrade_all_selected=false
    for ((word_index = 1; word_index < COMP_CWORD; ++word_index)); do
        if [[ " ${jpacker_global_opts} " == *" ${COMP_WORDS[word_index]} "* ]]; then
            continue
        fi
        if [[ ${COMP_WORDS[word_index]} == upgrade-all ]]; then
            upgrade_all_selected=true
        fi
        # parserと同じく、最初のnon-global tokenだけをcommandとして扱う。
        break
    done

    if [[ ${upgrade_all_selected} == true ]]; then
        remaining_upgrade_all_opts=""
        for option in ${targetless_upgrade_opts}; do
            used=false
            for ((word_index = 1; word_index < COMP_CWORD; ++word_index)); do
                if [[ ${COMP_WORDS[word_index]} == "${option}" ]]; then
                    used=true
                    break
                fi
            done

            if [[ ${used} == false ]]; then
                remaining_upgrade_all_opts+=" ${option}"
            fi
        done

        if [[ -n ${remaining_upgrade_all_opts} ]]; then
            COMPREPLY=( $(compgen -W "${remaining_upgrade_all_opts}" -- ${cur}) )
        fi
        return 0
    fi

    # 直前が特定のコマンドだった場合の挙動
    case "${prev}" in
        deps)
            COMPREPLY=( $(compgen -W "${deps_opts}" -- ${cur}) )
            return 0
            ;;
        upgrade-aur)
            # targetを取らず、AUR update lifecycleで意味を保てるoptionだけを提示する。
            COMPREPLY=( $(compgen -W "${targetless_upgrade_opts}" -- ${cur}) )
            return 0
            ;;
        -G|-Gp)
            # AUR package target を1件だけ取り、global/pacman option は受け付けない。
            COMPREPLY=()
            return 0
            ;;
        del-src|edit-src|revert)
            # 登録済みのパッケージ名を補完 (package.build内のファイル一覧)
            if [[ -d /etc/jpacker/package.build ]]; then
                local build_pkgs=$(ls /etc/jpacker/package.build 2>/dev/null)
                COMPREPLY=( $(compgen -W "${build_pkgs}" -- ${cur}) )
            fi
            return 0
            ;;
        -S)
            # パッケージ名の補完は重いので、--noedit などのオプションのみ提示
            COMPREPLY=( $(compgen -W "--noedit --nodiff --noconfirm --rebuild --cleanbuild --rmdeps --aur --repo --needed" -- ${cur}) )
            return 0
            ;;
        -Ss|-Si)
            COMPREPLY=( $(compgen -W "--aur --repo" -- ${cur}) )
            return 0
            ;;
        -Syu)
            COMPREPLY=( $(compgen -W "--noedit --nodiff --noconfirm --rebuild --cleanbuild --rmdeps --needed" -- ${cur}) )
            return 0
            ;;
    esac
}

complete -F _jpacker jpacker
