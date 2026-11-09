#!/usr/bin/env bash
# One command on an Apple Silicon Mac:
#   vendor the pinned FFmpeg, build the unsigned app, sign, notarize, staple.
#
# Usage (from anywhere, after the Developer ID certificate and notarytool
# profile in DISTRIBUTION.md):
#   ./AVA_V01_cpp/scripts/release_macos.sh

set -euo pipefail

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=macos_release_common.sh
source "${AVA_SCRIPT_DIR}/macos_release_common.sh"

ava_require_macos

echo "Downloading pinned FFmpeg"
"${AVA_SCRIPT_DIR}/vendor_ffmpeg_macos.sh"
echo "Building the unsigned app and DMG"
"${AVA_SCRIPT_DIR}/package_macos.sh"
echo "Signing and notarizing"
"${AVA_SCRIPT_DIR}/sign_and_notarize.sh"

echo
echo "Release finished. Send the DMG printed above. Do not send an unsigned build."
