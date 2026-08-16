#include "databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QFileInfo>
#include <QUuid>

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
                page_count    INTEGER NOT NULL DEFAULT 0,
                deleted_locally BOOLEAN NOT NULL DEFAULT 0,
                pending_tags  BOOLEAN NOT NULL DEFAULT 0
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

        // Pending notes
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS pending_notes (
                id      INTEGER PRIMARY KEY AUTOINCREMENT,
                pdf_id  INTEGER NOT NULL REFERENCES pdf_files(id) ON DELETE CASCADE,
                body    TEXT    NOT NULL
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

        // A local path's identity: the uuid it was first assigned, and — once
        // registered — which group and backend listing it maps to and the
        // content hash that was current at the time. group_id/remote_file_id
        // are -1 and registered_hash is empty until the first registration.
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_identity (
                path            TEXT PRIMARY KEY,
                uuid            TEXT NOT NULL UNIQUE,
                group_id        INTEGER NOT NULL DEFAULT -1,
                remote_file_id  INTEGER NOT NULL DEFAULT -1,
                registered_hash TEXT
            )
        )"),

        // Which backend group holds the PDFs of a directory. One row per
        // directory that holds a PDF directly — subdirectories get their own
        // rows and their own groups — created the first time one is scanned.
        //
        // group_name is remembered alongside the id because the id is only
        // meaningful to one server: signing out sets group_id to -1 but keeps
        // the name, which is how a directory finds its way back to the same
        // group afterwards. It is the only route back for a folder that was
        // joined by share code, whose local name says nothing about the group's.
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS folder_groups (
                folder_path TEXT    PRIMARY KEY,
                group_id    INTEGER NOT NULL,
                group_name  TEXT
            )
        )"),

        // A create/rename/delete of a tag *name* still waiting to reach a
        // group — see the header doc comment for why this is keyed by folder
        // path rather than group id.
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS pending_tag_ops (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                folder_path TEXT    NOT NULL,
                op          TEXT    NOT NULL,
                tag_name    TEXT    NOT NULL,
                new_name    TEXT
            )
        )"),

        // Performance indexes
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_folder ON pdf_files(folder_path)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_opened ON pdf_files(last_opened)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_identity_remote ON file_identity(group_id, remote_file_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pending_tag_ops_folder ON pending_tag_ops(folder_path)"),
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            qWarning() << "Schema DDL failed:" << q.lastError().text();
            return false;
        }
    }

    // folder_groups.group_name arrived after the table did, so an install from
    // before it needs the column added rather than the table created.
    if (!m_db.record(QStringLiteral("folder_groups")).contains(
            QStringLiteral("group_name"))) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE folder_groups ADD COLUMN group_name TEXT"))) {
            qWarning() << "Could not add folder_groups.group_name:"
                       << q.lastError().text();
        }
    }

    // deleted_locally and pending_tags were added later
    if (!m_db.record(QStringLiteral("pdf_files")).contains(
            QStringLiteral("deleted_locally"))) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE pdf_files ADD COLUMN deleted_locally BOOLEAN NOT NULL DEFAULT 0"))) {
            qWarning() << "Could not add pdf_files.deleted_locally:"
                       << q.lastError().text();
        }
    }
    if (!m_db.record(QStringLiteral("pdf_files")).contains(
            QStringLiteral("pending_tags"))) {
        if (!q.exec(QStringLiteral(
                "ALTER TABLE pdf_files ADD COLUMN pending_tags BOOLEAN NOT NULL DEFAULT 0"))) {
            qWarning() << "Could not add pdf_files.pending_tags:"
                       << q.lastError().text();
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
        // Superseded by file_identity: sync identity moved from content hash
        // to a uuid per local path, which survives a file being annotated.
        QStringLiteral("DROP TABLE IF EXISTS remote_files"),
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
        WHERE  deleted_locally = 0
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
    q.prepare(QStringLiteral("UPDATE pdf_files SET deleted_locally = 1 WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), filePath);
    const bool ok = q.exec();

    // The path is gone for good, not merely missing from one scan, so its
    // identity should not linger to be handed to some unrelated later file.
    forgetFileIdentity(filePath);
    return ok;
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

bool DatabaseManager::setPendingTags(int fileId, bool pending)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE pdf_files SET pending_tags = :p WHERE id = :id"));
    q.bindValue(QStringLiteral(":p"), pending ? 1 : 0);
    q.bindValue(QStringLiteral(":id"), fileId);
    return q.exec();
}

QList<int> DatabaseManager::getFilesWithPendingTags() const
{
    QList<int> result;
    QSqlQuery q(QStringLiteral("SELECT id FROM pdf_files WHERE pending_tags = 1"), m_db);
    while (q.next())
        result << q.value(0).toInt();
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

// ── File identity ───────────────────────────────────────────────────────────

QString DatabaseManager::fileUuid(const QString& path) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT uuid FROM file_identity WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), path);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QString DatabaseManager::ensureFileUuid(const QString& path)
{
    const QString existing = fileUuid(path);
    if (!existing.isEmpty())
        return existing;

    const QString fresh = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO file_identity (path, uuid) VALUES (:p, :u)"));
    q.bindValue(QStringLiteral(":p"), path);
    q.bindValue(QStringLiteral(":u"), fresh);
    if (!q.exec()) {
        qWarning() << "ensureFileUuid failed:" << q.lastError().text();
        return {};
    }
    return fresh;
}

bool DatabaseManager::adoptFileUuid(const QString& path, const QString& uuid)
{
    if (uuid.isEmpty()) return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO file_identity (path, uuid) VALUES (:p, :u)
        ON CONFLICT(path) DO UPDATE SET uuid = excluded.uuid
    )"));
    q.bindValue(QStringLiteral(":p"), path);
    q.bindValue(QStringLiteral(":u"), uuid);
    return q.exec();
}

QString DatabaseManager::pathForUuid(const QString& uuid) const
{
    if (uuid.isEmpty()) return {};

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT path FROM file_identity WHERE uuid = :u"));
    q.bindValue(QStringLiteral(":u"), uuid);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool DatabaseManager::forgetFileIdentity(const QString& path)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM file_identity WHERE path = :p"));
    q.bindValue(QStringLiteral(":p"), path);
    return q.exec();
}

int DatabaseManager::remoteFileId(int groupId, const QString& uuid) const
{
    if (uuid.isEmpty()) return -1;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT remote_file_id FROM file_identity
        WHERE uuid = :u AND group_id = :g AND remote_file_id >= 0
    )"));
    q.bindValue(QStringLiteral(":u"), uuid);
    q.bindValue(QStringLiteral(":g"), groupId);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

bool DatabaseManager::storeRemoteFileId(int groupId, const QString& uuid,
                                        int remoteFileId, const QString& contentHash)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        UPDATE file_identity
        SET group_id = :g, remote_file_id = :r, registered_hash = :h
        WHERE uuid = :u
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":r"), remoteFileId);
    q.bindValue(QStringLiteral(":h"), contentHash);
    q.bindValue(QStringLiteral(":u"), uuid);
    return q.exec();
}

