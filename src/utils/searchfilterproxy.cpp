#include "searchfilterproxy.h"
#include "models/pdfmodel.h"

SearchFilterProxy::SearchFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity  (Qt::CaseInsensitive);
}

void SearchFilterProxy::setSearchText(const QString& text)
{
    m_searchText = text.trimmed();
    invalidateFilter();
}

void SearchFilterProxy::setActiveTags(const QStringList& tags)
{
    m_activeTags = tags;
    invalidateFilter();
}

void SearchFilterProxy::setFolderFilter(const QString& folderPath)
{
    m_folderFilter = folderPath;
    invalidateFilter();
}

void SearchFilterProxy::clearFilters()
{
    m_searchText.clear();
    m_activeTags.clear();
    m_folderFilter.clear();
    invalidateFilter();
}

bool SearchFilterProxy::filterAcceptsRow(int sourceRow,
                                          const QModelIndex& sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);

    // ── Folder filter ─────────────────────────────────────────────────────────
    if (!m_folderFilter.isEmpty()) {
        const QString folder = idx.data(PdfModel::FolderPathRole).toString();
        if (!folder.startsWith(m_folderFilter))
            return false;
    }

    // ── Tag filter (AND) ──────────────────────────────────────────────────────
    if (!m_activeTags.isEmpty()) {
        const QStringList fileTags = idx.data(PdfModel::TagsRole).toStringList();
        for (const QString& required : m_activeTags) {
            if (!fileTags.contains(required, Qt::CaseInsensitive))
                return false;
        }
    }

    // ── Text search (name OR tags) ────────────────────────────────────────────
    if (!m_searchText.isEmpty()) {
        const QString name = idx.data(PdfModel::FileNameRole).toString();
        if (name.contains(m_searchText, Qt::CaseInsensitive))
            return true;

        const QStringList tags = idx.data(PdfModel::TagsRole).toStringList();
        for (const QString& tag : tags) {
            if (tag.contains(m_searchText, Qt::CaseInsensitive))
                return true;
        }
        return false;
    }

    return true;
}
