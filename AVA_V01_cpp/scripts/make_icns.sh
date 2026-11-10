#!/usr/bin/env bash
# Create AppIcon.icns from AppIcon.iconset.
# package_macos.sh runs this before the Release build. The committed
# AppIcon.icns is the fallback when a build cannot run iconutil.

set -euo pipefail

cd "$(dirname "$0")/.."
ICONSET="AppIcon.iconset"
OUT="AppIcon.icns"

if [[ ! -d "$ICONSET" ]]; then
  echo "Missing ${ICONSET}." >&2
  exit 1
fi
if ! command -v iconutil >/dev/null 2>&1; then
  echo "iconutil is not on PATH. Install the Xcode command line tools and retry." >&2
  exit 1
fi
iconutil -c icns "$ICONSET" -o "$OUT"
echo "Created ${OUT}"
