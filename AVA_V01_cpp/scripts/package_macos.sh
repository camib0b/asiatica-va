#!/usr/bin/env bash
# Build an unsigned Apple Silicon AVA.app and a DMG with an Applications alias.
# This does not sign or notarize. Gatekeeper will warn until release_macos.sh
# (or sign_and_notarize.sh) runs with a Developer ID certificate.
#
# Usage (from anywhere):
#   ./AVA_V01_cpp/scripts/package_macos.sh

set -euo pipefail

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=macos_release_common.sh
source "${AVA_SCRIPT_DIR}/macos_release_common.sh"

ava_require_macos

CMAKE_LISTS="${AVA_CPP_ROOT}/CMakeLists.txt"
ARCHITECTURE="$(ava_cmake_string AVA_MACOS_ARCHITECTURE "$CMAKE_LISTS")" || exit 1
DEPLOYMENT_TARGET="$(ava_cmake_string AVA_MACOS_DEPLOYMENT_TARGET "$CMAKE_LISTS")" || exit 1
APP_VERSION="$(ava_read_marketing_version "$AVA_VERSION_FILE")" || exit 1
APP_BUILD="$(ava_read_build_number "$AVA_VERSION_FILE")" || exit 1
ava_require_host_architecture "$ARCHITECTURE"

DISTRIBUTION_DIRECTORY="${AVA_CPP_ROOT}/dist"
BUILD_DIRECTORY="${AVA_CPP_ROOT}/build-release"
VENDOR_ROOT="${AVA_CPP_ROOT}/third_party/ffmpeg/macos"

"${AVA_SCRIPT_DIR}/make_icns.sh"

FFMPEG_SOURCE="$(ava_resolve_vendored_helper ffmpeg "$ARCHITECTURE" "$VENDOR_ROOT" || true)"
FFPROBE_SOURCE="$(ava_resolve_vendored_helper ffprobe "$ARCHITECTURE" "$VENDOR_ROOT" || true)"
if [[ -z "$FFMPEG_SOURCE" || -z "$FFPROBE_SOURCE" ]]; then
  echo "Vendored FFmpeg not found for ${ARCHITECTURE}. Running vendor_ffmpeg_macos.sh..."
  if ! "${AVA_SCRIPT_DIR}/vendor_ffmpeg_macos.sh"; then
    echo "FFmpeg download failed, so packaging cannot bundle ffmpeg and ffprobe." >&2
    echo "Read the error above, then retry:" >&2
    echo "  ${AVA_SCRIPT_DIR}/vendor_ffmpeg_macos.sh" >&2
    echo "  ${AVA_SCRIPT_DIR}/package_macos.sh" >&2
    exit 1
  fi
  FFMPEG_SOURCE="$(ava_resolve_vendored_helper ffmpeg "$ARCHITECTURE" "$VENDOR_ROOT" || true)"
  FFPROBE_SOURCE="$(ava_resolve_vendored_helper ffprobe "$ARCHITECTURE" "$VENDOR_ROOT" || true)"
fi
if [[ -z "$FFMPEG_SOURCE" || -z "$FFPROBE_SOURCE" ]]; then
  echo "FFmpeg download finished, but ffmpeg and ffprobe are still missing for ${ARCHITECTURE}." >&2
  echo "Expected them under third_party/ffmpeg/macos/${ARCHITECTURE}/ (or universal/)." >&2
  exit 1
fi

MACDEPLOYQT_BIN="$(ava_find_macdeployqt)" || exit 1
QT_PREFIX="$(ava_qt_prefix_from_tool "$MACDEPLOYQT_BIN")" || exit 1
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_PREFIX_ARGUMENT="${QT_PREFIX};${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_ARGUMENT="${QT_PREFIX}"
fi

cmake -S "$AVA_CPP_ROOT" -B "$BUILD_DIRECTORY" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_ARGUMENT}" \
  -DCMAKE_OSX_ARCHITECTURES="${ARCHITECTURE}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"

cmake --build "$BUILD_DIRECTORY" --config Release --parallel

