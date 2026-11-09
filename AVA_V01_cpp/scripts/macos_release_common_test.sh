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
current_version="$(ava_read_marketing_version "$AVA_VERSION_FILE")"
current_build="$(ava_read_build_number "$AVA_VERSION_FILE")"
if [[ -z "$current_version" || -z "$current_build" ]]; then
  fail "VERSION did not parse"
fi
next_version="$(ava_bump_marketing_version "$current_version" patch)"
next_build="$(ava_next_build_number "$current_build")"
if [[ "$next_version" == "$current_version" || "$next_build" == "$current_build" ]]; then
  fail "a release bump should change both the marketing version and the build number"
fi
assert_equal "$(ava_cmake_string AVA_MACOS_DEPLOYMENT_TARGET "$CMAKE_LISTS")" "12.0" "deployment target"
assert_equal "$(ava_cmake_string AVA_MACOS_ARCHITECTURE "$CMAKE_LISTS")" "arm64" "architecture"
assert_equal "$(ava_distribution_basename "0.1.1" "arm64")" "AVA-0.1.1-arm64" "artifact name"
assert_equal "$(ava_bump_marketing_version 0.1.0 patch)" "0.1.1" "patch bump"
assert_equal "$(ava_bump_marketing_version 0.1.0 minor)" "0.2.0" "minor bump"
assert_equal "$(ava_bump_marketing_version 0.1.0 major)" "1.0.0" "major bump"
assert_equal "$(ava_bump_marketing_version 0.1.9 patch)" "0.1.10" "patch carries"
assert_equal "$(ava_bump_marketing_version 1.9.9 minor)" "1.10.0" "minor carries"
assert_equal "$(ava_bump_marketing_version 9.9.9 major)" "10.0.0" "major carries"
assert_equal "$(ava_bump_marketing_version 0.1.09 patch)" "0.1.10" "decimal not octal"
assert_equal "$(ava_next_build_number 1)" "2" "next build"
assert_equal "$(ava_next_build_number 9)" "10" "build carries"
assert_equal "$(ava_parse_release_args)" "patch 0" "default args"
assert_equal "$(ava_parse_release_args --minor)" "minor 0" "minor arg"
assert_equal "$(ava_parse_release_args --major --push)" "major 1" "major and push"
assert_equal "$(ava_parse_release_args --push --patch)" "patch 1" "push and patch"
assert_equal "$(ava_parse_release_args --help)" "help" "help arg"
if ava_bump_marketing_version 1.2 extra >/dev/null 2>&1; then
  fail "invalid bump should fail"
fi
if ava_parse_release_args --minor --major >/dev/null 2>&1; then
  fail "two bump flags should fail"
fi
if ava_parse_release_args --foo >/dev/null 2>&1; then
  fail "unknown flag should fail"
fi

VERSION_FIXTURE="$(mktemp "${TMPDIR:-/tmp}/ava-version.XXXXXX")"
ava_write_version_file "$VERSION_FIXTURE" "0.2.0" "4"
assert_equal "$(ava_read_marketing_version "$VERSION_FIXTURE")" "0.2.0" "written version"
assert_equal "$(ava_read_build_number "$VERSION_FIXTURE")" "4" "written build"
rm -f "$VERSION_FIXTURE"
if ava_read_marketing_version "${VERSION_FIXTURE}.missing" >/dev/null 2>&1; then
  fail "missing VERSION should fail"
fi

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

RELEASE_REPO="$(mktemp -d "${TMPDIR:-/tmp}/ava-release-repo.XXXXXX")"
git -C "$RELEASE_REPO" init -q
git -C "$RELEASE_REPO" config user.email "ava-release-test@example.com"
git -C "$RELEASE_REPO" config user.name "AVA release test"
ava_require_clean_worktree "$RELEASE_REPO"
printf 'note\n' > "${RELEASE_REPO}/note.txt"
if ava_require_clean_worktree "$RELEASE_REPO" >/dev/null 2>&1; then
  fail "a dirty tree should be refused"
fi
git -C "$RELEASE_REPO" add note.txt
git -C "$RELEASE_REPO" commit -q -m "Initial"
ava_require_clean_worktree "$RELEASE_REPO"
ava_require_release_branch "$RELEASE_REPO"
if ava_release_tag_exists "$RELEASE_REPO" "0.1.1"; then
  fail "v0.1.1 should not exist yet"
fi
git -C "$RELEASE_REPO" tag -a v0.1.1 -m "AVA 0.1.1"
if ! ava_release_tag_exists "$RELEASE_REPO" "0.1.1"; then
  fail "v0.1.1 should exist"
fi
rm -rf "$RELEASE_REPO"

echo "macos_release_common_test: ok"
