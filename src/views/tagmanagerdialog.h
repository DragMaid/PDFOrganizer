#pragma once
#include <QDialog>
#include <QList>

#include "api/apitypes.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class ApiClient;

/**
 * @brief Modal dialog for managing one group's tag vocabulary.
 *
 * Tags belong to a group, so this dialog always edits a specific group's list
 * — the one selected in the toolbar. Creating a tag that already exists is a
 * no-op rather than an error, matching the backend: two people adding the same
 * tag at once should not see a failure.
 *
 * Renaming is the one operation that can genuinely conflict (merging two tags
 * would lose assignments), so that reports an error.
 */
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    TagManagerDialog(ApiClient* api, int groupId, const QString& groupName,
                     QWidget* parent = nullptr);

signals:
    /// Emitted whenever the vocabulary changed, so the caller can refresh the
    /// sidebar chips and the local mirror.
    void tagsChanged();

private slots:
    void onAddTag();
    void onRenameTag();
    void onDeleteTag();

private:
    void buildUi();
    void reload();
    void showError(const ApiError& error);
    [[nodiscard]] ApiTag selectedTag() const;

    ApiClient* m_api;
    int        m_groupId;
    QString    m_groupName;

    QList<ApiTag> m_tags;

    QListWidget* m_tagList    = nullptr;
    QLineEdit*   m_newTagEdit = nullptr;
    QPushButton* m_addBtn     = nullptr;
    QPushButton* m_renameBtn  = nullptr;
    QPushButton* m_delBtn     = nullptr;
};
