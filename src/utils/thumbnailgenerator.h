#pragma once
#include <QObject>
#include <QPixmap>
#include <QSize>

/**
 * @brief Generates PDF thumbnails asynchronously using Qt::Concurrent.
 *
 * If Qt6::Pdf (Qt PDF module) is available at build time (HAVE_QT_PDF) the
 * generator renders the first page.  Otherwise it returns a styled placeholder
 * that still looks good in the grid view.
 *
 * Usage:
 *   auto* gen = new ThumbnailGenerator(this);
 *   connect(gen, &ThumbnailGenerator::thumbnailReady,
 *           model, &PdfModel::setThumbnail);
 *   gen->requestThumbnail("/path/to/file.pdf");
 */
class ThumbnailGenerator : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultWidth  = 160;
    static constexpr int kDefaultHeight = 240;

    explicit ThumbnailGenerator(QObject* parent = nullptr);

    /// Schedule thumbnail generation for @p filePath on a worker thread.
    void requestThumbnail(const QString& filePath,
                          QSize targetSize = {kDefaultWidth, kDefaultHeight});

    /// Cancel any pending requests for this path (best-effort).
    void cancelThumbnail(const QString& filePath);

signals:
    /// Emitted on the main thread when the thumbnail is ready.
    void thumbnailReady(const QString& filePath, const QPixmap& pixmap);

private:
    /// Synchronous render called on the thread pool.
    static QPixmap renderThumbnail(const QString& filePath, QSize size);
    static QPixmap makePlaceholder(const QString& fileName, QSize size);
};