BUILT_APP="$(ava_find_built_app "$BUILD_DIRECTORY")" || exit 1
APPLICATION_BUNDLE="${DISTRIBUTION_DIRECTORY}/AVA.app"

rm -rf "$DISTRIBUTION_DIRECTORY"
mkdir -p "$DISTRIBUTION_DIRECTORY"
ditto "$BUILT_APP" "$APPLICATION_BUNDLE"

"$MACDEPLOYQT_BIN" "$APPLICATION_BUNDLE" -verbose=1 -always-overwrite

HELPERS_DIRECTORY="${APPLICATION_BUNDLE}/Contents/Helpers"
mkdir -p "$HELPERS_DIRECTORY"
cp "$FFMPEG_SOURCE" "${HELPERS_DIRECTORY}/ffmpeg"
cp "$FFPROBE_SOURCE" "${HELPERS_DIRECTORY}/ffprobe"
chmod 755 "${HELPERS_DIRECTORY}/ffmpeg" "${HELPERS_DIRECTORY}/ffprobe"
xattr -cr "${HELPERS_DIRECTORY}/ffmpeg" "${HELPERS_DIRECTORY}/ffprobe" 2>/dev/null || true

for helper_name in ffmpeg ffprobe; do
  helper_path="${HELPERS_DIRECTORY}/${helper_name}"
  ava_assert_macho "$helper_path"
  ava_assert_contains_architecture "$helper_path" "$ARCHITECTURE"
  ava_reject_homebrew_linkage "$helper_path"
done

ava_write_coach_marker "$APPLICATION_BUNDLE" "$APP_VERSION" "$APP_BUILD" "$ARCHITECTURE" "$DEPLOYMENT_TARGET"
ava_assert_bundle_metadata "$APPLICATION_BUNDLE" "$APP_VERSION" "$APP_BUILD" "$ARCHITECTURE" "$DEPLOYMENT_TARGET"

print_helper_version_line() {
  local helper_path="$1"
  local version_output
  version_output="$("$helper_path" -version 2>&1 || true)"
  echo "  ${version_output%%$'\n'*}"
}

echo "Bundled FFmpeg helpers into Contents/Helpers:"
print_helper_version_line "${HELPERS_DIRECTORY}/ffmpeg"
print_helper_version_line "${HELPERS_DIRECTORY}/ffprobe"

ARTIFACT_BASENAME="$(ava_distribution_basename "$APP_VERSION" "$ARCHITECTURE")"
ZIP_PATH="${DISTRIBUTION_DIRECTORY}/${ARTIFACT_BASENAME}.app.zip"
DISK_IMAGE_PATH="${DISTRIBUTION_DIRECTORY}/${ARTIFACT_BASENAME}.dmg"
GIT_SHA="$(ava_git_sha "$(cd "${AVA_CPP_ROOT}/.." && pwd)")"

ava_write_release_info \
  "${DISTRIBUTION_DIRECTORY}/release-info.txt" \
  "$APP_VERSION" \
  "$APP_BUILD" \
  "$ARCHITECTURE" \
  "$DEPLOYMENT_TARGET" \
  "$GIT_SHA"
ava_write_application_zip "$APPLICATION_BUNDLE" "$ZIP_PATH"
ava_create_dmg "$APPLICATION_BUNDLE" "$DISK_IMAGE_PATH" "AVA"
ava_verify_dmg_layout "$DISK_IMAGE_PATH"

echo
echo "Unsigned coach build:"
echo "  ${APPLICATION_BUNDLE}"
echo "  ${ZIP_PATH}"
echo "  ${DISK_IMAGE_PATH}"
echo
echo "This DMG is not notarized. Do not send it to coaches."
echo "On this Mac, after the Developer ID certificate and notarytool profile exist:"
echo "  ${AVA_SCRIPT_DIR}/release_macos.sh"
echo "Or, if this folder was just built:"
echo "  ${AVA_SCRIPT_DIR}/sign_and_notarize.sh"
