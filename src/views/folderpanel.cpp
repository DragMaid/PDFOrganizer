#include "folderpanel.h"
#include "models/foldermodel.h"
#include "models/tagmodel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QMenu>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QLayout> 

FolderPanel::FolderPanel(FolderModel* folderModel,
                         TagModel*    tagModel,
                         QWidget*     parent)
    : QWidget(parent)
    , m_folderModel(folderModel)
    , m_tagModel(tagModel)
{
    setAcceptDrops(true);
    setMinimumWidth(200);
    setMaximumWidth(280);
    buildUi();

    // Re-build tag chips whenever the tag model changes
    connect(m_tagModel, &QAbstractItemModel::modelReset,   this, &FolderPanel::buildTagChips);
    connect(m_tagModel, &QAbstractItemModel::rowsInserted, this, &FolderPanel::buildTagChips);
    connect(m_tagModel, &QAbstractItemModel::rowsRemoved,  this, &FolderPanel::buildTagChips);
}

void FolderPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Folders section ───────────────────────────────────────────────────────
    auto* folderHeader = new QWidget;
    folderHeader->setObjectName(QStringLiteral("sectionHeader"));
    auto* fhLayout = new QHBoxLayout(folderHeader);
    fhLayout->setContentsMargins(12, 8, 8, 8);

    auto* folderLabel = new QLabel(QStringLiteral("FOLDERS"));
    folderLabel->setObjectName(QStringLiteral("sectionLabel"));

    auto* addBtn = new QPushButton(QStringLiteral("+"));
    addBtn->setObjectName(QStringLiteral("iconButton"));
    addBtn->setFixedSize(22, 22);
    addBtn->setToolTip(QStringLiteral("Add folder (or drag && drop)"));
    connect(addBtn, &QPushButton::clicked, this, &FolderPanel::addFolderRequested);

    fhLayout->addWidget(folderLabel);
    fhLayout->addStretch();
    fhLayout->addWidget(addBtn);

    // "All" entry at top of folder list
    auto* allItem = new QPushButton(QStringLiteral("  All PDFs"));
    allItem->setObjectName(QStringLiteral("allItem"));
    allItem->setFlat(true);
    allItem->setCheckable(true);
    allItem->setChecked(true);
    allItem->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; padding: 6px 12px; color: #e0e3e8; }"
        "QPushButton:checked { background: #2b4060; border-radius: 4px; }"
    ));
    connect(allItem, &QPushButton::clicked, this,
            [this]() { emit folderSelected(QString{}); });

    m_folderList = new QListView;
    m_folderList->setModel(m_folderModel);
    m_folderList->setFrameShape(QFrame::NoFrame);
    m_folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_folderList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_folderList->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_folderList, &QListView::clicked, this, &FolderPanel::onFolderClicked);
    connect(m_folderList, &QListView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex idx = m_folderList->indexAt(pos);
                if (!idx.isValid()) return;
                const QString path = idx.data(Qt::UserRole).toString();

                QMenu menu(this);
                QAction* removeAct = menu.addAction(QStringLiteral("Remove Folder"));
                if (menu.exec(m_folderList->viewport()->mapToGlobal(pos)) == removeAct)
                    emit removeFolderRequested(path);
            });

    // ── Tags section ──────────────────────────────────────────────────────────
    auto* tagHeader = new QWidget;
    tagHeader->setObjectName(QStringLiteral("sectionHeader"));
    auto* thLayout = new QHBoxLayout(tagHeader);
    thLayout->setContentsMargins(12, 8, 8, 8);
    auto* tagLabel = new QLabel(QStringLiteral("FILTER BY TAG"));
    tagLabel->setObjectName(QStringLiteral("sectionLabel"));
    thLayout->addWidget(tagLabel);

    m_tagContainer = new QWidget;
    auto* tagScroll = new QScrollArea;
    tagScroll->setWidget(m_tagContainer);
    tagScroll->setWidgetResizable(true);
    tagScroll->setFrameShape(QFrame::NoFrame);
    tagScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // ── Assemble ──────────────────────────────────────────────────────────────
    root->addWidget(folderHeader);
    root->addWidget(allItem);
    root->addWidget(m_folderList);
    root->addWidget(tagHeader);
    root->addWidget(tagScroll, 1);

    buildTagChips();
}

void FolderPanel::buildTagChips()
{
    // Clear existing layout and children
    delete m_tagContainer->layout();
    const auto children = m_tagContainer->findChildren<QWidget*>(
                              QString(), Qt::FindDirectChildrenOnly);
    for (auto* w : children) w->deleteLater();

    auto* layout = new QVBoxLayout(m_tagContainer);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    const QStringList tags = m_tagModel->allTags();
    for (const QString& tag : tags) {
        auto* chip = new QPushButton(tag, m_tagContainer);
        chip->setCheckable(true);
        chip->setChecked(m_activeTags.contains(tag));
        chip->setObjectName(QStringLiteral("tagChip"));

        // Deterministic colour per tag
        const int hue = static_cast<int>(qHash(tag) % 360);
        const QColor c = QColor::fromHsl(hue, 120, 55);
        chip->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #2d3035; border: 1px solid %1;"
            "  border-radius: 12px; padding: 3px 10px;"
            "  color: %2; text-align: left;"
            "}"
            "QPushButton:checked {"
            "  background: %1; color: white;"
            "}"
        ).arg(c.name(), c.lighter(160).name()));

        connect(chip, &QPushButton::toggled, this,
                [this, tag](bool checked) {
                    if (checked)
                        m_activeTags.append(tag);
                    else
                        m_activeTags.removeAll(tag);
                    emit tagsSelected(m_activeTags);
                });
        layout->addWidget(chip);
    }
    layout->addStretch();
}

void FolderPanel::refresh()
{
    buildTagChips();
}

void FolderPanel::onFolderClicked(const QModelIndex& idx)
{
    emit folderSelected(idx.data(Qt::UserRole).toString());
}

// ── Drag and drop ─────────────────────────────────────────────────────────────

void FolderPanel::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void FolderPanel::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QDir(path).exists())
            emit addFolderRequested();   // MainWindow will prompt/verify
    }
    e->acceptProposedAction();
}

void FolderPanel::onTagToggled(QWidget*, bool)
{
    // Handled inline in buildTagChips lambdas
}
