#pragma once
#include <QObject>
#include <QDateTime>
#include "models/pdffile.h"

class PdfModel;
class DatabaseManager;
class FolderWatcher;
class ThumbnailGenerator;

/**
 * @brief Central controller that owns the PDF lifecycle.
 *
 * Responsibilities
 * ────────────────
 *  • Bootstrap: load persisted files from DB → populate PdfModel
 *  • Connect FolderWatcher signals → write new/removed files to DB + Model
 *  • Open a PDF externally → record last-opened timestamp
 *  • Coordinate ThumbnailGenerator → push thumbnails into PdfModel
 *
 * This class intentionally knows nothing about the UI.
 */
class PdfController : public QObject
{
    Q_OBJECT

public:
    explicit PdfController(PdfModel*       pdfModel,
                           DatabaseManager* db,
                           FolderWatcher*   watcher,
                           QObject*         parent = nullptr);

    void initialize();   ///< Call once after construction

    // ── Actions ───────────────────────────────────────────────────────────────

    /// Open the PDF at @p filePath in an external viewer and record the timestamp.
    void openPdf(const QString& filePath);

    /// Request a thumbnail for @p filePath (no-op if already generated).
    void requestThumbnail(const QString& filePath);

    /// Request thumbnails for all visible files (e.g. when switching to grid view).
    void requestVisibleThumbnails();

signals:
    void pdfOpened   (const QString& filePath, const QDateTime& when);
    void errorOccurred(const QString& message);

private slots:
    void onFileDiscovered(const PdfFile& file);
    void onFileRemoved   (const QString& filePath);
    void onThumbnailReady(const QString& filePath, const QPixmap& pix);

private:
    PdfModel*        m_model;
    DatabaseManager* m_db;
    FolderWatcher*   m_watcher;
    ThumbnailGenerator* m_thumbGen;

    QSet<QString> m_thumbnailRequested;   ///< Avoid duplicate requests
};
