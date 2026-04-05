#include "folderwatcher.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

FolderWatcher::FolderWatcher(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &FolderWatcher::onDirectoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FolderWatcher::onFileChanged);
}

// ── Public API ────────────────────────────────────────────────────────────────

void FolderWatcher::addRootFolder(const QString& folderPath)
{
    if (m_rootFolders.contains(folderPath)) return;
    m_rootFolders.append(folderPath);
    watchPath(folderPath);
    scanFolderAsync(folderPath);
}

void FolderWatcher::removeRootFolder(const QString& folderPath)
{
    m_rootFolders.removeAll(folderPath);
    // Remove this path and all sub-paths from the watcher
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    for (const QString& p : watched)
        if (p.startsWith(folderPath))
            m_watcher.removePath(p);
}

void FolderWatcher::rescanAll()
{
    for (const QString& root : std::as_const(m_rootFolders))
        scanFolderAsync(root);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void FolderWatcher::onDirectoryChanged(const QString& path)
{
    qDebug() << "FolderWatcher: directory changed ->" << path;
    // Find which root folder this change belongs to and rescan it
    for (const QString& root : std::as_const(m_rootFolders)) {
        if (path.startsWith(root)) {
            scanFolderAsync(root);
            return;
        }
    }
}

void FolderWatcher::onFileChanged(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        qDebug() << "FolderWatcher: file removed ->" << path;
        m_watcher.removePath(path);
        emit fileRemoved(path);
    }
    // If the file still exists it was modified – we don't need to re-emit it.
}

// ── Scanning ──────────────────────────────────────────────────────────────────

void FolderWatcher::scanFolderAsync(const QString& folderPath)
{
    const QString root = folderPath;

    auto* watcher = new QFutureWatcher<QList<PdfFile>>(this);

    connect(watcher, &QFutureWatcher<QList<PdfFile>>::finished, this,
            [this, watcher, root]() {
                const QList<PdfFile> files = watcher->result();
                for (const PdfFile& f : files) {
                    watchPath(f.filePath);
                    watchPath(f.folderPath);
                    emit fileDiscovered(f);
                }
                emit scanFinished(root);
                watcher->deleteLater();
            });

    QFuture<QList<PdfFile>> future = QtConcurrent::run(
        [root]() { return collectPdfs(root); }
    );
    watcher->setFuture(future);
}

// ── Static helpers ────────────────────────────────────────────────────────────

QList<PdfFile> FolderWatcher::collectPdfs(const QString& rootPath)
{
    QList<PdfFile> result;

    QDirIterator it(
        rootPath,
        QStringList{QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
        QDir::Files | QDir::Readable,
        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks
    );

    while (it.hasNext()) {
        it.next();
        result.append(buildPdfFile(it.filePath()));
    }

    return result;
}

PdfFile FolderWatcher::buildPdfFile(const QString& filePath)
{
    QFileInfo info(filePath);
    PdfFile f;
    f.filePath      = filePath;
    f.fileName      = info.fileName();
    f.folderPath    = info.absolutePath();
    f.fileSizeBytes = info.size();
    f.lastModified  = info.lastModified();
    // lastOpened and pageCount are filled in from the DB layer
    return f;
}

void FolderWatcher::watchPath(const QString& path)
{
    if (!m_watcher.directories().contains(path) &&
        !m_watcher.files().contains(path))
        m_watcher.addPath(path);
}
