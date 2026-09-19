#!/usr/bin/env bash
# Build a distributable AVA.app on Camila's Mac (Qt must be installed).
# This does NOT sign or notarize. Gatekeeper will still warn until she runs
# scripts/sign_and_notarize.sh with her Apple Developer ID.
# Bundles static ffmpeg/ffprobe into Contents/Helpers after macdeployqt
# (run scripts/vendor_ffmpeg_macos.sh first, or this script will invoke it).
#
# Usage (from anywhere):
#   ./AVA_V01_cpp/scripts/package_macos.sh
#
# Optional:
#   AVA_LICENSE_SIGNING_SECRET=... AVA_LICENSE_API_URL=https://ava-license.foo.workers.dev \
#     ./AVA_V01_cpp/scripts/package_macos.sh

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "package_macos.sh must run on macOS (Camila's Mac)."
  echo "This cloud/Linux environment cannot run macdeployqt or produce a coach-ready .app."
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
BUILD="$ROOT/build-release"

find_macdeployqt() {
  if [[ -n "${MACDEPLOYQT:-}" && -x "$MACDEPLOYQT" ]]; then
    echo "$MACDEPLOYQT"
    return
  fi
  if command -v macdeployqt >/dev/null 2>&1; then
    command -v macdeployqt
    return
  fi
  local qmake_bin=""
  if command -v qmake6 >/dev/null 2>&1; then
    qmake_bin="$(command -v qmake6)"
  elif command -v qmake >/dev/null 2>&1; then
    qmake_bin="$(command -v qmake)"
  fi
  if [[ -n "$qmake_bin" ]]; then
    local candidate
    candidate="$(dirname "$qmake_bin")/macdeployqt"
    if [[ -x "$candidate" ]]; then
      echo "$candidate"
      return
    fi
  fi
  if command -v brew >/dev/null 2>&1; then
    local prefix
    prefix="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
    if [[ -n "$prefix" && -x "$prefix/bin/macdeployqt" ]]; then
      echo "$prefix/bin/macdeployqt"
      return
    fi
  fi
  echo "Could not find macdeployqt. Install Qt 6 (brew install qt) and retry." >&2
  exit 1
}

MACDEPLOYQT_BIN="$(find_macdeployqt)"

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  ${AVA_LICENSE_SIGNING_SECRET:+-DAVA_LICENSE_SIGNING_SECRET="$AVA_LICENSE_SIGNING_SECRET"} \
  ${AVA_LICENSE_API_URL:+-DAVA_LICENSE_API_URL="$AVA_LICENSE_API_URL"}

cmake --build "$BUILD" --config Release

APP_SRC="$(find "$BUILD" -name 'AVA.app' -maxdepth 3 | head -n 1)"
if [[ -z "$APP_SRC" ]]; then
  echo "Build succeeded but AVA.app was not found under $BUILD" >&2
  exit 1
fi

rm -rf "$DIST"
mkdir -p "$DIST"
cp -R "$APP_SRC" "$DIST/AVA.app"

# Bundle optional runtime config next to the binary (Contents/MacOS).
if [[ -f "$ROOT/config/license_server.json" ]]; then
  cp "$ROOT/config/license_server.json" "$DIST/AVA.app/Contents/MacOS/license_server.json"
fi

"$MACDEPLOYQT_BIN" "$DIST/AVA.app" -verbose=1

# Copy static ffmpeg/ffprobe after macdeployqt so they stay in Contents/Helpers
# (not next to the Qt executable, which confuses macdeployqt).
HELPERS_DIR="$DIST/AVA.app/Contents/Helpers"
VENDOR_FFMPEG_ROOT="$ROOT/third_party/ffmpeg/macos"
HOST_ARCH="$(uname -m)"

resolve_vendored_helper() {
  local tool_name="$1"
  if [[ -x "$VENDOR_FFMPEG_ROOT/universal/$tool_name" ]]; then
    echo "$VENDOR_FFMPEG_ROOT/universal/$tool_name"
    return 0
  fi
  if [[ -x "$VENDOR_FFMPEG_ROOT/$HOST_ARCH/$tool_name" ]]; then
    echo "$VENDOR_FFMPEG_ROOT/$HOST_ARCH/$tool_name"
    return 0
  fi
  return 1
}

FFMPEG_SRC=""
FFPROBE_SRC=""
FFMPEG_SRC="$(resolve_vendored_helper ffmpeg)" || true
FFPROBE_SRC="$(resolve_vendored_helper ffprobe)" || true
if [[ -z "$FFMPEG_SRC" || -z "$FFPROBE_SRC" ]]; then
  echo "Vendored FFmpeg not found for $HOST_ARCH. Running vendor_ffmpeg_macos.sh..."
  "$ROOT/scripts/vendor_ffmpeg_macos.sh"
  FFMPEG_SRC="$(resolve_vendored_helper ffmpeg)"
  FFPROBE_SRC="$(resolve_vendored_helper ffprobe)"
fi
if [[ -z "$FFMPEG_SRC" || -z "$FFPROBE_SRC" ]]; then
  echo "Missing third_party/ffmpeg/macos/$HOST_ARCH/ffmpeg and ffprobe after vendoring." >&2
  echo "Run: $ROOT/scripts/vendor_ffmpeg_macos.sh" >&2
  exit 1
fi

mkdir -p "$HELPERS_DIR"
cp "$FFMPEG_SRC" "$HELPERS_DIR/ffmpeg"
cp "$FFPROBE_SRC" "$HELPERS_DIR/ffprobe"
chmod 755 "$HELPERS_DIR/ffmpeg" "$HELPERS_DIR/ffprobe"
xattr -cr "$HELPERS_DIR/ffmpeg" "$HELPERS_DIR/ffprobe" 2>/dev/null || true

print_helper_version_line() {
  local helper_path="$1"
  local version_output
  version_output="$("$helper_path" -version 2>&1)"
  echo "  ${version_output%%$'\n'*}"
}

echo "Bundled FFmpeg helpers into Contents/Helpers (after macdeployqt):"
print_helper_version_line "$HELPERS_DIR/ffmpeg"
print_helper_version_line "$HELPERS_DIR/ffprobe"

ditto -c -k --keepParent "$DIST/AVA.app" "$DIST/AVA.app.zip"

hdiutil create -volname "AVA" -srcfolder "$DIST/AVA.app" -ov -format UDZO "$DIST/AVA.dmg"

echo
echo "Built:"
echo "  $DIST/AVA.app"
echo "  $DIST/AVA.app.zip"
echo "  $DIST/AVA.dmg"
echo
echo "Next: sign and notarize on this Mac (see DISTRIBUTION.md):"
echo "  ./AVA_V01_cpp/scripts/sign_and_notarize.sh"
echo
echo "Without notarization, macOS Gatekeeper will scare non-technical coaches."
