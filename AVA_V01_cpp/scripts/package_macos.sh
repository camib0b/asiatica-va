#!/usr/bin/env bash
# Build a distributable AVA.app on Camila's Mac (Qt must be installed).
# This does NOT sign or notarize. Gatekeeper will still warn until she runs
# scripts/sign_and_notarize.sh with her Apple Developer ID.
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
if [[ -f "$ROOT/config/youtube_oauth.json" ]]; then
  cp "$ROOT/config/youtube_oauth.json" "$DIST/AVA.app/Contents/MacOS/youtube_oauth.json"
fi

"$MACDEPLOYQT_BIN" "$DIST/AVA.app" -verbose=1

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