QString DatabaseManager::registeredHash(const QString& uuid) const
{
    if (uuid.isEmpty()) return {};

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT registered_hash FROM file_identity WHERE uuid = :u"));
    q.bindValue(QStringLiteral(":u"), uuid);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QString DatabaseManager::pathForRemoteFile(int groupId, int remoteFileId) const
{
    if (remoteFileId < 0) return {};

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT path FROM file_identity
        WHERE group_id = :g AND remote_file_id = :r
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":r"), remoteFileId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool DatabaseManager::forgetRemoteFile(int groupId, const QString& uuid)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        UPDATE file_identity
        SET group_id = -1, remote_file_id = -1, registered_hash = NULL
        WHERE uuid = :u AND group_id = :g
    )"));
    q.bindValue(QStringLiteral(":u"), uuid);
    q.bindValue(QStringLiteral(":g"), groupId);
    return q.exec();
}

bool DatabaseManager::clearRemoteCache()
{
    // The registrations go, the uuids stay: a uuid is a purely local identity
    // for a path, unrelated to which server or account it was last synced
    // against — there is no reason for a sign-out to forget it.
    QSqlQuery q(m_db);
    const bool files = q.exec(QStringLiteral(
        "UPDATE file_identity SET group_id = -1, remote_file_id = -1, registered_hash = NULL"));

    // The ids go, the names stay. A different server (or account) numbers its
    // groups differently, so every id here is now meaningless — but the name is
    // what MainWindow re-attaches each directory by after the next sign-in, and
    // a folder joined by share code has no other way back to its group.
    const bool orphans = q.exec(QStringLiteral(
        "DELETE FROM folder_groups WHERE group_name IS NULL"));
    const bool folders = q.exec(QStringLiteral(
        "UPDATE folder_groups SET group_id = -1"));
    return files && orphans && folders;
}

