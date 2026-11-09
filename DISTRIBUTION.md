# Distributing AVA to coaches

A coach downloads one notarized DMG, drags AVA to Applications, and opens it. Tagging, export, and Presentation are available immediately, including offline. There is no license key.

Build this on your Mac. A cloud agent cannot hold your Developer ID or talk to Apple's notary service.

## One-time setup

1. Xcode command line tools: `xcode-select --install`
2. Qt 6 and CMake. If `macdeployqt` is not already available: `brew install qt cmake`
3. A **Developer ID Application** certificate in the login keychain of this Mac.
4. A notarytool profile named `ava-notarize`.

The certificate must be **Developer ID Application**. Apple Development and Mac App Distribution are different certificates; they do not notarize a DMG that coaches download outside the App Store.

### Create the Developer ID certificate

Your Apple Developer Program membership is already active. The certificate is not.

In Xcode: Settings → Accounts → your Apple ID → Manage Certificates → + → Developer ID Application.

Or from the developer site, with the private key created on this Mac:

1. Keychain Access → Certificate Assistant → Request a Certificate From a Certificate Authority. Save the request.
2. Open [Certificates](https://developer.apple.com/account/resources/certificates/add), choose Developer ID Application, upload the request, download the certificate, and double-click it.

Confirm the login keychain:

```bash
security find-identity -p codesigning -v
```

The line you want looks like `Developer ID Application: Camila Escudero (TEAMID)`. The Team ID is those 10 characters. It is also on the Membership page.

When that list contains more than one Developer ID Application identity, set:

```bash
export AVA_CODESIGN_IDENTITY='Developer ID Application: Camila Escudero (TEAMID)'
```

### Store the notarytool password

Create an app-specific password at [account.apple.com](https://account.apple.com) under Sign-In and Security → App-Specific Passwords. Then:

```bash
xcrun notarytool store-credentials "ava-notarize" \
  --apple-id "YOUR_APPLE_ID" \
  --team-id "YOUR_TEAM_ID" \
  --password "app-specific-password"
```

The password stays in the keychain. It is not written into the repo.

## Release

From the repo:

```bash
./AVA_V01_cpp/scripts/release_macos.sh
```

The script downloads the pinned static ffmpeg and ffprobe, builds a Release Apple Silicon app for macOS 12 or later, copies the helpers into `Contents/Helpers`, deploys Qt, and writes a DMG with an Applications alias. It signs nested code from the inside out with the hardened-runtime entitlements, notarizes and staples the app, then signs, notarizes, and staples the DMG.

Upload this file:

```text
AVA_V01_cpp/dist/AVA-0.1.0-arm64.dmg
```

The version in the name comes from `AVA_APP_VERSION` at the top of `AVA_V01_cpp/CMakeLists.txt`. An unsigned DMG is not a release. The script stops when notarization fails, and it says so before the signing step as well.

## Check the result

The script runs these checks. Run them again if you want to see the result yourself:

```bash
spctl --assess --type execute --verbose=4 AVA_V01_cpp/dist/AVA.app
xcrun stapler validate AVA_V01_cpp/dist/AVA.app
xcrun stapler validate AVA_V01_cpp/dist/AVA-0.1.0-arm64.dmg
```

Gatekeeper should say `accepted` and `source=Notarized Developer ID`. `stapler validate` says the ticket is attached, so the coach's Mac can open AVA without asking Apple first.

## What the coach does

1. Download the DMG.
2. Open it and drag AVA onto Applications.
3. Open AVA from Applications and import a video.

The build is Apple Silicon only and requires macOS 12 or later. Coaches do not install Qt, Homebrew, or FFmpeg. If the bundled helpers are missing, AVA says so. It does not quietly use a Homebrew FFmpeg that happens to be on your Mac.

Host the DMG wherever you already send files. There is no in-app updater.

## When Apple rejects the upload

The script prints a submission id. The reason is in the log:

```bash
xcrun notarytool log <submission-id> --keychain-profile "ava-notarize"
```

## Optional GitHub Actions build

`.github/workflows/macos-release.yml` builds an unsigned DMG on a Mac runner when you run the workflow by hand or push a `v*` tag. Pull requests only run shellcheck.

It signs and notarizes when every one of these repository secrets is set:

| Secret | What it is |
| --- | --- |
| `AVA_APPLE_CERTIFICATE_P12_BASE64` | base64 of the Developer ID Application `.p12` exported from Keychain Access |
| `AVA_APPLE_CERTIFICATE_PASSWORD` | password for that `.p12` |
| `AVA_APPLE_ID` | Apple ID email |
| `AVA_APPLE_TEAM_ID` | 10-character Team ID |
| `AVA_APPLE_APP_SPECIFIC_PASSWORD` | app-specific password |

`AVA_CODESIGN_IDENTITY` is optional. Leave it empty when the imported certificate contains one Developer ID Application identity.

With none of those secrets, the workflow uploads an unsigned artifact. Do not send that to coaches. With only some of them set, the job fails instead of uploading a half-signed build.

The DMG from `release_macos.sh` on your Mac is enough for the first coaches. The GitHub secrets are only for a later automated build.

## Version and minimum system

`AVA_APP_VERSION`, `AVA_MACOS_DEPLOYMENT_TARGET` (`12.0`), and `AVA_MACOS_ARCHITECTURE` (`arm64`) are the three `set(...)` lines at the top of `AVA_V01_cpp/CMakeLists.txt`. The DMG name, `CFBundleShortVersionString`, and `LSMinimumSystemVersion` follow those lines.
