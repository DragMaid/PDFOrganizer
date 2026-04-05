#include "thumbnailgenerator.h"
#include <QFileInfo>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QDebug>

#ifdef HAVE_QT_PDF
#  include <QPdfDocument>
#  include <QPdfPageRenderer>
#endif

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent)
{}

void ThumbnailGenerator::requestThumbnail(const QString& filePath, QSize targetSize)
{
    // Capture variables by value so the lambda is safe on any thread.
    const QString path = filePath;
    const QSize   size = targetSize;

    auto* watcher = new QFutureWatcher<QPixmap>(this);

    connect(watcher, &QFutureWatcher<QPixmap>::finished, this,
            [this, watcher, path]() {
                const QPixmap pix = watcher->result();
                emit thumbnailReady(path, pix);
                watcher->deleteLater();
            });

    QFuture<QPixmap> future = QtConcurrent::run(
        [path, size]() { return renderThumbnail(path, size); }
    );
    watcher->setFuture(future);
}

void ThumbnailGenerator::cancelThumbnail(const QString& /*filePath*/)
{
    // Qt Concurrent does not support per-task cancellation; no-op for now.
    // A production implementation could use a cancellation flag map.
}

// ── Static render helpers ─────────────────────────────────────────────────────

QPixmap ThumbnailGenerator::renderThumbnail(const QString& filePath, QSize size)
{
#ifdef HAVE_QT_PDF
    QPdfDocument doc;

    if (doc.load(filePath) == QPdfDocument::Error::None && doc.pageCount() > 0) {
        QSizeF pageSize(600, 800);

        QSize renderSize = pageSize.toSize();
        renderSize.scale(size, Qt::KeepAspectRatio);

        QImage img = doc.render(0, renderSize); 

        if (!img.isNull()) {
            QPixmap pixmap(size);
            pixmap.fill(Qt::white);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            
            // Center item
            int x = (size.width() - img.width()) / 2;
            int y = (size.height() - img.height()) / 2;
            p.drawImage(x, y, img);
            
            return pixmap;
        }
    }
#endif
    return makePlaceholder(QFileInfo(filePath).fileName(), size);
}

QPixmap ThumbnailGenerator::makePlaceholder(const QString& fileName, QSize size)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Card background
    const QRectF card(2, 2, size.width() - 4, size.height() - 4);
    p.setBrush(QColor(0x2b, 0x2d, 0x30));   // dark card
    p.setPen(QPen(QColor(0x4a, 0x4d, 0x52), 1.5));
    p.drawRoundedRect(card, 6, 6);

    // Folded corner decoration
    const int cornerSize = size.width() / 5;
    QPolygonF corner;
    corner << QPointF(card.right() - cornerSize, card.top())
           << QPointF(card.right(),               card.top())
           << QPointF(card.right(),               card.top() + cornerSize);
    p.setBrush(QColor(0x4a, 0x4d, 0x52));
    p.setPen(Qt::NoPen);
    p.drawPolygon(corner);

    // PDF label
    QFont bold;
    bold.setBold(true);
    bold.setPointSize(14);
    p.setFont(bold);
    p.setPen(QColor(0xf0, 0x3a, 0x3a));   // red PDF badge
    p.drawText(card.adjusted(8, 8, -8, -8),
               Qt::AlignTop | Qt::AlignLeft,
               QStringLiteral("PDF"));

    // File name (truncated)
    QFont small;
    small.setPointSize(7);
    p.setFont(small);
    p.setPen(QColor(0xb0, 0xb3, 0xb8));
    QFontMetrics fm(small);
    const QString elided = fm.elidedText(fileName, Qt::ElideMiddle, size.width() - 16);
    p.drawText(card.adjusted(8, 0, -8, -8), Qt::AlignBottom | Qt::AlignHCenter, elided);

    p.end();
    return pix;
}
