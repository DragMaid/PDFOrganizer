#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include "models/pdffile.h"

/**
 * @brief Local SQLite store: everything that is specific to this machine.
 *
 * Groups, notes, shared tags and uploaded blobs live on the backend (see
 * ApiClient) — this database holds only what a single installation needs,
 * plus caches that make talking to the backend cheaper.
 *
 * Schema
 * ──────
 *   folders      (id, path)                  – watched roots on this machine
 *   tags         (id, name)                  – local mirror of the tag vocabulary
 *   pdf_files    (id, path, folder_path, file_name, file_size, last_modified,
 *                 last_opened, page_count)   – scan results
 *   pdf_tags     (pdf_id, tag_id)            – many-to-many join
 *   settings     (key, value)                – preferences and the saved session
 *   file_hashes  (path, content_hash, …)     – so a file is hashed once, not
 *                                              on every sync
 *   remote_files (group_id, content_hash, remote_file_id)
 *                                            – local path → backend file id
 *   folder_groups(folder_path, group_id)     – directory → the backend group
 *                                              holding the PDFs directly in it
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

    // ── Content-hash cache ────────────────────────────────────────────────────
    // The backend identifies a PDF by the SHA-256 of its contents. Hashing is
    // the expensive part of a sync, so a digest is kept until the file's size
    // or modification time changes.

    /// Cached digest for @p path, or an empty string if absent or stale.
    QString        cachedHash(const QString& path, qint64 fileSize,
                              const QDateTime& modified) const;
    bool           storeHash (const QString& path, const QString& contentHash,
                              qint64 fileSize, const QDateTime& modified);

    // ── Remote id cache ───────────────────────────────────────────────────────
    /// Backend file id for a hash within a group, or -1 if not registered yet.
    int            remoteFileId    (int groupId, const QString& contentHash) const;
    bool           storeRemoteFileId(int groupId, const QString& contentHash,
                                     int remoteFileId);
    bool           forgetRemoteFile (int groupId, const QString& contentHash);
    /// Drop every cached backend id — used when signing out or changing server.
    /// This includes the folder → group *ids*, because a different server (or
    /// account) numbers its groups differently. The remembered group *names* are
    /// kept, and MainWindow re-attaches each directory by name on the next
    /// sign-in.
    bool           clearRemoteCache();

    // ── Folder → group mapping ────────────────────────────────────────────────
    // A directory *is* a group: the PDFs sitting directly in it are tracked in
    // that group, and there is no other way to put a file in one. A
    // subdirectory is a separate row, because it is a separate group.

    /// Backend group id for a directory, or -1 if it has none yet.
    int            folderGroupId  (const QString& folderPath) const;
    /// The group's name is stored too, and unlike the id it survives a sign-out
    /// (see clearRemoteCache) — it is how a directory is matched back to its
    /// group on the next sign-in.
    bool           storeFolderGroup(const QString& folderPath, int groupId,
                                    const QString& groupName);
    /// Name of the group this directory last belonged to, or an empty string.
    QString        folderGroupName(const QString& folderPath) const;
    bool           forgetFolderGroup(const QString& folderPath);
    /// The directory that maps to @p groupId, or an empty string.
    QString        folderForGroup (int groupId) const;
    /// Every directory that currently has a group, deepest paths last.
    QStringList    mappedFolders  () const;

private:
    bool createSchema();

    /// Return the integer id for a tag name, inserting it if necessary.
    int ensureTagId(const QString& name);
    int tagId      (const QString& name) const;
    int fileId     (const QString& filePath) const;

    QSqlDatabase m_db;
    QString      m_connectionName;
};
