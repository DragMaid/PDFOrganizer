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

signals:
    void tagCreated(const QString& name);
    void tagDeleted(const QString& name);
    void tagRenamed(const QString& oldName, const QString& newName);
    void fileTagsChanged(const QString& filePath, const QStringList& tags);

private:
    TagModel*        m_tagModel;
    PdfModel*        m_pdfModel;
    DatabaseManager* m_db;
};
