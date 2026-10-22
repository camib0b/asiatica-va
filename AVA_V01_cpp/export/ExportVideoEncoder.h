#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

/// H.264 encoder choice for clip export.
///
/// VideoToolbox is used when `ffmpeg -encoders` lists h264_videotoolbox.
/// That listing is probed once per ffmpeg binary, in a child process, and
/// cached. The UI thread does not wait for it. If the encoder is missing, or
/// a VideoToolbox encode fails, the clip uses libx264 -preset fast -crf 23.
namespace ExportVideoEncoder {

enum class Kind {
    Libx264,
    H264VideoToolbox,
};

bool encodersListingIncludesH264VideoToolbox(const QString& encodersListing);

/// When `hardwareEncodeFailedEarlier` is true, this clip and later clips in
/// the same export stay on libx264.
Kind kindForClip(bool h264VideoToolboxAvailable, bool hardwareEncodeFailedEarlier);

bool shouldRetryWithLibx264(Kind kindUsed);

/// Input options placed before the source `-i`. Empty for libx264.
QStringList sourceDecodeArguments(Kind kind);

/// Encoder options only. Pixel format and container flags stay in
/// `containerCompatibilityArguments` so both paths write the same output.
QStringList codecArguments(Kind kind);

QStringList containerCompatibilityArguments();

/// Calls `callback` later on `context`'s thread, including when the result
/// is already cached, so export setup does not run inside the probe.
void requestH264VideoToolboxAvailability(
    const QString& ffmpegPath,
    QObject* context,
    std::function<void(bool h264VideoToolboxAvailable)> callback);

}  // namespace ExportVideoEncoder
