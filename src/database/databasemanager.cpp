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

        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_groups (
                id                  INTEGER PRIMARY KEY AUTOINCREMENT,
                name                TEXT NOT NULL UNIQUE COLLATE NOCASE,
                github_repo_url     TEXT,
                github_status       TEXT,
                github_validated_at TEXT,
                b2_key_id           TEXT,
                b2_bucket_name      TEXT,
                b2_account_id       TEXT,
                b2_status           TEXT,
                b2_validated_at     TEXT
            )
        )"),

        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_group_members (
                group_id INTEGER NOT NULL REFERENCES file_groups(id) ON DELETE CASCADE,
                pdf_id   INTEGER NOT NULL REFERENCES pdf_files(id) ON DELETE CASCADE,
                PRIMARY KEY (group_id, pdf_id)
            )
        )"),

        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_notes (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                pdf_id     INTEGER NOT NULL REFERENCES pdf_files(id) ON DELETE CASCADE,
                author     TEXT NOT NULL,
                body       TEXT NOT NULL,
                created_at TEXT NOT NULL
            )
        )"),

            // Per-group arbitrary settings (folder path, stored tokens, ssh key path, etc.)
            QStringLiteral(R"(
                CREATE TABLE IF NOT EXISTS file_group_settings (
                    group_id INTEGER NOT NULL REFERENCES file_groups(id) ON DELETE CASCADE,
                    key      TEXT    NOT NULL,
                    value    TEXT,
                    PRIMARY KEY (group_id, key)
                )
            )"),

        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS file_uploads (
                group_id      INTEGER NOT NULL REFERENCES file_groups(id) ON DELETE CASCADE,
                pdf_id        INTEGER NOT NULL REFERENCES pdf_files(id) ON DELETE CASCADE,
                file_size     INTEGER NOT NULL,
                last_modified TEXT,
                b2_file_id    TEXT,
                uploaded_at   TEXT NOT NULL,
                PRIMARY KEY (group_id, pdf_id)
            )
        )"),

        // Performance indexes
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_folder ON pdf_files(folder_path)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pdf_opened ON pdf_files(last_opened)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_notes_pdf ON file_notes(pdf_id)"),
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            qWarning() << "Schema DDL failed:" << q.lastError().text();
            return false;
        }
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

// ── Groups and notes ──────────────────────────────────────────────────────────

static FileGroup readGroup(const QSqlQuery& q)
{
    FileGroup g;
    g.id = q.value(0).toInt();
    g.name = q.value(1).toString();
    g.githubRepoUrl = q.value(2).toString();
    g.githubStatus = q.value(3).toString();
    g.githubValidatedAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
    g.b2KeyId = q.value(5).toString();
    g.b2BucketName = q.value(6).toString();
    g.b2AccountId = q.value(7).toString();
    g.b2Status = q.value(8).toString();
    g.b2ValidatedAt = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
    return g;
}

QList<FileGroup> DatabaseManager::loadGroups() const
{
    QList<FileGroup> groups;
    QSqlQuery q(QStringLiteral(R"(
        SELECT id, name, github_repo_url, github_status, github_validated_at,
               b2_key_id, b2_bucket_name, b2_account_id, b2_status, b2_validated_at
        FROM file_groups
        ORDER BY name COLLATE NOCASE
    )"), m_db);
    while (q.next())
        groups.append(readGroup(q));
    return groups;
}

FileGroup DatabaseManager::groupById(int groupId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT id, name, github_repo_url, github_status, github_validated_at,
               b2_key_id, b2_bucket_name, b2_account_id, b2_status, b2_validated_at
        FROM file_groups
        WHERE id = :id
    )"));
    q.bindValue(QStringLiteral(":id"), groupId);
    if (q.exec() && q.next())
        return readGroup(q);
    return {};
}

int DatabaseManager::createGroup(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return -1;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO file_groups (name) VALUES (:name)"));
    q.bindValue(QStringLiteral(":name"), trimmed);
    if (!q.exec()) return -1;

    QSqlQuery idq(m_db);
    idq.prepare(QStringLiteral("SELECT id FROM file_groups WHERE name = :name COLLATE NOCASE"));
    idq.bindValue(QStringLiteral(":name"), trimmed);
    if (idq.exec() && idq.next())
        return idq.value(0).toInt();
    return -1;
}

bool DatabaseManager::deleteGroup(int groupId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM file_groups WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), groupId);
    return q.exec();
}

bool DatabaseManager::renameGroup(int groupId, const QString& newName)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE file_groups SET name = :name WHERE id = :id"));
    q.bindValue(QStringLiteral(":name"), newName.trimmed());
    q.bindValue(QStringLiteral(":id"), groupId);
    return q.exec();
}

bool DatabaseManager::clearGroupMembers(int groupId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM file_group_members WHERE group_id = :g"));
    q.bindValue(QStringLiteral(":g"), groupId);
    return q.exec();
}

