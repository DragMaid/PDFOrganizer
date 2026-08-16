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
 *   file_identity(path, uuid, group_id, remote_file_id, registered_hash)
 *                                            – local path → the client-owned
 *                                              identity a file is tracked by,
 *                                              and where the backend last
 *                                              said it landed. See below.
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
    bool            setPendingTags(int fileId, bool pending);
    QList<int>      getFilesWithPendingTags() const;

    // ── Pending Notes ─────────────────────────────────────────────────────────
    bool            savePendingNote(int fileId, const QString& body);
    QStringList     getPendingNotes(int fileId) const;
    bool            clearPendingNotes(int fileId);
    QList<int>      getFilesWithPendingNotes() const;

    // ── Settings key-value ────────────────────────────────────────────────────
    QVariant        getSetting(const QString& key, const QVariant& defaultValue = {}) const;
    bool            setSetting(const QString& key, const QVariant& value);

    // ── Content-hash cache ────────────────────────────────────────────────────
    // A PDF's bytes are hashed to tell the backend what to store it as, and
    // hashing is the expensive part of a sync — so a digest is kept until the
    // file's size or modification time changes. This is *not* how a file's
    // identity is tracked any more (see below): a hash changes the moment a
    // PDF is annotated, so it makes a poor anchor for "is this the same file".

    /// Cached digest for @p path, or an empty string if absent or stale.
    QString        cachedHash(const QString& path, qint64 fileSize,
                              const QDateTime& modified) const;
    bool           storeHash (const QString& path, const QString& contentHash,
                              qint64 fileSize, const QDateTime& modified);

    // ── File identity ─────────────────────────────────────────────────────────
    // Each locally tracked PDF gets a UUID the first time its path is seen, and
    // keeps it for as long as the path is tracked — regardless of how many
    // times the file's bytes change. A rescan comparing this table's paths
    // against what is on disk is what tells a "new" file (no row yet) from a
    // "missing" one (a row whose path is no longer there) without touching the
    // file's content at all.

    /// uuid for @p path, or empty if this path has never been seen before.
    QString        fileUuid(const QString& path) const;
    /// uuid for @p path, assigning and persisting a fresh one the first time
    /// this path is seen.
    QString        ensureFileUuid(const QString& path);
    /// Assign @p uuid to @p path outright, overwriting whatever uuid the path
    /// had. Used when a file lands on this machine by download: adopting the
    /// backend listing's own uuid, rather than minting a fresh local one, is
    /// what lets this machine recognise its own copy again later.
    bool           adoptFileUuid(const QString& path, const QString& uuid);
    /// The path @p uuid was assigned to, or empty if unknown.
    QString        pathForUuid(const QString& uuid) const;
    /// Drop everything known about @p path's identity, so a later, unrelated
    /// file at the same path starts clean. Called when a file is deleted for
    /// good, not merely missing from a scan.
    bool           forgetFileIdentity(const QString& path);

    /// Backend listing id @p uuid is registered as within @p groupId, or -1
    /// when this path has never been registered (or belonged to a different
    /// group last time it was).
    int            remoteFileId(int groupId, const QString& uuid) const;
    /// Record that @p uuid now maps to @p remoteFileId in @p groupId, current
    /// as of @p contentHash — so a later call whose hash has not moved can
    /// skip the round trip, and one whose hash has is recognised as needing to
    /// repoint the registration rather than being served the stale id.
    bool           storeRemoteFileId(int groupId, const QString& uuid,
                                     int remoteFileId, const QString& contentHash);
    /// The content hash @p uuid was registered under last time, or empty.
    QString        registeredHash(const QString& uuid) const;
    /// The local path currently registered as @p remoteFileId within
    /// @p groupId, or empty.
    QString        pathForRemoteFile(int groupId, int remoteFileId) const;
    /// Forget the registration (but not the uuid itself) — used when a file is
    /// removed from its group so a later scan registers it fresh.
    bool           forgetRemoteFile(int groupId, const QString& uuid);
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