// ── Folder → group mapping ────────────────────────────────────────────────────

int DatabaseManager::folderGroupId(const QString& folderPath) const
{
    if (folderPath.isEmpty()) return -1;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT group_id FROM folder_groups WHERE folder_path = :p"));
    q.bindValue(QStringLiteral(":p"), folderPath);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

bool DatabaseManager::storeFolderGroup(const QString& folderPath, int groupId,
                                      const QString& groupName)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO folder_groups (folder_path, group_id, group_name)
        VALUES (:p, :g, :n)
        ON CONFLICT(folder_path) DO UPDATE SET group_id   = excluded.group_id,
                                               group_name = excluded.group_name
    )"));
    q.bindValue(QStringLiteral(":p"), folderPath);
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":n"), groupName);
    return q.exec();
}

QString DatabaseManager::folderGroupName(const QString& folderPath) const
{
    if (folderPath.isEmpty()) return {};

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT group_name FROM folder_groups WHERE folder_path = :p"));
    q.bindValue(QStringLiteral(":p"), folderPath);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

bool DatabaseManager::forgetFolderGroup(const QString& folderPath)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM folder_groups WHERE folder_path = :p"));
    q.bindValue(QStringLiteral(":p"), folderPath);
    return q.exec();
}

QStringList DatabaseManager::mappedFolders() const
{
    QStringList result;
    QSqlQuery q(QStringLiteral(
        "SELECT folder_path FROM folder_groups ORDER BY folder_path"), m_db);
    while (q.next())
        result << q.value(0).toString();
    return result;
}

QString DatabaseManager::folderForGroup(int groupId) const
{
    // -1 is the "no group known yet" marker clearRemoteCache leaves behind, and
    // several folders can carry it at once — it identifies nothing.
    if (groupId < 0) return {};

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT folder_path FROM folder_groups WHERE group_id = :g"));
    q.bindValue(QStringLiteral(":g"), groupId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

// ── Pending Notes ─────────────────────────────────────────────────────────────

bool DatabaseManager::savePendingNote(int fileId, const QString& body)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO pending_notes (pdf_id, body) VALUES (:f, :b)"));
    q.bindValue(QStringLiteral(":f"), fileId);
    q.bindValue(QStringLiteral(":b"), body);
    return q.exec();
}

QStringList DatabaseManager::getPendingNotes(int fileId) const
{
    QStringList result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT body FROM pending_notes WHERE pdf_id = :f ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":f"), fileId);
    if (q.exec()) {
        while (q.next()) {
            result << q.value(0).toString();
        }
    }
    return result;
}

bool DatabaseManager::clearPendingNotes(int fileId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM pending_notes WHERE pdf_id = :f"));
    q.bindValue(QStringLiteral(":f"), fileId);
    return q.exec();
}

QList<int> DatabaseManager::getFilesWithPendingNotes() const
{
    QList<int> result;
    QSqlQuery q(QStringLiteral("SELECT DISTINCT pdf_id FROM pending_notes"), m_db);
    while (q.next())
        result << q.value(0).toInt();
    return result;
}

// ── Pending tag vocabulary ops ────────────────────────────────────────────────
//
//  queuePendingTagOp() collapses against whatever is already queued for the
//  same (folderPath, tagName) rather than inserting blindly. A tag that was
//  itself created here and never reached the server has no server-side id
//  for a later rename or delete to reference, so those cases rewrite or drop
//  the pending create instead of ever becoming a rename/delete op.

