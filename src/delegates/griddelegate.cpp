#include "griddelegate.h"
#include "models/pdfmodel.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QStringList>
#include <QApplication>

GridDelegate::GridDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

QSize GridDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return {kCardWidth, kCardHeight};
}

void GridDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect  rect     = option.rect.adjusted(6, 6, -6, -6);
    const bool   selected = option.state & QStyle::State_Selected;
    const bool   hovered  = option.state & QStyle::State_MouseOver;

    const QString     name  = index.data(PdfModel::FileNameRole).toString();
    const QStringList tags  = index.data(PdfModel::TagsRole).toStringList();
    const QPixmap     thumb = qvariant_cast<QPixmap>(index.data(PdfModel::ThumbnailRole));

    // ── Background card ───────────────────────────────────────────────────────
    drawCard(painter, rect, selected, hovered);

    // ── Thumbnail ─────────────────────────────────────────────────────────────
    const QRect thumbRect(rect.left(), rect.top(), rect.width(), kThumbHeight);
    drawThumbnail(painter, thumbRect, thumb);

    // ── Separator ─────────────────────────────────────────────────────────────
    painter->setPen(QPen(QColor(0x3a, 0x3d, 0x42), 1));
    painter->drawLine(rect.left(), rect.top() + kThumbHeight,
                      rect.right(), rect.top() + kThumbHeight);

    // ── Filename ──────────────────────────────────────────────────────────────
    const int infoTop = rect.top() + kThumbHeight + kPadding;
    const QRect nameRect(rect.left() + kPadding, infoTop,
                         rect.width() - 2 * kPadding, 20);
    drawFileName(painter, nameRect, name);

    // ── Tag pills ─────────────────────────────────────────────────────────────
    const QRect pillRect(rect.left() + kPadding, nameRect.bottom() + 4,
                         rect.width() - 2 * kPadding,
                         rect.bottom() - nameRect.bottom() - kPadding - 4);
    drawTagPills(painter, pillRect, tags);

    painter->restore();
}

// ── Private drawing helpers ───────────────────────────────────────────────────

void GridDelegate::drawCard(QPainter* p, const QRect& rect,
                             bool selected, bool hovered) const
{
    QPainterPath path;
    path.addRoundedRect(rect, kCornerRadius, kCornerRadius);

    QColor bg = selected ? QColor(0x37, 0x4e, 0x6e)
              : hovered   ? QColor(0x31, 0x33, 0x38)
                          : QColor(0x27, 0x29, 0x2e);
    p->fillPath(path, bg);

    QColor border = selected ? QColor(0x4d, 0x8e, 0xff) : QColor(0x3a, 0x3d, 0x42);
    p->setPen(QPen(border, selected ? 2.0 : 1.0));
    p->drawPath(path);
}

void GridDelegate::drawThumbnail(QPainter* p,
                                  const QRect& thumbRect,
                                  const QPixmap& pix) const
{
    // Clip to rounded top corners
    QPainterPath clip;
    clip.addRoundedRect(thumbRect, kCornerRadius, kCornerRadius);
    // Only round the top: subtract a rect covering the bottom half
    QRectF bottom(thumbRect.left(), thumbRect.center().y(),
                  thumbRect.width(), thumbRect.height() / 2.0 + 1);
    clip.addRect(bottom);
    p->setClipPath(clip);

    if (!pix.isNull()) {
        const QPixmap scaled = pix.scaled(thumbRect.size(),
                                          Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation);
        const QPoint offset((thumbRect.width() - scaled.width()) / 2,
                            (thumbRect.height() - scaled.height()) / 2);
        p->drawPixmap(thumbRect.topLeft() + offset, scaled);
    } else {
        // Gradient placeholder
        QLinearGradient grad(thumbRect.topLeft(), thumbRect.bottomLeft());
        grad.setColorAt(0, QColor(0x2d, 0x30, 0x35));
        grad.setColorAt(1, QColor(0x1e, 0x20, 0x24));
        p->fillRect(thumbRect, grad);

        // PDF text centred
        p->setClipping(false);
        QFont f; f.setPointSize(22); f.setBold(true);
        p->setFont(f);
        p->setPen(QColor(0xf0, 0x3a, 0x3a));
        p->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("PDF"));
    }
    p->setClipping(false);
}

void GridDelegate::drawFileName(QPainter* p,
                                 const QRect& rect,
                                 const QString& name) const
{
    QFont f = QApplication::font();
    f.setBold(true);
    f.setPointSize(8);
    p->setFont(f);
    p->setPen(QColor(0xe0, 0xe3, 0xe8));

    const QFontMetrics fm(f);
    const QString elided = fm.elidedText(name, Qt::ElideRight, rect.width());
    p->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, elided);
}

void GridDelegate::drawTagPills(QPainter* p,
                                 const QRect& available,
                                 const QStringList& tags) const
{
    if (tags.isEmpty()) return;

    QFont f = QApplication::font();
    f.setPointSize(7);
    p->setFont(f);
    const QFontMetrics fm(f);

    int x = available.left();
    int y = available.top();

    for (const QString& tag : tags) {
        const int textW = fm.horizontalAdvance(tag);
        const int pillW = textW + 10;

        if (x + pillW > available.right()) {
            x = available.left();
            y += kTagPillH + 3;
            if (y + kTagPillH > available.bottom()) break;
        }

        const QRect pill(x, y, pillW, kTagPillH);
        QPainterPath path;
        path.addRoundedRect(pill, kTagPillH / 2, kTagPillH / 2);

        const QColor bg = pillColor(tag);
        p->fillPath(path, bg);

        p->setPen(bg.lighter(180));
        p->drawText(pill, Qt::AlignCenter, tag);

        x += pillW + 4;
    }
}

QColor GridDelegate::pillColor(const QString& tag)
{
    // Deterministic colour from tag hash so the same tag always looks the same
    const uint h = qHash(tag);
    const int hue = h % 360;
    return QColor::fromHsl(hue, 130, 55);
}
