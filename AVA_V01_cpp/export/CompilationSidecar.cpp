#include "CompilationSidecar.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace CompilationSidecar {

namespace {

struct JsonFrame {
    bool isArray = false;
    bool hasValue = false;
};

QString quoted(const QString& text);

class JsonBuffer {
public:
    void beginObject() {
        if (parentIsArray()) {
            prepareArrayValue();
        }
        body_ += QLatin1String("{\n");
        scopes_.append(JsonFrame{false, false});
    }

    void beginArray() {
        if (parentIsArray()) {
            prepareArrayValue();
        }
        body_ += QLatin1String("[\n");
        scopes_.append(JsonFrame{true, false});
    }

    void key(const QString& name) {
        JsonFrame& frame = scopes_.last();
        if (frame.hasValue) {
            body_ += QLatin1String(",\n");
        }
        body_ += indentation(scopes_.size());
        body_ += quoted(name);
        body_ += QLatin1String(": ");
    }

    void literal(const QString& text) {
        if (parentIsArray()) {
            prepareArrayValue();
        }
        body_ += text;
        if (!scopes_.isEmpty()) {
            scopes_.last().hasValue = true;
        }
    }

    void endObject() { endScope(QLatin1Char('}'), QLatin1String("{\n")); }

    void endArray() { endScope(QLatin1Char(']'), QLatin1String("[\n")); }

    QByteArray toUtf8() const {
        QString text = body_;
        if (!text.endsWith(QLatin1Char('\n'))) {
            text += QLatin1Char('\n');
        }
        return text.toUtf8();
    }

private:
    bool parentIsArray() const {
        return !scopes_.isEmpty() && scopes_.last().isArray;
    }

    void prepareArrayValue() {
        JsonFrame& frame = scopes_.last();
        if (frame.hasValue) {
            body_ += QLatin1String(",\n");
        }
        body_ += indentation(scopes_.size());
    }

    void endScope(QChar closing, const QLatin1String& emptyOpening) {
        const bool empty = scopes_.isEmpty() || !scopes_.last().hasValue;
        if (!scopes_.isEmpty()) {
            scopes_.removeLast();
        }
        if (empty && body_.endsWith(emptyOpening)) {
            body_.chop(1);
            body_ += closing;
        } else {
            body_ += QLatin1Char('\n');
            body_ += indentation(scopes_.size());
            body_ += closing;
        }
        if (!scopes_.isEmpty()) {
            scopes_.last().hasValue = true;
        }
    }

    static QString indentation(int depth) {
        return QString(depth * 2, QLatin1Char(' '));
    }

    QString body_;
    QVector<JsonFrame> scopes_;
};

QString quoted(const QString& text) {
    QString escaped;
    escaped.reserve(text.size() + 2);
    escaped += QLatin1Char('"');
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        switch (character.unicode()) {
        case '"':
            escaped += QLatin1String("\\\"");
            break;
        case '\\':
            escaped += QLatin1String("\\\\");
            break;
        case '\b':
            escaped += QLatin1String("\\b");
            break;
        case '\f':
            escaped += QLatin1String("\\f");
            break;
        case '\n':
            escaped += QLatin1String("\\n");
            break;
        case '\r':
            escaped += QLatin1String("\\r");
            break;
        case '\t':
            escaped += QLatin1String("\\t");
            break;
        default:
            if (character.unicode() < 0x20) {
                escaped += QStringLiteral("\\u%1")
                               .arg(static_cast<int>(character.unicode()), 4, 16, QLatin1Char('0'));
            } else {
                escaped += character;
            }
            break;
        }
    }
    escaped += QLatin1Char('"');
    return escaped;
}

QString sharedField(const QVector<ClipCompilationRecord>& records,
                    QString ClipCompilationRecord::*field) {
    if (records.isEmpty()) {
        return {};
    }
    const QString& first = records.first().*field;
    if (first.isEmpty()) {
        return {};
    }
    for (const ClipCompilationRecord& record : records) {
        if (record.*field != first) {
            return {};
        }
    }
    return first;
}

