#!/usr/bin/env bash
# Sign dist/AVA.app inside-out, notarize and staple the app, then build a DMG,
# sign that DMG, and notarize and staple it. Does not use codesign --deep.
# Nested frameworks, plugins, and the FFmpeg helpers are signed before the
# outer bundle. The DMG is signed on its own; stapling an unsigned image fails.
#
# One-time (stores credentials in the login keychain):
#   xcrun notarytool store-credentials "ava-notarize" \
#     --apple-id "YOUR_APPLE_ID" \
#     --team-id "YOUR_TEAM_ID" \
#     --password "app-specific-password"
#
# Then, from the repo:
#   ./AVA_V01_cpp/scripts/sign_and_notarize.sh
#
# AVA_CODESIGN_IDENTITY is optional when the keychain has exactly one
# "Developer ID Application" identity. CI can pass APPLE_ID, APPLE_TEAM_ID,
# and APPLE_APP_SPECIFIC_PASSWORD instead of a saved profile.

set -euo pipefail

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=macos_release_common.sh
source "${AVA_SCRIPT_DIR}/macos_release_common.sh"

ava_require_macos

CMAKE_LISTS="${AVA_CPP_ROOT}/CMakeLists.txt"
ARCHITECTURE="$(ava_cmake_string AVA_MACOS_ARCHITECTURE "$CMAKE_LISTS")" || exit 1
DEPLOYMENT_TARGET="$(ava_cmake_string AVA_MACOS_DEPLOYMENT_TARGET "$CMAKE_LISTS")" || exit 1
APP_VERSION="$(ava_cmake_string AVA_APP_VERSION "$CMAKE_LISTS")" || exit 1
APPLICATION_BUNDLE="${AVA_CPP_ROOT}/dist/AVA.app"
DISTRIBUTION_DIRECTORY="${AVA_CPP_ROOT}/dist"
NOTARY_PROFILE="${NOTARYTOOL_PROFILE:-ava-notarize}"
APPLICATION_ENTITLEMENTS="${AVA_CPP_ROOT}/macos/entitlements.plist"
HELPER_ENTITLEMENTS="${AVA_CPP_ROOT}/macos/helper-entitlements.plist"

if [[ ! -d "$APPLICATION_BUNDLE" ]]; then
  echo "Missing ${APPLICATION_BUNDLE}." >&2
  echo "Run ${AVA_SCRIPT_DIR}/package_macos.sh first, or ${AVA_SCRIPT_DIR}/release_macos.sh." >&2
  exit 1
fi

if [[ ! -x "${APPLICATION_BUNDLE}/Contents/Helpers/ffmpeg" || ! -x "${APPLICATION_BUNDLE}/Contents/Helpers/ffprobe" ]]; then
  echo "Missing Contents/Helpers/ffmpeg or ffprobe." >&2
  echo "Re-run ${AVA_SCRIPT_DIR}/package_macos.sh." >&2
  exit 1
fi

IDENTITY="$(ava_detect_codesign_identity)" || exit 1
if [[ -z "$IDENTITY" ]]; then
  echo "No Developer ID Application identity was selected." >&2
  exit 1
fi
ava_prepare_notary_credentials "$NOTARY_PROFILE"

echo "Signing inside out with ${IDENTITY}"
echo "codesign --deep is not used."
ava_sign_application "$APPLICATION_BUNDLE" "$IDENTITY" "$APPLICATION_ENTITLEMENTS" "$HELPER_ENTITLEMENTS"
ava_verify_signed_application "$APPLICATION_BUNDLE"
ava_assert_bundle_metadata "$APPLICATION_BUNDLE" "$APP_VERSION" "$ARCHITECTURE" "$DEPLOYMENT_TARGET"

NOTARIZATION_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/ava-notarize.XXXXXX")"
NOTARIZATION_ZIP="${NOTARIZATION_DIRECTORY}/AVA.zip"
cleanup_notarization_zip() {
  rm -rf "$NOTARIZATION_DIRECTORY"
}
trap cleanup_notarization_zip EXIT
ava_write_application_zip "$APPLICATION_BUNDLE" "$NOTARIZATION_ZIP"

echo "Submitting to Apple notarization (profile: ${NOTARY_PROFILE})"
ava_submit_for_notarization "$NOTARIZATION_ZIP" "$NOTARY_PROFILE"
xcrun stapler staple "$APPLICATION_BUNDLE"
xcrun stapler validate "$APPLICATION_BUNDLE"
codesign --verify --deep --strict --verbose=2 "$APPLICATION_BUNDLE"

ARTIFACT_BASENAME="$(ava_distribution_basename "$APP_VERSION" "$ARCHITECTURE")"
ZIP_PATH="${DISTRIBUTION_DIRECTORY}/${ARTIFACT_BASENAME}.app.zip"
DISK_IMAGE_PATH="${DISTRIBUTION_DIRECTORY}/${ARTIFACT_BASENAME}.dmg"
GIT_SHA="$(ava_git_sha "$(cd "${AVA_CPP_ROOT}/.." && pwd)")"

ava_write_release_info \
  "${DISTRIBUTION_DIRECTORY}/release-info.txt" \
  "$APP_VERSION" \
  "$ARCHITECTURE" \
  "$DEPLOYMENT_TARGET" \
  "$GIT_SHA"
ava_write_application_zip "$APPLICATION_BUNDLE" "$ZIP_PATH"
AVA_DMG_LAYOUT=0 ava_create_dmg "$APPLICATION_BUNDLE" "$DISK_IMAGE_PATH" "AVA"
AVA_DMG_EXPECT_SIGNATURE=1 ava_verify_dmg_layout "$DISK_IMAGE_PATH"
echo "Signing the disk image with ${IDENTITY}"
ava_sign_disk_image "$DISK_IMAGE_PATH" "$IDENTITY"
echo "Submitting the disk image to Apple notarization (profile: ${NOTARY_PROFILE})"
ava_submit_for_notarization "$DISK_IMAGE_PATH" "$NOTARY_PROFILE"
xcrun stapler staple "$DISK_IMAGE_PATH"
xcrun stapler validate "$DISK_IMAGE_PATH"
xcrun stapler validate "$APPLICATION_BUNDLE"

echo "Checking Gatekeeper"
ava_spctl_assess execute "$APPLICATION_BUNDLE"

echo
echo "Notarized coach build:"
echo "  ${APPLICATION_BUNDLE}"
echo "  ${ZIP_PATH}"
echo "  ${DISK_IMAGE_PATH}"
echo
echo "Upload the DMG. A coach drags AVA to Applications and opens it."
