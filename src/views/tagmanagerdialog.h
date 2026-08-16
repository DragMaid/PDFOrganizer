#pragma once
#include <QDialog>
#include <QList>

#include "api/apitypes.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class ApiClient;
class TagController;

/// A create, rename, or delete of a tag *name* — as opposed to assigning one
/// to a file. Lives here (rather than nested in MainWindow) because this
/// dialog is the other side that needs to name it: it writes the local
/// mirror itself and reports the edit outward via vocabularyOpPending()
/// rather than reaching into MainWindow to push it.
enum class TagVocabOp { Create, Rename, Delete };

/**
 * @brief Modal dialog for managing one group's tag vocabulary.
 *
 * Tags belong to a group, so the *online* half of this dialog talks about a
 * specific group's list — the one selected in the toolbar. But the
 * vocabulary itself lives in the local mirror (TagController) first: every
 * edit is written there immediately, regardless of whether @p groupId is a
 * real group yet, and is only pushed to the server (or queued, when that is
 * not possible right now) as a second step the caller performs in response
 * to vocabularyOpPending(). This mirrors how tag *assignment* already works
 * elsewhere — write local, then try to send.
 *
 * Creating a tag that already exists is a no-op rather than an error,
 * matching the backend: two people adding the same tag at once should not
 * see a failure. Renaming is the one operation that can genuinely conflict
 * (merging two tags would lose assignments), so that reports an error.
 */
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    /// @p groupId may be -1 — @p folderPath has no backend group yet (never
    /// signed in, or still being created) — in which case every edit is
    /// local-only until vocabularyOpPending()'s caller can push it.
    TagManagerDialog(ApiClient* api, TagController* tagCtrl,
                     const QString& folderPath, int groupId,
                     const QString& groupName, QWidget* parent = nullptr);

signals:
    /// Emitted whenever the vocabulary changed, so the caller can refresh the
    /// sidebar chips and the local mirror.
    void tagsChanged();
    /// A vocabulary edit was just written to the local mirror and needs
    /// pushing to (or queuing for) @p tagName's group. The caller — not this
    /// dialog — owns the network attempt and the offline queue, the same
    /// division MainWindow::pushFileTags() already has with its own caller.
    void vocabularyOpPending(TagVocabOp op, const QString& tagName,
                             const QString& newName);

private slots:
    void onAddTag();
    void onRenameTag();
    void onDeleteTag();

private:
    void buildUi();
    void reload();
    void showError(const ApiError& error);
    [[nodiscard]] ApiTag selectedTag() const;
    /// The name of whatever is selected in the list, online or offline —
    /// what onRenameTag()/onDeleteTag() actually need, since an offline-only
    /// tag has no ApiTag id for selectedTag() to return.
    [[nodiscard]] QString selectedTagName() const;

    ApiClient*      m_api;
    TagController*  m_tagCtrl;
    QString         m_folderPath;
    int             m_groupId;
    QString         m_groupName;

    QList<ApiTag> m_tags;   ///< Online mode only (m_groupId >= 0).

    QListWidget* m_tagList    = nullptr;
    QLineEdit*   m_newTagEdit = nullptr;
    QPushButton* m_addBtn     = nullptr;
    QPushButton* m_renameBtn  = nullptr;
    QPushButton* m_delBtn     = nullptr;
};
