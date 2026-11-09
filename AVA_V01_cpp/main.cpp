#include "ui/MainWindow.h"
#include "i18n/AppLocale.h"
#include "state/EventDefaults.h"
#include "FfmpegLocator.h"
#include "XaiConfig.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <exception>
#include <new>
#include "style/theme.h"
#include "style/Motion.h"

namespace {

QString exceptionDetail(const std::exception& exception) {
    const char* what = exception.what();
    if (what == nullptr || what[0] == '\0') {
        return QStringLiteral("unknown error");
    }
    return QString::fromLocal8Bit(what);
}

void warnIfCoachBuildIsMissingFfmpeg() {
    const QStringList missingTools = FfmpegLocator::missingPackagedTools();
    if (missingTools.isEmpty()) {
        return;
    }
    const QString message =
        AppLocale::trUi("startup.packaged_ffmpeg_missing")
            .arg(missingTools.join(QStringLiteral(", ")));
    qCritical("%s", qPrintable(message));
    QMessageBox::critical(nullptr, AppLocale::trUi("app.title"), message);
}

int reportFatalStartupError(const QString& message, bool showDialog) {
    qCritical("%s", qPrintable(message));
    if (showDialog) {
        QMessageBox::critical(nullptr, QStringLiteral("AVA"), message);
    }
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        QApplication app(argc, argv);
        app.setOrganizationName(QStringLiteral("AsiaticaVA"));
        app.setApplicationName(QStringLiteral("AVA"));
        app.setWindowIcon(QIcon(":/ava-icon.png"));

        try {
            Style::ApplyLightTheme();
            XaiConfig::bootstrap();
            AppLocale::loadFromSettings();
            Motion::loadFromSettings();
            EventDefaults::loadFromSettings();
            warnIfCoachBuildIsMissingFfmpeg();

            MainWindow w;
            w.show();

            return app.exec();
        } catch (const std::bad_alloc&) {
            return reportFatalStartupError(
                QStringLiteral("AVA failed to start: insufficient memory."), true);
        } catch (const std::exception& exception) {
            return reportFatalStartupError(
                QStringLiteral("AVA failed to start: %1").arg(exceptionDetail(exception)), true);
        }
    } catch (const std::bad_alloc&) {
        return reportFatalStartupError(
            QStringLiteral("AVA failed to start: insufficient memory."), false);
    } catch (const std::exception& exception) {
        return reportFatalStartupError(
            QStringLiteral("AVA failed to start: %1").arg(exceptionDetail(exception)), false);
    }
}