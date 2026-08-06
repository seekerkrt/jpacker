#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
links_file=$(mktemp)
missing_file=$(mktemp)
archive_paths_file=$(mktemp)
source_links_file=$(mktemp)

cleanup() {
    rm -f \
        "$links_file" \
        "$missing_file" \
        "$archive_paths_file" \
        "$source_links_file"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'markdown-link-check: %s\n' "$*" >&2
    exit 1
}

cd "$repo_root"
command -v realpath >/dev/null 2>&1 ||
    fail "realpath is required for repository containment checks."

current_source_archive=${MOGUET_TEST_CURRENT_SOURCE_ARCHIVE-}
if [ -n "$current_source_archive" ]; then
    [ -f "$current_source_archive" ] && [ ! -L "$current_source_archive" ] ||
        fail "current source archive is missing, not regular, or a symlink: $current_source_archive"
    command -v bsdtar >/dev/null 2>&1 ||
        fail "bsdtar is required for current source archive enumeration."
    bsdtar -tf "$current_source_archive" >"$archive_paths_file" ||
        fail "unable to enumerate current source archive."

    while IFS= read -r archive_path
    do
        source_file=${archive_path#./}
        [ -n "$source_file" ] || continue
        case "$source_file" in
            /*|..|../*|*/..|*/../*)
                fail "unsafe current source archive path: $archive_path"
                ;;
            *.md)
                [ -f "$source_file" ] && [ ! -L "$source_file" ] ||
                    fail "archived Markdown is missing or not regular: $source_file"
                if grep -n -o -E '\[[^]]*\]\([^)]*\)' -- \
                    "$source_file" >"$source_links_file"; then
                    awk -v source_file="$source_file" \
                        '{ print source_file ":" $0 }' \
                        "$source_links_file" >>"$links_file"
                else
                    status=$?
                    [ "$status" -eq 1 ] ||
                        fail "unable to inspect archived Markdown: $source_file"
                fi
                ;;
        esac
    done <"$archive_paths_file"
else
    git grep -n -o -E '\[[^]]*\]\([^)]*\)' -- '*.md' >"$links_file" || {
        status=$?
        [ "$status" -eq 1 ] ||
            fail "unable to enumerate tracked Markdown links."
    }
fi

while IFS=: read -r source_file line_number markdown_link
do
    [ -n "$markdown_link" ] || continue
    destination=${markdown_link#*](}
    destination=${destination%)}

    # Optional angle brackets protect destinations containing spaces. Current
    # repository links do not need a title parser; only the destination itself
    # is validated.
    case "$destination" in
        \<*\>)
            destination=${destination#<}
            destination=${destination%>}
            ;;
    esac

    case "$destination" in
        ''|'#'*|'/'*|'//'*)
            continue
            ;;
    esac

    local_target=${destination%%#*}
    local_target=${local_target%%\?*}
    [ -n "$local_target" ] || continue

    case "$local_target" in
        [A-Za-z]*:*)
            # URI schemes such as http:, https:, and mailto: are external.
            continue
            ;;
    esac

    source_dir=$(dirname "$source_file")
    resolved_target=$(realpath -m -- "$source_dir/$local_target")
    case "$resolved_target" in
        "$repo_root"|"$repo_root"/*)
            ;;
        *)
            # A relative documentation reference outside the repository is
            # not a repository-local target and must not be misclassified.
            continue
            ;;
    esac

    if [ ! -e "$resolved_target" ] && [ ! -L "$resolved_target" ]; then
        printf '%s:%s: missing local target %s (resolved: %s)\n' \
            "$source_file" "$line_number" "$destination" \
            "$resolved_target" >> "$missing_file"
    fi
done < "$links_file"

if [ -s "$missing_file" ]; then
    cat "$missing_file" >&2
    fail "tracked Markdown contains missing repository-local targets."
fi

printf 'markdown-link-check: all checks passed\n'
