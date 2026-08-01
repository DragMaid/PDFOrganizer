#pragma once
#include <QObject>
#include <QStringList>

class TagModel;
class PdfModel;
class DatabaseManager;

/**
 * @brief Mediates all tag operations between TagModel, PdfModel, and the DB.
 *
 * Every tag mutation goes through this controller so that the database,
 * the global TagModel, and the per-file tags inside PdfModel all stay
 * in sync.
 *
 * The backend is the authority on tags — this controller keeps a local mirror
 * so the UI can draw without waiting on the network. Methods named
 * `applyRemote*` write the server's answer into that mirror and are the only
 * way tag state should change once signed in.
 */
class TagController : public QObject
{
    Q_OBJECT

public:
    explicit TagController(TagModel*        tagModel,
                           PdfModel*        pdfModel,
                           DatabaseManager* db,
                           QObject*         parent = nullptr);

    void initialize();   ///< Load tags from DB → TagModel

    // ── Tag CRUD ──────────────────────────────────────────────────────────────
    bool createTag(const QString& name);
    bool deleteTag(const QString& name);
    bool renameTag(const QString& oldName, const QString& newName);

    // ── Tag assignment ────────────────────────────────────────────────────────

    /// Replace the full tag list for the given file.
    bool setFileTags(const QString& filePath, const QStringList& tags);

    /// Add a single tag to a file (idempotent).
    bool addTagToFile(const QString& filePath, const QString& tag);

    /// Remove a single tag from a file.
    bool removeTagFromFile(const QString& filePath, const QString& tag);

    // ── Mirroring the backend ─────────────────────────────────────────────────

    /// Replace the local vocabulary with the tags the server reports across
    /// every group the user belongs to.
    ///
    /// Returns the tags that disappeared. A caller needs them because a tag
    /// going away is not a quiet event: anything filtering on it is now
    /// filtering on nothing, and every file that carried it is drawn wrong
    /// until it is redrawn.
    QStringList applyRemoteVocabulary(const QStringList& names);

    /// Record the tag list the server confirmed for a file. Used instead of
    /// setFileTags() once signed in, so the local copy always reflects what
    /// actually landed — including tags a teammate added concurrently.
    void applyRemoteFileTags(const QString& filePath, const QStringList& tags);

signals:
    void tagCreated(const QString& name);
    void tagDeleted(const QString& name);
    void tagRenamed(const QString& oldName, const QString& newName);
    void fileTagsChanged(const QString& filePath, const QStringList& tags);

    /// One or more tags left the vocabulary — deleted here, or by a teammate and
    /// heard about over the WebSocket. Emitted from applyRemoteVocabulary() so
    /// both routes arrive at the same place.
    void vocabularyShrank(const QStringList& removed);

private:
    TagModel*        m_tagModel;
    PdfModel*        m_pdfModel;
    DatabaseManager* m_db;
};
