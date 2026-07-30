#include "apiclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QWebSocket>
#include <memory>

namespace {

constexpr int kTimeoutMs = 60'000;

QJsonObject objectFrom(const QJsonDocument &doc) {
  return doc.isObject() ? doc.object() : QJsonObject{};
}

/// Turn a JSON array reply into a typed list.
template <typename T> QList<T> listFrom(const QJsonDocument &doc) {
  QList<T> out;
  for (const QJsonValue &item : doc.array())
    out << T::fromJson(item.toObject());
  return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Plumbing
// ─────────────────────────────────────────────────────────────────────────────

struct ApiClient::PendingRequest {
  QByteArray method;
  QString path;
  QJsonObject body;
  bool hasBody = false;
  ApiClient::RawHandler onOk;
  ApiClient::ErrorHandler onError;
  bool mayRetry = true;
};

void ApiClient::connectWebSocket() {
  if (m_accessToken.isEmpty())
    return;

  if (!m_webSocket) {
    m_webSocket = new QWebSocket(QStringLiteral("PDFOrg"),
                                 QWebSocketProtocol::VersionLatest, this);

    connect(m_webSocket, &QWebSocket::connected, this, [this]() {
      QJsonObject auth;
      auth["type"] = QStringLiteral("auth");
      auth["token"] = m_accessToken;

      QJsonDocument doc(auth);
      m_webSocket->sendTextMessage(
          QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    });

    connect(m_webSocket, &QWebSocket::textMessageReceived, this,
            [this](const QString &msg) {
              if (msg == QStringLiteral("sync_needed")) {
                emit syncNeeded();
              }
            });

    connect(m_webSocket, &QWebSocket::disconnected, this, [this]() {
      if (!m_accessToken.isEmpty()) {
        QTimer::singleShot(5000, this, &ApiClient::connectWebSocket);
      }
    });

    connect(m_webSocket,
            qOverload<QAbstractSocket::SocketError>(&QWebSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError error) {
              qDebug() << "WebSocket error:" << error << "-"
                       << m_webSocket->errorString();
            });
  }

  QUrl url = QUrl(m_baseUrl).resolved(QUrl(QStringLiteral("/ws")));
  url.setScheme(url.scheme() == QStringLiteral("https") ? QStringLiteral("wss")
                                                        : QStringLiteral("ws"));

  // No query parameters anymore.
  m_webSocket->open(url);
}

ApiClient::ApiClient(QObject *parent) : QObject(parent) {
  m_nam.setAutoDeleteReplies(false);
}

ApiClient::~ApiClient() = default;

void ApiClient::setBaseUrl(const QUrl &baseUrl) { m_baseUrl = baseUrl; }

QUrl ApiClient::resolve(const QString &path) const {
  QUrl url = m_baseUrl;
  QString base = url.path();
  while (base.endsWith(QLatin1Char('/')))
    base.chop(1);

  // A caller may hand us a query along with the path ("…/files/7?purge=true").
  // setPath() would escape the '?' into the path itself, so the two halves are
  // separated here rather than at every call site.
  const int mark = path.indexOf(QLatin1Char('?'));
  if (mark < 0) {
    url.setPath(base + path);
    return url;
  }
  url.setPath(base + path.left(mark));
  url.setQuery(path.mid(mark + 1));
  return url;
}

void ApiClient::report(const ErrorHandler &onError, const ApiError &error) {
  // The whole point of the default: a caller that does not opt into handling
  // a failure still gets it in front of the user.
  if (onError)
    onError(error);
  else
    emit errorOccurred(error);
}

void ApiClient::restoreSession(const QString &refreshToken,
                               const ApiUser &user) {
  m_refreshToken = refreshToken;
  m_user = user;
  m_accessToken.clear(); // obtained on the first refresh
}

void ApiClient::clearSession() {
  const bool wasAuthenticated = isAuthenticated();
  m_accessToken.clear();
  m_refreshToken.clear();
  m_user = {};
  if (m_webSocket) {
    m_webSocket->close();
  }
  if (wasAuthenticated)
    emit authenticatedChanged(false);
}

void ApiClient::applySession(const QJsonObject &tokenPair) {
  m_accessToken = tokenPair.value(QStringLiteral("access_token")).toString();
  m_refreshToken = tokenPair.value(QStringLiteral("refresh_token")).toString();
  m_user =
      ApiUser::fromJson(tokenPair.value(QStringLiteral("user")).toObject());
  emit authenticatedChanged(isAuthenticated());
  connectWebSocket();
}

void ApiClient::send(const QByteArray &method, const QString &path,
                     const QJsonObject &body, bool hasBody, RawHandler onOk,
                     ErrorHandler onError, bool mayRetry) {
  if (!hasBaseUrl()) {
    report(onError,
           ApiError::network(QStringLiteral(
               "No server address is configured. Set one in Settings.")));
    return;
  }

  PendingRequest request;
  request.method = method;
  request.path = path;
  request.body = body;
  request.hasBody = hasBody;
  request.onOk = std::move(onOk);
  request.onError = std::move(onError);
  request.mayRetry = mayRetry;
  dispatch(request);
}

void ApiClient::dispatch(const PendingRequest &request) {
  QNetworkRequest netRequest(resolve(request.path));
  netRequest.setTransferTimeout(kTimeoutMs);
  netRequest.setRawHeader("Accept", "application/json");
  if (request.hasBody) {
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                         QStringLiteral("application/json"));
  }
  if (!m_accessToken.isEmpty())
    netRequest.setRawHeader("Authorization",
                            "Bearer " + m_accessToken.toUtf8());

  const QByteArray payload =
      request.hasBody
          ? QJsonDocument(request.body).toJson(QJsonDocument::Compact)
          : QByteArray{};

  QNetworkReply *reply =
      request.hasBody
          ? m_nam.sendCustomRequest(netRequest, request.method, payload)
          : m_nam.sendCustomRequest(netRequest, request.method);

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, request]() { finish(reply, request); });
}

