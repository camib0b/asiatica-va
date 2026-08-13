#include "ui/MainWindow.h"
#include "i18n/AppLocale.h"
#include "state/EventDefaults.h"
#include "YouTubeConfig.h"
#include <QApplication>
#include <QIcon>
#include "style/theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("AsiaticaVA"));
    app.setApplicationName(QStringLiteral("AVA"));
    app.setWindowIcon(QIcon(":/ava-icon.png"));

    Style::ApplyLightTheme();
    YouTubeConfig::bootstrap();
    AppLocale::loadFromSettings();
    EventDefaults::loadFromSettings();

    MainWindow w;
    w.show();

    return app.exec();

}