void writeStringOrNull(JsonBuffer& json, const QString& key, const QString& value) {
    json.key(key);
    if (value.isEmpty()) {
        json.literal(QStringLiteral("null"));
    } else {
        json.literal(quoted(value));
    }
}

struct FfprobeResult {
    bool succeeded = false;
    QByteArray standardOutput;
};

FfprobeResult runFfprobe(const QString& ffprobePath,
                         const QStringList& arguments,
                         int timeoutMilliseconds) {
    FfprobeResult result;
    if (ffprobePath.isEmpty() || arguments.isEmpty()) {
        return result;
    }

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.start(ffprobePath, arguments);
    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(1000);
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return result;
    }
    result.succeeded = true;
    result.standardOutput = process.readAllStandardOutput();
    return result;
}

std::optional<double> jsonDouble(const QJsonValue& value) {
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number)) {
            return std::nullopt;
        }
        return number;
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    bool parsed = false;
    const double number = value.toString().toDouble(&parsed);
    if (!parsed || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::optional<double> positiveJsonDouble(const QJsonValue& value) {
    const std::optional<double> number = jsonDouble(value);
    if (!number.has_value() || !(*number > 0.0)) {
        return std::nullopt;
    }
    return number;
}

std::optional<double> frameRateFromFraction(const QString& fraction) {
    const QString trimmed = fraction.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String("0/0") || trimmed == QLatin1String("N/A")) {
        return std::nullopt;
    }
    const int slash = trimmed.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        bool parsed = false;
        const double number = trimmed.toDouble(&parsed);
        if (!parsed || !(number > 0.0) || !std::isfinite(number)) {
            return std::nullopt;
        }
        return number;
    }
    bool numeratorParsed = false;
    bool denominatorParsed = false;
    const double numerator = trimmed.left(slash).toDouble(&numeratorParsed);
    const double denominator = trimmed.mid(slash + 1).toDouble(&denominatorParsed);
    if (!numeratorParsed || !denominatorParsed || !(denominator > 0.0) || !(numerator > 0.0)) {
        return std::nullopt;
    }
    const double framesPerSecond = numerator / denominator;
    if (!std::isfinite(framesPerSecond) || !(framesPerSecond > 0.0)) {
        return std::nullopt;
    }
    return framesPerSecond;
}

std::optional<double> probeFormatDurationSeconds(const QString& ffprobePath,
                                                 const QString& mediaPath) {
    const FfprobeResult probe = runFfprobe(
        ffprobePath,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
         QStringLiteral("-of"), QStringLiteral("json"),
         mediaPath},
        15000);
    if (!probe.succeeded) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(probe.standardOutput);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonValue duration = document.object()
                                    .value(QStringLiteral("format"))
                                    .toObject()
                                    .value(QStringLiteral("duration"));
    return positiveJsonDouble(duration);
}

std::optional<double> probeLastPacketEndSeconds(const QString& ffprobePath,
                                                const QString& mediaPath) {
    const FfprobeResult probe = runFfprobe(
        ffprobePath,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"), QStringLiteral("packet=pts_time,duration_time"),
         QStringLiteral("-of"), QStringLiteral("json"),
         mediaPath},
        30000);
    if (!probe.succeeded) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(probe.standardOutput);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonArray packets = document.object().value(QStringLiteral("packets")).toArray();
    if (packets.isEmpty() || !packets.last().isObject()) {
        return std::nullopt;
    }
    const QJsonObject packet = packets.last().toObject();
    const std::optional<double> presentationTimestamp =
        jsonDouble(packet.value(QStringLiteral("pts_time")));
    const std::optional<double> packetDuration =
        jsonDouble(packet.value(QStringLiteral("duration_time")));
    if (!presentationTimestamp.has_value() || !packetDuration.has_value()) {
        return std::nullopt;
    }
    const double endSeconds = *presentationTimestamp + *packetDuration;
    if (!std::isfinite(endSeconds) || !(endSeconds > 0.0)) {
        return std::nullopt;
    }
    return endSeconds;
}

