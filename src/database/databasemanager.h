#pragma once
#include <QObject>
#include <QSqlDatabase>
#include "models/pdffile.h"

/**
 * @brief Thin wrapper around a SQLite database for persisting application state.
 *
 * Schema
 * ──────
 *   folders   (id, path)
 *   tags      (id, name)
 *   pdf_files (id, path, folder_path, file_name, file_size, last_modified,
 *              last_opened, page_count)
 *   pdf_tags  (pdf_id, tag_id)   – many-to-many join
 *
 * All public methods are synchronous and should be called from the main thread.
 * Heavy scanning work is done in FolderWatcher on a worker thread; only the
 * final persist step calls DatabaseManager.
 */
class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    /// Open (or create) the SQLite database at the given path.
    bool open(const QString& dbPath);
    void close();
    [[nodiscard]] bool isOpen() const;

    // ── Folders ───────────────────────────────────────────────────────────────
    QStringList     loadFolders()             const;
    bool            saveFolder  (const QString& path);
    bool            deleteFolder(const QString& path);

    // ── Tags ──────────────────────────────────────────────────────────────────
    QStringList     loadTags()                          const;
    bool            saveTag  (const QString& name);
    bool            deleteTag(const QString& name);
    bool            renameTag(const QString& oldName, const QString& newName);

    // ── PDF Files ─────────────────────────────────────────────────────────────
    QList<PdfFile>  loadAllFiles()                      const;
    bool            saveFile  (PdfFile& file);           ///< sets file.id on insert
    bool            deleteFile(const QString& filePath);
    bool            updateLastOpened(const QString& filePath, const QDateTime& dt);

    // ── Tag assignments ───────────────────────────────────────────────────────
    bool            setFileTags (int fileId, const QStringList& tags);
    QStringList     getFileTags (int fileId)            const;

    // ── Settings key-value ────────────────────────────────────────────────────
    QVariant        getSetting(const QString& key, const QVariant& defaultValue = {}) const;
    bool            setSetting(const QString& key, const QVariant& value);

private:
    bool createSchema();

    /// Return the integer id for a tag name, inserting it if necessary.
    int ensureTagId(const QString& name);
    int tagId      (const QString& name) const;
    int fileId     (const QString& filePath) const;

    QSqlDatabase m_db;
    QString      m_connectionName;
};
