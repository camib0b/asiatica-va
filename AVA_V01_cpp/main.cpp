#include "ui/MainWindow.h"
#include <QApplication>
#include <QIcon>
#include "style/theme.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/ava-icon.png"));

    Style::ApplyLightTheme();

    MainWindow w;
    w.show();

    return app.exec();

}