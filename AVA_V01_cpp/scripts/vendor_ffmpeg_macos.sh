#!/usr/bin/env bash
# Download pinned static ffmpeg/ffprobe into third_party/ffmpeg/macos/<arch>/.
# Binaries are gitignored. Re-run when macos/SHA256PINS changes.
#
# Usage:
#   ./AVA_V01_cpp/scripts/vendor_ffmpeg_macos.sh

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "vendor_ffmpeg_macos.sh must run on macOS (needs unzip, shasum, otool, lipo)."
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR_ROOT="$ROOT/third_party/ffmpeg/macos"
PINS_FILE="$VENDOR_ROOT/SHA256PINS"
DOWNLOAD_DIR="$VENDOR_ROOT/.download"

if [[ ! -f "$PINS_FILE" ]]; then
  echo "Missing pin file: $PINS_FILE" >&2
  exit 1
fi

mkdir -p "$DOWNLOAD_DIR"

has_non_system_dylib() {
  local binary_path="$1"
  otool -L "$binary_path" | awk 'NR > 1 { print $1 }' | grep -E '^(/opt/homebrew/|/usr/local/opt/|@rpath|@executable_path|@loader_path)' >/dev/null
}

expected_lipo_arch() {
  local uname_arch="$1"
  case "$uname_arch" in
    arm64) echo "arm64" ;;
    x86_64) echo "x86_64" ;;
    *) echo "$uname_arch" ;;
  esac
}

vendor_one() {
  local arch="$1"
  local tool_name="$2"
  local expected_sha256="$3"
  local url="$4"

  local zip_name="${arch}-${tool_name}.zip"
  local zip_path="$DOWNLOAD_DIR/$zip_name"
  local dest_dir="$VENDOR_ROOT/$arch"
  local dest_path="$dest_dir/$tool_name"

  echo "Downloading $tool_name ($arch)"
  curl -fL --retry 3 --retry-delay 2 -o "$zip_path" "$url"

  local actual_sha256
  actual_sha256="$(shasum -a 256 "$zip_path" | awk '{ print $1 }')"
  if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    echo "SHA256 mismatch for $zip_name" >&2
    echo "  expected: $expected_sha256" >&2
    echo "  actual:   $actual_sha256" >&2
    exit 1
  fi

  local extract_dir
  extract_dir="$(mktemp -d "${TMPDIR:-/tmp}/ava-ffmpeg.XXXXXX")"
  unzip -q -o "$zip_path" -d "$extract_dir"

  local extracted_binary
  extracted_binary="$(find "$extract_dir" -type f -name "$tool_name" | head -n 1)"
  if [[ -z "$extracted_binary" || ! -f "$extracted_binary" ]]; then
    echo "Zip did not contain $tool_name: $zip_path" >&2
    rm -rf "$extract_dir"
    exit 1
  fi

  local actual_arch
  actual_arch="$(lipo -archs "$extracted_binary" 2>/dev/null || true)"
  local expected_arch
  expected_arch="$(expected_lipo_arch "$arch")"
  if [[ "$actual_arch" != *"$expected_arch"* ]]; then
    echo "Unexpected architecture for $tool_name ($arch): lipo -archs => '$actual_arch'" >&2
    rm -rf "$extract_dir"
    exit 1
  fi

  if has_non_system_dylib "$extracted_binary"; then
    echo "Refusing $tool_name ($arch): links Homebrew or @rpath dylibs. Use a static snapshot." >&2
    otool -L "$extracted_binary" >&2
    rm -rf "$extract_dir"
    exit 1
  fi

  mkdir -p "$dest_dir"
  cp "$extracted_binary" "$dest_path"
  chmod 755 "$dest_path"
  xattr -cr "$dest_path" 2>/dev/null || true
  rm -rf "$extract_dir"
  echo "  -> $dest_path"
}

while read -r arch tool_name expected_sha256 url; do
  [[ -z "${arch:-}" || "$arch" == \#* ]] && continue
  vendor_one "$arch" "$tool_name" "$expected_sha256" "$url"
done < "$PINS_FILE"

rm -rf "$DOWNLOAD_DIR"

echo
echo "Vendored static FFmpeg into $VENDOR_ROOT"
echo "Next: ./AVA_V01_cpp/scripts/package_macos.sh"
