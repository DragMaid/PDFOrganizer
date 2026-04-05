#pragma once
#include <QWidget>
#include <QList>
#include "models/pdffile.h"

class QScrollArea;
class QVBoxLayout;
class PdfModel;

/**
 * @brief Shows the N most recently opened PDFs as a polished card-based panel.
 *
 * Sorted descending by lastOpened timestamp. Features:
 * - Card-based layout with thumbnails
 * - Hover effects for interactivity
 * - Clean typography and spacing
 * - Collapsible header with item count
 */
class RecentView : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kMaxItems = 15;
    static constexpr int kCardHeight = 80;
    static constexpr int kCardSpacing = 8;

    explicit RecentView(PdfModel* model, QWidget* parent = nullptr);

    /// Re-populate from the current model state.
    void refresh();

signals:
    void fileActivated(const QString& filePath);

private slots:
    void onToggleExpanded();

private:
    void buildUi();
    void buildCards();
    void createCard(const PdfFile& file);

    PdfModel*    m_model     = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QVBoxLayout* m_cardsLayout = nullptr;
    bool         m_isExpanded = true;
    int          m_itemCount = 0;
};
