#include "tagmanagerdialog.h"
#include "models/tagmodel.h"
#include "controllers/tagcontroller.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QInputDialog>

TagManagerDialog::TagManagerDialog(TagModel*      tagModel,
                                   TagController* tagCtrl,
                                   QWidget*       parent)
    : QDialog(parent)
    , m_tagModel(tagModel)
    , m_tagCtrl(tagCtrl)
{
    setWindowTitle(QStringLiteral("Manage Tags"));
    setMinimumSize(320, 420);
    buildUi();
}

void TagManagerDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    root->addWidget(new QLabel(QStringLiteral(
        "Double-click a tag to rename it.")));

    m_tagList = new QListView(this);
    m_tagList->setModel(m_tagModel);
    m_tagList->setEditTriggers(QAbstractItemView::DoubleClicked);
    root->addWidget(m_tagList, 1);

    // ── "Add new tag" row ─────────────────────────────────────────────────────
    auto* addRow = new QHBoxLayout;
    m_newTagEdit = new QLineEdit(this);
    m_newTagEdit->setPlaceholderText(QStringLiteral("New tag name…"));
    m_addBtn = new QPushButton(QStringLiteral("Add"), this);

    addRow->addWidget(m_newTagEdit);
    addRow->addWidget(m_addBtn);
    root->addLayout(addRow);

    // ── Delete button ─────────────────────────────────────────────────────────
    m_delBtn = new QPushButton(QStringLiteral("Delete Selected Tag"), this);
    m_delBtn->setObjectName(QStringLiteral("dangerButton"));
    root->addWidget(m_delBtn);

    // ── Close ─────────────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    // ── Signals ───────────────────────────────────────────────────────────────
    connect(m_addBtn,    &QPushButton::clicked, this, &TagManagerDialog::onAddTag);
    connect(m_newTagEdit, &QLineEdit::returnPressed, this, &TagManagerDialog::onAddTag);
    connect(m_delBtn,    &QPushButton::clicked, this, &TagManagerDialog::onDeleteTag);
}

void TagManagerDialog::onAddTag()
{
    const QString name = m_newTagEdit->text().trimmed();
    if (name.isEmpty()) return;

    if (!m_tagCtrl->createTag(name))
        QMessageBox::warning(this, QStringLiteral("Duplicate Tag"),
                             QStringLiteral("A tag named '%1' already exists.").arg(name));
    else
        m_newTagEdit->clear();
}

void TagManagerDialog::onDeleteTag()
{
    const QModelIndex idx = m_tagList->currentIndex();
    if (!idx.isValid()) return;

    const QString tag = idx.data().toString();

    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Delete Tag"),
        QStringLiteral("Delete tag '%1'?\n\nThis removes it from every tracked PDF.").arg(tag),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply == QMessageBox::Yes)
        m_tagCtrl->deleteTag(tag);
}
