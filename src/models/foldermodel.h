#pragma once
#include <QAbstractListModel>
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
