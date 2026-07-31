#pragma once
#include <QStyledItemDelegate>

/**
 * @brief Renders each PDF as a compact list row with an icon, name, and tag pills.
 *
 * Row anatomy (height = 52px):
 *  ┌─[icon 36×36]──[Filename.pdf]──────────────[tag] [tag]──[folder]──[date]─┐
 */
class ListDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int kRowHeight  = 52;
    static constexpr int kIconSize   = 36;
    static constexpr int kPadding    = 8;
    static constexpr int kTagPillH   = 16;

    explicit ListDelegate(QObject* parent = nullptr);

    void  paint   (QPainter* painter, const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index)  const override;

private:
    void drawBackground(QPainter* p, const QRect& rect, bool selected, bool hovered) const;
    void drawIcon      (QPainter* p, const QRect& iconRect, const QPixmap& thumb) const;
    void drawPrimaryText(QPainter* p, const QRect& r, const QString& name,
                         bool struckThrough) const;
    void drawSecondaryText(QPainter* p, const QRect& r, const QString& folder,
                           const QString& date) const;
    void drawTagPills  (QPainter* p, int x, int y, const QStringList& tags,
                        int maxWidth) const;

    static QColor pillColor(const QString& tag);
};
