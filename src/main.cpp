#include "mainwindow.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication a(argc, argv);
    QGuiApplication::setDesktopFileName("hw-test");
    MainWindow w;
#ifdef Q_OS_WIN
    w.resize(1280, 720);
    w.show();
#else
    if (a.arguments().contains("--pc")) {
        w.resize(1280, 720);
        w.show();
    } else {
        w.showFullScreen();
    }
#endif
    return a.exec();
}
