#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include "models/pdffile.h"

struct FileGroup
{
    int id = -1;
    QString name;
    QString githubRepoUrl;
    QString githubStatus;
    QDateTime githubValidatedAt;
    QString b2KeyId;
    QString b2BucketName;
    QString b2AccountId;
    QString b2Status;
    QDateTime b2ValidatedAt;
};

struct FileNote
{
    int id = -1;
    int fileId = -1;
    QString author;
    QString body;
    QDateTime createdAt;
};

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

    // ── Groups and notes ──────────────────────────────────────────────────────
    QList<FileGroup> loadGroups() const;
    FileGroup      groupById(int groupId) const;
    int            createGroup(const QString& name);
    bool           deleteGroup(int groupId);
    bool           setFileInGroup(int fileId, int groupId, bool tracked);
    QList<int>     fileGroupIds(int fileId) const;
    bool           saveGroupGithubValidation(int groupId, const QString& repoUrl, const QString& status);
    bool           saveGroupB2Validation(int groupId, const QString& keyId, const QString& bucketName,
                                         const QString& accountId, const QString& status);
    bool           wasFileUploaded(int groupId, int fileId, qint64 fileSize, const QDateTime& modified) const;
    bool           markFileUploaded(int groupId, int fileId, qint64 fileSize, const QDateTime& modified,
                                    const QString& b2FileId);
    QList<FileNote> loadNotes(int fileId) const;
    bool           addNote(int fileId, const QString& author, const QString& body);

private:
    bool createSchema();

    /// Return the integer id for a tag name, inserting it if necessary.
    int ensureTagId(const QString& name);
    int tagId      (const QString& name) const;
    int fileId     (const QString& filePath) const;

    QSqlDatabase m_db;
    QString      m_connectionName;
};
