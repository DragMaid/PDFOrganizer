#include <QApplication>
#include <QSettings>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    // ── Qt application object ─────────────────────────────────────────────────
    QApplication app(argc, argv);

    app.setApplicationName   (QStringLiteral("PDFOrganizer"));
    app.setOrganizationName  (QStringLiteral("PDFOrganizer"));
    app.setOrganizationDomain(QStringLiteral("pdforganizer.app"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // QSettings will automatically use the correct platform storage
    // (registry on Windows, plist on macOS, ~/.config on Linux)
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // ── High-DPI ──────────────────────────────────────────────────────────────
    // Qt 6 enables high-DPI scaling by default; no extra calls needed.

    // ── Main window ───────────────────────────────────────────────────────────
    MainWindow window;
    window.show();

    return app.exec();
}
