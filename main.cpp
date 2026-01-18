#include <QApplication>
#include <QWidget>
#include <QMediaPlayer>
#include <QDebug>
#include "MainWindow.h"


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.setWindowTitle("Developing AVA");
    window.show();
    return app.exec();
}