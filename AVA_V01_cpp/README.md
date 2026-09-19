# asiatica-va
field-hockey specific clipping and video-analysis tool

## App icon (macOS Dock / Finder)

Pre-made icons live in `AppIcon.iconset/`. To build `AppIcon.icns` for the app bundle (Dock and Finder), run from the project root:

```bash
./scripts/make_icns.sh
```

If `iconutil` reports "Invalid Iconset", run that command from Terminal (outside Cursor). Alternatively, create `AppIcon.icns` in Xcode (File → New → App Icons) or with another tool and place it in the project root. CMake will use it when building the `.app` bundle.

## Distributing a coach build

See [DISTRIBUTION.md](../DISTRIBUTION.md) at the repo root: 14-day trial, license keys, bundled FFmpeg helpers, `macdeployqt` packaging, and Apple notarization (must run on your Mac).

Clip export uses `ffmpeg` / `ffprobe` from `AVA.app/Contents/Helpers/` in a packaged build. Local cmake runs still use Homebrew FFmpeg if present (`/opt/homebrew/bin`). Coaches do not install Homebrew. Operator: `./scripts/vendor_ffmpeg_macos.sh` then `./scripts/package_macos.sh` (see `third_party/ffmpeg/README.md`).