bool DatabaseManager::setFileInGroup(int fileId, int groupId, bool tracked)
{
    QSqlQuery q(m_db);
    if (tracked) {
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO file_group_members (group_id, pdf_id) VALUES (:g, :p)"));
    } else {
        q.prepare(QStringLiteral("DELETE FROM file_group_members WHERE group_id = :g AND pdf_id = :p"));
    }
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":p"), fileId);
    return q.exec();
}

QList<int> DatabaseManager::fileGroupIds(int fileId) const
{
    QList<int> ids;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT group_id FROM file_group_members WHERE pdf_id = :p"));
    q.bindValue(QStringLiteral(":p"), fileId);
    q.exec();
    while (q.next())
        ids.append(q.value(0).toInt());
    return ids;
}

bool DatabaseManager::saveGroupGithubValidation(int groupId, const QString& repoUrl, const QString& status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        UPDATE file_groups
        SET github_repo_url = :url,
            github_status = :status,
            github_validated_at = :at
        WHERE id = :id
    )"));
    q.bindValue(QStringLiteral(":url"), repoUrl.trimmed());
    q.bindValue(QStringLiteral(":status"), status);
    q.bindValue(QStringLiteral(":at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":id"), groupId);
    return q.exec();
}

bool DatabaseManager::saveGroupB2Validation(int groupId, const QString& keyId, const QString& bucketName,
                                            const QString& accountId, const QString& status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        UPDATE file_groups
        SET b2_key_id = :key,
            b2_bucket_name = :bucket,
            b2_account_id = :account,
            b2_status = :status,
            b2_validated_at = :at
        WHERE id = :id
    )"));
    q.bindValue(QStringLiteral(":key"), keyId.trimmed());
    q.bindValue(QStringLiteral(":bucket"), bucketName.trimmed());
    q.bindValue(QStringLiteral(":account"), accountId);
    q.bindValue(QStringLiteral(":status"), status);
    q.bindValue(QStringLiteral(":at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":id"), groupId);
    return q.exec();
}

bool DatabaseManager::wasFileUploaded(int groupId, int fileId, qint64 fileSize, const QDateTime& modified) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT 1 FROM file_uploads
        WHERE group_id = :g AND pdf_id = :p AND file_size = :s AND last_modified = :m
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":p"), fileId);
    q.bindValue(QStringLiteral(":s"), fileSize);
    q.bindValue(QStringLiteral(":m"), modified.toString(Qt::ISODate));
    return q.exec() && q.next();
}

bool DatabaseManager::markFileUploaded(int groupId, int fileId, qint64 fileSize, const QDateTime& modified,
                                       const QString& b2FileId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO file_uploads
            (group_id, pdf_id, file_size, last_modified, b2_file_id, uploaded_at)
        VALUES (:g, :p, :s, :m, :b2, :at)
        ON CONFLICT(group_id, pdf_id) DO UPDATE SET
            file_size = excluded.file_size,
            last_modified = excluded.last_modified,
            b2_file_id = excluded.b2_file_id,
            uploaded_at = excluded.uploaded_at
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":p"), fileId);
    q.bindValue(QStringLiteral(":s"), fileSize);
    q.bindValue(QStringLiteral(":m"), modified.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":b2"), b2FileId);
    q.bindValue(QStringLiteral(":at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return q.exec();
}

QList<FileNote> DatabaseManager::loadNotes(int fileId) const
{
    QList<FileNote> notes;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT id, pdf_id, author, body, created_at
        FROM file_notes
        WHERE pdf_id = :p
        ORDER BY created_at DESC, id DESC
    )"));
    q.bindValue(QStringLiteral(":p"), fileId);
    q.exec();
    while (q.next()) {
        FileNote note;
        note.id = q.value(0).toInt();
        note.fileId = q.value(1).toInt();
        note.author = q.value(2).toString();
        note.body = q.value(3).toString();
        note.createdAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
        notes.append(note);
    }
    return notes;
}

bool DatabaseManager::addNote(int fileId, const QString& author, const QString& body)
{
    const QString trimmed = body.trimmed();
    if (fileId <= 0 || author.trimmed().isEmpty() || trimmed.isEmpty())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO file_notes (pdf_id, author, body, created_at)
        VALUES (:p, :author, :body, :at)
    )"));
    q.bindValue(QStringLiteral(":p"), fileId);
    q.bindValue(QStringLiteral(":author"), author.trimmed());
    q.bindValue(QStringLiteral(":body"), trimmed);
    q.bindValue(QStringLiteral(":at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return q.exec();
}

bool DatabaseManager::saveGroupSetting(int groupId, const QString& key, const QString& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO file_group_settings (group_id, key, value)
        VALUES (:g, :k, :v)
        ON CONFLICT(group_id, key) DO UPDATE SET value = excluded.value
    )"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value);
    return q.exec();
}

QString DatabaseManager::getGroupSetting(int groupId, const QString& key, const QString& defaultValue) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM file_group_settings WHERE group_id = :g AND key = :k"));
    q.bindValue(QStringLiteral(":g"), groupId);
    q.bindValue(QStringLiteral(":k"), key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
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
