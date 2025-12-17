#!/usr/bin/env bash

set -euo pipefail

ACTION=""
REPO="${GITHUB_REPOSITORY:-}"
NAME=""
TAG=""
TITLE=""
TARGET=""
PATH_ARG=""
KEEP_LAST=""

usage() {
  cat <<'EOF'
Usage:
  release.sh upload -p <file> -n <name> [-r <owner/repo>] [-T <title>] [-b <target_sha_or_branch>] [-t <tag>]
  release.sh delete -n <name> -l <keep_last> [-r <owner/repo>]

Notes:
  - Upload creates a GitHub Release and uploads the given file as an asset.
  - Delete keeps the newest N releases whose tag starts with "<name>-", deleting older releases and tags.
  - Authentication: set GH_TOKEN (or GITHUB_TOKEN) in the environment.
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing command: $1"
}

infer_repo_from_git_remote() {
  local url
  url="$(git config --get remote.origin.url || true)"
  if [[ "$url" =~ ^https://github\.com/([^/]+/[^/.]+)(\.git)?$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  if [[ "$url" =~ ^git@github\.com:([^/]+/[^/.]+)(\.git)?$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return 0
  fi
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    upload|delete)
      ACTION="$1"
      ;;
    -r|--repo)
      REPO="${2:-}"
      shift
      ;;
    -n|--name)
      NAME="${2:-}"
      shift
      ;;
    -t|--tag)
      TAG="${2:-}"
      shift
      ;;
    -T|--title)
      TITLE="${2:-}"
      shift
      ;;
    -b|--target)
      TARGET="${2:-}"
      shift
      ;;
    -p|--path)
      PATH_ARG="${2:-}"
      shift
      ;;
    -l|--last)
      KEEP_LAST="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown parameter: $1"
      ;;
  esac
  shift
done

need_cmd gh

if [[ -z "$REPO" ]]; then
  if command -v git >/dev/null 2>&1; then
    REPO="$(infer_repo_from_git_remote || true)"
  fi
fi

[[ -n "$ACTION" ]] || { usage; die "missing action (upload/delete)"; }
[[ -n "$REPO" ]] || die "missing repo; pass -r owner/repo or set GITHUB_REPOSITORY"

gh_release_delete_with_tag() {
  local tag="$1"
  if gh release delete "$tag" --repo "$REPO" -y --cleanup-tag >/dev/null 2>&1; then
    return 0
  fi

  gh release delete "$tag" --repo "$REPO" -y
  gh api -X DELETE "repos/$REPO/git/refs/tags/$tag" >/dev/null 2>&1 || true
}

do_upload() {
  [[ -n "$PATH_ARG" ]] || die "upload requires -p <file>"
  [[ -f "$PATH_ARG" ]] || die "not a file: $PATH_ARG"
  [[ -n "$NAME" || -n "$TAG" ]] || die "upload requires -n <name> or -t <tag>"

  local target="${TARGET:-${GITHUB_SHA:-}}"
  local short_sha=""
  if [[ -n "$target" ]]; then
    short_sha="${target:0:7}"
  fi

  if [[ -z "$TAG" ]]; then
    local ts
    ts="$(date -u +%Y%m%d%H%M%S)"
    if [[ -n "$short_sha" ]]; then
      TAG="${NAME}-${ts}-${short_sha}"
    else
      TAG="${NAME}-${ts}"
    fi
  fi

  local title="${TITLE:-$TAG}"
  local notes="Automated build for $REPO@$target"

  if [[ -n "$target" ]]; then
    gh release create "$TAG" "$PATH_ARG" --repo "$REPO" --title "$title" --notes "$notes" --target "$target"
  else
    gh release create "$TAG" "$PATH_ARG" --repo "$REPO" --title "$title" --notes "$notes"
  fi
}

do_delete() {
  [[ -n "$NAME" ]] || die "delete requires -n <name>"
  [[ -n "$KEEP_LAST" ]] || die "delete requires -l <keep_last>"
  [[ "$KEEP_LAST" =~ ^[0-9]+$ ]] || die "keep_last must be a number: $KEEP_LAST"

  local prefix="${NAME}-"
  local tags
  tags="$(gh api --paginate "repos/$REPO/releases" --jq ".[] | select(.tag_name | startswith(\"$prefix\")) | .tag_name")"

  local i=0
  while IFS= read -r tag; do
    [[ -n "$tag" ]] || continue
    i=$((i + 1))
    if (( i <= KEEP_LAST )); then
      continue
    fi
    echo "Deleting release+tag: $tag"
    gh_release_delete_with_tag "$tag"
  done <<< "$tags"
}

case "$ACTION" in
  upload) do_upload ;;
  delete) do_delete ;;
  *) die "unknown action: $ACTION" ;;
esac
