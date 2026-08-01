#pragma once
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStackedWidget>
#include <functional>

#include "api/apitypes.h"

class QLineEdit;
class QToolBar;
class QLabel;
class QSplitter;
class QAction;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QVBoxLayout;

class PdfModel;
class TagModel;
class FolderModel;
class FolderTreeModel;
class DatabaseManager;
class FolderWatcher;
class PdfController;
class TagController;
class SearchFilterProxy;
class ApiClient;

class FolderPanel;
class ListView;
class GridView;
class RecentView;

/**
 * @brief Application shell: assembles all controllers, models, and views.
 *
 * Layout
 * ──────
 *  ┌─[MenuBar]───────────────────────────────────────────────────────────┐
 *  ├─[ToolBar: search | Group | List/Grid | Tag Mgr | Settings]──────────┤
 *  │ ┌──[FolderPanel]──┬──[StackedWidget:  ├──[RecentView]──────────┐ │
 *  │ │  Folders        │   ListView |      │  Recently Opened       │ │
 *  │ │  Tag filters    │   GridView]       │  Card-based panel      │ │
 *  │ │                 │   PDF cards/rows  │                        │ │
 *  │ └─────────────────┴───────────────────┴────────────────────────┘ │
 *  └─[StatusBar: count | scan status | signed-in user]───────────────────┘
 *
 * Local vs. shared state
 * ──────────────────────
 * Folder watching, scanning and thumbnails stay local (DatabaseManager).
 * Groups, tags, notes and file storage belong to the backend and are reached
 * only through ApiClient — this class holds no database or storage credential.
 *
 * A directory *is* a group
 * ────────────────────────
 * Every directory that directly holds a PDF becomes a group named after its
 * path below the watched root ("Papers", "Papers/2023"), and it holds exactly
 * the PDFs sitting in it — the ones a level down belong to that level's own
 * group. There is no per-file group picker: a file's group is decided by which
 * directory it sits in.
 *
 * The **active group** is therefore derived, not chosen: it is the group of the
 * selected file's own directory, and it is the permission context for
 * everything shared — which vocabulary a tag lands in and which team sees a
 * note. The toolbar shows it read-only.
 *
 * Whoever added the folder owns its groups and is the only one who may invite
 * or remove members; everyone else sees the roster and may leave.
 *
 * Joining runs that backwards
 * ───────────────────────────
 * Every shareable group carries a **share code**. Redeeming one (File ▸ Join
 * Shared Folder…) grants membership, then creates a local folder for the group,
 * downloads its PDFs into it, and starts watching it — arriving at exactly the
 * state the creator's own machine is in, from the opposite direction.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ── View switching ────────────────────────────────────────────────────────
    void switchToListView();
    void switchToGridView();

    // ── Folder management ─────────────────────────────────────────────────────
    void onAddFolderRequested();
    void onRemoveFolderRequested(const QString& path);

    /// A folder was clicked in the sidebar. Besides filtering the file list,
    /// this *selects that folder's group* — which is what makes a group with no
    /// files left on this machine reachable again.
    void onFolderSelected  (const QString& folderPath);

    // ── PDF events ────────────────────────────────────────────────────────────
    void onFileActivated   (const QString& filePath);
    void onEditTagsRequested(const QString& filePath);
    /// Take a file out of its group, and — only if asked, and only for the
    /// owner — out of cloud storage too. This is the *only* path that destroys
    /// a shared copy; a PDF that merely disappeared from disk is re-downloaded
    /// by the next sync instead.
    void onRemoveFileRequested(const QString& filePath);
    void onPdfOpened       (const QString& filePath, const QDateTime& when);
    void onFileSelected    (const QString& filePath);

    // ── Session ───────────────────────────────────────────────────────────────
    void promptSignIn();
    void onSignOut();
    void onApiError(const ApiError& error);
    void onSessionExpired();
    /// Someone else changed something shared. Refreshes only the parts of the
    /// UI the event actually touches, then restates the sync indicators.
    void onRemoteEvent(const ApiRemoteEvent& event);

    // ── Groups ────────────────────────────────────────────────────────────────
    void onRenameGroup();
    void onInviteMember();
    void onLeaveGroup();
    void onSyncGroup();

    // ── Sharing a group by code ───────────────────────────────────────────────
    /// Put the active group's share code on the clipboard, ready to send.
    void onCopyShareCode();
    /// Owner-only. Issues a new code so the old one stops working.
    void onRotateShareCode();
    /// Redeem someone else's code, then mirror that group into a local folder.
    void onJoinSharedFolder();

    // ── Notes ─────────────────────────────────────────────────────────────────
    void onAddNote();

    // ── Search ────────────────────────────────────────────────────────────────
    void onSearchTextChanged(const QString& text);

    // ── Misc ──────────────────────────────────────────────────────────────────
    void openTagManager();
    void openSettings();
    void updateStatusBar();
    void applyDarkTheme(bool enabled);
    void onScanFinished(const QString& folder);

private:
    void initModels();
    void initControllers();
    void initViews();
    void initToolBar();
    void initMenuBar();
    void initStatusBar();
    void connectSignals();

    QWidget* buildDetailPane();
    void refreshDetailPane();
    void refreshGroupHeader();
    void refreshMembers();
    void refreshNotes();
    void setCollaborationEnabled(bool enabled);

    /// One member row; the "remove" affordance is drawn only for the owner,
    /// and never against themselves.
    QWidget* buildMemberRow(const ApiMember& member, const ApiGroup& group);
    void removeMember(const ApiMember& member);

    /// One note, with Edit/Delete shown only when the signed-in user wrote it.
    QWidget* buildNoteBubble(const ApiNote& note);
    /// A note written while offline, still sitting in the local queue. It has no
    /// id yet, so it cannot be edited or deleted through the API — it is shown
    /// so that typing a note never looks like it did nothing.
    QWidget* buildQueuedNoteBubble(const QString& body);
    void editNote  (const ApiNote& note);
    void deleteNote(const ApiNote& note);

    // ── Background activity ───────────────────────────────────────────────────
    /// Say what is happening, without a modal and without disabling anything.
    /// @p autoClearMs of 0 leaves the message until something replaces it.
    void showActivity(const QString& message, int autoClearMs = 0);
    void clearActivity();

    // ── Transfers running in the background ───────────────────────────────────
    //
    //  Moving files used to hold a modal progress dialog over the window until
    //  it finished, which made a folder of large PDFs into a decision to stop
    //  working. A transfer now reports itself in the status bar and nothing
    //  else: the user keeps browsing, tagging, reading and searching while it
    //  runs, and may stop it whenever they like.
    //
    //  Exactly one runs at a time. That is not a limitation of the plumbing but
    //  the point of it — one progress bar can only honestly describe one thing,
    //  and a second Sync started on top of the first would be doing the work
    //  the first is already doing.

    /// One upload or download run, in progress.
    struct Transfer
    {
        bool    active = false;
        int     groupId = -1;
        QString groupName;
        QString phase;      ///< "Uploading" / "Downloading"
        QString detail;     ///< The file currently in flight.
        int     done = 0;
        int     total = 0;
        /// Files already finished by an earlier phase. A sync uploads and then
        /// downloads over one bar, and the download half counts from zero — so
        /// the bar adds this to keep going forwards rather than starting again.
        int     base = 0;
        bool    canceled = false;
    };

    /// Put the status-bar transfer row up. Refuses — and says why — when one is
    /// already running, so a double-clicked Sync cannot start a second.
    bool beginTransfer(int groupId, const QString& groupName,
                       const QString& phase, int total);
    /// Move it on one file. @p phase may change mid-run: a sync uploads first
    /// and downloads afterwards, and both halves share one bar.
    void stepTransfer(const QString& phase, const QString& detail, int done);
    void endTransfer();
    void updateTransferRow();
    /// True once the user has pressed Stop. The transfer chains check this
    /// between files, exactly as they used to check the progress dialog.
    [[nodiscard]] bool transferCanceled() const { return m_transfer.canceled; }
    [[nodiscard]] bool transferActive()   const { return m_transfer.active; }

    // ── Session ───────────────────────────────────────────────────────────────
    void restoreSessionOrPrompt();
    void onSignedIn();
    void saveSession();
    void clearSavedSession();

    // ── Backend helpers ───────────────────────────────────────────────────────
    void reloadGroups(std::function<void()> onDone = {});
    void reloadTagVocabulary();

    // ── Folder-derived groups ─────────────────────────────────────────────────

    /// The watched root @p path sits under (the longest match, so a root nested
    /// inside another still wins), or an empty string when nothing watches it.
    [[nodiscard]] QString rootFolderFor(const QString& path) const;

    /// The directory whose group owns @p filePath — the directory the file is
    /// actually in, not the watched root above it.
    [[nodiscard]] QString groupFolderFor(const QString& filePath) const;

    /// Every directory currently holding at least one scanned PDF directly.
    [[nodiscard]] QStringList foldersHoldingPdfs() const;

    /// PDFs sitting directly in @p folderPath, excluding its subdirectories.
    [[nodiscard]] QStringList filesIn(const QString& folderPath) const;

    /// Name to give @p folderPath's group: its path below the watched root,
    /// e.g. "Papers" for the root itself and "Papers/2023" for a subfolder.
    [[nodiscard]] QString groupNameForFolder(const QString& folderPath) const;

    /// Backend group id for a directory, or -1 when it has none yet.
    [[nodiscard]] int groupIdForFolder(const QString& folderPath) const;

    /// Give every directory holding a PDF a group, adopting an owned group of
    /// the same name before creating a new one, then register the files in it.
    /// Runs after sign-in; @p rootPath limits it to one watched tree.
    void reconcileFolderGroups();
    void reconcileFoldersUnder(const QString& rootPath);

    /// Ensure @p folderPath has a group, then register the PDFs directly in it
    /// that the backend has not seen yet.
    void syncFolderGroup(const QString& folderPath);
    void trackFilesIn(int groupId, const QString& folderPath);
    void registerNext(const QString& folderPath, int groupId,
                      QStringList pending);

    // ── How far out of sync a group is ────────────────────────────────────────

    /// What syncing the active group would do right now, in both directions.
    ///
    /// The server cannot answer this alone: it knows which files it stores, and
    /// only this machine knows which of them are actually on disk here. So the
    /// two halves are matched by content hash — the same identity the backend
    /// keys files by — and a file this machine no longer holds is a download,
    /// exactly like a file nobody has uploaded yet is an upload.
    ///
    /// Only *files* appear here. Tags and notes are deliberately absent: they
    /// are sent the moment they are written and, when that is impossible, they
    /// wait quietly until the file they belong to goes up. Neither case is
    /// something to ask the user to press a button about.
    struct SyncPlan
    {
        QList<ApiFile> toUpload;    ///< Held here; the group has no copy stored.
        QList<ApiFile> toDownload;  ///< Stored for the group; missing here.
        /// PDFs sitting in the folder that the group has never been told about.
        /// They become uploads the moment they are registered, so they count
        /// towards the "ahead" number from the start.
        int unregistered = 0;

        [[nodiscard]] int uploads()   const { return toUpload.size() + unregistered; }
        [[nodiscard]] int downloads() const { return toDownload.size(); }
    };

    /// Match @p status against what is on disk for @p groupId. Not const: it
    /// hashes local files, and hashing caches.
    [[nodiscard]] SyncPlan planSync(int groupId, const ApiSyncStatus& status);

    /// What one group's counts came out as, kept per group rather than only for
    /// the selected one — the sidebar badges and the status-bar banner have to
    /// speak for folders nobody has clicked on.
    struct SyncCounts
    {
        int  uploads      = 0;
        int  downloads    = 0;
        bool known        = false;  ///< False until the first reply lands.

        [[nodiscard]] bool behind()   const { return downloads > 0; }
        [[nodiscard]] bool ahead()    const { return uploads > 0; }
        [[nodiscard]] bool inSync()   const { return !behind() && !ahead(); }
        /// "↑2 ↓1", or empty when there is nothing to say.
        [[nodiscard]] QString badge() const;
        [[nodiscard]] QString describe() const;
    };

    /// Ask the server for the active group's files and restate the Sync button.
    /// Cheap to call: it only makes a request when the active group changed or
    /// @p force says something happened that the counts cannot predict.
    void refreshSyncCounts(bool force = false);
    /// The same question for every group this machine has a folder for, which
    /// is what the sidebar badges and the status-bar banner are drawn from.
    /// Only groups marked stale are actually asked about.
    void refreshAllSyncCounts(bool force = false);
    /// Ask about one group and fold the answer into m_syncCounts.
    void refreshSyncCountsFor(int groupId, bool force);
    /// Redraw the Sync button from the last counts we were given.
    void updateSyncButton();
    /// Redraw everything that reports how far out of sync anything is: the
    /// sidebar badges, the status-bar banner and the window title.
    void updateSyncIndicators();
    /// A file appeared or disappeared in @p groupId's folder, so the counts are
    /// no longer a fact — recount. Only files reach here: a tag or a note never
    /// makes a folder out of sync.
    void markSyncPending(int groupId);
    /// Counts for @p groupId, or an all-zero unknown entry.
    [[nodiscard]] SyncCounts countsFor(int groupId) const;
    /// The group the status-bar banner is currently offering to sync, or -1.
    [[nodiscard]] int mostOutOfSyncGroup() const;

    /// Restate every file's dot in @p groupId's folder from what the server just
    /// said it holds. The counts answer "how far behind is this folder"; this
    /// answers "which of these PDFs is the folder behind on", which is the
    /// question someone looking at a file list is actually asking.
    void applyFileSyncStates(int groupId, const ApiSyncStatus& status);
    /// The same for one file, without a round trip — used while a transfer is
    /// moving it and the server has not been asked again yet.
    void markFileSyncState(const QString& filePath, int state,
                           const QString& detail);
    /// Local ids of files in @p groupId's folder that have tags or notes waiting.
    [[nodiscard]] QSet<int> filesWithPendingMetadata(const QString& folderPath) const;
    /// Restate the small "unsent edits" dot for every file in @p folderPath.
    void refreshPendingMetaMarks(const QString& folderPath);

    void syncPendingData(int groupId, std::function<void()> onDone);
    void syncNextPendingFile(int groupId, QStringList pending, std::function<void()> onDone);
    void syncNextPendingTag(int groupId, QList<int> pendingFiles, std::function<void()> onDone);
    void syncNextPendingNote(int groupId, QList<int> pendingFiles, std::function<void()> onDone);
    void syncNotesForFile(int groupId, int remoteFileId, int localFileId, QStringList notes, std::function<void()> onDone);

    // ── Tags and notes, which are not a sync ──────────────────────────────────
    //
    //  Writing a tag or a note is an edit, not a transfer. It goes up on its own
    //  the moment it is made, and the Sync button never lights up because of
    //  one. When it cannot go up — signed out, or the group has never been told
    //  about the file — it waits on this machine and rides along with the file
    //  itself, which is the only thing a sync was ever really about.

    /// Send @p tags for @p filePath to its group now, in the background.
    void pushFileTags(int groupId, const QString& filePath, int localFileId,
                      const QStringList& tags);
    /// Backend file id for @p filePath in @p groupId, or -1 when the group has
    /// not been told about it. Unlike resolveRemoteFile() this never registers
    /// anything: asking whether a file is known must not be what makes it known.
    [[nodiscard]] int knownRemoteFileId(int groupId, const QString& filePath);
    /// The local path @p remoteFileId maps to inside @p groupId, or empty.
    [[nodiscard]] QString localPathForRemoteFile(int groupId, int remoteFileId);
    /// Re-fetch one file's tags after a teammate changed them.
    void refreshRemoteFileTags(int groupId, int remoteFileId);

    /// SHA-256 for a local file, cached in SQLite so a file is hashed once.
    QString contentHashFor(const QString& filePath);

    /// Resolve a local path to its backend file id inside @p groupId,
    /// registering it if this is the first time. On failure @p onFailed runs
    /// instead; omit it and the failure is shown to the user as a modal.
    void resolveRemoteFile(int groupId, const QString& filePath,
                           std::function<void(int)> onReady,
                           std::function<void(const ApiError&)> onFailed = {});

    /// The directory whose group is currently in context: the selected file's,
    /// or — when no file is selected, or its folder has no group — the one
    /// clicked in the sidebar.
    [[nodiscard]] QString  activeFolderPath() const;

    /// The group of the active folder — the only place a tag or note this
    /// client writes can land.
    [[nodiscard]] int      activeGroupId() const;
    [[nodiscard]] ApiGroup activeGroup()   const;
    [[nodiscard]] ApiGroup groupById(int groupId) const;
    [[nodiscard]] ApiGroup groupByName(const QString& name) const;

    void showError(const ApiError& error);

    /// Carry out @p plan: uploads first, then downloads, then one summary.
    void runSync(int groupId, const ApiGroup& group, const SyncPlan& plan);
    /// Mark @p groupId's sync as finished, whichever way it ended.
    void finishSync(int groupId);

    /// Send each pending file's bytes, then hand the totals to @p onDone —
    /// which is where the download half of a sync picks up. Progress goes to
    /// the status-bar transfer row; the caller must have opened it already.
    void uploadNext(int groupId, QList<ApiFile> pending, int uploaded,
                    int skipped,
                    std::function<void(int uploaded, int skipped,
                                       bool canceled)> onDone);

    // ── Joining a shared group ────────────────────────────────────────────────

    /// Ask where @p group should live locally, prepare the folder, and pull its
    /// files down. Called once the share code has already been redeemed.
    void setUpJoinedFolder(const ApiGroup& group);

    /// Make @p folderPath ready to receive a joined group's files, asking about
    /// anything already in it. False means the user backed out or it failed.
    bool prepareJoinTarget(const QString& folderPath, const ApiGroup& group);

    /// Attach @p folderPath to @p group and pull its files down. The mapping is
    /// stored first, so the scan that watching later triggers adopts the joined
    /// group instead of creating a second one named after the new folder.
    void adoptJoinedFolder(const QString& folderPath, const ApiGroup& group);

    /// Start watching a folder whose download has finished, which is what makes
    /// its PDFs visible in the rest of the UI.
    void watchJoinedFolder(const QString& folderPath);

    /// Download the group's files into @p folderPath, one at a time, then hand
    /// the totals to @p onDone. Both callers — joining a shared folder and the
    /// download half of a sync — want the same transfer but a different ending,
    /// so the ending is theirs to write.
    void downloadNext(int groupId, const QString& folderPath,
                      QList<ApiFile> pending, QStringList taken, int downloaded,
                      int skipped,
                      std::function<void(int downloaded, int skipped,
                                         bool canceled)> onDone);

    /// A local file name for @p file that is safe to create inside
    /// @p folderPath: the display name stripped of any directory part, made
    /// unique against @p taken. Names come from another member's machine, so
    /// they are treated as untrusted input.
    [[nodiscard]] static QString localNameFor(const ApiFile& file,
                                              const QStringList& taken);

    void restoreLayout();
    void saveLayout();
    QString dataDir() const;
    QString dbPath() const;

    // ── Models ────────────────────────────────────────────────────────────────
    PdfModel*        m_pdfModel      = nullptr;
    TagModel*        m_tagModel      = nullptr;
    FolderModel*     m_folderModel   = nullptr;
    FolderTreeModel* m_folderTreeModel = nullptr;
    SearchFilterProxy* m_proxy       = nullptr;

    // ── Infrastructure ────────────────────────────────────────────────────────
    DatabaseManager* m_db          = nullptr;
    FolderWatcher*   m_watcher     = nullptr;
    PdfController*   m_pdfCtrl     = nullptr;
    TagController*   m_tagCtrl     = nullptr;
    ApiClient*       m_api         = nullptr;

    // ── Views ─────────────────────────────────────────────────────────────────
    QSplitter*       m_splitter    = nullptr;
    FolderPanel*     m_folderPanel = nullptr;
    QStackedWidget*  m_viewStack   = nullptr;
    ListView*        m_listView    = nullptr;
    GridView*        m_gridView    = nullptr;
    QTabWidget*      m_rightTabs   = nullptr;
    RecentView*      m_recentView  = nullptr;
    QLabel*          m_detailTitle = nullptr;
    QLabel*          m_detailMeta  = nullptr;
    QLabel*          m_groupHeader = nullptr;
    QLabel*          m_groupMeta   = nullptr;
    QLabel*          m_membersTitle = nullptr;
    QVBoxLayout*     m_membersLayout = nullptr;
    QLineEdit*       m_inviteEdit  = nullptr;
    QPushButton*     m_inviteBtn   = nullptr;
    QTextEdit*       m_noteEdit    = nullptr;
    QVBoxLayout*     m_notesLayout = nullptr;
    QPushButton*     m_addNoteBtn  = nullptr;
    QPushButton*     m_renameGroupBtn = nullptr;
    QPushButton*     m_leaveGroupBtn  = nullptr;
    QPushButton*     m_syncBtn     = nullptr;
    QPushButton*     m_shareBtn    = nullptr;
    QLabel*          m_shareCodeLabel = nullptr;
    QString          m_selectedFilePath;
    /// The folder clicked in the sidebar. It decides the active group whenever
    /// the selected file does not — including when there is no file left to
    /// select, which is the whole point of tracking it.
    QString          m_selectedFolderPath;

    // ── Toolbar widgets ───────────────────────────────────────────────────────
    QLineEdit*       m_searchEdit  = nullptr;
    QLabel*          m_groupLabel  = nullptr;
    QAction*         m_listAction  = nullptr;
    QAction*         m_gridAction  = nullptr;
    QAction*         m_signInAction  = nullptr;
    QAction*         m_signOutAction = nullptr;
    QAction*         m_joinFolderAction = nullptr;
    QAction*         m_rotateCodeAction = nullptr;

    // ── Status bar ────────────────────────────────────────────────────────────
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_scanLabel   = nullptr;
    QLabel*          m_userLabel   = nullptr;
    /// "↓3 to download in Papers" — clickable, and hidden when everything is up
    /// to date. Non-modal on purpose: being out of sync is a fact to report,
    /// not a decision to demand.
    QPushButton*     m_syncBanner  = nullptr;
    /// Work happening in the background right now ("Removing report.pdf…").
    /// Separate from m_scanLabel so a scan finishing cannot wipe it.
    QLabel*          m_activityLabel = nullptr;

    // ── The transfer row ──────────────────────────────────────────────────────
    /// "↑ Uploading notes.pdf — 3 of 8" plus a bar and a Stop button, hidden
    /// unless something is actually moving. This is the whole of the UI a
    /// transfer gets: nothing is greyed out and no window is blocked.
    QLabel*          m_transferLabel  = nullptr;
    QProgressBar*    m_transferBar    = nullptr;
    QPushButton*     m_transferStop   = nullptr;

    // ── Backend state ─────────────────────────────────────────────────────────
    QList<ApiGroup>  m_groups;
    QList<ApiNote>   m_notes;          ///< Notes for the selected file
    QList<ApiMember> m_members;        ///< Roster of the active group
    /// Directories whose files are being registered right now, so overlapping
    /// scans do not start a second pass over the same folder.
    QSet<QString>    m_foldersTracking;
    /// Directories whose group is being created right now, so an overlapping
    /// scan cannot create a second group under the same name.
    QSet<QString>    m_foldersCreatingGroup;
    bool             m_signingIn = false;

    // ── Sync state ────────────────────────────────────────────────────────────
    /// Group id → how far that group is out of sync. Every group with a local
    /// folder has an entry, so the sidebar can badge a folder the user has not
    /// selected and the banner can name the one that needs attention most.
    QHash<int, SyncCounts> m_syncCounts;
    /// Groups whose counts are known to be stale and worth re-asking about.
    QSet<int>        m_syncStale;
    /// Groups with a request already out, so a burst of events (a scan, or a
    /// busy group) does not queue one round trip per event.
    QSet<int>        m_syncInFlight;
    /// The group being synced right now, or -1. Pressing Sync again — on the
    /// button, on the status-bar banner, or on both in quick succession — says
    /// so instead of starting the same work a second time.
    int              m_syncingGroup = -1;
    /// The transfer the status bar is currently describing.
    Transfer         m_transfer;

    // ── Removals in flight ────────────────────────────────────────────────────
    /// Local paths whose removal request has not come back yet. Nothing is
    /// blocked while one is outstanding; this only stops the same file being
    /// submitted twice and lets the status bar say what is happening.
    QSet<QString>    m_removingFiles;
};
