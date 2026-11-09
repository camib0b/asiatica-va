#include "FfmpegLocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool writeFile(const QString& path, const QByteArray& contents, QFileDevice::Permissions permissions) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(contents) != contents.size()) {
        return false;
    }
    file.close();
    return QFile::setPermissions(path, permissions);
}

bool writeExecutableFile(const QString& path, const QByteArray& contents) {
    return writeFile(path, contents,
                     QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

bool writeCoachMarker(const QString& applicationDirectoryPath) {
    const QString markerPath = QDir::cleanPath(
        QDir(applicationDirectoryPath).filePath(QStringLiteral("../Resources/ava-coach-build")));
    return writeFile(markerPath, QByteArray("version=test\n"),
                     QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

}  // namespace

class FfmpegLocatorTest : public QObject {
    Q_OBJECT

private slots:
    void coachBuildUsesBundledHelperAndIgnoresOtherInstalls();
    void coachBuildDoesNotFallBackWhenHelperIsMissing();
    void coachBuildRejectsAHelperThatCannotBeExecuted();
    void unpackagedAppBundleStillUsesDeveloperSearchPath();
    void unpackagedBuildPrefersABundledHelperWhenOneExists();
    void markerFileMarksACoachBuild();
};

void FfmpegLocatorTest::coachBuildUsesBundledHelperAndIgnoresOtherInstalls() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory =
        directory.filePath(QStringLiteral("AVA.app/Contents/MacOS"));
    const QString helperPath = directory.filePath(QStringLiteral("AVA.app/Contents/Helpers/ffmpeg"));
    const QString otherInstall = directory.filePath(QStringLiteral("homebrew/bin/ffmpeg"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(writeCoachMarker(applicationDirectory));
    QVERIFY(writeExecutableFile(helperPath, QByteArray("bundled")));
    QVERIFY(writeExecutableFile(otherInstall, QByteArray("other")));
    QVERIFY(FfmpegLocator::isCoachDistributionBuild(applicationDirectory));
    QCOMPARE(FfmpegLocator::resolveExecutable(QStringLiteral("ffmpeg"), applicationDirectory,
                                              {otherInstall}),
             QDir::cleanPath(helperPath));
}

void FfmpegLocatorTest::coachBuildDoesNotFallBackWhenHelperIsMissing() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory =
        directory.filePath(QStringLiteral("AVA.app/Contents/MacOS"));
    const QString otherInstall = directory.filePath(QStringLiteral("homebrew/bin/ffmpeg"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(writeCoachMarker(applicationDirectory));
    QVERIFY(writeExecutableFile(otherInstall, QByteArray("other")));
    QCOMPARE(FfmpegLocator::resolveExecutable(QStringLiteral("ffmpeg"), applicationDirectory,
                                              {otherInstall}),
             QString());
}

void FfmpegLocatorTest::coachBuildRejectsAHelperThatCannotBeExecuted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory =
        directory.filePath(QStringLiteral("AVA.app/Contents/MacOS"));
    const QString helperPath = directory.filePath(QStringLiteral("AVA.app/Contents/Helpers/ffmpeg"));
    const QString otherInstall = directory.filePath(QStringLiteral("homebrew/bin/ffmpeg"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(writeCoachMarker(applicationDirectory));
    QVERIFY(writeFile(helperPath, QByteArray("not executable"),
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(writeExecutableFile(otherInstall, QByteArray("other")));
    QCOMPARE(FfmpegLocator::resolveExecutable(QStringLiteral("ffmpeg"), applicationDirectory,
                                              {otherInstall}),
             QString());
}

void FfmpegLocatorTest::unpackagedAppBundleStillUsesDeveloperSearchPath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory =
        directory.filePath(QStringLiteral("AVA.app/Contents/MacOS"));
    const QString otherInstall = directory.filePath(QStringLiteral("homebrew/bin/ffmpeg"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(writeExecutableFile(otherInstall, QByteArray("dev")));
    QVERIFY(!FfmpegLocator::isCoachDistributionBuild(applicationDirectory));
    QCOMPARE(FfmpegLocator::resolveExecutable(QStringLiteral("ffmpeg"), applicationDirectory,
                                              {otherInstall}),
             QDir::cleanPath(otherInstall));
}

void FfmpegLocatorTest::unpackagedBuildPrefersABundledHelperWhenOneExists() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory = directory.filePath(QStringLiteral("build"));
    const QString helperPath = directory.filePath(QStringLiteral("Helpers/ffmpeg"));
    const QString otherInstall = directory.filePath(QStringLiteral("homebrew/bin/ffmpeg"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(writeExecutableFile(helperPath, QByteArray("bundled")));
    QVERIFY(writeExecutableFile(otherInstall, QByteArray("other")));
    QVERIFY(!FfmpegLocator::isCoachDistributionBuild(applicationDirectory));
    QCOMPARE(FfmpegLocator::resolveExecutable(QStringLiteral("ffmpeg"), applicationDirectory,
                                              {otherInstall}),
             QDir::cleanPath(helperPath));
}

void FfmpegLocatorTest::markerFileMarksACoachBuild() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString applicationDirectory =
        directory.filePath(QStringLiteral("AVA.app/Contents/MacOS"));
    QVERIFY(QDir().mkpath(applicationDirectory));
    QVERIFY(!FfmpegLocator::isCoachDistributionBuild(applicationDirectory));
    QVERIFY(!FfmpegLocator::isCoachDistributionBuild(QStringLiteral("/tmp/Contents/MacOS")));
    QVERIFY(!FfmpegLocator::isCoachDistributionBuild(QString()));
    QVERIFY(writeCoachMarker(applicationDirectory));
    QVERIFY(FfmpegLocator::isCoachDistributionBuild(applicationDirectory));
}

int runFfmpegLocatorTests(int argc, char** argv) {
    FfmpegLocatorTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "FfmpegLocatorTests.moc"