QString escapeFfmetadata(const QString& text) {
    QString escaped;
    escaped.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        switch (character.unicode()) {
        case '\\':
        case '=':
        case ';':
        case '#':
        case '\n':
        case '\r':
            escaped += QLatin1Char('\\');
            if (character == QLatin1Char('\n')) {
                escaped += QLatin1Char('n');
            } else if (character == QLatin1Char('\r')) {
                escaped += QLatin1Char('r');
            } else {
                escaped += character;
            }
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

}  // namespace

qint64 roundSecondsToMilliseconds(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return 0;
    }
    const double milliseconds = seconds * 1000.0;
    if (milliseconds >= static_cast<double>(std::numeric_limits<qint64>::max())) {
        return std::numeric_limits<qint64>::max();
    }
    return std::llround(milliseconds);
}

QString formatSeconds(qint64 milliseconds) {
    const bool negative = milliseconds < 0;
    const qint64 absolute = negative ? -milliseconds : milliseconds;
    const qint64 wholeSeconds = absolute / 1000;
    const int fractionMilliseconds = static_cast<int>(absolute % 1000);
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(wholeSeconds)
        .arg(fractionMilliseconds, 3, 10, QLatin1Char('0'));
}

OffsetAccumulation accumulateOffsets(const QVector<qint64>& durationMilliseconds) {
    OffsetAccumulation accumulation;
    accumulation.offsetMilliseconds.reserve(durationMilliseconds.size());
    qint64 cursor = 0;
    for (const qint64 duration : durationMilliseconds) {
        accumulation.offsetMilliseconds.append(cursor);
        if (duration > 0 && cursor > std::numeric_limits<qint64>::max() - duration) {
            cursor = std::numeric_limits<qint64>::max();
        } else {
            cursor += duration;
        }
    }
    accumulation.summedEndMilliseconds = cursor;
    return accumulation;
}

bool finalDurationWithinTolerance(qint64 finalDurationMilliseconds,
                                  qint64 summedEndMilliseconds,
                                  double framesPerSecond,
                                  int toleranceFrames) {
    if (!(framesPerSecond > 0.0) || !std::isfinite(framesPerSecond) || toleranceFrames < 0) {
        return false;
    }
    if (finalDurationMilliseconds < 0 || summedEndMilliseconds < 0) {
        return false;
    }
    const double toleranceMilliseconds =
        (1000.0 * static_cast<double>(toleranceFrames)) / framesPerSecond;
    const qint64 difference = std::llabs(finalDurationMilliseconds - summedEndMilliseconds);
    return static_cast<double>(difference) <= toleranceMilliseconds;
}

