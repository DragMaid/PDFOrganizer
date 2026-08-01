#include <QApplication>
#include <QIcon>
#include <QSettings>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName   (QStringLiteral("PDFOrganizer"));
    app.setOrganizationName  (QStringLiteral("PDFOrganizer"));
    app.setOrganizationDomain(QStringLiteral("pdforganizer.app"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // QSettings will automatically use the correct platform storage
    // (registry on Windows, plist on macOS, ~/.config on Linux)
    QSettings::setDefaultFormat(QSettings::IniFormat);
    MainWindow window;
    window.show();

    return app.exec();
}
