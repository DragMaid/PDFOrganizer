#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QFileInfo>
#include <QUuid>

static constexpr int kSchemaVersion = 1;

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
    , m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
{}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open(const QString& dbPath)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "DatabaseManager: cannot open" << dbPath
                   << m_db.lastError().text();
        return false;
    }

    // Enable WAL mode and foreign keys
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    return createSchema();
}

void DatabaseManager::close()
{
    if (m_db.isOpen())
        m_db.close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::isOpen() const
{
    return m_db.isOpen();
}

// ── Schema ────────────────────────────────────────────────────────────────────

bool DatabaseManager::createSchema()
{
    QSqlQuery q(m_db);

    const QStringList ddl = {
        // Version table
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"),

        // Root folders
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS folders (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT    NOT NULL UNIQUE
            )
        )"),

        // Tags
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS tags (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT    NOT NULL UNIQUE COLLATE NOCASE
            )
        )"),

        // PDF files
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS pdf_files (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                path          TEXT    NOT NULL UNIQUE,
                folder_path   TEXT    NOT NULL,
                file_name     TEXT    NOT NULL,
                file_size     INTEGER NOT NULL DEFAULT 0,
                last_modified TEXT,
                last_opened   TEXT,
                page_count    INTEGER NOT NULL DEFAULT 0
            )
        )"),

        // Many-to-many: pdf ↔ tag
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS pdf_tags (
                pdf_id  INTEGER NOT NULL REFERENCES pdf_files(id) ON DELETE CASCADE,
                tag_id  INTEGER NOT NULL REFERENCES tags(id)      ON DELETE CASCADE,
                PRIMARY KEY (pdf_id, tag_id)
            )
        )"),

        // Application settings (key-value)
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS settings (
                key   TEXT NOT NULL UNIQUE,
                value TEXT
            )
        )"),

        // SHA-256 of each file's contents — the identity the backend keys
        // files by. Recomputed only when size or mtime moves.
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_hashes (
                path          TEXT PRIMARY KEY,
                content_hash  TEXT NOT NULL,
                file_size     INTEGER NOT NULL,
                last_modified TEXT
            )
        )"),

        // Maps a content hash to the id the backend assigned it in a group, so
        // routine operations skip a registration round trip.
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS remote_files (
                group_id       INTEGER NOT NULL,
                content_hash   TEXT    NOT NULL,
                remote_file_id INTEGER NOT NULL,
                PRIMARY KEY (group_id, content_hash)
            )
        )"),

        // Performance indexes
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_folder ON pdf_files(folder_path)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_opened ON pdf_files(last_opened)"),
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            qWarning() << "Schema DDL failed:" << q.lastError().text();
            return false;
        }
    }

    // Drop the tables from the retired GitHub/Backblaze sync so old installs
    // stop carrying credentials we no longer use.
    const QStringList retired = {
        QStringLiteral("DROP TABLE IF EXISTS file_uploads"),
        QStringLiteral("DROP TABLE IF EXISTS file_group_settings"),
        QStringLiteral("DROP TABLE IF EXISTS file_notes"),
        QStringLiteral("DROP TABLE IF EXISTS file_group_members"),
        QStringLiteral("DROP TABLE IF EXISTS file_groups"),
        QStringLiteral("DELETE FROM settings WHERE key = 'githubUser'"),
    };
    for (const QString& stmt : retired) {
        if (!q.exec(stmt))
            qWarning() << "Could not drop retired table:" << q.lastError().text();
    }

    return true;
}

// ── Folders ───────────────────────────────────────────────────────────────────

QStringList DatabaseManager::loadFolders() const
{
    QStringList result;
    QSqlQuery q(QStringLiteral("SELECT path FROM folders ORDER BY path"), m_db);
    while (q.next())
        result << q.value(0).toString();
    return result;
}

bool DatabaseManager::saveFolder(const QString& path)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO folders (path) VALUES (:p)"));
    q.bindValue(QStringLiteral(":p"), path);
    return q.exec();
}

bool DatabaseManager::deleteFolder(const QString& path)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM folders WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), path);
    return q.exec();
}

// ── Tags ──────────────────────────────────────────────────────────────────────

