#include "tagcontroller.h"
#include "models/tagmodel.h"
#include "models/pdfmodel.h"
#include "database/databasemanager.h"

TagController::TagController(TagModel*        tagModel,
                             PdfModel*        pdfModel,
                             DatabaseManager* db,
                             QObject*         parent)
    : QObject(parent)
    , m_tagModel(tagModel)
    , m_pdfModel(pdfModel)
    , m_db(db)
{}

void TagController::initialize()
{
    m_tagModel->resetTags(m_db->loadTags());
}

// ── Tag CRUD ──────────────────────────────────────────────────────────────────

bool TagController::createTag(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_tagModel->hasTag(trimmed))
        return false;

    if (!m_db->saveTag(trimmed)) return false;
    m_tagModel->addTag(trimmed);

    emit tagCreated(trimmed);
    return true;
}

bool TagController::deleteTag(const QString& name)
{
    if (!m_db->deleteTag(name)) return false;
    m_tagModel->removeTag(name);

    // Strip the tag from every in-memory PdfFile that carries it
    const QList<PdfFile> all = m_pdfModel->allFiles();
    for (PdfFile f : all) {
        if (f.tags.removeAll(name) > 0)
            m_pdfModel->updateFile(f);
    }

    emit tagDeleted(name);
    return true;
}

bool TagController::renameTag(const QString& oldName, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || m_tagModel->hasTag(trimmed)) return false;

    if (!m_db->renameTag(oldName, trimmed)) return false;
    m_tagModel->renameTag(oldName, trimmed);

    // Update the in-memory tag strings in each PdfFile
    const QList<PdfFile> all = m_pdfModel->allFiles();
    for (PdfFile f : all) {
        const int idx = f.tags.indexOf(oldName);
        if (idx >= 0) {
            f.tags[idx] = trimmed;
            m_pdfModel->updateFile(f);
        }
    }

    emit tagRenamed(oldName, trimmed);
    return true;
}

// ── Tag assignment ────────────────────────────────────────────────────────────

bool TagController::setFileTags(const QString& filePath, const QStringList& tags)
{
    PdfFile f = m_pdfModel->fileByPath(filePath);
    if (!f.isValid() || f.id < 0) return false;

    // Ensure every new tag exists in the global tag table
    for (const QString& t : tags)
        if (!m_tagModel->hasTag(t))
            createTag(t);

    if (!m_db->setFileTags(f.id, tags)) return false;

    f.tags = tags;
    m_pdfModel->updateFile(f);

    emit fileTagsChanged(filePath, tags);
    return true;
}

bool TagController::addTagToFile(const QString& filePath, const QString& tag)
{
    PdfFile f = m_pdfModel->fileByPath(filePath);
    if (!f.isValid() || f.hasTag(tag)) return false;

    if (!m_tagModel->hasTag(tag))
        createTag(tag);

    QStringList updated = f.tags;
    updated.append(tag);
    return setFileTags(filePath, updated);
}

bool TagController::removeTagFromFile(const QString& filePath, const QString& tag)
{
    PdfFile f = m_pdfModel->fileByPath(filePath);
    if (!f.isValid() || !f.hasTag(tag)) return false;

    QStringList updated = f.tags;
    updated.removeAll(tag);
    return setFileTags(filePath, updated);
}

// ── Mirroring the backend ─────────────────────────────────────────────────────

QStringList TagController::applyRemoteVocabulary(const QStringList& names)
{
    // A tag sitting on a file whose edit has not gone up yet is not one the
    // server forgot — it is one the server has never been told about. Deleting
    // it here would cascade the assignment away, and the edit the user is still
    // waiting to send would go up as a removal they never made.
    QStringList unsent;
    for (const int fileId : m_db->getFilesWithPendingTags()) {
        for (const QString& tag : m_db->getFileTags(fileId)) {
            if (!unsent.contains(tag, Qt::CaseInsensitive))
                unsent << tag;
        }
    }

    // Rebuild the local table so tags deleted by a teammate disappear here too.
    QStringList removed;
    for (const QString& existing : m_db->loadTags()) {
        if (names.contains(existing, Qt::CaseInsensitive))
            continue;
        if (unsent.contains(existing, Qt::CaseInsensitive))
            continue;
        m_db->deleteTag(existing);
        removed << existing;
    }
    for (const QString& name : names)
        m_db->saveTag(name);

    m_tagModel->resetTags(m_db->loadTags());

    if (removed.isEmpty())
        return removed;

    // Deleting the tags row cascades pdf_tags, but the PdfFile values the views
    // draw from live in memory and would happily keep showing a tag that no
    // longer exists in the database, on the server, or in the sidebar.
    for (PdfFile f : m_pdfModel->allFiles()) {
        QStringList kept;
        for (const QString& tag : f.tags) {
            if (!removed.contains(tag, Qt::CaseInsensitive))
                kept << tag;
        }
        if (kept.size() == f.tags.size())
            continue;
        f.tags = kept;
        m_pdfModel->updateFile(f);
    }

    emit vocabularyShrank(removed);
    return removed;
}

void TagController::applyRemoteFileTags(const QString& filePath,
                                        const QStringList& tags)
{
    PdfFile f = m_pdfModel->fileByPath(filePath);
    if (!f.isValid() || f.id < 0) return;

    for (const QString& t : tags)
        if (!m_tagModel->hasTag(t))
            createTag(t);

    m_db->setFileTags(f.id, tags);

    f.tags = tags;
    m_pdfModel->updateFile(f);

    emit fileTagsChanged(filePath, tags);
}
