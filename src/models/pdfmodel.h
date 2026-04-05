#pragma once
#include <QAbstractTableModel>
#include <QList>
#include "pdffile.h"

/**
 * @brief Qt item model that owns the canonical list of PdfFile objects.
 *
 * Roles above Qt::UserRole expose individual PdfFile fields so that both
 * list delegates and grid delegates can fetch exactly what they need without
 * casting to PdfFile manually.
 *
 * The model does NOT talk to the database directly; that responsibility
 * belongs to PdfController which populates and mutates the model.
 */
class PdfModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // ── Custom roles ──────────────────────────────────────────────────────────
    enum Roles {
        FilePathRole    = Qt::UserRole + 1,
        FileNameRole,
        FolderPathRole,
        TagsRole,           ///< QStringList
        LastOpenedRole,     ///< QDateTime
        LastModifiedRole,   ///< QDateTime
        FileSizeRole,       ///< qint64 (raw bytes)
        PageCountRole,
        ThumbnailRole,      ///< QPixmap (may be null)
        DatabaseIdRole,
        PdfFileRole         ///< full PdfFile as QVariant
    };

    // ── Columns for table / list view ─────────────────────────────────────────
    enum Column {
        ColFileName = 0,
        ColFolder,
        ColTags,
        ColLastOpened,
        ColSize,
        ColCount            ///< sentinel – keep last
    };

    explicit PdfModel(QObject* parent = nullptr);

    // ── QAbstractTableModel interface ─────────────────────────────────────────
    int      rowCount   (const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data       (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── Mutation ──────────────────────────────────────────────────────────────

    /// Replace the entire dataset (e.g. after a folder rescan)
    void resetFiles(const QList<PdfFile>& files);

    /// Add a single file (returns false if already present by path)
    bool addFile(const PdfFile& file);

    /// Remove a file by absolute path
    bool removeFile(const QString& filePath);

    /// Update metadata fields for an existing file (identified by filePath)
    bool updateFile(const PdfFile& updated);

    /// Update only the thumbnail for the given row (called from background thread via queued signal)
    void setThumbnail(const QString& filePath, const QPixmap& thumb);

    // ── Query ─────────────────────────────────────────────────────────────────
    [[nodiscard]] PdfFile  fileAt(int row)                        const;
    [[nodiscard]] PdfFile  fileByPath(const QString& path)        const;
    [[nodiscard]] int      rowForPath(const QString& path)        const; ///< -1 if not found
    [[nodiscard]] QList<PdfFile> allFiles()                       const { return m_files; }
    [[nodiscard]] int      totalCount()                           const { return m_files.size(); }

signals:
    void fileUpdated(const QString& filePath);

private:
    QList<PdfFile> m_files;
};
