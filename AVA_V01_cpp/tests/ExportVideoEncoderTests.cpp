#include "ExportVideoEncoder.h"

#include <QStringList>
#include <QTest>

namespace {

QString encoderListing(const QString& extraEncoderLines) {
    return QStringLiteral(
               "Encoders:\n"
               " V..... = Video\n"
               " A..... = Audio\n"
               " ------\n"
               " V....D libx264              libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (codec h264)\n"
               " V....D libx264rgb           libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 RGB (codec h264)\n"
               " V....D hevc_videotoolbox    VideoToolbox H.265 Encoder (codec hevc)\n"
               " V....D h264_nvenc           NVIDIA NVENC H.264 encoder (codec h264)\n"
               " V....D libx264              note h264_videotoolbox is a different encoder (codec h264)\n")
        + extraEncoderLines;
}

QStringList libx264CodecArguments() {
    return {
        QStringLiteral("-c:v"),
        QStringLiteral("libx264"),
        QStringLiteral("-preset"),
        QStringLiteral("fast"),
        QStringLiteral("-crf"),
        QStringLiteral("23"),
    };
}

QStringList videoToolboxCodecArguments() {
    return {
        QStringLiteral("-c:v"),
        QStringLiteral("h264_videotoolbox"),
        QStringLiteral("-q:v"),
        QStringLiteral("65"),
    };
}

QStringList compatibleContainerArguments() {
    return {
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-movflags"),
        QStringLiteral("+faststart"),
    };
}

}  // namespace

class ExportVideoEncoderTest : public QObject {
    Q_OBJECT

private slots:
    void softwareArgumentsMatchCurrentLibx264Export();
    void hardwareArgumentsUseVideoToolboxQualityAndDecode();
    void fallsBackToSoftwareWhenEncoderIsMissingOrEncodeFailed();
    void listingDetectsH264VideoToolboxOnly();
};

void ExportVideoEncoderTest::softwareArgumentsMatchCurrentLibx264Export() {
    const ExportVideoEncoder::Kind kind = ExportVideoEncoder::kindForClip(
        /*h264VideoToolboxAvailable=*/false, /*hardwareEncodeFailedEarlier=*/false);
    QCOMPARE(kind, ExportVideoEncoder::Kind::Libx264);
    QCOMPARE(ExportVideoEncoder::sourceDecodeArguments(kind), QStringList());
    QCOMPARE(ExportVideoEncoder::codecArguments(kind), libx264CodecArguments());
    QCOMPARE(ExportVideoEncoder::containerCompatibilityArguments(), compatibleContainerArguments());
    QVERIFY(!ExportVideoEncoder::shouldRetryWithLibx264(kind));
}

void ExportVideoEncoderTest::hardwareArgumentsUseVideoToolboxQualityAndDecode() {
    const ExportVideoEncoder::Kind kind = ExportVideoEncoder::kindForClip(
        /*h264VideoToolboxAvailable=*/true, /*hardwareEncodeFailedEarlier=*/false);
    QCOMPARE(kind, ExportVideoEncoder::Kind::H264VideoToolbox);
    QCOMPARE(ExportVideoEncoder::sourceDecodeArguments(kind),
             QStringList({QStringLiteral("-hwaccel"), QStringLiteral("videotoolbox")}));
    QCOMPARE(ExportVideoEncoder::codecArguments(kind), videoToolboxCodecArguments());
    QCOMPARE(ExportVideoEncoder::containerCompatibilityArguments(), compatibleContainerArguments());
    QVERIFY(ExportVideoEncoder::shouldRetryWithLibx264(kind));
    QVERIFY(!ExportVideoEncoder::codecArguments(kind).contains(QStringLiteral("libx264")));
    QVERIFY(!ExportVideoEncoder::codecArguments(kind).contains(QStringLiteral("-crf")));
}

void ExportVideoEncoderTest::fallsBackToSoftwareWhenEncoderIsMissingOrEncodeFailed() {
    QCOMPARE(ExportVideoEncoder::kindForClip(false, false), ExportVideoEncoder::Kind::Libx264);
    QCOMPARE(ExportVideoEncoder::kindForClip(false, true), ExportVideoEncoder::Kind::Libx264);
    QCOMPARE(ExportVideoEncoder::kindForClip(true, true), ExportVideoEncoder::Kind::Libx264);
    QCOMPARE(ExportVideoEncoder::codecArguments(ExportVideoEncoder::kindForClip(true, true)),
             libx264CodecArguments());
    QCOMPARE(ExportVideoEncoder::sourceDecodeArguments(ExportVideoEncoder::kindForClip(true, true)),
             QStringList());
    QVERIFY(!ExportVideoEncoder::shouldRetryWithLibx264(ExportVideoEncoder::Kind::Libx264));
    QVERIFY(ExportVideoEncoder::shouldRetryWithLibx264(
        ExportVideoEncoder::Kind::H264VideoToolbox));
}

void ExportVideoEncoderTest::listingDetectsH264VideoToolboxOnly() {
    QVERIFY(!ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(QString()));
    QVERIFY(!ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(encoderListing(QString())));
    QVERIFY(!ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(encoderListing(
        QStringLiteral(" V....D h264_videotoolbox_extra    not the Apple encoder\n"))));

    const QString listed = encoderListing(
        QStringLiteral(" V....D h264_videotoolbox    VideoToolbox H.264 Encoder (codec h264)\n"));
    QVERIFY(ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(listed));
    QString windowsListing = listed;
    windowsListing.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QVERIFY(ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(windowsListing));
    QVERIFY(ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(
        QStringLiteral(" V..X.D h264_videotoolbox    VideoToolbox H.264 Encoder (codec h264)\n")));
}

int runExportVideoEncoderTests(int argc, char** argv) {
    ExportVideoEncoderTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ExportVideoEncoderTests.moc"
