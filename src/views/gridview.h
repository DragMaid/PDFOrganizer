#pragma once
#include <QWidget>

class QListView;
class SearchFilterProxy;
class PdfModel;
class GridDelegate;

/**
 * @brief Grid view – shows PDFs as thumbnail cards using QListView in icon mode.
 */
class GridView : public QWidget
{
    Q_OBJECT

public:
    explicit GridView(PdfModel*         model,
                      SearchFilterProxy* proxy,
                      QWidget*           parent = nullptr);

    void scrollToTop();

    /// Call this after switching to this view so thumbnails are generated.
    void triggerThumbnailLoad();

signals:
    void fileActivated    (const QString& filePath);
    void editTagsRequested(const QString& filePath);
    void thumbnailNeeded  (const QString& filePath);

private slots:
    void onActivated  (const QModelIndex& proxyIndex);
    void showContextMenu(const QPoint& pos);
    void onViewScrolled();

private:
    void buildUi();
    void requestVisibleThumbnails();

    PdfModel*          m_model;
    SearchFilterProxy*  m_proxy;
    QListView*          m_listView  = nullptr;
    GridDelegate*       m_delegate  = nullptr;
};
