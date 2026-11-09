#!/usr/bin/env bash
# Linux-side checks for the macOS release helpers. Does not build or sign.

set -euo pipefail

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=macos_release_common.sh
source "${AVA_SCRIPT_DIR}/macos_release_common.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_equal() {
  local actual="$1"
  local expected="$2"
  local label="$3"
  if [[ "$actual" != "$expected" ]]; then
    fail "${label}: expected '${expected}', got '${actual}'"
  fi
}

CMAKE_LISTS="${AVA_CPP_ROOT}/CMakeLists.txt"
assert_equal "$(ava_cmake_string AVA_APP_VERSION "$CMAKE_LISTS")" "0.1.0" "app version"
assert_equal "$(ava_cmake_string AVA_MACOS_DEPLOYMENT_TARGET "$CMAKE_LISTS")" "12.0" "deployment target"
assert_equal "$(ava_cmake_string AVA_MACOS_ARCHITECTURE "$CMAKE_LISTS")" "arm64" "architecture"
assert_equal "$(ava_distribution_basename "0.1.0" "arm64")" "AVA-0.1.0-arm64" "artifact name"

if ava_cmake_string AVA_DOES_NOT_EXIST "$CMAKE_LISTS" >/dev/null 2>&1; then
  fail "missing cmake variable should fail"
fi

BUILD_VERSION_TEXT="$(cat <<'EOF'
Load command 10
      cmd LC_BUILD_VERSION
  cmdsize 32
 platform 1
    minos 12.0
      sdk 14.5
   ntools 1
     tool 3
  version 1.0
EOF
)"
assert_equal "$(printf '%s\n' "$BUILD_VERSION_TEXT" | ava_minos_from_otool_text)" "12.0" "LC_BUILD_VERSION minos"

LEGACY_VERSION_TEXT="$(cat <<'EOF'
Load command 1
      cmd LC_SEGMENT_64
  cmdsize 72
  version 0.0
Load command 8
      cmd LC_VERSION_MIN_MACOSX
  cmdsize 16
  version 12.0
      sdk 13.0
EOF
)"
assert_equal "$(printf '%s\n' "$LEGACY_VERSION_TEXT" | ava_minos_from_otool_text)" "12.0" "LC_VERSION_MIN_MACOSX"

if [[ -n "$(printf '%s\n' "cmd LC_LOAD_DYLIB" | ava_minos_from_otool_text)" ]]; then
  fail "unrelated load command should not yield a minimum OS"
fi

LINK_FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/ava-link.XXXXXX")"
cleanup_link_fixture() {
  rm -rf "$LINK_FIXTURE"
}
trap cleanup_link_fixture EXIT
mkdir -p "${LINK_FIXTURE}/qt/bin"
printf 'tool\n' > "${LINK_FIXTURE}/qt/bin/macdeployqt"
chmod +x "${LINK_FIXTURE}/qt/bin/macdeployqt"
ln -s "${LINK_FIXTURE}/qt/bin/macdeployqt" "${LINK_FIXTURE}/macdeployqt"
assert_equal "$(ava_resolve_path "${LINK_FIXTURE}/macdeployqt")" "${LINK_FIXTURE}/qt/bin/macdeployqt" "symlink resolve"
assert_equal "$(ava_qt_prefix_from_tool "${LINK_FIXTURE}/macdeployqt")" "${LINK_FIXTURE}/qt" "qt prefix"

assert_equal "$AVA_COACH_BUILD_MARKER_NAME" "ava-coach-build" "marker name"
if ! declare -F ava_sign_disk_image >/dev/null; then
  fail "ava_sign_disk_image is missing"
fi
if ! declare -F ava_notarytool >/dev/null; then
  fail "ava_notarytool is missing"
fi
if ! grep -q '"ava-coach-build"' "${AVA_CPP_ROOT}/export/FfmpegLocator.cpp"; then
  fail "FfmpegLocator.cpp must look for the same ava-coach-build marker"
fi

if ava_require_macos >/dev/null 2>&1; then
  if [[ "$(uname -s)" != "Darwin" ]]; then
    fail "ava_require_macos should fail off macOS"
  fi
else
  if [[ "$(uname -s)" == "Darwin" ]]; then
    fail "ava_require_macos should pass on macOS"
  fi
fi

echo "macos_release_common_test: ok"