SidecarDocument buildDocument(const SidecarBuildInput& input) {
    SidecarDocument document;
    document.avaVersion = input.avaVersion;
    document.videoFileName = input.videoFileName;
    document.sourceVideoFileName = input.sourceVideoFileName;
    document.matchXmlFileName = input.matchXmlFileName;

    if (input.measuredDurationSeconds.size() != input.clipsInConcatOrder.size()) {
        return document;
    }

    QVector<qint64> durationMilliseconds;
    durationMilliseconds.reserve(input.measuredDurationSeconds.size());
    for (const double measuredSeconds : input.measuredDurationSeconds) {
        durationMilliseconds.append(roundSecondsToMilliseconds(measuredSeconds));
    }
    const OffsetAccumulation offsets = accumulateOffsets(durationMilliseconds);

    document.team = sharedField(input.clipsInConcatOrder, &ClipCompilationRecord::teamAbbrev);
    document.code = sharedField(input.clipsInConcatOrder, &ClipCompilationRecord::code);
    document.overlayLabel = sharedField(input.clipsInConcatOrder, &ClipCompilationRecord::overlayLabel);

    document.clips.reserve(input.clipsInConcatOrder.size());
    for (int index = 0; index < input.clipsInConcatOrder.size(); ++index) {
        const ClipCompilationRecord& record = input.clipsInConcatOrder.at(index);
        SidecarClip clip;
        clip.ordinal = index + 1;
        clip.offsetMilliseconds = offsets.offsetMilliseconds.at(index);
        clip.durationMilliseconds = durationMilliseconds.at(index);
        clip.sourceStartMilliseconds = record.sourceStartMs;
        clip.sourceEndMilliseconds = record.sourceEndMs;
        clip.xmlInstanceId = record.xmlInstanceId;
        clip.periodLabel = record.periodLabel;
        clip.labels = record.labels;
        document.clips.append(clip);
    }

    if (input.framesPerSecond.has_value() && std::isfinite(*input.framesPerSecond) &&
        *input.framesPerSecond > 0.0) {
        document.framesPerSecondKnown = true;
        document.framesPerSecond = *input.framesPerSecond;
    }
    if (input.finalDurationSeconds.has_value()) {
        document.durationKnown = true;
        document.durationMilliseconds = roundSecondsToMilliseconds(*input.finalDurationSeconds);
    }
    if (document.durationKnown && document.framesPerSecondKnown) {
        document.verified = finalDurationWithinTolerance(document.durationMilliseconds,
                                                          offsets.summedEndMilliseconds,
                                                          document.framesPerSecond,
                                                          kDurationToleranceFrames);
    }
    return document;
}

QByteArray renderJson(const SidecarDocument& document) {
    JsonBuffer json;
    json.beginObject();
    json.key(QStringLiteral("schemaVersion"));
    json.literal(QString::number(kClipsSchemaVersion));
    writeStringOrNull(json, QStringLiteral("avaVersion"), document.avaVersion);
    writeStringOrNull(json, QStringLiteral("video"), document.videoFileName);
    writeStringOrNull(json, QStringLiteral("sourceVideo"), document.sourceVideoFileName);
    writeStringOrNull(json, QStringLiteral("matchXml"), document.matchXmlFileName);

    json.key(QStringLiteral("fps"));
    if (document.framesPerSecondKnown) {
        json.literal(QString::number(document.framesPerSecond, 'f', 3));
    } else {
        json.literal(QStringLiteral("null"));
    }

    json.key(QStringLiteral("durationS"));
    if (document.durationKnown) {
        json.literal(formatSeconds(document.durationMilliseconds));
    } else {
        json.literal(QStringLiteral("null"));
    }

    json.key(QStringLiteral("verified"));
    json.literal(document.verified ? QStringLiteral("true") : QStringLiteral("false"));
    writeStringOrNull(json, QStringLiteral("team"), document.team);
    writeStringOrNull(json, QStringLiteral("code"), document.code);
    writeStringOrNull(json, QStringLiteral("overlayLabel"), document.overlayLabel);

    json.key(QStringLiteral("clips"));
    json.beginArray();
    for (const SidecarClip& clip : document.clips) {
        json.beginObject();
        json.key(QStringLiteral("ord"));
        json.literal(QString::number(clip.ordinal));
        json.key(QStringLiteral("offsetS"));
        json.literal(formatSeconds(clip.offsetMilliseconds));
        json.key(QStringLiteral("durationS"));
        json.literal(formatSeconds(clip.durationMilliseconds));
        json.key(QStringLiteral("sourceStartS"));
        json.literal(formatSeconds(clip.sourceStartMilliseconds));
        json.key(QStringLiteral("sourceEndS"));
        json.literal(formatSeconds(clip.sourceEndMilliseconds));
        json.key(QStringLiteral("xmlInstanceId"));
        if (clip.xmlInstanceId > 0) {
            json.literal(QString::number(clip.xmlInstanceId));
        } else {
            json.literal(QStringLiteral("null"));
        }
        json.key(QStringLiteral("labels"));
        json.beginObject();
        for (const ClipSidecarLabel& label : clip.labels) {
            json.key(label.group);
            json.literal(quoted(label.text));
        }
        json.endObject();
        json.endObject();
    }
    json.endArray();
    json.endObject();
    return json.toUtf8();
}

