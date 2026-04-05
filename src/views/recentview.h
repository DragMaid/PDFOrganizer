#pragma once
#include <QWidget>
#include <QList>
#include "models/pdffile.h"

class QListWidget;
class QListWidgetItem;
class PdfModel;

/**
 * @brief Shows the N most recently opened PDFs as a quick-access panel.
 *
 * Sorted descending by lastOpened timestamp.  Clicking an item opens the PDF.
 */
class RecentView : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kMaxItems = 20;

    explicit RecentView(PdfModel* model, QWidget* parent = nullptr);

    /// Re-populate from the current model state.
    void refresh();

signals:
    void fileActivated(const QString& filePath);

private slots:
    void onItemActivated(QListWidgetItem* item);

private:
    void buildUi();

    PdfModel*    m_model     = nullptr;
    QListWidget* m_listWidget = nullptr;
};
