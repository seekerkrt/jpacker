#!/bin/sh

set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
links_file=$(mktemp)
missing_file=$(mktemp)

cleanup() {
    rm -f "$links_file" "$missing_file"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'markdown-link-check: %s\n' "$*" >&2
    exit 1
}

cd "$repo_root"
command -v realpath >/dev/null 2>&1 ||
    fail "realpath is required for repository containment checks."

git grep -n -o -E '\[[^]]*\]\([^)]*\)' -- '*.md' > "$links_file" || {
    status=$?
    [ "$status" -eq 1 ] || fail "unable to enumerate tracked Markdown links."
}

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
