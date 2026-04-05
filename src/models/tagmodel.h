#pragma once
#include <QAbstractListModel>
#include <QStringList>

/**
 * @brief Maintains the global list of tags available in the application.
 *
 * Tags are simple strings. This model is shared by the tag manager dialog,
 * the filter panel, and the tag-assignment widget on each PdfFile.
 */
class TagModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit TagModel(QObject* parent = nullptr);

    // ── QAbstractListModel ────────────────────────────────────────────────────
    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data    (const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool     setData (const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // ── Mutation ──────────────────────────────────────────────────────────────
    bool addTag   (const QString& tag);
    bool removeTag(const QString& tag);
    bool renameTag(const QString& oldName, const QString& newName);

    void resetTags(const QStringList& tags);

    // ── Query ─────────────────────────────────────────────────────────────────
    [[nodiscard]] bool        hasTag(const QString& tag)  const;
    [[nodiscard]] QStringList allTags()                   const { return m_tags; }
    [[nodiscard]] int         indexOf(const QString& tag) const { return m_tags.indexOf(tag); }

signals:
    void tagAdded  (const QString& tag);
    void tagRemoved(const QString& oldTag);
    void tagRenamed(const QString& oldTag, const QString& newTag);

private:
    QStringList m_tags;
};
