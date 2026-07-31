#include "apitypes.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace {

/// FastAPI emits microsecond precision, which Qt::ISODateWithMs rejects.
/// Trim the fractional part to milliseconds before parsing.
QDateTime parseTimestamp(const QJsonValue& value)
{
    const QString raw = value.toString();
    if (raw.isEmpty())
        return {};

    QDateTime parsed = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (parsed.isValid())
        return parsed;

    static const QRegularExpression fraction(QStringLiteral("\\.(\\d{3})\\d+"));
    QString trimmed = raw;
    trimmed.replace(fraction, QStringLiteral(".\\1"));

    parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (parsed.isValid())
        return parsed;

    return QDateTime::fromString(trimmed, Qt::ISODate);
}

QStringList toStringList(const QJsonValue& value)
{
    QStringList out;
    for (const QJsonValue& item : value.toArray())
        out << item.toString();
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  ApiError
// ─────────────────────────────────────────────────────────────────────────────

QString ApiError::title() const
{
    if (isNetworkFailure())
        return QStringLiteral("Cannot Reach Server");
    if (httpStatus == 401)
        return QStringLiteral("Signed Out");
    if (httpStatus == 403)
        return QStringLiteral("Not Allowed");
    if (httpStatus == 404)
        return QStringLiteral("Not Found");
    if (httpStatus == 409)
        return QStringLiteral("Conflict");
    if (httpStatus >= 500)
        return QStringLiteral("Server Error");
    return QStringLiteral("Request Failed");
}

ApiError ApiError::fromJson(int httpStatus, const QJsonObject& obj)
{
    ApiError error;
    error.httpStatus = httpStatus;
    error.code    = obj.value(QStringLiteral("code")).toString();
    error.message = obj.value(QStringLiteral("message")).toString();
    error.detail  = obj.value(QStringLiteral("detail")).toObject();

    if (error.message.isEmpty()) {
        error.message = QStringLiteral("The server returned an error (HTTP %1).")
                            .arg(httpStatus);
    }
    if (error.code.isEmpty())
        error.code = QStringLiteral("http_error");
    return error;
}

ApiError ApiError::network(const QString& message)
{
    ApiError error;
    error.httpStatus = 0;
    error.code    = QString::fromLatin1(NetworkFailure);
    error.message = message;
    return error;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Resources
// ─────────────────────────────────────────────────────────────────────────────

ApiUser ApiUser::fromJson(const QJsonObject& obj)
{
    ApiUser user;
    user.id          = obj.value(QStringLiteral("id")).toInt(-1);
    user.email       = obj.value(QStringLiteral("email")).toString();
    user.displayName = obj.value(QStringLiteral("display_name")).toString();
    return user;
}

ApiGroup ApiGroup::fromJson(const QJsonObject& obj)
{
    ApiGroup group;
    group.id          = obj.value(QStringLiteral("id")).toInt(-1);
    group.name        = obj.value(QStringLiteral("name")).toString();
    group.ownerId     = obj.value(QStringLiteral("owner_id")).toInt(-1);
    group.isPersonal  = obj.value(QStringLiteral("is_personal")).toBool();
    group.myRole      = obj.value(QStringLiteral("my_role")).toString();
    group.memberCount = obj.value(QStringLiteral("member_count")).toInt();
    group.fileCount   = obj.value(QStringLiteral("file_count")).toInt();
    group.createdAt   = parseTimestamp(obj.value(QStringLiteral("created_at")));
    group.shareCode   = obj.value(QStringLiteral("share_code")).toString();
    return group;
}

ApiMember ApiMember::fromJson(const QJsonObject& obj)
{
    ApiMember member;
    member.userId      = obj.value(QStringLiteral("user_id")).toInt(-1);
    member.email       = obj.value(QStringLiteral("email")).toString();
    member.displayName = obj.value(QStringLiteral("display_name")).toString();
    member.role        = obj.value(QStringLiteral("role")).toString();
    member.joinedAt    = parseTimestamp(obj.value(QStringLiteral("joined_at")));
    return member;
}

ApiFile ApiFile::fromJson(const QJsonObject& obj)
{
    ApiFile file;
    file.id            = obj.value(QStringLiteral("id")).toInt(-1);
    file.contentHash   = obj.value(QStringLiteral("content_hash")).toString();
    file.fileName      = obj.value(QStringLiteral("file_name")).toString();
    file.fileSizeBytes = static_cast<qint64>(
        obj.value(QStringLiteral("file_size_bytes")).toDouble());
    file.pageCount     = obj.value(QStringLiteral("page_count")).toInt();
    file.uploaded      = obj.value(QStringLiteral("uploaded")).toBool();
    file.uploadedAt    = parseTimestamp(obj.value(QStringLiteral("uploaded_at")));
    file.tags          = toStringList(obj.value(QStringLiteral("tags")));
    return file;
}

ApiTag ApiTag::fromJson(const QJsonObject& obj)
{
    ApiTag tag;
    tag.id      = obj.value(QStringLiteral("id")).toInt(-1);
    tag.groupId = obj.value(QStringLiteral("group_id")).toInt(-1);
    tag.name    = obj.value(QStringLiteral("name")).toString();
    return tag;
}

ApiNote ApiNote::fromJson(const QJsonObject& obj)
{
    ApiNote note;
    note.id         = obj.value(QStringLiteral("id")).toInt(-1);
    note.groupId    = obj.value(QStringLiteral("group_id")).toInt(-1);
    note.fileId     = obj.value(QStringLiteral("file_id")).toInt(-1);
    note.authorId   = obj.value(QStringLiteral("author_id")).toInt(-1);
    note.authorName = obj.value(QStringLiteral("author_name")).toString();
    note.body       = obj.value(QStringLiteral("body")).toString();
    note.version    = obj.value(QStringLiteral("version")).toInt();
    note.createdAt  = parseTimestamp(obj.value(QStringLiteral("created_at")));
    note.updatedAt  = parseTimestamp(obj.value(QStringLiteral("updated_at")));
    note.editable   = obj.value(QStringLiteral("editable")).toBool();
    return note;
}

ApiSyncStatus ApiSyncStatus::fromJson(const QJsonObject& obj)
{
    ApiSyncStatus status;
    status.groupId       = obj.value(QStringLiteral("group_id")).toInt(-1);
    status.totalFiles    = obj.value(QStringLiteral("total_files")).toInt();
    status.uploadedFiles = obj.value(QStringLiteral("uploaded_files")).toInt();
    for (const QJsonValue& item : obj.value(QStringLiteral("pending")).toArray())
        status.pending << ApiFile::fromJson(item.toObject());
    for (const QJsonValue& item : obj.value(QStringLiteral("files")).toArray())
        status.files << ApiFile::fromJson(item.toObject());
    return status;
}

ApiFileRemoval ApiFileRemoval::fromJson(const QJsonObject& obj)
{
    ApiFileRemoval removal;
    removal.fileId          = obj.value(QStringLiteral("file_id")).toInt(-1);
    removal.detached        = obj.value(QStringLiteral("detached")).toBool();
    removal.purged          = obj.value(QStringLiteral("purged")).toBool();
    removal.stillReferenced = obj.value(QStringLiteral("still_referenced")).toBool();
    removal.message         = obj.value(QStringLiteral("message")).toString();
    return removal;
}

ApiUploadResult ApiUploadResult::fromJson(const QJsonObject& obj)
{
    ApiUploadResult result;
    result.fileId         = obj.value(QStringLiteral("file_id")).toInt(-1);
    result.uploaded       = obj.value(QStringLiteral("uploaded")).toBool();
    result.alreadyPresent = obj.value(QStringLiteral("already_present")).toBool();
    result.message        = obj.value(QStringLiteral("message")).toString();
    return result;
}

ApiRemoteEvent ApiRemoteEvent::fromJson(const QJsonObject& obj)
{
    ApiRemoteEvent event;
    event.type    = obj.value(QStringLiteral("type")).toString();
    event.groupId = obj.value(QStringLiteral("group_id")).toInt(-1);
    event.fileId  = obj.value(QStringLiteral("file_id")).toInt(-1);
    event.noteId  = obj.value(QStringLiteral("note_id")).toInt(-1);
    event.tagId   = obj.value(QStringLiteral("tag_id")).toInt(-1);
    event.actorId = obj.value(QStringLiteral("actor_id")).toInt(-1);
    return event;
}

ApiServerInfo ApiServerInfo::fromJson(const QJsonObject& obj)
{
    ApiServerInfo info;
    info.status            = obj.value(QStringLiteral("status")).toString();
    info.version           = obj.value(QStringLiteral("version")).toString();
    info.databaseOk        = obj.value(QStringLiteral("database")).toBool();
    info.storageConfigured = obj.value(QStringLiteral("storage_configured")).toBool();
    info.registrationOpen  = obj.value(QStringLiteral("registration_open")).toBool(true);
    return info;
}