QStringList DatabaseManager::loadTags() const
{
    QStringList result;
    QSqlQuery q(QStringLiteral("SELECT name FROM tags ORDER BY name COLLATE NOCASE"), m_db);
    while (q.next())
        result << q.value(0).toString();
    return result;
}

bool DatabaseManager::saveTag(const QString& name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO tags (name) VALUES (:n)"));
    q.bindValue(QStringLiteral(":n"), name.trimmed());
    return q.exec();
}

bool DatabaseManager::deleteTag(const QString& name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM tags WHERE name = :n COLLATE NOCASE"));
    q.bindValue(QStringLiteral(":n"), name);
    return q.exec();
}

bool DatabaseManager::renameTag(const QString& oldName, const QString& newName)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE tags SET name = :new WHERE name = :old COLLATE NOCASE"));
    q.bindValue(QStringLiteral(":new"), newName.trimmed());
    q.bindValue(QStringLiteral(":old"), oldName);
    return q.exec();
}

// ── PDF Files ─────────────────────────────────────────────────────────────────

QList<PdfFile> DatabaseManager::loadAllFiles() const
{
    QList<PdfFile> result;

    QSqlQuery q(QStringLiteral(R"(
        SELECT id, path, folder_path, file_name, file_size,
               last_modified, last_opened, page_count
        FROM   pdf_files
        ORDER  BY file_name COLLATE NOCASE
    )"), m_db);

    while (q.next()) {
        PdfFile f;
        f.id            = q.value(0).toInt();
        f.filePath      = q.value(1).toString();
        f.folderPath    = q.value(2).toString();
        f.fileName      = q.value(3).toString();
        f.fileSizeBytes = q.value(4).toLongLong();
        f.lastModified  = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        f.lastOpened    = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        f.pageCount     = q.value(7).toInt();
        f.tags          = getFileTags(f.id);
        result.append(f);
    }
    return result;
}

bool DatabaseManager::saveFile(PdfFile& file)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO pdf_files
               (path, folder_path, file_name, file_size, last_modified, last_opened, page_count)
        VALUES (:path, :folder, :name, :size, :modified, :opened, :pages)
        ON CONFLICT(path) DO UPDATE SET
               folder_path   = excluded.folder_path,
               file_name     = excluded.file_name,
               file_size     = excluded.file_size,
               last_modified = excluded.last_modified,
               page_count    = excluded.page_count
    )"));

    q.bindValue(QStringLiteral(":path"),     file.filePath);
    q.bindValue(QStringLiteral(":folder"),   file.folderPath);
    q.bindValue(QStringLiteral(":name"),     file.fileName);
    q.bindValue(QStringLiteral(":size"),     file.fileSizeBytes);
    q.bindValue(QStringLiteral(":modified"), file.lastModified.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":opened"),   file.lastOpened.isValid()
                                                ? file.lastOpened.toString(Qt::ISODate)
                                                : QVariant{});
    q.bindValue(QStringLiteral(":pages"),    file.pageCount);

    if (!q.exec()) {
        qWarning() << "saveFile failed:" << q.lastError().text();
        return false;
    }

    // Retrieve the row id
    file.id = fileId(file.filePath);
    if (file.id > 0)
        setFileTags(file.id, file.tags);

    return file.id > 0;
}

bool DatabaseManager::deleteFile(const QString& filePath)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM pdf_files WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), filePath);
    return q.exec();
}

bool DatabaseManager::updateLastOpened(const QString& filePath, const QDateTime& dt)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE pdf_files SET last_opened = :dt WHERE path = :p"));
    q.bindValue(QStringLiteral(":dt"), dt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":p"),  filePath);
    return q.exec();
}

// ── Tag assignments ───────────────────────────────────────────────────────────

bool DatabaseManager::setFileTags(int pdfId, const QStringList& tags)
{
    // Delete existing assignments
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM pdf_tags WHERE pdf_id = :id"));
    del.bindValue(QStringLiteral(":id"), pdfId);
    if (!del.exec()) return false;

    // Insert new assignments
    for (const QString& tag : tags) {
        const int tid = ensureTagId(tag);
        if (tid < 0) continue;

        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO pdf_tags (pdf_id, tag_id) VALUES (:p, :t)"));
        ins.bindValue(QStringLiteral(":p"), pdfId);
        ins.bindValue(QStringLiteral(":t"), tid);
        ins.exec();
    }
    return true;
}

