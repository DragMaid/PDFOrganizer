#include "recentview.h"
#include "models/pdfmodel.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <algorithm>

RecentView::RecentView(PdfModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    buildUi();

    // Refresh whenever the underlying model changes
    connect(m_model, &QAbstractItemModel::dataChanged,  this, &RecentView::refresh);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &RecentView::refresh);
    connect(m_model, &QAbstractItemModel::modelReset,   this, &RecentView::refresh);

    refresh();
}

void RecentView::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QLabel(QStringLiteral("  Recently Opened"));
    header->setObjectName(QStringLiteral("sectionLabel"));
    header->setFixedHeight(32);

    m_listWidget = new QListWidget(this);
    m_listWidget->setFrameShape(QFrame::NoFrame);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listWidget->setSpacing(2);

    connect(m_listWidget, &QListWidget::itemActivated,
            this, &RecentView::onItemActivated);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &RecentView::onItemActivated);

    layout->addWidget(header);
    layout->addWidget(m_listWidget);
}

void RecentView::refresh()
{
    m_listWidget->clear();

    // Collect files that have been opened, sort by descending lastOpened
    QList<PdfFile> opened;
    const QList<PdfFile> all = m_model->allFiles();
    for (const PdfFile& f : all)
        if (f.lastOpened.isValid())
            opened.append(f);

    std::sort(opened.begin(), opened.end(),
              [](const PdfFile& a, const PdfFile& b) {
                  return a.lastOpened > b.lastOpened;
              });

    if (opened.size() > kMaxItems)
        opened = opened.mid(0, kMaxItems);

    for (const PdfFile& f : opened) {
        auto* item = new QListWidgetItem(m_listWidget);
        item->setData(Qt::UserRole, f.filePath);

        const QString when = f.lastOpened.toString(QStringLiteral("dd MMM, hh:mm"));
        item->setText(QStringLiteral("%1\n%2").arg(f.fileName, when));

        if (!f.thumbnail.isNull())
            item->setIcon(QIcon(f.thumbnail.scaled(32, 40, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation)));

        item->setToolTip(f.filePath);
    }
}

void RecentView::onItemActivated(QListWidgetItem* item)
{
    if (!item) return;
    emit fileActivated(item->data(Qt::UserRole).toString());
}