bool DatabaseManager::queuePendingTagOp(const QString& folderPath, const QString& op,
                                        const QString& tagName, const QString& newName)
{
    QSqlQuery q(m_db);

    if (op == QStringLiteral("create")) {
        q.prepare(QStringLiteral(
            "SELECT id FROM pending_tag_ops WHERE folder_path = :f AND op = 'create' "
            "AND tag_name = :t COLLATE NOCASE"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        if (q.exec() && q.next())
            return true; // already queued

        q.prepare(QStringLiteral(
            "INSERT INTO pending_tag_ops (folder_path, op, tag_name) VALUES (:f, 'create', :t)"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        return q.exec();
    }

    if (op == QStringLiteral("rename")) {
        // Still unsent under this exact name — nothing to rename remotely;
        // just change what the eventual create will be named.
        q.prepare(QStringLiteral(
            "SELECT id FROM pending_tag_ops WHERE folder_path = :f AND op = 'create' "
            "AND tag_name = :t COLLATE NOCASE"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        if (q.exec() && q.next()) {
            QSqlQuery upd(m_db);
            upd.prepare(QStringLiteral("UPDATE pending_tag_ops SET tag_name = :n WHERE id = :id"));
            upd.bindValue(QStringLiteral(":n"),  newName);
            upd.bindValue(QStringLiteral(":id"), q.value(0).toInt());
            return upd.exec();
        }

        // A chained or repeated rename landing on or starting from this name —
        // fold it into one row (A→B, then B→C, becomes one A→C row) rather
        // than queuing a second hop.
        q.prepare(QStringLiteral(
            "SELECT id FROM pending_tag_ops WHERE folder_path = :f AND op = 'rename' "
            "AND (new_name = :t COLLATE NOCASE OR tag_name = :t COLLATE NOCASE)"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        if (q.exec() && q.next()) {
            QSqlQuery upd(m_db);
            upd.prepare(QStringLiteral("UPDATE pending_tag_ops SET new_name = :n WHERE id = :id"));
            upd.bindValue(QStringLiteral(":n"),  newName);
            upd.bindValue(QStringLiteral(":id"), q.value(0).toInt());
            return upd.exec();
        }

        q.prepare(QStringLiteral(
            "INSERT INTO pending_tag_ops (folder_path, op, tag_name, new_name) "
            "VALUES (:f, 'rename', :t, :n)"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        q.bindValue(QStringLiteral(":n"), newName);
        return q.exec();
    }

    if (op == QStringLiteral("delete")) {
        // Never left this machine — the queued create is simply dropped.
        q.prepare(QStringLiteral(
            "SELECT id FROM pending_tag_ops WHERE folder_path = :f AND op = 'create' "
            "AND tag_name = :t COLLATE NOCASE"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        if (q.exec() && q.next())
            return removePendingTagOp(q.value(0).toInt());

        // A queued rename landing on this name never reached the server
        // either — drop the rename and delete under the name the server
        // still actually knows the tag by.
        q.prepare(QStringLiteral(
            "SELECT id, tag_name FROM pending_tag_ops WHERE folder_path = :f "
            "AND op = 'rename' AND new_name = :t COLLATE NOCASE"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        if (q.exec() && q.next()) {
            const int id = q.value(0).toInt();
            const QString originalName = q.value(1).toString();
            removePendingTagOp(id);
            return queuePendingTagOp(folderPath, QStringLiteral("delete"), originalName);
        }

        q.prepare(QStringLiteral(
            "INSERT INTO pending_tag_ops (folder_path, op, tag_name) VALUES (:f, 'delete', :t)"));
        q.bindValue(QStringLiteral(":f"), folderPath);
        q.bindValue(QStringLiteral(":t"), tagName);
        return q.exec();
    }

    return false;
}

QList<int> DatabaseManager::pendingTagOpIds(const QString& folderPath) const
{
    QList<int> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM pending_tag_ops WHERE folder_path = :f ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":f"), folderPath);
    if (q.exec()) {
        while (q.next())
            result << q.value(0).toInt();
    }
    return result;
}

DatabaseManager::PendingTagOp DatabaseManager::pendingTagOp(int id) const
{
    PendingTagOp row;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT folder_path, op, tag_name, new_name FROM pending_tag_ops WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) {
        row.id         = id;
        row.folderPath = q.value(0).toString();
        row.op         = q.value(1).toString();
        row.tagName    = q.value(2).toString();
        row.newName    = q.value(3).toString();
    }
    return row;
}

bool DatabaseManager::removePendingTagOp(int id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM pending_tag_ops WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    return q.exec();
}

QStringList DatabaseManager::pendingTagCreateNames() const
{
    QStringList result;
    QSqlQuery q(QStringLiteral(
        "SELECT DISTINCT tag_name FROM pending_tag_ops WHERE op = 'create'"), m_db);
    while (q.next())
        result << q.value(0).toString();
    return result;
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
