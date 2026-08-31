#!/usr/bin/env bash
# Sign and notarize dist/AVA.app with Camila's Apple Developer ID.
# Cloud agents cannot hold a Developer ID. Run this on her Mac after
# package_macos.sh.
#
# One-time (stores credentials in Keychain, no password on the command line):
#   xcrun notarytool store-credentials "ava-notarize" \
#     --apple-id "YOUR_APPLE_ID@email" \
#     --team-id "YOUR_TEAM_ID" \
#     --password "app-specific-password"
#
# Then:
#   AVA_CODESIGN_IDENTITY="Developer ID Application: Camila Escudero (TEAMID)" \
#     ./AVA_V01_cpp/scripts/sign_and_notarize.sh

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "sign_and_notarize.sh must run on Camila's Mac. A cloud VM cannot notarize."
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/dist/AVA.app"
ZIP="$ROOT/dist/AVA-notarize.zip"
PROFILE="${NOTARYTOOL_PROFILE:-ava-notarize}"
IDENTITY="${AVA_CODESIGN_IDENTITY:-}"

if [[ ! -d "$APP" ]]; then
  echo "Missing $APP — run ./AVA_V01_cpp/scripts/package_macos.sh first."
  exit 1
fi

if [[ -z "$IDENTITY" ]]; then
  echo "Set AVA_CODESIGN_IDENTITY to your Developer ID Application identity."
  echo "List identities with:"
  echo "  security find-identity -p codesigning -v"
  exit 1
fi

echo "Signing AVA.app with $IDENTITY"
codesign --force --deep --options runtime --timestamp \
  --sign "$IDENTITY" \
  "$APP"

codesign --verify --deep --strict --verbose=2 "$APP"

echo "Submitting to Apple notarization (profile: $PROFILE)"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"

# Refresh zip/dmg so coaches get the stapled ticket.
ditto -c -k --keepParent "$APP" "$ROOT/dist/AVA.app.zip"
hdiutil create -volname "AVA" -srcfolder "$APP" -ov -format UDZO "$ROOT/dist/AVA.dmg"

echo
echo "Notarized:"
echo "  $APP"
echo "  $ROOT/dist/AVA.app.zip"
echo "  $ROOT/dist/AVA.dmg"
echo
echo "Upload the .dmg (or .zip) wherever coaches will download it."
echo "Do not skip notarization — Gatekeeper will block or scare coaches."
