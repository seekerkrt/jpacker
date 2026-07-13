# fish completion for jpacker

set -l jpacker_operations \
    build \
    upgrade \
    clean \
    deps \
    plan \
    fetch \
    -G \
    -Gp \
    add-src \
    del-src \
    edit-src \
    list-src \
    revert \
    -S \
    -Syu \
    -Ss \
    -Si \
    -R \
    -Rs \
    -Rns \
    -Q \
    -Qua

set -l jpacker_global_options \
    --help \
    -h \
    --noedit \
    --nodiff \
    --noconfirm \
    --rebuild \
    --cleanbuild \
    --rmdeps \
    --aur \
    --repo

function __fish_jpacker_seen_operation
    set -l tokens (commandline -opc)

    for token in $tokens[2..-1]
        switch $token
            case --help -h --noedit --nodiff --noconfirm --rebuild --cleanbuild --rmdeps --aur --repo
                continue
            case '*'
                echo $token
                return 0
        end
    end

    return 1
end

function __fish_jpacker_no_operation
    set -l operation (__fish_jpacker_seen_operation)
    test -z "$operation"
end

function __fish_jpacker_using_operation
    set -l operation (__fish_jpacker_seen_operation)
    test "$operation" = "$argv[1]"
end

function __fish_jpacker_source_preferences
    set -l pref_dir /etc/jpacker/package.build

    if test -d $pref_dir
        for path in $pref_dir/*
            test -e $path; and basename $path
        end
    end
end

complete -c jpacker -f -n '__fish_jpacker_no_operation' -a "$jpacker_operations" -d 'jpacker operation'
complete -c jpacker -f -n '__fish_jpacker_no_operation' -a "$jpacker_global_options" -d 'global option'

complete -c jpacker -f -n '__fish_jpacker_using_operation deps' -a '--recursive' -d 'Resolve dependencies recursively'

complete -c jpacker -f -n '__fish_jpacker_using_operation -S; or __fish_jpacker_using_operation -Syu' -a '--needed' -d 'Skip reinstall at final package installation'

complete -c jpacker -f -n '__fish_jpacker_using_operation del-src' -a '(__fish_jpacker_source_preferences)' -d 'source-build preference'
complete -c jpacker -f -n '__fish_jpacker_using_operation edit-src' -a '(__fish_jpacker_source_preferences)' -d 'source-build preference'
complete -c jpacker -f -n '__fish_jpacker_using_operation revert' -a '(__fish_jpacker_source_preferences)' -d 'source-build preference'

complete -c jpacker -f -n 'not __fish_jpacker_no_operation; and not __fish_jpacker_using_operation -G; and not __fish_jpacker_using_operation -Gp' -a "$jpacker_global_options" -d 'global option'
