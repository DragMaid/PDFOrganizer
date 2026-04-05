#pragma once
#include <QDialog>

class QListView;
class QLineEdit;
class QPushButton;
class TagModel;
class TagController;

/**
 * @brief Modal dialog for managing the global tag list.
 *
 * Allows the user to create new tags, rename existing ones (double-click),
 * and delete tags (which cascades to all PDF assignments via TagController).
 */
class TagManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagManagerDialog(TagModel*      tagModel,
                              TagController* tagCtrl,
                              QWidget*       parent = nullptr);

private slots:
    void onAddTag();
    void onDeleteTag();

private:
    void buildUi();

    TagModel*      m_tagModel;
    TagController* m_tagCtrl;

    QListView*   m_tagList  = nullptr;
    QLineEdit*   m_newTagEdit = nullptr;
    QPushButton* m_addBtn   = nullptr;
    QPushButton* m_delBtn   = nullptr;
};
