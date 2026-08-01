#pragma once
#include <QWidget>

class QTableView;
class QHeaderView;
class SearchFilterProxy;
class PdfModel;
class ListDelegate;

/**
 * @brief List view – shows PDFs as a dense table with sortable columns.
 *
 * Wraps a QTableView with a custom ListDelegate, header configuration,
 * and keyboard/double-click handling.
 */
class ListView : public QWidget
{
    Q_OBJECT

public:
    explicit ListView(PdfModel*         model,
                      SearchFilterProxy* proxy,
                      QWidget*           parent = nullptr);

    void scrollToTop();

signals:
    /// User double-clicked (or pressed Enter) on a PDF row.
    void fileActivated(const QString& filePath);
    void fileSelected(const QString& filePath);

    /// User wants to assign/edit tags for this file.
    void editTagsRequested(const QString& filePath);

    /// User asked to take this file out of its group. Unlike deleting the PDF
    /// in a file manager, this is a deliberate shared-state change — MainWindow
    /// confirms it and decides what happens to the stored and local copies.
    void removeFileRequested(const QString& filePath);

    /// Request thumbnail for a file (for caching when switching to grid view).
    void thumbnailNeeded(const QString& filePath);

private slots:
    void onActivated   (const QModelIndex& proxyIndex);
    void showContextMenu(const QPoint& pos);
    void onProxyModelChanged();

private:
    void buildUi();
    void requestVisibleThumbnails();

    PdfModel*         m_model;
    SearchFilterProxy* m_proxy;
    QTableView*        m_tableView = nullptr;
    ListDelegate*      m_delegate  = nullptr;
};
