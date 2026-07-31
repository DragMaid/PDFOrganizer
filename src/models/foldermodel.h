#pragma once
#include <QAbstractListModel>
#include <QAbstractItemModel>
#include <QHash>
#include <QStringList>

/**
 * @brief Holds the list of user-added root folder paths.
 *
 * Root folders are the top-level directories the user has chosen to watch.
 * Subfolders are discovered by FolderWatcher and their PDFs are owned by PdfModel.
 */
class FolderModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit FolderModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data    (const QModelIndex& index, int role = Qt::DisplayRole) const override;

    bool addFolder   (const QString& path);
    bool removeFolder(const QString& path);
    void resetFolders(const QStringList& paths);

    [[nodiscard]] bool        hasFolder(const QString& path) const;
    [[nodiscard]] QStringList allFolders()                   const { return m_folders; }

signals:
    void folderAdded  (const QString& path);
    void folderRemoved(const QString& path);

private:
    QStringList m_folders;
};

// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief Hierarchical model of folder structure with subfolders.
 *
 * Displays folders as a tree: root folders at top level, with subfolders
 * nested beneath. Used by FolderPanel to display folder hierarchy.
 */
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit FolderTreeModel(QObject* parent = nullptr);
    ~FolderTreeModel();

    int      rowCount   (const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data       (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QModelIndex index   (int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent  (const QModelIndex& index) const override;

    bool addRootFolder   (const QString& path);
    bool removeRootFolder(const QString& path);

    [[nodiscard]] QString folderPathForIndex(const QModelIndex& index) const;

    /// How far each directory's group is out of sync, as a short suffix drawn
    /// after the folder's name ("↓3", "↑1 ↓2"). MainWindow owns the counting;
    /// this model only knows how to show the answer. Paths with no entry get
    /// no suffix, which is what an up-to-date folder looks like.
    void setSyncBadges(const QHash<QString, QString>& badges,
                       const QHash<QString, QString>& tooltips);

signals:
    void folderAdded  (const QString& path);
    void folderRemoved(const QString& path);

private:
    struct FolderNode {
        QString name;           // directory name only
        QString absolutePath;   // full path
        QList<FolderNode*> children;
        FolderNode* parent = nullptr;
    };

    void buildTreeForRoot(FolderNode* root, const QString& rootPath);
    void clearTree();
    FolderNode* nodeFromIndex(const QModelIndex& index) const;
    /// Repaint every row; badges can change on any node at once, and the tree
    /// is small enough that finding the individual indices is not worth it.
    void refreshAllRows(const QModelIndex& parent = {});

    QList<FolderNode*> m_roots;
    QHash<QString, QString> m_badges;        ///< absolute path → "↑1 ↓2"
    QHash<QString, QString> m_badgeTooltips; ///< absolute path → what it means
};
