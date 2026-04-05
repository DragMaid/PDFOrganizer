#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPixmap>

/**
 * @brief Represents a single PDF file with all its associated metadata.
 *
 * PdfFile is a plain value type that acts as the central domain object
 * throughout the application. It is cheap to copy (thumbnail is shared
 * via implicit sharing) and fully serialisable to/from SQLite.
 */
struct PdfFile
{
    // ── Identity ──────────────────────────────────────────────────────────────
    int         id          = -1;       ///< Database primary key (-1 = not persisted)
    QString     filePath;               ///< Absolute path on disk
    QString     fileName;               ///< Cached QFileInfo::fileName()
    QString     folderPath;             ///< Parent directory

    // ── Metadata ──────────────────────────────────────────────────────────────
    qint64      fileSizeBytes = 0;
    QDateTime   lastModified;
    QDateTime   lastOpened;             ///< Tracked by the application
    int         pageCount    = 0;       ///< 0 = unknown

    // ── Organisation ──────────────────────────────────────────────────────────
    QStringList tags;                   ///< Display strings (not IDs)

    // ── UI ────────────────────────────────────────────────────────────────────
    mutable QPixmap thumbnail;          ///< Lazy-loaded; may be null

    // ── Helpers ───────────────────────────────────────────────────────────────
    [[nodiscard]] bool isValid()    const { return !filePath.isEmpty(); }
    [[nodiscard]] bool hasTag(const QString& tag) const { return tags.contains(tag, Qt::CaseInsensitive); }

    /// Human-readable file size, e.g. "2.4 MB"
    [[nodiscard]] QString formattedSize() const;

    /// Equality is path-based so the struct can be stored in QSet / used as map key
    bool operator==(const PdfFile& other) const { return filePath == other.filePath; }
    bool operator!=(const PdfFile& other) const { return !(*this == other); }
};
