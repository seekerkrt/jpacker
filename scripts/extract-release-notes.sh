#!/bin/sh

set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/.." && pwd)
notes_file=${1:-$repo_root/RELEASE_NOTES.md}

[ -f "$notes_file" ] || {
    printf 'release-notes: notes file is missing: %s\n' "$notes_file" >&2
    exit 1
}

version=$(tr -d '[:space:]' < "$repo_root/VERSION")
[ -n "$version" ] || {
    printf 'release-notes: VERSION is empty\n' >&2
    exit 1
}
printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || {
    printf 'release-notes: VERSION must look like X.Y.Z: %s\n' "$version" >&2
    exit 1
}

heading="# Moguet v$version"

# Buffer the section so a missing or duplicate current heading cannot leave a
# truncated payload on stdout. The next top-level heading ends the section;
# this does not depend on historical section line numbers.
if ! awk -v heading="$heading" '
    $0 == heading {
        found++
        if (found == 1) {
            in_section = 1
            line_count = 0
            lines[++line_count] = $0
        }
        next
    }
    in_section && /^# / {
        in_section = 0
        next
    }
    in_section {
        lines[++line_count] = $0
    }
    END {
        if (found != 1) {
            exit 1
        }
        for (line_number = 1; line_number <= line_count; line_number++) {
            print lines[line_number]
        }
    }
' "$notes_file"
then
    printf 'release-notes: expected exactly one current heading: %s\n' "$heading" >&2
    exit 1
fi
