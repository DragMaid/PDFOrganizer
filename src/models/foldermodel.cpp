#include "foldermodel.h"
#include <QDir>
#include <QFont>

FolderModel::FolderModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int FolderModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_folders.size();
}

QVariant FolderModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_folders.size())
        return {};

    const QString& path = m_folders.at(index.row());

    switch (role) {
    case Qt::DisplayRole: return QDir(path).dirName();
    case Qt::ToolTipRole: return path;
    case Qt::UserRole:    return path;   // full path for programmatic use
    }
    return {};
}

void FolderModel::resetFolders(const QStringList& paths)
{
    beginResetModel();
    m_folders = paths;
    endResetModel();
}

bool FolderModel::addFolder(const QString& path)
{
    if (path.isEmpty() || hasFolder(path))
        return false;

    const int row = m_folders.size();
    beginInsertRows({}, row, row);
    m_folders.append(path);
    endInsertRows();

    emit folderAdded(path);
    return true;
}

bool FolderModel::removeFolder(const QString& path)
{
    const int row = m_folders.indexOf(path);
    if (row < 0) return false;

    beginRemoveRows({}, row, row);
    m_folders.removeAt(row);
    endRemoveRows();

    emit folderRemoved(path);
    return true;
}

bool FolderModel::hasFolder(const QString& path) const
{
    return m_folders.contains(path);
}
