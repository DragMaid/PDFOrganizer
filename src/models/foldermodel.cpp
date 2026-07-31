#include "foldermodel.h"
#include <QDir>
#include <QFont>
#include <QDirIterator>
#include <QFileInfo>
#include <functional>

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

// ────────────────────────────────────────────────────────────────────────────
// FolderTreeModel Implementation
// ────────────────────────────────────────────────────────────────────────────

FolderTreeModel::FolderTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{}

FolderTreeModel::~FolderTreeModel()
{
    clearTree();
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return m_roots.size();
    }

    const FolderNode* node = nodeFromIndex(parent);
    return node ? node->children.size() : 0;
}

int FolderTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};

    const FolderNode* node = nodeFromIndex(index);
    if (!node) return {};

    const QString badge = m_badges.value(node->absolutePath);

    switch (role) {
    case Qt::DisplayRole:
        // The badge rides along in the display text rather than in a role of
        // its own, so the stock tree delegate draws it without help.
        return badge.isEmpty() ? node->name
                               : QStringLiteral("%1  %2").arg(node->name, badge);
    case Qt::EditRole:
        return node->name;
    case Qt::ToolTipRole: {
        const QString explanation = m_badgeTooltips.value(node->absolutePath);
        return explanation.isEmpty()
            ? node->absolutePath
            : QStringLiteral("%1\n\n%2").arg(node->absolutePath, explanation);
    }
    case Qt::UserRole:
        return node->absolutePath;  // for programmatic access
    }
    return {};
}

void FolderTreeModel::setSyncBadges(const QHash<QString, QString>& badges,
                                    const QHash<QString, QString>& tooltips)
{
    if (badges == m_badges && tooltips == m_badgeTooltips)
        return;

    m_badges        = badges;
    m_badgeTooltips = tooltips;
    refreshAllRows();
}

void FolderTreeModel::refreshAllRows(const QModelIndex& parent)
{
    const int rows = rowCount(parent);
    if (rows == 0)
        return;

    emit dataChanged(index(0, 0, parent), index(rows - 1, 0, parent),
                     {Qt::DisplayRole, Qt::ToolTipRole});
    for (int row = 0; row < rows; ++row)
        refreshAllRows(index(row, 0, parent));
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column < 0 || column >= columnCount(parent)) return {};

    FolderNode* parentNode = nullptr;
    if (!parent.isValid()) {
        if (row >= m_roots.size()) return {};
        parentNode = nullptr;
    } else {
        parentNode = nodeFromIndex(parent);
        if (!parentNode || row >= parentNode->children.size()) return {};
    }

    FolderNode* childNode = parent.isValid()
        ? parentNode->children.at(row)
        : m_roots.at(row);
    return createIndex(row, column, childNode);
}

QModelIndex FolderTreeModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) return {};

    const FolderNode* node = static_cast<FolderNode*>(index.internalPointer());
    if (!node || !node->parent) return {};

    const FolderNode* grandparent = node->parent->parent;
    int row = 0;

    if (grandparent) {
        row = grandparent->children.indexOf(node->parent);
    } else {
        row = m_roots.indexOf(node->parent);
    }

    if (row < 0) return {};
    return createIndex(row, 0, node->parent);
}

bool FolderTreeModel::addRootFolder(const QString& path)
{
    if (path.isEmpty()) return false;

    // Check if already exists
    for (const FolderNode* root : m_roots) {
        if (root->absolutePath == path) return false;
    }

    const int row = m_roots.size();
    beginInsertRows({}, row, row);

    FolderNode* root = new FolderNode();
    QFileInfo info(path);
    root->name = info.fileName();
    root->absolutePath = path;
    root->parent = nullptr;

    buildTreeForRoot(root, path);
    m_roots.append(root);

    endInsertRows();
    emit folderAdded(path);
    return true;
}

bool FolderTreeModel::removeRootFolder(const QString& path)
{
    int row = 0;
    for (FolderNode* root : m_roots) {
        if (root->absolutePath == path) {
            beginRemoveRows({}, row, row);
            m_roots.removeAt(row);
            delete root;
            endRemoveRows();
            emit folderRemoved(path);
            return true;
        }
        ++row;
    }
    return false;
}

QString FolderTreeModel::folderPathForIndex(const QModelIndex& index) const
{
    const FolderNode* node = nodeFromIndex(index);
    return node ? node->absolutePath : QString();
}

void FolderTreeModel::buildTreeForRoot(FolderNode* root, const QString& rootPath)
{
    QDir dir(rootPath);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);

    for (const QString& subfolderName : dir.entryList()) {
        const QString subfolderPath = dir.filePath(subfolderName);
        FolderNode* child = new FolderNode();
        child->name = subfolderName;
        child->absolutePath = subfolderPath;
        child->parent = root;

        // Recursively build subtree
        buildTreeForRoot(child, subfolderPath);

        root->children.append(child);
    }
}

void FolderTreeModel::clearTree()
{
    beginResetModel();
    for (FolderNode* root : m_roots) {
        // Recursive delete
        std::function<void(FolderNode*)> deleteNode = [&](FolderNode* node) {
            for (FolderNode* child : node->children) {
                deleteNode(child);
            }
            delete node;
        };
        deleteNode(root);
    }
    m_roots.clear();
    endResetModel();
}

FolderTreeModel::FolderNode* FolderTreeModel::nodeFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) return nullptr;
    return static_cast<FolderNode*>(index.internalPointer());
}