void ApiClient::finish(QNetworkReply *reply, const PendingRequest &request) {
  reply->deleteLater();

  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray payload = reply->readAll();

  // No HTTP status at all means we never got a reply worth parsing.
  if (status == 0) {
    report(request.onError,
           ApiError::network(
               QStringLiteral("Could not reach %1.\n\n%2")
                   .arg(m_baseUrl.toString(), reply->errorString())));
    return;
  }

  if (status == 401 && request.mayRetry && !m_refreshToken.isEmpty() &&
      !m_refreshing) {
    retryAfterRefresh(request);
    return;
  }

  if (status >= 400) {
    const QJsonObject obj = objectFrom(QJsonDocument::fromJson(payload));
    ApiError error = ApiError::fromJson(status, obj);

    if (status == 401) {
      // Refresh already failed, or there was nothing to refresh with.
      clearSession();
      emit sessionExpired();
    }
    report(request.onError, error);
    return;
  }

  if (request.onOk)
    request.onOk(QJsonDocument::fromJson(payload));
}

void ApiClient::retryAfterRefresh(const PendingRequest &request) {
  PendingRequest replay = request;
  replay.mayRetry = false; // one attempt only, never a refresh loop

  refreshSession([this, replay]() { dispatch(replay); },
                 [this, replay](const ApiError &) {
                   clearSession();
                   emit sessionExpired();
                   ApiError expired;
                   expired.httpStatus = 401;
                   expired.code = QString::fromLatin1(ApiError::Unauthorized);
                   expired.message = QStringLiteral(
                       "Your session has expired. Sign in again.");
                   report(replay.onError, expired);
                 });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Auth
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::health(Handler<ApiServerInfo> onOk, ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/health"), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiServerInfo::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::registerAccount(const QString &email, const QString &password,
                                const QString &displayName,
                                Handler<ApiUser> onOk, ErrorHandler onError) {
  QJsonObject body{
      {QStringLiteral("email"), email},
      {QStringLiteral("password"), password},
      {QStringLiteral("display_name"), displayName},
  };
  send(
      "POST", QStringLiteral("/auth/register"), body, true,
      [this, onOk](const QJsonDocument &doc) {
        applySession(objectFrom(doc));
        if (onOk)
          onOk(m_user);
      },
      std::move(onError), /*mayRetry=*/false);
}

void ApiClient::login(const QString &email, const QString &password,
                      Handler<ApiUser> onOk, ErrorHandler onError) {
  QJsonObject body{
      {QStringLiteral("email"), email},
      {QStringLiteral("password"), password},
  };
  send(
      "POST", QStringLiteral("/auth/login"), body, true,
      [this, onOk](const QJsonDocument &doc) {
        applySession(objectFrom(doc));
        if (onOk)
          onOk(m_user);
      },
      std::move(onError), /*mayRetry=*/false);
}

void ApiClient::refreshSession(VoidHandler onOk, ErrorHandler onError) {
  if (m_refreshToken.isEmpty()) {
    ApiError error;
    error.httpStatus = 401;
    error.code = QString::fromLatin1(ApiError::Unauthorized);
    error.message = QStringLiteral("Sign in to continue.");
    report(onError, error);
    return;
  }

  m_refreshing = true;
  QJsonObject body{{QStringLiteral("refresh_token"), m_refreshToken}};
  send(
      "POST", QStringLiteral("/auth/refresh"), body, true,
      [this, onOk](const QJsonDocument &doc) {
        m_refreshing = false;
        applySession(objectFrom(doc));
        if (onOk)
          onOk();
      },
      [this, onError](const ApiError &error) {
        m_refreshing = false;
        report(onError, error);
      },
      /*mayRetry=*/false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Groups
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::listGroups(Handler<QList<ApiGroup>> onOk,
                           ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/groups"), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiGroup>(doc));
      },
      std::move(onError));
}

void ApiClient::createGroup(const QString &name, Handler<ApiGroup> onOk,
                            ErrorHandler onError) {
  send(
      "POST", QStringLiteral("/groups"),
      QJsonObject{{QStringLiteral("name"), name}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiGroup::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::renameGroup(int groupId, const QString &name,
                            Handler<ApiGroup> onOk, ErrorHandler onError) {
  send(
      "PATCH", QStringLiteral("/groups/%1").arg(groupId),
      QJsonObject{{QStringLiteral("name"), name}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiGroup::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::deleteGroup(int groupId, VoidHandler onOk,
                            ErrorHandler onError) {
  send(
      "DELETE", QStringLiteral("/groups/%1").arg(groupId), {}, false,
      [onOk](const QJsonDocument &) {
        if (onOk)
          onOk();
      },
      std::move(onError));
}

void ApiClient::joinGroup(const QString &shareCode, Handler<ApiGroup> onOk,
                          ErrorHandler onError) {
  send(
      "POST", QStringLiteral("/groups/join"),
      QJsonObject{{QStringLiteral("share_code"), shareCode}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiGroup::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::rotateShareCode(int groupId, Handler<ApiGroup> onOk,
                                ErrorHandler onError) {
  send(
      "POST", QStringLiteral("/groups/%1/share-code/rotate").arg(groupId), {},
      false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiGroup::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::listMembers(int groupId, Handler<QList<ApiMember>> onOk,
                            ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/groups/%1/members").arg(groupId), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiMember>(doc));
      },
      std::move(onError));
}

void ApiClient::addMember(int groupId, const QString &email,
                          Handler<ApiMember> onOk, ErrorHandler onError) {
  send(
      "POST", QStringLiteral("/groups/%1/members").arg(groupId),
      QJsonObject{{QStringLiteral("email"), email}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiMember::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::removeMember(int groupId, int userId, VoidHandler onOk,
                             ErrorHandler onError) {
  send(
      "DELETE",
      QStringLiteral("/groups/%1/members/%2").arg(groupId).arg(userId), {},
      false,
      [onOk](const QJsonDocument &) {
        if (onOk)
          onOk();
      },
      std::move(onError));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Files
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::listFiles(int groupId, Handler<QList<ApiFile>> onOk,
                          ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/groups/%1/files").arg(groupId), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiFile>(doc));
      },
      std::move(onError));
}

void ApiClient::registerFile(int groupId, const QString &contentHash,
                             const QString &fileName, qint64 fileSizeBytes,
                             int pageCount, Handler<ApiFile> onOk,
                             ErrorHandler onError) {
  QJsonObject body{
      {QStringLiteral("content_hash"), contentHash},
      {QStringLiteral("file_name"), fileName},
      {QStringLiteral("file_size_bytes"), static_cast<double>(fileSizeBytes)},
      {QStringLiteral("page_count"), pageCount},
  };
  send(
      "POST", QStringLiteral("/groups/%1/files").arg(groupId), body, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiFile::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::removeFile(int groupId, int fileId, bool deleteStoredCopy,
                           Handler<ApiFileRemoval> onOk,
                           ErrorHandler onError) {
  send(
      "DELETE",
      QStringLiteral("/groups/%1/files/%2?purge=%3")
          .arg(groupId)
          .arg(fileId)
          .arg(deleteStoredCopy ? QStringLiteral("true")
                                : QStringLiteral("false")),
      {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiFileRemoval::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tags
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::listAllTags(Handler<QList<ApiTag>> onOk, ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/tags"), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiTag>(doc));
      },
      std::move(onError));
}

void ApiClient::listGroupTags(int groupId, Handler<QList<ApiTag>> onOk,
                              ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/groups/%1/tags").arg(groupId), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiTag>(doc));
      },
      std::move(onError));
}

void ApiClient::createTag(int groupId, const QString &name,
                          Handler<ApiTag> onOk, ErrorHandler onError) {
  send(
      "POST", QStringLiteral("/groups/%1/tags").arg(groupId),
      QJsonObject{{QStringLiteral("name"), name}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiTag::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::renameTag(int groupId, int tagId, const QString &name,
                          Handler<ApiTag> onOk, ErrorHandler onError) {
  send(
      "PATCH", QStringLiteral("/groups/%1/tags/%2").arg(groupId).arg(tagId),
      QJsonObject{{QStringLiteral("name"), name}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiTag::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::deleteTag(int groupId, int tagId, VoidHandler onOk,
                          ErrorHandler onError) {
  send(
      "DELETE", QStringLiteral("/groups/%1/tags/%2").arg(groupId).arg(tagId),
      {}, false,
      [onOk](const QJsonDocument &) {
        if (onOk)
          onOk();
      },
      std::move(onError));
}

void ApiClient::listFileTags(int groupId, int fileId,
                             Handler<QList<ApiTag>> onOk,
                             ErrorHandler onError) {
  send(
      "GET",
      QStringLiteral("/groups/%1/files/%2/tags").arg(groupId).arg(fileId), {},
      false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiTag>(doc));
      },
      std::move(onError));
}

void ApiClient::addFileTag(int groupId, int fileId, const QString &name,
                           Handler<QList<ApiTag>> onOk, ErrorHandler onError) {
  send(
      "POST",
      QStringLiteral("/groups/%1/files/%2/tags").arg(groupId).arg(fileId),
      QJsonObject{{QStringLiteral("name"), name}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiTag>(doc));
      },
      std::move(onError));
}

void ApiClient::setFileTags(int groupId, int fileId, const QStringList &names,
                            Handler<QList<ApiTag>> onOk, ErrorHandler onError) {
  QJsonArray array;
  for (const QString &name : names)
    array.append(name);

  send(
      "PUT",
      QStringLiteral("/groups/%1/files/%2/tags").arg(groupId).arg(fileId),
      QJsonObject{{QStringLiteral("names"), array}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiTag>(doc));
      },
      std::move(onError));
}

void ApiClient::removeFileTag(int groupId, int fileId, int tagId,
                              VoidHandler onOk, ErrorHandler onError) {
  send(
      "DELETE",
      QStringLiteral("/groups/%1/files/%2/tags/%3")
          .arg(groupId)
          .arg(fileId)
          .arg(tagId),
      {}, false,
      [onOk](const QJsonDocument &) {
        if (onOk)
          onOk();
      },
      std::move(onError));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Notes
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::listNotes(int groupId, int fileId, Handler<QList<ApiNote>> onOk,
                          ErrorHandler onError) {
  send(
      "GET",
      QStringLiteral("/groups/%1/files/%2/notes").arg(groupId).arg(fileId), {},
      false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(listFrom<ApiNote>(doc));
      },
      std::move(onError));
}

void ApiClient::createNote(int groupId, int fileId, const QString &body,
                           Handler<ApiNote> onOk, ErrorHandler onError) {
  send(
      "POST",
      QStringLiteral("/groups/%1/files/%2/notes").arg(groupId).arg(fileId),
      QJsonObject{{QStringLiteral("body"), body}}, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiNote::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::updateNote(int noteId, const QString &body, int knownVersion,
                           Handler<ApiNote> onOk, ErrorHandler onError) {
  QJsonObject payload{{QStringLiteral("body"), body}};
  if (knownVersion > 0)
    payload.insert(QStringLiteral("version"), knownVersion);

  send(
      "PATCH", QStringLiteral("/notes/%1").arg(noteId), payload, true,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiNote::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::deleteNote(int noteId, VoidHandler onOk, ErrorHandler onError) {
  send(
      "DELETE", QStringLiteral("/notes/%1").arg(noteId), {}, false,
      [onOk](const QJsonDocument &) {
        if (onOk)
          onOk();
      },
      std::move(onError));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sync
// ─────────────────────────────────────────────────────────────────────────────

void ApiClient::syncStatus(int groupId, Handler<ApiSyncStatus> onOk,
                           ErrorHandler onError) {
  send(
      "GET", QStringLiteral("/groups/%1/sync-status").arg(groupId), {}, false,
      [onOk](const QJsonDocument &doc) {
        if (onOk)
          onOk(ApiSyncStatus::fromJson(objectFrom(doc)));
      },
      std::move(onError));
}

void ApiClient::uploadFile(int groupId, int fileId, const QString &localPath,
                           Handler<ApiUploadResult> onOk,
                           ErrorHandler onError) {
  if (!hasBaseUrl()) {
    report(onError,
           ApiError::network(QStringLiteral(
               "No server address is configured. Set one in Settings.")));
    return;
  }

  auto *file = new QFile(localPath);
  if (!file->open(QIODevice::ReadOnly)) {
    const QString reason = file->errorString();
    delete file;
    report(onError, ApiError::network(
                        QStringLiteral("Could not read %1.\n\n%2")
                            .arg(QFileInfo(localPath).fileName(), reason)));
    return;
  }

  auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
  QHttpPart part;
  part.setHeader(QNetworkRequest::ContentTypeHeader,
                 QStringLiteral("application/pdf"));
  part.setHeader(QNetworkRequest::ContentDispositionHeader,
                 QStringLiteral("form-data; name=\"content\"; filename=\"%1\"")
                     .arg(QFileInfo(localPath).fileName()));
  part.setBodyDevice(file);
  file->setParent(multiPart);
  multiPart->append(part);

  QNetworkRequest request(resolve(
      QStringLiteral("/groups/%1/files/%2/upload").arg(groupId).arg(fileId)));
  request.setRawHeader("Accept", "application/json");
  if (!m_accessToken.isEmpty())
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
  // Uploads are the one call that can legitimately run long, so they opt out
  // of the shared timeout.
  request.setTransferTimeout(0);

  QNetworkReply *reply = m_nam.post(request, multiPart);
  multiPart->setParent(reply);

  connect(
      reply, &QNetworkReply::finished, this, [this, reply, onOk, onError]() {
        reply->deleteLater();

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();

        if (status == 0) {
          report(onError,
                 ApiError::network(QStringLiteral("Upload failed.\n\n%1")
                                       .arg(reply->errorString())));
          return;
        }
        if (status >= 400) {
          report(onError,
                 ApiError::fromJson(
                     status, objectFrom(QJsonDocument::fromJson(payload))));
          return;
        }
        if (onOk) {
          onOk(ApiUploadResult::fromJson(
              objectFrom(QJsonDocument::fromJson(payload))));
        }
      });
}

void ApiClient::downloadFile(int groupId, int fileId, const QString &localPath,
                             VoidHandler onOk, ErrorHandler onError) {
  if (!hasBaseUrl()) {
    report(onError,
           ApiError::network(QStringLiteral(
               "No server address is configured. Set one in Settings.")));
    return;
  }

  dispatchDownload(
      QStringLiteral("/groups/%1/files/%2/content").arg(groupId).arg(fileId),
      localPath, std::move(onOk), std::move(onError), /*mayRetry=*/true);
}

void ApiClient::dispatchDownload(const QString &path, const QString &localPath,
                                 VoidHandler onOk, ErrorHandler onError,
                                 bool mayRetry) {
  // QSaveFile writes to a sibling temporary and renames on commit(), so a
  // failed or cancelled transfer leaves no partial PDF for the folder scanner
  // to pick up and register as a real file.
  auto *target = new QSaveFile(localPath);
  if (!target->open(QIODevice::WriteOnly)) {
    const QString reason = target->errorString();
    delete target;
    report(onError,
           ApiError::network(QStringLiteral("Could not write %1.\n\n%2")
                                 .arg(localPath, reason)));
    return;
  }

  QNetworkRequest request(resolve(path));
  request.setRawHeader("Accept", "application/pdf");
  if (!m_accessToken.isEmpty())
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
  // Like uploads, a download of a real PDF can legitimately run long.
  request.setTransferTimeout(0);

  QNetworkReply *reply = m_nam.get(request);
  target->setParent(reply);

  // An error reply is JSON, not PDF bytes, so it is collected here instead of
  // being written to the file.
  auto errorBody = std::make_shared<QByteArray>();

  connect(reply, &QNetworkReply::readyRead, this, [reply, target, errorBody]() {
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
      errorBody->append(reply->readAll());
    else
      target->write(reply->readAll());
  });

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, target, errorBody, path, localPath, onOk, onError,
       mayRetry]() {
        reply->deleteLater();

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (status == 0) {
          target->cancelWriting();
          report(onError,
                 ApiError::network(QStringLiteral("Download failed.\n\n%1")
                                       .arg(reply->errorString())));
          return;
        }

        if (status == 401 && mayRetry && !m_refreshToken.isEmpty() &&
            !m_refreshing) {
          target->cancelWriting();
          refreshSession(
              [this, path, localPath, onOk, onError]() {
                dispatchDownload(path, localPath, onOk, onError,
                                 /*mayRetry=*/false);
              },
              [this, onError](const ApiError &) {
                clearSession();
                emit sessionExpired();
                ApiError expired;
                expired.httpStatus = 401;
                expired.code = QString::fromLatin1(ApiError::Unauthorized);
                expired.message =
                    QStringLiteral("Your session has expired. Sign in again.");
                report(onError, expired);
              });
          return;
        }

        if (status >= 400) {
          target->cancelWriting();
          errorBody->append(reply->readAll());
          ApiError error = ApiError::fromJson(
              status, objectFrom(QJsonDocument::fromJson(*errorBody)));
          if (status == 401) {
            clearSession();
            emit sessionExpired();
          }
          report(onError, error);
          return;
        }

        target->write(reply->readAll());
        if (!target->commit()) {
          report(onError,
                 ApiError::network(
                     QStringLiteral("Could not save %1.").arg(localPath)));
          return;
        }
        if (onOk)
          onOk();
      });
}

QString ApiClient::hashFile(const QString &localPath) {
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly))
    return {};

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return {};
  return QString::fromLatin1(hash.result().toHex());
}
