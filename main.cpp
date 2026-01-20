#include "ui/MainWindow.h"
#include <QApplication>
#include "style/theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    Style::ApplyLightTheme();

    MainWindow w;
    w.show();

    return app.exec();

}