QString chapterTitle(int ordinal, int totalCount, const QString& periodLabel) {
    const QString counter = QStringLiteral("%1/%2").arg(ordinal).arg(totalCount);
    const QString period = periodLabel.trimmed();
    if (period.isEmpty()) {
        return counter;
    }
    return QStringLiteral("%1 · %2").arg(counter, period);
}

QString renderChapterMetadata(const QVector<qint64>& offsetMilliseconds,
                              const QVector<qint64>& durationMilliseconds,
                              const QStringList& titles) {
    const int count = qMin(offsetMilliseconds.size(),
                           qMin(durationMilliseconds.size(), titles.size()));
    QString metadata = QStringLiteral(";FFMETADATA1\n");
    for (int index = 0; index < count; ++index) {
        const qint64 startMilliseconds = offsetMilliseconds.at(index);
        qint64 endMilliseconds = startMilliseconds + durationMilliseconds.at(index);
        if (endMilliseconds <= startMilliseconds) {
            endMilliseconds = startMilliseconds + 1;
        }
        metadata += QStringLiteral("\n[CHAPTER]\n");
        metadata += QStringLiteral("TIMEBASE=1/1000\n");
        metadata += QStringLiteral("START=%1\n").arg(startMilliseconds);
        metadata += QStringLiteral("END=%1\n").arg(endMilliseconds);
        metadata += QStringLiteral("title=%1\n").arg(escapeFfmetadata(titles.at(index)));
    }
    return metadata;
}

QString clipsJsonPathForVideo(const QString& videoPath) {
    const QFileInfo videoInfo(videoPath);
    const QString jsonFileName = videoInfo.completeBaseName() + QStringLiteral(".clips.json");
    return QDir(videoInfo.absolutePath()).filePath(jsonFileName);
}

bool writeUtf8FileAtomically(const QString& filePath,
                             const QByteArray& utf8Bytes,
                             QString* errorMessage) {
    if (filePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No output file path provided.");
        }
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open file for writing: ") + file.errorString();
        }
        return false;
    }

    const qint64 bytesWritten = file.write(utf8Bytes);
    const bool deviceWriteFailed =
        bytesWritten != utf8Bytes.size() || !file.flush() || file.error() != QFileDevice::NoError;
    if (deviceWriteFailed) {
        const QString deviceError = file.errorString();
        file.cancelWriting();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write file: ") + deviceError;
        }
        return false;
    }

    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to commit file: ") + file.errorString();
        }
        return false;
    }
    return true;
}

std::optional<double> probeMediaDurationSeconds(const QString& ffprobePath,
                                                const QString& mediaPath) {
    if (ffprobePath.isEmpty() || mediaPath.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<double> formatDuration = probeFormatDurationSeconds(ffprobePath, mediaPath);
    if (formatDuration.has_value()) {
        return formatDuration;
    }
    return probeLastPacketEndSeconds(ffprobePath, mediaPath);
}

std::optional<double> probeVideoFramesPerSecond(const QString& ffprobePath,
                                                const QString& mediaPath) {
    if (ffprobePath.isEmpty() || mediaPath.isEmpty()) {
        return std::nullopt;
    }
    const FfprobeResult probe = runFfprobe(
        ffprobePath,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"), QStringLiteral("stream=avg_frame_rate,r_frame_rate"),
         QStringLiteral("-of"), QStringLiteral("json"),
         mediaPath},
        15000);
    if (!probe.succeeded) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(probe.standardOutput);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonArray streams = document.object().value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty() || !streams.at(0).isObject()) {
        return std::nullopt;
    }
    const QJsonObject stream = streams.at(0).toObject();
    const std::optional<double> average = frameRateFromFraction(
        stream.value(QStringLiteral("avg_frame_rate")).toString());
    if (average.has_value()) {
        return average;
    }
    return frameRateFromFraction(stream.value(QStringLiteral("r_frame_rate")).toString());
}

}  // namespace CompilationSidecar
