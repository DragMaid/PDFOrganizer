#include "tagmanagerdialog.h"
#include "api/apiclient.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TagManagerDialog::TagManagerDialog(ApiClient* api, int groupId,
                                   const QString& groupName, QWidget* parent)
    : QDialog(parent)
    , m_api(api)
    , m_groupId(groupId)
    , m_groupName(groupName)
{
    setWindowTitle(QStringLiteral("Manage Tags — %1").arg(groupName));
    setMinimumSize(340, 440);
    buildUi();
    reload();
}

void TagManagerDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("These tags belong to '%1' and are visible to everyone "
                       "in that group.").arg(m_groupName), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_tagList = new QListWidget(this);
    root->addWidget(m_tagList, 1);

    // ── "Add new tag" row ─────────────────────────────────────────────────────
    auto* addRow = new QHBoxLayout;
    m_newTagEdit = new QLineEdit(this);
    m_newTagEdit->setPlaceholderText(QStringLiteral("New tag name…"));
    m_addBtn = new QPushButton(QStringLiteral("Add"), this);

    addRow->addWidget(m_newTagEdit);
    addRow->addWidget(m_addBtn);
    root->addLayout(addRow);

    auto* buttonRow = new QHBoxLayout;
    m_renameBtn = new QPushButton(QStringLiteral("Rename Selected"), this);
    m_delBtn    = new QPushButton(QStringLiteral("Delete Selected"), this);
    m_delBtn->setObjectName(QStringLiteral("dangerButton"));
    buttonRow->addWidget(m_renameBtn);
    buttonRow->addWidget(m_delBtn);
    root->addLayout(buttonRow);

    // ── Close ─────────────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);

    // ── Signals ───────────────────────────────────────────────────────────────
    connect(m_addBtn,     &QPushButton::clicked,     this, &TagManagerDialog::onAddTag);
    connect(m_newTagEdit, &QLineEdit::returnPressed, this, &TagManagerDialog::onAddTag);
    connect(m_renameBtn,  &QPushButton::clicked,     this, &TagManagerDialog::onRenameTag);
    connect(m_delBtn,     &QPushButton::clicked,     this, &TagManagerDialog::onDeleteTag);
    connect(m_tagList,    &QListWidget::itemDoubleClicked,
            this, &TagManagerDialog::onRenameTag);
}

void TagManagerDialog::reload()
{
    m_api->listGroupTags(
        m_groupId,
        [this](const QList<ApiTag>& tags) {
            m_tags = tags;
            const int previous = selectedTag().id;

            m_tagList->clear();
            for (const ApiTag& tag : m_tags) {
                auto* item = new QListWidgetItem(tag.name, m_tagList);
                item->setData(Qt::UserRole, tag.id);
                if (tag.id == previous)
                    m_tagList->setCurrentItem(item);
            }
        },
        [this](const ApiError& error) { showError(error); });
}

ApiTag TagManagerDialog::selectedTag() const
{
    QListWidgetItem* item = m_tagList ? m_tagList->currentItem() : nullptr;
    if (!item)
        return {};

    const int id = item->data(Qt::UserRole).toInt();
    for (const ApiTag& tag : m_tags) {
        if (tag.id == id)
            return tag;
    }
    return {};
}

void TagManagerDialog::showError(const ApiError& error)
{
    QMessageBox::warning(this, error.title(), error.message);
}

void TagManagerDialog::onAddTag()
{
    const QString name = m_newTagEdit->text().trimmed();
    if (name.isEmpty()) return;

    // Deliberately no duplicate check: the backend returns the existing tag
    // rather than failing, so a teammate having just added it is a non-event.
    m_api->createTag(
        m_groupId, name,
        [this](const ApiTag&) {
            m_newTagEdit->clear();
            reload();
            emit tagsChanged();
        },
        [this](const ApiError& error) { showError(error); });
}

void TagManagerDialog::onRenameTag()
{
    const ApiTag tag = selectedTag();
    if (tag.id < 0) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename Tag"), QStringLiteral("New name:"),
        QLineEdit::Normal, tag.name, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == tag.name)
        return;

    m_api->renameTag(
        m_groupId, tag.id, name.trimmed(),
        [this](const ApiTag&) {
            reload();
            emit tagsChanged();
        },
        [this](const ApiError& error) { showError(error); });
}

void TagManagerDialog::onDeleteTag()
{
    const ApiTag tag = selectedTag();
    if (tag.id < 0) return;

    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Delete Tag"),
        QStringLiteral("Delete tag '%1'?\n\nIt is removed from every file in "
                       "'%2', for everyone in the group.")
            .arg(tag.name, m_groupName),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply != QMessageBox::Yes)
        return;

    m_api->deleteTag(
        m_groupId, tag.id,
        [this]() {
            reload();
            emit tagsChanged();
        },
        [this](const ApiError& error) { showError(error); });
}
