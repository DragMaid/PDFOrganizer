#include "pdfopener.h"
#include <QFileInfo>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QDebug>

bool PdfOpener::open(const QString& filePath)
{
    if (!QFileInfo::exists(filePath)) {
        qWarning() << "PdfOpener: file not found:" << filePath;
        return false;
    }

    // Prefer Okular on Linux; fall through to system default everywhere else.
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (isOkularAvailable())
        return openWithOkular(filePath);
#endif

    return openWithDefault(filePath);
}

bool PdfOpener::openWithOkular(const QString& filePath)
{
    // QProcess::startDetached does not block the calling thread.
    const bool ok = QProcess::startDetached(
        QStringLiteral("okular"),
        QStringList{filePath}
    );

    if (!ok)
        qWarning() << "PdfOpener: failed to launch okular for" << filePath;
    return ok;
}

bool PdfOpener::openWithDefault(const QString& filePath)
{
    const QUrl url = QUrl::fromLocalFile(filePath);
    const bool ok  = QDesktopServices::openUrl(url);
    if (!ok)
        qWarning() << "PdfOpener: QDesktopServices::openUrl failed for" << filePath;
    return ok;
}

bool PdfOpener::isOkularAvailable()
{
    // QStandardPaths::findExecutable searches the system PATH.
    return !QStandardPaths::findExecutable(QStringLiteral("okular")).isEmpty();
}
