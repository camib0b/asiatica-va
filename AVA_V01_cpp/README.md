# asiatica-va
field-hockey specific clipping and video-analysis tool

## App icon (macOS Dock / Finder)

Pre-made icons live in `AppIcon.iconset/`. `AppIcon.icns` is committed, and an Apple build regenerates it from that iconset with `iconutil`. To refresh the committed file:

```bash
./scripts/make_icns.sh
```

`package_macos.sh` runs that before the Release build. CMake always puts the icon in the app bundle.

## Distributing a coach build

See [DISTRIBUTION.md](../DISTRIBUTION.md). From this directory:

```bash
./scripts/release_macos.sh
```

A coach build uses `ffmpeg` and `ffprobe` from `AVA.app/Contents/Helpers/`. If either helper is missing it shows a dialog and does not search Homebrew. A local cmake build that was not packaged still uses Homebrew FFmpeg when it is installed (`/opt/homebrew/bin`). Coaches do not install Homebrew. Pins and the download script are in `third_party/ffmpeg/README.md`.
