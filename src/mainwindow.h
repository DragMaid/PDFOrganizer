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

    // ── PDF events ────────────────────────────────────────────────────────────
    void onFileActivated   (const QString& filePath);
    void onEditTagsRequested(const QString& filePath);
    void onPdfOpened       (const QString& filePath, const QDateTime& when);
    void onFileSelected    (const QString& filePath);

    // ── Session ───────────────────────────────────────────────────────────────
    void promptSignIn();
    void onSignOut();
    void onApiError(const ApiError& error);
    void onSessionExpired();

    // ── Groups ────────────────────────────────────────────────────────────────
    void onRenameGroup();
    void onInviteMember();
    void onLeaveGroup();
    void onSyncGroup();

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
    void editNote  (const ApiNote& note);
    void deleteNote(const ApiNote& note);

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

    /// SHA-256 for a local file, cached in SQLite so a file is hashed once.
    QString contentHashFor(const QString& filePath);

    /// Resolve a local path to its backend file id inside @p groupId,
    /// registering it if this is the first time. On failure @p onFailed runs
    /// instead; omit it and the failure is shown to the user as a modal.
    void resolveRemoteFile(int groupId, const QString& filePath,
                           std::function<void(int)> onReady,
                           std::function<void(const ApiError&)> onFailed = {});

    /// The group of the selected file's root folder — the only place a tag or
    /// note this client writes can land.
    [[nodiscard]] int      activeGroupId() const;
    [[nodiscard]] ApiGroup activeGroup()   const;
    [[nodiscard]] ApiGroup groupById(int groupId) const;
    [[nodiscard]] ApiGroup groupByName(const QString& name) const;

    void showError(const ApiError& error);
    void uploadNext(int groupId, QList<ApiFile> pending, int uploaded,
                    int skipped, class QProgressDialog* progress);

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
    QString          m_selectedFilePath;

    // ── Toolbar widgets ───────────────────────────────────────────────────────
    QLineEdit*       m_searchEdit  = nullptr;
    QLabel*          m_groupLabel  = nullptr;
    QAction*         m_listAction  = nullptr;
    QAction*         m_gridAction  = nullptr;
    QAction*         m_signInAction  = nullptr;
    QAction*         m_signOutAction = nullptr;

    // ── Status bar ────────────────────────────────────────────────────────────
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_scanLabel   = nullptr;
    QLabel*          m_userLabel   = nullptr;

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
};
