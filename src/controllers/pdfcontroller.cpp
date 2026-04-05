#include "pdfcontroller.h"
#include "models/pdfmodel.h"
#include "database/databasemanager.h"
#include "controllers/folderwatcher.h"
#include "utils/pdfopener.h"
#include "utils/thumbnailgenerator.h"
#include <QDebug>

PdfController::PdfController(PdfModel*        pdfModel,
                             DatabaseManager*  db,
                             FolderWatcher*    watcher,
                             QObject*          parent)
    : QObject(parent)
    , m_model(pdfModel)
    , m_db(db)
    , m_watcher(watcher)
    , m_thumbGen(new ThumbnailGenerator(this))
{
    connect(m_watcher, &FolderWatcher::fileDiscovered,
            this, &PdfController::onFileDiscovered);
    connect(m_watcher, &FolderWatcher::fileRemoved,
            this, &PdfController::onFileRemoved);
    connect(m_thumbGen, &ThumbnailGenerator::thumbnailReady,
            this, &PdfController::onThumbnailReady);
}

void PdfController::initialize()
{
    // Load all previously-known files from the database into the model.
    const QList<PdfFile> persisted = m_db->loadAllFiles();
    m_model->resetFiles(persisted);

    // Kick off folder scans for every saved root folder.
    const QStringList roots = m_db->loadFolders();
    for (const QString& root : roots)
        m_watcher->addRootFolder(root);
}

// ── Actions ───────────────────────────────────────────────────────────────────

void PdfController::openPdf(const QString& filePath)
{
    const bool launched = PdfOpener::open(filePath);
    if (!launched) {
        emit errorOccurred(
            tr("Could not open '%1'.\nMake sure a PDF viewer is installed.")
                .arg(filePath));
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();

    // Update database
    m_db->updateLastOpened(filePath, now);

    // Update model in-memory so the UI reflects the change immediately
    PdfFile f = m_model->fileByPath(filePath);
    if (f.isValid()) {
        f.lastOpened = now;
        m_model->updateFile(f);
    }

    emit pdfOpened(filePath, now);
}

void PdfController::requestThumbnail(const QString& filePath)
{
    if (m_thumbnailRequested.contains(filePath)) return;
    m_thumbnailRequested.insert(filePath);
    m_thumbGen->requestThumbnail(filePath);
}

void PdfController::requestVisibleThumbnails()
{
    const QList<PdfFile> files = m_model->allFiles();
    for (const PdfFile& f : files)
        requestThumbnail(f.filePath);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PdfController::onFileDiscovered(const PdfFile& incoming)
{
    // Merge with any existing DB record so we preserve lastOpened / tags.
    PdfFile existing = m_model->fileByPath(incoming.filePath);

    if (existing.isValid()) {
        // Already known – update disk-sourced fields if the file changed.
        if (existing.lastModified != incoming.lastModified ||
            existing.fileSizeBytes != incoming.fileSizeBytes)
        {
            existing.lastModified  = incoming.lastModified;
            existing.fileSizeBytes = incoming.fileSizeBytes;
            m_db->saveFile(existing);
            m_model->updateFile(existing);
        }
    } else {
        // Brand new file: persist and add to model.
        PdfFile f = incoming;
        f.tags    = m_db->getFileTags(f.id);   // id still -1 → empty list
        m_db->saveFile(f);                       // sets f.id
        m_model->addFile(f);
    }
}

void PdfController::onFileRemoved(const QString& filePath)
{
    m_db->deleteFile(filePath);
    m_model->removeFile(filePath);
    m_thumbnailRequested.remove(filePath);
}

void PdfController::onThumbnailReady(const QString& filePath, const QPixmap& pix)
{
    m_model->setThumbnail(filePath, pix);
}
