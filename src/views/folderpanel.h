#pragma once
#include <QWidget>
#include <QStringList>

class QListView;
class QListWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class FolderModel;
class TagModel;

/**
 * @brief Left-side panel: root folder list and tag filter chips.
 *
 * Signals emitted upward to MainWindow:
 *  folderSelected  – user clicked a root folder (filter by folder)
 *  tagsSelected    – user toggled tag filter chips
 *  addFolderRequested – "+" button clicked
 *  removeFolderRequested – "-" button or context menu on folder
 */
class FolderPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FolderPanel(FolderModel* folderModel,
                         TagModel*    tagModel,
                         QWidget*     parent = nullptr);

    void refresh();   ///< Re-draw tag chips from TagModel

signals:
    void folderSelected          (const QString& folderPath);  ///< "" = All
    void tagsSelected            (const QStringList& tags);
    void addFolderRequested      ();
    void removeFolderRequested   (const QString& folderPath);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent     (QDropEvent* e)      override;

private slots:
    void onFolderClicked(const QModelIndex& idx);
    void onTagToggled   (QWidget* chip, bool checked);

private:
    void buildUi();
    void buildTagChips();
    QWidget* makeTagChip(const QString& tag);

    FolderModel* m_folderModel;
    TagModel*    m_tagModel;

    QListView*    m_folderList   = nullptr;
    QWidget*      m_tagContainer = nullptr;
    QStringList   m_activeTags;
};
