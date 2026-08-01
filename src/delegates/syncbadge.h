#pragma once
#include <QColor>
#include <QPainter>
#include <QRect>

#include "models/pdfmodel.h"

/**
 * @brief The dot that says whether the group holds a copy of a file.
 *
 * Both the list and the grid answer the same question in the same place — the
 * bottom-right corner of the file's thumbnail — so the answer is drawn from one
 * function rather than twice from two delegates.
 *
 * Colour carries the whole meaning, so it is worth being deliberate about:
 * green is the resting state and needs no explanation, amber means *this
 * machine is holding something the group has not got*, and blue means it is
 * moving right now. A file whose folder has no group gets nothing at all —
 * silence is the honest answer when there is nobody to be in sync with.
 */
namespace SyncBadge {

inline constexpr int kDot = 11;      ///< Diameter of the state dot.
inline constexpr int kMetaDot = 7;   ///< The smaller "unsent edits" dot.

inline QColor colorFor(PdfModel::SyncState state)
{
    switch (state) {
    case PdfModel::SyncSynced:       return QColor(0x3f, 0xb9, 0x50);
    case PdfModel::SyncLocalOnly:    return QColor(0xd0, 0xa2, 0x4c);
    case PdfModel::SyncTransferring: return QColor(0x4d, 0x8e, 0xff);
    case PdfModel::SyncUnknown:      break;
    }
    return {};
}

/// Draw the badge over the bottom-right corner of @p anchor.
///
/// @p pendingMeta adds a second, smaller dot beside it for tags or notes
/// written here that have not reached the server. It is a separate mark on
/// purpose: metadata never holds a sync up and never asks to be sent, so it
/// must not look like the file itself is behind.
inline void paint(QPainter* p, const QRect& anchor,
                  PdfModel::SyncState state, bool pendingMeta)
{
    const QColor color = colorFor(state);
    if (!color.isValid() && !pendingMeta)
        return;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    int right = anchor.right() - 1;
    const int centerY = anchor.bottom() - kDot / 2 - 1;

    if (color.isValid()) {
        const QRect dot(right - kDot, centerY - kDot / 2, kDot, kDot);
        // A ring in the card's own background colour, so the dot stays legible
        // on top of a pale scanned page as well as a dark one.
        p->setPen(QPen(QColor(0x1e, 0x20, 0x24), 2.0));
        p->setBrush(color);
        p->drawEllipse(dot);
        right = dot.left() - 3;
    }

    if (pendingMeta) {
        const QRect dot(right - kMetaDot, centerY - kMetaDot / 2,
                        kMetaDot, kMetaDot);
        p->setPen(QPen(QColor(0x1e, 0x20, 0x24), 2.0));
        p->setBrush(QColor(0xa3, 0x71, 0xf7));
        p->drawEllipse(dot);
    }

    p->restore();
}

} // namespace SyncBadge
