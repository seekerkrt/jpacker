#!/bin/sh
set -eu

GITHUB_REPOSITORY='seekerkrt/moguet'

: "${CI_API_V4_URL:?CI_API_V4_URL is required}"
: "${CI_PROJECT_ID:?CI_PROJECT_ID is required}"
: "${CI_JOB_TOKEN:?CI_JOB_TOKEN is required}"
: "${MIRROR_RELEASE_ACTION:?MIRROR_RELEASE_ACTION is required}"
: "${MIRROR_RELEASE_TAG:?MIRROR_RELEASE_TAG is required}"

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

tag="$MIRROR_RELEASE_TAG"
encoded_tag="$(jq -nr --arg value "$tag" '$value | @uri')"
release_url="${CI_API_V4_URL}/projects/${CI_PROJECT_ID}/releases/${encoded_tag}"
response_file="${workdir}/response.json"

case "$MIRROR_RELEASE_ACTION" in
  delete)
    status="$(curl --silent --show-error \
      --output "$response_file" \
      --write-out '%{http_code}' \
      --request DELETE \
      --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
      "$release_url")"

    case "$status" in
      200)
        echo "Deleted GitLab release: $tag"
        ;;
      404)
        echo "GitLab release is already absent: $tag"
        ;;
      *)
        cat "$response_file" >&2
        echo "Failed to delete GitLab release $tag (HTTP $status)" >&2
        exit 1
        ;;
    esac
    exit 0
    ;;
  sync)
    ;;
  *)
    echo "Unsupported mirror action: $MIRROR_RELEASE_ACTION" >&2
    exit 1
    ;;
esac

github_release_file="${workdir}/github-release.json"
github_tag="$(jq -nr --arg value "$tag" '$value | @uri')"

curl --fail --location --silent --show-error \
  --header 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/${GITHUB_REPOSITORY}/releases/tags/${github_tag}" \
  > "$github_release_file"

name="$(jq -r 'if (.name // "") == "" then .tag_name else .name end' "$github_release_file")"
released_at="$(jq -r '.published_at // .created_at' "$github_release_file")"
github_url="$(jq -r '.html_url' "$github_release_file")"
prerelease="$(jq -r '.prerelease' "$github_release_file")"
body_file="${workdir}/body.md"
description_file="${workdir}/description.md"

jq -r '.body // ""' "$github_release_file" > "$body_file"

{
  cat "$body_file"
  if [ -s "$body_file" ]; then
    printf '\n'
  fi
  printf '\n---\n\n'
  if [ "$prerelease" = "true" ]; then
    printf '> This release is marked as a pre-release on GitHub.\n\n'
  fi
  printf '_Read-only mirror of [GitHub Release %s](%s). GitHub is canonical._\n' \
    "$tag" "$github_url"
} > "$description_file"

create_payload="${workdir}/create-release.json"
update_payload="${workdir}/update-release.json"

jq -n \
  --arg name "$name" \
  --arg tag_name "$tag" \
  --rawfile description "$description_file" \
  --arg released_at "$released_at" \
  '{name: $name, tag_name: $tag_name, description: $description, released_at: $released_at}' \
  > "$create_payload"

jq -n \
  --arg name "$name" \
  --rawfile description "$description_file" \
  --arg released_at "$released_at" \
  '{name: $name, description: $description, released_at: $released_at}' \
  > "$update_payload"

status="$(curl --silent --show-error \
  --output "$response_file" \
  --write-out '%{http_code}' \
  --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
  "$release_url")"

case "$status" in
  200)
    status="$(curl --silent --show-error \
      --output "$response_file" \
      --write-out '%{http_code}' \
      --request PUT \
      --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
      --header 'Content-Type: application/json' \
      --data-binary "@${update_payload}" \
      "$release_url")"
    expected_status=200
    operation=updated
    verb=update
    ;;
  404)
    status="$(curl --silent --show-error \
      --output "$response_file" \
      --write-out '%{http_code}' \
      --request POST \
      --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
      --header 'Content-Type: application/json' \
      --data-binary "@${create_payload}" \
      "${CI_API_V4_URL}/projects/${CI_PROJECT_ID}/releases")"
    expected_status=201
    operation=created
    verb=create
    ;;
  *)
    cat "$response_file" >&2
    echo "Failed to inspect GitLab release $tag (HTTP $status)" >&2
    exit 1
    ;;
esac

if [ "$status" != "$expected_status" ]; then
  cat "$response_file" >&2
  echo "Failed to $verb GitLab release $tag (HTTP $status)" >&2
  exit 1
fi

echo "GitLab release $operation: $tag"

links_url="${release_url}/assets/links"
links_file="${workdir}/links.json"
status="$(curl --silent --show-error \
  --output "$links_file" \
  --write-out '%{http_code}' \
  --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
  "$links_url")"

if [ "$status" != 200 ]; then
  cat "$links_file" >&2
  echo "Failed to list GitLab release links for $tag (HTTP $status)" >&2
  exit 1
fi

jq -r '.[].id' "$links_file" | while IFS= read -r link_id; do
  [ -n "$link_id" ] || continue

  status="$(curl --silent --show-error \
    --output "$response_file" \
    --write-out '%{http_code}' \
    --request DELETE \
    --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
    "${links_url}/${link_id}")"

  if [ "$status" != 200 ]; then
    cat "$response_file" >&2
    echo "Failed to delete GitLab release link $link_id (HTTP $status)" >&2
    exit 1
  fi
done

jq -c '.assets[] | {name: .name, url: .browser_download_url}' "$github_release_file" |
while IFS= read -r asset; do
  asset_payload="${workdir}/asset-link.json"
  printf '%s\n' "$asset" |
    jq '. + {link_type: "other"}' > "$asset_payload"

  status="$(curl --silent --show-error \
    --output "$response_file" \
    --write-out '%{http_code}' \
    --request POST \
    --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
    --header 'Content-Type: application/json' \
    --data-binary "@${asset_payload}" \
    "$links_url")"

  if [ "$status" != 201 ]; then
    cat "$response_file" >&2
    echo "Failed to create a GitLab release asset link (HTTP $status)" >&2
    exit 1
  fi
done

echo "GitHub release asset links synchronized: $tag"
