#include "gridview.h"
#include "models/pdfmodel.h"
#include "utils/searchfilterproxy.h"
#include "delegates/griddelegate.h"
#include <QListView>
#include <QVBoxLayout>
#include <QMenu>
#include <QScrollBar>
#include <QAction>
#include <QTimer>
#include <QItemSelectionModel>

GridView::GridView(PdfModel*         model,
                   SearchFilterProxy* proxy,
                   QWidget*           parent)
    : QWidget(parent)
    , m_model(model)
    , m_proxy(proxy)
{
    buildUi();
}

void GridView::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 10, 30, 10);

    m_listView = new QListView(this);
    m_delegate = new GridDelegate(this);

    m_listView->setModel(m_proxy);
    m_listView->setItemDelegate(m_delegate);

    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setGridSize(QSize(GridDelegate::kCardWidth + 12,
                                  GridDelegate::kCardHeight + 12));
    m_listView->setSpacing(6);
    m_listView->setUniformItemSizes(true);
    m_listView->setMovement(QListView::Static);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setMouseTracking(true);

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QListView::customContextMenuRequested,
            this, &GridView::showContextMenu);
    connect(m_listView, &QListView::activated,
            this, &GridView::onActivated);
    connect(m_listView, &QListView::doubleClicked,
            this, &GridView::onActivated);
    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (!current.isValid()) return;
                const QModelIndex srcIdx = m_proxy->mapToSource(current);
                emit fileSelected(m_model->data(srcIdx, PdfModel::FilePathRole).toString());
            });

    // Request thumbnails as the user scrolls
    connect(m_listView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &GridView::onViewScrolled);

    // Request thumbnails when proxy model is updated (folder changed, filter changed, etc)
    connect(m_proxy, &SearchFilterProxy::layoutChanged,
            this, &GridView::onProxyModelChanged);
    connect(m_proxy, &SearchFilterProxy::modelReset,
            this, &GridView::onProxyModelChanged);

    layout->addWidget(m_listView);
}

void GridView::scrollToTop()
{
    m_listView->scrollToTop();
}

void GridView::triggerThumbnailLoad()
{
    requestVisibleThumbnails();
}

void GridView::onActivated(const QModelIndex& proxyIndex)
{
    if (!proxyIndex.isValid()) return;
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIndex);
    emit fileActivated(m_model->data(srcIdx, PdfModel::FilePathRole).toString());
}

void GridView::showContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIdx = m_listView->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    const QString path = m_model->data(srcIdx, PdfModel::FilePathRole).toString();

    QMenu menu(this);
    QAction* openAct = menu.addAction(QStringLiteral("Open PDF"));
    menu.addSeparator();
    QAction* tagAct  = menu.addAction(QStringLiteral("Edit Tags…"));

    const QAction* chosen = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == openAct)
        emit fileActivated(path);
    else if (chosen == tagAct)
        emit editTagsRequested(path);
}

void GridView::onViewScrolled()
{
    requestVisibleThumbnails();
}

void GridView::onProxyModelChanged()
{
    // When the proxy model changes (folder filter, search, etc),
    // scroll to top and reload visible thumbnails.
    // Use a timer to ensure the view is properly laid out first.
    m_listView->scrollToTop();
    QTimer::singleShot(0, this, &GridView::requestVisibleThumbnails);
}

void GridView::requestVisibleThumbnails()
{
    // Emit thumbnailNeeded for every visible index
    const QRect viewport = m_listView->viewport()->rect();
    QModelIndex first = m_listView->indexAt(viewport.topLeft());
    QModelIndex last  = m_listView->indexAt(viewport.bottomRight());

    if (!first.isValid()) first = m_proxy->index(0, 0);
    if (!last.isValid())  last  = m_proxy->index(m_proxy->rowCount() - 1, 0);

    for (int row = first.row(); row <= last.row(); ++row) {
        const QModelIndex proxyIdx = m_proxy->index(row, 0);
        const QModelIndex srcIdx   = m_proxy->mapToSource(proxyIdx);
        const QString path = m_model->data(srcIdx, PdfModel::FilePathRole).toString();
        if (!path.isEmpty())
            emit thumbnailNeeded(path);
    }
}
