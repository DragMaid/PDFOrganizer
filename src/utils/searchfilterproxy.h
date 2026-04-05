#pragma once
#include <QSortFilterProxyModel>
#include <QStringList>

/**
 * @brief Proxy model that applies full-text search (by name) and
 *        tag-based filtering on top of PdfModel.
 *
 * Filtering rules:
 *  - If m_searchText is non-empty, only rows whose file name or tag list
 *    contains the text (case-insensitive) are shown.
 *  - If m_activeTags is non-empty, only rows that have ALL active tags
 *    are shown (AND semantics – use OR if preferred by changing acceptsRow).
 *  - Both filters are combined with AND.
 */
class SearchFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit SearchFilterProxy(QObject* parent = nullptr);

    void setSearchText  (const QString& text);
    void setActiveTags  (const QStringList& tags);
    void setFolderFilter(const QString& folderPath);  ///< "" = show all folders
    void clearFilters   ();

    [[nodiscard]] QString     searchText()   const { return m_searchText; }
    [[nodiscard]] QStringList activeTags()   const { return m_activeTags; }
    [[nodiscard]] QString     folderFilter() const { return m_folderFilter; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString     m_searchText;
    QStringList m_activeTags;
    QString     m_folderFilter;
};
