#pragma once
#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QSet>
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
        PdfFileRole,        ///< full PdfFile as QVariant
        /// bool — a removal for this file is in flight. The row stays where it
        /// is and stays clickable; it is only drawn as on its way out, because
        /// the request can still fail and the user should not be made to wait
        /// for it either way.
        PendingRemovalRole,
        /// SyncState — whether the group this file's folder belongs to holds a
        /// copy of it. This is the *file's* answer, not the folder's: a folder
        /// badge says how much work a sync would do, and this says which
        /// individual PDFs that work is about.
        SyncStateRole,
        /// QString — the sentence behind SyncStateRole, shown on hover.
        SyncDetailRole,
        /// bool — tags or notes written here that the server has not taken yet.
        /// Deliberately separate from SyncStateRole: metadata never blocks and
        /// never asks to be synced, it just rides along with the next one.
        PendingMetaRole
    };

    /// How far this machine's copy of a PDF and the group's copy agree.
    enum SyncState {
        SyncUnknown = 0,    ///< Signed out, or the folder has no group — say nothing.
        SyncLocalOnly,      ///< Held here; the group has no copy stored.
        SyncTransferring,   ///< Being uploaded or downloaded right now.
        SyncSynced          ///< The group stores this exact content.
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

    /// Mark a file as being removed from its group, or clear the mark when the
    /// request finishes. Purely presentational — the row is removed for real
    /// only if the removal succeeded and the local copy went with it.
    void setPendingRemoval(const QString& filePath, bool pending);
    [[nodiscard]] bool isPendingRemoval(const QString& filePath) const;

    /// Record whether the group holds this file, and the sentence explaining it.
    /// MainWindow owns the answer — it is the only thing that can match what is
    /// on disk against what the server reports — and this model only draws it.
    void setSyncState(const QString& filePath, SyncState state,
                      const QString& detail = {});
    [[nodiscard]] SyncState syncState(const QString& filePath) const;

    /// Tag or note edits sitting on this machine, waiting for the file itself to
    /// reach the group.
    void setPendingMeta(const QString& filePath, bool pending);

    /// Forget every file's sync state. Signed out there is no group for any of
    /// them to be in sync with, so a stale green dot would be a lie.
    void clearSyncStates();

    // ── Query ─────────────────────────────────────────────────────────────────
    [[nodiscard]] PdfFile  fileAt(int row)                        const;
    [[nodiscard]] PdfFile  fileByPath(const QString& path)        const;
    [[nodiscard]] int      rowForPath(const QString& path)        const; ///< -1 if not found
    [[nodiscard]] QList<PdfFile> allFiles()                       const { return m_files; }
    [[nodiscard]] int      totalCount()                           const { return m_files.size(); }

signals:
    void fileUpdated(const QString& filePath);

private:
    /// Keyed by path rather than stored on PdfFile, so a rescan replacing the
    /// whole file list does not throw the answers away with it.
    struct SyncMark {
        SyncState state = SyncUnknown;
        QString   detail;
    };

    QList<PdfFile>            m_files;
    QSet<QString>             m_pendingRemoval;
    QHash<QString, SyncMark>  m_syncMarks;
    QSet<QString>             m_pendingMeta;
};
