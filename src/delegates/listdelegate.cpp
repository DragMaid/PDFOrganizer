#include "listdelegate.h"
#include "models/pdfmodel.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QApplication>
#include <QDateTime>

ListDelegate::ListDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

QSize ListDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return {-1, kRowHeight};
}

void ListDelegate::paint(QPainter* painter,
                          const QStyleOptionViewItem& option,
                          const QModelIndex& index) const
{
    // Only draw the complex layout with icon for the filename column
    if (index.column() != PdfModel::ColFileName) {
        // For other columns, use default delegate painting
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect  rect     = option.rect;
    const bool   selected = option.state & QStyle::State_Selected;
    const bool   hovered  = option.state & QStyle::State_MouseOver;

    const QString     name     = index.data(PdfModel::FileNameRole).toString();
    const QString     folder   = index.data(PdfModel::FolderPathRole).toString();
    const QStringList tags     = index.data(PdfModel::TagsRole).toStringList();
    const QDateTime   opened   = index.data(PdfModel::LastOpenedRole).toDateTime();
    const QPixmap     thumb    = qvariant_cast<QPixmap>(index.data(PdfModel::ThumbnailRole));
    const bool        removing = index.data(PdfModel::PendingRemovalRole).toBool();

    // A removal is a round trip that can fail, so the row is faded rather than
    // taken away — the user sees it is on its way out and can carry on.
    if (removing)
        painter->setOpacity(0.45);

    const QString dateStr = opened.isValid()
        ? opened.toString(QStringLiteral("dd MMM yyyy"))
        : QStringLiteral("Never");

    // ── Background ────────────────────────────────────────────────────────────
    drawBackground(painter, rect, selected, hovered);

    // Separator line at the bottom
    painter->setPen(QPen(QColor(0x30, 0x33, 0x38), 1));
    painter->drawLine(rect.left() + kIconSize + kPadding * 2, rect.bottom(),
                      rect.right(), rect.bottom());

    // ── Icon ──────────────────────────────────────────────────────────────────
    const QRect iconRect(rect.left() + kPadding,
                         rect.top() + (rect.height() - kIconSize) / 2,
                         kIconSize, kIconSize);
    drawIcon(painter, iconRect, thumb);

    // ── Text area ─────────────────────────────────────────────────────────────
    const int textLeft = iconRect.right() + kPadding;
    // Reserve ~140px for date on the right
    const int dateWidth   = 110;
    const int textRight   = rect.right() - dateWidth - kPadding;

    const QRect primaryRect(textLeft, rect.top() + 7,
                            textRight - textLeft - 4, 18);
    drawPrimaryText(painter, primaryRect, name, removing);

    // Tag pills on the second line
    const int pillY = primaryRect.bottom() + 4;
    drawTagPills(painter, textLeft, pillY, tags,
                 textRight - textLeft - 4);

    // ── Date + folder (right-aligned) ─────────────────────────────────────────
    const QRect rightRect(textRight, rect.top() + 5, dateWidth, rect.height() - 10);
    drawSecondaryText(painter, rightRect, folder,
                      removing ? QStringLiteral("Removing…") : dateStr);

    painter->restore();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ListDelegate::drawBackground(QPainter* p, const QRect& rect,
                                   bool selected, bool hovered) const
{
    QColor bg = selected ? QColor(0x2b, 0x40, 0x60)
              : hovered   ? QColor(0x2a, 0x2c, 0x31)
                          : QColor(0x1e, 0x20, 0x24);
    p->fillRect(rect, bg);
}

void ListDelegate::drawIcon(QPainter* p, const QRect& iconRect,
                              const QPixmap& thumb) const
{
    // Rounded clip
    QPainterPath clip;
    clip.addRoundedRect(iconRect, 5, 5);
    p->setClipPath(clip);

    if (!thumb.isNull()) {
        const QPixmap scaled = thumb.scaled(iconRect.size(),
                                            Qt::KeepAspectRatioByExpanding,
                                            Qt::SmoothTransformation);
        const QPoint off((iconRect.width() - scaled.width()) / 2,
                         (iconRect.height() - scaled.height()) / 2);
        p->drawPixmap(iconRect.topLeft() + off, scaled);
    } else {
        p->fillRect(iconRect, QColor(0x2d, 0x30, 0x35));
        QFont f; f.setBold(true); f.setPointSize(9);
        p->setFont(f);
        p->setPen(QColor(0xf0, 0x3a, 0x3a));
        p->drawText(iconRect, Qt::AlignCenter, QStringLiteral("PDF"));
    }
    p->setClipping(false);
}

void ListDelegate::drawPrimaryText(QPainter* p, const QRect& r,
                                    const QString& name, bool struckThrough) const
{
    QFont f = QApplication::font();
    f.setBold(true);
    f.setPointSize(9);
    f.setStrikeOut(struckThrough);
    p->setFont(f);
    p->setPen(QColor(0xe0, 0xe3, 0xe8));

    const QFontMetrics fm(f);
    p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                fm.elidedText(name, Qt::ElideRight, r.width()));
}

void ListDelegate::drawSecondaryText(QPainter* p, const QRect& r,
                                      const QString& folder,
                                      const QString& date) const
{
    QFont f = QApplication::font();
    f.setPointSize(7);
    p->setFont(f);
    p->setPen(QColor(0x7a, 0x7d, 0x85));

    const QFontMetrics fm(f);
    // Date on top
    p->drawText(r.adjusted(0, 0, 0, -r.height() / 2),
                Qt::AlignRight | Qt::AlignVCenter, date);
    // Folder on bottom
    p->drawText(r.adjusted(0, r.height() / 2, 0, 0),
                Qt::AlignRight | Qt::AlignVCenter,
                fm.elidedText(folder.section(QLatin1Char('/'), -1), Qt::ElideLeft,
                              r.width()));
}

void ListDelegate::drawTagPills(QPainter* p, int startX, int y,
                                 const QStringList& tags, int maxWidth) const
{
    if (tags.isEmpty()) return;

    QFont f = QApplication::font();
    f.setPointSize(7);
    p->setFont(f);
    const QFontMetrics fm(f);

    int x = startX;
    for (const QString& tag : tags) {
        const int textW = fm.horizontalAdvance(tag);
        const int pillW = textW + 8;
        if (x + pillW > startX + maxWidth) break;

        const QRect pill(x, y, pillW, kTagPillH);
        QPainterPath path;
        path.addRoundedRect(pill, kTagPillH / 2, kTagPillH / 2);

        const QColor bg = pillColor(tag);
        p->fillPath(path, bg);
        p->setPen(bg.lighter(180));
        p->drawText(pill, Qt::AlignCenter, tag);

        x += pillW + 3;
    }
}

QColor ListDelegate::pillColor(const QString& tag)
{
    const uint h = qHash(tag);
    return QColor::fromHsl(static_cast<int>(h % 360), 130, 55);
}
