#include "listview.h"
#include "models/pdfmodel.h"
#include "utils/searchfilterproxy.h"
#include "delegates/listdelegate.h"
#include <QTableView>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QMenu>
#include <QKeyEvent>
#include <QAction>
#include <QTimer>
#include <QScrollBar>
#include <QItemSelectionModel>

ListView::ListView(PdfModel*         model,
                   SearchFilterProxy* proxy,
                   QWidget*           parent)
    : QWidget(parent)
    , m_model(model)
    , m_proxy(proxy)
{
    buildUi();
}

void ListView::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tableView = new QTableView(this);
    m_delegate  = new ListDelegate(this);

    m_tableView->setModel(m_proxy);
    m_tableView->setItemDelegate(m_delegate);

    // ── Appearance ────────────────────────────────────────────────────────────
    m_tableView->setShowGrid(false);
    m_tableView->setFrameShape(QFrame::NoFrame);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setAlternatingRowColors(false);
    m_tableView->setMouseTracking(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Row height
    m_tableView->verticalHeader()->setDefaultSectionSize(ListDelegate::kRowHeight);

    // Column widths
    QHeaderView* hdr = m_tableView->horizontalHeader();
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(PdfModel::ColFileName,   QHeaderView::Stretch);
    hdr->setSectionResizeMode(PdfModel::ColFolder,     QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(PdfModel::ColTags,       QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(PdfModel::ColLastOpened, QHeaderView::Fixed);
    hdr->setSectionResizeMode(PdfModel::ColSize,       QHeaderView::Fixed);
    m_tableView->setColumnWidth(PdfModel::ColLastOpened, 130);
    m_tableView->setColumnWidth(PdfModel::ColSize,       75);

    // Sort by last opened descending by default
    m_tableView->sortByColumn(PdfModel::ColLastOpened, Qt::DescendingOrder);

    // ── Context menu ──────────────────────────────────────────────────────────
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &ListView::showContextMenu);

    connect(m_tableView, &QTableView::activated, this, &ListView::onActivated);
    connect(m_tableView, &QTableView::doubleClicked, this, &ListView::onActivated);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (!current.isValid()) return;
                const QModelIndex srcIdx = m_proxy->mapToSource(current);
                emit fileSelected(m_model->data(srcIdx, PdfModel::FilePathRole).toString());
            });

    // Request thumbnails as the user scrolls
    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ListView::requestVisibleThumbnails);

    // Auto-load visible rows when proxy model changes (folder filter, search, etc)
    connect(m_proxy, &SearchFilterProxy::layoutChanged,
            this, &ListView::onProxyModelChanged);
    connect(m_proxy, &SearchFilterProxy::modelReset,
            this, &ListView::onProxyModelChanged);

    layout->addWidget(m_tableView);
}

void ListView::scrollToTop()
{
    m_tableView->scrollToTop();
}

void ListView::onProxyModelChanged()
{
    // When the proxy model changes (folder filter, search, etc),
    // scroll to top to show the filtered results and request thumbnails.
    // Use a timer to ensure the view is properly laid out first.
    QTimer::singleShot(0, this, [this]() {
        m_tableView->scrollToTop();
        requestVisibleThumbnails();
    });
}

void ListView::requestVisibleThumbnails()
{
    // Emit thumbnailNeeded for every visible row to pre-cache them
    const QRect viewport = m_tableView->viewport()->rect();
    const QModelIndex firstIdx = m_tableView->indexAt(viewport.topLeft());
    const QModelIndex lastIdx = m_tableView->indexAt(viewport.bottomRight());

    int firstRow = firstIdx.isValid() ? firstIdx.row() : 0;
    int lastRow = lastIdx.isValid() ? lastIdx.row() : m_proxy->rowCount() - 1;

    for (int row = firstRow; row <= lastRow; ++row) {
        const QModelIndex proxyIdx = m_proxy->index(row, 0);
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        const QString path = m_model->data(srcIdx, PdfModel::FilePathRole).toString();
        if (!path.isEmpty())
            emit thumbnailNeeded(path);
    }
}

void ListView::onActivated(const QModelIndex& proxyIndex)
{
    if (!proxyIndex.isValid()) return;
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIndex);
    emit fileActivated(m_model->data(srcIdx, PdfModel::FilePathRole).toString());
}

void ListView::showContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIdx = m_tableView->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    const QString path = m_model->data(srcIdx, PdfModel::FilePathRole).toString();
    const QString name = m_model->data(srcIdx, PdfModel::FileNameRole).toString();

    QMenu menu(this);
    menu.setTitle(name);

    QAction* openAct = menu.addAction(QStringLiteral("Open PDF"));
    menu.addSeparator();
    QAction* tagAct  = menu.addAction(QStringLiteral("Edit Tags…"));
    menu.addSeparator();
    QAction* removeAct = menu.addAction(QStringLiteral("Remove from Group…"));
    removeAct->setToolTip(QStringLiteral(
        "Take this file out of its shared group. Deleting the PDF in a file "
        "manager does not do this."));

    const QAction* chosen = menu.exec(m_tableView->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == openAct)
        emit fileActivated(path);
    else if (chosen == tagAct)
        emit editTagsRequested(path);
    else if (chosen == removeAct)
        emit removeFileRequested(path);
}
