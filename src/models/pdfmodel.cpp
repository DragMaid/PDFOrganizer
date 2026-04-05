#include "pdfmodel.h"
#include <QFont>
#include <QColor>

PdfModel::PdfModel(QObject* parent)
    : QAbstractTableModel(parent)
{}

// ── QAbstractTableModel ───────────────────────────────────────────────────────

int PdfModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_files.size();
}

int PdfModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant PdfModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.size())
        return {};

    const PdfFile& f = m_files.at(index.row());

    // ── Custom roles (column-independent) ─────────────────────────────────────
    switch (role) {
    case FilePathRole:    return f.filePath;
    case FileNameRole:    return f.fileName;
    case FolderPathRole:  return f.folderPath;
    case TagsRole:        return QVariant::fromValue(f.tags);
    case LastOpenedRole:  return f.lastOpened;
    case LastModifiedRole:return f.lastModified;
    case FileSizeRole:    return f.fileSizeBytes;
    case PageCountRole:   return f.pageCount;
    case ThumbnailRole:   return QVariant::fromValue(f.thumbnail);
    case DatabaseIdRole:  return f.id;
    case PdfFileRole:     return QVariant::fromValue(f);
    default: break;
    }

    // ── Standard roles ────────────────────────────────────────────────────────
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFileName:   return f.fileName;
        case ColFolder:     return f.folderPath;
        case ColTags:       return f.tags.join(QLatin1String(", "));
        case ColLastOpened:
            return f.lastOpened.isValid()
                ? QLocale().toString(f.lastOpened, QLocale::ShortFormat)
                : QStringLiteral("Never");
        case ColSize:       return f.formattedSize();
        }
    }

    if (role == Qt::ToolTipRole)
        return f.filePath;

    if (role == Qt::DecorationRole && index.column() == ColFileName)
        return !f.thumbnail.isNull() ? f.thumbnail.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                     : QVariant{};

    return {};
}

QVariant PdfModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColFileName:   return QStringLiteral("File Name");
    case ColFolder:     return QStringLiteral("Folder");
    case ColTags:       return QStringLiteral("Tags");
    case ColLastOpened: return QStringLiteral("Last Opened");
    case ColSize:       return QStringLiteral("Size");
    }
    return {};
}

QHash<int, QByteArray> PdfModel::roleNames() const
{
    auto roles = QAbstractTableModel::roleNames();
    roles[FilePathRole]    = "filePath";
    roles[FileNameRole]    = "fileName";
    roles[FolderPathRole]  = "folderPath";
    roles[TagsRole]        = "tags";
    roles[LastOpenedRole]  = "lastOpened";
    roles[LastModifiedRole]= "lastModified";
    roles[FileSizeRole]    = "fileSize";
    roles[PageCountRole]   = "pageCount";
    roles[ThumbnailRole]   = "thumbnail";
    roles[DatabaseIdRole]  = "databaseId";
    roles[PdfFileRole]     = "pdfFile";
    return roles;
}

// ── Mutation ──────────────────────────────────────────────────────────────────

void PdfModel::resetFiles(const QList<PdfFile>& files)
{
    beginResetModel();
    m_files = files;
    endResetModel();
}

bool PdfModel::addFile(const PdfFile& file)
{
    if (rowForPath(file.filePath) >= 0)
        return false;                       // already present

    const int row = m_files.size();
    beginInsertRows({}, row, row);
    m_files.append(file);
    endInsertRows();
    return true;
}

bool PdfModel::removeFile(const QString& filePath)
{
    const int row = rowForPath(filePath);
    if (row < 0) return false;

    beginRemoveRows({}, row, row);
    m_files.removeAt(row);
    endRemoveRows();
    return true;
}

bool PdfModel::updateFile(const PdfFile& updated)
{
    const int row = rowForPath(updated.filePath);
    if (row < 0) return false;

    m_files[row] = updated;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
    emit fileUpdated(updated.filePath);
    return true;
}

void PdfModel::setThumbnail(const QString& filePath, const QPixmap& thumb)
{
    const int row = rowForPath(filePath);
    if (row < 0) return;

    m_files[row].thumbnail = thumb;
    const QModelIndex idx = index(row, ColFileName);
    emit dataChanged(idx, idx, {ThumbnailRole, Qt::DecorationRole});
}

// ── Query ─────────────────────────────────────────────────────────────────────

PdfFile PdfModel::fileAt(int row) const
{
    if (row < 0 || row >= m_files.size()) return {};
    return m_files.at(row);
}

PdfFile PdfModel::fileByPath(const QString& path) const
{
    for (const auto& f : m_files)
        if (f.filePath == path) return f;
    return {};
}

int PdfModel::rowForPath(const QString& path) const
{
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files.at(i).filePath == path) return i;
    return -1;
}
