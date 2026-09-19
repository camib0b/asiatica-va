# Bundled FFmpeg for macOS AVA.app

Coach builds ship `ffmpeg` and `ffprobe` in `AVA.app/Contents/Helpers/`. They are **not** committed to git (each binary is tens of megabytes).

## Download (operator Mac)

From the repo:

```bash
./AVA_V01_cpp/scripts/vendor_ffmpeg_macos.sh
```

That reads the pinned URLs and SHA256s in `macos/SHA256PINS`, downloads both architectures (`arm64` and `x86_64`), and extracts:

- `macos/arm64/ffmpeg`, `macos/arm64/ffprobe`
- `macos/x86_64/ffmpeg`, `macos/x86_64/ffprobe`

`package_macos.sh` copies the **host** architecture (or `macos/universal/` if you create a `lipo` pair) into the `.app` **after** `macdeployqt`.

## Pin / version

| | |
| --- | --- |
| Version | FFmpeg **9.0.1** |
| Upstream | [Martin Riedl static builds](https://ffmpeg.martin-riedl.de/) (evermeet.cx-style static snapshot; Intel + Apple Silicon) |
| Recipes | https://gitlab.com/martinr92/ffmpeg |
| Pin file | `macos/SHA256PINS` (SHA256 of each zip, not the extracted binary) |

To bump the pin: replace the four zip URLs and hashes in `SHA256PINS`, re-run `vendor_ffmpeg_macos.sh`, then `package_macos.sh`.

## License

This pin enables **GPL** and **version 3**, and links **x264** (needed for AVA clip export / playback transcode with `libx264`):

`--enable-gpl --enable-version3 --enable-libx264` (plus other codecs in the Martin Riedl build).

- FFmpeg: https://ffmpeg.org/legal.html
- x264: GPL
- Combined build: treat as **GPL-3.0-or-later** for the helper binaries

AVA talks to these tools through `QProcess` (separate executables). Shipping them still requires offering the corresponding FFmpeg source; the URLs above are that offer.

Do **not** copy a live Homebrew cellar into the app unless you rewrite install names to `@executable_path` and document it. These zips only link macOS system frameworks (`Foundation`, `VideoToolbox`, `libSystem`, …), not `/opt/homebrew`.
