#!/usr/bin/env bash
# One command on an Apple Silicon Mac.
# Bumps AVA_V01_cpp/VERSION, builds, signs, notarizes, then commits and tags.
# A failed build or notarization restores VERSION and does not tag.
#
#   ./AVA_V01_cpp/scripts/release_macos.sh                 # 0.1.0 -> 0.1.1
#   ./AVA_V01_cpp/scripts/release_macos.sh --minor         # 0.1.0 -> 0.2.0
#   ./AVA_V01_cpp/scripts/release_macos.sh --major         # 0.1.0 -> 1.0.0
#   ./AVA_V01_cpp/scripts/release_macos.sh --push          # also push branch and tag
#   ./AVA_V01_cpp/scripts/release_macos.sh --minor --push

set -euo pipefail

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=macos_release_common.sh
source "${AVA_SCRIPT_DIR}/macos_release_common.sh"

ava_release_usage() {
  cat <<EOF
Usage: $(basename "$0") [--patch|--minor|--major] [--push]

Bumps AVA_V01_cpp/VERSION, builds the coach DMG, notarizes it, then commits
and tags. The default bump is patch. --push sends the branch and the tag to
origin. Without --push, the script prints the push commands and stops.
EOF
}

parsed="$(ava_parse_release_args "$@")" || exit 1
if [[ "$parsed" == "help" ]]; then
  ava_release_usage
  exit 0
fi

ava_require_macos

bump_part="${parsed%% *}"
push_release="${parsed##* }"
AVA_REPO_ROOT="$(cd "${AVA_CPP_ROOT}/.." && pwd)"
CMAKE_LISTS="${AVA_CPP_ROOT}/CMakeLists.txt"

ava_require_clean_worktree "$AVA_REPO_ROOT"
ava_require_release_branch "$AVA_REPO_ROOT"

current_version="$(ava_read_marketing_version "$AVA_VERSION_FILE")" || exit 1
current_build="$(ava_read_build_number "$AVA_VERSION_FILE")" || exit 1
new_version="$(ava_bump_marketing_version "$current_version" "$bump_part")" || exit 1
new_build="$(ava_next_build_number "$current_build")" || exit 1
architecture="$(ava_cmake_string AVA_MACOS_ARCHITECTURE "$CMAKE_LISTS")" || exit 1

if ava_release_tag_exists "$AVA_REPO_ROOT" "$new_version"; then
  echo "Tag v${new_version} already exists. Not releasing." >&2
  exit 1
fi

echo "Version ${current_version} (build ${current_build}) -> ${new_version} (build ${new_build})"
ava_write_version_file "$AVA_VERSION_FILE" "$new_version" "$new_build"

AVA_RELEASE_KEEP_VERSION=0
ava_release_exit() {
  local status=$?
  if [[ "$AVA_RELEASE_KEEP_VERSION" -eq 0 ]]; then
    git -C "$AVA_REPO_ROOT" checkout HEAD -- AVA_V01_cpp/VERSION || true
    echo "Release did not finish. VERSION is unchanged, and no tag was created." >&2
  fi
  exit "$status"
}
trap ava_release_exit EXIT

echo "Downloading pinned FFmpeg"
"${AVA_SCRIPT_DIR}/vendor_ffmpeg_macos.sh"
echo "Building the unsigned app and DMG"
"${AVA_SCRIPT_DIR}/package_macos.sh"
echo "Signing and notarizing"
"${AVA_SCRIPT_DIR}/sign_and_notarize.sh"

# Notarization finished. Keep the new version even if the later git commands fail.
AVA_RELEASE_KEEP_VERSION=1

other_changes="$(git -C "$AVA_REPO_ROOT" status --porcelain | grep -v 'AVA_V01_cpp/VERSION' || true)"
if [[ -n "$other_changes" ]]; then
  echo "Other files changed during the build. They are not part of the release commit:" >&2
  printf '%s\n' "$other_changes" >&2
fi

if ! git -C "$AVA_REPO_ROOT" add -- AVA_V01_cpp/VERSION \
  || ! git -C "$AVA_REPO_ROOT" commit -m "Release AVA ${new_version}." \
  || ! git -C "$AVA_REPO_ROOT" tag -a "v${new_version}" -m "AVA ${new_version}"; then
  echo "Notarization succeeded. VERSION is ${new_version} (build ${new_build})." >&2
  echo "The commit or tag did not finish. The DMG matches that version. Finish git with:" >&2
  echo "  git add -- AVA_V01_cpp/VERSION" >&2
  echo "  git commit -m \"Release AVA ${new_version}.\"" >&2
  echo "  git tag -a v${new_version} -m \"AVA ${new_version}\"" >&2
  exit 1
fi

disk_image="${AVA_CPP_ROOT}/dist/AVA-${new_version}-${architecture}.dmg"
echo
echo "Release finished. Upload:"
echo "  ${disk_image}"
echo "Tagged v${new_version}. Do not send an unsigned build."
if [[ "$push_release" -eq 1 ]]; then
  git -C "$AVA_REPO_ROOT" push origin HEAD
  git -C "$AVA_REPO_ROOT" push origin "v${new_version}"
  echo "Pushed the current branch and v${new_version}."
else
  echo "Not pushed. When you want this commit and tag on GitHub:"
  echo "  git push origin HEAD"
  echo "  git push origin v${new_version}"
fi
