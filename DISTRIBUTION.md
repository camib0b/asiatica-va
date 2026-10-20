# Distributing AVA to coaches

Operator guide for packaging a Mac build coaches can open. The welcome screen is the first thing they see, and tagging, export, and Presentation are available immediately, including offline.

## Coach first launch

1. Coach downloads `AVA.dmg` (notarized) and drags AVA to Applications.
2. Opens AVA and imports a video from the welcome screen.

## Build / sign / notarize / upload (on your Mac)

Cloud agents **cannot** produce a notarized build. Developer ID lives on your Mac.

```bash
# 1) One-time: Apple Developer Program, certificates in Keychain, app-specific password
xcrun notarytool store-credentials "ava-notarize" \
  --apple-id "YOUR_APPLE_ID@email" \
  --team-id "YOUR_TEAM_ID" \
  --password "app-specific-password"

# 2) Download pinned static ffmpeg/ffprobe (not in git; arm64 + x86_64)
#    Pins: AVA_V01_cpp/third_party/ffmpeg/macos/SHA256PINS
./AVA_V01_cpp/scripts/vendor_ffmpeg_macos.sh

# 3) Build a .app that coaches can run without installing Qt or Homebrew
#    Copies host-arch ffmpeg+ffprobe into dist/AVA.app/Contents/Helpers/ after macdeployqt
./AVA_V01_cpp/scripts/package_macos.sh

# 4) Sign helpers individually, then the .app, then notarize
#    Nested Contents/Helpers binaries need their own codesign --options runtime
#    before the bundle; --deep is not enough under hardened runtime.
export AVA_CODESIGN_IDENTITY='Developer ID Application: Camila Escudero (TEAMID)'
./AVA_V01_cpp/scripts/sign_and_notarize.sh
```

Upload `AVA_V01_cpp/dist/AVA.dmg` (or `AVA.app.zip`) wherever you already send files: your site, Google Drive, Cloudflare R2. There is no in-app updater.

## Still manual

| You still do | Why |
| --- | --- |
| Apple Developer account, signing, notarization | Only your Mac / Keychain can hold Developer ID |
| Host the `.dmg` | No store listing |
