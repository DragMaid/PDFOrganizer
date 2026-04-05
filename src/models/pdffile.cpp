#include "pdffile.h"

QString PdfFile::formattedSize() const
{
    if (fileSizeBytes < 1024)
        return QStringLiteral("%1 B").arg(fileSizeBytes);
    if (fileSizeBytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(fileSizeBytes / 1024.0, 0, 'f', 1);
    if (fileSizeBytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(fileSizeBytes / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(fileSizeBytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}