QStringList DatabaseManager::getFileTags(int pdfId) const
{
    QStringList result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT t.name FROM tags t
        JOIN   pdf_tags pt ON pt.tag_id = t.id
        WHERE  pt.pdf_id = :id
        ORDER  BY t.name COLLATE NOCASE
    )"));
    q.bindValue(QStringLiteral(":id"), pdfId);
    q.exec();
    while (q.next())
        result << q.value(0).toString();
    return result;
}

// ── Settings ──────────────────────────────────────────────────────────────────

QVariant DatabaseManager::getSetting(const QString& key, const QVariant& defaultValue) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = :k"));
    q.bindValue(QStringLiteral(":k"), key);
    if (q.exec() && q.next())
        return q.value(0);
    return defaultValue;
}

bool DatabaseManager::setSetting(const QString& key, const QVariant& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (:k, :v) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value.toString());
    return q.exec();
}

// ── Content-hash cache ────────────────────────────────────────────────────────

QString DatabaseManager::cachedHash(const QString& path, qint64 fileSize,
                                    const QDateTime& modified) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT content_hash FROM file_hashes
        WHERE path = :p AND file_size = :s AND last_modified = :m
    )"));
    q.bindValue(QStringLiteral(":p"), path);
    q.bindValue(QStringLiteral(":s"), fileSize);
    q.bindValue(QStringLiteral(":m"), modified.toString(Qt::ISODate));
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool DatabaseManager::storeHash(const QString& path, const QString& contentHash,
                                qint64 fileSize, const QDateTime& modified)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO file_hashes (path, content_hash, file_size, last_modified)
        VALUES (:p, :h, :s, :m)
        ON CONFLICT(path) DO UPDATE SET
            content_hash  = excluded.content_hash,
            file_size     = excluded.file_size,
            last_modified = excluded.last_modified
    )"));
    q.bindValue(QStringLiteral(":p"), path);
    q.bindValue(QStringLiteral(":h"), contentHash);
    q.bindValue(QStringLiteral(":s"), fileSize);
    q.bindValue(QStringLiteral(":m"), modified.toString(Qt::ISODate));
    return q.exec();
}

// ── Remote id cache ───────────────────────────────────────────────────────────

int DatabaseManager::remoteFileId(int groupId, const QString& contentHash) const
{
    if (contentHash.isEmpty()) return -1;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT remote_file_id FROM remote_files
        WHERE group_id = :g AND content_hash = :h
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":h"), contentHash);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

bool DatabaseManager::storeRemoteFileId(int groupId, const QString& contentHash,
                                        int remoteFileId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO remote_files (group_id, content_hash, remote_file_id)
        VALUES (:g, :h, :r)
        ON CONFLICT(group_id, content_hash) DO UPDATE SET
            remote_file_id = excluded.remote_file_id
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":h"), contentHash);
    q.bindValue(QStringLiteral(":r"), remoteFileId);
    return q.exec();
}

bool DatabaseManager::forgetRemoteFile(int groupId, const QString& contentHash)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM remote_files WHERE group_id = :g AND content_hash = :h"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":h"), contentHash);
    return q.exec();
}

bool DatabaseManager::clearRemoteCache()
{
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral("DELETE FROM remote_files"));
}

// ── Private helpers ───────────────────────────────────────────────────────────

int DatabaseManager::ensureTagId(const QString& name)
{
    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral("INSERT OR IGNORE INTO tags (name) VALUES (:n)"));
    ins.bindValue(QStringLiteral(":n"), name);
    ins.exec();
    return tagId(name);
}

int DatabaseManager::tagId(const QString& name) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM tags WHERE name = :n COLLATE NOCASE"));
    q.bindValue(QStringLiteral(":n"), name);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

int DatabaseManager::fileId(const QString& filePath) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM pdf_files WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), filePath);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}
