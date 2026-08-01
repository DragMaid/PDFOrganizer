#pragma once
#include <QWidget>
#include <QStringList>

class QTreeView;
class QListWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class FolderTreeModel;
class TagModel;

/**
 * @brief Left-side panel: root folder list and tag filter chips.
 *
 * Signals emitted upward to MainWindow:
 *  folderSelected  – user clicked a folder in the tree (filter by folder)
 *  tagsSelected    – user toggled tag filter chips
 *  addFolderRequested – "+" button clicked
 *  removeFolderRequested – "-" button or context menu on folder
 */
class FolderPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FolderPanel(FolderTreeModel* folderTreeModel,
                         TagModel*        tagModel,
                         QWidget*         parent = nullptr);

    void refresh();   ///< Re-draw tag chips from TagModel

    /// Drop every tag filter and say so, so callers do not have to reach into
    /// the chips. Used when a tag is deleted: a filter pinned to a tag that no
    /// longer exists matches nothing, and an empty file list with no visible
    /// cause is indistinguishable from having lost the files.
    void clearTagFilter();

    [[nodiscard]] QStringList activeTags() const { return m_activeTags; }

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

    FolderTreeModel* m_folderTreeModel;
    TagModel*        m_tagModel;

    QTreeView*    m_folderTree   = nullptr;
    QWidget*      m_tagContainer = nullptr;
    QStringList   m_activeTags;
};
