#pragma once
#include <QObject>
#include <QStringList>
#include <QList>
#include <QFileSystemWatcher>
#include "models/pdffile.h"

/**
 * @brief Scans root folders for PDFs and watches for file system changes.
 *
 * Scanning is offloaded to a thread pool via QtConcurrent.  Results are
 * delivered via signals so the caller (PdfController) can update the model
 * and database on the main thread.
 *
 * File system watching uses QFileSystemWatcher.  When a watched directory
 * changes, a rescan of that folder is triggered automatically.
 */
class FolderWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FolderWatcher(QObject* parent = nullptr);

    // ── Scanning ──────────────────────────────────────────────────────────────

    /// Add a new root folder and trigger an immediate scan.
    void addRootFolder(const QString& folderPath);

    /// Remove a root folder and stop watching it.
    void removeRootFolder(const QString& folderPath);

    /// Re-scan all currently watched root folders.
    void rescanAll();

    [[nodiscard]] QStringList rootFolders() const { return m_rootFolders; }

signals:
    /// Emitted for each PDF found (or re-discovered) during a scan.
    void fileDiscovered(const PdfFile& file);

    /// Emitted when a PDF is removed from disk.
    void fileRemoved(const QString& filePath);

    /// Emitted when a scan of @p folderPath completes.
    void scanFinished(const QString& folderPath);

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged     (const QString& path);

private:
    /// Recursively collect .pdf paths under @p rootPath.
    static QList<PdfFile> collectPdfs(const QString& rootPath);
    static PdfFile        buildPdfFile(const QString& filePath);

    void scanFolderAsync(const QString& folderPath);
    void watchPath      (const QString& path);

    QFileSystemWatcher m_watcher;
    QStringList        m_rootFolders;
};
