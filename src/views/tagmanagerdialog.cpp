#include "tagmanagerdialog.h"
#include "api/apiclient.h"
#include "controllers/tagcontroller.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TagManagerDialog::TagManagerDialog(ApiClient* api, TagController* tagCtrl,
                                   const QString& folderPath, int groupId,
                                   const QString& groupName, QWidget* parent)
    : QDialog(parent)
    , m_api(api)
    , m_tagCtrl(tagCtrl)
    , m_folderPath(folderPath)
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

    const QString hintText =
        m_groupId >= 0
            ? QStringLiteral("These tags belong to '%1' and are visible to "
                             "everyone in that group.").arg(m_groupName)
            : QStringLiteral("These tags aren't shared with anyone yet — "
                             "'%1' has no group until it finishes syncing. "
                             "They'll go up automatically once it does.")
                  .arg(m_groupName);
    auto* hint = new QLabel(hintText, this);
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
    if (m_groupId < 0) {
        // No group to ask yet — draw straight from the local mirror. Every
        // tag shown here is fully editable; it just has nobody to tell about
        // an edit until this folder's group exists.
        const QString previous = m_tagList->currentItem()
                                     ? m_tagList->currentItem()->text()
                                     : QString();
        m_tags.clear();
        m_tagList->clear();
        for (const QString& name : m_tagCtrl->allTagNames()) {
            auto* item = new QListWidgetItem(name, m_tagList);
            if (name == previous)
                m_tagList->setCurrentItem(item);
        }
        return;
    }

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

QString TagManagerDialog::selectedTagName() const
{
    QListWidgetItem* item = m_tagList ? m_tagList->currentItem() : nullptr;
    return item ? item->text() : QString();
}

void TagManagerDialog::showError(const ApiError& error)
{
    QMessageBox::warning(this, error.title(), error.message);
}

void TagManagerDialog::onAddTag()
{
    const QString name = m_newTagEdit->text().trimmed();
    if (name.isEmpty()) return;

    // Local mirror first, always — this is what makes the tag exist and show
    // up immediately regardless of group or network state. Idempotent: a name
    // already in the local vocabulary is simply left alone.
    m_tagCtrl->createTag(name);
    m_newTagEdit->clear();
    reload();
    emit tagsChanged();
    emit vocabularyOpPending(TagVocabOp::Create, name, {});
}

void TagManagerDialog::onRenameTag()
{
    const QString oldName = selectedTagName();
    if (oldName.isEmpty()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename Tag"), QStringLiteral("New name:"),
        QLineEdit::Normal, oldName, &ok);
    const QString trimmed = name.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == oldName)
        return;

    if (!m_tagCtrl->renameTag(oldName, trimmed))
        return; // name already taken locally — TagController declined it

    reload();
    emit tagsChanged();
    emit vocabularyOpPending(TagVocabOp::Rename, oldName, trimmed);
}

void TagManagerDialog::onDeleteTag()
{
    const QString name = selectedTagName();
    if (name.isEmpty()) return;

    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Delete Tag"),
        QStringLiteral("Delete tag '%1'?\n\nIt is removed from every file "
                       "that carries it, for everyone in the group.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply != QMessageBox::Yes)
        return;

    if (!m_tagCtrl->deleteTag(name))
        return;

    reload();
    emit tagsChanged();
    emit vocabularyOpPending(TagVocabOp::Delete, name, {});
}
