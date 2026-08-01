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
    case PendingRemovalRole: return m_pendingRemoval.contains(f.filePath);
    case SyncStateRole:   return static_cast<int>(m_syncMarks.value(f.filePath).state);
    case SyncDetailRole:  return m_syncMarks.value(f.filePath).detail;
    case PendingMetaRole: return m_pendingMeta.contains(f.filePath);
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

    if (role == Qt::ToolTipRole) {
        // The path answers "which file"; the sync line answers the question the
        // coloured dot on the row raises but cannot spell out.
        QStringList lines{f.filePath};
        const QString detail = m_syncMarks.value(f.filePath).detail;
        if (!detail.isEmpty())
            lines << detail;
        if (m_pendingMeta.contains(f.filePath)) {
            lines << QStringLiteral("Tags or notes written here are waiting to "
                                    "go up with this file.");
        }
        return lines.join(QLatin1Char('\n'));
    }

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
    roles[PendingRemovalRole] = "pendingRemoval";
    roles[SyncStateRole]   = "syncState";
    roles[SyncDetailRole]  = "syncDetail";
    roles[PendingMetaRole] = "pendingMeta";
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

void PdfModel::setPendingRemoval(const QString& filePath, bool pending)
{
    if (m_pendingRemoval.contains(filePath) == pending)
        return;

    if (pending)
        m_pendingRemoval.insert(filePath);
    else
        m_pendingRemoval.remove(filePath);

    // A file whose row is already gone can still be mid-removal — the local
    // copy may have been deleted first — so the set is updated either way and
    // only the repaint needs a row.
    const int row = rowForPath(filePath);
    if (row < 0)
        return;

    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {PendingRemovalRole});
}

bool PdfModel::isPendingRemoval(const QString& filePath) const
{
    return m_pendingRemoval.contains(filePath);
}

void PdfModel::setSyncState(const QString& filePath, SyncState state,
                            const QString& detail)
{
    const SyncMark previous = m_syncMarks.value(filePath);
    if (previous.state == state && previous.detail == detail)
        return;

    if (state == SyncUnknown && detail.isEmpty())
        m_syncMarks.remove(filePath);
    else
        m_syncMarks.insert(filePath, SyncMark{state, detail});

    const int row = rowForPath(filePath);
    if (row < 0)
        return;
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {SyncStateRole, SyncDetailRole, Qt::ToolTipRole});
}

PdfModel::SyncState PdfModel::syncState(const QString& filePath) const
{
    return m_syncMarks.value(filePath).state;
}

void PdfModel::setPendingMeta(const QString& filePath, bool pending)
{
    if (m_pendingMeta.contains(filePath) == pending)
        return;

    if (pending)
        m_pendingMeta.insert(filePath);
    else
        m_pendingMeta.remove(filePath);

    const int row = rowForPath(filePath);
    if (row < 0)
        return;
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {PendingMetaRole, Qt::ToolTipRole});
}

void PdfModel::clearSyncStates()
{
    if (m_syncMarks.isEmpty() && m_pendingMeta.isEmpty())
        return;

    m_syncMarks.clear();
    m_pendingMeta.clear();
    if (m_files.isEmpty())
        return;
    emit dataChanged(index(0, 0), index(m_files.size() - 1, ColCount - 1),
                     {SyncStateRole, SyncDetailRole, PendingMetaRole,
                      Qt::ToolTipRole});
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
