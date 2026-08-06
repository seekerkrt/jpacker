#!/bin/sh

set -eu
set -C

readonly_sentinel_status=86
log_root=/var/log/moguet-live-validation

reject() {
    printf '%s\n' "moguet-live-pacman-sentinel: rejected argv ($1)" >&2
    exit "$readonly_sentinel_status"
}

if [ "$(/usr/bin/id -u)" -ne 0 ]; then
    reject 'effective user is not root'
fi

case_name=${MOGUET_LIVE_SENTINEL_CASE-}
case "$case_name" in
    sentinel-accept-rust|sentinel-accept-rustup-noconfirm|\
    sentinel-reject-pacman-u|sentinel-reject-remove|sentinel-reject-syu|\
    sentinel-reject-multiple|sentinel-reject-unqualified|\
    sentinel-reject-option|sentinel-reject-unknown-target|\
    provider-discovery|rust-selection|rustup-selection|invalid-retry|\
    cancel-empty|cancel-q|provider-eof|non-tty-pipe|noconfirm-tty)
        ;;
    *)
        reject 'missing or unknown case identity'
        ;;
esac

case_directory=$log_root/$case_name
if [ ! -d "$case_directory" ] || [ -L "$case_directory" ]; then
    reject 'case log directory is missing or unsafe'
fi

directory_metadata=$(/usr/bin/stat -c '%U:%G:%a:%F' "$case_directory")
if [ "$directory_metadata" != 'root:moguet-validation:750:directory' ]; then
    reject 'case log directory ownership or mode changed'
fi

log_file=$case_directory/sentinel.argv
umask 027
if ! : > "$log_file"; then
    reject 'case sentinel log already exists or cannot be created'
fi

{
    printf '%s\0' sudo pacman
    for argument do
        printf '%s\0' "$argument"
    done
} >> "$log_file"
/usr/bin/chown root:moguet-validation "$log_file"
/usr/bin/chmod 0640 "$log_file"

if [ "$#" -eq 5 ]; then
    [ "$1" = '-S' ] || reject 'operation is not -S'
    [ "$2" = '--asdeps' ] || reject '--asdeps is missing or reordered'
    [ "$3" = '--needed' ] || reject '--needed is missing or reordered'
    [ "$4" = '--' ] || reject 'target delimiter is missing or reordered'
    selected_target=$5
elif [ "$#" -eq 6 ]; then
    [ "$1" = '-S' ] || reject 'operation is not -S'
    [ "$2" = '--asdeps' ] || reject '--asdeps is missing or reordered'
    [ "$3" = '--needed' ] || reject '--needed is missing or reordered'
    [ "$4" = '--noconfirm' ] || reject 'unknown optional argument'
    [ "$5" = '--' ] || reject 'target delimiter is missing or reordered'
    selected_target=$6
else
    reject 'expected exactly one target and the fixed option set'
fi

case "$selected_target" in
    extra/rust|extra/rustup)
        ;;
    *)
        reject 'target is not an allowed repo-qualified provider'
        ;;
esac

printf '%s\n' \
    "moguet-live-pacman-sentinel: accepted and blocked sudo pacman argv for $selected_target" >&2
exit "$readonly_sentinel_status"
