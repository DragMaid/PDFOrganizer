#pragma once
#include <QStyledItemDelegate>
#include <QSize>

/**
 * @brief Renders each PDF as a card in the grid view.
 *
 * Card anatomy
 * ────────────
 *  ┌──────────────────┐
 *  │  [thumbnail]     │  ← scaled pixmap or placeholder
 *  │                  │
 *  ├──────────────────┤
 *  │ Filename.pdf     │  ← bold, elided
 *  │ [tag] [tag]      │  ← pill badges
 *  └──────────────────┘
 */
class GridDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int kCardWidth    = 180;
    static constexpr int kCardHeight   = 240;
    static constexpr int kThumbHeight  = 170;
    static constexpr int kPadding      = 8;
    static constexpr int kTagPillH     = 16;
    static constexpr int kCornerRadius = 8;

    explicit GridDelegate(QObject* parent = nullptr);

    void  paint      (QPainter* painter, const QStyleOptionViewItem& option,
                      const QModelIndex& index) const override;
    QSize sizeHint   (const QStyleOptionViewItem& option,
                      const QModelIndex& index)  const override;

private:
    void drawCard      (QPainter* p, const QRect& rect, bool selected, bool hovered) const;
    void drawThumbnail (QPainter* p, const QRect& thumbRect, const QPixmap& pix) const;
    void drawFileName  (QPainter* p, const QRect& textRect, const QString& name,
                        bool struckThrough) const;
    void drawTagPills  (QPainter* p, const QRect& pillRect, const QStringList& tags) const;

    static QColor pillColor(const QString& tag);
};
