#include "tagmodel.h"

TagModel::TagModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int TagModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_tags.size();
}

QVariant TagModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_tags.size())
        return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return m_tags.at(index.row());
    return {};
}

bool TagModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;

    const QString newName = value.toString().trimmed();
    if (newName.isEmpty() || hasTag(newName))
        return false;

    const QString old = m_tags.at(index.row());
    m_tags[index.row()] = newName;
    m_tags.sort(Qt::CaseInsensitive);
    emit dataChanged(this->index(0), this->index(m_tags.size() - 1));
    emit tagRenamed(old, newName);
    return true;
}

Qt::ItemFlags TagModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TagModel::resetTags(const QStringList& tags)
{
    beginResetModel();
    m_tags = tags;
    m_tags.sort(Qt::CaseInsensitive);
    endResetModel();
}

bool TagModel::addTag(const QString& tag)
{
    const QString trimmed = tag.trimmed();
    if (trimmed.isEmpty() || hasTag(trimmed))
        return false;

    // Insert sorted
    int insertAt = 0;
    while (insertAt < m_tags.size() &&
           m_tags.at(insertAt).compare(trimmed, Qt::CaseInsensitive) < 0)
        ++insertAt;

    beginInsertRows({}, insertAt, insertAt);
    m_tags.insert(insertAt, trimmed);
    endInsertRows();

    emit tagAdded(trimmed);
    return true;
}

bool TagModel::removeTag(const QString& tag)
{
    const int row = m_tags.indexOf(tag, 0);
    if (row < 0) return false;

    beginRemoveRows({}, row, row);
    m_tags.removeAt(row);
    endRemoveRows();

    emit tagRemoved(tag);
    return true;
}

bool TagModel::renameTag(const QString& oldName, const QString& newName)
{
    return setData(index(indexOf(oldName)), newName, Qt::EditRole);
}

bool TagModel::hasTag(const QString& tag) const
{
    return m_tags.contains(tag, Qt::CaseInsensitive);
}
