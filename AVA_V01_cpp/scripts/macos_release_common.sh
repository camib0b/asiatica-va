#!/usr/bin/env bash
# Shared helpers for the Apple Silicon coach release.
# Sourced by package_macos.sh, sign_and_notarize.sh, and release_macos.sh.
# macOS still ships Bash 3.2. Do not use mapfile, associative arrays, or ${var,,}.

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "Source this file from the macOS release scripts. Do not run it directly." >&2
  exit 1
fi

AVA_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC2034 # Callers that source this file read AVA_CPP_ROOT.
AVA_CPP_ROOT="$(cd "${AVA_SCRIPT_DIR}/.." && pwd)"

# Keep this file name in sync with FfmpegLocator.cpp (ava-coach-build).
AVA_COACH_BUILD_MARKER_NAME="ava-coach-build"

AVA_DMG_STAGING=""
AVA_DMG_MOUNT=""
AVA_DMG_ATTACHED=0
AVA_CODESIGN_DER=""

ava_require_macos() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script must run on macOS. A Linux agent cannot run macdeployqt, codesign, or notarization." >&2
    return 1
  fi
}

ava_cmake_string() {
  local variable_name="$1"
  local cmake_file="$2"
  local line value
  line="$(grep -E "^set\\(${variable_name} \"" "$cmake_file" | head -n 1 || true)"
  value="$(printf '%s\n' "$line" | sed -n 's/^set([^ ]* "\([^"]*\)").*/\1/p')"
  if [[ -z "$value" ]]; then
    echo "Could not read ${variable_name} from ${cmake_file}." >&2
    return 1
  fi
  printf '%s\n' "$value"
}

ava_require_host_architecture() {
  local expected_architecture="$1"
  local host_architecture
  host_architecture="$(uname -m)"
  if [[ "$host_architecture" != "$expected_architecture" ]]; then
    echo "This Mac is ${host_architecture}. AVA coach builds are ${expected_architecture} and this script does not cross-compile." >&2
    return 1
  fi
}

ava_resolve_path() {
  local path="$1"
  local link_target directory hops=0
  while [[ -L "$path" ]]; do
    hops=$((hops + 1))
    if [[ "$hops" -gt 20 ]]; then
      echo "Too many symlinks resolving ${1}." >&2
      return 1
    fi
    link_target="$(readlink "$path")"
    if [[ "$link_target" != /* ]]; then
      directory="$(cd "$(dirname "$path")" && pwd)"
      link_target="${directory}/${link_target}"
    fi
    path="$link_target"
  done
  if [[ ! -e "$path" ]]; then
    echo "Path does not exist: ${1}" >&2
    return 1
  fi
  printf '%s\n' "$path"
}

ava_qt_prefix_from_tool() {
  local tool_path="$1"
  local resolved prefix
  resolved="$(ava_resolve_path "$tool_path")"
  prefix="$(cd "$(dirname "$resolved")/.." && pwd)"
  printf '%s\n' "$prefix"
}

ava_find_macdeployqt() {
  local candidate prefix qmake_bin
  if [[ -n "${MACDEPLOYQT:-}" && -x "${MACDEPLOYQT}" ]]; then
    printf '%s\n' "$MACDEPLOYQT"
    return 0
  fi
  if command -v macdeployqt >/dev/null 2>&1; then
    command -v macdeployqt
    return 0
  fi
  qmake_bin=""
  if command -v qmake6 >/dev/null 2>&1; then
    qmake_bin="$(command -v qmake6)"
  elif command -v qmake >/dev/null 2>&1; then
    qmake_bin="$(command -v qmake)"
  fi
  if [[ -n "$qmake_bin" ]]; then
    candidate="$(dirname "$qmake_bin")/macdeployqt"
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  fi
  if command -v brew >/dev/null 2>&1; then
    prefix="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
    if [[ -n "$prefix" && -x "${prefix}/bin/macdeployqt" ]]; then
      printf '%s\n' "${prefix}/bin/macdeployqt"
      return 0
    fi
  fi
  echo "Could not find macdeployqt. Install Qt 6 (brew install qt) and retry." >&2
  return 1
}

ava_find_built_app() {
  local build_directory="$1"
  local candidate found=""
  while IFS= read -r candidate; do
    if [[ -d "$candidate" ]]; then
      found="$candidate"
      break
    fi
  done < <(find "$build_directory" -name "AVA.app" -type d -maxdepth 4)
  if [[ -z "$found" ]]; then
    echo "Build finished but AVA.app was not found under ${build_directory}." >&2
    return 1
  fi
  printf '%s\n' "$found"
}

ava_plist_string() {
  local key="$1"
  local plist_path="$2"
  plutil -extract "$key" raw -o - "$plist_path"
}

ava_minos_from_otool_text() {
  awk '
    $1 == "cmd" && $2 == "LC_BUILD_VERSION" { in_build = 1; in_legacy = 0 }
    $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { in_legacy = 1; in_build = 0 }
    $1 == "cmd" && $2 != "LC_BUILD_VERSION" && $2 != "LC_VERSION_MIN_MACOSX" {
      in_build = 0
      in_legacy = 0
    }
    in_build && $1 == "minos" { print $2; exit }
    in_legacy && $1 == "version" { print $2; exit }
  '
}

ava_binary_minos() {
  local binary_path="$1"
  local minos
  minos="$(otool -l "$binary_path" | ava_minos_from_otool_text)"
  if [[ -z "$minos" ]]; then
    echo "Could not read the minimum macOS version from ${binary_path}." >&2
    return 1
  fi
  printf '%s\n' "$minos"
}

ava_assert_macho() {
  local binary_path="$1"
  local description
  description="$(file -b "$binary_path")"
  if [[ "$description" != *"Mach-O"* ]]; then
    echo "${binary_path} is not a Mach-O binary (${description})." >&2
    return 1
  fi
}

ava_assert_exact_architecture() {
  local binary_path="$1"
  local expected_architecture="$2"
  local actual_architecture
  actual_architecture="$(lipo -archs "$binary_path")"
  if [[ "$actual_architecture" != "$expected_architecture" ]]; then
    echo "${binary_path} is '${actual_architecture}', expected ${expected_architecture}." >&2
    return 1
  fi
}

ava_assert_contains_architecture() {
  local binary_path="$1"
  local expected_architecture="$2"
  local actual_architectures
  actual_architectures="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  case " ${actual_architectures} " in
    *" ${expected_architecture} "*) return 0 ;;
  esac
  echo "${binary_path} does not include ${expected_architecture} (lipo -archs: '${actual_architectures}')." >&2
  return 1
}

ava_reject_homebrew_linkage() {
  local binary_path="$1"
  local linked
  linked="$(otool -L "$binary_path" | awk 'NR > 1 { print $1 }' | grep -E '^(/opt/homebrew/|/usr/local/opt/|@rpath|@executable_path|@loader_path)' || true)"
  if [[ -n "$linked" ]]; then
    echo "Refusing ${binary_path}: it is not a static macOS build." >&2
    printf '%s\n' "$linked" >&2
    otool -L "$binary_path" >&2
    return 1
  fi
}

ava_assert_bundle_metadata() {
  local application_bundle="$1"
  local expected_version="$2"
  local expected_architecture="$3"
  local expected_deployment_target="$4"
  local plist_path="${application_bundle}/Contents/Info.plist"
  local binary_path="${application_bundle}/Contents/MacOS/AVA"
  local short_version bundle_version minimum_macos binary_minos icon_name
  short_version="$(ava_plist_string CFBundleShortVersionString "$plist_path")"
  bundle_version="$(ava_plist_string CFBundleVersion "$plist_path")"
  minimum_macos="$(ava_plist_string LSMinimumSystemVersion "$plist_path")"
  icon_name="$(ava_plist_string CFBundleIconFile "$plist_path")"
  if [[ "$short_version" != "$expected_version" || "$bundle_version" != "$expected_version" ]]; then
    echo "Bundle version is ${short_version} / ${bundle_version}, CMakeLists says ${expected_version}." >&2
    return 1
  fi
  if [[ "$minimum_macos" != "$expected_deployment_target" ]]; then
    echo "LSMinimumSystemVersion is ${minimum_macos}, expected ${expected_deployment_target}." >&2
    return 1
  fi
  if [[ "$icon_name" != "AppIcon.icns" && "$icon_name" != "AppIcon" ]]; then
    echo "CFBundleIconFile is '${icon_name}', expected AppIcon.icns." >&2
    return 1
  fi
  if [[ ! -f "${application_bundle}/Contents/Resources/AppIcon.icns" ]]; then
    echo "AVA.app is missing Contents/Resources/AppIcon.icns." >&2
    return 1
  fi
  if [[ ! -f "${application_bundle}/Contents/Resources/${AVA_COACH_BUILD_MARKER_NAME}" ]]; then
    echo "AVA.app is missing the coach-build marker Contents/Resources/${AVA_COACH_BUILD_MARKER_NAME}." >&2
    return 1
  fi
  ava_assert_macho "$binary_path"
  ava_assert_exact_architecture "$binary_path" "$expected_architecture"
  binary_minos="$(ava_binary_minos "$binary_path")"
  if [[ "$binary_minos" != "$expected_deployment_target" ]]; then
    echo "Binary minimum macOS is ${binary_minos}, Info.plist says ${expected_deployment_target}." >&2
    return 1
  fi
}

ava_resolve_vendored_helper() {
  local tool_name="$1"
  local architecture="$2"
  local vendor_root="$3"
  if [[ -x "${vendor_root}/universal/${tool_name}" ]]; then
    printf '%s\n' "${vendor_root}/universal/${tool_name}"
    return 0
  fi
  if [[ -x "${vendor_root}/${architecture}/${tool_name}" ]]; then
    printf '%s\n' "${vendor_root}/${architecture}/${tool_name}"
    return 0
  fi
  return 1
}

ava_write_coach_marker() {
  local application_bundle="$1"
  local version="$2"
  local architecture="$3"
  local deployment_target="$4"
  local marker_path="${application_bundle}/Contents/Resources/${AVA_COACH_BUILD_MARKER_NAME}"
  mkdir -p "$(dirname "$marker_path")"
  cat > "$marker_path" <<EOF
version=${version}
architecture=${architecture}
minimum_macos=${deployment_target}
EOF
}

ava_write_release_info() {
  local info_path="$1"
  local version="$2"
  local architecture="$3"
  local deployment_target="$4"
  local git_sha="$5"
  cat > "$info_path" <<EOF
version=${version}
architecture=${architecture}
minimum_macos=${deployment_target}
git_sha=${git_sha}
EOF
}

ava_git_sha() {
  local repository_root="$1"
  local git_sha=""
  if command -v git >/dev/null 2>&1; then
    git_sha="$(git -C "$repository_root" rev-parse --short=12 HEAD 2>/dev/null || true)"
  fi
  if [[ -z "$git_sha" ]]; then
    git_sha="unknown"
  fi
  printf '%s\n' "$git_sha"
}

ava_distribution_basename() {
  local version="$1"
  local architecture="$2"
  printf 'AVA-%s-%s\n' "$version" "$architecture"
}

ava_write_application_zip() {
  local application_bundle="$1"
  local zip_path="$2"
  rm -f "$zip_path"
  ditto -c -k --keepParent "$application_bundle" "$zip_path"
}

ava_detach_mounted_image() {
  local mount_point="$1"
  local attempt
  for attempt in 1 2 3 4 5; do
    if hdiutil detach "$mount_point" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  hdiutil detach -force "$mount_point" >/dev/null 2>&1
}

ava_dmg_cleanup() {
  trap - RETURN
  if [[ "${AVA_DMG_ATTACHED:-0}" -eq 1 && -n "${AVA_DMG_MOUNT:-}" ]]; then
    ava_detach_mounted_image "$AVA_DMG_MOUNT" || true
    AVA_DMG_ATTACHED=0
  fi
  if [[ -n "${AVA_DMG_STAGING:-}" && -d "$AVA_DMG_STAGING" ]]; then
    rm -rf "$AVA_DMG_STAGING"
    AVA_DMG_STAGING=""
  fi
}

ava_layout_dmg_window() {
  local volume_name="$1"
  if [[ "${CI:-}" == "true" || "${AVA_DMG_LAYOUT:-1}" == "0" ]]; then
    return 0
  fi
  osascript <<EOF || echo "Finder window layout was skipped. The disk still contains AVA and an Applications alias." >&2
tell application "Finder"
  tell disk "${volume_name}"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {120, 120, 720, 440}
    set viewOptions to the icon view options of container window
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 96
    set position of item "AVA.app" of container window to {160, 180}
    set position of item "Applications" of container window to {440, 180}
    update without registering applications
    delay 2
    close
  end tell
end tell
EOF
}

ava_create_dmg() {
  local application_bundle="$1"
  local disk_image_path="$2"
  local volume_name="$3"
  local app_kilobytes image_megabytes readwrite_image
  AVA_DMG_STAGING="$(mktemp -d "${TMPDIR:-/tmp}/ava-dmg.XXXXXX")"
  AVA_DMG_MOUNT="${AVA_DMG_STAGING}/mount"
  AVA_DMG_ATTACHED=0
  mkdir -p "$AVA_DMG_MOUNT"
  trap ava_dmg_cleanup RETURN

  app_kilobytes="$(du -sk "$application_bundle" | awk '{ print $1 }')"
  image_megabytes=$((app_kilobytes / 1024 + app_kilobytes / 10240 + 64))
  if [[ "$image_megabytes" -lt 128 ]]; then
    image_megabytes=128
  fi
  readwrite_image="${AVA_DMG_STAGING}/rw.dmg"
  hdiutil create -ov -fs HFS+ -volname "$volume_name" -size "${image_megabytes}m" "$readwrite_image"
  if [[ ! -f "$readwrite_image" && -f "${readwrite_image}.dmg" ]]; then
    mv "${readwrite_image}.dmg" "$readwrite_image"
  fi
  hdiutil attach "$readwrite_image" -mountpoint "$AVA_DMG_MOUNT" -nobrowse -noverify -noautoopen
  AVA_DMG_ATTACHED=1
  ditto "$application_bundle" "${AVA_DMG_MOUNT}/AVA.app"
  ln -s /Applications "${AVA_DMG_MOUNT}/Applications"
  if [[ ! -L "${AVA_DMG_MOUNT}/Applications" ]]; then
    echo "Applications alias was not created on the disk image." >&2
    return 1
  fi
  if [[ "$(readlink "${AVA_DMG_MOUNT}/Applications")" != "/Applications" ]]; then
    echo "Applications alias does not point at /Applications." >&2
    return 1
  fi
  # Finder icon layout writes extra attributes onto AVA.app and breaks a
  # signature that was already applied. Unsigned images can still be arranged.
  if codesign --verify "${AVA_DMG_MOUNT}/AVA.app" >/dev/null 2>&1; then
    echo "Skipping Finder layout so the signed app is left unchanged."
  else
    ava_layout_dmg_window "$volume_name"
  fi
  sync || true
  ava_detach_mounted_image "$AVA_DMG_MOUNT"
  AVA_DMG_ATTACHED=0
  rm -f "$disk_image_path"
  hdiutil convert "$readwrite_image" -format UDZO -imagekey zlib-level=9 -ov -o "$disk_image_path"
  if [[ ! -f "$disk_image_path" && -f "${disk_image_path}.dmg" ]]; then
    mv "${disk_image_path}.dmg" "$disk_image_path"
  fi
  if [[ ! -f "$disk_image_path" ]]; then
    echo "hdiutil did not write ${disk_image_path}." >&2
    return 1
  fi
}

ava_verify_dmg_layout() {
  local disk_image_path="$1"
  local plist_path minimum_macos icon_name short_version
  AVA_DMG_STAGING="$(mktemp -d "${TMPDIR:-/tmp}/ava-dmg-check.XXXXXX")"
  AVA_DMG_MOUNT="${AVA_DMG_STAGING}/mount"
  AVA_DMG_ATTACHED=0
  mkdir -p "$AVA_DMG_MOUNT"
  trap ava_dmg_cleanup RETURN
  hdiutil attach "$disk_image_path" -mountpoint "$AVA_DMG_MOUNT" -nobrowse -readonly -noverify -noautoopen
  AVA_DMG_ATTACHED=1
  if [[ ! -d "${AVA_DMG_MOUNT}/AVA.app" ]]; then
    echo "Disk image is missing AVA.app." >&2
    return 1
  fi
  if [[ ! -L "${AVA_DMG_MOUNT}/Applications" ]]; then
    echo "Disk image is missing the Applications alias." >&2
    return 1
  fi
  if [[ "$(readlink "${AVA_DMG_MOUNT}/Applications")" != "/Applications" ]]; then
    echo "Applications alias does not point at /Applications." >&2
    return 1
  fi
  if [[ ! -x "${AVA_DMG_MOUNT}/AVA.app/Contents/Helpers/ffmpeg" || ! -x "${AVA_DMG_MOUNT}/AVA.app/Contents/Helpers/ffprobe" ]]; then
    echo "Disk image is missing ffmpeg or ffprobe in Contents/Helpers." >&2
    return 1
  fi
  if [[ ! -f "${AVA_DMG_MOUNT}/AVA.app/Contents/Resources/AppIcon.icns" ]]; then
    echo "Disk image is missing AppIcon.icns." >&2
    return 1
  fi
  if [[ ! -f "${AVA_DMG_MOUNT}/AVA.app/Contents/Resources/${AVA_COACH_BUILD_MARKER_NAME}" ]]; then
    echo "Disk image is missing the coach-build marker." >&2
    return 1
  fi
  plist_path="${AVA_DMG_MOUNT}/AVA.app/Contents/Info.plist"
  minimum_macos="$(ava_plist_string LSMinimumSystemVersion "$plist_path")"
  icon_name="$(ava_plist_string CFBundleIconFile "$plist_path")"
  short_version="$(ava_plist_string CFBundleShortVersionString "$plist_path")"
  if [[ -z "$minimum_macos" || -z "$short_version" ]]; then
    echo "Info.plist in the disk image is missing its version or minimum macOS." >&2
    return 1
  fi
  if [[ "$icon_name" != "AppIcon.icns" && "$icon_name" != "AppIcon" ]]; then
    echo "Info.plist icon is '${icon_name}', expected AppIcon.icns." >&2
    return 1
  fi
  if [[ "${AVA_DMG_EXPECT_SIGNATURE:-}" == "1" ]]; then
    codesign --verify --deep --strict "${AVA_DMG_MOUNT}/AVA.app"
    codesign --verify --strict "${AVA_DMG_MOUNT}/AVA.app/Contents/Helpers/ffmpeg"
    codesign --verify --strict "${AVA_DMG_MOUNT}/AVA.app/Contents/Helpers/ffprobe"
    xcrun stapler validate "${AVA_DMG_MOUNT}/AVA.app"
  fi
}

ava_codesign_use_der() {
  local help_text
  if [[ -z "$AVA_CODESIGN_DER" ]]; then
    help_text="$(codesign --help 2>&1 || true)"
    if [[ "$help_text" == *"--generate-entitlement-der"* ]]; then
      AVA_CODESIGN_DER="yes"
    else
      AVA_CODESIGN_DER="no"
    fi
  fi
  [[ "$AVA_CODESIGN_DER" == "yes" ]]
}

ava_codesign_item() {
  local target="$1"
  local identity="$2"
  local entitlements_path="${3:-}"
  local -a arguments
  arguments=(--force --sign "$identity" --options runtime --timestamp)
  if [[ -n "$entitlements_path" ]]; then
    arguments+=(--entitlements "$entitlements_path")
    if ava_codesign_use_der; then
      arguments+=(--generate-entitlement-der)
    fi
  fi
  codesign "${arguments[@]}" "$target"
}

ava_is_nested_bundle_directory() {
  local path="$1"
  local base
  base="$(basename "$path")"
  case "$base" in
    *.framework|*.app|*.bundle|*.xpc|*.plugin|*.kext|*.appex) return 0 ;;
    *) return 1 ;;
  esac
}

ava_should_probe_macho() {
  local path="$1"
  local base
  base="$(basename "$path")"
  case "$base" in
    *.png|*.icns|*.plist|*.strings|*.qss|*.txt|*.json|*.nib|*.mom|*.momd|*.car|*.metallib|*.pdf|*.ttf|*.otf|*.html|*.qm|*.pak|*.dat|*.prl|*.css|*.js|*.svg|*.tiff|*.tif)
      return 1
      ;;
  esac
  return 0
}

ava_is_macho_file() {
  local path="$1"
  local description
  [[ -f "$path" && ! -L "$path" ]] || return 1
  ava_should_probe_macho "$path" || return 1
  description="$(file -b "$path")"
  [[ "$description" == *"Mach-O"* ]]
}

ava_sign_application() {
  local application_bundle="$1"
  local identity="$2"
  local application_entitlements="$3"
  local helper_entitlements="$4"
  local nested_path entitlements_for_item
  if [[ ! -d "$application_bundle" ]]; then
    echo "Missing application bundle: ${application_bundle}" >&2
    return 1
  fi
  if [[ ! -f "$application_entitlements" || ! -f "$helper_entitlements" ]]; then
    echo "Missing entitlements plist next to the macOS sources." >&2
    return 1
  fi
  xattr -cr "$application_bundle" 2>/dev/null || true
  while IFS= read -r -d '' nested_path; do
    if [[ "$nested_path" == "$application_bundle" || -L "$nested_path" ]]; then
      continue
    fi
    if [[ -d "$nested_path" ]]; then
      if ava_is_nested_bundle_directory "$nested_path"; then
        echo "Signing bundle ${nested_path}"
        ava_codesign_item "$nested_path" "$identity" ""
      fi
      continue
    fi
    if ava_is_macho_file "$nested_path"; then
      entitlements_for_item=""
      case "$nested_path" in
        "${application_bundle}/Contents/Helpers/"*)
          entitlements_for_item="$helper_entitlements"
          ;;
      esac
      echo "Signing ${nested_path}"
      ava_codesign_item "$nested_path" "$identity" "$entitlements_for_item"
    fi
  done < <(find "$application_bundle" -depth -print0)
  echo "Signing ${application_bundle}"
  ava_codesign_item "$application_bundle" "$identity" "$application_entitlements"
}

ava_assert_hardened_runtime() {
  local target="$1"
  local details
  details="$(codesign -dv "$target" 2>&1 || true)"
  if [[ "$details" != *"runtime"* ]]; then
    echo "Hardened runtime is missing on ${target}." >&2
    printf '%s\n' "$details" >&2
    return 1
  fi
}

ava_assert_entitlement() {
  local target="$1"
  local entitlement_key="$2"
  local dumped
  dumped="$(codesign -d --entitlements :- "$target" 2>/dev/null || codesign -d --entitlements - "$target" 2>&1 || true)"
  if [[ "$dumped" != *"${entitlement_key}"* ]]; then
    echo "Missing entitlement ${entitlement_key} on ${target}." >&2
    printf '%s\n' "$dumped" >&2
    return 1
  fi
}

ava_verify_signed_application() {
  local application_bundle="$1"
  local ffmpeg_path="${application_bundle}/Contents/Helpers/ffmpeg"
  local ffprobe_path="${application_bundle}/Contents/Helpers/ffprobe"
  codesign --verify --deep --strict --verbose=2 "$application_bundle"
  codesign --verify --strict --verbose=2 "$ffmpeg_path"
  codesign --verify --strict --verbose=2 "$ffprobe_path"
  ava_assert_hardened_runtime "$application_bundle"
  ava_assert_hardened_runtime "$ffmpeg_path"
  ava_assert_hardened_runtime "$ffprobe_path"
  ava_assert_entitlement "$application_bundle" "com.apple.security.cs.disable-library-validation"
  ava_assert_entitlement "$application_bundle" "com.apple.security.cs.allow-unsigned-executable-memory"
  ava_assert_entitlement "$application_bundle" "com.apple.security.cs.allow-jit"
  ava_assert_entitlement "$ffmpeg_path" "com.apple.security.cs.disable-library-validation"
  ava_assert_entitlement "$ffmpeg_path" "com.apple.security.cs.allow-unsigned-executable-memory"
  ava_assert_entitlement "$ffmpeg_path" "com.apple.security.cs.allow-jit"
  ava_assert_entitlement "$ffprobe_path" "com.apple.security.cs.disable-library-validation"
  ava_assert_entitlement "$ffprobe_path" "com.apple.security.cs.allow-unsigned-executable-memory"
  ava_assert_entitlement "$ffprobe_path" "com.apple.security.cs.allow-jit"
}

ava_sign_disk_image() {
  local disk_image_path="$1"
  local identity="$2"
  if [[ ! -f "$disk_image_path" ]]; then
    echo "Missing disk image: ${disk_image_path}" >&2
    return 1
  fi
  # Quarantine and Finder info on the image file itself make codesign refuse it.
  xattr -cr "$disk_image_path" 2>/dev/null || true
  codesign --force --sign "$identity" --timestamp "$disk_image_path"
  codesign --verify --verbose=2 "$disk_image_path"
}

ava_notarytool() {
  # Bash 3.2 with set -u rejects an empty array, so the keychain flag is a branch.
  if [[ -n "${AVA_KEYCHAIN_PATH:-}" ]]; then
    xcrun notarytool "$@" --keychain "$AVA_KEYCHAIN_PATH"
  else
    xcrun notarytool "$@"
  fi
}

ava_detect_codesign_identity() {
  local listed identity_count
  if [[ -n "${AVA_CODESIGN_IDENTITY:-}" ]]; then
    printf '%s\n' "$AVA_CODESIGN_IDENTITY"
    return 0
  fi
  listed="$(security find-identity -p codesigning -v | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' || true)"
  identity_count="$(printf '%s\n' "$listed" | sed '/^$/d' | wc -l | tr -d '[:space:]')"
  if [[ "$identity_count" == "1" ]]; then
    printf '%s\n' "$listed" | sed '/^$/d'
    return 0
  fi
  echo "Set AVA_CODESIGN_IDENTITY to a Developer ID Application certificate." >&2
  echo "Xcode → Settings → Accounts → Manage Certificates → + → Developer ID Application." >&2
  echo "Apple Development and Mac App Distribution certificates cannot notarize a DMG for coaches." >&2
  echo "Identities in this keychain:" >&2
  security find-identity -p codesigning -v >&2 || true
  return 1
}

ava_prepare_notary_credentials() {
  local profile_name="$1"
  if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" ]]; then
    ava_notarytool store-credentials "$profile_name" \
      --apple-id "$APPLE_ID" \
      --team-id "$APPLE_TEAM_ID" \
      --password "$APPLE_APP_SPECIFIC_PASSWORD"
    return 0
  fi
  if ava_notarytool history --keychain-profile "$profile_name" >/dev/null 2>&1; then
    return 0
  fi
  echo "notarytool profile \"${profile_name}\" is not in the keychain." >&2
  echo "Create an app-specific password at https://account.apple.com and run:" >&2
  echo "  xcrun notarytool store-credentials \"${profile_name}\" \\" >&2
  echo "    --apple-id \"YOUR_APPLE_ID\" \\" >&2
  echo "    --team-id \"YOUR_TEAM_ID\" \\" >&2
  echo "    --password \"app-specific-password\"" >&2
  return 1
}

ava_submit_for_notarization() {
  local zip_path="$1"
  local profile_name="$2"
  if ! ava_notarytool submit "$zip_path" --keychain-profile "$profile_name" --wait; then
    echo "Apple rejected the submission. The submission id is in the output above." >&2
    echo "  xcrun notarytool log <submission-id> --keychain-profile \"${profile_name}\"" >&2
    if [[ -n "${AVA_KEYCHAIN_PATH:-}" ]]; then
      echo "  That keychain is ${AVA_KEYCHAIN_PATH}. Pass --keychain with that path." >&2
    fi
    return 1
  fi
}

ava_spctl_assess() {
  local assess_type="$1"
  local target="$2"
  local attempt
  for attempt in 1 2 3 4 5; do
    if spctl --assess --verbose=4 --type "$assess_type" "$target"; then
      return 0
    fi
    echo "Gatekeeper has not accepted ${target} yet (attempt ${attempt})." >&2
    sleep 2
  done
  echo "Gatekeeper did not accept ${target}." >&2
  return 1
}
