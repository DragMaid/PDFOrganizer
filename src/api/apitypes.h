#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @brief Value types mirroring the backend's JSON payloads.
 *
 * These are plain structs with a `fromJson` factory — no Qt meta-object
 * machinery, so they are cheap to copy into lambdas.
 */

// ─────────────────────────────────────────────────────────────────────────────
//  Errors
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief One failure, in the shape every backend endpoint returns.
 *
 * The backend writes `message` for humans, so it can go straight into a
 * QMessageBox without rephrasing. `code` is the stable machine-readable half —
 * use it when a specific failure needs specific handling (see
 * ApiError::StaleNote).
 */
struct ApiError
{
    int         httpStatus = 0;         ///< 0 when the request never reached the server
    QString     code;                   ///< e.g. "forbidden", "stale_note"
    QString     message;                ///< Human-readable; safe to show as-is
    QJsonObject detail;                 ///< Endpoint-specific extras

    // Codes worth branching on.
    static constexpr auto Unauthorized       = "unauthorized";
    static constexpr auto Forbidden          = "forbidden";
    static constexpr auto NotFound           = "not_found";
    static constexpr auto StaleNote          = "stale_note";
    static constexpr auto TagExists          = "tag_exists";
    static constexpr auto EmailTaken         = "email_taken";
    static constexpr auto UserNotFound       = "user_not_found";
    static constexpr auto OwnerLocked        = "owner_locked";
    static constexpr auto StorageUnconfigured = "storage_unconfigured";
    static constexpr auto NetworkFailure     = "network_failure";
    static constexpr auto ShareCodeNotFound  = "share_code_not_found";
    /// The file is registered in the group but its bytes were never uploaded,
    /// so there is nothing to download yet.
    static constexpr auto NotUploaded        = "not_uploaded";

    [[nodiscard]] bool isNetworkFailure() const { return httpStatus == 0; }
    [[nodiscard]] bool isAuthFailure()    const { return httpStatus == 401; }

    /// Short title for the modal, derived from the kind of failure.
    [[nodiscard]] QString title() const;

    static ApiError fromJson(int httpStatus, const QJsonObject& obj);
    static ApiError network(const QString& message);
};

// ─────────────────────────────────────────────────────────────────────────────
//  Resources
// ─────────────────────────────────────────────────────────────────────────────

struct ApiUser
{
    int     id = -1;
    QString email;
    QString displayName;

    [[nodiscard]] bool isValid() const { return id >= 0; }
    static ApiUser fromJson(const QJsonObject& obj);
};

struct ApiGroup
{
    int       id = -1;
    QString   name;
    int       ownerId = -1;
    bool      isPersonal = false;
    QString   myRole;               ///< "owner" or "member"
    int       memberCount = 0;
    int       fileCount = 0;
    QDateTime createdAt;
    /// The string to hand someone so they can join this group — holding it is
    /// itself the permission, so treat it like a password. Empty for a personal
    /// group, which nobody else may join.
    QString   shareCode;

    [[nodiscard]] bool isValid() const { return id >= 0; }
    [[nodiscard]] bool isOwner() const { return myRole == QLatin1String("owner"); }
    [[nodiscard]] bool isShareable() const {
        return isValid() && !isPersonal && !shareCode.isEmpty();
    }
    static ApiGroup fromJson(const QJsonObject& obj);
};

struct ApiMember
{
    int       userId = -1;
    QString   email;
    QString   displayName;
    QString   role;
    QDateTime joinedAt;

    [[nodiscard]] bool isOwner() const { return role == QLatin1String("owner"); }
    static ApiMember fromJson(const QJsonObject& obj);
};

struct ApiFile
{
    int         id = -1;
    QString     contentHash;
    QString     fileName;
    qint64      fileSizeBytes = 0;
    int         pageCount = 0;
    bool        uploaded = false;
    QDateTime   uploadedAt;
    QStringList tags;

    [[nodiscard]] bool isValid() const { return id >= 0; }
    static ApiFile fromJson(const QJsonObject& obj);
};

struct ApiTag
{
    int     id = -1;
    int     groupId = -1;
    QString name;

    static ApiTag fromJson(const QJsonObject& obj);
};

struct ApiNote
{
    int       id = -1;
    int       groupId = -1;
    int       fileId = -1;
    int       authorId = -1;
    QString   authorName;
    QString   body;
    int       version = 0;
    QDateTime createdAt;
    QDateTime updatedAt;
    /// True when the signed-in user wrote this note. Only then may the client
    /// offer Edit/Delete — the backend enforces the same rule regardless.
    bool      editable = false;

    [[nodiscard]] bool isValid() const { return id >= 0; }
    static ApiNote fromJson(const QJsonObject& obj);
};

struct ApiSyncStatus
{
    int           groupId = -1;
    int           totalFiles = 0;
    int           uploadedFiles = 0;
    /// Registered in the group but never uploaded — the upload side of a sync.
    QList<ApiFile> pending;
    /// Every file in the group. The server cannot know what this machine holds
    /// on disk, so the download side is worked out here by subtracting the
    /// content hashes we already have from this list.
    QList<ApiFile> files;

    static ApiSyncStatus fromJson(const QJsonObject& obj);
};

/// What a removal actually did. Detaching a file from a group and destroying
/// the stored copy are separate acts: the second is owner-only and is skipped
/// when another group still shares the same content.
struct ApiFileRemoval
{
    int     fileId = -1;
    bool    detached = false;
    bool    purged = false;
    bool    stillReferenced = false;
    QString message;

    static ApiFileRemoval fromJson(const QJsonObject& obj);
};

struct ApiUploadResult
{
    int     fileId = -1;
    bool    uploaded = false;
    bool    alreadyPresent = false;
    QString message;

    static ApiUploadResult fromJson(const QJsonObject& obj);
};

struct ApiServerInfo
{
    QString status;
    QString version;
    bool    databaseOk = false;
    bool    storageConfigured = false;
    bool    registrationOpen = true;

    static ApiServerInfo fromJson(const QJsonObject& obj);
};
