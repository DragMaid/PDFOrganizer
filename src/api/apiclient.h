#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <functional>

#include "apitypes.h"

class QNetworkReply;

/**
 * @brief Every network call the application makes.
 *
 * All calls are asynchronous: they return immediately and invoke the success
 * handler on the UI thread when the reply arrives. Nothing here blocks the
 * event loop.
 *
 * Error handling
 * ──────────────
 * Each call takes an optional error handler. **Omit it and the failure is
 * emitted as errorOccurred()**, which MainWindow turns into a modal — so the
 * default for any unhandled problem is that the user sees it. Pass one only
 * when a specific failure needs specific treatment (a stale note, say, or a
 * login form that shows the message inline).
 *
 * Sessions
 * ────────
 * A short-lived access token is sent as a bearer header. When the server
 * rejects it as expired, the client transparently spends the refresh token and
 * replays the original request once. If that also fails it emits
 * sessionExpired() and clears the session, so the caller never has to think
 * about token lifetimes.
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    template <typename T>
    using Handler = std::function<void(const T&)>;
    using VoidHandler  = std::function<void()>;
    using ErrorHandler = std::function<void(const ApiError&)>;

    explicit ApiClient(QObject* parent = nullptr);
    ~ApiClient() override;

    // ── Configuration ─────────────────────────────────────────────────────────
    void setBaseUrl(const QUrl& baseUrl);
    [[nodiscard]] QUrl baseUrl() const { return m_baseUrl; }
    [[nodiscard]] bool hasBaseUrl() const { return !m_baseUrl.isEmpty(); }

    // ── Session ───────────────────────────────────────────────────────────────
    [[nodiscard]] bool    isAuthenticated() const { return !m_accessToken.isEmpty(); }
    [[nodiscard]] ApiUser currentUser()     const { return m_user; }
    [[nodiscard]] QString refreshToken()    const { return m_refreshToken; }

    /// Restore a session persisted from a previous run. Does not validate it;
    /// the first call that fails with 401 will refresh or sign the user out.
    void restoreSession(const QString& refreshToken, const ApiUser& user);
    void clearSession();

    // ── Auth ──────────────────────────────────────────────────────────────────
    void health         (Handler<ApiServerInfo> onOk, ErrorHandler onError = {});
    void registerAccount(const QString& email, const QString& password,
                         const QString& displayName,
                         Handler<ApiUser> onOk, ErrorHandler onError = {});
    void login          (const QString& email, const QString& password,
                         Handler<ApiUser> onOk, ErrorHandler onError = {});
    /// Exchange the stored refresh token for a fresh pair.
    void refreshSession (VoidHandler onOk, ErrorHandler onError = {});

    // ── Groups ────────────────────────────────────────────────────────────────
    void listGroups   (Handler<QList<ApiGroup>> onOk, ErrorHandler onError = {});
    void createGroup  (const QString& name, Handler<ApiGroup> onOk,
                       ErrorHandler onError = {});
    void renameGroup  (int groupId, const QString& name, Handler<ApiGroup> onOk,
                       ErrorHandler onError = {});
    void deleteGroup  (int groupId, VoidHandler onOk, ErrorHandler onError = {});

    /// Redeem a share code and become a member of the group behind it. The code
    /// alone is the authorization — no invitation is needed — so a successful
    /// call means the caller now has access. Accepts the code in any casing or
    /// hyphenation; the server canonicalises it.
    ///
    /// Idempotent: redeeming a code for a group you already belong to succeeds
    /// and returns the group unchanged, which is what makes re-syncing a folder
    /// you deleted locally work.
    void joinGroup    (const QString& shareCode, Handler<ApiGroup> onOk,
                       ErrorHandler onError = {});
    /// Owner-only. Invalidates the old code; existing members keep their access.
    void rotateShareCode(int groupId, Handler<ApiGroup> onOk,
                         ErrorHandler onError = {});

    void listMembers  (int groupId, Handler<QList<ApiMember>> onOk,
                       ErrorHandler onError = {});
    void addMember    (int groupId, const QString& email, Handler<ApiMember> onOk,
                       ErrorHandler onError = {});
    void removeMember (int groupId, int userId, VoidHandler onOk,
                       ErrorHandler onError = {});

    // ── Files ─────────────────────────────────────────────────────────────────
    void listFiles    (int groupId, Handler<QList<ApiFile>> onOk,
                       ErrorHandler onError = {});
    /// Idempotent: re-registering the same content hash returns the existing
    /// record instead of failing, so two members may add the same PDF freely.
    void registerFile (int groupId, const QString& contentHash,
                       const QString& fileName, qint64 fileSizeBytes,
                       int pageCount, Handler<ApiFile> onOk,
                       ErrorHandler onError = {});
    void removeFile   (int groupId, int fileId, VoidHandler onOk,
                       ErrorHandler onError = {});

    // ── Tags ──────────────────────────────────────────────────────────────────
    void listAllTags   (Handler<QList<ApiTag>> onOk, ErrorHandler onError = {});
    void listGroupTags (int groupId, Handler<QList<ApiTag>> onOk,
                        ErrorHandler onError = {});
    void createTag     (int groupId, const QString& name, Handler<ApiTag> onOk,
                        ErrorHandler onError = {});
    void renameTag     (int groupId, int tagId, const QString& name,
                        Handler<ApiTag> onOk, ErrorHandler onError = {});
    void deleteTag     (int groupId, int tagId, VoidHandler onOk,
                        ErrorHandler onError = {});

    void listFileTags  (int groupId, int fileId, Handler<QList<ApiTag>> onOk,
                        ErrorHandler onError = {});
    /// Adding a tag another member already added succeeds and changes nothing.
    void addFileTag    (int groupId, int fileId, const QString& name,
                        Handler<QList<ApiTag>> onOk, ErrorHandler onError = {});
    void setFileTags   (int groupId, int fileId, const QStringList& names,
                        Handler<QList<ApiTag>> onOk, ErrorHandler onError = {});
    void removeFileTag (int groupId, int fileId, int tagId, VoidHandler onOk,
                        ErrorHandler onError = {});

    // ── Notes ─────────────────────────────────────────────────────────────────
    void listNotes  (int groupId, int fileId, Handler<QList<ApiNote>> onOk,
                     ErrorHandler onError = {});
    void createNote (int groupId, int fileId, const QString& body,
                     Handler<ApiNote> onOk, ErrorHandler onError = {});
    /// `knownVersion` is the version the UI last displayed. If the note moved
    /// on since, the call fails with ApiError::StaleNote rather than
    /// overwriting the newer text.
    void updateNote (int noteId, const QString& body, int knownVersion,
                     Handler<ApiNote> onOk, ErrorHandler onError = {});
    void deleteNote (int noteId, VoidHandler onOk, ErrorHandler onError = {});

    // ── Sync ──────────────────────────────────────────────────────────────────
    void syncStatus (int groupId, Handler<ApiSyncStatus> onOk,
                     ErrorHandler onError = {});
    /// Streams `localPath` to the backend, which stores it in Backblaze. The
    /// client never sees a B2 credential.
    void uploadFile (int groupId, int fileId, const QString& localPath,
                     Handler<ApiUploadResult> onOk, ErrorHandler onError = {});
    /// The reverse of uploadFile: fetch a group file's bytes and write them to
    /// `localPath`. Nothing is left behind on failure — the bytes land in a
    /// temporary file and are moved into place only once the whole transfer has
    /// arrived, so a half-written PDF never appears on disk.
    ///
    /// Fails with ApiError::NotUploaded when the file is registered in the group
    /// but nobody has synced its contents yet.
    void downloadFile(int groupId, int fileId, const QString& localPath,
                      VoidHandler onOk, ErrorHandler onError = {});

    /// SHA-256 of a file's contents — the identity the backend keys files by.
    /// Returns an empty string if the file cannot be read.
    static QString hashFile(const QString& localPath);

signals:
    /// Emitted for any failure whose caller did not supply an error handler.
    void errorOccurred(const ApiError& error);
    /// The refresh token is gone or rejected; the user must sign in again.
    void sessionExpired();
    void authenticatedChanged(bool authenticated);

private:
    using RawHandler = std::function<void(const QJsonDocument&)>;

    struct PendingRequest;

    void send(const QByteArray& method, const QString& path,
              const QJsonObject& body, bool hasBody,
              RawHandler onOk, ErrorHandler onError, bool mayRetry = true);
    void dispatch(const PendingRequest& request);
    /// Binary GET straight to disk. Separate from dispatch() because the reply
    /// is streamed to a file rather than parsed as JSON, but it carries the same
    /// refresh-once-on-401 behaviour: a folder of PDFs can take longer to fetch
    /// than an access token lives.
    void dispatchDownload(const QString& path, const QString& localPath,
                          VoidHandler onOk, ErrorHandler onError, bool mayRetry);
    void finish(QNetworkReply* reply, const PendingRequest& request);
    void retryAfterRefresh(const PendingRequest& request);

    void applySession(const QJsonObject& tokenPair);
    void report(const ErrorHandler& onError, const ApiError& error);

    [[nodiscard]] QUrl resolve(const QString& path) const;

    QNetworkAccessManager m_nam;
    QUrl                  m_baseUrl;
    QString               m_accessToken;
    QString               m_refreshToken;
    ApiUser               m_user;
    bool                  m_refreshing = false;
};
